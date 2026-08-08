#include "converter.h"

#include "file_operations.h"
#include "image_converter.h"
#include "utf8.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <system_error>
#include <utility>
#include <vector>

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
            heic_converter::FindHeicFiles(options.folder, options.recursive, warnings);
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
            status_ = files.empty()
                          ? "没有找到 HEIC 文件"
                          : "正在并行转换（" + std::to_string(workerCount) + " 个线程）…";
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

                            std::error_code filesystemError;
                            const bool outputExists = std::filesystem::exists(output, filesystemError);
                            if (filesystemError) {
                                completeFile(FileResult::Failed, "失败: " + inputName + " — 无法检查目标文件");
                                continue;
                            }
                            if (outputExists && !options.overwriteExisting) {
                                completeFile(FileResult::Skipped, "跳过: " + inputName + " — 目标文件已存在");
                                continue;
                            }

                            const std::filesystem::path temporary =
                                heic_converter::MakeTemporaryPath(output, index);
                            std::string errorMessage;
                            bool success = heic_converter::ConvertImage(
                                input, temporary, options.outputFormat, options.jpegQuality, errorMessage);
                            if (success) {
                                success = heic_converter::CommitTemporaryFile(
                                    temporary, output, options.overwriteExisting, errorMessage);
                            }

                            if (!success) {
                                std::error_code ignored;
                                std::filesystem::remove(temporary, ignored);
                                completeFile(FileResult::Failed, "失败: " + inputName + " — " + errorMessage);
                                continue;
                            }

                            std::string logLine =
                                "完成: " + inputName + " → " + WideToUtf8(output.wstring());
                            if (options.deleteOriginals) {
                                std::error_code deleteError;
                                if (!std::filesystem::remove(input, deleteError) || deleteError) {
                                    logLine += "（目标图片已生成，但原文件删除失败）";
                                } else {
                                    logLine += "（已删除原文件）";
                                }
                            }
                            completeFile(FileResult::Succeeded, std::move(logLine));
                        } catch (const std::exception& error) {
                            const std::string displayName = inputName.empty() ? "<无法显示的路径>" : inputName;
                            completeFile(FileResult::Failed, "失败: " + displayName + " — " + error.what());
                        } catch (...) {
                            const std::string displayName = inputName.empty() ? "<无法显示的路径>" : inputName;
                            completeFile(FileResult::Failed, "失败: " + displayName + " — 未知错误");
                        }
                    }
                });
            }
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
