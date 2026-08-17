#include "antiDebuff.h"

#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/jniResolve.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"
#include "../../../patcher/patcher.h"

AntiDebuff::AntiDebuff()
    : Module("Anti Debuff", "Hides negative visual effects (Blindness darkness, Nausea distortion) without altering movement mechanics.", Category::Render)
{
    m_blindness = &add<BoolSetting>("Blindness", true);
    m_blindness->description = "Removes visual darkness when Blindness potion effect is active.";

    m_nausea = &add<BoolSetting>("Nausea", true);
    m_nausea->description = "Removes screen warping and distortion when Nausea potion effect is active.";

    setEnabled(false);
}

void AntiDebuff::onEnable()
{
    Logger::Info("AntiDebuff", "Enabled");
}

void AntiDebuff::onDisable()
{
    Logger::Info("AntiDebuff", "Disabled");
}

void AntiDebuff::onTick()
{
    if (!isEnabled() || !SDK::Minecraft) return;

    // Blindness (Potion ID 15)
    if (m_blindness->value && SDK::Minecraft->thePlayer)
    {
        if (SDK::Minecraft->thePlayer->IsPotionActive(15))
        {
            SDK::Minecraft->thePlayer->RemovePotionEffect(15);
        }
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject localPlayer = SDK::Minecraft->GetThePlayerObject();
    if (!localPlayer) return;

    // Nausea (portal warp visual effect) uses timeInPortal / prevTimeInPortal on EntityPlayerSP.
    // Zeroing these float fields removes screen distortion visual effect without touching
    // active potion effects or interfering with server movement checks / anti-cheat.
    if (m_nausea->value && StrayCache::entityPlayerSP_timeInPortal && StrayCache::entityPlayerSP_prevTimeInPortal)
    {
        env->SetFloatField(localPlayer, StrayCache::entityPlayerSP_timeInPortal, 0.0f);
        if (env->ExceptionCheck()) env->ExceptionClear();

        env->SetFloatField(localPlayer, StrayCache::entityPlayerSP_prevTimeInPortal, 0.0f);
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    env->DeleteLocalRef(localPlayer);
}

void AntiDebuff::pushAll()
{
    // Intentionally empty — no Java patcher consumer exists for antidebuff flags;
    // the JNI potion/portal-time path in onTick is the actual implementation.
}


