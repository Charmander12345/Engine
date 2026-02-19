#include "OpenGLMaterial.h"
#include "Logger.h"
#include <vector>
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

    // Cache uniform locations once
    m_locModel = glGetUniformLocation(m_program, "uModel");
    m_locView = glGetUniformLocation(m_program, "uView");
    m_locProjection = glGetUniformLocation(m_program, "uProjection");
    m_locLightPos = glGetUniformLocation(m_program, "uLightPos");
    m_locLightColor = glGetUniformLocation(m_program, "uLightColor");
    m_locLightIntensity = glGetUniformLocation(m_program, "uLightIntensity");
    m_locViewPos = glGetUniformLocation(m_program, "uViewPos");
    m_locMaterialShininess = glGetUniformLocation(m_program, "material.shininess");
    m_locHasSpecularMap = glGetUniformLocation(m_program, "uHasSpecularMap");
    m_locLightCount = glGetUniformLocation(m_program, "uLightCount");
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

    // Cache texture sampler locations.
    // Try material struct names first (material.diffuse, material.specular),
    // then fall back to textureN for backward compatibility.
    m_texUniformLocs.clear();
    static const char* kMaterialSamplerNames[] = { "material.diffuse", "material.specular" };
    static constexpr int kMaterialSamplerCount = 2;
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
            auto glTex = std::make_shared<OpenGLTexture>();
            if (!glTex->initialize(*texCpu))
            {
                Logger::Instance().log("OpenGLMaterial: Failed to initialize OpenGL texture from Texture asset.", Logger::LogLevel::ERROR);
                ++unit;
                continue;
            }
            it = m_textureCache.emplace(key, std::move(glTex)).first;
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

    bindTextures();
}

void OpenGLMaterial::unbind()
{
}

void OpenGLMaterial::render()
{
    bind();
    if (m_indexCount > 0)
    {
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
        glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
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
    if (m_locMaterialShininess >= 0)
    {
        glUniform1f(m_locMaterialShininess, m_shininess);
    }

    bindTextures();

    if (m_indexCount > 0)
    {
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    }
    else
    {
        glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    }
}
