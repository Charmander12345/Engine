#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <mutex>
#include <filesystem>
#include <vector>
#include <memory>
#include <functional>
#include "../Core/EngineLevel.h"
#include "../Core/MathTypes.h"
#include "HardwareInfo.h"

class DiagnosticsManager
{
public:
    using KeyCallback = std::function<bool()>;
	using ActiveLevelChangedCallback = std::function<void(EngineLevel*)>;

    enum class RHIType
    {
        Unknown,
        OpenGL,
        Vulkan,
        DirectX11,
        DirectX12
    };

    
    struct ProjectInfo
    {
        std::string projectName;
        std::string projectVersion;
        std::string engineVersion;
        std::string projectPath;
		RHIType selectedRHI;
	};

    enum class ActionType
    {
        LoadingAsset,
        SavingAsset,
        BuildingProject,
        LoadingProject,
        SavingProject,
		ImportingAsset,
        NONE
	};

    struct Action
    {
        ActionType type { ActionType::NONE };
        bool inProgress{ true };
		unsigned int ID{ 0 };
    };

    enum WindowState
    {
        Normal,
        Maximized,
        Fullscreen
    };

    static DiagnosticsManager& Instance();

    // Engine-wide states
    void setState(const std::string& key, const std::string& value);
    std::optional<std::string> getState(const std::string& key) const;
    std::unordered_map<std::string, std::string> getStates() const;

    // Project-specific states (stored in <project>/Config/defaults.ini)
    void setProjectState(const std::string& key, const std::string& value);
    std::optional<std::string> getProjectState(const std::string& key) const;
    void clearProjectStates();

    // Clear all stored states
    void clear();

    // RHI selection
    void setRHIType(RHIType type);
    RHIType getRHIType() const;
    static const char* rhiTypeToString(RHIType type);
    static const char* windowStateToString(WindowState state);
    static WindowState windowStateFromString(const std::string& value);

    // Persist/load simple key=value pairs to/from config/config.ini in the engine directory
    bool saveConfig() const;
    bool loadConfig(bool merge = false);

    // Project config (defaults.ini)
    bool saveProjectConfig() const;
    bool loadProjectConfig();
    bool loadProjectConfigFromString(const std::string& content);

	// Project info
	bool isProjectLoaded() const;
	const ProjectInfo& getProjectInfo() const;
	void setProjectInfo(const ProjectInfo& info);
	void clearProjectInfo();

	// Action states
    bool isActionInProgress() const;
	void setActionInProgress(ActionType action, bool inProgress);

    // Input event dispatch (minimal)
    void registerKeyDownHandler(int key, KeyCallback callback);
    void registerKeyUpHandler(int key, KeyCallback callback);

    bool dispatchKeyDown(int key);
    bool dispatchKeyUp(int key);

    void setActiveLevel(std::unique_ptr<EngineLevel> level);
    std::unique_ptr<EngineLevel> getActiveLevel();
    EngineLevel* getActiveLevelSoft();
    std::unique_ptr<EngineLevel> swapActiveLevel(std::unique_ptr<EngineLevel> newLevel);
	size_t registerActiveLevelChangedCallback(ActiveLevelChangedCallback callback);
	void unregisterActiveLevelChangedCallback(size_t token);

    void setScenePrepared(bool prepared);
    bool isScenePrepared() const;

    void invalidateEntity(unsigned int entityId);
    std::vector<unsigned int> consumeDirtyEntities();
    void consumeDirtyEntities(std::vector<unsigned int>& out);
    bool hasDirtyEntities() const;

    void setAssetRegistryReady(bool ready);
    bool isAssetRegistryReady() const;

    void setPIEActive(bool active);
    bool isPIEActive() const;

    // Known / recent projects
    void addKnownProject(const std::string& projectPath);
    void removeKnownProject(const std::string& projectPath);
    std::vector<std::string> getKnownProjects() const;

    void requestShutdown();
    bool isShutdownRequested() const;
    void resetShutdownRequest();

    enum class NotificationLevel
    {
        Info,
        Success,
        Warning,
        Error
    };

    struct ToastNotification
    {
        std::string message;
        float durationSeconds{ 0.0f };
        NotificationLevel level{ NotificationLevel::Info };
    };

    void enqueueModalNotification(const std::string& message, bool dedupe = true);
    void enqueueToastNotification(const std::string& message, float durationSeconds, NotificationLevel level = NotificationLevel::Info);
    std::vector<std::string> consumeModalNotifications();
    std::vector<ToastNotification> consumeToastNotifications();

	// Action tracking (only intended for async actions)
	bool isActionFinished(unsigned int actionID) const;
	Action& registerAction(ActionType type);
	bool updateActionProgress(unsigned int actionID, bool inProgress);

    //Set window size
	void setWindowSize(const Vec2& size);
	Vec2 getWindowSize() const;

	//Set window state (normal, maximized, fullscreen)
	void setWindowState(WindowState state);
	WindowState getWindowState() const;

	// Hardware info (lazy-cached)
	const HardwareInfo& getHardwareInfo();
	void setGpuInfo(const GpuInfo& gpu);
	void refreshHardwareInfo();

	// Frame-level performance metrics (ring buffer for profiler tab)
	struct FrameMetrics
	{
		double fps{ 0.0 };
		double cpuFrameMs{ 0.0 };
		double gpuFrameMs{ 0.0 };
		double cpuWorldMs{ 0.0 };
		double cpuUiMs{ 0.0 };
		double cpuUiLayoutMs{ 0.0 };
		double cpuUiDrawMs{ 0.0 };
		double cpuEcsMs{ 0.0 };
		double cpuInputMs{ 0.0 };
		double cpuEventMs{ 0.0 };
		double cpuGcMs{ 0.0 };
		double cpuRenderMs{ 0.0 };
		double cpuOtherMs{ 0.0 };
		uint32_t visibleCount{ 0 };
		uint32_t hiddenCount{ 0 };
		uint32_t totalCount{ 0 };
	};

	static constexpr size_t kMaxFrameHistory = 300;
	void pushFrameMetrics(const FrameMetrics& metrics);
	const std::vector<FrameMetrics>& getFrameHistory() const;
	const FrameMetrics& getLatestMetrics() const;
	size_t getFrameHistorySize() const;

private:
    DiagnosticsManager();
    ~DiagnosticsManager() = default;
    DiagnosticsManager(const DiagnosticsManager&) = delete;
    DiagnosticsManager& operator=(const DiagnosticsManager&) = delete;

    std::filesystem::path getConfigPath() const;
    std::filesystem::path getProjectConfigPath() const;
    bool saveConfigInternal() const;

    static bool readKeyValueFile(const std::filesystem::path& filePath, std::unordered_map<std::string, std::string>& out);
    static bool writeKeyValueFile(const std::filesystem::path& filePath, const std::unordered_map<std::string, std::string>& data);

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_states;
    std::unordered_map<std::string, std::string> m_projectStates;
    RHIType m_rhiType{RHIType::Unknown};

	bool projectLoaded{ false };
	ProjectInfo m_projectInfo;
    std::unique_ptr<EngineLevel> m_activeLevel;

    bool m_scenePrepared{ false };
    std::unordered_set<unsigned int> m_dirtyEntities;
    bool m_pieActive{ false };
    bool m_assetRegistryReady{ false };
    std::vector<std::string> m_knownProjects;

	bool isLoadingAsset{ false };
    bool isSavingAsset{ false };
    bool isBuildingProject{ false };
    bool isLoadingProject{ false };
    bool isSavingProject{ false };

    std::unordered_map<int, std::vector<KeyCallback>> m_keyDownHandlers;
    std::unordered_map<int, std::vector<KeyCallback>> m_keyUpHandlers;
	std::unordered_map<size_t, ActiveLevelChangedCallback> m_activeLevelChangedCallbacks;
	size_t m_nextLevelCallbackToken{ 1 };

	std::unordered_map<unsigned int, Action> m_actions;
	bool m_shutdownRequested{ false };
	Vec2 m_windowSize{ 0.0f, 0.0f };
	WindowState m_windowState{ WindowState::Maximized };
    std::unordered_set<std::string> m_modalNotificationCache;
    std::vector<std::string> m_modalNotifications;
    std::vector<ToastNotification> m_toastNotifications;

    HardwareInfo m_hardwareInfo;
    bool m_hardwareInfoQueried{ false };

    // Profiler ring buffer
    std::vector<FrameMetrics> m_frameHistory;
    size_t m_frameHistoryIndex{ 0 };
    size_t m_frameHistoryCount{ 0 };
    FrameMetrics m_emptyMetrics{};
};
