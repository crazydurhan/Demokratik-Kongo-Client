#include "piercing.h"
#include "friends.h"
#include "itemWhitelist.h"

#include "../../../patcher/patcher.h"

Piercing* Piercing::s_instance = nullptr;

Piercing::Piercing()
    : Module("Piercing",
             "Raytrace skips friends so you can hit enemies behind them. Does not extend reach.",
             Category::Combat)
{
    s_instance = this;

    m_limitItemsEnabled = &add<BoolSetting>("Limit to Items", false);
    m_allowedItems = &add<StringSetting>("Allowed Items", "sword,axe");
    m_allowedItems->itemList = true;
    m_allowedItems->visible = [this]{ return m_limitItemsEnabled->value; };
    m_skipPlayers = &add<BoolSetting>("Skip Players", false);
    m_skipMobs    = &add<BoolSetting>("Skip Mobs", false);
}

void Piercing::onDisable()
{
    Patcher::put("piercing_enabled", "false");
    Patcher::put("piercing_skip_players", "false");
    Patcher::put("piercing_skip_mobs", "false");
}

bool Piercing::IsActive()
{
    return s_instance && s_instance->isEnabled() && s_instance->passesItemFilter();
}

bool Piercing::SkipPlayers()
{
    return s_instance && s_instance->isEnabled() && s_instance->m_skipPlayers
        && s_instance->m_skipPlayers->value;
}

bool Piercing::SkipMobs()
{
    return s_instance && s_instance->isEnabled() && s_instance->m_skipMobs
        && s_instance->m_skipMobs->value;
}

bool Piercing::passesItemFilter() const
{
    if (!m_limitItemsEnabled->value)
        return true;

    return ItemWhitelist::IsAllowed(ItemWhitelist::GetHeldDisplayName(), m_allowedItems->value);
}

void Piercing::onTick()
{
    // Raytrace runs on the client thread in EspBridge.onGetMouseOverPost.
    // Distance: Reach's reach_distance when Reach is on, else vanilla 3.0.
    if (!isEnabled() || !passesItemFilter())
    {
        Patcher::put("piercing_enabled", "false");
        Patcher::put("piercing_skip_players", "false");
        Patcher::put("piercing_skip_mobs", "false");
        return;
    }

    Patcher::put("piercing_enabled", "true");
    Patcher::put("piercing_skip_players", m_skipPlayers->value ? "true" : "false");
    Patcher::put("piercing_skip_mobs",    m_skipMobs->value    ? "true" : "false");

    // esp_friends is owned by the Friends module to avoid double-pushing
    // (Katman 3 of the stability plan).  Patcher::put is cached internally
    // so duplicate keys are a no-op anyway, but removing the duplicate
    // build here also removes a per-tick std::string concatenation.
}
