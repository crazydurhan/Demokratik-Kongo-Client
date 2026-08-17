#include "reach.h"

#include "../../commonData.h"
#include "combatBridge.h"
#include "../../../menu/menu.h"
#include "../../../util/math/math.h"
#include "../../../util/math/geometry.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/sdk.h"
#include "../../../patcher/patcher.h"

#include <random>
#include <algorithm>
#include <string>
#include <mutex>

Reach::Reach()
    : Module("Reach", "Extends melee reach distance safely and reliably.", Category::Combat)
{
    // Ceiling kept at 4.0: 1.8.9 anti-cheats flag sustained reach beyond ~4
    // almost instantly, so the slider stops at the usable envelope.
    m_range = &add<FloatRangeSetting>("Range", 3.1f, 3.5f, 3.0f, 4.0f, 0.05f);
    m_range->suffix = " blocks";

    m_chance = &add<IntRangeSetting>("Chance", 100, 100, 0, 100, 1);
    m_chance->suffix = "%";

    m_chanceMode = &add<EnumSetting>("Chance Mode", std::vector<const char*>{"Normal", "Advanced"}, 0);

    m_verticalCheck = &add<BoolSetting>("Vertical Check", true);
    m_onlySprinting = &add<BoolSetting>("Only While Sprinting", false);
    m_disableInWater = &add<BoolSetting>("Disable in Water", true);
    m_throughBlocks = &add<BoolSetting>("Through Blocks", false);
}

namespace
{
    constexpr float kVanillaReach = 3.0f;

    inline void applyReach(float v, float& mirror, bool throughBlocks)
    {
        mirror = v;
        CombatBridge::SetReach(v);
        const bool extended = v > kVanillaReach + 0.01f;
        Patcher::put("reach_enabled", extended ? "true" : "false");
        Patcher::put("reach_through_blocks", (extended && throughBlocks) ? "true" : "false");
    }
}

void Reach::onEnable()
{
    // Freeze at vanilla for the first tick so an extended value from the
    // previous session can never leak into the first attack after toggling.
    m_lastTargetName.clear();
    m_rolledReach = kVanillaReach;
    m_ticksWithTarget = 0;
    applyReach(kVanillaReach, m_currentReach, false);
}

void Reach::onDisable()
{
    m_lastTargetName.clear();
    m_rolledReach = kVanillaReach;
    m_ticksWithTarget = 0;
    applyReach(kVanillaReach, m_currentReach, false);
    Patcher::put("reach_enabled", "false");
    Patcher::put("reach_through_blocks", "false");
}

std::string Reach::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    char buf[32];
    if (detail == SuffixDetail::Basic) {
        std::snprintf(buf, sizeof(buf), "%.1fb", m_currentReach);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f / %.1f-%.1fb",
            m_currentReach, m_range->getLow(), m_range->getHigh());
    }
    return buf;
}

Color Reach::arrayListColorOverride() const
{
    if (m_currentReach > kVanillaReach + 0.05f) {
        return Color{ 1.0f, 0.32f, 0.32f, 1.0f };
    }
    return Color{ -1, -1, -1, -1 };
}

void Reach::onTick()
{
    const bool throughBlocks = m_throughBlocks && m_throughBlocks->value;

    if (!CombatBridge::InGame()) {
        applyReach(kVanillaReach, m_currentReach, false);
        return;
    }

    CEntityPlayerSP* thePlayer = SDK::Minecraft->thePlayer;
    if (!thePlayer) {
        applyReach(kVanillaReach, m_currentReach, false);
        return;
    }

    if (m_onlySprinting->value && !thePlayer->IsSprinting()) {
        applyReach(kVanillaReach, m_currentReach, false);
        return;
    }

    if (m_disableInWater->value && thePlayer->IsInWater()) {
        applyReach(kVanillaReach, m_currentReach, false);
        return;
    }

    // Roll reach when crosshair ray hits a player beyond vanilla range.
    const float maxConfigured = m_range->getHigh();
    CombatBridge::EntityRayHit aimHit = CombatBridge::RaycastEntity(maxConfigured);
    JNIEnv* env = Java::GetEnv();

    // The raycast local ref must stay alive until the very end of this
    // function (the MovingObjectPosition write below uses it), so release
    // it exactly once from a scope guard instead of on every exit path.
    struct EntityRefGuard
    {
        JNIEnv* env;
        jobject obj;
        ~EntityRefGuard() { if (env && obj) env->DeleteLocalRef(obj); }
    } entityGuard{ env, aimHit.entity };

    std::string targetName;
    float targetDist = 0.0f;
    bool hasExtendedTarget = false;

    if (aimHit.hit && aimHit.entity && aimHit.distance > kVanillaReach && env)
    {
        std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
        for (const auto& pd : CommonData::nativePlayerList)
        {
            if (pd.obj.Instance.Get() && env->IsSameObject(pd.obj.Instance.Get(), aimHit.entity))
            {
                targetName = pd.name;
                targetDist = aimHit.distance;
                hasExtendedTarget = true;
                break;
            }
        }
    }

    if (!hasExtendedTarget)
    {
        m_lastTargetName.clear();
        applyReach(kVanillaReach, m_currentReach, false);
        return;
    }

    Vector3 localPos = thePlayer->GetPos();

    if (m_verticalCheck->value)
    {
        std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
        for (const auto& pd : CommonData::nativePlayerList)
        {
            if (pd.name == targetName)
            {
                if (fabsf(localPos.y - pd.pos.y) > 1.5f)
                {
                    applyReach(kVanillaReach, m_currentReach, false);
                    return;
                }
                break;
            }
        }
    }

    if (m_lastTargetName != targetName || m_ticksWithTarget > 10)
    {
        m_lastTargetName = targetName;
        m_ticksWithTarget = 0;

        static std::random_device rd;
        static std::mt19937 gen(rd());

        // Chance: effective % is random in [min, max], then classic roll (not midpoint).
        int chanceLo = m_chance->getLow();
        int chanceHi = m_chance->getHigh();
        std::uniform_int_distribution<int> chanceDist(chanceLo, chanceHi);
        int effectiveChance = chanceDist(gen);

        if (m_chanceMode->index == 1)
        {
            std::lock_guard<std::mutex> lock(CommonData::playerListMutex);
            for (const auto& pd : CommonData::nativePlayerList)
            {
                if (pd.name != targetName) continue;

                bool airborne = false;
                if (env && StrayCache::entity_onGround && pd.obj.Instance.Get())
                {
                    airborne = !env->GetBooleanField(pd.obj.Instance.Get(), StrayCache::entity_onGround);
                    if (env->ExceptionCheck()) { env->ExceptionClear(); airborne = false; }
                }
                bool hurt = false;

                if (env && StrayCache::entityLivingBase_hurtTime && pd.obj.Instance.Get())
                {
                    int hurtTime = env->GetIntField(pd.obj.Instance.Get(), StrayCache::entityLivingBase_hurtTime);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    hurt = hurtTime > 0;
                }

                float targetYaw = 0.0f;
                if (env && StrayCache::entity_rotationYaw && pd.obj.Instance.Get())
                {
                    targetYaw = env->GetFloatField(pd.obj.Instance.Get(), StrayCache::entity_rotationYaw);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }

                // Facing away = the target's look direction points away from
                // us: compare its yaw against the yaw of the target->local
                // vector, not against our own view yaw.
                const float toLocalYaw = Math::getAngles(pd.pos, localPos).x;
                const bool facingAway =
                    fabsf(Math::wrapAngleTo180(targetYaw - toLocalYaw)) > 90.0f;

                if (facingAway || airborne || hurt)
                    effectiveChance = std::min(100, (int)(effectiveChance * 1.5f));
                break;
            }
        }

        std::uniform_int_distribution<int> rollDist(0, 99);
        if (rollDist(gen) < effectiveChance)
        {
            float lo = m_range->getLow();
            float hi = m_range->getHigh();
            std::uniform_real_distribution<float> distDis(lo, hi);
            m_rolledReach = distDis(gen);
        }
        else
        {
            m_rolledReach = kVanillaReach;
        }
    }
    else
    {
        m_ticksWithTarget++;
    }

    // Only expose extended reach while aiming at a valid extended target.
    if (hasExtendedTarget && targetDist > kVanillaReach && m_rolledReach > kVanillaReach)
    {
        applyReach(m_rolledReach, m_currentReach, throughBlocks);
        if (aimHit.hit && aimHit.entity)
        {
            if (env && StrayCache::movingObjectPosition_class && StrayCache::movingObjectPosition_initEntity && StrayCache::minecraft_objectMouseOver)
            {
                jobject mop = env->NewObject(StrayCache::movingObjectPosition_class, StrayCache::movingObjectPosition_initEntity, aimHit.entity);
                if (env->ExceptionCheck()) { env->ExceptionClear(); }
                if (mop)
                {
                    jobject mcInst = SDK::Minecraft->GetInstance();
                    if (mcInst)
                    {
                        env->SetObjectField(mcInst, StrayCache::minecraft_objectMouseOver, mop);
                        JniResolve::ClearException(env);
                    }
                    env->DeleteLocalRef(mop);
                }
            }
        }
    }
    else
    {
        applyReach(kVanillaReach, m_currentReach, false);
    }
}

bool Reach::tryReachAttack()
{
    if (!CombatBridge::InGame() || m_currentReach <= kVanillaReach + 0.01f)
        return false;

    CombatBridge::EntityRayHit hit = CombatBridge::RaycastEntity(m_currentReach);
    if (!hit.hit || !hit.entity)
        return false;

    JNIEnv* env = Java::GetEnv();

    const bool extended = hit.distance > kVanillaReach && hit.distance <= m_currentReach;
    if (!extended)
    {
        if (env)
            env->DeleteLocalRef(hit.entity);
        return false;
    }

    CombatBridge::LeftClick();
    if (env)
        env->DeleteLocalRef(hit.entity);
    return true;
}
