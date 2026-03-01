#pragma once

#include "../SplashWindow.h"
#include <string>

struct SDL_Window;

/// OpenGL implementation of the splash / startup window.
/// Uses its own SDL window + OpenGL context for minimal rendering.
class OpenGLSplashWindow : public SplashWindow
{
public:
    OpenGLSplashWindow();
    ~OpenGLSplashWindow() override;

    bool create() override;
    void setStatus(const std::string& text) override;
    void render() override;
    void close() override;
    bool isOpen() const override { return m_window != nullptr; }
    bool wasCloseRequested() const override { return m_closeRequested; }

private:
    void initGLResources();
    void releaseGLResources();
    void drawQuad(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
    void drawText(const std::string& text, float x, float y, float scale, float r, float g, float b);

    SDL_Window*  m_window{ nullptr };
    void*        m_glContext{ nullptr };
    bool m_closeRequested{ false };

    std::string m_statusText;

    // Minimal GL resources
    unsigned int m_quadVao{ 0 };
    unsigned int m_quadVbo{ 0 };
    unsigned int m_quadProgram{ 0 };

    // Minimal text rendering (glyph atlas built from FreeType)
    struct GlyphInfo
    {
        float tx0, ty0, tx1, ty1; // texcoords in atlas
        int width, height;
        int bearingX, bearingY;
        int advance;
    };
    GlyphInfo m_glyphs[128]{};
    unsigned int m_fontAtlas{ 0 };
    int m_atlasWidth{ 0 };
    int m_atlasHeight{ 0 };
    unsigned int m_textVao{ 0 };
    unsigned int m_textVbo{ 0 };
    unsigned int m_textProgram{ 0 };
    bool m_glResourcesReady{ false };
};
