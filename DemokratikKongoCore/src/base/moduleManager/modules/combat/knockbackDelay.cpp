#include "knockbackDelay.h"

#include "../../../patcher/patcher.h"
#include "../../../util/format.h"

#include <string>

KnockbackDelay::KnockbackDelay()
    : Module("Knockback Delay",
             "Buffers incoming packets when hit, freezing the world for a short delay.",
             Category::Combat)
{
    // Defaults are 0-0 (no hold) on purpose: a nonzero default freezes inbound
    // packets for every hit out of the box, which is an instant spectator flag.
    m_delay = &add<IntRangeSetting>("Delay", 0, 0, 0, 1000, 1);
    m_delay->suffix = "ms";

    m_chance = &add<IntRangeSetting>("Chance", 100, 100, 0, 100, 1);
    m_chance->suffix = "%";

    m_onlyWhenTargeting = &add<BoolSetting>("Only When Targeting", true);
}

void KnockbackDelay::onDisable()
{
    Patcher::put("kbd_enabled", "false");
    Patcher::put("kbd_only_when_targeting", "false");
}

std::string KnockbackDelay::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d-%dms",
        m_delay->getLow(), m_delay->getHigh());
    return buf;
}

void KnockbackDelay::onTick()
{
    if (!isEnabled())
    {
        Patcher::put("kbd_enabled", "false");
        return;
    }

    Patcher::put("kbd_enabled", "true");
    Patcher::put("kbd_delay_min", istr(m_delay->getLow()));
    Patcher::put("kbd_delay_max", istr(m_delay->getHigh()));
    Patcher::put("kbd_chance_min", istr(m_chance->getLow()));
    Patcher::put("kbd_chance_max", istr(m_chance->getHigh()));
    Patcher::put("kbd_only_when_targeting", m_onlyWhenTargeting->value ? "true" : "false");
}
