#include "macros.h"

#include "../../moduleManager.h"
#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../sdk/jniResolve.h"
#include "../../../java/java.h"
#include "../../../util/keybindUtil.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

namespace
{
    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void switchHotbarSlot(JNIEnv* env, jobject invObj, int slot)
    {
        if (!env || !invObj || !StrayCache::inventoryPlayer_currentItem)
            return;

        if (slot < 0) slot = 0;
        if (slot > 8) slot = 8;

        env->SetIntField(invObj, StrayCache::inventoryPlayer_currentItem, slot);
        JniResolve::ClearException(env);
        SDK::Minecraft->SyncCurrentPlayItem();
    }

    bool hotbarHasItem(JNIEnv* env, jobject invObj, int hotbarSlot)
    {
        if (!env || !invObj || hotbarSlot < 0 || hotbarSlot > 8)
            return false;
        if (!StrayCache::inventoryPlayer_mainInventory)
            return false;

        jobjectArray mainInv = (jobjectArray)env->GetObjectField(invObj, StrayCache::inventoryPlayer_mainInventory);
        JniResolve::ClearException(env);
        if (!mainInv)
            return false;

        jobject stack = env->GetObjectArrayElement(mainInv, hotbarSlot);
        JniResolve::ClearException(env);
        env->DeleteLocalRef(mainInv);

        if (!stack)
            return false;

        env->DeleteLocalRef(stack);
        return true;
    }
}

Macros::Macros()
    : Module("Macros", "Assign hotkeys to chat messages or quick-use hotbar items.", Category::Misc)
{
    for (int i = 0; i < kSlotCount; ++i)
        setupSlot(i);
}

void Macros::setupSlot(int index)
{
    const int n = index + 1;
    SlotNameStorage& names = m_slotNames[index];

    auto assignName = [&](std::string& storage, const char* fmt) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), fmt, n);
        storage = buf;
    };

    assignName(names.enabled, "Macro %d");
    m_slots[index].enabled = &add<BoolSetting>(names.enabled.c_str(), false);

    assignName(names.key, "Macro %d Key");
    m_slots[index].key = &add<KeybindSetting>(names.key.c_str(), 0);

    assignName(names.mode, "Macro %d Mode");
    m_slots[index].mode = &add<EnumSetting>(names.mode.c_str(),
        std::vector<const char*>{ "Chat", "Use" }, 0);

    assignName(names.text, "Macro %d Text");
    m_slots[index].text = &add<StringSetting>(names.text.c_str(), "", 256);

    assignName(names.hotbarSlot, "Macro %d Slot");
    m_slots[index].hotbarSlot = &add<NumberSetting>(names.hotbarSlot.c_str(), static_cast<float>(n), 1.0f, 9.0f, 1.0f);
    m_slots[index].hotbarSlot->slotPicker = true;
    m_slots[index].hotbarSlot->description = "Hotbar slot to use.";

    assignName(names.actionDelay, "Macro %d Delay");
    m_slots[index].actionDelay = &add<IntRangeSetting>(names.actionDelay.c_str(), 180, 360, 30, 1500, 1);
    m_slots[index].actionDelay->suffix = "ms";
    m_slots[index].actionDelay->description =
        "Total time for switch, use and switch back. Each step takes a random third.";

    auto slotOn = [this, index] {
        return m_slots[index].enabled->value;
    };

    m_slots[index].key->visible = slotOn;
    m_slots[index].mode->visible = slotOn;
    m_slots[index].text->visible = [this, index] {
        return m_slots[index].enabled->value
            && m_slots[index].mode->index == static_cast<int>(Mode::Chat);
    };
    m_slots[index].hotbarSlot->visible = [this, index] {
        return m_slots[index].enabled->value
            && m_slots[index].mode->index == static_cast<int>(Mode::Use);
    };
    m_slots[index].actionDelay->visible = m_slots[index].hotbarSlot->visible;
}

void Macros::onDisable()
{
    m_pendingChatSlot = -1;
    m_useState        = UseState::Idle;
    m_activeUseSlot   = -1;
    m_nextActionTime  = 0;
}

bool Macros::canRun() const
{
    if (!isEnabled())
        return false;
    if (!CombatBridge::InGame())
        return false;
    if (!SDK::Minecraft || !SDK::Minecraft->IsReady())
        return false;
    if (SDK::Minecraft->IsInGuiState())
        return false;
    if (!SDK::Minecraft->HasInGameFocus())
        return false;
    if (!StrayCache::entityPlayer_inventory
        || !StrayCache::inventoryPlayer_currentItem
        || !StrayCache::inventoryPlayer_mainInventory)
        return false;
    return true;
}

long long Macros::randomPhaseDelayMs(IntRangeSetting* total) const
{
    if (!total)
        return 0;

    // The user configures one total budget; every phase gets a third of it and
    // then jitters inside that third so the timing is never identical twice.
    const int lo = std::max(0, total->getLow() / 3);
    const int hi = std::max(lo, total->getHigh() / 3);
    if (hi <= lo)
        return lo;

    static std::mt19937 gen{ std::random_device{}() };
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(gen);
}

void Macros::executeChat(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kSlotCount)
        return;
    if (!canRun())
        return;

    const std::string& text = m_slots[slotIndex].text->value;
    if (text.empty())
        return;
    if (!StrayCache::entityPlayerSP_sendChatMessage)
        return;
    if (!SDK::Minecraft->thePlayer)
        return;

    SDK::Minecraft->thePlayer->SendChatMessage(text);
}

void Macros::processPendingChat()
{
    if (m_pendingChatSlot < 0)
        return;

    const int slot = m_pendingChatSlot;
    m_pendingChatSlot = -1;
    executeChat(slot);
}

void Macros::startUseMacro(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kSlotCount)
        return;
    if (!canRun())
        return;
    if (m_useState != UseState::Idle)
        return;

    JNIEnv* env = Java::GetEnv();
    if (!env)
        return;

    jobject playerObj = SDK::Minecraft->GetThePlayerObject();
    if (!playerObj)
        return;

    jobject invObj = env->GetObjectField(playerObj, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!invObj) {
        env->DeleteLocalRef(playerObj);
        return;
    }

    const int targetSlot = static_cast<int>(m_slots[slotIndex].hotbarSlot->value) - 1;
    if (targetSlot < 0 || targetSlot > 8) {
        env->DeleteLocalRef(invObj);
        env->DeleteLocalRef(playerObj);
        return;
    }

    if (!hotbarHasItem(env, invObj, targetSlot)) {
        env->DeleteLocalRef(invObj);
        env->DeleteLocalRef(playerObj);
        return;
    }

    m_savedHotbarSlot = env->GetIntField(invObj, StrayCache::inventoryPlayer_currentItem);
    JniResolve::ClearException(env);
    if (m_savedHotbarSlot < 0) m_savedHotbarSlot = 0;
    if (m_savedHotbarSlot > 8) m_savedHotbarSlot = 8;

    m_activeUseSlot = slotIndex;
    switchHotbarSlot(env, invObj, targetSlot);
    m_useState = UseState::WaitSwitch;
    m_nextActionTime = nowMs() + randomPhaseDelayMs(m_slots[slotIndex].actionDelay);

    env->DeleteLocalRef(invObj);
    env->DeleteLocalRef(playerObj);
}

void Macros::tickUseStateMachine()
{
    if (m_useState == UseState::Idle)
        return;
    if (!canRun()) {
        m_useState = UseState::Idle;
        m_activeUseSlot = -1;

        // Restore the hotbar slot if we bailed mid-use (mirrors WaitUse/WaitBack).
        JNIEnv* env = Java::GetEnv();
        jobject playerObj = env && SDK::Minecraft ? SDK::Minecraft->GetThePlayerObject() : nullptr;
        if (playerObj) {
            jobject invObj = env->GetObjectField(playerObj, StrayCache::entityPlayer_inventory);
            JniResolve::ClearException(env);
            if (invObj) {
                switchHotbarSlot(env, invObj, m_savedHotbarSlot);
                env->DeleteLocalRef(invObj);
            }
            env->DeleteLocalRef(playerObj);
        }
        return;
    }

    const long long now = nowMs();
    if (now < m_nextActionTime)
        return;

    if (m_activeUseSlot < 0 || m_activeUseSlot >= kSlotCount) {
        m_useState = UseState::Idle;
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env)
        return;

    jobject playerObj = SDK::Minecraft->GetThePlayerObject();
    if (!playerObj)
        return;

    jobject invObj = env->GetObjectField(playerObj, StrayCache::entityPlayer_inventory);
    JniResolve::ClearException(env);
    if (!invObj) {
        env->DeleteLocalRef(playerObj);
        m_useState = UseState::Idle;
        return;
    }

    SlotSettings& slot = m_slots[m_activeUseSlot];

    switch (m_useState) {
        case UseState::WaitSwitch:
            if (StrayCache::minecraft_rightClickDelayTimer)
                SDK::Minecraft->SetRightClickDelayTimer(0);
            CombatBridge::RightClick();
            m_useState = UseState::WaitUse;
            m_nextActionTime = now + randomPhaseDelayMs(slot.actionDelay);
            break;

        case UseState::WaitUse:
            switchHotbarSlot(env, invObj, m_savedHotbarSlot);
            m_useState = UseState::WaitBack;
            m_nextActionTime = now + randomPhaseDelayMs(slot.actionDelay);
            break;

        case UseState::WaitBack:
            m_useState = UseState::Idle;
            m_activeUseSlot = -1;
            break;

        default:
            m_useState = UseState::Idle;
            m_activeUseSlot = -1;
            break;
    }

    env->DeleteLocalRef(invObj);
    env->DeleteLocalRef(playerObj);
}

void Macros::onTick()
{
    processPendingChat();
    tickUseStateMachine();
}

void Macros::OnHotkey(int vk, LPARAM lParam)
{
    Macros* self = ModuleManager::Get<Macros>();
    if (!self || !self->isEnabled())
        return;
    if (self->m_useState != UseState::Idle)
        return;

    for (int i = 0; i < kSlotCount; ++i) {
        const SlotSettings& slot = self->m_slots[i];
        if (!slot.enabled || !slot.enabled->value)
            continue;
        if (!slot.key || slot.key->virtualKey == 0)
            continue;
        if (!KeybindUtil::KeybindMatches(slot.key->virtualKey, vk, lParam))
            continue;

        if (slot.mode->index == static_cast<int>(Mode::Chat))
            self->m_pendingChatSlot = i;
        else
            self->startUseMacro(i);
        return;
    }
}
