#include "noInteract.h"

#include "../combat/itemWhitelist.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"

NoInteract::NoInteract()
    : Module("NoInteract",
             "Skips block interaction (doors, chests, etc.) while holding whitelisted items, so the item is used instead (e.g. throw pearls at doors).",
             Category::Utility)
{
    m_useWhitelist = &add<BoolSetting>("Use Whitelist", true);
    m_whitelist = &add<StringSetting>("Item Whitelist", "368");
    m_whitelist->itemList = true;
    m_whitelist->visible = [this]{ return m_useWhitelist->value; };
    m_whitelist->description =
        "Same syntax as combat whitelists: ids (368), id:meta, name fragments, "
        "categories (swords, food, potions, pearls, apples, bows, arrows, blocks), "
        "'slot N', 'hand'. 368 = ender pearl.";
}

void NoInteract::onTick()
{
    if (!isEnabled())
    {
        Patcher::put("nointeract_enabled", "false");
        Patcher::put("nointeract_match", "false");
        return;
    }

    // Resolved in C++ with the combat whitelist engine; the Java
    // onPlayerRightClick hook only consumes the boolean verdict.
    const std::string heldName = ItemWhitelist::GetHeldDisplayName();
    const bool match = m_useWhitelist->value
        && ItemWhitelist::IsAllowed(heldName, m_whitelist->value);

    if ((int)match != m_lastLoggedMatch)
    {
        m_lastLoggedMatch = (int)match;
        Logger::Info("NoInteract", "match=" + std::string(match ? "true" : "false")
            + " held='" + heldName + "' whitelist='" + m_whitelist->value + "'");
    }

    Patcher::put("nointeract_enabled", "true");
    Patcher::put("nointeract_match", match ? "true" : "false");
}

void NoInteract::onDisable()
{
    Patcher::put("nointeract_enabled", "false");
    Patcher::put("nointeract_match", "false");
}

std::string NoInteract::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    return m_whitelist->value;
}
