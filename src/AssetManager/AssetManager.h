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

struct SDL_Window;

struct AssetRegistryEntry
{
    std::string name;
    std::string path; // relative to Content
    AssetType type{ AssetType::Unknown };
};

class Material;

class AssetManager
{
public:
    static AssetManager& Instance();

	bool initialize();

	bool saveAllAssets();

	// Load and return the asset object if successful (also registers with GC)
	// Returns nullptr on failure.
	std::shared_ptr<EngineObject> loadAsset(const std::string& path);

	// Convenience wrapper (kept for readability/compatibility): returns an object pointer.
	std::shared_ptr<EngineObject> loadAssetObject(const std::string& path);

    bool saveAsset(const std::shared_ptr<EngineObject>& object);

    std::shared_ptr<EngineObject> createAsset(AssetType type, const std::string& path, const std::string& name, const std::string& sourcePath = "");

    void importAssetWithDialog(SDL_Window* parentWindow, AssetType preferredType = AssetType::Unknown);

    // Sync import (kept for existing callers)
    std::shared_ptr<EngineObject> importAssetFromFile(AssetType type, const std::string& sourceFilePath);

    // Async import: runs the heavy work on the asset worker thread.
    void importAssetFromFileAsync(AssetType type, std::string sourceFilePath);

    // Async save of all currently tracked assets
    void saveAllAssetsAsync();

    // Optional: process any main-thread follow-ups (currently lightweight)
    void pump();

	// Synchronous: tidies up expired weak_ptr tracked resources.
	void collectGarbage();

    // Resolve <project>/Content/<relative> if a project is loaded.
    // Returns empty string if no project loaded.
    std::string getAbsoluteContentPath(const std::string& relativeToContent) const;

    // Convenience for runtime: resolve an Object2D/3D material + textures based on stored asset paths.
    // Creates a renderer Material instance (OpenGLMaterial etc. is responsibility of renderer; this only loads CPU assets).
	std::shared_ptr<Material> loadMaterialAsset(const std::string& materialAssetPath);
	std::vector<std::shared_ptr<Texture>> loadTexturesForMaterial(const Material& material);


    // Asset Registry
    bool doesAssetExist(const std::string& pathOrName) const;
    const std::vector<AssetRegistryEntry>& getAssetRegistry() const;

    bool loadProject(const std::string& projectPath);
    bool saveProject(const std::string& projectPath);
    bool createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info);

private:
    // Worker lifecycle
    void startWorker();
    void stopWorker();
    void enqueueJob(std::function<void()> job);

    bool loadAssetRegistry(const std::string& projectRoot);
    bool saveAssetRegistry(const std::string& projectRoot) const;
    bool discoverAssetsAndBuildRegistry(const std::string& projectRoot);
    void registerAssetInRegistry(const AssetRegistryEntry& entry);

    void ensureDefaultTriangleAssetSaved();
    void ensureDefaultAssetsCreated();
    std::unique_ptr<EngineLevel> createLevelWithDefaultTriangle(const std::string& levelPath, const std::string& levelName);

    AssetManager() = default;
    ~AssetManager();
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
	GarbageCollector m_garbageCollector;

    // Keeps built-in assets alive (GarbageCollector only stores weak_ptr).
    std::shared_ptr<EngineObject> m_defaultTriangle2D;

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
};
