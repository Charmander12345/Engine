#pragma once

#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include "glad/include/gl.h"
#include <memory>
#include "OpenGLShader.h"
#include "../IShaderProgram.h"

class OpenGLShaderProgram : public IShaderProgram
{
public:
    OpenGLShaderProgram();
    ~OpenGLShaderProgram() override;

    bool attach(const OpenGLShader& shader);
    bool link() override;

    void bind() const override;
    void unbind() const override;

    bool isLinked() const override { return m_linked; }
    const std::string& linkLog() const override { return m_linkLog; }
    GLuint id() const { return m_program; }

    // Uniform setters
    void setUniform(const std::string& name, float value) override;
    void setUniform(const std::string& name, int32_t value) override;
    void setUniform(const std::string& name, const glm::vec3& value);
    void setUniform(const std::string& name, const glm::vec4& value);
    void setUniformMat4(const std::string& name, const glm::mat4& value);

private:
    GLint getUniformLocation(const std::string& name) const;

    GLuint m_program{0};
    bool m_linked{false};
    std::string m_linkLog;
};
