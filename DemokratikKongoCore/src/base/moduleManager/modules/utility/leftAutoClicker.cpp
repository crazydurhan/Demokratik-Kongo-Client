#include "leftAutoClicker.h"

#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>
#include <Windows.h>

namespace
{
    thread_local std::mt19937 g_rng{ std::random_device{}() };

    LARGE_INTEGER& qpcFreq()
    {
        static LARGE_INTEGER f{};
        return f;
    }

    void initQpc()
    {
        static bool init = false;
        if (!init)
        {
            QueryPerformanceFrequency(&qpcFreq());
            init = true;
        }
    }

    inline long long nowUs()
    {
        initQpc();
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart * 1000000LL / qpcFreq().QuadPart;
    }

    inline long long nowMs()
    {
        return nowUs() / 1000LL;
    }

    void waitUntilUs(long long targetUs)
    {
        while (true)
        {
            const long long now = nowUs();
            const long long remaining = targetUs - now;
            if (remaining <= 0) return;

            if (remaining > 4000)
                std::this_thread::sleep_for(std::chrono::microseconds(remaining - 2000));
            else
                YieldProcessor();
        }
    }

    void setLwjglMouseButton(JNIEnv* env, int button, bool down)
    {
        static jclass bridgeClass = nullptr;
        static jmethodID mid = nullptr;
        if (!bridgeClass)
        {
            jclass local = nullptr;
            if (Java::AssignClass("io.github.lefraudeur.RuntimeBridge", local) && local)
                bridgeClass = local;
        }
        if (bridgeClass && !mid)
        {
            mid = env->GetStaticMethodID(bridgeClass, "setLwjglMouseButton", "(IZ)V");
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (!bridgeClass || !mid) return;
        env->CallStaticVoidMethod(bridgeClass, mid, (jint)button, down ? JNI_TRUE : JNI_FALSE);
        JniResolve::ClearException(env);
    }

    struct SwingCache
    {
        jclass   livingBaseClass = nullptr;
        jmethodID isUsingItem    = nullptr;
        jclass   itemSwordClass  = nullptr;
        jclass   itemAxeClass    = nullptr;
    };

    SwingCache& swingCache()
    {
        static SwingCache c;
        return c;
    }

    bool ensureSwingFields(JNIEnv* env)
    {
        SwingCache& c = swingCache();
        if (c.livingBaseClass)
            return true;
        if (!env) return false;

        if (!c.livingBaseClass)
        {
            jclass local = nullptr;
            if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", local))
                return false;
            c.livingBaseClass = local;
        }

        if (!c.isUsingItem && c.livingBaseClass)
            c.isUsingItem = JniResolve::Method(env, c.livingBaseClass, "()Z", "isUsingItem");

        if (!c.itemSwordClass)
            Java::AssignClass("net.minecraft.item.ItemSword", c.itemSwordClass);
        if (!c.itemAxeClass)
            Java::AssignClass("net.minecraft.item.ItemAxe", c.itemAxeClass);

        return c.livingBaseClass != nullptr;
    }
}

LeftAutoClicker::LeftAutoClicker()
    : Module("AutoClicker", "Clicks automatically while the left mouse button is pressed.", Category::Combat)
{
    m_clickPattern = &add<EnumSetting>("Click Pattern",
        std::vector<const char*>{ "Jitter", "Butterfly" }, 0);
    m_clickPattern->description = "Butterfly generates double-clicks to mimic legitimate butterfly clicking.";

    m_targetCps = &add<NumberSetting>("Target CPS", 10.0f, 1.0f, 20.0f, 0.5f);
    m_targetCps->description = "The clicker produces clicks at a rate close to this speed.";

    m_randomize = &add<BoolSetting>("Randomize", true);
    m_randomize->description = "Randomizes the speed of generated clicks to prevent a static pattern.";

    m_exhaust = &add<BoolSetting>("Simulate Exhaust", true);
    m_exhaust->description = "Occasionally clicks slower, like a legitimate player tiring out.";

    m_breakBlocks = &add<BoolSetting>("Allow Breaking Blocks", true);
    m_breakBlocks->description = "Disables the module while aiming at a block so mining still works.";

    m_inventory = &add<BoolSetting>("Inventory", false);
    m_inventory->description = "Enables the clicker in inventories/chests when SHIFT is held.";

    m_invRandomize = &add<BoolSetting>("Randomize Speed", true);
    m_invRandomize->description = "Limits inventory clicks to a reasonable speed instead of max speed.";
    m_invRandomize->visible = [this] { return m_inventory->value; };

    m_holdingWeapon = &add<BoolSetting>("Holding Weapon", false);
    m_holdingWeapon->description = "Only activates while holding a weapon (Sword or Axe).";

    m_notUsingItem = &add<BoolSetting>("Not Using Item", true);
    m_notUsingItem->description = "Disables the clicker while using an item (eating, blocking, bow).";
}

std::string LeftAutoClicker::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";

    int realCps = const_cast<LeftAutoClicker*>(this)->getRealCPS();
    const char* patternName = (m_clickPattern && m_clickPattern->index == 1) ? "Butterfly" : "Jitter";

    char buf[128];
    if (detail == SuffixDetail::Basic) {
        std::snprintf(buf, sizeof(buf), "%d CPS", realCps);
    } else {
        std::snprintf(buf, sizeof(buf), "%d CPS [%s] (%.0f)",
            realCps, patternName, m_targetCps->value);
    }
    return buf;
}

bool LeftAutoClicker::isLeftMouseHeld() const
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

int LeftAutoClicker::getRealCPS() const
{
    long long now = nowMs();
    int count = 0;
    std::lock_guard<std::mutex> lock(m_cpsMutex);
    for (int i = 0; i < m_cpsCount; ++i)
        if (now - m_cpsTimestamps[i] < 1000)
            ++count;
    return count;
}

bool LeftAutoClicker::isHoldingWeapon(JNIEnv* env, jobject player)
{
    if (!env || !player) return false;
    ensureSwingFields(env);
    SwingCache& c = swingCache();

    if (StrayCache::entityPlayer_inventory && StrayCache::inventoryPlayer_getCurrentItem)
    {
        jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
        JniResolve::ClearException(env);
        if (inv)
        {
            jobject stack = env->CallObjectMethod(inv, StrayCache::inventoryPlayer_getCurrentItem);
            JniResolve::ClearException(env);
            if (stack && StrayCache::itemStack_getItem)
            {
                jobject item = env->CallObjectMethod(stack, StrayCache::itemStack_getItem);
                JniResolve::ClearException(env);
                if (item)
                {
                    bool isWpn = (c.itemSwordClass && env->IsInstanceOf(item, c.itemSwordClass))
                              || (c.itemAxeClass   && env->IsInstanceOf(item, c.itemAxeClass));
                    env->DeleteLocalRef(item);
                    env->DeleteLocalRef(stack);
                    env->DeleteLocalRef(inv);
                    return isWpn;
                }
                env->DeleteLocalRef(stack);
            }
            env->DeleteLocalRef(inv);
        }
    }
    return false;
}

bool LeftAutoClicker::isUsingItem(JNIEnv* env, jobject player)
{
    if (!env || !player) return false;
    ensureSwingFields(env);
    SwingCache& c = swingCache();

    if (c.isUsingItem)
    {
        jboolean usingItem = env->CallBooleanMethod(player, c.isUsingItem);
        JniResolve::ClearException(env);
        return usingItem == JNI_TRUE;
    }
    return false;
}

bool LeftAutoClicker::isTargetingBlock(JNIEnv* env)
{
    if (!env || !SDK::Minecraft) return false;
    if (StrayCache::minecraft_objectMouseOver && StrayCache::movingObjectPosition_typeOfHit)
    {
        jobject mouseOver = env->GetObjectField(SDK::Minecraft->GetInstance(), StrayCache::minecraft_objectMouseOver);
        JniResolve::ClearException(env);
        if (mouseOver)
        {
            jobject typeOfHit = env->GetObjectField(mouseOver, StrayCache::movingObjectPosition_typeOfHit);
            JniResolve::ClearException(env);
            if (typeOfHit)
            {
                static jmethodID s_ordinalMid = nullptr;
                if (!s_ordinalMid)
                {
                    jclass enumClass = env->GetObjectClass(typeOfHit);
                    if (enumClass)
                    {
                        s_ordinalMid = env->GetMethodID(enumClass, "ordinal", "()I");
                        JniResolve::ClearException(env);
                        env->DeleteLocalRef(enumClass);
                    }
                }
                if (s_ordinalMid)
                {
                    const int ordinal = env->CallIntMethod(typeOfHit, s_ordinalMid);
                    JniResolve::ClearException(env);
                    env->DeleteLocalRef(typeOfHit);
                    env->DeleteLocalRef(mouseOver);
                    // 1.8.9 MovingObjectType: MISS=0, BLOCK=1, ENTITY=2
                    return ordinal == 1;
                }
                env->DeleteLocalRef(typeOfHit);
            }
            env->DeleteLocalRef(mouseOver);
        }
    }
    return false;
}

long long LeftAutoClicker::sampleDelayUs(bool inventory)
{
    // Inventory mode with "Randomize Speed" caps the pace to a plausible
    // 5-8 CPS band; otherwise inventory clicks run at the full combat speed.
    if (inventory && m_invRandomize && m_invRandomize->value)
    {
        const float invCps = std::uniform_real_distribution<float>(5.0f, 8.0f)(g_rng);
        return std::max(1000LL, (long long)std::llround(1000000.0 / invCps));
    }

    float cps = m_targetCps->value;

    if (m_randomize->value)
    {
        // Small jitter around the target speed.
        cps += std::normal_distribution<float>(0.0f, 0.6f)(g_rng);
    }

    cps = std::clamp(cps, 1.0f, 30.0f);
    return std::max(1000LL, (long long)std::llround(1000000.0 / cps));
}

void LeftAutoClicker::onEnable()
{
    stopThread();
    m_nextClickUs = 0;
    m_butterflyPendingSecond = false;
    m_exhaustUntilUs = 0;
    m_clicksSinceExhaust = 0;
    m_threadRunning.store(true, std::memory_order_release);
    m_thread = std::thread([this] { threadMain(); });
}

void LeftAutoClicker::onDisable()
{
    stopThread();
    m_nextClickUs = 0;
    {
        std::lock_guard<std::mutex> lock(m_cpsMutex);
        m_cpsCount = 0;
        memset(m_cpsTimestamps, 0, sizeof(m_cpsTimestamps));
    }
}

void LeftAutoClicker::stopThread()
{
    if (m_threadRunning.exchange(false, std::memory_order_acq_rel) && m_thread.joinable())
        m_thread.join();
}

void LeftAutoClicker::threadMain()
{
    while (m_threadRunning.load(std::memory_order_acquire))
    {
        const long long now = nowUs();
        JNIEnv* env = Java::GetEnv();

        const bool inGui = SDK::Minecraft ? SDK::Minecraft->IsInGuiState() : false;
        const bool isShiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        // Inventory clicking mode (SHIFT held while a GUI is open).
        const bool invClickActive = m_inventory->value && inGui && isShiftDown;

        // In-game clicking mode.
        const bool inGameActive = isEnabled() && CombatBridge::InGame() && !inGui;
        const bool holdingLmb = isLeftMouseHeld();

        bool canClick = false;
        if (invClickActive)
        {
            canClick = true;
        }
        else if (inGameActive && holdingLmb)
        {
            canClick = true;

            if (env && SDK::Minecraft && SDK::Minecraft->thePlayer)
            {
                jobject player = SDK::Minecraft->GetThePlayerObject();
                if (player)
                {
                    if (m_holdingWeapon->value && !isHoldingWeapon(env, player))
                        canClick = false;
                    if (m_notUsingItem->value && isUsingItem(env, player))
                        canClick = false;
                    env->DeleteLocalRef(player);
                }
            }

            if (canClick && m_breakBlocks->value && isTargetingBlock(env))
                canClick = false;
        }

        bool clicked = false;
        if (canClick && (m_nextClickUs == 0 || now >= m_nextClickUs))
        {
            // Simulated exhaustion: pause briefly after a burst of clicks.
            if (m_exhaust->value && !invClickActive)
            {
                if (now < m_exhaustUntilUs)
                {
                    waitUntilUs(m_exhaustUntilUs);
                }
                else
                {
                    m_clicksSinceExhaust++;
                    if (m_clicksSinceExhaust > static_cast<int>(15 + (g_rng() % 20)))
                    {
                        m_clicksSinceExhaust = 0;
                        m_exhaustUntilUs = now + (long long)((50 + (g_rng() % 100)) * 1000LL);
                    }
                }
            }

            // Re-read the clock: the exhaust wait above burns 50-150ms and
            // scheduling from the stale timestamp caused a click burst.
            const long long clickNow = nowUs();

            if (env)
            {
                if (env->ExceptionCheck()) env->ExceptionClear();

                setLwjglMouseButton(env, 0, true);
                CombatBridge::LeftClick();
                setLwjglMouseButton(env, 0, false);

                if (env->ExceptionCheck()) env->ExceptionClear();

                clicked = true;
                const long long nowMsVal = nowMs();
                {
                    std::lock_guard<std::mutex> lock(m_cpsMutex);
                    if (m_cpsCount < 60)
                        m_cpsTimestamps[m_cpsCount++] = nowMsVal;
                    else
                    {
                        memmove(m_cpsTimestamps, m_cpsTimestamps + 1, 59 * sizeof(long long));
                        m_cpsTimestamps[59] = nowMsVal;
                    }
                }

                if (m_clickPattern->index == 1 && !invClickActive) // Butterfly
                {
                    if (m_butterflyPendingSecond)
                    {
                        m_butterflyPendingSecond = false;
                        m_nextClickUs = clickNow + sampleDelayUs(false);
                    }
                    else
                    {
                        m_butterflyPendingSecond = true;
                        // Rapid double-click interval (10-25ms).
                        const long long doubleClickUs = (long long)((10 + (g_rng() % 15)) * 1000LL);
                        m_nextClickUs = clickNow + doubleClickUs;
                    }
                }
                else
                {
                    m_nextClickUs = clickNow + sampleDelayUs(invClickActive);
                }
            }
            else
            {
                m_nextClickUs = clickNow + 5000;
            }
        }

        long long nextWakeUs = (m_nextClickUs != 0) ? m_nextClickUs : (now + 10000);

        if (!canClick)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!clicked) m_nextClickUs = 0;
            continue;
        }

        if (nextWakeUs > now)
            waitUntilUs(nextWakeUs);
    }
}
