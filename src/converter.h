#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum class OutputFormat {
    Png,
    Jpeg,
};

struct ConversionOptions {
    std::filesystem::path folder;
    bool recursive = true;
    bool deleteOriginals = false;
    bool overwriteExisting = false;
    OutputFormat outputFormat = OutputFormat::Png;
    int jpegQuality = 92;
};

struct ConversionSnapshot {
    bool running = false;
    bool cancellationRequested = false;
    size_t total = 0;
    size_t processed = 0;
    size_t succeeded = 0;
    size_t failed = 0;
    size_t skipped = 0;
    std::string currentFile;
    std::string status;
    std::vector<std::string> log;
};

class ConversionController {
public:
    ConversionController() = default;
    ~ConversionController();

    ConversionController(const ConversionController&) = delete;
    ConversionController& operator=(const ConversionController&) = delete;

    bool Start(ConversionOptions options);
    void Cancel();
    ConversionSnapshot Snapshot() const;

private:
    void Run(ConversionOptions options);
    void AddLog(std::string line);

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool cancelRequested_{false};
    bool running_ = false;
    size_t total_ = 0;
    size_t processed_ = 0;
    size_t succeeded_ = 0;
    size_t failed_ = 0;
    size_t skipped_ = 0;
    std::string currentFile_;
    std::string status_ = "就绪";
    std::vector<std::string> log_;
};
