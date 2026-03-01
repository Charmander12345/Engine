#pragma once

#include <string>

/// Abstract base class for splash / startup windows shown before the main engine window.
/// Each backend provides a concrete implementation (e.g. OpenGLSplashWindow).
class SplashWindow
{
public:
    virtual ~SplashWindow() = default;

    /// Create the splash window.  Returns false on failure.
    virtual bool create() = 0;

    /// Update the status text shown in the bottom-left corner.
    virtual void setStatus(const std::string& text) = 0;

    /// Pump SDL events and render one frame.
    virtual void render() = 0;

    /// Destroy the window and release all resources.
    virtual void close() = 0;

    /// True while the window is alive.
    virtual bool isOpen() const = 0;

    /// Returns true if the user pressed the close button / Alt-F4.
    virtual bool wasCloseRequested() const = 0;
};
