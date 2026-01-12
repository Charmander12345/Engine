#pragma once

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include <unordered_map>
#include "AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Basics/EngineObject.h"
#include "../Basics/Object2D.h"
#include "../Basics/Object3D.h"
#include "../Basics/EngineLevel.h"
#include "../Basics/Texture.h"
#include "../Basics/MaterialAsset.h"
#include "../Logger/Logger.h"
#include "GarbageCollector.h"

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

    bool loadAsset(const std::string& path);

    // Load and return the asset object if successful (also registers with GC)
    std::shared_ptr<EngineObject> loadAssetObject(const std::string& path);

    bool saveAsset(const std::shared_ptr<EngineObject>& object);

    std::shared_ptr<EngineObject> createAsset(AssetType type, const std::string& path, const std::string& name, const std::string& sourcePath = "");

    // Resolve <project>/Content/<relative> if a project is loaded.
    // Returns empty string if no project loaded.
    std::string getAbsoluteContentPath(const std::string& relativeToContent) const;

    // Convenience for runtime: resolve an Object2D/3D material + textures based on stored asset paths.
    // Creates a renderer Material instance (OpenGLMaterial etc. is responsibility of renderer; this only loads CPU assets).
    std::shared_ptr<MaterialAsset> loadMaterialAsset(const std::string& materialAssetPath);
    std::vector<std::shared_ptr<Texture>> loadTexturesForMaterial(const MaterialAsset& material);


    // Asset Registry
    bool doesAssetExist(const std::string& pathOrName) const;
    const std::vector<AssetRegistryEntry>& getAssetRegistry() const;

    bool loadProject(const std::string& projectPath);
    bool saveProject(const std::string& projectPath);
    bool createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info);

private:
    bool loadAssetRegistry(const std::string& projectRoot);
    bool saveAssetRegistry(const std::string& projectRoot) const;
    bool discoverAssetsAndBuildRegistry(const std::string& projectRoot);
    void registerAssetInRegistry(const AssetRegistryEntry& entry);

    AssetManager() = default;
    ~AssetManager() = default;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
	GarbageCollector m_garbageCollector;

    // Keeps built-in assets alive (GarbageCollector only stores weak_ptr).
    std::shared_ptr<EngineObject> m_defaultTriangle2D;

    std::vector<AssetRegistryEntry> m_registry;
    std::unordered_map<std::string, size_t> m_registryByPath;
    std::unordered_map<std::string, size_t> m_registryByName;
};
