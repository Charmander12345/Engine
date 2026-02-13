#include "OpenGLRenderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <cmath>
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

namespace
{
    Vec4 HsvToRgb(float hue, float saturation, float value)
    {
        hue = std::fmod(std::max(0.0f, hue), 1.0f) * 6.0f;
        const float c = value * saturation;
        const float x = c * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
        const float m = value - c;

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if (hue < 1.0f)
        {
            r = c;
            g = x;
        }
        else if (hue < 2.0f)
        {
            r = x;
            g = c;
        }
        else if (hue < 3.0f)
        {
            g = c;
            b = x;
        }
        else if (hue < 4.0f)
        {
            g = x;
            b = c;
        }
        else if (hue < 5.0f)
        {
            r = x;
            b = c;
        }
        else
        {
            r = c;
            b = x;
        }

        return Vec4{ r + m, g + m, b + m, 1.0f };
    }

    struct FrustumPlane
    {
        glm::vec3 normal{0.0f};
        float d{0.0f};
    };

    std::array<FrustumPlane, 6> ExtractFrustumPlanes(const glm::mat4& matrix)
    {
        std::array<FrustumPlane, 6> planes{};

        auto makePlane = [&](float a, float b, float c, float d)
        {
            FrustumPlane plane;
            plane.normal = glm::vec3(a, b, c);
            const float length = glm::length(plane.normal);
            if (length > 0.0f)
            {
                plane.normal /= length;
                plane.d = d / length;
            }
            else
            {
                plane.d = d;
            }
            return plane;
        };

        planes[0] = makePlane(matrix[0][3] + matrix[0][0], matrix[1][3] + matrix[1][0], matrix[2][3] + matrix[2][0], matrix[3][3] + matrix[3][0]); // Left
        planes[1] = makePlane(matrix[0][3] - matrix[0][0], matrix[1][3] - matrix[1][0], matrix[2][3] - matrix[2][0], matrix[3][3] - matrix[3][0]); // Right
        planes[2] = makePlane(matrix[0][3] + matrix[0][1], matrix[1][3] + matrix[1][1], matrix[2][3] + matrix[2][1], matrix[3][3] + matrix[3][1]); // Bottom
        planes[3] = makePlane(matrix[0][3] - matrix[0][1], matrix[1][3] - matrix[1][1], matrix[2][3] - matrix[2][1], matrix[3][3] - matrix[3][1]); // Top
        planes[4] = makePlane(matrix[0][3] + matrix[0][2], matrix[1][3] + matrix[1][2], matrix[2][3] + matrix[2][2], matrix[3][3] + matrix[3][2]); // Near
        planes[5] = makePlane(matrix[0][3] - matrix[0][2], matrix[1][3] - matrix[1][2], matrix[2][3] - matrix[2][2], matrix[3][3] - matrix[3][2]); // Far

        return planes;
    }

    bool IsSphereInsideFrustum(const std::array<FrustumPlane, 6>& planes, const glm::vec3& center, float radius)
    {
        for (const auto& plane : planes)
        {
            const float distance = glm::dot(plane.normal, center) + plane.d;
            if (distance < -radius)
            {
                return false;
            }
        }
        return true;
    }

    float MaxScale(float sx, float sy, float sz)
    {
        return std::max(sx, std::max(sy, sz));
    }

    void ComputeWorldAabb(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& model, glm::vec3& outMin, glm::vec3& outMax)
    {
        const glm::vec3 corners[8] = {
            glm::vec3(localMin.x, localMin.y, localMin.z),
            glm::vec3(localMax.x, localMin.y, localMin.z),
            glm::vec3(localMax.x, localMax.y, localMin.z),
            glm::vec3(localMin.x, localMax.y, localMin.z),
            glm::vec3(localMin.x, localMin.y, localMax.z),
            glm::vec3(localMax.x, localMin.y, localMax.z),
            glm::vec3(localMax.x, localMax.y, localMax.z),
            glm::vec3(localMin.x, localMax.y, localMax.z)
        };

        glm::vec3 minPos(std::numeric_limits<float>::max());
        glm::vec3 maxPos(std::numeric_limits<float>::lowest());
        for (const auto& corner : corners)
        {
            const glm::vec4 world = model * glm::vec4(corner, 1.0f);
            const glm::vec3 pos = glm::vec3(world);
            minPos = glm::min(minPos, pos);
            maxPos = glm::max(maxPos, pos);
        }
        outMin = minPos;
        outMax = maxPos;
    }

    bool IsAabbInsideFrustum(const std::array<FrustumPlane, 6>& planes, const glm::vec3& minBounds, const glm::vec3& maxBounds)
    {
        for (const auto& plane : planes)
        {
            glm::vec3 positive = minBounds;
            if (plane.normal.x >= 0.0f) positive.x = maxBounds.x;
            if (plane.normal.y >= 0.0f) positive.y = maxBounds.y;
            if (plane.normal.z >= 0.0f) positive.z = maxBounds.z;
            if (glm::dot(plane.normal, positive) + plane.d < 0.0f)
            {
                return false;
            }
        }
        return true;
    }

    static SDL_HitTestResult SDLCALL WindowHitTestCallback(SDL_Window* window, const SDL_Point* area, void* data)
    {
        if (!window || !area || !data)
        {
            return SDL_HITTEST_NORMAL;
        }

        const auto* ctx = static_cast<WindowHitTestContext*>(data);
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        const int x = area->x;
        const int y = area->y;

        if (y < ctx->buttonStripHeight && x >= width - ctx->buttonStripWidth)
        {
            return SDL_HITTEST_NORMAL;
        }

        const bool left = x < ctx->resizeBorder;
        const bool right = x >= width - ctx->resizeBorder;
        const bool top = y < ctx->resizeBorder;
        const bool bottom = y >= height - ctx->resizeBorder;

        if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
        if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
        if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        if (left) return SDL_HITTEST_RESIZE_LEFT;
        if (right) return SDL_HITTEST_RESIZE_RIGHT;
        if (top) return SDL_HITTEST_RESIZE_TOP;
        if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;

        if (y < ctx->titlebarHeight)
        {
            return SDL_HITTEST_DRAGGABLE;
        }

        return SDL_HITTEST_NORMAL;
    }

}

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
    if (m_gpuQueriesInitialized)
    {
        glDeleteQueries(static_cast<GLsizei>(m_gpuTimerQueries.size()), m_gpuTimerQueries.data());
        m_gpuQueriesInitialized = false;
        m_gpuTimerQueries.fill(0);
    }
    releaseOcclusionResources();
    releaseBoundsDebugResources();
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

	auto& diagnostics = DiagnosticsManager::Instance();
	Vec2 windowSize = diagnostics.getWindowSize();
	int width = static_cast<int>(windowSize.x);
	int height = static_cast<int>(windowSize.y);
	DiagnosticsManager::WindowState windowState = diagnostics.getWindowState();

    m_window = SDL_CreateWindow("Engine Project", width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE);
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

    glGenQueries(static_cast<GLsizei>(m_gpuTimerQueries.size()), m_gpuTimerQueries.data());
    m_gpuQueriesInitialized = true;

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
            entry.entity = renderable.entity;
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
    switch (windowState)
    {
    case DiagnosticsManager::WindowState::Fullscreen:
        SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN);
        break;
    case DiagnosticsManager::WindowState::Normal:
        SDL_RestoreWindow(m_window);
        break;
    case DiagnosticsManager::WindowState::Maximized:
        SDL_MaximizeWindow(m_window);
        break;
    }

    SDL_ClearError();
    if (SDL_SetWindowHitTest(m_window, WindowHitTestCallback, &m_hitTestContext) != 0)
    {
        const char* err = SDL_GetError();
        std::string msg = "Failed to set window hit test";
        if (err && *err)
        {
            msg += ": ";
            msg += err;
        }
        else
        {
            msg += ": (no SDL error string)";
        }
        logger.log(Logger::Category::Rendering, msg, Logger::LogLevel::WARNING);
    }
    else
    {
        logger.log(Logger::Category::Rendering, "Window hit test enabled.", Logger::LogLevel::INFO);
    }
    return true;
}

void OpenGLRenderer::clear()
{
    glClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::setClearColor(const Vec4& color)
{
    m_clearColor = color;
}

const Vec4& OpenGLRenderer::getClearColor() const
{
    return m_clearColor;
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

    ++m_frameIndex;

    const uint64_t freq = SDL_GetPerformanceFrequency();

    if (m_gpuQueriesInitialized)
    {
        glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuQueryIndex]);
    }

    const uint64_t worldStart = SDL_GetPerformanceCounter();
    renderWorld();
    const uint64_t worldEnd = SDL_GetPerformanceCounter();
    m_cpuRenderWorldMs = (freq > 0) ? (static_cast<double>(worldEnd - worldStart) * 1000.0 / static_cast<double>(freq)) : 0.0;

    renderUI();

    if (m_gpuQueriesInitialized)
    {
        glEndQuery(GL_TIME_ELAPSED);
        const size_t readIndex = (m_gpuQueryIndex + m_gpuTimerQueries.size() - 1) % m_gpuTimerQueries.size();
        GLuint available = 0;
        glGetQueryObjectuiv(m_gpuTimerQueries[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available != 0)
        {
            GLuint64 elapsedNs = 0;
            glGetQueryObjectui64v(m_gpuTimerQueries[readIndex], GL_QUERY_RESULT, &elapsedNs);
            m_lastGpuFrameMs = static_cast<double>(elapsedNs) / 1'000'000.0;
        }
        m_gpuQueryIndex = (m_gpuQueryIndex + 1) % m_gpuTimerQueries.size();
    }
}

void OpenGLRenderer::renderWorld()
{
    if (!m_initialized)
    {
        return;
    }

    m_lastVisibleCount = 0;
    m_lastHiddenCount = 0;
    m_lastTotalCount = 0;

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
        m_meshEntries.clear();
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

        ECS::Schema meshSchema;
        meshSchema.require<ECS::MeshComponent>().require<ECS::TransformComponent>();
        m_meshEntries.clear();
        const auto meshRenderables = m_resourceManager.buildRenderablesForSchema(meshSchema);
        m_meshEntries.reserve(meshRenderables.size());
        for (const auto& renderable : meshRenderables)
        {
            RenderEntry entry;
            entry.entity = renderable.entity;
            entry.transform = renderable.transform;
            entry.object3D = renderable.object3D;
            entry.object2D = renderable.object2D;
            if (!entry.object3D && !entry.object2D)
            {
                continue;
            }
            m_meshEntries.push_back(std::move(entry));
        }
    }

    glm::mat4 view(1.0f);
    if (m_camera)
    {
        Mat4 engineView = m_camera->getViewMatrixColumnMajor();
        view = glm::make_mat4(engineView.m);
    }
    const glm::mat4 viewProj = m_projectionMatrix * view;
    const auto frustumPlanes = ExtractFrustumPlanes(viewProj);

    if (m_occlusionEnabled)
    {
        updateOcclusionResults();
    }


    const uint64_t ecsStartCounter = SDL_GetPerformanceCounter();
    auto& ecs = ECS::ECSManager::Instance();
    glm::vec3 lightPosition{ 0.0f, 1.2f, 0.0f };
    glm::vec3 lightColor{ 1.0f, 1.0f, 1.0f };
    float lightIntensity = 1.0f;

    if (!m_lightSchemaInitialized)
    {
        m_lightSchema.require<ECS::LightComponent>().require<ECS::TransformComponent>();
        m_lightSchemaInitialized = true;
    }
    const auto lightEntities = ecs.getEntitiesMatchingSchema(m_lightSchema);
    if (!lightEntities.empty())
    {
        const auto lightEntity = lightEntities.front();
        if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(lightEntity))
        {
            lightPosition = glm::vec3(transform->position[0], transform->position[1], transform->position[2]);
        }
        if (const auto* light = ecs.getComponent<ECS::LightComponent>(lightEntity))
        {
            lightColor = glm::vec3(light->color[0], light->color[1], light->color[2]);
            lightIntensity = light->intensity;
        }
    }
    const uint64_t ecsEndCounter = SDL_GetPerformanceCounter();
    const uint64_t freq = SDL_GetPerformanceFrequency();
    m_cpuEcsMs = (freq > 0) ? (static_cast<double>(ecsEndCounter - ecsStartCounter) * 1000.0 / static_cast<double>(freq)) : 0.0;

    if (!m_renderEntries.empty())
    {
        for (auto& entry : m_renderEntries)
        {
            if (entry.entity != 0)
            {
                if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(entry.entity))
                {
                    entry.transform = *transform;
                }
            }

            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));

            if (entry.object3D)
            {
                ++m_lastTotalCount;
                bool hasBounds = false;
                glm::vec3 boundsMin{};
                glm::vec3 boundsMax{};
                if (entry.object3D->hasLocalBounds())
                {
                    ComputeWorldAabb(entry.object3D->getLocalBoundsMin(), entry.object3D->getLocalBoundsMax(), modelMatrix, boundsMin, boundsMax);
                    hasBounds = true;
                    if (!IsAabbInsideFrustum(frustumPlanes, boundsMin, boundsMax))
                    {
                        ++m_lastHiddenCount;
                        continue;
                    }
                }

                const bool shouldRender = !m_occlusionEnabled || !hasBounds || shouldRenderOcclusion(entry.object3D.get());
                if (shouldRender)
                {
                    ++m_lastVisibleCount;
                    entry.object3D->setLightData(lightPosition, lightColor, lightIntensity);
                    entry.object3D->setMatrices(modelMatrix, view, m_projectionMatrix);
                    entry.object3D->render();
                }
                else
                {
                    ++m_lastHiddenCount;
                }
                if (m_occlusionEnabled && hasBounds)
                {
                    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                    const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                    issueOcclusionQuery(entry.object3D.get(), center, extent, viewProj);
                }
                if (m_boundsDebugEnabled && hasBounds && shouldRender)
                {
                    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                    const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                    drawBoundsDebugBox(center, extent, viewProj);
                }
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

    if (m_occlusionEnabled)
    {
        const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean colorMask[4];
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

        for (const auto& entry : m_renderEntries)
        {
            if (!entry.object3D)
            {
                continue;
            }
            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));
            entry.object3D->setMatrices(modelMatrix, view, m_projectionMatrix);
            entry.object3D->render();
        }

        for (const auto& entry : objs)
        {
            if (!entry || entry->object->getPath().empty())
            {
                continue;
            }
            auto asset = entry->object ? std::dynamic_pointer_cast<AssetData>(entry->object) : nullptr;
            if (!asset)
            {
                continue;
            }
            if (asset->getAssetType() != AssetType::Model3D && asset->getAssetType() != AssetType::PointLight)
            {
                continue;
            }
            auto glObj = m_resourceManager.getOrCreateObject3D(asset, {});
            if (!glObj)
            {
                continue;
            }
            Mat4 engineMat = entry->transform.getMatrix4ColumnMajor();
            glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
            modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 1.0f, 0.0f));
            glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
            glObj->render();
        }

        for (const auto& groupEntry : groups)
        {
            const auto& groupObjects = groupEntry.objects;
            for (size_t i = 0; i < groupObjects.size(); ++i)
            {
                const auto& entry = groupObjects[i];
                if (!entry || entry->getPath().empty())
                {
                    continue;
                }
                auto asset = entry ? std::dynamic_pointer_cast<AssetData>(entry) : nullptr;
                if (!asset)
                {
                    continue;
                }
                if (asset->getAssetType() != AssetType::Model3D)
                {
                    continue;
                }
                auto glObj = m_resourceManager.getOrCreateObject3D(asset, {});
                if (!glObj)
                {
                    continue;
                }
                Transform* t = (i < groupEntry.transforms.size()) ? const_cast<Transform*>(&groupEntry.transforms[i]) : nullptr;
                if (!t)
                {
                    continue;
                }
                Mat4 engineMat = t->getMatrix4ColumnMajor();
                glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
                glObj->render();
            }
        }

        for (const auto& entry : m_meshEntries)
        {
            if (!entry.object3D)
            {
                continue;
            }
            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));
            entry.object3D->setMatrices(modelMatrix, view, m_projectionMatrix);
            entry.object3D->render();
        }

        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        glDepthMask(depthMask);
        if (!depthEnabled)
        {
            glDisable(GL_DEPTH_TEST);
        }
    }

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

            if (asset->getAssetType() == AssetType::Model3D || asset->getAssetType() == AssetType::PointLight)
            {
                auto glObj = m_resourceManager.getOrCreateObject3D(asset, {});
                if (!glObj)
                    continue;

                ++m_lastTotalCount;
                const bool skipOcclusion = (asset->getAssetType() == AssetType::PointLight);

                if (t)
                {
                    Mat4 engineMat = t->getMatrix4ColumnMajor();
                    glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                    modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 1.0f, 0.0f));
                    bool hasBounds = false;
                    glm::vec3 boundsMin{};
                    glm::vec3 boundsMax{};
                    if (glObj->hasLocalBounds())
                    {
                        ComputeWorldAabb(glObj->getLocalBoundsMin(), glObj->getLocalBoundsMax(), modelMatrix, boundsMin, boundsMax);
                        hasBounds = true;
                        if (!IsAabbInsideFrustum(frustumPlanes, boundsMin, boundsMax))
                        {
                            ++m_lastHiddenCount;
                            continue;
                        }
                    }

                    const bool shouldRender = skipOcclusion || !m_occlusionEnabled || !hasBounds || shouldRenderOcclusion(glObj.get());
                    if (shouldRender)
                    {
                        ++m_lastVisibleCount;
                        glObj->setLightData(lightPosition, lightColor, lightIntensity);
                        glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
                    }
                    else
                    {
                        ++m_lastHiddenCount;
                    }
                    if (m_occlusionEnabled && hasBounds && !skipOcclusion)
                    {
                        const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                        const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                        issueOcclusionQuery(glObj.get(), center, extent, viewProj);
                    }
                    if (m_boundsDebugEnabled && hasBounds && shouldRender)
                    {
                        const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                        const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                        drawBoundsDebugBox(center, extent, viewProj);
                    }
                    if (!shouldRender)
                    {
                        continue;
                    }
                }

                glObj->render();
            }
            else if (asset->getAssetType() == AssetType::Model2D)
            {
                auto glObj = m_resourceManager.getOrCreateObject2D(asset, {});
                if (!glObj)
                    continue;

                if (t)
                {
                    Mat4 engineMat = t->getMatrix4ColumnMajor();
                    glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                    modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 0.0f, 1.0f));
                    glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
                }

                glObj->render();
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
                    auto glObj = m_resourceManager.getOrCreateObject3D(asset, {});
                    if (!glObj)
                        continue;

                    ++m_lastTotalCount;

                    if (t)
                    {
                        Mat4 engineMat = t->getMatrix4ColumnMajor();
                        glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                        bool hasBounds = false;
                        glm::vec3 boundsMin{};
                        glm::vec3 boundsMax{};
                        if (glObj->hasLocalBounds())
                        {
                            ComputeWorldAabb(glObj->getLocalBoundsMin(), glObj->getLocalBoundsMax(), modelMatrix, boundsMin, boundsMax);
                            hasBounds = true;
                            if (!IsAabbInsideFrustum(frustumPlanes, boundsMin, boundsMax))
                            {
                                ++m_lastHiddenCount;
                                continue;
                            }
                        }

                        const bool shouldRender = !m_occlusionEnabled || !hasBounds || shouldRenderOcclusion(glObj.get());
                        if (shouldRender)
                        {
                            ++m_lastVisibleCount;
                            glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
                        }
                        else
                        {
                            ++m_lastHiddenCount;
                        }
                        if (m_occlusionEnabled && hasBounds)
                        {
                            const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                            const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                            issueOcclusionQuery(glObj.get(), center, extent, viewProj);
                        }
                        if (m_boundsDebugEnabled && hasBounds && shouldRender)
                        {
                            const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                            const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                            drawBoundsDebugBox(center, extent, viewProj);
                        }
                        if (!shouldRender)
                        {
                            continue;
                        }
                    }
                    glObj->render();
                }
                else if (asset->getAssetType() == AssetType::Model2D)
                {
                    auto glObj = m_resourceManager.getOrCreateObject2D(asset, {});
                    if (!glObj)
                        continue;

                    if (t)
                    {
                        Mat4 engineMat = t->getMatrix4ColumnMajor();
                        glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
                        glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
                    }
                    glObj->render();
                }
            }
        }

}

    if (!m_meshEntries.empty())
    {
        for (const auto& entry : m_meshEntries)
        {
            glm::mat4 modelMatrix(1.0f);
            modelMatrix = glm::translate(modelMatrix, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            modelMatrix = glm::scale(modelMatrix, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));

            if (entry.object3D)
            {
                ++m_lastTotalCount;
                bool hasBounds = false;
                glm::vec3 boundsMin{};
                glm::vec3 boundsMax{};
                if (entry.object3D->hasLocalBounds())
                {
                    ComputeWorldAabb(entry.object3D->getLocalBoundsMin(), entry.object3D->getLocalBoundsMax(), modelMatrix, boundsMin, boundsMax);
                    hasBounds = true;
                    if (!IsAabbInsideFrustum(frustumPlanes, boundsMin, boundsMax))
                    {
                        ++m_lastHiddenCount;
                        continue;
                    }
                }

                const bool shouldRender = !m_occlusionEnabled || !hasBounds || shouldRenderOcclusion(entry.object3D.get());
                if (shouldRender)
                {
                    ++m_lastVisibleCount;
                    entry.object3D->setLightData(lightPosition, lightColor, lightIntensity);
                    entry.object3D->setMatrices(modelMatrix, view, m_projectionMatrix);
                    entry.object3D->render();
                }
                else
                {
                    ++m_lastHiddenCount;
                }
                if (m_occlusionEnabled && hasBounds)
                {
                    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                    const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                    issueOcclusionQuery(entry.object3D.get(), center, extent, viewProj);
                }
                if (m_boundsDebugEnabled && hasBounds && shouldRender)
                {
                    const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
                    const glm::vec3 extent = (boundsMax - boundsMin) * 0.5f;
                    drawBoundsDebugBox(center, extent, viewProj);
                }
            }
            else if (entry.object2D)
            {
                entry.object2D->setMatrices(modelMatrix, view, m_projectionMatrix);
                entry.object2D->render();
            }
        }
    }
}

void OpenGLRenderer::renderUI()
{
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t uiStart = SDL_GetPerformanceCounter();
    const uint64_t drawStart = SDL_GetPerformanceCounter();
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

    if (m_uiManager.needsLayoutUpdate())
    {
        m_uiManager.updateLayouts([this](const std::string& text, float scale)
            {
                return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{};
            });
    }

    const auto& ordered = m_uiManager.getWidgetsOrderedByZ();
    if (!ordered.empty() && ensureUIQuadRenderer())
    {
        ensureUIShaderDefaults();
        const glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);

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
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.color, false);
                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 1.0f, 0.9f, 0.1f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::ColorPicker)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);

                if (element.isCompact)
                {
                    if (element.color.w > 0.0f)
                    {
                        drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.color, false);
                    }
                    for (const auto& child : element.children)
                    {
                        self(self, child, x0, y0, widthPx, heightPx);
                    }
                    if (m_uiDebugEnabled)
                    {
                        drawUIOutline(x0, y0, x1, y1, Vec4{ 0.6f, 0.9f, 0.2f, 1.0f }, uiProjection, program);
                    }
                    return;
                }

                const int columns = 24;
                const int rows = 6;
                const float cellW = (columns > 0) ? (widthPx / static_cast<float>(columns)) : widthPx;
                const float cellH = (rows > 0) ? (heightPx / static_cast<float>(rows)) : heightPx;

                for (int row = 0; row < rows; ++row)
                {
                    const float value = 1.0f - (static_cast<float>(row) / std::max(1.0f, static_cast<float>(rows - 1)));
                    for (int col = 0; col < columns; ++col)
                    {
                        const float hue = static_cast<float>(col) / std::max(1.0f, static_cast<float>(columns - 1));
                        const Vec4 color = HsvToRgb(hue, 1.0f, value);
                        const float cx0 = x0 + cellW * static_cast<float>(col);
                        const float cy0 = y0 + cellH * static_cast<float>(row);
                        drawUIPanel(cx0, cy0, cx0 + cellW, cy0 + cellH, color, uiProjection, program, color, false);
                    }
                }

                const float swatchSize = std::min(heightPx, 18.0f);
                if (swatchSize > 0.0f)
                {
                    const float swatchX0 = x1 - swatchSize - 4.0f;
                    const float swatchY0 = y0 + 4.0f;
                    drawUIPanel(swatchX0, swatchY0, swatchX0 + swatchSize, swatchY0 + swatchSize,
                        element.color, uiProjection, program, element.color, false);
                    drawUIOutline(swatchX0, swatchY0, swatchX0 + swatchSize, swatchY0 + swatchSize,
                        Vec4{ 0.1f, 0.1f, 0.1f, 0.9f }, uiProjection, program);
                }

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.6f, 0.9f, 0.2f, 1.0f }, uiProjection, program);
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
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultTextVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultTextFragment);
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
                    const GLuint program = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.1f, 0.8f, 1.0f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::EntryBar)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.hoverColor, element.isHovered);

                const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
                const float scale = fontSize / 48.0f;
                std::string display = element.value;
                if (element.isPassword)
                {
                    display.assign(element.value.size(), '*');
                }

                const float contentX0 = x0 + element.padding.x;
                const float contentY0 = y0 + element.padding.y;
                const float contentX1 = x1 - element.padding.x;
                const float contentY1 = y1 - element.padding.y;
                const float contentW = std::max(0.0f, contentX1 - contentX0);
                const float contentH = std::max(0.0f, contentY1 - contentY0);

                Vec2 textSize{};
                if (!display.empty())
                {
                    textSize = m_textRenderer->measureText(display, scale);
                    const float textX = contentX0;
                    const float textY = contentY0 + std::max(0.0f, (contentH - textSize.y) * 0.5f);
                    m_textRenderer->drawText(display, Vec2{ textX, textY }, scale, element.textColor);
                }

                if (element.isFocused)
                {
                    const float caretHeight = std::max(0.0f, contentH - 2.0f);
                    const float caretX = contentX0 + textSize.x + 1.0f;
                    if (caretX < contentX1)
                    {
                        drawUIPanel(caretX, contentY0 + 1.0f, caretX + 2.0f, contentY0 + 1.0f + caretHeight,
                            element.textColor, uiProjection, program, element.textColor, false);
                    }
                }

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.5f, 0.8f, 1.0f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::EntryBar)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.hoverColor, element.isHovered);

                const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
                const float scale = fontSize / 48.0f;
                std::string display = element.value;
                if (element.isPassword)
                {
                    display.assign(element.value.size(), '*');
                }

                const float contentX0 = x0 + element.padding.x;
                const float contentY0 = y0 + element.padding.y;
                const float contentX1 = x1 - element.padding.x;
                const float contentY1 = y1 - element.padding.y;
                const float contentW = std::max(0.0f, contentX1 - contentX0);
                const float contentH = std::max(0.0f, contentY1 - contentY0);

                Vec2 textSize{};
                if (!display.empty())
                {
                    textSize = m_textRenderer->measureText(display, scale);
                    const float textX = contentX0;
                    const float textY = contentY0 + std::max(0.0f, (contentH - textSize.y) * 0.5f);
                    m_textRenderer->drawText(display, Vec2{ textX, textY }, scale, element.textColor);
                }

                if (element.isFocused)
                {
                    const float caretHeight = std::max(0.0f, contentH - 2.0f);
                    const float caretX = contentX0 + textSize.x + 1.0f;
                    if (caretX < contentX1)
                    {
                        drawUIPanel(caretX, contentY0 + 1.0f, caretX + 2.0f, contentY0 + 1.0f + caretHeight,
                            element.textColor, uiProjection, program, element.textColor, false);
                    }
                }

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.5f, 0.8f, 1.0f, 1.0f }, uiProjection, program);
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

                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(bx0, by0, bx1, by1, element.color, uiProjection, program, element.hoverColor, element.isHovered);

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
            if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::Grid)
            {
                if (element.color.w > 0.0f)
                {
                    const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                    const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                    const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                    drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.color, false);
                }
                for (const auto& child : element.children)
                {
                    self(self, child, x0, y0, widthPx, heightPx);
                }
                if (m_uiDebugEnabled)
                {
                    const GLuint program = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
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
            Vec2 widgetPosition{};
            if (widget->hasComputedSize())
            {
                widgetSize = widget->getComputedSizePixels();
            }
            if (widget->hasComputedPosition())
            {
                widgetPosition = widget->getComputedPositionPixels();
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
                renderElement(renderElement, element, widgetPosition.x, widgetPosition.y, widgetSize.x, widgetSize.y);
            }
        }
        const uint64_t drawEnd = SDL_GetPerformanceCounter();
        m_cpuUiDrawMs = (freq > 0) ? (static_cast<double>(drawEnd - drawStart) * 1000.0 / static_cast<double>(freq)) : 0.0;
    }
    else
    {
        m_cpuUiDrawMs = 0.0;
    }

    Vec2 viewportSize = m_uiManager.getAvailableViewportSize();
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
    {
        viewportSize = Vec2{ static_cast<float>(width), static_cast<float>(height) };
    }

    float contentLeft = 0.0f;
    float contentTop = 0.0f;
    float contentRight = viewportSize.x;
    float contentBottom = viewportSize.y;

    const auto& uiWidgets = m_uiManager.getRegisteredWidgets();
    for (const auto& entry : uiWidgets)
    {
        if (!entry.widget)
        {
            continue;
        }

        if (!entry.widget->hasComputedSize() || !entry.widget->hasComputedPosition())
        {
            continue;
        }

        const Vec2 widgetPos = entry.widget->getComputedPositionPixels();
        const Vec2 widgetSize = entry.widget->getComputedSizePixels();
        const bool spansWidth = widgetSize.x >= viewportSize.x * 0.8f;
        const bool spansHeight = widgetSize.y >= viewportSize.y * 0.5f;

        if (widgetPos.y <= 0.0f && spansWidth)
        {
            contentTop = std::max(contentTop, widgetPos.y + widgetSize.y);
        }
        if (widgetPos.x <= 0.0f && spansHeight)
        {
            contentLeft = std::max(contentLeft, widgetPos.x + widgetSize.x);
        }
        if (widgetPos.y + widgetSize.y >= viewportSize.y * 0.98f && spansWidth)
        {
            contentBottom = std::min(contentBottom, widgetPos.y);
        }
        if (widgetPos.x + widgetSize.x >= viewportSize.x * 0.98f && spansHeight)
        {
            contentRight = std::min(contentRight, widgetPos.x);
        }
    }

    const float contentWidth = std::max(0.0f, contentRight - contentLeft);
    const float contentHeight = std::max(0.0f, contentBottom - contentTop);

    if (!m_textQueue.empty())
    {
        m_textRenderer->setScreenSize(static_cast<int>(viewportSize.x), static_cast<int>(viewportSize.y));
    }

    for (const auto& command : m_textQueue)
    {
        Vec2 pixelPos{
            contentLeft + command.screenPos.x * contentWidth,
            contentTop + command.screenPos.y * contentHeight
        };
        m_textRenderer->drawText(command.text, pixelPos, command.scale, command.color);
    }

    m_textQueue.clear();

    if (depthEnabled)
    {
        glEnable(GL_DEPTH_TEST);
    }

    const uint64_t uiEnd = SDL_GetPerformanceCounter();
    m_cpuRenderUiMs = (freq > 0) ? (static_cast<double>(uiEnd - uiStart) * 1000.0 / static_cast<double>(freq)) : 0.0;
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

bool OpenGLRenderer::ensureOcclusionResources()
{
    if (m_occlusionResourcesReady)
    {
        return true;
    }

    auto vertex = std::make_shared<OpenGLShader>();
    auto fragment = std::make_shared<OpenGLShader>();

    const std::string vertexSource =
        "#version 460 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "uniform mat4 uViewProj;\n"
        "uniform mat4 uModel;\n"
        "void main() {\n"
        "    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);\n"
        "}\n";

    const std::string fragmentSource =
        "#version 460 core\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = vec4(1.0);\n"
        "}\n";

    if (!vertex->loadFromSource(Shader::Type::Vertex, vertexSource) ||
        !fragment->loadFromSource(Shader::Type::Fragment, fragmentSource))
    {
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex->id());
    glAttachShader(program, fragment->id());
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        glDeleteProgram(program);
        return false;
    }

    glGenVertexArrays(1, &m_occlusionVao);
    glGenBuffers(1, &m_occlusionVbo);
    glBindVertexArray(m_occlusionVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_occlusionVbo);

    const float cubeVerts[] = {
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,

         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,

        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f
    };

    glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVerts), cubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_occlusionProgram = program;
    m_occlusionResourcesReady = true;
    return true;
}

void OpenGLRenderer::releaseOcclusionResources()
{
    for (auto& [object, data] : m_occlusionQueries)
    {
        if (data.queryId != 0)
        {
            glDeleteQueries(1, &data.queryId);
        }
    }
    m_occlusionQueries.clear();

    if (m_occlusionProgram)
    {
        glDeleteProgram(m_occlusionProgram);
        m_occlusionProgram = 0;
    }
    if (m_occlusionVbo)
    {
        glDeleteBuffers(1, &m_occlusionVbo);
        m_occlusionVbo = 0;
    }
    if (m_occlusionVao)
    {
        glDeleteVertexArrays(1, &m_occlusionVao);
        m_occlusionVao = 0;
    }
    m_occlusionResourcesReady = false;
}

bool OpenGLRenderer::ensureBoundsDebugResources()
{
    if (m_boundsDebugProgram != 0 && m_boundsDebugVao != 0 && m_boundsDebugVbo != 0)
    {
        return true;
    }

    auto vertex = std::make_shared<OpenGLShader>();
    auto fragment = std::make_shared<OpenGLShader>();

    const std::string vertexSource =
        "#version 460 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "uniform mat4 uViewProj;\n"
        "uniform mat4 uModel;\n"
        "void main() {\n"
        "    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);\n"
        "}\n";

    const std::string fragmentSource =
        "#version 460 core\n"
        "out vec4 FragColor;\n"
        "uniform vec4 uColor;\n"
        "void main() {\n"
        "    FragColor = uColor;\n"
        "}\n";

    if (!vertex->loadFromSource(Shader::Type::Vertex, vertexSource) ||
        !fragment->loadFromSource(Shader::Type::Fragment, fragmentSource))
    {
        return false;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex->id());
    glAttachShader(program, fragment->id());
    glLinkProgram(program);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        glDeleteProgram(program);
        return false;
    }

    std::vector<float> vertices;
    vertices.reserve(24 * 3);

    const glm::vec3 corners[8] = {
        glm::vec3(-1.0f, -1.0f, -1.0f),
        glm::vec3(1.0f, -1.0f, -1.0f),
        glm::vec3(1.0f, 1.0f, -1.0f),
        glm::vec3(-1.0f, 1.0f, -1.0f),
        glm::vec3(-1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, -1.0f, 1.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(-1.0f, 1.0f, 1.0f)
    };

    const int edges[24] = {
        0, 1, 1, 2, 2, 3, 3, 0,
        4, 5, 5, 6, 6, 7, 7, 4,
        0, 4, 1, 5, 2, 6, 3, 7
    };

    for (int i = 0; i < 24; ++i)
    {
        const glm::vec3& p = corners[edges[i]];
        vertices.push_back(p.x);
        vertices.push_back(p.y);
        vertices.push_back(p.z);
    }

    glGenVertexArrays(1, &m_boundsDebugVao);
    glGenBuffers(1, &m_boundsDebugVbo);
    glBindVertexArray(m_boundsDebugVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_boundsDebugVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_boundsDebugProgram = program;
    m_boundsDebugVertexCount = static_cast<GLsizei>(vertices.size() / 3);
    return true;
}

void OpenGLRenderer::releaseBoundsDebugResources()
{
    if (m_boundsDebugProgram)
    {
        glDeleteProgram(m_boundsDebugProgram);
        m_boundsDebugProgram = 0;
    }
    if (m_boundsDebugVbo)
    {
        glDeleteBuffers(1, &m_boundsDebugVbo);
        m_boundsDebugVbo = 0;
    }
    if (m_boundsDebugVao)
    {
        glDeleteVertexArrays(1, &m_boundsDebugVao);
        m_boundsDebugVao = 0;
    }
    m_boundsDebugVertexCount = 0;
}

void OpenGLRenderer::drawBoundsDebugBox(const glm::vec3& center, const glm::vec3& extent, const glm::mat4& viewProj)
{
    if (!m_boundsDebugEnabled)
    {
        return;
    }
    if (!ensureBoundsDebugResources() || m_boundsDebugVertexCount == 0)
    {
        return;
    }

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(m_boundsDebugProgram);
    glBindVertexArray(m_boundsDebugVao);

    glm::mat4 model(1.0f);
    model = glm::translate(model, center);
    model = glm::scale(model, extent);

    const GLint viewProjLoc = glGetUniformLocation(m_boundsDebugProgram, "uViewProj");
    if (viewProjLoc >= 0)
    {
        glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, &viewProj[0][0]);
    }
    const GLint modelLoc = glGetUniformLocation(m_boundsDebugProgram, "uModel");
    if (modelLoc >= 0)
    {
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
    }
    const GLint colorLoc = glGetUniformLocation(m_boundsDebugProgram, "uColor");
    if (colorLoc >= 0)
    {
        glUniform4f(colorLoc, 0.95f, 0.4f, 0.2f, 1.0f);
    }

    glDrawArrays(GL_LINES, 0, m_boundsDebugVertexCount);

    glBindVertexArray(0);
    glUseProgram(0);

    if (!depthEnabled)
    {
        glDisable(GL_DEPTH_TEST);
    }
    if (cullEnabled)
    {
        glEnable(GL_CULL_FACE);
    }
}

void OpenGLRenderer::updateOcclusionResults()
{
    constexpr uint8_t kOcclusionGraceFrames = 4;
    for (auto& [object, data] : m_occlusionQueries)
    {
        if (data.queryId == 0)
        {
            continue;
        }
        GLuint available = 0;
        glGetQueryObjectuiv(data.queryId, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == 0)
        {
            continue;
        }
        GLuint result = 0;
        glGetQueryObjectuiv(data.queryId, GL_QUERY_RESULT, &result);
        if (result != 0)
        {
            data.lastVisible = true;
            data.occludedFrames = 0;
            data.lastResultFrame = m_frameIndex;
        }
        else
        {
            data.occludedFrames = static_cast<uint8_t>(std::min<uint8_t>(data.occludedFrames + 1, kOcclusionGraceFrames));
            if (data.occludedFrames >= kOcclusionGraceFrames)
            {
                data.lastVisible = false;
            }
            data.lastResultFrame = m_frameIndex;
        }
        data.hasResult = true;
    }
}

bool OpenGLRenderer::shouldRenderOcclusion(const OpenGLObject3D* object) const
{
    constexpr uint8_t kOcclusionGraceFrames = 4;
    constexpr uint64_t kOcclusionDelayFrames = 2;
    auto it = m_occlusionQueries.find(object);
    if (it == m_occlusionQueries.end())
    {
        return true;
    }
    if (!it->second.hasResult)
    {
        return true;
    }
    if (m_frameIndex - it->second.lastResultFrame < kOcclusionDelayFrames)
    {
        return true;
    }
    if (it->second.lastVisible)
    {
        return true;
    }
    return it->second.occludedFrames < kOcclusionGraceFrames;
}

void OpenGLRenderer::issueOcclusionQuery(const OpenGLObject3D* object, const glm::vec3& center, const glm::vec3& extent, const glm::mat4& viewProj)
{
    if (!object || !m_occlusionEnabled)
    {
        return;
    }
    if (!ensureOcclusionResources())
    {
        return;
    }

    auto& data = m_occlusionQueries[object];
    if (data.queryId == 0)
    {
        glGenQueries(1, &data.queryId);
    }

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean colorMask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    glUseProgram(m_occlusionProgram);
    glBindVertexArray(m_occlusionVao);

    glm::mat4 model(1.0f);
    model = glm::translate(model, center);
    model = glm::scale(model, extent);

    const GLint viewProjLoc = glGetUniformLocation(m_occlusionProgram, "uViewProj");
    if (viewProjLoc >= 0)
    {
        glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, &viewProj[0][0]);
    }
    const GLint modelLoc = glGetUniformLocation(m_occlusionProgram, "uModel");
    if (modelLoc >= 0)
    {
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
    }

    GLenum queryTarget = GL_ANY_SAMPLES_PASSED;
#if defined(GL_ANY_SAMPLES_PASSED_CONSERVATIVE)
    queryTarget = GL_ANY_SAMPLES_PASSED_CONSERVATIVE;
#endif
    glBeginQuery(queryTarget, data.queryId);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glEndQuery(queryTarget);

    glBindVertexArray(0);
    glUseProgram(0);

    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthMask);
    if (!depthEnabled)
    {
        glDisable(GL_DEPTH_TEST);
    }
}


void OpenGLRenderer::ensureUIShaderDefaults()
{
    if (m_uiShaderDefaultsInitialized)
    {
        return;
    }

    const std::filesystem::path shadersDir = std::filesystem::current_path() / "shaders";
    m_uiShaderBaseDir = shadersDir.string();
    m_defaultPanelVertex = (shadersDir / "panel_vertex.glsl").string();
    m_defaultPanelFragment = (shadersDir / "panel_fragment.glsl").string();
    m_defaultButtonVertex = (shadersDir / "button_vertex.glsl").string();
    m_defaultButtonFragment = (shadersDir / "button_fragment.glsl").string();
    m_defaultTextVertex = (shadersDir / "text_vertex.glsl").string();
    m_defaultTextFragment = (shadersDir / "text_fragment.glsl").string();
    m_uiShaderDefaultsInitialized = true;
}

const std::string& OpenGLRenderer::resolveUIShaderPath(const std::string& value, const std::string& fallback)
{
    if (value.empty())
    {
        return fallback;
    }

    auto it = m_uiShaderPathCache.find(value);
    if (it != m_uiShaderPathCache.end())
    {
        return it->second;
    }

    std::filesystem::path resolved(value);
    if (resolved.is_relative())
    {
        resolved = std::filesystem::path(m_uiShaderBaseDir) / resolved;
    }

    auto [insertedIt, inserted] = m_uiShaderPathCache.emplace(value, resolved.string());
    return insertedIt->second;
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

void OpenGLRenderer::drawUIPanel(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program,
    const Vec4& hoverColor, bool isHovered)
{
    if (program == 0 || m_uiQuadVao == 0)
    {
        return;
    }

    const glm::vec4 glColor{ color.x, color.y, color.z, color.w };
    const glm::vec4 glHoverColor{ hoverColor.x, hoverColor.y, hoverColor.z, hoverColor.w };
    const Vec4 baseColor = isHovered ? hoverColor : color;
    const glm::vec4 glBorderColor{
        std::min(1.0f, baseColor.x + 0.1f),
        std::min(1.0f, baseColor.y + 0.1f),
        std::min(1.0f, baseColor.z + 0.1f),
        baseColor.w
    };
    const Vec2 viewportSize = (m_uiManager.getAvailableViewportSize().x > 0.0f && m_uiManager.getAvailableViewportSize().y > 0.0f)
        ? m_uiManager.getAvailableViewportSize()
        : getViewportSize();

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

    const GLint borderColorLoc = glGetUniformLocation(program, "uBorderColor");
    if (borderColorLoc >= 0)
    {
        glUniform4fv(borderColorLoc, 1, &glBorderColor[0]);
    }
    const GLint borderSizeLoc = glGetUniformLocation(program, "uBorderSize");
    if (borderSizeLoc >= 0)
    {
        glUniform1f(borderSizeLoc, 1.0f);
    }
    const GLint rectLoc = glGetUniformLocation(program, "uRect");
    if (rectLoc >= 0)
    {
        glUniform4f(rectLoc, x0, y0, x1, y1);
    }
    const GLint viewportLoc = glGetUniformLocation(program, "uViewportSize");
    if (viewportLoc >= 0)
    {
        glUniform2f(viewportLoc, viewportSize.x, viewportSize.y);
    }

    const GLint hoverColorLoc = glGetUniformLocation(program, "uHoverColor");
    if (hoverColorLoc >= 0)
    {
        glUniform4fv(hoverColorLoc, 1, &glHoverColor[0]);
    }

    const GLint hoverFlagLoc = glGetUniformLocation(program, "uIsHovered");
    if (hoverFlagLoc >= 0)
    {
        glUniform1f(hoverFlagLoc, isHovered ? 1.0f : 0.0f);
    }

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
    drawUIPanel(x0, y0, x1, y1, color, projection, program, color, false);
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
    auto widget = m_resourceManager.buildWidgetAsset(asset);
    if (!widget || !asset)
    {
        return widget;
    }

    if (asset->getName() == "WorldSettings")
    {
        const auto findById = [](std::vector<WidgetElement>& elements, const std::string& id) -> WidgetElement*
        {
            const std::function<WidgetElement*(WidgetElement&)> find = [&](WidgetElement& element) -> WidgetElement*
                {
                    if (element.id == id)
                    {
                        return &element;
                    }
                    for (auto& child : element.children)
                    {
                        if (auto* match = find(child))
                        {
                            return match;
                        }
                    }
                    return nullptr;
                };
            for (auto& element : elements)
            {
                if (auto* match = find(element))
                {
                    return match;
                }
            }
            return nullptr;
        };

        if (auto* picker = findById(widget->getElementsMutable(), "WorldSettings.ClearColor"))
        {
            picker->color = m_clearColor;
            picker->onColorChanged = [this](const Vec4& color)
                {
                    setClearColor(color);
                };
        }
    }

    return widget;
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
