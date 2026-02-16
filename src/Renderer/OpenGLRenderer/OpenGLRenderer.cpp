#include "OpenGLRenderer.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Logger.h"
#include "OpenGLMaterial.h"
#include "OpenGLCamera.h"

#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Basics/EngineLevel.h"

#include "../RenderResourceManager.h"

#include "../../Basics/Object3D.h"
#include "../../Basics/Object2D.h"
#include "../../Basics/RenderableObject.h"
#include "../../Basics/MathTypes.h"

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

    auto& diagnostics = DiagnosticsManager::Instance();
    if (EngineLevel* level = diagnostics.getActiveLevel())
    {
        auto& objs = level->getWorldObjects();
        size_t cleared = 0;
        for (auto& entry : objs)
        {
            if (!entry || !entry->object)
                continue;

            // Use RenderableObject base class to avoid dynamic_cast
            if (auto renderableObj = std::dynamic_pointer_cast<RenderableObject>(entry->object))
            {
                renderableObj->setMaterial(nullptr);
                ++cleared;
            }
        }

        logger.log(Logger::Category::Rendering,
            "OpenGLRenderer shutdown: cleared materials on " + std::to_string(cleared) + " world objects.",
            Logger::LogLevel::INFO);
    }

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
    if (!m_initialized || !m_window)
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
    EngineLevel* level = diagnostics.getActiveLevel();
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
    }

    glm::mat4 view(1.0f);
    if (m_camera)
    {
        Mat4 engineView = m_camera->getViewMatrixColumnMajor();
        view = glm::make_mat4(engineView.m);
    }

    const auto& objs = level->getWorldObjects();
	const auto& groups = level->getGroups();

    // Helper lambda to render a single object
    auto renderObject = [&](const std::shared_ptr<EngineObject>& obj, const Transform* t, bool is3D) {
        if (!obj || obj->getPath().empty() || !t)
            return;

        auto renderableObj = std::dynamic_pointer_cast<RenderableObject>(obj);
        if (!renderableObj)
            return;

        auto material = renderableObj->getMaterial();
        auto glMaterial = std::dynamic_pointer_cast<OpenGLMaterial>(material);
        if (glMaterial)
        {
            Mat4 engineMat = t->getMatrix4ColumnMajor();
            glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);

            if (is3D)
            {
                modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 1.0f, 0.0f));
            }
            else
            {
                modelMatrix = glm::rotate(modelMatrix, (float)SDL_GetTicks() / 1000.0f, glm::vec3(0.0f, 0.0f, 1.0f));
            }

            glMaterial->setModelMatrix(modelMatrix);
            glMaterial->setViewMatrix(view);
            glMaterial->setProjectionMatrix(m_projectionMatrix);
        }
        renderableObj->render();
    };

    if (!objs.empty())
    {
        for (const auto& entry : objs)
        {
            if (!entry)
                continue;

            const Transform* t = &entry->transform;
            bool is3D = std::dynamic_pointer_cast<Object3D>(entry->object) != nullptr;
            renderObject(entry->object, t, is3D);
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
				const Transform* t = (i < groupEntry.transforms.size()) ? &groupEntry.transforms[i] : nullptr;
                bool is3D = std::dynamic_pointer_cast<Object3D>(entry) != nullptr;
                renderObject(entry, t, is3D);
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