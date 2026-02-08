#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <filesystem>
#include <vector>
#include <memory>
#include <functional>
#include "../Core/EngineLevel.h"
#include "../Core/MathTypes.h"

class DiagnosticsManager
{
public:
    using KeyCallback = std::function<bool()>;

    enum class RHIType
    {
        Unknown,
        OpenGL,
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

    // Project-specific states (stored in <project>/Config/defaults.ini)
    void setProjectState(const std::string& key, const std::string& value);
    std::optional<std::string> getProjectState(const std::string& key) const;
    void clearProjectStates();

    // Clear all stored states
    void clear();

    // RHI selection
    void setRHIType(RHIType type);
    RHIType getRHIType() const;
    static std::string rhiTypeToString(RHIType type);
    static std::string windowStateToString(WindowState state);
    static WindowState windowStateFromString(const std::string& value);

    // Persist/load simple key=value pairs to/from config/config.ini in the engine directory
    bool saveConfig() const;
    bool loadConfig();

    // Project config (defaults.ini)
    bool saveProjectConfig() const;
    bool loadProjectConfig();

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

    void setScenePrepared(bool prepared);
    bool isScenePrepared() const;

    void requestShutdown();
    bool isShutdownRequested() const;

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

private:
    DiagnosticsManager();
    ~DiagnosticsManager() = default;
    DiagnosticsManager(const DiagnosticsManager&) = delete;
    DiagnosticsManager& operator=(const DiagnosticsManager&) = delete;

    std::filesystem::path getConfigPath() const;
    std::filesystem::path getProjectConfigPath() const;

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

	bool isLoadingAsset{ false };
    bool isSavingAsset{ false };
    bool isBuildingProject{ false };
    bool isLoadingProject{ false };
    bool isSavingProject{ false };

    std::unordered_map<int, std::vector<KeyCallback>> m_keyDownHandlers;
    std::unordered_map<int, std::vector<KeyCallback>> m_keyUpHandlers;

	std::unordered_map<unsigned int, Action> m_actions;
	bool m_shutdownRequested{ false };
	Vec2 m_windowSize{ 0.0f, 0.0f };
	WindowState m_windowState{ WindowState::Maximized };
};
