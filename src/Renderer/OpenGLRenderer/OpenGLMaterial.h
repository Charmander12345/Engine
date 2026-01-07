#pragma once

#include <vector>
#include <memory>
#include "../Material.h"
#include "OpenGLShader.h"
#include "glad/include/gl.h"

class OpenGLMaterial : public Material
{
public:
    struct LayoutElement
    {
        GLuint index;
        GLint size;
        GLenum type;
        GLboolean normalized;
        GLsizei stride;
        size_t offset;
    };

    OpenGLMaterial() = default;
    ~OpenGLMaterial() override;

    void addShader(const std::shared_ptr<OpenGLShader>& shader);

    void setVertexData(const std::vector<float>& data) { m_vertexData = data; }
    void setLayout(const std::vector<LayoutElement>& layout) { m_layout = layout; }

    bool build() override;

    void bind() override;
    void unbind() override;
    void render() override;

private:
    std::vector<std::shared_ptr<OpenGLShader>> m_shaders;
    std::vector<float> m_vertexData;
    std::vector<LayoutElement> m_layout;
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLsizei m_vertexCount{0};
};
