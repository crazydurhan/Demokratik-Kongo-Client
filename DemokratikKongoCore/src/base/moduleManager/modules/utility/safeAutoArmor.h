#pragma once

#include "../../module.h"

#include <array>
#include <string>

class SafeAutoArmor : public Module
{
public:
    SafeAutoArmor();

    void onEnable() override;
    void onTick() override;
    void onDisable() override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    enum class State
    {
        Idle,
        SwapToArmor,
        RightClick,
        SwapBack,
        Cooldown
    };

    struct ArmorCandidate
    {
        int hotbarSlot = -1;
        int armorSlot = -1; // MC armor slot: 0 boots, 1 leggings, 2 chestplate, 3 helmet
        int itemId = 0;
        int score = 0;
        int durability = 0;
    };

    bool canRun() const;
    void resetState(bool restoreSlot);
    void scheduleNext(long long now, float extraMultiplier = 1.0f);

    bool scanBestCandidate(ArmorCandidate& outCandidate);
    bool readArmorPresence(std::array<bool, 4>& outPresence) const;
    bool shouldFillArmorSlot(int armorSlot, const std::array<bool, 4>& presence) const;

    int getCurrentHotbarSlot() const;
    bool setHotbarSlot(int slot) const;
    bool isStillValidCandidate(const ArmorCandidate& candidate) const;
    bool readHotbarArmorCandidate(int hotbarSlot, ArmorCandidate& outCandidate) const;

    int armorSlotFromItemId(int itemId) const;
    int baseArmorScoreFromItemId(int itemId) const;
    int durabilityRemaining(void* stack) const;
    int enchantLevel(int enchantmentId, void* stack) const;
    int scoreArmorStack(void* stack, int itemId, int* outDurability) const;

    bool recentlyHurt(long long now) const;
    void updateCombatTimer(long long now);
    const char* stateName() const;

    // --- Two checkboxes: both can be on at the same time ---
    BoolSetting*     m_hotbarMode        = nullptr; // Mode 1: right-click equip from hotbar
    BoolSetting*     m_inventoryMode     = nullptr; // Mode 2: shift-click swap from inv

    // --- Hotbar mode settings ---
    BoolSetting*     m_onlyOnBreak      = nullptr;
    NumberSetting*   m_minDelay         = nullptr;
    NumberSetting*   m_maxDelay         = nullptr;
    BoolSetting*     m_dynamicCombat    = nullptr;

    // --- Inventory mode settings ---
    NumberSetting*   m_threshold       = nullptr; // durability % below which to swap
    IntRangeSetting* m_swapDelay        = nullptr;
    IntRangeSetting* m_startDelay      = nullptr;
    BoolSetting*    m_dropOld          = nullptr; // drop replaced armor on ground

    State m_state = State::Idle;
    ArmorCandidate m_candidate{};
    int m_originalHotbarSlot = -1;
    long long m_nextActionMs = 0;
    long long m_lastHurtMs = 0;

    std::array<bool, 4> m_hadArmor{};
    bool m_presenceInitialized = false;
};
