#pragma once

#include "../../module.h"

class SafeWalk : public Module
{
public:
    SafeWalk();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;

private:
    bool m_wasSneaking = false;
};
