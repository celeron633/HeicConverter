#pragma once

#include "localization.h"

#include <cstdint>
#include <string>
#include <vector>

struct heif_image_handle;

namespace heic_converter {

bool ExtractExifMetadata(
    const heif_image_handle* handle,
    bool preserveExif,
    Language language,
    std::vector<uint8_t>& exif,
    std::string& errorMessage);

void NormalizeExifMetadata(std::vector<uint8_t>& exif, uint32_t width, uint32_t height);

} // namespace heic_converter
