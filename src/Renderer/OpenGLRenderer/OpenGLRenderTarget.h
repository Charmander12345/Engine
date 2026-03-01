#pragma once

#include "../IRenderTarget.h"
#include "glad/include/gl.h"

class OpenGLRenderTarget : public IRenderTarget
{
public:
    OpenGLRenderTarget() = default;
    ~OpenGLRenderTarget() override { destroy(); }

    bool resize(int width, int height) override;
    void bind() override;
    void unbind() override;
    void destroy() override;
    bool isValid() const override;

    int getWidth() const override { return m_width; }
    int getHeight() const override { return m_height; }

    unsigned int getColorTextureId() const override { return m_colorTex; }

    void takeSnapshot() override;
    bool hasSnapshot() const override { return m_hasSnapshot; }
    unsigned int getSnapshotTextureId() const override { return m_snapshotTex; }

    /// OpenGL-specific: raw FBO handle for blit / read operations.
    GLuint getGLFramebuffer() const { return m_fbo; }

private:
    GLuint m_fbo{ 0 };
    GLuint m_colorTex{ 0 };
    GLuint m_depthRbo{ 0 };
    GLuint m_snapshotTex{ 0 };
    int    m_width{ 0 };
    int    m_height{ 0 };
    bool   m_hasSnapshot{ false };
};
