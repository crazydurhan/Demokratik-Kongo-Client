#include "installPath.h"
#include "stealthNames.h"

#include <Windows.h>
#include <filesystem>

namespace dk {

std::wstring siblingPayloadPath()
{
    wchar_t exeBuf[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return L"";

    std::filesystem::path exePath(exeBuf);
    return (exePath.parent_path() / stealth::kPayloadDll).wstring();
}

} // namespace dk
