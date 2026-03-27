#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <mutex>
#include <deque>
#include <functional>
#include <vector>
#include <cstdint>
#include <atomic>
#include <thread>

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

	// ── CrashHandler pipe integration ───────────────────────────────────
	/// Launch the out-of-process CrashHandler and establish the pipe.
	/// Called once during startup (after initialize()).
	void startCrashHandler();

	/// Send arbitrary data to the CrashHandler via the pipe.
	void sendToCrashHandler(const std::string& tag, const std::string& data);

	/// Check if the CrashHandler process is still alive; restart if not.
	void ensureCrashHandlerAlive();

	/// Send a graceful QUIT message and close the pipe.  Called on clean shutdown.
	void stopCrashHandler();

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

	// ── CrashHandler pipe state ─────────────────────────────────────────
	void launchCrashHandlerProcess();
	void connectPipe();
	void writePipe(const void* data, size_t size);

#if defined(_WIN32)
	void* m_crashHandlerProcess{ nullptr };
	void* m_pipe{ reinterpret_cast<void*>(static_cast<intptr_t>(-1)) }; // INVALID_HANDLE_VALUE
#else
	int    m_crashHandlerPid{ 0 };
	int    m_pipeFd{ -1 };
#endif
	std::mutex m_pipeMutex;
	bool m_crashHandlerRunning{ false };
};