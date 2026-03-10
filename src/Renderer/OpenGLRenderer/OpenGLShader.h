#pragma once

#include <string>
#include <cstdint>
#include <glm/glm.hpp>
#include "glad/include/gl.h"
#include "../Shader.h"

class OpenGLShader : public Shader
{
public:
    OpenGLShader();
    ~OpenGLShader() override;

    bool loadFromSource(Shader::Type type, const std::string& source) override;
    bool loadFromFile(Shader::Type type, const std::string& filePath) override;
    bool loadFromFileWithDefines(Shader::Type type, const std::string& filePath, const std::string& defines);
    bool compile() override;

    Type type() const override { return static_cast<Type>(m_type); }
    const std::string& source() const override { return m_source; }
    const std::string& compileLog() const override { return m_compileLog; }

    GLuint id() const { return m_shader; }

private:
    GLuint m_shader{0};
    Shader::Type m_type{Shader::Type::Vertex};
    std::string m_source;
    std::string m_compileLog;
    bool m_compiled{false};
};
