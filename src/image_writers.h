#pragma once

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
    std::string& errorMessage);

bool WriteJpeg(
    const std::filesystem::path& output,
    const uint8_t* pixels,
    int width,
    int height,
    int stride,
    int quality,
    std::string& errorMessage);

} // namespace heic_converter
