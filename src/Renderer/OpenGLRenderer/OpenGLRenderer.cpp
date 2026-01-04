#include "OpenGLRenderer.h"
#include <vector>
#include <string>
#include <filesystem>
#include <SDL3/SDL.h>
#include <iostream>
#include "Logger.h"
#include "OpenGLShader.h"

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
    m_vao = 0;
    m_vbo = 0;
}

OpenGLRenderer::~OpenGLRenderer()
{
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
    }
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
    }
}

bool OpenGLRenderer::initialize(SDL_Window* appwindow)
{
    auto& logger = Logger::Instance();
    logger.log("OpenGLRenderer initialize gestartet", Logger::LogLevel::INFO);

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        logger.log("Failed to initialize GLAD", Logger::LogLevel::ERROR);
        return false;
    }

    logger.log("GLAD initialisiert", Logger::LogLevel::INFO);

    int nrAttributes;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
    logger.log("Maximale Anzahl an Vertex-Attributen: " + std::to_string(nrAttributes), Logger::LogLevel::INFO);

    m_window = appwindow;

    std::string vertexPath = ResolveShaderPath("vertex.glsl");
    std::string fragmentPath = ResolveShaderPath("fragment.glsl");

    if (vertexPath.empty() || fragmentPath.empty())
    {
        logger.log("Shader-Dateien vertex.glsl oder fragment.glsl wurden nicht gefunden", Logger::LogLevel::ERROR);
        return false;
    }

    OpenGLShader vertexShader;
    OpenGLShader fragmentShader;

    if (!vertexShader.loadFromFile(OpenGLShader::ShaderType::Vertex, vertexPath))
    {
        logger.log("Vertex-Shader konnte nicht geladen/kompiliert werden", Logger::LogLevel::ERROR);
        return false;
    }

    if (!fragmentShader.loadFromFile(OpenGLShader::ShaderType::Fragment, fragmentPath))
    {
        logger.log("Fragment-Shader konnte nicht geladen/kompiliert werden", Logger::LogLevel::ERROR);
        return false;
    }

    if (!m_program.attach(vertexShader) || !m_program.attach(fragmentShader) || !m_program.link())
    {
        logger.log("Shader-Programm konnte nicht erstellt werden: " + m_program.linkLog(), Logger::LogLevel::ERROR);
        return false;
    }

    logger.log("Shader-Programm erstellt", Logger::LogLevel::INFO);

    const float vertices[] = {
        // positions        // colors
         0.0f,  0.5f, 0.0f,  1.0f, 0.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,  0.0f, 1.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  0.0f, 0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);

    m_initialized = true;
    logger.log("OpenGLRenderer initialize erfolgreich", Logger::LogLevel::INFO);
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

    m_program.bind();
    glBindVertexArray(m_vao);
    //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    m_program.unbind();
}

const std::string& OpenGLRenderer::name() const
{
    return m_name;
}