#pragma once

#include "../../module.h"

struct IntRangeSetting;
struct BoolSetting;

class KnockbackDelay : public Module
{
public:
    KnockbackDelay();

    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    IntRangeSetting* m_delay  = nullptr;
    IntRangeSetting* m_chance = nullptr;
    BoolSetting*     m_onlyWhenTargeting = nullptr;
};
