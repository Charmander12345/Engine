#include "OpenGLRenderer.h"
#include <vector>
#include <string>
#include <SDL3/SDL.h>
#include <iostream>
#include "Logger.h"

namespace
{
    GLuint CompileShader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint CreateProgram(const char* vsSource, const char* fsSource)
    {
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vsSource);
        if (!vs)
        {
            return 0;
        }
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fsSource);
        if (!fs)
        {
            glDeleteShader(vs);
            return 0;
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);

        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (!linked)
        {
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }
}

OpenGLRenderer::OpenGLRenderer()
{
    m_initialized = false;
    m_name = "OpenGL Renderer";
    m_window = nullptr;
    m_vao = 0;
    m_vbo = 0;
    m_program = 0;
}

OpenGLRenderer::~OpenGLRenderer()
{
    if (m_program)
    {
        glDeleteProgram(m_program);
    }
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

    m_window = appwindow;

    const char* vertexSrc = R"GLSL(
        #version 150 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec3 aColor;
        out vec3 vColor;
        void main()
        {
            vColor = aColor;
            gl_Position = vec4(aPos, 1.0);
        }
    )GLSL";

    const char* fragmentSrc = R"GLSL(
        #version 150 core
        in vec3 vColor;
        out vec4 FragColor;
        void main()
        {
            FragColor = vec4(vColor, 1.0);
        }
    )GLSL";

    m_program = CreateProgram(vertexSrc, fragmentSrc);
    if (!m_program)
    {
        logger.log("Shader-Programm konnte nicht erstellt werden", Logger::LogLevel::ERROR);
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

    glUseProgram(m_program);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

const std::string& OpenGLRenderer::name() const
{
    return m_name;
}