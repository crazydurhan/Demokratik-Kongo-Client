#include "invMove.h"
#include "../combat/combatBridge.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/strayCache.h"
#include "../../../java/java.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"
#include <Windows.h>
#include <cstdio>

namespace {
    inline std::string bstr(bool v) { return v ? "true" : "false"; }

    // Resolved once through the game's ClassLoader and pinned as globals. These
    // used to be FindClass'd on every tick while a screen was open, which also
    // meant they could never resolve on clients that do not expose MCP names to
    // the system class loader.
    jclass g_guiChestClass = nullptr;
    jclass g_guiChatClass = nullptr;

    jclass chestScreenClass()
    {
        Java::AssignClass("net.minecraft.client.gui.inventory.GuiChest", g_guiChestClass);
        return g_guiChestClass;
    }

    jclass chatScreenClass()
    {
        Java::AssignClass("net.minecraft.client.gui.GuiChat", g_guiChatClass);
        return g_guiChatClass;
    }
}

InvMove::InvMove()
    : Module("InvMove", "Move while inventory/chests are open. Blink queues clicks.", Category::Movement)
{
    m_inventoryMode = &add<EnumSetting>("Inventory",
        std::vector<const char*>{ "Disabled", "Vanilla", "Blink", "Close" }, 1);
    m_chestMode = &add<EnumSetting>("Chest",
        std::vector<const char*>{ "Disabled", "Vanilla", "Blink" }, 1);
    setEnabled(false);
}

void InvMove::onEnable()
{
    Logger::Info("InvMove", "Enabled");
    pushAll();
}

void InvMove::onDisable()
{
    Logger::Info("InvMove", "Disabled");
    CombatBridge::ClearMoveKeyStates();
    Patcher::put("invmove_enabled", "false");
    Patcher::put("invmove_blink_clicks", "false");
}

bool InvMove::isChestScreen(JNIEnv* env, jobject screen) const
{
    if (!env || !screen) return false;
    jclass chestCls = chestScreenClass();
    if (!chestCls) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }
    const bool isChest = env->IsInstanceOf(screen, chestCls) == JNI_TRUE;
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return isChest;
}

void InvMove::onTick()
{
    if (!isEnabled() || !SDK::Minecraft || !SDK::Minecraft->thePlayer)
    {
        CombatBridge::ClearMoveKeyStates();
        Patcher::put("invmove_blink_clicks", "false");
        return;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env || !StrayCache::minecraft_currentScreen)
    {
        CombatBridge::ClearMoveKeyStates();
        return;
    }

    jobject mcObj = SDK::Minecraft->GetInstance();
    if (!mcObj)
    {
        CombatBridge::ClearMoveKeyStates();
        return;
    }

    jobject currentScreen = env->GetObjectField(mcObj, StrayCache::minecraft_currentScreen);
    if (env->ExceptionCheck()) { env->ExceptionClear(); currentScreen = nullptr; }

    if (!currentScreen)
    {
        // Screen closed: one final release pass runs in the drain so a key
        // held at close can never stick pressed.
        CombatBridge::ClearMoveKeyStates();
        Patcher::put("invmove_blink_clicks", "false");
        pushAll();
        return;
    }

    // Skip chat
    if (jclass chatCls = chatScreenClass())
    {
        const bool isChat = env->IsInstanceOf(currentScreen, chatCls) == JNI_TRUE;
        if (env->ExceptionCheck()) env->ExceptionClear();
        if (isChat)
        {
            env->DeleteLocalRef(currentScreen);
            CombatBridge::ClearMoveKeyStates();
            Patcher::put("invmove_blink_clicks", "false");
            return;
        }
    }

    const bool chest = isChestScreen(env, currentScreen);
    const int mode = chest
        ? (m_chestMode ? m_chestMode->index : 1)
        : (m_inventoryMode ? m_inventoryMode->index : 1);

    env->DeleteLocalRef(currentScreen);

    if (mode == 0) {
        CombatBridge::ClearMoveKeyStates();
        Patcher::put("invmove_blink_clicks", "false");
        pushAll();
        return;
    }

    // Blink (2) or Close (3): queue window clicks while moving
    const bool blinkClicks = (mode == 2 || mode == 3);
    Patcher::put("invmove_enabled", "true");
    Patcher::put("invmove_blink_clicks", blinkClicks ? "true" : "false");

    // Drive the real movement keybinds — vanilla updatePlayerMoveState()
    // reads keyBindForward.pressed etc. every tick regardless of the open
    // screen, so the player moves natively (sprint/jump included).
    const bool forward = (GetAsyncKeyState('W') & 0x8000) != 0;
    const bool back    = (GetAsyncKeyState('S') & 0x8000) != 0;
    const bool left    = (GetAsyncKeyState('A') & 0x8000) != 0;
    const bool right   = (GetAsyncKeyState('D') & 0x8000) != 0;
    const bool jump    = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;

    CombatBridge::RequestMoveKeyStates(forward, back, left, right, jump);
    pushAll();
}

void InvMove::pushAll()
{
    Patcher::put("invmove_enabled", isEnabled() ? "true" : "false");
}
