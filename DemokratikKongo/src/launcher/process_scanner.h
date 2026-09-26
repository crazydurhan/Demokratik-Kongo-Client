#pragma once

#include <string>
#include <vector>
#include <windows.h>

namespace dk {

enum class LauncherKind {
    Unknown,
    Vanilla,
    Forge,
    Fabric,
    Lunar,
    Badlion,
    OptiFine,
    Custom,
};

struct McProcess {
    DWORD pid = 0;
    std::wstring exeName;
    std::wstring exePath;
    std::wstring windowTitle;
    bool hasJvm = false;
    bool hasLwjgl = false;
    LauncherKind launcher = LauncherKind::Unknown;
    bool x64 = true;
    bool x64Unknown = false; // IsWow64Process failed — bitness not determined
};

class ProcessScanner {
public:
    std::vector<McProcess> scan();
    const char* launcherName(LauncherKind k) const;

private:
    bool inspectProcess(DWORD pid, const std::wstring& snapExe,
                        const std::wstring& knownTitle, McProcess& out);
    LauncherKind classify(const McProcess& p, const std::wstring& cmdLine);
    std::wstring readCommandLine(HANDLE hProc);
};

} // namespace dk
