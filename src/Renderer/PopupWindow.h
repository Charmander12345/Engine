#pragma once

#include <string>
#include <SDL3/SDL.h>

#include "UIManager.h"

// A secondary OS window with a shared OpenGL context and its own UIManager.
// Owned by OpenGLRenderer; created via openPopupWindow().
class PopupWindow
{
public:
    PopupWindow() = default;
    ~PopupWindow();

    // Create the SDL window and a shared GL context (main context must be current).
    // Returns false on failure.
    bool create(const std::string& title, int width, int height);

    // Destroy SDL window and GL context.
    void destroy();

    bool isOpen() const { return m_open; }
    void close() { m_open = false; }

    SDL_Window*    sdlWindow() const { return m_window; }
    SDL_GLContext  glContext()  const { return m_context; }
    SDL_WindowID   windowId()  const { return m_window ? SDL_GetWindowID(m_window) : 0; }

    UIManager&       uiManager()       { return m_uiManager; }
    const UIManager& uiManager() const { return m_uiManager; }

    int width()  const { return m_width;  }
    int height() const { return m_height; }

    // Update cached size from the actual SDL window (call after resize events).
    void refreshSize();

private:
    SDL_Window*   m_window  { nullptr };
    SDL_GLContext m_context { nullptr };
    UIManager     m_uiManager;
    bool          m_open   { false };
    int           m_width  { 0 };
    int           m_height { 0 };
};
