#pragma once

#include "../../module.h"

struct EnumSetting;
struct NumberSetting;
struct IntRangeSetting;

class Refill : public Module
{
public:
    Refill();

    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    enum class Mode : int
    {
        Soup   = 0,
        Potion = 1,
        Armor  = 2,
    };

    EnumSetting*     m_mode         = nullptr;
    IntRangeSetting* m_startDelay   = nullptr;
    IntRangeSetting* m_delay        = nullptr;
    NumberSetting*   m_helmetSlot   = nullptr;
    NumberSetting*   m_chestSlot    = nullptr;
    NumberSetting*   m_leggingsSlot = nullptr;
    NumberSetting*   m_bootsSlot    = nullptr;
};
