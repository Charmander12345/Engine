#include "OpenGLMaterial.h"
#include "Logger.h"
#include <vector>

OpenGLMaterial::~OpenGLMaterial()
{
    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
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

void OpenGLMaterial::bind()
{
    glUseProgram(m_program);
    glBindVertexArray(m_vao);
}

void OpenGLMaterial::unbind()
{
    glBindVertexArray(0);
    glUseProgram(0);
}

void OpenGLMaterial::render()
{
    bind();
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    unbind();
}
