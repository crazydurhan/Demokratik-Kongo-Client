# AGENTS.md

Minecraft 1.8.9 injectable client. Two C++20 / MSVC / x64 components:

- `DemokratikKongo/` — launcher EXE (ImGui + DX11 GUI) that injects the core DLL into a JVM process.
- `DemokratikKongoCore/` — injected DLL (JNI/JVMTI, MinHook, OpenGL overlay, ImGui ClickGUI, module system).

## Injection: attach first, CRT fallback

`Injector::inject` tries two routes in order:

1. **jvmti-attach (preferred):** the launcher extracts a companion agent JAR
   next to the core DLL and spawns the target's own `java.exe` with
   `-jar RuntimeHostAgent.jar <pid> "<dllPath>"`. The agent uses the JDK
   Attach API (`VirtualMachine.attach/loadAgent`) to have the target VM load
   the JAR, and `agentmain` calls `System.load(coreDll)` inside the target.
   No remote thread, no LoadLibraryW, no loader-lock contention.
2. **nt-create-thread-ex (fallback):** the classic
   `VirtualAllocEx + WriteProcessMemory + NtCreateThreadEx(LoadLibraryW)`
   pipeline, used when the attach route is unavailable (no discoverable
   `java.exe`, `-XX:+DisableAttachMechanism`, x86 target, ...).

Both routes converge on the core's single bootstrap (`src/attachEntry.cpp`):
`DllMain` and `Agent_OnAttach` are exported and funnel into one guarded
`RunOnce` so the core can never double-initialize.

## Build

```powershell
.\setup.ps1 -Build      # or: .\setup.ps1 then .\build.ps1
```

- Requires MSVC (any recent Visual Studio; `MSBuild.exe` is found via vswhere). A JDK is optional: `setup.ps1` writes the JDK's `jvm.lib` path to git-ignored `local.props` if it finds one; otherwise the build links the vendored `DemokratikKongoCore\ext\jni\jvm.lib`.
- All third-party deps (imgui, minhook, jni, fonts, dxsdk, json) are vendored under `ext/` and committed — no `Örnekler/` folder or network needed.
- `Release|x64` only — the `.sln` maps only x64 (Core vcxproj also has Win32 configs, but the sln never builds them).
- On machines without the v143 (VS 2022) toolset, pass the installed toolset to MSBuild, e.g. `/p:PlatformToolset=v145` (VS 18 BuildTools ships v145).

## Companion agent JAR (generated — do not edit by hand)

`DemokratikKongo/src/launcher/agentJarData.h` embeds `RuntimeHostAgent.jar`
as base64. It is generated from `DemokratikKongo/resources/RuntimeHostAgent.java`:

```powershell
powershell -ExecutionPolicy Bypass -File DemokratikKongo\resources\build-agent.ps1
```

Re-run after ANY change to `RuntimeHostAgent.java`, then commit the header.
The script needs a JDK (`javac` + `jar` on PATH or `JAVA_HOME`); it compiles
with `-source 8 -target 8` so the agent runs on every JVM Minecraft 1.8.9
might use. The JAR is compiled once and embedded — the runtime never needs
javac.

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

## ClickGUI: vendored arifclient GUI

The in-game ClickGUI is a vendored copy of the standalone `arifclient`
ImGui ClickGUI (arifclient repo: `native/clickgui/`). Sync by porting
upstream diffs into the vendored tree — do not rewrite it in place:

- `DemokratikKongoCore/src/base/gui/arifgui/` — the GUI library
  (`include/arif/gui/*.hpp` + `src/*.cpp`; upstream `main.cpp`/demo shell
  is intentionally not vendored). Every local adaptation is marked with a
  `DK-PORT:` comment so future upstream diffs stay reviewable.
- `DemokratikKongoCore/src/base/gui/arifHost.cpp/.h` — the ONLY bridge to
  the client: two-way model sync (modules/settings/keybinds/favourites/
  friends/profiles/stats), the per-frame driver, and the GL-context
  lifecycle. It hangs off `Menu::SetupImgui` (Init), `Gui::Render`
  (Render) and `Menu::ProcessPendingGLTeardown` (Shutdown).
- `DemokratikKongoCore/ext/arifgui/*.h` — auto-generated embedded assets
  (Plus Jakarta Sans, JetBrains Mono, FA6 solid fonts + the PNG icon
  pack). Regenerate after changing arifclient's `resource/` folder:
  `powershell -ExecutionPolicy Bypass -File tools\generate-arif-assets.ps1`
- The legacy ClickGUI body in `gui.cpp` is superseded; that file still
  owns the shared notification/tooltip/FakeLogin/ItemLogger plumbing.
- The GUI theme preset persists through the profile JSON (`arifTheme` key).

## No tests / lint / CI

Build success is the only verification. `tools/scan_probe.cpp` is a small
manual probe for the target `java.exe` discovery (compile command in its
header comment); it is not part of the build.

## Useful commands

- Headless inject (no GUI): `RuntimeHost.exe --inject <pid>` — exit 0 on success; logs to stdout.
- In-game ClickGUI opens with the **Insert** key.
- Agent log markers in the target's stdout: `RH_AGENT ok` / `RH_AGENT err <reason>`.
