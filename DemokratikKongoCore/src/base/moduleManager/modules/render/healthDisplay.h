#pragma once

#include "../../module.h"

struct NumberSetting;

/*
    HealthDisplay — Vape-style center-screen health readout:
    "10.5 ❤" below the crosshair, colored by remaining hearts.
*/
class HealthDisplay : public Module
{
public:
    HealthDisplay();

    void onRender2D() override;

private:
    NumberSetting* m_offsetY = nullptr;
    NumberSetting* m_scale   = nullptr;
};
