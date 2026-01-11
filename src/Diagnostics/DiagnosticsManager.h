#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <filesystem>
#include <vector>
#include <memory>
#include "../Basics/EngineLevel.h"

class DiagnosticsManager
{
public:
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
        SavingProject
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

    void setActiveLevel(std::unique_ptr<EngineLevel> level);
    EngineLevel* getActiveLevel();

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

	bool isLoadingAsset{ false };
    bool isSavingAsset{ false };
    bool isBuildingProject{ false };
    bool isLoadingProject{ false };
    bool isSavingProject{ false };
};
