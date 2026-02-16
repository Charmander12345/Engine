#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <mutex>

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
        ERROR,
        FATAL
    };

    enum class Category {
        General,
        Engine,
        Scripting,
        AssetManagement,
        Diagnostics,
        Rendering,
        Input,
        Project,
        IO,
        UI
    };

    static Logger& Instance();
    void initialize();

	void setMinimumLogLevel(LogLevel level);
	void setSuppressStdout(bool suppress);

	// Log with default category (General)
	void log(const std::string& message, LogLevel level = LogLevel::INFO);
	// Log with specified category
	void log(Category category, const std::string& message, LogLevel level = LogLevel::INFO);

    bool hasErrors() const;
    bool hasFatal() const;
    bool hasErrorsOrFatal() const;
    const std::string& getLogFilename() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static const char* toString(LogLevel level);
    static const char* toString(Category category);

	std::ofstream logFile;
	bool initialized{false};
	std::string filename;
	bool loggedError{ false };
	bool loggedFatal{ false };
	bool suppressStdout{ false };
	std::mutex logMutex;

	LogLevel minimumLevel{ LogLevel::INFO };
};