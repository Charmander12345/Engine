#include "OpenGLTextRenderer.h"

#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <glm/gtc/matrix_transform.hpp>

#include "OpenGLShader.h"
#include "Logger.h"

OpenGLTextRenderer::~OpenGLTextRenderer()
{
    shutdown();
}

void OpenGLTextRenderer::shutdown()
{
    if (m_popupVao)
    {
        glDeleteVertexArrays(1, &m_popupVao);
        m_popupVao = 0;
    }
    if (m_vbo)
    {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao)
    {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_atlasTexture)
    {
        glDeleteTextures(1, &m_atlasTexture);
        m_atlasTexture = 0;
    }
    for (auto& entry : m_programCache)
    {
        if (entry.second)
        {
            glDeleteProgram(entry.second);
        }
    }
    m_programCache.clear();
    m_program = 0;
}

bool OpenGLTextRenderer::initialize(const std::string& fontPath, const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
    if (!buildShaderProgram(vertexShaderPath, fragmentShaderPath, m_program))
    {
        return false;
    }

    const std::string key = vertexShaderPath + "|" + fragmentShaderPath;
    m_programCache[key] = m_program;

    if (!buildGlyphAtlas(fontPath))
    {
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glUseProgram(m_program);
    glUniform1i(glGetUniformLocation(m_program, "uTextAtlas"), 0);

    return true;
}

void OpenGLTextRenderer::setScreenSize(int width, int height)
{
    m_projection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
}

void OpenGLTextRenderer::drawText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color)
{
    drawTextWithProgram(text, screenPos, scale, color, m_program);
}

void OpenGLTextRenderer::drawTextWithShader(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color,
    const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
    const GLuint program = getProgramForShaders(vertexShaderPath, fragmentShaderPath);
    if (program == 0)
    {
        return;
    }

    drawTextWithProgram(text, screenPos, scale, color, program);
}

bool OpenGLTextRenderer::buildShaderProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath, GLuint& outProgram)
{
    auto vertex = std::make_shared<OpenGLShader>();
    auto fragment = std::make_shared<OpenGLShader>();

    if (!vertex->loadFromFile(Shader::Type::Vertex, vertexShaderPath) || !fragment->loadFromFile(Shader::Type::Fragment, fragmentShaderPath))
    {
        return false;
    }

    outProgram = glCreateProgram();
    glAttachShader(outProgram, vertex->id());
    glAttachShader(outProgram, fragment->id());
    glLinkProgram(outProgram);

    GLint linked = 0;
    glGetProgramiv(outProgram, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        Logger::Instance().log("OpenGLTextRenderer: Failed to link text shader program", Logger::LogLevel::ERROR);
        glDeleteProgram(outProgram);
        outProgram = 0;
        return false;
    }

    return true;
}

GLuint OpenGLTextRenderer::getProgramForShaders(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
    if (vertexShaderPath.empty() || fragmentShaderPath.empty())
    {
        return m_program;
    }

    const std::string key = vertexShaderPath + "|" + fragmentShaderPath;
    auto it = m_programCache.find(key);
    if (it != m_programCache.end())
    {
        return it->second;
    }

    GLuint program = 0;
    if (!buildShaderProgram(vertexShaderPath, fragmentShaderPath, program))
    {
        return 0;
    }

    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uTextAtlas"), 0);

    m_programCache[key] = program;
    return program;
}

void OpenGLTextRenderer::drawTextWithProgram(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color, GLuint program)
{
    if (!program || !m_atlasTexture)
    {
        return;
    }

    const glm::vec4 glColor{ color.x, color.y, color.z, color.w };

    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uProjection"), 1, GL_FALSE, &m_projection[0][0]);
    glUniform4fv(glGetUniformLocation(program, "uTextColor"), 1, &glColor[0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);

    glBindVertexArray(m_vao);

    float xpos = screenPos.x;
    float baseline = screenPos.y;
    if (m_fontAscent > 0.0f)
    {
        baseline += m_fontAscent * scale;
    }

    GLint prevBlendSrcRGB = 0, prevBlendDstRGB = 0, prevBlendSrcAlpha = 0, prevBlendDstAlpha = 0;
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (char c : text)
    {
        auto it = m_glyphs.find(c);
        if (it == m_glyphs.end())
        {
            continue;
        }

        const Glyph& glyph = it->second;

        float gx = xpos + static_cast<float>(glyph.bearing.x) * scale;
        float gy = baseline - static_cast<float>(glyph.bearing.y) * scale;
        float w = static_cast<float>(glyph.size.x) * scale;
        float h = static_cast<float>(glyph.size.y) * scale;

        const float u0 = glyph.uvOffset.x;
        const float v0 = glyph.uvOffset.y;
        const float u1 = glyph.uvOffset.x + glyph.uvSize.x;
        const float v1 = glyph.uvOffset.y + glyph.uvSize.y;

        float vertices[6][4] = {
            { gx,     gy + h, u0, v1 },
            { gx,     gy,     u0, v0 },
            { gx + w, gy,     u1, v0 },

            { gx,     gy + h, u0, v1 },
            { gx + w, gy,     u1, v0 },
            { gx + w, gy + h, u1, v1 }
        };

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        xpos += (static_cast<float>(glyph.advance) / 64.0f) * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha);
    if (!blendWasEnabled)
    {
        glDisable(GL_BLEND);
    }
}

Vec2 OpenGLTextRenderer::measureText(const std::string& text, float scale) const
{
    float width = 0.0f;
    for (char c : text)
    {
        auto it = m_glyphs.find(c);
        if (it == m_glyphs.end())
        {
            continue;
        }
        width += (static_cast<float>(it->second.advance) / 64.0f) * scale;
    }

    const float height = getLineHeight(scale);
    return Vec2{ width, height };
}

float OpenGLTextRenderer::getLineHeight(float scale) const
{
    const float lineHeight = (m_fontAscent - m_fontDescent);
    if (lineHeight > 0.0f)
    {
        return lineHeight * scale;
    }
    return 48.0f * scale;
}

void OpenGLTextRenderer::ensurePopupVao()
{
    if (m_popupVao != 0) return;
    if (m_vbo == 0) return;

    glGenVertexArrays(1, &m_popupVao);
    glBindVertexArray(m_popupVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

bool OpenGLTextRenderer::buildGlyphAtlas(const std::string& fontPath)
{
    FT_Library ft;
    if (FT_Init_FreeType(&ft))
    {
        Logger::Instance().log("OpenGLTextRenderer: Failed to init FreeType", Logger::LogLevel::ERROR);
        return false;
    }

    FT_Face face;
    if (FT_New_Face(ft, fontPath.c_str(), 0, &face))
    {
        Logger::Instance().log("OpenGLTextRenderer: Failed to load font: " + fontPath, Logger::LogLevel::ERROR);
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, 48);
    m_fontAscent = static_cast<float>(face->size->metrics.ascender) / 64.0f;
    m_fontDescent = static_cast<float>(face->size->metrics.descender) / 64.0f;

    const int atlasWidth = 1024;
    const int atlasHeight = 1024;
    std::vector<unsigned char> atlas(atlasWidth * atlasHeight, 0);

    int penX = 0;
    int penY = 0;
    int rowHeight = 0;

    for (unsigned char c = 32; c < 127; ++c)
    {
        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
        {
            continue;
        }

        const FT_GlyphSlot glyph = face->glyph;

        if (penX + glyph->bitmap.width >= atlasWidth)
        {
            penX = 0;
            penY += rowHeight + 1;
            rowHeight = 0;
        }

        if (penY + glyph->bitmap.rows >= atlasHeight)
        {
            Logger::Instance().log("OpenGLTextRenderer: Glyph atlas overflow", Logger::LogLevel::ERROR);
            break;
        }

        for (int y = 0; y < static_cast<int>(glyph->bitmap.rows); ++y)
        {
            for (int x = 0; x < static_cast<int>(glyph->bitmap.width); ++x)
            {
                atlas[(penX + x) + (penY + y) * atlasWidth] = glyph->bitmap.buffer[x + y * glyph->bitmap.width];
            }
        }

        Glyph out;
        out.size = glm::ivec2(glyph->bitmap.width, glyph->bitmap.rows);
        out.bearing = glm::ivec2(glyph->bitmap_left, glyph->bitmap_top);
        out.advance = static_cast<unsigned int>(glyph->advance.x);
        out.uvOffset = glm::vec2(static_cast<float>(penX) / atlasWidth, static_cast<float>(penY) / atlasHeight);
        out.uvSize = glm::vec2(static_cast<float>(glyph->bitmap.width) / atlasWidth, static_cast<float>(glyph->bitmap.rows) / atlasHeight);
        m_glyphs.emplace(static_cast<char>(c), out);

        penX += static_cast<int>(glyph->bitmap.width) + 1;
        rowHeight = std::max(rowHeight, static_cast<int>(glyph->bitmap.rows));
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &m_atlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasWidth, atlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_atlasWidth = atlasWidth;
    m_atlasHeight = atlasHeight;

    FT_Done_Face(face);
    FT_Done_FreeType(ft);

    return true;
}
