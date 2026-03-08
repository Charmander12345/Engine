#pragma once

#include <memory>
#include <glm/glm.hpp>
#include "glad/include/gl.h"

#include "../../Core/EngineObject.h"
#include "../IRenderObject3D.h"
#include "OpenGLMaterial.h"

class AssetData;
class Texture;

class OpenGLObject3D : public EngineObject, public IRenderObject3D
{
public:
    explicit OpenGLObject3D(const std::shared_ptr<AssetData>& asset);

    bool prepare() override;
    void setMaterialCacheKeySuffix(const std::string& suffix) override { m_materialCacheKeySuffix = suffix; }
    void setFragmentShaderOverride(const std::string& name) override { m_fragmentShaderOverride = name; }
    void setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
    void setLightData(const glm::vec3& position, const glm::vec3& color, float intensity);
    void setLights(const std::vector<OpenGLMaterial::LightData>& lights);
    void setShadowData(GLuint shadowMapArray, const glm::mat4* matrices, const int* lightIndices, int count);
    void setPointShadowData(GLuint cubeArray, const glm::vec3* positions, const float* farPlanes, const int* lightIndices, int count);
    void setFogData(bool enabled, const glm::vec3& color, float density);
    void setCsmData(GLuint texArray, const glm::mat4* matrices, const float* splits,
                    int lightIndex, bool enabled, const glm::mat4& viewMatrix);
    void setPbrData(bool enabled, float metallic, float roughness);
    void setDebugMode(int mode);
    void setDebugColor(const glm::vec3& color);
    void setNearFarPlanes(float nearPlane, float farPlane);
    void render() override;
    void renderBatchContinuation();
    void setTextures(const std::vector<std::shared_ptr<Texture>>& textures) override;
    void setShininess(float shininess) override;
    bool hasLocalBounds() const override;
    Vec3 getLocalBoundsMin() const override;
    Vec3 getLocalBoundsMax() const override;
    int  getVertexCount() const override;
    int  getIndexCount() const override;

    // GLM-typed accessors for internal OpenGL backend use
    const glm::vec3& localBoundsMinGLM() const { return m_localBoundsMin; }
    const glm::vec3& localBoundsMaxGLM() const { return m_localBoundsMax; }
    GLuint getProgram() const;
    GLuint getVao() const;
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
