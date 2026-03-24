#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <mutex>
#include <deque>
#include <functional>
#include <vector>
#include <cstdint>

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

	struct ConsoleEntry
	{
		std::string timestamp;
		LogLevel level{ LogLevel::INFO };
		Category category{ Category::General };
		std::string message;
		uint64_t sequenceId{ 0 };
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

	// Console ring-buffer access (thread-safe)
	static constexpr size_t kMaxConsoleEntries = 2000;
	const std::deque<ConsoleEntry>& getConsoleEntries() const { return m_consoleEntries; }
	uint64_t getLatestSequenceId() const { return m_nextSequenceId - 1; }
	void clearConsoleBuffer();

	/// Flush all buffered log data to disk immediately.
	void flush();

	/// Install OS-level crash handlers so the last logs survive a crash.
	static void installCrashHandler();

	static const char* levelToString(LogLevel level);
	static const char* categoryToString(Category category);

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

	// Console ring-buffer
	std::deque<ConsoleEntry> m_consoleEntries;
	uint64_t m_nextSequenceId{ 1 };
};