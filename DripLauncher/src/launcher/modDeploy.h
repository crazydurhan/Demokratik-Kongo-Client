#pragma once

#include "coreUpdater.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dk {

// State of the Lunar Client installation as far as the launcher cares:
// whether Lunar is installed, whether our loader mod is in its 1.8.9 Forge
// mods folder, which core DLL is cached, and whether the last game session
// actually loaded the core.
struct LunarStatus
{
    bool lunarFound = false;            // %USERPROFILE%\.lunarclient exists
    bool modDeployed = false;           // a runtimehost-mod-*.jar is present
    std::string modJarName;             // file name of that loader mod
    bool coreLoadedLastSession = false; // mod-loader.log shows a successful load
    std::string coreSha12;              // newest cached core DLL ("" if none)
    std::wstring lunarExe;              // Lunar Client.exe path ("" if unknown)
    std::wstring modsDir;               // ...\profiles\1.8\mods\forge-1.8.9
};

LunarStatus detectLunar();

// The embedded Forge loader mod (built by resources/build-mod.ps1).
std::vector<uint8_t> embeddedModJarBytes();

struct DeployResult
{
    bool ok = false;
    std::string error;
    std::string detail; // one line for the log
};

// Writes %APPDATA%\RuntimeHost\load\current.txt pointing at the core DLL.
// The loader mod reads this file and System.load()s the core inside the game.
DeployResult writeCorePointer(const std::wstring& coreDllPath);

// Writes the embedded loader mod into the Lunar 1.8.9 Forge mods folder
// (content-named; older copies are pruned best-effort).
DeployResult deployLoaderMod();

// Newest RuntimeHostCore-<edition>-<sha12>.dll in %APPDATA%\RuntimeHost\update.
std::pair<std::wstring, std::string> newestCachedCore(Edition edition);

// Starts the Lunar client launcher if we know where it lives.
bool launchLunarClient();

} // namespace dk
