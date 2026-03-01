#pragma once

struct SDL_Window;

// Backend-agnostic rendering context for secondary windows (popups, etc.).
// Each backend provides a concrete implementation (e.g. OpenGLRenderContext).
class IRenderContext
{
public:
    virtual ~IRenderContext() = default;

    // Create/initialize the context for the given window.
    virtual bool initialize(SDL_Window* window) = 0;

    // Make this context current for the given window.
    virtual void makeCurrent(SDL_Window* window) = 0;

    // Release/destroy the context.
    virtual void destroy() = 0;
};
