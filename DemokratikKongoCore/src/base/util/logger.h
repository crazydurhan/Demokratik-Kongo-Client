#pragma once
#include <iostream>
#include <string>
#include "../util/math/geometry.h"

enum class LogLevel
{
	Trace = 0,
	Debug,
	Info,
	Warn,
	Error
};

struct Logger
{
	static void Init();
	static void Kill();

	// Legacy (maps to General subsystem)
	static void Log(std::string message);
	static void LogPosition(Vector3 message);
	static void Err(std::string message);

	static void LogWait(std::string message, int seconds = 1);
	static void ErrWait(std::string message, int seconds = 1);

	// Structured logging
	static void SetMinLevel(LogLevel level);
	static LogLevel GetMinLevel();
	static void Write(LogLevel level, const char* subsystem, const std::string& message);

	static void Trace(const char* subsystem, const std::string& message);
	static void Debug(const char* subsystem, const std::string& message);
	static void Info(const char* subsystem, const std::string& message);
	static void Warn(const char* subsystem, const std::string& message);
	static void Error(const char* subsystem, const std::string& message);

	static inline bool Initialized = false;
};
