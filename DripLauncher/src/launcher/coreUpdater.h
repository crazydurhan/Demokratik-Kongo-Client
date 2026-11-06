#pragma once

#include <cstdint>
#include <string>

namespace dk {

// Which client core the launcher is working with. The update service keeps one
// lane per edition, so the two clients ship independently.
enum class Edition
{
    Legacy, // Minecraft 1.8.9
    Modern, // high versions (1.21.x Fabric)
};

const char* editionQueryValue(Edition edition);   // "legacy" / "modern" (server query)
const wchar_t* editionCacheTag(Edition edition);  // L"legacy" / L"modern" (cache file name)
const char* editionDisplayName(Edition edition);  // "1.8.9" / "1.21.x"

// The launcher's client for the server-hosted core DLL (the "DLL / Update"
// flow in LISANS_VE_YUKLEME_STANDARTLARI.md): every inject tries to download
// the current core for the selected edition from the update service first
// (host configured in include/serviceEndpoints.h), so updates ship by uploading
// a new DLL in the panel - the EXE never changes.
// The download is license-gated server-side (validate-key with key + HWID),
// and integrity is checked against the X-DLL-SHA256 response header here.
struct CoreUpdateResult
{
    bool ok = false;
    std::wstring filePathW; // verified DLL in %APPDATA%\RuntimeHost\update (for the injector)
    std::string filePath;   // same, UTF-8 (for logs)
    std::string sha256Hex;  // the verified checksum
    std::string name;       // server-side file name (display only)
    int64_t size = 0;
    std::string error;      // human-readable reason when ok == false
};

// Blocking; run on a worker thread. Never throws. `licenseKey` is the key the
// launcher already validated; the server re-validates it together with the
// machine fingerprint before handing out the file.
CoreUpdateResult fetchLatestCore(const std::string& licenseKey, Edition edition);

std::string coreUpdateBaseUrl();

// The launcher's self-update (same idea as the core updater, one lane up):
// the site hosts the current RuntimeHost.exe and the launcher checks/updates
// itself once per start. Windows cannot overwrite a running EXE - it *can*
// rename it - so the swap is: rename the running file to `<exe>.old`, move the
// staged copy into place, then the caller relaunches from the original path.
struct LauncherUpdateResult
{
    bool checked = false;      // the site answered the metadata request
    bool updated = false;      // a new EXE was downloaded and swapped into place
    bool needsRestart = false; // caller should relaunch the (new) EXE now
    std::string sha256Hex;     // checksum of the new launcher
    std::string message;       // one line for logs / the status row
};

// Never throws. Cheap when up to date: only the small /info JSON is fetched;
// the EXE itself is downloaded only when the checksums differ.
LauncherUpdateResult checkAndApplyLauncherUpdate();

std::wstring ownExecutablePath();

} // namespace dk
