#pragma once

#include <string>
#include <memory>
#include <vector>
#include <unordered_set>
#include <SDL3/SDL.h>
#include "../Core/MathTypes.h"
#include "RendererCapabilities.h"
#include "CameraPath.h"
#include "RendererEnums.h"
#if ENGINE_EDITOR
#include "IEditorRenderer.h"
#endif

class UIManager;
class ViewportUIManager;
#if ENGINE_EDITOR
class PopupWindow;
class MeshViewerWindow;
class MaterialEditorWindow;
class TextureViewerWindow;
#endif
class Widget;
class AssetData;

/// @brief Abstract renderer base class – the primary interface every graphics backend must implement.
///
/// ## How to add a new backend
///   1. Create a sub-folder under `src/Renderer/` (e.g. `VulkanRenderer/`).
///   2. Derive a concrete class from `Renderer` and implement **all** pure-virtual methods.
///      Non-pure methods have safe defaults but should be overridden where the backend
///      provides the functionality (shadows, gizmos, picking, etc.).
///   3. Register the backend in `RendererFactory` so that `createRenderer()` /
///      `createSplashWindow()` can instantiate it via the `RendererBackend` enum.
///   4. Provide concrete implementations of the related abstract interfaces:
///      - `IRenderObject2D` / `IRenderObject3D` (scene objects)
///      - `ITexture`, `IShaderProgram`, `ITextRenderer` (GPU resources)
///      - `IRenderTarget`, `IRenderContext` (off-screen targets & context)
///   5. Add the new source files to `src/Renderer/CMakeLists.txt` under a
///      backend-specific `if()` block so builds stay modular.
///
/// ## Lifetime & threading
///   - `initialize()` creates the window and GPU context; `shutdown()` tears them down.
///   - All virtual methods are called from the **main thread** only.
///   - The renderer instance is owned by `main()` and deleted after `shutdown()`.
///
/// ## Method categories (see section comments below)
///   | Tag           | Obligation          | Notes                               |
///   |---------------|---------------------|-------------------------------------|
///   | Core          | **must** override   | init / shutdown / render loop        |
///   | Camera        | **must** override   | editor & runtime camera              |
///   | UIManager     | **must** override   | every renderer owns a UIManager      |
///   | Capabilities  | *should* override   | report GPU / driver info             |
///   | Toggles       | *may* override      | shadows, vsync, wireframe, etc.      |
///   | Debug         | *may* override      | UI-debug / bounds-debug overlays     |
///   | Picking       | *may* override      | entity picking via colour buffer     |
///   | Gizmo         | *may* override      | translate / rotate / scale gizmo     |
///   | Tabs          | *may* override      | editor multi-tab support             |
///   | Popups        | *may* override      | popup / child windows                |
///   | MeshViewer    | *may* override      | standalone mesh preview windows      |
///   | Viewport      | *may* override      | clear colour, skybox, text overlay   |
///   | Scene         | *may* override      | entity refresh after ECS changes     |
///   | Metrics       | *may* override      | per-frame GPU/CPU timing counters    |
class Renderer
#if ENGINE_EDITOR
    : public IEditorRenderer
#endif
{
public:
    // Type aliases (definitions in RendererEnums.h; kept here for backward compatibility)
    using GizmoMode = ::GizmoMode;
    using GizmoAxis = ::GizmoAxis;
    using AntiAliasingMode = ::AntiAliasingMode;
    using DebugRenderMode = ::DebugRenderMode;
    using ViewportLayout = ::ViewportLayout;
    using SubViewportPreset = ::SubViewportPreset;
    using SubViewportCamera = ::SubViewportCamera;

    virtual ~Renderer() = default;

    // --- Core (pure virtual) ---
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void clear() = 0;
    virtual void render() = 0;
    virtual void present() = 0;
    virtual const std::string& name() const = 0;

    virtual SDL_Window* window() const = 0;

    // --- Camera controls (pure virtual) ---
    virtual void moveCamera(float forward, float right, float up) = 0;
    virtual void rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees) = 0;
    virtual Vec3 getCameraPosition() const = 0;
    virtual void setCameraPosition(const Vec3& position) = 0;
    virtual Vec2 getCameraRotationDegrees() const = 0;
    virtual void setCameraRotationDegrees(float yawDegrees, float pitchDegrees) = 0;

    // --- Camera transitions (smooth interpolation) ---
    struct CameraTransition
    {
        Vec3 startPos{};
        Vec3 endPos{};
        float startYaw{0};
        float startPitch{0};
        float endYaw{0};
        float endPitch{0};
        float duration{1.0f};
        float elapsed{0.0f};
        bool active{false};
    };

    /// Start a smooth camera transition to the given position/rotation over `durationSec` seconds.
    virtual void startCameraTransition(const Vec3& targetPos, float targetYaw, float targetPitch, float durationSec)
    {
        (void)targetPos; (void)targetYaw; (void)targetPitch; (void)durationSec;
    }

    /// Returns true while a camera transition is in progress.
    virtual bool isCameraTransitioning() const { return false; }

    /// Cancel an active camera transition, keeping the current interpolated position.
    virtual void cancelCameraTransition() {}

    // --- Cinematic camera path (spline-based) ---

    /// Start playback of a camera path (Catmull-Rom spline through control points).
    /// Each point is {x, y, z, yaw, pitch}.  Duration is total playback time.
    virtual void startCameraPath(const std::vector<CameraPathPoint>& points, float duration, bool loop = false)
    {
        (void)points; (void)duration; (void)loop;
    }

    /// Returns true while a camera path is playing.
    virtual bool isCameraPathPlaying() const { return false; }

    /// Pause / resume a playing camera path.
    virtual void pauseCameraPath() {}
    virtual void resumeCameraPath() {}

    /// Stop the camera path, keeping the current interpolated position.
    virtual void stopCameraPath() {}

    /// Get normalised progress [0,1] of the current camera path.
    virtual float getCameraPathProgress() const { return 0.0f; }

    /// Read/write the control-point list that backs the camera path (Sequencer editing).
    virtual std::vector<CameraPathPoint> getCameraPathPoints() const { return {}; }
    virtual void setCameraPathPoints(const std::vector<CameraPathPoint>& pts) { (void)pts; }
    virtual float getCameraPathDuration() const { return 1.0f; }
    virtual void setCameraPathDuration(float d) { (void)d; }
    virtual bool getCameraPathLoop() const { return false; }
    virtual void setCameraPathLoop(bool l) { (void)l; }

    // Active entity camera (used at runtime / PIE)
    virtual void setActiveCameraEntity(unsigned int entity) = 0;
    virtual unsigned int getActiveCameraEntity() const = 0;
    virtual void clearActiveCameraEntity() = 0;

    // Screen-to-world unprojection using depth buffer
    virtual bool screenToWorldPos(int screenX, int screenY, Vec3& outWorldPos) const = 0;

    // --- UIManager access (pure virtual — every renderer owns a UIManager) ---
    virtual UIManager& getUIManager() = 0;
    virtual const UIManager& getUIManager() const = 0;
    virtual ViewportUIManager* getViewportUIManagerPtr() { return nullptr; }
    virtual const ViewportUIManager* getViewportUIManagerPtr() const { return nullptr; }

    // --- Capabilities ---
    virtual RendererCapabilities getCapabilities() const { return {}; }

    // --- Rendering toggles ---
    virtual bool isShadowsEnabled() const { return false; }
    virtual void setShadowsEnabled(bool /*enabled*/) {}
    virtual bool isVSyncEnabled() const { return false; }
    virtual void setVSyncEnabled(bool /*enabled*/) {}
    virtual bool isWireframeEnabled() const { return false; }
    virtual void setWireframeEnabled(bool /*enabled*/) {}
    virtual bool isOcclusionCullingEnabled() const { return false; }
    virtual void setOcclusionCullingEnabled(bool /*enabled*/) {}
    virtual bool isPostProcessingEnabled() const { return false; }
    virtual void setPostProcessingEnabled(bool /*enabled*/) {}
    virtual bool isGammaCorrectionEnabled() const { return true; }
    virtual void setGammaCorrectionEnabled(bool /*enabled*/) {}
    virtual bool isToneMappingEnabled() const { return true; }
    virtual void setToneMappingEnabled(bool /*enabled*/) {}
    virtual float getExposure() const { return 1.0f; }
    virtual void setExposure(float /*exposure*/) {}
    virtual AntiAliasingMode getAntiAliasingMode() const { return AntiAliasingMode::None; }
    virtual void setAntiAliasingMode(AntiAliasingMode /*mode*/) {}
    virtual bool isFogEnabled() const { return false; }
    virtual void setFogEnabled(bool /*enabled*/) {}
    virtual Vec4 getFogParams() const { return {}; }
    virtual void setFogParams(const Vec4& /*params*/) {}
    virtual Vec3 getFogColor() const { return {0.7f, 0.7f, 0.8f}; }
    virtual void setFogColor(const Vec3& /*color*/) {}
    virtual bool isBloomEnabled() const { return false; }
    virtual void setBloomEnabled(bool /*enabled*/) {}
    virtual float getBloomThreshold() const { return 1.0f; }
    virtual void setBloomThreshold(float /*threshold*/) {}
    virtual float getBloomIntensity() const { return 0.3f; }
    virtual void setBloomIntensity(float /*intensity*/) {}
    virtual bool isSsaoEnabled() const { return false; }
    virtual void setSsaoEnabled(bool /*enabled*/) {}
    virtual bool isCsmEnabled() const { return true; }
    virtual void setCsmEnabled(bool /*enabled*/) {}
    virtual bool isOitEnabled() const { return false; }
    virtual void setOitEnabled(bool /*enabled*/) {}
    virtual bool isTextureCompressionEnabled() const { return false; }
    virtual void setTextureCompressionEnabled(bool /*enabled*/) {}
    virtual bool isTextureStreamingEnabled() const { return false; }
    virtual void setTextureStreamingEnabled(bool /*enabled*/) {}

    // --- Displacement Mapping (Tessellation) ---
    virtual bool isDisplacementMappingEnabled() const { return false; }
    virtual void setDisplacementMappingEnabled(bool /*enabled*/) {}
    virtual float getDisplacementScale() const { return 0.5f; }
    virtual void setDisplacementScale(float /*scale*/) {}
    virtual float getTessellationLevel() const { return 16.0f; }
    virtual void setTessellationLevel(float /*level*/) {}

    #if ENGINE_EDITOR
    // --- Debug render mode (IEditorRenderer overrides with defaults) ---
    DebugRenderMode getDebugRenderMode() const override { return DebugRenderMode::Lit; }
    void setDebugRenderMode(DebugRenderMode /*mode*/) override {}

    // --- Debug visualizations ---
    bool isUIDebugEnabled() const override { return false; }
    void toggleUIDebug() override {}
    bool isBoundsDebugEnabled() const override { return false; }
    void toggleBoundsDebug() override {}
    bool isHeightFieldDebugEnabled() const override { return false; }
    void setHeightFieldDebugEnabled(bool /*enabled*/) override {}
    void setCollidersVisible(bool visible) override { m_collidersVisible = visible; }
    bool isCollidersVisible() const override { return m_collidersVisible; }
    void setBonesVisible(bool visible) override { m_bonesVisible = visible; }
    bool isBonesVisible() const override { return m_bonesVisible; }

    // --- Entity picking ---

    unsigned int pickEntityAt(int /*x*/, int /*y*/) override { return 0; }
    unsigned int pickEntityAtImmediate(int /*x*/, int /*y*/) override { return 0; }
    void requestPick(int /*screenX*/, int /*screenY*/, bool /*ctrlHeld*/ = false) override {}
    unsigned int getSelectedEntity() const override { return 0; }
    void setSelectedEntity(unsigned int /*entity*/) override {}

    // --- Multi-select ---
    const std::unordered_set<unsigned int>& getSelectedEntities() const override { static const std::unordered_set<unsigned int> empty; return empty; }
    void addSelectedEntity(unsigned int /*entity*/) override {}
    void removeSelectedEntity(unsigned int /*entity*/) override {}
    void clearSelection() override {}
    bool isEntitySelected(unsigned int /*entity*/) const override { return false; }

    /// Focus camera on the currently selected entity (smooth transition to AABB center).
    void focusOnSelectedEntity() override {}

    // --- Rubber-band (marquee) selection ---
    void beginRubberBand(int /*screenX*/, int /*screenY*/) override {}
    void updateRubberBand(int /*screenX*/, int /*screenY*/) override {}
    void endRubberBand(bool /*ctrlHeld*/ = false) override {}
    void cancelRubberBand() override {}
    bool isRubberBandActive() const override { return false; }
    Vec2 getRubberBandStart() const override { return Vec2{}; }

    // --- Gizmo ---
    void setGizmoMode(GizmoMode /*mode*/) override {}
    GizmoMode getGizmoMode() const override { return GizmoMode::None; }
    bool beginGizmoDrag(int /*screenX*/, int /*screenY*/) override { return false; }
    void updateGizmoDrag(int /*screenX*/, int /*screenY*/) override {}
    void endGizmoDrag() override {}
    bool isGizmoDragging() const override { return false; }

    // --- Snap & Grid ---
    void setSnapEnabled(bool enabled) override { m_snapEnabled = enabled; }
    bool isSnapEnabled() const override { return m_snapEnabled; }
    void setGridVisible(bool visible) override { m_gridVisible = visible; }
    bool isGridVisible() const override { return m_gridVisible; }
    void setGridSize(float size) override { m_gridSize = size; }
    float getGridSize() const override { return m_gridSize; }
    void setRotationSnapDeg(float deg) override { m_rotationSnapDeg = deg; }
    float getRotationSnapDeg() const override { return m_rotationSnapDeg; }
    void setScaleSnapStep(float step) override { m_scaleSnapStep = step; }
    float getScaleSnapStep() const override { return m_scaleSnapStep; }

    // --- Editor tabs ---
    void addTab(const std::string& /*id*/, const std::string& /*name*/, bool /*closable*/) override {}
    void removeTab(const std::string& /*id*/) override {}
    void setActiveTab(const std::string& /*id*/) override {}
    const std::string& getActiveTabId() const override { static const std::string s; return s; }
    void cleanupWidgetEditorPreview(const std::string& /*tabId*/) override {}
#endif // ENGINE_EDITOR — Debug, Picking, Gizmo, Snap/Grid, Editor Tabs

    // --- Skeletal animation queries
    struct AnimationClipInfo
    {
        std::string name;
        float duration{ 0.0f };
        float ticksPerSecond{ 25.0f };
        int channelCount{ 0 };
    };
    virtual bool  isEntitySkinned(unsigned int /*entity*/) const { return false; }
    virtual int   getEntityAnimationClipCount(unsigned int /*entity*/) const { return 0; }
    virtual AnimationClipInfo getEntityAnimationClipInfo(unsigned int /*entity*/, int /*clipIndex*/) const { return {}; }
    virtual int   getEntityAnimatorCurrentClip(unsigned int /*entity*/) const { return -1; }
    virtual float getEntityAnimatorCurrentTime(unsigned int /*entity*/) const { return 0.0f; }
    virtual bool  isEntityAnimatorPlaying(unsigned int /*entity*/) const { return false; }
    virtual void  playEntityAnimation(unsigned int /*entity*/, int /*clipIndex*/, bool /*loop*/ = true) {}
    virtual void  stopEntityAnimation(unsigned int /*entity*/) {}
    virtual void  setEntityAnimationSpeed(unsigned int /*entity*/, float /*speed*/) {}
    virtual int   getEntityBoneCount(unsigned int /*entity*/) const { return 0; }
    virtual std::string getEntityBoneName(unsigned int /*entity*/, int /*boneIndex*/) const { return {}; }
    virtual int   getEntityBoneParent(unsigned int /*entity*/, int /*boneIndex*/) const { return -1; }

    // --- Popup windows (IEditorRenderer overrides) ---
#if ENGINE_EDITOR
    PopupWindow* openPopupWindow(const std::string& /*id*/, const std::string& /*title*/, int /*width*/, int /*height*/) override { return nullptr; }
    void closePopupWindow(const std::string& /*id*/) override {}
    PopupWindow* getPopupWindow(const std::string& /*id*/) override { return nullptr; }
    bool routeEventToPopup(SDL_Event& /*event*/) override { return false; }

    // --- Mesh viewer ---
    MeshViewerWindow* openMeshViewer(const std::string& /*assetPath*/) override { return nullptr; }
    void closeMeshViewer(const std::string& /*assetPath*/) override {}
    MeshViewerWindow* getMeshViewer(const std::string& /*assetPath*/) override { return nullptr; }

    // --- Texture viewer ---
    TextureViewerWindow* openTextureViewer(const std::string& /*assetPath*/) override { return nullptr; }
    void closeTextureViewer(const std::string& /*assetPath*/) override {}
    TextureViewerWindow* getTextureViewer(const std::string& /*assetPath*/) override { return nullptr; }

    // --- Material editor tab ---
    MaterialEditorWindow* openMaterialEditorTab(const std::string& /*assetPath*/) override { return nullptr; }
    void closeMaterialEditorTab(const std::string& /*assetPath*/) override {}
    MaterialEditorWindow* getMaterialEditor(const std::string& /*assetPath*/) override { return nullptr; }
#endif // ENGINE_EDITOR

    // --- Viewport & visuals ---
    virtual Vec2 getViewportSize() const { return {}; }
    virtual void setClearColor(const Vec4& /*color*/) {}
    virtual Vec4 getClearColor() const { return {}; }
    virtual void setSkyboxPath(const std::string& /*pathOrFolder*/) {}
    virtual std::string getSkyboxPath() const { return {}; }
    virtual void queueText(const std::string& /*text*/, const Vec2& /*screenPos*/, float /*scale*/, const Vec4& /*color*/) {}
    virtual std::shared_ptr<Widget> createWidgetFromAsset(const std::shared_ptr<AssetData>& /*asset*/) { return nullptr; }
    virtual unsigned int preloadUITexture(const std::string& /*path*/) { return 0; }
    /// Request a full shader hot-reload (invalidate caches, recompile all shaders).
    virtual void requestShaderReload() {}
    /// Generate an FBO-rendered thumbnail for a 3D model or material asset.
    /// @param assetPath  Content-relative asset path
    /// @param assetType  int-cast of AssetType (Model3D=3, Material=1)
    /// @return OpenGL texture ID (cached), or 0 on failure
    virtual unsigned int generateAssetThumbnail(const std::string& /*assetPath*/, int /*assetType*/) { return 0; }

    // --- Scene management ---
    virtual void refreshEntity(unsigned int /*entity*/) {}

    // --- Render freeze (keeps last frame on screen while level is loading) ---
    virtual void setRenderFrozen(bool /*frozen*/) {}
    virtual bool isRenderFrozen() const { return false; }

    // --- Performance metrics ---
    virtual double getLastGpuFrameMs() const { return 0.0; }
    virtual double getLastCpuRenderWorldMs() const { return 0.0; }
    virtual double getLastCpuRenderUiMs() const { return 0.0; }
    virtual double getLastCpuUiLayoutMs() const { return 0.0; }
    virtual double getLastCpuUiDrawMs() const { return 0.0; }
    virtual double getLastCpuEcsMs() const { return 0.0; }
    virtual uint32_t getLastVisibleCount() const { return 0; }
    virtual uint32_t getLastHiddenCount() const { return 0; }
    virtual uint32_t getLastTotalCount() const { return 0; }

    /// Describes a single render pass for the Render-Pass-Debugger tab.
    struct RenderPassInfo
    {
        std::string name;          // e.g. "Shadow Map", "Geometry", "Bloom"
        std::string category;      // e.g. "Shadow", "Geometry", "Post-Process", "Overlay"
        bool        enabled{true}; // whether the pass is currently active
        int         fboWidth{0};
        int         fboHeight{0};
        std::string fboFormat;     // e.g. "RGBA16F", "Depth24", "R8"
        std::string details;       // extra info (draw counts, cascade count, etc.)
    };
    /// Returns info about all render passes in the current pipeline.
    virtual std::vector<RenderPassInfo> getRenderPassInfo() const { return {}; }

    // --- Multi-Viewport Layout (Phase 11.1) ---
    // Enum/struct definitions moved to RendererEnums.h; using-aliases at class top.

#if ENGINE_EDITOR
    void setViewportLayout(ViewportLayout layout) override { m_viewportLayout = layout; }
    ViewportLayout getViewportLayout() const override { return m_viewportLayout; }
    int  getActiveSubViewport() const override { return m_activeSubViewport; }
    void setActiveSubViewport(int index) override { m_activeSubViewport = index; }
    int  getSubViewportCount() const override
    {
        switch (m_viewportLayout)
        {
        case ViewportLayout::TwoHorizontal:
        case ViewportLayout::TwoVertical:   return 2;
        case ViewportLayout::Quad:           return 4;
        default:                             return 1;
        }
    }
    SubViewportCamera getSubViewportCamera(int /*index*/) const override { return {}; }
    void setSubViewportCamera(int /*index*/, const SubViewportCamera& /*cam*/) override {}
    /// Returns which sub-viewport (0-based) contains the given screen position, or -1.
    int subViewportHitTest(int /*screenX*/, int /*screenY*/) const override { return 0; }

    static const char* viewportLayoutToString(ViewportLayout l)
    {
        switch (l)
        {
        case ViewportLayout::TwoHorizontal: return "Two Horizontal";
        case ViewportLayout::TwoVertical:   return "Two Vertical";
        case ViewportLayout::Quad:          return "Quad";
        default:                            return "Single";
        }
    }

    static const char* subViewportPresetToString(SubViewportPreset p)
    {
        switch (p)
        {
        case SubViewportPreset::Top:   return "Top";
        case SubViewportPreset::Front: return "Front";
        case SubViewportPreset::Right: return "Right";
        default:                       return "Perspective";
        }
    }
#endif // ENGINE_EDITOR — Multi-Viewport Layout

    // --- Level-Streaming (Phase 11.4) ---
    struct SubLevelEntry
    {
        std::string name;              // display name
        std::string levelPath;         // relative asset path
        bool        loaded{ false };
        bool        visible{ true };
        Vec4        color{ 0.2f, 0.6f, 1.0f, 1.0f }; // wireframe tint
    };

    struct StreamingVolume
    {
        Vec3 center{ 0.0f, 0.0f, 0.0f };
        Vec3 halfExtents{ 10.0f, 10.0f, 10.0f };
        int  subLevelIndex{ -1 };      // index into m_subLevels
    };

    const std::vector<SubLevelEntry>& getSubLevels() const { return m_subLevels; }
    const std::vector<StreamingVolume>& getStreamingVolumes() const { return m_streamingVolumes; }

    void addSubLevel(const std::string& name, const std::string& path)
    {
        SubLevelEntry e;
        e.name      = name;
        e.levelPath = path;
        // Assign a unique colour per sub-level (cycle through a small palette)
        static const Vec4 palette[] = {
            { 0.2f, 0.6f, 1.0f, 1.0f },  // blue
            { 0.2f, 0.9f, 0.3f, 1.0f },  // green
            { 1.0f, 0.6f, 0.1f, 1.0f },  // orange
            { 0.9f, 0.2f, 0.4f, 1.0f },  // red
            { 0.7f, 0.3f, 1.0f, 1.0f },  // purple
            { 1.0f, 0.9f, 0.2f, 1.0f },  // yellow
        };
        e.color = palette[m_subLevels.size() % 6];
        m_subLevels.push_back(e);
    }

    void removeSubLevel(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_subLevels.size())) return;
        // Remove associated streaming volumes
        for (auto it = m_streamingVolumes.begin(); it != m_streamingVolumes.end();)
        {
            if (it->subLevelIndex == index) it = m_streamingVolumes.erase(it);
            else
            {
                if (it->subLevelIndex > index) --it->subLevelIndex;
                ++it;
            }
        }
        m_subLevels.erase(m_subLevels.begin() + index);
    }

    void setSubLevelLoaded(int index, bool loaded)
    {
        if (index >= 0 && index < static_cast<int>(m_subLevels.size()))
            m_subLevels[index].loaded = loaded;
    }

    void setSubLevelVisible(int index, bool visible)
    {
        if (index >= 0 && index < static_cast<int>(m_subLevels.size()))
            m_subLevels[index].visible = visible;
    }

    void addStreamingVolume(const Vec3& center, const Vec3& halfExtents, int subLevelIndex)
    {
        m_streamingVolumes.push_back({ center, halfExtents, subLevelIndex });
    }

    void removeStreamingVolume(int index)
    {
        if (index >= 0 && index < static_cast<int>(m_streamingVolumes.size()))
            m_streamingVolumes.erase(m_streamingVolumes.begin() + index);
    }

    /// Camera-based auto-load/unload: call once per frame with the active camera position.
    void updateLevelStreaming(const Vec3& cameraPos)
    {
        for (auto& vol : m_streamingVolumes)
        {
            if (vol.subLevelIndex < 0 || vol.subLevelIndex >= static_cast<int>(m_subLevels.size()))
                continue;

            bool inside =
                std::abs(cameraPos.x - vol.center.x) <= vol.halfExtents.x &&
                std::abs(cameraPos.y - vol.center.y) <= vol.halfExtents.y &&
                std::abs(cameraPos.z - vol.center.z) <= vol.halfExtents.z;

            m_subLevels[vol.subLevelIndex].loaded = inside;
        }
    }

    bool m_streamingVolumesVisible{ true };

protected:
#if ENGINE_EDITOR
    // Snap & Grid state
    bool  m_snapEnabled{ false };
    bool  m_gridVisible{ true };
    float m_gridSize{ 1.0f };
    float m_rotationSnapDeg{ 15.0f };
    float m_scaleSnapStep{ 0.1f };
    bool  m_collidersVisible{ false };
    bool  m_bonesVisible{ false };

    // Multi-Viewport state
    ViewportLayout m_viewportLayout{ ViewportLayout::Single };
    int m_activeSubViewport{ 0 };
#endif // ENGINE_EDITOR

    // Level-Streaming state (Phase 11.4)
    std::vector<SubLevelEntry>   m_subLevels;
    std::vector<StreamingVolume> m_streamingVolumes;
};
