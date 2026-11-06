# Drip Launcher

A Windows launcher for our Minecraft 1.8.9 client: licence check, client
delivery and setup in one small executable (`RuntimeHost.exe`).

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
  an embedded JVMTI tool agent — the same primitive sequence ordinary JDK
  tooling performs, so behavioural AV stays quiet. Updates ship by uploading
  a new DLL in the panel; the launcher never needs to be redistributed for a
  core update.
- **Self-update** — on every start the launcher compares itself with the
  build published in the panel and swaps in a newer one automatically.
- **Injection** — the client is injected on demand: pick the game process,
  press Inject; the launcher downloads/verifies the core and the remote load
  runs inside a java.exe host (no thread-hiding flags, no classic injector
  signature coming from the launcher EXE).

## Download

Service host names and the licence account slug are configured in
`include/serviceEndpoints.h`.

## Building

```powershell
.\setup.ps1 -Build
```

Requires MSVC (VS 2022 or newer — the toolset is detected automatically);
everything else is vendored in the repository. Output: `release\RuntimeHost.exe`.

## Documentation

- `AGENTS.md` — architecture, injection routes, hidden CLI.
- `LISANS_VE_YUKLEME_STANDARTLARI.md` — the licence and update system in full
  (endpoints, token format, HWID recipe, operations guide).
