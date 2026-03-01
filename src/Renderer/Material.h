#pragma once

#include <memory>
#include <string>
#include <vector>

#include "EngineObject.h"

class Texture;

// CPU-side material data asset. Rendering backends should derive from this and override build/bind/render.
class Material : public EngineObject
{
public:
    Material() = default;
    ~Material() override = default;

    // Non-abstract by design: AssetManager can instantiate and serialize this.
    virtual bool build() { return true; }
    virtual void bind() { }
    virtual void unbind() { }
    virtual void render() { }

    void setTextures(const std::vector<std::shared_ptr<Texture>>& textures) { m_textures = textures; }
    const std::vector<std::shared_ptr<Texture>>& getTextures() const { return m_textures; }

    void setTextureAssetPaths(std::vector<std::string> paths) { m_textureAssetPaths = std::move(paths); }
    const std::vector<std::string>& getTextureAssetPaths() const { return m_textureAssetPaths; }

    void setShininess(float shininess) { m_shininess = shininess; }
    float getShininess() const { return m_shininess; }

private:
protected:
    std::vector<std::string> m_textureAssetPaths;
    std::vector<std::shared_ptr<Texture>> m_textures;
    float m_shininess{32.0f};
};
