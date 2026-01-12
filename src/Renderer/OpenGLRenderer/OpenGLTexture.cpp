#include "OpenGLTexture.h"

#include <utility>

#include "gl.h"

#include "Texture.h"

OpenGLTexture::OpenGLTexture(const Texture& texture)
{
    initialize(texture);
}

OpenGLTexture::OpenGLTexture(OpenGLTexture&& other) noexcept
{
    *this = std::move(other);
}

OpenGLTexture& OpenGLTexture::operator=(OpenGLTexture&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    shutdown();

    m_textureId = other.m_textureId;
    m_width = other.m_width;
    m_height = other.m_height;
    m_channels = other.m_channels;

    other.m_textureId = 0;
    other.m_width = 0;
    other.m_height = 0;
    other.m_channels = 0;

    return *this;
}

OpenGLTexture::~OpenGLTexture()
{
    shutdown();
}

bool OpenGLTexture::initialize(const Texture& texture)
{
    shutdown();

    m_width = texture.getWidth();
    m_height = texture.getHeight();
    m_channels = texture.getChannels();

    if (m_width <= 0 || m_height <= 0 || m_channels <= 0)
    {
        return false;
    }

    const auto& data = texture.getData();
    if (data.empty())
    {
        return false;
    }

    GLenum internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;

    if (m_channels == 1)
    {
        internalFormat = GL_R8;
        format = GL_RED;
    }
    else if (m_channels == 2)
    {
        internalFormat = GL_RG8;
        format = GL_RG;
    }
    else if (m_channels == 3)
    {
        internalFormat = GL_RGB8;
        format = GL_RGB;
    }

    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        static_cast<GLint>(internalFormat),
        m_width,
        m_height,
        0,
        format,
        GL_UNSIGNED_BYTE,
        data.data());

    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);

    return m_textureId != 0;
}

void OpenGLTexture::shutdown()
{
    if (m_textureId != 0)
    {
        GLuint id = m_textureId;
        glDeleteTextures(1, &id);
        m_textureId = 0;
    }

    m_width = 0;
    m_height = 0;
    m_channels = 0;
}

void OpenGLTexture::bind(uint32_t unit) const
{
    if (m_textureId == 0)
    {
        return;
    }

    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}

void OpenGLTexture::unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}
