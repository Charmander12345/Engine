#include "../RendererFactory.h"
#include "OpenGLRenderer.h"
#include "OpenGLSplashWindow.h"

#include <stdexcept>

Renderer* RendererFactory::createRenderer(RendererBackend backend)
{
    switch (backend)
    {
    case RendererBackend::OpenGL:
        return new OpenGLRenderer();
    default:
        throw std::runtime_error("RendererFactory: unsupported backend");
    }
}

std::unique_ptr<SplashWindow> RendererFactory::createSplashWindow(RendererBackend backend)
{
    switch (backend)
    {
    case RendererBackend::OpenGL:
        return std::make_unique<OpenGLSplashWindow>();
    default:
        throw std::runtime_error("RendererFactory: unsupported backend for SplashWindow");
    }
}
