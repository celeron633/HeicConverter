#pragma once

#include "localization.h"
#include "output_format.h"

#include <filesystem>
#include <optional>
#include <string>

namespace heic_converter {

bool ConvertImage(
    const std::filesystem::path& input,
    const std::filesystem::path& temporaryOutput,
    OutputFormat outputFormat,
    int pngCompressionLevel,
    int jpegQuality,
    bool preserveExif,
    bool extractExifDateTime,
    Language language,
    std::optional<std::string>& exifDateTime,
    std::string& errorMessage);

} // namespace heic_converter
