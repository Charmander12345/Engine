#include "OpenGLShader.h"

#include <vector>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#include "Logger.h"

OpenGLShader::OpenGLShader() = default;

OpenGLShader::~OpenGLShader()
{
    if (m_shader)
    {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
}

bool OpenGLShader::loadFromSource(Shader::Type type, const std::string& source)
{
    m_type = type;
    m_source = source;
    return compile();
}

bool OpenGLShader::loadFromFile(Shader::Type type, const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file)
    {
        Logger::Instance().log("Shader-Datei konnte nicht geöffnet werden: " + filePath, Logger::LogLevel::ERROR);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    m_source = buffer.str();
    m_type = type;
    return compile();
}

static GLenum ToGLType(Shader::Type type)
{
    switch (type)
    {
    case Shader::Type::Vertex: return GL_VERTEX_SHADER;
    case Shader::Type::Fragment: return GL_FRAGMENT_SHADER;
    case Shader::Type::Geometry: return GL_GEOMETRY_SHADER;
    case Shader::Type::Compute: return GL_COMPUTE_SHADER;
    default: return 0;
    }
}

bool OpenGLShader::compile()
{
    if (m_shader)
    {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
    m_compileLog.clear();
    m_compiled = false;

    GLenum glType = ToGLType(m_type);
    if (glType == 0)
    {
        Logger::Instance().log("Unsupported shader type for OpenGL", Logger::LogLevel::ERROR);
        return false;
    }

    GLuint shader = glCreateShader(glType);
    const char* src = m_source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    GLint logLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1)
    {
        std::vector<char> log(logLen);
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        m_compileLog.assign(log.data(), log.size());
    }

    if (!success)
    {
        Logger::Instance().log(m_compileLog.empty() ? "Shader-Compile fehlgeschlagen" : m_compileLog, Logger::LogLevel::ERROR);
        glDeleteShader(shader);
        return false;
    }

    m_shader = shader;
    m_compiled = true;
    return true;
}