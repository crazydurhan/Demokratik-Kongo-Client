// One-off development probe: verifies the launcher's target java.exe
// discovery (process_scanner.cpp) against a live process.
//
// Build (from repo root, x64 dev prompt):
//   cl /nologo /std:c++20 /EHsc /I DemokratikKongo\src\launcher ^
//      tools\scan_probe.cpp ^
//      DemokratikKongo\src\launcher\process_scanner.cpp ^
//      DemokratikKongo\src\launcher\logger.cpp ^
//      DemokratikKongo\src\launcher\util.cpp ^
//      /link psapi.lib ntdll.lib version.lib /out:build\scan_probe.exe
//
// Usage: scan_probe.exe <pid>
#include <cstdio>
#include <string>

#include "../DemokratikKongo/src/launcher/process_scanner.h"
#include "../DemokratikKongo/src/launcher/util.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: scan_probe <pid>\n");
        return 2;
    }

    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10));
    const std::wstring java = dk::findJavaExeForTarget(pid);

    if (java.empty()) {
        std::printf("findJavaExeForTarget(%lu) = <none>\n", pid);
        return 1;
    }

    std::printf("findJavaExeForTarget(%lu) = %s\n", pid, dk::wideToUtf8(java).c_str());
    return 0;
}
