#include "crash_diagnostics.h"
#include "logger.h"

#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <DbgHelp.h>

#pragma comment(lib, "dbghelp.lib")

// ═══════════════════════════════════════════════════════════════════════
//  Initialization & Shutdown
// ═══════════════════════════════════════════════════════════════════════

void CrashDiag::Init()
{
    if (Initialized) return;

    m_initTime = std::chrono::high_resolution_clock::now();
    m_mainThreadId = GetCurrentThreadId();

    // Build log file path: same directory as the DLL, or TEMP
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    m_logPath = std::string(tempPath) + "demokratikkongo_crash_debug.log";

    // Open log file (append mode)
    m_logFile.open(m_logPath, std::ios::out | std::ios::trunc);
    if (m_logFile.is_open()) {
        // Write header
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm lt;
        localtime_s(&lt, &t);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &lt);

        m_logFile << "═══════════════════════════════════════════════════════════\n";
        m_logFile << "  DemokratikKongo :: Crash Diagnostics Log\n";
        m_logFile << "  Started: " << timeBuf << "\n";
        m_logFile << "  Main Thread ID: " << m_mainThreadId << "\n";
        m_logFile << "  Process ID: " << GetCurrentProcessId() << "\n";
        m_logFile << "═══════════════════════════════════════════════════════════\n\n";
        m_logFile.flush();
    }

    // Install global SEH handler
    m_oldExceptionFilter = SetUnhandledExceptionFilter(GlobalExceptionHandler);

    Initialized = true;

    LogEvent("INFO", "CrashDiag", "Diagnostics initialized. Log: %s", m_logPath.c_str());
}

void CrashDiag::Shutdown()
{
    if (!Initialized) return;

    LogEvent("INFO", "CrashDiag", "Clean shutdown.");
    WriteTimelineToFile();

    if (m_logFile.is_open()) {
        m_logFile << "\n[SHUTDOWN] Clean exit.\n";
        m_logFile.close();
    }

    // Restore original exception filter
    if (m_oldExceptionFilter) {
        SetUnhandledExceptionFilter(m_oldExceptionFilter);
        m_oldExceptionFilter = nullptr;
    }

    Initialized = false;
}

// ═══════════════════════════════════════════════════════════════════════
//  Boot Timeline
// ═══════════════════════════════════════════════════════════════════════

double CrashDiag::ElapsedMs()
{
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(now - m_initTime).count();
}

void CrashDiag::BeginStage(const char* stageName)
{
    if (!Initialized) return;

    char buf[256];
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        double ms = ElapsedMs();
        m_currentStage = stageName;
        m_stageStart = std::chrono::high_resolution_clock::now();

        StageEntry entry;
        entry.name = stageName;
        entry.status = "BEGIN";
        entry.elapsedMs = ms;
        entry.durationMs = 0;
        m_timeline.push_back(entry);

        snprintf(buf, sizeof(buf), "[+%8.1f ms] ── BEGIN ── %s", ms, stageName);

        if (m_logFile.is_open()) {
            m_logFile << buf << "\n";
            m_logFile.flush();
        }
    }

    Logger::Log(std::string("[CrashDiag] ") + buf);
}

void CrashDiag::EndStage(const char* stageName)
{
    if (!Initialized) return;

    char buf[256];
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        double ms = ElapsedMs();
        auto now = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::milli>(now - m_stageStart).count();

        StageEntry entry;
        entry.name = stageName;
        entry.status = "END";
        entry.elapsedMs = ms;
        entry.durationMs = duration;
        m_timeline.push_back(entry);

        m_currentStage.clear();

        snprintf(buf, sizeof(buf), "[+%8.1f ms] ── END   ── %s (%.1f ms)", ms, stageName, duration);

        if (m_logFile.is_open()) {
            m_logFile << buf << "\n";
            m_logFile.flush();
        }
    }

    Logger::Log(std::string("[CrashDiag] ") + buf);
}

void CrashDiag::LogEvent(const char* level, const char* subsystem, const char* fmt, ...)
{
    if (!Initialized) return;

    char msgBuf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    double ms = ElapsedMs();

    char fullBuf[640];
    snprintf(fullBuf, sizeof(fullBuf), "[+%8.1f ms] [%s] [%s] %s", ms, level, subsystem, msgBuf);

    // Console
    if (strcmp(level, "ERROR") == 0)
        Logger::Err(std::string("[CrashDiag] ") + fullBuf);
    else
        Logger::Log(std::string("[CrashDiag] ") + fullBuf);

    // File
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open()) {
        m_logFile << fullBuf << "\n";
        m_logFile.flush();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  SEH Guard
// ═══════════════════════════════════════════════════════════════════════

// We need a separate .cpp / wrapper because __try/__except cannot
// coexist with C++ objects with destructors in the same function.
// So we use a plain function pointer callback approach.

static CrashDiag::GuardCallback g_guardFn = nullptr;
static const char* g_guardContext = nullptr;

static void ExecuteGuarded()
{
    if (g_guardFn) g_guardFn();
}

static LONG WINAPI GuardExceptionFilter(EXCEPTION_POINTERS* pExInfo, const char* context)
{
    if (pExInfo)
    {
        CrashDiag::GlobalExceptionHandler(pExInfo);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

bool CrashDiag::Guard(GuardCallback fn, const char* context)
{
    g_guardFn = fn;
    g_guardContext = context;

    __try
    {
        ExecuteGuarded();
        return true;
    }
    __except (GuardExceptionFilter(GetExceptionInformation(), context))
    {
        return false;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Global Unhandled Exception Handler
// ═══════════════════════════════════════════════════════════════════════

LONG WINAPI CrashDiag::GlobalExceptionHandler(EXCEPTION_POINTERS* pExInfo)
{
    if (!pExInfo || !pExInfo->ExceptionRecord) {
        if (m_oldExceptionFilter)
            return m_oldExceptionFilter(pExInfo);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    DWORD code = pExInfo->ExceptionRecord->ExceptionCode;

    // Skip non-fatal exceptions (breakpoints, C++ exceptions, etc.)
    if (code == EXCEPTION_BREAKPOINT || code == 0xE06D7363 /* C++ exception */) {
        if (m_oldExceptionFilter)
            return m_oldExceptionFilter(pExInfo);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Build crash report
    std::ostringstream ss;
    ss << "═══════════════════════════════════════════════════════════\n";
    ss << "  FATAL UNHANDLED EXCEPTION\n";
    ss << "═══════════════════════════════════════════════════════════\n\n";
    ss << "Exception Code : 0x" << std::hex << code << " (" << ExceptionCodeToString(code) << ")\n";
    ss << "Address        : 0x" << std::hex << (uintptr_t)pExInfo->ExceptionRecord->ExceptionAddress << "\n";
    ss << "Thread ID      : " << std::dec << GetCurrentThreadId() << "\n";
    ss << "Main Thread    : " << (IsMainThread() ? "YES" : "NO") << "\n";

    if (!m_currentStage.empty()) {
        ss << "Active Stage   : " << m_currentStage << "\n";
    }

    // Context registers
    if (pExInfo->ContextRecord) {
        auto* ctx = pExInfo->ContextRecord;
#ifdef _WIN64
        ss << "\n── Registers (x64) ──\n";
        ss << "  RAX=0x" << std::hex << ctx->Rax << "  RBX=0x" << ctx->Rbx << "\n";
        ss << "  RCX=0x" << std::hex << ctx->Rcx << "  RDX=0x" << ctx->Rdx << "\n";
        ss << "  RSI=0x" << std::hex << ctx->Rsi << "  RDI=0x" << ctx->Rdi << "\n";
        ss << "  RSP=0x" << std::hex << ctx->Rsp << "  RBP=0x" << ctx->Rbp << "\n";
        ss << "  RIP=0x" << std::hex << ctx->Rip << "\n";
        ss << "  R8 =0x" << std::hex << ctx->R8  << "  R9 =0x" << ctx->R9  << "\n";
        ss << "  R10=0x" << std::hex << ctx->R10 << "  R11=0x" << ctx->R11 << "\n";
        ss << "  R12=0x" << std::hex << ctx->R12 << "  R13=0x" << ctx->R13 << "\n";
        ss << "  R14=0x" << std::hex << ctx->R14 << "  R15=0x" << ctx->R15 << "\n";
#else
        ss << "\n── Registers (x86) ──\n";
        ss << "  EAX=0x" << std::hex << ctx->Eax << "  EBX=0x" << ctx->Ebx << "\n";
        ss << "  ECX=0x" << std::hex << ctx->Ecx << "  EDX=0x" << ctx->Edx << "\n";
        ss << "  ESI=0x" << std::hex << ctx->Esi << "  EDI=0x" << ctx->Edi << "\n";
        ss << "  ESP=0x" << std::hex << ctx->Esp << "  EBP=0x" << ctx->Ebp << "\n";
        ss << "  EIP=0x" << std::hex << ctx->Eip << "\n";
#endif
    }

    // Stack trace via DbgHelp
    ss << "\n── Stack Trace ──\n";
    {
        HANDLE process = GetCurrentProcess();
        HANDLE thread  = GetCurrentThread();
        SymInitialize(process, NULL, TRUE);

        STACKFRAME64 frame = {};
        CONTEXT ctx = *pExInfo->ContextRecord;

#ifdef _WIN64
        frame.AddrPC.Offset    = ctx.Rip;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrStack.Offset = ctx.Rsp;
        DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
#else
        frame.AddrPC.Offset    = ctx.Eip;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrStack.Offset = ctx.Esp;
        DWORD machineType = IMAGE_FILE_MACHINE_I386;
#endif
        frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        for (int i = 0; i < 32; i++) {
            if (!StackWalk64(machineType, process, thread, &frame, &ctx,
                            NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                break;

            if (frame.AddrPC.Offset == 0) break;

            char symbolBuf[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuf;
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen   = 255;

            DWORD64 displacement64 = 0;
            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD displacement32 = 0;

            ss << "  [" << std::dec << i << "] 0x" << std::hex << frame.AddrPC.Offset;

            if (SymFromAddr(process, frame.AddrPC.Offset, &displacement64, symbol)) {
                ss << " " << symbol->Name << "+0x" << std::hex << displacement64;
            }

            if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &displacement32, &line)) {
                ss << " (" << line.FileName << ":" << std::dec << line.LineNumber << ")";
            }

            // Module name
            HMODULE hMod = NULL;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)frame.AddrPC.Offset, &hMod) && hMod) {
                char modName[MAX_PATH];
                GetModuleFileNameA(hMod, modName, MAX_PATH);
                // Just get the filename
                char* lastSlash = strrchr(modName, '\\');
                ss << " [" << (lastSlash ? lastSlash + 1 : modName) << "]";
            }

            ss << "\n";
        }

        SymCleanup(process);
    }

    // Boot timeline dump
    ss << "\n── Boot Timeline ──\n";
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& e : m_timeline) {
            ss << "  [+" << std::fixed << std::setprecision(1) << e.elapsedMs << " ms] "
               << e.status << " " << e.name;
            if (e.status == "END") ss << " (" << e.durationMs << " ms)";
            ss << "\n";
        }
    }

    // Write to file
    WriteFatalReport("Unhandled Exception", ss.str().c_str());

    // Chain to previous handler
    if (m_oldExceptionFilter)
        return m_oldExceptionFilter(pExInfo);

    return EXCEPTION_CONTINUE_SEARCH;
}

// ═══════════════════════════════════════════════════════════════════════
//  JNI Exception Monitor
// ═══════════════════════════════════════════════════════════════════════

bool CrashDiag::CheckJNI(JNIEnv* env, const char* context)
{
    if (!env || !env->ExceptionCheck()) return false;

    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();

    if (!exc) {
        LogEvent("ERROR", "JNI", "Exception in [%s]: (could not retrieve throwable)", context);
        return true;
    }

    // Get exception class name
    std::string excClassName = "unknown";
    std::string excMessage = "";

    jclass excClass = env->GetObjectClass(exc);
    if (excClass) {
        jclass classClass = env->FindClass("java/lang/Class");
        if (classClass) {
            jmethodID getNameId = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
            if (getNameId) {
                jstring nameStr = (jstring)env->CallObjectMethod(excClass, getNameId);
                if (nameStr) {
                    const char* nameChars = env->GetStringUTFChars(nameStr, nullptr);
                    if (nameChars) {
                        excClassName = nameChars;
                        env->ReleaseStringUTFChars(nameStr, nameChars);
                    }
                    env->DeleteLocalRef(nameStr);
                }
            }
            env->DeleteLocalRef(classClass);
        }

        // Get exception message
        jmethodID getMessageId = env->GetMethodID(excClass, "getMessage", "()Ljava/lang/String;");
        if (getMessageId) {
            jstring msgStr = (jstring)env->CallObjectMethod(exc, getMessageId);
            if (!env->ExceptionCheck() && msgStr) {
                const char* msgChars = env->GetStringUTFChars(msgStr, nullptr);
                if (msgChars) {
                    excMessage = msgChars;
                    env->ReleaseStringUTFChars(msgStr, msgChars);
                }
                env->DeleteLocalRef(msgStr);
            }
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        env->DeleteLocalRef(excClass);
    }

    env->DeleteLocalRef(exc);

    // Log the exception
    if (excMessage.empty()) {
        LogEvent("ERROR", "JNI", "Exception in [%s]: %s", context, excClassName.c_str());
    } else {
        LogEvent("ERROR", "JNI", "Exception in [%s]: %s: %s", context, excClassName.c_str(), excMessage.c_str());
    }

    // Check for critical exception types
    if (excClassName.find("ClassFormatError") != std::string::npos ||
        excClassName.find("VerifyError") != std::string::npos ||
        excClassName.find("LinkageError") != std::string::npos) {
        LogEvent("ERROR", "JNI", "!!! CRITICAL: Bytecode corruption detected in [%s] - class: %s",
                 context, excClassName.c_str());
    }

    if (excClassName.find("IllegalAccessError") != std::string::npos) {
        LogEvent("WARN", "JNI", "!!! ACCESS DENIED in [%s] - AntiCheat may be blocking access: %s",
                 context, excMessage.c_str());
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════
//  Thread Safety
// ═══════════════════════════════════════════════════════════════════════

void CrashDiag::SetMainThreadId(DWORD tid)
{
    m_mainThreadId = tid;
}

bool CrashDiag::IsMainThread()
{
    return GetCurrentThreadId() == m_mainThreadId;
}

DWORD CrashDiag::GetMainThreadId()
{
    return m_mainThreadId;
}

void CrashDiag::AssertMainThread(const char* context)
{
    if (!IsMainThread()) {
        LogEvent("ERROR", "Thread",
            "Thread violation in [%s]: Called on thread %lu, expected main thread %lu. "
            "OpenGL context is NOT available on this thread!",
            context, GetCurrentThreadId(), m_mainThreadId);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Crash Report Writer
// ═══════════════════════════════════════════════════════════════════════

void CrashDiag::WriteFatalReport(const char* title, const char* details)
{
    // Write to the main log file
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open()) {
            m_logFile << "\n╔═══════════════════════════════════════════════════════════╗\n";
            m_logFile << "║  FATAL: " << title << "\n";
            m_logFile << "╚═══════════════════════════════════════════════════════════╝\n";
            m_logFile << details << "\n";
            m_logFile.flush();
        }
    }

    // Also write to separate crash file for easy finding
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string crashPath = std::string(tempPath) + "demokratikkongo_fatal_crash.log";

    std::ofstream crashFile(crashPath, std::ios::out | std::ios::trunc);
    if (crashFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm lt;
        localtime_s(&lt, &t);
        char timeBuf[64];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &lt);

        crashFile << "DemokratikKongo Fatal Crash Report\n";
        crashFile << "Time: " << timeBuf << "\n";
        crashFile << "Title: " << title << "\n\n";
        crashFile << details << "\n";
        crashFile.close();
    }

    // Console output
    Logger::Err(std::string("[CrashDiag] FATAL: ") + title);
}

std::string CrashDiag::GetLogFilePath()
{
    return m_logPath;
}

void CrashDiag::AppendLine(const std::string& line)
{
    if (!Initialized) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFile.is_open())
    {
        m_logFile << line << "\n";
        m_logFile.flush();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════════

const char* CrashDiag::ExceptionCodeToString(DWORD code)
{
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_OVERFLOW:             return "FLT_OVERFLOW";
        case EXCEPTION_FLT_UNDERFLOW:            return "FLT_UNDERFLOW";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
        case EXCEPTION_INVALID_DISPOSITION:      return "INVALID_DISPOSITION";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        case STATUS_HEAP_CORRUPTION:             return "HEAP_CORRUPTION";
        default:                                 return "UNKNOWN";
    }
}

void CrashDiag::WriteTimelineToFile()
{
    if (!m_logFile.is_open()) return;

    m_logFile << "\n── Complete Boot Timeline ──\n";
    for (auto& e : m_timeline) {
        m_logFile << "  [+" << std::fixed << std::setprecision(1) << e.elapsedMs << " ms] "
                  << e.status << " " << e.name;
        if (e.status == "END") m_logFile << " (" << e.durationMs << " ms)";
        m_logFile << "\n";
    }
    m_logFile.flush();
}
