#include "javaInject.h"

#include "injectorDllData.h"
#include "util.h"

#include <Windows.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

/*
================================================================================
    Drip Launcher - java.exe-hosted injection
--------------------------------------------------------------------------------
    The remote LoadLibraryW is executed by a java.exe host process running the
    embedded JVMTI tool agent (src/agent/injectAgent.cpp). The launcher itself
    never touches the target process; the actor pattern on the machine is the
    same one ordinary JDK tooling (jcmd, profilers) produces every day, which
    keeps behavioural AV heuristics quiet.
================================================================================
*/

namespace dk {

namespace {

uint64_t fnv1a64(const void* data, size_t len)
{
    constexpr uint64_t kOffset = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = kOffset;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }
    return hash;
}

uint64_t fileSizeBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return 0;
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    return sz.QuadPart;
}

bool writeFileAtomic(const std::wstring& path, const void* data, DWORD size)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    const std::wstring tmp = path + L".tmp";

    HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);

    if (!ok || written != size) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    return true;
}

} // namespace

std::wstring ensureAgentDll()
{
    const std::vector<uint8_t> dll = decodeBase64(injectordll::kInjectorDllBase64());
    if (dll.empty())
        return L"";

    const uint64_t hash = fnv1a64(dll.data(), dll.size());
    wchar_t hex[17] = {};
    swprintf_s(hex, L"%016llx", static_cast<unsigned long long>(hash));

    wchar_t tempBuf[MAX_PATH] = {};
    const DWORD tempLen = GetTempPathW(MAX_PATH, tempBuf);
    if (tempLen == 0 || tempLen >= MAX_PATH)
        return L"";

    std::wstring dir = std::wstring(tempBuf) + L"RuntimeHost\\" + hex;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec)
        return L"";

    const std::wstring path = dir + L"\\RuntimeHostInjector.dll";
    const bool exists = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!exists || fileSizeBytes(path) != dll.size()) {
        if (!writeFileAtomic(path, dll.data(), static_cast<DWORD>(dll.size())))
            return L"";
    }

    return path;
}

JavaInjectResult runJavaAgentInject(const std::wstring& javaExe,
                                    const std::wstring& agentDll,
                                    DWORD targetPid,
                                    const std::wstring& payloadDll)
{
    JavaInjectResult r;
    if (javaExe.empty() || agentDll.empty() || payloadDll.empty() || targetPid == 0) {
        r.error = "invalid arguments";
        return r;
    }

    // One argv element: -agentpath:<dll>=pid=<pid>;dll=<payload>
    const std::wstring agentArg = L"-agentpath:" + agentDll + L"=pid=" +
                                  std::to_wstring(targetPid) + L";dll=" + payloadDll;
    std::wstring cmd = L"\"" + javaExe + L"\" \"" + agentArg + L"\" -version";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        r.error = "CreateProcess failed: " + lastErrorString();
        return r;
    }

    const DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    r.exitCode = exitCode;
    if (wait != WAIT_OBJECT_0) {
        r.error = "java host timed out";
        return r;
    }
    if (exitCode != 0) {
        r.error = "java agent exit code " + std::to_string(exitCode) + " (see inject-agent.log)";
        return r;
    }

    r.ok = true;
    r.detail = "hosted by " + wideToUtf8(javaExe);
    return r;
}

} // namespace dk
