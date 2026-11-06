#include "launcher/launcherApp.h"
#include "launcher/installPath.h"
#include "launcher/javaInject.h"
#include "launcher/modDeploy.h"
#include "launcher/process_scanner.h"
#include "licenseToken.h"

#include <Windows.h>
#include <shellapi.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace {

// Hidden CLI: RuntimeHost.exe --inject <pid> [dll]
// Runs the java.exe-hosted injector against the given pid without opening the
// GUI (smoke tests, scripted reloads). The DLL defaults to the newest cached
// core from past licensed downloads. Returns -1 when no CLI flag was given
// (GUI path), otherwise the process exit code.
int runCliInject()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return -1;

    DWORD pid = 0;
    std::wstring dllArg;
    bool requested = false;
    dk::Edition edition = dk::Edition::Legacy;
    for (int i = 1; i < argc; ++i)
    {
        if (lstrcmpW(argv[i], L"--edition") == 0 && i + 1 < argc)
        {
            if (lstrcmpW(argv[i + 1], L"modern") == 0)
                edition = dk::Edition::Modern;
            continue;
        }
        if (lstrcmpW(argv[i], L"--inject") == 0)
        {
            requested = true;
            if (i + 1 < argc)
                pid = static_cast<DWORD>(_wtoi(argv[i + 1]));
            if (i + 2 < argc && argv[i + 2][0] != L'-')
                dllArg = argv[i + 2];
            break;
        }
    }
    LocalFree(argv);
    if (!requested)
        return -1;

    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }

    if (!pid)
    {
        std::fprintf(stderr, "[dk-inject] usage: RuntimeHost.exe --inject <pid> [dll] [--edition modern|legacy]\n");
        return 3;
    }

    std::wstring payload = dllArg;
    if (payload.empty())
    {
        const auto cached = dk::newestCachedCore(edition);
        if (!cached.first.empty())
            payload = cached.first;
    }
    if (payload.empty() || GetFileAttributesW(payload.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        std::fprintf(stderr, "[dk-inject] no payload DLL (pass one, or cache a core first)\n");
        return 4;
    }

    const std::wstring agent = dk::ensureAgentDll();
    if (agent.empty())
    {
        std::fprintf(stderr, "[dk-inject] embedded injector DLL unavailable - rebuild the launcher\n");
        return 4;
    }

    std::wstring javaExe;
    for (const std::wstring& candidate : dk::collectAttachJavaExeCandidates(pid))
    {
        if (!candidate.empty() && GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            javaExe = candidate;
            break;
        }
    }
    if (javaExe.empty())
    {
        std::fprintf(stderr, "[dk-inject] no java.exe found to host the injector\n");
        return 4;
    }

    std::fprintf(stdout, "[dk-inject] target=%lu payload=%ls\n", pid, payload.c_str());
    std::fprintf(stdout, "[dk-inject] java host=%ls\n", javaExe.c_str());

    const dk::JavaInjectResult result = dk::runJavaAgentInject(javaExe, agent, pid, payload);
    if (!result.ok)
    {
        std::fprintf(stderr, "[dk-inject] FAILED: %s (exit=%lu)\n", result.error.c_str(),
                     static_cast<unsigned long>(result.exitCode));
        return 5;
    }

    std::fprintf(stdout, "[dk-inject] success - %s\n", result.detail.c_str());
    return 0;
}

// Hidden CLI: RuntimeHost.exe --token-test <file>
// Verifies an offline licence token with the same shared code the core uses
// (embedded Ed25519 public key) and prints the result. Exit 0 when the token
// verifies and has not expired, 2 otherwise, 3 when the file cannot be read.
// Used for support and deployment checks; never touches the network.
int runTokenTest()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return -1;

    std::wstring path;
    for (int i = 1; i < argc; ++i)
    {
        if (lstrcmpW(argv[i], L"--token-test") == 0 && i + 1 < argc)
        {
            path = argv[i + 1];
            break;
        }
    }
    LocalFree(argv);
    if (path.empty())
        return -1;

    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "[dk-token] cannot open %ls\n", path.c_str());
        return 3;
    }
    std::string token((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!token.empty() && (token.back() == '\r' || token.back() == '\n' || token.back() == ' '))
        token.pop_back();

    const dk::licensing::TokenInfo t = dk::licensing::verifyLicenseToken(token);
    if (!t.valid)
    {
        std::fprintf(stderr, "[dk-token] INVALID: %s\n", t.reason.c_str());
        return 2;
    }

    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int64_t left = t.expiresAt - now;
    if (left <= 0)
    {
        std::fprintf(stderr, "[dk-token] EXPIRED\n");
        return 2;
    }

    std::fprintf(stdout, "[dk-token] VALID key=%s fp=%s name=%s days_left=%lld\n",
        t.key.c_str(), t.fingerprint.substr(0, 16).c_str(), t.holder.c_str(),
        static_cast<long long>(left / 86400));
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    const int tokenTest = runTokenTest();
    if (tokenTest >= 0)
        return tokenTest;

    const int inject = runCliInject();
    if (inject >= 0)
        return inject;

    dk::LauncherApp app;
    return app.run(hInstance);
}
