#include "injector.h"

#include "logger.h"
#include "payloadDiagnostics.h"
#include "process_scanner.h"
#include "resourceLoader.h"
#include "util.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <cstddef>
#include <cstring>
#include <sstream>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ntdll.lib")

namespace dk {

const char* injectStepName(InjectStep step)
{
    switch (step)
    {
    case InjectStep::ValidatePayload:       return "ValidatePayload";
    case InjectStep::PrepareAttach:         return "PrepareAttach";
    case InjectStep::AttachJvm:             return "AttachJvm";
    case InjectStep::OpenProcess:           return "OpenProcess";
    case InjectStep::ArchCheck:             return "ArchCheck";
    case InjectStep::VirtualAllocEx:        return "VirtualAllocEx";
    case InjectStep::WriteProcessMemory:    return "WriteProcessMemory";
    case InjectStep::ResolveLoadLibraryW:   return "ResolveLoadLibraryW";
    case InjectStep::NtCreateThreadEx:      return "NtCreateThreadEx";
    case InjectStep::WaitRemoteThread:      return "WaitRemoteThread";
    case InjectStep::VerifyModule:          return "VerifyModule";
    default:                                return "Unknown";
    }
}

namespace {

constexpr DWORD kInjectWaitMs = 30000;
constexpr DWORD kAttachWaitMs = 30000;   // helper java + attach + JVM agent load
constexpr int   kAttachJarAttempts = 2;  // allow one retry (JVM still booting)

constexpr ULONG kThreadHideFromDebugger = 0x00000004;

using NtCreateThreadExFn = NTSTATUS(NTAPI*)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE ProcessHandle,
    PVOID StartRoutine,
    PVOID Argument,
    ULONG CreateFlags,
    SIZE_T ZeroBits,
    SIZE_T StackSize,
    SIZE_T MaximumStackSize,
    PVOID AttributeList);

// ────────────────────────────────────────────────────────────────── logging

void logPayloadDiagnostics(const PayloadDiagnostics& d)
{
    auto& log = LauncherLog::I();
    log.debug("Payload path: " + d.pathUtf8);

    if (!d.exists) {
        log.error("Payload file missing on disk (Win32 " + lastErrorString(d.lastError) + ")");
        log.warn("Windows Defender may have removed the extracted core — restart launcher to re-extract from EXE.");
        log.warn("Only RuntimeHost.exe is required; core is embedded and unpacked to TEMP on inject.");
        return;
    }

    log.debug("Payload size: " + std::to_string(d.fileSize) + " bytes");
    log.debug(std::string("Readable: ") + (d.readable ? "yes" : "no"));

    if (d.fileAttrs & FILE_ATTRIBUTE_READONLY)
        log.trace("File attribute: READONLY");
    if (d.fileAttrs & FILE_ATTRIBUTE_HIDDEN)
        log.trace("File attribute: HIDDEN");

    if (d.peValid)
        log.ok("PE header validation passed (x64 DLL, no local load)");
    else
        log.warn("PE validation failed: " + d.peErrorText);
}

void fail(InjectionResult& r, InjectStep step, const std::string& msg, DWORD err = 0)
{
    r.ok = false;
    r.failedStep = step;
    r.message = msg;
    r.systemError = err ? err : GetLastError();
    r.steps.push_back(std::string(injectStepName(step)) + " FAILED: " + msg);
    LauncherLog::I().error("[" + std::string(injectStepName(step)) + "] " + msg +
                           (r.systemError ? " (" + lastErrorString(r.systemError) + ")" : ""));
}

void pass(InjectionResult& r, InjectStep step, const std::string& detail = {})
{
    std::string line = std::string(injectStepName(step)) + " OK";
    if (!detail.empty())
        line += " — " + detail;
    r.steps.push_back(line);
    LauncherLog::I().debug("[" + std::string(injectStepName(step)) + "]" +
                           (detail.empty() ? " OK" : " " + detail));
}

// ─────────────────────────────────────────────── shared process helpers

bool isModuleLoadedInProcess(DWORD pid, const std::wstring& dllPath)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h)
        return false;

    HMODULE mods[1024];
    DWORD needed = 0;
    bool found = false;

    if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        const DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; ++i) {
            wchar_t fn[MAX_PATH] = {};
            if (GetModuleFileNameExW(h, mods[i], fn, MAX_PATH)) {
                if (_wcsicmp(fn, dllPath.c_str()) == 0) {
                    found = true;
                    break;
                }
            }
        }
    }

    CloseHandle(h);
    return found;
}

bool sameArchAsTarget(HANDLE hProc, std::string& err, std::string& detail)
{
    BOOL targetWow = FALSE;
    BOOL selfWow = FALSE;
    if (!IsWow64Process(hProc, &targetWow)) {
        err = "IsWow64Process(target) failed";
        detail = lastErrorString();
        return false;
    }
    IsWow64Process(GetCurrentProcess(), &selfWow);

    const bool target64 = !targetWow;
    const bool self64 = !selfWow;
    detail = std::string("injector=") + (self64 ? "x64" : "x86") +
             ", target=" + (target64 ? "x64" : "x86");

    if (target64 != self64) {
        err = target64 ? "Architecture mismatch — target is x64, injector is x86."
                       : "Architecture mismatch — target is x86, injector is x64.";
        return false;
    }
    return true;
}

// ────────────────────────────────────────────────────────── attach route

// Spawn `java -jar <agentJar> <pid> <dllPath>` and wait for it to finish.
// The helper attaches to the target JVM, enqueues the agent JAR, prints
// "OK" or "ERR <reason>" and exits; stdout is captured for diagnostics.
bool runAttachHelper(const std::wstring& javaExe,
                     const std::wstring& agentJar,
                     DWORD pid,
                     const std::wstring& dllPath,
                     DWORD timeoutMs,
                     std::string& output,
                     DWORD& exitCode)
{
    output.clear();
    exitCode = 0;

    // Quoted arguments. java/lang/Process-free: we spawn via CreateProcessW
    // and the helper only ever receives paths we generated ourselves.
    std::wstring cmd = L"\"" + javaExe + L"\" -jar \"" + agentJar + L"\" " +
                       std::to_wstring(pid) + L" \"" + dllPath + L"\"";

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return false;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    const BOOL launched = CreateProcessW(
        nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(writePipe);

    if (!launched) {
        CloseHandle(readPipe);
        return false;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0);
        exitCode = static_cast<DWORD>(-1);
    }

    // The helper emits a handful of bytes; reading after exit is safe.
    DWORD available = 0;
    std::string text;
    while (PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
        char buf[512];
        DWORD got = 0;
        const DWORD want = available < sizeof(buf) ? available : static_cast<DWORD>(sizeof(buf));
        if (!ReadFile(readPipe, buf, want, &got, nullptr) || got == 0)
            break;
        text.append(buf, got);
    }

    CloseHandle(readPipe);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    output = text;
    return wait != WAIT_TIMEOUT;
}

// Attempt the JVM attach route with one java candidate. On failure the caller
// moves on to the next candidate, then to the CRT fallback.
bool injectViaAttachRoute(DWORD pid, const std::wstring& dllPath,
                          const std::wstring& javaExe, int attempts,
                          InjectionResult& r, std::string& lastAttachError)
{
    auto& log = LauncherLog::I();

    const std::wstring agentJar = ensureEmbeddedAgentJar();
    if (agentJar.empty()) {
        lastAttachError = "embedded agent JAR unavailable";
        return false;
    }

    for (int attempt = 1; attempt <= attempts; ++attempt) {
        std::string output;
        DWORD exitCode = 0;

        if (!runAttachHelper(javaExe, agentJar, pid, dllPath, kAttachWaitMs, output, exitCode)) {
            lastAttachError = "attach helper timed out";
            log.warn("Attach helper timed out (attempt " + std::to_string(attempt) + ").");
            continue;
        }

        if (exitCode == 0 && output.find("OK") != std::string::npos) {
            pass(r, InjectStep::AttachJvm, "agent JAR enqueued via JDK attach");
            return true;
        }

        lastAttachError = output.empty() ? ("helper exit code " + std::to_string(exitCode))
                                         : output;
        // Strip trailing newlines for the log line.
        while (!lastAttachError.empty() &&
               (lastAttachError.back() == '\n' || lastAttachError.back() == '\r'))
            lastAttachError.pop_back();

        log.warn("Attach attempt " + std::to_string(attempt) + " failed: " + lastAttachError);
    }

    return false;
}

// ─────────────────────────────────────────────────────────── CRT route

bool injectViaNtCreateThreadEx(HANDLE hProc, DWORD pid, const std::wstring& dllPath,
                               void* remotePath, LPTHREAD_START_ROUTINE loadLib,
                               InjectionResult& r, DWORD& moduleHandle,
                               bool& keepRemoteMem)
{
    (void)pid;
    (void)dllPath;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto ntCreateThreadEx = reinterpret_cast<NtCreateThreadExFn>(
        GetProcAddress(ntdll, "NtCreateThreadEx"));
    if (!ntCreateThreadEx) {
        fail(r, InjectStep::NtCreateThreadEx, "GetProcAddress(NtCreateThreadEx) failed.", GetLastError());
        return false;
    }

    HANDLE hThread = nullptr;
    const NTSTATUS status = ntCreateThreadEx(
        &hThread,
        THREAD_ALL_ACCESS,
        nullptr,
        hProc,
        loadLib,
        remotePath,
        kThreadHideFromDebugger,
        0, 0, 0, nullptr);

    if (status != 0 || !hThread) {
        // NtCreateThreadEx can fail after partially creating the thread
        // object; close the handle so it doesn't leak.
        if (hThread)
            CloseHandle(hThread);
        const DWORD ntErr = GetLastError();
        std::ostringstream oss;
        oss << "NtCreateThreadEx failed (NTSTATUS 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(status) << ").";
        fail(r, InjectStep::NtCreateThreadEx, oss.str(), ntErr);
        return false;
    }
    pass(r, InjectStep::NtCreateThreadEx, "remote thread created (hide-from-debugger)");

    const DWORD wait = WaitForSingleObject(hThread, kInjectWaitMs);
    if (wait != WAIT_OBJECT_0) {
        const char* waitName = wait == WAIT_TIMEOUT ? "WAIT_TIMEOUT (30s)" : "unexpected";
        const DWORD waitErr = GetLastError();
        // The remote thread may still be executing LoadLibraryW on the
        // remote buffer — freeing it now would be a use-after-free inside
        // the target. Leak the page deliberately instead.
        keepRemoteMem = true;
        fail(r, InjectStep::WaitRemoteThread,
             std::string("Remote thread did not complete: ") + waitName, waitErr);
        CloseHandle(hThread);
        return false;
    }

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    // LoadLibraryW returns a full HMODULE (64-bit on x64) but the thread
    // exit code is only 32 bits, so this value is truncated and can never
    // be used as a real handle. It is heuristic only — actual verification
    // is done by isPayloadLoaded()/EnumProcessModulesEx below.
    moduleHandle = exitCode;
    if (exitCode == 0)
        return false;

    r.injectMethod = "nt-create-thread-ex";
    {
        std::ostringstream oss;
        oss << "thread exit code 0x" << std::hex << exitCode
            << " (truncated 32-bit of LoadLibraryW HMODULE — heuristic only)";
        pass(r, InjectStep::WaitRemoteThread, oss.str());
    }
    return true;
}

} // namespace

// ───────────────────────────────────────────────────────────── pipeline

InjectionResult Injector::inject(DWORD pid, const std::wstring& dllPath, InjectProgressFn progress)
{
    InjectionResult r;
    auto& log = LauncherLog::I();

    // Progress budget: attach (1 step) + CRT pipeline (9 steps).
    constexpr int kTotalSteps = 10;
    auto report = [&](int step, const char* label) {
        if (progress)
            progress(step, kTotalSteps, label);
    };

    log.separator();
    log.info("Starting injection into PID " + std::to_string(pid));

    // ── Step 1: Validate payload ─────────────────────────────────────
    {
        report(0, "Validating payload");
        r.failedStep = InjectStep::ValidatePayload;
        PayloadDiagnostics diag = diagnosePayload(dllPath);
        logPayloadDiagnostics(diag);

        if (!diag.exists) {
            fail(r, InjectStep::ValidatePayload,
                 "Payload DLL not found — file may have been removed by antivirus.", diag.lastError);
            return r;
        }
        if (diag.fileSize == 0) {
            fail(r, InjectStep::ValidatePayload, "Payload file is empty (0 bytes).");
            return r;
        }
        if (!diag.readable) {
            fail(r, InjectStep::ValidatePayload, "Cannot read payload file (access denied?).");
            return r;
        }
        if (!diag.peValid) {
            fail(r, InjectStep::ValidatePayload,
                 "Payload PE invalid or corrupt: " + diag.peErrorText);
            return r;
        }
        pass(r, InjectStep::ValidatePayload,
             std::to_string(diag.fileSize) + " bytes @ " + diag.pathUtf8);
    }

    std::string attachError;

    // ── Attach route (preferred) ─────────────────────────────────────
    // Uses the target JVM's own attach listener: no remote thread, no
    // LoadLibraryW, no loader-lock contention. Falls back to CRT below.
    {
        report(1, "Trying JVM attach route");

        const std::vector<std::wstring> javaCandidates = collectAttachJavaExeCandidates(pid);
        bool targetIs64 = false;
        {
            // The attach route ships an x64 agent; x86 targets fall back.
            HANDLE hProbe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (hProbe) {
                BOOL wow = FALSE;
                if (IsWow64Process(hProbe, &wow))
                    targetIs64 = !wow;
                CloseHandle(hProbe);
            }
        }

        if (!targetIs64) {
            log.info("Target is not x64 — skipping attach route, using classic injection.");
        } else if (javaCandidates.empty()) {
            log.info("No attach-capable java found — skipping attach route, using classic injection.");
        } else {
            // Candidates are ordered attach-capable-first (full JDKs); the
            // target's own JRE comes last because JRE images lack the attach
            // module. Trying them in order keeps Lunar/vanilla injects on the
            // no-remote-write attach path, which is what keeps Defender's
            // behavioural model quiet.
            for (size_t i = 0; i < javaCandidates.size(); ++i) {
                const std::wstring& javaExe = javaCandidates[i];
                log.info("Attach route: trying \"" + wideToUtf8(javaExe) + "\"");

                // First candidate gets the JVM-still-booting retry; later ones
                // get a single shot each.
                const int attempts = (i == 0) ? kAttachJarAttempts : 1;
                if (injectViaAttachRoute(pid, dllPath, javaExe, attempts, r, attachError)) {
                    // Verify the core actually landed in the target's module list.
                    report(2, "Verifying module");
                    if (isModuleLoadedInProcess(pid, dllPath)) {
                        pass(r, InjectStep::VerifyModule, "module visible in target (attach path)");
                        r.ok = true;
                        r.injectMethod = "jvmti-attach";
                        report(kTotalSteps, "Done");
                        r.message = "Injection complete via jvmti-attach — payload verified in target process.";
                        log.ok(r.message);
                        return r;
                    }
                    log.warn("Attach reported success but the core DLL is not in the target module list yet.");
                    attachError = "attach reported OK but module not verifiable";
                }
                if (!attachError.empty())
                    log.warn("Attach via \"" + wideToUtf8(javaExe) + "\" failed (" + attachError + ")");
            }
            // Fall through to CRT with the last attach error recorded.
            log.warn("Attach route failed on all candidates (" + attachError + ") — falling back to classic injection.");
            r.attachAttemptError = attachError;
            r.failedStep = InjectStep::AttachJvm;
        }
    }

    // ── Step 2: OpenProcess (CRT) ────────────────────────────────────
    report(2, "Opening process");
    const DWORD access =
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;

    HANDLE hProc = OpenProcess(access, FALSE, pid);
    if (!hProc) {
        if (!attachError.empty()) {
            fail(r, InjectStep::OpenProcess,
                 "Attach route failed (" + attachError + ") and OpenProcess also failed — try running as Administrator.",
                 GetLastError());
        } else {
            fail(r, InjectStep::OpenProcess,
                 "OpenProcess failed — try running as Administrator.", GetLastError());
        }
        log.warn("Required access: CREATE_THREAD | VM_OPERATION | VM_WRITE | VM_READ");
        return r;
    }
    pass(r, InjectStep::OpenProcess, "handle acquired");

    // ── Step 3: Architecture check ───────────────────────────────────
    {
        report(3, "Checking architecture");
        std::string archErr, archDetail;
        if (!sameArchAsTarget(hProc, archErr, archDetail)) {
            fail(r, InjectStep::ArchCheck, archErr);
            log.error(archDetail);
            CloseHandle(hProc);
            return r;
        }
        pass(r, InjectStep::ArchCheck, archDetail);
    }

    // ── Step 4: VirtualAllocEx ───────────────────────────────────────
    report(4, "Allocating remote memory");
    const SIZE_T pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remoteMem = VirtualAllocEx(hProc, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) {
        fail(r, InjectStep::VirtualAllocEx, "VirtualAllocEx failed in target process.", GetLastError());
        CloseHandle(hProc);
        return r;
    }
    {
        std::ostringstream oss;
        oss << "remote @ 0x" << std::hex << reinterpret_cast<uintptr_t>(remoteMem)
            << ", " << std::dec << pathBytes << " bytes";
        pass(r, InjectStep::VirtualAllocEx, oss.str());
    }

    // ── Step 5: WriteProcessMemory ───────────────────────────────────
    report(5, "Writing DLL path");
    if (!WriteProcessMemory(hProc, remoteMem, dllPath.c_str(), pathBytes, nullptr)) {
        fail(r, InjectStep::WriteProcessMemory, "WriteProcessMemory failed.", GetLastError());
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return r;
    }
    pass(r, InjectStep::WriteProcessMemory, wideToUtf8(dllPath));

    // ── Step 6: Resolve LoadLibraryW ─────────────────────────────────
    report(6, "Resolving LoadLibraryW");
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(k32, "LoadLibraryW"));
    if (!loadLib) {
        fail(r, InjectStep::ResolveLoadLibraryW, "GetProcAddress(LoadLibraryW) failed.", GetLastError());
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return r;
    }
    {
        std::ostringstream oss;
        oss << "LoadLibraryW @ 0x" << std::hex << reinterpret_cast<uintptr_t>(loadLib);
        pass(r, InjectStep::ResolveLoadLibraryW, oss.str());
    }

    // ── Step 7: NtCreateThreadEx ─────────────────────────────────────
    // Thread hijacking was removed: suspending an arbitrary JVM thread and
    // redirecting its RIP is inherently unsafe (stack misalignment for x64
    // WinAPI calls, loader-lock races, GC safepoint corruption) and was the
    // root cause of the 0xC0000005 crash in ntdll.dll on injection.
    report(7, "Creating remote thread");
    DWORD moduleHandle = 0;
    bool keepRemoteMem = false;
    bool loaded = injectViaNtCreateThreadEx(hProc, pid, dllPath, remoteMem, loadLib, r, moduleHandle,
                                            keepRemoteMem);

    if (!keepRemoteMem)
        VirtualFreeEx(hProc, remoteMem, 0, MEM_RELEASE);

    if (!loaded) {
        r.failedStep = InjectStep::WaitRemoteThread;

        const bool stillExists = GetFileAttributesW(dllPath.c_str()) != INVALID_FILE_ATTRIBUTES;
        r.payloadMissingAfterFail = !stillExists;

        if (moduleHandle == 0 && r.message.empty())
            r.message = "LoadLibraryW returned NULL in target process.";
        if (r.message.empty())
            r.message = "Injection failed — NtCreateThreadEx could not load the payload.";

        r.steps.push_back(std::string(injectStepName(InjectStep::WaitRemoteThread)) + " FAILED: " + r.message);
        log.error(r.message);

        if (r.payloadMissingAfterFail) {
            log.error("Extracted core DISAPPEARED after inject — antivirus likely removed the TEMP cache file.");
            log.warn("Restart launcher — core will be re-extracted from EXE automatically.");
        } else {
            log.warn("Payload still on disk — possible causes:");
            log.warn("  • DllMain returned FALSE or crashed during init");
            log.warn("  • Missing dependency inside target (jvm.dll not loaded yet?)");
            log.warn("  • Antivirus blocked load without deleting file");
            log.warn("  • Target blocked DLL load (protected process)");
        }

        CloseHandle(hProc);
        return r;
    }

    r.remoteModuleHandle = moduleHandle;

    // ── Step 8: Verify module in target ──────────────────────────────
    report(8, "Verifying module");
    if (!isPayloadLoaded(pid, dllPath)) {
        fail(r, InjectStep::VerifyModule,
             "LoadLibraryW succeeded but module not found in target module list.");
        log.error("CONFIRMED LOADED: LoadLibraryW returned a non-zero module handle — the payload DLL "
                  "REMAINS LOADED in the target process even though it was not found via "
                  "EnumProcessModulesEx. The DLL is NOT unloaded; a restart of the target is required "
                  "to clear it.");
        log.warn("The DLL may have loaded then immediately unloaded (DllMain failure).");
        CloseHandle(hProc);
        return r;
    }
    pass(r, InjectStep::VerifyModule, "module visible in target");

    CloseHandle(hProc);
    r.ok = true;
    report(kTotalSteps, "Done");
    r.message = std::string("Injection complete via ") +
                (r.injectMethod ? r.injectMethod : "unknown") +
                " — payload verified in target process.";
    log.ok(r.message);
    return r;
}

bool Injector::isPayloadLoaded(DWORD pid, const std::wstring& dllPath)
{
    return isModuleLoadedInProcess(pid, dllPath);
}

} // namespace dk
