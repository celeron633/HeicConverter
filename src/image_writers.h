#pragma once

#include "localization.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace heic_converter {

bool WritePng(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    const std::vector<uint8_t>& exif,
    Language language,
    std::string& errorMessage);

bool WriteJpeg(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    int quality,
    const std::vector<uint8_t>& exif,
    Language language,
    std::string& errorMessage);

} // namespace heic_converter
