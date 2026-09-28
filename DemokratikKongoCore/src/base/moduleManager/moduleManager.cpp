#include "moduleManager.h"

#include "../util/keybindUtil.h"
#include "../sdk/jniResolve.h"
#include "../java/java.h"

#include "modules/render/esp3d.h"
#include "modules/render/xrayBypass.h"
#include "modules/render/nametags.h"
#include "modules/render/nametags.h"
#include "modules/render/arrayList.h"
#include "modules/render/tracers.h"
#include "modules/render/fullbright.h"
#include "modules/render/itemEsp.h"
#include "modules/render/noHurtCam.h"
#include "modules/render/antiDebuff.h"
#include "modules/render/healthDisplay.h"
#include "modules/render/trajectories.h"
#include "modules/utility/delayRemover.h"
#include "modules/utility/fastPlace.h"
#include "modules/utility/safeAutoArmor.h"
#include "modules/combat/hitbox.h"
#include "modules/combat/aimAssist.h"
#include "modules/combat/reach.h"
#include "modules/combat/knockbackDelay.h"
#include "modules/combat/velocity.h"
#include "modules/combat/combatBridge.h"
#include "modules/combat/piercing.h"
#include "modules/combat/antibot.h"
#include "modules/combat/autoBlock.h"
#include "modules/combat/backtrack.h"
#include "modules/movement/sprint.h"
#include "modules/utility/leftAutoClicker.h"
#include "modules/utility/refill.h"
#include "modules/utility/noInteract.h"
#include "modules/utility/itemLock.h"
#include "modules/utility/autoTool.h"
#include "modules/utility/fastMine.h"
#include "modules/misc/macros.h"
#include "modules/utility/itemLogger.h"

#include "modules/movement/invMove.h"
#include "modules/movement/safeWalk.h"
#include "modules/utility/chestStealer.h"
#include "modules/render/targetHud.h"
#include "modules/render/cameraClip.h"
#include "modules/render/freelook.h"
#include "modules/render/fallView.h"
#include "modules/render/damageTags.h"
#include "modules/misc/latencyAlerts.h"
#include "modules/utility/blink.h"
#include "modules/utility/fakeLag.h"
#include "modules/player/freecam.h"

#include "../util/rotationHelper.h"
#include "../util/logger.h"
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
    registerModule<Esp3D>();
    registerModule<XrayBypass>();
    registerModule<NameTags>();
    registerModule<ArrayList>();
    registerModule<Tracers>();
    registerModule<Fullbright>();
    registerModule<ItemESP>();
    registerModule<NoHurtCam>();
    registerModule<AntiDebuff>();
    registerModule<HealthDisplay>();
    registerModule<Trajectories>();
    registerModule<TargetHUD>();
    registerModule<CameraClip>();
    registerModule<Freelook>();
    registerModule<FallView>();
    registerModule<DamageTags>();

    // Combat
    registerModule<Hitbox>();
    registerModule<AimAssist>();
    registerModule<Reach>();
    registerModule<KnockbackDelay>();
    registerModule<Velocity>();
    registerModule<Piercing>();
    registerModule<AntiBot>();
    registerModule<AutoBlock>();
    registerModule<Backtrack>();
    registerModule<LeftAutoClicker>();

    // Movement
    registerModule<Sprint>();
    registerModule<InvMove>();
    registerModule<SafeWalk>();

    // Utility
    registerModule<DelayRemover>();
    registerModule<FastPlace>();
    registerModule<SafeAutoArmor>();
    registerModule<Refill>();
    registerModule<NoInteract>();
    registerModule<ItemLock>();
    registerModule<ItemLogger>();
    registerModule<AutoTool>();
    registerModule<FastMine>();
    registerModule<ChestStealer>();
    registerModule<Blink>();
    registerModule<FakeLag>();
    registerModule<Freecam>();

    // Misc
    registerModule<Macros>();
    registerModule<LatencyAlerts>();

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

	if (!StrayCache::IsReady())
		return;
	Patcher::Tick();

	// Surface hook state transitions — the flag-only modules (ESP, Velocity,
	// Reach, ...) silently go inert whenever retransforms are not applied.
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

	// Non-toggleable modules (e.g. Friends) always tick so shared state
	// like esp_friends stays synced for Java hooks (Piercing, ESP, ...).
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
	RunDataUpdate();
	RunModuleTicks();
}

void ModuleManager::OnRender2D()
{
    // Hold the storage lock during dispatch (see g_modulesMutex note).
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (!mod->toggleable() || mod->isEnabled() || mod->renderWhenDisabled())
            mod->onRender2D();
}

void ModuleManager::OnRender3D(float partialTicks)
{
    // Invoked from the JVM via the ClassPatcher render-world-pass hook.
    // Runs inside Minecraft's GL world matrix, so modules can issue
    // raw OpenGL calls in camera/world space.
    //
    // This fires every frame, so any module leaking a single local ref would
    // overflow the reference table within seconds; the frame bounds the leak.
    JniResolve::LocalFrame frame(Java::GetEnv(), 128);

    // Hold the storage lock during dispatch (see g_modulesMutex note).
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    for (auto& mod : storage())
        if (mod->isEnabled()) mod->onRender3D(partialTicks);
}

void ModuleManager::OnGameTick()
{
    // Walking-update pre (client thread): keep movement inputs applied after
    // vanilla movement processing so InvMove/SafeWalk stick for the tick.
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

    // DelayRemover's NoHitDelay/NoJumpDelay must apply within the frame — the
    // 45ms gate below would leave leftClickCounter non-zero for several frames
    // after a miss, swallowing re-clicks at high CPS.
    CombatBridge::DrainUrgentInputs();

    // getMouseOver can fire every frame; runTick once per tick. Cap work ~20 Hz
    // so dual hooks (or high FPS) do not over-drive Aim/AC drain.
    {
        static long long s_lastWorkMs = 0;
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - s_lastWorkMs < 45)
            return;
        s_lastWorkMs = now;
    }

    // AimAssist no longer ticks here — it applies per frame via onRender3D.

    if (FastMine* fm = Get<FastMine>())
    {
        if (fm->isEnabled())
            fm->clientTick();
    }
    if (ChestStealer* cs = Get<ChestStealer>())
    {
        if (cs->isEnabled())
            cs->clientTick();
    }
    if (AutoTool* at = Get<AutoTool>())
    {
        if (at->isEnabled())
            at->clientTick();
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
