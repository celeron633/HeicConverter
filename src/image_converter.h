#pragma once

#include "output_format.h"

#include <filesystem>
#include <string>

namespace heic_converter {

bool ConvertImage(
    const std::filesystem::path& input,
    const std::filesystem::path& temporaryOutput,
    OutputFormat outputFormat,
    int jpegQuality,
    std::string& errorMessage);

} // namespace heic_converter
