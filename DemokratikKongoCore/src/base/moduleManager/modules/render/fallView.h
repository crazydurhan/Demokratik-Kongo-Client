#pragma once

#include "../../module.h"

class FallView : public Module
{
public:
    FallView();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender2D() override;

private:
    NumberSetting* m_threshold = nullptr;
    BoolSetting*   m_showDamage = nullptr;
    BoolSetting*   m_showDistance = nullptr;
    BoolSetting*   m_disableFlying = nullptr;
    BoolSetting*   m_onlySneaking = nullptr;

    float m_fallDistance = 0.0f;
    int   m_predictedDamage = 0; // HP points (half-hearts * 2 scale = raw damage)
    bool  m_show = false;
};
