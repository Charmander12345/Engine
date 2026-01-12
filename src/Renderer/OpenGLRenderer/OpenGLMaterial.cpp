#include "OpenGLMaterial.h"
#include "Logger.h"
#include <vector>

#include "OpenGLTexture.h"
#include "Texture.h"

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

    return true;
}

void OpenGLMaterial::bindTextures()
{
    if (m_textures.empty())
    {
        return;
    }

    // Must have the program bound before calling glUniform*
    glUseProgram(m_program);

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

        // Bind texture to unit first
        it->second->bind(unit);

        const std::string uniformName = "texture" + std::to_string(unit);
        GLint loc = glGetUniformLocation(m_program, uniformName.c_str());
        if (loc >= 0)
        {
            glUniform1i(loc, static_cast<GLint>(unit));
        }

        ++unit;
    }

    // Ensure we return to texture unit 0
    glActiveTexture(GL_TEXTURE0);
}

void OpenGLMaterial::bind()
{
    glUseProgram(m_program);
    glBindVertexArray(m_vao);

    bindTextures();
}

void OpenGLMaterial::unbind()
{
    // Unbind textures (by unit)
    uint32_t unit = 0;
    for (const auto& texCpu : m_textures)
    {
        (void)texCpu;
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, 0);
        ++unit;
    }
    glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(0);
    glUseProgram(0);
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
