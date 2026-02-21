#pragma once

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"
#include "GarbageCollector.h"
#include "json.hpp"
#include "Core.h"

using json = nlohmann::json;

struct SDL_Window;

struct AssetRegistryEntry
{
    std::string name;
    std::string path; // relative to Content
    AssetType type{ AssetType::Unknown };
};

struct Asset
{
	AssetType type{ AssetType::Unknown };
	unsigned int ID{ 0 };
};

class Material;

class AssetManager
{
public:

    enum SyncState
    {
        Async,
		Sync
    };

    static AssetManager& Instance();

	bool initialize();
	void ensureEditorWidgetsCreated();

	// Load and return the asset object if successful (also registers with GC) - Returns nullptr on failure.
	int loadAsset(const std::string& path, AssetType type, SyncState syncState = Sync);
	int loadAssetAsync(const std::string& path, AssetType type, bool allowGc = false);
	bool tryConsumeAssetLoadResult(int jobId, int& outAssetId);
	std::vector<int> getRunningAssetLoadJobs() const;
    //Saves the specified asset to a file. returns whether saving was successful. Note: when saving async function returns whether the job was queued successfully.
    bool saveAsset(const Asset& asset, SyncState syncState = Async, DiagnosticsManager::Action action = DiagnosticsManager::Action{});
	//Saves all currently loaded and altered assets. returns whether all saves were successful. Note: when saving async function returns whether the job was queued successfully.
    bool saveAllAssets(SyncState syncState = Async);
	//Create a new asset of the specified type at the specified path with the specified name. returns the asset ID if successful, 0 on failure.
    int createAsset(AssetType type, const std::string& path, const std::string& name, const std::string& sourcePath = "", SyncState syncState = Async);

    //import asset
	bool OpenImportDialog(SDL_Window* parentWindow, AssetType forcedType = AssetType::Unknown, SyncState syncState = Async);
    //Worker function for importing an asset from a selected file path
    void importAssetFromPath(std::string path, AssetType preferredType, unsigned int ActionID);

	// Synchronous: tidies up expired weak_ptr tracked resources.
	void collectGarbage();
	bool unloadAsset(unsigned int assetId);

	// Register runtime resources (e.g., render objects) with the GC.
	bool registerRuntimeResource(const std::shared_ptr<EngineObject>& resource);

	// Resolve <project>/Content/<relative> if a project is loaded. Returns empty string if no project is loaded.
    std::string getAbsoluteContentPath(const std::string& relativeToContent) const;
    // Resolve <engine>/Editor/Widgets/<relative>.
    std::string getEditorWidgetPath(const std::string& relativeToEditorWidgets) const;

	// Load an audio asset by content-relative path (.asset or .wav). Returns asset ID or 0 on failure.
	int loadAudioFromContentPath(const std::string& relativeToContent, bool allowGc = false);
	// Check if an audio asset is currently playing by content-relative path.
	bool isAudioPlayingContentPath(const std::string& relativeToContent) const;
	void markAssetGcEligible(unsigned int id, bool eligible);
	void releaseAudioAsset(unsigned int assetId);

	// Raw image loading (stb_image)
	unsigned char* loadRawImageData(const std::string& path, int& width, int& height, int& channels);
	void freeRawImageData(unsigned char* data);

	// Asset Registry
	bool doesAssetExist(const std::string& pathOrName) const;
	std::vector<std::shared_ptr<AssetData>> getAssetsByType(AssetType type) const;
	std::shared_ptr<AssetData> getLoadedAssetByID(unsigned int id) const;
	bool isAssetLoaded(const std::string& path) const;
	size_t getUnsavedAssetCount() const;

	// Async save with per-asset progress callback (called from worker thread).
	void saveAllAssetsAsync(std::function<void(size_t saved, size_t total)> onProgress = {}, std::function<void(bool success)> onFinished = {});

	//Project management
	bool loadProject(const std::string& projectPath, SyncState syncState = Sync);
	bool saveProject(const std::string& projectPath, SyncState syncState = Sync);
	bool createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info, SyncState syncState = Sync);
	void unloadAllAssets();

	// Returns the full flat asset registry (all discovered .asset files with type + relative path).
	const std::vector<AssetRegistryEntry>& getAssetRegistry() const;

	// Move an asset to a new folder, updating all references (registry, ECS components, .asset files).
	bool moveAsset(const std::string& oldRelPath, const std::string& newRelPath);

	// Save only the active level (e.g. to persist editor camera on shutdown).
	bool saveActiveLevel();

	// Called after a successful asset import
	void setOnImportCompleted(std::function<void()> callback) { m_onImportCompleted = std::move(callback); }

private:
    // Worker lifecycle
    void startWorker();
    void stopWorker();
    void enqueueJob(std::function<void()> job);

    void createWorldSettingsWidgetAsset();

	bool loadAssetRegistry(const std::string& projectRoot);
	bool saveAssetRegistry(const std::string& projectRoot) const;
	bool discoverAssetsAndBuildRegistry(const std::string& projectRoot);
	void registerAssetInRegistry(const AssetRegistryEntry& entry);

	// Scan all .asset files under contentDir for string references to oldRelPath and replace with newRelPath.
	void updateAssetFileReferences(const std::filesystem::path& contentDir, const std::string& oldRelPath, const std::string& newRelPath);

	// Default assets
    void ensureDefaultAssetsCreated();

    //Saving specific assettypes
    struct SaveResult
    {
        bool success{ false };
		std::string errorMessage;
    };

	SaveResult saveTextureAsset(const std::shared_ptr<AssetData>& texture);
	SaveResult saveAudioAsset(const std::shared_ptr<AssetData>& audio);
	SaveResult saveMaterialAsset(const std::shared_ptr<AssetData>& material);
	SaveResult saveObject2DAsset(const std::shared_ptr<AssetData>& object2D);
	SaveResult saveObject3DAsset(const std::shared_ptr<AssetData>& object3D);
	SaveResult saveLevelAsset(const std::unique_ptr<EngineLevel>& level);
	SaveResult saveLevelAsset(EngineLevel* level);
	SaveResult saveWidgetAsset(const std::shared_ptr<AssetData>& widget);

	//Loading specific assettypes
    struct LoadResult
    {
		json j;
		std::string errorMessage; 
		bool success{ false };
    };

	LoadResult ReadAssetHeader(const std::string& path, AssetType& outType);
	LoadResult loadTextureAsset(const std::string& path);
	LoadResult loadAudioAsset(const std::string& path);
	LoadResult loadMaterialAsset(const std::string& path);
	LoadResult loadObject2DAsset(const std::string& path);
	LoadResult loadObject3DAsset(const std::string& path);
	LoadResult loadLevelAsset(const std::string& path);
	LoadResult loadWidgetAsset(const std::string& path);

	// Creating specific assettypes
    struct CreateResult
	{
		std::shared_ptr<AssetData> object{ nullptr };
		std::string errorMessage;
		bool success{ false };
	};

	CreateResult createTextureAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createMaterialAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createObject2DAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createObject3DAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createLevelAsset(const std::string& path, const std::string& name, const std::string& sourcePath);

	unsigned int registerLoadedAsset(const std::shared_ptr<AssetData>& object);

    AssetManager() = default;
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
	GarbageCollector m_garbageCollector;
	std::function<void()> m_onImportCompleted;

    std::vector<AssetRegistryEntry> m_registry;
    std::unordered_map<std::string, size_t> m_registryByPath;
    std::unordered_map<std::string, size_t> m_registryByName;

    // Registry + GC / internal state protection
    mutable std::mutex m_stateMutex;

    // Async job queue
    std::thread m_worker;
    std::mutex m_jobMutex;
    std::condition_variable m_jobCv;
    std::queue<std::function<void()>> m_jobs;
    bool m_workerRunning{ false };
    bool m_workerStopRequested{ false };

	std::unordered_map<unsigned int, std::shared_ptr<AssetData>> m_loadedAssets;
	std::unordered_set<unsigned int> m_gcEligibleAssets;
	mutable std::mutex m_asyncJobMutex;
	int m_nextAsyncJobId{ 1 };
	std::unordered_set<int> m_runningAssetJobs;
	std::unordered_map<int, int> m_finishedAssetJobs;
    static int s_nextAssetID;
};
