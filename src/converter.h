#pragma once

#include "localization.h"
#include "output_format.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ConversionOptions {
    std::filesystem::path folder;
    bool recursive = true;
    bool deleteOriginals = false;
    bool overwriteExisting = false;
    bool preserveExif = true;
    OutputFormat outputFormat = OutputFormat::Png;
    int pngCompressionLevel = 6;
    int jpegQuality = 92;
    // Zero selects a worker count automatically from the available logical CPUs.
    size_t workerCount = 0;
    Language language = Language::Chinese;
};

struct ConversionSnapshot {
    bool running = false;
    bool cancellationRequested = false;
    size_t total = 0;
    size_t processed = 0;
    size_t succeeded = 0;
    size_t failed = 0;
    size_t skipped = 0;
    size_t workerCount = 0;
    size_t activeWorkers = 0;
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

    mutable std::mutex mutex_;
    std::thread worker_;
    std::atomic_bool cancelRequested_{false};
    bool running_ = false;
    size_t total_ = 0;
    size_t processed_ = 0;
    size_t succeeded_ = 0;
    size_t failed_ = 0;
    size_t skipped_ = 0;
    size_t workerCount_ = 0;
    size_t activeWorkers_ = 0;
    std::string currentFile_;
    std::string status_ = "就绪";
    std::vector<std::string> log_;
};
