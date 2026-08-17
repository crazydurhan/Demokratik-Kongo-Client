#pragma once

#include "../../module.h"

class DamageTags : public Module
{
public:
    DamageTags();
    void onEnable() override;
    void onDisable() override;
    void onTick() override;

private:
    void pushAll();
    NumberSetting* m_duration = nullptr;
    NumberSetting* m_scale = nullptr;
    BoolSetting*   m_shadow = nullptr;
};
