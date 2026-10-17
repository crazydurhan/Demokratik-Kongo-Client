#include "process_scanner.h"
#include "logger.h"
#include "util.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "version.lib")

namespace dk {

namespace {

bool icontains(const std::wstring& hay, const std::wstring& needle)
{
    if (needle.empty())
        return true;
    if (hay.size() < needle.size())
        return false;

    std::wstring h = hay;
    std::wstring n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::towlower);
    std::transform(n.begin(), n.end(), n.begin(), ::towlower);
    return h.find(n) != std::wstring::npos;
}

bool isJvmHostProcess(const std::wstring& exe)
{
    return icontains(exe, L"javaw.exe") || icontains(exe, L"java.exe") ||
           icontains(exe, L"lunarclient.exe") || icontains(exe, L"minecraft.exe");
}

bool isJavaRuntimeDescription(const std::wstring& desc)
{
    return icontains(desc, L"java") || icontains(desc, L"openjdk") ||
           icontains(desc, L"hotspot") || icontains(desc, L"jre") ||
           icontains(desc, L"jdk");
}

bool isGameLikeText(const std::wstring& text)
{
    static const wchar_t* kTokens[] = {
        L"minecraft", L"muzcraft", L"lunar", L"badlion", L"feather",
        L"labymod", L"laby", L"prism", L"multimc", L"polymc", L"optifine",
        L"forge", L"fabric", L"1.8.9", L"1.8.8", L"lwjgl", L"blc",
        L"genesis", L"featherclient",
    };
    for (const wchar_t* token : kTokens) {
        if (icontains(text, token))
            return true;
    }
    return false;
}

std::wstring queryFileDescription(const std::wstring& path)
{
    if (path.empty())
        return L"";

    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size)
        return L"";

    std::vector<wchar_t> buf(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buf.data()))
        return L"";

    struct LangAndCodePage {
        WORD language;
        WORD codePage;
    };

    LangAndCodePage* translate = nullptr;
    UINT translateBytes = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<void**>(&translate), &translateBytes) ||
        !translate || translateBytes < sizeof(LangAndCodePage)) {
        return L"";
    }

    wchar_t subBlock[64] = {};
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileDescription",
               translate[0].language, translate[0].codePage);

    wchar_t* desc = nullptr;
    UINT descLen = 0;
    if (!VerQueryValueW(buf.data(), subBlock, reinterpret_cast<void**>(&desc), &descLen) || !desc)
        return L"";
    return desc;
}

struct EnumWindowCtx {
    std::unordered_map<DWORD, std::wstring>* titles;
};

BOOL CALLBACK enumWinProc(HWND hwnd, LPARAM lp)
{
    auto* titles = reinterpret_cast<EnumWindowCtx*>(lp)->titles;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return TRUE;

    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 511);
    if (title[0] == 0)
        return TRUE;

    // Prefer the longest visible title; fall back to any non-empty title
    // so protected / tool windows still surface (e.g. "MuzCraft Client").
    const bool visible = IsWindowVisible(hwnd) != FALSE;
    auto it = titles->find(pid);
    if (it == titles->end()) {
        if (visible || isGameLikeText(title))
            (*titles)[pid] = title;
        return TRUE;
    }
    if (visible && wcslen(title) > it->second.size())
        it->second = title;
    return TRUE;
}

std::unordered_map<DWORD, std::wstring> collectWindowTitles()
{
    std::unordered_map<DWORD, std::wstring> titles;
    EnumWindowCtx ctx{ &titles };
    EnumWindows(enumWinProc, reinterpret_cast<LPARAM>(&ctx));
    return titles;
}

} // namespace

std::wstring ProcessScanner::readCommandLine(HANDLE hProc)
{
    PROCESS_BASIC_INFORMATION pbi{};
    ULONG ret = 0;
    if (NtQueryInformationProcess(hProc, ProcessBasicInformation, &pbi, sizeof(pbi), &ret) != 0)
        return L"";
    if (!pbi.PebBaseAddress)
        return L"";

    PEB peb{};
    if (!ReadProcessMemory(hProc, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr))
        return L"";

    RTL_USER_PROCESS_PARAMETERS rupp{};
    if (!ReadProcessMemory(hProc, peb.ProcessParameters, &rupp, sizeof(rupp), nullptr))
        return L"";

    if (!rupp.CommandLine.Buffer || rupp.CommandLine.Length == 0)
        return L"";

    std::wstring out(rupp.CommandLine.Length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(hProc, rupp.CommandLine.Buffer, out.data(), rupp.CommandLine.Length, nullptr))
        return L"";
    return out;
}

LauncherKind ProcessScanner::classify(const McProcess& p, const std::wstring& cmdLine)
{
    if (icontains(p.windowTitle, L"Badlion") ||
        icontains(p.exePath, L"badlion") ||
        icontains(p.exePath, L"BLClient") ||
        icontains(cmdLine, L"badlion") ||
        icontains(cmdLine, L"BLClient") ||
        icontains(cmdLine, L"net.badlion")) {
        return LauncherKind::Badlion;
    }

    if (icontains(p.windowTitle, L"Lunar") ||
        icontains(p.exePath, L"lunarclient") ||
        icontains(cmdLine, L"lunarclient") ||
        icontains(cmdLine, L"com.moonsworth") ||
        icontains(cmdLine, L"genesisclient")) {
        return LauncherKind::Lunar;
    }

    if (icontains(p.windowTitle, L"MuzCraft") ||
        icontains(p.exePath, L"muzcraft") ||
        icontains(p.exeName, L"muzcraft") ||
        icontains(cmdLine, L"muzcraft") ||
        icontains(p.windowTitle, L"Feather") ||
        icontains(p.exePath, L"feather") ||
        icontains(cmdLine, L"featherclient") ||
        icontains(p.windowTitle, L"Laby") ||
        icontains(cmdLine, L"labymod")) {
        return LauncherKind::Custom;
    }

    if (icontains(cmdLine, L"net.minecraftforge") || icontains(cmdLine, L"forge"))
        return LauncherKind::Forge;
    if (icontains(cmdLine, L"net.fabricmc") || icontains(cmdLine, L"fabric-loader"))
        return LauncherKind::Fabric;
    if (icontains(cmdLine, L"optifine"))
        return LauncherKind::OptiFine;
    if (icontains(cmdLine, L"net.minecraft") || icontains(p.windowTitle, L"Minecraft"))
        return LauncherKind::Vanilla;

    return LauncherKind::Unknown;
}

bool ProcessScanner::inspectProcess(DWORD pid, const std::wstring& snapExe,
                                    const std::wstring& knownTitle, McProcess& out)
{
    out.pid = pid;
    out.exeName = snapExe;
    out.windowTitle = knownTitle;

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h)
        h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!h) {
        // ACL / protection can deny OpenProcess. Still list the PID when the
        // window title or snapshot name looks like a Minecraft client.
        const bool accept = isGameLikeText(knownTitle) ||
                            (isJvmHostProcess(snapExe) && isGameLikeText(snapExe));
        if (accept) {
            out.launcher = classify(out, L"");
            LauncherLog::I().debug(
                "PID " + std::to_string(pid) + " listed without OpenProcess (" +
                wideToUtf8(snapExe) + " / \"" + wideToUtf8(knownTitle) + "\")");
        }
        return accept;
    }

    wchar_t imageName[MAX_PATH] = {};
    DWORD imageNameSize = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, imageName, &imageNameSize)) {
        out.exePath = imageName;
        const auto pos = out.exePath.find_last_of(L"\\/");
        out.exeName = (pos == std::wstring::npos) ? out.exePath : out.exePath.substr(pos + 1);
    } else if (out.exeName.empty()) {
        CloseHandle(h);
        return isGameLikeText(knownTitle);
    }

    const std::wstring fileDesc = queryFileDescription(out.exePath);
    const bool javaHost = isJvmHostProcess(out.exeName) || isJavaRuntimeDescription(fileDesc);

    BOOL isWow64 = FALSE;
    if (IsWow64Process(h, &isWow64)) {
        out.x64 = !isWow64;
    } else {
        // Could not determine bitness — do not silently default to x64.
        out.x64Unknown = true;
        out.x64 = false;
        LauncherLog::I().debug("PID " + std::to_string(pid) +
                               ": IsWow64Process failed — bitness unknown (" + lastErrorString() + ")");
    }

    HMODULE mods[1024];
    DWORD needed = 0;
    if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        const DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; ++i) {
            wchar_t modName[MAX_PATH] = {};
            if (GetModuleBaseNameW(h, mods[i], modName, MAX_PATH)) {
                const std::wstring m = modName;
                if (icontains(m, L"jvm.dll"))
                    out.hasJvm = true;
                if (icontains(m, L"lwjgl"))
                    out.hasLwjgl = true;
            }
        }
    }

    if (out.windowTitle.empty())
        out.windowTitle = knownTitle;

    const std::wstring cmd = readCommandLine(h);
    out.launcher = classify(out, cmd);
    CloseHandle(h);

    const bool gameText =
        isGameLikeText(out.windowTitle) ||
        isGameLikeText(out.exePath) ||
        isGameLikeText(out.exeName) ||
        isGameLikeText(cmd);

    // Do not require jvm.dll visibility: some clients block module enumeration.
    const bool looksLikeMc =
        (out.hasJvm && (out.hasLwjgl || out.launcher != LauncherKind::Unknown || gameText)) ||
        (javaHost && (out.hasLwjgl || out.launcher != LauncherKind::Unknown || gameText)) ||
        (javaHost && !out.windowTitle.empty() && gameText) ||
        gameText;

    return looksLikeMc;
}

std::vector<McProcess> ProcessScanner::scan()
{
    std::vector<McProcess> result;
    std::unordered_set<DWORD> seen;
    const auto titles = collectWindowTitles();

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{ sizeof(pe) };
        if (Process32FirstW(snap, &pe)) {
            do {
                const DWORD pid = pe.th32ProcessID;
                const std::wstring snapExe = pe.szExeFile;
                std::wstring title;
                if (auto it = titles.find(pid); it != titles.end())
                    title = it->second;

                const bool candidate =
                    isJvmHostProcess(snapExe) ||
                    isGameLikeText(snapExe) ||
                    isGameLikeText(title);
                if (!candidate)
                    continue;

                McProcess mc;
                if (inspectProcess(pid, snapExe, title, mc)) {
                    result.push_back(std::move(mc));
                    seen.insert(pid);
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    // Window-first fallback: custom clients (MuzCraft) may rename the JVM
    // binary so the snapshot exe filter misses them. The visible title still
    // maps to a PID via EnumWindows — no OpenProcess required for discovery.
    for (const auto& [pid, title] : titles) {
        if (seen.count(pid) || !isGameLikeText(title))
            continue;

        McProcess mc;
        if (inspectProcess(pid, L"", title, mc)) {
            result.push_back(std::move(mc));
            seen.insert(pid);
        }
    }

    return result;
}

const char* ProcessScanner::launcherName(LauncherKind k) const
{
    switch (k) {
    case LauncherKind::Vanilla:  return "Vanilla";
    case LauncherKind::Forge:    return "Forge";
    case LauncherKind::Fabric:   return "Fabric";
    case LauncherKind::Lunar:    return "Lunar";
    case LauncherKind::Badlion:  return "Badlion";
    case LauncherKind::OptiFine: return "OptiFine";
    case LauncherKind::Custom:   return "Custom";
    default:                     return "Unknown";
    }
}

namespace {

// First whitespace-delimited token of a Windows command line, honoring
// quoted paths ("C:\Program Files\...\java.exe" -Xmx2G ...).
std::wstring firstCommandLineToken(const std::wstring& cmd)
{
    size_t i = 0;
    while (i < cmd.size() && (cmd[i] == L' ' || cmd[i] == L'\t'))
        ++i;

    if (i >= cmd.size())
        return L"";

    if (cmd[i] == L'"') {
        const size_t end = cmd.find(L'"', i + 1);
        if (end == std::wstring::npos)
            return cmd.substr(i + 1);
        return cmd.substr(i + 1, end - i - 1);
    }

    size_t end = i;
    while (end < cmd.size() && cmd[end] != L' ' && cmd[end] != L'\t')
        ++end;
    return cmd.substr(i, end - i);
}

bool looksLikeJavaExePath(const std::wstring& path)
{
    if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;
    return icontains(path, L"java.exe") || icontains(path, L"javaw.exe");
}

std::wstring javaFromRegistry()
{
    // HKCU\Software\JavaSoft\Java Runtime Environment / Java Development Kit
    // "JavaHome" value — covers Oracle/OpenJDK installers.
    static const wchar_t* kRoots[] = {
        L"Software\\JavaSoft\\Java Runtime Environment",
        L"Software\\JavaSoft\\Java Development Kit",
    };

    for (const wchar_t* root : kRoots) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, root, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;

        wchar_t version[64] = {};
        DWORD versionLen = sizeof(version);
        if (RegQueryValueExW(key, L"CurrentVersion", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(version), &versionLen) == ERROR_SUCCESS) {
            std::wstring sub = std::wstring(root) + L"\\" + version;
            HKEY subKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &subKey) == ERROR_SUCCESS) {
                wchar_t home[MAX_PATH] = {};
                DWORD homeLen = sizeof(home);
                if (RegQueryValueExW(subKey, L"JavaHome", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(home), &homeLen) == ERROR_SUCCESS) {
                    for (const wchar_t* name : { L"\\bin\\java.exe", L"\\bin\\javaw.exe" }) {
                        const std::wstring candidate = std::wstring(home) + name;
                        if (looksLikeJavaExePath(candidate)) {
                            RegCloseKey(subKey);
                            RegCloseKey(key);
                            return candidate;
                        }
                    }
                }
                RegCloseKey(subKey);
            }
        }
        RegCloseKey(key);
    }
    return L"";
}

std::wstring javaFromEnvironment()
{
    wchar_t* value = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&value, &len, L"JAVA_HOME") != 0 || !value)
        return L"";

    const std::wstring home = value;
    free(value);

    for (const wchar_t* name : { L"\\java.exe", L"\\javaw.exe" }) {
        const std::wstring candidate = home + L"\\bin" + name;
        if (looksLikeJavaExePath(candidate))
            return candidate;
    }
    return L"";
}

std::wstring javaFromPath()
{
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD len = GetEnvironmentVariableW(L"PATH", buf, MAX_PATH * 4);
    if (len == 0 || len >= MAX_PATH * 4)
        return L"";

    std::wstring path = buf;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(L';', start);
        if (end == std::wstring::npos)
            end = path.size();

        std::wstring dir = path.substr(start, end - start);
        if (!dir.empty()) {
            std::wstring candidate = dir + L"\\javaw.exe";
            if (looksLikeJavaExePath(candidate))
                return candidate;
        }
        start = end + 1;
    }
    return L"";
}

} // namespace

std::wstring findJavaExeForTarget(DWORD pid)
{
    // 1) The target's own java.exe: read the command line from its PEB.
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!h)
            h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

        if (h) {
            std::wstring cmd;
            PROCESS_BASIC_INFORMATION pbi{};
            ULONG ret = 0;
            if (NtQueryInformationProcess(h, ProcessBasicInformation, &pbi, sizeof(pbi), &ret) == 0 &&
                pbi.PebBaseAddress) {
                PEB peb{};
                if (ReadProcessMemory(h, pbi.PebBaseAddress, &peb, sizeof(peb), nullptr)) {
                    RTL_USER_PROCESS_PARAMETERS rupp{};
                    if (ReadProcessMemory(h, peb.ProcessParameters, &rupp, sizeof(rupp), nullptr) &&
                        rupp.CommandLine.Buffer && rupp.CommandLine.Length > 0) {
                        const size_t chars = rupp.CommandLine.Length / sizeof(wchar_t);
                        std::wstring out(chars, L'\0');
                        if (ReadProcessMemory(h, rupp.CommandLine.Buffer, out.data(),
                                              rupp.CommandLine.Length, nullptr))
                            cmd = out;
                    }
                }
            }
            CloseHandle(h);

            const std::wstring exe = firstCommandLineToken(cmd);
            if (looksLikeJavaExePath(exe))
                return exe;

            // Custom launchers (Lunar etc.) rename java.exe: if the target's
            // own image is a JVM host, reuse its directory's java/javaw.
            const size_t slash = exe.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                const std::wstring dir = exe.substr(0, slash);
                for (const wchar_t* name : { L"\\java.exe", L"\\javaw.exe" }) {
                    const std::wstring candidate = dir + name;
                    if (looksLikeJavaExePath(candidate))
                        return candidate;
                }
            }
        }
    }

    // 2) Well-known JDK locations.
    const std::wstring fromEnv = javaFromEnvironment();
    if (!fromEnv.empty())
        return fromEnv;

    const std::wstring fromRegistry = javaFromRegistry();
    if (!fromRegistry.empty())
        return fromRegistry;

    return javaFromPath();
}

namespace {

// A java that can run `com.sun.tools.attach.VirtualMachine`: full JDKs carry
// jmods\jdk.attach.jmod (9+) or lib\tools.jar (8). JRE images (e.g. the Zulu
// JREs Lunar bundles) do not - attaching through one fails with
// ClassNotFoundException, which used to push every Lunar inject onto the
// classic remote-thread route (and its Defender behavioural flag).
bool isAttachCapableJava(const std::wstring& javaExe)
{
    const size_t slash = javaExe.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return false;
    const std::wstring binDir = javaExe.substr(0, slash);      // ...\<jdk>\bin
    const size_t slash2 = binDir.find_last_of(L"\\/");
    if (slash2 == std::wstring::npos)
        return false;
    const std::wstring home = binDir.substr(0, slash2);        // ...\<jdk>

    if (GetFileAttributesW((home + L"\\jmods\\jdk.attach.jmod").c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;
    return GetFileAttributesW((home + L"\\lib\\tools.jar").c_str()) != INVALID_FILE_ATTRIBUTES;
}

void pushUniquePath(std::vector<std::wstring>& out, const std::wstring& path)
{
    if (path.empty())
        return;
    std::wstring lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    for (const auto& e : out) {
        std::wstring el = e;
        std::transform(el.begin(), el.end(), el.begin(), ::towlower);
        if (el == lower)
            return;
    }
    out.push_back(path);
}

// HKLM JavaSoft\JDK\<version>\JavaHome - covers machine-wide installers that
// never touch the user's JAVA_HOME (javaFromRegistry reads HKCU only).
void appendRegistryJdks(std::vector<std::wstring>& out)
{
    static const wchar_t* kFamilies[] = { L"JDK", L"Java Development Kit" };
    for (const wchar_t* family : kFamilies) {
        const std::wstring base = std::wstring(L"SOFTWARE\\JavaSoft\\") + family;
        HKEY fam = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, base.c_str(), 0,
                          KEY_READ | KEY_WOW64_64KEY, &fam) != ERROR_SUCCESS)
            continue;

        wchar_t version[64] = {};
        DWORD index = 0;
        for (;;) {
            DWORD versionLen = sizeof(version);
            if (RegEnumKeyExW(fam, index++, version, &versionLen, nullptr, nullptr, nullptr,
                              nullptr) != ERROR_SUCCESS)
                break;
            HKEY key = nullptr;
            if (RegOpenKeyExW(fam, version, 0, KEY_READ, &key) == ERROR_SUCCESS) {
                wchar_t home[MAX_PATH] = {};
                DWORD homeLen = sizeof(home);
                if (RegQueryValueExW(key, L"JavaHome", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(home), &homeLen) == ERROR_SUCCESS)
                    pushUniquePath(out, std::wstring(home) + L"\\bin\\javaw.exe");
                RegCloseKey(key);
            }
        }
        RegCloseKey(fam);
    }
}

// One level (and one extra for vendor subfolders like Microsoft\jdk-*) under
// the common install roots.
void appendCommonInstallRoots(std::vector<std::wstring>& out)
{
    static const wchar_t* kRoots[] = {
        L"C:\\Program Files\\Eclipse Adoptium",
        L"C:\\Program Files\\Java",
        L"C:\\Program Files\\Zulu",
        L"C:\\Program Files\\Amazon Corretto",
        L"C:\\Program Files\\BellSoft",
        L"C:\\Program Files\\Semeru",
        L"C:\\Program Files\\OpenJDK",
        L"C:\\Program Files\\Microsoft",
    };

    for (const wchar_t* root : kRoots) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((std::wstring(root) + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            const std::wstring sub = std::wstring(root) + L"\\" + fd.cFileName;
            pushUniquePath(out, sub + L"\\bin\\javaw.exe");

            WIN32_FIND_DATAW fd2{};
            HANDLE h2 = FindFirstFileW((sub + L"\\*").c_str(), &fd2);
            if (h2 != INVALID_HANDLE_VALUE) {
                do {
                    if (fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                        pushUniquePath(out, sub + L"\\" + fd2.cFileName + L"\\bin\\javaw.exe");
                } while (FindNextFileW(h2, &fd2));
                FindClose(h2);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

} // namespace

std::vector<std::wstring> collectAttachJavaExeCandidates(DWORD pid)
{
    std::vector<std::wstring> out;

    pushUniquePath(out, javaFromEnvironment());   // JAVA_HOME
    pushUniquePath(out, javaFromRegistry());      // HKCU JavaSoft (installers)
    appendRegistryJdks(out);                      // HKLM JavaSoft\JDK\*
    pushUniquePath(out, javaFromPath());          // PATH
    appendCommonInstallRoots(out);                // Program Files roots
    pushUniquePath(out, findJavaExeForTarget(pid)); // target's own JRE, last

    std::vector<std::wstring> filtered;
    for (const auto& c : out) {
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES)
            filtered.push_back(c);
    }

    std::stable_sort(filtered.begin(), filtered.end(),
                     [](const std::wstring& a, const std::wstring& b) {
                         return isAttachCapableJava(a) && !isAttachCapableJava(b);
                     });
    return filtered;
}

} // namespace dk
