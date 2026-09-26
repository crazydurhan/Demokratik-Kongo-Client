#pragma once

#include "imgui.h"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace dk {

enum class LogLevel : uint8_t
{
    Trace,
    Debug,
    Info,
    Ok,
    Warn,
    Error,
};

struct LogEntry
{
    LogLevel    level = LogLevel::Info;
    std::string timestamp;
    std::string message;
};

class LauncherLog {
public:
    static LauncherLog& I();

    void trace(const std::string& msg);
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void ok(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

    void log(LogLevel level, const std::string& msg);
    void logf(LogLevel level, const char* fmt, ...);

    void step(const std::string& stepName, bool success, const std::string& detail = {});
    void separator();

    const std::vector<LogEntry>& entries() const { return entries_; }
    void clear();

    // Runs fn(entries) while holding the log mutex, so the render thread can
    // iterate while the injection thread is appending.
    template <typename Fn>
    void withEntries(Fn&& fn) const
    {
        std::lock_guard lock(mutex_);
        fn(entries_);
    }

    void setPersistToFile(bool enabled) { persistToFile_ = enabled; }
    std::string logFilePath() const;

    static const char* levelLabel(LogLevel level);
    static ImVec4 levelColor(LogLevel level);

private:
    LauncherLog() = default;

    void append(LogLevel level, std::string message);
    void persistLine(const std::string& line);
    std::string timestampNow() const;

    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
    bool persistToFile_ = true;
    static constexpr size_t kMaxEntries = 800;
};

} // namespace dk
