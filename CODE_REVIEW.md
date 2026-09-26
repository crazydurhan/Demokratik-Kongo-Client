# Demokratik-Kongo-Client — Full Code Review

Four independent reviewer passes (launcher, core/hooks, modules/SDK, build/scripts) over commit `24d0d8d`. No tests exist in the repo; test-gap notes are at the end.

## Regression check (latest commit)

- ✅ freecam "Disable On Damage" default → `false` landed correctly (`freecam.cpp:20`).
- ✅ `patcher/data.h` (39k-line regenerated blob) is internally consistent: declared size matches token count, blob is a valid JAR, consumed via `sizeof(data)` so no drift possible.

## Critical bugs (fix first)

### Launcher
1. **`injector.cpp:198`** — `GetExitCodeThread` returns a 32-bit DWORD stored as the 64-bit `HMODULE`; silently truncated, and a module whose low 32 bits are 0 is misreported as load failure. Use it only as heuristic (you already verify via `EnumProcessModulesEx`).
2. **`injector.cpp:185-191, 322`** — On remote-thread timeout, `VirtualFreeEx` frees the path buffer while the remote thread may still be executing `LoadLibraryW` from it → use-after-free inside the target (can crash the game).
3. **`launcherApp.cpp:140-145`** — `CreateWindowExW` / `RegisterClassExW` results never checked; failure cascades into a null-window device creation and `ShowWindow(nullptr)` crash.
4. **`launcherApp.cpp:83-87`** — `createRenderTarget()` never checks `GetBuffer`/`CreateRenderTargetView` results; device-removed mid-resize → null deref instead of recovery.

### Core / hooks
5. **`java.cpp:32-34` + `java.h:33-34`** — `attachedThreads` is a `std::set<JavaVM*>` but each *thread* must attach/detach independently. Threads other than the first never detach; `Java::Kill()` detaches only the calling thread. Design is broken, not just leaky.
6. **`menu/menu.cpp:61-75`** — GL teardown runs on the cheat thread, but `MenuGLContext` is current on the render thread (GL contexts are thread-affine): `wglMakeCurrent`/`wglDeleteContext` fail → leaked GL context, skipped ImGui backend shutdown on every unload.
7. **`moduleManager.cpp:79-91, 231-236`** — module `storage()` is built/cleared on the cheat thread while render/tick/wndProc threads iterate it with no synchronization → use-after-free during unload if a key event lands mid-`clear()`.
8. **`config/config.cpp:296`** — `friendsMiddleClick` parse does `doc.substr(c+1).find("true")` — searches the rest of the *whole document*, so any later `true` in the file flips the setting on. Parse the single token.

### Modules
9. **`macros.cpp:247-251`** — if `canRun()` goes false mid-use, state resets to Idle but `m_savedHotbarSlot` is never restored → hotbar left permanently switched.
10. **`fullbright.cpp:314`** — night-vision removal uses hardcoded potion id `16` while enable resolves it dynamically; on other id mappings the effect never gets removed on disable.
11. **`chestStealer.cpp:287`** — auto-close condition `(!hasItemsLeft || !lootedAny)` closes the chest while items remain (first pass loots nothing → closes). Should close only on `!hasItemsLeft`.
12. **`autoBlock.cpp:220-228`** — if the target leaves range during the lag phase, `resetBlockState()` is skipped and the state machine sticks until a target re-enters range.

### Build
13. **`build.ps1:8-9`** — missing vswhere.exe throws before the `if (-not $msbuild)` fallback is reached (under `$ErrorActionPreference="Stop"`); the fallback is dead code. Wrap in try/catch or `Test-Path` first.
14. **`build.ps1:41`** — DLL copy to `release\` isn't wrapped in try/catch (the exe copy is); a locked DLL aborts the script leaving a mismatched new-exe/old-dll pair.

## Notable concerns

- **Injection on the UI thread** (`launcherGui.cpp:162-203`): blocks up to 30 s → "(Not Responding)" ghost window.
- **Clipboard leak** (`launcherGui.cpp:111-127`): `SetClipboardData` failure leaks the `HGLOBAL`; `EmptyClipboard` already destroyed prior content.
- **`injector.cpp:356-362`**: VerifyModule failure after successful LoadLibrary leaves the payload half-initialized in the game with no cleanup/retry.
- **`%TEMP%` payload extraction** (`resourceLoader.cpp:141-196`): world-writable; pre-planting/TOCTOU between hash check and inject. Consider user-scoped dir or verify at inject time.
- **eventBus** (`core/eventBus.h:37-55`): static slot vector with zero locking; subscribe (cheat thread) vs dispatch (render thread) → reallocation during dispatch invalidates slots.
- **`patcher.cpp:1917-1923`**: `CallObjectMethod` result used without `ExceptionCheck`/`ExceptionClear`; pending exception poisons the next JNI call.
- **`wglSwapBuffers.cpp:320-333`**: `wglCreateContext`/`wglMakeCurrent` results unchecked in `SetupImgui`.
- **freelook** (`freelook.cpp:44-53`): user's perspective setting can be restored wrongly if the game re-creates `gameSettings` mid-use.
- **`noHurtCam.cpp:46-53`**: partial mode rewrites `hurtTime` every tick, corrupting the hurt signal other modules (AutoBlock, Velocity) depend on.
- **`leftAutoClicker.cpp`**: clicker thread reads/writes CPS state and calls JNI with no synchronization or thread-attach guard; `antibot.cpp:98` treats a null entity as "bot" (silently filters everything on a JNI failure).
- **`fallView.cpp:64-72, 84-85`**: two settings have empty if-bodies (mislabeled no-ops); Resistance multiplier (0.80) is wrong — it doesn't reduce fall damage in 1.8.9.
- **`mappedRegistry.cpp:111-118, 273-287`**: duplicate global refs leaked on shutdown; a copy-pasted third `GetStaticFieldID` call that can never succeed.
- **`fullbright.cpp:230-236`**: local-ref leak per tick in Fade mode when `world` or `player` fails to resolve.
- **vcxproj**: Debug/Release share an `IntDir` (`DemokratikKongo.vcxproj:44-52`); core lacks `/utf-8` that launcher has; core ships `imgui_demo.cpp`.
- **build.ps1 + payload.rc**: no freshness check that the embedded payload matches the freshly-built core DLL on incremental builds.

## Nits (selected)

`injector.cpp:176-182` thread-handle leak on failed `NtCreateThreadEx`; `backtrack.cpp:166` `std::rand()` never seeded; `fastPlace.cpp:55` pending `rightClickDelayTimer` not reset on disable; `targetHud.cpp:149` display health not reset on target switch; `arrayList.cpp:176` dead `totalH`; `clickPatternStore.h` appears unreferenced (dead file?); `ext/dxsdk/Include/detours.cpp` is a source file in an Include dir; `process_scanner.cpp:252` `IsWow64Process` failure labels process as x64; app.manifest lacks DPI awareness (blurry launcher on scaled displays).

## Missing tests (repo has none; build success is the only verification)

Highest-value, cheapest-to-add (all testable against a sacrificial host process, no game needed):

1. **Injector**: dead/32-bit/protected PID; timeout path asserting no `VirtualFreeEx` while thread alive; failed-`NtCreateThreadEx` handle-leak; `remoteModuleHandle` truncation under high ASLR.
2. **Payload pipeline**: missing embedded resource → exit 4; hash-cache reuse; size-mismatch reinstall; `.tmp` rename collision; invalid-PE sibling DLL fallback; `%TEMP%` unwritable.
3. **PE validation**: truncation at every header boundary, negative `e_lfanew`, 0-byte file, locked file.
4. **Scanner**: PID reuse between scan and inject, module list > 1024, WoW64 labeling, hung-window enumeration.
5. **CLI contract**: `--inject` missing/garbage pid → exit 3; success 0; failure 5.
6. **Config parser** (pure logic, no game): `friendsMiddleClick` true/false-vs-later-true case, legacy key mapping, round-trip save/load for every module's settings.
7. **Module state restore**: enable→disable for freecam/freelook/fullbright/macros/sprint asserting game state returns to baseline — the exact class of bug in findings #9/#10.
