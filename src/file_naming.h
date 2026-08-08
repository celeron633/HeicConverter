#pragma once

#include "localization.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

struct FileNamingOptions {
    bool enabled = false;
    std::string pattern = "{text}_{datetime}_{seq}";
    std::string customText = "Photo";
    uint64_t sequenceStart = 1;
    int sequenceDigits = 4;
};

namespace heic_converter {

struct FileNamingResult {
    bool success = false;
    bool usedCustomName = false;
    std::string stem;
    std::string error;
};

FileNamingResult BuildFileStem(
    const FileNamingOptions& options,
    const std::string& originalStem,
    const std::optional<std::string>& exifDateTime,
    size_t fileIndex,
    Language language);

} // namespace heic_converter
