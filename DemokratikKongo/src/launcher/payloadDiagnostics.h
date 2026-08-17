#pragma once

#include <string>
#include <windows.h>

namespace dk {

struct PayloadDiagnostics {
    bool        exists = false;
    bool        readable = false;
    bool        peValid = false;
    DWORD       fileSize = 0;
    DWORD       fileAttrs = 0;
    DWORD       lastError = 0;
    std::string pathUtf8;
    std::string peErrorText;
};

// Validates file presence/size and PE headers only — does NOT LoadLibrary the payload
// (loading cheat DLL in launcher triggers unnecessary AV scans).
PayloadDiagnostics diagnosePayload(const std::wstring& dllPath);

} // namespace dk
