#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace dk {

enum class InjectStep : uint8_t
{
    ValidatePayload,
    OpenProcess,
    ArchCheck,
    VirtualAllocEx,
    WriteProcessMemory,
    ResolveLoadLibraryW,
    NtCreateThreadEx,
    WaitRemoteThread,
    VerifyModule,
};

struct InjectionResult {
    bool ok = false;
    DWORD systemError = 0;
    std::string message;
    InjectStep failedStep = InjectStep::ValidatePayload;
    std::vector<std::string> steps;   // human-readable step log
    DWORD remoteModuleHandle = 0;     // LoadLibraryW return value in target
    bool payloadMissingAfterFail = false; // AV quarantine hint
    const char* injectMethod = nullptr;   // "nt-create-thread-ex"
};

const char* injectStepName(InjectStep step);

class Injector {
public:
    InjectionResult inject(DWORD pid, const std::wstring& dllPath);
    bool isPayloadLoaded(DWORD pid, const std::wstring& dllPath);
};

} // namespace dk
