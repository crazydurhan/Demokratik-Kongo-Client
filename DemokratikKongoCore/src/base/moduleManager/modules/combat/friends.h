#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_set>

/*
    Friends — static utility, not a registered Module.

    Holds the friends list shared by the combat/render modules and the
    "Middle click" toggle. The GUI Friends page edits these directly; the
    profile serializer persists them (friendsCsv + friendsMiddleClick).
*/
class Friends
{
public:
    static bool IsFriend(const std::string& name);
    static void ToggleFriend(const std::string& name);

    // True when the "Middle click" setting allows adding/removing friends
    // by middle-clicking a player in-game (read by the WndProc hook).
    static bool MiddleClickEnabled();

    static inline bool MiddleClick = true;
    static inline std::unordered_set<std::string> List;
    static inline std::atomic<uint64_t> Version{ 0 };
};
