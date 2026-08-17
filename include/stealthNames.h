#pragma once

// System-facing identifiers kept generic so third-party launchers do not
// blacklist the process/window/path surface. UI branding stays separate.

namespace dk::stealth {

inline constexpr wchar_t kWindowTitle[] = L"Host Process for Windows Services";
inline constexpr wchar_t kWindowClass[] = L"AppFrameHostWindow";

inline constexpr wchar_t kAppFolder[] = L"RuntimeHost";
inline constexpr wchar_t kPayloadDll[] = L"RuntimeHostCore.dll";
inline constexpr wchar_t kLauncherExe[] = L"RuntimeHost.exe";

inline constexpr char kAppFolderUtf8[] = "RuntimeHost";
inline constexpr char kPayloadDllUtf8[] = "RuntimeHostCore.dll";
inline constexpr char kLauncherExeUtf8[] = "RuntimeHost.exe";

} // namespace dk::stealth
