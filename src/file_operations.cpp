#include "file_operations.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <system_error>

namespace heic_converter {
namespace {

bool IsHeicFile(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return extension == L".heic";
}

} // namespace

std::vector<std::filesystem::path> FindHeicFiles(
    const std::filesystem::path& folder,
    bool recursive,
    Language language,
    std::vector<std::string>& warnings) {
    std::vector<std::filesystem::path> files;
    const auto options = std::filesystem::directory_options::skip_permission_denied;

    const auto inspect = [&files](const std::filesystem::directory_entry& entry) {
        std::error_code error;
        if (entry.is_regular_file(error) && !error && IsHeicFile(entry.path())) {
            files.push_back(entry.path());
        }
    };

    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, options)) {
                inspect(entry);
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(folder, options)) {
                inspect(entry);
            }
        }
    } catch (const std::filesystem::filesystem_error& error) {
        warnings.emplace_back(
            std::string(SelectText(language, "扫描警告: ", "Scan warning: ")) + error.what());
    }

    std::sort(files.begin(), files.end());
    return files;
}

std::filesystem::path MakeTemporaryPath(const std::filesystem::path& output, size_t index) {
    std::wostringstream name;
    name << output.filename().wstring() << L".heicconverter-" << GetCurrentProcessId() << L'-'
         << GetTickCount64() << L'-' << index << L".tmp";
    return output.parent_path() / name.str();
}

bool CommitTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& output,
    bool overwrite,
    Language language,
    std::string& errorMessage) {
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (overwrite) {
        flags |= MOVEFILE_REPLACE_EXISTING;
    }
    if (MoveFileExW(temporary.c_str(), output.c_str(), flags) != FALSE) {
        return true;
    }

    const DWORD code = GetLastError();
    std::ostringstream stream;
    if (language == Language::English) {
        stream << "Could not save the final image (Windows error " << code << ")";
    } else {
        stream << "无法保存最终图片（Windows 错误 " << code << "）";
    }
    errorMessage = stream.str();
    return false;
}

} // namespace heic_converter
