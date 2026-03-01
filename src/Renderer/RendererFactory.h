#pragma once

#include <memory>

class Renderer;
class SplashWindow;

/// Supported renderer backends.
enum class RendererBackend
{
    OpenGL,
    Vulkan,      // placeholder – not yet implemented
    DirectX12,   // placeholder – not yet implemented
    Software     // placeholder – not yet implemented
};

/// Factory for creating backend-specific Renderer and SplashWindow instances.
/// The concrete implementation lives in the backend library (e.g. RendererOpenGL).
class RendererFactory
{
public:
    /// Create a new Renderer for the given backend.
    /// The caller owns the returned pointer (delete when done).
    static Renderer* createRenderer(RendererBackend backend);

    /// Create a new SplashWindow for the given backend.
    static std::unique_ptr<SplashWindow> createSplashWindow(RendererBackend backend);
};
