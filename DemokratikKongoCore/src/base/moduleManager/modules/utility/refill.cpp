#include "refill.h"

#include "../../../patcher/patcher.h"
#include "../../../util/format.h"

#include <cstdio>
#include <string>

Refill::Refill()
    : Module("Refill", "Refills missing hotbar items or armor while inventory is open.", Category::Utility)
{
    m_mode = &add<EnumSetting>("Mode",
        std::vector<const char*>{ "Soup", "Potion", "Armor" }, 0);

    // Wait after opening inventory before the first click (Grim multi-actions / packet order).
    m_startDelay = &add<IntRangeSetting>("Start Delay", 100, 200, 0, 1000, 1);
    m_startDelay->suffix = "ms";

    // Delay between successful refill clicks.
    m_delay = &add<IntRangeSetting>("Delay", 50, 150, 0, 1000, 1);
    m_delay->suffix = "ms";

    m_helmetSlot = &add<NumberSetting>("Helmet Slot", 1.0f, 1.0f, 9.0f, 1.0f);
    m_chestSlot = &add<NumberSetting>("Chestplate Slot", 2.0f, 1.0f, 9.0f, 1.0f);
    m_leggingsSlot = &add<NumberSetting>("Leggings Slot", 3.0f, 1.0f, 9.0f, 1.0f);
    m_bootsSlot = &add<NumberSetting>("Boots Slot", 4.0f, 1.0f, 9.0f, 1.0f);

    auto armorOnly = [this] { return isEnabled() && m_mode->index == static_cast<int>(Mode::Armor); };
    m_helmetSlot->visible = armorOnly;
    m_chestSlot->visible = armorOnly;
    m_leggingsSlot->visible = armorOnly;
    m_bootsSlot->visible = armorOnly;
}

void Refill::onDisable()
{
    Patcher::put("refill_enabled", "false");
}

std::string Refill::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    if (!m_mode || m_mode->options.empty()) return "";
    if (detail == SuffixDetail::Basic)
        return m_mode->current();

    char buf[48];
    std::snprintf(buf, sizeof(buf), "%s %d-%dms",
        m_mode->current(),
        m_delay ? m_delay->getLow() : 0,
        m_delay ? m_delay->getHigh() : 0);
    return buf;
}

void Refill::onTick()
{
    // Inventory clicks run on the client thread in RuntimeBridge.tickRefill.
    if (!isEnabled())
    {
        Patcher::put("refill_enabled", "false");
        return;
    }

    Patcher::put("refill_enabled", "true");
    Patcher::put("refill_mode", std::to_string(m_mode->index));
    Patcher::put("refill_start_min", istr(m_startDelay->getLow()));
    Patcher::put("refill_start_max", istr(m_startDelay->getHigh()));
    Patcher::put("refill_delay_min", istr(m_delay->getLow()));
    Patcher::put("refill_delay_max", istr(m_delay->getHigh()));
    Patcher::put("refill_helmet_slot", fstr(m_helmetSlot->value));
    Patcher::put("refill_chest_slot", fstr(m_chestSlot->value));
    Patcher::put("refill_leggings_slot", fstr(m_leggingsSlot->value));
    Patcher::put("refill_boots_slot", fstr(m_bootsSlot->value));
}
