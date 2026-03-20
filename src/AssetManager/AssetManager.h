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
#include <atomic>
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

// Intermediate result of reading an asset from disk without touching any shared state.
// Thread-safe to produce from any thread; must be finalized on the owning thread.
struct RawAssetData
{
	json data;
	std::string name;
	std::string path;
	AssetType type{ AssetType::Unknown };
	bool success{ false };
	std::string errorMessage;
};

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
	// Resolve <engine_exe>/Content/<relative> (built-in engine assets).
	std::string getAbsoluteEngineContentPath(const std::string& relativeToContent) const;
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
	std::shared_ptr<AssetData> getLoadedAssetByPath(const std::string& path) const;
	bool isAssetLoaded(const std::string& path) const;
	size_t getUnsavedAssetCount() const;

	// Information about a single unsaved asset for the save dialog.
	struct UnsavedAssetInfo
	{
		unsigned int id{ 0 };
		std::string name;
		std::string path;
		AssetType type{ AssetType::Unknown };
		bool isLevel{ false };
	};
	// Returns a list of all currently unsaved assets (including the active level).
	std::vector<UnsavedAssetInfo> getUnsavedAssetList() const;

	// Async save with per-asset progress callback (called from worker thread).
	void saveAllAssetsAsync(std::function<void(size_t saved, size_t total)> onProgress = {}, std::function<void(bool success)> onFinished = {});

	// Async save only the selected assets. selectedIds contains asset runtime IDs;
	// pass 0 to include the active level. Calls onFinished on completion.
	void saveSelectedAssetsAsync(const std::vector<unsigned int>& selectedIds, bool includeLevel,
		std::function<void(size_t saved, size_t total)> onProgress = {},
		std::function<void(bool success)> onFinished = {});

	//Project management
	bool loadProject(const std::string& projectPath, SyncState syncState = Sync, bool ensureDefaultContent = true);
	bool saveProject(const std::string& projectPath, SyncState syncState = Sync);
	bool createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info, SyncState syncState = Sync, bool includeDefaultContent = true);
	void unloadAllAssets();

	// Returns the full flat asset registry (all discovered .asset files with type + relative path).
	const std::vector<AssetRegistryEntry>& getAssetRegistry() const;

	// Monotonically increasing version; bumped whenever the registry changes.
	uint64_t getRegistryVersion() const { return m_registryVersion.load(std::memory_order_relaxed); }

	// Move an asset to a new folder, updating all references (registry, ECS components, .asset files).
	bool moveAsset(const std::string& oldRelPath, const std::string& newRelPath);

	// Rename an asset (file on disk + registry + ECS references + cross-asset references).
	// newName is the new filename stem (without extension). Returns true on success.
	bool renameAsset(const std::string& relPath, const std::string& newName);

	// Save only the active level (e.g. to persist editor camera on shutdown).
	bool saveActiveLevel();

	// Validate registry entries against disk; removes stale entries whose files no longer exist.
	// Returns the number of entries removed.
	size_t validateRegistry();

	// Validate ECS entity asset references against the registry. Logs warnings and
	// optionally shows toast messages for broken references. Returns the number of
	// broken references found.
	size_t validateEntityReferences(bool showToast = true);

	// Repair broken ECS entity asset references before rendering:
	// - Missing mesh: removes MeshComponent from the entity.
	// - Missing material: replaces with the WorldGrid material path.
	// Returns the number of components repaired.
	size_t repairEntityReferences();

	// Called after a successful asset import
	void setOnImportCompleted(std::function<void()> callback) { m_onImportCompleted = std::move(callback); }

	// --- Asset Reference Tracking ---
	struct AssetReference
	{
		std::string sourcePath;   // Content-relative path of the referencing asset / entity description
		std::string sourceType;   // "Level", "Material", "Entity", etc.
	};

	// Find all assets and ECS entities that reference the given content-relative path.
	std::vector<AssetReference> findReferencesTo(const std::string& relPath) const;

	// Return all content-relative asset paths that the given asset depends on (e.g. textures in a material).
	std::vector<std::string> getAssetDependencies(const std::string& relPath) const;

	// Register an asset entry in the registry (public for external create flows)
	void registerAssetInRegistry(const AssetRegistryEntry& entry);
	// Remove an asset from the registry and optionally delete from disk. Returns true on success.
	bool deleteAsset(const std::string& relPath, bool deleteFromDisk = true);
	// Register a loaded asset object and return its runtime ID (public for external create flows)
	unsigned int registerLoadedAsset(const std::shared_ptr<AssetData>& object);

	// Save a level asset to disk (public wrapper). Returns true on success.
	bool saveNewLevelAsset(EngineLevel* level);

	// --- Parallel loading API ---
	// Read an asset from disk without touching any shared state (thread-safe).
	static RawAssetData readAssetFromDisk(const std::string& path, AssetType expectedType);
	// Finalize a disk-loaded asset: create AssetData, register with GC. Returns asset ID (0 on failure).
	unsigned int finalizeAssetLoad(RawAssetData&& raw);
	// Load multiple assets in parallel. Returns map of path → asset ID for successfully loaded assets.
	std::unordered_map<std::string, int> loadBatchParallel(const std::vector<std::pair<std::string, AssetType>>& requests);

	// Load a level asset from an absolute file path. Sets the active level in DiagnosticsManager.
	struct LoadResult
	{
		json j;
		std::string errorMessage;
		bool success{ false };
	};
	LoadResult loadLevelAsset(const std::string& path);

private:
	void startWorkerPool();
	void stopWorkerPool();
	void enqueueJob(std::function<void()> job);

    void createWorldSettingsWidgetAsset();

	bool loadAssetRegistry(const std::string& projectRoot);
	bool saveAssetRegistry(const std::string& projectRoot) const;
	bool discoverAssetsAndBuildRegistry(const std::string& projectRoot);
	void discoverAssetsAndBuildRegistryAsync(const std::string& projectRoot);

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
	SaveResult saveSkyboxAsset(const std::shared_ptr<AssetData>& skybox);

	//Loading specific assettypes
	LoadResult ReadAssetHeader(const std::string& path, AssetType& outType);
	LoadResult loadTextureAsset(const std::string& path);
	LoadResult loadAudioAsset(const std::string& path);
	LoadResult loadMaterialAsset(const std::string& path);
	LoadResult loadObject2DAsset(const std::string& path);
	LoadResult loadObject3DAsset(const std::string& path);
	LoadResult loadWidgetAsset(const std::string& path);
	LoadResult loadSkyboxAsset(const std::string& path);

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
	CreateResult createSkyboxAsset(const std::string& path, const std::string& name, const std::string& sourcePath);

    AssetManager() = default;
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
	GarbageCollector m_garbageCollector;
	std::function<void()> m_onImportCompleted;

	std::vector<AssetRegistryEntry> m_registry;
	std::unordered_map<std::string, size_t> m_registryByPath;
	std::unordered_map<std::string, size_t> m_registryByName;
	bool m_suppressRegistrySave{ false };
	std::atomic<uint64_t> m_registryVersion{ 0 };

	// Registry + GC / internal state protection
	mutable std::mutex m_stateMutex;

	// Thread pool (sized to hardware_concurrency)
	std::vector<std::thread> m_workerPool;
	std::mutex m_jobMutex;
	std::condition_variable m_jobCv;
	std::queue<std::function<void()>> m_jobs;
	bool m_poolRunning{ false };
	bool m_poolStopRequested{ false };
	unsigned int m_poolSize{ 0 };

	// Batch-wait support: callers can wait until a batch of jobs finishes
	std::atomic<int> m_batchPending{ 0 };
	std::mutex m_batchMutex;
	std::condition_variable m_batchCv;

	std::unordered_map<unsigned int, std::shared_ptr<AssetData>> m_loadedAssets;
	std::unordered_map<std::string, unsigned int> m_loadedAssetsByPath;
	std::unordered_set<unsigned int> m_gcEligibleAssets;
	bool m_initialized{ false };
	mutable std::mutex m_asyncJobMutex;
	int m_nextAsyncJobId{ 1 };
	std::unordered_set<int> m_runningAssetJobs;
	std::unordered_map<int, int> m_finishedAssetJobs;
    static int s_nextAssetID;
};
