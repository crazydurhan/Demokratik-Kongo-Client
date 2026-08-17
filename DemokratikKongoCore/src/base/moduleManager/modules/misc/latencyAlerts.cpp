#include "latencyAlerts.h"

#include "../../../java/java.h"
#include "../../../sdk/sdk.h"
#include "../../../sdk/jniResolve.h"
#include "../../../gui/guiWidgets.h"
#include "../../../util/logger.h"

#include <chrono>
#include <cstdio>

namespace
{
    inline long long nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

LatencyAlerts::LatencyAlerts()
    : Module("LatencyAlerts", "Toast when inbound packets stall (packet loss / high latency).", Category::Misc)
{
    m_intervalSec = &add<NumberSetting>("Alert Interval", 3.0f, 0.5f, 10.0f, 0.1f);
    m_intervalSec->suffix = " s";
    m_intervalSec->description = "Minimum time between two alerts.";

    m_highLatencySec = &add<NumberSetting>("High Latency", 0.5f, 0.1f, 5.0f, 0.1f);
    m_highLatencySec->suffix = " s";
    m_highLatencySec->description =
        "Alert when no inbound packet arrives for this long. On a healthy connection packets "
        "flow constantly, so staying silent means everything is fine — the ArrayList suffix "
        "shows the live gap so you can confirm the module is working.";

    add<ActionSetting>("Test Alert", [] {
        Gui::Notify("Latency", "Test alert — notifications are working.",
                    Gui::Icon::Warning, Gui::Colors().danger);
    }, "TEST");

    setEnabled(false);
}

void LatencyAlerts::onEnable()
{
    Logger::Info("LatencyAlerts", "Enabled");
    m_lastAlertMs = nowMs();
    m_lastGapMs = -1;
    m_loggedAlive = false;
}

void LatencyAlerts::onDisable()
{
    Logger::Info("LatencyAlerts", "Disabled");
    m_lastAlertMs = 0;
    m_lastGapMs = -1;
}

long long LatencyAlerts::readLastInboundMs()
{
    JNIEnv* env = Java::GetEnv();
    if (!env) return 0;

    if (!m_lagBridgeClass)
    {
        jclass local = nullptr;
        if (!Java::AssignClass("io.github.lefraudeur.LagBridge", local) || !local)
            return 0;
        m_lagBridgeClass = local;
    }
    if (!m_getLastInbound)
    {
        m_getLastInbound = env->GetStaticMethodID(m_lagBridgeClass, "getLastInboundWallMs", "()J");
        if (env->ExceptionCheck()) { env->ExceptionClear(); m_getLastInbound = nullptr; }
    }
    if (!m_getLastInbound) return 0;

    const jlong v = env->CallStaticLongMethod(m_lagBridgeClass, m_getLastInbound);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    return static_cast<long long>(v);
}

void LatencyAlerts::onTick()
{
    if (!isEnabled()) return;
    if (!SDK::Minecraft || !SDK::Minecraft->thePlayer) return;

    const long long lastPkt = readLastInboundMs();
    if (lastPkt <= 0) return;

    const long long wallNow = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const long long gap = wallNow - lastPkt;
    m_lastGapMs = gap;

    if (!m_loggedAlive)
    {
        m_loggedAlive = true;
        Logger::Info("LatencyAlerts", "Bridge alive, inbound packet clock ticking (gap=" +
            std::to_string(gap) + " ms)");
    }

    const long long thresholdMs = static_cast<long long>((m_highLatencySec ? m_highLatencySec->value : 0.5f) * 1000.0f);
    const long long intervalMs = static_cast<long long>((m_intervalSec ? m_intervalSec->value : 3.0f) * 1000.0f);

    if (gap < thresholdMs) return;

    const long long steady = nowMs();
    if (steady - m_lastAlertMs < intervalMs) return;
    m_lastAlertMs = steady;

    char msg[64];
    std::snprintf(msg, sizeof(msg), "No packet for %lld ms", gap);
    Gui::Notify("Latency", msg, Gui::Icon::Warning, Gui::Colors().danger);
}

std::string LatencyAlerts::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    if (m_lastGapMs < 0) return "waiting";

    char buf[24];
    if (m_lastGapMs >= 10000)
        std::snprintf(buf, sizeof(buf), "%.1f s", m_lastGapMs / 1000.0);
    else
        std::snprintf(buf, sizeof(buf), "%lld ms", m_lastGapMs);
    return buf;
}
