#pragma once

#include <vector>
#include <mutex>

/*
    PROJECTX :: ClickPatternStore
    -----------------------------------------------------------------
    Tiny in-memory ring of inter-click delays (in ms) captured by the
    ClickRecorder module. Other modules (e.g. an AutoClicker in
    "Record" mode) can play these back to mimic the user's exact
    cadence on strict anticheats.

    Single global instance, thread-safe via a mutex; the recorder
    writes from onTick(), the player reads at click time.
*/

class ClickPatternStore
{
public:
    static ClickPatternStore& I() { static ClickPatternStore s; return s; }

    // Append a millisecond delay (from previous click) to the buffer.
    // Negative or zero delays are silently dropped to keep playback safe.
    void addDelay(int ms)
    {
        if (ms <= 0) return;
        std::lock_guard<std::mutex> lock(m_mtx);
        while (m_delays.size() >= m_capacity) {
            m_delays.erase(m_delays.begin());
            if (m_cursor > 0) m_cursor--;
        }
        m_delays.push_back(ms);
    }

    // Adjust the rolling buffer's maximum size at runtime. Excess
    // entries get trimmed from the FRONT so newest clicks survive.
    void setCapacity(size_t cap)
    {
        if (cap < 4) cap = 4;
        std::lock_guard<std::mutex> lock(m_mtx);
        m_capacity = cap;
        while (m_delays.size() > m_capacity) {
            m_delays.erase(m_delays.begin());
            if (m_cursor > 0) m_cursor--;
        }
    }

    // Wipe everything (called when ClickRecorder is re-enabled).
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_delays.clear();
        m_cursor = 0;
    }

    // Return the next delay in the recorded sequence (round-robin). If
    // the buffer is empty, returns the supplied fallbackMs so callers
    // can stay running without a guard at every call site.
    int nextDelay(int fallbackMs)
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        if (m_delays.empty()) return fallbackMs;
        int v = m_delays[m_cursor % m_delays.size()];
        m_cursor++;
        return v;
    }

    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_delays.size();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        return m_delays.empty();
    }

private:
    ClickPatternStore() = default;

    mutable std::mutex m_mtx;
    std::vector<int>   m_delays;
    size_t             m_cursor   = 0;
    size_t             m_capacity = 2048;
};
