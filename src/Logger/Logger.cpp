#include "Logger.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <vector>
#include <deque>
#include <cstdio>
#include <csignal>

namespace
{
    static std::tm localTime(std::time_t t)
    {
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        tm = *std::localtime(&t);
#endif
        return tm;
    }

    static std::string nowTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        const std::tm tm = localTime(t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
}

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

void Logger::setMinimumLogLevel(LogLevel level)
{
    minimumLevel = level;
}

void Logger::setSuppressStdout(bool suppress)
{
    suppressStdout = suppress;
}

const char* Logger::toString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::INFO: return "INFO";
    case LogLevel::WARNING: return "WARNING";
    case LogLevel::ERROR: return "ERROR";
    case LogLevel::FATAL: return "FATAL";
    default: return "INFO";
    }
}

const char* Logger::toString(Category category)
{
    switch (category)
    {
    case Category::General: return "General";
    case Category::Engine: return "Engine";
    case Category::Scripting: return "Scripting";
    case Category::AssetManagement: return "AssetManagement";
    case Category::Diagnostics: return "Diagnostics";
    case Category::Rendering: return "Rendering";
    case Category::Input: return "Input";
    case Category::Project: return "Project";
    case Category::IO: return "IO";
    case Category::UI: return "UI";
    default: return "General";
    }
}

void Logger::initialize()
{
    if (initialized)
    {
        return;
    }

    std::filesystem::path logDir = std::filesystem::current_path() / "Logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    const std::tm tm = localTime(t);

    std::ostringstream oss;
    oss << "log_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".log";
    filename = (logDir / oss.str()).string();

    logFile.open(filename, std::ios::out | std::ios::trunc);
    if (!logFile.is_open())
    {
        std::cerr << "Failed to open log file: " << filename << std::endl;
    }
    else
    {
        initialized = true;
    }

    if (initialized)
    {
        std::vector<std::filesystem::directory_entry> logFiles;
        for (const auto& entry : std::filesystem::directory_iterator(logDir, ec))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            const auto& path = entry.path();
            if (path.extension() == ".log")
            {
                logFiles.push_back(entry);
            }
        }

        std::sort(logFiles.begin(), logFiles.end(), [](const auto& a, const auto& b)
            {
                return a.last_write_time() > b.last_write_time();
            });

        for (size_t i = 5; i < logFiles.size(); ++i)
        {
            std::filesystem::remove(logFiles[i].path(), ec);
        }
    }

    log(Category::Engine, std::string("Logger initialised. Output file: ") + filename, LogLevel::INFO);
}

Logger::~Logger()
{
    if (logFile.is_open())
    {
        logFile.close();
    }
}

void Logger::log(const std::string& message, LogLevel level)
{
    log(Category::General, message, level);
}

void Logger::log(Category category, const std::string& message, LogLevel level)
{
    if (static_cast<int>(level) < static_cast<int>(minimumLevel))
    {
        return;
    }

    std::lock_guard<std::mutex> lock(logMutex);

    if (level == LogLevel::ERROR)
    {
        loggedError = true;
    }
    else if (level == LogLevel::FATAL)
    {
        loggedFatal = true;
    }

    const std::string ts = nowTimestamp();
    const char* levelStr = toString(level);
    const char* catStr = toString(category);

    if (logFile.is_open())
    {
        logFile << "[" << ts << "][" << catStr << "][" << levelStr << "] " << message << '\n';
        logFile.flush();
    }

    if (!suppressStdout)
    {
        std::cout << "[" << ts << "][" << catStr << "][" << levelStr << "] " << message << std::endl;
    }

    // Append to console ring-buffer
    ConsoleEntry entry;
    entry.timestamp = ts;
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.sequenceId = m_nextSequenceId++;
    m_consoleEntries.push_back(std::move(entry));
    while (m_consoleEntries.size() > kMaxConsoleEntries)
    {
        m_consoleEntries.pop_front();
    }
}

void Logger::clearConsoleBuffer()
{
    std::lock_guard<std::mutex> lock(logMutex);
    m_consoleEntries.clear();
}

const char* Logger::levelToString(LogLevel level)
{
    return toString(level);
}

const char* Logger::categoryToString(Category category)
{
    return toString(category);
}

bool Logger::hasErrors() const
{
    return loggedError;
}

bool Logger::hasFatal() const
{
    return loggedFatal;
}

bool Logger::hasErrorsOrFatal() const
{
    return loggedError || loggedFatal;
}

const std::string& Logger::getLogFilename() const
{
    return filename;
}

void Logger::flush()
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open())
    {
        logFile.flush();
    }
}

// ── Crash handler ────────────────────────────────────────────────────────
#if defined(_WIN32)
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

static LONG WINAPI EngineCrashHandler(EXCEPTION_POINTERS* ep)
{
    auto& logger = Logger::Instance();

    const DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
    const void* addr  = ep ? ep->ExceptionRecord->ExceptionAddress : nullptr;

    const char* desc = "Unknown";
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:       desc = "Access Violation"; break;
    case EXCEPTION_STACK_OVERFLOW:         desc = "Stack Overflow"; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:     desc = "Integer Divide By Zero"; break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:     desc = "Float Divide By Zero"; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:    desc = "Illegal Instruction"; break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:  desc = "Array Bounds Exceeded"; break;
    case EXCEPTION_IN_PAGE_ERROR:          desc = "In Page Error"; break;
    default: break;
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "FATAL CRASH — %s (code 0x%08lX) at address %p",
        desc, static_cast<unsigned long>(code), addr);

    logger.log(Logger::Category::Engine, buf, Logger::LogLevel::FATAL);

    // Walk the call stack for extra context
    if (ep)
    {
        HANDLE process = GetCurrentProcess();
        HANDLE thread  = GetCurrentThread();
        SymInitialize(process, nullptr, TRUE);

        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame{};
        DWORD machineType = 0;
#if defined(_M_X64)
        machineType = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset    = ctx.Rip;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_IX86)
        machineType = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset    = ctx.Eip;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrStack.Offset = ctx.Esp;
#endif
        frame.AddrPC.Mode    = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;

        logger.log(Logger::Category::Engine, "--- Stack Trace ---", Logger::LogLevel::FATAL);

        constexpr int kMaxFrames = 32;
        for (int i = 0; i < kMaxFrames; ++i)
        {
            if (!StackWalk64(machineType, process, thread, &frame, &ctx,
                nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
                break;
            if (frame.AddrPC.Offset == 0)
                break;

            char symbolBuf[sizeof(SYMBOL_INFO) + 256];
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuf);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen   = 255;

            char frameBuf[512];
            if (SymFromAddr(process, frame.AddrPC.Offset, nullptr, symbol))
            {
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                DWORD displacement = 0;
                if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &displacement, &line))
                {
                    std::snprintf(frameBuf, sizeof(frameBuf), "  [%d] %s (%s:%lu)",
                        i, symbol->Name, line.FileName, line.LineNumber);
                }
                else
                {
                    std::snprintf(frameBuf, sizeof(frameBuf), "  [%d] %s (0x%llX)",
                        i, symbol->Name, static_cast<unsigned long long>(frame.AddrPC.Offset));
                }
            }
            else
            {
                std::snprintf(frameBuf, sizeof(frameBuf), "  [%d] 0x%llX",
                    i, static_cast<unsigned long long>(frame.AddrPC.Offset));
            }
            logger.log(Logger::Category::Engine, frameBuf, Logger::LogLevel::FATAL);
        }

        SymCleanup(process);
    }

    logger.flush();
    return EXCEPTION_CONTINUE_SEARCH;
}

static void EngineCppTerminateHandler()
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Engine,
        "FATAL CRASH — std::terminate() called (unhandled C++ exception)",
        Logger::LogLevel::FATAL);
    logger.flush();
    std::abort();
}

void Logger::installCrashHandler()
{
    SetUnhandledExceptionFilter(EngineCrashHandler);
    std::set_terminate(EngineCppTerminateHandler);
    Instance().log(Category::Engine, "Crash handlers installed (SEH + std::terminate)", LogLevel::INFO);
}

#else // non-Windows
#include <csignal>
#include <cstdlib>

static void EngineSignalHandler(int sig)
{
    const char* name = "Unknown";
    switch (sig)
    {
    case SIGSEGV: name = "SIGSEGV (Segmentation fault)"; break;
    case SIGABRT: name = "SIGABRT (Abort)"; break;
    case SIGFPE:  name = "SIGFPE (Floating point exception)"; break;
    case SIGILL:  name = "SIGILL (Illegal instruction)"; break;
    default: break;
    }

    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Engine,
        std::string("FATAL CRASH — signal ") + name,
        Logger::LogLevel::FATAL);
    logger.flush();
    std::_Exit(128 + sig);
}

static void EngineCppTerminateHandler()
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Engine,
        "FATAL CRASH — std::terminate() called (unhandled C++ exception)",
        Logger::LogLevel::FATAL);
    logger.flush();
    std::abort();
}

void Logger::installCrashHandler()
{
    std::signal(SIGSEGV, EngineSignalHandler);
    std::signal(SIGABRT, EngineSignalHandler);
    std::signal(SIGFPE,  EngineSignalHandler);
    std::signal(SIGILL,  EngineSignalHandler);
    std::set_terminate(EngineCppTerminateHandler);
    Instance().log(Category::Engine, "Crash handlers installed (signal + std::terminate)", LogLevel::INFO);
}
#endif
