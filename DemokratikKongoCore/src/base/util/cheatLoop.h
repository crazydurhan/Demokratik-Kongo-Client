#pragma once

// Cheat loop interval (ms). Read by base.cpp main loop.  Default 40ms = 25Hz.
// 40ms keeps the cheat loop off the render hot path; legacy 5ms can be
// restored by storing a smaller value here.
#include <atomic>

namespace CheatLoop
{
    inline std::atomic<int>& IntervalMs()
    {
        static std::atomic<int> v{ 40 };
        return v;
    }

    inline constexpr int kDefaultLoopMs = 40;
}
