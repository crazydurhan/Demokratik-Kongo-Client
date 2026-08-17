#pragma once

#include "../../module.h"

class NoHurtCam : public Module
{
public:
    NoHurtCam();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    void pushAll();

    NumberSetting* m_intensity = nullptr;
};

