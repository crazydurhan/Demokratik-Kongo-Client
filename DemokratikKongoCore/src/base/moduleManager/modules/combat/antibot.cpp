#include "antibot.h"
#include "../../commonData.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/jniResolve.h"
#include "../../../util/logger.h"
#include <cstdio>

AntiBot::AntiBot()
    : Module("AntiBot", "Detects bot players and filters them from other modules.", Category::Combat)
{
    m_checkHeight = &add<BoolSetting>("Height Check", true);
    m_checkHeight->description = "Flag players with height <= 0.5 as bots.";

    m_checkSleeping = &add<BoolSetting>("Sleeping Check", true);
    m_checkSleeping->description = "Flag sleeping players as bots.";

    m_checkHealth = &add<BoolSetting>("Health Check", true);
    m_checkHealth->description = "Flag players with health <= 0 as bots.";

    m_checkInvisible = &add<BoolSetting>("Invisible Check", false);
    m_checkInvisible->description = "Flag invisible players as bots.";
}

std::string AntiBot::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%zu bots", s_botList.size());
    return buf;
}

void AntiBot::onTick()
{
    if (!CommonData::SanityCheck()) return;

    // Katman 6: throttle antibot detection to once every 10 ticks (~50ms at
    // 5ms loop, 100ms at 10ms). Players don't go from valid->bot->valid in
    // 5ms, but the iteration + JNI CallBooleanMethod is a hot spot in 3D
    // (20+ players). 10 ticks is well below the human reaction time that
    // CombatBridge.IsBotEntity actually depends on.
    static int s_ticksSinceLast = 100;
    if (++s_ticksSinceLast < 10) return;
    s_ticksSinceLast = 0;

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    static jmethodID isSleepingMid = nullptr;
    if (!isSleepingMid) {
        isSleepingMid = JniResolve::Method(env, StrayCache::entityPlayer_class,
            "()Z", "isPlayerSleeping");
    }

    s_heightBots.clear();
    s_sleepingBots.clear();
    s_healthBots.clear();
    s_invisibleBots.clear();

    // In-place walk: copying the list allocated a JNI global ref per player.
    std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
    for (const auto& pd : CommonData::nativePlayerList)
    {
        if (pd.isLocalPlayer) continue;
        if (pd.name.empty()) continue;

        if (m_checkHeight->value && pd.height <= 0.5f)
            s_heightBots.insert(pd.name);

        if (m_checkSleeping->value && isSleepingMid && pd.obj.Instance.Get())
        {
            bool sleeping = env->CallBooleanMethod(pd.obj.Instance.Get(), isSleepingMid);
            if (env->ExceptionCheck()) { env->ExceptionClear(); sleeping = false; }
            if (sleeping)
                s_sleepingBots.insert(pd.name);
        }

        if (m_checkHealth->value && pd.health <= 0.0f)
            s_healthBots.insert(pd.name);

        if (m_checkInvisible->value && pd.isInvisible)
            s_invisibleBots.insert(pd.name);
    }

    s_botList.clear();
    for (const auto& n : s_heightBots)    s_botList.insert(n);
    for (const auto& n : s_sleepingBots)  s_botList.insert(n);
    for (const auto& n : s_healthBots)    s_botList.insert(n);
    for (const auto& n : s_invisibleBots) s_botList.insert(n);
}

bool AntiBot::IsBot(const std::string& name)
{
    if (name.empty()) return false;
    return s_botList.find(name) != s_botList.end();
}

bool AntiBot::IsBotEntity(jobject entityObj)
{
    if (!entityObj) return false; // unknown entity != bot

    JNIEnv* env = Java::GetEnv();
    if (!env) return false;

    // Check if it's the local player
    if (SDK::Minecraft->thePlayer)
    {
        jobject localPlayer = SDK::Minecraft->thePlayer->GetInstance();
        if (localPlayer)
        {
            bool same = env->IsSameObject(entityObj, localPlayer);
            env->DeleteLocalRef(localPlayer);
            if (same) return false;
        }
    }

    std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
    for (const auto& pd : CommonData::nativePlayerList)
    {
        if (pd.obj.Instance.Get() && env->IsSameObject(pd.obj.Instance.Get(), entityObj))
            return s_botList.find(pd.name) != s_botList.end();
    }

    return false;
}
