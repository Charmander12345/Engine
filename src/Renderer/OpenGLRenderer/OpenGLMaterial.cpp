#include "OpenGLMaterial.h"
#include "TextureStreamingManager.h"
#include "Logger.h"
#include <vector>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

#include "OpenGLTexture.h"
#include "../Texture.h"

OpenGLMaterial::~OpenGLMaterial()
{
    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_ebo)
    {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
}

void OpenGLMaterial::addShader(const std::shared_ptr<OpenGLShader>& shader)
{
    m_shaders.push_back(shader);
}

void OpenGLMaterial::setShadowData(GLuint texArray, const glm::mat4* matrices, const int* lightIndices, int count)
{
    m_shadowMapArray = texArray;
    m_shadowCount = std::min(count, kMaxShadowLights);
    for (int i = 0; i < m_shadowCount; ++i)
    {
        m_shadowMatrices[i] = matrices[i];
        m_shadowLightIndices[i] = lightIndices[i];
    }
}

void OpenGLMaterial::setPointShadowData(GLuint cubeArray, const glm::vec3* positions, const float* farPlanes, const int* lightIndices, int count)
{
    m_pointShadowCubeArray = cubeArray;
    m_pointShadowCount = std::min(count, kMaxPointShadowLights);
    for (int i = 0; i < m_pointShadowCount; ++i)
    {
        m_pointShadowPositions[i] = positions[i];
        m_pointShadowFarPlanes[i] = farPlanes[i];
        m_pointShadowLightIndices[i] = lightIndices[i];
    }
}

void OpenGLMaterial::setFogData(bool enabled, const glm::vec3& color, float density)
{
    m_fogEnabled = enabled;
    m_fogColor = color;
    m_fogDensity = density;
}

void OpenGLMaterial::setCsmData(GLuint texArray, const glm::mat4* matrices, const float* splits,
                                int lightIndex, bool enabled, const glm::mat4& viewMatrix)
{
    m_csmMapArray = texArray;
    m_csmEnabled = enabled;
    m_csmLightIndex = lightIndex;
    m_csmViewMatrix = viewMatrix;
    for (int i = 0; i < kMaxCsmCascades; ++i)
    {
        m_csmMatrices[i] = matrices[i];
        m_csmSplits[i] = splits[i];
    }
}

void OpenGLMaterial::setPbrData(bool enabled, float metallic, float roughness)
{
    m_pbrEnabled = enabled;
    m_pbrMetallic = metallic;
    m_pbrRoughness = roughness;
}

void OpenGLMaterial::setVariantKey(ShaderVariantKey key)
{
    if (key == m_variantKey || m_fragmentShaderPath.empty())
        return;
    m_variantKey = key;

    // Find the fragment shader in m_shaders and recompile with defines
    const std::string defines = buildVariantDefines(key);
    for (auto& s : m_shaders)
    {
        if (s && s->type() == Shader::Type::Fragment)
        {
            auto newFrag = std::make_shared<OpenGLShader>();
            if (newFrag->loadFromFileWithDefines(Shader::Type::Fragment, m_fragmentShaderPath, defines))
            {
                s = newFrag;
            }
            break;
        }
    }
    // Relink program (keep VAO/VBO)
    if (m_program)
    {
        GLuint newProg = glCreateProgram();
        for (auto& s : m_shaders)
        {
            if (s && s->id() != 0)
                glAttachShader(newProg, s->id());
        }
        glLinkProgram(newProg);
        GLint linked = 0;
        glGetProgramiv(newProg, GL_LINK_STATUS, &linked);
        if (linked)
        {
            for (auto& s : m_shaders)
            {
                if (s && s->id() != 0)
                    glDetachShader(newProg, s->id());
            }
            glDeleteProgram(m_program);
            m_program = newProg;
            // Re-cache all uniform locations
            cacheUniformLocations();
        }
        else
        {
            glDeleteProgram(newProg);
        }
    }
}

bool OpenGLMaterial::build()
{
    if (m_shaders.empty())
    {
        Logger::Instance().log("OpenGLMaterial: keine Shader zum Bauen vorhanden", Logger::LogLevel::ERROR);
        return false;
    }
    if (m_vertexData.empty() || m_layout.empty())
    {
        Logger::Instance().log("OpenGLMaterial: keine Vertexdaten oder Layout definiert", Logger::LogLevel::ERROR);
        return false;
    }

    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_ebo)
    {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }

    m_program = glCreateProgram();
    for (auto& s : m_shaders)
    {
        if (!s || s->id() == 0)
        {
            Logger::Instance().log("OpenGLMaterial: Shader nicht kompiliert", Logger::LogLevel::ERROR);
            return false;
        }
        glAttachShader(m_program, s->id());
    }

    glLinkProgram(m_program);

    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);

    GLint logLen = 0;
    glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1)
    {
        std::vector<char> log(logLen);
        glGetProgramInfoLog(m_program, logLen, nullptr, log.data());
        Logger::Instance().log(std::string("Program info log: ") + log.data(), linked ? Logger::LogLevel::WARNING : Logger::LogLevel::ERROR);
    }

    if (!linked)
    {
        Logger::Instance().log("OpenGLMaterial: Programmlink fehlgeschlagen", Logger::LogLevel::ERROR);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    for (auto& s : m_shaders)
    {
        if (s && s->id() != 0)
            glDetachShader(m_program, s->id());
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_vertexData.size() * sizeof(float), m_vertexData.data(), GL_STATIC_DRAW);

    for (const auto& elem : m_layout)
    {
        glEnableVertexAttribArray(elem.index);
        glVertexAttribPointer(elem.index, elem.size, elem.type, elem.normalized, elem.stride, reinterpret_cast<void*>(elem.offset));
    }

    // Optional indexed drawing
    m_indexCount = 0;
    if (!m_indexData.empty())
    {
        glGenBuffers(1, &m_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indexData.size() * sizeof(uint32_t), m_indexData.data(), GL_STATIC_DRAW);
        m_indexCount = static_cast<GLsizei>(m_indexData.size());
    }

    // Note: do not unbind GL_ELEMENT_ARRAY_BUFFER while VAO is bound.
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // compute vertex count from stride and data size
    GLsizei stride = m_layout[0].stride;
    if (stride == 0)
    {
        Logger::Instance().log("OpenGLMaterial: Layout stride ist 0", Logger::LogLevel::ERROR);
        return false;
    }
    m_vertexCount = static_cast<GLsizei>((m_vertexData.size() * sizeof(float)) / stride);

    cacheUniformLocations();

    // Auto-detect transparency from diffuse texture alpha channel
    if (!m_textures.empty() && m_textures[0] && m_textures[0]->getChannels() == 4)
    {
        m_transparent = true;
    }

    // Cache texture sampler locations.
    // Try material struct names first (material.diffuse, material.specular),
    // then fall back to textureN for backward compatibility.
    m_texUniformLocs.clear();
    static const char* kMaterialSamplerNames[] = { "material.diffuse", "material.specular", "material.normalMap", "material.emissiveMap", "material.metallicRoughness" };
    static constexpr int kMaterialSamplerCount = 5;
    for (int i = 0; i < 16; ++i)
    {
        GLint loc = -1;
        if (i < kMaterialSamplerCount)
        {
            loc = glGetUniformLocation(m_program, kMaterialSamplerNames[i]);
        }
        if (loc < 0)
        {
            const std::string name = "texture" + std::to_string(i);
            loc = glGetUniformLocation(m_program, name.c_str());
        }
        if (loc < 0)
        {
            break;
        }
        m_texUniformLocs.push_back(loc);
    }

    return true;
}

void OpenGLMaterial::cacheUniformLocations()
{
    m_locModel = glGetUniformLocation(m_program, "uModel");
    m_locView = glGetUniformLocation(m_program, "uView");
    m_locProjection = glGetUniformLocation(m_program, "uProjection");
    m_locLightPos = glGetUniformLocation(m_program, "uLightPos");
    m_locLightColor = glGetUniformLocation(m_program, "uLightColor");
    m_locLightIntensity = glGetUniformLocation(m_program, "uLightIntensity");
    m_locViewPos = glGetUniformLocation(m_program, "uViewPos");
    m_locMaterialShininess = glGetUniformLocation(m_program, "material.shininess");
    m_locHasSpecularMap = glGetUniformLocation(m_program, "uHasSpecularMap");
    m_locHasDiffuseMap = glGetUniformLocation(m_program, "uHasDiffuseMap");
    m_locLightCount = glGetUniformLocation(m_program, "uLightCount");
    m_locShadowMaps = glGetUniformLocation(m_program, "uShadowMaps");
    m_locShadowCount = glGetUniformLocation(m_program, "uShadowCount");
    for (int i = 0; i < kMaxShadowLights; ++i)
    {
        const std::string idx = "[" + std::to_string(i) + "]";
        m_shadowLocs[i].lightSpaceMatrix = glGetUniformLocation(m_program, ("uLightSpaceMatrices" + idx).c_str());
        m_shadowLocs[i].lightIndex       = glGetUniformLocation(m_program, ("uShadowLightIndices" + idx).c_str());
    }
    m_locPointShadowMaps = glGetUniformLocation(m_program, "uPointShadowMaps");
    m_locPointShadowCount = glGetUniformLocation(m_program, "uPointShadowCount");
    for (int i = 0; i < kMaxPointShadowLights; ++i)
    {
        const std::string idx = "[" + std::to_string(i) + "]";
        m_pointShadowLocs[i].position  = glGetUniformLocation(m_program, ("uPointShadowPositions" + idx).c_str());
        m_pointShadowLocs[i].farPlane  = glGetUniformLocation(m_program, ("uPointShadowFarPlanes" + idx).c_str());
        m_pointShadowLocs[i].lightIndex = glGetUniformLocation(m_program, ("uPointShadowLightIndices" + idx).c_str());
    }
    for (int i = 0; i < kMaxLights; ++i)
    {
        const std::string prefix = "uLights[" + std::to_string(i) + "].";
        m_lightLocs[i].position       = glGetUniformLocation(m_program, (prefix + "position").c_str());
        m_lightLocs[i].direction      = glGetUniformLocation(m_program, (prefix + "direction").c_str());
        m_lightLocs[i].color          = glGetUniformLocation(m_program, (prefix + "color").c_str());
        m_lightLocs[i].intensity      = glGetUniformLocation(m_program, (prefix + "intensity").c_str());
        m_lightLocs[i].range          = glGetUniformLocation(m_program, (prefix + "range").c_str());
        m_lightLocs[i].spotCutoff     = glGetUniformLocation(m_program, (prefix + "spotCutoff").c_str());
        m_lightLocs[i].spotOuterCutoff = glGetUniformLocation(m_program, (prefix + "spotOuterCutoff").c_str());
        m_lightLocs[i].type           = glGetUniformLocation(m_program, (prefix + "type").c_str());
    }
    m_locFogEnabled = glGetUniformLocation(m_program, "uFogEnabled");
    m_locFogColor   = glGetUniformLocation(m_program, "uFogColor");
    m_locFogDensity = glGetUniformLocation(m_program, "uFogDensity");
    m_locHasNormalMap = glGetUniformLocation(m_program, "uHasNormalMap");
    m_locHasEmissiveMap = glGetUniformLocation(m_program, "uHasEmissiveMap");
    m_locCsmMaps       = glGetUniformLocation(m_program, "uCsmMaps");
    m_locCsmEnabled    = glGetUniformLocation(m_program, "uCsmEnabled");
    m_locCsmLightIndex = glGetUniformLocation(m_program, "uCsmLightIndex");
    m_locViewMatrix    = glGetUniformLocation(m_program, "uViewMatrix");
    for (int i = 0; i < kMaxCsmCascades; ++i)
    {
        const std::string idx = "[" + std::to_string(i) + "]";
        m_locCsmMatrices[i] = glGetUniformLocation(m_program, ("uCsmMatrices" + idx).c_str());
        m_locCsmSplits[i]   = glGetUniformLocation(m_program, ("uCsmSplits" + idx).c_str());
    }
    m_locPbrEnabled              = glGetUniformLocation(m_program, "uPbrEnabled");
    m_locMetallic                = glGetUniformLocation(m_program, "uMetallic");
    m_locRoughness               = glGetUniformLocation(m_program, "uRoughness");
    m_locSpecularMultiplier      = glGetUniformLocation(m_program, "uSpecularMultiplier");
    m_locHasMetallicRoughnessMap = glGetUniformLocation(m_program, "uHasMetallicRoughnessMap");
    m_locInstanced = glGetUniformLocation(m_program, "uInstanced");
    m_locDebugMode  = glGetUniformLocation(m_program, "uDebugMode");
    m_locDebugColor = glGetUniformLocation(m_program, "uDebugColor");
    m_locNearPlane  = glGetUniformLocation(m_program, "uNearPlane");
    m_locFarPlane   = glGetUniformLocation(m_program, "uFarPlane");
    m_locOitEnabled = glGetUniformLocation(m_program, "uOitEnabled");
    m_locSkinned = glGetUniformLocation(m_program, "uSkinned");
    m_locBoneMatrices = glGetUniformLocation(m_program, "uBoneMatrices[0]");
    m_locColorTint = glGetUniformLocation(m_program, "uColorTint");
    m_locDisplacementMap   = glGetUniformLocation(m_program, "uDisplacementMap");
    m_locDisplacementScale = glGetUniformLocation(m_program, "uDisplacementScale");
    m_locTessLevel         = glGetUniformLocation(m_program, "uTessLevel");
}

void OpenGLMaterial::bindTextures()
{
    if (m_textures.empty())
    {
        return;
    }

    uint32_t unit = 0;
    for (const auto& texCpu : m_textures)
    {
        if (!texCpu)
        {
            ++unit;
            continue;
        }

        const Texture* key = texCpu.get();
        auto it = m_textureCache.find(key);
        if (it == m_textureCache.end())
        {
            // When a streaming manager is available, use it to get a GPU
            // texture that may initially be a placeholder.
            if (m_textureStreamingMgr && m_textureStreamingMgr->isInitialized())
            {
                auto streamed = m_textureStreamingMgr->streamTexture(texCpu);
                if (streamed)
                {
                    it = m_textureCache.emplace(key, std::move(streamed)).first;
                }
                else
                {
                    ++unit;
                    continue;
                }
            }
            else
            {
                auto glTex = std::make_shared<OpenGLTexture>();
                if (!glTex->initialize(*texCpu))
                {
                    Logger::Instance().log("OpenGLMaterial: Failed to initialize OpenGL texture from Texture asset.", Logger::LogLevel::ERROR);
                    ++unit;
                    continue;
                }
                it = m_textureCache.emplace(key, std::move(glTex)).first;
            }
        }

        it->second->bind(unit);

        if (unit < static_cast<uint32_t>(m_texUniformLocs.size()) && m_texUniformLocs[unit] >= 0)
        {
            glUniform1i(m_texUniformLocs[unit], static_cast<GLint>(unit));
        }

        ++unit;
    }

    glActiveTexture(GL_TEXTURE0);
}

void OpenGLMaterial::bind()
{
    glUseProgram(m_program);
    glBindVertexArray(m_vao);

    if (m_locInstanced >= 0)
    {
        glUniform1i(m_locInstanced, 0);
    }

    if (m_locModel >= 0)
    {
        glUniformMatrix4fv(m_locModel, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
    }
    if (m_locView >= 0)
    {
        glUniformMatrix4fv(m_locView, 1, GL_FALSE, glm::value_ptr(m_viewMatrix));
    }
    if (m_locProjection >= 0)
    {
        glUniformMatrix4fv(m_locProjection, 1, GL_FALSE, glm::value_ptr(m_projectionMatrix));
    }
    if (m_locLightPos >= 0)
    {
        glUniform3fv(m_locLightPos, 1, glm::value_ptr(m_lightPosition));
    }
    if (m_locLightColor >= 0)
    {
        glUniform3fv(m_locLightColor, 1, glm::value_ptr(m_lightColor));
    }
    if (m_locLightIntensity >= 0)
    {
        glUniform1f(m_locLightIntensity, m_lightIntensity);
    }
    if (m_locViewPos >= 0)
    {
        const glm::mat4 invView = glm::inverse(m_viewMatrix);
        glUniform3f(m_locViewPos, invView[3][0], invView[3][1], invView[3][2]);
    }
    if (m_locMaterialShininess >= 0)
    {
        glUniform1f(m_locMaterialShininess, m_shininess);
    }
    if (m_locHasSpecularMap >= 0)
    {
        glUniform1i(m_locHasSpecularMap, (m_textures.size() >= 2 && m_textures[1]) ? 1 : 0);
    }
    if (m_locHasDiffuseMap >= 0)
    {
        glUniform1i(m_locHasDiffuseMap, (!m_textures.empty() && m_textures[0]) ? 1 : 0);
    }
    if (m_locHasNormalMap >= 0)
    {
        glUniform1i(m_locHasNormalMap, (m_textures.size() >= 3 && m_textures[2]) ? 1 : 0);
    }
    if (m_locHasEmissiveMap >= 0)
    {
        glUniform1i(m_locHasEmissiveMap, (m_textures.size() >= 4 && m_textures[3]) ? 1 : 0);
    }

    // Multi-light uniforms
    const int lightCount = static_cast<int>(std::min(m_lights.size(), static_cast<size_t>(kMaxLights)));
    if (m_locLightCount >= 0)
    {
        glUniform1i(m_locLightCount, lightCount);
    }
    for (int i = 0; i < lightCount; ++i)
    {
        const auto& l = m_lights[i];
        const auto& loc = m_lightLocs[i];
        if (loc.position >= 0)       glUniform3fv(loc.position, 1, glm::value_ptr(l.position));
        if (loc.direction >= 0)      glUniform3fv(loc.direction, 1, glm::value_ptr(l.direction));
        if (loc.color >= 0)          glUniform3fv(loc.color, 1, glm::value_ptr(l.color));
        if (loc.intensity >= 0)      glUniform1f(loc.intensity, l.intensity);
        if (loc.range >= 0)          glUniform1f(loc.range, l.range);
        if (loc.spotCutoff >= 0)     glUniform1f(loc.spotCutoff, l.spotCutoff);
        if (loc.spotOuterCutoff >= 0) glUniform1f(loc.spotOuterCutoff, l.spotOuterCutoff);
        if (loc.type >= 0)           glUniform1i(loc.type, l.type);
    }

    // Multi-light shadow uniforms
    if (m_locShadowCount >= 0)
    {
        glUniform1i(m_locShadowCount, m_shadowCount);
    }
    for (int i = 0; i < m_shadowCount; ++i)
    {
        if (m_shadowLocs[i].lightSpaceMatrix >= 0)
            glUniformMatrix4fv(m_shadowLocs[i].lightSpaceMatrix, 1, GL_FALSE, glm::value_ptr(m_shadowMatrices[i]));
        if (m_shadowLocs[i].lightIndex >= 0)
            glUniform1i(m_shadowLocs[i].lightIndex, m_shadowLightIndices[i]);
    }
    if (m_locShadowMaps >= 0 && m_shadowMapArray != 0)
    {
        constexpr int kShadowTexUnit = 5;
        glActiveTexture(GL_TEXTURE0 + kShadowTexUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowMapArray);
        glUniform1i(m_locShadowMaps, kShadowTexUnit);
    }

    // Point shadow uniforms
    if (m_locPointShadowCount >= 0)
    {
        glUniform1i(m_locPointShadowCount, m_pointShadowCount);
    }
    for (int i = 0; i < m_pointShadowCount; ++i)
    {
        if (m_pointShadowLocs[i].position >= 0)
            glUniform3fv(m_pointShadowLocs[i].position, 1, glm::value_ptr(m_pointShadowPositions[i]));
        if (m_pointShadowLocs[i].farPlane >= 0)
            glUniform1f(m_pointShadowLocs[i].farPlane, m_pointShadowFarPlanes[i]);
        if (m_pointShadowLocs[i].lightIndex >= 0)
            glUniform1i(m_pointShadowLocs[i].lightIndex, m_pointShadowLightIndices[i]);
    }
    if (m_locPointShadowMaps >= 0 && m_pointShadowCubeArray != 0)
    {
        constexpr int kPointShadowTexUnit = 6;
        glActiveTexture(GL_TEXTURE0 + kPointShadowTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_pointShadowCubeArray);
        glUniform1i(m_locPointShadowMaps, kPointShadowTexUnit);
    }

    // Fog uniforms
    if (m_locFogEnabled >= 0)
        glUniform1i(m_locFogEnabled, m_fogEnabled ? 1 : 0);
    if (m_locFogColor >= 0)
        glUniform3fv(m_locFogColor, 1, glm::value_ptr(m_fogColor));
    if (m_locFogDensity >= 0)
        glUniform1f(m_locFogDensity, m_fogDensity);

    // CSM uniforms
    if (m_locCsmEnabled >= 0)
        glUniform1i(m_locCsmEnabled, m_csmEnabled ? 1 : 0);
    if (m_locCsmLightIndex >= 0)
        glUniform1i(m_locCsmLightIndex, m_csmLightIndex);
    if (m_locViewMatrix >= 0)
        glUniformMatrix4fv(m_locViewMatrix, 1, GL_FALSE, glm::value_ptr(m_csmViewMatrix));
    for (int i = 0; i < kMaxCsmCascades; ++i)
    {
        if (m_locCsmMatrices[i] >= 0)
            glUniformMatrix4fv(m_locCsmMatrices[i], 1, GL_FALSE, glm::value_ptr(m_csmMatrices[i]));
        if (m_locCsmSplits[i] >= 0)
            glUniform1f(m_locCsmSplits[i], m_csmSplits[i]);
    }
    if (m_locCsmMaps >= 0 && m_csmMapArray != 0)
    {
        constexpr int kCsmTexUnit = 7;
        glActiveTexture(GL_TEXTURE0 + kCsmTexUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_csmMapArray);
        glUniform1i(m_locCsmMaps, kCsmTexUnit);
    }

    // PBR uniforms
    if (m_locPbrEnabled >= 0)
        glUniform1i(m_locPbrEnabled, m_pbrEnabled ? 1 : 0);
    if (m_locMetallic >= 0)
        glUniform1f(m_locMetallic, m_pbrMetallic);
    if (m_locRoughness >= 0)
        glUniform1f(m_locRoughness, m_pbrRoughness);
    if (m_locSpecularMultiplier >= 0)
        glUniform1f(m_locSpecularMultiplier, m_specularMultiplier);
    if (m_locHasMetallicRoughnessMap >= 0)
        glUniform1i(m_locHasMetallicRoughnessMap, (m_textures.size() >= 5 && m_textures[4]) ? 1 : 0);

    // Debug render mode uniforms
    if (m_locDebugMode >= 0)
        glUniform1i(m_locDebugMode, m_debugMode);
    if (m_locDebugColor >= 0)
        glUniform3fv(m_locDebugColor, 1, glm::value_ptr(m_debugColor));
    if (m_locNearPlane >= 0)
        glUniform1f(m_locNearPlane, m_nearPlane);
    if (m_locFarPlane >= 0)
        glUniform1f(m_locFarPlane, m_farPlane);

    // OIT uniform
    if (m_locOitEnabled >= 0)
        glUniform1i(m_locOitEnabled, m_oitEnabled ? 1 : 0);

    // Skeletal animation uniforms
    if (m_locSkinned >= 0)
        glUniform1i(m_locSkinned, m_skinned ? 1 : 0);
    if (m_skinned && m_locBoneMatrices >= 0 && m_boneCount > 0)
        glUniformMatrix4fv(m_locBoneMatrices, m_boneCount, GL_TRUE, m_boneMatrixData);

    // Material-instance color tint
    if (m_locColorTint >= 0)
        glUniform3fv(m_locColorTint, 1, glm::value_ptr(m_colorTint));

    // Displacement mapping (tessellation) uniforms
    if (m_displacementEnabled)
    {
        if (m_locDisplacementScale >= 0)
            glUniform1f(m_locDisplacementScale, m_displacementScale);
        if (m_locTessLevel >= 0)
            glUniform1f(m_locTessLevel, m_tessLevel);
        // Displacement map bound on texture unit 8
        if (m_locDisplacementMap >= 0 && m_displacementTexture != 0)
        {
            constexpr int kDisplacementTexUnit = 8;
            glActiveTexture(GL_TEXTURE0 + kDisplacementTexUnit);
            glBindTexture(GL_TEXTURE_2D, m_displacementTexture);
            glUniform1i(m_locDisplacementMap, kDisplacementTexUnit);
        }
    }

    bindTextures();
}

void OpenGLMaterial::unbind()
{
}

void OpenGLMaterial::render()
{
    bind();
    const GLenum drawMode = m_displacementEnabled ? GL_PATCHES : GL_TRIANGLES;
    if (m_displacementEnabled)
        glPatchParameteri(GL_PATCH_VERTICES, 3);
    if (m_indexCount > 0)
    {
        glDrawElements(drawMode, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
        glDrawArrays(drawMode, 0, m_vertexCount);
    }
    unbind();
}

void OpenGLMaterial::renderBatchContinuation()
{
    glBindVertexArray(m_vao);

    if (m_locModel >= 0)
    {
        glUniformMatrix4fv(m_locModel, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
    }
    if (m_locHasSpecularMap >= 0)
    {
        glUniform1i(m_locHasSpecularMap, (m_textures.size() >= 2 && m_textures[1]) ? 1 : 0);
    }
    if (m_locHasDiffuseMap >= 0)
    {
        glUniform1i(m_locHasDiffuseMap, (!m_textures.empty() && m_textures[0]) ? 1 : 0);
    }
    if (m_locHasNormalMap >= 0)
    {
        glUniform1i(m_locHasNormalMap, (m_textures.size() >= 3 && m_textures[2]) ? 1 : 0);
    }
    if (m_locHasEmissiveMap >= 0)
    {
        glUniform1i(m_locHasEmissiveMap, (m_textures.size() >= 4 && m_textures[3]) ? 1 : 0);
    }
    if (m_locMaterialShininess >= 0)
    {
        glUniform1f(m_locMaterialShininess, m_shininess);
    }
    if (m_locPbrEnabled >= 0)
        glUniform1i(m_locPbrEnabled, m_pbrEnabled ? 1 : 0);
    if (m_locMetallic >= 0)
        glUniform1f(m_locMetallic, m_pbrMetallic);
    if (m_locRoughness >= 0)
        glUniform1f(m_locRoughness, m_pbrRoughness);
    if (m_locHasMetallicRoughnessMap >= 0)
        glUniform1i(m_locHasMetallicRoughnessMap, (m_textures.size() >= 5 && m_textures[4]) ? 1 : 0);

    bindTextures();

    const GLenum drawMode = m_displacementEnabled ? GL_PATCHES : GL_TRIANGLES;
    if (m_displacementEnabled)
        glPatchParameteri(GL_PATCH_VERTICES, 3);
    if (m_indexCount > 0)
    {
        glDrawElements(drawMode, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
        glDrawArrays(drawMode, 0, m_vertexCount);
    }
}

void OpenGLMaterial::renderInstanced(int instanceCount)
{
    bind();
    if (m_locInstanced >= 0)
    {
        glUniform1i(m_locInstanced, 1);
    }
    const GLenum drawMode = m_displacementEnabled ? GL_PATCHES : GL_TRIANGLES;
    if (m_displacementEnabled)
        glPatchParameteri(GL_PATCH_VERTICES, 3);
    if (m_indexCount > 0)
    {
        glDrawElementsInstanced(drawMode, m_indexCount, GL_UNSIGNED_INT, nullptr, instanceCount);
    }
    else
    {
        glDrawArraysInstanced(drawMode, 0, m_vertexCount, instanceCount);
    }
    // Reset instanced state so subsequent non-instanced draws cannot
    // accidentally read stale SSBO data through the same binding point.
    if (m_locInstanced >= 0)
    {
        glUniform1i(m_locInstanced, 0);
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    unbind();
}

void OpenGLMaterial::setBoneMatrices(const float* data, int count)
{
    m_boneCount = std::min(count, kMaxBones);
    if (m_boneCount > 0 && data)
    {
        std::memcpy(m_boneMatrixData, data, m_boneCount * 16 * sizeof(float));
    }
}
