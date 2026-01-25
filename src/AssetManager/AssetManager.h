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
#include "AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Basics/EngineObject.h"
#include "../Basics/Object2D.h"
#include "../Basics/Object3D.h"
#include "../Basics/EngineLevel.h"
#include "../Basics/Texture.h"
#include "../Basics/Material.h"
#include "../Logger/Logger.h"
#include "GarbageCollector.h"
#include "json.hpp"

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

	// Load and return the asset object if successful (also registers with GC) - Returns nullptr on failure.
	int loadAsset(const std::string& path, AssetType type, SyncState syncState = Sync);
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

	// Resolve <project>/Content/<relative> if a project is loaded. Returns empty string if no project is loaded.
    std::string getAbsoluteContentPath(const std::string& relativeToContent) const;

    // Asset Registry
    bool doesAssetExist(const std::string& pathOrName) const;

	//Project management
    bool loadProject(const std::string& projectPath, SyncState syncState = Sync);
    bool saveProject(const std::string& projectPath, SyncState syncState = Sync);
    bool createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info, SyncState syncState = Sync);

private:
    // Worker lifecycle
    void startWorker();
    void stopWorker();
    void enqueueJob(std::function<void()> job);

    bool loadAssetRegistry(const std::string& projectRoot);
    bool saveAssetRegistry(const std::string& projectRoot) const;
    bool discoverAssetsAndBuildRegistry(const std::string& projectRoot);
    void registerAssetInRegistry(const AssetRegistryEntry& entry);
    const std::vector<AssetRegistryEntry>& getAssetRegistry() const;

	// Default assets
    void ensureDefaultAssetsCreated();

    //Saving specific assettypes
    struct SaveResult
    {
        bool success{ false };
		std::string errorMessage;
    };

	SaveResult saveTextureAsset(const std::shared_ptr<Texture>& texture);
	SaveResult saveMaterialAsset(const std::shared_ptr<Material>& material);
	SaveResult saveObject2DAsset(const std::shared_ptr<Object2D>& object2D);
	SaveResult saveObject3DAsset(const std::shared_ptr<Object3D>& object3D);
	SaveResult saveLevelAsset(const std::unique_ptr<EngineLevel>& level);

	//Loading specific assettypes
    struct LoadResult
    {
		json j;
		std::string errorMessage; 
		bool success{ false };
    };

	LoadResult ReadAssetHeader(const std::string& path, AssetType& outType);
	LoadResult loadTextureAsset(const std::string& path);
	LoadResult loadMaterialAsset(const std::string& path);
	LoadResult loadObject2DAsset(const std::string& path);
	LoadResult loadObject3DAsset(const std::string& path);
	LoadResult loadLevelAsset(const std::string& path);

	// Creating specific assettypes
    struct CreateResult
	{
		std::shared_ptr<EngineObject> object{ nullptr };
		std::string errorMessage;
		bool success{ false };
	};

	CreateResult createTextureAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createMaterialAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createObject2DAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createObject3DAsset(const std::string& path, const std::string& name, const std::string& sourcePath);
	CreateResult createLevelAsset(const std::string& path, const std::string& name, const std::string& sourcePath);

	unsigned int registerLoadedAsset(const std::shared_ptr<EngineObject>& object);
	std::shared_ptr<EngineObject> getLoadedAssetByID(unsigned int id) const;

    AssetManager() = default;
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
	GarbageCollector m_garbageCollector;

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

    std::unordered_map<unsigned int, std::shared_ptr<EngineObject>> m_loadedAssets;
    static int s_nextAssetID;
};
