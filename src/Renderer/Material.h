#pragma once

#include <memory>
#include <vector>

class Texture;

class Material
{
public:
    virtual ~Material() = default;
    virtual bool build() = 0;
    virtual void bind() = 0;
    virtual void unbind() = 0;
    virtual void render() = 0;

    void setTextures(const std::vector<std::shared_ptr<Texture>>& textures) { m_textures = textures; }
    const std::vector<std::shared_ptr<Texture>>& getTextures() const { return m_textures; }

protected:
    std::vector<std::shared_ptr<Texture>> m_textures;
};
