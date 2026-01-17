#pragma once

#include "../Renderer.h"
#include "../Camera.h"
#include "glad/include/gl.h"
#include <memory>
#include <glm/glm.hpp>

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

    void moveCamera(float dx, float dy, float dz) override;
    void rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees) override;

private:
    bool m_initialized;
    std::string m_name;
    SDL_Window* m_window;
    SDL_GLContext m_glContext;

    std::unique_ptr<Camera> m_camera;
    glm::mat4 m_projectionMatrix;
};