#pragma once

#include "../../module.h"
#include "../../../java/java.h"

class AutoBlock : public Module
{
public:
    AutoBlock();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    void resetBlockState();

    // Settings
    NumberSetting* m_range                  = nullptr;
    NumberSetting* m_maxHurtTime           = nullptr;
    NumberSetting* m_maxHoldDuration       = nullptr;
    BoolSetting*   m_forceAnimation        = nullptr;
    BoolSetting*   m_animOnlyInRange       = nullptr;
    NumberSetting* m_lagChance             = nullptr;
    NumberSetting* m_maxLagDuration        = nullptr;
    BoolSetting*   m_preventDelayAttacks   = nullptr;
    BoolSetting*   m_blockAgainImmediately = nullptr;
    BoolSetting*   m_requireLmb            = nullptr;
    BoolSetting*   m_requireRmb            = nullptr;
    BoolSetting*   m_requireDamage         = nullptr;

    // Runtime state
    bool      m_isBlocking   = false;
    bool      m_isLagging    = false;
    long long m_blockStartMs = 0;
    long long m_lagStartMs   = 0;
    long long m_lastDamageMs  = 0;
};
