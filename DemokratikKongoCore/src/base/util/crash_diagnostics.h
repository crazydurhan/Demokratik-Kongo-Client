#pragma once

#include <Windows.h>
#include <string>
#include <chrono>
#include <vector>
#include <mutex>
#include <fstream>

#include "../../../ext/jni/jni.h"

/*
========================================================================
    PROJECTX :: Crash Diagnostics & Global Debug System
------------------------------------------------------------------------
    Early-stage debugger that wraps the entire DLL lifecycle.
    
    Features:
    1. Boot Logger      - ms-precision timeline of every init stage
    2. SEH Guard        - catches access violations, stack overflows etc.
    3. JNI Monitor      - logs every JNI exception with class name + context
    4. Thread Validator  - ensures OpenGL calls are on the correct thread
    5. Crash Reporter    - writes detailed crash log to file before exit
    
    Usage:
        CrashDiag::Init();           // very first call in DllMain thread
        CrashDiag::BeginStage("X");  // before each init stage
        CrashDiag::EndStage("X");    // after each init stage
        CrashDiag::Guard([](){ ... }); // SEH-wrapped execution
========================================================================
*/

class CrashDiag
{
public:
    // ── Lifecycle ────────────────────────────────────────────────────
    static void Init();      // Call ONCE, before anything else
    static void Shutdown();  // Call on clean exit

    // ── Boot Timeline ────────────────────────────────────────────────
    static void BeginStage(const char* stageName);
    static void EndStage(const char* stageName);
    static void LogEvent(const char* level, const char* subsystem, const char* fmt, ...);

    // ── SEH Guard ────────────────────────────────────────────────────
    // Wraps a callable in __try/__except. Returns true if no exception.
    // On exception: logs full details to file and console.
    typedef void(*GuardCallback)();
    static bool Guard(GuardCallback fn, const char* context);

    // Also captures unhandled exceptions globally
    static LONG WINAPI GlobalExceptionHandler(EXCEPTION_POINTERS* pExInfo);

    // ── JNI Exception Monitor ────────────────────────────────────────
    // Returns true if JNI exception was pending (clears it + logs)
    static bool CheckJNI(JNIEnv* env, const char* context);

    // ── Thread Safety ────────────────────────────────────────────────
    static void SetMainThreadId(DWORD tid);
    static bool IsMainThread();
    static void AssertMainThread(const char* context);
    static DWORD GetMainThreadId();

    // ── Crash Report ─────────────────────────────────────────────────
    static void WriteFatalReport(const char* title, const char* details);
    static std::string GetLogFilePath();
    /** Mirror a formatted log line to projectx_crash_debug.log (no console duplicate). */
    static void AppendLine(const std::string& line);

    // ── State ────────────────────────────────────────────────────────
    static inline bool Initialized = false;

private:
    // Boot timeline entry
    struct StageEntry {
        std::string name;
        std::string status;     // "BEGIN", "END", "CRASH"
        double      elapsedMs;  // ms since CrashDiag::Init()
        double      durationMs; // for END entries, time spent in stage
    };

    static inline std::vector<StageEntry>    m_timeline;
    static inline std::mutex                 m_mutex;
    static inline DWORD                      m_mainThreadId = 0;
    static inline LPTOP_LEVEL_EXCEPTION_FILTER m_oldExceptionFilter = nullptr;

    static inline std::chrono::high_resolution_clock::time_point m_initTime;
    static inline std::chrono::high_resolution_clock::time_point m_stageStart;
    static inline std::string m_currentStage;

    static inline std::string m_logPath;
    static inline std::ofstream m_logFile;

    static double ElapsedMs();
    static const char* ExceptionCodeToString(DWORD code);
    static void WriteTimelineToFile();
};

// ── Macros for convenience ──────────────────────────────────────────
#define DIAG_STAGE_BEGIN(name)  CrashDiag::BeginStage(name)
#define DIAG_STAGE_END(name)   CrashDiag::EndStage(name)
#define DIAG_LOG(sub, fmt, ...)  CrashDiag::LogEvent("INFO", sub, fmt, ##__VA_ARGS__)
#define DIAG_WARN(sub, fmt, ...) CrashDiag::LogEvent("WARN", sub, fmt, ##__VA_ARGS__)
#define DIAG_ERR(sub, fmt, ...)  CrashDiag::LogEvent("ERROR", sub, fmt, ##__VA_ARGS__)
#define DIAG_CHECK_JNI(env, ctx) CrashDiag::CheckJNI(env, ctx)
