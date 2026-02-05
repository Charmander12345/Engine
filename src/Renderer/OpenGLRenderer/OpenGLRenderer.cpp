#include "OpenGLRenderer.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Logger.h"
#include "OpenGLMaterial.h"
#include "OpenGLCamera.h"
#include "OpenGLObject2D.h"
#include "OpenGLObject3D.h"

#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Core/EngineLevel.h"

#include "../RenderResourceManager.h"

#include "../../Core/Asset.h"
#include "../../Core/MathTypes.h"
#include "../../AssetManager/AssetManager.h"
#include "../Texture.h"

OpenGLRenderer::OpenGLRenderer()
{
    m_initialized = false;
    m_name = "OpenGL Renderer";
    m_window = nullptr;
    m_glContext = nullptr;

    m_camera = std::make_unique<OpenGLCamera>();

    // Initialize projection matrix (will be updated with proper aspect ratio)
    m_projectionMatrix = glm::perspective(
        glm::radians(45.0f),  // FOV
        800.0f / 600.0f,      // Aspect ratio (will be updated)
        0.1f,                 // Near plane
        100.0f                // Far plane
    );

    // Note: any initial camera offset should be expressed as initial camera position.
}

void OpenGLRenderer::shutdown()
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Rendering, "OpenGLRenderer shutdown: releasing GPU resources...", Logger::LogLevel::INFO);

    // Ensure GL context is current so destructors can safely delete GL objects.
    if (m_window && m_glContext)
    {
        SDL_GL_MakeCurrent(m_window, m_glContext);
    }

    OpenGLObject2D::ClearCache();
    OpenGLObject3D::ClearCache();

    // Any additional renderer-owned GPU resources can be released here.
    m_camera.reset();

    logger.log(Logger::Category::Rendering, "OpenGLRenderer shutdown: done.", Logger::LogLevel::INFO);
}

OpenGLRenderer::~OpenGLRenderer()
{
    SDL_GL_DestroyContext(m_glContext);
    SDL_DestroyWindow(m_window);
}

bool OpenGLRenderer::initialize()
{
    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Rendering, "OpenGLRenderer initialize started", Logger::LogLevel::INFO);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    logger.log(Logger::Category::Rendering, "Creating Window...", Logger::LogLevel::INFO);

    m_window = SDL_CreateWindow("Engine Project", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_MINIMIZED);
    if (!m_window) {
        logger.log(Logger::Category::Rendering, std::string("Failed to create window: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_Quit();
        return false;
    }
    logger.log(Logger::Category::Rendering, "Window created successfully.", Logger::LogLevel::INFO);
    logger.log(Logger::Category::Rendering, "Creating OpenGL context...", Logger::LogLevel::INFO);

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        logger.log(Logger::Category::Rendering, std::string("Failed to create OpenGL context: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    if (!SDL_GL_MakeCurrent(m_window, m_glContext)) {
        logger.log(Logger::Category::Rendering, std::string("Failed to make created context current: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_GL_DestroyContext(m_glContext);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    logger.log(Logger::Category::Rendering, "OpenGL context created successfully.", Logger::LogLevel::INFO);

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        logger.log(Logger::Category::Rendering, "Failed to initialize GLAD", Logger::LogLevel::ERROR);
        return false;
    }

    logger.log(Logger::Category::Rendering, "GLAD initialised.", Logger::LogLevel::INFO);

    int nrAttributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
    logger.log(Logger::Category::Rendering, "Max number of vertex attributes: " + std::to_string(nrAttributes), Logger::LogLevel::INFO);

    glEnable(GL_DEPTH_TEST);

    m_initialized = true;
    logger.log(Logger::Category::Rendering, "Initialisation of the OpenGL renderer complete.", Logger::LogLevel::INFO);

    // Prime render resources as soon as the renderer is ready.
    {
        RenderResourceManager rrm;
        if (rrm.prepareActiveLevel())
        {
            DiagnosticsManager::Instance().setScenePrepared(true);
        }
    }

    {
        RenderResourceManager rrm;
        auto& ecs = ECS::ECSManager::Instance();
        m_renderEntries.clear();
        const auto renderables = rrm.buildRenderablesForSchema(ecs.getRenderSchema());
        m_renderEntries.reserve(renderables.size());
        for (const auto& renderable : renderables)
        {
            RenderEntry entry;
            entry.transform = renderable.transform;
            if (renderable.assetType == AssetType::Model3D)
            {
                entry.object3D = std::make_shared<OpenGLObject3D>(renderable.asset);
                if (!entry.object3D->prepare())
                {
                    continue;
                }
                entry.object3D->setTextures(renderable.textures);
            }
            else if (renderable.assetType == AssetType::Model2D)
            {
                entry.object2D = std::make_shared<OpenGLObject2D>(renderable.asset);
                if (!entry.object2D->prepare())
                {
                    continue;
                }
                entry.object2D->setTextures(renderable.textures);
            }
            else
            {
                continue;
            }
            m_renderEntries.push_back(std::move(entry));
        }
    }

    SDL_ShowWindow(m_window);
    SDL_RestoreWindow(m_window);
    return true;
}

void OpenGLRenderer::clear()
{
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::present()
{
    if (m_window)
    {
        SDL_GL_SwapWindow(m_window);
    }
}

void OpenGLRenderer::render()
{
    if (!m_initialized)
    {
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    glViewport(0, 0, width, height);

    // Update projection matrix with current aspect ratio
    if (height > 0)
    {
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        m_projectionMatrix = glm::perspective(
            glm::radians(45.0f),
            aspectRatio,
            0.1f,
            100.0f
        );
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    EngineLevel* level = diagnostics.getActiveLevelSoft();
    if (!level)
	{
		Logger::Instance().log(Logger::Category::Rendering, "No active level to render.", Logger::LogLevel::WARNING);
        return;
	}

    if (!diagnostics.isScenePrepared())
    {
        RenderResourceManager rrm;
        if (rrm.prepareActiveLevel())
        {
            diagnostics.setScenePrepared(true);
        }

        auto& ecs = ECS::ECSManager::Instance();
        m_renderEntries.clear();
        const auto renderables = rrm.buildRenderablesForSchema(ecs.getRenderSchema());
        m_renderEntries.reserve(renderables.size());
        for (const auto& renderable : renderables)
        {
            RenderEntry entry;
            entry.transform = renderable.transform;
            if (renderable.assetType == AssetType::Model3D)
            {
                entry.object3D = std::make_shared<OpenGLObject3D>(renderable.asset);
                if (!entry.object3D->prepare())
                {
                    continue;
                }
                entry.object3D->setTextures(renderable.textures);
            }
            else if (renderable.assetType == AssetType::Model2D)
            {
                entry.object2D = std::make_shared<OpenGLObject2D>(renderable.asset);
                if (!entry.object2D->prepare())
                {
                    continue;
                }
                entry.object2D->setTextures(renderable.textures);
            }
            else
            {
                continue;
            }
            m_renderEntries.push_back(std::move(entry));
        }
    }

    glm::mat4 view(1.0f);
    if (m_camera)
    {
        Mat4 engineView = m_camera->getViewMatrixColumnMajor();
        view = glm::make_mat4(engineView.m);
    }

    if (!m_renderEntries.empty())
    {
        for (const auto& entry : m_renderEntries)
        {
            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));

            if (entry.object3D)
            {
                entry.object3D->setMatrices(modelMatrix, view, m_projectionMatrix);
                entry.object3D->render();
            }
            else if (entry.object2D)
            {
                entry.object2D->setMatrices(modelMatrix, view, m_projectionMatrix);
                entry.object2D->render();
            }
        }
    }

    const auto& objs = level->getWorldObjects();
	const auto& groups = level->getGroups();
    const auto& ecsEntities = level->getEntities();

    if (!objs.empty())
    {
        for (const auto& entry : objs)
        {
            if (!entry || entry->object->getPath().empty())
                continue;

            Transform* t = &entry->transform;

            auto asset = entry->object ? std::dynamic_pointer_cast<AssetData>(entry->object) : nullptr;
            if (!asset)
                continue;

            if (asset->getAssetType() == AssetType::Model3D)
            {
                OpenGLObject3D glObj(asset);
                if (!glObj.prepare())
                    continue;

                if (t)
                {
                    Mat4 engineMat = t->getMatrix4ColumnMajor();
                    glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                    modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 1.0f, 0.0f));
                    glObj.setMatrices(modelMatrix, view, m_projectionMatrix);
                }

    if (!ecsEntities.empty())
    {
        ECS::Schema meshSchema;
        meshSchema.require<ECS::MeshComponent>();
        auto& ecs = ECS::ECSManager::Instance();
        const auto matches = ecs.getEntitiesMatchingSchema(meshSchema);
        if (!matches.empty())
        {
            auto& assetManager = AssetManager::Instance();
            for (const auto entity : matches)
            {
                const auto* meshComponent = ecs.getComponent<ECS::MeshComponent>(entity);
                if (!meshComponent || meshComponent->meshAssetPath.empty())
                {
                    continue;
                }

                std::vector<std::shared_ptr<Texture>> textures;
                if (const auto* materialComponent = ecs.getComponent<ECS::MaterialComponent>(entity))
                {
                    if (!materialComponent->materialAssetPath.empty())
                    {
                        std::string materialPath = materialComponent->materialAssetPath;
                        const std::filesystem::path matFs(materialPath);
                        if (!matFs.is_absolute())
                        {
                            const auto absPath = assetManager.getAbsoluteContentPath(materialPath);
                            if (!absPath.empty())
                            {
                                materialPath = absPath;
                            }
                        }

                        int matId = assetManager.loadAsset(materialPath, AssetType::Material, AssetManager::Sync);
                        if (matId != 0)
                        {
                            if (auto matAsset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(matId)))
                            {
                                const auto& matData = matAsset->getData();
                                if (matData.is_object() && matData.contains("m_textureAssetPaths"))
                                {
                                    const auto& paths = matData.at("m_textureAssetPaths");
                                    if (paths.is_array())
                                    {
                                        for (const auto& pathValue : paths)
                                        {
                                            if (!pathValue.is_string())
                                            {
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
                                                continue;
                                            }
                                            auto texAsset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(texId));
                                            if (!texAsset)
                                            {
                                                continue;
                                            }
                                            const auto& texData = texAsset->getData();
                                            if (!texData.is_object())
                                            {
                                                continue;
                                            }
                                            if (!texData.contains("m_width") || !texData.contains("m_height") || !texData.contains("m_channels") || !texData.contains("m_data"))
                                            {
                                                continue;
                                            }
                                            auto texture = std::make_shared<Texture>();
                                            texture->setWidth(texData.at("m_width").get<int>());
                                            texture->setHeight(texData.at("m_height").get<int>());
                                            texture->setChannels(texData.at("m_channels").get<int>());
                                            texture->setData(texData.at("m_data").get<std::vector<unsigned char>>());
                                            textures.push_back(std::move(texture));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                std::string meshPath = meshComponent->meshAssetPath;
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
                    continue;
                }

                auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
                if (!asset)
                {
                    continue;
                }

                ECS::TransformComponent transform{};
                if (const auto* transformComponent = ecs.getComponent<ECS::TransformComponent>(entity))
                {
                    transform = *transformComponent;
                }

                glm::mat4 modelMatrix(1.0f);
                modelMatrix = glm::translate(modelMatrix, glm::vec3(transform.position[0], transform.position[1], transform.position[2]));
                modelMatrix = glm::rotate(modelMatrix, glm::radians(transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                modelMatrix = glm::rotate(modelMatrix, glm::radians(transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
                modelMatrix = glm::rotate(modelMatrix, glm::radians(transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
                modelMatrix = glm::scale(modelMatrix, glm::vec3(transform.scale[0], transform.scale[1], transform.scale[2]));

                if (asset->getAssetType() == AssetType::Model3D)
                {
                    OpenGLObject3D glObj(asset);
                    if (!glObj.prepare())
                    {
                        continue;
                    }
                    glObj.setTextures(textures);
                    glObj.setMatrices(modelMatrix, view, m_projectionMatrix);
                    glObj.render();
                }
                else if (asset->getAssetType() == AssetType::Model2D)
                {
                    OpenGLObject2D glObj(asset);
                    if (!glObj.prepare())
                    {
                        continue;
                    }
                    glObj.setTextures(textures);
                    glObj.setMatrices(modelMatrix, view, m_projectionMatrix);
                    glObj.render();
                }
            }
        }
    }
                glObj.render();
            }
            else if (asset->getAssetType() == AssetType::Model2D)
            {
                OpenGLObject2D glObj(asset);
                if (!glObj.prepare())
                    continue;

                if (t)
                {
                    Mat4 engineMat = t->getMatrix4ColumnMajor();
                    glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                    modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 0.0f, 1.0f));
                    glObj.setMatrices(modelMatrix, view, m_projectionMatrix);
                }
                glObj.render();
            }
        }
    }
	if (!groups.empty())
    {
        for (const auto& groupEntry : groups)
        {
            const auto& groupObjects = groupEntry.objects;
            for (size_t i = 0; i < groupObjects.size(); ++i)
            {
                const auto& entry = groupObjects[i];
                if (!entry || entry->getPath().empty())
                    continue;
				Transform* t = (i < groupEntry.transforms.size()) ? const_cast<Transform*>(&groupEntry.transforms[i]) : nullptr;
                auto asset = entry ? std::dynamic_pointer_cast<AssetData>(entry) : nullptr;
                if (!asset)
                    continue;

                if (asset->getAssetType() == AssetType::Model3D)
                {
                    OpenGLObject3D glObj(asset);
                    if (!glObj.prepare())
                        continue;

                    if (t)
                    {
                        Mat4 engineMat = t->getMatrix4ColumnMajor();
                        glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                        glObj.setMatrices(modelMatrix, view, m_projectionMatrix);
                    }
                    glObj.render();
                }
                else if (asset->getAssetType() == AssetType::Model2D)
                {
                    OpenGLObject2D glObj(asset);
                    if (!glObj.prepare())
                        continue;

                    if (t)
                    {
                        Mat4 engineMat = t->getMatrix4ColumnMajor();
                        glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                        glObj.setMatrices(modelMatrix, view, m_projectionMatrix);
                    }
                    glObj.render();
                }
            }
        }
    }
}

void OpenGLRenderer::moveCamera(float forward, float right, float up)
{
    if (!m_camera)
        return;
    m_camera->moveRelative(forward, right, up);
}

void OpenGLRenderer::rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees)
{
    if (!m_camera)
        return;
    m_camera->rotate(yawDeltaDegrees, pitchDeltaDegrees);
}

const std::string& OpenGLRenderer::name() const
{
    return m_name;
}

SDL_Window* OpenGLRenderer::window() const
{
    return m_window;
}