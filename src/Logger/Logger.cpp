#include "Logger.h"

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

void Logger::initialize(const std::string& filename)
{
    if (initialized)
    {
        return;
    }
    logFile.open(filename, std::ios::out | std::ios::trunc);
    if (!logFile.is_open()) {
        std::cerr << "Fehler beim Öffnen der Log-Datei: " << filename << std::endl;
    } else {
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
