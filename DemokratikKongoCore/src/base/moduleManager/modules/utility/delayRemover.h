#pragma once

#include "../../module.h"

struct BoolSetting;

class DelayRemover : public Module
{
public:
    DelayRemover();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    BoolSetting* m_noHitDelay  = nullptr;
    BoolSetting* m_noJumpDelay = nullptr;
};
