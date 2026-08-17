#pragma once

#include "../../module.h"

#include <string>
#include <unordered_map>

struct EnumSetting;
struct NumberSetting;
struct BoolSetting;

/*
    LiquidBounce-nextgen style ArrayList HUD (Inter 14px, accent side stripe,
    surface background, width sort, slide/fade module animations).
*/

class ArrayList : public Module
{
public:
    ArrayList();
    void onRender2D() override;

private:
    NumberSetting* m_offsetX     = nullptr;
    NumberSetting* m_offsetY     = nullptr;
    BoolSetting*   m_showTags    = nullptr;
    EnumSetting*   m_itemAlign   = nullptr;   // Left, Right
    EnumSetting*   m_order       = nullptr;   // Ascending, Descending
    EnumSetting*   m_colorMode   = nullptr;
    BoolSetting*   m_animations  = nullptr;
    NumberSetting* m_scale       = nullptr;

    struct RowAnim {
        float target   = 0.0f;
        float value    = 0.0f;
        float slide    = 0.0f;
    };
    std::unordered_map<Module*, RowAnim> m_rowAnims;
    float m_rainbowPhase = 0.0f;
};
