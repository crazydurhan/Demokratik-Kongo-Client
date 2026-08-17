#include "itemEsp.h"

#include "../../../patcher/patcher.h"
#include "../../../util/format.h"
#include "../../../util/logger.h"

#include <string>

ItemESP::ItemESP()
    : Module("ItemESP", "Show dropped item names in the world.", Category::Render)
{
    m_distance = &add<BoolSetting>("Distance", true);
    m_groupItems = &add<BoolSetting>("Group Items", false);
    m_scale = &add<NumberSetting>("Scale", 1.0f, 0.5f, 3.0f, 0.05f);
    m_autoScale = &add<BoolSetting>("Auto Scale", true);
    m_whitelistOnly = &add<BoolSetting>("Whitelist Only", false);
    m_allowedItems = &add<StringSetting>("Allowed Items", "");
    m_allowedItems->visible = [this] { return m_whitelistOnly->value; };

    setEnabled(false);
}

void ItemESP::onEnable()
{
    Logger::Info("ItemESP", "Enabled — Java world pass item labels");
    pushAll();
}

void ItemESP::onDisable()
{
    Patcher::put("itemesp_enabled", "false");
    Logger::Info("ItemESP", "Disabled");
}

void ItemESP::onTick()
{
    if (!isEnabled()) return;
    pushAll();
}

void ItemESP::pushAll()
{
    Patcher::put("itemesp_enabled",          bstr(isEnabled()));
    Patcher::put("itemesp_distance",         bstr(m_distance->value));
    Patcher::put("itemesp_group",            bstr(m_groupItems->value));
    Patcher::put("itemesp_scale",            fstr(m_scale->value));
    Patcher::put("itemesp_auto_scale",       bstr(m_autoScale->value));
    Patcher::put("itemesp_whitelist_only",   bstr(m_whitelistOnly->value));
    Patcher::put("itemesp_allowed",          m_allowedItems->value);
}
