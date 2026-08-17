#pragma once

#include <Windows.h>
#include <string>

#include "../../module.h"

class Macros : public Module
{
public:
    static constexpr int kSlotCount = 6;

    Macros();

    void onDisable() override;
    void onTick() override;

    static void OnHotkey(int vk, LPARAM lParam);

private:
    enum class Mode : int { Chat = 0, Use = 1 };
    enum class UseState : int { Idle, WaitSwitch, WaitUse, WaitBack };

    struct SlotSettings
    {
        BoolSetting*      enabled     = nullptr;
        KeybindSetting*   key         = nullptr;
        EnumSetting*      mode        = nullptr;
        StringSetting*    text        = nullptr;
        NumberSetting*    hotbarSlot  = nullptr;

        // Total time budget for the whole switch -> use -> switch back cycle.
        // Each of the three phases draws a random value from one third of it.
        IntRangeSetting*  actionDelay = nullptr;
    };

    void setupSlot(int index);
    bool canRun() const;
    void processPendingChat();
    void startUseMacro(int slotIndex);
    void tickUseStateMachine();
    void executeChat(int slotIndex);

    // Random delay for a single phase: one third of the configured range.
    long long randomPhaseDelayMs(IntRangeSetting* total) const;

    struct SlotNameStorage
    {
        std::string enabled;
        std::string key;
        std::string mode;
        std::string text;
        std::string hotbarSlot;
        std::string actionDelay;
    };

    SlotSettings m_slots[kSlotCount]{};
    SlotNameStorage m_slotNames[kSlotCount]{};
    int         m_pendingChatSlot = -1;
    UseState    m_useState        = UseState::Idle;
    int         m_activeUseSlot   = -1;
    int         m_savedHotbarSlot = 0;
    long long   m_nextActionTime  = 0;
};
