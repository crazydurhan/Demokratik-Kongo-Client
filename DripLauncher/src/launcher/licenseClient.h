#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dk {

// The launcher's client for the Drip Launcher licensing service. The wire
// format and the exact request sequence are frozen by
// LISANS_VE_YUKLEME_STANDARTLARI.md at the repository root; keep this header
// and that document in sync whenever the contract changes.
//
// Everything here is blocking and is meant to run on a worker thread; the GUI
// only ever reads snapshots of LicenseInfo through its own mutex.
struct LicenseInfo
{
    bool valid = false;        // the key passed validation on this machine
    bool offline = false;      // accepted from the local record (grace window)
    bool networkError = false; // server unreachable; never treated as a ban
    std::string code;          // Keygen validation code (VALID, EXPIRED, ...)
    std::string detail;        // server-side detail string (English)
    std::string userMessage;   // one line for the status row / toast
    std::string holder;        // license name as set in the panel
    std::string licenseId;
    std::string expiryIso;     // empty when the license never expires
    int daysLeft = -1;         // -1 = never expires
    int64_t checkedAtUnix = 0; // when this snapshot was produced
};

// The DPAPI-encrypted on-disk record, %APPDATA%\RuntimeHost\license.dat.
struct LicenseRecord
{
    std::string key;
    int64_t lastValidAtUnix = 0; // last successful server validation
    std::string expiryIso;
    std::string token; // phase-3 signed offline token (refreshed on every check)
};

class LicenseClient {
public:
    // Machine fingerprint: "dkk1|MachineGuid|C:-serial|hostname", SHA-256,
    // lowercase hex. The recipe is frozen by the standards document - never
    // change it without bumping the "dkk1" prefix, or every already activated
    // machine in the wild stops matching its key.
    static std::string hwid();

    // Validates the key for this machine and, on the very first use, binds it
    // to the hardware (one key = one PC, per the licensing policy). Never
    // throws; every failure is described by the returned LicenseInfo.
    LicenseInfo check(const std::string& rawKey);

private:
    bool httpPost(const wchar_t* host, const std::wstring& path, const std::string& body,
                  const std::wstring& authorization, std::string& outBody,
                  long& outStatus, std::string& outError);

    // Fetches a signed offline token for a key that just validated; empty on
    // any failure (check() then keeps the previous token, if any).
    std::string fetchLicenseToken(const std::string& key, const std::string& fingerprint);
};

// Normalises user input (trim + ASCII upper-case) into the canonical key form.
std::string normalizeLicenseKey(const std::string& raw);

// ---- persistent record (DPAPI, per Windows user) --------------------------
std::optional<LicenseRecord> loadLicenseRecord();
bool saveLicenseRecord(const LicenseRecord& record);
void clearLicenseRecord();

} // namespace dk
