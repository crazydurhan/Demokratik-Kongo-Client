#pragma once

#include <string>

namespace ItemWhitelist {

std::string GetHeldDisplayName();
int GetHeldHotbarSlot();
bool IsAllowed(const std::string& heldItemName, const std::string& configList);
bool MatchesDisplayName(const std::string& displayName, const std::string& configList);
// Full matcher for non-held stacks (ItemLogger ground/pickup items): Java
// supplies id/meta/isBlock so numeric and category tokens work there too.
bool MatchesItem(const std::string& displayName, int itemId, int itemMeta,
                 bool isBlock, const std::string& configList);

} // namespace ItemWhitelist
