#pragma once

#include <functional>
#include <string>
#include <vector>
#include <windows.h>

namespace dk {

enum class InjectStep : uint8_t
{
    ValidatePayload,
    PrepareAttach,
    AttachJvm,           // agent JAR route (preferred)
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
    DWORD remoteModuleHandle = 0;     // LoadLibraryW return value in target (CRT path)
    bool payloadMissingAfterFail = false; // AV quarantine hint
    const char* injectMethod = nullptr;   // "jvmti-attach" or "nt-create-thread-ex"
    std::string       attachAttemptError; // populated when the attach route fell back to CRT
};

const char* injectStepName(InjectStep step);

using InjectProgressFn = std::function<void(int step, int total, const char* label)>;

class Injector {
public:
    // Tries the JVM attach route first (agent JAR + JDK Attach API, no remote
    // thread, no LoadLibraryW). Falls back to the classic CRT route on failure.
    InjectionResult inject(DWORD pid, const std::wstring& dllPath, InjectProgressFn progress = nullptr);
    bool isPayloadLoaded(DWORD pid, const std::wstring& dllPath);
};

} // namespace dk
