#include "RenderResourceManager.h"

#include <memory>
#include <unordered_set>

#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"

#include "../Core/EngineLevel.h"
#include "../Core/Asset.h"
#include "../AssetManager/AssetManager.h"
#include "../Renderer/Texture.h"
#include "../Renderer/DDSLoader.h"
#include <filesystem>

#include "OpenGLRenderer/OpenGLObject2D.h"
#include "OpenGLRenderer/OpenGLObject3D.h"
#include "OpenGLRenderer/OpenGLMaterial.h"
#include "OpenGLRenderer/OpenGLTextRenderer.h"
#include "IRenderObject2D.h"
#include "IRenderObject3D.h"
#include "ITextRenderer.h"
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

	// Validate entity asset references after ECS is populated
#if ENGINE_EDITOR
	{
		auto& assetMgr = AssetManager::Instance();

		// Repair broken references before rendering:
		// - Missing mesh: remove MeshComponent so the entity is skipped.
		// - Missing material: swap in the WorldGrid material.
		const size_t fixed = assetMgr.repairEntityReferences();
		if (fixed > 0)
		{
			logger.log(Logger::Category::Rendering,
				"RenderResourceManager: repaired " + std::to_string(fixed) + " broken entity reference(s).",
				Logger::LogLevel::WARNING);
		}

		// Log any remaining warnings (e.g. missing scripts)
		const size_t broken = assetMgr.validateEntityReferences(true);
		if (broken > 0)
		{
			logger.log(Logger::Category::Rendering,
				"RenderResourceManager: " + std::to_string(broken) + " broken entity asset reference(s) detected.",
				Logger::LogLevel::WARNING);
		}
	}
#endif

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

std::vector<RenderResourceManager::RenderableAsset> RenderResourceManager::buildRenderablesForSchema(const ECS::Schema& schema, const std::string& defaultFragmentShader)
{
    auto& logger = Logger::Instance();
    auto& ecs = ECS::ECSManager::Instance();
    auto matches = ecs.getAssetsMatchingSchema(schema);
    std::vector<RenderableAsset> renderables;
    renderables.reserve(matches.size());

    auto& assetManager = AssetManager::Instance();

    // Helper: resolve a relative content path to absolute
    const auto resolvePath = [this](const std::string& rawPath) -> std::string
    {
        return resolveContentPath(rawPath);
    };

    // ── Pass 1: Collect all unique mesh + material paths ──
    std::vector<std::pair<std::string, AssetType>> batchRequests;
    std::unordered_set<std::string> seen;
    for (const auto& match : matches)
    {
        if (!match.mesh.meshAssetPath.empty())
        {
            const std::string meshPath = resolvePath(match.mesh.meshAssetPath);
            if (!meshPath.empty() && seen.insert(meshPath).second)
            {
                batchRequests.push_back({ meshPath, AssetType::Model3D });
            }
        }
        if (!match.material.materialAssetPath.empty())
        {
            const std::string matPath = resolvePath(match.material.materialAssetPath);
            if (!matPath.empty() && seen.insert(matPath).second)
            {
                batchRequests.push_back({ matPath, AssetType::Material });
            }
        }
    }

    // ── Batch-load meshes + materials in parallel ──
    if (!batchRequests.empty())
    {
        logger.log(Logger::Category::Rendering,
            "buildRenderablesForSchema: queuing " + std::to_string(batchRequests.size()) + " mesh/material load jobs",
            Logger::LogLevel::INFO);
        assetManager.loadBatchParallel(batchRequests);
        logger.log(Logger::Category::Rendering,
            "buildRenderablesForSchema: mesh/material batch done",
            Logger::LogLevel::INFO);
    }

    // ── Pass 2: Discover texture paths from loaded materials ──
    std::vector<std::pair<std::string, AssetType>> textureBatchRequests;
    for (const auto& match : matches)
    {
        if (match.material.materialAssetPath.empty())
            continue;
        const std::string matPath = resolvePath(match.material.materialAssetPath);
        if (matPath.empty())
            continue;

        // Skip if we already cached this material's data
        if (m_materialDataCache.count(matPath))
            continue;

        // Try to read texture paths from the loaded material
        auto matAsset = assetManager.getLoadedAssetByPath(matPath);
        if (!matAsset)
            continue;
        const auto& matData = matAsset->getData();
        if (!matData.is_object() || !matData.contains("m_textureAssetPaths"))
            continue;
        const auto& texPaths = matData.at("m_textureAssetPaths");
        if (!texPaths.is_array())
            continue;
        for (const auto& pathValue : texPaths)
        {
            if (!pathValue.is_string())
                continue;
            const std::string texPath = resolvePath(pathValue.get<std::string>());
            if (!texPath.empty() && seen.insert(texPath).second)
            {
                textureBatchRequests.push_back({ texPath, AssetType::Texture });
            }
        }
    }

    // ── Batch-load textures in parallel (stbi_load is the heavy part) ──
    if (!textureBatchRequests.empty())
    {
        logger.log(Logger::Category::Rendering,
            "buildRenderablesForSchema: queuing " + std::to_string(textureBatchRequests.size()) + " texture load jobs",
            Logger::LogLevel::INFO);
        assetManager.loadBatchParallel(textureBatchRequests);
        logger.log(Logger::Category::Rendering,
            "buildRenderablesForSchema: texture batch done",
            Logger::LogLevel::INFO);
    }

    // ── Pass 3: Build renderables (all assets now in cache → instant hits) ──
    for (const auto& match : matches)
    {
        std::vector<std::shared_ptr<Texture>> textures;
        std::string materialCacheKey;
        float shininess = 32.0f;
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specularMultiplier = 1.0f;
        bool pbrEnabled = false;
        std::string fragmentShaderOverride;
        if (!match.material.materialAssetPath.empty())
        {
            std::string materialPath = resolvePath(match.material.materialAssetPath);
            materialCacheKey = materialPath;
            auto cachedMat = m_materialDataCache.find(materialCacheKey);
            if (cachedMat != m_materialDataCache.end())
            {
                textures = cachedMat->second.textures;
                shininess = cachedMat->second.shininess;
                metallic = cachedMat->second.metallic;
                roughness = cachedMat->second.roughness;
                specularMultiplier = cachedMat->second.specularMultiplier;
                pbrEnabled = cachedMat->second.pbrEnabled;
                fragmentShaderOverride = cachedMat->second.shaderFragment;
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
                            if (matData.contains("m_shaderFragment"))
                            {
                                fragmentShaderOverride = matData.at("m_shaderFragment").get<std::string>();
                            }
                            if (matData.contains("m_shininess"))
                            {
                                shininess = matData.at("m_shininess").get<float>();
                            }
                            if (matData.contains("m_metallic"))
                            {
                                metallic = matData.at("m_metallic").get<float>();
                            }
                            if (matData.contains("m_roughness"))
                            {
                                roughness = matData.at("m_roughness").get<float>();
                            }
                            if (matData.contains("m_specularMultiplier"))
                            {
                                specularMultiplier = matData.at("m_specularMultiplier").get<float>();
                            }
                            if (matData.contains("m_pbrEnabled"))
                            {
                                pbrEnabled = matData.at("m_pbrEnabled").get<bool>();
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
                                        std::string texPath = resolvePath(pathValue.get<std::string>());
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

                                        // Check for compressed DDS texture
                                        if (texData.contains("m_compressed") && texData["m_compressed"].get<bool>() && texData.contains("m_ddsPath"))
                                        {
                                            const std::string ddsPath = texData["m_ddsPath"].get<std::string>();
                                            auto ddsTexture = loadDDS(ddsPath);
                                            if (ddsTexture)
                                            {
                                                textures.push_back(std::move(ddsTexture));
                                                logger.log(Logger::Category::Rendering, "RenderResourceManager: prepared compressed texture '" + texPath + "'", Logger::LogLevel::INFO);
                                            }
                                            else
                                            {
                                                logger.log(Logger::Category::Rendering, "RenderResourceManager: failed to load DDS texture '" + ddsPath + "'", Logger::LogLevel::WARNING);
                                                textures.push_back(nullptr);
                                            }
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

                                        // Driver-side compression: request the GL driver to compress
                                        // uncompressed textures into BCn format during upload.
                                        if (auto tc = DiagnosticsManager::Instance().getState("TextureCompressionEnabled"))
                                        {
                                            if (*tc == "1")
                                                texture->setRequestCompression(true);
                                        }

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
                    m_materialDataCache[materialCacheKey] = { textures, shininess, metallic, roughness, specularMultiplier, pbrEnabled, fragmentShaderOverride };
                }
            }
        }

        std::string meshPath = resolvePath(match.mesh.meshAssetPath);

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
        renderable.metallic = metallic;
        renderable.roughness = roughness;
        renderable.specularMultiplier = specularMultiplier;
        renderable.pbrEnabled = pbrEnabled;

        // Determine effective fragment shader override: material > default parameter
        std::string effectiveFragShader = fragmentShaderOverride;
        if (effectiveFragShader.empty())
        {
            effectiveFragShader = defaultFragmentShader;
        }
        renderable.fragmentShaderOverride = effectiveFragShader;

        if (match.hasTransform)
        {
            renderable.transform = match.transform;
        }

        const unsigned int meshCacheId = (asset->getId() != 0) ? asset->getId() : static_cast<unsigned int>(assetId);
        const std::string obj3DCacheKey = std::to_string(meshCacheId) + "|" + materialCacheKey + "|" + effectiveFragShader;

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
                if (!effectiveFragShader.empty())
                {
                    obj->setFragmentShaderOverride(effectiveFragShader);
                }
                if (obj->prepare())
                {
                    obj->setTextures(textures);
                    obj->setShininess(shininess);
                    obj->setPbrData(pbrEnabled, metallic, roughness);
                    obj->setSpecularMultiplier(specularMultiplier);
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

std::shared_ptr<IRenderObject2D> RenderResourceManager::getOrCreateObject2D(const std::shared_ptr<AssetData>& asset,
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

std::shared_ptr<IRenderObject3D> RenderResourceManager::getOrCreateObject3D(const std::shared_ptr<AssetData>& asset,
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

std::shared_ptr<ITextRenderer> RenderResourceManager::prepareTextRenderer()
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

std::string RenderResourceManager::resolveContentPath(const std::string& rawPath) const
{
	if (rawPath.empty())
		return {};
	auto& assetManager = AssetManager::Instance();
	std::string resolved = rawPath;
	const std::filesystem::path p(rawPath);
	if (!p.is_absolute())
	{
		const auto absPath = assetManager.getAbsoluteContentPath(rawPath);
		if (!absPath.empty())
			resolved = absPath;
	}
	if (!std::filesystem::exists(resolved))
	{
		const auto enginePath = assetManager.getAbsoluteEngineContentPath(rawPath);
		if (!enginePath.empty() && std::filesystem::exists(enginePath))
			resolved = enginePath;
	}
	return resolved;
}

RenderResourceManager::RenderableAsset RenderResourceManager::refreshEntityRenderable(
	ECS::Entity entity, const std::string& defaultFragmentShader)
{
	auto& logger = Logger::Instance();
	auto& ecs = ECS::ECSManager::Instance();
	auto& assetManager = AssetManager::Instance();

	RenderableAsset result{};

	// The entity must have at least a MeshComponent to produce a renderable.
	const auto* meshComp = ecs.getComponent<ECS::MeshComponent>(entity);
	if (!meshComp || meshComp->meshAssetPath.empty())
		return result;

	const auto* matComp = ecs.getComponent<ECS::MaterialComponent>(entity);
	const auto* transComp = ecs.getComponent<ECS::TransformComponent>(entity);

	// --- Resolve material (textures, shininess, fragment shader) ---
	std::vector<std::shared_ptr<Texture>> textures;
	std::string materialCacheKey;
	float shininess = 32.0f;
	float metallic = 0.0f;
	float roughness = 0.5f;
	float specularMultiplier = 1.0f;
	bool pbrEnabled = false;
	std::string fragmentShaderOverride;

	if (matComp && !matComp->materialAssetPath.empty())
	{
		std::string materialPath = resolveContentPath(matComp->materialAssetPath);
		materialCacheKey = materialPath;

		auto cachedMat = m_materialDataCache.find(materialCacheKey);
		if (cachedMat != m_materialDataCache.end())
		{
			textures = cachedMat->second.textures;
			shininess = cachedMat->second.shininess;
			metallic = cachedMat->second.metallic;
			roughness = cachedMat->second.roughness;
			specularMultiplier = cachedMat->second.specularMultiplier;
			pbrEnabled = cachedMat->second.pbrEnabled;
			fragmentShaderOverride = cachedMat->second.shaderFragment;
		}
		else
		{
			int matId = assetManager.loadAsset(materialPath, AssetType::Material, AssetManager::Sync);
			if (matId != 0)
			{
				if (auto matAsset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(matId)))
				{
					const auto& matData = matAsset->getData();
					if (matData.is_object())
					{
						if (matData.contains("m_shaderFragment"))
							fragmentShaderOverride = matData.at("m_shaderFragment").get<std::string>();
						if (matData.contains("m_shininess"))
							shininess = matData.at("m_shininess").get<float>();
						if (matData.contains("m_metallic"))
							metallic = matData.at("m_metallic").get<float>();
						if (matData.contains("m_roughness"))
							roughness = matData.at("m_roughness").get<float>();
						if (matData.contains("m_specularMultiplier"))
							specularMultiplier = matData.at("m_specularMultiplier").get<float>();
						if (matData.contains("m_pbrEnabled"))
							pbrEnabled = matData.at("m_pbrEnabled").get<bool>();
						if (matData.contains("m_textureAssetPaths"))
						{
							const auto& paths = matData.at("m_textureAssetPaths");
							if (paths.is_array())
							{
								for (const auto& pathValue : paths)
								{
									if (!pathValue.is_string()) { textures.push_back(nullptr); continue; }
									std::string texPath = resolveContentPath(pathValue.get<std::string>());
									int texId = assetManager.loadAsset(texPath, AssetType::Texture, AssetManager::Sync);
									if (texId == 0) { textures.push_back(nullptr); continue; }
									auto texAsset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(texId));
									if (!texAsset) { textures.push_back(nullptr); continue; }
									const auto& texData = texAsset->getData();
									if (!texData.is_object() || !texData.contains("m_width") || !texData.contains("m_height")
										|| !texData.contains("m_channels") || !texData.contains("m_data"))
									{
										textures.push_back(nullptr); continue;
									}
									auto texture = std::make_shared<Texture>();
										texture->setWidth(texData.at("m_width").get<int>());
										texture->setHeight(texData.at("m_height").get<int>());
										texture->setChannels(texData.at("m_channels").get<int>());
										texture->setData(texData.at("m_data").get<std::vector<unsigned char>>());
										if (auto tc = DiagnosticsManager::Instance().getState("TextureCompressionEnabled"))
										{
											if (*tc == "1")
												texture->setRequestCompression(true);
										}
										textures.push_back(std::move(texture));
								}
							}
						}
					}
				}
			}
				if (!materialCacheKey.empty())
					{
						m_materialDataCache[materialCacheKey] = { textures, shininess, metallic, roughness, specularMultiplier, pbrEnabled, fragmentShaderOverride };
					}
				}
			}

			// --- Resolve mesh ---
	std::string meshPath = resolveContentPath(meshComp->meshAssetPath);
	int assetId = assetManager.loadAsset(meshPath, AssetType::Model3D, AssetManager::Sync);
	if (assetId == 0)
		assetId = assetManager.loadAsset(meshPath, AssetType::Model2D, AssetManager::Sync);
	if (assetId == 0)
	{
		logger.log(Logger::Category::Rendering,
			"refreshEntityRenderable: failed to load mesh '" + meshComp->meshAssetPath + "'",
			Logger::LogLevel::WARNING);
		return result;
	}

	auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
	if (!asset)
		return result;

	result.entity = entity;
	result.asset = asset;
	result.textures = textures;
	result.assetType = asset->getAssetType();
	result.shininess = shininess;
	result.metallic = metallic;
	result.roughness = roughness;
	result.specularMultiplier = specularMultiplier;
	result.pbrEnabled = pbrEnabled;

	std::string effectiveFragShader = fragmentShaderOverride.empty() ? defaultFragmentShader : fragmentShaderOverride;
	result.fragmentShaderOverride = effectiveFragShader;

	if (transComp)
		result.transform = *transComp;

	const unsigned int meshCacheId = (asset->getId() != 0) ? asset->getId() : static_cast<unsigned int>(assetId);
	const std::string obj3DCacheKey = std::to_string(meshCacheId) + "|" + materialCacheKey + "|" + effectiveFragShader;

	if (result.assetType == AssetType::Model3D || result.assetType == AssetType::PointLight)
	{
		auto it = m_object3DCache.find(obj3DCacheKey);
		if (it != m_object3DCache.end())
		{
			result.object3D = it->second.lock();
			if (!result.object3D)
				m_object3DCache.erase(it);
		}
		if (!result.object3D)
		{
			auto obj = std::make_shared<OpenGLObject3D>(asset);
			obj->setMaterialCacheKeySuffix(materialCacheKey);
			if (!effectiveFragShader.empty())
				obj->setFragmentShaderOverride(effectiveFragShader);
			if (obj->prepare())
			{
				obj->setTextures(textures);
				obj->setShininess(shininess);
				obj->setPbrData(pbrEnabled, metallic, roughness);
				obj->setSpecularMultiplier(specularMultiplier);
				result.object3D = obj;
				m_object3DCache[obj3DCacheKey] = obj;
				AssetManager::Instance().registerRuntimeResource(obj);
			}
		}
	}
	else if (result.assetType == AssetType::Model2D)
	{
		auto it = m_object2DCache.find(meshCacheId);
		if (it != m_object2DCache.end())
		{
			result.object2D = it->second.lock();
			if (!result.object2D)
				m_object2DCache.erase(it);
		}
		if (!result.object2D)
		{
			auto obj = std::make_shared<OpenGLObject2D>(asset);
			if (obj->prepare())
			{
				obj->setTextures(textures);
				result.object2D = obj;
				if (meshCacheId != 0)
					m_object2DCache[meshCacheId] = obj;
				AssetManager::Instance().registerRuntimeResource(obj);
			}
		}
	}

	return result;
}
