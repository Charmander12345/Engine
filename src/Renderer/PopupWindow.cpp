#include "PopupWindow.h"

#include "Logger.h"

PopupWindow::~PopupWindow()
{
    destroy();
}

bool PopupWindow::create(const std::string& title, int width, int height)
{
    m_width  = width;
    m_height = height;

    m_window = SDL_CreateWindow(title.c_str(), width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_window)
    {
        Logger::Instance().log(Logger::Category::Rendering,
            std::string("PopupWindow: SDL_CreateWindow failed: ") + SDL_GetError(),
            Logger::LogLevel::ERROR);
        return false;
    }

    // Request a context that shares resources with the currently-current context.
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    m_context = SDL_GL_CreateContext(m_window);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);

    if (!m_context)
    {
        Logger::Instance().log(Logger::Category::Rendering,
            std::string("PopupWindow: SDL_GL_CreateContext failed: ") + SDL_GetError(),
            Logger::LogLevel::ERROR);
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }

    m_open = true;
    SDL_ShowWindow(m_window);
    SDL_StartTextInput(m_window);
    return true;
}

void PopupWindow::destroy()
{
    if (m_context)
    {
        SDL_GL_DestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_window)
    {
        SDL_StopTextInput(m_window);
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    m_open = false;
}

void PopupWindow::refreshSize()
{
    if (m_window)
    {
        SDL_GetWindowSizeInPixels(m_window, &m_width, &m_height);
    }
}
