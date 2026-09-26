#include "launcher/launcherApp.h"
#include "launcher/injector.h"
#include "launcher/resourceLoader.h"

#include <Windows.h>
#include <shellapi.h>
#include <cstdio>
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

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
    const int cli = runCliInject();
    if (cli >= 0)
        return cli;

    dk::LauncherApp app;
    return app.run(hInstance);
}
