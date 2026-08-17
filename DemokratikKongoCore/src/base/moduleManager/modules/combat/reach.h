#pragma once

#include "../../module.h"
#include "../../commonData.h"
#include "../../../util/math/geometry.h"
#include <Windows.h>

struct FloatRangeSetting;
struct IntRangeSetting;

class Reach : public Module
{
public:
    Reach();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;
    Color       arrayListColorOverride() const override;

    // Ray-traces on click; returns true when an extended-reach attack was handled.
    bool tryReachAttack();

private:
    mutable float m_currentReach = 3.0f;

    // Per-session roll state; reset on enable/disable so a rolled reach can
    // never leak across worlds, servers, or module toggles.
    std::string m_lastTargetName;
    float       m_rolledReach = 3.0f;
    int         m_ticksWithTarget = 0;

    FloatRangeSetting* m_range         = nullptr;
    IntRangeSetting*   m_chance        = nullptr;
    EnumSetting*       m_chanceMode    = nullptr;
    BoolSetting*       m_verticalCheck = nullptr;
    BoolSetting*       m_onlySprinting = nullptr;
    BoolSetting*       m_disableInWater = nullptr;
    BoolSetting*       m_throughBlocks = nullptr;
};
