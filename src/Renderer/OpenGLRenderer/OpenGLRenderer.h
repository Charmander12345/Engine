#pragma once

#include "../Renderer.h"
#include "glad/include/gl.h"

class OpenGLRenderer : public Renderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool initialize(SDL_Window* appwindow) override;
    void clear() override;
    void render() override;
    void present() override;
    const std::string& name() const override;

private:
    bool m_initialized;
    std::string m_name;
    SDL_Window* m_window;
    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_program;
};