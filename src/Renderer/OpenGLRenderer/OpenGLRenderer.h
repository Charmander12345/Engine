#pragma once

#include "../Renderer.h"
#include "../Camera.h"
#include "../RenderResourceManager.h"
#include "../UIManager.h"
#include "../ViewportUIManager.h"
#include "../EditorWindows/PopupWindow.h"
#include "../EditorWindows/MeshViewerWindow.h"
#include "../EditorWindows/MaterialEditorWindow.h"
#include "../EditorWindows/TextureViewerWindow.h"
#include "../IRenderTarget.h"
#include "OpenGLRenderTarget.h"
#include "PostProcessStack.h"
#include "ShaderHotReload.h"
#include "ParticleSystem.h"
#include "TextureStreamingManager.h"

#include "../../Core/MathTypes.h"
#include "../../Core/EngineLevel.h"
#include "../../Core/SkeletalData.h"
#include "glad/include/gl.h"
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <array>
#include <glm/glm.hpp>

#include "../../Core/ECS/Components.h"
#include "OpenGLMaterial.h"

class OpenGLObject2D;
class OpenGLObject3D;
class OpenGLTextRenderer;

struct WindowHitTestContext
{
    int titlebarHeight{ 50 };
    int resizeBorder{ 6 };
    int buttonStripWidth{ 140 };
    int buttonStripHeight{ 50 };
    bool buttonsOnLeft{ false };
};

struct EditorTab
{
    std::string id;
    std::string name;
    bool closable{ true };
    bool active{ false };
    std::unique_ptr<IRenderTarget> renderTarget;
};

class OpenGLRenderer : public Renderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool initialize() override;
    void shutdown() override;
    void clear() override;
    void render() override;
    void present() override;
    const std::string& name() const override;

    RendererCapabilities getCapabilities() const override;

    double getLastGpuFrameMs() const override { return m_lastGpuFrameMs; }
    double getLastCpuRenderWorldMs() const override { return m_cpuRenderWorldMs; }
    double getLastCpuRenderUiMs() const override { return m_cpuRenderUiMs; }
    double getLastCpuUiLayoutMs() const override { return m_cpuUiLayoutMs; }
    double getLastCpuUiDrawMs() const override { return m_cpuUiDrawMs; }
    double getLastCpuEcsMs() const override { return m_cpuEcsMs; }

    SDL_Window* window() const override;

    void moveCamera(float forward, float right, float up) override;
    void rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees) override;
    Vec3 getCameraPosition() const override;
    void setCameraPosition(const Vec3& position) override;
    Vec2 getCameraRotationDegrees() const override;
    void setCameraRotationDegrees(float yawDegrees, float pitchDegrees) override;

    void startCameraTransition(const Vec3& targetPos, float targetYaw, float targetPitch, float durationSec) override;
    bool isCameraTransitioning() const override;
    void cancelCameraTransition() override;

    void startCameraPath(const std::vector<CameraPathPoint>& points, float duration, bool loop) override;
    bool isCameraPathPlaying() const override;
    void pauseCameraPath() override;
    void resumeCameraPath() override;
    void stopCameraPath() override;
    float getCameraPathProgress() const override;
    std::vector<CameraPathPoint> getCameraPathPoints() const override;
    void setCameraPathPoints(const std::vector<CameraPathPoint>& pts) override;
    float getCameraPathDuration() const override;
    void setCameraPathDuration(float d) override;
    bool getCameraPathLoop() const override;
    void setCameraPathLoop(bool l) override;

    void setActiveCameraEntity(unsigned int entity) override;
    unsigned int getActiveCameraEntity() const override;
    void clearActiveCameraEntity() override;

    void setClearColor(const Vec4& color) override;
    Vec4 getClearColor() const override;

    void queueText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color) override;
    Vec2 getViewportSize() const override;
    UIManager& getUIManager() override;
    const UIManager& getUIManager() const override;
    ViewportUIManager* getViewportUIManagerPtr() override { return &m_viewportUIManager; }
    const ViewportUIManager* getViewportUIManagerPtr() const override { return &m_viewportUIManager; }
    ViewportUIManager& getViewportUIManager() { return m_viewportUIManager; }
    TextureStreamingManager& getTextureStreamingManager() { return m_textureStreaming; }
    const ViewportUIManager& getViewportUIManager() const { return m_viewportUIManager; }
    std::shared_ptr<Widget> createWidgetFromAsset(const std::shared_ptr<AssetData>& asset) override;
    unsigned int preloadUITexture(const std::string& path) override;
    void requestShaderReload() override;
    unsigned int generateAssetThumbnail(const std::string& assetPath, int assetType) override;

    void addTab(const std::string& id, const std::string& name, bool closable) override;
    void removeTab(const std::string& id) override;
    void setActiveTab(const std::string& id) override;
    const std::string& getActiveTabId() const override;
    const std::vector<EditorTab>& getTabs() const;
    void cleanupWidgetEditorPreview(const std::string& tabId) override;

    unsigned int pickEntityAt(int x, int y) override;

    // Re-renders the pick buffer with the latest view/projection, then picks.
    unsigned int pickEntityAtImmediate(int x, int y) override;

    // Unproject a screen pixel to world space using the depth buffer.
    // Returns false if the depth at that pixel is at the far plane (no geometry hit).
    bool screenToWorldPos(int screenX, int screenY, Vec3& outWorldPos) const override;

    // Refresh a single entity's render data without rebuilding the entire scene.
    void refreshEntity(ECS::Entity entity) override;

    // Multi-Viewport overrides (Phase 11.1)
    SubViewportCamera getSubViewportCamera(int index) const override;
    void setSubViewportCamera(int index, const SubViewportCamera& cam) override;
    int subViewportHitTest(int screenX, int screenY) const override;

private:
    void renderWorld();
    void renderViewportUI();
    void renderUI();
    bool ensureUIQuadRenderer();
    GLuint getUIQuadProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    void ensureUIShaderDefaults();
    void rebuildTitleBarTabs();
    const std::string& resolveUIShaderPath(const std::string& value, const std::string& fallback);
    bool ensureUiFbo(int width, int height);
    void releaseUiFbo();
    void blitUiCache(int width, int height);
    bool ensureHzbResources(int width, int height);
    void releaseHzbResources();
    void buildHzb();
    bool testAabbAgainstHzb(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const glm::mat4& viewProj) const;
    bool ensureBoundsDebugResources();
    void releaseBoundsDebugResources();
    void drawBoundsDebugBox(const glm::vec3& center, const glm::vec3& extent, const glm::mat4& viewProj);
    void rebuildHeightFieldDebugMesh();
    void renderHeightFieldDebug(const glm::mat4& viewProj);
    void releaseHeightFieldDebugResources();
    bool ensureDisplacementResources();
    void releaseDisplacementResources();
    bool ensurePickFbo(int width, int height);
    void releasePickFbo();
    void renderPickBuffer(const glm::mat4& view, const glm::mat4& projection);
    void renderPickBufferSelectedEntities(const glm::mat4& view, const glm::mat4& projection, const std::unordered_set<unsigned int>& entityIds);
    bool ensureOutlineResources();
    void releaseOutlineResources();
    void drawUIPanel(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program,
        const Vec4& hoverColor = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }, float hoverT = 0.0f, float borderRadius = 0.0f);
    void drawUIBrush(float x0, float y0, float x1, float y1, const UIBrush& brush, const glm::mat4& projection, float opacity = 1.0f, float hoverT = 0.0f, const UIBrush* hoverBrush = nullptr, float borderRadius = 0.0f);
    void drawUIImage(float x0, float y0, float x1, float y1, GLuint textureId, const glm::mat4& projection, const Vec4& tintColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, bool invertRGB = false, bool flipY = false);
    GLuint getOrLoadUITexture(const std::string& path);
    bool ensureThumbnailFbo(int size);
    void releaseThumbnailFbo();
    void drawUIOutline(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program);
    void drawUIShadow(float x0, float y0, float x1, float y1, const Vec4& shadowColor, const Vec2& shadowOffset, const glm::mat4& projection, GLuint program, float borderRadius = 0.0f, float blurRadius = 6.0f);

    struct RenderEntry
    {
        ECS::Entity entity{ 0 };
        ECS::TransformComponent transform{};
        std::shared_ptr<OpenGLObject2D> object2D;
        std::shared_ptr<OpenGLObject3D> object3D;
        glm::mat4 cachedModelMatrix{1.0f};

        // LOD variants (sorted by ascending maxDistance; empty = no LOD)
        struct LodVariant
        {
            std::shared_ptr<OpenGLObject3D> object3D;
            float maxDistance{0.0f}; // 0 = fallback
        };
        std::vector<LodVariant> lodLevels;
    };

    bool isRenderEntryRelevant(const RenderEntry& entry) const;

    struct TextCommand
    {
        std::string text;
        Vec2 screenPos;
        float scale{1.0f};
        Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    bool m_initialized;
    std::string m_name;
    SDL_Window* m_window;
    SDL_GLContext m_glContext;

    std::unique_ptr<Camera> m_camera;
    unsigned int m_activeCameraEntity{ 0 };
    CameraTransition m_cameraTransition;
    uint64_t m_lastTransitionTick{0};

    CameraPath m_cameraPath;
    float m_cameraPathElapsed{0.0f};
    bool m_cameraPathActive{false};
    bool m_cameraPathPaused{false};
    uint64_t m_lastPathTick{0};

    // Multi-Viewport sub-cameras (Phase 11.1) – up to 4 sub-viewports
    static constexpr int kMaxSubViewports = 4;
    SubViewportCamera m_subViewportCameras[kMaxSubViewports];
    bool m_subViewportCamerasInitialized{false};
    void ensureSubViewportCameras();

    ParticleSystem m_particleSystem;
    uint64_t m_lastParticleTick{0};
    glm::mat4 m_projectionMatrix;
    glm::mat4 m_lastViewMatrix{ 1.0f };
    std::vector<RenderEntry> m_renderEntries;
    std::vector<RenderEntry> m_meshEntries;
    RenderResourceManager m_resourceManager;
    EngineLevel* m_cachedLevel{ nullptr };
    bool m_restoreCameraOnPrepare{ false };
    Vec4 m_clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    UIManager m_uiManager;
    ViewportUIManager m_viewportUIManager;

    std::shared_ptr<OpenGLTextRenderer> m_textRenderer;
    std::vector<TextCommand> m_textQueue;


    GLuint m_uiQuadProgram{0};
    GLuint m_uiQuadVao{0};
    GLuint m_uiQuadVbo{0};
    std::unordered_map<std::string, GLuint> m_uiQuadPrograms;
    std::unordered_map<std::string, std::string> m_uiShaderPathCache;
    std::string m_uiShaderBaseDir;
    std::string m_defaultPanelVertex;
    std::string m_defaultPanelFragment;
    std::string m_defaultButtonVertex;
    std::string m_defaultButtonFragment;
    std::string m_defaultTextVertex;
    std::string m_defaultTextFragment;
    bool m_uiShaderDefaultsInitialized{false};
    GLuint m_uiImageProgram{0};
    GLuint m_uiGradientProgram{0};
    // Cached uniform locations per shader program
    struct UIPanelUniforms
    {
        GLint projection{ -1 };
        GLint color{ -1 };
        GLint borderColor{ -1 };
        GLint borderSize{ -1 };
        GLint rect{ -1 };
        GLint viewportSize{ -1 };
        GLint hoverColor{ -1 };
        GLint isHovered{ -1 };
        GLint borderRadiusLoc{ -1 };
    };
    struct UIImageUniforms
    {
        GLint projection{ -1 };
        GLint rect{ -1 };
        GLint tintColor{ -1 };
        GLint invertRGB{ -1 };
        GLint flipY{ -1 };
        GLint texture{ -1 };
    };
    std::unordered_map<GLuint, UIPanelUniforms> m_uiPanelUniformCache;
    UIImageUniforms m_uiImageUniforms{};
    struct UIGradientUniforms
    {
        GLint projection{ -1 };
        GLint rect{ -1 };
        GLint colorStart{ -1 };
        GLint colorEnd{ -1 };
        GLint angle{ -1 };
    };
    UIGradientUniforms m_uiGradientUniforms{};
    std::unordered_map<std::string, GLuint> m_uiTextureCache;
    std::unordered_map<std::string, GLuint> m_thumbnailCache;
    GLuint m_thumbFbo{0};
    GLuint m_thumbColorTex{0};
    GLuint m_thumbDepthRbo{0};
    int m_thumbSize{0};
    ECS::Schema m_lightSchema{};
    bool m_lightSchemaInitialized{false};
    std::vector<OpenGLMaterial::LightData> m_sceneLights;

    // Multi-viewport render state (Phase 11.1)
    // When >= 0, renderWorld uses this sub-viewport's camera + scissor rect
    int m_currentSubViewportIndex{ -1 };
    struct SubViewportRect { int x, y, w, h; };
    SubViewportRect m_currentSubViewportRect{};
    void computeSubViewportRects(int vpX, int vpY, int vpW, int vpH,
                                 SubViewportRect* outRects, int count) const;
    int m_cachedWindowWidth{0};
    int m_cachedWindowHeight{0};
    uint64_t m_lastUiAnimationTickCounter{0};
    int m_lastProjectionWidth{0};
    int m_lastProjectionHeight{0};
    Vec4 m_cachedViewportContentRect{}; // {x, y, w, h} viewport area after editor panels dock
    bool m_uiDebugEnabled{false};
    bool m_uiDebugEnabledPrev{false};
    WindowHitTestContext m_hitTestContext{};

    GLuint m_uiFbo{0};
    GLuint m_uiFboTexture{0};
    int m_uiFboWidth{0};
    int m_uiFboHeight{0};
    bool m_uiFboCacheValid{false};

    struct HzbLevel
    {
        std::vector<float> data;
        int width{0};
        int height{0};
    };

    GLuint m_hzbTexture{0};
    GLuint m_hzbFbo{0};
    GLuint m_hzbDepthCopyFbo{0};
    GLuint m_hzbDepthTexture{0};
    GLuint m_hzbDownsampleProgram{0};
    GLuint m_hzbCopyProgram{0};
    GLuint m_hzbFullscreenVao{0};
    GLint m_hzbPrevMipLoc{-1};
    GLint m_hzbCopyDepthTexLoc{-1};
    GLint m_hzbDownsampleTexLoc{-1};
    int m_hzbWidth{0};
    int m_hzbHeight{0};
    int m_hzbMipLevels{0};
    bool m_hzbResourcesReady{false};
    std::vector<HzbLevel> m_hzbCpuData;
    static constexpr int kHzbPboCount = 2;
    std::array<GLuint, kHzbPboCount> m_hzbPbos{};
    int m_hzbPboIndex{0};
    size_t m_hzbPboSize{0};
    bool m_hzbPboReady{false};
    bool m_occlusionEnabled{true};
    uint32_t m_lastVisibleCount{0};
    uint32_t m_lastHiddenCount{0};
    uint32_t m_lastTotalCount{0};

    GLuint m_boundsDebugVao{0};
    GLuint m_boundsDebugVbo{0};
    GLuint m_boundsDebugProgram{0};
    GLsizei m_boundsDebugVertexCount{0};
    bool m_boundsDebugEnabled{false};

    // HeightField debug wireframe
    GLuint m_hfDebugVao{0};
    GLuint m_hfDebugVbo{0};
    GLuint m_hfDebugIbo{0};
    GLsizei m_hfDebugIndexCount{0};
    bool m_hfDebugEnabled{false};
    unsigned int m_hfDebugVersion{0};

    bool m_wireframeEnabled{false};
    bool m_vsyncEnabled{false};
    DebugRenderMode m_debugRenderMode{DebugRenderMode::Lit};

    // Post-processing pipeline
    PostProcessStack m_postProcessStack;
    bool m_postProcessEnabled{true};
    bool m_gammaEnabled{true};
    bool m_toneMappingEnabled{true};

    // Shader hot-reload
    ShaderHotReload m_shaderHotReload;
    void handleShaderHotReload();

    // Texture streaming
    TextureStreamingManager m_textureStreaming;

    // Displacement Mapping (Tessellation)
    bool  m_displacementEnabled{false};
    float m_displacementScale{0.5f};
    float m_tessLevel{16.0f};
    GLuint m_displacementProgram{0};  // Shader program with tess stages

    float m_exposure{1.0f};
    AntiAliasingMode m_aaMode{AntiAliasingMode::None};
    bool m_bloomEnabled{false};
    float m_bloomThreshold{1.0f};
    float m_bloomIntensity{0.3f};
    bool m_ssaoEnabled{false};
    bool m_fogEnabled{false};
    Vec4 m_fogParams{20.0f, 100.0f, 0.02f, 0.0f}; // start, end, density, unused
    Vec3 m_fogColor{0.7f, 0.7f, 0.8f};
    bool ensurePostProcessResources();

    // Skybox
    GLuint m_skyboxVao{0};
    GLuint m_skyboxVbo{0};
    GLuint m_skyboxProgram{0};
    GLint  m_skyboxLocProjection{-1};
    GLint  m_skyboxLocView{-1};
    GLint  m_skyboxLocSampler{-1};
    GLuint m_skyboxCubemap{0};
    std::string m_skyboxLoadedPath;
    bool ensureSkyboxResources();
    void releaseSkyboxResources();
    bool loadSkyboxCubemap(const std::string& folderPath);
    void renderSkybox(const glm::mat4& view, const glm::mat4& projection);

    static constexpr size_t kFrameQueryCount = 3;
    std::array<GLuint, kFrameQueryCount> m_gpuTimerQueries{};
    size_t m_gpuQueryIndex{0};
    bool m_gpuQueriesInitialized{false};
    double m_lastGpuFrameMs{0.0};
    double m_cpuRenderWorldMs{0.0};
    double m_cpuRenderUiMs{0.0};
    double m_cpuUiLayoutMs{0.0};
    double m_cpuUiDrawMs{0.0};
    double m_cpuEcsMs{0.0};

    struct DrawCmd
    {
        OpenGLObject3D* obj{nullptr};
        OpenGLMaterial* material{nullptr};
        glm::mat4 modelMatrix{1.0f};
        GLuint program{0};
        glm::vec3 boundsMin{};
        glm::vec3 boundsMax{};
        glm::vec3 emissionColor{-1.0f};
        unsigned int entityId{0};
        bool hasBounds{false};
        bool hasEmission{false};
        bool isTransparent{false};
        bool isSkinned{false};
        ECS::MaterialOverrides overrides{};
    };
    std::vector<DrawCmd> m_drawList;
    std::vector<DrawCmd> m_transparentDrawList;
    std::vector<DrawCmd> m_shadowCasterList;

    // GPU Instanced Rendering (SSBO)
    GLuint m_instanceSSBO{0};
    size_t m_instanceSSBOCapacity{0};
    std::vector<glm::mat4> m_instanceMatrixBuffer;
    void uploadInstanceData(const glm::mat4* data, size_t count);
    void releaseInstanceResources();

    // Order-Independent Transparency (Weighted Blended OIT)
    bool ensureOitResources(int width, int height);
    void releaseOitResources();
    void renderOitTransparentPass(const glm::mat4& view, const glm::vec3& lightPosition,
                                  const glm::vec3& lightColor, float lightIntensity,
                                  const glm::vec3& fogColor, int debugMode, float activeNear, float activeFar);
    void compositeOit(GLuint dstFbo, int vpX, int vpY, int vpW, int vpH);
    GLuint m_oitFbo{0};
    GLuint m_oitAccumTex{0};
    GLuint m_oitRevealageTex{0};
    GLuint m_oitDepthRbo{0};
    int m_oitWidth{0};
    int m_oitHeight{0};
    GLuint m_oitCompositeProgram{0};
    GLuint m_oitCompositeVao{0};
    GLint m_oitCompLocAccum{-1};
    GLint m_oitCompLocRevealage{-1};
    bool m_oitEnabled{true};
    bool m_textureCompressionEnabled{false};
    bool m_textureStreamingEnabled{false};
    bool m_renderFrozen{false};

    // Skeletal animation: per-entity animators
    std::unordered_map<unsigned int, std::shared_ptr<SkeletalAnimator>> m_entityAnimators;
    uint64_t m_lastAnimTickCounter{0};

    // Shadow mapping
    bool ensureShadowResources();
    void releaseShadowResources();
    void renderShadowMap(const std::vector<DrawCmd>& drawList);
    void findShadowLightIndices();
    glm::mat4 computeLightSpaceMatrix(const OpenGLMaterial::LightData& light) const;

    static constexpr int kShadowMapSize = 4096;
    static constexpr int kMaxShadowLights = OpenGLMaterial::kMaxShadowLights;
    GLuint m_shadowFbo{0};
    GLuint m_shadowDepthArray{0};
    GLuint m_shadowProgram{0};
    GLint m_shadowLocModel{-1};
    GLint m_shadowLocLightSpace{-1};
    GLint m_shadowLocInstanced{-1};
    GLint m_shadowLocSkinned{-1};
    GLint m_shadowLocBoneMatrices{-1};
    glm::mat4 m_shadowLightSpaceMatrices[kMaxShadowLights]{};
    int m_shadowLightIndices[kMaxShadowLights]{};
    int m_shadowCount{0};
    bool m_shadowEnabled{true};

    // Point light shadow mapping (cube maps)
    bool ensurePointShadowResources();
    void releasePointShadowResources();
    void renderPointShadowMaps(const std::vector<DrawCmd>& drawList);
    void findPointShadowLightIndices();

    static constexpr int kMaxPointShadowLights = OpenGLMaterial::kMaxPointShadowLights;
    static constexpr int kPointShadowMapSize = 1024;
    GLuint m_pointShadowFbo{0};
    GLuint m_pointShadowCubeArray{0};
    GLuint m_pointShadowProgram{0};
    GLint m_pointShadowLocModel{-1};
    GLint m_pointShadowLocLightPos{-1};
    GLint m_pointShadowLocFarPlane{-1};
    GLint m_pointShadowLocShadowMatrices{-1};
    glm::vec3 m_pointShadowPositions[kMaxPointShadowLights]{};
    float m_pointShadowFarPlanes[kMaxPointShadowLights]{};
    int m_pointShadowLightIndices[kMaxPointShadowLights]{};
    int m_pointShadowCount{0};

    // Cascaded Shadow Maps (directional light)
    bool ensureCsmResources();
    void releaseCsmResources();
    void computeCsmMatrices(const OpenGLMaterial::LightData& light, const glm::mat4& view, const glm::mat4& proj,
                            float nearPlane, float farPlane);
    void renderCsmShadowMaps(const std::vector<DrawCmd>& drawList);

    static constexpr int kNumCsmCascades = OpenGLMaterial::kMaxCsmCascades;
    static constexpr int kCsmMapSize = 2048;
    GLuint m_csmFbo{0};
    GLuint m_csmDepthArray{0};
    glm::mat4 m_csmMatrices[kNumCsmCascades]{};
    float m_csmSplits[kNumCsmCascades]{};
    int m_csmLightIndex{-1};
    bool m_csmEnabled{false};
    bool m_csmUserEnabled{true};

    // Entity picking FBO
    GLuint m_pickFbo{0};
    GLuint m_pickColorTex{0};
    GLuint m_pickDepthRbo{0};
    GLuint m_pickProgram{0};
    GLint m_pickLocModel{-1};
    GLint m_pickLocView{-1};
    GLint m_pickLocProjection{-1};
    GLint m_pickLocEntityId{-1};
    int m_pickWidth{0};
    int m_pickHeight{0};
    bool m_pickDirty{true};

    // Selection outline (post-process edge detection on pick buffer)
    GLuint m_outlineProgram{0};
    GLuint m_outlineVao{0};
    GLint m_outlineLocPickTex{-1};
    GLint m_outlineLocOutlineColor{-1};
    GLint m_outlineLocThickness{-1};
    std::unordered_set<unsigned int> m_selectedEntities;
    bool m_pickRequested{false};
    bool m_pickCtrlHeld{false};
    int m_pickX{0};
    int m_pickY{0};

    void drawSelectionOutline();

    // ---- Rubber-Band (Marquee) Selection ----
    bool m_rubberBandActive{false};
    Vec2 m_rubberBandStart{};
    Vec2 m_rubberBandEnd{};
    void drawRubberBand(const glm::mat4& ortho);
    void resolveRubberBandSelection();

    // ---- Editor Gizmos ----

    bool ensureGizmoResources();
    void releaseGizmoResources();
    void renderGizmo(const glm::mat4& view, const glm::mat4& projection);
    GizmoAxis pickGizmoAxis(const glm::mat4& view, const glm::mat4& projection, int screenX, int screenY) const;
    glm::mat3 getEntityRotationMatrix(const ECS::TransformComponent& tc) const;
    glm::vec3 getGizmoWorldAxis(const ECS::TransformComponent& tc, int axisIdx) const;

    GLuint m_gizmoProgram{ 0 };
    GLuint m_gizmoVao{ 0 };
    GLuint m_gizmoVbo{ 0 };
    GLint m_gizmoLocMVP{ -1 };
    GLint m_gizmoLocColor{ -1 };
    GizmoMode m_gizmoMode{ GizmoMode::Translate };
    GizmoAxis m_gizmoActiveAxis{ GizmoAxis::None };
    GizmoAxis m_gizmoHoveredAxis{ GizmoAxis::None };
    bool m_gizmoDragging{ false };
    glm::vec3 m_gizmoDragEntityStart{ 0.0f };  // entity position at drag start
    glm::vec3 m_gizmoDragWorldAxis{ 0.0f };     // world-space axis direction during drag
    float m_gizmoDragStartT{ 0.0f };            // initial projection of mouse onto axis ray
    float m_gizmoDragRotStart{ 0.0f };
    float m_gizmoDragScaleStart{ 1.0f };
    glm::vec2 m_gizmoDragStartScreen{ 0.0f };   // screen position at drag start
    ECS::TransformComponent m_gizmoDragOldTransform{};  // primary entity transform snapshot for undo
    std::unordered_map<unsigned int, ECS::TransformComponent> m_gizmoDragOldTransforms;  // all selected entities for group undo

    // ---- Collider Debug Visualization ----
    void renderColliderDebug(const glm::mat4& view, const glm::mat4& projection);

    // ---- Streaming Volume Debug Visualization (Phase 11.4) ----
    void renderStreamingVolumeDebug(const glm::mat4& view, const glm::mat4& projection);

    // ---- Bone / Skeleton Debug Overlay ----
    void renderBoneDebug(const glm::mat4& view, const glm::mat4& projection);

    // ---- Viewport Grid ----
    bool ensureGridResources();
    void releaseGridResources();
    void drawViewportGrid(const glm::mat4& view, const glm::mat4& projection);

    GLuint m_gridProgram{ 0 };
    GLuint m_gridVao{ 0 };
    GLuint m_gridVbo{ 0 };
    GLint m_gridLocViewProj{ -1 };
    GLint m_gridLocCameraPos{ -1 };
    GLint m_gridLocGridSize{ -1 };
    GLint m_gridLocColor{ -1 };
    GLint m_gridLocFadeRadius{ -1 };

    // Editor tab system
    void releaseAllTabFbos();
    std::vector<EditorTab> m_editorTabs;
    std::string m_activeTabId{ "Viewport" };
    EditorTab* m_cachedActiveTab{ nullptr };

    // Saved viewport level when a mesh viewer tab is active
    std::unique_ptr<EngineLevel> m_savedViewportLevel;
    Vec3 m_savedCameraPos{};
    Vec2 m_savedCameraRot{};
    std::unordered_set<unsigned int> m_savedViewportSelectedEntities;
    std::unordered_map<std::string, std::unordered_set<unsigned int>> m_tabSelectedEntities;

    // Popup window management (multi-window)
    void drawUIWidgetsToFramebuffer(UIManager& mgr, int width, int height);
    void ensurePopupUIVao();
    void renderPopupWindows();
    std::unordered_map<std::string, std::unique_ptr<PopupWindow>> m_popupWindows;
    GLuint m_popupUiVao{ 0 };

    // Current rendering viewport size (set at the start of drawUIWidgetsToFramebuffer
    // so that drawUIPanel/drawUIBrush pass the correct uViewportSize to shaders,
    // even when rendering into popup windows).
    Vec2 m_currentRenderViewportSize{};

    // Mesh viewer editor windows
    std::unordered_map<std::string, std::unique_ptr<MeshViewerWindow>> m_meshViewers;

    // Material editor windows
    std::unordered_map<std::string, std::unique_ptr<MaterialEditorWindow>> m_materialEditors;

    // Texture viewer editor windows
    std::unordered_map<std::string, std::unique_ptr<TextureViewerWindow>> m_textureViewers;
    GLuint m_texViewerChannelProgram{ 0 };
    struct TexViewerChannelUniforms
    {
        GLint projection{ -1 };
        GLint rect{ -1 };
        GLint texture{ -1 };
        GLint channelMask{ -1 };
        GLint checkerboard{ -1 };
        GLint flipY{ -1 };
    };
    TexViewerChannelUniforms m_texViewerChannelUniforms{};

    // Widget editor preview FBOs (one per open editor tab)
    std::unordered_map<std::string, std::unique_ptr<OpenGLRenderTarget>> m_widgetEditorPreviews;

public:
    void toggleUIDebug() override { m_uiDebugEnabled = !m_uiDebugEnabled; }
    bool isUIDebugEnabled() const override { return m_uiDebugEnabled; }
    uint32_t getLastVisibleCount() const override { return m_lastVisibleCount; }
    uint32_t getLastHiddenCount() const override { return m_lastHiddenCount; }
    uint32_t getLastTotalCount() const override { return m_lastTotalCount; }
    std::vector<RenderPassInfo> getRenderPassInfo() const override;
    void toggleBoundsDebug() override { m_boundsDebugEnabled = !m_boundsDebugEnabled; }
    bool isBoundsDebugEnabled() const override { return m_boundsDebugEnabled; }
    void setHeightFieldDebugEnabled(bool enabled) override { m_hfDebugEnabled = enabled; }
    bool isHeightFieldDebugEnabled() const override { return m_hfDebugEnabled; }

    void setShadowsEnabled(bool enabled) override { m_shadowEnabled = enabled; }
    bool isShadowsEnabled() const override { return m_shadowEnabled; }
    void setOcclusionCullingEnabled(bool enabled) override { m_occlusionEnabled = enabled; }
    bool isOcclusionCullingEnabled() const override { return m_occlusionEnabled; }
    void setWireframeEnabled(bool enabled) override { m_wireframeEnabled = enabled; }
    bool isWireframeEnabled() const override { return m_wireframeEnabled; }
    void setVSyncEnabled(bool enabled) override;
    bool isVSyncEnabled() const override { return m_vsyncEnabled; }
    void setPostProcessingEnabled(bool enabled) override { m_postProcessEnabled = enabled; }
    bool isPostProcessingEnabled() const override { return m_postProcessEnabled; }
    void setGammaCorrectionEnabled(bool enabled) override { m_gammaEnabled = enabled; }
    bool isGammaCorrectionEnabled() const override { return m_gammaEnabled; }
    void setToneMappingEnabled(bool enabled) override { m_toneMappingEnabled = enabled; }
    bool isToneMappingEnabled() const override { return m_toneMappingEnabled; }
    void setExposure(float exposure) override { m_exposure = exposure; }
    float getExposure() const override { return m_exposure; }
    void setAntiAliasingMode(AntiAliasingMode mode) override { m_aaMode = mode; }
    AntiAliasingMode getAntiAliasingMode() const override { return m_aaMode; }
    void setFogEnabled(bool enabled) override { m_fogEnabled = enabled; }
    bool isFogEnabled() const override { return m_fogEnabled; }
    void setFogParams(const Vec4& params) override { m_fogParams = params; }
    Vec4 getFogParams() const override { return m_fogParams; }
    void setFogColor(const Vec3& color) override { m_fogColor = color; }
    Vec3 getFogColor() const override { return m_fogColor; }
    void setBloomEnabled(bool enabled) override { m_bloomEnabled = enabled; }
    bool isBloomEnabled() const override { return m_bloomEnabled; }
    void setBloomThreshold(float threshold) override { m_bloomThreshold = threshold; }
    float getBloomThreshold() const override { return m_bloomThreshold; }
    void setBloomIntensity(float intensity) override { m_bloomIntensity = intensity; }
    float getBloomIntensity() const override { return m_bloomIntensity; }
    void setSsaoEnabled(bool enabled) override { m_ssaoEnabled = enabled; }
    bool isSsaoEnabled() const override { return m_ssaoEnabled; }
    void setCsmEnabled(bool enabled) override { m_csmUserEnabled = enabled; }
    bool isCsmEnabled() const override { return m_csmUserEnabled; }
    void setOitEnabled(bool enabled) override { m_oitEnabled = enabled; }
    bool isOitEnabled() const override { return m_oitEnabled; }
    void setTextureCompressionEnabled(bool enabled) override { m_textureCompressionEnabled = enabled; }
    bool isTextureCompressionEnabled() const override { return m_textureCompressionEnabled; }
    void setTextureStreamingEnabled(bool enabled) override { m_textureStreamingEnabled = enabled; }
    bool isTextureStreamingEnabled() const override { return m_textureStreamingEnabled; }
    void setDisplacementMappingEnabled(bool enabled) override { m_displacementEnabled = enabled; }
    bool isDisplacementMappingEnabled() const override { return m_displacementEnabled; }
    void setDisplacementScale(float scale) override { m_displacementScale = scale; }
    float getDisplacementScale() const override { return m_displacementScale; }
    void setTessellationLevel(float level) override { m_tessLevel = level; }
    float getTessellationLevel() const override { return m_tessLevel; }
    void setRenderFrozen(bool frozen) override { m_renderFrozen = frozen; }
    bool isRenderFrozen() const override { return m_renderFrozen; }
    void setDebugRenderMode(DebugRenderMode mode) override { m_debugRenderMode = mode; }
    DebugRenderMode getDebugRenderMode() const override { return m_debugRenderMode; }
    void setSkyboxPath(const std::string& pathOrFolder) override;
    std::string getSkyboxPath() const override { return m_skyboxLoadedPath; }
    void requestPick(int screenX, int screenY, bool ctrlHeld = false) override { m_pickRequested = true; m_pickX = screenX; m_pickY = screenY; m_pickCtrlHeld = ctrlHeld; }
    unsigned int getSelectedEntity() const override { return m_selectedEntities.empty() ? 0u : *m_selectedEntities.begin(); }
    void setSelectedEntity(unsigned int entity) override { m_selectedEntities.clear(); if (entity != 0) m_selectedEntities.insert(entity); }
    const std::unordered_set<unsigned int>& getSelectedEntities() const override { return m_selectedEntities; }
    void addSelectedEntity(unsigned int entity) override { if (entity != 0) m_selectedEntities.insert(entity); }
    void removeSelectedEntity(unsigned int entity) override { m_selectedEntities.erase(entity); }
    void clearSelection() override { m_selectedEntities.clear(); }
    bool isEntitySelected(unsigned int entity) const override { return m_selectedEntities.count(entity) > 0; }
    void focusOnSelectedEntity() override;

    // Rubber-band selection public API
    void beginRubberBand(int screenX, int screenY) override;
    void updateRubberBand(int screenX, int screenY) override;
    void endRubberBand(bool ctrlHeld) override;
    void cancelRubberBand() override;
    bool isRubberBandActive() const override { return m_rubberBandActive; }
    Vec2 getRubberBandStart() const override { return m_rubberBandStart; }

    // Gizmo public API
    void setGizmoMode(GizmoMode mode) override { m_gizmoMode = mode; }
    GizmoMode getGizmoMode() const override { return m_gizmoMode; }
    bool beginGizmoDrag(int screenX, int screenY) override;
    void updateGizmoDrag(int screenX, int screenY) override;
    void endGizmoDrag() override;
    bool isGizmoDragging() const override { return m_gizmoDragging; }

    // Multi-window / popup management
    // Opens (or returns existing) a popup window with shared GL context.
    PopupWindow* openPopupWindow(const std::string& id, const std::string& title, int width, int height) override;
    void          closePopupWindow(const std::string& id) override;
    PopupWindow*  getPopupWindow(const std::string& id) override;
    // Route an SDL event to the focused popup. Returns true if the event was consumed.
    bool          routeEventToPopup(SDL_Event& event) override;

    // Mesh viewer editor window
    MeshViewerWindow* openMeshViewer(const std::string& assetPath) override;
    void              closeMeshViewer(const std::string& assetPath) override;
    MeshViewerWindow* getMeshViewer(const std::string& assetPath) override;

    // Material editor tab
    MaterialEditorWindow* openMaterialEditorTab(const std::string& assetPath) override;
    void                  closeMaterialEditorTab(const std::string& assetPath) override;
    MaterialEditorWindow* getMaterialEditor(const std::string& assetPath) override;

    // Texture viewer editor window
    TextureViewerWindow* openTextureViewer(const std::string& assetPath) override;
    void                 closeTextureViewer(const std::string& assetPath) override;
    TextureViewerWindow* getTextureViewer(const std::string& assetPath) override;

    // Skeletal animation queries
    bool  isEntitySkinned(unsigned int entity) const override;
    int   getEntityAnimationClipCount(unsigned int entity) const override;
    AnimationClipInfo getEntityAnimationClipInfo(unsigned int entity, int clipIndex) const override;
    int   getEntityAnimatorCurrentClip(unsigned int entity) const override;
    float getEntityAnimatorCurrentTime(unsigned int entity) const override;
    bool  isEntityAnimatorPlaying(unsigned int entity) const override;
    void  playEntityAnimation(unsigned int entity, int clipIndex, bool loop) override;
    void  stopEntityAnimation(unsigned int entity) override;
    void  setEntityAnimationSpeed(unsigned int entity, float speed) override;
    int   getEntityBoneCount(unsigned int entity) const override;
    std::string getEntityBoneName(unsigned int entity, int boneIndex) const override;
    int   getEntityBoneParent(unsigned int entity, int boneIndex) const override;

};