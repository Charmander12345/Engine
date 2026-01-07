#pragma once

#include <string>
#include <optional>
#include <memory>
#include "AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Basics/EngineObject.h"
#include "../Basics/Object3D.h"
#include "../Logger/Logger.h"

class AssetManager
{
public:
    static AssetManager& Instance();

    bool loadAsset(const std::string& path);
    bool saveAsset(const std::shared_ptr<EngineObject>& object);

    std::shared_ptr<EngineObject> createAsset(AssetType type, const std::string& path, const std::string& name);

    bool loadProject(const std::string& projectPath);
    bool saveProject(const std::string& projectPath);
    bool createProject(const std::string& parentDir, const std::string& projectName, const DiagnosticsManager::ProjectInfo& info);

private:
    AssetManager() = default;
    ~AssetManager() = default;
    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;
};
