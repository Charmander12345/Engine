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

    void setMetallic(float metallic) { m_metallic = metallic; }
    float getMetallic() const { return m_metallic; }

    void setRoughness(float roughness) { m_roughness = roughness; }
    float getRoughness() const { return m_roughness; }

    void setSpecularMultiplier(float v) { m_specularMultiplier = v; }
    float getSpecularMultiplier() const { return m_specularMultiplier; }

    void setPbrEnabled(bool enabled) { m_pbrEnabled = enabled; }
    bool getPbrEnabled() const { return m_pbrEnabled; }

    void setTransparent(bool transparent) { m_transparent = transparent; }
    bool isTransparent() const { return m_transparent; }

private:
protected:
    std::vector<std::string> m_textureAssetPaths;
    std::vector<std::shared_ptr<Texture>> m_textures;
    float m_shininess{32.0f};
    float m_metallic{0.0f};
    float m_roughness{0.5f};
    float m_specularMultiplier{1.0f};
    bool m_pbrEnabled{false};
    bool m_transparent{false};
};
