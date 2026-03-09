#include "PostProcessStack.h"
#include "OpenGLShader.h"
#include "../Shader.h"
#include "Logger.h"
#include <algorithm>
#include <random>
#include <glm/gtc/matrix_inverse.hpp>

PostProcessStack::~PostProcessStack()
{
    shutdown();
}

bool PostProcessStack::init(const char* resolveVertPath, const char* resolveFragPath)
{
    if (m_initialized)
        return true;

    m_resolveVertPath = resolveVertPath;
    m_resolveFragPath = resolveFragPath;

    // Fullscreen triangle VAO (positions generated from gl_VertexID in the vertex shader)
    glGenVertexArrays(1, &m_fullscreenVao);

    if (!compileResolveProgram(resolveVertPath, resolveFragPath))
    {
        shutdown();
        return false;
    }

    m_initialized = true;
    return true;
}

bool PostProcessStack::resize(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (m_hdrFbo != 0 && m_hdrWidth == width && m_hdrHeight == height)
        return true;

    // Release previous FBO resources
    if (m_hdrColorTex) { glDeleteTextures(1, &m_hdrColorTex);      m_hdrColorTex = 0; }
    if (m_hdrDepthTex) { glDeleteTextures(1, &m_hdrDepthTex);      m_hdrDepthTex = 0; }
    if (m_hdrFbo)      { glDeleteFramebuffers(1, &m_hdrFbo);       m_hdrFbo = 0; }

    m_hdrWidth  = width;
    m_hdrHeight = height;

    glGenFramebuffers(1, &m_hdrFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFbo);

    // RGBA16F colour attachment (HDR capable)
    glGenTextures(1, &m_hdrColorTex);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hdrColorTex, 0);

    // Depth texture (sampleable for SSAO)
    glGenTextures(1, &m_hdrDepthTex);
    glBindTexture(GL_TEXTURE_2D, m_hdrDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_hdrDepthTex, 0);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "PostProcessStack: HDR FBO incomplete (status " + std::to_string(status) + ")",
            Logger::LogLevel::ERROR);
        if (m_hdrColorTex) { glDeleteTextures(1, &m_hdrColorTex);      m_hdrColorTex = 0; }
        if (m_hdrDepthTex) { glDeleteTextures(1, &m_hdrDepthTex);      m_hdrDepthTex = 0; }
        if (m_hdrFbo)      { glDeleteFramebuffers(1, &m_hdrFbo);       m_hdrFbo = 0; }
        m_hdrWidth = 0;
        m_hdrHeight = 0;
        return false;
    }

    return true;
}

void PostProcessStack::bindHdrTarget()
{
    // When MSAA is active, render into the multisampled FBO
    if (m_msaaFbo != 0 && m_aaMode >= 2)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);
        return;
    }
    if (m_hdrFbo != 0)
        glBindFramebuffer(GL_FRAMEBUFFER, m_hdrFbo);
}

void PostProcessStack::execute(GLuint dstFbo, int vpX, int vpY, int vpW, int vpH)
{
    if (!m_initialized || m_hdrFbo == 0 || m_resolveProgram == 0)
        return;

    // Resolve MSAA → HDR texture when multisampling is active
    if (m_msaaFbo != 0 && m_aaMode >= 2)
    {
        resolveMsaaToHdr();
    }

    // Run SSAO pass if enabled (produces blurred AO texture)
    const bool doSsao = m_ssaoEnabled && m_ssaoProgram != 0 && m_ssaoBlurProgram != 0;
    if (doSsao)
    {
        executeSsaoPass();
    }

    // Run bloom pass if enabled (produces blurred bloom texture)
    const bool doBloom = m_bloomEnabled && m_bloomDownsampleProgram != 0 && m_bloomBlurProgram != 0;
    if (doBloom)
    {
        executeBloomPass();
    }

    // Bind destination framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    glViewport(vpX, vpY, vpW, vpH);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glUseProgram(m_resolveProgram);

    // Bind HDR colour texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    glUniform1i(m_resolveLocScene, 0);

    // Bind bloom texture to unit 1
    if (m_resolveLocBloomTexture >= 0)
    {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, (doBloom && m_bloomMipTextures[0] != 0) ? m_bloomMipTextures[0] : 0);
        glUniform1i(m_resolveLocBloomTexture, 1);
    }
    if (m_resolveLocBloomEnabled >= 0)
        glUniform1i(m_resolveLocBloomEnabled, doBloom ? 1 : 0);
    if (m_resolveLocBloomIntensity >= 0)
        glUniform1f(m_resolveLocBloomIntensity, m_bloomIntensity);

    // Bind SSAO blurred texture to unit 2
    if (m_resolveLocSsaoTexture >= 0)
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, (doSsao && m_ssaoBlurTex != 0) ? m_ssaoBlurTex : 0);
        glUniform1i(m_resolveLocSsaoTexture, 2);
    }
    if (m_resolveLocSsaoEnabled >= 0)
        glUniform1i(m_resolveLocSsaoEnabled, doSsao ? 1 : 0);

    // Post-process parameters
    if (m_resolveLocGamma >= 0)
        glUniform1i(m_resolveLocGamma, m_gammaEnabled ? 1 : 0);
    if (m_resolveLocToneMapping >= 0)
        glUniform1i(m_resolveLocToneMapping, m_toneMappingEnabled ? 1 : 0);
    if (m_resolveLocExposure >= 0)
        glUniform1f(m_resolveLocExposure, m_exposure);
    // AA mode: 0=None, 1=FXAA (post-process), 2+=MSAA (already resolved, no shader AA needed)
    if (m_resolveLocAAMode >= 0)
        glUniform1i(m_resolveLocAAMode, m_aaMode);
    if (m_resolveLocTexelSize >= 0 && m_hdrWidth > 0 && m_hdrHeight > 0)
        glUniform2f(m_resolveLocTexelSize, 1.0f / static_cast<float>(m_hdrWidth), 1.0f / static_cast<float>(m_hdrHeight));

    // Draw fullscreen triangle
    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void PostProcessStack::shutdown()
{
    releaseBloomResources();
    releaseSsaoResources();
    releaseFxaaResources();
    releaseMsaaFbo();
    if (m_bloomDownsampleProgram) { glDeleteProgram(m_bloomDownsampleProgram); m_bloomDownsampleProgram = 0; }
    if (m_bloomBlurProgram)       { glDeleteProgram(m_bloomBlurProgram);       m_bloomBlurProgram = 0; }
    if (m_ssaoProgram)            { glDeleteProgram(m_ssaoProgram);            m_ssaoProgram = 0; }
    if (m_ssaoBlurProgram)        { glDeleteProgram(m_ssaoBlurProgram);        m_ssaoBlurProgram = 0; }
    if (m_ssaoNoiseTex)           { glDeleteTextures(1, &m_ssaoNoiseTex);      m_ssaoNoiseTex = 0; }
    if (m_hdrColorTex) { glDeleteTextures(1, &m_hdrColorTex);      m_hdrColorTex = 0; }
    if (m_hdrDepthTex) { glDeleteTextures(1, &m_hdrDepthTex);      m_hdrDepthTex = 0; }
    if (m_hdrFbo)      { glDeleteFramebuffers(1, &m_hdrFbo);       m_hdrFbo = 0; }
    if (m_fullscreenVao) { glDeleteVertexArrays(1, &m_fullscreenVao); m_fullscreenVao = 0; }
    if (m_resolveProgram) { glDeleteProgram(m_resolveProgram); m_resolveProgram = 0; }
    m_hdrWidth = 0;
    m_hdrHeight = 0;
    m_initialized = false;
}

bool PostProcessStack::compileResolveProgram(const char* vertPath, const char* fragPath)
{
    OpenGLShader vertShader;
    if (!vertShader.loadFromFile(Shader::Type::Vertex, vertPath))
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "PostProcessStack: failed to load resolve vertex shader: " + std::string(vertPath),
            Logger::LogLevel::ERROR);
        return false;
    }

    OpenGLShader fragShader;
    if (!fragShader.loadFromFile(Shader::Type::Fragment, fragPath))
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "PostProcessStack: failed to load resolve fragment shader: " + std::string(fragPath),
            Logger::LogLevel::ERROR);
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vertShader.id());
    glAttachShader(prog, fragShader.id());
    glLinkProgram(prog);

    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        Logger::Instance().log(Logger::Category::Rendering,
            std::string("PostProcessStack: resolve program link failed: ") + log,
            Logger::LogLevel::ERROR);
        glDeleteProgram(prog);
        return false;
    }

    m_resolveProgram        = prog;
    m_resolveLocScene       = glGetUniformLocation(prog, "uSceneTexture");
    m_resolveLocGamma       = glGetUniformLocation(prog, "uGammaEnabled");
    m_resolveLocToneMapping = glGetUniformLocation(prog, "uToneMappingEnabled");
    m_resolveLocExposure    = glGetUniformLocation(prog, "uExposure");
    m_resolveLocAAMode      = glGetUniformLocation(prog, "uAAMode");
    m_resolveLocTexelSize   = glGetUniformLocation(prog, "uTexelSize");
    m_resolveLocBloomEnabled   = glGetUniformLocation(prog, "uBloomEnabled");
    m_resolveLocBloomTexture   = glGetUniformLocation(prog, "uBloomTexture");
    m_resolveLocBloomIntensity = glGetUniformLocation(prog, "uBloomIntensity");
    m_resolveLocSsaoEnabled    = glGetUniformLocation(prog, "uSsaoEnabled");
    m_resolveLocSsaoTexture    = glGetUniformLocation(prog, "uSsaoTexture");
    return true;
}

bool PostProcessStack::initBloom(const char* vertPath, const char* downsampleFragPath, const char* blurFragPath)
{
    m_bloomVertPath = vertPath;
    m_bloomDownsampleFragPath = downsampleFragPath;
    m_bloomBlurFragPath = blurFragPath;
    return compileBloomPrograms(vertPath, downsampleFragPath, blurFragPath);
}

bool PostProcessStack::compileBloomPrograms(const char* vertPath, const char* downsampleFragPath, const char* blurFragPath)
{
    // Downsample (bright-pass + mip chain)
    {
        OpenGLShader vert;
        if (!vert.loadFromFile(Shader::Type::Vertex, vertPath)) return false;
        OpenGLShader frag;
        if (!frag.loadFromFile(Shader::Type::Fragment, downsampleFragPath)) return false;
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert.id());
        glAttachShader(prog, frag.id());
        glLinkProgram(prog);
        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked) { glDeleteProgram(prog); return false; }
        m_bloomDownsampleProgram = prog;
        m_bloomDSLocSource    = glGetUniformLocation(prog, "uSourceTexture");
        m_bloomDSLocThreshold = glGetUniformLocation(prog, "uThreshold");
        m_bloomDSLocPass      = glGetUniformLocation(prog, "uPass");
    }
    // Gaussian blur
    {
        OpenGLShader vert;
        if (!vert.loadFromFile(Shader::Type::Vertex, vertPath)) return false;
        OpenGLShader frag;
        if (!frag.loadFromFile(Shader::Type::Fragment, blurFragPath)) return false;
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert.id());
        glAttachShader(prog, frag.id());
        glLinkProgram(prog);
        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked) { glDeleteProgram(prog); return false; }
        m_bloomBlurProgram = prog;
        m_bloomBlurLocSource     = glGetUniformLocation(prog, "uSourceTexture");
        m_bloomBlurLocHorizontal = glGetUniformLocation(prog, "uHorizontal");
    }
    return true;
}

bool PostProcessStack::ensureBloomResources(int width, int height)
{
    if (width <= 0 || height <= 0) return false;
    if (m_bloomFbo != 0 && m_bloomAllocWidth == width && m_bloomAllocHeight == height)
        return true;

    releaseBloomResources();
    m_bloomAllocWidth  = width;
    m_bloomAllocHeight = height;

    glGenFramebuffers(1, &m_bloomFbo);

    int w = width / 2;
    int h = height / 2;
    for (int i = 0; i < kBloomMipCount; ++i)
    {
        w = std::max(w, 1);
        h = std::max(h, 1);
        m_bloomMipWidths[i]  = w;
        m_bloomMipHeights[i] = h;

        glGenTextures(1, &m_bloomMipTextures[i]);
        glBindTexture(GL_TEXTURE_2D, m_bloomMipTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        w /= 2;
        h /= 2;
    }

    // Ping-pong texture (same size as first mip for blur passes)
    glGenTextures(1, &m_bloomPingPongTex);
    glBindTexture(GL_TEXTURE_2D, m_bloomPingPongTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_bloomMipWidths[0], m_bloomMipHeights[0], 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void PostProcessStack::releaseBloomResources()
{
    for (int i = 0; i < kBloomMipCount; ++i)
    {
        if (m_bloomMipTextures[i]) { glDeleteTextures(1, &m_bloomMipTextures[i]); m_bloomMipTextures[i] = 0; }
    }
    if (m_bloomPingPongTex) { glDeleteTextures(1, &m_bloomPingPongTex); m_bloomPingPongTex = 0; }
    if (m_bloomFbo) { glDeleteFramebuffers(1, &m_bloomFbo); m_bloomFbo = 0; }
    m_bloomAllocWidth  = 0;
    m_bloomAllocHeight = 0;
}

void PostProcessStack::executeBloomPass()
{
    if (!ensureBloomResources(m_hdrWidth, m_hdrHeight))
        return;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbo);
    glBindVertexArray(m_fullscreenVao);

    // --- Pass 0: bright-pass extract from HDR into mip 0 ---
    glUseProgram(m_bloomDownsampleProgram);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomMipTextures[0], 0);
    glViewport(0, 0, m_bloomMipWidths[0], m_bloomMipHeights[0]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrColorTex);
    if (m_bloomDSLocSource >= 0)    glUniform1i(m_bloomDSLocSource, 0);
    if (m_bloomDSLocThreshold >= 0) glUniform1f(m_bloomDSLocThreshold, m_bloomThreshold);
    if (m_bloomDSLocPass >= 0)      glUniform1i(m_bloomDSLocPass, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Passes 1..N-1: progressive downsample ---
    for (int i = 1; i < kBloomMipCount; ++i)
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomMipTextures[i], 0);
        glViewport(0, 0, m_bloomMipWidths[i], m_bloomMipHeights[i]);
        glBindTexture(GL_TEXTURE_2D, m_bloomMipTextures[i - 1]);
        if (m_bloomDSLocPass >= 0) glUniform1i(m_bloomDSLocPass, i);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // --- Upsample + blur: walk back from smallest mip to mip 0 ---
    glUseProgram(m_bloomBlurProgram);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // additive blend for upsample accumulation

    for (int i = kBloomMipCount - 1; i >= 0; --i)
    {
        const int w = m_bloomMipWidths[i];
        const int h = m_bloomMipHeights[i];

        // Horizontal blur: mip[i] -> pingPong
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomPingPongTex, 0);
        // Resize ping-pong to match current mip level
        glBindTexture(GL_TEXTURE_2D, m_bloomPingPongTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glViewport(0, 0, w, h);
        glBindTexture(GL_TEXTURE_2D, m_bloomMipTextures[i]);
        if (m_bloomBlurLocSource >= 0)     glUniform1i(m_bloomBlurLocSource, 0);
        if (m_bloomBlurLocHorizontal >= 0) glUniform1i(m_bloomBlurLocHorizontal, 1);
        glDisable(GL_BLEND); // first blur pass: overwrite
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Vertical blur: pingPong -> mip[i]
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomMipTextures[i], 0);
        glBindTexture(GL_TEXTURE_2D, m_bloomPingPongTex);
        if (m_bloomBlurLocHorizontal >= 0) glUniform1i(m_bloomBlurLocHorizontal, 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // Additive upsample: blend smaller mip into the next larger one
        if (i > 0)
        {
            glEnable(GL_BLEND);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomMipTextures[i - 1], 0);
            glViewport(0, 0, m_bloomMipWidths[i - 1], m_bloomMipHeights[i - 1]);
            glBindTexture(GL_TEXTURE_2D, m_bloomMipTextures[i]);
            if (m_bloomBlurLocHorizontal >= 0) glUniform1i(m_bloomBlurLocHorizontal, 1);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// MSAA FBO management
// ---------------------------------------------------------------------------

bool PostProcessStack::ensureMsaaFbo(int width, int height, int samples)
{
    if (samples <= 1)
    {
        releaseMsaaFbo();
        return true;
    }

    if (m_msaaFbo != 0 && m_msaaWidth == width && m_msaaHeight == height && m_msaaSamples == samples)
        return true;

    releaseMsaaFbo();

    m_msaaWidth   = width;
    m_msaaHeight  = height;
    m_msaaSamples = samples;

    glGenFramebuffers(1, &m_msaaFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFbo);

    // Multisampled colour renderbuffer (RGBA16F for HDR)
    glGenRenderbuffers(1, &m_msaaColorRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRbo);

    // Multisampled depth+stencil renderbuffer
    glGenRenderbuffers(1, &m_msaaDepthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRbo);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRbo);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::Instance().log(Logger::Category::Rendering,
            "PostProcessStack: MSAA FBO incomplete (status " + std::to_string(status) +
            ", samples=" + std::to_string(samples) + ")",
            Logger::LogLevel::ERROR);
        releaseMsaaFbo();
        return false;
    }

    return true;
}

void PostProcessStack::releaseMsaaFbo()
{
    if (m_msaaColorRbo) { glDeleteRenderbuffers(1, &m_msaaColorRbo); m_msaaColorRbo = 0; }
    if (m_msaaDepthRbo) { glDeleteRenderbuffers(1, &m_msaaDepthRbo); m_msaaDepthRbo = 0; }
    if (m_msaaFbo)      { glDeleteFramebuffers(1, &m_msaaFbo);       m_msaaFbo = 0; }
    m_msaaWidth = 0;
    m_msaaHeight = 0;
    m_msaaSamples = 0;
}

void PostProcessStack::resolveMsaaToHdr()
{
    if (m_msaaFbo == 0 || m_hdrFbo == 0)
        return;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_hdrFbo);
    glBlitFramebuffer(0, 0, m_msaaWidth, m_msaaHeight,
                      0, 0, m_hdrWidth, m_hdrHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// SSAO
// ---------------------------------------------------------------------------

bool PostProcessStack::initSsao(const char* vertPath, const char* ssaoFragPath, const char* blurFragPath)
{
    m_ssaoVertPath = vertPath;
    m_ssaoFragPath = ssaoFragPath;
    m_ssaoBlurFragPath = blurFragPath;
    if (!compileSsaoPrograms(vertPath, ssaoFragPath, blurFragPath))
        return false;
    generateSsaoKernel();
    generateSsaoNoiseTexture();
    return true;
}

bool PostProcessStack::compileSsaoPrograms(const char* vertPath, const char* ssaoFragPath, const char* blurFragPath)
{
    // SSAO main program
    {
        OpenGLShader vert;
        if (!vert.loadFromFile(Shader::Type::Vertex, vertPath)) return false;
        OpenGLShader frag;
        if (!frag.loadFromFile(Shader::Type::Fragment, ssaoFragPath)) return false;
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert.id());
        glAttachShader(prog, frag.id());
        glLinkProgram(prog);
        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char log[512];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            Logger::Instance().log(Logger::Category::Rendering,
                std::string("PostProcessStack: SSAO program link failed: ") + log,
                Logger::LogLevel::ERROR);
            glDeleteProgram(prog);
            return false;
        }
        m_ssaoProgram = prog;
        m_ssaoLocDepth          = glGetUniformLocation(prog, "uDepthTexture");
        m_ssaoLocNoise          = glGetUniformLocation(prog, "uNoiseTexture");
        m_ssaoLocProjection     = glGetUniformLocation(prog, "uProjection");
        m_ssaoLocInvProjection  = glGetUniformLocation(prog, "uInvProjection");
        m_ssaoLocNoiseScale     = glGetUniformLocation(prog, "uNoiseScale");
        m_ssaoLocRadius         = glGetUniformLocation(prog, "uRadius");
        m_ssaoLocBias           = glGetUniformLocation(prog, "uBias");
        m_ssaoLocPower          = glGetUniformLocation(prog, "uPower");
        m_ssaoLocSamples        = glGetUniformLocation(prog, "uSamples[0]");
    }
    // SSAO blur program
    {
        OpenGLShader vert;
        if (!vert.loadFromFile(Shader::Type::Vertex, vertPath)) return false;
        OpenGLShader frag;
        if (!frag.loadFromFile(Shader::Type::Fragment, blurFragPath)) return false;
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert.id());
        glAttachShader(prog, frag.id());
        glLinkProgram(prog);
        GLint linked = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            char log[512];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            Logger::Instance().log(Logger::Category::Rendering,
                std::string("PostProcessStack: SSAO blur program link failed: ") + log,
                Logger::LogLevel::ERROR);
            glDeleteProgram(prog);
            return false;
        }
        m_ssaoBlurProgram    = prog;
        m_ssaoBlurLocInput   = glGetUniformLocation(prog, "uSsaoInput");
        m_ssaoBlurLocDepth   = glGetUniformLocation(prog, "uDepthTexture");
    }
    return true;
}

void PostProcessStack::generateSsaoKernel()
{
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::default_random_engine gen(42);
    m_ssaoKernel.resize(kSsaoKernelSize);

    for (int i = 0; i < kSsaoKernelSize; ++i)
    {
        glm::vec3 sample(
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f,
            dist(gen)               // hemisphere: z in [0,1]
        );
        sample = glm::normalize(sample) * dist(gen);

        // Accelerating interpolation: more samples close to the origin
        float scale = static_cast<float>(i) / static_cast<float>(kSsaoKernelSize);
        scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, scale*scale)
        sample *= scale;

        m_ssaoKernel[i] = sample;
    }
}

void PostProcessStack::generateSsaoNoiseTexture()
{
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::default_random_engine gen(123);

    constexpr int noiseSize = 4; // 4x4 rotation vectors
    std::vector<glm::vec3> noise(noiseSize * noiseSize);
    for (auto& n : noise)
    {
        n = glm::vec3(
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f,
            0.0f
        );
    }

    glGenTextures(1, &m_ssaoNoiseTex);
    glBindTexture(GL_TEXTURE_2D, m_ssaoNoiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, noiseSize, noiseSize, 0, GL_RGB, GL_FLOAT, noise.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool PostProcessStack::ensureSsaoResources(int width, int height)
{
    // Half resolution
    const int w = width / 2;
    const int h = height / 2;
    if (w <= 0 || h <= 0) return false;
    if (m_ssaoFbo != 0 && m_ssaoAllocW == w && m_ssaoAllocH == h)
        return true;

    releaseSsaoResources();
    m_ssaoAllocW = w;
    m_ssaoAllocH = h;

    // SSAO FBO + R8 color
    glGenFramebuffers(1, &m_ssaoFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);

    glGenTextures(1, &m_ssaoColorTex);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoColorTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        releaseSsaoResources();
        return false;
    }

    // Blur FBO + R8 color
    glGenFramebuffers(1, &m_ssaoBlurFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFbo);

    glGenTextures(1, &m_ssaoBlurTex);
    glBindTexture(GL_TEXTURE_2D, m_ssaoBlurTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoBlurTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        releaseSsaoResources();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void PostProcessStack::releaseSsaoResources()
{
    if (m_ssaoColorTex) { glDeleteTextures(1, &m_ssaoColorTex); m_ssaoColorTex = 0; }
    if (m_ssaoBlurTex)  { glDeleteTextures(1, &m_ssaoBlurTex);  m_ssaoBlurTex = 0; }
    if (m_ssaoFbo)      { glDeleteFramebuffers(1, &m_ssaoFbo);  m_ssaoFbo = 0; }
    if (m_ssaoBlurFbo)  { glDeleteFramebuffers(1, &m_ssaoBlurFbo); m_ssaoBlurFbo = 0; }
    m_ssaoAllocW = 0;
    m_ssaoAllocH = 0;
}

void PostProcessStack::executeSsaoPass()
{
    if (!ensureSsaoResources(m_hdrWidth, m_hdrHeight))
        return;

    const int halfW = m_ssaoAllocW;
    const int halfH = m_ssaoAllocH;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_fullscreenVao);

    // --- SSAO pass: render AO into m_ssaoColorTex ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    glViewport(0, 0, halfW, halfH);
    glUseProgram(m_ssaoProgram);

    // Bind depth texture to unit 0
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hdrDepthTex);
    if (m_ssaoLocDepth >= 0) glUniform1i(m_ssaoLocDepth, 0);

    // Bind noise texture to unit 1
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_ssaoNoiseTex);
    if (m_ssaoLocNoise >= 0) glUniform1i(m_ssaoLocNoise, 1);

    // Projection matrices
    if (m_ssaoLocProjection >= 0)
        glUniformMatrix4fv(m_ssaoLocProjection, 1, GL_FALSE, &m_projection[0][0]);

    glm::mat4 invProj = glm::inverse(m_projection);
    if (m_ssaoLocInvProjection >= 0)
        glUniformMatrix4fv(m_ssaoLocInvProjection, 1, GL_FALSE, &invProj[0][0]);

    // Noise scale (viewport / noise texture size = half-res / 4)
    if (m_ssaoLocNoiseScale >= 0)
        glUniform2f(m_ssaoLocNoiseScale, static_cast<float>(halfW) / 4.0f, static_cast<float>(halfH) / 4.0f);

    if (m_ssaoLocRadius >= 0) glUniform1f(m_ssaoLocRadius, m_ssaoRadius);
    if (m_ssaoLocBias >= 0)   glUniform1f(m_ssaoLocBias, 0.025f);
    if (m_ssaoLocPower >= 0)  glUniform1f(m_ssaoLocPower, m_ssaoPower);

    // Upload kernel samples
    if (m_ssaoLocSamples >= 0 && !m_ssaoKernel.empty())
    {
        glUniform3fv(m_ssaoLocSamples, kSsaoKernelSize, &m_ssaoKernel[0].x);
    }

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // --- Blur pass: m_ssaoColorTex -> m_ssaoBlurTex ---
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFbo);
    glViewport(0, 0, halfW, halfH);
    glUseProgram(m_ssaoBlurProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorTex);
    if (m_ssaoBlurLocInput >= 0) glUniform1i(m_ssaoBlurLocInput, 0);

    // Bind depth texture for bilateral blur (depth-aware edge rejection)
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_hdrDepthTex);
    if (m_ssaoBlurLocDepth >= 0) glUniform1i(m_ssaoBlurLocDepth, 1);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Cleanup
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// FXAA deferred pass (applied after overlays / gizmos)
// ---------------------------------------------------------------------------

bool PostProcessStack::ensureFxaaResources(int width, int height)
{
    if (m_fxaaTempFbo != 0 && m_fxaaAllocW == width && m_fxaaAllocH == height)
        return true;

    releaseFxaaResources();

    glGenFramebuffers(1, &m_fxaaTempFbo);
    glGenTextures(1, &m_fxaaTempTex);

    glBindTexture(GL_TEXTURE_2D, m_fxaaTempTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fxaaTempFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fxaaTempTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        releaseFxaaResources();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_fxaaAllocW = width;
    m_fxaaAllocH = height;
    return true;
}

void PostProcessStack::releaseFxaaResources()
{
    if (m_fxaaTempTex) { glDeleteTextures(1, &m_fxaaTempTex); m_fxaaTempTex = 0; }
    if (m_fxaaTempFbo) { glDeleteFramebuffers(1, &m_fxaaTempFbo); m_fxaaTempFbo = 0; }
    m_fxaaAllocW = 0;
    m_fxaaAllocH = 0;
}

void PostProcessStack::executeFxaaPass(GLuint dstFbo, int vpX, int vpY, int vpW, int vpH)
{
    if (!m_initialized || m_resolveProgram == 0 || m_aaMode != 1)
        return;

    if (!ensureFxaaResources(vpW, vpH))
        return;

    // Copy current dstFbo content to temp texture via blit
    glBindFramebuffer(GL_READ_FRAMEBUFFER, dstFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fxaaTempFbo);
    glBlitFramebuffer(vpX, vpY, vpX + vpW, vpY + vpH,
                      0, 0, vpW, vpH,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    // Render FXAA from temp texture back to dstFbo
    glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
    glViewport(vpX, vpY, vpW, vpH);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glUseProgram(m_resolveProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fxaaTempTex);
    if (m_resolveLocScene >= 0)         glUniform1i(m_resolveLocScene, 0);
    if (m_resolveLocAAMode >= 0)        glUniform1i(m_resolveLocAAMode, 1); // FXAA
    if (m_resolveLocGamma >= 0)         glUniform1i(m_resolveLocGamma, 0);
    if (m_resolveLocToneMapping >= 0)   glUniform1i(m_resolveLocToneMapping, 0);
    if (m_resolveLocBloomEnabled >= 0)  glUniform1i(m_resolveLocBloomEnabled, 0);
    if (m_resolveLocSsaoEnabled >= 0)   glUniform1i(m_resolveLocSsaoEnabled, 0);
    if (m_resolveLocExposure >= 0)      glUniform1f(m_resolveLocExposure, 1.0f);
    if (m_resolveLocTexelSize >= 0 && vpW > 0 && vpH > 0)
        glUniform2f(m_resolveLocTexelSize, 1.0f / static_cast<float>(vpW), 1.0f / static_cast<float>(vpH));

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// ---------------------------------------------------------------------------
// reloadPrograms – re-compile all shader programs from stored file paths
// ---------------------------------------------------------------------------
bool PostProcessStack::reloadPrograms()
{
    if (!m_initialized)
        return false;

    auto& logger = Logger::Instance();
    bool anyOk = false;

    // Resolve program
    if (!m_resolveVertPath.empty() && !m_resolveFragPath.empty())
    {
        if (m_resolveProgram)
        {
            glDeleteProgram(m_resolveProgram);
            m_resolveProgram = 0;
        }
        if (compileResolveProgram(m_resolveVertPath.c_str(), m_resolveFragPath.c_str()))
        {
            anyOk = true;
        }
        else
        {
            logger.log(Logger::Category::Rendering,
                "ShaderHotReload: failed to reload resolve program",
                Logger::LogLevel::WARNING);
        }
    }

    // Bloom programs
    if (!m_bloomVertPath.empty() && !m_bloomDownsampleFragPath.empty() && !m_bloomBlurFragPath.empty())
    {
        if (m_bloomDownsampleProgram)
        {
            glDeleteProgram(m_bloomDownsampleProgram);
            m_bloomDownsampleProgram = 0;
        }
        if (m_bloomBlurProgram)
        {
            glDeleteProgram(m_bloomBlurProgram);
            m_bloomBlurProgram = 0;
        }
        if (compileBloomPrograms(m_bloomVertPath.c_str(), m_bloomDownsampleFragPath.c_str(), m_bloomBlurFragPath.c_str()))
        {
            anyOk = true;
        }
        else
        {
            logger.log(Logger::Category::Rendering,
                "ShaderHotReload: failed to reload bloom programs",
                Logger::LogLevel::WARNING);
        }
    }

    // SSAO programs
    if (!m_ssaoVertPath.empty() && !m_ssaoFragPath.empty() && !m_ssaoBlurFragPath.empty())
    {
        if (m_ssaoProgram)
        {
            glDeleteProgram(m_ssaoProgram);
            m_ssaoProgram = 0;
        }
        if (m_ssaoBlurProgram)
        {
            glDeleteProgram(m_ssaoBlurProgram);
            m_ssaoBlurProgram = 0;
        }
        if (compileSsaoPrograms(m_ssaoVertPath.c_str(), m_ssaoFragPath.c_str(), m_ssaoBlurFragPath.c_str()))
        {
            anyOk = true;
        }
        else
        {
            logger.log(Logger::Category::Rendering,
                "ShaderHotReload: failed to reload SSAO programs",
                Logger::LogLevel::WARNING);
        }
    }

    return anyOk;
}
