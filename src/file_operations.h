#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace heic_converter {

std::vector<std::filesystem::path> FindHeicFiles(
    const std::filesystem::path& folder,
    bool recursive,
    std::vector<std::string>& warnings);

std::filesystem::path MakeTemporaryPath(const std::filesystem::path& output, size_t index);

bool CommitTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& output,
    bool overwrite,
    std::string& errorMessage);

} // namespace heic_converter
