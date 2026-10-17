#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace dk {

std::string wideToUtf8(const std::wstring& w);
std::string lastErrorString(DWORD err = GetLastError());

// Decode standard base64 (no whitespace); returns empty vector on malformed input.
std::vector<uint8_t> decodeBase64(const std::string& in);

} // namespace dk
