#pragma once

#include "../../module.h"

struct BoolSetting;
struct ColorSetting;
struct NumberSetting;

/*
    Trajectories — predicts and renders the flight path of held
    projectiles (bow, ender pearl, etc.) in world space.
    Rendering is done in Java (EspBridge) using settings synced
    via Patcher::put.
*/
class Trajectories : public Module
{
public:
    Trajectories();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    void pushAll();

    ColorSetting*  m_color = nullptr;
    NumberSetting*  m_lineWidth = nullptr;
    BoolSetting*   m_showLanding = nullptr;
    BoolSetting*   m_showBow = nullptr;
    BoolSetting*   m_showPearl = nullptr;
    BoolSetting*   m_showSnowball = nullptr;

    bool m_dirty = true;
    Color m_lastColor{0,0,0,0};
    float m_lastLineWidth = 0;
    bool m_lastShowLanding = false;
    bool m_lastShowBow = false;
    bool m_lastShowPearl = false;
    bool m_lastShowSnowball = false;
};
