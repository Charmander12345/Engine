#include "Logger.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
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
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    tm = *std::localtime(&t);
#endif

    std::ostringstream oss;
    oss << "log_" << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << ".log";
    filename = (logDir / oss.str()).string();

    logFile.open(filename, std::ios::out | std::ios::trunc);
    if (!logFile.is_open())
    {
        std::cerr << "Fehler beim Öffnen der Log-Datei: " << filename << std::endl;
    }
    else
    {
        initialized = true;
    }
}

Logger::~Logger()
{
    if (logFile.is_open()) {
        logFile.close();
    }
}

void Logger::log(const std::string& message, LogLevel level)
{
    std::string levelStr;
    switch (level) {
        case LogLevel::INFO:
            levelStr = "INFO";
            break;
        case LogLevel::WARNING:
            levelStr = "WARNING";
            break;
        case LogLevel::ERROR:
            levelStr = "ERROR";
            break;
    }

    if (logFile.is_open()) {
        logFile << "[" << levelStr << "] " << message << std::endl;
    }
    std::cout << "[" << levelStr << "] " << message << std::endl;
}
