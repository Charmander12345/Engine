#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include "../Material.h"
#include "OpenGLShader.h"
#include "glad/include/gl.h"

class Texture;
class OpenGLTexture;

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

    // Interleaved unique vertex buffer (positions/colors/uvs) and optional indices
    void setVertexData(const std::vector<float>& data) { m_vertexData = data; }
    void setIndexData(const std::vector<uint32_t>& indices) { m_indexData = indices; }

    void setLayout(const std::vector<LayoutElement>& layout) { m_layout = layout; }

    bool build() override;

    void bind() override;
    void unbind() override;
    void render() override;

private:
    void bindTextures();

    std::vector<std::shared_ptr<OpenGLShader>> m_shaders;
    std::vector<float> m_vertexData;
    std::vector<uint32_t> m_indexData;
    std::vector<LayoutElement> m_layout;
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_ebo{0};
    GLsizei m_vertexCount{0};
    GLsizei m_indexCount{0};

    // Cache GPU textures per CPU-Texture pointer
    std::unordered_map<const Texture*, std::shared_ptr<OpenGLTexture>> m_textureCache;
};
