#include "image_writers.h"

#include <cstdio>
#include <csetjmp>
#include <jpeglib.h>
#include <png.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

namespace heic_converter {

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
    Language language,
    std::string& errorMessage) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, output.c_str(), L"wb") != 0 || file == nullptr) {
        errorMessage = SelectText(language, "无法创建临时 PNG 文件", "Could not create the temporary PNG file");
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (png == nullptr) {
        fclose(file);
        errorMessage = SelectText(language, "libpng 初始化失败", "Failed to initialize libpng");
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (info == nullptr) {
        png_destroy_write_struct(&png, nullptr);
        fclose(file);
        errorMessage = SelectText(
            language, "libpng 信息结构初始化失败", "Failed to initialize the libpng info structure");
        return false;
    }

    std::vector<png_bytep> rows(static_cast<size_t>(height));
    for (int row = 0; row < height; ++row) {
        rows[static_cast<size_t>(row)] = const_cast<png_bytep>(pixels + static_cast<size_t>(row) * stride);
    }

    if (setjmp(png_jmpbuf(png)) != 0) {
        png_destroy_write_struct(&png, &info);
        fclose(file);
        errorMessage = SelectText(language, "libpng 写入失败", "libpng failed to write the image");
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
        errorMessage = SelectText(
            language, "刷新 PNG 文件到磁盘时失败", "Failed to flush the PNG file to disk");
        return false;
    }
    return true;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace {

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

} // namespace

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
    Language language,
    std::string& errorMessage) {
    if (static_cast<size_t>(width) > SIZE_MAX / 3U) {
        errorMessage = SelectText(language, "图像宽度过大", "The image width is too large");
        return false;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, output.c_str(), L"wb") != 0 || file == nullptr) {
        errorMessage = SelectText(language, "无法创建临时 JPG 文件", "Could not create the temporary JPG file");
        return false;
    }

    auto* row = static_cast<JSAMPLE*>(malloc(static_cast<size_t>(width) * 3U));
    if (row == nullptr) {
        fclose(file);
        errorMessage = SelectText(language, "JPG 行缓冲区分配失败", "Failed to allocate the JPG row buffer");
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
        const std::string detail = errorManager.message[0] == '\0' ? "" : std::string(": ") + errorManager.message;
        errorMessage = std::string(SelectText(
                           language, "libjpeg 写入失败", "libjpeg failed to write the image")) +
                       detail;
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
                row[column * 3 + channel] = static_cast<JSAMPLE>(
                    (color * alpha + 255U * (255U - alpha) + 127U) / 255U);
            }
        }
        JSAMPROW rowPointer = row;
        jpeg_write_scanlines(&compressor, &rowPointer, 1);
    }

    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    free(row);
    if (fclose(file) != 0) {
        errorMessage = SelectText(
            language, "刷新 JPG 文件到磁盘时失败", "Failed to flush the JPG file to disk");
        return false;
    }
    return true;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace heic_converter
