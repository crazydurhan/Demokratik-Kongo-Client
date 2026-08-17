#pragma once

#include "../../module.h"

class Sprint : public Module
{
public:
    Sprint();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
};
