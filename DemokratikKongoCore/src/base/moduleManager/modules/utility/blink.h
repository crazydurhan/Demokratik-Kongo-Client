#pragma once

#include "../../module.h"

class Blink : public Module
{
public:
    Blink();
    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    void pushAll();
    EnumSetting*   m_mode = nullptr;
    BoolSetting*   m_maxDuration = nullptr;
    NumberSetting* m_disableAfter = nullptr;
    BoolSetting*   m_disableOnAttack = nullptr;
    long long m_enableMs = 0;
};
