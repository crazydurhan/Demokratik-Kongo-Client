#include "installPath.h"
#include "stealthNames.h"

#include <Windows.h>
#include <filesystem>
#include <cwchar>

namespace dk {

std::wstring payloadExtractPath(uint64_t embeddedContentHash)
{
    wchar_t tempBuf[MAX_PATH]{};
    const DWORD tempLen = GetTempPathW(MAX_PATH, tempBuf);
    if (tempLen == 0 || tempLen >= MAX_PATH)
        return L"";

    std::wstring root = tempBuf;
    if (!root.empty() && root.back() != L'\\')
        root += L'\\';
    root += stealth::kAppFolder;
    root += L'\\';

    wchar_t hashHex[17]{};
    swprintf_s(hashHex, L"%016llx", static_cast<unsigned long long>(embeddedContentHash));
    root += hashHex;
    root += L'\\';

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec)
        return L"";

    return root + stealth::kPayloadDll;
}

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
