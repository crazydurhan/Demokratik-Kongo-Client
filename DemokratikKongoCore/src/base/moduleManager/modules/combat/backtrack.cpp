#include "backtrack.h"

#include "combatBridge.h"
#include "friends.h"
#include "antibot.h"
#include "../../commonData.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/net/minecraft/client/Minecraft.h"
#include "../../../java/java.h"
#include "../../../patcher/patcher.h"
#include "../../../util/format.h"
#include "../../../util/logger.h"

#include <Windows.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // A target strafing out of the distance window for a tick or two must
    // not kill the lag session; only a sustained loss ends it.
    constexpr long long kTargetLostGraceMs = 400;

    jclass g_itemSwordClass = nullptr;
    jclass g_itemAxeClass   = nullptr;
}

Backtrack::Backtrack()
    : Module("Backtrack", "Lags target player movement packets when moving out of reach.", Category::Combat)
{
    m_minDistance = &add<NumberSetting>("Min Distance", 1.0f, 1.0f, 6.0f, 0.1f);
    m_minDistance->suffix = " blocks";
    m_minDistance->description = "Minimum distance to target required for Backtrack activation.";

    m_maxDistance = &add<NumberSetting>("Max Distance", 4.0f, 1.0f, 6.0f, 0.1f);
    m_maxDistance->suffix = " blocks";
    m_maxDistance->description = "Maximum distance to target required for Backtrack activation.";

    m_delay = &add<IntRangeSetting>("Delay", 100, 250, 0, 1000, 10);
    m_delay->suffix = " ms";
    m_delay->description = "Randomized lag duration per Backtrack session (min .. max).";

    m_maxHurtTime = &add<NumberSetting>("Maximum Hurt Time", 200.0f, 0.0f, 500.0f, 10.0f);
    m_maxHurtTime->suffix = " ms";
    m_maxHurtTime->description = "Skips activation while the target's hurt animation is newer than this (0 = off).";

    m_cooldown = &add<NumberSetting>("Cooldown", 1.0f, 0.0f, 5.0f, 0.1f);
    m_cooldown->suffix = " s";
    m_cooldown->description = "Delay after Backtrack deactivates before it can activate again.";

    m_disableOnHit = &add<BoolSetting>("Disable On Hit", true);
    m_disableOnHit->description = "Immediately stops lagging if local player takes damage/knockback.";

    m_holdingWeaponOnly = &add<BoolSetting>("Holding Weapon Only", false);
    m_holdingWeaponOnly->description = "Only activate Backtrack while holding a weapon (Sword or Axe).";

    m_onlyWhenTargeting = &add<BoolSetting>("Only When Targeting", false);
    m_onlyWhenTargeting->description = "Only lag while you recently hit a player (legacy behavior). Off = the distance window decides.";

    // Real Position Indicator
    m_showRealPos = &add<BoolSetting>("Real Position Indicator", true);
    m_showRealPos->description = "Renders a 3D box at the player's true un-lagged position.";

    m_boxColor = &add<ColorSetting>("Box Color", Color{ 1.0f, 0.2f, 0.2f, 0.6f });
    m_boxColor->visible = [this] { return m_showRealPos && m_showRealPos->value; };

    m_lineWidth = &add<NumberSetting>("Line Width", 2.0f, 1.0f, 5.0f, 0.5f);
    m_lineWidth->suffix = " px";
    m_lineWidth->visible = [this] { return m_showRealPos && m_showRealPos->value; };

    m_filled = &add<BoolSetting>("Filled", false);
    m_filled->description = "Fills the 3D real position box.";
    m_filled->visible = [this] { return m_showRealPos && m_showRealPos->value; };

    m_headRotation = &add<BoolSetting>("Apply Head Rotation", true);
    m_headRotation->description = "Rotates the indicator box according to target head rotation.";
    m_headRotation->visible = [this] { return m_showRealPos && m_showRealPos->value; };

    setEnabled(false);
}

std::string Backtrack::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None || !m_maxDistance || !m_delay) return "";
    char buf[48];
    if (detail == SuffixDetail::Basic)
    {
        std::snprintf(buf, sizeof(buf), "%.1f-%.1fb", m_minDistance->value, m_maxDistance->value);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.1f-%.1fb %d-%dms",
        m_minDistance->value, m_maxDistance->value, m_delay->getLow(), m_delay->getHigh());
    return buf;
}

bool Backtrack::isHoldingWeapon(JNIEnv* env, jobject player)
{
    if (!env || !player) return false;

    if (!g_itemSwordClass) Java::AssignClass("net.minecraft.item.ItemSword", g_itemSwordClass);
    if (!g_itemAxeClass)   Java::AssignClass("net.minecraft.item.ItemAxe", g_itemAxeClass);

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
                    bool isWpn = (g_itemSwordClass && env->IsInstanceOf(item, g_itemSwordClass))
                              || (g_itemAxeClass   && env->IsInstanceOf(item, g_itemAxeClass));
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

int Backtrack::resolveEntityId(JNIEnv* env, jobject entity)
{
    if (!env || !entity || !StrayCache::entity_class) return -1;

    static jmethodID s_getEntityId = nullptr;
    if (!s_getEntityId)
        s_getEntityId = JniResolve::Method(env, StrayCache::entity_class, "()I", "getEntityId");
    if (!s_getEntityId) return -1;

    const int id = env->CallIntMethod(entity, s_getEntityId);
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return -1;
    }
    return id;
}

long long Backtrack::rollSessionMs() const
{
    const int lo = m_delay->getLow();
    const int hi = m_delay->getHigh();
    const int span = hi - lo;
    return static_cast<long long>(lo + (span <= 0 ? 0 : std::rand() % (span + 1)));
}

void Backtrack::endSession(long long now, bool startCooldown)
{
    m_isActive = false;
    if (startCooldown)
        m_lastActiveMs = now;
    m_sessionMs = 0;
    m_targetId = -1;
}

void Backtrack::onEnable()
{
    m_isActive = false;
    m_lastActiveMs = 0;
    m_activeStartMs = 0;
    m_lastSeenMs = 0;
    m_sessionMs = 0;
    m_targetId = -1;
    pushAll();
}

void Backtrack::onDisable()
{
    m_isActive = false;
    m_targetId = -1;
    Patcher::put("backtrack_enabled", "false");
    Patcher::put("backtrack_target_id", "-1");
    Patcher::put("backtrack_box_enabled", "false");
}

void Backtrack::pushAll()
{
    const bool sessionOn = isEnabled() && m_isActive;
    Patcher::put("backtrack_enabled", bstr(sessionOn));
    // While idle, push the range high so a fresh session's first packets
    // already have a sane hold horizon before the roll lands.
    Patcher::put("backtrack_max_delay", istr(static_cast<int>(
        m_sessionMs > 0 ? m_sessionMs : m_delay->getHigh())));
    Patcher::put("backtrack_disable_on_hit", bstr(m_disableOnHit->value));
    Patcher::put("backtrack_only_when_targeting", bstr(m_onlyWhenTargeting->value));
    Patcher::put("backtrack_target_id", istr(sessionOn ? m_targetId : -1));

    const bool boxOn = sessionOn && m_showRealPos->value;
    const Color& c = m_boxColor->value;
    Patcher::put("backtrack_box_enabled", bstr(boxOn));
    Patcher::put("backtrack_box_color_r", fstr(c.r));
    Patcher::put("backtrack_box_color_g", fstr(c.g));
    Patcher::put("backtrack_box_color_b", fstr(c.b));
    Patcher::put("backtrack_box_color_a", fstr(c.a));
    Patcher::put("backtrack_box_width",   fstr(m_lineWidth->value));
    Patcher::put("backtrack_box_filled",  bstr(m_filled->value));
    Patcher::put("backtrack_box_rotate",  bstr(m_headRotation->value));
}

void Backtrack::onTick()
{
    if (!isEnabled() || !CombatBridge::CanCombat() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        if (m_isActive)
        {
            endSession(0, false);
            pushAll();
        }
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!player) return;

    const long long now = nowMs();

    // Disable On Hit: local player takes damage/knockback -> drop the session.
    if (m_disableOnHit->value && StrayCache::entityLivingBase_hurtTime)
    {
        const int hurtTime = env->GetIntField(player, StrayCache::entityLivingBase_hurtTime);
        JniResolve::ClearException(env);
        if (hurtTime > 0)
        {
            if (m_isActive)
            {
                endSession(now, true);
                pushAll();
            }
            env->DeleteLocalRef(player);
            return;
        }
    }

    // Weapon condition check
    if (m_holdingWeaponOnly->value && !isHoldingWeapon(env, player))
    {
        if (m_isActive)
        {
            endSession(now, true);
            pushAll();
        }
        env->DeleteLocalRef(player);
        return;
    }

    // Nearest valid player inside the distance window
    Vector3 localPos = SDK::Minecraft->thePlayer->GetPos();

    const float minDist = m_minDistance->value;
    const float maxDist = m_maxDistance->value;

    // Scan the live list in place and take a single local ref for the winner.
    // Copying the whole vector allocated a JNI global ref per player every tick.
    jobject bestObj = nullptr;
    bool haveBest = false;
    {
        float bestDist = maxDist + 1.0f;
        std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
        for (const auto& pd : CommonData::nativePlayerList)
        {
            if (pd.isLocalPlayer) continue;
            if (pd.name.empty() && pd.displayName.empty()) continue;
            if (Friends::IsFriend(pd.name)) continue;
            if (AntiBot::IsBot(pd.name)) continue;

            const float dx = pd.pos.x - localPos.x;
            const float dy = pd.pos.y - localPos.y;
            const float dz = pd.pos.z - localPos.z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (dist >= minDist && dist <= maxDist && dist < bestDist)
            {
                bestDist = dist;
                haveBest = true;
                if (bestObj) env->DeleteLocalRef(bestObj);
                bestObj = pd.obj.Instance.Get() ? env->NewLocalRef(pd.obj.Instance.Get()) : nullptr;
            }
        }
    }

    struct BestRefGuard
    {
        JNIEnv* env;
        jobject obj;
        ~BestRefGuard() { if (env && obj) env->DeleteLocalRef(obj); }
    } bestGuard{ env, bestObj };

    // Max Hurt Time gate: skip candidates whose hurt animation is too fresh.
    if (haveBest && bestObj && m_maxHurtTime->value > 0.0f && StrayCache::entityLivingBase_hurtTime)
    {
        const int targetHurt = env->GetIntField(bestObj, StrayCache::entityLivingBase_hurtTime);
        JniResolve::ClearException(env);
        if (targetHurt * 50.0f > m_maxHurtTime->value)
            haveBest = false;
    }

    const bool cooldownActive = (now - m_lastActiveMs) < static_cast<long long>(m_cooldown->value * 1000.0f);

    if (haveBest)
    {
        if (!m_isActive && !cooldownActive)
        {
            m_isActive = true;
            m_activeStartMs = now;
            m_sessionMs = rollSessionMs();
        }

        if (m_isActive)
        {
            m_lastSeenMs = now;
            m_targetId = resolveEntityId(env, bestObj);

            // Session budget spent -> release + cooldown.
            if (now - m_activeStartMs >= m_sessionMs)
                endSession(now, true);
        }
    }
    else if (m_isActive && now - m_lastSeenMs >= kTargetLostGraceMs)
    {
        // Target left the window and stayed out past the grace window.
        endSession(now, true);
    }

    pushAll();
    env->DeleteLocalRef(player);
}
