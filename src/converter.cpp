#include "converter.h"

#include "utf8.h"

#include <cstdio>
#include <csetjmp>
#include <jpeglib.h>
#include <libheif/heif.h>
#include <png.h>
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <system_error>
#include <vector>

namespace {

using HeifContextPtr = std::unique_ptr<heif_context, decltype(&heif_context_free)>;
using HeifHandlePtr = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>;
using HeifImagePtr = std::unique_ptr<heif_image, decltype(&heif_image_release)>;

std::string ErrorText(const heif_error& error) {
    std::ostringstream stream;
    stream << "libheif error " << static_cast<int>(error.code) << "/"
           << static_cast<int>(error.subcode);
    if (error.message != nullptr && error.message[0] != '\0') {
        stream << ": " << error.message;
    }
    return stream.str();
}

bool IsHeicFile(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(towlower(character));
    });
    return extension == L".heic";
}

std::vector<std::filesystem::path> FindHeicFiles(
    const std::filesystem::path& folder,
    bool recursive,
    std::vector<std::string>& warnings) {
    std::vector<std::filesystem::path> files;
    const auto options = std::filesystem::directory_options::skip_permission_denied;

    const auto inspect = [&files](const std::filesystem::directory_entry& entry) {
        std::error_code error;
        if (entry.is_regular_file(error) && !error && IsHeicFile(entry.path())) {
            files.push_back(entry.path());
        }
    };

    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, options)) {
                inspect(entry);
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(folder, options)) {
                inspect(entry);
            }
        }
    } catch (const std::filesystem::filesystem_error& error) {
        warnings.emplace_back(std::string("扫描警告: ") + error.what());
    }

    std::sort(files.begin(), files.end());
    return files;
}

#if defined(_MSC_VER)
#pragma warning(push)
// libpng's documented error handling API is based on setjmp/longjmp.
#pragma warning(disable : 4611)
#endif
bool WritePng(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    std::string& errorMessage) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, output.c_str(), L"wb") != 0 || file == nullptr) {
        errorMessage = "无法创建临时 PNG 文件";
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        fclose(file);
        errorMessage = "libpng 初始化失败";
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        fclose(file);
        errorMessage = "libpng 信息结构初始化失败";
        return false;
    }

    std::vector<png_bytep> rows(static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        rows[static_cast<size_t>(row)] = const_cast<png_bytep>(pixels + static_cast<size_t>(row) * stride);
    }

    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_write_struct(&png, &info);
        fclose(file);
        errorMessage = "libpng 写入失败";
        return false;
    }

    png_init_io(png, file);
    png_set_compression_level(png, 6);
    png_set_IHDR(
        png,
        info,
        static_cast<png_uint_32>(width),
        static_cast<png_uint_32>(height),
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);
    png_set_rows(png, info, rows.data());
    png_write_png(png, info, PNG_TRANSFORM_IDENTITY, nullptr);
    png_destroy_write_struct(&png, &info);

    if (fclose(file) != 0) {
        errorMessage = "刷新 PNG 文件到磁盘时失败";
        return false;
    }
    return true;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(_MSC_VER)
#pragma warning(push)
// jmp_buf has an explicit alignment requirement; the padding is intentional.
#pragma warning(disable : 4324)
#endif
struct JpegErrorManager {
    jpeg_error_mgr base{};
    jmp_buf jumpBuffer{};
    char message[JMSG_LENGTH_MAX]{};
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

void JpegErrorExit(j_common_ptr info) {
    auto* manager = reinterpret_cast<JpegErrorManager*>(info->err);
    (*info->err->format_message)(info, manager->message);
    longjmp(manager->jumpBuffer, 1);
}

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4611)
#endif
bool WriteJpeg(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    int quality,
    std::string& errorMessage) {
    if (static_cast<size_t>(width) > SIZE_MAX / 3U) {
        errorMessage = "图像宽度过大";
        return false;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, output.c_str(), L"wb") != 0 || file == nullptr) {
        errorMessage = "无法创建临时 JPG 文件";
        return false;
    }

    auto* row = static_cast<JSAMPLE*>(malloc(static_cast<size_t>(width) * 3U));
    if (row == nullptr) {
        fclose(file);
        errorMessage = "JPG 行缓冲区分配失败";
        return false;
    }

    jpeg_compress_struct compressor{};
    JpegErrorManager errorManager{};
    compressor.err = jpeg_std_error(&errorManager.base);
    errorManager.base.error_exit = JpegErrorExit;
    volatile bool compressorCreated = false;

    if (setjmp(errorManager.jumpBuffer) != 0) {
        if (compressorCreated) {
            jpeg_destroy_compress(&compressor);
        }
        free(row);
        fclose(file);
        errorMessage = errorManager.message[0] == '\0' ? "libjpeg 写入失败" : errorManager.message;
        return false;
    }

    jpeg_create_compress(&compressor);
    compressorCreated = true;
    jpeg_stdio_dest(&compressor, file);
    compressor.image_width = static_cast<JDIMENSION>(width);
    compressor.image_height = static_cast<JDIMENSION>(height);
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, std::clamp(quality, 1, 100), TRUE);
    compressor.optimize_coding = TRUE;
    jpeg_start_compress(&compressor, TRUE);

    while (compressor.next_scanline < compressor.image_height) {
        const auto* source = pixels + static_cast<size_t>(compressor.next_scanline) * stride;
        for (int column = 0; column < width; ++column) {
            const unsigned int alpha = source[column * 4 + 3];
            // JPEG has no alpha channel. Composite transparent HEIC pixels onto white.
            for (int channel = 0; channel < 3; ++channel) {
                const unsigned int color = source[column * 4 + channel];
                row[column * 3 + channel] = static_cast<JSAMPLE>((color * alpha + 255U * (255U - alpha) + 127U) / 255U);
            }
        }
        JSAMPROW rowPointer = row;
        jpeg_write_scanlines(&compressor, &rowPointer, 1);
    }

    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    free(row);
    if (fclose(file) != 0) {
        errorMessage = "刷新 JPG 文件到磁盘时失败";
        return false;
    }
    return true;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

bool ConvertOne(
    const std::filesystem::path& input,
    const std::filesystem::path& tempOutput,
    OutputFormat outputFormat,
    int jpegQuality,
    std::string& errorMessage) {
    std::ifstream stream(input, std::ios::binary | std::ios::ate);
    if (!stream) {
        errorMessage = "无法打开 HEIC 文件";
        return false;
    }

    const std::streampos end = stream.tellg();
    if (end <= 0) {
        errorMessage = "HEIC 文件为空或无法读取长度";
        return false;
    }
    if (static_cast<uintmax_t>(end) > static_cast<uintmax_t>(SIZE_MAX)) {
        errorMessage = "HEIC 文件过大";
        return false;
    }

    std::vector<uint8_t> encoded(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()))) {
        errorMessage = "读取 HEIC 文件失败";
        return false;
    }

    HeifContextPtr context(heif_context_alloc(), &heif_context_free);
    if (!context) {
        errorMessage = "libheif 上下文初始化失败";
        return false;
    }

    heif_error error = heif_context_read_from_memory_without_copy(
        context.get(), encoded.data(), encoded.size(), nullptr);
    if (error.code != heif_error_Ok) {
        errorMessage = ErrorText(error);
        return false;
    }

    heif_image_handle* rawHandle = nullptr;
    error = heif_context_get_primary_image_handle(context.get(), &rawHandle);
    if (error.code != heif_error_Ok || rawHandle == nullptr) {
        errorMessage = ErrorText(error);
        return false;
    }
    HeifHandlePtr handle(rawHandle, &heif_image_handle_release);

    heif_image* rawImage = nullptr;
    error = heif_decode_image(
        handle.get(), &rawImage, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
    if (error.code != heif_error_Ok || rawImage == nullptr) {
        errorMessage = ErrorText(error);
        return false;
    }
    HeifImagePtr image(rawImage, &heif_image_release);

    int stride = 0;
    const uint8_t* pixels = heif_image_get_plane_readonly(image.get(), heif_channel_interleaved, &stride);
    const int width = heif_image_get_width(image.get(), heif_channel_interleaved);
    const int height = heif_image_get_height(image.get(), heif_channel_interleaved);
    if (pixels == nullptr || width <= 0 || height <= 0 || stride <= 0 ||
        static_cast<size_t>(stride) < static_cast<size_t>(width) * 4U) {
        errorMessage = "libheif 返回了无效的 RGBA 图像数据";
        return false;
    }

    if (outputFormat == OutputFormat::Jpeg) {
        return WriteJpeg(tempOutput, pixels, width, height, stride, jpegQuality, errorMessage);
    }
    return WritePng(tempOutput, pixels, width, height, stride, errorMessage);
}

std::filesystem::path MakeTemporaryPath(const std::filesystem::path& output, size_t index) {
    std::wostringstream name;
    name << output.filename().wstring() << L".heicconverter-" << GetCurrentProcessId() << L'-'
         << GetTickCount64() << L'-' << index << L".tmp";
    return output.parent_path() / name.str();
}

bool CommitTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& output,
    bool overwrite,
    std::string& errorMessage) {
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (overwrite) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (MoveFileExW(temporary.c_str(), output.c_str(), flags) != FALSE) {
        return true;
    }

    const DWORD code = GetLastError();
    std::ostringstream stream;
    stream << "无法保存最终图片（Windows 错误 " << code << "）";
    errorMessage = stream.str();
    return false;
}

} // namespace

ConversionController::~ConversionController() {
    Cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool ConversionController::Start(ConversionOptions options) {
    std::lock_guard lock(mutex_);
    if (running_) {
        return false;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    cancelRequested_.store(false);
    running_ = true;
    total_ = 0;
    processed_ = 0;
    succeeded_ = 0;
    failed_ = 0;
    skipped_ = 0;
    currentFile_.clear();
    status_ = "正在扫描 HEIC 文件…";
    log_.clear();
    worker_ = std::thread(&ConversionController::Run, this, std::move(options));
    return true;
}

void ConversionController::Cancel() {
    cancelRequested_.store(true);
}

ConversionSnapshot ConversionController::Snapshot() const {
    std::lock_guard lock(mutex_);
    return {
        running_,
        cancelRequested_.load(),
        total_,
        processed_,
        succeeded_,
        failed_,
        skipped_,
        currentFile_,
        status_,
        log_};
}

void ConversionController::AddLog(std::string line) {
    std::lock_guard lock(mutex_);
    log_.push_back(std::move(line));
}

void ConversionController::Run(ConversionOptions options) {
    try {
        std::vector<std::string> warnings;
        std::vector<std::filesystem::path> files = FindHeicFiles(options.folder, options.recursive, warnings);
        {
            std::lock_guard lock(mutex_);
            total_ = files.size();
            status_ = files.empty() ? "没有找到 HEIC 文件" : "正在转换…";
            for (auto& warning : warnings) {
                log_.push_back(std::move(warning));
            }
        }

        for (size_t index = 0; index < files.size(); ++index) {
            if (cancelRequested_.load()) {
                break;
            }

            const std::filesystem::path& input = files[index];
            std::filesystem::path output = input;
            output.replace_extension(options.outputFormat == OutputFormat::Jpeg ? L".jpg" : L".png");
            {
                std::lock_guard lock(mutex_);
                currentFile_ = WideToUtf8(input.wstring());
            }

            std::error_code filesystemError;
            const bool outputExists = std::filesystem::exists(output, filesystemError);
            if (filesystemError) {
                AddLog("失败: " + WideToUtf8(input.wstring()) + " — 无法检查目标文件");
                std::lock_guard lock(mutex_);
                ++failed_;
                ++processed_;
                continue;
            }
            if (outputExists && !options.overwriteExisting) {
                AddLog("跳过: " + WideToUtf8(input.wstring()) + " — 目标文件已存在");
                std::lock_guard lock(mutex_);
                ++skipped_;
                ++processed_;
                continue;
            }

            const std::filesystem::path temporary = MakeTemporaryPath(output, index);
            std::string errorMessage;
            bool success = ConvertOne(
                input, temporary, options.outputFormat, options.jpegQuality, errorMessage);
            if (success) {
                success = CommitTemporaryFile(temporary, output, options.overwriteExisting, errorMessage);
            }

            if (!success) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                AddLog("失败: " + WideToUtf8(input.wstring()) + " — " + errorMessage);
                std::lock_guard lock(mutex_);
                ++failed_;
                ++processed_;
                continue;
            }

            std::string logLine = "完成: " + WideToUtf8(input.wstring()) + " → " + WideToUtf8(output.wstring());
            if (options.deleteOriginals) {
                std::error_code deleteError;
                if (!std::filesystem::remove(input, deleteError) || deleteError) {
                    logLine += "（PNG 已生成，但原文件删除失败）";
                } else {
                    logLine += "（已删除原文件）";
                }
            }
            AddLog(std::move(logLine));
            std::lock_guard lock(mutex_);
            ++succeeded_;
            ++processed_;
        }

        std::lock_guard lock(mutex_);
        currentFile_.clear();
        if (cancelRequested_.load()) {
            status_ = "已取消";
        } else if (files.empty()) {
            status_ = "没有找到 HEIC 文件";
        } else {
            status_ = "转换完成";
        }
        running_ = false;
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        log_.push_back(std::string("任务失败: ") + error.what());
        currentFile_.clear();
        status_ = "任务失败";
        running_ = false;
    } catch (...) {
        std::lock_guard lock(mutex_);
        log_.push_back("任务失败: 未知错误");
        currentFile_.clear();
        status_ = "任务失败";
        running_ = false;
    }
}
