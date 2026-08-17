#include "friends.h"

#include "../../../util/logger.h"
#include "../../../patcher/patcher.h"

bool Friends::MiddleClickEnabled()
{
    return MiddleClick;
}

bool Friends::IsFriend(const std::string& name)
{
    if (name.empty()) return false;
    return List.find(name) != List.end();
}

void Friends::ToggleFriend(const std::string& name)
{
    if (name.empty()) return;

    auto it = List.find(name);
    if (it != List.end())
    {
        List.erase(it);
        Logger::Log("[Friends] Removed: " + name);
    }
    else
    {
        List.insert(name);
        Logger::Log("[Friends] Added: " + name);
    }
    Version.fetch_add(1, std::memory_order_acq_rel);

    // Push the updated CSV to the Java side (esp_friends) so Piercing / ESP
    // / aim-assist hooks ignore friend players immediately.
    std::string out;
    for (const auto& f : List)
    {
        if (!out.empty()) out += ",";
        out += f;
    }
    Patcher::put("esp_friends", out);
}
