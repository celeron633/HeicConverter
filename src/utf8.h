#pragma once

#include <string>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);

