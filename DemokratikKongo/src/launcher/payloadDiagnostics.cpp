#include "payloadDiagnostics.h"

#include "util.h"

#include <cstring>

namespace dk {

namespace {

bool validatePeHeaders(HANDLE file, DWORD fileSize, std::string& err)
{
    if (fileSize < sizeof(IMAGE_DOS_HEADER)) {
        err = "File too small for PE";
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    if (!ReadFile(file, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos)) {
        err = "Cannot read DOS header";
        return false;
    }
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) {
        err = "Invalid DOS signature (not MZ)";
        return false;
    }
    if (static_cast<DWORD>(dos.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > fileSize) {
        err = "Invalid PE header offset";
        return false;
    }

    if (SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        err = "Cannot seek to PE header";
        return false;
    }

    DWORD peSig = 0;
    if (!ReadFile(file, &peSig, sizeof(peSig), &read, nullptr) || read != sizeof(peSig)) {
        err = "Cannot read PE signature";
        return false;
    }
    if (peSig != IMAGE_NT_SIGNATURE) {
        err = "Invalid PE signature";
        return false;
    }

    IMAGE_FILE_HEADER fileHeader{};
    if (!ReadFile(file, &fileHeader, sizeof(fileHeader), &read, nullptr) || read != sizeof(fileHeader)) {
        err = "Cannot read COFF header";
        return false;
    }

#ifdef _WIN64
    if (fileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        err = "PE machine is not x64";
        return false;
    }
#else
    if (fileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        err = "PE machine is not x86";
        return false;
    }
#endif

    return true;
}

} // namespace

PayloadDiagnostics diagnosePayload(const std::wstring& dllPath)
{
    PayloadDiagnostics d;
    d.pathUtf8 = wideToUtf8(dllPath);

    const DWORD attrs = GetFileAttributesW(dllPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        d.exists = false;
        d.lastError = GetLastError();
        return d;
    }

    d.exists = true;
    d.fileAttrs = attrs;

    HANDLE h = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        d.lastError = GetLastError();
        return d;
    }

    d.readable = true;
    LARGE_INTEGER sz{};
    bool sizeKnown = false;
    if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 && sz.QuadPart <= 0xFFFFFFFFu) {
        d.fileSize = static_cast<DWORD>(sz.QuadPart);
        sizeKnown = true;
    }

    std::string peErr;
    if (!sizeKnown) {
        // GetFileSizeEx failed — do not report this as "too small for PE".
        d.peValid = false;
        d.peErrorText = "Cannot read file (GetFileSizeEx failed: " + lastErrorString() + ")";
    } else {
        d.peValid = validatePeHeaders(h, d.fileSize, peErr);
        if (!d.peValid)
            d.peErrorText = peErr;
    }

    CloseHandle(h);
    return d;
}

} // namespace dk
