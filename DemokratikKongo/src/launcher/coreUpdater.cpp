#include "coreUpdater.h"
#include "hashUtils.h"
#include "licenseClient.h"
#include "util.h"
#include "stealthNames.h"

#include <windows.h>
#include <winhttp.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include "json.hpp"

#pragma comment(lib, "winhttp.lib")

/*
================================================================================
    Demokratik Kongo launcher - server-hosted core updater
--------------------------------------------------------------------------------
    Downloads the currently active core DLL from the update service and caches
    it content-addressed (RuntimeHostCore-<sha12>.dll) under
    %APPDATA%\RuntimeHost\update\. Content addressing is deliberate: a DLL that
    is still loaded in a running game process is locked by Windows, and a new
    version must never be blocked by the old file.

    A failed fetch is never fatal - the caller falls back to the embedded
    payload, so an update outage cannot lock licensed users out.
================================================================================
*/

namespace dk {

namespace {

const wchar_t kUpdateHost[] = L"kongo.drip.invalid";
const wchar_t kUpdatePath[] = L"/up/dl/current";
const wchar_t kUserAgent[] = L"RuntimeHostLauncher/1.0";
constexpr int kTimeoutMs = 20000;
constexpr size_t kMaxCoreBytes = 128ull * 1024 * 1024;

std::filesystem::path updateDir()
{
    wchar_t buf[MAX_PATH]{};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};
    return std::filesystem::path(buf) / stealth::kAppFolder / L"update";
}

std::optional<std::string> queryHeaderString(HINTERNET request, const wchar_t* name)
{
    wchar_t buf[256]{};
    DWORD len = sizeof(buf);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, buf, &len, WINHTTP_NO_HEADER_INDEX))
        return std::nullopt;
    return wideToUtf8(buf);
}

} // namespace

CoreUpdateResult fetchLatestCore(const std::string& licenseKey)
{
    CoreUpdateResult result;

    const std::string key = normalizeLicenseKey(licenseKey);
    const std::string fingerprint = LicenseClient::hwid();
    if (key.empty() || fingerprint.empty()) {
        result.error = "no license key";
        return result;
    }

    const std::wstring query = std::wstring(kUpdatePath) + L"?key=" + std::wstring(key.begin(), key.end()) +
                               L"&fp=" + std::wstring(fingerprint.begin(), fingerprint.end());

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = "WinHttpOpen";
        return result;
    }
    WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    HINTERNET connect = WinHttpConnect(session, kUpdateHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"GET", query.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;

    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

        if (status == 200) {
            const auto expectedSha = queryHeaderString(request, L"X-DLL-SHA256");
            const auto serverName = queryHeaderString(request, L"X-DLL-NAME");

            std::string body;
            bool overflow = false;
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                    break;
                if (body.size() + available > kMaxCoreBytes) {
                    overflow = true;
                    break;
                }
                const size_t offset = body.size();
                body.resize(offset + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, body.data() + offset, available, &read))
                    break;
                body.resize(offset + read);
            }

            if (overflow) {
                result.error = "core download too large";
            } else if (!expectedSha) {
                result.error = "missing checksum header";
            } else if (body.size() < 1024) {
                result.error = "empty core download";
            } else {
                std::string wantSha = *expectedSha;
                for (char& c : wantSha) {
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c - 'A' + 'a');
                }
                const std::string actualSha = sha256HexOfBytes(body.data(), body.size());
                if (actualSha.empty() || actualSha != wantSha) {
                    result.error = "checksum mismatch";
                } else {
                    const std::filesystem::path dir = updateDir();
                    if (dir.empty()) {
                        result.error = "no update cache dir";
                    } else {
                        std::error_code ec;
                        std::filesystem::create_directories(dir, ec);

                        const std::string fileName = "RuntimeHostCore-" + actualSha.substr(0, 12) + ".dll";
                        const std::filesystem::path filePath = dir / fileName.c_str();

                        bool wrote = false;
                        if (std::filesystem::exists(filePath, ec)) {
                            wrote = true; // already downloaded by an earlier inject
                        } else {
                            std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
                            if (out) {
                                out.write(body.data(), static_cast<std::streamsize>(body.size()));
                                wrote = out.good();
                            }
                        }

                        if (!wrote) {
                            result.error = "cache write failed";
                        } else {
                            result.ok = true;
                            result.filePathW = filePath.wstring();
                            result.filePath = wideToUtf8(result.filePathW);
                            result.sha256Hex = actualSha;
                            result.name = serverName ? *serverName : fileName;
                            result.size = static_cast<int64_t>(body.size());
                        }
                    }
                }
            }
        } else if (status == 403) {
            result.error = "server refused (license check)";
        } else if (status == 404) {
            result.error = "no core published yet";
        } else {
            result.error = "http " + std::to_string(status);
        }
        WinHttpCloseHandle(request);
    } else {
        result.error = "request failed";
        if (request)
            WinHttpCloseHandle(request);
    }

    if (connect)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

std::string coreUpdateBaseUrl()
{
    return "https://" + wideToUtf8(kUpdateHost);
}

// ============================================================ launcher self-update

namespace {

std::wstring ownPathImpl()
{
    // Long-path aware: grow until GetModuleFileName stops truncating.
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return {};
        if (n < buf.size() - 1) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

bool httpGet(const std::wstring& pathAndQuery, std::string& outBody, long& outStatus)
{
    outBody.clear();
    outStatus = 0;

    HINTERNET session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return false;
    WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    HINTERNET connect = WinHttpConnect(session, kUpdateHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"GET", pathAndQuery.c_str(), nullptr, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
        : nullptr;

    bool ok = false;
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0, statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        outStatus = static_cast<long>(status);

        std::string raw;
        bool overflow = false;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                break;
            if (raw.size() + available > kMaxCoreBytes) {
                overflow = true;
                break;
            }
            const size_t offset = raw.size();
            raw.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(request, raw.data() + offset, available, &read))
                break;
            raw.resize(offset + read);
        }
        if (!overflow) {
            outBody = std::move(raw);
            ok = true;
        }
    }
    if (request)
        WinHttpCloseHandle(request);
    if (connect)
        WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

std::string lowerAscii(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

} // namespace

std::wstring ownExecutablePath()
{
    return ownPathImpl();
}

LauncherUpdateResult checkAndApplyLauncherUpdate()
{
    LauncherUpdateResult result;

    const std::wstring exePath = ownPathImpl();
    if (exePath.empty()) {
        result.message = "cannot resolve own path";
        return result;
    }

    // 1) metadata first: never download a multi-MB EXE just to compare
    std::string info;
    long status = 0;
    if (!httpGet(L"/up/dl/launcher/info", info, status)) {
        result.message = "launcher info unavailable";
        return result;
    }
    if (status == 404) {
        result.message = "no launcher published";
        return result;
    }
    if (status != 200) {
        result.message = "info http " + std::to_string(status);
        return result;
    }

    const nlohmann::json j = nlohmann::json::parse(info, nullptr, false);
    if (j.is_discarded() || !j.contains("sha256") || !j["sha256"].is_string()) {
        result.message = "bad info json";
        return result;
    }
    const std::string serverSha = lowerAscii(j["sha256"].get<std::string>());
    const std::string ownSha = sha256HexOfFile(std::filesystem::path(exePath));
    result.checked = true;

    if (ownSha.empty() || ownSha == serverSha) {
        result.sha256Hex = serverSha;
        result.message = "launcher up to date";
        return result;
    }

    // 2) new version: download and verify against the announced checksum
    std::string bytes;
    if (!httpGet(L"/up/dl/launcher", bytes, status) || status != 200) {
        result.message = "launcher download failed";
        return result;
    }
    if (bytes.size() < 1024 || sha256HexOfBytes(bytes.data(), bytes.size()) != serverSha) {
        result.message = "launcher checksum mismatch";
        return result;
    }

    // 3) stage content-addressed (an old .exe that is still running somewhere
    //    else can never block a new one)
    const std::filesystem::path dir = updateDir();
    if (dir.empty()) {
        result.message = "no update cache dir";
        return result;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::filesystem::path staged = dir / ("RuntimeHost-" + serverSha.substr(0, 12) + ".exe").c_str();
    if (!std::filesystem::exists(staged, ec)) {
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        if (!out) {
            result.message = "stage write failed";
            return result;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out.good()) {
            result.message = "stage write failed";
            return result;
        }
    }

    // 4) swap: rename the running EXE (allowed), move the new one in (its own
    //    name/path only, so it is not loaded), roll back if the second step
    //    fails so the user is never left without a working executable.
    const std::wstring oldPath = exePath + L".old";
    DeleteFileW(oldPath.c_str()); // stale leftover from an earlier update; ignore failure

    if (!MoveFileExW(exePath.c_str(), oldPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        result.message = "rename failed (" + std::to_string(GetLastError()) + ")";
        return result;
    }
    if (!MoveFileExW(staged.c_str(), exePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        MoveFileExW(oldPath.c_str(), exePath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
        result.message = "swap failed";
        return result;
    }

    result.updated = true;
    result.needsRestart = true;
    result.sha256Hex = serverSha;
    result.message = "launcher updated";
    return result;
}

} // namespace dk
