#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>

/// @brief Watches a Content directory tree for modified .py script files.
///
/// Uses std::filesystem::last_write_time with recursive iteration to detect
/// edits across all subfolders.  Polls are throttled so at most one scan
/// happens per configurable interval (default 500 ms).
class ScriptHotReload
{
public:
    ScriptHotReload() = default;

    /// Initialise the watcher for the given Content directory.
    /// Scans once immediately to capture baseline timestamps.
    bool init(const std::string& contentDirectory, double pollIntervalSeconds = 0.5);

    /// Check for changed .py files.  Returns the list of absolute paths whose
    /// write-time has changed since the last successful poll.  The list is
    /// empty when no changes are detected or when the poll interval has not
    /// yet elapsed.
    std::vector<std::string> poll();

    /// Returns the watched Content directory.
    const std::string& directory() const { return m_directory; }

    bool isInitialized() const { return m_initialized; }

private:
    void scanDirectory();

    std::string m_directory;
    double m_pollIntervalSeconds{ 0.5 };
    std::chrono::steady_clock::time_point m_lastPollTime{};
    bool m_initialized{ false };

    struct FileEntry
    {
        std::filesystem::file_time_type lastWriteTime{};
    };
    std::unordered_map<std::string, FileEntry> m_files;
    std::vector<std::string> m_changedFiles;
};
