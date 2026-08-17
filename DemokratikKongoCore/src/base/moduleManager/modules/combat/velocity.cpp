#include "velocity.h"

#include "combatBridge.h"
#include "../../../patcher/patcher.h"
#include "../../../util/format.h"

#include <string>

Velocity::Velocity()
    : Module("Velocity", "Reduces or eliminates knockback.", Category::Combat)
{
    m_horizontal = &add<NumberSetting>("Horizontal", 100.0f, 0.0f, 100.0f, 1.0f);
    m_horizontal->suffix = "%";
    m_vertical = &add<NumberSetting>("Vertical", 100.0f, 0.0f, 100.0f, 1.0f);
    m_vertical->suffix = "%";

    m_chance = &add<IntRangeSetting>("Chance", 100, 100, 0, 100, 1);
    m_chance->suffix = "%";

    m_onlyWhenTargeting = &add<BoolSetting>("Only When Targeting", true);
}

void Velocity::onDisable()
{
    Patcher::put("velocity_enabled", "false");
}

std::string Velocity::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f%% %.0f%%",
        m_horizontal->value, m_vertical->value);
    return buf;
}

void Velocity::onTick()
{
    if (!isEnabled())
    {
        Patcher::put("velocity_enabled", "false");
        return;
    }

    // LagBridge reads these on the netty/packet thread when velocity arrives.
    Patcher::put("velocity_enabled", "true");
    Patcher::put("velocity_h", fstr(m_horizontal->value));
    Patcher::put("velocity_v", fstr(m_vertical->value));
    Patcher::put("velocity_chance_min", istr(m_chance->getLow()));
    Patcher::put("velocity_chance_max", istr(m_chance->getHigh()));
    Patcher::put("velocity_only_when_targeting", bstr(m_onlyWhenTargeting->value));
}
