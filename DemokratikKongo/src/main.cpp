#include "launcher/launcherApp.h"
#include "launcher/injector.h"
#include "launcher/resourceLoader.h"
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

// Hidden CLI: RuntimeHost.exe --inject <pid>
// Injects the embedded/sibling core without opening the GUI (smoke tests,
// scripted reloads). Returns -1 when no CLI flag was given (GUI path),
// otherwise the process exit code.
int runCliInject()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return -1;

    DWORD pid = 0;
    bool requested = false;
    for (int i = 1; i < argc; ++i)
    {
        if (lstrcmpW(argv[i], L"--inject") == 0)
        {
            requested = true;
            if (i + 1 < argc)
                pid = static_cast<DWORD>(_wtoi(argv[i + 1]));
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
        std::fprintf(stderr, "[dk-inject] usage: RuntimeHost.exe --inject <pid>\n");
        return 3;
    }

    std::wstring payload = dk::ensureEmbeddedPayload();
    if (payload.empty())
        payload = dk::forceExtractEmbeddedPayload();
    if (payload.empty())
    {
        std::fprintf(stderr, "[dk-inject] payload resolve failed\n");
        return 4;
    }

    std::fprintf(stdout, "[dk-inject] injecting into PID %lu ...\n", pid);
    dk::Injector injector;
    const dk::InjectionResult result = injector.inject(pid, payload);
    for (const std::string& s : result.steps)
        std::fprintf(stdout, "[dk-inject] %s\n", s.c_str());

    if (!result.ok)
    {
        std::fprintf(stderr, "[dk-inject] FAILED at step %s (win32=%lu) %s\n",
            dk::injectStepName(result.failedStep), result.systemError,
            result.message.c_str());
        return 5;
    }

    std::fprintf(stdout, "[dk-inject] success via %s\n",
        result.injectMethod ? result.injectMethod : "unknown");
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

    const int cli = runCliInject();
    if (cli >= 0)
        return cli;

    dk::LauncherApp app;
    return app.run(hInstance);
}
