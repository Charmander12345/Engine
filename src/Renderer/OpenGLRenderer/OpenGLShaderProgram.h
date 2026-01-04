#pragma once

#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include "glad/include/gl.h"
#include "OpenGLShader.h"

class OpenGLShaderProgram
{
public:
    OpenGLShaderProgram();
    ~OpenGLShaderProgram();

    bool attach(const OpenGLShader& shader);
    bool link();

    void bind() const;
    void unbind() const;

    bool isLinked() const { return m_linked; }
    const std::string& linkLog() const { return m_linkLog; }
    GLuint id() const { return m_program; }

    // Uniform setters
    void setUniform(const std::string& name, float value);
    void setUniform(const std::string& name, int32_t value);
    void setUniform(const std::string& name, const glm::vec3& value);
    void setUniform(const std::string& name, const glm::vec4& value);
    void setUniformMat4(const std::string& name, const glm::mat4& value);

private:
    GLint getUniformLocation(const std::string& name) const;

    GLuint m_program{0};
    bool m_linked{false};
    std::string m_linkLog;
};
