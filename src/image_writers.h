#pragma once

#include "localization.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace heic_converter {

bool WritePng(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    Language language,
    std::string& errorMessage);

bool WriteJpeg(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    int quality,
    Language language,
    std::string& errorMessage);

} // namespace heic_converter
