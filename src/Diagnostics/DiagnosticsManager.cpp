#include "DiagnosticsManager.h"

#include <fstream>
#include <sstream>

DiagnosticsManager& DiagnosticsManager::Instance()
{
    static DiagnosticsManager instance;
    return instance;
}

void DiagnosticsManager::setState(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states[key] = value;
}

std::optional<std::string> DiagnosticsManager::getState(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_states.find(key);
    if (it != m_states.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void DiagnosticsManager::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.clear();
}

std::filesystem::path DiagnosticsManager::getConfigPath() const
{
    std::filesystem::path base = std::filesystem::current_path();
    std::filesystem::path configDir = base / "config";
    return configDir / "config.ini";
}

bool DiagnosticsManager::saveConfig() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path cfgPath = getConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(cfgPath.parent_path(), ec);

    std::ofstream out(cfgPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    for (const auto& [k, v] : m_states)
    {
        out << k << "=" << v << "\n";
    }
    return true;
}

bool DiagnosticsManager::loadConfig()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path cfgPath = getConfigPath();
    std::ifstream in(cfgPath);
    if (!in.is_open())
    {
        return false;
    }

    m_states.clear();
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        m_states[key] = val;
    }
    return true;
}
