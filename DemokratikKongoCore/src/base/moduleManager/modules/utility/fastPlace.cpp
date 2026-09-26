#include "fastPlace.h"

#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/net/minecraft/client/Minecraft.h"
#include "../../../sdk/net/minecraft/item/ItemStack.h"
#include "../../../java/java.h"

#include <Windows.h>
#include <cmath>
#include <random>

FastPlace::FastPlace()
    : Module("FastPlace", "Reduces or removes the right-click block placement delay.", Category::Utility)
{
    m_delay = &add<NumberSetting>("Delay", 0.0f, 0.0f, 4.0f, 1.0f);
    m_delay->suffix = " ticks";

    m_mode = &add<EnumSetting>("Mode", std::vector<const char*>{ "Normal", "Hold" }, 0);
    m_blocksOnly = &add<BoolSetting>("Blocks Only", true);
    m_autoSwitch = &add<BoolSetting>("Auto Switch Blocks", false);
    m_autoSwitch->description = "When the held block stack runs out, switch to another hotbar slot with blocks.";

    m_aimLock = &add<BoolSetting>("Aim Lock", false);
    m_aimLock->description = "Snap view to exact cardinal yaw (N 180, E -90, S 0, W 90) with pitch 0 while placing.";

    // Varsayilan kapali: acikken bazi tiklerde 1 tick gecikme birakir (anti-cheat)
    m_jitter = &add<BoolSetting>("Randomization", false);
    m_jitterChance = &add<NumberSetting>("Skip Chance", 20.0f, 0.0f, 50.0f, 1.0f);
    m_jitterChance->suffix = " %";
    m_jitterChance->visible = [this]{ return m_jitter->value; };
}

std::string FastPlace::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None)
        return "";

    char buf[48];
    const int delay = static_cast<int>(std::lround(m_delay->value));
    if (detail == SuffixDetail::Basic)
    {
        std::snprintf(buf, sizeof(buf), "%d tick", delay);
        return buf;
    }

    std::snprintf(buf, sizeof(buf), "%d tick%s",
        delay,
        m_mode->index == 1 ? " [Hold]" : "");
    return buf;
}

void FastPlace::onDisable()
{
    // If we reduced the placement delay, restore the vanilla 4-tick timer so
    // placement doesn't stay stuck faster than vanilla after the module turns off.
    if (SDK::Minecraft && SDK::Minecraft->IsReady() && StrayCache::minecraft_rightClickDelayTimer)
    {
        const int current = SDK::Minecraft->GetRightClickDelayTimer();
        if (current >= 0 && current < 4)
            SDK::Minecraft->SetRightClickDelayTimer(4);
    }
}

bool FastPlace::isRightMouseActive() const
{
    return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

bool FastPlace::isBlockStack(JNIEnv* env, jobject stack, int* outCount) const
{
    if (!env || !stack || !StrayCache::itemBlock_class || !StrayCache::itemStack_getItem)
        return false;

    jobject item = env->CallObjectMethod(stack, StrayCache::itemStack_getItem);
    JniResolve::ClearException(env);
    if (!item)
        return false;

    const bool isBlock = env->IsInstanceOf(item, StrayCache::itemBlock_class) == JNI_TRUE;
    int count = 0;
    if (isBlock && StrayCache::itemStack_stackSize)
        count = env->GetIntField(stack, StrayCache::itemStack_stackSize);

    env->DeleteLocalRef(item);

    if (!isBlock || count <= 0)
        return false;

    if (outCount)
        *outCount = count;
    return true;
}

bool FastPlace::isHoldingPlaceableBlock() const
{
    if (!StrayCache::itemBlock_class || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return false;

    JNIEnv* env = Java::GetEnv();
    if (!env || !StrayCache::inventoryPlayer_getCurrentItem)
        return false;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!player)
        return false;

    bool isBlock = false;
    if (StrayCache::entityPlayer_inventory)
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
                    isBlock = env->IsInstanceOf(item, StrayCache::itemBlock_class) == JNI_TRUE;
                    env->DeleteLocalRef(item);
                }
                env->DeleteLocalRef(stack);
            }
            env->DeleteLocalRef(inv);
        }
    }

    env->DeleteLocalRef(player);
    return isBlock;
}

bool FastPlace::canRun() const
{
    if (!CombatBridge::InGame())
        return false;

    if (!StrayCache::minecraft_rightClickDelayTimer)
        return false;

    if (m_mode->index == 1 && !isRightMouseActive())
        return false;

    if (m_blocksOnly->value && !isHoldingPlaceableBlock())
        return false;

    return true;
}

void FastPlace::applyDelay(int targetDelay)
{
    // 4 tick = vanilla; dokunma
    if (targetDelay >= 4)
        return;

    if (targetDelay == 0)
    {
        if (m_jitter->value)
        {
            static thread_local std::mt19937 gen{ std::random_device{}() };
            std::uniform_real_distribution<float> dist(0.0f, 100.0f);
            if (dist(gen) < m_jitterChance->value)
            {
                CombatBridge::RequestRightClickDelay(1);
                return;
            }
        }

        CombatBridge::RequestRightClickDelay(0);
        return;
    }

    const int current = SDK::Minecraft->GetRightClickDelayTimer();
    if (current == 4)
        CombatBridge::RequestRightClickDelay(targetDelay);
}

void FastPlace::trySwitchToBlockSlot()
{
    if (!m_autoSwitch->value)
        return;
    if (!CombatBridge::InGame())
        return;
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return;
    if (SDK::Minecraft->IsInGuiState())
        return;
    if (!isRightMouseActive())
        return;
    if (!StrayCache::entityPlayer_inventory
        || !StrayCache::inventoryPlayer_currentItem
        || !StrayCache::inventoryPlayer_mainInventory)
        return;

    JNIEnv* env = Java::GetEnv();
    if (!env)
        return;

    jobject player = SDK::Minecraft->GetThePlayerObject();
    if (!player)
        return;

    jobject inv = env->GetObjectField(player, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!inv) {
        env->DeleteLocalRef(player);
        return;
    }

    const int currentSlot = env->GetIntField(inv, StrayCache::inventoryPlayer_currentItem);
    JniResolve::ClearException(env);

    jobjectArray mainInv = (jobjectArray)env->GetObjectField(inv, StrayCache::inventoryPlayer_mainInventory);
    JniResolve::ClearException(env);
    if (!mainInv) {
        env->DeleteLocalRef(inv);
        env->DeleteLocalRef(player);
        return;
    }

    bool needsSwitch = true;
    if (currentSlot >= 0 && currentSlot <= 8) {
        jobject currentStack = env->GetObjectArrayElement(mainInv, currentSlot);
        JniResolve::ClearException(env);
        if (currentStack) {
            needsSwitch = !isBlockStack(env, currentStack);
            env->DeleteLocalRef(currentStack);
        }
    }

    if (!needsSwitch) {
        env->DeleteLocalRef(mainInv);
        env->DeleteLocalRef(inv);
        env->DeleteLocalRef(player);
        return;
    }

    int bestSlot = -1;
    int bestCount = 0;
    for (int slot = 0; slot < 9; ++slot) {
        if (slot == currentSlot)
            continue;

        jobject stack = env->GetObjectArrayElement(mainInv, slot);
        JniResolve::ClearException(env);
        if (!stack)
            continue;

        int count = 0;
        if (isBlockStack(env, stack, &count) && count > bestCount) {
            bestCount = count;
            bestSlot = slot;
        }
        env->DeleteLocalRef(stack);
    }

    if (bestSlot >= 0 && bestSlot != currentSlot) {
        CombatBridge::RequestHotbarSlot(bestSlot);
    }

    env->DeleteLocalRef(mainInv);
    env->DeleteLocalRef(inv);
    env->DeleteLocalRef(player);
}

float FastPlace::normalizeYaw(float yaw)
{
    while (yaw >= 180.f)
        yaw -= 360.f;
    while (yaw < -180.f)
        yaw += 360.f;
    return yaw;
}

float FastPlace::cardinalLockYaw(float yaw)
{
    const float n = normalizeYaw(yaw);

    if (n >= -45.f && n < 45.f)
        return 0.f;    // South
    if (n >= 45.f && n < 135.f)
        return 90.f;   // West
    if (n >= -135.f && n < -45.f)
        return -90.f;  // East
    return 180.f;      // North
}

void FastPlace::applyAimLock() const
{
    if (!m_aimLock->value)
        return;
    if (!CombatBridge::InGame())
        return;
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer)
        return;
    if (SDK::Minecraft->IsInGuiState())
        return;
    if (!isRightMouseActive())
        return;
    if (m_blocksOnly->value && !isHoldingPlaceableBlock())
        return;

    CEntityPlayerSP* player = SDK::Minecraft->thePlayer;
    const Vector2 current = player->GetAngles();
    const float lockedYaw = cardinalLockYaw(current.x);

    if (current.x == lockedYaw && current.y == 0.f)
        return;

    player->SetAngles(Vector2(lockedYaw, 0.f));
}

void FastPlace::onRender2D()
{
    if (!isEnabled())
        return;

    applyAimLock();
}

void FastPlace::onTick()
{
    if (!isEnabled())
        return;

    trySwitchToBlockSlot();

    if (!canRun())
        return;

    const int targetDelay = static_cast<int>(std::lround(m_delay->value));
    if (targetDelay < 0 || targetDelay > 4)
        return;

    applyDelay(targetDelay);
}
