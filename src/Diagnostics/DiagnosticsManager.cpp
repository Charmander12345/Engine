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
    m_rhiType = RHIType::Unknown;
}

void DiagnosticsManager::setRHIType(RHIType type)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rhiType = type;
}

DiagnosticsManager::RHIType DiagnosticsManager::getRHIType() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rhiType;
}

std::string DiagnosticsManager::rhiTypeToString(RHIType type)
{
    switch (type)
    {
    case RHIType::OpenGL: return "OpenGL";
    case RHIType::DirectX11: return "DirectX11";
    case RHIType::DirectX12: return "DirectX12";
    default: return "Unknown";
    }
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

    // persist RHI type
    out << "RHI=" << rhiTypeToString(m_rhiType) << "\n";

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
    m_rhiType = RHIType::Unknown;
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
        if (key == "RHI")
        {
            if (val == "OpenGL") m_rhiType = RHIType::OpenGL;
            else if (val == "DirectX11") m_rhiType = RHIType::DirectX11;
            else if (val == "DirectX12") m_rhiType = RHIType::DirectX12;
            else m_rhiType = RHIType::Unknown;
            continue;
        }
        m_states[key] = val;
    }
    return true;
}

bool DiagnosticsManager::isProjectLoaded() const
{
    return projectLoaded;
}

const DiagnosticsManager::ProjectInfo& DiagnosticsManager::getProjectInfo() const
{
	return m_projectInfo;
}

void DiagnosticsManager::setProjectInfo(const ProjectInfo& info)
{
    m_projectInfo = info;
	projectLoaded = true;
}

void DiagnosticsManager::clearProjectInfo()
{
    m_projectInfo = { "", "", "", RHIType::Unknown };
	projectLoaded = false;
}

bool DiagnosticsManager::isActionInProgress() const
{
    return isLoadingAsset || isLoadingProject || isSavingAsset || isSavingProject || isBuildingProject;
}

void DiagnosticsManager::setActionInProgress(ActionType action, bool inProgress)
{
    switch (action)
    {
    case ActionType::LoadingAsset:
        isLoadingAsset = inProgress;
        break;
    case ActionType::SavingAsset:
        isSavingAsset = inProgress;
        break;
    case ActionType::BuildingProject:
        isBuildingProject = inProgress;
        break;
    case ActionType::LoadingProject:
        isLoadingProject = inProgress;
        break;
    case ActionType::SavingProject:
        isSavingProject = inProgress;
        break;
    default:
        break;
	}
}

DiagnosticsManager::DiagnosticsManager()
{
	m_projectInfo = { "", "", "", RHIType::Unknown };
}
