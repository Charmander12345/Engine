#pragma once

#include <string>
#include <memory>
#include <SDL3/SDL.h>

#include "../UIManager.h"

class IRenderContext;

// A secondary OS window with a backend-agnostic render context and its own UIManager.
// Owned by the Renderer implementation; created via openPopupWindow().
class PopupWindow
{
public:
    PopupWindow() = default;
    ~PopupWindow();

    // Create the SDL window with the given extra flags (e.g. SDL_WINDOW_OPENGL)
    // and initialize the provided render context.
    // Returns false on failure.
    bool create(const std::string& title, int width, int height,
                SDL_WindowFlags extraFlags, std::unique_ptr<IRenderContext> context);

    // Destroy SDL window and render context.
    void destroy();

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }

    SDL_Window*     sdlWindow()     const { return m_window; }
    IRenderContext* renderContext() const { return m_renderContext.get(); }
    SDL_WindowID    windowId()      const { return m_window ? SDL_GetWindowID(m_window) : 0; }

    UIManager&       uiManager()       { return m_uiManager; }
    const UIManager& uiManager() const { return m_uiManager; }

    int width()  const { return m_width;  }
    int height() const { return m_height; }

    // Update cached size from the actual SDL window (call after resize events).
    void refreshSize();

private:
    SDL_Window*                     m_window { nullptr };
    std::unique_ptr<IRenderContext> m_renderContext;
    UIManager                       m_uiManager;
    bool                            m_open   { false };
    int                             m_width  { 0 };
    int                             m_height { 0 };
};
