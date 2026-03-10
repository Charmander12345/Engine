#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include "../Material.h"
#include "OpenGLShader.h"
#include "ShaderVariantKey.h"
#include "glad/include/gl.h"
#include <glm/glm.hpp>

class Texture;
class OpenGLTexture;

class OpenGLMaterial : public Material
{
public:
    static constexpr int kMaxLights = 8;

    struct LightData
    {
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity{1.0f};
        float range{10.0f};
        float spotCutoff{0.0f};
        float spotOuterCutoff{0.0f};
        int type{0}; // 0=point, 1=directional, 2=spot
    };

    struct LayoutElement
    {
        GLuint index;
        GLint size;
        GLenum type;
        GLboolean normalized;
        GLsizei stride;
        size_t offset;
    };

    OpenGLMaterial() = default;
    ~OpenGLMaterial() override;

    void addShader(const std::shared_ptr<OpenGLShader>& shader);

    // Interleaved unique vertex buffer (positions/colors/uvs) and optional indices
    void setVertexData(const std::vector<float>& data) { m_vertexData = data; }
    void setIndexData(const std::vector<uint32_t>& indices) { m_indexData = indices; }

    void setLayout(const std::vector<LayoutElement>& layout) { m_layout = layout; }

    bool build() override;

    /// Set a shader variant key. If different from the current key, the
    /// fragment shader is recompiled with preprocessor defines that
    /// eliminate runtime branching for unused texture/feature paths.
    void setVariantKey(ShaderVariantKey key);
    ShaderVariantKey getVariantKey() const { return m_variantKey; }

    void setFragmentShaderPath(const std::string& path) { m_fragmentShaderPath = path; }

    void bind() override;
    void unbind() override;
    void render() override;
    void renderBatchContinuation();
    void renderInstanced(int instanceCount);

    GLuint getProgram() const { return m_program; }
    GLuint getVao() const { return m_vao; }
    GLsizei getVertexCount() const { return m_vertexCount; }
    GLsizei getIndexCount() const { return m_indexCount; }

    // Matrix uniform setters
    void setModelMatrix(const glm::mat4& matrix) { m_modelMatrix = matrix; }
    void setViewMatrix(const glm::mat4& matrix) { m_viewMatrix = matrix; }
    void setProjectionMatrix(const glm::mat4& matrix) { m_projectionMatrix = matrix; }
    void setLightData(const glm::vec3& position, const glm::vec3& color, float intensity)
    {
        m_lightPosition = position;
        m_lightColor = color;
        m_lightIntensity = intensity;
    }
    void setLights(const std::vector<LightData>& lights) { m_lights = lights; }
    void setShininess(float shininess) { m_shininess = shininess; }

    // Shadow mapping (multi-light)
    static constexpr int kMaxShadowLights = 4;
    void setShadowData(GLuint texArray, const glm::mat4* matrices, const int* lightIndices, int count);

    // Point light shadow mapping (cube maps)
    static constexpr int kMaxPointShadowLights = 4;
    void setPointShadowData(GLuint cubeArray, const glm::vec3* positions, const float* farPlanes, const int* lightIndices, int count);

    // Fog
    void setFogData(bool enabled, const glm::vec3& color, float density);

    // Cascaded Shadow Maps (directional light)
    static constexpr int kMaxCsmCascades = 4;
    void setCsmData(GLuint texArray, const glm::mat4* matrices, const float* splits,
                    int lightIndex, bool enabled, const glm::mat4& viewMatrix);

    // PBR (Metallic/Roughness)
    void setPbrData(bool enabled, float metallic, float roughness);

    // Debug render mode
    void setDebugMode(int mode) { m_debugMode = mode; }
    void setDebugColor(const glm::vec3& color) { m_debugColor = color; }
    void setNearFarPlanes(float nearPlane, float farPlane) { m_nearPlane = nearPlane; m_farPlane = farPlane; }

    // OIT (Order-Independent Transparency)
    void setOitEnabled(bool enabled) { m_oitEnabled = enabled; }

    // Material-Instance Overrides (per-entity)
    void setColorTint(const glm::vec3& tint) { m_colorTint = tint; }
    void setOverrideMetallic(float v) { m_pbrMetallic = v; }
    void setOverrideRoughness(float v) { m_pbrRoughness = v; }
    void setOverrideShininess(float v) { m_shininess = v; }

    // Skeletal Animation
    void setSkinned(bool skinned) { m_skinned = skinned; }
    void setBoneMatrices(const float* data, int count);

    // Texture Streaming
    void setTextureStreamingManager(class TextureStreamingManager* mgr) { m_textureStreamingMgr = mgr; }

private:
    void bindTextures();
    void cacheUniformLocations();

    class TextureStreamingManager* m_textureStreamingMgr{ nullptr };

    std::vector<std::shared_ptr<OpenGLShader>> m_shaders;
    std::vector<float> m_vertexData;
    std::vector<uint32_t> m_indexData;
    std::vector<LayoutElement> m_layout;
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_ebo{0};
    GLsizei m_vertexCount{0};
    GLsizei m_indexCount{0};

    // Shader variant
    ShaderVariantKey m_variantKey{SVF_NONE};
    std::string m_fragmentShaderPath;  ///< Stored for variant recompilation

    // Cache GPU textures per CPU-Texture pointer
    std::unordered_map<const Texture*, std::shared_ptr<OpenGLTexture>> m_textureCache;

    // Transformation matrices
    glm::mat4 m_modelMatrix{1.0f};
    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projectionMatrix{1.0f};
    glm::vec3 m_lightPosition{0.0f, 1.0f, 0.0f};
    glm::vec3 m_lightColor{1.0f, 1.0f, 1.0f};
    float m_lightIntensity{1.0f};
    float m_shininess{32.0f};
    std::vector<LightData> m_lights;

    // Shadow mapping (multi-light)
    GLuint m_shadowMapArray{0};
    int m_shadowCount{0};
    glm::mat4 m_shadowMatrices[kMaxShadowLights]{};
    int m_shadowLightIndices[kMaxShadowLights]{};

    // Cached uniform locations (queried once at build time)
    GLint m_locModel{-1};
    GLint m_locView{-1};
    GLint m_locProjection{-1};
    GLint m_locLightPos{-1};
    GLint m_locLightColor{-1};
    GLint m_locLightIntensity{-1};
    GLint m_locViewPos{-1};
    GLint m_locMaterialShininess{-1};
    GLint m_locHasSpecularMap{-1};
    GLint m_locHasDiffuseMap{-1};
    GLint m_locHasNormalMap{-1};
    GLint m_locHasEmissiveMap{-1};
    GLint m_locLightCount{-1};
    struct LightUniformLocs
    {
        GLint position{-1};
        GLint direction{-1};
        GLint color{-1};
        GLint intensity{-1};
        GLint range{-1};
        GLint spotCutoff{-1};
        GLint spotOuterCutoff{-1};
        GLint type{-1};
    };
    LightUniformLocs m_lightLocs[kMaxLights]{};
    std::vector<GLint> m_texUniformLocs;

    // Point light shadow data
    GLuint m_pointShadowCubeArray{0};
    int m_pointShadowCount{0};
    glm::vec3 m_pointShadowPositions[kMaxPointShadowLights]{};
    float m_pointShadowFarPlanes[kMaxPointShadowLights]{};
    int m_pointShadowLightIndices[kMaxPointShadowLights]{};

    // Shadow uniform locations
    GLint m_locShadowMaps{-1};
    GLint m_locShadowCount{-1};
    struct ShadowUniformLocs
    {
        GLint lightSpaceMatrix{-1};
        GLint lightIndex{-1};
    };
    ShadowUniformLocs m_shadowLocs[kMaxShadowLights]{};

    // Point shadow uniform locations
    GLint m_locPointShadowMaps{-1};
    GLint m_locPointShadowCount{-1};
    struct PointShadowUniformLocs
    {
        GLint position{-1};
        GLint farPlane{-1};
        GLint lightIndex{-1};
    };
    PointShadowUniformLocs m_pointShadowLocs[kMaxPointShadowLights]{};

    // Fog data
    bool m_fogEnabled{false};
    glm::vec3 m_fogColor{0.7f, 0.7f, 0.8f};
    float m_fogDensity{0.02f};
    GLint m_locFogEnabled{-1};
    GLint m_locFogColor{-1};
    GLint m_locFogDensity{-1};

    // CSM data
    GLuint m_csmMapArray{0};
    bool m_csmEnabled{false};
    int m_csmLightIndex{-1};
    glm::mat4 m_csmMatrices[kMaxCsmCascades]{};
    float m_csmSplits[kMaxCsmCascades]{};
    glm::mat4 m_csmViewMatrix{1.0f};
    GLint m_locCsmMaps{-1};
    GLint m_locCsmEnabled{-1};
    GLint m_locCsmLightIndex{-1};
    GLint m_locCsmMatrices[kMaxCsmCascades]{-1, -1, -1, -1};
    GLint m_locCsmSplits[kMaxCsmCascades]{-1, -1, -1, -1};
    GLint m_locViewMatrix{-1};

    // PBR data
    bool  m_pbrEnabled{false};
    float m_pbrMetallic{0.0f};
    float m_pbrRoughness{0.5f};
    GLint m_locPbrEnabled{-1};
    GLint m_locMetallic{-1};
    GLint m_locRoughness{-1};
    GLint m_locHasMetallicRoughnessMap{-1};

    // Instancing
    GLint m_locInstanced{-1};

    // Debug render mode
    int       m_debugMode{0};
    glm::vec3 m_debugColor{1.0f};
    float     m_nearPlane{0.1f};
    float     m_farPlane{1000.0f};
    GLint m_locDebugMode{-1};
    GLint m_locDebugColor{-1};
    GLint m_locNearPlane{-1};
    GLint m_locFarPlane{-1};

    // OIT
    bool  m_oitEnabled{false};
    GLint m_locOitEnabled{-1};

    // Material-Instance color tint
    glm::vec3 m_colorTint{1.0f, 1.0f, 1.0f};
    GLint m_locColorTint{-1};

    // Skeletal Animation
    static constexpr int kMaxBones = 128;
    bool  m_skinned{false};
    int   m_boneCount{0};
    float m_boneMatrixData[kMaxBones * 16]{}; // row-major float[16] per bone
    GLint m_locSkinned{-1};
    GLint m_locBoneMatrices{-1};
};
