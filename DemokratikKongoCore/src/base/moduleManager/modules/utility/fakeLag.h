#pragma once

#include "../../module.h"

/*
    FakeLag — Vape-style simulated lag (Latency / Dynamic / Repel).
    Packet work runs in LagBridge; this module only pushes Patcher flags.
*/
class FakeLag : public Module
{
public:
    FakeLag();
    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    void pushAll();
    EnumSetting*   m_mode = nullptr;   // Latency / Dynamic / Repel
    EnumSetting*   m_direction = nullptr; // Inbound / Outbound / Both (Latency)
    NumberSetting* m_delay = nullptr;
    NumberSetting* m_transmissionOffset = nullptr; // Dynamic only
};
