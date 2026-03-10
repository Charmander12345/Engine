#include "OpenGLTexture.h"

#include <utility>

#include "gl.h"

// S3TC compressed texture format constants (GL_EXT_texture_compression_s3tc)
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

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

    if (m_width <= 0 || m_height <= 0)
    {
        return false;
    }

    // --- Compressed texture path (DDS / BCn) ---
    if (texture.isCompressed())
    {
        const auto& mips = texture.getCompressedMips();
        if (mips.empty())
        {
            return false;
        }

        GLenum internalFormat = 0;
        switch (texture.getCompressedFormat())
        {
        case CompressedFormat::BC1:  internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;   break;
        case CompressedFormat::BC1A: internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;  break;
        case CompressedFormat::BC2:  internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;  break;
        case CompressedFormat::BC3:  internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;  break;
        case CompressedFormat::BC4:  internalFormat = GL_COMPRESSED_RED_RGTC1;           break;
        case CompressedFormat::BC5:  internalFormat = GL_COMPRESSED_RG_RGTC2;            break;
        case CompressedFormat::BC7:  internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;     break;
        default: return false;
        }

        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
            mips.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        if (mips.size() > 1)
        {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(mips.size() - 1));
        }

        for (size_t i = 0; i < mips.size(); ++i)
        {
            const auto& mip = mips[i];
            glCompressedTexImage2D(
                GL_TEXTURE_2D,
                static_cast<GLint>(i),
                internalFormat,
                mip.width,
                mip.height,
                0,
                static_cast<GLsizei>(mip.data.size()),
                mip.data.data());
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        return m_textureId != 0;
    }

    // --- Uncompressed texture path (stb_image / raw pixel data) ---
    if (m_channels <= 0)
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

    // Driver-side compression: use a compressed internalFormat so the GL
    // driver compresses the uncompressed pixel data during upload.
    if (texture.isCompressionRequested())
    {
        if (m_channels == 1)
            internalFormat = GL_COMPRESSED_RED_RGTC1;        // BC4
        else if (m_channels == 2)
            internalFormat = GL_COMPRESSED_RG_RGTC2;         // BC5
        else if (m_channels == 3)
            internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT; // BC1
        else
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; // BC3
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
