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

bool OpenGLShader::loadFromSource(ShaderType type, const std::string& source)
{
    m_type = type;
    m_source = source;
    return compile();
}

bool OpenGLShader::loadFromFile(ShaderType type, const std::string& filePath)
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

bool OpenGLShader::compile()
{
    // Clean previous
    if (m_shader)
    {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
    m_compileLog.clear();
    m_compiled = false;

    GLuint shader = glCreateShader(static_cast<GLenum>(m_type));
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