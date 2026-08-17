#include "itemLogger.h"

#include "../../../patcher/patcher.h"
#include "../../../util/format.h"
#include "../../../menu/menu.h"

#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr size_t kMaxLogs = 200;

    std::mutex g_logMutex;
    std::deque<std::string> g_logs;
}

void ItemLogger::PushLog(const std::string& message)
{
    if (message.empty()) return;
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logs.push_back(message);
    while (g_logs.size() > kMaxLogs)
        g_logs.pop_front();
}

std::vector<std::string> ItemLogger::SnapshotLogs()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    return std::vector<std::string>(g_logs.begin(), g_logs.end());
}

void ItemLogger::ClearLogs()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logs.clear();
}

ItemLogger::ItemLogger()
    : Module("Item Logger",
             "Logs valuable item drops and pickups. Open Log Manager for the last 200 entries.",
             Category::Utility)
{
    m_minSharpness = &add<NumberSetting>("Min Sharpness", 25.0f, 1.0f, 100.0f, 1.0f);
    m_minSharpness->description =
        "Only log swords with Sharpness (enchant id 16) at or above this level.";

    m_useWhitelist = &add<BoolSetting>("Use Whitelist", true);
    m_whitelist = &add<StringSetting>("Item Whitelist", "");
    m_whitelist->itemList = true;
    m_whitelist->visible = [this]{ return m_useWhitelist->value; };
    m_whitelist->description =
        "Extra items to log, same syntax as combat whitelists: ids (276), id:meta, "
        "name fragments, categories (swords, food, potions, pearls, apples, bows, "
        "arrows, blocks). Empty = built-in valuables only (nether star, blaze rod, sharp swords).";

    m_chatLog = &add<BoolSetting>("Chat Log", true);
    m_chatLog->description = "Also print item logs to the in-game chat.";

    m_openManager = &add<ActionSetting>("Open Log Manager",
        [] { Menu::ItemLogOpen = true; }, "Open");
    m_openManager->description = "Show the panel with the last 200 item logs.";
    m_openManager->valueProvider = []
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Open (%d)", (int)ItemLogger::SnapshotLogs().size());
        return std::string(buf);
    };
}

void ItemLogger::onDisable()
{
    Patcher::put("itemlog_enabled", "false");
}

std::string ItemLogger::arrayListSuffix(SuffixDetail detail) const
{
    if (detail == SuffixDetail::None) return "";
    char buf[24];
    std::snprintf(buf, sizeof(buf), "Sh%d+", (int)m_minSharpness->value);
    return buf;
}

void ItemLogger::onTick()
{
    if (!isEnabled())
    {
        Patcher::put("itemlog_enabled", "false");
        return;
    }

    Patcher::put("itemlog_enabled", "true");
    Patcher::put("itemlog_min_sharpness", istr((int)m_minSharpness->value));
    Patcher::put("itemlog_whitelist",
                 (m_useWhitelist->value && m_whitelist) ? m_whitelist->value : "");
    Patcher::put("itemlog_chat", bstr(m_chatLog && m_chatLog->value));
}
