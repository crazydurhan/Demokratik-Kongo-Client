# Drip Launcher

A Windows launcher for our Minecraft client: licence check, client delivery
and injection in one small executable (`RuntimeHost.exe`).

The launcher is written in **Rust** (`launcher-rs/`, eframe/egui GUI). The
previous C++/ImGui implementation stays in the repository as
`DripLauncher/` for reference and rollback (`build.ps1 -Cpp`).

> **Disclaimer** — For educational purposes only. Injecting into Minecraft
> violates the Minecraft EULA, most server rules and anticheat policies. You
> are solely responsible for how you use this software. Use only on servers
> where you have explicit permission.

## What it does

- **Licence** — the user enters a key; the launcher validates it against the
  licence service, binds it to the PC's hardware fingerprint (one key = one
  PC), stores it DPAPI-encrypted and keeps a **server-signed offline token**
  valid for seven days without a connection.
- **Client delivery** — the client core DLL is fetched from the update
  service on every inject (key + HWID gated, SHA-256 verified) and cached
  content-addressed. The remote load runs inside a **java.exe host** running
  an embedded JVMTI tool agent, so behavioural AV stays quiet. Updates ship by
  uploading a new DLL in the panel; the launcher never needs to be
  redistributed for a core update.
- **Self-update** — on every start the launcher compares itself with the
  build published in the panel and swaps in a newer one automatically.
- **Injection** — pick the game process, press Inject; the launcher
  downloads/verifies the core and the remote load runs inside a java.exe host.

## Download

Service host names and the licence account slug are configured in
`launcher-rs/src/config.rs`.

## Building

```powershell
.\build.ps1            # Rust launcher -> release\RuntimeHost.exe
.\build.ps1 -Cpp       # legacy MSVC/ImGui launcher
```

Rust build needs a Rust toolchain (`rustup`, MSVC target) and the MSVC linker;
everything else is vendored/locked (`Cargo.lock`, embedded injector DLL and
icon under `launcher-rs/assets/`). Output: `release\RuntimeHost.exe`.

## Documentation

- `AGENTS.md` — architecture, injection routes, hidden CLI.
- `launcher-rs/README.md` — Rust implementation notes and QA commands.
- `LISANS_VE_YUKLEME_STANDARTLARI.md` — the licence and update system in full
  (endpoints, token format, HWID recipe, operations guide).
