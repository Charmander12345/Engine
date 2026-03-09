#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <chrono>

/// @brief Watches a directory of shader files for modifications and reports changes.
///
/// Uses std::filesystem::last_write_time to detect edits.  Polls are throttled
/// so at most one scan happens per configurable interval (default 500 ms).
class ShaderHotReload
{
public:
    ShaderHotReload() = default;

    /// Initialise the watcher for the given directory.
    /// Scans once immediately to capture the baseline timestamps.
    bool init(const std::string& directory, double pollIntervalSeconds = 0.5);

    /// Check for changed files.  Returns the list of absolute paths whose
    /// write-time has changed since the last successful poll.  The list is
    /// empty when no changes are detected or when the poll interval has not
    /// yet elapsed.
    std::vector<std::string> poll();

    /// Returns the watched directory.
    const std::string& directory() const { return m_directory; }

private:
    void scanDirectory();

    std::string m_directory;
    double m_pollIntervalSeconds{0.5};
    std::chrono::steady_clock::time_point m_lastPollTime{};
    bool m_initialized{false};

    struct FileEntry
    {
        std::filesystem::file_time_type lastWriteTime{};
    };
    std::unordered_map<std::string, FileEntry> m_files;
    std::vector<std::string> m_changedFiles;
};
