#include "image_converter.h"

#include "exif_metadata.h"
#include "image_writers.h"

#include <libheif/heif.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace heic_converter {
namespace {

using HeifContextPtr = std::unique_ptr<heif_context, decltype(&heif_context_free)>;
using HeifHandlePtr = std::unique_ptr<heif_image_handle, decltype(&heif_image_handle_release)>;
using HeifImagePtr = std::unique_ptr<heif_image, decltype(&heif_image_release)>;

std::string ErrorText(const heif_error& error, Language language) {
    std::ostringstream detail;
    detail << static_cast<int>(error.code) << "/" << static_cast<int>(error.subcode);
    if (error.message != nullptr && error.message[0] != '\0') {
        detail << ": " << error.message;
    }
    return std::string(SelectText(language, "libheif 错误 ", "libheif error ")) + detail.str();
}

} // namespace

bool ConvertImage(
    const std::filesystem::path& input,
    const std::filesystem::path& temporaryOutput,
    OutputFormat outputFormat,
    int pngCompressionLevel,
    int jpegQuality,
    bool preserveExif,
    Language language,
    std::string& errorMessage) {
    std::ifstream stream(input, std::ios::binary | std::ios::ate);
    if (!stream) {
        errorMessage = SelectText(language, "无法打开 HEIC 文件", "Could not open the HEIC file");
        return false;
    }

    const std::streampos end = stream.tellg();
    if (end <= 0) {
        errorMessage = SelectText(
            language, "HEIC 文件为空或无法读取长度", "The HEIC file is empty or its size could not be read");
        return false;
    }
    if (static_cast<uintmax_t>(end) > static_cast<uintmax_t>(SIZE_MAX)) {
        errorMessage = SelectText(language, "HEIC 文件过大", "The HEIC file is too large");
        return false;
    }

    std::vector<uint8_t> encoded(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()))) {
        errorMessage = SelectText(language, "读取 HEIC 文件失败", "Failed to read the HEIC file");
        return false;
    }

    HeifContextPtr context(heif_context_alloc(), &heif_context_free);
    if (!context) {
        errorMessage = SelectText(
            language, "libheif 上下文初始化失败", "Failed to initialize the libheif context");
        return false;
    }

    heif_error error = heif_context_read_from_memory_without_copy(
        context.get(), encoded.data(), encoded.size(), nullptr);
    if (error.code != heif_error_Ok) {
        errorMessage = ErrorText(error, language);
        return false;
    }

    heif_image_handle* rawHandle = nullptr;
    error = heif_context_get_primary_image_handle(context.get(), &rawHandle);
    if (error.code != heif_error_Ok || rawHandle == nullptr) {
        errorMessage = ErrorText(error, language);
        return false;
    }
    HeifHandlePtr handle(rawHandle, &heif_image_handle_release);

    std::vector<uint8_t> exif;
    if (!ExtractExifMetadata(handle.get(), preserveExif, language, exif, errorMessage)) {
        return false;
    }

    heif_image* rawImage = nullptr;
    error = heif_decode_image(
        handle.get(), &rawImage, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr);
    if (error.code != heif_error_Ok || rawImage == nullptr) {
        errorMessage = ErrorText(error, language);
        return false;
    }
    HeifImagePtr image(rawImage, &heif_image_release);

    int stride = 0;
    const uint8_t* pixels = heif_image_get_plane_readonly(image.get(), heif_channel_interleaved, &stride);
    const int width = heif_image_get_width(image.get(), heif_channel_interleaved);
    const int height = heif_image_get_height(image.get(), heif_channel_interleaved);
    if (pixels == nullptr || width <= 0 || height <= 0 || stride <= 0 ||
        static_cast<size_t>(stride) < static_cast<size_t>(width) * 4U) {
        errorMessage = SelectText(
            language,
            "libheif 返回了无效的 RGBA 图像数据",
            "libheif returned invalid RGBA image data");
        return false;
    }

    NormalizeExifMetadata(exif, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    if (outputFormat == OutputFormat::Jpeg) {
        return WriteJpeg(
            temporaryOutput, pixels, width, height, stride, jpegQuality, exif, language, errorMessage);
    }
    return WritePng(
        temporaryOutput,
        pixels,
        width,
        height,
        stride,
        pngCompressionLevel,
        exif,
        language,
        errorMessage);
}

} // namespace heic_converter
