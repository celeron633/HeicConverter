#include "converter.h"

#include "file_operations.h"
#include "image_converter.h"
#include "utf8.h"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <exception>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

std::wstring OutputPathKey(const std::filesystem::path& path) {
    std::wstring key = path.lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return key;
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
    workerCount_ = 0;
    activeWorkers_ = 0;
    currentFile_.clear();
    status_ = SelectText(options.language, "正在扫描 HEIC 文件…", "Scanning for HEIC files...");
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
        workerCount_,
        activeWorkers_,
        currentFile_,
        status_,
        log_};
}

void ConversionController::Run(ConversionOptions options) {
    try {
        std::vector<std::string> warnings;
        std::vector<std::filesystem::path> files =
            heic_converter::FindHeicFiles(options.folder, options.recursive, options.language, warnings);
        constexpr size_t maxWorkerCount = 64;
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        const size_t requestedWorkerCount = options.workerCount == 0
                                                ? std::max<size_t>(1, hardwareThreads)
                                                : options.workerCount;
        const size_t workerCount = std::min(files.size(), std::min(requestedWorkerCount, maxWorkerCount));
        {
            std::lock_guard lock(mutex_);
            total_ = files.size();
            workerCount_ = workerCount;
            if (files.empty()) {
                status_ = SelectText(options.language, "没有找到 HEIC 文件", "No HEIC files found");
            } else if (options.language == Language::English) {
                status_ = "Converting in parallel (" + std::to_string(workerCount) + " threads)...";
            } else {
                status_ = "正在并行转换（" + std::to_string(workerCount) + " 个线程）…";
            }
            for (auto& warning : warnings) {
                log_.push_back(std::move(warning));
            }
        }

        enum class FileResult {
            Succeeded,
            Failed,
            Skipped,
        };
        const auto completeFile = [this](FileResult result, std::string logLine) {
            std::lock_guard lock(mutex_);
            log_.push_back(std::move(logLine));
            ++processed_;
            if (activeWorkers_ > 0) {
                --activeWorkers_;
            }
            switch (result) {
            case FileResult::Succeeded:
                ++succeeded_;
                break;
            case FileResult::Failed:
                ++failed_;
                break;
            case FileResult::Skipped:
                ++skipped_;
                break;
            }
        };

        std::atomic_size_t nextIndex{0};
        std::mutex outputPathsMutex;
        std::unordered_set<std::wstring> outputPaths;
        {
            std::vector<std::jthread> workers;
            workers.reserve(workerCount);
            for (size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
                workers.emplace_back([&] {
                    while (!cancelRequested_.load()) {
                        const size_t index = nextIndex.fetch_add(1);
                        if (index >= files.size() || cancelRequested_.load()) {
                            return;
                        }

                        const std::filesystem::path& input = files[index];
                        std::string inputName;
                        {
                            std::lock_guard lock(mutex_);
                            ++activeWorkers_;
                        }

                        try {
                            inputName = WideToUtf8(input.wstring());
                            {
                                std::lock_guard lock(mutex_);
                                currentFile_ = inputName;
                            }

                            std::filesystem::path output = input;
                            output.replace_extension(options.outputFormat == OutputFormat::Jpeg ? L".jpg" : L".png");

                            if (!options.fileNaming.enabled) {
                                std::error_code filesystemError;
                                const bool outputExists = std::filesystem::exists(output, filesystemError);
                                if (filesystemError) {
                                    completeFile(
                                        FileResult::Failed,
                                        std::string(SelectText(options.language, "失败: ", "Failed: ")) + inputName +
                                            SelectText(
                                                options.language,
                                                " — 无法检查目标文件",
                                                " — Could not inspect the output file"));
                                    continue;
                                }
                                if (outputExists && !options.overwriteExisting) {
                                    completeFile(
                                        FileResult::Skipped,
                                        std::string(SelectText(options.language, "跳过: ", "Skipped: ")) + inputName +
                                            SelectText(
                                                options.language,
                                                " — 目标文件已存在",
                                                " — The output file already exists"));
                                    continue;
                                }
                            }

                            const std::filesystem::path temporary =
                                heic_converter::MakeTemporaryPath(output, index);
                            std::string errorMessage;
                            std::optional<std::string> exifDateTime;
                            bool success = heic_converter::ConvertImage(
                                input,
                                temporary,
                                options.outputFormat,
                                options.pngCompressionLevel,
                                options.jpegQuality,
                                options.preserveExif,
                                options.fileNaming.enabled,
                                options.language,
                                exifDateTime,
                                errorMessage);

                            bool usedCustomName = false;
                            if (success) {
                                const heic_converter::FileNamingResult naming = heic_converter::BuildFileStem(
                                    options.fileNaming,
                                    WideToUtf8(input.stem().wstring()),
                                    exifDateTime,
                                    index,
                                    options.language);
                                if (!naming.success) {
                                    success = false;
                                    errorMessage = naming.error;
                                } else if (options.fileNaming.enabled) {
                                    usedCustomName = naming.usedCustomName;
                                    const std::wstring extension =
                                        options.outputFormat == OutputFormat::Jpeg ? L".jpg" : L".png";
                                    output = input.parent_path() / (Utf8ToWide(naming.stem) + extension);

                                    std::lock_guard outputLock(outputPathsMutex);
                                    if (!outputPaths.insert(OutputPathKey(output)).second) {
                                        success = false;
                                        errorMessage = SelectText(
                                            options.language,
                                            "生成的文件名与本批次中的其他文件重复",
                                            "The generated filename duplicates another file in this batch");
                                    }
                                }
                            }
                            if (success && options.fileNaming.enabled) {
                                std::error_code filesystemError;
                                const bool outputExists = std::filesystem::exists(output, filesystemError);
                                if (filesystemError) {
                                    success = false;
                                    errorMessage = SelectText(
                                        options.language,
                                        "无法检查目标文件",
                                        "Could not inspect the output file");
                                } else if (outputExists && !options.overwriteExisting) {
                                    std::error_code ignored;
                                    std::filesystem::remove(temporary, ignored);
                                    completeFile(
                                        FileResult::Skipped,
                                        std::string(SelectText(options.language, "跳过: ", "Skipped: ")) + inputName +
                                            SelectText(
                                                options.language,
                                                " — 目标文件已存在",
                                                " — The output file already exists"));
                                    continue;
                                }
                            }
                            if (success) {
                                success = heic_converter::CommitTemporaryFile(
                                    temporary,
                                    output,
                                    options.overwriteExisting,
                                    options.language,
                                    errorMessage);
                            }

                            if (!success) {
                                std::error_code ignored;
                                std::filesystem::remove(temporary, ignored);
                                completeFile(
                                    FileResult::Failed,
                                    std::string(SelectText(options.language, "失败: ", "Failed: ")) + inputName +
                                        " — " + errorMessage);
                                continue;
                            }

                            const std::string outputName = WideToUtf8(output.wstring());
                            std::string logLine =
                                std::string(SelectText(options.language, "完成: ", "Completed: ")) + inputName +
                                " → " + outputName;
                            if (options.fileNaming.enabled && !usedCustomName) {
                                logLine += SelectText(
                                    options.language,
                                    "（未找到 EXIF 日期，保留原文件名）",
                                    " (no EXIF date found; kept the original filename)");
                            }
                            if (options.deleteOriginals) {
                                std::error_code deleteError;
                                if (!std::filesystem::remove(input, deleteError) || deleteError) {
                                    logLine += SelectText(
                                        options.language,
                                        "（目标图片已生成，但原文件删除失败）",
                                        " (output created, but the original could not be deleted)");
                                } else {
                                    logLine += SelectText(
                                        options.language, "（已删除原文件）", " (original deleted)");
                                }
                            }
                            completeFile(FileResult::Succeeded, std::move(logLine));
                        } catch (const std::exception& error) {
                            const std::string displayName = inputName.empty()
                                                                ? SelectText(
                                                                      options.language,
                                                                      "<无法显示的路径>",
                                                                      "<unprintable path>")
                                                                : inputName;
                            completeFile(
                                FileResult::Failed,
                                std::string(SelectText(options.language, "失败: ", "Failed: ")) + displayName +
                                    " — " + error.what());
                        } catch (...) {
                            const std::string displayName = inputName.empty()
                                                                ? SelectText(
                                                                      options.language,
                                                                      "<无法显示的路径>",
                                                                      "<unprintable path>")
                                                                : inputName;
                            completeFile(
                                FileResult::Failed,
                                std::string(SelectText(options.language, "失败: ", "Failed: ")) + displayName +
                                    SelectText(options.language, " — 未知错误", " — Unknown error"));
                        }
                    }
                });
            }
        }

        std::lock_guard lock(mutex_);
        currentFile_.clear();
        if (cancelRequested_.load()) {
            status_ = SelectText(options.language, "已取消", "Cancelled");
        } else if (files.empty()) {
            status_ = SelectText(options.language, "没有找到 HEIC 文件", "No HEIC files found");
        } else {
            status_ = SelectText(options.language, "转换完成", "Conversion complete");
        }
        running_ = false;
    } catch (const std::exception& error) {
        std::lock_guard lock(mutex_);
        log_.push_back(
            std::string(SelectText(options.language, "任务失败: ", "Task failed: ")) + error.what());
        currentFile_.clear();
        status_ = SelectText(options.language, "任务失败", "Task failed");
        running_ = false;
    } catch (...) {
        std::lock_guard lock(mutex_);
        log_.push_back(SelectText(options.language, "任务失败: 未知错误", "Task failed: Unknown error"));
        currentFile_.clear();
        status_ = SelectText(options.language, "任务失败", "Task failed");
        running_ = false;
    }
}
