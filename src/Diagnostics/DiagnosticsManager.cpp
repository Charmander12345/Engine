#include "DiagnosticsManager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

DiagnosticsManager& DiagnosticsManager::Instance()
{
    static DiagnosticsManager instance;
    return instance;
}

static float parseFloatOrDefault(const std::string& value, float fallback)
{
    if (value.empty())
    {
        return fallback;
    }

    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || errno == ERANGE)
    {
        return fallback;
    }

    return parsed;
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
    m_modalNotificationCache.clear();
    m_modalNotifications.clear();
    m_toastNotifications.clear();
}

void DiagnosticsManager::enqueueModalNotification(const std::string& message, bool dedupe)
{
    if (message.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (dedupe && !m_modalNotificationCache.insert(message).second)
    {
        return;
    }
    m_modalNotifications.push_back(message);
}

void DiagnosticsManager::enqueueToastNotification(const std::string& message, float durationSeconds, NotificationLevel level)
{
    if (message.empty())
    {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    ToastNotification toast{};
    toast.message = message;
    toast.durationSeconds = durationSeconds;
    toast.level = level;
    m_toastNotifications.push_back(std::move(toast));
}

std::vector<std::string> DiagnosticsManager::consumeModalNotifications()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> result = std::move(m_modalNotifications);
    m_modalNotifications.clear();
    return result;
}

std::vector<DiagnosticsManager::ToastNotification> DiagnosticsManager::consumeToastNotifications()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ToastNotification> result = std::move(m_toastNotifications);
    m_toastNotifications.clear();
    return result;
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
    case RHIType::Vulkan: return "Vulkan";
    case RHIType::DirectX11: return "DirectX11";
    case RHIType::DirectX12: return "DirectX12";
    default: return "Unknown";
    }
}

std::string DiagnosticsManager::windowStateToString(WindowState state)
{
    switch (state)
    {
    case WindowState::Maximized: return "Maximized";
    case WindowState::Fullscreen: return "Fullscreen";
    default: return "Normal";
    }
}

DiagnosticsManager::WindowState DiagnosticsManager::windowStateFromString(const std::string& value)
{
    if (value == "Maximized") return WindowState::Maximized;
    if (value == "Fullscreen") return WindowState::Fullscreen;
    return WindowState::Normal;
}

std::filesystem::path DiagnosticsManager::getConfigPath() const
{
    std::error_code ec;
    std::filesystem::path base = std::filesystem::current_path(ec);
    if (ec)
    {
        return {};
    }
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

bool DiagnosticsManager::saveConfigInternal() const
{
    const auto cfg = getConfigPath();
    if (cfg.empty())
    {
        return false;
    }

    // persist RHI in engine config too
    std::unordered_map<std::string, std::string> data = m_states;
    data["RHI"] = rhiTypeToString(m_rhiType);
    data["WindowWidth"] = std::to_string(m_windowSize.x);
    data["WindowHeight"] = std::to_string(m_windowSize.y);
    data["WindowState"] = windowStateToString(m_windowState);

    return writeKeyValueFile(cfg, data);
}

bool DiagnosticsManager::saveConfig() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return saveConfigInternal();
}

bool DiagnosticsManager::loadConfig()
{
    //std::lock_guard<std::mutex> lock(m_mutex);

    std::unordered_map<std::string, std::string> data;
    const auto cfg = getConfigPath();
    if (cfg.empty() || !readKeyValueFile(cfg, data))
    {
        data["RHI"] = "OpenGL";
    }

    m_states = data;

    auto it = m_states.find("RHI");
    if (it != m_states.end())
    {
        const auto& val = it->second;
        if (val == "OpenGL") m_rhiType = RHIType::OpenGL;
        else if (val == "Vulkan") m_rhiType = RHIType::Vulkan;
        else if (val == "DirectX11") m_rhiType = RHIType::DirectX11;
        else if (val == "DirectX12") m_rhiType = RHIType::DirectX12;
        else m_rhiType = RHIType::Unknown;
    }
    float width = 800.0f;
    float height = 600.0f;

    auto it2 = m_states.find("WindowWidth");
    if (it2 != m_states.end())
    {
        width = parseFloatOrDefault(it2->second, 800.0f);
    }

    auto it3 = m_states.find("WindowHeight");
    if (it3 != m_states.end())
    {
        height = parseFloatOrDefault(it3->second, 600.0f);
    }

    auto it4 = m_states.find("WindowState");
    if (it4 != m_states.end())
    {
        m_windowState = windowStateFromString(it4->second);
    }
    else
    {
        m_windowState = WindowState::Maximized;
    }

    // Restore known projects from semicolon-separated list
    m_knownProjects.clear();
    auto itKP = m_states.find("KnownProjects");
    if (itKP != m_states.end() && !itKP->second.empty())
    {
        std::istringstream ss(itKP->second);
        std::string token;
        while (std::getline(ss, token, ';'))
        {
            if (!token.empty())
                m_knownProjects.push_back(token);
        }
    }

    setWindowSize(Vec2{ width, height });
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
    std::error_code ec;
    if (!std::filesystem::exists(cfg, ec) || ec)
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

    for (auto& [token, callback] : m_activeLevelChangedCallbacks)
    {
        if (callback)
        {
            callback(nullptr);
        }
    }
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
    if (m_activeLevel)
    {
        m_activeLevel->setOnDirtyCallback([this]()
        {
            setScenePrepared(false);
        });
    }
    EngineLevel* active = m_activeLevel.get();
    for (auto& [token, callback] : m_activeLevelChangedCallbacks)
    {
        if (callback)
        {
            callback(active);
        }
    }
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

std::unique_ptr<EngineLevel> DiagnosticsManager::swapActiveLevel(std::unique_ptr<EngineLevel> newLevel)
{
    auto old = std::move(m_activeLevel);
    m_activeLevel = std::move(newLevel);
    if (m_activeLevel)
    {
        m_activeLevel->setOnDirtyCallback([this]()
        {
            setScenePrepared(false);
        });
    }
    EngineLevel* active = m_activeLevel.get();
    for (auto& [token, callback] : m_activeLevelChangedCallbacks)
    {
        if (callback)
        {
            callback(active);
        }
    }
    return old;
}

size_t DiagnosticsManager::registerActiveLevelChangedCallback(ActiveLevelChangedCallback callback)
{
    const size_t token = m_nextLevelCallbackToken++;
    m_activeLevelChangedCallbacks[token] = std::move(callback);
    return token;
}

void DiagnosticsManager::unregisterActiveLevelChangedCallback(size_t token)
{
    m_activeLevelChangedCallbacks.erase(token);
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

void DiagnosticsManager::invalidateEntity(unsigned int entityId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirtyEntities.insert(entityId);
}

std::vector<unsigned int> DiagnosticsManager::consumeDirtyEntities()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<unsigned int> result(m_dirtyEntities.begin(), m_dirtyEntities.end());
    m_dirtyEntities.clear();
    return result;
}

bool DiagnosticsManager::hasDirtyEntities() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_dirtyEntities.empty();
}

void DiagnosticsManager::setAssetRegistryReady(bool ready)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assetRegistryReady = ready;
}

bool DiagnosticsManager::isAssetRegistryReady() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_assetRegistryReady;
}

void DiagnosticsManager::setPIEActive(bool active)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pieActive = active;
}

bool DiagnosticsManager::isPIEActive() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_pieActive;
}

void DiagnosticsManager::addKnownProject(const std::string& projectPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Remove duplicates first
    for (auto it = m_knownProjects.begin(); it != m_knownProjects.end(); ++it)
    {
        if (*it == projectPath) { m_knownProjects.erase(it); break; }
    }
    // Push to front (most recent)
    m_knownProjects.insert(m_knownProjects.begin(), projectPath);
    // Keep at most 20 entries
    if (m_knownProjects.size() > 20)
        m_knownProjects.resize(20);

    // Persist into state as semicolon-separated list
    std::string joined;
    for (size_t i = 0; i < m_knownProjects.size(); ++i)
    {
        if (i > 0) joined += ';';
        joined += m_knownProjects[i];
    }
    m_states["KnownProjects"] = joined;
    saveConfigInternal();
}

void DiagnosticsManager::removeKnownProject(const std::string& projectPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_knownProjects.erase(
        std::remove(m_knownProjects.begin(), m_knownProjects.end(), projectPath),
        m_knownProjects.end());

    std::string joined;
    for (size_t i = 0; i < m_knownProjects.size(); ++i)
    {
        if (i > 0) joined += ';';
        joined += m_knownProjects[i];
    }
    m_states["KnownProjects"] = joined;
    saveConfigInternal();
}

std::vector<std::string> DiagnosticsManager::getKnownProjects() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_knownProjects;
}

void DiagnosticsManager::requestShutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdownRequested = true;
}

bool DiagnosticsManager::isShutdownRequested() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_shutdownRequested;
}

void DiagnosticsManager::resetShutdownRequest()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shutdownRequested = false;
}

DiagnosticsManager::DiagnosticsManager()
{
	m_projectInfo = { "", "", "", "", RHIType::Unknown };
    m_windowState = WindowState::Maximized;
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

void DiagnosticsManager::setWindowSize(const Vec2& size)
{
    std::lock_guard<std::mutex> lock(m_mutex);
	m_windowSize = size;
}

Vec2 DiagnosticsManager::getWindowSize() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_windowSize;
}

void DiagnosticsManager::setWindowState(WindowState state)
{
    m_windowState = state;
}

DiagnosticsManager::WindowState DiagnosticsManager::getWindowState() const
{
    return m_windowState;
}

// ??? Hardware info ???????????????????????????????????????????????????????????

const HardwareInfo& DiagnosticsManager::getHardwareInfo()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hardwareInfoQueried)
    {
        m_hardwareInfo.cpu      = queryCpuInfo();
        m_hardwareInfo.ram      = queryRamInfo();
        m_hardwareInfo.monitors = queryMonitorInfo();
        m_hardwareInfoQueried   = true;
    }
    return m_hardwareInfo;
}

void DiagnosticsManager::setGpuInfo(const GpuInfo& gpu)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hardwareInfo.gpu = gpu;
}

void DiagnosticsManager::refreshHardwareInfo()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hardwareInfo.cpu      = queryCpuInfo();
    m_hardwareInfo.ram      = queryRamInfo();
    m_hardwareInfo.monitors = queryMonitorInfo();
    m_hardwareInfoQueried   = true;
}

// ?? Frame metrics ring buffer ????????????????????????????????????????????????

void DiagnosticsManager::pushFrameMetrics(const FrameMetrics& metrics)
{
    if (m_frameHistory.size() < kMaxFrameHistory)
    {
        m_frameHistory.push_back(metrics);
        m_frameHistoryIndex = m_frameHistory.size() - 1;
        m_frameHistoryCount = m_frameHistory.size();
    }
    else
    {
        m_frameHistoryIndex = (m_frameHistoryIndex + 1) % kMaxFrameHistory;
        m_frameHistory[m_frameHistoryIndex] = metrics;
        m_frameHistoryCount = kMaxFrameHistory;
    }
}

const std::vector<DiagnosticsManager::FrameMetrics>& DiagnosticsManager::getFrameHistory() const
{
    return m_frameHistory;
}

const DiagnosticsManager::FrameMetrics& DiagnosticsManager::getLatestMetrics() const
{
    if (m_frameHistory.empty())
        return m_emptyMetrics;
    return m_frameHistory[m_frameHistoryIndex];
}

size_t DiagnosticsManager::getFrameHistorySize() const
{
    return m_frameHistoryCount;
}
