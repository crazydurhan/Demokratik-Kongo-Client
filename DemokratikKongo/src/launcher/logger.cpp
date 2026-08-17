#include "logger.h"
#include "stealthNames.h"

#include "util.h"

#include <Shlobj.h>
#include <cstdarg>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace dk {

LauncherLog& LauncherLog::I()
{
    static LauncherLog g;
    return g;
}

const char* LauncherLog::levelLabel(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Ok:    return "OK";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Error: return "ERROR";
    default:              return "?";
    }
}

ImVec4 LauncherLog::levelColor(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return ImVec4(0.45f, 0.48f, 0.52f, 1.0f);
    case LogLevel::Debug: return ImVec4(0.55f, 0.60f, 0.68f, 1.0f);
    case LogLevel::Info:  return ImVec4(0.75f, 0.82f, 0.92f, 1.0f);
    case LogLevel::Ok:    return ImVec4(0.35f, 0.90f, 0.50f, 1.0f);
    case LogLevel::Warn:  return ImVec4(1.00f, 0.78f, 0.30f, 1.0f);
    case LogLevel::Error: return ImVec4(1.00f, 0.42f, 0.42f, 1.0f);
    default:              return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

std::string LauncherLog::timestampNow() const
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_s(&tm, &t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string LauncherLog::logFilePath() const
{
    wchar_t* roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) || !roaming)
        return {};

    std::wstring dir = roaming;
    CoTaskMemFree(roaming);

    std::error_code ec;
    const std::wstring appDir = dir + L'\\' + stealth::kAppFolder;
    std::filesystem::create_directories(appDir, ec);
    return wideToUtf8(appDir + L"\\injector.log");
}

void LauncherLog::persistLine(const std::string& line)
{
    if (!persistToFile_)
        return;

    const std::string path = logFilePath();
    if (path.empty())
        return;

    std::ofstream out(path, std::ios::app);
    if (out)
        out << line << '\n';
}

void LauncherLog::append(LogLevel level, std::string message)
{
    std::lock_guard lock(mutex_);

    LogEntry e;
    e.level = level;
    e.timestamp = timestampNow();
    e.message = std::move(message);
    entries_.push_back(std::move(e));

    if (entries_.size() > kMaxEntries)
        entries_.erase(entries_.begin(), entries_.begin() + (entries_.size() - kMaxEntries));

    std::ostringstream line;
    line << '[' << entries_.back().timestamp << "] [" << levelLabel(level) << "] "
         << entries_.back().message;
    persistLine(line.str());
}

void LauncherLog::log(LogLevel level, const std::string& msg)
{
    append(level, msg);
}

void LauncherLog::logf(LogLevel level, const char* fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    append(level, buf);
}

void LauncherLog::trace(const std::string& msg) { log(LogLevel::Trace, msg); }
void LauncherLog::debug(const std::string& msg) { log(LogLevel::Debug, msg); }
void LauncherLog::info(const std::string& msg)  { log(LogLevel::Info, msg); }
void LauncherLog::ok(const std::string& msg)    { log(LogLevel::Ok, msg); }
void LauncherLog::warn(const std::string& msg)  { log(LogLevel::Warn, msg); }
void LauncherLog::error(const std::string& msg) { log(LogLevel::Error, msg); }

void LauncherLog::step(const std::string& stepName, bool success, const std::string& detail)
{
    if (detail.empty())
        log(success ? LogLevel::Ok : LogLevel::Error, stepName + (success ? " — OK" : " — FAILED"));
    else
        log(success ? LogLevel::Ok : LogLevel::Error, stepName + (success ? " — OK: " : " — FAILED: ") + detail);
}

void LauncherLog::separator()
{
    append(LogLevel::Debug, "────────────────────────────────────────");
}

void LauncherLog::clear()
{
    std::lock_guard lock(mutex_);
    entries_.clear();
}

} // namespace dk
