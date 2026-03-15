#pragma once

#include <string>
#include <cstdint>

// Forward declarations
class Texture;

// Editor window that displays a single texture asset with zoom/pan,
// channel isolation and metadata display.
class TextureViewerWindow
{
public:
    TextureViewerWindow();
    ~TextureViewerWindow();

    // Load the texture asset and extract metadata.
    bool initialize(const std::string& assetPath);

    const std::string& getAssetPath() const { return m_assetPath; }
    bool isInitialized() const { return m_initialized; }

    // --- Metadata ---
    int  getWidth()    const { return m_width; }
    int  getHeight()   const { return m_height; }
    int  getChannels() const { return m_channels; }
    int  getMipCount() const { return m_mipCount; }
    bool isCompressed() const { return m_compressed; }
    const std::string& getFormatString() const { return m_formatString; }
    size_t getFileSizeBytes() const { return m_fileSizeBytes; }

    // --- Channel mask (RGBA toggles) ---
    bool isChannelR() const { return m_channelR; }
    bool isChannelG() const { return m_channelG; }
    bool isChannelB() const { return m_channelB; }
    bool isChannelA() const { return m_channelA; }
    void setChannelR(bool v) { m_channelR = v; }
    void setChannelG(bool v) { m_channelG = v; }
    void setChannelB(bool v) { m_channelB = v; }
    void setChannelA(bool v) { m_channelA = v; }

    // --- Zoom / Pan ---
    float getZoom() const { return m_zoom; }
    void  setZoom(float z) { m_zoom = z; }
    float getPanX() const { return m_panX; }
    float getPanY() const { return m_panY; }
    void  setPanX(float x) { m_panX = x; }
    void  setPanY(float y) { m_panY = y; }

    // --- Checkerboard background toggle ---
    bool isCheckerboard() const { return m_checkerboard; }
    void setCheckerboard(bool v) { m_checkerboard = v; }

    // GPU texture handle (set by the renderer after uploading)
    unsigned int getGLTextureId() const { return m_glTextureId; }
    void setGLTextureId(unsigned int id) { m_glTextureId = id; }

private:
    std::string m_assetPath;
    bool m_initialized{ false };

    // Metadata
    int         m_width{ 0 };
    int         m_height{ 0 };
    int         m_channels{ 0 };
    int         m_mipCount{ 1 };
    bool        m_compressed{ false };
    std::string m_formatString{ "Unknown" };
    size_t      m_fileSizeBytes{ 0 };

    // Channel mask (all on by default)
    bool m_channelR{ true };
    bool m_channelG{ true };
    bool m_channelB{ true };
    bool m_channelA{ true };

    // Zoom / Pan
    float m_zoom{ 1.0f };
    float m_panX{ 0.0f };
    float m_panY{ 0.0f };

    // Checkerboard background (on by default)
    bool m_checkerboard{ true };

    // GL texture handle (owned elsewhere, not freed here)
    unsigned int m_glTextureId{ 0 };
};
