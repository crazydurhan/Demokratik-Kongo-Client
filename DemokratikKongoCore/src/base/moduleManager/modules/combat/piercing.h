#pragma once

#include "../../module.h"

class Piercing : public Module
{
public:
    Piercing();

    void onDisable() override;
    void onTick()    override;

    // Used by CombatBridge::RaycastEntity (friend skip in C++ raycast).
    static bool IsActive();
    static bool SkipPlayers();
    static bool SkipMobs();

private:
    bool passesItemFilter() const;

    BoolSetting*   m_limitItemsEnabled = nullptr;
    StringSetting* m_allowedItems = nullptr;
    BoolSetting*   m_skipPlayers = nullptr;
    BoolSetting*   m_skipMobs = nullptr;

    static Piercing* s_instance;
};
