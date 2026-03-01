#include "PopupWindow.h"
#include "IRenderContext.h"

#include "Logger.h"

PopupWindow::~PopupWindow()
{
    destroy();
}

bool PopupWindow::create(const std::string& title, int width, int height,
                         SDL_WindowFlags extraFlags, std::unique_ptr<IRenderContext> context)
{
    m_width  = width;
    m_height = height;

    m_window = SDL_CreateWindow(title.c_str(), width, height,
        SDL_WINDOW_RESIZABLE | extraFlags);
    if (!m_window)
    {
        Logger::Instance().log(Logger::Category::Rendering,
            std::string("PopupWindow: SDL_CreateWindow failed: ") + SDL_GetError(),
            Logger::LogLevel::ERROR);
        return false;
    }

    if (context)
    {
        if (!context->initialize(m_window))
        {
            Logger::Instance().log(Logger::Category::Rendering,
                std::string("PopupWindow: render context initialization failed"),
                Logger::LogLevel::ERROR);
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
            return false;
        }
        m_renderContext = std::move(context);
    }

    m_open = true;
    SDL_ShowWindow(m_window);
    SDL_StartTextInput(m_window);
    return true;
}

void PopupWindow::destroy()
{
    if (m_renderContext)
    {
        m_renderContext->destroy();
        m_renderContext.reset();
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
