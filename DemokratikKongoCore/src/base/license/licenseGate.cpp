#include "licenseGate.h"

#include "../util/logger.h"
#include "../gui/guiWidgets.h"
#include "../gui/guiCore.h"

#include "stealthNames.h"
#include "licenseToken.h"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "json.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

/*
================================================================================
    Demokratik Kongo core - licence gate (Phase 2)
--------------------------------------------------------------------------------
    The wire contract, the frozen HWID recipe and the policy knobs live in
    LISANS_VE_YUKLEME_STANDARTLARI.md at the repository root. Summary:

      * HWID: sha256("dkk1|MachineGuid|C:-serial|hostname") - identical to the
        launcher's recipe; both sides must bump the prefix together or every
        activated machine stops matching.
      * Record: %APPDATA%\RuntimeHost\license.dat - a DPAPI blob the launcher
        writes on every successful check ({key, lastValidAt, expiry}).
      * Check: POST validate-key with key + fingerprint (no auth header - the
        key is the credential). VALID unlocks; anything else keeps the lock.
      * Offline: unreachable server is tolerated for 24 h counted from
        lastValidAt, mirroring the launcher's grace window. Expired local
        records fail closed even offline.
      * Heartbeat: 30 minutes. While locked but unannounced it retries every
        10 s (and waits for the UI to settle before the first notification -
        Notify() touches GUI prefs that only exist once the client is up).
================================================================================
*/

namespace
{
    constexpr wchar_t kApiHost[] = L"api.drip.invalid";
    constexpr wchar_t kValidatePath[] = L"/v1/accounts/drip/licenses/actions/validate-key";
    constexpr wchar_t kUserAgent[] = L"RuntimeHostCore/1.0";
    constexpr int kTimeoutMs = 8000;
    constexpr int kHeartbeatSeconds = 30 * 60;
    constexpr int kLockedRetrySeconds = 10;
    constexpr int kOfflineGraceSeconds = 24 * 60 * 60;
    constexpr wchar_t kRecordFileName[] = L"license.dat";

    std::atomic<bool> g_active{ false }; // fail-closed
    std::atomic<bool> g_stop{ false };
    std::atomic<bool> g_started{ false };
    std::thread g_worker;

    // ------------------------------------------------------------ helpers

    int64_t nowUnix()
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // "2026-11-16T19:17:35.954Z" -> unix seconds; 0 on failure.
    int64_t parseIso8601(const std::string& iso)
    {
        std::tm tm{};
        int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
        if (sscanf_s(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3)
            return 0;
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = s;
        return static_cast<int64_t>(_mkgmtime64(&tm));
    }

    std::string wideToUtf8(const std::wstring& w)
    {
        std::string s;
        if (!w.empty())
        {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len > 1)
            {
                s.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
            }
        }
        return s;
    }

    bool sha256Hex(const std::string& input, std::string& outHex)
    {
        BCRYPT_ALG_HANDLE alg = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD cb = 0, objLen = 0, hashLen = 0;
        bool ok = false;
        std::vector<unsigned char> obj, digest;

        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
            return false;

        if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen),
                              sizeof(objLen), &cb, 0) == 0 &&
            BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
                              sizeof(hashLen), &cb, 0) == 0)
        {
            obj.resize(objLen);
            digest.resize(hashLen);
            if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) == 0 &&
                BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                               static_cast<ULONG>(input.size()), 0) == 0 &&
                BCryptFinishHash(hash, digest.data(), hashLen, 0) == 0)
            {
                static const char kHex[] = "0123456789abcdef";
                outHex.clear();
                outHex.reserve(hashLen * 2);
                for (unsigned char b : digest)
                {
                    outHex.push_back(kHex[b >> 4]);
                    outHex.push_back(kHex[b & 0x0F]);
                }
                ok = true;
            }
        }

        if (hash)
            BCryptDestroyHash(hash);
        if (alg)
            BCryptCloseAlgorithmProvider(alg, 0);
        return ok;
    }

    std::string machineGuid()
    {
        wchar_t buf[256]{};
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                         RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &size) != ERROR_SUCCESS)
            return {};
        return wideToUtf8(buf);
    }

    uint32_t systemVolumeSerial()
    {
        DWORD serial = 0;
        if (!GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
            return 0;
        return serial;
    }

    std::string computerName()
    {
        wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1]{};
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        if (!GetComputerNameW(buf, &size))
            return {};
        return wideToUtf8(buf);
    }

    // Frozen recipe - see the standards document. Never change without bumping
    // the "dkk1" prefix on BOTH sides (launcher + core).
    std::string hwid()
    {
        char serial[16];
        snprintf(serial, sizeof(serial), "%08lx", static_cast<unsigned long>(systemVolumeSerial()));

        const std::string source =
            std::string("dkk1|") + machineGuid() + "|" + serial + "|" + computerName();

        std::string hex;
        if (!sha256Hex(source, hex))
            return {};
        return hex;
    }

    // ------------------------------------------------------------ record

    struct Record
    {
        std::string key;
        int64_t lastValidAt = 0;
        std::string expiryIso;
        std::string token; // phase-3 signed offline token (launcher-managed)
    };

    std::filesystem::path recordPath()
    {
        wchar_t buf[MAX_PATH]{};
        const DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return {};
        return std::filesystem::path(buf) / dk::stealth::kAppFolder / kRecordFileName;
    }

    bool loadRecord(Record& out)
    {
        const std::filesystem::path path = recordPath();
        if (path.empty())
            return false;

        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::vector<char> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (blob.empty())
            return false;

        DATA_BLOB inBlob{ static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(blob.data()) };
        DATA_BLOB outBlob{};
        if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob))
            return false;

        const std::string text(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
        LocalFree(outBlob.pbData);

        const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded())
            return false;
        if (j.contains("key") && j["key"].is_string())
            out.key = j["key"].get<std::string>();
        if (j.contains("lastValidAt") && j["lastValidAt"].is_number_integer())
            out.lastValidAt = j["lastValidAt"].get<int64_t>();
        if (j.contains("expiry") && j["expiry"].is_string())
            out.expiryIso = j["expiry"].get<std::string>();
        if (j.contains("token") && j["token"].is_string())
            out.token = j["token"].get<std::string>();
        return !out.key.empty();
    }

    // ------------------------------------------------------------ server

    // Returns true when the server answered; outCode then holds meta.code.
    // (A network failure returns false - that is what the grace window is for.)
    bool httpValidate(const std::string& key, const std::string& fingerprint, std::string& outCode)
    {
        outCode.clear();

        HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            return false;
        WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

        HINTERNET connect = WinHttpConnect(session, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET request = connect
            ? WinHttpOpenRequest(connect, L"POST", kValidatePath, nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
            : nullptr;

        bool answered = false;
        if (request)
        {
            const nlohmann::json body = {
                { "meta", { { "key", key }, { "scope", { { "fingerprint", fingerprint } } } } },
            };
            const std::string payload = body.dump();

            const wchar_t* headers = L"Content-Type: application/vnd.api+json\r\nAccept: application/vnd.api+json\r\n";
            if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                                   const_cast<char*>(payload.data()), static_cast<DWORD>(payload.size()),
                                   static_cast<DWORD>(payload.size()), 0) &&
                WinHttpReceiveResponse(request, nullptr))
            {
                std::string raw;
                for (;;)
                {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                        break;
                    const size_t offset = raw.size();
                    raw.resize(offset + available);
                    DWORD read = 0;
                    if (!WinHttpReadData(request, raw.data() + offset, available, &read))
                        break;
                    raw.resize(offset + read);
                }

                const nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
                if (!j.is_discarded() && j.contains("meta") && j["meta"].is_object())
                {
                    if (j["meta"].contains("code") && j["meta"]["code"].is_string())
                        outCode = j["meta"]["code"].get<std::string>();
                    answered = true;
                }
            }
            WinHttpCloseHandle(request);
        }
        if (connect)
            WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return answered;
    }

    // ------------------------------------------------------------ policy

    // true  = licence may run (sets outDetail to a short reason)
    bool verifyOnce(std::string& outDetail)
    {
        Record rec;
        if (!loadRecord(rec))
        {
            outDetail = "no local licence record";
            return false;
        }

        // Local expiry fails closed even offline.
        if (!rec.expiryIso.empty())
        {
            const int64_t t = parseIso8601(rec.expiryIso);
            if (t > 0 && t <= nowUnix())
            {
                outDetail = "expired";
                return false;
            }
        }

        std::string code;
        if (httpValidate(rec.key, hwid(), code))
        {
            if (code == "VALID")
            {
                outDetail = "valid";
                return true;
            }
            outDetail = "server: " + (code.empty() ? "unknown" : code);
            return false;
        }

        // Phase 3: a server-signed token verifies locally - signature, key,
        // HWID and expiry - so a spoofed or absent network changes nothing
        // for a licence that was genuinely active recently.
        if (!rec.token.empty())
        {
            const auto t = dk::licensing::verifyLicenseToken(rec.token);
            if (t.valid && t.key == rec.key && t.fingerprint == hwid() && t.expiresAt > nowUnix())
            {
                outDetail = "offline token";
                return true;
            }
        }

        // Legacy fallback for records written before Phase 3 (no token yet).
        if (nowUnix() - rec.lastValidAt <= kOfflineGraceSeconds)
        {
            outDetail = "offline grace";
            return true;
        }
        outDetail = "unreachable beyond grace";
        return false;
    }

    void workerMain()
    {
        const auto startedAt = std::chrono::steady_clock::now();
        bool announcedLock = false;

        for (;;)
        {
            if (g_stop.load(std::memory_order_acquire))
                break;

            std::string detail;
            const bool ok = verifyOnce(detail);
            g_active.store(ok, std::memory_order_release);

            const bool uiSettled =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - startedAt).count() >= 10;

            if (!ok && !announcedLock && uiSettled)
            {
                announcedLock = true;
                Logger::Warn("LicenseGate", "licence not verified (" + detail + ") - features disabled");
                // Never log the key itself - only the outcome.
                Gui::Notify("License", "License not verified - features disabled",
                            Gui::Icon::Warning, Gui::Colors().danger);
            }
            else if (ok && announcedLock)
            {
                announcedLock = false;
                Logger::Info("LicenseGate", "licence verified again - features enabled");
                Gui::Notify("License", "License verified - features enabled",
                            Gui::Icon::Check, Gui::Colors().good);
            }
            else if (ok)
            {
                Logger::Info("LicenseGate", "licence verified (" + detail + ")");
            }

            // While locked and unannounced, retry fast so a slow first paint
            // never leaves the client dark for a whole heartbeat.
            const int interval = (ok || announcedLock) ? kHeartbeatSeconds : kLockedRetrySeconds;
            for (int i = 0; i < interval && !g_stop.load(std::memory_order_acquire); ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
} // namespace

void LicenseGate::Init()
{
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    g_stop.store(false, std::memory_order_release);
    g_worker = std::thread(workerMain);
}

void LicenseGate::Kill()
{
    g_stop.store(true, std::memory_order_release);
    if (g_worker.joinable())
        g_worker.join();
}

bool LicenseGate::Active()
{
    return g_active.load(std::memory_order_acquire);
}
