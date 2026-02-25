#include "OpenGLRenderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <cmath>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <fstream>
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
#include "../../AssetManager/AssetManager.h"

#include "../RenderResourceManager.h"

#include "../../Core/Asset.h"
#include "../../Core/MathTypes.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Core/UndoRedoManager.h"
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

        // Exclude button strip area from draggable region
        if (y < ctx->buttonStripHeight)
        {
            if (ctx->buttonsOnLeft && x < ctx->buttonStripWidth)
            {
                return SDL_HITTEST_NORMAL;
            }
            if (!ctx->buttonsOnLeft && x >= width - ctx->buttonStripWidth)
            {
                return SDL_HITTEST_NORMAL;
            }
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
    releaseHzbResources();
    releaseBoundsDebugResources();
    releaseSkyboxResources();
    releaseShadowResources();
    releasePointShadowResources();
    releasePickFbo();
    releaseOutlineResources();
    releaseUiFbo();
    releaseAllTabFbos();

    // Release mesh viewer state before destroying GL resources.
    m_meshViewers.clear();

    // Destroy all popup windows before releasing main GL resources.
    m_popupWindows.clear();

    auto& logger = Logger::Instance();
    logger.log(Logger::Category::Rendering, "OpenGLRenderer shutdown: releasing GPU resources...", Logger::LogLevel::INFO);

    // Ensure GL context is current so destructors can safely delete GL objects.
    if (m_window && m_glContext)
    {
        SDL_GL_MakeCurrent(m_window, m_glContext);
    }

    OpenGLObject2D::ClearCache();
    OpenGLObject3D::ClearCache();

    if (m_popupUiVao)
    {
        glDeleteVertexArrays(1, &m_popupUiVao);
        m_popupUiVao = 0;
    }
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
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    logger.log(Logger::Category::Rendering, "Creating Window...", Logger::LogLevel::INFO);

	auto& diagnostics = DiagnosticsManager::Instance();
	Vec2 windowSize = diagnostics.getWindowSize();
	int width = static_cast<int>(windowSize.x);
	int height = static_cast<int>(windowSize.y);
	DiagnosticsManager::WindowState windowState = diagnostics.getWindowState();

    m_window = SDL_CreateWindow("HorizonEngine", width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
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
    m_uiManager.setRenderer(this);
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

        // Restore editor camera from the level if available
        if (m_cachedLevel && m_cachedLevel->hasEditorCamera() && m_camera)
        {
            m_camera->setPosition(m_cachedLevel->getEditorCameraPosition());
            const Vec2& rot = m_cachedLevel->getEditorCameraRotation();
            m_camera->setRotationDegrees(rot.x, rot.y);
        }
    }

    // Window stays hidden until main.cpp shows it after freeing the console.
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

    const uint64_t freq = SDL_GetPerformanceFrequency();

    if (m_gpuQueriesInitialized)
    {
        glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuQueryIndex]);
    }

    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    m_cachedWindowWidth = width;
    m_cachedWindowHeight = height;

    // Ensure active tab has a valid FBO (cache for renderWorld reuse)
    m_cachedActiveTab = nullptr;
    for (auto& tab : m_editorTabs)
    {
        if (tab.active)
        {
            ensureTabFbo(tab, width, height);
            m_cachedActiveTab = &tab;
            break;
        }
    }
    EditorTab* activeTab = m_cachedActiveTab;

    // Render content for the active tab (world or mesh viewer)
    const uint64_t worldStart = SDL_GetPerformanceCounter();
    {
        if (activeTab)
        {
            renderWorld();
        }
    }
    const uint64_t worldEnd = SDL_GetPerformanceCounter();
    m_cpuRenderWorldMs = (freq > 0) ? (static_cast<double>(worldEnd - worldStart) * 1000.0 / static_cast<double>(freq)) : 0.0;

    // Composite: blit world content to default framebuffer
    if (activeTab && activeTab->fbo != 0)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, activeTab->fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);

    renderUI();
    renderPopupWindows();

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

    const Vec2 viewportSize = getViewportSize();
    const int width = static_cast<int>(viewportSize.x);
    const int height = static_cast<int>(viewportSize.y);

    // Render into the active tab's FBO
    {
        EditorTab* activeTab = m_cachedActiveTab;
        if (activeTab && activeTab->fbo != 0)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, activeTab->fbo);
        }
    }

    glViewport(0, 0, width, height);
    glClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Update projection matrix with current aspect ratio (default, may be overridden by entity camera)
    float activeFov = 45.0f;
    float activeNear = 0.1f;
    float activeFar = 100.0f;
    if (height > 0 && (width != m_lastProjectionWidth || height != m_lastProjectionHeight))
    {
        float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
        m_projectionMatrix = glm::perspective(
            glm::radians(activeFov),
            aspectRatio,
            activeNear,
            activeFar
        );
        m_lastProjectionWidth = width;
        m_lastProjectionHeight = height;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
	EngineLevel* level = diagnostics.getActiveLevelSoft();
	if (!level)
	{
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
        m_restoreCameraOnPrepare = true;
        // Reset cached skybox path so setSkyboxPath re-loads for the new level
        m_skyboxLoadedPath.clear();
        setSkyboxPath(level->getSkyboxPath());
    }

    if (!diagnostics.isScenePrepared())
    {
        if (m_resourceManager.prepareActiveLevel())
        {
            diagnostics.setScenePrepared(true);
        }

        // (Re-)load skybox if the level has one but it is not loaded yet
        const std::string& levelSkybox = level->getSkyboxPath();
        if (!levelSkybox.empty() && (m_skyboxCubemap == 0 || m_skyboxLoadedPath != levelSkybox))
        {
            m_skyboxLoadedPath.clear();
            setSkyboxPath(levelSkybox);
        }

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

        ECS::Schema meshSchema;
        meshSchema.require<ECS::MeshComponent>().require<ECS::TransformComponent>();
        m_meshEntries.clear();

        // Collect entities already covered by the render schema to avoid duplicates
        std::unordered_set<ECS::Entity> renderEntitySet;
        renderEntitySet.reserve(m_renderEntries.size());
        for (const auto& re : m_renderEntries)
        {
            if (re.entity != 0)
                renderEntitySet.insert(re.entity);
        }

        const auto meshRenderables = m_resourceManager.buildRenderablesForSchema(meshSchema, "grid_fragment.glsl");
        m_meshEntries.reserve(meshRenderables.size());
        for (const auto& renderable : meshRenderables)
        {
            // Skip entities already in m_renderEntries (Mesh+Material)
            if (renderable.entity != 0 && renderEntitySet.count(renderable.entity))
            {
                continue;
            }
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

        // Restore editor camera from the level only on level change (not on every re-prepare)
        if (m_restoreCameraOnPrepare && level && level->hasEditorCamera() && m_camera)
        {
            m_camera->setPosition(level->getEditorCameraPosition());
            const Vec2& rot = level->getEditorCameraRotation();
            m_camera->setRotationDegrees(rot.x, rot.y);
        }
		m_restoreCameraOnPrepare = false;
	}

	// Per-entity refresh for dirty entities (avoids full scene rebuild)
	if (diagnostics.hasDirtyEntities())
	{
		auto dirtyEntities = diagnostics.consumeDirtyEntities();
		for (unsigned int entityId : dirtyEntities)
		{
			refreshEntity(static_cast<ECS::Entity>(entityId));
		}
		m_pickDirty = true;
	}

	glm::mat4 view(1.0f);
	bool usedEntityCamera = false;

	// Entity camera is only used during PIE (Play In Editor)
	auto& ecs = ECS::ECSManager::Instance();
	const bool pieActive = diagnostics.isPIEActive();
	if (pieActive && m_activeCameraEntity != 0)
	{
		const auto* camComp = ecs.getComponent<ECS::CameraComponent>(static_cast<ECS::Entity>(m_activeCameraEntity));
		const auto* camTransform = ecs.getComponent<ECS::TransformComponent>(static_cast<ECS::Entity>(m_activeCameraEntity));
		if (camComp && camTransform)
		{
			const glm::vec3 pos(camTransform->position[0], camTransform->position[1], camTransform->position[2]);
			const float pitch = glm::radians(camTransform->rotation[0]);
			const float yaw   = glm::radians(camTransform->rotation[1]);
			const glm::vec3 front = glm::normalize(glm::vec3(
				cosf(yaw) * cosf(pitch),
				sinf(pitch),
				sinf(yaw) * cosf(pitch)
			));
			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
			const glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
			const glm::vec3 up = glm::normalize(glm::cross(right, front));
			view = glm::lookAt(pos, pos + front, up);

			// Override projection with entity camera parameters
			if (height > 0)
			{
				const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
				m_projectionMatrix = glm::perspective(
					glm::radians(camComp->fov),
					aspectRatio,
					camComp->nearClip,
					camComp->farClip
				);
			}
			usedEntityCamera = true;
		}
	}

	// Editor camera (always used outside PIE, or as fallback)
	if (!usedEntityCamera && m_camera)
	{
		Mat4 engineView = m_camera->getViewMatrixColumnMajor();
		view = glm::make_mat4(engineView.m);
	}
	const glm::mat4 viewProj = m_projectionMatrix * view;
	m_lastViewMatrix = view;
	const auto frustumPlanes = ExtractFrustumPlanes(viewProj);

	// Render skybox before scene (depth write disabled, drawn at depth=1)
	glEnable(GL_DEPTH_TEST);
	renderSkybox(view, m_projectionMatrix);

	const uint64_t ecsStartCounter = SDL_GetPerformanceCounter();
	glm::vec3 lightPosition{ 0.0f, 1.2f, 0.0f };
	glm::vec3 lightColor{ 1.0f, 1.0f, 1.0f };
	float lightIntensity = 1.0f;

	if (!m_lightSchemaInitialized)
	{
		m_lightSchema.require<ECS::LightComponent>().require<ECS::TransformComponent>();
		m_lightSchemaInitialized = true;
	}

	m_sceneLights.clear();
	const auto lightEntities = ecs.getEntitiesMatchingSchema(m_lightSchema);
	for (const auto lightEntity : lightEntities)
	{
		const auto* transform = ecs.getComponent<ECS::TransformComponent>(lightEntity);
		const auto* light = ecs.getComponent<ECS::LightComponent>(lightEntity);
		if (!transform || !light)
			continue;

		OpenGLMaterial::LightData ld;
		ld.position = glm::vec3(transform->position[0], transform->position[1], transform->position[2]);
		ld.color = glm::vec3(light->color[0], light->color[1], light->color[2]);
		ld.intensity = light->intensity;
		ld.range = light->range;

		// Compute forward direction from rotation (pitch=X, yaw=Y)
		const float pitch = glm::radians(transform->rotation[0]);
		const float yaw = glm::radians(transform->rotation[1]);
		ld.direction = glm::normalize(glm::vec3(
			std::cos(pitch) * std::sin(yaw),
			-std::sin(pitch),
			std::cos(pitch) * std::cos(yaw)
		));

		switch (light->type)
		{
		case ECS::LightComponent::LightType::Point:
			ld.type = 0;
			break;
		case ECS::LightComponent::LightType::Directional:
			ld.type = 1;
			break;
		case ECS::LightComponent::LightType::Spot:
			ld.type = 2;
			ld.spotCutoff = std::cos(glm::radians(light->spotAngle));
			ld.spotOuterCutoff = std::cos(glm::radians(light->spotAngle + 5.0f));
			break;
		}

		m_sceneLights.push_back(ld);

		if (m_sceneLights.size() >= OpenGLMaterial::kMaxLights)
			break;
	}

	// Legacy fallback: use first point light position
	if (!m_sceneLights.empty())
	{
		lightPosition = m_sceneLights[0].position;
		lightColor = m_sceneLights[0].color;
		lightIntensity = m_sceneLights[0].intensity;
	}
	const uint64_t ecsEndCounter = SDL_GetPerformanceCounter();
	const uint64_t freq = SDL_GetPerformanceFrequency();
	m_cpuEcsMs = (freq > 0) ? (static_cast<double>(ecsEndCounter - ecsStartCounter) * 1000.0 / static_cast<double>(freq)) : 0.0;

	// Pre-compute model matrices for all entry lists
	const auto updateModelMatrices = [&ecs](std::vector<RenderEntry>& entries)
	{
		for (auto& entry : entries)
		{
			if (entry.entity != 0)
			{
				if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(entry.entity))
				{
					entry.transform = *transform;
				}
			}
			auto& m = entry.cachedModelMatrix;
			m = glm::mat4(1.0f);
			m = glm::translate(m, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
			m = glm::rotate(m, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
			m = glm::rotate(m, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::rotate(m, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
			m = glm::scale(m, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));
		}
	};
	updateModelMatrices(m_renderEntries);
	updateModelMatrices(m_meshEntries);

	const auto& objs = level->getWorldObjects();
	const auto& groups = level->getGroups();

	// Sync selection state with UIManager (outliner may have changed it)
	if (m_uiManager.getSelectedEntity() != m_selectedEntity && !m_pickRequested)
	{
		m_selectedEntity = m_uiManager.getSelectedEntity();
	}

	// Collect all visible 3D objects into a unified draw list for batching
	m_drawList.clear();
	m_shadowCasterList.clear();
	m_drawList.reserve(m_renderEntries.size() + objs.size() + m_meshEntries.size() + 16);
	m_shadowCasterList.reserve(m_renderEntries.size() + objs.size() + m_meshEntries.size() + 16);

	const float timeRotation = static_cast<float>(SDL_GetTicks()) / 1000.0f;

	// Gather ECS render entries
	for (auto& entry : m_renderEntries)
	{
		if (entry.object3D)
		{
			++m_lastTotalCount;
			DrawCmd cmd;
			cmd.obj = entry.object3D.get();
			cmd.modelMatrix = entry.cachedModelMatrix;
			cmd.entityId = static_cast<unsigned int>(entry.entity);
			bool isLight = false;
			if (entry.entity != 0)
			{
				if (const auto* light = ecs.getComponent<ECS::LightComponent>(entry.entity))
				{
					cmd.emissionColor = glm::vec3(light->color[0], light->color[1], light->color[2]);
					cmd.hasEmission = true;
					isLight = true;
				}
			}
			// Shadow caster: include all non-light objects regardless of camera frustum
			if (!isLight)
			{
				m_shadowCasterList.push_back(cmd);
			}
			if (entry.object3D->hasLocalBounds())
			{
				ComputeWorldAabb(entry.object3D->getLocalBoundsMin(), entry.object3D->getLocalBoundsMax(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
				cmd.hasBounds = true;
				if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
				{
					++m_lastHiddenCount;
					continue;
				}
				if (m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
				{
					++m_lastHiddenCount;
					continue;
				}
			}
			cmd.program = entry.object3D->getProgram();
			++m_lastVisibleCount;
			m_drawList.push_back(std::move(cmd));
		}
		else if (entry.object2D)
		{
			entry.object2D->setMatrices(entry.cachedModelMatrix, view, m_projectionMatrix);
			entry.object2D->render();
		}
	}

	// Gather world objects
	for (const auto& entry : objs)
	{
		if (!entry || !entry->object || entry->object->getPath().empty())
			continue;

		const AssetType objType = entry->object->getAssetType();
		auto asset = std::static_pointer_cast<AssetData>(entry->object);
		if (!asset)
			continue;

		if (objType == AssetType::Model3D || objType == AssetType::PointLight)
		{
			auto glObj = m_resourceManager.getOrCreateObject3D(asset, {});
			if (!glObj)
				continue;

			++m_lastTotalCount;
			const bool skipOcclusion = (objType == AssetType::PointLight);

			DrawCmd cmd;
			cmd.obj = glObj.get();
			Mat4 engineMat = entry->transform.getMatrix4ColumnMajor();
			cmd.modelMatrix = glm::make_mat4(engineMat.m);
			cmd.modelMatrix = glm::rotate(cmd.modelMatrix, timeRotation, glm::vec3(0.0f, 1.0f, 0.0f));

			// Shadow caster: include non-light objects regardless of camera frustum
			if (objType != AssetType::PointLight)
			{
				m_shadowCasterList.push_back(cmd);
			}

			if (glObj->hasLocalBounds())
			{
				ComputeWorldAabb(glObj->getLocalBoundsMin(), glObj->getLocalBoundsMax(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
				cmd.hasBounds = true;
				if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
				{
					++m_lastHiddenCount;
					continue;
				}
				if (!skipOcclusion && m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
				{
					++m_lastHiddenCount;
					continue;
				}
			}

			cmd.program = glObj->getProgram();
				++m_lastVisibleCount;
				m_drawList.push_back(std::move(cmd));
			}
			else if (objType == AssetType::Model2D)
			{
				auto glObj = m_resourceManager.getOrCreateObject2D(asset, {});
				if (!glObj)
					continue;

				Mat4 engineMat = entry->transform.getMatrix4ColumnMajor();
			glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
			modelMatrix = glm::rotate(modelMatrix, timeRotation, glm::vec3(0.0f, 0.0f, 1.0f));
			glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
			glObj->render();
		}
	}

	// Gather groups
	for (const auto& groupEntry : groups)
	{
		const auto& groupObjects = groupEntry.objects;
		for (size_t i = 0; i < groupObjects.size(); ++i)
		{
			const auto& entry = groupObjects[i];
			if (!entry || entry->getPath().empty())
				continue;
			Transform* t = (i < groupEntry.transforms.size()) ? const_cast<Transform*>(&groupEntry.transforms[i]) : nullptr;
			const AssetType groupObjType = entry->getAssetType();
			auto asset = std::static_pointer_cast<AssetData>(entry);
			if (!asset)
				continue;

			if (groupObjType == AssetType::Model3D)
			{
				auto glObj = m_resourceManager.getOrCreateObject3D(asset, {});
				if (!glObj || !t)
					continue;

				++m_lastTotalCount;

				DrawCmd cmd;
				cmd.obj = glObj.get();
				Mat4 engineMat = t->getMatrix4ColumnMajor();
				cmd.modelMatrix = glm::make_mat4(engineMat.m);

				m_shadowCasterList.push_back(cmd);

				if (glObj->hasLocalBounds())
				{
					ComputeWorldAabb(glObj->getLocalBoundsMin(), glObj->getLocalBoundsMax(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
					cmd.hasBounds = true;
					if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
					{
						++m_lastHiddenCount;
						continue;
					}
					if (m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
					{
						++m_lastHiddenCount;
						continue;
					}
				}

				cmd.program = glObj->getProgram();
				++m_lastVisibleCount;
				m_drawList.push_back(std::move(cmd));
			}
			else if (groupObjType == AssetType::Model2D)
			{
				auto glObj = m_resourceManager.getOrCreateObject2D(asset, {});
				if (!glObj || !t)
					continue;

				Mat4 engineMat = t->getMatrix4ColumnMajor();
				glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
				glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
				glObj->render();
			}
		}
	}

	// Gather mesh entries
	for (auto& entry : m_meshEntries)
	{
		if (entry.object3D)
		{
			++m_lastTotalCount;
			DrawCmd cmd;
			cmd.obj = entry.object3D.get();
			cmd.modelMatrix = entry.cachedModelMatrix;
			cmd.entityId = static_cast<unsigned int>(entry.entity);
			bool isLight = false;
			if (entry.entity != 0)
			{
				if (const auto* light = ecs.getComponent<ECS::LightComponent>(entry.entity))
				{
					cmd.emissionColor = glm::vec3(light->color[0], light->color[1], light->color[2]);
					cmd.hasEmission = true;
					isLight = true;
				}
			}
			// Shadow caster: include all non-light objects regardless of camera frustum
			if (!isLight)
			{
				m_shadowCasterList.push_back(cmd);
			}
			if (entry.object3D->hasLocalBounds())
			{
				ComputeWorldAabb(entry.object3D->getLocalBoundsMin(), entry.object3D->getLocalBoundsMax(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
				cmd.hasBounds = true;
				if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
				{
					++m_lastHiddenCount;
					continue;
				}
				if (m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
				{
					++m_lastHiddenCount;
					continue;
				}
			}
			cmd.program = entry.object3D->getProgram();
			++m_lastVisibleCount;
			m_drawList.push_back(std::move(cmd));
		}
		else if (entry.object2D)
		{
			entry.object2D->setMatrices(entry.cachedModelMatrix, view, m_projectionMatrix);
			entry.object2D->render();
		}
	}

	// Sort by shader program to minimize state changes
	std::sort(m_drawList.begin(), m_drawList.end(),
		[](const DrawCmd& a, const DrawCmd& b) { return a.program < b.program; });

	// ---- Shadow map pass (multi-light) ----
	m_shadowCount = 0;
	if (m_shadowEnabled && ensureShadowResources())
	{
		findShadowLightIndices();
		for (int s = 0; s < m_shadowCount; ++s)
		{
			m_shadowLightSpaceMatrices[s] = computeLightSpaceMatrix(m_sceneLights[m_shadowLightIndices[s]]);
		}
		if (m_shadowCount > 0)
		{
			renderShadowMap(m_shadowCasterList);

			// Restore main viewport
			glViewport(0, 0, width, height);
		}
	}

	// ---- Point light shadow map pass (cube maps) ----
	m_pointShadowCount = 0;
	if (m_shadowEnabled && ensurePointShadowResources())
	{
		findPointShadowLightIndices();
		if (m_pointShadowCount > 0)
		{
			renderPointShadowMaps(m_shadowCasterList);

			// Restore main viewport
			glViewport(0, 0, width, height);
		}
	}

	// Render sorted draw list
	if (m_wireframeEnabled)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	GLuint lastProgram = 0;
	for (const auto& cmd : m_drawList)
	{
		cmd.obj->setMatrices(cmd.modelMatrix, view, m_projectionMatrix);
		cmd.obj->setShadowData(m_shadowDepthArray, m_shadowLightSpaceMatrices, m_shadowLightIndices, m_shadowCount);
		cmd.obj->setPointShadowData(m_pointShadowCubeArray, m_pointShadowPositions, m_pointShadowFarPlanes, m_pointShadowLightIndices, m_pointShadowCount);
		if (cmd.hasEmission)
		{
			cmd.obj->setLightData(lightPosition, cmd.emissionColor, lightIntensity);
			cmd.obj->setLights(m_sceneLights);
			cmd.obj->render();
			lastProgram = 0;
		}
		else if (cmd.program != lastProgram)
		{
			cmd.obj->setLightData(lightPosition, lightColor, lightIntensity);
			cmd.obj->setLights(m_sceneLights);
			cmd.obj->render();
			lastProgram = cmd.program;
		}
		else
		{
			cmd.obj->renderBatchContinuation();
		}

		if (m_boundsDebugEnabled && cmd.hasBounds)
		{
			const glm::vec3 center = (cmd.boundsMin + cmd.boundsMax) * 0.5f;
			const glm::vec3 extent = (cmd.boundsMax - cmd.boundsMin) * 0.5f;
			drawBoundsDebugBox(center, extent, viewProj);
			lastProgram = 0;
		}
	}

	if (m_wireframeEnabled)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}

	// Build HZB from current frame's depth buffer for next frame's occlusion tests
	// Skip HZB when scene has few objects (overhead exceeds benefit)
	static constexpr uint32_t kHzbMinObjects = 20;
	if (m_occlusionEnabled && m_lastTotalCount >= kHzbMinObjects)
	{
		buildHzb();
	}

	// Pick pass: full scene render only on actual click; single-entity for outline
	if (m_pickRequested)
	{
		if (ensurePickFbo(width, height))
		{
			renderPickBuffer(view, m_projectionMatrix);
		}
		m_pickRequested = false;
		if (!diagnostics.isPIEActive())
		{
			const unsigned int picked = pickEntityAt(m_pickX, m_pickY);
			m_selectedEntity = picked;
			m_uiManager.selectEntity(picked);
		}
	}

	// Draw selection outline: render only the selected entity into pick buffer (1 draw call)
	if (m_selectedEntity != 0 && !diagnostics.isPIEActive())
	{
		if (ensurePickFbo(width, height))
		{
			renderPickBufferSingleEntity(view, m_projectionMatrix, m_selectedEntity);
		}
		if (m_pickColorTex != 0)
		{
			drawSelectionOutline();
		}

		// Update gizmo hover highlight
		if (!m_gizmoDragging)
		{
			const Vec2 mousePos = m_uiManager.getMousePosition();
			m_gizmoHoveredAxis = pickGizmoAxis(view, m_projectionMatrix,
				static_cast<int>(mousePos.x), static_cast<int>(mousePos.y));
		}

		// Draw gizmo overlay on top of the scene
		renderGizmo(view, m_projectionMatrix);
	}
}

void OpenGLRenderer::refreshEntity(ECS::Entity entity)
{
	if (entity == 0)
		return;

	auto& ecs = ECS::ECSManager::Instance();
	const bool hasMesh = ecs.hasComponent<ECS::MeshComponent>(entity);
	const bool hasMat = ecs.hasComponent<ECS::MaterialComponent>(entity);

	// Helper: try to find and update an existing entry in a list.
	// Returns true if the entity was found (updated or removed).
	auto updateList = [&](std::vector<RenderEntry>& entries, const std::string& fragDefault) -> bool
	{
		for (auto it = entries.begin(); it != entries.end(); ++it)
		{
			if (it->entity != entity)
				continue;

			if (!hasMesh)
			{
				// Entity no longer has a mesh → remove from list
				entries.erase(it);
				return true;
			}

			auto renderable = m_resourceManager.refreshEntityRenderable(entity, fragDefault);
			if (renderable.entity == 0 || (!renderable.object3D && !renderable.object2D))
			{
				entries.erase(it);
				return true;
			}

			it->object3D = renderable.object3D;
			it->object2D = renderable.object2D;
			it->transform = renderable.transform;
			return true;
		}
		return false;
	};

	// Try render entries first (entities with Mesh+Material)
	if (updateList(m_renderEntries, ""))
	{
		// If entity was removed from renderEntries but still has a mesh (without material),
		// it might need to go into meshEntries.
		if (hasMesh && !hasMat)
		{
			// Check if already in meshEntries
			bool inMesh = false;
			for (const auto& e : m_meshEntries)
			{
				if (e.entity == entity) { inMesh = true; break; }
			}
			if (!inMesh)
			{
				auto renderable = m_resourceManager.refreshEntityRenderable(entity, "grid_fragment.glsl");
				if (renderable.entity != 0 && (renderable.object3D || renderable.object2D))
				{
					RenderEntry entry;
					entry.entity = renderable.entity;
					entry.transform = renderable.transform;
					entry.object3D = renderable.object3D;
					entry.object2D = renderable.object2D;
					m_meshEntries.push_back(std::move(entry));
				}
			}
		}
		return;
	}

	// Try mesh-only entries
	if (updateList(m_meshEntries, "grid_fragment.glsl"))
	{
		// If entity gained a material, it should move to renderEntries
		if (hasMesh && hasMat)
		{
			bool inRender = false;
			for (const auto& e : m_renderEntries)
			{
				if (e.entity == entity) { inRender = true; break; }
			}
			if (!inRender)
			{
				auto renderable = m_resourceManager.refreshEntityRenderable(entity, "");
				if (renderable.entity != 0 && (renderable.object3D || renderable.object2D))
				{
					RenderEntry entry;
					entry.entity = renderable.entity;
					entry.transform = renderable.transform;
					entry.object3D = renderable.object3D;
					entry.object2D = renderable.object2D;
					m_renderEntries.push_back(std::move(entry));
				}
			}
		}
		return;
	}

	// Entity wasn't in either list — it's new. Add if it has a mesh.
	if (hasMesh)
	{
		const std::string fragDefault = hasMat ? "" : "grid_fragment.glsl";
		auto renderable = m_resourceManager.refreshEntityRenderable(entity, fragDefault);
		if (renderable.entity != 0 && (renderable.object3D || renderable.object2D))
		{
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = renderable.object3D;
			entry.object2D = renderable.object2D;
			if (hasMat)
				m_renderEntries.push_back(std::move(entry));
			else
				m_meshEntries.push_back(std::move(entry));
		}
	}
}

bool OpenGLRenderer::ensureUiFbo(int width, int height)
{
    if (m_uiFbo != 0 && m_uiFboWidth == width && m_uiFboHeight == height)
    {
        return true;
    }

    releaseUiFbo();

    m_uiFboWidth = width;
    m_uiFboHeight = height;

    glGenFramebuffers(1, &m_uiFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);

    glGenTextures(1, &m_uiFboTexture);
    glBindTexture(GL_TEXTURE_2D, m_uiFboTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_uiFboTexture, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        releaseUiFbo();
        return false;
    }

    m_uiFboCacheValid = false;
    return true;
}

void OpenGLRenderer::releaseUiFbo()
{
    if (m_uiFboTexture)
    {
        glDeleteTextures(1, &m_uiFboTexture);
        m_uiFboTexture = 0;
    }
    if (m_uiFbo)
    {
        glDeleteFramebuffers(1, &m_uiFbo);
        m_uiFbo = 0;
    }
    m_uiFboWidth = 0;
    m_uiFboHeight = 0;
    m_uiFboCacheValid = false;
}

void OpenGLRenderer::blitUiCache(int width, int height)
{
    if (m_uiFboTexture == 0)
    {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // Use standard Y-up projection so texture V=0 (bottom) maps to screen bottom
    // and V=1 (top) maps to screen top, matching how the FBO stores the UI.
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
    drawUIImage(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), m_uiFboTexture, projection);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// drawUIWidgetsToFramebuffer
// Renders all widgets of 'mgr' into the currently-bound framebuffer.
// Caller is responsible for binding the target FBO and setting glViewport.
// ---------------------------------------------------------------------------
void OpenGLRenderer::drawUIWidgetsToFramebuffer(UIManager& mgr, int width, int height)
{
    if (!m_textRenderer || !ensureUIQuadRenderer()) return;

    m_textRenderer->setScreenSize(width, height);
    mgr.setAvailableViewportSize(Vec2{ static_cast<float>(width), static_cast<float>(height) });

    if (mgr.needsLayoutUpdate())
    {
        mgr.updateLayouts([this](const std::string& text, float scale)
            { return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{}; });
    }

    ensureUIShaderDefaults();
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    const glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);

    struct DeferredDropDownCompact { float x0, y0, x1, y1, fontSize; int selectedIndex; std::vector<std::string> items; Vec4 textColor, hoverColor; Vec2 padding; };
    std::vector<DeferredDropDownCompact> deferredDropDownsCompact;

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
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, prog, element.color, false);
            if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{1.0f,0.9f,0.1f,1.0f}, uiProjection, prog);
            return;
        }
        if (element.type == WidgetElementType::ColorPicker)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            if (element.isCompact)
            {
                if (element.color.w > 0.0f) drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, prog, element.color, false);
                for (const auto& child : element.children) self(self, child, x0, y0, widthPx, heightPx);
                if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.6f,0.9f,0.2f,1.0f}, uiProjection, prog);
                return;
            }
            const int columns = 24; const int rows = 6;
            const float cellW = widthPx / static_cast<float>(columns);
            const float cellH = heightPx / static_cast<float>(rows);
            for (int row = 0; row < rows; ++row)
            {
                const float value = 1.0f - (static_cast<float>(row) / std::max(1.0f, static_cast<float>(rows-1)));
                for (int col = 0; col < columns; ++col)
                {
                    const float hue = static_cast<float>(col) / std::max(1.0f, static_cast<float>(columns-1));
                    const Vec4 c = HsvToRgb(hue, 1.0f, value);
                    drawUIPanel(x0+cellW*col, y0+cellH*row, x0+cellW*(col+1), y0+cellH*(row+1), c, uiProjection, prog, c, false);
                }
            }
            const float sw = std::min(heightPx, 18.0f);
            if (sw > 0.0f)
            {
                const float sx = x1 - sw - 4.0f, sy = y0 + 4.0f;
                drawUIPanel(sx, sy, sx+sw, sy+sw, element.color, uiProjection, prog, element.color, false);
                drawUIOutline(sx, sy, sx+sw, sy+sw, Vec4{0.1f,0.1f,0.1f,0.9f}, uiProjection, prog);
            }
            if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.6f,0.9f,0.2f,1.0f}, uiProjection, prog);
            return;
        }
        if (element.type == WidgetElementType::Text)
        {
            const float wPx = std::max(0.0f, x1-x0), hPx = std::max(0.0f, y1-y0);
            const float cx0 = x0+element.padding.x, cy0 = y0+element.padding.y;
            const float cx1 = x1-element.padding.x, cy1 = y1-element.padding.y;
            const float cw = std::max(0.0f, cx1-cx0), ch = std::max(0.0f, cy1-cy0);
            float scale = (element.fontSize > 0.0f) ? (element.fontSize/48.0f) : 1.0f;
            if (element.fontSize <= 0.0f)
            {
                const Vec2 ts = m_textRenderer->measureText(element.text, 1.0f);
                if (ts.x > 0.0f && ts.y > 0.0f) scale = std::min(wPx/ts.x, hPx/ts.y)*0.9f;
                else if (hPx > 0.0f) scale = hPx/48.0f;
            }
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultTextVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultTextFragment);
            const auto drawLine = [&](const std::string& line, const Vec2& pos)
            {
                if (!element.shaderVertex.empty() || !element.shaderFragment.empty())
                    m_textRenderer->drawTextWithShader(line, pos, scale, element.textColor, vp, fp);
                else
                    m_textRenderer->drawText(line, pos, scale, element.textColor);
            };
            if (element.wrapText && cw > 0.0f)
            {
                std::vector<std::string> lines;
                std::vector<std::string> paragraphs;
                std::string para;
                for (char c : element.text) { if (c=='\n'){paragraphs.push_back(para);para.clear();}else para.push_back(c); }
                paragraphs.push_back(para);
                const auto wrapWord = [&](const std::string& word, std::string& cur)
                {
                    const Vec2 ws = m_textRenderer->measureText(word, scale);
                    if (cur.empty() && ws.x > cw)
                    {
                        std::string chunk;
                        for (char wc : word)
                        {
                            std::string cand = chunk; cand.push_back(wc);
                            if (m_textRenderer->measureText(cand, scale).x > cw && !chunk.empty()) { lines.push_back(chunk); chunk.clear(); chunk.push_back(wc); }
                            else chunk = cand;
                        }
                        cur = chunk; return;
                    }
                    const std::string cand = cur.empty() ? word : (cur+" "+word);
                    if (m_textRenderer->measureText(cand, scale).x <= cw || cur.empty()) cur = cand;
                    else { lines.push_back(cur); cur = word; }
                };
                for (const auto& p : paragraphs)
                {
                    std::string cur, wrd;
                    for (char c : p) { if (std::isspace(static_cast<unsigned char>(c))) { if (!wrd.empty()){ wrapWord(wrd,cur); wrd.clear(); } } else wrd.push_back(c); }
                    if (!wrd.empty()) wrapWord(wrd, cur);
                    if (!cur.empty()) lines.push_back(cur);
                }
                const float lh = m_textRenderer->getLineHeight(scale);
                const float th = lh * static_cast<float>(lines.size());
                float sy = cy0;
                if (element.textAlignV==TextAlignV::Center) sy = cy0+(ch-th)*0.5f;
                else if (element.textAlignV==TextAlignV::Bottom) sy = cy1-th;
                for (size_t i=0;i<lines.size();++i)
                {
                    const Vec2 ls = m_textRenderer->measureText(lines[i], scale);
                    float tx = cx0;
                    if (element.textAlignH==TextAlignH::Center) tx = cx0+(cw-ls.x)*0.5f;
                    else if (element.textAlignH==TextAlignH::Right) tx = cx1-ls.x;
                    drawLine(lines[i], Vec2{tx, sy+lh*static_cast<float>(i)});
                }
            }
            else
            {
                Vec2 ts = m_textRenderer->measureText(element.text, scale);
                float tx = cx0, ty = cy0;
                if (element.textAlignH==TextAlignH::Center) tx = cx0+(cw-ts.x)*0.5f;
                else if (element.textAlignH==TextAlignH::Right) tx = cx1-ts.x;
                if (element.textAlignV==TextAlignV::Center) ty = cy0+(ch-ts.y)*0.5f;
                else if (element.textAlignV==TextAlignV::Bottom) ty = cy1-ts.y;
                drawLine(element.text, Vec2{tx, ty});
            }
            if (m_uiDebugEnabled)
            {
                const GLuint prog = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
                drawUIOutline(x0, y0, x1, y1, Vec4{0.1f,0.8f,1.0f,1.0f}, uiProjection, prog);
            }
            return;
        }
        if (element.type == WidgetElementType::ProgressBar)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, prog, element.color, false);
            const float range = element.maxValue - element.minValue;
            const float ratio = (range>0.0f) ? std::clamp((element.valueFloat-element.minValue)/range, 0.0f, 1.0f) : 0.0f;
            if (ratio > 0.0f) drawUIPanel(x0, y0, x0+widthPx*ratio, y1, element.fillColor, uiProjection, prog, element.fillColor, false);
            if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.2f,0.9f,0.6f,1.0f}, uiProjection, prog);
            return;
        }
        if (element.type == WidgetElementType::Slider)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            const float range = element.maxValue - element.minValue;
            const float ratio = (range>0.0f) ? std::clamp((element.valueFloat-element.minValue)/range, 0.0f, 1.0f) : 0.0f;
            const float th = std::min(heightPx, 6.0f);
            const float ty0 = y0+(heightPx-th)*0.5f, ty1 = ty0+th;
            drawUIPanel(x0, ty0, x1, ty1, element.color, uiProjection, prog, element.color, false);
            if (ratio > 0.0f) drawUIPanel(x0, ty0, x0+widthPx*ratio, ty1, element.fillColor, uiProjection, prog, element.fillColor, false);
            const float hs = std::max(10.0f, heightPx);
            float hx = std::clamp(x0+widthPx*ratio-hs*0.5f, x0, x1-hs);
            drawUIPanel(hx, y0, hx+hs, y1, element.textColor, uiProjection, prog, element.textColor, false);
            if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.2f,0.7f,1.0f,1.0f}, uiProjection, prog);
            return;
        }
        if (element.type == WidgetElementType::EntryBar)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, prog, element.hoverColor, element.isHovered);
            const float fs = (element.fontSize>0.0f)?element.fontSize:14.0f, sc = fs/48.0f;
            std::string disp = element.value;
            if (element.isPassword) disp.assign(element.value.size(), '*');
            const float cx0e = x0+element.padding.x, cy0e = y0+element.padding.y;
            const float cx1e = x1-element.padding.x, cy1e = y1-element.padding.y;
            const float chE = std::max(0.0f, cy1e-cy0e);
            Vec2 ts{};
            if (!disp.empty())
            {
                ts = m_textRenderer->measureText(disp, sc);
                m_textRenderer->drawText(disp, Vec2{cx0e, cy0e+std::max(0.0f,(chE-ts.y)*0.5f)}, sc, element.textColor);
            }
            if (element.isFocused)
            {
                const float ch2 = std::max(0.0f, chE-2.0f);
                const float cx = cx0e+ts.x+1.0f;
                if (cx < cx1e) drawUIPanel(cx, cy0e+1.0f, cx+2.0f, cy0e+1.0f+ch2, element.textColor, uiProjection, prog, element.textColor, false);
            }
            if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.5f,0.8f,1.0f,1.0f}, uiProjection, prog);
            return;
        }
        if (element.type == WidgetElementType::Button)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, prog, element.hoverColor, element.isHovered);
            if (!element.text.empty())
            {
                const float cx0b=x0+element.padding.x, cy0b=y0+element.padding.y;
                const float cx1b=x1-element.padding.x, cy1b=y1-element.padding.y;
                const float cwB=std::max(0.0f,cx1b-cx0b), chB=std::max(0.0f,cy1b-cy0b);
                float sc = (element.fontSize>0.0f) ? element.fontSize/48.0f : 1.0f;
                if (element.fontSize<=0.0f)
                {
                    const Vec2 ts=m_textRenderer->measureText(element.text,1.0f);
                    if (ts.x>0.0f&&ts.y>0.0f) sc=std::min(cwB/ts.x,chB/ts.y)*0.9f;
                }
                Vec2 ts=m_textRenderer->measureText(element.text,sc);
                float tx=cx0b, ty=cy0b;
                if (element.textAlignH==TextAlignH::Center) tx=cx0b+(cwB-ts.x)*0.5f;
                else if (element.textAlignH==TextAlignH::Right) tx=cx1b-ts.x;
                if (element.textAlignV==TextAlignV::Center) ty=cy0b+(chB-ts.y)*0.5f;
                else if (element.textAlignV==TextAlignV::Bottom) ty=cy1b-ts.y;
                m_textRenderer->drawText(element.text, Vec2{tx,ty}, sc, element.textColor);
            }
            else if (element.textureId!=0 || !element.imagePath.empty())
            {
                const GLuint tex=(element.textureId!=0)?static_cast<GLuint>(element.textureId):getOrLoadUITexture(element.imagePath);
                if (tex!=0) drawUIImage(x0+4.0f, y0+4.0f, x1-4.0f, y1-4.0f, tex, uiProjection);
            }
            for (const auto& child : element.children) self(self, child, x0, y0, x1-x0, y1-y0);
            if (m_uiDebugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.7f,1.0f,0.3f,1.0f}, uiProjection, prog);
            return;
        }
        if (element.type == WidgetElementType::Image)
        {
            if (element.textureId!=0 || !element.imagePath.empty())
            {
                const GLuint tex=(element.textureId!=0)?static_cast<GLuint>(element.textureId):getOrLoadUITexture(element.imagePath);
                if (tex!=0) drawUIImage(x0, y0, x1, y1, tex, uiProjection, element.color, true);
            }
            if (m_uiDebugEnabled)
            {
                const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
                drawUIOutline(x0, y0, x1, y1, Vec4{0.2f,1.0f,1.0f,1.0f}, uiProjection, prog);
            }
            return;
        }
        if (element.type == WidgetElementType::CheckBox)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            const float bs=std::min(heightPx-2.0f,16.0f);
            const float bx0=x0+element.padding.x, by0=y0+(heightPx-bs)*0.5f, bx1=bx0+bs, by1=by0+bs;
            drawUIPanel(bx0,by0,bx1,by1,element.color,uiProjection,prog,element.hoverColor,element.isHovered);
            drawUIOutline(bx0,by0,bx1,by1,Vec4{0.4f,0.4f,0.45f,0.9f},uiProjection,prog);
            if (element.isChecked)
            {
                const float inset=3.0f;
                drawUIPanel(bx0+inset,by0+inset,bx1-inset,by1-inset,element.fillColor,uiProjection,prog,element.fillColor,false);
            }
            if (!element.text.empty())
            {
                const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;
                const Vec2 ts=m_textRenderer->measureText(element.text,sc);
                m_textRenderer->drawText(element.text,Vec2{bx1+6.0f,y0+(heightPx-ts.y)*0.5f},sc,element.textColor);
            }
            if (m_uiDebugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.5f,0.2f,1.0f},uiProjection,prog);
            return;
        }
        if (element.type == WidgetElementType::DropdownButton)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0,y0,x1,y1,element.color,uiProjection,prog,element.hoverColor,element.isHovered);
            const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;

            // Draw image icon (if set, like a Button)
            if (!element.imagePath.empty() && element.textureId != 0)
            {
                const float imgSize=std::min(widthPx-element.padding.x*2.0f-12.0f,heightPx-element.padding.y*2.0f);
                if (imgSize>0.0f)
                {
                    const float ix0=x0+element.padding.x, iy0=y0+(heightPx-imgSize)*0.5f;
                    drawUIImage(ix0,iy0,ix0+imgSize,iy0+imgSize,element.textureId,uiProjection,element.textColor);
                }
            }

            // Draw text
            if (!element.text.empty())
            {
                const Vec2 ts=m_textRenderer->measureText(element.text,sc);
                float tx=x0+element.padding.x;
                if (!element.imagePath.empty() && element.textureId!=0)
                {
                    const float imgSize=std::min(widthPx-element.padding.x*2.0f-12.0f,heightPx-element.padding.y*2.0f);
                    tx+=imgSize+4.0f;
                }
                const float ty=y0+(heightPx-ts.y)*0.5f;
                m_textRenderer->drawText(element.text,Vec2{tx,ty},sc,element.textColor);
            }

            // Draw small down-arrow indicator on the right
            const float arrowSize=std::min(heightPx*0.3f,6.0f);
            const float arrowX=x1-element.padding.x-arrowSize;
            const float arrowY=y0+(heightPx-arrowSize)*0.5f;
            drawUIPanel(arrowX,arrowY,arrowX+arrowSize,arrowY+arrowSize,element.textColor,uiProjection,prog,element.textColor,false);

            if (m_uiDebugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.7f,0.1f,1.0f},uiProjection,prog);
            return;
        }
        if (element.type == WidgetElementType::DropDown)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0,y0,x1,y1,element.color,uiProjection,prog,element.hoverColor,element.isHovered);
            const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;
            const float cx0d=x0+element.padding.x, cy0d=y0+element.padding.y, cy1d=y1-element.padding.y;
            const float chD=std::max(0.0f,cy1d-cy0d);
            std::string disp=element.text;
            if (disp.empty()&&element.selectedIndex>=0&&element.selectedIndex<static_cast<int>(element.items.size()))
                disp=element.items[static_cast<size_t>(element.selectedIndex)];
            if (!disp.empty())
            {
                const Vec2 ts=m_textRenderer->measureText(disp,sc);
                m_textRenderer->drawText(disp,Vec2{cx0d,cy0d+std::max(0.0f,(chD-ts.y)*0.5f)},sc,element.textColor);
            }
            const float as=std::min(heightPx*0.4f,8.0f);
            drawUIPanel(x1-element.padding.x-as, y0+(heightPx-as)*0.5f, x1-element.padding.x, y0+(heightPx-as)*0.5f+as, element.textColor,uiProjection,prog,element.textColor,false);
            if (element.isExpanded && !element.items.empty())
            {
                const float fs=(element.fontSize>0.0f)?element.fontSize:14.0f;
                deferredDropDownsCompact.push_back({ x0, y0, x1, y1, fs, element.selectedIndex, element.items, element.textColor, element.hoverColor, element.padding });
            }
            if (m_uiDebugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.6f,0.1f,1.0f},uiProjection,prog);
            return;
        }
        if (element.type == WidgetElementType::DropdownButton)
        {
            const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
            const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            drawUIPanel(x0,y0,x1,y1,element.color,uiProjection,prog,element.hoverColor,element.isHovered);
            if (!element.text.empty())
            {
                const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;
                const float cx0b=x0+element.padding.x, cy0b=y0+element.padding.y;
                const float cx1b=x1-element.padding.x, cy1b=y1-element.padding.y;
                const float cwB=std::max(0.0f,cx1b-cx0b), chB=std::max(0.0f,cy1b-cy0b);
                const Vec2 ts=m_textRenderer->measureText(element.text,sc);
                float textX=cx0b, textY=cy0b+std::max(0.0f,(chB-ts.y)*0.5f);
                switch(element.textAlignH){case TextAlignH::Center: textX=cx0b+(cwB-ts.x)*0.5f; break; case TextAlignH::Right: textX=cx1b-ts.x; break; default: break;}
                m_textRenderer->drawText(element.text,Vec2{textX,textY},sc,element.textColor);
            }
            const float as=std::min(heightPx*0.3f,6.0f);
            const float arrowX=x1-element.padding.x-as;
            const float arrowY=y0+(heightPx-as)*0.5f;
            drawUIPanel(arrowX,arrowY,arrowX+as,arrowY+as,element.textColor,uiProjection,prog,element.textColor,false);
            if (m_uiDebugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.7f,0.1f,1.0f},uiProjection,prog);
            return;
        }
        if (element.type == WidgetElementType::TreeView || element.type == WidgetElementType::TabView)
        {
            if (element.color.w > 0.0f)
            {
                const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                drawUIPanel(x0,y0,x1,y1,element.color,uiProjection,getUIQuadProgram(vp,fp),element.color,false);
            }
            const GLint cx=static_cast<GLint>(x0);
            const GLint cy=static_cast<GLint>(static_cast<float>(height)-y1);
            const GLsizei cw=static_cast<GLsizei>(std::max(0.0f,widthPx));
            const GLsizei ch=static_cast<GLsizei>(std::max(0.0f,heightPx));
            glEnable(GL_SCISSOR_TEST); glScissor(cx,cy,cw,ch);
            for (const auto& child : element.children) self(self, child, x0, y0, widthPx, heightPx);
            glDisable(GL_SCISSOR_TEST);
            if (m_uiDebugEnabled)
            {
                const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
                drawUIOutline(x0,y0,x1,y1,Vec4{0.4f,0.9f,0.6f,1.0f},uiProjection,prog);
            }
            return;
        }
        if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::Grid)
        {
            if (element.color.w > 0.0f)
            {
                const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                drawUIPanel(x0,y0,x1,y1,element.color,uiProjection,getUIQuadProgram(vp,fp),element.color,false);
            }
            if (element.scrollable)
            {
                const GLint cx=static_cast<GLint>(x0);
                const GLint cy=static_cast<GLint>(static_cast<float>(height)-y1);
                const GLsizei cw=static_cast<GLsizei>(std::max(0.0f,widthPx));
                const GLsizei ch=static_cast<GLsizei>(std::max(0.0f,heightPx));
                glEnable(GL_SCISSOR_TEST); glScissor(cx,cy,cw,ch);
                for (const auto& child : element.children) self(self, child, x0, y0, widthPx, heightPx);
                glDisable(GL_SCISSOR_TEST);
            }
            else
            {
                for (const auto& child : element.children) self(self, child, x0, y0, widthPx, heightPx);
            }
            if (m_uiDebugEnabled)
            {
                const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
                drawUIOutline(x0,y0,x1,y1,Vec4{1.0f,0.4f,0.8f,1.0f},uiProjection,prog);
            }
        }
    };

    const auto& ordered = mgr.getWidgetsOrderedByZ();
    for (const auto* entry : ordered)
    {
        if (!entry || !entry->widget) continue;
        const auto& widget = entry->widget;
        Vec2 widgetSize = widget->getSizePixels();
        Vec2 widgetPosition{};
        if (widget->hasComputedSize())    widgetSize    = widget->getComputedSizePixels();
        if (widget->hasComputedPosition()) widgetPosition = widget->getComputedPositionPixels();
        if (widgetSize.x <= 0.0f) widgetSize.x = static_cast<float>(width);
        if (widgetSize.y <= 0.0f) widgetSize.y = static_cast<float>(height);
        for (const auto& element : widget->getElements())
            renderElement(renderElement, element, widgetPosition.x, widgetPosition.y, widgetSize.x, widgetSize.y);
    }

    // Deferred pass: draw expanded DropDown items on top of everything
    for (const auto& dd : deferredDropDownsCompact)
    {
        const std::string& vp = resolveUIShaderPath("", m_defaultPanelVertex);
        const std::string& fp = resolveUIShaderPath("", m_defaultPanelFragment);
        const GLuint prog = getUIQuadProgram(vp, fp);
        const float sc = dd.fontSize / 48.0f;
        const float heightPx = dd.y1 - dd.y0;
        const float ih = std::max(20.0f, heightPx);
        const float cx0d = dd.x0 + dd.padding.x;
        for (size_t i = 0; i < dd.items.size(); ++i)
        {
            const float iy0 = dd.y1 + static_cast<float>(i) * ih;
            const float iy1 = iy0 + ih;
            const bool sel = (static_cast<int>(i) == dd.selectedIndex);
            const Vec4 ic = sel ? Vec4{0.22f,0.22f,0.28f,0.98f} : Vec4{0.14f,0.14f,0.18f,0.98f};
            drawUIPanel(dd.x0, iy0, dd.x1, iy1, ic, uiProjection, prog, dd.hoverColor, false);
            const Vec2 its = m_textRenderer->measureText(dd.items[i], sc);
            m_textRenderer->drawText(dd.items[i], Vec2{cx0d, iy0 + (ih - its.y) * 0.5f}, sc, dd.textColor);
        }
    }

    mgr.clearRenderDirty();
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// ensurePopupUIVao – creates a context-local VAO in the currently-current
// popup GL context that reuses the shared m_uiQuadVbo.
// ---------------------------------------------------------------------------
void OpenGLRenderer::ensurePopupUIVao()
{
    if (m_popupUiVao != 0) return;
    if (m_uiQuadVbo == 0) return; // main VBO must exist first

    glGenVertexArrays(1, &m_popupUiVao);
    glBindVertexArray(m_popupUiVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo); // shared VBO is accessible
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// renderPopupWindows – renders every open popup window.
// ---------------------------------------------------------------------------
void OpenGLRenderer::renderPopupWindows()
{
    if (m_popupWindows.empty()) return;

    // Clean up popups that have been marked as closed (deferred destruction).
    for (auto it = m_popupWindows.begin(); it != m_popupWindows.end(); )
    {
        if (it->second && !it->second->isOpen())
        {
            it->second->destroy();
            it = m_popupWindows.erase(it);

            // The popup's GL context was destroyed, invalidating context-local VAOs.
            // Reset handles so they get recreated for the next popup.
            m_popupUiVao = 0;
            if (m_textRenderer)
            {
                m_textRenderer->resetPopupVao();
            }
        }
        else
        {
            ++it;
        }
    }

    if (m_popupWindows.empty()) return;
    if (!m_textRenderer) return;

    // Ensure main context resources are ready before we switch contexts.
    ensureUIQuadRenderer();
    ensureUIShaderDefaults();

    for (auto& [id, popup] : m_popupWindows)
    {
        if (!popup || !popup->isOpen()) continue;
        if (!popup->sdlWindow() || !popup->glContext()) continue;

        SDL_GL_MakeCurrent(popup->sdlWindow(), popup->glContext());

        // Create context-local VAOs on first use (VAOs are not shared between GL contexts).
        ensurePopupUIVao();
        m_textRenderer->ensurePopupVao();

        popup->refreshSize();
        const int pw = popup->width();
        const int ph = popup->height();
        if (pw <= 0 || ph <= 0)
        {
            SDL_GL_MakeCurrent(m_window, m_glContext);
            continue;
        }

        glViewport(0, 0, pw, ph);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.13f, 0.13f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Temporarily swap in the popup VAOs so drawUIPanel/drawUIImage/drawText use them.
        const GLuint savedVao = m_uiQuadVao;
        m_uiQuadVao = m_popupUiVao;
        const GLuint savedTextVao = m_textRenderer->swapVao(m_textRenderer->getPopupVao());

        drawUIWidgetsToFramebuffer(popup->uiManager(), pw, ph);

        m_uiQuadVao = savedVao;
        m_textRenderer->swapVao(savedTextVao);

        SDL_GL_SwapWindow(popup->sdlWindow());
    }

    // Restore main context.
    SDL_GL_MakeCurrent(m_window, m_glContext);
}

// ---------------------------------------------------------------------------
// Popup window management
// ---------------------------------------------------------------------------
PopupWindow* OpenGLRenderer::openPopupWindow(const std::string& id, const std::string& title, int width, int height)
{
    auto it = m_popupWindows.find(id);
    if (it != m_popupWindows.end())
    {
        if (it->second && it->second->isOpen())
        {
            SDL_RaiseWindow(it->second->sdlWindow());
            return it->second.get();
        }
        m_popupWindows.erase(it);
    }

    // Main context must be current when creating the shared child context.
    SDL_GL_MakeCurrent(m_window, m_glContext);

    auto popup = std::make_unique<PopupWindow>();
    if (!popup->create(title, width, height))
        return nullptr;

    // Restore main context after popup creation.
    SDL_GL_MakeCurrent(m_window, m_glContext);

    PopupWindow* ptr = popup.get();
    m_popupWindows[id] = std::move(popup);
    Logger::Instance().log(Logger::Category::Rendering,
        "Popup window opened: " + id, Logger::LogLevel::INFO);
    return ptr;
}

void OpenGLRenderer::closePopupWindow(const std::string& id)
{
    auto it = m_popupWindows.find(id);
    if (it != m_popupWindows.end())
    {
        // Mark for deferred destruction in renderPopupWindows().
        // Immediate destruction is unsafe when called from within event routing
        // (the popup's UIManager and SDL window may still be referenced on the stack).
        if (it->second)
        {
            it->second->close();
        }
        Logger::Instance().log(Logger::Category::Rendering,
            "Popup window closing: " + id, Logger::LogLevel::INFO);
    }
}

PopupWindow* OpenGLRenderer::getPopupWindow(const std::string& id)
{
    auto it = m_popupWindows.find(id);
    return (it != m_popupWindows.end() && it->second && it->second->isOpen())
        ? it->second.get() : nullptr;
}

bool OpenGLRenderer::routeEventToPopup(SDL_Event& event)
{
    if (m_popupWindows.empty()) return false;

    SDL_WindowID eventWindowId = 0;
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:   eventWindowId = event.button.windowID; break;
    case SDL_EVENT_MOUSE_MOTION:      eventWindowId = event.motion.windowID; break;
    case SDL_EVENT_MOUSE_WHEEL:       eventWindowId = event.wheel.windowID;  break;
    case SDL_EVENT_TEXT_INPUT:        eventWindowId = event.text.windowID;   break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:            eventWindowId = event.key.windowID;    break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        for (auto& [id, popup] : m_popupWindows)
        {
            if (popup && popup->sdlWindow() &&
                SDL_GetWindowID(popup->sdlWindow()) == event.window.windowID)
            {
                popup->close();
                return true;
            }
        }
        return false;
    default: return false;
    }

    for (auto& [id, popup] : m_popupWindows)
    {
        if (!popup || !popup->isOpen() || !popup->sdlWindow()) continue;
        if (SDL_GetWindowID(popup->sdlWindow()) != eventWindowId) continue;

        UIManager& uiMgr = popup->uiManager();
        switch (event.type)
        {
        case SDL_EVENT_MOUSE_MOTION:
        {
            const Vec2 pos{ event.motion.x, event.motion.y };
            uiMgr.setMousePosition(pos);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            const Vec2 pos{ event.button.x, event.button.y };
            uiMgr.setMousePosition(pos);
            uiMgr.handleMouseDown(pos, event.button.button);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            const Vec2 pos{ event.button.x, event.button.y };
            uiMgr.handleMouseUp(pos, event.button.button);
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            uiMgr.handleScroll(uiMgr.getMousePosition(), event.wheel.y);
            return true;
        case SDL_EVENT_TEXT_INPUT:
            uiMgr.handleTextInput(event.text.text);
            return true;
        case SDL_EVENT_KEY_DOWN:
            uiMgr.handleKeyDown(event.key.key);
            return true;
        case SDL_EVENT_KEY_UP:
            return true;
        default: break;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Mesh viewer editor window – takes over viewport, no tab created
// ---------------------------------------------------------------------------
MeshViewerWindow* OpenGLRenderer::openMeshViewer(const std::string& assetPath)
{
    // Return existing viewer if already open – just switch to its tab.
    {
        auto it = m_meshViewers.find(assetPath);
        if (it != m_meshViewers.end() && it->second)
        {
            setActiveTab(assetPath);
            return it->second.get();
        }
    }

    const std::string displayName = std::filesystem::path(assetPath).stem().string();

    m_uiManager.showToastMessage("Loading " + displayName + "...", 4.0f);

    const std::string resolvedPath = m_resourceManager.resolveContentPath(assetPath);
    if (resolvedPath.empty())
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "openMeshViewer: could not resolve content path for '" + assetPath + "'",
            Logger::LogLevel::WARNING);
        m_uiManager.showToastMessage("Failed to resolve " + displayName, 3.0f);
        return nullptr;
    }

    auto& assetMgr = AssetManager::Instance();
    auto existingAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
    if (!existingAsset)
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "openMeshViewer: asset '" + resolvedPath + "' not in memory, calling loadAsset (Sync)...",
            Logger::LogLevel::INFO);
        const int loadId = assetMgr.loadAsset(resolvedPath, AssetType::Model3D, AssetManager::Sync);
        if (loadId == 0)
        {
            Logger::Instance().log(Logger::Category::Rendering,
                "openMeshViewer: loadAsset returned 0 for '" + resolvedPath
                + "' (original='" + assetPath + "'). The asset file may not exist or could not be parsed.",
                Logger::LogLevel::WARNING);
            m_uiManager.showToastMessage("Failed to load " + displayName, 3.0f);
            return nullptr;
        }
        Logger::Instance().log(Logger::Category::Rendering,
            "openMeshViewer: loadAsset returned id=" + std::to_string(loadId) + " for '" + resolvedPath + "'",
            Logger::LogLevel::INFO);
    }
    else
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "openMeshViewer: asset '" + resolvedPath + "' already in memory (id="
            + std::to_string(existingAsset->getId()) + ")",
            Logger::LogLevel::INFO);
    }

    auto viewer = std::make_unique<MeshViewerWindow>();
    if (!viewer->initialize(resolvedPath, m_resourceManager))
    {
        m_uiManager.showToastMessage("Failed to open " + displayName, 3.0f);
        Logger::Instance().log(Logger::Category::Rendering,
            "Mesh viewer init failed: " + resolvedPath + " (see preceding log lines for the specific step that failed)",
            Logger::LogLevel::WARNING);
        return nullptr;
    }

    if (!viewer->createRuntimeLevel(assetPath))
    {
        m_uiManager.showToastMessage("Failed to create preview level for " + displayName, 3.0f);
        return nullptr;
    }

    MeshViewerWindow* ptr = viewer.get();
    m_meshViewers[assetPath] = std::move(viewer);

    // Create a new editor tab for this mesh viewer
    addTab(assetPath, displayName, true);

    // Create a dynamic tab button in the TitleBar.Tabs StackPanel
    {
        auto* tabsStack = m_uiManager.findElementById("TitleBar.Tabs");
        if (tabsStack)
        {
            // Tab button
            WidgetElement tabBtn{};
            tabBtn.type = WidgetElementType::Button;
            tabBtn.id = "TitleBar.Tab." + assetPath;
            tabBtn.clickEvent = "TitleBar.Tab." + assetPath;
            tabBtn.text = displayName;
            tabBtn.font = "default.ttf";
            tabBtn.fontSize = 12.0f;
            tabBtn.textAlignH = TextAlignH::Center;
            tabBtn.textAlignV = TextAlignV::Center;
            tabBtn.fillY = true;
            tabBtn.color = Vec4{ 0.14f, 0.14f, 0.14f, 1.0f };
            tabBtn.hoverColor = Vec4{ 0.2f, 0.2f, 0.2f, 1.0f };
            tabBtn.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
            tabBtn.minSize = Vec2{ 90.0f, 0.0f };
            tabBtn.padding = Vec2{ 6.0f, 0.0f };
            tabBtn.isHitTestable = true;
            tabBtn.runtimeOnly = true;
            tabsStack->children.push_back(std::move(tabBtn));

            // Close button (×) next to the tab
            WidgetElement closeBtn{};
            closeBtn.type = WidgetElementType::Button;
            closeBtn.id = "TitleBar.TabClose." + assetPath;
            closeBtn.clickEvent = "TitleBar.TabClose." + assetPath;
            closeBtn.text = "x";
            closeBtn.font = "default.ttf";
            closeBtn.fontSize = 12.0f;
            closeBtn.textAlignH = TextAlignH::Center;
            closeBtn.textAlignV = TextAlignV::Center;
            closeBtn.fillY = true;
            closeBtn.color = Vec4{ 0.14f, 0.14f, 0.14f, 1.0f };
            closeBtn.hoverColor = Vec4{ 0.6f, 0.15f, 0.15f, 1.0f };
            closeBtn.textColor = Vec4{ 0.7f, 0.7f, 0.7f, 1.0f };
            closeBtn.minSize = Vec2{ 24.0f, 0.0f };
            closeBtn.padding = Vec2{ 2.0f, 0.0f };
            closeBtn.isHitTestable = true;
            closeBtn.runtimeOnly = true;
            tabsStack->children.push_back(std::move(closeBtn));
        }
    }

    // Register click events for the tab button and close button
    m_uiManager.registerClickEvent("TitleBar.Tab." + assetPath, [this, assetPath]()
    {
        setActiveTab(assetPath);
        m_uiManager.markAllWidgetsDirty();
    });
    m_uiManager.registerClickEvent("TitleBar.TabClose." + assetPath, [this, assetPath]()
    {
        closeMeshViewer(assetPath);
    });

    // Register the details panel widget (tab-scoped – only visible when this tab is active)
    {
        auto propsWidget = std::make_shared<Widget>();
        propsWidget->setName("MeshViewerDetails." + assetPath);
        propsWidget->setAnchor(WidgetAnchor::TopRight);
        propsWidget->setSizePixels(Vec2{ 320.0f, 0.0f });
        propsWidget->setFillY(true);
        propsWidget->setZOrder(1);

        std::vector<WidgetElement> elements;

        WidgetElement root{};
        root.type = WidgetElementType::StackPanel;
        root.id = "MeshViewer.Details.Root";
        root.from = Vec2{ 0.0f, 0.0f };
        root.to = Vec2{ 1.0f, 1.0f };
        root.fillX = true;
        root.fillY = true;
        root.color = Vec4{ 0.14f, 0.14f, 0.18f, 0.95f };
        root.padding = Vec2{ 10.0f, 10.0f };
        root.scrollable = true;

        WidgetElement title{};
        title.type = WidgetElementType::Text;
        title.id = "MeshViewer.Details.Title";
        title.text = displayName;
        title.font = "default.ttf";
        title.fontSize = 16.0f;
        title.textColor = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
        title.fillX = true;
        title.minSize = Vec2{ 0.0f, 28.0f };
        title.runtimeOnly = true;
        root.children.push_back(std::move(title));

        WidgetElement pathLine{};
        pathLine.type = WidgetElementType::Text;
        pathLine.id = "MeshViewer.Details.Path";
        pathLine.text = "Path: " + assetPath;
        pathLine.font = "default.ttf";
        pathLine.fontSize = 12.0f;
        pathLine.textColor = Vec4{ 0.7f, 0.7f, 0.75f, 1.0f };
        pathLine.fillX = true;
        pathLine.minSize = Vec2{ 0.0f, 20.0f };
        pathLine.runtimeOnly = true;
        root.children.push_back(std::move(pathLine));

        WidgetElement statsLine{};
        statsLine.type = WidgetElementType::Text;
        statsLine.id = "MeshViewer.Details.Stats";
        statsLine.text = "Vertices: " + std::to_string(ptr->getVertexCount())
            + "  Indices: " + std::to_string(ptr->getIndexCount());
        statsLine.font = "default.ttf";
        statsLine.fontSize = 12.0f;
        statsLine.textColor = Vec4{ 0.7f, 0.7f, 0.75f, 1.0f };
        statsLine.fillX = true;
        statsLine.minSize = Vec2{ 0.0f, 20.0f };
        statsLine.runtimeOnly = true;
        root.children.push_back(std::move(statsLine));

        // --- Separator ---
        {
            WidgetElement sep{};
            sep.type = WidgetElementType::Panel;
            sep.fillX = true;
            sep.minSize = Vec2{ 0.0f, 1.0f };
            sep.color = Vec4{ 0.3f, 0.3f, 0.35f, 0.6f };
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // --- Section header: Transform ---
        {
            WidgetElement header{};
            header.type = WidgetElementType::Text;
            header.text = "Transform";
            header.font = "default.ttf";
            header.fontSize = 13.0f;
            header.textColor = Vec4{ 0.85f, 0.85f, 0.9f, 1.0f };
            header.fillX = true;
            header.minSize = Vec2{ 0.0f, 24.0f };
            header.runtimeOnly = true;
            root.children.push_back(std::move(header));
        }

        // Helper to create a label + entry bar row
        const auto makeFloatRow = [&](const std::string& label, const std::string& fieldId, float value,
            std::function<void(const std::string&)> onChange)
        {
            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.fillX = true;
            row.minSize = Vec2{ 0.0f, 26.0f };
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.runtimeOnly = true;
            row.orientation = StackOrientation::Horizontal;

            WidgetElement lbl{};
            lbl.type = WidgetElementType::Text;
            lbl.text = label;
            lbl.font = "default.ttf";
            lbl.fontSize = 12.0f;
            lbl.textColor = Vec4{ 0.7f, 0.7f, 0.75f, 1.0f };
            lbl.minSize = Vec2{ 80.0f, 26.0f };
            lbl.textAlignV = TextAlignV::Center;
            lbl.runtimeOnly = true;
            row.children.push_back(std::move(lbl));

            WidgetElement entry{};
            entry.type = WidgetElementType::EntryBar;
            entry.id = fieldId;
            entry.value = std::to_string(value);
            // Trim trailing zeros for cleaner display
            if (entry.value.find('.') != std::string::npos)
            {
                entry.value.erase(entry.value.find_last_not_of('0') + 1, std::string::npos);
                if (entry.value.back() == '.') entry.value.push_back('0');
            }
            entry.font = "default.ttf";
            entry.fontSize = 12.0f;
            entry.fillX = true;
            entry.minSize = Vec2{ 60.0f, 24.0f };
            entry.color = Vec4{ 0.1f, 0.1f, 0.13f, 1.0f };
            entry.textColor = Vec4{ 0.9f, 0.9f, 0.92f, 1.0f };
            entry.hoverColor = Vec4{ 0.15f, 0.15f, 0.18f, 1.0f };
            entry.padding = Vec2{ 6.0f, 4.0f };
            entry.isHitTestable = true;
            entry.runtimeOnly = true;
            entry.onValueChanged = std::move(onChange);
            row.children.push_back(std::move(entry));

            root.children.push_back(std::move(row));
        };

        // Read current scale from the mesh asset data (defaults to 1.0)
        float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
        std::string currentMaterialPath;
        {
            auto meshAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
            if (meshAsset)
            {
                const auto& data = meshAsset->getData();
                if (data.contains("m_scale") && data["m_scale"].is_array() && data["m_scale"].size() >= 3)
                {
                    scaleX = data["m_scale"][0].get<float>();
                    scaleY = data["m_scale"][1].get<float>();
                    scaleZ = data["m_scale"][2].get<float>();
                }
                if (data.contains("m_materialAssetPaths") && data["m_materialAssetPaths"].is_array()
                    && !data["m_materialAssetPaths"].empty())
                {
                    currentMaterialPath = data["m_materialAssetPaths"][0].get<std::string>();
                }
            }
        }

        // Scale X/Y/Z
        const std::string capturedResolved = resolvedPath;
        makeFloatRow("Scale X", "MeshViewer.Details.ScaleX", scaleX,
            [capturedResolved](const std::string& val) {
                auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
                if (!asset) return;
                try {
                    float v = std::stof(val);
                    auto& data = asset->getData();
                    if (!data.contains("m_scale")) data["m_scale"] = json::array({1.0f, 1.0f, 1.0f});
                    data["m_scale"][0] = v;
                    asset->setIsSaved(false);
                } catch (...) {}
            });
        makeFloatRow("Scale Y", "MeshViewer.Details.ScaleY", scaleY,
            [capturedResolved](const std::string& val) {
                auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
                if (!asset) return;
                try {
                    float v = std::stof(val);
                    auto& data = asset->getData();
                    if (!data.contains("m_scale")) data["m_scale"] = json::array({1.0f, 1.0f, 1.0f});
                    data["m_scale"][1] = v;
                    asset->setIsSaved(false);
                } catch (...) {}
            });
        makeFloatRow("Scale Z", "MeshViewer.Details.ScaleZ", scaleZ,
            [capturedResolved](const std::string& val) {
                auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
                if (!asset) return;
                try {
                    float v = std::stof(val);
                    auto& data = asset->getData();
                    if (!data.contains("m_scale")) data["m_scale"] = json::array({1.0f, 1.0f, 1.0f});
                    data["m_scale"][2] = v;
                    asset->setIsSaved(false);
                } catch (...) {}
            });

        // --- Separator ---
        {
            WidgetElement sep{};
            sep.type = WidgetElementType::Panel;
            sep.fillX = true;
            sep.minSize = Vec2{ 0.0f, 1.0f };
            sep.color = Vec4{ 0.3f, 0.3f, 0.35f, 0.6f };
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // --- Section header: Material ---
        {
            WidgetElement header{};
            header.type = WidgetElementType::Text;
            header.text = "Material";
            header.font = "default.ttf";
            header.fontSize = 13.0f;
            header.textColor = Vec4{ 0.85f, 0.85f, 0.9f, 1.0f };
            header.fillX = true;
            header.minSize = Vec2{ 0.0f, 24.0f };
            header.runtimeOnly = true;
            root.children.push_back(std::move(header));
        }

        // Material path entry
        {
            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.fillX = true;
            row.minSize = Vec2{ 0.0f, 26.0f };
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.runtimeOnly = true;
            row.orientation = StackOrientation::Horizontal;

            WidgetElement lbl{};
            lbl.type = WidgetElementType::Text;
            lbl.text = "Path";
            lbl.font = "default.ttf";
            lbl.fontSize = 12.0f;
            lbl.textColor = Vec4{ 0.7f, 0.7f, 0.75f, 1.0f };
            lbl.minSize = Vec2{ 80.0f, 26.0f };
            lbl.textAlignV = TextAlignV::Center;
            lbl.runtimeOnly = true;
            row.children.push_back(std::move(lbl));

            WidgetElement entry{};
            entry.type = WidgetElementType::EntryBar;
            entry.id = "MeshViewer.Details.MaterialPath";
            entry.value = currentMaterialPath;
            entry.font = "default.ttf";
            entry.fontSize = 12.0f;
            entry.fillX = true;
            entry.minSize = Vec2{ 60.0f, 24.0f };
            entry.color = Vec4{ 0.1f, 0.1f, 0.13f, 1.0f };
            entry.textColor = Vec4{ 0.9f, 0.9f, 0.92f, 1.0f };
            entry.hoverColor = Vec4{ 0.15f, 0.15f, 0.18f, 1.0f };
            entry.padding = Vec2{ 6.0f, 4.0f };
            entry.isHitTestable = true;
            entry.runtimeOnly = true;
            entry.onValueChanged = [capturedResolved](const std::string& val) {
                auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
                if (!asset) return;
                auto& data = asset->getData();
                if (!data.contains("m_materialAssetPaths"))
                    data["m_materialAssetPaths"] = json::array();
                auto& paths = data["m_materialAssetPaths"];
                if (paths.empty()) paths.push_back(val);
                else paths[0] = val;
                asset->setIsSaved(false);
            };
            row.children.push_back(std::move(entry));
            root.children.push_back(std::move(row));
        }

        WidgetElement content{};
        content.type = WidgetElementType::StackPanel;
        content.id = "MeshViewer.Details.Content";
        content.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        content.fillX = true;
        content.sizeToContent = true;
        content.padding = Vec2{ 0.0f, 4.0f };
        content.runtimeOnly = true;
        root.children.push_back(std::move(content));

        elements.push_back(std::move(root));
        propsWidget->setElements(std::move(elements));

        // Tab-scoped: only visible/layouted when this tab is active
        m_uiManager.registerWidget("MeshViewerDetails." + assetPath, propsWidget, assetPath);
    }

    // Switch to the new tab (handles level swap, camera save/restore)
    setActiveTab(assetPath);

    m_uiManager.markAllWidgetsDirty();
    m_uiManager.showToastMessage(displayName + " ready", 2.5f);
    Logger::Instance().log(Logger::Category::Rendering,
        "Mesh viewer opened: " + assetPath, Logger::LogLevel::INFO);
    return ptr;
}

void OpenGLRenderer::closeMeshViewer(const std::string& assetPath)
{
    // If this viewer's tab is active, switch back to Viewport first (handles level swap)
    if (m_activeTabId == assetPath)
    {
        setActiveTab("Viewport");
    }

    // Destroy the viewer
    auto it = m_meshViewers.find(assetPath);
    if (it != m_meshViewers.end() && it->second)
    {
        it->second->destroyRuntimeLevel();
    }
    m_meshViewers.erase(assetPath);

    // Clean up per-tab selection state
    m_tabSelectedEntity.erase(assetPath);

    // Unregister the details panel widget
    m_uiManager.unregisterWidget("MeshViewerDetails." + assetPath);

    // Remove the tab button and close button from TitleBar
    {
        auto* tabsStack = m_uiManager.findElementById("TitleBar.Tabs");
        if (tabsStack)
        {
            const std::string tabBtnId = "TitleBar.Tab." + assetPath;
            const std::string closeBtnId = "TitleBar.TabClose." + assetPath;
            tabsStack->children.erase(
                std::remove_if(tabsStack->children.begin(), tabsStack->children.end(),
                    [&](const WidgetElement& el) { return el.id == tabBtnId || el.id == closeBtnId; }),
                tabsStack->children.end());
        }
    }

    // Remove the editor tab and its FBO
    removeTab(assetPath);

    m_uiManager.markAllWidgetsDirty();
}

MeshViewerWindow* OpenGLRenderer::getMeshViewer(const std::string& assetPath)
{
    auto it = m_meshViewers.find(assetPath);
    return (it != m_meshViewers.end()) ? it->second.get() : nullptr;
}

void OpenGLRenderer::renderUI()
{
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t uiStart = SDL_GetPerformanceCounter();
    const uint64_t drawStart = uiStart;
    glDisable(GL_DEPTH_TEST);

    if (!m_textRenderer)
    {
        m_textRenderer = m_resourceManager.prepareTextRenderer();
    }

    if (!m_textRenderer)
    {
        m_textQueue.clear();
        return;
    }

    int width = m_cachedWindowWidth;
    int height = m_cachedWindowHeight;
    if (width <= 0 || height <= 0)
    {
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
    }
    m_textRenderer->setScreenSize(width, height);
    m_uiManager.setAvailableViewportSize(Vec2{ static_cast<float>(width), static_cast<float>(height) });

    const bool layoutDirty = m_uiManager.needsLayoutUpdate();
    if (layoutDirty)
    {
        m_uiManager.updateLayouts([this](const std::string& text, float scale)
            {
                return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{};
            });
    }

    const bool debugToggled = (m_uiDebugEnabled != m_uiDebugEnabledPrev);
    m_uiDebugEnabledPrev = m_uiDebugEnabled;

    const bool sizeChanged = (width != m_uiFboWidth || height != m_uiFboHeight);
    const bool needsRedraw = layoutDirty || m_uiManager.isRenderDirty() || debugToggled || sizeChanged || !m_uiFboCacheValid;

    const auto& ordered = m_uiManager.getWidgetsOrderedByZ();
    if (!ordered.empty() && ensureUIQuadRenderer() && ensureUiFbo(width, height))
    {
        if (needsRedraw)
        {
            m_uiManager.clearRenderDirty();

            glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        ensureUIShaderDefaults();
        const glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);

        struct DeferredDropDown { float x0, y0, x1, y1, fontSize; int selectedIndex; std::vector<std::string> items; Vec4 textColor, hoverColor; Vec2 padding; };
        std::vector<DeferredDropDown> deferredDropDowns;

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
                const float contentX0 = x0 + element.padding.x;
                const float contentY0 = y0 + element.padding.y;
                const float contentX1 = x1 - element.padding.x;
                const float contentY1 = y1 - element.padding.y;
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
                const auto drawLine = [&](const std::string& line, const Vec2& pos)
                    {
                        if (!element.shaderVertex.empty() || !element.shaderFragment.empty())
                        {
                            m_textRenderer->drawTextWithShader(line, pos, scale, element.textColor, vertexPath, fragmentPath);
                        }
                        else
                        {
                            m_textRenderer->drawText(line, pos, scale, element.textColor);
                        }
                    };

                if (element.wrapText && contentW > 0.0f)
                {
                    std::vector<std::string> lines;
                    std::vector<std::string> paragraphs;
                    std::string paragraph;
                    for (char c : element.text)
                    {
                        if (c == '\n')
                        {
                            paragraphs.push_back(paragraph);
                            paragraph.clear();
                        }
                        else
                        {
                            paragraph.push_back(c);
                        }
                    }
                    paragraphs.push_back(paragraph);

                    const auto pushWrappedLine = [&](const std::string& line)
                        {
                            if (!line.empty())
                            {
                                lines.push_back(line);
                            }
                        };

                    const auto wrapWord = [&](const std::string& word, std::string& currentLine)
                        {
                            const Vec2 wordSize = m_textRenderer->measureText(word, scale);
                            if (currentLine.empty() && wordSize.x > contentW)
                            {
                                std::string chunk;
                                for (char wc : word)
                                {
                                    std::string candidate = chunk;
                                    candidate.push_back(wc);
                                    const Vec2 chunkSize = m_textRenderer->measureText(candidate, scale);
                                    if (chunkSize.x > contentW && !chunk.empty())
                                    {
                                        lines.push_back(chunk);
                                        chunk.clear();
                                        chunk.push_back(wc);
                                    }
                                    else
                                    {
                                        chunk = candidate;
                                    }
                                }
                                currentLine = chunk;
                                return;
                            }

                            std::string candidate = currentLine.empty() ? word : (currentLine + " " + word);
                            const Vec2 candidateSize = m_textRenderer->measureText(candidate, scale);
                            if (candidateSize.x <= contentW || currentLine.empty())
                            {
                                currentLine = candidate;
                            }
                            else
                            {
                                lines.push_back(currentLine);
                                currentLine = word;
                            }
                        };

                    for (const auto& para : paragraphs)
                    {
                        std::string currentLine;
                        std::string currentWord;
                        for (char c : para)
                        {
                            if (std::isspace(static_cast<unsigned char>(c)))
                            {
                                if (!currentWord.empty())
                                {
                                    wrapWord(currentWord, currentLine);
                                    currentWord.clear();
                                }
                            }
                            else
                            {
                                currentWord.push_back(c);
                            }
                        }
                        if (!currentWord.empty())
                        {
                            wrapWord(currentWord, currentLine);
                        }
                        pushWrappedLine(currentLine);
                    }

                    const float lineHeight = m_textRenderer->getLineHeight(scale);
                    const float totalHeight = lineHeight * static_cast<float>(lines.size());
                    float startY = contentY0;
                    switch (element.textAlignV)
                    {
                    case TextAlignV::Center:
                        startY = contentY0 + (contentH - totalHeight) * 0.5f;
                        break;
                    case TextAlignV::Bottom:
                        startY = contentY1 - totalHeight;
                        break;
                    default:
                        startY = contentY0;
                        break;
                    }

                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        const std::string& line = lines[i];
                        const Vec2 lineSize = m_textRenderer->measureText(line, scale);
                        float textX = contentX0;
                        switch (element.textAlignH)
                        {
                        case TextAlignH::Center:
                            textX = contentX0 + (contentW - lineSize.x) * 0.5f;
                            break;
                        case TextAlignH::Right:
                            textX = contentX1 - lineSize.x;
                            break;
                        default:
                            textX = contentX0;
                            break;
                        }
                        const float textY = startY + lineHeight * static_cast<float>(i);
                        drawLine(line, Vec2{ textX, textY });
                    }
                }
                else
                {
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

                    drawLine(element.text, Vec2{ textX, textY });
                }
                if (m_uiDebugEnabled)
                {
                    const GLuint program = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.1f, 0.8f, 1.0f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::ProgressBar)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.color, false);

                const float range = element.maxValue - element.minValue;
                const float ratio = (range > 0.0f)
                    ? std::clamp((element.valueFloat - element.minValue) / range, 0.0f, 1.0f)
                    : 0.0f;
                if (ratio > 0.0f)
                {
                    const float fillX1 = x0 + widthPx * ratio;
                    drawUIPanel(x0, y0, fillX1, y1, element.fillColor, uiProjection, program, element.fillColor, false);
                }

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.2f, 0.9f, 0.6f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::Slider)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);

                const float range = element.maxValue - element.minValue;
                const float ratio = (range > 0.0f)
                    ? std::clamp((element.valueFloat - element.minValue) / range, 0.0f, 1.0f)
                    : 0.0f;

                const float trackHeight = std::min(heightPx, 6.0f);
                const float trackY0 = y0 + (heightPx - trackHeight) * 0.5f;
                const float trackY1 = trackY0 + trackHeight;
                drawUIPanel(x0, trackY0, x1, trackY1, element.color, uiProjection, program, element.color, false);
                if (ratio > 0.0f)
                {
                    const float fillX1 = x0 + widthPx * ratio;
                    drawUIPanel(x0, trackY0, fillX1, trackY1, element.fillColor, uiProjection, program, element.fillColor, false);
                }

                const float handleSize = std::max(10.0f, heightPx);
                float handleX0 = x0 + widthPx * ratio - handleSize * 0.5f;
                handleX0 = std::clamp(handleX0, x0, x1 - handleSize);
                drawUIPanel(handleX0, y0, handleX0 + handleSize, y1, element.textColor, uiProjection, program, element.textColor, false);

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.2f, 0.7f, 1.0f, 1.0f }, uiProjection, program);
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
                const float bx0 = x0;
                const float by0 = y0;
                const float bx1 = x1;
                const float by1 = y1;

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
                else if (element.textureId != 0 || !element.imagePath.empty())
                {
                    const GLuint tex = (element.textureId != 0) ? static_cast<GLuint>(element.textureId) : getOrLoadUITexture(element.imagePath);
                    if (tex != 0)
                    {
                        const float pad = 4.0f;
                        drawUIImage(bx0 + pad, by0 + pad, bx1 - pad, by1 - pad, tex, uiProjection);
                    }
                }
                for (const auto& child : element.children)
                {
                    self(self, child, bx0, by0, bx1 - bx0, by1 - by0);
                }
                if (m_uiDebugEnabled)
                {
                    drawUIOutline(bx0, by0, bx1, by1, Vec4{ 0.7f, 1.0f, 0.3f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::Image)
            {
                if (element.textureId != 0 || !element.imagePath.empty())
                {
                    const GLuint tex = (element.textureId != 0) ? static_cast<GLuint>(element.textureId) : getOrLoadUITexture(element.imagePath);
                    if (tex != 0)
                    {
                        drawUIImage(x0, y0, x1, y1, tex, uiProjection, element.color, true);
                    }
                }
                if (m_uiDebugEnabled)
                {
                    const GLuint program = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.2f, 1.0f, 1.0f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::CheckBox)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);

                const float boxSize = std::min(heightPx - 2.0f, 16.0f);
                const float boxX0 = x0 + element.padding.x;
                const float boxY0 = y0 + (heightPx - boxSize) * 0.5f;
                const float boxX1 = boxX0 + boxSize;
                const float boxY1 = boxY0 + boxSize;

                drawUIPanel(boxX0, boxY0, boxX1, boxY1, element.color, uiProjection, program, element.hoverColor, element.isHovered);
                drawUIOutline(boxX0, boxY0, boxX1, boxY1, Vec4{ 0.4f, 0.4f, 0.45f, 0.9f }, uiProjection, program);

                if (element.isChecked)
                {
                    const float inset = 3.0f;
                    drawUIPanel(boxX0 + inset, boxY0 + inset, boxX1 - inset, boxY1 - inset,
                        element.fillColor, uiProjection, program, element.fillColor, false);
                }

                if (!element.text.empty())
                {
                    const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
                    const float scale = fontSize / 48.0f;
                    const float textX = boxX1 + 6.0f;
                    const Vec2 textSize = m_textRenderer->measureText(element.text, scale);
                    const float textY = y0 + (heightPx - textSize.y) * 0.5f;
                    m_textRenderer->drawText(element.text, Vec2{ textX, textY }, scale, element.textColor);
                }

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.9f, 0.5f, 0.2f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::DropDown)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);

                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.hoverColor, element.isHovered);

                const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
                const float scale = fontSize / 48.0f;
                const float contentX0 = x0 + element.padding.x;
                const float contentY0 = y0 + element.padding.y;
                const float contentX1 = x1 - element.padding.x;
                const float contentY1 = y1 - element.padding.y;
                const float contentH = std::max(0.0f, contentY1 - contentY0);

                std::string display = element.text;
                if (display.empty() && element.selectedIndex >= 0 &&
                    element.selectedIndex < static_cast<int>(element.items.size()))
                {
                    display = element.items[static_cast<size_t>(element.selectedIndex)];
                }
                if (!display.empty())
                {
                    const Vec2 textSize = m_textRenderer->measureText(display, scale);
                    const float textY = contentY0 + std::max(0.0f, (contentH - textSize.y) * 0.5f);
                    m_textRenderer->drawText(display, Vec2{ contentX0, textY }, scale, element.textColor);
                }

                const float arrowSize = std::min(heightPx * 0.4f, 8.0f);
                const float arrowX = contentX1 - arrowSize;
                const float arrowY = y0 + (heightPx - arrowSize) * 0.5f;
                drawUIPanel(arrowX, arrowY, arrowX + arrowSize, arrowY + arrowSize,
                    element.textColor, uiProjection, program, element.textColor, false);

                if (element.isExpanded && !element.items.empty())
                {
                    deferredDropDowns.push_back({ x0, y0, x1, y1, fontSize, element.selectedIndex, element.items, element.textColor, element.hoverColor, element.padding });
                }

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.9f, 0.6f, 0.1f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::DropdownButton)
            {
                const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
                const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
                const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.hoverColor, element.isHovered);

                if (!element.text.empty())
                {
                    const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
                    const float scale = fontSize / 48.0f;
                    const float contentX0 = x0 + element.padding.x;
                    const float contentY0 = y0 + element.padding.y;
                    const float contentX1 = x1 - element.padding.x;
                    const float contentY1 = y1 - element.padding.y;
                    const float contentW = std::max(0.0f, contentX1 - contentX0);
                    const float contentH = std::max(0.0f, contentY1 - contentY0);

                    const Vec2 textSize = m_textRenderer->measureText(element.text, scale);
                    float textX = contentX0;
                    float textY = contentY0 + std::max(0.0f, (contentH - textSize.y) * 0.5f);

                    switch (element.textAlignH)
                    {
                    case TextAlignH::Center:
                        textX = contentX0 + (contentW - textSize.x) * 0.5f;
                        break;
                    case TextAlignH::Right:
                        textX = contentX1 - textSize.x;
                        break;
                    default:
                        break;
                    }

                    m_textRenderer->drawText(element.text, Vec2{ textX, textY }, scale, element.textColor);
                }

                const float arrowSize = std::min(heightPx * 0.3f, 6.0f);
                const float arrowX = x1 - element.padding.x - arrowSize;
                const float arrowY = y0 + (heightPx - arrowSize) * 0.5f;
                drawUIPanel(arrowX, arrowY, arrowX + arrowSize, arrowY + arrowSize,
                    element.textColor, uiProjection, program, element.textColor, false);

                if (m_uiDebugEnabled)
                {
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.9f, 0.7f, 0.1f, 1.0f }, uiProjection, program);
                }
                return;
            }
            if (element.type == WidgetElementType::TreeView || element.type == WidgetElementType::TabView)
            {
                if (element.color.w > 0.0f)
                {
                    const std::string& vertexPath = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
                    const std::string& fragmentPath = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
                    const GLuint program = getUIQuadProgram(vertexPath, fragmentPath);
                    drawUIPanel(x0, y0, x1, y1, element.color, uiProjection, program, element.color, false);
                }

                // Clip children to the TreeView/TabView bounds
                const GLint clipX = static_cast<GLint>(x0);
                const GLint clipY = static_cast<GLint>(static_cast<float>(height) - y1); // GL scissor is bottom-left
                const GLsizei clipW = static_cast<GLsizei>(std::max(0.0f, widthPx));
                const GLsizei clipH = static_cast<GLsizei>(std::max(0.0f, heightPx));

                glEnable(GL_SCISSOR_TEST);
                glScissor(clipX, clipY, clipW, clipH);

                for (const auto& child : element.children)
                {
                    self(self, child, x0, y0, widthPx, heightPx);
                }

                glDisable(GL_SCISSOR_TEST);

                if (m_uiDebugEnabled)
                {
                    const GLuint program = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
                    drawUIOutline(x0, y0, x1, y1, Vec4{ 0.4f, 0.9f, 0.6f, 1.0f }, uiProjection, program);
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

                const bool needsClip = element.scrollable;
                if (needsClip)
                {
                    const GLint clipX = static_cast<GLint>(x0);
                    const GLint clipY = static_cast<GLint>(static_cast<float>(height) - y1);
                    const GLsizei clipW = static_cast<GLsizei>(std::max(0.0f, widthPx));
                    const GLsizei clipH = static_cast<GLsizei>(std::max(0.0f, heightPx));
                    glEnable(GL_SCISSOR_TEST);
                    glScissor(clipX, clipY, clipW, clipH);
                }

                for (const auto& child : element.children)
                {
                    self(self, child, x0, y0, widthPx, heightPx);
                }

                if (needsClip)
                {
                    glDisable(GL_SCISSOR_TEST);
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

        // Deferred pass: draw expanded DropDown items on top of everything
        for (const auto& dd : deferredDropDowns)
        {
            const std::string& vp = resolveUIShaderPath("", m_defaultPanelVertex);
            const std::string& fp = resolveUIShaderPath("", m_defaultPanelFragment);
            const GLuint prog = getUIQuadProgram(vp, fp);
            const float scale = dd.fontSize / 48.0f;
            const float heightPx = dd.y1 - dd.y0;
            const float itemHeight = std::max(20.0f, heightPx);
            const float contentX0 = dd.x0 + dd.padding.x;
            for (size_t i = 0; i < dd.items.size(); ++i)
            {
                const float iy0 = dd.y1 + static_cast<float>(i) * itemHeight;
                const float iy1 = iy0 + itemHeight;
                const bool isSelected = (static_cast<int>(i) == dd.selectedIndex);
                const Vec4 itemColor = isSelected
                    ? Vec4{ 0.22f, 0.22f, 0.28f, 0.98f }
                    : Vec4{ 0.14f, 0.14f, 0.18f, 0.98f };
                drawUIPanel(dd.x0, iy0, dd.x1, iy1, itemColor, uiProjection, prog, dd.hoverColor, false);
                const Vec2 itemTextSize = m_textRenderer->measureText(dd.items[i], scale);
                const float itemTextY = iy0 + (itemHeight - itemTextSize.y) * 0.5f;
                m_textRenderer->drawText(dd.items[i], Vec2{ contentX0, itemTextY }, scale, dd.textColor);
            }
        }

            glDisable(GL_BLEND);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            m_uiFboCacheValid = true;

            const uint64_t drawEnd = SDL_GetPerformanceCounter();
            m_cpuUiDrawMs = (freq > 0) ? (static_cast<double>(drawEnd - drawStart) * 1000.0 / static_cast<double>(freq)) : 0.0;
        }
        else
        {
            m_cpuUiDrawMs = 0.0;
        }

        blitUiCache(width, height);
    }
    else
    {
        m_cpuUiDrawMs = 0.0;
    }

    if (!m_textQueue.empty())
    {
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

        m_textRenderer->setScreenSize(static_cast<int>(viewportSize.x), static_cast<int>(viewportSize.y));

        for (const auto& command : m_textQueue)
        {
            Vec2 pixelPos{
                contentLeft + command.screenPos.x * contentWidth,
                contentTop + command.screenPos.y * contentHeight
            };
            m_textRenderer->drawText(command.text, pixelPos, command.scale, command.color);
        }
    }
    m_textQueue.clear();

    // Render drag indicator when the user is dragging a content browser asset
    if (m_textRenderer && m_uiManager.isDragging())
    {
        const Vec2 mousePos = m_uiManager.getMousePosition();
        const std::string& payload = m_uiManager.getDragPayload();
        // Extract the label from payload ("typeInt|relPath" → stem of relPath)
        std::string dragLabel = "Dragging...";
        const auto sep = payload.find('|');
        if (sep != std::string::npos)
        {
            const std::string relPath = payload.substr(sep + 1);
            const auto lastSlash = relPath.find_last_of('/');
            const auto dot = relPath.find_last_of('.');
            if (dot != std::string::npos)
            {
                const size_t nameStart = (lastSlash != std::string::npos) ? lastSlash + 1 : 0;
                dragLabel = relPath.substr(nameStart, dot - nameStart);
            }
        }
        const Vec2 labelPos{ mousePos.x + 16.0f, mousePos.y - 8.0f };
        m_textRenderer->drawText(dragLabel, labelPos, 0.42f, Vec4{ 1.0f, 1.0f, 1.0f, 0.9f });
    }

    glEnable(GL_DEPTH_TEST);

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

bool OpenGLRenderer::ensureHzbResources(int width, int height)
{
    if (m_hzbResourcesReady && m_hzbWidth == width && m_hzbHeight == height)
    {
        return true;
    }

    releaseHzbResources();

    m_hzbWidth = width;
    m_hzbHeight = height;
    m_hzbMipLevels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height)))));

    // Depth texture to receive a copy of the default framebuffer depth
    glGenTextures(1, &m_hzbDepthTexture);
    glBindTexture(GL_TEXTURE_2D, m_hzbDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glGenFramebuffers(1, &m_hzbDepthCopyFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hzbDepthCopyFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_hzbDepthTexture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // R32F texture for HZB mip chain
    glGenTextures(1, &m_hzbTexture);
    glBindTexture(GL_TEXTURE_2D, m_hzbTexture);
    glTexStorage2D(GL_TEXTURE_2D, m_hzbMipLevels, GL_R32F, width, height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // FBO for writing to HZB mip levels
    glGenFramebuffers(1, &m_hzbFbo);

    // Empty VAO for fullscreen triangle (positions generated from gl_VertexID)
    glGenVertexArrays(1, &m_hzbFullscreenVao);

    // Shared vertex shader for fullscreen triangle
    const std::string fullscreenVert =
        "#version 460 core\n"
        "out vec2 vTexCoord;\n"
        "void main() {\n"
        "    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
        "    vTexCoord = pos;\n"
        "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";

    // Copy shader: reads depth texture, writes to R32F mip 0
    const std::string copyFrag =
        "#version 460 core\n"
        "uniform sampler2D uDepthTexture;\n"
        "in vec2 vTexCoord;\n"
        "out float FragColor;\n"
        "void main() {\n"
        "    FragColor = texture(uDepthTexture, vTexCoord).r;\n"
        "}\n";

    // Downsample shader: reads from previous HZB mip, writes max to current mip
    const std::string downsampleFrag =
        "#version 460 core\n"
        "uniform sampler2D uHzbTexture;\n"
        "uniform int uPrevMipLevel;\n"
        "in vec2 vTexCoord;\n"
        "out float FragColor;\n"
        "void main() {\n"
        "    ivec2 coord = ivec2(gl_FragCoord.xy) * 2;\n"
        "    ivec2 prevSize = textureSize(uHzbTexture, uPrevMipLevel);\n"
        "    ivec2 c00 = min(coord, prevSize - 1);\n"
        "    ivec2 c10 = min(coord + ivec2(1, 0), prevSize - 1);\n"
        "    ivec2 c01 = min(coord + ivec2(0, 1), prevSize - 1);\n"
        "    ivec2 c11 = min(coord + ivec2(1, 1), prevSize - 1);\n"
        "    float d0 = texelFetch(uHzbTexture, c00, uPrevMipLevel).r;\n"
        "    float d1 = texelFetch(uHzbTexture, c10, uPrevMipLevel).r;\n"
        "    float d2 = texelFetch(uHzbTexture, c01, uPrevMipLevel).r;\n"
        "    float d3 = texelFetch(uHzbTexture, c11, uPrevMipLevel).r;\n"
        "    FragColor = max(max(d0, d1), max(d2, d3));\n"
        "}\n";

    auto compileShader = [](const std::string& source, Shader::Type type) -> std::shared_ptr<OpenGLShader>
    {
        auto s = std::make_shared<OpenGLShader>();
        if (!s->loadFromSource(type, source))
        {
            return nullptr;
        }
        return s;
    };

    auto linkProgram = [](GLuint vs, GLuint fs) -> GLuint
    {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);
        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    };

    auto vertShader = compileShader(fullscreenVert, Shader::Type::Vertex);
    auto copyFragShader = compileShader(copyFrag, Shader::Type::Fragment);
    auto downsampleFragShader = compileShader(downsampleFrag, Shader::Type::Fragment);

    if (!vertShader || !copyFragShader || !downsampleFragShader)
    {
        releaseHzbResources();
        return false;
    }

    m_hzbCopyProgram = linkProgram(vertShader->id(), copyFragShader->id());
    m_hzbDownsampleProgram = linkProgram(vertShader->id(), downsampleFragShader->id());
    if (!m_hzbCopyProgram || !m_hzbDownsampleProgram)
    {
        releaseHzbResources();
        return false;
    }

    m_hzbPrevMipLoc = glGetUniformLocation(m_hzbDownsampleProgram, "uPrevMipLevel");
    m_hzbCopyDepthTexLoc = glGetUniformLocation(m_hzbCopyProgram, "uDepthTexture");
    m_hzbDownsampleTexLoc = glGetUniformLocation(m_hzbDownsampleProgram, "uHzbTexture");

    // Allocate CPU readback storage (only for mip levels >= kMinReadbackMip)
    constexpr int kMinReadbackMip = 3;
    const int readbackLevels = std::max(0, m_hzbMipLevels - kMinReadbackMip);
    m_hzbCpuData.resize(readbackLevels);

    // Create PBOs for async readback (double-buffered)
    size_t totalPixels = 0;
    for (int i = 0; i < readbackLevels; ++i)
    {
        const int mip = kMinReadbackMip + i;
        const int mipW = std::max(1, width >> mip);
        const int mipH = std::max(1, height >> mip);
        totalPixels += static_cast<size_t>(mipW) * mipH;
    }
    m_hzbPboSize = totalPixels * sizeof(float);
    if (m_hzbPboSize > 0)
    {
        glGenBuffers(kHzbPboCount, m_hzbPbos.data());
        for (int i = 0; i < kHzbPboCount; ++i)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hzbPbos[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(m_hzbPboSize), nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    m_hzbPboIndex = 0;
    m_hzbPboReady = false;

    m_hzbResourcesReady = true;
    return true;
}

void OpenGLRenderer::releaseHzbResources()
{
    if (m_hzbCopyProgram)
    {
        glDeleteProgram(m_hzbCopyProgram);
        m_hzbCopyProgram = 0;
    }
    if (m_hzbDownsampleProgram)
    {
        glDeleteProgram(m_hzbDownsampleProgram);
        m_hzbDownsampleProgram = 0;
    }
    m_hzbPrevMipLoc = -1;
    m_hzbCopyDepthTexLoc = -1;
    m_hzbDownsampleTexLoc = -1;
    if (m_hzbFbo)
    {
        glDeleteFramebuffers(1, &m_hzbFbo);
        m_hzbFbo = 0;
    }
    if (m_hzbDepthCopyFbo)
    {
        glDeleteFramebuffers(1, &m_hzbDepthCopyFbo);
        m_hzbDepthCopyFbo = 0;
    }
    if (m_hzbTexture)
    {
        glDeleteTextures(1, &m_hzbTexture);
        m_hzbTexture = 0;
    }
    if (m_hzbDepthTexture)
    {
        glDeleteTextures(1, &m_hzbDepthTexture);
        m_hzbDepthTexture = 0;
    }
    if (m_hzbFullscreenVao)
    {
        glDeleteVertexArrays(1, &m_hzbFullscreenVao);
        m_hzbFullscreenVao = 0;
    }
    m_hzbCpuData.clear();
    for (auto& pbo : m_hzbPbos)
    {
        if (pbo)
        {
            glDeleteBuffers(1, &pbo);
            pbo = 0;
        }
    }
    m_hzbPboSize = 0;
    m_hzbPboIndex = 0;
    m_hzbPboReady = false;
    m_hzbWidth = 0;
    m_hzbHeight = 0;
    m_hzbMipLevels = 0;
    m_hzbResourcesReady = false;
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

// ---- Skybox ----

static const float kSkyboxVertices[] = {
    -1, 1,-1, -1,-1,-1, 1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
    -1,-1, 1, -1,-1,-1, -1, 1,-1, -1, 1,-1, -1, 1, 1, -1,-1, 1,
     1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
    -1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
    -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1,
    -1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1
};

bool OpenGLRenderer::ensureSkyboxResources()
{
    if (m_skyboxProgram != 0)
        return true;

    const char* vs = R"(
#version 460 core
layout(location=0) in vec3 aPos;
out vec3 vTexCoord;
uniform mat4 uProjection;
uniform mat4 uView;
void main(){
    vTexCoord = aPos;
    vec4 pos = uProjection * uView * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
})";
    const char* fs = R"(
#version 460 core
in vec3 vTexCoord;
out vec4 FragColor;
uniform samplerCube uSkybox;
void main(){
    FragColor = texture(uSkybox, vTexCoord);
})";

    GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsh, 1, &vs, nullptr);
    glCompileShader(vsh);
    GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsh, 1, &fs, nullptr);
    glCompileShader(fsh);
    m_skyboxProgram = glCreateProgram();
    glAttachShader(m_skyboxProgram, vsh);
    glAttachShader(m_skyboxProgram, fsh);
    glLinkProgram(m_skyboxProgram);
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    m_skyboxLocProjection = glGetUniformLocation(m_skyboxProgram, "uProjection");
    m_skyboxLocView = glGetUniformLocation(m_skyboxProgram, "uView");
    m_skyboxLocSampler = glGetUniformLocation(m_skyboxProgram, "uSkybox");

    glGenVertexArrays(1, &m_skyboxVao);
    glGenBuffers(1, &m_skyboxVbo);
    glBindVertexArray(m_skyboxVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_skyboxVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kSkyboxVertices), kSkyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    return true;
}

void OpenGLRenderer::releaseSkyboxResources()
{
    if (m_skyboxProgram) { glDeleteProgram(m_skyboxProgram); m_skyboxProgram = 0; }
    if (m_skyboxVbo)     { glDeleteBuffers(1, &m_skyboxVbo); m_skyboxVbo = 0; }
    if (m_skyboxVao)     { glDeleteVertexArrays(1, &m_skyboxVao); m_skyboxVao = 0; }
    if (m_skyboxCubemap) { glDeleteTextures(1, &m_skyboxCubemap); m_skyboxCubemap = 0; }
    m_skyboxLoadedPath.clear();
}

bool OpenGLRenderer::loadSkyboxCubemap(const std::string& folderPath)
{
    if (folderPath.empty())
    {
        if (m_skyboxCubemap) { glDeleteTextures(1, &m_skyboxCubemap); m_skyboxCubemap = 0; }
        m_skyboxLoadedPath.clear();
        return false;
    }
    if (folderPath == m_skyboxLoadedPath && m_skyboxCubemap != 0)
        return true;

    // Each face slot has a list of alternative file names (e.g. top/up, bottom/down)
    // Standard cubemap order: +X, -X, +Y, -Y, +Z, -Z
    struct FaceSlot { std::vector<std::string> names; GLenum target; };
    const FaceSlot faceSlots[6] = {
        { { "right" },          GL_TEXTURE_CUBE_MAP_POSITIVE_X },
        { { "left" },           GL_TEXTURE_CUBE_MAP_NEGATIVE_X },
        { { "top", "up" },     GL_TEXTURE_CUBE_MAP_POSITIVE_Y },
        { { "bottom", "down" },GL_TEXTURE_CUBE_MAP_NEGATIVE_Y },
        { { "front" },          GL_TEXTURE_CUBE_MAP_NEGATIVE_Z },
        { { "back" },           GL_TEXTURE_CUBE_MAP_POSITIVE_Z }
    };
    const std::string extensions[3] = { ".jpg", ".png", ".bmp" };

    if (m_skyboxCubemap) { glDeleteTextures(1, &m_skyboxCubemap); m_skyboxCubemap = 0; }

    glGenTextures(1, &m_skyboxCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_skyboxCubemap);

    auto& assetMgr = AssetManager::Instance();
    bool allLoaded = true;

    for (int i = 0; i < 6; ++i)
    {
        bool loaded = false;
        for (const auto& name : faceSlots[i].names)
        {
            for (const auto& ext : extensions)
            {
                const std::string filePath = (std::filesystem::path(folderPath) / (name + ext)).string();
                if (!std::filesystem::exists(filePath))
                    continue;
                int w = 0, h = 0, ch = 0;
                unsigned char* data = assetMgr.loadRawImageData(filePath, w, h, ch);
                if (data && w > 0 && h > 0)
                {
                    GLenum format = (ch == 4) ? GL_RGBA : GL_RGB;
                    glTexImage2D(faceSlots[i].target, 0, GL_RGBA8, w, h, 0, format, GL_UNSIGNED_BYTE, data);
                    assetMgr.freeRawImageData(data);
                    loaded = true;
                    break;
                }
                if (data) assetMgr.freeRawImageData(data);
            }
            if (loaded) break;
        }
        if (!loaded)
        {
            Logger::Instance().log(Logger::Category::Rendering,
                "Skybox: missing face '" + faceSlots[i].names.front() + "' in " + folderPath, Logger::LogLevel::WARNING);
            allLoaded = false;
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    if (!allLoaded)
    {
        glDeleteTextures(1, &m_skyboxCubemap);
        m_skyboxCubemap = 0;
        return false;
    }

    return true;
}

void OpenGLRenderer::setSkyboxPath(const std::string& pathOrFolder)
{
    if (pathOrFolder.empty())
    {
        loadSkyboxCubemap("");
        m_skyboxLoadedPath.clear();
        return;
    }

    if (pathOrFolder == m_skyboxLoadedPath && m_skyboxCubemap != 0)
        return;

    // If the path ends with .asset, resolve to the folder path from the asset data
    if (pathOrFolder.size() > 6 && pathOrFolder.substr(pathOrFolder.size() - 6) == ".asset")
    {
        auto& assetMgr = AssetManager::Instance();
        const std::string absPath = assetMgr.getAbsoluteContentPath(pathOrFolder);
        if (absPath.empty())
        {
            Logger::Instance().log(Logger::Category::Rendering,
                "Skybox: failed to resolve asset path '" + pathOrFolder + "'", Logger::LogLevel::WARNING);
            loadSkyboxCubemap("");
            m_skyboxLoadedPath.clear();
            return;
        }

        std::ifstream in(absPath, std::ios::binary);
        if (!in.is_open())
        {
            Logger::Instance().log(Logger::Category::Rendering,
                "Skybox: cannot open asset file '" + absPath + "'", Logger::LogLevel::WARNING);
            loadSkyboxCubemap("");
            m_skyboxLoadedPath.clear();
            return;
        }

        json fileJson;
        try { fileJson = json::parse(in); } catch (...) { loadSkyboxCubemap(""); m_skyboxLoadedPath.clear(); return; }
        in.close();

        if (!fileJson.is_object() || !fileJson.contains("data"))
        {
            loadSkyboxCubemap("");
            m_skyboxLoadedPath.clear();
            return;
        }
        const auto& data = fileJson.at("data");

        // Try folderPath first (absolute or project-relative)
        if (data.is_object() && data.contains("folderPath") && data.at("folderPath").is_string())
        {
            const std::string folder = data.at("folderPath").get<std::string>();
            auto& diagnostics = DiagnosticsManager::Instance();
            const std::string absFolder = (std::filesystem::path(diagnostics.getProjectInfo().projectPath) / folder).lexically_normal().string();
            if (loadSkyboxCubemap(absFolder))
            {
                m_skyboxLoadedPath = pathOrFolder;
            }
            else
            {
                Logger::Instance().log(Logger::Category::Rendering,
                    "Skybox: failed to load cubemap from '" + absFolder + "'", Logger::LogLevel::WARNING);
                m_skyboxLoadedPath.clear();
            }
            return;
        }

        // Fallback: try to resolve individual face paths
        if (data.is_object() && data.contains("faces") && data.at("faces").is_object())
        {
            const auto& faces = data.at("faces");
            for (const auto& [key, val] : faces.items())
            {
                if (val.is_string() && !val.get<std::string>().empty())
                {
                    std::filesystem::path facePath(val.get<std::string>());
                    std::string folder = facePath.parent_path().string();
                    std::string absFolder = assetMgr.getAbsoluteContentPath(folder);
                    if (!absFolder.empty())
                    {
                        absFolder = std::filesystem::path(absFolder).lexically_normal().string();
                        if (loadSkyboxCubemap(absFolder))
                        {
                            m_skyboxLoadedPath = pathOrFolder;
                        }
                        else
                        {
                            m_skyboxLoadedPath.clear();
                        }
                        return;
                    }
                }
            }
        }

        loadSkyboxCubemap("");
        m_skyboxLoadedPath.clear();
        return;
    }

    // Direct folder path — try as-is first, then resolve via Content path
    std::string resolvedFolder = std::filesystem::path(pathOrFolder).lexically_normal().string();
    if (loadSkyboxCubemap(resolvedFolder))
    {
        m_skyboxLoadedPath = pathOrFolder;
        return;
    }

    // Path may be content-relative — resolve through AssetManager
    const std::string contentResolved = AssetManager::Instance().getAbsoluteContentPath(pathOrFolder);
    if (!contentResolved.empty() && loadSkyboxCubemap(std::filesystem::path(contentResolved).lexically_normal().string()))
    {
        m_skyboxLoadedPath = pathOrFolder;
    }
    else
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "Skybox: failed to load from folder '" + pathOrFolder + "'", Logger::LogLevel::WARNING);
        m_skyboxLoadedPath.clear();
    }
}

void OpenGLRenderer::renderSkybox(const glm::mat4& view, const glm::mat4& projection)
{
    if (m_skyboxCubemap == 0 || !ensureSkyboxResources())
        return;

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glUseProgram(m_skyboxProgram);
    // Strip translation from view matrix
    glm::mat4 skyView = glm::mat4(glm::mat3(view));
    glUniformMatrix4fv(m_skyboxLocProjection, 1, GL_FALSE, &projection[0][0]);
    glUniformMatrix4fv(m_skyboxLocView, 1, GL_FALSE, &skyView[0][0]);
    glUniform1i(m_skyboxLocSampler, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_skyboxCubemap);

    glBindVertexArray(m_skyboxVao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

// ---- Shadow Mapping ----

bool OpenGLRenderer::ensureShadowResources()
{
    if (m_shadowProgram != 0)
        return true;

    // Compile shadow depth shader
    const char* vs = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;
void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0);
}
)";
    const char* fs = R"(
#version 460 core
void main() {}
)";

    GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsh, 1, &vs, nullptr);
    glCompileShader(vsh);
    GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsh, 1, &fs, nullptr);
    glCompileShader(fsh);
    m_shadowProgram = glCreateProgram();
    glAttachShader(m_shadowProgram, vsh);
    glAttachShader(m_shadowProgram, fsh);
    glLinkProgram(m_shadowProgram);
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    m_shadowLocModel = glGetUniformLocation(m_shadowProgram, "uModel");
    m_shadowLocLightSpace = glGetUniformLocation(m_shadowProgram, "uLightSpaceMatrix");

    // Create shadow FBO + depth texture array (one layer per shadow-casting light)
    glGenTextures(1, &m_shadowDepthArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowDepthArray);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                 kShadowMapSize, kShadowMapSize, kMaxShadowLights,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    glGenFramebuffers(1, &m_shadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowDepthArray, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

void OpenGLRenderer::releaseShadowResources()
{
    if (m_shadowProgram)    { glDeleteProgram(m_shadowProgram); m_shadowProgram = 0; }
    if (m_shadowDepthArray) { glDeleteTextures(1, &m_shadowDepthArray); m_shadowDepthArray = 0; }
    if (m_shadowFbo)        { glDeleteFramebuffers(1, &m_shadowFbo); m_shadowFbo = 0; }
}

void OpenGLRenderer::findShadowLightIndices()
{
    m_shadowCount = 0;
    for (int i = 0; i < static_cast<int>(m_sceneLights.size()) && m_shadowCount < kMaxShadowLights; ++i)
    {
        const int type = m_sceneLights[i].type;
        if (type == 1 || type == 2) // directional or spot
        {
            m_shadowLightIndices[m_shadowCount] = i;
            ++m_shadowCount;
        }
    }
}

glm::mat4 OpenGLRenderer::computeLightSpaceMatrix(const OpenGLMaterial::LightData& light) const
{
    const glm::vec3 lightDir = glm::normalize(light.direction);

    // Choose a stable up vector that isn't parallel to lightDir
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightDir, up)) > 0.99f)
    {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    if (light.type == 2) // LIGHT_SPOT
    {
        float fov = 2.0f * std::acos(light.spotOuterCutoff);
        if (fov <= 0.0f || fov > glm::radians(179.0f))
            fov = glm::radians(90.0f);

        const float farPlane = light.range > 0.0f ? light.range : 50.0f;
        const glm::mat4 lightView = glm::lookAt(light.position, light.position + lightDir, up);
        const glm::mat4 lightProj = glm::perspective(fov, 1.0f, 0.1f, farPlane);
        return lightProj * lightView;
    }

    // Directional light: orthographic projection centered on the camera
    glm::vec3 center(0.0f);
    if (m_camera)
    {
        const Vec3& camPos = m_camera->getPosition();
        center = glm::vec3(camPos.x, camPos.y, camPos.z);
    }

    const float shadowRange = 15.0f;
    const float shadowDepth = 60.0f;
    const glm::vec3 lightPos = center - lightDir * (shadowDepth * 0.5f);
    const glm::mat4 lightView = glm::lookAt(lightPos, center, up);
    const glm::mat4 lightProj = glm::ortho(-shadowRange, shadowRange, -shadowRange, shadowRange,
                                            0.1f, shadowDepth);
    return lightProj * lightView;
}

void OpenGLRenderer::renderShadowMap(const std::vector<DrawCmd>& drawList)
{
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glUseProgram(m_shadowProgram);

    for (int s = 0; s < m_shadowCount; ++s)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbo);
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowDepthArray, 0, s);
        glViewport(0, 0, kShadowMapSize, kShadowMapSize);
        glClear(GL_DEPTH_BUFFER_BIT);

        glUniformMatrix4fv(m_shadowLocLightSpace, 1, GL_FALSE, &m_shadowLightSpaceMatrices[s][0][0]);

        for (const auto& cmd : drawList)
        {
            glUniformMatrix4fv(m_shadowLocModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);

            auto* mat = cmd.obj->getMaterial();
            if (!mat)
                continue;
            glBindVertexArray(mat->getVao());
            if (mat->getIndexCount() > 0)
            {
                glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
            }
            else
            {
                glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
            }
        }
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

// ---- Point Light Shadow Mapping (Cube Maps) ----

bool OpenGLRenderer::ensurePointShadowResources()
{
    if (m_pointShadowProgram != 0)
        return true;

    // Vertex shader: transform to world space
    const char* vs = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
void main() {
    gl_Position = uModel * vec4(aPos, 1.0);
}
)";

    // Geometry shader: render to all 6 cube faces in one pass using layered rendering
    const char* gs = R"(
#version 460 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 18) out;

uniform mat4 uShadowMatrices[6];
uniform int uLayerOffset;

out vec4 FragPos;

void main() {
    for (int face = 0; face < 6; ++face) {
        gl_Layer = uLayerOffset + face;
        for (int i = 0; i < 3; ++i) {
            FragPos = gl_in[i].gl_Position;
            gl_Position = uShadowMatrices[face] * FragPos;
            EmitVertex();
        }
        EndPrimitive();
    }
}
)";

    // Fragment shader: write linear depth
    const char* fs = R"(
#version 460 core
in vec4 FragPos;
uniform vec3 uLightPos;
uniform float uFarPlane;
void main() {
    float dist = length(FragPos.xyz - uLightPos);
    gl_FragDepth = dist / uFarPlane;
}
)";

    GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsh, 1, &vs, nullptr);
    glCompileShader(vsh);
    GLuint gsh = glCreateShader(GL_GEOMETRY_SHADER);
    glShaderSource(gsh, 1, &gs, nullptr);
    glCompileShader(gsh);
    GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsh, 1, &fs, nullptr);
    glCompileShader(fsh);

    m_pointShadowProgram = glCreateProgram();
    glAttachShader(m_pointShadowProgram, vsh);
    glAttachShader(m_pointShadowProgram, gsh);
    glAttachShader(m_pointShadowProgram, fsh);
    glLinkProgram(m_pointShadowProgram);
    glDeleteShader(vsh);
    glDeleteShader(gsh);
    glDeleteShader(fsh);

    m_pointShadowLocModel = glGetUniformLocation(m_pointShadowProgram, "uModel");
    m_pointShadowLocLightPos = glGetUniformLocation(m_pointShadowProgram, "uLightPos");
    m_pointShadowLocFarPlane = glGetUniformLocation(m_pointShadowProgram, "uFarPlane");
    m_pointShadowLocShadowMatrices = glGetUniformLocation(m_pointShadowProgram, "uShadowMatrices[0]");

    // Create cube map array texture
    glGenTextures(1, &m_pointShadowCubeArray);
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_pointShadowCubeArray);
    glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24,
                 kPointShadowMapSize, kPointShadowMapSize, kMaxPointShadowLights * 6,
                 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

    // Create FBO (layered attachment – geometry shader selects the layer)
    glGenFramebuffers(1, &m_pointShadowFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFbo);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_pointShadowCubeArray, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
}

void OpenGLRenderer::releasePointShadowResources()
{
    if (m_pointShadowProgram)   { glDeleteProgram(m_pointShadowProgram); m_pointShadowProgram = 0; }
    if (m_pointShadowCubeArray) { glDeleteTextures(1, &m_pointShadowCubeArray); m_pointShadowCubeArray = 0; }
    if (m_pointShadowFbo)       { glDeleteFramebuffers(1, &m_pointShadowFbo); m_pointShadowFbo = 0; }
}

void OpenGLRenderer::findPointShadowLightIndices()
{
    m_pointShadowCount = 0;
    for (int i = 0; i < static_cast<int>(m_sceneLights.size()) && m_pointShadowCount < kMaxPointShadowLights; ++i)
    {
        if (m_sceneLights[i].type == 0) // point light
        {
            m_pointShadowLightIndices[m_pointShadowCount] = i;
            m_pointShadowPositions[m_pointShadowCount] = m_sceneLights[i].position;
            m_pointShadowFarPlanes[m_pointShadowCount] = m_sceneLights[i].range > 0.0f ? m_sceneLights[i].range : 25.0f;
            ++m_pointShadowCount;
        }
    }
}

void OpenGLRenderer::renderPointShadowMaps(const std::vector<DrawCmd>& drawList)
{
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glUseProgram(m_pointShadowProgram);

    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFbo);
    glViewport(0, 0, kPointShadowMapSize, kPointShadowMapSize);

    // Clear all layers once
    glClear(GL_DEPTH_BUFFER_BIT);

    const GLint locLayerOffset = glGetUniformLocation(m_pointShadowProgram, "uLayerOffset");

    for (int s = 0; s < m_pointShadowCount; ++s)
    {
        const glm::vec3& lightPos = m_pointShadowPositions[s];
        const float farPlane = m_pointShadowFarPlanes[s];
        const float nearPlane = 0.1f;

        const glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);
        glm::mat4 shadowViews[6] = {
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
        };

        // Upload per-light uniforms
        if (m_pointShadowLocLightPos >= 0)
            glUniform3fv(m_pointShadowLocLightPos, 1, &lightPos[0]);
        if (m_pointShadowLocFarPlane >= 0)
            glUniform1f(m_pointShadowLocFarPlane, farPlane);
        if (m_pointShadowLocShadowMatrices >= 0)
            glUniformMatrix4fv(m_pointShadowLocShadowMatrices, 6, GL_FALSE, &shadowViews[0][0][0]);
        if (locLayerOffset >= 0)
            glUniform1i(locLayerOffset, s * 6);

        for (const auto& cmd : drawList)
        {
            if (m_pointShadowLocModel >= 0)
                glUniformMatrix4fv(m_pointShadowLocModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);

            auto* mat = cmd.obj->getMaterial();
            if (!mat)
                continue;
            glBindVertexArray(mat->getVao());
            if (mat->getIndexCount() > 0)
                glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
            else
                glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
        }
    }

    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
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

    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
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
    if (cullWasEnabled)
    {
        glEnable(GL_CULL_FACE);
    }
}

void OpenGLRenderer::buildHzb()
{
    const Vec2 viewportSize = getViewportSize();
    const int width = static_cast<int>(viewportSize.x);
    const int height = static_cast<int>(viewportSize.y);
    if (width <= 0 || height <= 0)
    {
        return;
    }
    if (!ensureHzbResources(width, height))
    {
        return;
    }

    constexpr int kMinReadbackMip = 3;
    const int readbackLevels = static_cast<int>(m_hzbCpuData.size());

    // Map the previous frame's PBO to retrieve async readback data (non-blocking)
    if (m_hzbPboReady && m_hzbPboSize > 0)
    {
        const int readPbo = (m_hzbPboIndex + 1) % kHzbPboCount;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hzbPbos[readPbo]);
        const float* mapped = static_cast<const float*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
        if (mapped)
        {
            size_t offset = 0;
            for (int i = 0; i < readbackLevels; ++i)
            {
                const int mip = kMinReadbackMip + i;
                if (mip >= m_hzbMipLevels)
                {
                    break;
                }
                const int mipW = std::max(1, width >> mip);
                const int mipH = std::max(1, height >> mip);
                const size_t count = static_cast<size_t>(mipW) * mipH;
                auto& level = m_hzbCpuData[i];
                level.width = mipW;
                level.height = mipH;
                level.data.resize(count);
                std::memcpy(level.data.data(), mapped + offset, count * sizeof(float));
                offset += count;
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    // Copy viewport tab FBO depth into the depth texture via blit
    GLuint viewportFbo = 0;
    for (const auto& tab : m_editorTabs)
    {
        if (tab.id == "Viewport")
        {
            viewportFbo = tab.fbo;
            break;
        }
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, viewportFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_hzbDepthCopyFbo);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_hzbFullscreenVao);

    // Mip 0: copy depth texture into HZB R32F texture
    glBindFramebuffer(GL_FRAMEBUFFER, m_hzbFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hzbTexture, 0);
    glViewport(0, 0, width, height);
    glUseProgram(m_hzbCopyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hzbDepthTexture);
    if (m_hzbCopyDepthTexLoc >= 0)
    {
        glUniform1i(m_hzbCopyDepthTexLoc, 0);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Mip 1+: downsample with max operator
    glUseProgram(m_hzbDownsampleProgram);
    if (m_hzbDownsampleTexLoc >= 0)
    {
        glUniform1i(m_hzbDownsampleTexLoc, 0);
    }
    glBindTexture(GL_TEXTURE_2D, m_hzbTexture);

    for (int mip = 1; mip < m_hzbMipLevels; ++mip)
    {
        const int mipW = std::max(1, width >> mip);
        const int mipH = std::max(1, height >> mip);

        glTextureBarrier();

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hzbTexture, mip);
        glViewport(0, 0, mipW, mipH);

        if (m_hzbPrevMipLoc >= 0)
        {
            glUniform1i(m_hzbPrevMipLoc, mip - 1);
        }

        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Initiate async readback into the current PBO (GPU will fill it in the background)
    if (m_hzbPboSize > 0)
    {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hzbPbos[m_hzbPboIndex]);
        size_t offset = 0;
        for (int i = 0; i < readbackLevels; ++i)
        {
            const int mip = kMinReadbackMip + i;
            if (mip >= m_hzbMipLevels)
            {
                break;
            }
            const int mipW = std::max(1, width >> mip);
            const int mipH = std::max(1, height >> mip);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hzbTexture, mip);
            glReadPixels(0, 0, mipW, mipH, GL_RED, GL_FLOAT, reinterpret_cast<void*>(offset * sizeof(float)));
            offset += static_cast<size_t>(mipW) * mipH;
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        m_hzbPboIndex = (m_hzbPboIndex + 1) % kHzbPboCount;
        m_hzbPboReady = true;
    }

    // Restore viewport tab FBO so subsequent passes (pick, outline) draw into it
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo);
    glBindVertexArray(0);
    glUseProgram(0);

    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, width, height);
}

bool OpenGLRenderer::testAabbAgainstHzb(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const glm::mat4& viewProj) const
{
    if (m_hzbCpuData.empty() || m_hzbWidth <= 0 || m_hzbHeight <= 0)
    {
        return true;
    }

    constexpr int kMinReadbackMip = 3;

    const glm::vec3 corners[8] = {
        {boundsMin.x, boundsMin.y, boundsMin.z},
        {boundsMax.x, boundsMin.y, boundsMin.z},
        {boundsMin.x, boundsMax.y, boundsMin.z},
        {boundsMax.x, boundsMax.y, boundsMin.z},
        {boundsMin.x, boundsMin.y, boundsMax.z},
        {boundsMax.x, boundsMin.y, boundsMax.z},
        {boundsMin.x, boundsMax.y, boundsMax.z},
        {boundsMax.x, boundsMax.y, boundsMax.z}
    };

    float sMinX = 1.0f, sMaxX = 0.0f;
    float sMinY = 1.0f, sMaxY = 0.0f;
    float minZ = 1.0f;

    for (int i = 0; i < 8; ++i)
    {
        const glm::vec4 clip = viewProj * glm::vec4(corners[i], 1.0f);
        if (clip.w <= 0.0f)
        {
            return true;
        }
        const float invW = 1.0f / clip.w;
        const float sx = clip.x * invW * 0.5f + 0.5f;
        const float sy = clip.y * invW * 0.5f + 0.5f;
        const float sz = clip.z * invW * 0.5f + 0.5f;

        sMinX = std::min(sMinX, sx);
        sMaxX = std::max(sMaxX, sx);
        sMinY = std::min(sMinY, sy);
        sMaxY = std::max(sMaxY, sy);
        minZ = std::min(minZ, sz);
    }

    sMinX = std::max(0.0f, sMinX);
    sMaxX = std::min(1.0f, sMaxX);
    sMinY = std::max(0.0f, sMinY);
    sMaxY = std::min(1.0f, sMaxY);

    if (sMinX >= sMaxX || sMinY >= sMaxY)
    {
        return true;
    }

    const float screenW = (sMaxX - sMinX) * static_cast<float>(m_hzbWidth);
    const float screenH = (sMaxY - sMinY) * static_cast<float>(m_hzbHeight);
    const float maxDim = std::max(screenW, screenH);
    int mipLevel = (maxDim > 1.0f) ? static_cast<int>(std::ceil(std::log2(maxDim))) : 0;

    // Clamp to available readback range
    if (mipLevel < kMinReadbackMip)
    {
        return true;
    }

    const int idx = mipLevel - kMinReadbackMip;
    if (idx >= static_cast<int>(m_hzbCpuData.size()))
    {
        return true;
    }

    const auto& level = m_hzbCpuData[idx];
    if (level.width <= 0 || level.height <= 0 || level.data.empty())
    {
        return true;
    }

    const int x0 = std::max(0, static_cast<int>(sMinX * level.width));
    const int y0 = std::max(0, static_cast<int>(sMinY * level.height));
    const int x1 = std::min(level.width - 1, static_cast<int>(sMaxX * level.width));
    const int y1 = std::min(level.height - 1, static_cast<int>(sMaxY * level.height));

    float maxHzbDepth = 0.0f;
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const float d = level.data[y * level.width + x];
            maxHzbDepth = std::max(maxHzbDepth, d);
        }
    }

    return minZ <= maxHzbDepth;
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

    // Cache uniform locations per program
    auto it = m_uiPanelUniformCache.find(program);
    if (it == m_uiPanelUniformCache.end())
    {
        UIPanelUniforms u{};
        u.projection  = glGetUniformLocation(program, "uProjection");
        u.color       = glGetUniformLocation(program, "uColor");
        u.borderColor = glGetUniformLocation(program, "uBorderColor");
        u.borderSize  = glGetUniformLocation(program, "uBorderSize");
        u.rect        = glGetUniformLocation(program, "uRect");
        u.viewportSize = glGetUniformLocation(program, "uViewportSize");
        u.hoverColor  = glGetUniformLocation(program, "uHoverColor");
        u.isHovered   = glGetUniformLocation(program, "uIsHovered");
        it = m_uiPanelUniformCache.emplace(program, u).first;
    }
    const auto& u = it->second;

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
    glUniformMatrix4fv(u.projection, 1, GL_FALSE, &projection[0][0]);
    glUniform4fv(u.color, 1, &glColor[0]);
    if (u.borderColor >= 0) glUniform4fv(u.borderColor, 1, &glBorderColor[0]);
    if (u.borderSize >= 0)  glUniform1f(u.borderSize, 1.0f);
    if (u.rect >= 0)        glUniform4f(u.rect, x0, y0, x1, y1);
    if (u.viewportSize >= 0) glUniform2f(u.viewportSize, viewportSize.x, viewportSize.y);
    if (u.hoverColor >= 0)  glUniform4fv(u.hoverColor, 1, &glHoverColor[0]);
    if (u.isHovered >= 0)   glUniform1f(u.isHovered, isHovered ? 1.0f : 0.0f);

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

void OpenGLRenderer::setVSyncEnabled(bool enabled)
{
    m_vsyncEnabled = enabled;
    if (m_window)
    {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }
}

GLuint OpenGLRenderer::getOrLoadUITexture(const std::string& path)
{
    if (path.empty())
    {
        return 0;
    }
    auto it = m_uiTextureCache.find(path);
    if (it != m_uiTextureCache.end())
    {
        return it->second;
    }

    std::string resolved = path;
    {
        const std::filesystem::path p(resolved);
        if (p.is_relative())
        {
            const std::filesystem::path editorPath = std::filesystem::current_path() / "Editor" / "Textures" / path;
            if (std::filesystem::exists(editorPath))
            {
                resolved = editorPath.string();
            }
        }
    }

    int w = 0;
    int h = 0;
    int ch = 0;
    auto* data = AssetManager::Instance().loadRawImageData(resolved, w, h, ch);
    if (!data || w <= 0 || h <= 0)
    {
        m_uiTextureCache[path] = 0;
        return 0;
    }

    GLenum format = GL_RGBA;
    GLenum internalFormat = GL_RGBA8;
    if (ch == 3)
    {
        format = GL_RGB;
        internalFormat = GL_RGB8;
    }
    else if (ch == 1)
    {
        format = GL_RED;
        internalFormat = GL_R8;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    AssetManager::Instance().freeRawImageData(data);

    m_uiTextureCache[path] = tex;
    return tex;
}

GLuint OpenGLRenderer::preloadUITexture(const std::string& path)
{
    return getOrLoadUITexture(path);
}

void OpenGLRenderer::drawUIImage(float x0, float y0, float x1, float y1, GLuint textureId, const glm::mat4& projection, const Vec4& tintColor, bool invertRGB)
{
    if (textureId == 0 || m_uiQuadVao == 0)
    {
        return;
    }

    if (m_uiImageProgram == 0)
    {
        const char* vsSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vTexCoord;
uniform mat4 uProjection;
uniform vec4 uRect;
void main() {
    gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
    vec2 normalised = (aPos - uRect.xy) / max(uRect.zw - uRect.xy, vec2(1.0));
    vTexCoord = vec2(normalised.x, normalised.y);
}
)";
        const char* fsSource = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec4 uTintColor;
uniform float uInvertRGB;
void main() {
    vec4 texel = texture(uTexture, vTexCoord);
    texel.rgb = mix(texel.rgb, vec3(1.0) - texel.rgb, uInvertRGB);
    FragColor = texel * uTintColor;
}
)";
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &vsSource, nullptr);
        glCompileShader(vs);
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &fsSource, nullptr);
        glCompileShader(fs);
        m_uiImageProgram = glCreateProgram();
        glAttachShader(m_uiImageProgram, vs);
        glAttachShader(m_uiImageProgram, fs);
        glLinkProgram(m_uiImageProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        // Cache uniform locations once
        m_uiImageUniforms.projection = glGetUniformLocation(m_uiImageProgram, "uProjection");
        m_uiImageUniforms.rect       = glGetUniformLocation(m_uiImageProgram, "uRect");
        m_uiImageUniforms.tintColor  = glGetUniformLocation(m_uiImageProgram, "uTintColor");
        m_uiImageUniforms.invertRGB  = glGetUniformLocation(m_uiImageProgram, "uInvertRGB");
        m_uiImageUniforms.texture    = glGetUniformLocation(m_uiImageProgram, "uTexture");
    }

    float vertices[6][2] = {
        { x0, y1 },
        { x0, y0 },
        { x1, y0 },
        { x0, y1 },
        { x1, y0 },
        { x1, y1 }
    };

    glUseProgram(m_uiImageProgram);
    glUniformMatrix4fv(m_uiImageUniforms.projection, 1, GL_FALSE, &projection[0][0]);
    glUniform4f(m_uiImageUniforms.rect, x0, y0, x1, y1);
    glUniform4f(m_uiImageUniforms.tintColor, tintColor.x, tintColor.y, tintColor.z, tintColor.w);
    glUniform1f(m_uiImageUniforms.invertRGB, invertRGB ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glUniform1i(m_uiImageUniforms.texture, 0);

    glBindVertexArray(m_uiQuadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

Vec2 OpenGLRenderer::getViewportSize() const
{
    if (m_cachedWindowWidth > 0 && m_cachedWindowHeight > 0)
    {
        return Vec2{ static_cast<float>(m_cachedWindowWidth), static_cast<float>(m_cachedWindowHeight) };
    }
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

Vec3 OpenGLRenderer::getCameraPosition() const
{
    if (m_camera)
    {
        return m_camera->getPosition();
    }
    return Vec3{};
}

void OpenGLRenderer::setCameraPosition(const Vec3& position)
{
    if (m_camera)
    {
        m_camera->setPosition(position);
    }
}

Vec2 OpenGLRenderer::getCameraRotationDegrees() const
{
    if (m_camera)
    {
        return m_camera->getRotationDegrees();
    }
    return Vec2{};
}

void OpenGLRenderer::setCameraRotationDegrees(float yawDegrees, float pitchDegrees)
{
    if (m_camera)
    {
        m_camera->setRotationDegrees(yawDegrees, pitchDegrees);
    }
}

void OpenGLRenderer::setActiveCameraEntity(unsigned int entity)
{
    m_activeCameraEntity = entity;
}

unsigned int OpenGLRenderer::getActiveCameraEntity() const
{
    return m_activeCameraEntity;
}

void OpenGLRenderer::clearActiveCameraEntity()
{
    m_activeCameraEntity = 0;
}

const std::string& OpenGLRenderer::name() const
{
    return m_name;
}

SDL_Window* OpenGLRenderer::window() const
{
    return m_window;
}

// â”€â”€â”€ Entity pick FBO â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
bool OpenGLRenderer::ensurePickFbo(int width, int height)
{
    if (m_pickFbo != 0 && m_pickWidth == width && m_pickHeight == height)
    {
        return true;
    }
    releasePickFbo();
    m_pickWidth = width;
    m_pickHeight = height;

    glGenFramebuffers(1, &m_pickFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);

    glGenTextures(1, &m_pickColorTex);
    glBindTexture(GL_TEXTURE_2D, m_pickColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pickColorTex, 0);

    glGenRenderbuffers(1, &m_pickDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_pickDepthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_pickDepthRbo);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        releasePickFbo();
        return false;
    }

    // Build pick shader if needed
    if (m_pickProgram == 0)
    {
        const std::string vs =
            "#version 460 core\n"
            "layout(location = 0) in vec3 aPos;\n"
            "layout(location = 1) in vec3 aNormal;\n"
            "layout(location = 2) in vec2 aTexCoord;\n"
            "uniform mat4 uModel;\n"
            "uniform mat4 uView;\n"
            "uniform mat4 uProjection;\n"
            "void main() {\n"
            "    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);\n"
            "}\n";
        const std::string fs =
            "#version 460 core\n"
            "layout(location = 0) out uint oEntityId;\n"
            "uniform uint uEntityId;\n"
            "void main() {\n"
            "    oEntityId = uEntityId;\n"
            "}\n";

        auto vShader = std::make_shared<OpenGLShader>();
        auto fShader = std::make_shared<OpenGLShader>();
        if (!vShader->loadFromSource(Shader::Type::Vertex, vs) ||
            !fShader->loadFromSource(Shader::Type::Fragment, fs))
        {
            return false;
        }
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vShader->id());
        glAttachShader(prog, fShader->id());
        glLinkProgram(prog);
        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            glDeleteProgram(prog);
            return false;
        }
        m_pickProgram = prog;
        m_pickLocModel = glGetUniformLocation(prog, "uModel");
        m_pickLocView = glGetUniformLocation(prog, "uView");
        m_pickLocProjection = glGetUniformLocation(prog, "uProjection");
        m_pickLocEntityId = glGetUniformLocation(prog, "uEntityId");
    }
    return true;
}

void OpenGLRenderer::releasePickFbo()
{
    if (m_pickColorTex) { glDeleteTextures(1, &m_pickColorTex); m_pickColorTex = 0; }
    if (m_pickDepthRbo) { glDeleteRenderbuffers(1, &m_pickDepthRbo); m_pickDepthRbo = 0; }
    if (m_pickFbo) { glDeleteFramebuffers(1, &m_pickFbo); m_pickFbo = 0; }
    if (m_pickProgram) { glDeleteProgram(m_pickProgram); m_pickProgram = 0; }
    m_pickWidth = 0;
    m_pickHeight = 0;
}

void OpenGLRenderer::renderPickBuffer(const glm::mat4& view, const glm::mat4& projection)
{
    if (m_pickFbo == 0 || m_pickProgram == 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
    const GLuint clearId = 0;
    glClearBufferuiv(GL_COLOR, 0, &clearId);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_pickProgram);
    if (m_pickLocView >= 0)
        glUniformMatrix4fv(m_pickLocView, 1, GL_FALSE, &view[0][0]);
    if (m_pickLocProjection >= 0)
        glUniformMatrix4fv(m_pickLocProjection, 1, GL_FALSE, &projection[0][0]);

    for (const auto& cmd : m_drawList)
    {
        if (cmd.entityId == 0 || !cmd.obj)
            continue;

        if (m_pickLocModel >= 0)
            glUniformMatrix4fv(m_pickLocModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);
        if (m_pickLocEntityId >= 0)
            glUniform1ui(m_pickLocEntityId, cmd.entityId);

        const GLuint vao = cmd.obj->getVao();
        const GLsizei indexCount = cmd.obj->getIndexCount();
        const GLsizei vertexCount = cmd.obj->getVertexCount();
        glBindVertexArray(vao);
        if (indexCount > 0)
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        else
            glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    // Restore viewport tab FBO so selection outline draws into the tab
    GLuint vpFbo = 0;
    for (const auto& tab : m_editorTabs)
    {
        if (tab.active)
        {
            vpFbo = tab.fbo;
            break;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, vpFbo);
}

void OpenGLRenderer::renderPickBufferSingleEntity(const glm::mat4& view, const glm::mat4& projection, unsigned int entityId)
{
    if (m_pickFbo == 0 || m_pickProgram == 0 || entityId == 0)
        return;

    glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
    const GLuint clearId = 0;
    glClearBufferuiv(GL_COLOR, 0, &clearId);
    glClear(GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_pickProgram);
    if (m_pickLocView >= 0)
        glUniformMatrix4fv(m_pickLocView, 1, GL_FALSE, &view[0][0]);
    if (m_pickLocProjection >= 0)
        glUniformMatrix4fv(m_pickLocProjection, 1, GL_FALSE, &projection[0][0]);

    for (const auto& cmd : m_drawList)
    {
        if (cmd.entityId != entityId || !cmd.obj)
            continue;

        if (m_pickLocModel >= 0)
            glUniformMatrix4fv(m_pickLocModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);
        if (m_pickLocEntityId >= 0)
            glUniform1ui(m_pickLocEntityId, cmd.entityId);

        const GLuint vao = cmd.obj->getVao();
        const GLsizei indexCount = cmd.obj->getIndexCount();
        const GLsizei vertexCount = cmd.obj->getVertexCount();
        glBindVertexArray(vao);
        if (indexCount > 0)
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        else
            glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        break;
    }

    glBindVertexArray(0);
    glUseProgram(0);
    GLuint vpFbo = 0;
    for (const auto& tab : m_editorTabs)
    {
        if (tab.active)
        {
            vpFbo = tab.fbo;
            break;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, vpFbo);
}

unsigned int OpenGLRenderer::pickEntityAt(int x, int y)
{
    if (m_pickFbo == 0)
        return 0;

    // Flip Y (SDL top-left â†’ GL bottom-left)
    const int glY = m_pickHeight - 1 - y;
    if (x < 0 || x >= m_pickWidth || glY < 0 || glY >= m_pickHeight)
        return 0;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pickFbo);
    unsigned int entityId = 0;
    glReadPixels(x, glY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &entityId);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return entityId;
}

unsigned int OpenGLRenderer::pickEntityAtImmediate(int x, int y)
{
    const Vec2 vs = getViewportSize();
    const int w = static_cast<int>(vs.x);
    const int h = static_cast<int>(vs.y);
    if (w <= 0 || h <= 0)
        return 0;

    if (!ensurePickFbo(w, h))
        return 0;

    renderPickBuffer(m_lastViewMatrix, m_projectionMatrix);
    return pickEntityAt(x, y);
}

bool OpenGLRenderer::screenToWorldPos(int screenX, int screenY, Vec3& outWorldPos) const
{
    // Find the active tab FBO to read depth from
    GLuint fbo = 0;
    int fboW = 0, fboH = 0;
    for (const auto& tab : m_editorTabs)
    {
        if (tab.active && tab.fbo != 0)
        {
            fbo = tab.fbo;
            fboW = tab.fboWidth;
            fboH = tab.fboHeight;
            break;
        }
    }

    if (fbo == 0)
    {
        const Vec2 vs = getViewportSize();
        fboW = static_cast<int>(vs.x);
        fboH = static_cast<int>(vs.y);
    }

    if (fboW <= 0 || fboH <= 0)
        return false;

    const int glY = fboH - 1 - screenY;
    if (screenX < 0 || screenX >= fboW || glY < 0 || glY >= fboH)
        return false;

    // Read depth at pixel
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    float depth = 1.0f;
    glReadPixels(screenX, glY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

    // depth == 1.0 means far plane (no geometry)
    if (depth >= 1.0f)
        return false;

    // Unproject: NDC -> world
    const float ndcX = (2.0f * static_cast<float>(screenX) / static_cast<float>(fboW)) - 1.0f;
    const float ndcY = (2.0f * static_cast<float>(glY) / static_cast<float>(fboH)) - 1.0f;
    const float ndcZ = 2.0f * depth - 1.0f;

    const glm::mat4 invViewProj = glm::inverse(m_projectionMatrix * m_lastViewMatrix);
    const glm::vec4 clipPos(ndcX, ndcY, ndcZ, 1.0f);
    glm::vec4 worldPos = invViewProj * clipPos;

    if (std::abs(worldPos.w) < 1e-7f)
        return false;

    worldPos /= worldPos.w;
    outWorldPos = Vec3{ worldPos.x, worldPos.y, worldPos.z };
    return true;
}

// â”€â”€â”€ Selection outline (post-process edge detection on pick buffer) â”€â”€â”€â”€â”€â”€â”€â”€â”€
bool OpenGLRenderer::ensureOutlineResources()
{
    if (m_outlineProgram != 0)
        return true;

    const std::string vs =
        "#version 460 core\n"
        "out vec2 vTexCoord;\n"
        "void main() {\n"
        "    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
        "    vTexCoord = pos;\n"
        "    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
        "}\n";

    const std::string fs =
        "#version 460 core\n"
        "out vec4 FragColor;\n"
        "in vec2 vTexCoord;\n"
        "uniform usampler2D uPickTex;\n"
        "uniform uint uSelectedId;\n"
        "uniform vec4 uOutlineColor;\n"
        "uniform float uThickness;\n"
        "void main() {\n"
        "    ivec2 coord = ivec2(gl_FragCoord.xy);\n"
        "    uint center = texelFetch(uPickTex, coord, 0).r;\n"
        "    bool centerIsSelected = (center == uSelectedId);\n"
        "    int r = int(uThickness);\n"
        "    bool foundEdge = false;\n"
        "    for (int dy = -r; dy <= r && !foundEdge; ++dy) {\n"
        "        for (int dx = -r; dx <= r && !foundEdge; ++dx) {\n"
        "            if (dx == 0 && dy == 0) continue;\n"
        "            uint neighbor = texelFetch(uPickTex, coord + ivec2(dx, dy), 0).r;\n"
        "            bool neighborIsSelected = (neighbor == uSelectedId);\n"
        "            if (centerIsSelected != neighborIsSelected) {\n"
        "                foundEdge = true;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    if (foundEdge) {\n"
        "        FragColor = uOutlineColor;\n"
        "    } else {\n"
        "        discard;\n"
        "    }\n"
        "}\n";

    auto vShader = std::make_shared<OpenGLShader>();
    auto fShader = std::make_shared<OpenGLShader>();
    if (!vShader->loadFromSource(Shader::Type::Vertex, vs) ||
        !fShader->loadFromSource(Shader::Type::Fragment, fs))
        return false;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vShader->id());
    glAttachShader(prog, fShader->id());
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) { glDeleteProgram(prog); return false; }

    m_outlineProgram = prog;
    m_outlineLocPickTex = glGetUniformLocation(prog, "uPickTex");
    m_outlineLocSelectedId = glGetUniformLocation(prog, "uSelectedId");
    m_outlineLocOutlineColor = glGetUniformLocation(prog, "uOutlineColor");
    m_outlineLocThickness = glGetUniformLocation(prog, "uThickness");

    glGenVertexArrays(1, &m_outlineVao);

    return true;
}

void OpenGLRenderer::releaseOutlineResources()
{
    if (m_outlineProgram) { glDeleteProgram(m_outlineProgram); m_outlineProgram = 0; }
    if (m_outlineVao) { glDeleteVertexArrays(1, &m_outlineVao); m_outlineVao = 0; }
}

void OpenGLRenderer::drawSelectionOutline()
{
    if (!ensureOutlineResources() || m_pickColorTex == 0 || m_selectedEntity == 0)
        return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_outlineProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_pickColorTex);
    if (m_outlineLocPickTex >= 0)
        glUniform1i(m_outlineLocPickTex, 0);
    if (m_outlineLocSelectedId >= 0)
        glUniform1ui(m_outlineLocSelectedId, m_selectedEntity);
    if (m_outlineLocOutlineColor >= 0)
        glUniform4f(m_outlineLocOutlineColor, 1.0f, 0.6f, 0.0f, 1.0f);
    if (m_outlineLocThickness >= 0)
        glUniform1f(m_outlineLocThickness, 2.0f);

    glBindVertexArray(m_outlineVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

// â”€â”€â”€ Editor tab system â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
void OpenGLRenderer::addTab(const std::string& id, const std::string& tabName, bool closable)
{
    for (const auto& tab : m_editorTabs)
    {
        if (tab.id == id)
        {
            return;
        }
    }
    EditorTab tab;
    tab.id = id;
    tab.name = tabName;
    tab.closable = closable;
    tab.active = m_editorTabs.empty();
    if (tab.active)
    {
        m_activeTabId = id;
    }
    m_editorTabs.push_back(std::move(tab));
}

void OpenGLRenderer::removeTab(const std::string& id)
{
    auto it = std::find_if(m_editorTabs.begin(), m_editorTabs.end(),
        [&](const EditorTab& tab) { return tab.id == id; });
    if (it == m_editorTabs.end() || !it->closable)
    {
        return;
    }
    const bool wasActive = it->active;
    releaseTabFbo(*it);
    m_editorTabs.erase(it);
    if (wasActive && !m_editorTabs.empty())
    {
        m_editorTabs.front().active = true;
        m_activeTabId = m_editorTabs.front().id;
    }
}

void OpenGLRenderer::setActiveTab(const std::string& id)
{
    // Block tab switching during PIE
    if (DiagnosticsManager::Instance().isPIEActive())
    {
        return;
    }

    if (m_activeTabId == id)
    {
        return;
    }

    auto& diag = DiagnosticsManager::Instance();
    const std::string oldTabId = m_activeTabId;

    // --- Level swap: leaving a mesh viewer tab → give level back to viewer ---
    if (oldTabId != "Viewport")
    {
        auto viewerIt = m_meshViewers.find(oldTabId);
        if (viewerIt != m_meshViewers.end() && viewerIt->second)
        {
            // Save per-tab entity selection
            m_tabSelectedEntity[oldTabId] = m_selectedEntity;

            // Save camera state into the viewer's runtime level before swapping out
            auto returnedLevel = diag.swapActiveLevel(std::move(m_savedViewportLevel));
            if (returnedLevel)
            {
                if (m_camera)
                {
                    returnedLevel->setEditorCameraPosition(m_camera->getPosition());
                    returnedLevel->setEditorCameraRotation(m_camera->getRotationDegrees());
                    returnedLevel->setHasEditorCamera(true);
                }
                returnedLevel->resetPreparedState();
                viewerIt->second->giveRuntimeLevel(std::move(returnedLevel));
            }
        }
    }
    else
    {
        // Leaving Viewport: save the editor camera state and selection
        m_savedViewportSelectedEntity = m_selectedEntity;
        if (m_camera)
        {
            m_savedCameraPos = m_camera->getPosition();
            m_savedCameraRot = m_camera->getRotationDegrees();
        }
    }

    // Clear selection before switching tabs
    m_selectedEntity = 0;
    m_uiManager.selectEntity(0);

    // --- Level swap: entering a mesh viewer tab → swap in viewer's level ---
    if (id != "Viewport")
    {
        auto viewerIt = m_meshViewers.find(id);
        if (viewerIt != m_meshViewers.end() && viewerIt->second)
        {
            auto viewerLevel = viewerIt->second->takeRuntimeLevel();
            if (viewerLevel)
            {
                viewerLevel->resetPreparedState();
                m_savedViewportLevel = diag.swapActiveLevel(std::move(viewerLevel));
                if (m_savedViewportLevel)
                {
                    m_savedViewportLevel->resetPreparedState();
                }
            }
            diag.setScenePrepared(false);

            // Restore per-tab entity selection
            auto selIt = m_tabSelectedEntity.find(id);
            if (selIt != m_tabSelectedEntity.end())
            {
                m_selectedEntity = selIt->second;
                m_uiManager.selectEntity(m_selectedEntity);
            }
        }
    }
    else
    {
        // Returning to Viewport: restore the saved level
        if (m_savedViewportLevel)
        {
            m_savedViewportLevel->resetPreparedState();
            auto old = diag.swapActiveLevel(std::move(m_savedViewportLevel));
            if (old)
            {
                auto viewerIt = m_meshViewers.find(oldTabId);
                if (viewerIt != m_meshViewers.end() && viewerIt->second)
                {
                    old->resetPreparedState();
                    viewerIt->second->giveRuntimeLevel(std::move(old));
                }
            }
            diag.setScenePrepared(false);
        }
        // Restore editor camera and selection
        if (m_camera)
        {
            m_camera->setPosition(m_savedCameraPos);
            m_camera->setRotationDegrees(m_savedCameraRot.x, m_savedCameraRot.y);
        }
        m_selectedEntity = m_savedViewportSelectedEntity;
        m_uiManager.selectEntity(m_selectedEntity);
    }

    // Snapshot the current active tab so it can be displayed as a cached image later
    for (auto& tab : m_editorTabs)
    {
        if (tab.active && tab.colorTex != 0)
        {
            snapshotTabBeforeSwitch(tab);
        }
        tab.active = (tab.id == id);
        if (tab.active)
        {
            m_activeTabId = id;
        }
    }

    // Update the UIManager's active tab for widget filtering
    m_uiManager.setActiveTabId(id);
}

const std::string& OpenGLRenderer::getActiveTabId() const
{
    return m_activeTabId;
}

const std::vector<EditorTab>& OpenGLRenderer::getTabs() const
{
    return m_editorTabs;
}

bool OpenGLRenderer::ensureTabFbo(EditorTab& tab, int width, int height)
{
    if (tab.fbo != 0 && tab.fboWidth == width && tab.fboHeight == height)
    {
        return true;
    }
    releaseTabFbo(tab);
    tab.fboWidth = width;
    tab.fboHeight = height;

    glGenFramebuffers(1, &tab.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, tab.fbo);

    glGenTextures(1, &tab.colorTex);
    glBindTexture(GL_TEXTURE_2D, tab.colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tab.colorTex, 0);

    glGenRenderbuffers(1, &tab.depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, tab.depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, tab.depthRbo);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        releaseTabFbo(tab);
        return false;
    }
    return true;
}

void OpenGLRenderer::releaseTabFbo(EditorTab& tab)
{
    if (tab.colorTex) { glDeleteTextures(1, &tab.colorTex); tab.colorTex = 0; }
    if (tab.depthRbo) { glDeleteRenderbuffers(1, &tab.depthRbo); tab.depthRbo = 0; }
    if (tab.fbo) { glDeleteFramebuffers(1, &tab.fbo); tab.fbo = 0; }
    if (tab.snapshotTex) { glDeleteTextures(1, &tab.snapshotTex); tab.snapshotTex = 0; }
    tab.fboWidth = 0;
    tab.fboHeight = 0;
    tab.hasSnapshot = false;
}

void OpenGLRenderer::releaseAllTabFbos()
{
    for (auto& tab : m_editorTabs)
    {
        releaseTabFbo(tab);
    }
}

void OpenGLRenderer::snapshotTabBeforeSwitch(EditorTab& tab)
{
    if (tab.colorTex == 0 || tab.fboWidth <= 0 || tab.fboHeight <= 0)
    {
        return;
    }

    // Create or resize snapshot texture
    if (tab.snapshotTex == 0)
    {
        glGenTextures(1, &tab.snapshotTex);
        glBindTexture(GL_TEXTURE_2D, tab.snapshotTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tab.fboWidth, tab.fboHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, tab.snapshotTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tab.fboWidth, tab.fboHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Copy colorTex -> snapshotTex via FBO blit
    GLuint srcFbo = 0, dstFbo = 0;
    glGenFramebuffers(1, &srcFbo);
    glGenFramebuffers(1, &dstFbo);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tab.colorTex, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tab.snapshotTex, 0);

    glBlitFramebuffer(0, 0, tab.fboWidth, tab.fboHeight,
        0, 0, tab.fboWidth, tab.fboHeight,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &srcFbo);
    glDeleteFramebuffers(1, &dstFbo);

    tab.hasSnapshot = true;
}

// ============================================================================
// Editor Gizmos
// ============================================================================

bool OpenGLRenderer::ensureGizmoResources()
{
    if (m_gizmoProgram != 0)
        return true;

    const char* vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";
    const char* fs = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

    GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsh, 1, &vs, nullptr);
    glCompileShader(vsh);
    GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsh, 1, &fs, nullptr);
    glCompileShader(fsh);
    m_gizmoProgram = glCreateProgram();
    glAttachShader(m_gizmoProgram, vsh);
    glAttachShader(m_gizmoProgram, fsh);
    glLinkProgram(m_gizmoProgram);
    glDeleteShader(vsh);
    glDeleteShader(fsh);

    m_gizmoLocMVP = glGetUniformLocation(m_gizmoProgram, "uMVP");
    m_gizmoLocColor = glGetUniformLocation(m_gizmoProgram, "uColor");

    glGenVertexArrays(1, &m_gizmoVao);
    glGenBuffers(1, &m_gizmoVbo);
    glBindVertexArray(m_gizmoVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
    glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);

    return true;
}

void OpenGLRenderer::releaseGizmoResources()
{
    if (m_gizmoProgram) { glDeleteProgram(m_gizmoProgram); m_gizmoProgram = 0; }
    if (m_gizmoVbo) { glDeleteBuffers(1, &m_gizmoVbo); m_gizmoVbo = 0; }
    if (m_gizmoVao) { glDeleteVertexArrays(1, &m_gizmoVao); m_gizmoVao = 0; }
}

static void buildCircleVerts(std::vector<float>& verts, int segments, float radius, int axis)
{
    for (int i = 0; i <= segments; ++i)
    {
        const float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(segments);
        const float c = std::cos(angle) * radius;
        const float s = std::sin(angle) * radius;
        switch (axis)
        {
        case 0: verts.push_back(0.0f); verts.push_back(c);    verts.push_back(s);    break;
        case 1: verts.push_back(c);    verts.push_back(0.0f); verts.push_back(s);    break;
        case 2: verts.push_back(c);    verts.push_back(s);    verts.push_back(0.0f); break;
        }
    }
}

glm::mat3 OpenGLRenderer::getEntityRotationMatrix(const ECS::TransformComponent& tc) const
{
    const float rx = glm::radians(tc.rotation[0]);
    const float ry = glm::radians(tc.rotation[1]);
    const float rz = glm::radians(tc.rotation[2]);
    // Must match the object rendering order: T * Rx * Ry * Rz * S
    const glm::mat4 rotMat =
        glm::rotate(glm::mat4(1.0f), rx, glm::vec3(1, 0, 0)) *
        glm::rotate(glm::mat4(1.0f), ry, glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1.0f), rz, glm::vec3(0, 0, 1));
    return glm::mat3(rotMat);
}

glm::vec3 OpenGLRenderer::getGizmoWorldAxis(const ECS::TransformComponent& tc, int axisIdx) const
{
    const glm::mat3 rot = getEntityRotationMatrix(tc);
    return glm::normalize(rot[axisIdx]);
}

void OpenGLRenderer::renderGizmo(const glm::mat4& view, const glm::mat4& projection)
{
    if (m_selectedEntity == 0 || m_gizmoMode == GizmoMode::None)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    const auto* tc = ecs.getComponent<ECS::TransformComponent>(m_selectedEntity);
    if (!tc)
        return;

    if (!ensureGizmoResources())
        return;

    const glm::vec3 entityPos{ tc->position[0], tc->position[1], tc->position[2] };

    const glm::vec4 viewPos = view * glm::vec4(entityPos, 1.0f);
    const float dist = std::abs(viewPos.z);
    const float gizmoScale = dist * 0.12f;

    // Build model matrix: translate to entity, apply entity rotation, then scale
    const glm::mat3 rotMat = getEntityRotationMatrix(*tc);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), entityPos);
    model *= glm::mat4(rotMat);
    model = model * glm::scale(glm::mat4(1.0f), glm::vec3(gizmoScale));
    const glm::mat4 mvp = projection * view * model;

    glUseProgram(m_gizmoProgram);
    glUniformMatrix4fv(m_gizmoLocMVP, 1, GL_FALSE, &mvp[0][0]);
    glBindVertexArray(m_gizmoVao);

    glDisable(GL_DEPTH_TEST);
    glLineWidth(4.0f);

    const glm::vec3 axisColors[3] = {
        { 1.0f, 0.2f, 0.2f },
        { 0.2f, 1.0f, 0.2f },
        { 0.3f, 0.3f, 1.0f }
    };

    if (m_gizmoMode == GizmoMode::Translate)
    {
        for (int a = 0; a < 3; ++a)
        {
            const bool hovered = (static_cast<int>(m_gizmoHoveredAxis) - 1 == a) ||
                                 (static_cast<int>(m_gizmoActiveAxis) - 1 == a);
            glm::vec3 col = hovered ? glm::vec3(1.0f, 1.0f, 0.3f) : axisColors[a];
            glUniform3fv(m_gizmoLocColor, 1, &col[0]);

            // In local space, axes are simply (1,0,0), (0,1,0), (0,0,1)
            glm::vec3 dir(0.0f); dir[a] = 1.0f;
            glm::vec3 up(0.0f); up[(a + 1) % 3] = 1.0f;
            const float arrowLen = 0.15f;
            const glm::vec3 tip = dir;
            const glm::vec3 back = dir * (1.0f - arrowLen);

            float verts[] = {
                0.0f, 0.0f, 0.0f, dir.x, dir.y, dir.z,
                tip.x, tip.y, tip.z, back.x + up.x * arrowLen * 0.3f, back.y + up.y * arrowLen * 0.3f, back.z + up.z * arrowLen * 0.3f,
                tip.x, tip.y, tip.z, back.x - up.x * arrowLen * 0.3f, back.y - up.y * arrowLen * 0.3f, back.z - up.z * arrowLen * 0.3f
            };

            glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
            glDrawArrays(GL_LINES, 0, 6);
        }
    }
    else if (m_gizmoMode == GizmoMode::Rotate)
    {
        constexpr int segments = 48;
        for (int a = 0; a < 3; ++a)
        {
            const bool hovered = (static_cast<int>(m_gizmoHoveredAxis) - 1 == a) ||
                                 (static_cast<int>(m_gizmoActiveAxis) - 1 == a);
            glm::vec3 col = hovered ? glm::vec3(1.0f, 1.0f, 0.3f) : axisColors[a];
            glUniform3fv(m_gizmoLocColor, 1, &col[0]);

            std::vector<float> circleVerts;
            circleVerts.reserve((segments + 1) * 3);
            buildCircleVerts(circleVerts, segments, 1.0f, a);

            glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, circleVerts.size() * sizeof(float), circleVerts.data());
            glDrawArrays(GL_LINE_STRIP, 0, segments + 1);
        }
    }
    else if (m_gizmoMode == GizmoMode::Scale)
    {
        for (int a = 0; a < 3; ++a)
        {
            const bool hovered = (static_cast<int>(m_gizmoHoveredAxis) - 1 == a) ||
                                 (static_cast<int>(m_gizmoActiveAxis) - 1 == a);
            glm::vec3 col = hovered ? glm::vec3(1.0f, 1.0f, 0.3f) : axisColors[a];
            glUniform3fv(m_gizmoLocColor, 1, &col[0]);

            glm::vec3 dir(0.0f); dir[a] = 1.0f;
            const float cubeSize = 0.06f;
            const glm::vec3 tip = dir;

            float shaft[] = { 0.0f, 0.0f, 0.0f, tip.x, tip.y, tip.z };
            glBindBuffer(GL_ARRAY_BUFFER, m_gizmoVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(shaft), shaft);
            glDrawArrays(GL_LINES, 0, 2);

            glm::vec3 u(0.0f), v(0.0f);
            u[(a + 1) % 3] = cubeSize;
            v[(a + 2) % 3] = cubeSize;
            float cube[] = {
                tip.x - u.x, tip.y - u.y, tip.z - u.z, tip.x + u.x, tip.y + u.y, tip.z + u.z,
                tip.x - v.x, tip.y - v.y, tip.z - v.z, tip.x + v.x, tip.y + v.y, tip.z + v.z,
            };
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(cube), cube);
            glDrawArrays(GL_LINES, 0, 4);
        }
    }

    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
    glBindVertexArray(0);
}

// Pick gizmo axis using rotated (local-space) axes projected to screen
OpenGLRenderer::GizmoAxis OpenGLRenderer::pickGizmoAxis(const glm::mat4& view, const glm::mat4& projection, int screenX, int screenY) const
{
    if (m_selectedEntity == 0 || m_gizmoMode == GizmoMode::None)
        return GizmoAxis::None;

    auto& ecs = ECS::ECSManager::Instance();
    const auto* tc = ecs.getComponent<ECS::TransformComponent>(m_selectedEntity);
    if (!tc)
        return GizmoAxis::None;

    const glm::vec3 entityPos{ tc->position[0], tc->position[1], tc->position[2] };
    const glm::vec4 viewPos = view * glm::vec4(entityPos, 1.0f);
    const float dist = std::abs(viewPos.z);
    const float gizmoScale = dist * 0.12f;

    const int w = m_cachedWindowWidth > 0 ? m_cachedWindowWidth : 1;
    const int h = m_cachedWindowHeight > 0 ? m_cachedWindowHeight : 1;

    const auto project = [&](const glm::vec3& worldPos) -> glm::vec2
    {
        const glm::vec4 clip = projection * view * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0001f) return { -9999.0f, -9999.0f };
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return {
            (ndc.x * 0.5f + 0.5f) * static_cast<float>(w),
            (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h)
        };
    };

    const auto distToSegment = [](glm::vec2 p, glm::vec2 a, glm::vec2 b) -> float
    {
        const glm::vec2 ab = b - a;
        const float lenSq = glm::dot(ab, ab);
        if (lenSq < 0.0001f) return glm::length(p - a);
        const float t = glm::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
        return glm::length(p - (a + ab * t));
    };

    const glm::vec2 mousePos{ static_cast<float>(screenX), static_cast<float>(screenY) };
    const glm::vec2 origin = project(entityPos);

    // Skip if entity center is behind camera
    if (origin.x < -9000.0f) return GizmoAxis::None;

    GizmoAxis best = GizmoAxis::None;
    constexpr float kPickThreshold = 22.0f; // pixel threshold
    float bestDist = kPickThreshold;

    if (m_gizmoMode == GizmoMode::Rotate)
    {
        // For rotation gizmos, test distance to the projected circle (ring)
        const glm::mat3 rotMat = getEntityRotationMatrix(*tc);
        constexpr int pickSegments = 32;

        for (int a = 0; a < 3; ++a)
        {
            float minDist = kPickThreshold;
            glm::vec2 prevScreen{ -9999.0f, -9999.0f };

            for (int i = 0; i <= pickSegments; ++i)
            {
                const float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(pickSegments);
                const float ca = std::cos(angle);
                const float sa = std::sin(angle);
                glm::vec3 localPt(0.0f);
                switch (a)
                {
                case 0: localPt = glm::vec3(0.0f, ca, sa); break; // YZ circle, perpendicular to X
                case 1: localPt = glm::vec3(ca, 0.0f, sa); break; // XZ circle, perpendicular to Y
                case 2: localPt = glm::vec3(ca, sa, 0.0f); break; // XY circle, perpendicular to Z
                }
                const glm::vec3 worldPt = entityPos + rotMat * localPt * gizmoScale;
                const glm::vec2 screenPt = project(worldPt);

                if (i > 0 && prevScreen.x > -9000.0f && screenPt.x > -9000.0f)
                {
                    const float d = distToSegment(mousePos, prevScreen, screenPt);
                    if (d < minDist)
                    {
                        minDist = d;
                    }
                }
                prevScreen = screenPt;
            }

            if (minDist < bestDist)
            {
                bestDist = minDist;
                best = static_cast<GizmoAxis>(a + 1);
            }
        }
    }
    else
    {
        // For translate / scale, test distance to the axis line segment
        for (int a = 0; a < 3; ++a)
        {
            const glm::vec3 worldAxis = getGizmoWorldAxis(*tc, a);
            const glm::vec2 tip = project(entityPos + worldAxis * gizmoScale);
            if (tip.x < -9000.0f) continue;

            const float d = distToSegment(mousePos, origin, tip);
            if (d < bestDist)
            {
                bestDist = d;
                best = static_cast<GizmoAxis>(a + 1);
            }
        }
    }

    return best;
}

// Helper: unproject screen coords to a world ray
static void screenToRay(int screenX, int screenY, int w, int h,
    const glm::mat4& invVP, glm::vec3& rayOrigin, glm::vec3& rayDir)
{
    const float nx = (2.0f * static_cast<float>(screenX) / static_cast<float>(w)) - 1.0f;
    const float ny = 1.0f - (2.0f * static_cast<float>(screenY) / static_cast<float>(h));

    glm::vec4 nearClip = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 farClip  = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
    nearClip /= nearClip.w;
    farClip  /= farClip.w;

    rayOrigin = glm::vec3(nearClip);
    rayDir = glm::normalize(glm::vec3(farClip) - glm::vec3(nearClip));
}

// Project a point onto the closest point on a ray (parameterized by t)
static float closestTOnAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
    const glm::vec3& axisOrigin, const glm::vec3& axisDir)
{
    // Find the parameter along axisDir where the two rays are closest
    // Using the standard closest-approach formula for two lines
    const glm::vec3 w0 = axisOrigin - rayOrigin;
    const float a = glm::dot(axisDir, axisDir);
    const float b = glm::dot(axisDir, rayDir);
    const float c = glm::dot(rayDir, rayDir);
    const float d = glm::dot(axisDir, w0);
    const float e = glm::dot(rayDir, w0);
    const float denom = a * c - b * b;
    if (std::abs(denom) < 1e-8f) return 0.0f;
    return (b * e - c * d) / denom;
}

bool OpenGLRenderer::beginGizmoDrag(int screenX, int screenY)
{
    if (m_selectedEntity == 0 || m_gizmoMode == GizmoMode::None || !m_camera)
        return false;

    const Mat4 engineView = m_camera->getViewMatrixColumnMajor();
    const glm::mat4 view = glm::make_mat4(engineView.m);

    const GizmoAxis axis = pickGizmoAxis(view, m_projectionMatrix, screenX, screenY);
    if (axis == GizmoAxis::None)
        return false;

    auto& ecs = ECS::ECSManager::Instance();
    const auto* tc = ecs.getComponent<ECS::TransformComponent>(m_selectedEntity);
    if (!tc)
        return false;

    const int axisIdx = static_cast<int>(axis) - 1;
    const glm::vec3 entityPos{ tc->position[0], tc->position[1], tc->position[2] };
    const glm::vec3 worldAxis = getGizmoWorldAxis(*tc, axisIdx);

    m_gizmoActiveAxis = axis;
    m_gizmoDragging = true;
    m_gizmoDragEntityStart = entityPos;
    m_gizmoDragWorldAxis = worldAxis;
    m_gizmoDragRotStart = tc->rotation[axisIdx];
    m_gizmoDragScaleStart = tc->scale[axisIdx];
    m_gizmoDragStartScreen = glm::vec2{ static_cast<float>(screenX), static_cast<float>(screenY) };
    m_gizmoDragOldTransform = *tc;

    // Compute the initial parameter along the axis ray
    const int w = m_cachedWindowWidth > 0 ? m_cachedWindowWidth : 1;
    const int h = m_cachedWindowHeight > 0 ? m_cachedWindowHeight : 1;
    const glm::mat4 invVP = glm::inverse(m_projectionMatrix * view);
    glm::vec3 rayO, rayD;
    screenToRay(screenX, screenY, w, h, invVP, rayO, rayD);
    m_gizmoDragStartT = closestTOnAxis(rayO, rayD, entityPos, worldAxis);

    return true;
}

void OpenGLRenderer::updateGizmoDrag(int screenX, int screenY)
{
    if (!m_gizmoDragging || m_gizmoActiveAxis == GizmoAxis::None || m_selectedEntity == 0 || !m_camera)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    auto* tc = ecs.getComponent<ECS::TransformComponent>(m_selectedEntity);
    if (!tc)
        return;

    const int axisIdx = static_cast<int>(m_gizmoActiveAxis) - 1;
    const Mat4 engineView = m_camera->getViewMatrixColumnMajor();
    const glm::mat4 view = glm::make_mat4(engineView.m);

    const int w = m_cachedWindowWidth > 0 ? m_cachedWindowWidth : 1;
    const int h = m_cachedWindowHeight > 0 ? m_cachedWindowHeight : 1;

    if (m_gizmoMode == GizmoMode::Translate)
    {
        // Ray-plane intersection for 1:1 movement
        const glm::mat4 invVP = glm::inverse(m_projectionMatrix * view);
        glm::vec3 rayO, rayD;
        screenToRay(screenX, screenY, w, h, invVP, rayO, rayD);

        const float currentT = closestTOnAxis(rayO, rayD, m_gizmoDragEntityStart, m_gizmoDragWorldAxis);
        const float deltaT = currentT - m_gizmoDragStartT;

        // Move entity along the world-space axis
        const glm::vec3 newPos = m_gizmoDragEntityStart + m_gizmoDragWorldAxis * deltaT;
        tc->position[0] = newPos.x;
        tc->position[1] = newPos.y;
        tc->position[2] = newPos.z;
    }
    else if (m_gizmoMode == GizmoMode::Rotate)
    {
        // Compute screen-space delta angle from mouse movement perpendicular
        // to the projected rotation axis.
        const auto project = [&](const glm::vec3& worldPos) -> glm::vec2
        {
            const glm::vec4 clip = m_projectionMatrix * view * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.0001f) return { 0.0f, 0.0f };
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return {
                (ndc.x * 0.5f + 0.5f) * static_cast<float>(w),
                (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h)
            };
        };

        const glm::vec2 originScreen = project(m_gizmoDragEntityStart);
        const glm::vec2 axisTipScreen = project(m_gizmoDragEntityStart + m_gizmoDragWorldAxis);
        glm::vec2 axisScreenDir = axisTipScreen - originScreen;
        const float axisScreenLen = glm::length(axisScreenDir);
        if (axisScreenLen < 1.0f) return;

        const glm::vec2 axisPerp{ -axisScreenDir.y / axisScreenLen, axisScreenDir.x / axisScreenLen };
        const glm::vec2 mouseDelta{
            static_cast<float>(screenX) - m_gizmoDragStartScreen.x,
            static_cast<float>(screenY) - m_gizmoDragStartScreen.y
        };
        const float projectedPixels = glm::dot(mouseDelta, axisPerp);
        const float deltaAngleRad = glm::radians(projectedPixels * 0.5f);

        // Build the old rotation matrix from the drag-start snapshot
        const glm::mat3 oldRot = getEntityRotationMatrix(m_gizmoDragOldTransform);

        // Apply incremental rotation in local space around the selected unit axis
        glm::vec3 localAxis(0.0f);
        localAxis[axisIdx] = 1.0f;
        const glm::mat3 deltaRot = glm::mat3(glm::rotate(glm::mat4(1.0f), deltaAngleRad, localAxis));
        const glm::mat3 newRot = oldRot * deltaRot;

        // Decompose back to Euler XYZ (matching Rx * Ry * Rz order)
        // R[2][0] = sin(ry)
        const float sinRy = glm::clamp(newRot[2][0], -1.0f, 1.0f);
        const float ry = std::asin(sinRy);
        float rx, rz;
        if (std::abs(std::cos(ry)) > 1e-4f)
        {
            rx = std::atan2(-newRot[2][1], newRot[2][2]);
            rz = std::atan2(-newRot[1][0], newRot[0][0]);
        }
        else
        {
            // Gimbal lock fallback
            rx = std::atan2(newRot[0][1], newRot[1][1]);
            rz = 0.0f;
        }

        tc->rotation[0] = glm::degrees(rx);
        tc->rotation[1] = glm::degrees(ry);
        tc->rotation[2] = glm::degrees(rz);
    }
    else if (m_gizmoMode == GizmoMode::Scale)
    {
        // Use screen-space pixel delta projected onto the screen-space axis direction
        const auto project = [&](const glm::vec3& worldPos) -> glm::vec2
        {
            const glm::vec4 clip = m_projectionMatrix * view * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.0001f) return { 0.0f, 0.0f };
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            return {
                (ndc.x * 0.5f + 0.5f) * static_cast<float>(w),
                (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h)
            };
        };

        const glm::vec2 originScreen = project(m_gizmoDragEntityStart);
        const glm::vec2 axisTipScreen = project(m_gizmoDragEntityStart + m_gizmoDragWorldAxis);
        const glm::vec2 axisScreenDir = axisTipScreen - originScreen;
        const float axisScreenLen = glm::length(axisScreenDir);
        if (axisScreenLen < 1.0f) return;

        const glm::vec2 axisNorm = axisScreenDir / axisScreenLen;
        const glm::vec2 mouseDelta{
            static_cast<float>(screenX) - m_gizmoDragStartScreen.x,
            static_cast<float>(screenY) - m_gizmoDragStartScreen.y
        };
        const float projectedPixels = glm::dot(mouseDelta, axisNorm);
        tc->scale[axisIdx] = std::max(0.01f, m_gizmoDragScaleStart + projectedPixels * 0.01f);
    }

    ecs.setComponent<ECS::TransformComponent>(m_selectedEntity, *tc);
}

void OpenGLRenderer::endGizmoDrag()
{
    if (m_selectedEntity != 0)
    {
        auto& ecs = ECS::ECSManager::Instance();
        const auto* tc = ecs.getComponent<ECS::TransformComponent>(m_selectedEntity);
        if (tc)
        {
            const ECS::TransformComponent newTransform = *tc;
            const ECS::TransformComponent oldTransform = m_gizmoDragOldTransform;
            const unsigned int entity = m_selectedEntity;

            std::string modeLabel = "Transform";
            switch (m_gizmoMode)
            {
            case GizmoMode::Translate: modeLabel = "Move"; break;
            case GizmoMode::Rotate:    modeLabel = "Rotate"; break;
            case GizmoMode::Scale:     modeLabel = "Scale"; break;
            default: break;
            }

            UndoRedoManager::Command cmd;
            cmd.description = modeLabel;
            cmd.execute = [entity, newTransform]() {
                auto& e = ECS::ECSManager::Instance();
                e.setComponent<ECS::TransformComponent>(entity, newTransform);
            };
            cmd.undo = [entity, oldTransform]() {
                auto& e = ECS::ECSManager::Instance();
                e.setComponent<ECS::TransformComponent>(entity, oldTransform);
            };
            UndoRedoManager::Instance().pushCommand(std::move(cmd));
        }
    }

    m_gizmoDragging = false;
    m_gizmoActiveAxis = GizmoAxis::None;
}
