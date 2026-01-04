#include "OpenGLShaderProgram.h"

#include <vector>
#include <glm/gtc/type_ptr.hpp>
#include "Logger.h"

OpenGLShaderProgram::OpenGLShaderProgram() = default;

OpenGLShaderProgram::~OpenGLShaderProgram()
{
    if (m_program)
    {
        glDeleteProgram(m_program);
        m_program = 0;
    }
}

bool OpenGLShaderProgram::attach(const OpenGLShader& shader)
{
    if (!shader.isCompiled() || shader.id() == 0)
    {
        Logger::Instance().log("Attach fehlgeschlagen: Shader nicht kompiliert", Logger::LogLevel::ERROR);
        return false;
    }

    if (m_program == 0)
    {
        m_program = glCreateProgram();
    }

    glAttachShader(m_program, shader.id());
    m_linked = false;
    return true;
}

bool OpenGLShaderProgram::link()
{
    if (m_program == 0)
    {
        Logger::Instance().log("Kein Programm zum Linken vorhanden", Logger::LogLevel::ERROR);
        return false;
    }

    glLinkProgram(m_program);

    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);

    GLint logLen = 0;
    glGetProgramiv(m_program, GL_INFO_LOG_LENGTH, &logLen);
    m_linkLog.clear();
    if (logLen > 1)
    {
        std::vector<char> log(logLen);
        glGetProgramInfoLog(m_program, logLen, nullptr, log.data());
        m_linkLog.assign(log.data(), log.size());
    }

    if (!linked)
    {
        Logger::Instance().log(m_linkLog.empty() ? "Programmlink fehlgeschlagen" : m_linkLog, Logger::LogLevel::ERROR);
        m_linked = false;
        return false;
    }

    m_linked = true;
    return true;
}

void OpenGLShaderProgram::bind() const
{
    glUseProgram(m_program);
}

void OpenGLShaderProgram::unbind() const
{
    glUseProgram(0);
}

GLint OpenGLShaderProgram::getUniformLocation(const std::string& name) const
{
    return glGetUniformLocation(m_program, name.c_str());
}

void OpenGLShaderProgram::setUniform(const std::string& name, float value)
{
    GLint loc = getUniformLocation(name);
    if (loc >= 0)
    {
        glUniform1f(loc, value);
    }
}

void OpenGLShaderProgram::setUniform(const std::string& name, int32_t value)
{
    GLint loc = getUniformLocation(name);
    if (loc >= 0)
    {
        glUniform1i(loc, value);
    }
}

void OpenGLShaderProgram::setUniform(const std::string& name, const glm::vec3& value)
{
    GLint loc = getUniformLocation(name);
    if (loc >= 0)
    {
        glUniform3fv(loc, 1, glm::value_ptr(value));
    }
}

void OpenGLShaderProgram::setUniform(const std::string& name, const glm::vec4& value)
{
    GLint loc = getUniformLocation(name);
    if (loc >= 0)
    {
        glUniform4fv(loc, 1, glm::value_ptr(value));
    }
}

void OpenGLShaderProgram::setUniformMat4(const std::string& name, const glm::mat4& value)
{
    GLint loc = getUniformLocation(name);
    if (loc >= 0)
    {
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
    }
}
