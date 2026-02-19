#include "RenderResourceManager.h"

#include <memory>

#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"

#include "../Core/EngineLevel.h"
#include "../Core/Asset.h"
#include "../AssetManager/AssetManager.h"
#include "../Renderer/Texture.h"
#include <filesystem>

#include "OpenGLRenderer/OpenGLObject2D.h"
#include "OpenGLRenderer/OpenGLObject3D.h"
#include "OpenGLRenderer/OpenGLMaterial.h"
#include "OpenGLRenderer/OpenGLTextRenderer.h"
#include "UIWidget.h"

bool RenderResourceManager::prepareActiveLevel()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    EngineLevel* level = diagnostics.getActiveLevelSoft();
    if (!level)
        return false;

    auto& logger = Logger::Instance();
	if (!level->prepareEcs())
	{
		logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to prepare ECS for level", Logger::LogLevel::ERROR);
		return false;
	}

    logger.log(Logger::Category::Rendering, "RenderResourceManager: prepareActiveLevel() start", Logger::LogLevel::INFO);

    switch (diagnostics.getRHIType())
    {
    case DiagnosticsManager::RHIType::OpenGL:
        logger.log(Logger::Category::Rendering, "RenderResourceManager: selected backend OpenGL", Logger::LogLevel::INFO);
        return prepareOpenGL(*level);
    default:
        logger.log(Logger::Category::Rendering, "RenderResourceManager: unsupported backend", Logger::LogLevel::ERROR);
        return false;
    }
}

bool RenderResourceManager::prepareOpenGL(EngineLevel& level)
{
    auto& logger = Logger::Instance();

    const auto& objs = level.getWorldObjects();
    bool ok = true;

    logger.log(Logger::Category::Rendering,
        "RenderResourceManager: OpenGL prepare begin. objectCount=" + std::to_string(objs.size()),
        Logger::LogLevel::INFO);

	for (const auto& obj : objs)
	{
		if (!obj)
			continue;

		auto engineObj = obj->object;
		if (!engineObj)
			continue;

		const AssetType engineObjType = engineObj->getAssetType();
		if (engineObjType == AssetType::Unknown)
			continue;

		auto asset = std::static_pointer_cast<AssetData>(engineObj);
		const auto type = asset->getAssetType();
		if (type == AssetType::Model2D)
		{
			logger.log(Logger::Category::Rendering, "RenderResourceManager: preparing Model2D '" + asset->getPath() + "'", Logger::LogLevel::INFO);
			if (!prepareOpenGLObject2D(asset, {}))
			{
				logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to prepare OpenGL resources for Model2D: " + asset->getPath(), Logger::LogLevel::ERROR);
				ok = false;
			}
			else
			{
				logger.log(Logger::Category::Rendering, "RenderResourceManager: prepared Model2D '" + asset->getPath() + "'", Logger::LogLevel::INFO);
			}
		}
		else if (type == AssetType::Model3D || type == AssetType::PointLight)
		{
			logger.log(Logger::Category::Rendering, "RenderResourceManager: preparing Model3D '" + asset->getPath() + "'", Logger::LogLevel::INFO);
			if (!prepareOpenGLObject3D(asset, {}))
			{
				logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to prepare OpenGL resources for Model3D: " + asset->getPath() + "", Logger::LogLevel::ERROR);
				ok = false;
			}
			else
			{
				logger.log(Logger::Category::Rendering, "RenderResourceManager: prepared Model3D '" + asset->getPath() + "'", Logger::LogLevel::INFO);
			}
		}
	}

    logger.log(Logger::Category::Rendering,
        std::string("RenderResourceManager: OpenGL prepare end. result=") + (ok ? "success" : "failure"),
        ok ? Logger::LogLevel::INFO : Logger::LogLevel::WARNING);

    return ok;
}

void RenderResourceManager::prepareAssets(const std::vector<AssetPrepKind>& kinds)
{
    for (const auto kind : kinds)
    {
        switch (kind)
        {
        case AssetPrepKind::Text:
            prepareTextRenderer();
            break;
        case AssetPrepKind::Widget:
            break;
        default:
            break;
        }
    }
}

std::shared_ptr<Widget> RenderResourceManager::buildWidgetAsset(const std::shared_ptr<AssetData>& asset)
{
    if (!asset || asset->getAssetType() != AssetType::Widget)
    {
        return nullptr;
    }

    const unsigned int assetId = asset->getId();
    if (assetId != 0)
    {
        auto cached = m_widgetCache.find(assetId);
        if (cached != m_widgetCache.end())
        {
            if (auto widget = cached->second.lock())
            {
                return widget;
            }
            m_widgetCache.erase(cached);
        }
    }

    auto widget = std::make_shared<Widget>();
    widget->setName(asset->getName());
    widget->setPath(asset->getPath());
    widget->setAssetType(AssetType::Widget);
    widget->loadFromJson(asset->getData());

    if (assetId != 0)
    {
        m_widgetCache[assetId] = widget;
    }

    AssetManager::Instance().registerRuntimeResource(widget);
    return widget;
}

std::vector<RenderResourceManager::RenderableAsset> RenderResourceManager::buildRenderablesForSchema(const ECS::Schema& schema)
{
    auto& logger = Logger::Instance();
    auto& ecs = ECS::ECSManager::Instance();
    auto matches = ecs.getAssetsMatchingSchema(schema);
    std::vector<RenderableAsset> renderables;
    renderables.reserve(matches.size());

    auto& assetManager = AssetManager::Instance();
    for (const auto& match : matches)
    {
        std::vector<std::shared_ptr<Texture>> textures;
        std::string materialCacheKey;
        float shininess = 32.0f;
        if (!match.material.materialAssetPath.empty())
        {
            std::string materialPath = match.material.materialAssetPath;
            const std::filesystem::path matFs(materialPath);
            if (!matFs.is_absolute())
            {
                const auto absPath = assetManager.getAbsoluteContentPath(materialPath);
                if (!absPath.empty())
                {
                    materialPath = absPath;
                }
            }

            materialCacheKey = materialPath;
            auto cachedMat = m_materialDataCache.find(materialCacheKey);
            if (cachedMat != m_materialDataCache.end())
            {
                textures = cachedMat->second.textures;
                shininess = cachedMat->second.shininess;
            }
            else
            {
                int matId = assetManager.loadAsset(materialPath, AssetType::Material, AssetManager::Sync);
                if (matId != 0)
                {
                    logger.log(Logger::Category::Rendering, "RenderResourceManager: loaded material '" + materialPath + "'", Logger::LogLevel::INFO);
                    if (auto matAsset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(matId)))
                    {
                        const auto& matData = matAsset->getData();
                        if (matData.is_object())
                        {
                            if (matData.contains("m_shininess"))
                            {
                                shininess = matData.at("m_shininess").get<float>();
                            }
                            if (matData.contains("m_textureAssetPaths"))
                            {
                                const auto& paths = matData.at("m_textureAssetPaths");
                                if (paths.is_array())
                                {
                                    for (const auto& pathValue : paths)
                                    {
                                        if (!pathValue.is_string())
                                        {
                                            textures.push_back(nullptr);
                                            continue;
                                        }
                                        std::string texPath = pathValue.get<std::string>();
                                        const std::filesystem::path texFs(texPath);
                                        if (!texFs.is_absolute())
                                        {
                                            const auto absTex = assetManager.getAbsoluteContentPath(texPath);
                                            if (!absTex.empty())
                                            {
                                                texPath = absTex;
                                            }
                                        }
                                        int texId = assetManager.loadAsset(texPath, AssetType::Texture, AssetManager::Sync);
                                        if (texId == 0)
                                        {
                                            logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to load texture '" + texPath + "'", Logger::LogLevel::WARNING);
                                            textures.push_back(nullptr);
                                            continue;
                                        }
                                        auto texAsset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(texId));
                                        if (!texAsset)
                                        {
                                            logger.log(Logger::Category::Rendering, "RenderResourceManager: texture asset missing after load '" + texPath + "'", Logger::LogLevel::WARNING);
                                            textures.push_back(nullptr);
                                            continue;
                                        }
                                        const auto& texData = texAsset->getData();
                                        if (!texData.is_object())
                                        {
                                            textures.push_back(nullptr);
                                            continue;
                                        }
                                        if (!texData.contains("m_width") || !texData.contains("m_height") || !texData.contains("m_channels") || !texData.contains("m_data"))
                                        {
                                            textures.push_back(nullptr);
                                            continue;
                                        }
                                        auto texture = std::make_shared<Texture>();
                                        texture->setWidth(texData.at("m_width").get<int>());
                                        texture->setHeight(texData.at("m_height").get<int>());
                                        texture->setChannels(texData.at("m_channels").get<int>());
                                        texture->setData(texData.at("m_data").get<std::vector<unsigned char>>());
                                        textures.push_back(std::move(texture));
                                        logger.log(Logger::Category::Rendering, "RenderResourceManager: prepared texture '" + texPath + "'", Logger::LogLevel::INFO);
                                    }
                                }
                            }
                        }
                    }
                }

                if (!materialCacheKey.empty())
                {
                    m_materialDataCache[materialCacheKey] = { textures, shininess };
                }
            }
        }

        std::string meshPath = match.mesh.meshAssetPath;
        if (!meshPath.empty())
        {
            const std::filesystem::path meshFs(meshPath);
            if (!meshFs.is_absolute())
            {
                const auto absPath = assetManager.getAbsoluteContentPath(meshPath);
                if (!absPath.empty())
                {
                    meshPath = absPath;
                }
            }
        }

        int assetId = assetManager.loadAsset(meshPath, AssetType::Model3D, AssetManager::Sync);
        if (assetId == 0)
        {
            assetId = assetManager.loadAsset(meshPath, AssetType::Model2D, AssetManager::Sync);
        }
        if (assetId == 0)
        {
            logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to load mesh asset '" + match.mesh.meshAssetPath + "'", Logger::LogLevel::ERROR);
            continue;
        }

        auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
        if (!asset)
        {
            logger.log(Logger::Category::Rendering, "RenderResourceManager: mesh asset not found after load: '" + match.mesh.meshAssetPath + "'", Logger::LogLevel::ERROR);
            continue;
        }

        RenderableAsset renderable;
        renderable.entity = match.entity;
        renderable.asset = asset;
        renderable.textures = textures;
        renderable.assetType = asset->getAssetType();
        renderable.shininess = shininess;
        if (match.hasTransform)
        {
            renderable.transform = match.transform;
        }

        const unsigned int meshCacheId = (asset->getId() != 0) ? asset->getId() : static_cast<unsigned int>(assetId);
        const std::string obj3DCacheKey = std::to_string(meshCacheId) + "|" + materialCacheKey;

        if (renderable.assetType == AssetType::Model3D || renderable.assetType == AssetType::PointLight)
        {
            auto it = m_object3DCache.find(obj3DCacheKey);
            if (it != m_object3DCache.end())
            {
                renderable.object3D = it->second.lock();
                if (!renderable.object3D)
                {
                    m_object3DCache.erase(it);
                }
            }
            if (!renderable.object3D)
            {
                auto obj = std::make_shared<OpenGLObject3D>(asset);
                obj->setMaterialCacheKeySuffix(materialCacheKey);
                if (obj->prepare())
                {
                    obj->setTextures(textures);
                    obj->setShininess(shininess);
                    renderable.object3D = obj;
                    m_object3DCache[obj3DCacheKey] = obj;
                    AssetManager::Instance().registerRuntimeResource(obj);
                }
            }
        }
        else if (renderable.assetType == AssetType::Model2D)
        {
            auto it = m_object2DCache.find(meshCacheId);
            if (it != m_object2DCache.end())
            {
                renderable.object2D = it->second.lock();
                if (!renderable.object2D)
                {
                    m_object2DCache.erase(it);
                }
            }
            if (!renderable.object2D)
            {
                auto obj = std::make_shared<OpenGLObject2D>(asset);
                if (obj->prepare())
                {
                    obj->setTextures(textures);
                    renderable.object2D = obj;
                    if (meshCacheId != 0)
                    {
                        m_object2DCache[meshCacheId] = obj;
                    }
                    AssetManager::Instance().registerRuntimeResource(obj);
                }
            }
        }
        renderables.push_back(std::move(renderable));
    }

    return renderables;
}

std::shared_ptr<OpenGLObject2D> RenderResourceManager::getOrCreateObject2D(const std::shared_ptr<AssetData>& asset,
    const std::vector<std::shared_ptr<Texture>>& textures)
{
    if (!asset)
    {
        return nullptr;
    }

    const unsigned int cacheId = asset->getId();
    if (cacheId != 0)
    {
        auto it = m_object2DCache.find(cacheId);
        if (it != m_object2DCache.end())
        {
            if (auto existing = it->second.lock())
            {
                existing->setTextures(textures);
                return existing;
            }
            m_object2DCache.erase(it);
        }
    }

    auto obj = std::make_shared<OpenGLObject2D>(asset);
    if (!obj->prepare())
    {
        return nullptr;
    }
    obj->setTextures(textures);
    if (cacheId != 0)
    {
        m_object2DCache[cacheId] = obj;
    }
    AssetManager::Instance().registerRuntimeResource(obj);
    return obj;
}

std::shared_ptr<OpenGLObject3D> RenderResourceManager::getOrCreateObject3D(const std::shared_ptr<AssetData>& asset,
    const std::vector<std::shared_ptr<Texture>>& textures)
{
    if (!asset)
    {
        return nullptr;
    }

    const std::string cacheKey = std::to_string(asset->getId()) + "|";
    if (asset->getId() != 0)
    {
        auto it = m_object3DCache.find(cacheKey);
        if (it != m_object3DCache.end())
        {
            if (auto existing = it->second.lock())
            {
                existing->setTextures(textures);
                return existing;
            }
            m_object3DCache.erase(it);
        }
    }

    auto obj = std::make_shared<OpenGLObject3D>(asset);
    if (!obj->prepare())
    {
        return nullptr;
    }
    obj->setTextures(textures);
    if (asset->getId() != 0)
    {
        m_object3DCache[cacheKey] = obj;
    }
    AssetManager::Instance().registerRuntimeResource(obj);
    return obj;
}

void RenderResourceManager::clearCaches()
{
    m_object2DCache.clear();
    m_object3DCache.clear();
    m_materialDataCache.clear();
    m_textRenderer.reset();
}

std::shared_ptr<OpenGLTextRenderer> RenderResourceManager::prepareTextRenderer()
{
    if (auto existing = m_textRenderer.lock())
    {
        return existing;
    }

    const std::filesystem::path fontPath = std::filesystem::current_path() / "Content" / "Fonts" / "default.ttf";
    const std::filesystem::path vertexPath = std::filesystem::current_path() / "shaders" / "text_vertex.glsl";
    const std::filesystem::path fragmentPath = std::filesystem::current_path() / "shaders" / "text_fragment.glsl";

    auto renderer = std::make_shared<OpenGLTextRenderer>();
    if (!renderer->initialize(fontPath.string(), vertexPath.string(), fragmentPath.string()))
    {
        Logger::Instance().log(Logger::Category::Rendering, "RenderResourceManager: failed to initialize text renderer", Logger::LogLevel::ERROR);
        return nullptr;
    }

    m_textRenderer = renderer;
    AssetManager::Instance().registerRuntimeResource(renderer);
    return renderer;
}

bool RenderResourceManager::prepareOpenGLObject3D(const std::shared_ptr<AssetData>& asset, const std::vector<std::shared_ptr<Texture>>& textures)
{
	if (!asset)
        return false;

    OpenGLObject3D glObj(asset);
    if (!glObj.prepare())
    {
        return false;
    }
    glObj.setTextures(textures);
    return true;
}

bool RenderResourceManager::prepareOpenGLObject2D(const std::shared_ptr<AssetData>& asset, const std::vector<std::shared_ptr<Texture>>& textures)
{
	if (!asset)
        return false;

    OpenGLObject2D glObj(asset);
    if (!glObj.prepare())
    {
        return false;
    }
    glObj.setTextures(textures);
    return true;
}
