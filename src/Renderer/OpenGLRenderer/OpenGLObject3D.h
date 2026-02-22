#pragma once

#include <memory>
#include <glm/glm.hpp>
#include "glad/include/gl.h"

#include "../../Core/EngineObject.h"
#include "OpenGLMaterial.h"

class AssetData;
class Texture;

class OpenGLObject3D : public EngineObject
{
public:
    explicit OpenGLObject3D(const std::shared_ptr<AssetData>& asset);

    bool prepare();
    void setMaterialCacheKeySuffix(const std::string& suffix) { m_materialCacheKeySuffix = suffix; }
    void setFragmentShaderOverride(const std::string& name) { m_fragmentShaderOverride = name; }
    void setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
    void setLightData(const glm::vec3& position, const glm::vec3& color, float intensity);
    void setLights(const std::vector<OpenGLMaterial::LightData>& lights);
    void setShadowData(GLuint shadowMapArray, const glm::mat4* matrices, const int* lightIndices, int count);
    void setPointShadowData(GLuint cubeArray, const glm::vec3* positions, const float* farPlanes, const int* lightIndices, int count);
    void render();
    void renderBatchContinuation();
    void setTextures(const std::vector<std::shared_ptr<Texture>>& textures);
    void setShininess(float shininess);
    bool hasLocalBounds() const;
    const glm::vec3& getLocalBoundsMin() const;
    const glm::vec3& getLocalBoundsMax() const;
    GLuint getProgram() const;
    GLuint getVao() const;
    GLsizei getVertexCount() const;
    GLsizei getIndexCount() const;
    OpenGLMaterial* getMaterial() const { return m_material.get(); }

    static void ClearCache();

private:
    std::shared_ptr<AssetData> m_asset;
    std::shared_ptr<OpenGLMaterial> m_material;
    std::string m_materialCacheKeySuffix;
    std::string m_fragmentShaderOverride;
    glm::vec3 m_localBoundsMin{0.0f};
    glm::vec3 m_localBoundsMax{0.0f};
    bool m_hasLocalBounds{false};
};
