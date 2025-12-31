#include "Logger.h"

Logger::Logger(const std::string& filename)
{
    logFile.open(filename, std::ios::out | std::ios::trunc);
    if (!logFile.is_open()) {
        std::cerr << "Fehler beim Öffnen der Log-Datei: " << filename << std::endl;
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
    if (logFile.is_open()) {
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
        logFile << "[" << levelStr << "] " << message << std::endl;
        std::cout << "[" << levelStr << "] " << message << std::endl;
    }
}
