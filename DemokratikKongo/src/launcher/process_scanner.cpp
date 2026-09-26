#include "process_scanner.h"
#include "logger.h"
#include "util.h"

#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
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

} // namespace dk
