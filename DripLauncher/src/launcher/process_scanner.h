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

// Find the java.exe that started the given target. Preferred source is the
// target process command line (same JVM guarantees attach-protocol
// compatibility). Falls back to JAVA_HOME / registry / PATH.
std::wstring findJavaExeForTarget(DWORD pid);

// Attach-helper java candidates, best first: full JDKs (the attach module is
// present) before JRE images, with the target's own java as the last resort.
// JRE images - e.g. the Zulu JREs Lunar bundles - lack
// com.sun.tools.attach.VirtualMachine, and attaching through one used to fail
// straight into the classic remote-thread fallback. See injector.cpp.
std::vector<std::wstring> collectAttachJavaExeCandidates(DWORD pid);

} // namespace dk
