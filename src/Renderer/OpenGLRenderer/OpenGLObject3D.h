#pragma once

#include <memory>
#include <glm/glm.hpp>

#include "../../Core/EngineObject.h"

class AssetData;
class OpenGLMaterial;
class Texture;

class OpenGLObject3D : public EngineObject
{
public:
    explicit OpenGLObject3D(const std::shared_ptr<AssetData>& asset);

    bool prepare();
    void setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
    void setLightData(const glm::vec3& position, const glm::vec3& color, float intensity);
    void render();
    void setTextures(const std::vector<std::shared_ptr<Texture>>& textures);
    bool hasLocalBounds() const;
    const glm::vec3& getLocalBoundsMin() const;
    const glm::vec3& getLocalBoundsMax() const;

    static void ClearCache();

private:
    std::shared_ptr<AssetData> m_asset;
    std::shared_ptr<OpenGLMaterial> m_material;
    glm::vec3 m_localBoundsMin{0.0f};
    glm::vec3 m_localBoundsMax{0.0f};
    bool m_hasLocalBounds{false};
};
