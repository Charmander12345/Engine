#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <string>

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
    void drawTextWithShader(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color,
        const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    Vec2 measureText(const std::string& text, float scale) const;
    float getLineHeight(float scale) const;

    // Popup GL-context VAO support (VAOs are not shared between contexts).
    void ensurePopupVao();
    void resetPopupVao() { m_popupVao = 0; }
    GLuint getPopupVao() const { return m_popupVao; }
    GLuint swapVao(GLuint newVao) { GLuint old = m_vao; m_vao = newVao; return old; }

private:
    bool buildShaderProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath, GLuint& outProgram);
    bool buildGlyphAtlas(const std::string& fontPath);
    GLuint getProgramForShaders(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    void drawTextWithProgram(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color, GLuint program);

    std::unordered_map<char, Glyph> m_glyphs;
    GLuint m_program{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_atlasTexture{0};
    glm::mat4 m_projection{1.0f};
    int m_atlasWidth{0};
    int m_atlasHeight{0};
    float m_fontAscent{0.0f};
    float m_fontDescent{0.0f};
    std::unordered_map<std::string, GLuint> m_programCache;
    GLuint m_popupVao{0};
};
