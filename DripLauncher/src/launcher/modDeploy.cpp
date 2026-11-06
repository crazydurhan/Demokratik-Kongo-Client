#include "modDeploy.h"

#include "modJarData.h"
#include "stealthNames.h"
#include "util.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

/*
================================================================================
    Drip Launcher - Lunar Client loader deployment
--------------------------------------------------------------------------------
    The launcher no longer injects anything. It downloads the core DLL (from
    the update service, license-gated), writes its path into
    %APPDATA%\RuntimeHost\load\current.txt and drops a small Forge 1.8.9
    loader mod into the Lunar Client mods folder
    (<user>\.lunarclient\profiles\1.8\mods\forge-1.8.9). The game loads the
    mod on its own; the mod System.load()s the core. No cross-process work,
    nothing for behavioural AV heuristics to flag.
================================================================================
*/

namespace dk {

namespace {

std::wstring envVar(const wchar_t* name)
{
    wchar_t buf[32768];
    const DWORD n = GetEnvironmentVariableW(name, buf, 32768);
    if (n == 0 || n >= 32768)
        return L"";
    return std::wstring(buf, n);
}

std::wstring changesDir()
{
    const std::wstring appdata = envVar(L"APPDATA");
    if (appdata.empty())
        return L"";
    return appdata + L"\\" + stealth::kAppFolder;
}

uint64_t fnv1a64(const void* data, size_t len)
{
    constexpr uint64_t kOffset = 14695981039346656037ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = kOffset;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }
    return hash;
}

uint64_t fileSizeBytes(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
        return 0;
    ULARGE_INTEGER sz{};
    sz.LowPart = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    return sz.QuadPart;
}

// Write to .tmp then atomic rename, like a normal updater.
bool writeFileAtomic(const std::wstring& path, const void* data, DWORD size)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    const std::wstring tmp = path + L".tmp";

    HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);

    if (!ok || written != size) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }

    return true;
}

std::string readFileTailUtf8(const std::wstring& path, size_t maxBytes)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0)
        return {};

    const std::streamoff start = size > static_cast<std::streamoff>(maxBytes)
                                     ? size - static_cast<std::streamoff>(maxBytes)
                                     : 0;
    in.seekg(start, std::ios::beg);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool isLoaderJarName(const std::wstring& name)
{
    constexpr wchar_t kPrefix[] = L"runtimehost-mod-";
    return name.rfind(kPrefix, 0) == 0 && name.size() > 4 &&
           name.compare(name.size() - 4, 4, L".jar") == 0;
}

} // namespace

LunarStatus detectLunar()
{
    LunarStatus st;

    const std::wstring home = envVar(L"USERPROFILE");
    if (!home.empty()) {
        const std::wstring lunarRoot = home + L"\\.lunarclient";
        st.lunarFound = GetFileAttributesW(lunarRoot.c_str()) != INVALID_FILE_ATTRIBUTES;
        st.modsDir = lunarRoot + L"\\profiles\\1.8\\mods\\forge-1.8.9";
    }

    if (st.lunarFound && !st.modsDir.empty()) {
        std::error_code ec;
        std::filesystem::directory_iterator it(st.modsDir, ec), end;
        if (!ec) {
            for (; it != end; it = it.increment(ec)) {
                if (ec)
                    break;
                const std::wstring name = it->path().filename().wstring();
                if (isLoaderJarName(name)) {
                    st.modDeployed = true;
                    st.modJarName = wideToUtf8(name);
                    break;
                }
            }
        }
    }

    const auto cached = newestCachedCore(Edition::Legacy);
    st.coreSha12 = cached.second;

    const std::string tail = readFileTailUtf8(changesDir() + L"\\mod-loader.log", 8192);
    st.coreLoadedLastSession = tail.find("core loaded") != std::string::npos;

    const std::wstring local = envVar(L"LOCALAPPDATA");
    if (!local.empty()) {
        const std::wstring exe = local + L"\\Programs\\Lunar Client\\Lunar Client.exe";
        if (GetFileAttributesW(exe.c_str()) != INVALID_FILE_ATTRIBUTES)
            st.lunarExe = exe;
    }

    return st;
}

std::vector<uint8_t> embeddedModJarBytes()
{
    return decodeBase64(modjar::kModJarBase64());
}

DeployResult writeCorePointer(const std::wstring& coreDllPath)
{
    DeployResult r;
    if (coreDllPath.empty()) {
        r.error = "empty core path";
        return r;
    }

    const std::wstring dir = changesDir() + L"\\load";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::wstring path = dir + L"\\current.txt";
    const std::string text = wideToUtf8(coreDllPath);
    if (!writeFileAtomic(path, text.data(), static_cast<DWORD>(text.size()))) {
        r.error = "failed to write current.txt: " + lastErrorString();
        return r;
    }

    r.ok = true;
    r.detail = text;
    return r;
}

DeployResult deployLoaderMod()
{
    DeployResult r;

    const std::vector<uint8_t> jar = embeddedModJarBytes();
    if (jar.empty()) {
        r.error = "embedded loader mod missing (run resources/build-mod.ps1)";
        return r;
    }

    const LunarStatus st = detectLunar();
    if (!st.lunarFound) {
        r.error = "Lunar Client installation not found";
        return r;
    }

    std::error_code ec;
    std::filesystem::create_directories(st.modsDir, ec);
    if (ec) {
        r.error = "cannot create mods folder: " + ec.message();
        return r;
    }

    char hex[17] = {};
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(fnv1a64(jar.data(), jar.size())));
    std::wstring name = L"runtimehost-mod-";
    for (const char* c = hex; *c; ++c)
        name += static_cast<wchar_t>(*c);
    name += L".jar";
    const std::wstring path = st.modsDir + L"\\" + name;

    // Prune older loader copies. Files currently loaded by a running game are
    // locked; those are silently skipped and removed on the next run.
    {
        std::vector<std::wstring> stale;
        std::filesystem::directory_iterator it(st.modsDir, ec), end;
        if (!ec) {
            for (; it != end; it = it.increment(ec)) {
                if (ec)
                    break;
                const std::wstring other = it->path().filename().wstring();
                if (isLoaderJarName(other) && other != name)
                    stale.push_back(other);
            }
        }
        for (const std::wstring& old : stale) {
            DeleteFileW((st.modsDir + L"\\" + old).c_str());
        }
    }

    const bool present = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!present || fileSizeBytes(path) != jar.size()) {
        if (!writeFileAtomic(path, jar.data(), static_cast<DWORD>(jar.size()))) {
            r.error = "failed to write loader mod: " + lastErrorString();
            return r;
        }
    }

    r.ok = true;
    r.detail = wideToUtf8(name) + " (" + std::to_string(jar.size()) + " bytes)";
    return r;
}

std::pair<std::wstring, std::string> newestCachedCore(Edition edition)
{
    std::pair<std::wstring, std::string> best;
    std::filesystem::file_time_type bestTime{};

    const std::wstring dir = changesDir() + L"\\update";
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    if (ec)
        return best;

    const std::wstring prefix = std::wstring(L"RuntimeHostCore-") + editionCacheTag(edition) + L"-";
    const size_t kPrefixLen = prefix.size();
    for (; it != end; it = it.increment(ec)) {
        if (ec)
            break;
        const std::wstring name = it->path().filename().wstring();
        if (name.rfind(prefix, 0) != 0 || name.size() <= kPrefixLen + 4)
            continue;
        if (name.compare(name.size() - 4, 4, L".dll") != 0)
            continue;

        const std::wstring sha = name.substr(kPrefixLen, name.size() - kPrefixLen - 4);
        const auto t = it->last_write_time(ec);
        if (ec)
            continue;
        if (best.first.empty() || t > bestTime) {
            best.first = it->path().wstring();
            best.second = wideToUtf8(sha);
            bestTime = t;
        }
    }
    return best;
}

bool launchLunarClient()
{
    const LunarStatus st = detectLunar();
    if (st.lunarExe.empty())
        return false;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", st.lunarExe.c_str(),
                                                   nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

} // namespace dk
