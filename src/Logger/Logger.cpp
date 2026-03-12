#include "Logger.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <vector>
#include <deque>

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
    }

    if (!suppressStdout)
    {
        std::cout << "[" << ts << "][" << catStr << "][" << levelStr << "] " << message << '\n';
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
