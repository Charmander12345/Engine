// ── CrashHandler – Out-of-Process Crash Reporter ────────────────────────
// Started by the engine at boot.  Listens on a named pipe for continuous
// state/log/heartbeat messages.  When the pipe disconnects unexpectedly
// (i.e. without a QUIT message), the handler assumes the engine crashed
// and shows a detailed crash report built from the accumulated data.
//
// Usage:  CrashHandler.exe <engine_pid>
//   <engine_pid>   PID of the engine process (for display / WaitForSingleObject)

#include "CrashProtocol.h"

#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cstring>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
#  include <shellapi.h>
#else
#  include <iostream>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#  include <csignal>
#endif

// ── Accumulated crash-report data ───────────────────────────────────────

static constexpr size_t kMaxLogLines = 200;

struct CrashReport
{
    // Identity
    std::string engineVersion;
    std::string commandLine;
    std::string workingDir;
    std::string enginePid;
    std::string uptimeSeconds;

    // Hardware
    std::string hardwareInfo;

    // Project
    std::string projectName;
    std::string projectVersion;
    std::string projectPath;
    std::string activeLevel;

    // Engine state (key=value pairs)
    std::string engineState;

    // Frame metrics (latest)
    std::string frameInfo;

    // Loaded modules
    std::string modules;

    // Log tail (ring buffer)
    std::deque<std::string> logEntries;

    // Crash info (set by CRASH tag or inferred from disconnect)
    bool        crashReceived{ false };
    std::string crashDescription;
    std::string stackTrace;

    // Graceful shutdown?
    bool gracefulQuit{ false };

    void addLog(const std::string& entry)
    {
        logEntries.push_back(entry);
        while (logEntries.size() > kMaxLogLines)
            logEntries.pop_front();
    }
};

static CrashReport g_report;

// ── Message processing ──────────────────────────────────────────────────

static void processMessage(const std::string& payload)
{
    std::string tag, data;
    if (!CrashProtocol::parsePayload(payload, tag, data))
        return;

    if (tag == CrashProtocol::Tag::Heartbeat)
    {
        // Nothing to store – the fact that we're receiving is enough
    }
    else if (tag == CrashProtocol::Tag::LogEntry)
    {
        g_report.addLog(data);
    }
    else if (tag == CrashProtocol::Tag::EngineState)
    {
        g_report.engineState = data;
    }
    else if (tag == CrashProtocol::Tag::Hardware)
    {
        g_report.hardwareInfo = data;
    }
    else if (tag == CrashProtocol::Tag::Project)
    {
        // "name|version|path|level"
        std::istringstream ss(data);
        std::string tok;
        if (std::getline(ss, tok, '|')) g_report.projectName    = tok;
        if (std::getline(ss, tok, '|')) g_report.projectVersion = tok;
        if (std::getline(ss, tok, '|')) g_report.projectPath    = tok;
        if (std::getline(ss, tok, '|')) g_report.activeLevel    = tok;
    }
    else if (tag == CrashProtocol::Tag::FrameInfo)
    {
        g_report.frameInfo = data;
    }
    else if (tag == CrashProtocol::Tag::Crash)
    {
        g_report.crashReceived = true;
        auto sep = data.find('|');
        if (sep != std::string::npos)
        {
            g_report.crashDescription = data.substr(0, sep);
            g_report.stackTrace       = data.substr(sep + 1);
        }
        else
        {
            g_report.crashDescription = data;
        }
    }
    else if (tag == CrashProtocol::Tag::Shutdown)
    {
        g_report.gracefulQuit = true;
    }
    else if (tag == CrashProtocol::Tag::Modules)
    {
        g_report.modules = data;
    }
    else if (tag == CrashProtocol::Tag::Uptime)
    {
        g_report.uptimeSeconds = data;
    }
    else if (tag == CrashProtocol::Tag::Commandline)
    {
        g_report.commandLine = data;
    }
    else if (tag == CrashProtocol::Tag::WorkingDir)
    {
        g_report.workingDir = data;
    }
    else if (tag == CrashProtocol::Tag::EngineVer)
    {
        g_report.engineVersion = data;
    }
}

// ── Report generation ───────────────────────────────────────────────────

static std::string buildReportText()
{
    std::ostringstream r;
    r << "=== HorizonEngine Crash Report ===\n\n";

    // Crash description
    if (g_report.crashReceived)
        r << "Error: " << g_report.crashDescription << "\n\n";
    else
        r << "Error: Engine process terminated unexpectedly\n\n";

    // Stack trace
    if (!g_report.stackTrace.empty())
    {
        r << "--- Stack Trace ---\n" << g_report.stackTrace << "\n\n";
    }

    // Engine info
    r << "--- Engine Info ---\n";
    if (!g_report.engineVersion.empty()) r << "Version:     " << g_report.engineVersion << "\n";
    if (!g_report.enginePid.empty())     r << "PID:         " << g_report.enginePid << "\n";
    if (!g_report.uptimeSeconds.empty()) r << "Uptime:      " << g_report.uptimeSeconds << " s\n";
    if (!g_report.commandLine.empty())   r << "Commandline: " << g_report.commandLine << "\n";
    if (!g_report.workingDir.empty())    r << "Working Dir: " << g_report.workingDir << "\n";
    r << "\n";

    // Project info
    if (!g_report.projectName.empty())
    {
        r << "--- Project ---\n";
        r << "Name:    " << g_report.projectName << "\n";
        if (!g_report.projectVersion.empty()) r << "Version: " << g_report.projectVersion << "\n";
        if (!g_report.projectPath.empty())    r << "Path:    " << g_report.projectPath << "\n";
        if (!g_report.activeLevel.empty())    r << "Level:   " << g_report.activeLevel << "\n";
        r << "\n";
    }

    // Hardware
    if (!g_report.hardwareInfo.empty())
    {
        r << "--- Hardware ---\n" << g_report.hardwareInfo << "\n\n";
    }

    // Frame metrics
    if (!g_report.frameInfo.empty())
    {
        r << "--- Last Frame Metrics ---\n" << g_report.frameInfo << "\n\n";
    }

    // Engine state
    if (!g_report.engineState.empty())
    {
        r << "--- Engine State ---\n" << g_report.engineState << "\n\n";
    }

    // Loaded modules
    if (!g_report.modules.empty())
    {
        r << "--- Loaded Modules ---\n" << g_report.modules << "\n\n";
    }

    // Log tail
    if (!g_report.logEntries.empty())
    {
        r << "--- Last " << g_report.logEntries.size() << " Log Entries ---\n";
        for (const auto& entry : g_report.logEntries)
            r << entry << "\n";
        r << "\n";
    }

    return r.str();
}

static void saveReportFile(const std::string& report)
{
    auto dir = std::filesystem::current_path() / "CrashReports";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    tm = *std::localtime(&t);
#endif

    std::ostringstream fname;
    fname << "crash_"
          << (tm.tm_year + 1900) << "-"
          << (tm.tm_mon + 1) << "-"
          << tm.tm_mday << "_"
          << tm.tm_hour << "-"
          << tm.tm_min << "-"
          << tm.tm_sec << ".txt";

    auto path = dir / fname.str();
    std::ofstream f(path);
    if (f.is_open())
        f << report;
}

// ═══════════════════════════════════════════════════════════════════════
// Platform: Windows
// ═══════════════════════════════════════════════════════════════════════

#if defined(_WIN32)

static bool readExact(HANDLE pipe, void* buf, DWORD count)
{
    DWORD total = 0;
    while (total < count)
    {
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, static_cast<char*>(buf) + total, count - total, &bytesRead, nullptr))
            return false;
        if (bytesRead == 0)
            return false;
        total += bytesRead;
    }
    return true;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return 1;

    auto wideToUtf8 = [](const wchar_t* w) -> std::string {
        if (!w || !*w) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string s(static_cast<size_t>(len) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
        return s;
    };

    g_report.enginePid = (argc > 1) ? wideToUtf8(argv[1]) : "?";
    LocalFree(argv);

    // Build PID-specific pipe name so multiple instances can coexist
    std::string pipePath = CrashProtocol::pipeName(
        static_cast<uint32_t>(std::stoul(g_report.enginePid)));

    // Create named pipe (server side – engine connects as client)
    HANDLE hPipe = CreateNamedPipeA(
        pipePath.c_str(),
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,          // max instances
        0,          // out buffer
        64 * 1024,  // in buffer
        0,          // default timeout
        nullptr);

    if (hPipe == INVALID_HANDLE_VALUE)
        return 1;

    // Wait for engine to connect
    if (!ConnectNamedPipe(hPipe, nullptr))
    {
        if (GetLastError() != ERROR_PIPE_CONNECTED)
        {
            CloseHandle(hPipe);
            return 1;
        }
    }

    // Read messages until pipe breaks
    while (true)
    {
        uint32_t len = 0;
        if (!readExact(hPipe, &len, 4))
            break;

        if (len == 0 || len > 4 * 1024 * 1024) // sanity: max 4 MB
            break;

        std::string payload(len, '\0');
        if (!readExact(hPipe, payload.data(), len))
            break;

        processMessage(payload);

        if (g_report.gracefulQuit)
            break;
    }

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);

    // Only show a report dialog if this was NOT a graceful quit
    if (!g_report.gracefulQuit)
    {
        std::string report = buildReportText();
        saveReportFile(report);

        // Truncate for MessageBox if very long (max ~16 KB for display)
        std::string display = report;
        if (display.size() > 16000)
        {
            display.resize(16000);
            display += "\n\n... (truncated – see full report in CrashReports/ folder)";
        }

        MessageBoxA(nullptr, display.c_str(),
                    "HorizonEngine \u2013 Crash Report",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
    }

    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
// Platform: Linux / macOS
// ═══════════════════════════════════════════════════════════════════════

#else

static bool readExact(int fd, void* buf, size_t count)
{
    size_t total = 0;
    while (total < count)
    {
        ssize_t n = read(fd, static_cast<char*>(buf) + total, count - total);
        if (n <= 0)
            return false;
        total += static_cast<size_t>(n);
    }
    return true;
}

int main(int argc, char* argv[])
{
    g_report.enginePid = (argc > 1) ? argv[1] : "?";

    // Build PID-specific socket path so multiple instances can coexist
    std::string pipePath = CrashProtocol::pipeName(
        static_cast<uint32_t>(std::stoul(g_report.enginePid)));

    // Create Unix domain socket
    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0)
        return 1;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, pipePath.c_str(), sizeof(addr.sun_path) - 1);

    unlink(pipePath.c_str());
    if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        close(serverFd);
        return 1;
    }
    listen(serverFd, 1);

    int clientFd = accept(serverFd, nullptr, nullptr);
    if (clientFd < 0)
    {
        close(serverFd);
        unlink(pipePath.c_str());
        return 1;
    }

    // Read messages
    while (true)
    {
        uint32_t len = 0;
        if (!readExact(clientFd, &len, 4))
            break;
        if (len == 0 || len > 4 * 1024 * 1024)
            break;

        std::string payload(len, '\0');
        if (!readExact(clientFd, payload.data(), len))
            break;

        processMessage(payload);

        if (g_report.gracefulQuit)
            break;
    }

    close(clientFd);
    close(serverFd);
    unlink(pipePath.c_str());

    if (!g_report.gracefulQuit)
    {
        std::string report = buildReportText();
        saveReportFile(report);
        std::cerr << report;
    }

    return 0;
}

#endif