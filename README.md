# DemokratikKongo

A Minecraft 1.8.9 injectable client for Windows, written in C++20. It ships as a
small launcher (`RuntimeHost.exe`) that injects an embedded core DLL into a
running JVM process (Vanilla/Forge/Lunar), plus an in-game ClickGUI overlay.

> **Disclaimer** — For educational purposes only. Injecting into Minecraft
> violates the Minecraft EULA, most server rules and anticheat policies. You are
> solely responsible for how you use this code. Use only on servers where you
> have explicit permission.

## Features

- **Combat** — Aim Assist, Reach, Hitbox, Velocity, Auto Block, AntiBot,
  Backtrack, Piercing, Knockback Delay, AutoClicker, friends system.
- **Movement** — Sprint, InvMove, SafeWalk.
- **Render** — 3D ESP, NameTags, Tracers, ArrayList, TargetHUD, Health,
  NoHurtCam, Trajectories, ItemESP, Fullbright, and more.
- **Utility** — Delay Remover, FastPlace, Fast Mine, ChestStealer, Refill,
  Auto Tool, Item Lock, Item Logger, Blink, FakeLag, Freecam, and more.
- **Misc** — Macros, Fake Login, Latency Alerts.
- Modern ImGui ClickGUI with spring animations, custom vector icon font, and
  profile-based config (`%APPDATA%\RuntimeHost\`).

## Building

Requirements:

- **Visual Studio 2022** (C++ v143 toolset, x64).
- A **JDK** — used only for `jvm.lib` at link time. If `setup.ps1` finds one it
  writes the path to a git-ignored `local.props`; otherwise the build falls
  back to the vendored `DemokratikKongoCore\ext\jni\jvm.lib`.

```powershell
.\setup.ps1 -Build      # or: .\setup.ps1  then  .\build.ps1
```

All third-party dependencies (Dear ImGui, MinHook, JNI/JVMTI headers, the
DirectX SDK, nlohmann/json, fonts) are vendored under `ext/` and committed, so
no network access is required to build.

Output (`Release|x64`):

- `release\RuntimeHost.exe` — launcher with the core DLL embedded as a resource.
- `release\RuntimeHostCore.dll` — optional sibling DLL; when placed next to the
  launcher it is used directly instead of the embedded copy.

## Usage

1. Run `RuntimeHost.exe`.
2. Pick the Minecraft/JVM process.
3. Click **Inject**.
4. In-game, press **Insert** to open the ClickGUI.

Headless injection (no GUI): `RuntimeHost.exe --inject <pid>`

## Project layout

- `DemokratikKongo/` — the launcher (ImGui + DX11 GUI, DLL injection).
- `DemokratikKongoCore/` — the injected DLL (JNI/JVMTI, MinHook, OpenGL
  overlay, ImGui ClickGUI, module system).
- `include/stealthNames.h` — system-facing names (`RuntimeHost.exe`,
  `RuntimeHostCore.dll`, window title/class, config folder).

## Third-party

- [Dear ImGui](https://github.com/ocornut/imgui) — MIT
- [MinHook](https://github.com/TsudaKageyu/minhook) — BSD-2-Clause
- [stb](https://github.com/nothings/stb) — public domain / MIT
- [miniz](https://github.com/richgel999/miniz) — MIT
- [nlohmann/json](https://github.com/nlohmann/json) — MIT
- [ASM](https://asm.ow2.io/) — BSD-3-Clause
- [Inter](https://rsms.me/inter/) — SIL Open Font License 1.1
- [JetBrains Mono](https://www.jetbrains.com/lp/mono/) — SIL Open Font License 1.1
- [Font Awesome Free](https://fontawesome.com/) — SIL Open Font License 1.1
- Oracle JNI/JVMTI headers — GPL-2.0 with Classpath Exception

## License

[MIT](LICENSE)
