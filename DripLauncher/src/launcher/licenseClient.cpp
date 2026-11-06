#include "licenseClient.h"
#include "licenseToken.h"
#include "hashUtils.h"
#include "serviceEndpoints.h"
#include "util.h"
#include "stealthNames.h"

#include <windows.h>
#include <dpapi.h>
#include <winhttp.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "json.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

/*
================================================================================
    Drip Launcher - licensing client
--------------------------------------------------------------------------------
    Talks to the Keygen CE deployment whose host names live in
    include/serviceEndpoints.h. The contract lives in
    LISANS_VE_YUKLEME_STANDARTLARI.md at the repository root:

      1. POST .../licenses/actions/validate-key  { meta.key + fingerprint }
         - unauthenticated; the key itself is the credential.
      2. When the code is NO_MACHINE, the key was never activated: bind this
         PC with POST .../machines using "Authorization: License <key>" and a
         relationships.license pointing at the id the validation returned,
         then re-validate.
      3. Any other failure code maps to one line of user-facing text.

    The key is device-bound (maxMachines = 1, strict): a second PC gets
    FINGERPRINT_SCOPE_MISMATCH, and a panel-side "HWID reset" deactivates the
    old machine so the next launch can bind again.
================================================================================
*/

namespace dk {

namespace {

// ---------------------------------------------------------------- service
// Hosts and account slug: include/serviceEndpoints.h (single source of truth).
using service::kApiHost;
using service::kPathLicenseToken;
using service::kPathMachines;
using service::kPathValidateKey;
using service::kUpdateHost;
const wchar_t kUserAgent[] = L"RuntimeHostLauncher/1.0";
constexpr int kTimeoutMs = 8000;
constexpr int64_t kOfflineGraceSeconds = 24 * 60 * 60;
const wchar_t kRecordFileName[] = L"license.dat";
// The current fingerprint recipe version. Any change to LicenseClient::hwid()
// must bump this prefix; see the header comment.
const char kHwidPrefix[] = "dkk1|";

// ---------------------------------------------------------------- time
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

int daysUntilIso(const std::string& iso)
{
    const int64_t t = parseIso8601(iso);
    if (t <= 0)
        return -1;
    const double days = static_cast<double>(t - nowUnix()) / 86400.0;
    return static_cast<int>(std::ceil(days));
}

// ---------------------------------------------------------------- machine ids
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

// ---------------------------------------------------------------- user text
// One line per validation failure; the mapping is specified in the standards
// document so the launcher, the panel and the core all phrase failures the
// same way. English on purpose - the launcher UI is English throughout.
std::string userMessageForCode(const std::string& code, const std::string& detail)
{
    if (code == "VALID")
        return "License active";
    if (code == "FINGERPRINT_SCOPE_MISMATCH" || code == "MACHINE_SCOPE_MISMATCH" ||
        code == "TOO_MANY_MACHINES")
        return "Key is linked to another PC - ask for a HWID reset";
    if (code == "NO_MACHINE")
        return "Activation failed on this PC";
    if (code == "SUSPENDED" || code == "BANNED")
        return "License suspended by the administrator";
    if (code == "EXPIRED")
        return "License expired";
    if (code == "NOT_FOUND")
        return "Invalid key - check it and try again";
    if (code == "NETWORK")
        return "Could not reach the license server";
    return detail.empty() ? "License check failed" : detail;
}

// The server could not be reached. Phase 3: a server-signed token verifies
// locally (signature + key + HWID + expiry) and is preferred; freshly written
// records always carry one. The lastValidAt grace stays as the fallback for
// records written before Phase 3 existed.
LicenseInfo offlineFallback(LicenseInfo info, const std::string& key)
{
    const auto record = loadLicenseRecord();
    if (record && record->key == key) {
        if (!record->token.empty()) {
            const auto t = dk::licensing::verifyLicenseToken(record->token);
            if (t.valid && t.key == key && t.fingerprint == LicenseClient::hwid() &&
                t.expiresAt > nowUnix()) {
                info.valid = true;
                info.offline = true;
                info.holder = t.holder;
                info.expiryIso = record->expiryIso;
                info.daysLeft = daysUntilIso(record->expiryIso);
                const int64_t days = (t.expiresAt - nowUnix()) / 86400;
                info.userMessage = "Offline token accepted (" + std::to_string(days) + "d left)";
                return info;
            }
        }
        if (nowUnix() - record->lastValidAtUnix <= kOfflineGraceSeconds) {
            info.valid = true;
            info.offline = true;
            info.expiryIso = record->expiryIso;
            info.daysLeft = daysUntilIso(record->expiryIso);
            const int64_t hours = (nowUnix() - record->lastValidAtUnix) / 3600;
            info.userMessage = "Offline mode - last check " + std::to_string(hours) + "h ago";
            return info;
        }
    }
    info.userMessage = userMessageForCode("NETWORK", "");
    return info;
}

// ---------------------------------------------------------------- record file
std::filesystem::path recordPath()
{
    wchar_t buf[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};
    return std::filesystem::path(buf) / stealth::kAppFolder / kRecordFileName;
}

} // namespace

// ============================================================ public

std::string normalizeLicenseKey(const std::string& raw)
{
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
        out.push_back(c);
    }
    return out;
}

std::string LicenseClient::hwid()
{
    char serial[16];
    snprintf(serial, sizeof(serial), "%08lx", static_cast<unsigned long>(systemVolumeSerial()));

    const std::string source =
        std::string(kHwidPrefix) + machineGuid() + "|" + serial + "|" + computerName();

    const std::string hex = sha256HexOfBytes(source.data(), source.size());
    if (hex.empty())
        return {};
    return hex;
}

LicenseInfo LicenseClient::check(const std::string& rawKey)
{
    LicenseInfo info;
    info.checkedAtUnix = nowUnix();

    const std::string key = normalizeLicenseKey(rawKey);
    if (key.empty()) {
        info.code = "EMPTY";
        info.userMessage = "Enter a license key";
        return info;
    }

    const std::string fingerprint = hwid();
    if (fingerprint.empty()) {
        info.code = "HWID";
        info.userMessage = "Could not read a machine ID";
        return info;
    }

    const nlohmann::json validateBody = {
        { "meta", { { "key", key }, { "scope", { { "fingerprint", fingerprint } } } } },
    };

    // ---- 1) validate; the key itself is the credential (no auth header)
    std::string body, err;
    long status = 0;
    if (!httpPost(kApiHost, kPathValidateKey, validateBody.dump(), L"", body, status, err)) {
        info.networkError = true;
        info.code = "NETWORK";
        return offlineFallback(info, key);
    }

    nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("meta")) {
        info.code = "PROTOCOL";
        info.userMessage = "Unexpected server response";
        return info;
    }

    std::string code = j["meta"].is_object() && j["meta"].contains("code") && j["meta"]["code"].is_string()
                           ? j["meta"]["code"].get<std::string>() : "";
    info.detail = j["meta"].is_object() && j["meta"].contains("detail") && j["meta"]["detail"].is_string()
                      ? j["meta"]["detail"].get<std::string>() : "";

    if (j.contains("data") && j["data"].is_object()) {
        const nlohmann::json& data = j["data"];
        if (data.contains("id") && data["id"].is_string())
            info.licenseId = data["id"].get<std::string>();
        if (data.contains("attributes") && data["attributes"].is_object()) {
            const nlohmann::json& attrs = data["attributes"];
            if (attrs.contains("name") && attrs["name"].is_string())
                info.holder = attrs["name"].get<std::string>();
            if (attrs.contains("expiry") && attrs["expiry"].is_string())
                info.expiryIso = attrs["expiry"].get<std::string>();
        }
    }

    // ---- 2) first activation on this machine
    if (code == "NO_MACHINE" && !info.licenseId.empty()) {
        const nlohmann::json machineBody = {
            { "data", {
                { "type", "machines" },
                { "attributes", {
                    { "fingerprint", fingerprint },
                    { "name", computerName() },
                    { "platform", "windows" },
                } },
                { "relationships", { { "license", { { "data", {
                    { "type", "licenses" }, { "id", info.licenseId },
                } } } } } },
            } },
        };
        const std::wstring licenseAuth = L"License " + std::wstring(key.begin(), key.end());
        if (!httpPost(kApiHost, kPathMachines, machineBody.dump(), licenseAuth, body, status, err)) {
            info.networkError = true;
            info.code = "NETWORK";
            return offlineFallback(info, key);
        }

        if (status < 200 || status >= 300) {
            std::string detail;
            if (nlohmann::json e = nlohmann::json::parse(body, nullptr, false);
                !e.is_discarded() && e.contains("errors") && e["errors"].is_array() && !e["errors"].empty()) {
                const nlohmann::json& first = e["errors"][0];
                if (first.contains("code") && first["code"].is_string())
                    detail = first["code"].get<std::string>();
                if (first.contains("detail") && first["detail"].is_string()) {
                    if (!detail.empty())
                        detail += " - ";
                    detail += first["detail"].get<std::string>();
                }
            }
            info.detail = detail;
            info.code = detail.find("TOO_MANY_MACHINES") != std::string::npos ? "TOO_MANY_MACHINES"
                                                                              : "ACTIVATION_FAILED";
            info.userMessage = userMessageForCode(info.code, detail);
            return info;
        }

        // ---- 3) re-validate now that the machine exists
        if (!httpPost(kApiHost, kPathValidateKey, validateBody.dump(), L"", body, status, err)) {
            info.networkError = true;
            info.code = "NETWORK";
            return offlineFallback(info, key);
        }
        j = nlohmann::json::parse(body, nullptr, false);
        if (j.is_discarded() || !j.contains("meta")) {
            info.code = "PROTOCOL";
            info.userMessage = "Unexpected server response";
            return info;
        }
        code = j["meta"].is_object() && j["meta"].contains("code") && j["meta"]["code"].is_string()
                   ? j["meta"]["code"].get<std::string>() : "";
        if (j["meta"].is_object() && j["meta"].contains("detail") && j["meta"]["detail"].is_string())
            info.detail = j["meta"]["detail"].get<std::string>();
    }

    info.code = code;
    info.valid = (code == "VALID");
    if (info.valid) {
        info.daysLeft = daysUntilIso(info.expiryIso);
        info.userMessage = userMessageForCode("VALID", "");

        // Phase 3: a signed offline token is fetched on every successful
        // check; the core and the offline path verify it without the network.
        std::string token = fetchLicenseToken(key, fingerprint);
        if (token.empty()) {
            if (const auto previous = loadLicenseRecord(); previous && previous->key == key)
                token = previous->token;
        }

        LicenseRecord record;
        record.key = key;
        record.lastValidAtUnix = nowUnix();
        record.expiryIso = info.expiryIso;
        record.token = token;
        saveLicenseRecord(record);
    } else {
        info.userMessage = userMessageForCode(code, info.detail);
    }
    return info;
}

// ============================================================ http

bool LicenseClient::httpPost(const wchar_t* host, const std::wstring& path, const std::string& body,
                             const std::wstring& authorization, std::string& outBody,
                             long& outStatus, std::string& outError)
{
    outBody.clear();
    outStatus = 0;
    outError.clear();

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        outError = "WinHttpOpen";
        return false;
    }
    WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    HINTERNET connect = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;

    bool ok = false;
    if (request) {
        std::wstring headers =
            L"Content-Type: application/vnd.api+json\r\n"
            L"Accept: application/vnd.api+json\r\n";
        if (!authorization.empty())
            headers += L"Authorization: " + authorization + L"\r\n";

        if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
                               const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                               static_cast<DWORD>(body.size()), 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, statusSize = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                WINHTTP_NO_HEADER_INDEX);
            outStatus = static_cast<long>(status);

            std::string raw;
            for (;;) {
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
            outBody = std::move(raw);
            ok = true;
        } else {
            outError = "WinHttp request failed";
        }
        WinHttpCloseHandle(request);
    } else {
        outError = "WinHttp connect failed";
    }
    if (connect)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

// ============================================================ record storage

std::optional<LicenseRecord> loadLicenseRecord()
{
    const std::filesystem::path path = recordPath();
    if (path.empty())
        return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::vector<char> blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    if (blob.empty())
        return std::nullopt;

    DATA_BLOB inBlob{ static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(blob.data()) };
    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &outBlob))
        return std::nullopt;

    const std::string text(reinterpret_cast<char*>(outBlob.pbData), outBlob.cbData);
    LocalFree(outBlob.pbData);

    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded())
        return std::nullopt;

    LicenseRecord record;
    if (j.contains("key") && j["key"].is_string())
        record.key = j["key"].get<std::string>();
    if (j.contains("lastValidAt") && j["lastValidAt"].is_number_integer())
        record.lastValidAtUnix = j["lastValidAt"].get<int64_t>();
    if (j.contains("expiry") && j["expiry"].is_string())
        record.expiryIso = j["expiry"].get<std::string>();
    if (j.contains("token") && j["token"].is_string())
        record.token = j["token"].get<std::string>();

    if (record.key.empty())
        return std::nullopt;
    return record;
}

bool saveLicenseRecord(const LicenseRecord& record)
{
    const std::filesystem::path path = recordPath();
    if (path.empty())
        return false;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    const nlohmann::json j = {
        { "key", record.key },
        { "lastValidAt", record.lastValidAtUnix },
        { "expiry", record.expiryIso },
        { "token", record.token },
    };
    const std::string text = j.dump();

    DATA_BLOB inBlob{ static_cast<DWORD>(text.size()),
                      reinterpret_cast<BYTE*>(const_cast<char*>(text.data())) };
    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, L"RuntimeHost license", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &outBlob))
        return false;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    bool ok = false;
    if (out) {
        out.write(reinterpret_cast<const char*>(outBlob.pbData), outBlob.cbData);
        ok = out.good();
    }
    LocalFree(outBlob.pbData);
    return ok;
}

void clearLicenseRecord()
{
    const std::filesystem::path path = recordPath();
    if (path.empty())
        return;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

std::string LicenseClient::fetchLicenseToken(const std::string& key, const std::string& fingerprint)
{
    const nlohmann::json body = { { "key", key }, { "fp", fingerprint } };
    std::string out, err;
    long status = 0;
    if (!httpPost(kUpdateHost, kPathLicenseToken, body.dump(), L"", out, status, err) || status != 200)
        return {};
    const nlohmann::json j = nlohmann::json::parse(out, nullptr, false);
    if (j.is_discarded() || !j.contains("token") || !j["token"].is_string())
        return {};
    return j["token"].get<std::string>();
}

} // namespace dk
