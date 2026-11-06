# AGENTS.md

Drip Launcher — a Windows launcher for our Minecraft 1.8.9 client
(`RuntimeHost.exe`, C++20 / MSVC / x64, ImGui + DX11 GUI).

The launcher itself contains no cheat code. It:

- shows a licence screen, verifies the key against the licence service and
  binds it to this PC's HWID (see `LISANS_VE_YUKLEME_STANDARTLARI.md`),
- fetches the **client core DLL** from the update service (licence-gated,
  SHA-256-verified, content-addressed cache) and injects it into the running
  game by running an embedded JVMTI tool agent inside a **java.exe host
  process** (see below),
- keeps itself up to date: every start compares itself against the executable
  published in the panel and swaps in a newer build.

The client core (module system, ClickGUI, JVMTI patcher, ...) is developed in
a separate repository; only its built DLL is distributed through the update
service.

## Layout

- `DripLauncher/` — the launcher EXE (`src/launcher/*`, ImGui GUI,
  `src/agent/injectAgent.cpp` — the java-hosted injector agent).
- `include/` — shared headers: stealth names, embedded fonts, and the licence
  token verifier (`licenseShared/`, TweetNaCl — public domain — plus the
  service public key) used by the launcher and the core.

## Why injection runs inside java.exe

The launcher EXE performing remote-thread injection gets flagged by Defender's
behavioural ML (`Behavior:Win32/DefenseEvasion.A!ml`). The exact same
primitives (OpenProcess + VirtualAllocEx + WriteProcessMemory +
CreateRemoteThread) executed **from inside a java.exe host** do not — that is
the pattern ordinary JDK tooling (jcmd, profilers) produces on every attach,
so it reads as normal developer activity.

Flow: the launcher spawns

```
java.exe -agentpath:<RuntimeHostInjector.dll>=pid=<targetPid>;dll=<coreDll> -version
```

The agent (`src/agent/injectAgent.cpp`, built as `RuntimeHostInjector.dll`)
parses the pid + payload path, injects a small position-independent stub into
the target, the stub calls `LoadLibraryW(coreDll)` inside the target, the
agent verifies the module is visible and exits with `0`. Log:
`%APPDATA%\RuntimeHost\inject-agent.log`. Any HotSpot java works — no
`jdk.attach` module is involved, so even JRE images (the Zulu JRE Lunar
bundles) can host the agent. Candidate order: JDKs first
(`collectAttachJavaExeCandidates` in `process_scanner.cpp`).

## Build

```powershell
.\setup.ps1 -Build      # or: .\setup.ps1 then .\build.ps1
```

- Requires MSVC (any recent Visual Studio; `MSBuild.exe` is found via
  vswhere). `build.ps1` auto-detects the platform toolset (VS 2022 → v143,
  VS 18 BuildTools → v145). Builds both projects: the launcher EXE and the
  injector DLL.
- Everything else is vendored under `DripLauncher/ext/` and committed.
- `Release|x64` only. Output: `release\RuntimeHost.exe`.

## Companion injector (generated — do not edit by hand)

`DripLauncher/src/launcher/injectorDllData.h` embeds the built
`RuntimeHostInjector.dll` as base64. Order of operations after editing
`src/agent/injectAgent.cpp`:

```powershell
.\build.ps1
powershell -ExecutionPolicy Bypass -File DripLauncher\resources\build-injector.ps1
.\build.ps1
```

Commit the generated header. The launcher extracts the DLL into its temp
cache (content-hash keyed) at inject time.

## Binaries are named RuntimeHost, NOT DripLauncher

`TargetName` in the `.vcxproj`s and `include/stealthNames.h` use stealth
names: `RuntimeHost.exe`, `RuntimeHostCore.dll`, `RuntimeHostInjector.dll`,
and the window class/title and `%APPDATA%` folder are all `RuntimeHost`.

## Hidden CLI

- `RuntimeHost.exe --inject <pid> [dll]` — run the java-hosted injector
  without the GUI (smoke tests); the DLL defaults to the newest cached core
  from past licensed downloads.
- `RuntimeHost.exe --token-test <file>` — verify an offline licence token
  against the embedded public key (exit 0 = valid, 2 = invalid).

## Licensing & update system

Full documentation: `LISANS_VE_YUKLEME_STANDARTLARI.md`. Service host names
and the Keygen account slug live in **`include/serviceEndpoints.h`** (the only
place in the launcher); the update service exposes the panel at `/`, public
token API under `/up/api/*`, licence-gated payload downloads under `/up/dl/*`.

## No tests / lint / CI

Build success is the only verification.
