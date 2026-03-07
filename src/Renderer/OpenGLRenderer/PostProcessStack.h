#pragma once

#include "glad/include/gl.h"
#include <glm/glm.hpp>
#include <vector>

/// @brief Manages the post-processing pipeline for the OpenGL renderer.
///
/// The stack owns an HDR framebuffer (RGBA16F) and a fullscreen-triangle VAO.
/// During a frame the 3D scene is rendered into the HDR FBO instead of the
/// final tab FBO.  After scene rendering, execute() runs the resolve pass
/// which copies the HDR colour into the destination FBO (currently a simple
/// blit; later steps will add tone mapping, gamma correction, bloom, etc.).
///
/// Lifecycle:
///   1. init()                – compile shaders, create fullscreen VAO
///   2. resize(w,h)           – (re)create the HDR FBO to match viewport size
///   3. bindHdrTarget()       – bind the HDR FBO as render target
///   4. execute(dstFbo, w, h) – run the resolve pass into the destination FBO
///   5. shutdown()            – release all GPU resources
class PostProcessStack
{
public:
    PostProcessStack() = default;
    ~PostProcessStack();

    /// Compile resolve shaders and create fullscreen VAO.  Call once.
    bool init(const char* resolveVertPath, const char* resolveFragPath);

    /// (Re)create the HDR FBO to the given size.  No-op when size unchanged.
    bool resize(int width, int height);

    /// Bind the HDR FBO as the current render target.
    void bindHdrTarget();

    /// Run the resolve pass: read from HDR colour → write to @p dstFbo.
    /// @p vpX, vpY, vpW, vpH define the sub-rectangle to blit.
    void execute(GLuint dstFbo, int vpX, int vpY, int vpW, int vpH);

    /// Release all GPU resources.
    void shutdown();

    /// @return the raw GL framebuffer handle for the HDR FBO.
    GLuint getHdrFbo() const { return m_hdrFbo; }

    /// @return the colour texture attached to the HDR FBO.
    GLuint getHdrColorTexture() const { return m_hdrColorTex; }

    bool isInitialized() const { return m_initialized; }

    /// Compile bloom shaders (downsample + blur).  Call once after init().
    bool initBloom(const char* vertPath, const char* downsampleFragPath, const char* blurFragPath);

    /// Compile SSAO shaders (ssao + blur).  Call once after init().
    bool initSsao(const char* vertPath, const char* ssaoFragPath, const char* blurFragPath);

    /// @return the depth texture attached to the HDR FBO (for external use such as HZB).
    GLuint getHdrDepthTexture() const { return m_hdrDepthTex; }

    // --- Post-process parameter setters ---
    void setGammaEnabled(bool v)        { m_gammaEnabled = v; }
    void setToneMappingEnabled(bool v)   { m_toneMappingEnabled = v; }
    void setExposure(float v)            { m_exposure = v; }
    void setAntiAliasingMode(int mode)   { m_aaMode = mode; }
    void setBloomEnabled(bool v)         { m_bloomEnabled = v; }
    void setBloomThreshold(float v)      { m_bloomThreshold = v; }
    void setBloomIntensity(float v)      { m_bloomIntensity = v; }
    void setSsaoEnabled(bool v)          { m_ssaoEnabled = v; }
    void setSsaoRadius(float v)          { m_ssaoRadius = v; }
    void setSsaoPower(float v)           { m_ssaoPower = v; }
    void setProjectionMatrix(const glm::mat4& proj) { m_projection = proj; }

    /// (Re)create the MSAA FBO for the given size and sample count.
    /// Pass samples <= 1 to release any existing MSAA resources.
    bool ensureMsaaFbo(int width, int height, int samples);

    /// Run a deferred FXAA-only pass on the destination framebuffer.
    /// Copies dstFbo content to a temp texture, then applies FXAA back to dstFbo.
    /// Call after overlays (gizmos, outlines) have been rendered to dstFbo.
    void executeFxaaPass(GLuint dstFbo, int vpX, int vpY, int vpW, int vpH);

private:
    bool compileResolveProgram(const char* vertPath, const char* fragPath);
    bool compileBloomPrograms(const char* vertPath, const char* downsampleFragPath, const char* blurFragPath);
    bool ensureBloomResources(int width, int height);
    void releaseBloomResources();
    void executeBloomPass();

    bool compileSsaoPrograms(const char* vertPath, const char* ssaoFragPath, const char* blurFragPath);
    bool ensureSsaoResources(int width, int height);
    void releaseSsaoResources();
    void executeSsaoPass();
    void generateSsaoKernel();
    void generateSsaoNoiseTexture();

    // HDR FBO
    GLuint m_hdrFbo{ 0 };
    GLuint m_hdrColorTex{ 0 };
    GLuint m_hdrDepthTex{ 0 };  // depth as texture (sampleable for SSAO)
    int    m_hdrWidth{ 0 };
    int    m_hdrHeight{ 0 };

    // Fullscreen triangle (positions generated from gl_VertexID)
    GLuint m_fullscreenVao{ 0 };

    // Resolve program
    GLuint m_resolveProgram{ 0 };
    GLint  m_resolveLocScene{ -1 };
    GLint  m_resolveLocGamma{ -1 };
    GLint  m_resolveLocToneMapping{ -1 };
    GLint  m_resolveLocExposure{ -1 };
    GLint  m_resolveLocAAMode{ -1 };
    GLint  m_resolveLocTexelSize{ -1 };
    GLint  m_resolveLocBloomEnabled{ -1 };
    GLint  m_resolveLocBloomTexture{ -1 };
    GLint  m_resolveLocBloomIntensity{ -1 };
    GLint  m_resolveLocSsaoEnabled{ -1 };
    GLint  m_resolveLocSsaoTexture{ -1 };

    // MSAA FBO (multisampled scene target; resolved to m_hdrColorTex before post-process)
    GLuint m_msaaFbo{ 0 };
    GLuint m_msaaColorRbo{ 0 };
    GLuint m_msaaDepthRbo{ 0 };
    int    m_msaaSamples{ 0 };
    int    m_msaaWidth{ 0 };
    int    m_msaaHeight{ 0 };
    void releaseMsaaFbo();
    void resolveMsaaToHdr();

    // FXAA deferred pass (applied after overlays)
    bool ensureFxaaResources(int width, int height);
    void releaseFxaaResources();
    GLuint m_fxaaTempFbo{ 0 };
    GLuint m_fxaaTempTex{ 0 };
    int    m_fxaaAllocW{ 0 };
    int    m_fxaaAllocH{ 0 };

    // Bloom programs
    GLuint m_bloomDownsampleProgram{ 0 };
    GLint  m_bloomDSLocSource{ -1 };
    GLint  m_bloomDSLocThreshold{ -1 };
    GLint  m_bloomDSLocPass{ -1 };

    GLuint m_bloomBlurProgram{ 0 };
    GLint  m_bloomBlurLocSource{ -1 };
    GLint  m_bloomBlurLocHorizontal{ -1 };

    // Bloom FBO chain (progressive downsample + ping-pong blur)
    static constexpr int kBloomMipCount = 5;
    GLuint m_bloomFbo{ 0 };
    GLuint m_bloomMipTextures[kBloomMipCount]{};
    GLuint m_bloomPingPongTex{ 0 };  // temporary texture for ping-pong blur
    int    m_bloomMipWidths[kBloomMipCount]{};
    int    m_bloomMipHeights[kBloomMipCount]{};
    int    m_bloomAllocWidth{ 0 };
    int    m_bloomAllocHeight{ 0 };

    // SSAO
    GLuint m_ssaoProgram{ 0 };
    GLint  m_ssaoLocDepth{ -1 };
    GLint  m_ssaoLocNoise{ -1 };
    GLint  m_ssaoLocProjection{ -1 };
    GLint  m_ssaoLocInvProjection{ -1 };
    GLint  m_ssaoLocNoiseScale{ -1 };
    GLint  m_ssaoLocRadius{ -1 };
    GLint  m_ssaoLocBias{ -1 };
    GLint  m_ssaoLocPower{ -1 };
    GLint  m_ssaoLocSamples{ -1 };

    GLuint m_ssaoBlurProgram{ 0 };
    GLint  m_ssaoBlurLocInput{ -1 };
    GLint  m_ssaoBlurLocDepth{ -1 };

    GLuint m_ssaoFbo{ 0 };
    GLuint m_ssaoColorTex{ 0 };      // R8 half-res AO
    GLuint m_ssaoBlurFbo{ 0 };
    GLuint m_ssaoBlurTex{ 0 };       // R8 half-res blurred AO
    GLuint m_ssaoNoiseTex{ 0 };
    int    m_ssaoAllocW{ 0 };
    int    m_ssaoAllocH{ 0 };

    static constexpr int kSsaoKernelSize = 32;
    std::vector<glm::vec3> m_ssaoKernel;

    // Cached parameter state (set by caller before execute())
    bool  m_gammaEnabled{ true };
    bool  m_toneMappingEnabled{ true };
    float m_exposure{ 1.0f };
    int   m_aaMode{ 0 };  // 0=None, 1=FXAA, 2=MSAA2x, 3=MSAA4x
    bool  m_bloomEnabled{ false };
    float m_bloomThreshold{ 1.0f };
    float m_bloomIntensity{ 0.3f };
    bool  m_ssaoEnabled{ false };
    float m_ssaoRadius{ 0.5f };
    float m_ssaoPower{ 1.5f };
    glm::mat4 m_projection{ 1.0f };

    bool m_initialized{ false };
};
