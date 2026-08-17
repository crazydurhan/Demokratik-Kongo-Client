#pragma once

#include "../../module.h"
#include "../../../java/java.h"

class LatencyAlerts : public Module
{
public:
    LatencyAlerts();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;

    std::string arrayListSuffix(SuffixDetail detail) const override;

private:
    long long readLastInboundMs();

    NumberSetting* m_intervalSec = nullptr;
    NumberSetting* m_highLatencySec = nullptr;

    long long m_lastAlertMs = 0;
    long long m_lastGapMs   = -1; // last measured inbound silence, for the HUD suffix
    bool      m_loggedAlive = false;
    jclass    m_lagBridgeClass = nullptr;
    jmethodID m_getLastInbound = nullptr;
};
