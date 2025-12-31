#include "OpenGLRenderer.h"

OpenGLRenderer::OpenGLRenderer()
{
    m_initialized = false;
    m_name = "OpenGL Renderer";
    m_window = nullptr;
}

OpenGLRenderer::~OpenGLRenderer()
{
}

bool OpenGLRenderer::initialize(SDL_Window* appwindow)
{
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        return false;
    }

    m_window = appwindow;
    m_initialized = true;
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

}

const std::string& OpenGLRenderer::name() const
{
    return m_name;
}