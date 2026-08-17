#include "blink.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"
#include <chrono>
#include <cstdio>

namespace {
    inline long long nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    inline std::string bstr(bool v) { return v ? "true" : "false"; }
    inline std::string istr(int v) { char b[16]; std::snprintf(b, sizeof(b), "%d", v); return b; }
}

Blink::Blink()
    : Module("Blink", "Hold inbound and/or outbound packets until disabled.", Category::Utility)
{
    m_mode = &add<EnumSetting>("Mode", std::vector<const char*>{ "Inbound", "Outbound", "Both" }, 1);
    m_maxDuration = &add<BoolSetting>("Max Duration", false);
    m_disableAfter = &add<NumberSetting>("Disable After", 500.0f, 50.0f, 20000.0f, 50.0f);
    m_disableAfter->suffix = " ms";
    m_disableAfter->visible = [this] { return m_maxDuration && m_maxDuration->value; };
    m_disableOnAttack = &add<BoolSetting>("Disable On Attack", false);
    setEnabled(false);
}

std::string Blink::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None || !m_mode) return "";
    return m_mode->current();
}

void Blink::onEnable()
{
    Logger::Info("Blink", "Enabled");
    m_enableMs = nowMs();
    // Mutex with FakeLag
    Patcher::put("fakelag_enabled", "false");
    pushAll();
}

void Blink::onDisable()
{
    Logger::Info("Blink", "Disabled");
    Patcher::put("blink_enabled", "false");
}

void Blink::onTick()
{
    if (!isEnabled()) {
        Patcher::put("blink_enabled", "false");
        return;
    }
    if (m_maxDuration && m_maxDuration->value) {
        if (nowMs() - m_enableMs >= (long long)m_disableAfter->value) {
            setEnabled(false);
            return;
        }
    }
    pushAll();
}

void Blink::pushAll()
{
    Patcher::put("blink_enabled", bstr(isEnabled()));
    Patcher::put("blink_mode", istr(m_mode ? m_mode->index : 1));
}
