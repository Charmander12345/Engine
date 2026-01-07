#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <filesystem>
#include <vector>

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

    // Set or update a state entry
    void setState(const std::string& key, const std::string& value);

    // Retrieve a state entry; returns std::nullopt if missing
    std::optional<std::string> getState(const std::string& key) const;

    // Clear all stored states
    void clear();

    // RHI selection
    void setRHIType(RHIType type);
    RHIType getRHIType() const;
    static std::string rhiTypeToString(RHIType type);

    // Persist/load simple key=value pairs to/from config/config.ini in the engine directory
    bool saveConfig() const;
    bool loadConfig();

	// Project info
	bool isProjectLoaded() const;
	const ProjectInfo& getProjectInfo() const;
	void setProjectInfo(const ProjectInfo& info);
	void clearProjectInfo();

	// Action states
    bool isActionInProgress() const;
	void setActionInProgress(ActionType action, bool inProgress);

private:
    DiagnosticsManager();
    ~DiagnosticsManager() = default;
    DiagnosticsManager(const DiagnosticsManager&) = delete;
    DiagnosticsManager& operator=(const DiagnosticsManager&) = delete;

    std::filesystem::path getConfigPath() const;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::string> m_states;
    RHIType m_rhiType{RHIType::Unknown};

	bool projectLoaded{ false };
	ProjectInfo m_projectInfo;

	bool isLoadingAsset{ false };
    bool isSavingAsset{ false };
    bool isBuildingProject{ false };
    bool isLoadingProject{ false };
    bool isSavingProject{ false };
};
