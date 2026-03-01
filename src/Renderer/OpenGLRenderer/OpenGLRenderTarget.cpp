#include "OpenGLRenderTarget.h"

bool OpenGLRenderTarget::resize(int width, int height)
{
    if (m_fbo != 0 && m_width == width && m_height == height)
        return true;

    destroy();
    m_width = width;
    m_height = height;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

    glGenRenderbuffers(1, &m_depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        destroy();
        return false;
    }
    return true;
}

void OpenGLRenderTarget::bind()
{
    if (m_fbo != 0)
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
}

void OpenGLRenderTarget::unbind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLRenderTarget::destroy()
{
    if (m_colorTex)   { glDeleteTextures(1, &m_colorTex);       m_colorTex = 0; }
    if (m_depthRbo)   { glDeleteRenderbuffers(1, &m_depthRbo);  m_depthRbo = 0; }
    if (m_fbo)        { glDeleteFramebuffers(1, &m_fbo);        m_fbo = 0; }
    if (m_snapshotTex){ glDeleteTextures(1, &m_snapshotTex);    m_snapshotTex = 0; }
    m_width = 0;
    m_height = 0;
    m_hasSnapshot = false;
}

bool OpenGLRenderTarget::isValid() const
{
    return m_fbo != 0;
}

void OpenGLRenderTarget::takeSnapshot()
{
    if (m_colorTex == 0 || m_width <= 0 || m_height <= 0)
        return;

    if (m_snapshotTex == 0)
    {
        glGenTextures(1, &m_snapshotTex);
        glBindTexture(GL_TEXTURE_2D, m_snapshotTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, m_snapshotTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    GLuint srcFbo = 0, dstFbo = 0;
    glGenFramebuffers(1, &srcFbo);
    glGenFramebuffers(1, &dstFbo);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_snapshotTex, 0);

    glBlitFramebuffer(0, 0, m_width, m_height,
        0, 0, m_width, m_height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &srcFbo);
    glDeleteFramebuffers(1, &dstFbo);

    m_hasSnapshot = true;
}
