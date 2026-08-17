# AGENTS.md

Minecraft 1.8.9 injectable client. Two C++20 / MSVC v143 / x64 components:

- `DemokratikKongo/` — launcher EXE (ImGui + DX11 GUI) that injects the core DLL into a JVM process.
- `DemokratikKongoCore/` — injected DLL (JNI/JVMTI, MinHook, OpenGL overlay, ImGui ClickGUI, module system).

## Build

```powershell
.\setup.ps1 -Build      # or: .\setup.ps1 then .\build.ps1
```

- Requires Visual Studio 2022 (MSBuild found via vswhere). A JDK is optional: `setup.ps1` writes the JDK's `jvm.lib` path to git-ignored `local.props` if it finds one; otherwise the build links the vendored `DemokratikKongoCore\ext\jni\jvm.lib`.
- All third-party deps (imgui, minhook, jni, fonts, dxsdk, json) are vendored under `ext/` and committed — no `Örnekler/` folder or network needed.
- `Release|x64` only — the `.sln` maps only x64 (Core vcxproj also has Win32 configs, but the sln never builds them).

## Binaries are named RuntimeHost, NOT DemokratikKongo

`TargetName` in both `.vcxproj` files and `include/stealthNames.h` use stealth names:

- launcher → `RuntimeHost.exe`, core → `RuntimeHostCore.dll`
- window title/class, `%APPDATA%` folder, and config dir are all `RuntimeHost` (see `include/stealthNames.h`).
- Real config path: `%APPDATA%\RuntimeHost\*.json` (config.cpp uses `dk::stealth::kAppFolderUtf8`).

## Build order matters (embedded payload)

`DemokratikKongo/resources/payload.rc` embeds the built core DLL as an RCDATA resource:

```
IDR_PAYLOAD_DLL RCDATA "..\\..\\build\\RuntimeHostCore.dll"
```

Core's `OutDir` is `$(SolutionDir)build\`, so the core DLL must build BEFORE the launcher. The `.sln` declares this dependency and `build.ps1` builds the whole solution; if you build projects individually, build Core first.

## Generated mapping data — do not edit by hand

`DemokratikKongoCore/src/base/sdk/gen/vapeMappingData.{h,cpp}` is a committed
data table of Minecraft 1.8.9 obfuscation mappings. It is required at runtime
by the SDK's field/method resolver (`mappedRegistry.cpp`, `automap/`). The
extractor script that produced it was intentionally removed for the public
release, so treat this table as a frozen, hand-maintained blob.

## Module system

- Modules live in `DemokratikKongoCore/src/base/moduleManager/modules/<category>/`.
- Register new modules in `moduleManager.cpp` via `registerModule<T>()` (~line 160); names must be unique.
- Both `.vcxproj` files enumerate sources explicitly (no globbing) — a new `.cpp`/`.h` must also be added to the vcxproj or it will not compile.

## No tests / lint / CI

Build success is the only verification.

## Useful commands

- Headless inject (no GUI): `RuntimeHost.exe --inject <pid>` — exit 0 on success; logs to stdout.
- In-game ClickGUI opens with the **Insert** key.
