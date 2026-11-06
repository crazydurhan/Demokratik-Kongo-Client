// RuntimeHostInjector - a JVMTI tool agent that loads a DLL into a target
// process with the classic remote-thread primitives, executed from inside a
// java.exe host process. Running the operation from java.exe (instead of the
// launcher EXE) makes the actor pattern identical to ordinary JDK tooling
// (jcmd / profilers do the same OpenProcess + VirtualAllocEx +
// WriteProcessMemory + CreateRemoteThread sequence on every attach), which
// behavioural AV heuristics treat as normal developer activity.
//
// Usage (spawned by the launcher):
//   java.exe -agentpath:<this.dll>=pid=<pid>;dll=<absolute dll path> -version
//
// The agent runs inside the tool JVM, performs the injection, writes a
// diagnostic log and terminates the host process with the result as its exit
// code (0 = ok, 4 = args, 5 = open process, 6 = alloc/write, 7 = thread/load).

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

// The JVM only needs the exported symbol; no JNI calls are made.
extern "C" __declspec(dllexport) int Agent_OnLoad(void* vm, char* options, void* reserved);

namespace {

// ---------------------------------------------------------------- payload

// Data block copied into the target: the LoadLibraryW address (kernel32 is
// mapped at the same base in every process of the session) plus the DLL path.
struct Payload
{
    void* loadLibraryW;
    wchar_t dllPath[1024];
};

// The stub copied into the target. No absolute references: it only derefs the
// caller-passed block and calls through the function pointer it holds. The
// optimize pragmas keep the codegen simple and make the byte range between
// StubMain and StubEnd stable (function-level linking is disabled for this
// project so the two stay adjacent, in source order).
#pragma optimize("", off)
DWORD WINAPI StubMain(Payload* p)
{
    using LoadLibFn = HMODULE(WINAPI*)(LPCWSTR);
    const auto fn = reinterpret_cast<LoadLibFn>(p->loadLibraryW);
    const HMODULE mod = fn(p->dllPath);
    return static_cast<DWORD>(reinterpret_cast<uintptr_t>(mod));
}

DWORD WINAPI StubEnd()
{
    return 0;
}
#pragma optimize("", on)

// ---------------------------------------------------------------- helpers

std::wstring appDataRuntimeHostDir()
{
    wchar_t buf[32768];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, 32768);
    if (n == 0 || n >= 32768)
        return L"";
    return std::wstring(buf, n) + L"\\RuntimeHost";
}

void logLine(const char* fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    std::fprintf(stdout, "[inject-agent] %s\n", msg);
    std::fflush(stdout);

    const std::wstring dir = appDataRuntimeHostDir();
    if (dir.empty())
        return;
    const std::wstring path = dir + L"\\inject-agent.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"a") == 0 && f)
    {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u] %s\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
        std::fclose(f);
    }
}

bool parseOption(const char* options, const char* key, std::string& out)
{
    const std::string opts = options ? options : "";
    const std::string needle = std::string(key) + "=";
    const size_t at = opts.find(needle);
    if (at == std::string::npos)
        return false;
    const size_t start = at + needle.size();
    const size_t end = opts.find(';', start);
    out = opts.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return !out.empty();
}

// Agent options arrive as a C string: modern JVMs (18+) hand them over as
// UTF-8, older JDKs use the active ANSI code page on Windows. Decode both -
// a byte-wise widening corrupts any non-ASCII path (e.g. a Turkish user name
// like "C:\Users\Ömer\...").
std::wstring decodeOptionText(const std::string& s)
{
    if (s.empty())
        return L"";

    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int len = MultiByteToWideChar(codePage, flags, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) {
        codePage = CP_ACP;
        flags = 0;
        len = MultiByteToWideChar(codePage, flags, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    }
    if (len <= 0)
        return L"";

    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(codePage, flags, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

bool moduleLoadedInTarget(DWORD pid, const wchar_t* dllBaseName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    bool found = false;
    if (Module32FirstW(snap, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, dllBaseName) == 0)
            {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

bool processIsX64(HANDLE hProc, bool& x64)
{
    BOOL wow64 = FALSE;
    if (!IsWow64Process(hProc, &wow64))
        return false;
    x64 = (wow64 == FALSE);
    return true;
}

} // namespace

extern "C" __declspec(dllexport) int Agent_OnLoad(void* vm, char* options, void* reserved)
{
    (void)vm;
    (void)reserved;

    std::string pidText;
    std::string dllText;
    if (!parseOption(options, "pid", pidText) || !parseOption(options, "dll", dllText))
    {
        logLine("ERR args: expected pid=<pid>;dll=<path>");
        std::exit(4);
    }

    const DWORD pid = static_cast<DWORD>(std::strtoul(pidText.c_str(), nullptr, 10));
    if (pid == 0)
    {
        logLine("ERR invalid pid");
        std::exit(4);
    }

    const std::wstring dllPathUtf8 = decodeOptionText(dllText);
    if (GetFileAttributesW(dllPathUtf8.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        logLine("ERR dll not found: %s", dllText.c_str());
        std::exit(4);
    }

    logLine("host pid %lu -> target pid %lu", GetCurrentProcessId(), pid);

    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                   PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid);
    if (!hProc)
    {
        logLine("ERR OpenProcess failed (%lu)", GetLastError());
        std::exit(5);
    }

    bool target64 = false;
    if (!processIsX64(hProc, target64) || !target64)
    {
        logLine("ERR target is not an x64 process");
        CloseHandle(hProc);
        std::exit(5);
    }

    Payload payload{};
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    payload.loadLibraryW = reinterpret_cast<void*>(GetProcAddress(k32, "LoadLibraryW"));
    if (!payload.loadLibraryW)
    {
        logLine("ERR GetProcAddress(LoadLibraryW) failed");
        CloseHandle(hProc);
        std::exit(6);
    }
    wcsncpy_s(payload.dllPath, dllPathUtf8.c_str(), _TRUNCATE);

    void* remoteData = VirtualAllocEx(hProc, nullptr, sizeof(Payload), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteData || !WriteProcessMemory(hProc, remoteData, &payload, sizeof(Payload), nullptr))
    {
        logLine("ERR VirtualAllocEx/WriteProcessMemory (payload) failed (%lu)", GetLastError());
        CloseHandle(hProc);
        std::exit(6);
    }

    const ptrdiff_t stubDelta =
        reinterpret_cast<const BYTE*>(&StubEnd) - reinterpret_cast<const BYTE*>(&StubMain);
    if (stubDelta <= 0 || stubDelta > 4096)
    {
        logLine("ERR stub layout invalid (delta=%lld)", static_cast<long long>(stubDelta));
        CloseHandle(hProc);
        std::exit(6);
    }
    const SIZE_T stubSize = static_cast<SIZE_T>(stubDelta);

    void* remoteStub = VirtualAllocEx(hProc, nullptr, stubSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteStub || !WriteProcessMemory(hProc, remoteStub, reinterpret_cast<const void*>(&StubMain), stubSize, nullptr))
    {
        logLine("ERR VirtualAllocEx/WriteProcessMemory (stub, size=%llu) failed (%lu)",
                static_cast<unsigned long long>(stubSize), GetLastError());
        CloseHandle(hProc);
        std::exit(6);
    }
    logLine("stub copied: %llu bytes", static_cast<unsigned long long>(stubSize));

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0,
                                        reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteStub),
                                        remoteData, 0, nullptr);
    if (!hThread)
    {
        logLine("ERR CreateRemoteThread failed (%lu)", GetLastError());
        CloseHandle(hProc);
        std::exit(7);
    }

    const DWORD wait = WaitForSingleObject(hThread, 15000);
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);

    VirtualFreeEx(hProc, remoteStub, 0, MEM_RELEASE);
    VirtualFreeEx(hProc, remoteData, 0, MEM_RELEASE);

    if (wait != WAIT_OBJECT_0 || exitCode == 0)
    {
        logLine("ERR remote LoadLibraryW failed (wait=%lu, hmodule=0x%08lx)", wait, exitCode);
        CloseHandle(hProc);
        std::exit(7);
    }

    const wchar_t* baseName = wcsrchr(payload.dllPath, L'\\');
    baseName = baseName ? baseName + 1 : payload.dllPath;
    const bool verified = moduleLoadedInTarget(pid, baseName);

    logLine("OK remote LoadLibraryW -> hmodule=0x%08lx, module visible=%s, dll=%s",
            exitCode, verified ? "yes" : "no", dllText.c_str());
    CloseHandle(hProc);

    std::fflush(nullptr);
    std::exit(verified ? 0 : 8);
}
