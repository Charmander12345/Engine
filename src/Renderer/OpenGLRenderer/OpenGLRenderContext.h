#pragma once

#include "../IRenderContext.h"
#include <SDL3/SDL.h>

// OpenGL implementation of IRenderContext.
// Creates a shared GL context for secondary windows (popups).
class OpenGLRenderContext : public IRenderContext
{
public:
    ~OpenGLRenderContext() override { destroy(); }

    bool initialize(SDL_Window* window) override
    {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
        m_context = SDL_GL_CreateContext(window);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
        return m_context != nullptr;
    }

    void makeCurrent(SDL_Window* window) override
    {
        SDL_GL_MakeCurrent(window, m_context);
    }

    void destroy() override
    {
        if (m_context)
        {
            SDL_GL_DestroyContext(m_context);
            m_context = nullptr;
        }
    }

private:
    SDL_GLContext m_context{ nullptr };
};
