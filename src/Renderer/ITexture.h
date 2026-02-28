#pragma once

#include <cstdint>

class Texture;

class ITexture
{
public:
    virtual ~ITexture() = default;

    virtual bool initialize(const Texture& texture) = 0;
    virtual void shutdown() = 0;
    virtual void bind(uint32_t unit = 0) const = 0;
    virtual void unbind() const = 0;
    virtual uint32_t getHandle() const = 0;
};
