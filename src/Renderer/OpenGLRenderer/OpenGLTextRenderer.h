#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../../Core/EngineObject.h"
#include "../../Core/MathTypes.h"
#include "glad/include/gl.h"

class OpenGLTextRenderer : public EngineObject
{
public:
    struct Glyph
    {
        glm::ivec2 size{};
        glm::ivec2 bearing{};
        unsigned int advance{0};
        glm::vec2 uvOffset{};
        glm::vec2 uvSize{};
    };

    OpenGLTextRenderer() = default;
    ~OpenGLTextRenderer() override;

    bool initialize(const std::string& fontPath, const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    void setScreenSize(int width, int height);
    void drawText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color);

private:
    bool buildShaderProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    bool buildGlyphAtlas(const std::string& fontPath);

    std::unordered_map<char, Glyph> m_glyphs;
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_atlasTexture{0};
    glm::mat4 m_projection{1.0f};
    int m_atlasWidth{0};
    int m_atlasHeight{0};
};
