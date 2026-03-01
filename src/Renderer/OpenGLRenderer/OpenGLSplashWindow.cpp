#include "OpenGLSplashWindow.h"

#include <SDL3/SDL.h>
#include "glad/include/gl.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <filesystem>
#include <cstring>
#include <algorithm>
#include <vector>

// ── Minimal inline shaders ──────────────────────────────────────────

static const char* kQuadVert = R"(
#version 330 core
layout(location=0) in vec2 aPos;
uniform mat4 uProj;
void main(){ gl_Position = uProj * vec4(aPos, 0.0, 1.0); }
)";

static const char* kQuadFrag = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main(){ FragColor = uColor; }
)";

static const char* kTextVert = R"(
#version 330 core
layout(location=0) in vec4 aVertex; // xy=pos, zw=uv
uniform mat4 uProj;
out vec2 vUV;
void main(){
    gl_Position = uProj * vec4(aVertex.xy, 0.0, 1.0);
    vUV = aVertex.zw;
}
)";

static const char* kTextFrag = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uAtlas;
uniform vec3 uColor;
out vec4 FragColor;
void main(){
    float a = texture(uAtlas, vUV).r;
    FragColor = vec4(uColor, a);
}
)";

// ── Helpers ─────────────────────────────────────────────────────────

static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

static GLuint linkProgram(const char* vertSrc, const char* fragSrc)
{
    GLuint v = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

static void setOrtho(GLuint prog, float w, float h)
{
    // Simple 2D orthographic projection (origin top-left)
    float proj[16] = {
        2.0f / w,  0.0f,       0.0f, 0.0f,
        0.0f,     -2.0f / h,   0.0f, 0.0f,
        0.0f,      0.0f,      -1.0f, 0.0f,
       -1.0f,      1.0f,       0.0f, 1.0f
    };
    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uProj"), 1, GL_FALSE, proj);
}

// ── OpenGLSplashWindow ─────────────────────────────────────────────

OpenGLSplashWindow::OpenGLSplashWindow() = default;

OpenGLSplashWindow::~OpenGLSplashWindow()
{
    close();
}

bool OpenGLSplashWindow::create()
{
    if (m_window) return true;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    constexpr int kW = 480;
    constexpr int kH = 260;

    m_window = SDL_CreateWindow("HorizonEngine",
        kW, kH,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP);
    if (!m_window) return false;

    // Centre on screen
    {
        SDL_DisplayID displayId = SDL_GetPrimaryDisplay();
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displayId);
        if (mode)
        {
            int sx = (mode->w - kW) / 2;
            int sy = (mode->h - kH) / 2;
            SDL_SetWindowPosition(m_window, sx, sy);
        }
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }
    SDL_GL_MakeCurrent(m_window, static_cast<SDL_GLContext>(m_glContext));

    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
    {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_glContext));
        SDL_DestroyWindow(m_window);
        m_glContext = nullptr;
        m_window = nullptr;
        return false;
    }

    initGLResources();
    SDL_ShowWindow(m_window);
    return true;
}

void OpenGLSplashWindow::setStatus(const std::string& text)
{
    m_statusText = text;
}

void OpenGLSplashWindow::render()
{
    if (!m_window) return;

    // Pump events
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
    {
        if (ev.type == SDL_EVENT_QUIT)
            m_closeRequested = true;
        if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            m_closeRequested = true;
    }

    // Save the current (main renderer) GL context so we can restore it after drawing.
    SDL_Window*  prevWin = SDL_GL_GetCurrentWindow();
    SDL_GLContext prevCtx = SDL_GL_GetCurrentContext();

    SDL_GL_MakeCurrent(m_window, static_cast<SDL_GLContext>(m_glContext));

    constexpr int kW = 480;
    constexpr int kH = 260;

    glViewport(0, 0, kW, kH);
    glClearColor(0.09f, 0.09f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (m_glResourcesReady)
    {
        // Subtle border
        setOrtho(m_quadProgram, static_cast<float>(kW), static_cast<float>(kH));
        drawQuad(0, 0, static_cast<float>(kW), 1, 0.25f, 0.25f, 0.30f, 1.0f);
        drawQuad(0, static_cast<float>(kH - 1), static_cast<float>(kW), static_cast<float>(kH), 0.25f, 0.25f, 0.30f, 1.0f);
        drawQuad(0, 0, 1, static_cast<float>(kH), 0.25f, 0.25f, 0.30f, 1.0f);
        drawQuad(static_cast<float>(kW - 1), 0, static_cast<float>(kW), static_cast<float>(kH), 0.25f, 0.25f, 0.30f, 1.0f);

        // Title – "Horizon Engine"
        setOrtho(m_textProgram, static_cast<float>(kW), static_cast<float>(kH));
        drawText("Horizon Engine", 24.0f, 100.0f, 1.6f, 0.85f, 0.85f, 0.90f);

        // Status text – bottom-left
        if (!m_statusText.empty())
        {
            drawText(m_statusText, 14.0f, static_cast<float>(kH) - 18.0f, 0.7f, 0.55f, 0.58f, 0.65f);
        }
    }

    SDL_GL_SwapWindow(m_window);

    // Restore the previous GL context so the main renderer keeps working.
    if (prevWin && prevCtx)
    {
        SDL_GL_MakeCurrent(prevWin, prevCtx);
    }
}

void OpenGLSplashWindow::close()
{
    if (!m_window) return;

    // Save the current context so we can restore it after destroying splash resources.
    SDL_Window*  prevWin = SDL_GL_GetCurrentWindow();
    SDL_GLContext prevCtx = SDL_GL_GetCurrentContext();

    SDL_GL_MakeCurrent(m_window, static_cast<SDL_GLContext>(m_glContext));
    releaseGLResources();

    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_glContext));
    SDL_DestroyWindow(m_window);
    m_glContext = nullptr;
    m_window = nullptr;

    // Restore the previous GL context (the main renderer's).
    if (prevWin && prevCtx)
    {
        SDL_GL_MakeCurrent(prevWin, prevCtx);
    }
}

// ── GL resource management ──────────────────────────────────────────

void OpenGLSplashWindow::initGLResources()
{
    // Quad shader + VAO
    m_quadProgram = linkProgram(kQuadVert, kQuadFrag);

    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers(1, &m_quadVbo);
    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Text shader + VAO
    m_textProgram = linkProgram(kTextVert, kTextFrag);

    glGenVertexArrays(1, &m_textVao);
    glGenBuffers(1, &m_textVbo);
    glBindVertexArray(m_textVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glBindVertexArray(0);

    // Build glyph atlas from FreeType
    const std::filesystem::path fontPath = std::filesystem::current_path() / "Content" / "Fonts" / "default.ttf";

    FT_Library ft{};
    FT_Face face{};
    if (FT_Init_FreeType(&ft) == 0 && FT_New_Face(ft, fontPath.string().c_str(), 0, &face) == 0)
    {
        FT_Set_Pixel_Sizes(face, 0, 28);

        // Calculate atlas dimensions
        int maxH = 0;
        int totalW = 0;
        for (unsigned char c = 32; c < 127; ++c)
        {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0) continue;
            totalW += static_cast<int>(face->glyph->bitmap.width) + 1;
            maxH = std::max(maxH, static_cast<int>(face->glyph->bitmap.rows));
        }

        m_atlasWidth = totalW;
        m_atlasHeight = maxH;

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glGenTextures(1, &m_fontAtlas);
        glBindTexture(GL_TEXTURE_2D, m_fontAtlas);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_atlasWidth, m_atlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int xOff = 0;
        for (unsigned char c = 32; c < 127; ++c)
        {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER) != 0) continue;
            auto& bmp = face->glyph->bitmap;
            if (bmp.width > 0 && bmp.rows > 0)
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, xOff, 0,
                    static_cast<int>(bmp.width), static_cast<int>(bmp.rows),
                    GL_RED, GL_UNSIGNED_BYTE, bmp.buffer);
            }

            auto& g = m_glyphs[c];
            g.width    = static_cast<int>(bmp.width);
            g.height   = static_cast<int>(bmp.rows);
            g.bearingX = face->glyph->bitmap_left;
            g.bearingY = face->glyph->bitmap_top;
            g.advance  = static_cast<int>(face->glyph->advance.x >> 6);
            g.tx0 = static_cast<float>(xOff) / static_cast<float>(m_atlasWidth);
            g.ty0 = 0.0f;
            g.tx1 = static_cast<float>(xOff + g.width) / static_cast<float>(m_atlasWidth);
            g.ty1 = static_cast<float>(g.height) / static_cast<float>(m_atlasHeight);

            xOff += static_cast<int>(bmp.width) + 1;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
    }

    m_glResourcesReady = true;
}

void OpenGLSplashWindow::releaseGLResources()
{
    if (m_fontAtlas)  { glDeleteTextures(1, &m_fontAtlas); m_fontAtlas = 0; }
    if (m_textVbo)    { glDeleteBuffers(1, &m_textVbo);    m_textVbo = 0; }
    if (m_textVao)    { glDeleteVertexArrays(1, &m_textVao); m_textVao = 0; }
    if (m_textProgram){ glDeleteProgram(m_textProgram);    m_textProgram = 0; }
    if (m_quadVbo)    { glDeleteBuffers(1, &m_quadVbo);    m_quadVbo = 0; }
    if (m_quadVao)    { glDeleteVertexArrays(1, &m_quadVao); m_quadVao = 0; }
    if (m_quadProgram){ glDeleteProgram(m_quadProgram);    m_quadProgram = 0; }
    m_glResourcesReady = false;
}

void OpenGLSplashWindow::drawQuad(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
{
    float verts[] = { x0,y0, x1,y0, x0,y1, x1,y0, x1,y1, x0,y1 };
    glUseProgram(m_quadProgram);
    glUniform4f(glGetUniformLocation(m_quadProgram, "uColor"), r, g, b, a);
    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void OpenGLSplashWindow::drawText(const std::string& text, float x, float y, float scale, float r, float g, float b)
{
    glUseProgram(m_textProgram);
    glUniform3f(glGetUniformLocation(m_textProgram, "uColor"), r, g, b);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlas);
    glUniform1i(glGetUniformLocation(m_textProgram, "uAtlas"), 0);
    glBindVertexArray(m_textVao);

    float cursorX = x;
    for (unsigned char c : text)
    {
        if (c >= 128 || c < 32) continue;
        auto& g2 = m_glyphs[c];
        if (g2.width == 0 && g2.height == 0) { cursorX += g2.advance * scale; continue; }

        float xpos = cursorX + g2.bearingX * scale;
        float ypos = y - g2.bearingY * scale;
        float w    = g2.width * scale;
        float h    = g2.height * scale;

        float verts[6][4] = {
            { xpos,     ypos,     g2.tx0, g2.ty0 },
            { xpos + w, ypos,     g2.tx1, g2.ty0 },
            { xpos,     ypos + h, g2.tx0, g2.ty1 },
            { xpos + w, ypos,     g2.tx1, g2.ty0 },
            { xpos + w, ypos + h, g2.tx1, g2.ty1 },
            { xpos,     ypos + h, g2.tx0, g2.ty1 },
        };
        glBindBuffer(GL_ARRAY_BUFFER, m_textVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        cursorX += g2.advance * scale;
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
