#include "autoBlock.h"

#include "combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/net/minecraft/client/Minecraft.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"

#include <Windows.h>
#include <chrono>
#include <random>
#include <cmath>
#include <cstdio>

namespace
{
    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    jclass g_itemSwordClass = nullptr;

    bool isHoldingSword(JNIEnv* env, jobject player)
    {
        if (!env || !player) return false;

        if (!g_itemSwordClass)
        {
            jclass local = nullptr;
            if (Java::AssignClass("net.minecraft.item.ItemSword", local) && local)
                g_itemSwordClass = local;
        }
        if (!g_itemSwordClass) return false;

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
                        const bool isSword = env->IsInstanceOf(item, g_itemSwordClass) == JNI_TRUE;
                        env->DeleteLocalRef(item);
                        env->DeleteLocalRef(stack);
                        env->DeleteLocalRef(inv);
                        return isSword;
                    }
                    env->DeleteLocalRef(stack);
                }
                env->DeleteLocalRef(inv);
            }
        }

        return false;
    }
}

AutoBlock::AutoBlock()
    : Module("Auto Block", "Predicts incoming damage and automatically blocks your sword.", Category::Combat)
{
    m_range = &add<NumberSetting>("Range", 3.5f, 2.0f, 6.0f, 0.1f);
    m_range->suffix = " blocks";
    m_range->description = "Distance to target player at which Auto Block activates.";

    m_maxHurtTime = &add<NumberSetting>("Max Hurt Time", 200.0f, 0.0f, 500.0f, 10.0f);
    m_maxHurtTime->suffix = " ms";
    m_maxHurtTime->description = "How early to begin blocking before incoming damage.";

    m_maxHoldDuration = &add<NumberSetting>("Max Hold Duration", 150.0f, 10.0f, 500.0f, 10.0f);
    m_maxHoldDuration->suffix = " ms";
    m_maxHoldDuration->description = "Duration the sword remains blocked during a cycle.";

    m_forceAnimation = &add<BoolSetting>("Force Block Animation", false);
    m_forceAnimation->description = "Client-side cosmetic override to keep sword visually in block stance.";

    m_animOnlyInRange = &add<BoolSetting>("Animation Only In Range", true);
    m_animOnlyInRange->description = "Only display forced block animation when target is within range.";
    m_animOnlyInRange->visible = [this] { return m_forceAnimation && m_forceAnimation->value; };

    m_lagChance = &add<NumberSetting>("Lag Chance", 0.0f, 0.0f, 100.0f, 5.0f);
    m_lagChance->suffix = "%";
    m_lagChance->description = "Chance of delaying unblock to keep sword blocked server-side.";

    m_maxLagDuration = &add<NumberSetting>("Max Lag Duration", 150.0f, 0.0f, 500.0f, 10.0f);
    m_maxLagDuration->suffix = " ms";
    m_maxLagDuration->description = "Maximum duration of the server-side lag/blink unblock delay.";
    m_maxLagDuration->visible = [this] { return m_lagChance && m_lagChance->value > 0.0f; };

    m_preventDelayAttacks = &add<BoolSetting>("Prevent Delaying Attacks", true);
    m_preventDelayAttacks->description = "Immediately stops lagging when attacking so hits are never delayed.";

    m_blockAgainImmediately = &add<BoolSetting>("Block Again Immediately", false);
    m_blockAgainImmediately->description = "Blocks sword again directly after ending the lag phase.";

    m_requireLmb = &add<BoolSetting>("Require Left Click", true);
    m_requireLmb->description = "Only activate Auto Block while left mouse button is pressed.";

    m_requireRmb = &add<BoolSetting>("Require Right Click", false);
    m_requireRmb->description = "Only activate Auto Block while right mouse button is pressed.";

    m_requireDamage = &add<BoolSetting>("Require Recent Damage", false);
    m_requireDamage->description = "Only activate Auto Block if player recently took damage.";

    setEnabled(false);
}

std::string AutoBlock::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None || !m_range) return "";
    char buf[48];
    if (detail == SuffixDetail::Basic)
    {
        std::snprintf(buf, sizeof(buf), "%.1fb", m_range->value);
        return buf;
    }
    std::snprintf(buf, sizeof(buf), "%.1fb %.0fms", m_range->value, m_maxHurtTime->value);
    return buf;
}

void AutoBlock::onEnable()
{
    resetBlockState();
}

void AutoBlock::onDisable()
{
    resetBlockState();
}

void AutoBlock::resetBlockState()
{
    if (m_isBlocking)
        CombatBridge::RequestBlock(false);
    m_isBlocking = false;
    m_isLagging = false;
    m_blockStartMs = 0;
    m_lagStartMs = 0;
}

void AutoBlock::onTick()
{
    if (!isEnabled() || !CombatBridge::CanCombat() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        resetBlockState();
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!player) return;

    const bool hasSword = isHoldingSword(env, player);
    if (!hasSword)
    {
        resetBlockState();
        env->DeleteLocalRef(player);
        return;
    }

    const long long now = nowMs();

    // Track hurt time / recent damage
    if (StrayCache::entityLivingBase_hurtTime)
    {
        const int hurtTime = env->GetIntField(player, StrayCache::entityLivingBase_hurtTime);
        JniResolve::ClearException(env);
        if (hurtTime > 0)
        {
            m_lastDamageMs = now;
        }
    }

    // Evaluate input/damage conditions
    const bool isLmbPressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool isRmbPressed = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

    if (m_requireLmb->value && !isLmbPressed)
    {
        resetBlockState();
        env->DeleteLocalRef(player);
        return;
    }

    if (m_requireRmb->value && !isRmbPressed)
    {
        resetBlockState();
        env->DeleteLocalRef(player);
        return;
    }

    if (m_requireDamage->value && (now - m_lastDamageMs > 2000))
    {
        resetBlockState();
        env->DeleteLocalRef(player);
        return;
    }

    // Target raycast check within Range
    CombatBridge::EntityRayHit hit = CombatBridge::RaycastEntity(m_range->value, true);
    const bool targetInRange = hit.hit && hit.entity && (hit.distance <= m_range->value);

    if (hit.entity)
    {
        env->DeleteLocalRef(hit.entity);
    }

    if (!targetInRange)
    {
        if (m_isBlocking && !m_isLagging)
        {
            resetBlockState();
        }
        env->DeleteLocalRef(player);
        return;
    }

    // Handle Lag / Blink unblock phase
    if (m_isLagging)
    {
        if (m_preventDelayAttacks->value && isLmbPressed)
        {
            m_isLagging = false;
            resetBlockState();
        }
        else if (now - m_lagStartMs >= static_cast<long long>(m_maxLagDuration->value))
        {
            m_isLagging = false;
            if (m_blockAgainImmediately->value)
            {
                m_isBlocking = true;
                m_blockStartMs = now;
                CombatBridge::RequestBlock(true);
            }
            else
            {
                resetBlockState();
            }
        }

        env->DeleteLocalRef(player);
        return;
    }

    // Auto Block state machine
    if (m_isBlocking)
    {
        if (now - m_blockStartMs >= static_cast<long long>(m_maxHoldDuration->value))
        {
            bool triggerLag = false;
            if (m_lagChance->value > 0.0f)
            {
                static thread_local std::mt19937 rng{ std::random_device{}() };
                std::uniform_real_distribution<float> dist(0.0f, 100.0f);
                if (dist(rng) < m_lagChance->value)
                {
                    triggerLag = true;
                }
            }

            if (triggerLag)
            {
                m_isLagging = true;
                m_lagStartMs = now;
                CombatBridge::RequestBlock(false);
            }
            else
            {
                resetBlockState();
            }
        }
    }
    else
    {
        m_isBlocking = true;
        m_blockStartMs = now;
        CombatBridge::RequestBlock(true);
    }

    env->DeleteLocalRef(player);
}
