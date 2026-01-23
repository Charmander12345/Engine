#include "DiagnosticsManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

DiagnosticsManager& DiagnosticsManager::Instance()
{
    static DiagnosticsManager instance;
    return instance;
}

static void trim(std::string& s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
}

bool DiagnosticsManager::readKeyValueFile(const std::filesystem::path& filePath, std::unordered_map<std::string, std::string>& out)
{
    std::ifstream in(filePath);
    if (!in.is_open())
    {
        return false;
    }

    out.clear();
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
        trim(key);
        trim(val);
        if (!key.empty())
        {
            out[key] = val;
        }
    }

    return true;
}

bool DiagnosticsManager::writeKeyValueFile(const std::filesystem::path& filePath, const std::unordered_map<std::string, std::string>& data)
{
    std::error_code ec;
    std::filesystem::create_directories(filePath.parent_path(), ec);

    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }

    for (const auto& [k, v] : data)
    {
        out << k << "=" << v << "\n";
    }

    return true;
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

void DiagnosticsManager::setProjectState(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectStates[key] = value;
}

std::optional<std::string> DiagnosticsManager::getProjectState(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_projectStates.find(key);
    if (it != m_projectStates.end())
    {
        return it->second;
    }
    return std::nullopt;
}

void DiagnosticsManager::clearProjectStates()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_projectStates.clear();
}

void DiagnosticsManager::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.clear();
    m_projectStates.clear();
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

std::filesystem::path DiagnosticsManager::getProjectConfigPath() const
{
    if (m_projectInfo.projectPath.empty())
    {
        return {};
    }

    std::filesystem::path root = std::filesystem::path(m_projectInfo.projectPath);
    return root / "Config" / "defaults.ini";
}

bool DiagnosticsManager::saveConfig() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // persist RHI in engine config too
    std::unordered_map<std::string, std::string> data = m_states;
    data["RHI"] = rhiTypeToString(m_rhiType);

    return writeKeyValueFile(getConfigPath(), data);
}

bool DiagnosticsManager::loadConfig()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::unordered_map<std::string, std::string> data;
    if (!readKeyValueFile(getConfigPath(), data))
    {
        return false;
    }

    m_states = data;

    auto it = m_states.find("RHI");
    if (it != m_states.end())
    {
        const auto& val = it->second;
        if (val == "OpenGL") m_rhiType = RHIType::OpenGL;
        else if (val == "DirectX11") m_rhiType = RHIType::DirectX11;
        else if (val == "DirectX12") m_rhiType = RHIType::DirectX12;
        else m_rhiType = RHIType::Unknown;
    }

    return true;
}

bool DiagnosticsManager::saveProjectConfig() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path cfg = getProjectConfigPath();
    if (cfg.empty())
    {
        return false;
    }
    return writeKeyValueFile(cfg, m_projectStates);
}

bool DiagnosticsManager::loadProjectConfig()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::filesystem::path cfg = getProjectConfigPath();
    if (cfg.empty())
    {
        return false;
    }

    // if missing, create an empty defaults.ini
    if (!std::filesystem::exists(cfg))
    {
        std::unordered_map<std::string, std::string> empty;
        if (!writeKeyValueFile(cfg, empty))
        {
            return false;
        }
    }

    return readKeyValueFile(cfg, m_projectStates);
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
    m_projectInfo = { "", "", "", "", RHIType::Unknown };
    projectLoaded = false;
    m_projectStates.clear();
    m_activeLevel.reset();
    m_scenePrepared = false;
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

void DiagnosticsManager::setActiveLevel(std::unique_ptr<EngineLevel> level)
{
    m_activeLevel = std::move(level);
}

std::unique_ptr<EngineLevel> DiagnosticsManager::getActiveLevel()
{
    // Rückgabe einer Kopie des Zeigers ist nicht möglich, da unique_ptr nicht kopierbar ist.
    // Stattdessen: Rückgabe eines Raw-Pointers oder einer Referenz, oder Übergabe des Besitzes (move).
    // Hier: Rückgabe eines Raw-Pointers (Aufrufer besitzt das Objekt nicht!).
    return m_activeLevel ? std::make_unique<EngineLevel>(*m_activeLevel) : nullptr;
}

EngineLevel* DiagnosticsManager::getActiveLevelSoft()
{
    return m_activeLevel.get();
}

// Alternativ, wenn Sie den Besitz übertragen wollen (Achtung: m_activeLevel ist danach nullptr!):
// std::unique_ptr<EngineLevel> DiagnosticsManager::getActiveLevel()
// {
//     return std::move(m_activeLevel);
// }

// Oder, wenn Sie nur einen Zeiger zurückgeben wollen (kein Besitzübergang!):
// EngineLevel* DiagnosticsManager::getActiveLevel()
// {
//     return m_activeLevel.get();
// }

void DiagnosticsManager::setScenePrepared(bool prepared)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_scenePrepared = prepared;
}

bool DiagnosticsManager::isScenePrepared() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_scenePrepared;
}

DiagnosticsManager::DiagnosticsManager()
{
	m_projectInfo = { "", "", "", "", RHIType::Unknown };
}

void DiagnosticsManager::registerKeyDownHandler(int key, KeyCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyDownHandlers[key].push_back(std::move(callback));
}

void DiagnosticsManager::registerKeyUpHandler(int key, KeyCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keyUpHandlers[key].push_back(std::move(callback));
}

bool DiagnosticsManager::dispatchKeyDown(int key)
{
    std::vector<KeyCallback> handlers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_keyDownHandlers.find(key);
        if (it != m_keyDownHandlers.end())
        {
            handlers = it->second;
        }
    }

    for (auto& h : handlers)
    {
        if (h && h())
        {
            return true;
        }
    }

    return false;
}

bool DiagnosticsManager::dispatchKeyUp(int key)
{
    std::vector<KeyCallback> handlers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_keyUpHandlers.find(key);
        if (it != m_keyUpHandlers.end())
        {
            handlers = it->second;
        }
    }

    for (auto& h : handlers)
    {
        if (h && h())
        {
            return true;
        }
    }

    return false;
}

bool DiagnosticsManager::isActionFinished(unsigned int actionID) const
{
	auto result = m_actions.find(actionID);
    return result != m_actions.end() && !result->second.inProgress;
}

DiagnosticsManager::Action& DiagnosticsManager::registerAction(ActionType type)
{
    static unsigned int nextID = 1;
    unsigned int actionID = nextID++;

    m_actions[actionID] = { type, true };
    return m_actions[actionID];
}

bool DiagnosticsManager::updateActionProgress(unsigned int actionID, bool inProgress)
{
    auto result = m_actions.find(actionID);
    if (result == m_actions.end())
    {
        return false;
    }
    result->second.inProgress = inProgress;
    if (!inProgress)
    {
        // Optionally remove finished actions from the map
        m_actions.erase(result);
	}
    return true;
}