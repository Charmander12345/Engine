#pragma once

#include "../Renderer.h"
#include "glad/include/gl.h"
#include "OpenGLMaterial.h"
#include <memory>

class OpenGLRenderer : public Renderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool initialize() override;
    void clear() override;
    void render() override;
    void present() override;
    const std::string& name() const override;

private:
    bool m_initialized;
    std::string m_name;
    SDL_Window* m_window;
    SDL_GLContext m_glContext;
    std::shared_ptr<OpenGLMaterial> m_material;
};