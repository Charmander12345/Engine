#include "OpenGLRenderer.h"
#include <vector>
#include <string>
#include <filesystem>
#include <SDL3/SDL.h>
#include <iostream>
#include "Logger.h"
#include "OpenGLShader.h"
#include "../../Basics/Object3D.h"

namespace
{
    std::string ResolveShaderPath(const std::string& filename)
    {
        std::filesystem::path base = std::filesystem::current_path();

        std::filesystem::path shadersfolder = base / "shaders" / filename;

        if (std::filesystem::exists(shadersfolder))
        {
            return shadersfolder.string();
        }
        return {};
    }
}

OpenGLRenderer::OpenGLRenderer()
{
    m_initialized = false;
    m_name = "OpenGL Renderer";
    m_window = nullptr;
    m_glContext = nullptr;
}

OpenGLRenderer::~OpenGLRenderer()
{
    SDL_GL_DestroyContext(m_glContext);
    SDL_DestroyWindow(m_window);
}

bool OpenGLRenderer::initialize()
{
    auto& logger = Logger::Instance();
    logger.log("OpenGLRenderer initialize started", Logger::LogLevel::INFO);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    logger.log("Creating Window...", Logger::LogLevel::INFO);

    m_window = SDL_CreateWindow("Engine Project", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_MINIMIZED);
    if (!m_window) {
        logger.log(std::string("Failed to create window: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_Quit();
        return false;
    }
    logger.log("Window created successfully.", Logger::LogLevel::INFO);
    logger.log("Creating OpenGL context...", Logger::LogLevel::INFO);

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext) {
        logger.log(std::string("Failed to create OpenGL context: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    if (!SDL_GL_MakeCurrent(m_window, m_glContext)) {
        logger.log(std::string("Failed to make created context current: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_GL_DestroyContext(m_glContext);
        SDL_DestroyWindow(m_window);
        SDL_Quit();
        return false;
    }

    logger.log("OpenGL context created successfully.", Logger::LogLevel::INFO);

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        logger.log("Failed to initialize GLAD", Logger::LogLevel::ERROR);
        return false;
    }

    logger.log("GLAD initialised.", Logger::LogLevel::INFO);

    int nrAttributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
    logger.log("Max number of vertex attributes: " + std::to_string(nrAttributes), Logger::LogLevel::INFO);

    std::string vertexPath = ResolveShaderPath("vertex.glsl");
    std::string fragmentPath = ResolveShaderPath("fragment.glsl");

    if (vertexPath.empty() || fragmentPath.empty())
    {
        logger.log("Couldn't locate vertex.glsl and/or fragment.glsl.", Logger::LogLevel::ERROR);
        return false;
    }

    auto vertexShader = std::make_shared<OpenGLShader>();
    auto fragmentShader = std::make_shared<OpenGLShader>();

    if (!vertexShader->loadFromFile(Shader::Type::Vertex, vertexPath))
    {
        logger.log("Failed to load/compile vertex.glsl.", Logger::LogLevel::ERROR);
        return false;
    }

    if (!fragmentShader->loadFromFile(Shader::Type::Fragment, fragmentPath))
    {
        logger.log("Failed to load/compile fragment.glsl.", Logger::LogLevel::ERROR);
        return false;
    }

    m_material = std::make_shared<OpenGLMaterial>();
    m_material->addShader(vertexShader);
    m_material->addShader(fragmentShader);

    // Build a static 3D object with vertex data and layout
    auto object3D = std::make_shared<Object3D>();
    object3D->setMaterial(m_material);

    const std::vector<float> vertices = {
        // positions        // colors
         0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };
    object3D->setVertices(vertices);

    m_material->setVertexData(vertices);

    std::vector<OpenGLMaterial::LayoutElement> layout = {
        {0, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(6 * sizeof(float)), 0},
        {1, 3, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(6 * sizeof(float)), static_cast<size_t>(3 * sizeof(float))}
    };
    m_material->setLayout(layout);

    if (!m_material->build())
    {
        logger.log("Failed to build OpenGL material (program link).", Logger::LogLevel::ERROR);
        return false;
    }

    logger.log("Shader program created successfully.", Logger::LogLevel::INFO);

    glEnable(GL_DEPTH_TEST);

    m_initialized = true;
    logger.log("Initialisation of the OpenGL renderer complete.", Logger::LogLevel::INFO);
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

    if (m_material)
    {
        m_material->bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        m_material->unbind();
    }
}

const std::string& OpenGLRenderer::name() const
{
    return m_name;
}