#pragma once

#include "localization.h"
#include "output_format.h"

#include <filesystem>
#include <string>

namespace heic_converter {

bool ConvertImage(
    const std::filesystem::path& input,
    const std::filesystem::path& temporaryOutput,
    OutputFormat outputFormat,
    int pngCompressionLevel,
    int jpegQuality,
    bool preserveExif,
    Language language,
    std::string& errorMessage);

} // namespace heic_converter
