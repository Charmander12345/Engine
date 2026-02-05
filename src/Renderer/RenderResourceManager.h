#pragma once

#include <memory>
#include <vector>

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetTypes.h"

class EngineLevel;
class AssetData;
class Texture;

class RenderResourceManager
{
public:
    RenderResourceManager() = default;

    bool prepareActiveLevel();

    struct RenderableAsset
    {
        ECS::TransformComponent transform{};
        std::shared_ptr<AssetData> asset;
        std::vector<std::shared_ptr<Texture>> textures;
        AssetType assetType{ AssetType::Unknown };
    };

    std::vector<RenderableAsset> buildRenderablesForSchema(const ECS::Schema& schema);

private:
    bool prepareOpenGL(EngineLevel& level);
    bool prepareOpenGLObject2D(const std::shared_ptr<AssetData>& asset, const std::vector<std::shared_ptr<Texture>>& textures);
	bool prepareOpenGLObject3D(const std::shared_ptr<AssetData>& asset, const std::vector<std::shared_ptr<Texture>>& textures);
};
