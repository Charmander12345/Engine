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
	Logger(const std::string& filename);
	~Logger();
	void log(const std::string& message, LogLevel level = LogLevel::INFO);

private:
	std::ofstream logFile;
};