#include "safeWalk.h"
#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/jniResolve.h"
#include "../../../java/java.h"
#include "../../../util/logger.h"
#include <cmath>

SafeWalk::SafeWalk()
    : Module("SafeWalk", "Sneaks at block edges so you never walk off (vanilla shift behavior, no slowdown).", Category::Movement)
{
    setEnabled(false);
}

void SafeWalk::onEnable()
{
    Logger::Info("SafeWalk", "Enabled");
    m_wasSneaking = false;
}

void SafeWalk::onDisable()
{
    Logger::Info("SafeWalk", "Disabled");
    if (m_wasSneaking) {
        CombatBridge::RequestSneak(false);
        m_wasSneaking = false;
    }
}

void SafeWalk::onTick()
{
    if (!isEnabled() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        if (m_wasSneaking) { CombatBridge::RequestSneak(false); m_wasSneaking = false; }
        return;
    }

    CEntityPlayerSP* thePlayer = SDK::Minecraft->thePlayer;
    if (!thePlayer->GetInstance() || !thePlayer->GetOnGround())
    {
        if (m_wasSneaking) { CombatBridge::RequestSneak(false); m_wasSneaking = false; }
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return;

    jobject playerObj = thePlayer->GetInstance();

    Vector3 pos = thePlayer->GetPos();
    double posX = pos.x;
    double posY = pos.y;
    double posZ = pos.z;

    double motionX = StrayCache::entity_motionX ? env->GetDoubleField(playerObj, StrayCache::entity_motionX) : 0.0;
    double motionZ = StrayCache::entity_motionZ ? env->GetDoubleField(playerObj, StrayCache::entity_motionZ) : 0.0;
    if (env->ExceptionCheck()) env->ExceptionClear();

    double checkX = posX + (motionX > 0 ? 0.35 : (motionX < 0 ? -0.35 : 0));
    double checkZ = posZ + (motionZ > 0 ? 0.35 : (motionZ < 0 ? -0.35 : 0));

    jobject theWorld = SDK::Minecraft->GetTheWorldObject();
    if (!theWorld) return;

    // Check if block below check position is Air / null
    int blockX = (int)std::floor(checkX);
    int blockY = (int)std::floor(posY - 0.5);
    int blockZ = (int)std::floor(checkZ);

    // Everything is cached — FindClass/GetMethodID every tick is hot, and the
    // previous SRG fallback ran with a pending exception, which never resolves.
    static jclass blockPosClass = nullptr;
    static jmethodID blockPosInit = nullptr;
    static jmethodID isAirBlockMid = nullptr;
    static bool s_reflectionFailed = false;

    if (!blockPosClass && !s_reflectionFailed)
    {
        jclass local = nullptr;
        if (Java::AssignClass("net.minecraft.util.BlockPos", local) && local)
        {
            blockPosClass = (jclass)env->NewGlobalRef(local);
            env->DeleteLocalRef(local);
            blockPosInit = env->GetMethodID(blockPosClass, "<init>", "(III)V");
            JniResolve::ClearException(env);
        }
        if (!blockPosClass || !blockPosInit)
            s_reflectionFailed = true;
    }

    if (!isAirBlockMid && !s_reflectionFailed)
    {
        jclass worldClass = env->GetObjectClass(theWorld);
        if (worldClass)
        {
            isAirBlockMid = JniResolve::Method(env, worldClass,
                "(Lnet/minecraft/util/BlockPos;)Z", "isAirBlock");
            env->DeleteLocalRef(worldClass);
        }
        if (!isAirBlockMid)
            s_reflectionFailed = true;
    }

    bool isAir = false;
    if (!s_reflectionFailed)
    {
        jobject blockPosObj = env->NewObject(blockPosClass, blockPosInit, blockX, blockY, blockZ);
        JniResolve::ClearException(env);
        if (blockPosObj)
        {
            isAir = env->CallBooleanMethod(theWorld, isAirBlockMid, blockPosObj);
            if (env->ExceptionCheck()) { env->ExceptionClear(); isAir = false; }
            env->DeleteLocalRef(blockPosObj);
        }
    }
    env->DeleteLocalRef(theWorld);

    // Edge detected below the movement path: press the sneak keybind on the
    // client thread. Vanilla Entity.moveEntity() then clips movement at block
    // borders exactly like holding shift — no slowdown, no motion zeroing.
    if (isAir)
    {
        if (!m_wasSneaking) { CombatBridge::RequestSneak(true); m_wasSneaking = true; }
    }
    else if (m_wasSneaking)
    {
        CombatBridge::RequestSneak(false);
        m_wasSneaking = false;
    }
}
