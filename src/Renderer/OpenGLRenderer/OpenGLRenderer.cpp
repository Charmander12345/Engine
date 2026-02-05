#include "OpenGLRenderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Logger.h"
#include "OpenGLMaterial.h"
#include "OpenGLCamera.h"
#include "OpenGLObject2D.h"
#include "OpenGLObject3D.h"
#include "OpenGLTextRenderer.h"
#include "OpenGLShader.h"

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

    if (m_uiQuadVbo)
    {
        glDeleteBuffers(1, &m_uiQuadVbo);
        m_uiQuadVbo = 0;
    }
    if (m_uiQuadVao)
    {
        glDeleteVertexArrays(1, &m_uiQuadVao);
        m_uiQuadVao = 0;
    }
    if (m_uiQuadProgram)
    {
        glDeleteProgram(m_uiQuadProgram);
        m_uiQuadProgram = 0;
    }
    for (auto& entry : m_uiQuadPrograms)
    {
        if (entry.second)
        {
            glDeleteProgram(entry.second);
        }
    }
    m_uiQuadPrograms.clear();

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

    m_window = SDL_CreateWindow("Engine Project", 800, 600, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
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
        if (m_resourceManager.prepareActiveLevel())
        {
            DiagnosticsManager::Instance().setScenePrepared(true);
        }
    }

    {
        auto& ecs = ECS::ECSManager::Instance();
        m_renderEntries.clear();
        const auto renderables = m_resourceManager.buildRenderablesForSchema(ecs.getRenderSchema());
        m_renderEntries.reserve(renderables.size());
        for (const auto& renderable : renderables)
        {
            RenderEntry entry;
            entry.transform = renderable.transform;
            entry.object3D = renderable.object3D;
            entry.object2D = renderable.object2D;
            if (!entry.object3D && !entry.object2D)
            {
                continue;
            }
            m_renderEntries.push_back(std::move(entry));
        }
        m_cachedLevel = DiagnosticsManager::Instance().getActiveLevelSoft();
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

    renderWorld();
    renderUI();
}

void OpenGLRenderer::renderWorld()
{
    if (!m_initialized)
    {
        return;
    }

    m_renderEntries.erase(
        std::remove_if(m_renderEntries.begin(), m_renderEntries.end(),
            [this](const RenderEntry& entry) { return !isRenderEntryRelevant(entry); }),
        m_renderEntries.end());

    const Vec2 viewportSize = getViewportSize();
    m_uiManager.setAvailableViewportSize(viewportSize);
    const int width = static_cast<int>(viewportSize.x);
    const int height = static_cast<int>(viewportSize.y);
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

    if (m_cachedLevel != level)
    {
        m_resourceManager.clearCaches();
        m_renderEntries.clear();
        diagnostics.setScenePrepared(false);
        m_cachedLevel = level;
        m_textRenderer.reset();
    }

    if (!diagnostics.isScenePrepared())
    {
        if (m_resourceManager.prepareActiveLevel())
        {
            diagnostics.setScenePrepared(true);
        }

        auto& ecs = ECS::ECSManager::Instance();
        m_renderEntries.clear();
        const auto renderables = m_resourceManager.buildRenderablesForSchema(ecs.getRenderSchema());
        m_renderEntries.reserve(renderables.size());
        for (const auto& renderable : renderables)
        {
            RenderEntry entry;
            entry.transform = renderable.transform;
            entry.object3D = renderable.object3D;
            entry.object2D = renderable.object2D;
            if (!entry.object3D && !entry.object2D)
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

void OpenGLRenderer::renderUI()
{
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthEnabled)
    {
        glDisable(GL_DEPTH_TEST);
    }

    if (!m_textRenderer)
    {
        m_textRenderer = m_resourceManager.prepareTextRenderer();
    }

    if (!m_textRenderer)
    {
        m_textQueue.clear();
        return;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    m_textRenderer->setScreenSize(width, height);
    m_uiManager.setAvailableViewportSize(Vec2{ static_cast<float>(width), static_cast<float>(height) });

    m_uiManager.updateLayouts([this](const std::string& text, float scale)
        {
            return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{};
        });

    const auto& widgets = m_uiManager.getRegisteredWidgets();
    if (!widgets.empty() && ensureUIQuadRenderer())
    {
        std::vector<const UIManager::WidgetEntry*> ordered;
        ordered.reserve(widgets.size());
        for (const auto& entry : widgets)
        {
            ordered.push_back(&entry);
        }

        std::sort(ordered.begin(), ordered.end(), [](const UIManager::WidgetEntry* a, const UIManager::WidgetEntry* b)
        {
            const int za = (a && a->widget) ? a->widget->getZOrder() : 0;
            const int zb = (b && b->widget) ? b->widget->getZOrder() : 0;
            return za < zb;
        });

        const glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
        const std::filesystem::path shadersDir = std::filesystem::current_path() / "shaders";
        const std::string defaultPanelVertex = (shadersDir / "panel_vertex.glsl").string();
        const std::string defaultPanelFragment = (shadersDir / "panel_fragment.glsl").string();
        const std::string defaultButtonVertex = (shadersDir / "button_vertex.glsl").string();
        const std::string defaultButtonFragment = (shadersDir / "button_fragment.glsl").string();
        const std::string defaultTextVertex = (shadersDir / "text_vertex.glsl").string();
        const std::string defaultTextFragment = (shadersDir / "text_fragment.glsl").string();

        const auto resolveShaderPath = [&](const std::string& value, const std::string& fallback)
        {
            if (value.empty())
            {
                return fallback;
            }
            std::filesystem::path p(value);
            if (p.is_relative())
            {
                p = shadersDir / p;
            }
            return p.string();
        };

        const auto renderElement = [&](const auto& self, const WidgetElement& element,
            float parentX, float parentY, float parentW, float parentH) -> void
        {
            float x0 = element.hasComputedPosition ? element.computedPositionPixels.x : (parentX + element.from.x * parentW);
            float y0 = element.hasComputedPosition ? element.computedPositionPixels.y : (parentY + element.from.y * parentH);
            float widthPx = element.hasComputedSize ? element.computedSizePixels.x : (parentW * (element.to.x - element.from.x));
            float heightPx = element.hasComputedSize ? element.computedSizePixels.y : (parentH * (element.to.y - element.from.y));
            const float x1 = x0 + widthPx;
            const float y1 = y0 + heightPx;

            if (element.type == WidgetElementType::Panel)
            {
                const std::string vertexPath = resolveShaderPath(element.shaderVertex, defaultPanelVertex);
                const std::string fragmentPath = resolveShaderPath(element.shaderFragment, defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program);
                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 1.0f, 0.9f, 0.1f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::Text)
            {
                const float heightPx = std::max(0.0f, y1 - y0);
                const float widthPx = std::max(0.0f, x1 - x0);
                float scale = 1.0f;
                if (element.fontSize > 0.0f)
                {
                    scale = element.fontSize / 48.0f;
                }
                else
                {
                    const Vec2 textSize = m_textRenderer->measureText(element.text, 1.0f);
                    if (textSize.x > 0.0f && textSize.y > 0.0f)
                    {
                        const float scaleX = (widthPx > 0.0f) ? (widthPx / textSize.x) : 1.0f;
                        const float scaleY = (heightPx > 0.0f) ? (heightPx / textSize.y) : 1.0f;
                        scale = std::min(scaleX, scaleY) * 0.9f;
                    }
                    else if (heightPx > 0.0f)
                    {
                        scale = heightPx / 48.0f;
                    }
                }
                const std::string vertexPath = resolveShaderPath(element.shaderVertex, defaultTextVertex);
                const std::string fragmentPath = resolveShaderPath(element.shaderFragment, defaultTextFragment);
                if (!element.shaderVertex.empty() || !element.shaderFragment.empty())
                {
                    m_textRenderer->drawTextWithShader(element.text, Vec2{ x0, y0 }, scale, element.color,
                        vertexPath, fragmentPath);
                }
                else
                {
                    m_textRenderer->drawText(element.text, Vec2{ x0, y0 }, scale, element.color);
                }
                if (m_uiDebugEnabled)
                {
                    const GLuint program = getUIQuadProgram(defaultPanelVertex, defaultPanelFragment);
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.1f, 0.8f, 1.0f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::Button)
            {
                const float widthPx = std::max(0.0f, x1 - x0);
                const float heightPx = std::max(0.0f, y1 - y0);
                const float sizePx = std::min(widthPx, heightPx);
                const float bx0 = x0 + (widthPx - sizePx) * 0.5f;
                const float by0 = y0 + (heightPx - sizePx) * 0.5f;
                const float bx1 = bx0 + sizePx;
                const float by1 = by0 + sizePx;

                const std::string vertexPath = resolveShaderPath(element.shaderVertex, defaultButtonVertex);
                const std::string fragmentPath = resolveShaderPath(element.shaderFragment, defaultButtonFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(bx0, by0, bx1, by1, element.color, uiProjection, program);

                if (!element.text.empty())
                {
                    const float contentX0 = bx0 + element.padding.x;
                    const float contentY0 = by0 + element.padding.y;
                    const float contentX1 = bx1 - element.padding.x;
                    const float contentY1 = by1 - element.padding.y;
                    const float contentW = std::max(0.0f, contentX1 - contentX0);
                    const float contentH = std::max(0.0f, contentY1 - contentY0);

                    float scale = 1.0f;
                    if (element.fontSize > 0.0f)
                    {
                        scale = element.fontSize / 48.0f;
                    }
                    else
                    {
                        const Vec2 textSize = m_textRenderer->measureText(element.text, 1.0f);
                        if (textSize.x > 0.0f && textSize.y > 0.0f)
                        {
                            const float scaleX = (contentW > 0.0f) ? (contentW / textSize.x) : 1.0f;
                            const float scaleY = (contentH > 0.0f) ? (contentH / textSize.y) : 1.0f;
                            scale = std::min(scaleX, scaleY) * 0.9f;
                        }
                    }

                    Vec2 textSize = m_textRenderer->measureText(element.text, scale);
                    float textX = contentX0;
                    float textY = contentY0;

                    switch (element.textAlignH)
                    {
                    case TextAlignH::Center:
                        textX = contentX0 + (contentW - textSize.x) * 0.5f;
                        break;
                    case TextAlignH::Right:
                        textX = contentX1 - textSize.x;
                        break;
                    default:
                        textX = contentX0;
                        break;
                    }

                    switch (element.textAlignV)
                    {
                    case TextAlignV::Center:
                        textY = contentY0 + (contentH - textSize.y) * 0.5f;
                        break;
                    case TextAlignV::Bottom:
                        textY = contentY1 - textSize.y;
                        break;
                    default:
                        textY = contentY0;
                        break;
                    }

                    m_textRenderer->drawText(element.text, Vec2{ textX, textY }, scale, element.textColor);
                }
                if (m_uiDebugEnabled)
                {
                    drawUIOutline(bx0, by0, bx1, by1, Vec4{ 0.7f, 1.0f, 0.3f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::StackPanel)
            {
                for (const auto& child : element.children)
                {
                    self(self, child, x0, y0, widthPx, heightPx);
                }
                if (m_uiDebugEnabled)
                {
                    const GLuint program = getUIQuadProgram(defaultPanelVertex, defaultPanelFragment);
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 1.0f, 0.4f, 0.8f, 1.0f }, uiProjection, program);
                }
            }
        };

        for (const auto* entry : ordered)
        {
            if (!entry || !entry->widget)
            {
                continue;
            }

            const auto& widget = entry->widget;
            Vec2 widgetSize = widget->getSizePixels();
            if (widget->hasComputedSize())
            {
                widgetSize = widget->getComputedSizePixels();
            }
            if (widgetSize.x <= 0.0f)
            {
                widgetSize.x = static_cast<float>(width);
            }
            if (widgetSize.y <= 0.0f)
            {
                widgetSize.y = static_cast<float>(height);
            }

            for (const auto& element : widget->getElements())
            {
                renderElement(renderElement, element, 0.0f, 0.0f, widgetSize.x, widgetSize.y);
            }
        }
    }

    Vec2 viewportSize = m_uiManager.getAvailableViewportSize();
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        viewportSize = Vec2{ static_cast<float>(width), static_cast<float>(height) };
    }

    if (!m_textQueue.empty())
    {
        m_textRenderer->setScreenSize(static_cast<int>(viewportSize.x), static_cast<int>(viewportSize.y));
    }

    for (const auto& command : m_textQueue)
    {
        Vec2 pixelPos{
            command.screenPos.x * viewportSize.x,
            command.screenPos.y * viewportSize.y
        };
        m_textRenderer->drawText(command.text, pixelPos, command.scale, command.color);
    }

    m_textQueue.clear();

    if (depthEnabled)
    {
        glEnable(GL_DEPTH_TEST);
    }
}

void OpenGLRenderer::moveCamera(float forward, float right, float up)
{
    if (!m_camera)
        return;
    m_camera->moveRelative(forward, right, up);
}

void OpenGLRenderer::queueText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color)
{
    TextCommand command;
    command.text = text;
    command.screenPos = screenPos;
    command.scale = scale;
    command.color = color;
    m_textQueue.push_back(std::move(command));
}

bool OpenGLRenderer::ensureUIQuadRenderer()
{
    if (m_uiQuadProgram != 0)
    {
        return true;
    }

    glGenVertexArrays(1, &m_uiQuadVao);
    glGenBuffers(1, &m_uiQuadVbo);
    glBindVertexArray(m_uiQuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return true;
}

GLuint OpenGLRenderer::getUIQuadProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
    if (vertexShaderPath.empty() || fragmentShaderPath.empty())
    {
        return m_uiQuadProgram;
    }

    const std::string key = vertexShaderPath + "|" + fragmentShaderPath;
    auto it = m_uiQuadPrograms.find(key);
    if (it != m_uiQuadPrograms.end())
    {
        return it->second;
    }

    auto vertex = std::make_shared<OpenGLShader>();
    auto fragment = std::make_shared<OpenGLShader>();

    if (!vertex->loadFromFile(Shader::Type::Vertex, vertexShaderPath) ||
        !fragment->loadFromFile(Shader::Type::Fragment, fragmentShaderPath))
    {
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex->id());
    glAttachShader(program, fragment->id());
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Logger::Instance().log("OpenGLRenderer: Failed to link UI quad shader", Logger::LogLevel::ERROR);
        glDeleteProgram(program);
        return 0;
    }

    m_uiQuadPrograms[key] = program;
    if (m_uiQuadProgram == 0)
    {
        m_uiQuadProgram = program;
    }
    return program;
}

void OpenGLRenderer::drawUIPanel(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program)
{
    if (program == 0 || m_uiQuadVao == 0)
    {
        return;
    }

    const glm::vec4 glColor{ color.x, color.y, color.z, color.w };

    float vertices[6][2] = {
        { x0, y1 },
        { x0, y0 },
        { x1, y0 },
        { x0, y1 },
        { x1, y0 },
        { x1, y1 }
    };

    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uProjection"), 1, GL_FALSE, &projection[0][0]);
    glUniform4fv(glGetUniformLocation(program, "uColor"), 1, &glColor[0]);

    glBindVertexArray(m_uiQuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void OpenGLRenderer::drawUIOutline(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program)
{
    if (program == 0 || m_uiQuadVao == 0)
    {
        return;
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(1.0f);
    drawUIPanel(x0, y0, x1, y1, color, projection, program);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

Vec2 OpenGLRenderer::getViewportSize() const
{
    int width = 0;
    int height = 0;
    if (m_window)
    {
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
    }

    return Vec2{ static_cast<float>(width), static_cast<float>(height) };
}

UIManager& OpenGLRenderer::getUIManager()
{
    return m_uiManager;
}

const UIManager& OpenGLRenderer::getUIManager() const
{
    return m_uiManager;
}

std::shared_ptr<Widget> OpenGLRenderer::createWidgetFromAsset(const std::shared_ptr<AssetData>& asset)
{
    return m_resourceManager.buildWidgetAsset(asset);
}

bool OpenGLRenderer::isRenderEntryRelevant(const RenderEntry& entry) const
{
    (void)entry;
    return true;
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