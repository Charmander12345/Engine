#pragma once

#include <memory>
#include <vector>
#include <unordered_map>

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetTypes.h"

class EngineLevel;
class AssetData;
class Texture;
class OpenGLObject2D;
class OpenGLObject3D;
class OpenGLTextRenderer;
class Widget;

class RenderResourceManager
{
public:
    enum class AssetPrepKind
    {
        Text,
        Widget
    };
    RenderResourceManager() = default;

    bool prepareActiveLevel();

    struct RenderableAsset
    {
        ECS::Entity entity{ 0 };
        ECS::TransformComponent transform{};
        std::shared_ptr<AssetData> asset;
        std::vector<std::shared_ptr<Texture>> textures;
        std::shared_ptr<OpenGLObject2D> object2D;
        std::shared_ptr<OpenGLObject3D> object3D;
        AssetType assetType{ AssetType::Unknown };
        float shininess{32.0f};
        std::string fragmentShaderOverride;
    };

    std::vector<RenderableAsset> buildRenderablesForSchema(const ECS::Schema& schema, const std::string& defaultFragmentShader = "");
    std::shared_ptr<OpenGLObject2D> getOrCreateObject2D(const std::shared_ptr<AssetData>& asset,
        const std::vector<std::shared_ptr<Texture>>& textures);
    std::shared_ptr<OpenGLObject3D> getOrCreateObject3D(const std::shared_ptr<AssetData>& asset,
        const std::vector<std::shared_ptr<Texture>>& textures);
    void clearCaches();
    std::shared_ptr<OpenGLTextRenderer> prepareTextRenderer();
    void prepareAssets(const std::vector<AssetPrepKind>& kinds);
    std::shared_ptr<Widget> buildWidgetAsset(const std::shared_ptr<AssetData>& asset);

private:
    bool prepareOpenGL(EngineLevel& level);
    bool prepareOpenGLObject2D(const std::shared_ptr<AssetData>& asset, const std::vector<std::shared_ptr<Texture>>& textures);
	bool prepareOpenGLObject3D(const std::shared_ptr<AssetData>& asset, const std::vector<std::shared_ptr<Texture>>& textures);

    std::unordered_map<unsigned int, std::weak_ptr<OpenGLObject2D>> m_object2DCache;
    std::unordered_map<std::string, std::weak_ptr<OpenGLObject3D>> m_object3DCache;

    struct CachedMaterialData
    {
        std::vector<std::shared_ptr<Texture>> textures;
        float shininess{32.0f};
        std::string shaderFragment;
    };
    std::unordered_map<std::string, CachedMaterialData> m_materialDataCache;
    std::weak_ptr<OpenGLTextRenderer> m_textRenderer;
    std::unordered_map<unsigned int, std::weak_ptr<Widget>> m_widgetCache;
};
