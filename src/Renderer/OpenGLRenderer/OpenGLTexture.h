#pragma once

#include <cstdint>

#include "../Core/Texture.h"

class Texture;

class OpenGLTexture
{
public:
    OpenGLTexture() = default;
    explicit OpenGLTexture(const Texture& texture);

    OpenGLTexture(const OpenGLTexture&) = delete;
    OpenGLTexture& operator=(const OpenGLTexture&) = delete;

    OpenGLTexture(OpenGLTexture&& other) noexcept;
    OpenGLTexture& operator=(OpenGLTexture&& other) noexcept;

    ~OpenGLTexture();

    bool initialize(const Texture& texture);
    void shutdown();

    void bind(uint32_t unit = 0) const;
    void unbind() const;

    uint32_t getHandle() const { return m_textureId; }

private:
    uint32_t m_textureId{ 0 };
    int m_width{ 0 };
    int m_height{ 0 };
    int m_channels{ 0 };
};
