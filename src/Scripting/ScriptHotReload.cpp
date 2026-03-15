#include "ScriptHotReload.h"
#include "../Logger/Logger.h"

bool ScriptHotReload::init(const std::string& contentDirectory, double pollIntervalSeconds)
{
    namespace fs = std::filesystem;

    if (!fs::exists(contentDirectory) || !fs::is_directory(contentDirectory))
    {
        Logger::Instance().log(Logger::Category::Engine,
            "ScriptHotReload: directory does not exist: " + contentDirectory,
            Logger::LogLevel::WARNING);
        return false;
    }

    m_directory = contentDirectory;
    m_pollIntervalSeconds = pollIntervalSeconds;
    m_files.clear();
    m_changedFiles.clear();

    // Baseline scan – record current timestamps without reporting changes.
    scanDirectory();

    m_lastPollTime = std::chrono::steady_clock::now();
    m_initialized = true;

    Logger::Instance().log(Logger::Category::Engine,
        "ScriptHotReload: watching " + std::to_string(m_files.size()) +
        " script files in " + contentDirectory,
        Logger::LogLevel::INFO);
    return true;
}

std::vector<std::string> ScriptHotReload::poll()
{
    m_changedFiles.clear();

    if (!m_initialized)
        return m_changedFiles;

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - m_lastPollTime).count();
    if (elapsed < m_pollIntervalSeconds)
        return m_changedFiles;

    m_lastPollTime = now;

    namespace fs = std::filesystem;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(m_directory, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;

        const auto& path = entry.path();
        if (path.extension() != ".py")
            continue;

        const std::string pathStr = path.string();
        const auto writeTime = entry.last_write_time(ec);
        if (ec)
            continue;

        auto it = m_files.find(pathStr);
        if (it == m_files.end())
        {
            // New file appeared
            m_files[pathStr] = FileEntry{ writeTime };
            m_changedFiles.push_back(pathStr);
        }
        else if (it->second.lastWriteTime != writeTime)
        {
            // Existing file was modified
            it->second.lastWriteTime = writeTime;
            m_changedFiles.push_back(pathStr);
        }
    }

    return m_changedFiles;
}

void ScriptHotReload::scanDirectory()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(m_directory, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;

        const auto& path = entry.path();
        if (path.extension() != ".py")
            continue;

        const std::string pathStr = path.string();
        const auto writeTime = entry.last_write_time(ec);
        if (ec)
            continue;

        m_files[pathStr] = FileEntry{ writeTime };
    }
}
