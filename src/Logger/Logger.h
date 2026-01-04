#pragma once

#include <string>
#include <iostream>
#include <fstream>

#ifdef ERROR
#undef ERROR
#endif
#ifdef WARNING
#undef WARNING
#endif

class Logger
{
public:
    enum class LogLevel {
        INFO,
        WARNING,
        ERROR
    };

    static Logger& Instance();
    void initialize();
    void log(const std::string& message, LogLevel level = LogLevel::INFO);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream logFile;
    bool initialized{false};
    std::string filename;
};