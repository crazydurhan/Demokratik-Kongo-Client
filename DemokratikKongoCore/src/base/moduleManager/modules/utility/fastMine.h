#pragma once

#include "../../module.h"

class FastMine : public Module
{
public:
    FastMine();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override { /* worker: no-op — runs on client thread */ }
    void clientTick();

private:
    NumberSetting* m_speed               = nullptr;
    NumberSetting* m_breakDelay          = nullptr;
    BoolSetting*   m_alwaysDecreaseDelay = nullptr;

    float m_prevDamage = 0.0f;
};
