#pragma once

#include "../../module.h"

class Freecam : public Module
{
public:
    Freecam();
    void onEnable() override;
    void onDisable() override;
    void onTick() override;

private:
    void pushAll();
    NumberSetting* m_speed = nullptr;
    BoolSetting* m_disableOnDamage = nullptr;
    BoolSetting* m_allowDig = nullptr;
    BoolSetting* m_allowPlace = nullptr;
    BoolSetting* m_allowInteract = nullptr;
};
