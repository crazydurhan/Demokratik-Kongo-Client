#pragma once

#include <string>
#include <windows.h>

namespace dk {

std::string wideToUtf8(const std::wstring& w);
std::string lastErrorString(DWORD err = GetLastError());

} // namespace dk
