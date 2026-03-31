#include "Logger.h"
#include "CrashProtocol.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <string_view>
#include <algorithm>
#include <vector>
#include <deque>
#include <cstdio>
#include <csignal>

#if !defined(_WIN32)
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#  include <sys/wait.h>
#  include <signal.h>
#endif

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

        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buf);
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

void Logger::setLogDirectory(const std::string& dir)
{
    m_customLogDir = dir;
}

void Logger::setToolsDirectory(const std::string& dir)
{
    m_customToolsDir = dir;
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

    std::filesystem::path logDir;
    if (!m_customLogDir.empty())
        logDir = m_customLogDir;
    else
        logDir = std::filesystem::current_path() / "Logs";
    std::error_code ec;
    std::filesystem::create_directories(logDir, ec);

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    const std::tm tm = localTime(t);

    char nameBuf[64];
    std::strftime(nameBuf, sizeof(nameBuf), "log_%Y-%m-%d_%H-%M-%S.log", &tm);
    filename = (logDir / nameBuf).string();

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
    stopCrashHandler();
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

    // Forward to CrashHandler pipe (non-blocking best-effort)
    if (m_crashHandlerRunning)
    {
        char logBuf[1024];
        const int logLen = std::snprintf(logBuf, sizeof(logBuf), "%s|%s|[%s] %s",
            levelStr, catStr, ts.c_str(), message.c_str());
        if (logLen > 0)
        {
            const std::string_view logData(logBuf, static_cast<size_t>(std::min(logLen, static_cast<int>(sizeof(logBuf) - 1))));
            std::string msg = CrashProtocol::buildMessage(CrashProtocol::Tag::LogEntry, std::string(logData));
            writePipe(msg.data(), msg.size());
        }
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

// Collect stack trace into a string (used by SEH / signal handlers).
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
#  ifdef ERROR
#    undef ERROR
#  endif
#  ifdef WARNING
#    undef WARNING
#  endif
#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

static std::string captureStackTrace(EXCEPTION_POINTERS* ep)
{
    if (!ep) return {};

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

    std::string result;
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
        result += frameBuf;
        result += "\n";
    }

    SymCleanup(process);
    return result;
}

static void sendCrashToPipe(const char* description, const std::string& stackTrace = {})
{
    auto& logger = Logger::Instance();
    std::string data = std::string(description ? description : "Unknown") + "|" + stackTrace;
    logger.sendToCrashHandler(CrashProtocol::Tag::Crash, data);
}

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
        "FATAL CRASH \u2014 %s (code 0x%08lX) at address %p",
        desc, static_cast<unsigned long>(code), addr);

    // Capture stack trace before logging (logging may fail in a crash)
    std::string stackTrace = captureStackTrace(ep);

    logger.log(Logger::Category::Engine, buf, Logger::LogLevel::FATAL);
    if (!stackTrace.empty())
        logger.log(Logger::Category::Engine, "--- Stack Trace ---\n" + stackTrace, Logger::LogLevel::FATAL);
    logger.flush();

    // Send CRASH message to CrashHandler via pipe
    sendCrashToPipe(buf, stackTrace);

    return EXCEPTION_CONTINUE_SEARCH;
}

static void EngineCppTerminateHandler()
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Engine,
        "FATAL CRASH \u2014 std::terminate() called (unhandled C++ exception)",
        Logger::LogLevel::FATAL);
    logger.flush();
    sendCrashToPipe("std::terminate() called (unhandled C++ exception)");
    std::abort();
}

void Logger::installCrashHandler()
{
    SetUnhandledExceptionFilter(EngineCrashHandler);
    std::set_terminate(EngineCppTerminateHandler);
    Instance().log(Category::Engine, "Crash handlers installed (SEH + std::terminate)", LogLevel::INFO);
}

// ── CrashHandler pipe integration (Windows) ─────────────────────────────

void Logger::launchCrashHandlerProcess()
{
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::path crashExe;
    if (!m_customToolsDir.empty())
        crashExe = std::filesystem::path(m_customToolsDir) / "CrashHandler.exe";
    else
        crashExe = std::filesystem::path(exePath).parent_path() / "Tools" / "CrashHandler.exe";
    if (!std::filesystem::exists(crashExe))
    {
        log(Category::Engine, "CrashHandler.exe not found at: " + crashExe.string(), LogLevel::WARNING);
        return;
    }

    DWORD pid = GetCurrentProcessId();
    std::string cmdLine = "\"" + crashExe.string() + "\" " + std::to_string(pid);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
    {
        log(Category::Engine, "Failed to launch CrashHandler process", LogLevel::WARNING);
        return;
    }

    m_crashHandlerProcess = pi.hProcess;
    if (pi.hThread) CloseHandle(pi.hThread);
}

void Logger::connectPipe()
{
    // Try connecting to the CrashHandler's named pipe server with retries
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        m_pipe = CreateFileA(
            CrashProtocol::pipeName(GetCurrentProcessId()).c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            m_crashHandlerRunning = true;
            return;
        }

        Sleep(100); // Wait for CrashHandler to create the pipe
    }

    log(Category::Engine, "Failed to connect to CrashHandler pipe after retries", LogLevel::WARNING);
}

void Logger::writePipe(const void* data, size_t size)
{
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    if (m_pipe == INVALID_HANDLE_VALUE)
        return;

    DWORD written = 0;
    if (!WriteFile(m_pipe, data, static_cast<DWORD>(size), &written, nullptr))
    {
        // Pipe broken – CrashHandler may have died
        m_crashHandlerRunning = false;
    }
}

void Logger::startCrashHandler()
{
    launchCrashHandlerProcess();
    if (!m_crashHandlerProcess)
        return;

    connectPipe();

    if (m_crashHandlerRunning)
    {
        // Send initial info: working directory, PID, command line
        sendToCrashHandler(CrashProtocol::Tag::WorkingDir, std::filesystem::current_path().string());

        char cmdBuf[32768];
        GetModuleFileNameA(nullptr, cmdBuf, sizeof(cmdBuf));
        sendToCrashHandler(CrashProtocol::Tag::Commandline, cmdBuf);

        log(Category::Engine, "CrashHandler connected via pipe", LogLevel::INFO);
    }
}

void Logger::ensureCrashHandlerAlive()
{
    if (!m_crashHandlerProcess)
        return;

    DWORD exitCode = 0;
    if (GetExitCodeProcess(m_crashHandlerProcess, &exitCode) && exitCode != STILL_ACTIVE)
    {
        log(Category::Engine, "CrashHandler process died – restarting...", LogLevel::WARNING);
        CloseHandle(m_crashHandlerProcess);
        m_crashHandlerProcess = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_pipeMutex);
            if (m_pipe != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_pipe);
                m_pipe = INVALID_HANDLE_VALUE;
            }
        }
        m_crashHandlerRunning = false;

        launchCrashHandlerProcess();
        if (m_crashHandlerProcess)
        {
            connectPipe();
            if (m_crashHandlerRunning)
            {
                sendToCrashHandler(CrashProtocol::Tag::WorkingDir, std::filesystem::current_path().string());
                log(Category::Engine, "CrashHandler restarted successfully", LogLevel::INFO);
            }
        }
    }
}

void Logger::sendToCrashHandler(const std::string& tag, const std::string& data)
{
    if (!m_crashHandlerRunning)
        return;
    std::string msg = CrashProtocol::buildMessage(tag.c_str(), data);
    writePipe(msg.data(), msg.size());
}

void Logger::stopCrashHandler()
{
    if (!m_crashHandlerRunning)
        return;

    sendToCrashHandler(CrashProtocol::Tag::Shutdown, "");
    m_crashHandlerRunning = false;

    {
        std::lock_guard<std::mutex> lock(m_pipeMutex);
        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            FlushFileBuffers(m_pipe);
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

    // Give CrashHandler time to exit gracefully, then close handle
    if (m_crashHandlerProcess)
    {
        WaitForSingleObject(m_crashHandlerProcess, 2000);
        CloseHandle(m_crashHandlerProcess);
        m_crashHandlerProcess = nullptr;
    }
}

#else // non-Windows ─────────────────────────────────────────────────────

static void sendCrashToPipe(const char* description, const std::string& stackTrace = {})
{
    auto& logger = Logger::Instance();
    std::string data = std::string(description ? description : "Unknown") + "|" + stackTrace;
    logger.sendToCrashHandler(CrashProtocol::Tag::Crash, data);
}

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
        std::string("FATAL CRASH \u2014 signal ") + name,
        Logger::LogLevel::FATAL);
    logger.flush();
    sendCrashToPipe(name);
    std::_Exit(128 + sig);
}

static void EngineCppTerminateHandler()
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Engine,
        "FATAL CRASH \u2014 std::terminate() called (unhandled C++ exception)",
        Logger::LogLevel::FATAL);
    logger.flush();
    sendCrashToPipe("std::terminate() called (unhandled C++ exception)");
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

// ── CrashHandler pipe integration (Linux/macOS) ────────────────────────

void Logger::launchCrashHandlerProcess()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return;
    fs::path crashExe;
    if (!m_customToolsDir.empty())
        crashExe = fs::path(m_customToolsDir) / "CrashHandler";
    else
        crashExe = self.parent_path() / "Tools" / "CrashHandler";
    if (!fs::exists(crashExe))
    {
        log(Category::Engine, "CrashHandler not found at: " + crashExe.string(), LogLevel::WARNING);
        return;
    }

    pid_t pid = fork();
    if (pid == 0)
    {
        // Child – exec CrashHandler
        std::string pidStr = std::to_string(getppid());
        execl(crashExe.c_str(), crashExe.c_str(), pidStr.c_str(), nullptr);
        _exit(1);
    }
    else if (pid > 0)
    {
        m_crashHandlerPid = pid;
    }
}

void Logger::connectPipe()
{
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) { usleep(100000); continue; }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, CrashProtocol::kPipeName, sizeof(addr.sun_path) - 1);

        if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0)
        {
            m_pipeFd = fd;
            m_crashHandlerRunning = true;
            return;
        }
        close(fd);
        usleep(100000);
    }
    log(Category::Engine, "Failed to connect to CrashHandler socket after retries", LogLevel::WARNING);
}

void Logger::writePipe(const void* data, size_t size)
{
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    if (m_pipeFd < 0) return;

    ssize_t n = write(m_pipeFd, data, size);
    if (n < 0)
        m_crashHandlerRunning = false;
}

void Logger::startCrashHandler()
{
    launchCrashHandlerProcess();
    if (m_crashHandlerPid <= 0) return;

    connectPipe();
    if (m_crashHandlerRunning)
    {
        sendToCrashHandler(CrashProtocol::Tag::WorkingDir, std::filesystem::current_path().string());
        log(Category::Engine, "CrashHandler connected via socket", LogLevel::INFO);
    }
}

void Logger::ensureCrashHandlerAlive()
{
    if (m_crashHandlerPid <= 0) return;

    int status = 0;
    pid_t result = waitpid(m_crashHandlerPid, &status, WNOHANG);
    if (result == m_crashHandlerPid)
    {
        log(Category::Engine, "CrashHandler process died – restarting...", LogLevel::WARNING);
        {
            std::lock_guard<std::mutex> lock(m_pipeMutex);
            if (m_pipeFd >= 0) { close(m_pipeFd); m_pipeFd = -1; }
        }
        m_crashHandlerRunning = false;
        m_crashHandlerPid = 0;

        launchCrashHandlerProcess();
        if (m_crashHandlerPid > 0)
        {
            connectPipe();
            if (m_crashHandlerRunning)
            {
                sendToCrashHandler(CrashProtocol::Tag::WorkingDir, std::filesystem::current_path().string());
                log(Category::Engine, "CrashHandler restarted successfully", LogLevel::INFO);
            }
        }
    }
}

void Logger::sendToCrashHandler(const std::string& tag, const std::string& data)
{
    if (!m_crashHandlerRunning) return;
    std::string msg = CrashProtocol::buildMessage(tag.c_str(), data);
    writePipe(msg.data(), msg.size());
}

void Logger::stopCrashHandler()
{
    if (!m_crashHandlerRunning) return;

    sendToCrashHandler(CrashProtocol::Tag::Shutdown, "");
    m_crashHandlerRunning = false;

    {
        std::lock_guard<std::mutex> lock(m_pipeMutex);
        if (m_pipeFd >= 0) { close(m_pipeFd); m_pipeFd = -1; }
    }

    if (m_crashHandlerPid > 0)
    {
        int status = 0;
        waitpid(m_crashHandlerPid, &status, 0);
        m_crashHandlerPid = 0;
    }
}

#endif
