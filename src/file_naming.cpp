#include "file_naming.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace heic_converter {
namespace {

bool IsReservedWindowsName(std::string_view stem) {
    const size_t dot = stem.find('.');
    std::string base(stem.substr(0, dot));
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") {
        return true;
    }
    if (base.size() == 4 && (base.rfind("COM", 0) == 0 || base.rfind("LPT", 0) == 0) &&
        base[3] >= '1' && base[3] <= '9') {
        return true;
    }
    return false;
}

bool IsValidWindowsStem(const std::string& stem) {
    if (stem.empty() || stem.back() == ' ' || stem.back() == '.' || IsReservedWindowsName(stem)) {
        return false;
    }
    constexpr std::string_view invalid = "<>:\"/\\|?*";
    for (unsigned char character : stem) {
        if (character < 32 || invalid.find(static_cast<char>(character)) != std::string_view::npos) {
            return false;
        }
    }
    return true;
}

std::string FormatSequence(const FileNamingOptions& options, size_t fileIndex, bool& overflow) {
    overflow = fileIndex > std::numeric_limits<uint64_t>::max() - options.sequenceStart;
    if (overflow) {
        return {};
    }
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(std::clamp(options.sequenceDigits, 1, 12))
           << options.sequenceStart + static_cast<uint64_t>(fileIndex);
    return stream.str();
}

} // namespace

FileNamingResult BuildFileStem(
    const FileNamingOptions& options,
    const std::string& originalStem,
    const std::optional<std::string>& exifDateTime,
    size_t fileIndex,
    Language language) {
    if (!options.enabled || !exifDateTime.has_value()) {
        return {true, false, originalStem, {}};
    }
    if (options.pattern.empty()) {
        return {
            false,
            false,
            {},
            SelectText(language, "文件名模板不能为空", "The filename template cannot be empty"),
        };
    }

    bool sequenceOverflow = false;
    const std::string sequence = FormatSequence(options, fileIndex, sequenceOverflow);
    if (sequenceOverflow) {
        return {
            false,
            false,
            {},
            SelectText(language, "文件名序号超出范围", "The filename sequence is out of range"),
        };
    }

    std::string stem;
    for (size_t position = 0; position < options.pattern.size();) {
        if (options.pattern[position] != '{') {
            stem.push_back(options.pattern[position++]);
            continue;
        }

        const size_t end = options.pattern.find('}', position + 1);
        if (end == std::string::npos) {
            return {
                false,
                false,
                {},
                SelectText(language, "文件名模板中的占位符未闭合", "A filename placeholder is not closed"),
            };
        }

        const std::string_view token(options.pattern.data() + position, end - position + 1);
        if (token == "{text}") {
            stem += options.customText;
        } else if (token == "{datetime}") {
            stem += *exifDateTime;
        } else if (token == "{seq}") {
            stem += sequence;
        } else {
            return {
                false,
                false,
                {},
                std::string(SelectText(language, "未知的文件名占位符: ", "Unknown filename placeholder: ")) +
                    std::string(token),
            };
        }
        position = end + 1;
    }

    if (!IsValidWindowsStem(stem)) {
        return {
            false,
            false,
            {},
            SelectText(
                language,
                "生成的文件名为空、包含 Windows 禁用字符或使用了保留名称",
                "The generated filename is empty, contains invalid Windows characters, or uses a reserved name"),
        };
    }
    return {true, true, std::move(stem), {}};
}

} // namespace heic_converter
