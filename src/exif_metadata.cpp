#include "exif_metadata.h"

#include <libheif/heif.h>

#include <algorithm>
#include <cstddef>

namespace heic_converter {
namespace {

constexpr uint16_t tagImageWidth = 0x0100;
constexpr uint16_t tagImageHeight = 0x0101;
constexpr uint16_t tagOrientation = 0x0112;
constexpr uint16_t tagExifIfd = 0x8769;
constexpr uint16_t tagPixelWidth = 0xA002;
constexpr uint16_t tagPixelHeight = 0xA003;
constexpr uint16_t typeShort = 3;
constexpr uint16_t typeLong = 4;

bool Read16(const std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint16_t& value) {
    if (offset > data.size() || data.size() - offset < 2) {
        return false;
    }
    value = littleEndian
                ? static_cast<uint16_t>(data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8U))
                : static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) | data[offset + 1]);
    return true;
}

bool Read32(const std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint32_t& value) {
    if (offset > data.size() || data.size() - offset < 4) {
        return false;
    }
    if (littleEndian) {
        value = static_cast<uint32_t>(data[offset]) |
                (static_cast<uint32_t>(data[offset + 1]) << 8U) |
                (static_cast<uint32_t>(data[offset + 2]) << 16U) |
                (static_cast<uint32_t>(data[offset + 3]) << 24U);
    } else {
        value = (static_cast<uint32_t>(data[offset]) << 24U) |
                (static_cast<uint32_t>(data[offset + 1]) << 16U) |
                (static_cast<uint32_t>(data[offset + 2]) << 8U) |
                static_cast<uint32_t>(data[offset + 3]);
    }
    return true;
}

void Write16(std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint16_t value) {
    if (offset > data.size() || data.size() - offset < 2) {
        return;
    }
    data[offset + (littleEndian ? 0 : 1)] = static_cast<uint8_t>(value & 0xFFU);
    data[offset + (littleEndian ? 1 : 0)] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void Write32(std::vector<uint8_t>& data, size_t offset, bool littleEndian, uint32_t value) {
    if (offset > data.size() || data.size() - offset < 4) {
        return;
    }
    for (size_t index = 0; index < 4; ++index) {
        const size_t target = littleEndian ? index : 3 - index;
        data[offset + target] = static_cast<uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void UpdateDimension(
    std::vector<uint8_t>& exif,
    size_t valueOffset,
    bool littleEndian,
    uint16_t type,
    uint32_t count,
    uint32_t value) {
    if (count != 1) {
        return;
    }
    if (type == typeLong) {
        Write32(exif, valueOffset, littleEndian, value);
    } else if (type == typeShort && value <= 0xFFFFU) {
        Write16(exif, valueOffset, littleEndian, static_cast<uint16_t>(value));
    }
}

void UpdateIfd(
    std::vector<uint8_t>& exif,
    uint32_t ifdOffset,
    bool littleEndian,
    uint32_t width,
    uint32_t height,
    bool followExifIfd) {
    uint16_t entryCount = 0;
    if (!Read16(exif, ifdOffset, littleEndian, entryCount)) {
        return;
    }

    const size_t entriesOffset = static_cast<size_t>(ifdOffset) + 2U;
    if (entriesOffset > exif.size() || entryCount > (exif.size() - entriesOffset) / 12U) {
        return;
    }

    uint32_t exifIfdOffset = 0;
    for (uint16_t index = 0; index < entryCount; ++index) {
        const size_t entry = entriesOffset + static_cast<size_t>(index) * 12U;
        uint16_t tag = 0;
        uint16_t type = 0;
        uint32_t count = 0;
        if (!Read16(exif, entry, littleEndian, tag) ||
            !Read16(exif, entry + 2U, littleEndian, type) ||
            !Read32(exif, entry + 4U, littleEndian, count)) {
            continue;
        }

        if (tag == tagOrientation && type == typeShort && count == 1) {
            Write16(exif, entry + 8U, littleEndian, 1);
        } else if (tag == tagImageWidth || tag == tagPixelWidth) {
            UpdateDimension(exif, entry + 8U, littleEndian, type, count, width);
        } else if (tag == tagImageHeight || tag == tagPixelHeight) {
            UpdateDimension(exif, entry + 8U, littleEndian, type, count, height);
        } else if (followExifIfd && tag == tagExifIfd && type == typeLong && count == 1) {
            Read32(exif, entry + 8U, littleEndian, exifIfdOffset);
        }
    }

    if (followExifIfd && exifIfdOffset != 0) {
        UpdateIfd(exif, exifIfdOffset, littleEndian, width, height, false);
    }
}

bool HasTiffHeader(const std::vector<uint8_t>& exif, bool& littleEndian) {
    if (exif.size() < 8) {
        return false;
    }
    if (exif[0] == 'I' && exif[1] == 'I') {
        littleEndian = true;
    } else if (exif[0] == 'M' && exif[1] == 'M') {
        littleEndian = false;
    } else {
        return false;
    }
    uint16_t marker = 0;
    return Read16(exif, 2, littleEndian, marker) && marker == 42;
}

} // namespace

bool ExtractExifMetadata(
    const heif_image_handle* handle,
    bool preserveExif,
    Language language,
    std::vector<uint8_t>& exif,
    std::string& errorMessage) {
    exif.clear();
    if (!preserveExif) {
        return true;
    }

    const int blockCount = heif_image_handle_get_number_of_metadata_blocks(handle, "Exif");
    if (blockCount <= 0) {
        return true;
    }

    std::vector<heif_item_id> ids(static_cast<size_t>(blockCount));
    const int idCount = heif_image_handle_get_list_of_metadata_block_IDs(
        handle, "Exif", ids.data(), blockCount);
    if (idCount <= 0) {
        errorMessage = SelectText(language, "无法读取 EXIF 元数据列表", "Could not read the EXIF metadata list");
        return false;
    }

    const size_t rawSize = heif_image_handle_get_metadata_size(handle, ids.front());
    if (rawSize <= 4) {
        errorMessage = SelectText(language, "EXIF 元数据无效", "The EXIF metadata is invalid");
        return false;
    }

    std::vector<uint8_t> raw(rawSize);
    const heif_error error = heif_image_handle_get_metadata(handle, ids.front(), raw.data());
    if (error.code != heif_error_Ok) {
        errorMessage = std::string(SelectText(
                           language, "读取 EXIF 元数据失败", "Failed to read the EXIF metadata")) +
                       (error.message == nullptr ? "" : std::string(": ") + error.message);
        return false;
    }

    const uint32_t tiffOffset = (static_cast<uint32_t>(raw[0]) << 24U) |
                                (static_cast<uint32_t>(raw[1]) << 16U) |
                                (static_cast<uint32_t>(raw[2]) << 8U) |
                                static_cast<uint32_t>(raw[3]);
    const size_t start = 4U + static_cast<size_t>(tiffOffset);
    if (start >= raw.size()) {
        errorMessage = SelectText(language, "EXIF 元数据偏移无效", "The EXIF metadata offset is invalid");
        return false;
    }

    exif.assign(raw.begin() + static_cast<std::ptrdiff_t>(start), raw.end());
    bool littleEndian = false;
    if (!HasTiffHeader(exif, littleEndian)) {
        exif.clear();
        errorMessage = SelectText(language, "EXIF TIFF 数据无效", "The EXIF TIFF data is invalid");
        return false;
    }
    return true;
}

void NormalizeExifMetadata(std::vector<uint8_t>& exif, uint32_t width, uint32_t height) {
    bool littleEndian = false;
    uint32_t firstIfdOffset = 0;
    if (!HasTiffHeader(exif, littleEndian) || !Read32(exif, 4, littleEndian, firstIfdOffset)) {
        return;
    }
    UpdateIfd(exif, firstIfdOffset, littleEndian, width, height, true);
}

} // namespace heic_converter
