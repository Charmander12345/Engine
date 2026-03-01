#pragma once

#include <cstdint>

#include "../Texture.h"
#include "../ITexture.h"

class Texture;

class OpenGLTexture : public ITexture
{
public:
    OpenGLTexture() = default;
    explicit OpenGLTexture(const Texture& texture);

    OpenGLTexture(const OpenGLTexture&) = delete;
    OpenGLTexture& operator=(const OpenGLTexture&) = delete;

    OpenGLTexture(OpenGLTexture&& other) noexcept;
    OpenGLTexture& operator=(OpenGLTexture&& other) noexcept;

    ~OpenGLTexture() override;

    bool initialize(const Texture& texture) override;
    void shutdown() override;

    void bind(uint32_t unit = 0) const override;
    void unbind() const override;

    uint32_t getHandle() const override { return m_textureId; }

private:
    uint32_t m_textureId{ 0 };
    int m_width{ 0 };
    int m_height{ 0 };
    int m_channels{ 0 };
};
