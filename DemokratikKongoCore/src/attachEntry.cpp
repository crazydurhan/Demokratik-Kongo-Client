#include "attachEntry.h"

#include "main.h"
#include "base/util/crash_diagnostics.h"

#include "../../ext/jni/jni.h"
#include "../../ext/jni/jvmti.h"

#include <Windows.h>
#include <atomic>

namespace {

std::atomic<bool> g_bootstrapClaimed{ false };

// Single bootstrap for all load paths. The winning caller starts the core
// init on a dedicated thread; the JVM blocks the attach listener until
// Agent_OnAttach returns, so init must NOT run inline there.
bool RunOnce(HMODULE self)
{
    bool expected = false;
    if (!g_bootstrapClaimed.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;

    Main::HModule = self;

    HANDLE thread = CreateThread(
        nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(&Main::Init),
        self, 0, nullptr);

    if (thread)
        CloseHandle(thread);

    return true;
}

} // namespace

namespace AttachEntry {

bool StartFromDllMain(HMODULE self)
{
    return RunOnce(self);
}

} // namespace AttachEntry

extern "C" {

// JVMTI attach entry point. Runs on the target VM's attach listener thread
// after this DLL has been loaded into the target JVM (via System.load from
// the agent JAR, or -agentpath). Agent_OnAttach is preferred by the VM when
// both entries exist.
__declspec(dllexport) jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved)
{
    (void)vm;
    (void)reserved;

    HMODULE self = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Agent_OnAttach), &self);

    const bool started = AttachEntry::StartFromDllMain(self);

    if (options && options[0])
        DIAG_LOG("Agent_OnAttach", "options: %s", options);
    DIAG_LOG("Agent_OnAttach", started ? "bootstrap started (agent path)"
                                       : "bootstrap already running (DLL path)");

    return JNI_OK;
}

// Kept for -agentpath / loadAgentLibrary users; not part of the attach flow
// because System.load outside agentlib mode does not call JVMTI entries.
__declspec(dllexport) jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved)
{
    return Agent_OnAttach(vm, options, reserved);
}

} // extern "C"
