#pragma once

#include "../../module.h"

struct IntRangeSetting;
struct BoolSetting;

class Velocity : public Module
{
public:
    Velocity();

    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    NumberSetting*   m_horizontal = nullptr;
    NumberSetting*   m_vertical   = nullptr;
    IntRangeSetting* m_chance     = nullptr;
    BoolSetting*     m_onlyWhenTargeting = nullptr;
};
