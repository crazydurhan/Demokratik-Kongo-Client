#pragma once

#include <Windows.h>

// JVMTI agent entry points for the injected core.
//
// Two injection paths converge here:
//   * CRT path  : launcher does CreateRemoteThread + LoadLibraryW  -> DllMain.
//   * Agent path: launcher uses the target JVM's Attach API to load the
//                 core DLL as a JVMTI agent   -> Agent_OnAttach.
//
// Both routes funnel into a single guarded bootstrap (RunOnce) so the core
// can never double-initialize if a VM (or tooling) invokes both.

namespace AttachEntry {

// Called from DllMain(DLL_PROCESS_ATTACH). Returns false if the JVM agent
// route already claimed the bootstrap (in which case DllMain does nothing).
bool StartFromDllMain(HMODULE self);

} // namespace AttachEntry
