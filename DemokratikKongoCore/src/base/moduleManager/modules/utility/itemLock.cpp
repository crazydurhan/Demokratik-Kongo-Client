#include "itemLock.h"

#include "../combat/itemWhitelist.h"
#include "../../../patcher/patcher.h"
#include "../../../util/logger.h"

#include <sstream>

ItemLock::ItemLock()
    : Module("Item Lock",
             "Prevents dropping whitelisted items with the drop key (Q / Ctrl+Q).",
             Category::Utility)
{
    m_useWhitelist = &add<BoolSetting>("Use Whitelist", true);
    m_whitelist = &add<StringSetting>("Item Whitelist", "276,322");
    m_whitelist->itemList = true;
    m_whitelist->visible = [this]{ return m_useWhitelist->value; };
    m_whitelist->description =
        "Same syntax as combat whitelists: ids (276), id:meta (322:1), name fragments, "
        "categories (swords, axes, pickaxes, shovels, food, potions, pearls, apples, bows, "
        "arrows, blocks), 'slot N', 'hand'. 276 = diamond sword, 322 = golden apple.";

    m_slotWhitelist = &add<StringSetting>("Slot Whitelist", "");
    m_slotWhitelist->slotPicker = true;
    m_slotWhitelist->description =
        "Hotbar slots to keep locked (tap to toggle, 1-9). "
        "Locked regardless of the held item; combined with the item whitelist above.";
}

namespace
{
    // Turns "1,5,9" into ",slot 1,slot 5,slot 9" so the shared combat
    // whitelist engine evaluates the slots with its native 'slot N' token.
    // Raw numbers must never reach the engine directly — there they mean
    // item ids, not hotbar slots.
    void AppendSlotTokens(std::string& list, const std::string& slotsCsv)
    {
        std::stringstream ss(slotsCsv);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            const size_t start = token.find_first_not_of(" \t\r\n");
            const size_t end = token.find_last_not_of(" \t\r\n");
            if (start == std::string::npos)
                continue;

            const std::string trimmed = token.substr(start, end - start + 1);
            if (trimmed.empty() ||
                trimmed.find_first_not_of("0123456789") != std::string::npos)
                continue;

            int slot = 0;
            try { slot = std::stoi(trimmed); } catch (...) { continue; }
            if (slot < 1 || slot > 9)
                continue;

            list += ",slot " + std::to_string(slot);
        }
    }
}

void ItemLock::onTick()
{
    if (!isEnabled())
    {
        Patcher::put("itemlock_enabled", "false");
        Patcher::put("itemlock_match", "false");
        return;
    }

    // Match in C++ with the combat whitelist engine (ids, categories, slots)
    // and push the resolved verdict — the Java dropOneItem hook only reads
    // the boolean, so matching can never diverge from AimAssist/Piercing.
    const std::string heldName = ItemWhitelist::GetHeldDisplayName();

    std::string combined;
    if (m_useWhitelist->value)
        combined = m_whitelist->value;
    if (m_slotWhitelist && !m_slotWhitelist->value.empty())
        AppendSlotTokens(combined, m_slotWhitelist->value);

    const bool match = ItemWhitelist::IsAllowed(heldName, combined);

    if ((int)match != m_lastLoggedMatch)
    {
        m_lastLoggedMatch = (int)match;
        Logger::Info("ItemLock", "match=" + std::string(match ? "true" : "false")
            + " held='" + heldName + "' whitelist='" + combined + "'");
    }

    Patcher::put("itemlock_enabled", "true");
    Patcher::put("itemlock_match", match ? "true" : "false");
}

void ItemLock::onDisable()
{
    Patcher::put("itemlock_enabled", "false");
    Patcher::put("itemlock_match", "false");
}

std::string ItemLock::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    if (m_slotWhitelist && !m_slotWhitelist->value.empty())
        return m_whitelist->value + " | slots " + m_slotWhitelist->value;
    return m_whitelist->value;
}
