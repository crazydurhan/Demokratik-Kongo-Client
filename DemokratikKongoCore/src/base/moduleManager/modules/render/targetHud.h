#pragma once

#include "../../module.h"
#include <string>

class TargetHUD : public Module
{
public:
    TargetHUD();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender2D() override;

private:
    NumberSetting* m_posX;
    NumberSetting* m_posY;
    BoolSetting* m_showArmor;

    std::string m_targetName;
    float m_targetHealth = 20.0f;
    float m_targetMaxHealth = 20.0f;
    float m_displayHealth = 20.0f;
    float m_targetDistance = 0.0f;
    bool m_hasTarget = false;

    // Name of the player the local user last landed/attempted a hit on.
    // The HUD locks onto this player instead of the nearest one.
    std::string m_lockedName;
    bool m_prevAttackDown = false;
};
