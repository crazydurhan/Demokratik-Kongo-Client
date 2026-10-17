#include "moduleManager.h"

// CommonData:: is used directly below (SanityCheck / UpdateData /
// RefreshRenderState). It used to arrive transitively through esp3d.h, which
// is gone; include what you use rather than relying on a module header.
#include "commonData.h"

#include "../util/keybindUtil.h"
#include "../sdk/jniResolve.h"
#include "../java/java.h"

#include "modules/render/nametags.h"
#include "modules/render/itemEsp.h"
#include "modules/render/cameraClip.h"
#include "modules/utility/delayRemover.h"
#include "modules/utility/fastPlace.h"
#include "modules/utility/safeAutoArmor.h"
#include "modules/utility/refill.h"
#include "modules/utility/itemLogger.h"
#include "modules/misc/macros.h"
#include "modules/combat/combatBridge.h"

#include "../util/rotationHelper.h"
#include "../util/logger.h"
#include "../license/licenseGate.h"
#include "../buildVersion.h"
#include "../menu/menu.h"
#include "../patcher/patcher.h"
#include "../sdk/strayCache.h"
#include "../gui/guiWidgets.h"
#include "../gui/guiCore.h"

#include <Windows.h>
#include <chrono>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
	std::atomic<bool> g_commonDataUpdated{ false };
	// Guards storage() mutations (Init/Kill) and dispatch iteration: storage is
	// built/cleared on the cheat thread while render/tick/wndProc threads
	// iterate it. All()/Get() stay lock-free (they only read raw pointers and
	// are called from inside dispatched module code — locking there would
	// deadlock); they are safe as long as no clear() runs concurrently.
	std::mutex g_modulesMutex;
}

bool CommonData::DataUpdated() { return g_commonDataUpdated.load(std::memory_order_acquire); }
void CommonData::SetDataUpdated(bool v) { g_commonDataUpdated.store(v, std::memory_order_release); }

namespace
{
    std::vector<std::unique_ptr<Module>>& storage()
    {
        static std::vector<std::unique_ptr<Module>> g;
        return g;
    }

    template <typename T>
    void registerModule()
    {
        auto mod = std::make_unique<T>();
        mod->finalizeRegistration();
        storage().emplace_back(std::move(mod));
    }

    long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool runCombatTargetingGuarded()
    {
        __try
        {
            CombatBridge::TickTargetingTrack();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool runModuleTickGuarded(Module* mod)
    {
        __try
        {
            mod->onTick();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void recordTickFault(Module* mod)
    {
        if (!mod)
            return;

        static std::unordered_map<std::string, int> faultCounts;
        static std::unordered_map<std::string, long long> lastLogMs;

        const std::string name = mod->name();
        const int count = ++faultCounts[name];
        const long long now = nowMs();
        const long long last = lastLogMs[name];

        if (count <= 5 || count % 50 == 0 || now - last > 3000)
        {
            Logger::Warn("ModuleManager", "Module onTick fault in \"" + name
                + "\" (count=" + std::to_string(count) + "). This module was skipped for this tick.");
            lastLogMs[name] = now;
        }

        if (mod->toggleable() && count == 10)
        {
            Logger::Error("ModuleManager", "Auto-disabling faulting module \"" + name
                + "\" after 10 onTick exceptions to keep the client loop smooth.");
            mod->setEnabled(false);
            Gui::Notify("Module fault", (name + " auto-disabled").c_str(),
                        Gui::Icon::Warning, Gui::Colors().danger);
        }
    }
}

void ModuleManager::Init()
{
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    auto& m = storage();
    m.clear();

    // Render
    registerModule<NameTags>();
    registerModule<ItemESP>();
    registerModule<CameraClip>();

    // Utility
    registerModule<DelayRemover>();
    registerModule<FastPlace>();
    registerModule<SafeAutoArmor>();
    registerModule<Refill>();
    registerModule<ItemLogger>();

    // Misc
    registerModule<Macros>();

    std::unordered_set<std::string> seenNames;
    for (const auto& mod : m) {
        const std::string& n = mod->name();
        if (!seenNames.insert(n).second)
            Logger::Warn("ModuleManager", "Duplicate module name registered: \"" + n + "\"");
    }

    std::ostringstream modList;
    modList << "build=" << DK_CORE_BUILD_ID << " modules(" << m.size() << "): ";
    for (size_t i = 0; i < m.size(); ++i) {
        if (i) modList << ", ";
        modList << m[i]->name();
    }
    Logger::Info("ModuleManager", modList.str());
}


void ModuleManager::Kill()
{
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (mod->isEnabled()) mod->setEnabled(false);
    storage().clear();
}

void ModuleManager::RunPatcherTick()
{
	// Install wglSwapBuffers + wglGetProcAddress hooks once opengl32 is live
	// (deferred from Menu::Init so inject hits the render thread mid-game).
	Menu::EnsureRenderHooks();

	// Licence gate: keep the GUI hooks alive so a locked client can still
	// show why it is locked, but apply no bytecode patches while unverified.
	if (!LicenseGate::Active())
		return;

	if (!StrayCache::IsReady())
		return;
	Patcher::Tick();

	// Surface hook state transitions — the flag-only modules silently go inert
	// whenever retransforms are not applied.
	static bool s_wasReady = false;
	static bool s_announcedDown = false;
	const bool ready = Patcher::IsReady();
	if (ready && !s_wasReady && s_announcedDown)
	{
		s_announcedDown = false;
		Gui::Notify("Hooks ready", "Bytecode patches applied", Gui::Icon::Check, Gui::Colors().good);
	}
	else if (!ready && s_wasReady)
	{
		s_announcedDown = true;
		Gui::Notify("Hooks lost", "Bytecode patches detached — modules may be inert",
			Gui::Icon::Warning, Gui::Colors().danger);
	}
	s_wasReady = ready;
}

void ModuleManager::RunDataUpdate()
{
	if (!StrayCache::IsReady())
		return;
	if (!CommonData::SanityCheck())
		return;
	CommonData::UpdateData();
}

void ModuleManager::RunModuleTicks()
{
	if (!StrayCache::IsReady())
		return;

	// Hold the storage lock during dispatch (see g_modulesMutex note).
	std::lock_guard<std::mutex> lock(g_modulesMutex);

	if (!runCombatTargetingGuarded())
	{
		static long long s_lastTargetWarnMs = 0;
		const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
		if (now - s_lastTargetWarnMs > 5000)
		{
			s_lastTargetWarnMs = now;
			Logger::Warn("ModuleManager", "CombatBridge::TickTargetingTrack faulted; skipped this tick.");
		}
	}

	// Non-toggleable modules always tick, even when disabled, so the shared
	// state they publish into the Patcher stays synced for the Java hooks.
	for (auto& mod : storage())
	{
		if (!mod->toggleable() || mod->isEnabled())
		{
			if (!runModuleTickGuarded(mod.get()))
				recordTickFault(mod.get());
		}
	}

	RotationHelper::I().flush();
}

void ModuleManager::UpdateModules()
{
	RunPatcherTick();
	if (!LicenseGate::Active())
		return;
	RunDataUpdate();
	RunModuleTicks();
}

void ModuleManager::OnRender2D()
{
    // Licence gate: module HUD output is the product being licensed.
    if (!LicenseGate::Active())
        return;

    // Hold the storage lock during dispatch (see g_modulesMutex note).
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (!mod->toggleable() || mod->isEnabled() || mod->renderWhenDisabled())
            mod->onRender2D();
}

void ModuleManager::OnRender3D(float partialTicks)
{
    // Licence gate: world-space output is the product being licensed.
    if (!LicenseGate::Active())
        return;

    // Invoked from the JVM via the ClassPatcher render-world-pass hook.
    // Runs inside Minecraft's GL world matrix, so modules can issue
    // raw OpenGL calls in camera/world space.
    //
    // This fires every frame, so any module leaking a single local ref would
    // overflow the reference table within seconds; the frame bounds the leak.
    // Matrices read during the world pass are the real camera transform;
    // a tick-phase read can catch a different GL phase and shift boxes.
    CommonData::RefreshRenderState();

    JniResolve::LocalFrame frame(Java::GetEnv(), 128);

    // Hold the storage lock during dispatch (see g_modulesMutex note).
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (mod->isEnabled()) mod->onRender3D(partialTicks);
}

void ModuleManager::OnGameTick()
{
    // Licence gate: movement input application is a feature.
    if (!LicenseGate::Active())
        return;

    // Walking-update pre (client thread): apply queued movement inputs after
    // vanilla movement processing so they stick for the tick.
    CombatBridge::DrainPendingInputs();
}

void ModuleManager::OnRunTickPre()
{
    // Also reachable from the per-frame getMouseOver hook, so bound the locals
    // created by the client-tick helpers below.
    JniResolve::LocalFrame frame(Java::GetEnv(), 128);

    // Hold the storage lock: the Get<T>() lookups below iterate the module
    // vector while the cheat thread may rebuild it (see g_modulesMutex note).
    std::lock_guard<std::mutex> lock(g_modulesMutex);

    Patcher::NoteRunTickPre();

    // Licence gate: input drains and delay removers are features.
    if (!LicenseGate::Active())
        return;

    // DelayRemover's NoHitDelay/NoJumpDelay must apply within the frame — the
    // 45ms gate below would leave leftClickCounter non-zero for several frames
    // after a miss, swallowing re-clicks at high CPS.
    CombatBridge::DrainUrgentInputs();

    // getMouseOver can fire every frame; runTick once per tick. Cap work ~20 Hz
    // so dual hooks (or high FPS) do not over-drive the drain.
    {
        static long long s_lastWorkMs = 0;
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - s_lastWorkMs < 45)
            return;
        s_lastWorkMs = now;
    }

    CombatBridge::DrainPendingInputs();
    CombatBridge::TickTargetingTrack();
}

void ModuleManager::OnKey(int vk, bool down, LPARAM lParam)
{
    if (!down) return;
    // Hold the storage lock during iteration (see g_modulesMutex note).
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (mod->toggleable()
            && KeybindUtil::KeybindMatches(mod->keybind().virtualKey, vk, lParam))
        {
            mod->toggle();
        }
}

const std::vector<std::unique_ptr<Module>>& ModuleManager::All()
{
    return storage();
}

std::vector<Module*> ModuleManager::ByCategory(Category c)
{
    std::vector<Module*> out;
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (mod->category() == c && mod->showInMenu())
            out.push_back(mod.get());
    return out;
}
