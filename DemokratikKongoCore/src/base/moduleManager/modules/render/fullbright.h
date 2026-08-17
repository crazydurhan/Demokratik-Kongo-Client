#pragma once

#include "../../module.h"

struct EnumSetting;
struct NumberSetting;
struct BoolSetting;

class Fullbright : public Module
{
public:
    Fullbright();

    void onEnable()  override;
    void onDisable() override;
    void onTick()    override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    void applyMode();
    void revertMode();
    float readBlockLight();
    void applyNightVision(bool enable);

    EnumSetting*   m_mode          = nullptr;

    // Gamma mode
    NumberSetting* m_gammaStrength = nullptr;

    // Fade mode
    NumberSetting* m_fadeMin       = nullptr;
    NumberSetting* m_fadeMax       = nullptr;
    NumberSetting* m_fadeSpeed     = nullptr;

    // Night Vision mode
    BoolSetting*   m_hideParticles = nullptr;

    float m_savedGamma = 1.0f;
    float m_fadeGamma  = 1.0f;
    bool  m_nvActive   = false;
};
