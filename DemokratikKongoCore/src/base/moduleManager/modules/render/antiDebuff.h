#pragma once

#include "../../module.h"

class AntiDebuff : public Module
{
public:
    AntiDebuff();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

private:
    void pushAll();

    BoolSetting* m_blindness = nullptr;
    BoolSetting* m_nausea    = nullptr;
};
