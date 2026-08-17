#include <Windows.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <iomanip>
#include <sstream>

#include "logger.h"
#include "crash_diagnostics.h"

FILE* out;
FILE* err;

namespace
{
	std::mutex g_logMutex;
	LogLevel g_minLevel = LogLevel::Debug;

	const char* levelTag(LogLevel level)
	{
		switch (level)
		{
		case LogLevel::Trace: return "TRACE";
		case LogLevel::Debug: return "DEBUG";
		case LogLevel::Info:  return "INFO ";
		case LogLevel::Warn:  return "WARN ";
		case LogLevel::Error: return "ERROR";
		default:              return "?????";
		}
	}

	std::string timestampNow()
	{
		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()) % 1000;

		struct tm lt {};
		localtime_s(&lt, &t);

		std::ostringstream oss;
		oss << std::setfill('0')
			<< std::setw(2) << lt.tm_hour << ':'
			<< std::setw(2) << lt.tm_min << ':'
			<< std::setw(2) << lt.tm_sec << '.'
			<< std::setw(3) << ms.count();
		return oss.str();
	}
}

void Logger::Init()
{
	// AllocConsole();
	// freopen_s(&out, "CONOUT$", "w", stdout);
	// freopen_s(&err, "CONOUT$", "w", stderr);
	Logger::Initialized = true;
}

void Logger::Kill()
{
	if (!Logger::Initialized)
		return;

	if (out) { fclose(out); out = nullptr; }
	if (err) { fclose(err); err = nullptr; }
	// FreeConsole();
	Logger::Initialized = false;
}

void Logger::SetMinLevel(LogLevel level)
{
	g_minLevel = level;
}

LogLevel Logger::GetMinLevel()
{
	return g_minLevel;
}

void Logger::Write(LogLevel level, const char* subsystem, const std::string& message)
{
	if (!Logger::Initialized) Logger::Init();
	if (level < g_minLevel) return;

	const char* sub = (subsystem && subsystem[0]) ? subsystem : "General";
	std::string line = "[" + timestampNow() + "] [" + levelTag(level) + "] [" + sub + "] " + message;

	std::lock_guard<std::mutex> lock(g_logMutex);

	if (level >= LogLevel::Error)
		std::cout << "[ ERR ] :: " << line << std::endl;
	else if (level >= LogLevel::Warn)
		std::cout << "[ WRN ] :: " << line << std::endl;
	else
		std::cout << "[ LOG ] :: " << line << std::endl;

	if (CrashDiag::Initialized && std::string(sub) != "CrashDiag")
		CrashDiag::AppendLine(line);
}

void Logger::Trace(const char* subsystem, const std::string& message)
{
	Write(LogLevel::Trace, subsystem, message);
}

void Logger::Debug(const char* subsystem, const std::string& message)
{
	Write(LogLevel::Debug, subsystem, message);
}

void Logger::Info(const char* subsystem, const std::string& message)
{
	Write(LogLevel::Info, subsystem, message);
}

void Logger::Warn(const char* subsystem, const std::string& message)
{
	Write(LogLevel::Warn, subsystem, message);
}

void Logger::Error(const char* subsystem, const std::string& message)
{
	Write(LogLevel::Error, subsystem, message);
}

void Logger::Log(std::string message)
{
	Info("General", message);
}

void Logger::LogPosition(Vector3 position)
{
	std::ostringstream oss;
	oss << "X=" << position.x << " Y=" << position.y << " Z=" << position.z;
	Debug("Position", oss.str());
}

void Logger::Err(std::string message)
{
	Error("General", message);
}

void Logger::LogWait(std::string message, int seconds)
{
	Log(message);
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

void Logger::ErrWait(std::string message, int seconds)
{
	Err(message);
	std::this_thread::sleep_for(std::chrono::seconds(seconds));
}
