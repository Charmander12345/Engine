#pragma once

#include <string>
#include <glm/glm.hpp>
#include "glad/include/gl.h"

class OpenGLShader
{
public:
    enum class ShaderType : GLenum
    {
        Vertex   = GL_VERTEX_SHADER,
        Fragment = GL_FRAGMENT_SHADER
    };

    OpenGLShader();
    ~OpenGLShader();

    // Load shader source from string or file and compile
    bool loadFromSource(ShaderType type, const std::string& source);
    bool loadFromFile(ShaderType type, const std::string& filePath);

    // Compile current source; called internally by load helpers
    bool compile();

    // Introspection
    ShaderType type() const { return m_type; }
    bool isCompiled() const { return m_compiled; }
    const std::string& source() const { return m_source; }
    const std::string& compileLog() const { return m_compileLog; }
    GLuint id() const { return m_shader; }

private:
    GLuint m_shader{0};
    ShaderType m_type{ShaderType::Vertex};
    std::string m_source;
    std::string m_compileLog;
    bool m_compiled{false};
};
