#include "fastMine.h"

#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/net/minecraft/client/Minecraft.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"

#include <cmath>
#include <algorithm>

FastMine::FastMine()
    : Module("Fast Mine", "Allows you to mine blocks faster than vanilla.", Category::Utility)
{
    m_speed = &add<NumberSetting>("Speed Multiplier", 1.20f, 1.00f, 2.00f, 0.01f);
    m_speed->suffix = "x";
    m_speed->description = "Block mining speed multiplier (Vanilla max is 1.42x).";

    m_breakDelay = &add<NumberSetting>("Break Delay", 0.0f, 0.0f, 250.0f, 10.0f);
    m_breakDelay->suffix = " ms";
    m_breakDelay->description = "Delay after breaking a block before you can mine again (Vanilla is 250 ms).";

    m_alwaysDecreaseDelay = &add<BoolSetting>("Always Decrease Break Delay", true);
    m_alwaysDecreaseDelay->description = "Break delay continuously counts down each tick even when not mining.";

    setEnabled(false);
}

void FastMine::onEnable()
{
    m_prevDamage = 0.0f;
}

void FastMine::onDisable()
{
    m_prevDamage = 0.0f;
}

void FastMine::clientTick()
{
    if (!isEnabled() || !CombatBridge::InGame() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        m_prevDamage = 0.0f;
        return;
    }

    if (SDK::Minecraft->IsInGuiState())
    {
        m_prevDamage = 0.0f;
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    if (!StrayCache::minecraft_playerController
        || !StrayCache::playerControllerMP_curBlockDamageMP
        || !StrayCache::playerControllerMP_blockHitDelay
        || !StrayCache::playerControllerMP_isHittingBlock)
    {
        return;
    }

    jobject controller = SDK::Minecraft->GetPlayerControllerObject();
    if (!controller) return;

    const bool isHittingBlock = env->GetBooleanField(controller, StrayCache::playerControllerMP_isHittingBlock) == JNI_TRUE;
    JniResolve::ClearException(env);

    int currentHitDelay = env->GetIntField(controller, StrayCache::playerControllerMP_blockHitDelay);
    JniResolve::ClearException(env);

    // 1. Break Delay Management (250ms = 5 ticks, 50ms per tick)
    const int targetTicks = static_cast<int>(std::floor(m_breakDelay->value / 50.0f));
    if (currentHitDelay > targetTicks)
    {
        env->SetIntField(controller, StrayCache::playerControllerMP_blockHitDelay, targetTicks);
        JniResolve::ClearException(env);
        currentHitDelay = targetTicks;
    }

    // 2. Always Decrease Break Delay
    if (m_alwaysDecreaseDelay->value && !isHittingBlock && currentHitDelay > 0)
    {
        env->SetIntField(controller, StrayCache::playerControllerMP_blockHitDelay, currentHitDelay - 1);
        JniResolve::ClearException(env);
    }

    // 3. Mining Speed Multiplier
    if (isHittingBlock)
    {
        const float curDamage = env->GetFloatField(controller, StrayCache::playerControllerMP_curBlockDamageMP);
        JniResolve::ClearException(env);

        if (curDamage > m_prevDamage && m_speed->value > 1.0f)
        {
            const float delta = curDamage - m_prevDamage;
            if (delta > 0.0001f)
            {
                const float extra = delta * (m_speed->value - 1.0f);
                float newDamage = curDamage + extra;
                if (newDamage > 1.0f)
                    newDamage = 1.0f;

                env->SetFloatField(controller, StrayCache::playerControllerMP_curBlockDamageMP, newDamage);
                JniResolve::ClearException(env);
                m_prevDamage = newDamage;
            }
            else
            {
                m_prevDamage = curDamage;
            }
        }
        else
        {
            m_prevDamage = curDamage;
        }
    }
    else
    {
        m_prevDamage = 0.0f;
    }

    env->DeleteLocalRef(controller);
}
