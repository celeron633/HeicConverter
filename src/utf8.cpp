#include "utf8.h"

#include <windows.h>

#include <stdexcept>

namespace {

template <typename SourceChar, typename DestinationChar, typename SizeFunction, typename ConvertFunction>
std::basic_string<DestinationChar> ConvertString(
    const std::basic_string<SourceChar>& value,
    SizeFunction sizeFunction,
    ConvertFunction convertFunction) {
    if (value.empty()) {
        return {};
    }

    const int sourceLength = static_cast<int>(value.size());
    const int destinationLength = sizeFunction(sourceLength);
    if (destinationLength <= 0) {
        throw std::runtime_error("Invalid UTF-8 or UTF-16 text.");
    }

    std::basic_string<DestinationChar> result(static_cast<size_t>(destinationLength), DestinationChar{});
    if (convertFunction(sourceLength, result.data(), destinationLength) == 0) {
        throw std::runtime_error("Text encoding conversion failed.");
    }
    return result;
}

} // namespace

std::string WideToUtf8(const std::wstring& value) {
    return ConvertString<wchar_t, char>(
        value,
        [&value](int length) {
            return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr, nullptr);
        },
        [&value](int length, char* output, int outputLength) {
            return WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, output, outputLength, nullptr, nullptr);
        });
}

std::wstring Utf8ToWide(const std::string& value) {
    return ConvertString<char, wchar_t>(
        value,
        [&value](int length) {
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, nullptr, 0);
        },
        [&value](int length, wchar_t* output, int outputLength) {
            return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), length, output, outputLength);
        });
}

