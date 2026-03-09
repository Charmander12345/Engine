#include "ShaderHotReload.h"
#include "Logger.h"

bool ShaderHotReload::init(const std::string& directory, double pollIntervalSeconds)
{
    namespace fs = std::filesystem;

    if (!fs::exists(directory) || !fs::is_directory(directory))
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "ShaderHotReload: directory does not exist: " + directory,
            Logger::LogLevel::WARNING);
        return false;
    }

    m_directory = directory;
    m_pollIntervalSeconds = pollIntervalSeconds;
    m_files.clear();
    m_changedFiles.clear();

    // Baseline scan – record current timestamps without reporting changes.
    scanDirectory();

    m_lastPollTime = std::chrono::steady_clock::now();
    m_initialized = true;

    Logger::Instance().log(Logger::Category::Rendering,
        "ShaderHotReload: watching " + std::to_string(m_files.size()) +
        " shader files in " + directory,
        Logger::LogLevel::INFO);
    return true;
}

std::vector<std::string> ShaderHotReload::poll()
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
    for (const auto& entry : fs::directory_iterator(m_directory, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;

        const auto& path = entry.path();
        const auto ext = path.extension().string();
        if (ext != ".glsl" && ext != ".vert" && ext != ".frag" && ext != ".geom" && ext != ".comp")
            continue;

        const std::string pathStr = path.string();
        const auto writeTime = entry.last_write_time(ec);
        if (ec)
            continue;

        auto it = m_files.find(pathStr);
        if (it == m_files.end())
        {
            // New file appeared
            m_files[pathStr] = FileEntry{writeTime};
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

void ShaderHotReload::scanDirectory()
{
    namespace fs = std::filesystem;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(m_directory, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;

        const auto& path = entry.path();
        const auto ext = path.extension().string();
        if (ext != ".glsl" && ext != ".vert" && ext != ".frag" && ext != ".geom" && ext != ".comp")
            continue;

        const std::string pathStr = path.string();
        const auto writeTime = entry.last_write_time(ec);
        if (ec)
            continue;

        m_files[pathStr] = FileEntry{writeTime};
    }
}
