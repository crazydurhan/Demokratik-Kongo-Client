#include "fakeLag.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"
#include <cstdio>

namespace {
    inline std::string bstr(bool v) { return v ? "true" : "false"; }
    inline std::string istr(int v) { char b[16]; std::snprintf(b, sizeof(b), "%d", v); return b; }
}

FakeLag::FakeLag()
    : Module("FakeLag", "Simulates lag (Vape Latency / Dynamic / Repel).", Category::Utility)
{
    m_mode = &add<EnumSetting>("Mode",
        std::vector<const char*>{ "Latency", "Dynamic", "Repel" }, 0);
    m_direction = &add<EnumSetting>("Direction",
        std::vector<const char*>{ "Inbound", "Outbound", "Both" }, 1);
    m_delay = &add<NumberSetting>("Delay", 100.0f, 1.0f, 1000.0f, 10.0f);
    m_transmissionOffset = &add<NumberSetting>("Transmission Offset", 5.0f, 0.0f, 50.0f, 1.0f);

    m_delay->suffix = " ms";
    m_delay->description = "Base packet hold time (Vape Delay).";
    m_transmissionOffset->description = "Dynamic: stagger queued outbound packets.";
    m_direction->description = "Latency only — which traffic to delay. Dynamic/Repel are outbound.";

    m_direction->visible = [this] { return m_mode && m_mode->index == 0; };
    m_transmissionOffset->visible = [this] { return m_mode && m_mode->index == 1; };

    setEnabled(false);
}

std::string FakeLag::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    static const char* kModes[] = { "Latency", "Dynamic", "Repel" };
    const int mi = m_mode ? m_mode->index : 0;
    const char* modeName = (mi >= 0 && mi < 3) ? kModes[mi] : "Latency";
    char buf[48];
    if (detail == SuffixDetail::Extended) {
        std::snprintf(buf, sizeof(buf), "%s %dms", modeName, (int)(m_delay ? m_delay->value : 0));
    } else {
        std::snprintf(buf, sizeof(buf), "%s", modeName);
    }
    return buf;
}

void FakeLag::onEnable()
{
    Logger::Info("FakeLag", "Enabled");
    Patcher::put("blink_enabled", "false"); // mutex with Blink
    pushAll();
}

void FakeLag::onDisable()
{
    Logger::Info("FakeLag", "Disabled");
    Patcher::put("fakelag_enabled", "false");
}

void FakeLag::onTick()
{
    if (!isEnabled()) {
        Patcher::put("fakelag_enabled", "false");
        return;
    }
    if (m_delay && m_delay->value <= 0.0f) {
        setEnabled(false);
        return;
    }
    pushAll();
}

void FakeLag::pushAll()
{
    // style: 0 Latency, 1 Dynamic, 2 Repel
    const int style = m_mode ? m_mode->index : 0;
    // direction: 0 In, 1 Out, 2 Both — Dynamic/Repel force outbound
    int direction = m_direction ? m_direction->index : 1;
    if (style != 0)
        direction = 1;

    Patcher::put("fakelag_enabled", bstr(isEnabled()));
    Patcher::put("fakelag_style", istr(style));
    Patcher::put("fakelag_mode", istr(direction)); // LagBridge still uses this for in/out
    Patcher::put("fakelag_delay", istr(m_delay ? (int)m_delay->value : 100));
    Patcher::put("fakelag_tx_offset", istr(m_transmissionOffset ? (int)m_transmissionOffset->value : 5));
}
