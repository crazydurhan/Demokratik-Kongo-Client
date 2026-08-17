#include "base.h"

#include "../main.h"
#include "java/java.h"
#include "launcher/launcherDetection.h"
#include "util/logger.h"
#include "util/crash_diagnostics.h"
#include "util/cheatLoop.h"
#include "buildVersion.h"
#include "menu/menu.h"
#include "moduleManager/moduleManager.h"
#include "sdk/sdk.h"
#include "sdk/automap/autoMapper.h"
#include "sdk/automap/autoMapReport.h"
#include "util/window/borderless.h"
#include "patcher/patcher.h"
#include "config/config.h"
#include "util/rotationHelper.h"
#include "gui/guiWidgets.h"
#include "gui/guiCore.h"

#include "../../ext/minhook/minhook.h"

#include <thread>
#include <unordered_map>
#include <chrono>

std::atomic<bool> Base::ShuttingDown{ false };
std::atomic<int>  Base::RenderHookDepth{ 0 };

namespace
{
	std::atomic<bool> g_baseRunning{ false };
}

bool Base::IsRunning() { return g_baseRunning.load(std::memory_order_acquire); }
void Base::SetRunning(bool v) { g_baseRunning.store(v, std::memory_order_release); }

void Base::WaitForRenderQuiescent()
{
	// Give the render thread up to ~3s to finish any in-flight wglSwapBuffers hook.
	for (int i = 0; i < 300; ++i)
	{
		if (Base::RenderHookDepth.load(std::memory_order_acquire) == 0)
			return;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

bool IsKeyReleased(int key)
{
	static std::unordered_map<int, bool> keyStates;

	bool currentState = (GetAsyncKeyState(key) & 0x8000) == 0;
	bool prevState = keyStates[key];
	keyStates[key] = currentState;

	return prevState && !currentState;
}

// SEH-guarded cheat tick stages for clearer crash diagnostics.
static bool RunStage(const char* stage, void(*fn)())
{
	__try
	{
		fn();
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		DIAG_ERR("Base", "Cheat tick stage '%s' raised a structured exception.", stage);
		return false;
	}
}

static void StagePatcherTick()
{
	ModuleManager::RunPatcherTick();
}

static void StageDataUpdate()
{
	ModuleManager::RunDataUpdate();
}

static void StageModuleTicks()
{
	ModuleManager::RunModuleTicks();
}

// The ClickGUI requests the dummy-screen toggle from the render thread; the
// actual JNI call must happen here, on the JVM-attached cheat thread.
static void StageInputBlock()
{
	Menu::ApplyPendingInputBlock();
}

static bool RunCheatTickGuarded()
{
	if (!RunStage("Patcher::Tick", StagePatcherTick))
		return false;
	if (!RunStage("Menu::InputBlock", StageInputBlock))
		return false;
	if (!RunStage("CommonData::UpdateData", StageDataUpdate))
		return false;
	return RunStage("Module::onTick", StageModuleTicks);
}

void Base::Init()
{
	// Reset all tracking flags
	Base::ShuttingDown.store(false, std::memory_order_release);
	Base::RenderHookDepth.store(0, std::memory_order_release);
	MH_Initialized = false;
	Java_Initialized = false;
	SDK_Initialized = false;
	Patcher_Initialized = false;
	Menu_Initialized = false;
	ModuleManager_Initialized = false;

	// ── Stage 1: MinHook ──────────────────────────────────────────
	DIAG_STAGE_BEGIN("MH_Initialize");
	{
		// Without the hook engine every render/input hook silently fails and the
		// client loads into a permanently inert state, so bail out loudly instead.
		const MH_STATUS mhStatus = MH_Initialize();
		if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_INITIALIZED)
		{
			MH_Initialized = (mhStatus == MH_OK);
		}
		else
		{
			DIAG_ERR("Base", "MH_Initialize failed (%d) — hooks cannot be installed. Aborting...", (int)mhStatus);
			Logger::Err("[Base] MinHook initialization failed! Aborting...");
			DIAG_STAGE_END("MH_Initialize");
			Main::Kill();
			return;
		}
	}
	DIAG_STAGE_END("MH_Initialize");

	// ── Stage 2: Java/JNI Attach ──────────────────────────────────
	DIAG_STAGE_BEGIN("Java::Init");
	Java::Init();
	DIAG_STAGE_END("Java::Init");

	if (!Java::Initialized)
	{
		DIAG_ERR("Base", "Java initialization failed! Aborting...");
		Logger::Err("[Base] Java initialization failed! Aborting...");
		Main::Kill();
		return;
	}
	Java_Initialized = true;
	DIAG_LOG("Base", "Java initialized. JNIEnv=%p, JVMTI=%p", Java::GetEnv(), Java::tiEnv);
	Logger::Info("Base", std::string("DemokratikKongo Core ") + DK_CORE_BUILD_ID);

	DIAG_STAGE_BEGIN("LauncherDetection::Init");
	LauncherDetection::Init();
	DIAG_STAGE_END("LauncherDetection::Init");

	DIAG_STAGE_BEGIN("AutoMapper::Init");
	AutoMapper::Init();
	AutoMapReport::Dump();
	DIAG_STAGE_END("AutoMapper::Init");

	// ── Stage 3: SDK (StrayCache + Minecraft) ─────────────────────
	DIAG_STAGE_BEGIN("SDK::Init");
	for (int attempt = 0; attempt < 60; ++attempt)
	{
		SDK::Init();
		if (StrayCache::IsReady() && SDK::Minecraft && SDK::Minecraft->IsReady())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	if (!StrayCache::IsReady() || !SDK::Minecraft || !SDK::Minecraft->IsReady())
	{
		DIAG_WARN("Base", "SDK/StrayCache incomplete — continuing; see automap-report.txt");
		Logger::Warn("Base", "SDK mappings incomplete — modules that need missing IDs stay inert");
	}
	else
	{
		SDK_Initialized = true;
	}
	if (StrayCache::initialized || (SDK::Minecraft && SDK::Minecraft->IsReady()))
		SDK_Initialized = true;
	DIAG_STAGE_END("SDK::Init");

	// Check for JNI exceptions after SDK init
	DIAG_CHECK_JNI(Java::Env, "SDK::Init");

	// ── Stage 4: Patcher (bytecode transforms) ────────────────────
	DIAG_STAGE_BEGIN("Patcher::Init");
	Patcher::Init();
	Patcher_Initialized = true;
	DIAG_STAGE_END("Patcher::Init");

	if (Patcher::IsReady())
	{
		DIAG_LOG("Base", "Patcher ready — bytecode patches + JNI natives active");
	}
	else
	{
		// Most modules only push ThreadContext flags into these patches; without
		// them the whole client is inert, so this must be loud and user-visible
		// instead of a buried log line.
		DIAG_WARN("Base", "Patcher NOT ready — will retry; check [Patcher] logs for details");
		Logger::Err("[Base] Bytecode patches failed to apply (retransform rejected). Most modules will be inert until the hooks attach.");
		Gui::Notify("Hooks not ready",
			"Bytecode patches failed — most modules inert",
			Gui::Icon::Warning, Gui::Colors().danger);
	}

	DIAG_CHECK_JNI(Java::Env, "Patcher::Init");

	// ── Stage 5: Menu (OpenGL hooks) ──────────────────────────────
	DIAG_STAGE_BEGIN("Menu::Init");
	CrashDiag::AssertMainThread("Menu::Init");  // OpenGL context warning
	Menu::Init();
	Menu_Initialized = true;
	DIAG_STAGE_END("Menu::Init");

	// ── Stage 6: Module Manager ───────────────────────────────────
	DIAG_STAGE_BEGIN("ModuleManager::Init");
	ModuleManager::Init();
	ModuleManager_Initialized = true;
	DIAG_STAGE_END("ModuleManager::Init");

	// Auto-load the default profile so settings, keybinds, favorites and
	// GUI placement survive re-injection without a manual "Load" click.
	ProfileManager::load("default");

	DIAG_LOG("Base", "All initialization stages complete. Entering cheat loop.");

	Base::ShuttingDown.store(false, std::memory_order_release);
	Base::SetRunning(true);

	int consecutiveFaults = 0;
	while (Base::IsRunning())
	{
		JNIEnv* env = Java::Env;
		if (env)
		{
			env->PushLocalFrame(512);
		}

		bool ok = RunCheatTickGuarded();

		if (env)
		{
			env->PopLocalFrame(nullptr);
		}

		if (!ok)
		{
			// A tick faulted (commonly a null JNI method/field ID from a stale
			// mapping). Log the first several occurrences and back off, but DO
			// NOT exit - keeping the loop alive means detach still works and the
			// thread is not permanently wedged.
			if (++consecutiveFaults <= 10)
				DIAG_ERR("Base", "Cheat tick raised a structured exception (likely a null JNI method/field ID - check the StrayCache 'MISSING mapping' lines logged at startup). Loop continues so detach still works.");
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
		else
		{
			consecutiveFaults = 0;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(CheatLoop::IntervalMs().load(std::memory_order_relaxed)));
	}

	Main::Kill();
}	

void Base::Kill()
{
	Base::ShuttingDown.store(true, std::memory_order_release);
	Base::SetRunning(false);
	Base::WaitForRenderQuiescent();

	// Auto-save the default profile while every module is still registered
	// (must run before ModuleManager::Kill clears the registry).
	if (ModuleManager_Initialized)
	{
		ProfileManager::save("default");
	}

	if (Menu_Initialized)
	{
		Menu::PrepareShutdown();
	}

	if (ModuleManager_Initialized)
	{
		ModuleManager::Kill();
	}

	if (Patcher_Initialized)
	{
		Patcher::Kill();
	}
	
	if (Borderless::Enabled)
	{
		Borderless::Restore(Menu::HandleWindow);
	}

	if (SDK_Initialized && StrayCache::initialized)
	{
		StrayCache::DeleteRefs();
	}

	if (SDK::Minecraft)
	{
		delete SDK::Minecraft;
		SDK::Minecraft = nullptr;
	}

	if (Menu_Initialized)
	{
		Menu::Kill();
	}

	if (Java_Initialized)
	{
		Java::Kill();
	}

	Logger::Kill();

	if (MH_Initialized)
	{
		MH_Uninitialize();
	}
}
