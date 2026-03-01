#pragma once

#include <string>
#include <memory>
#include <vector>
#include <SDL3/SDL.h>
#include "../Core/MathTypes.h"
#include "RendererCapabilities.h"

class UIManager;
class ViewportUIManager;
class PopupWindow;
class MeshViewerWindow;
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
{
public:
    enum class GizmoMode { None, Translate, Rotate, Scale };
    enum class GizmoAxis { None, X, Y, Z };

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

    // --- Debug visualizations ---
    virtual bool isUIDebugEnabled() const { return false; }
    virtual void toggleUIDebug() {}
    virtual bool isBoundsDebugEnabled() const { return false; }
    virtual void toggleBoundsDebug() {}
    virtual bool isHeightFieldDebugEnabled() const { return false; }
    virtual void setHeightFieldDebugEnabled(bool /*enabled*/) {}

    // --- Entity picking ---
    virtual unsigned int pickEntityAt(int /*x*/, int /*y*/) { return 0; }
    virtual unsigned int pickEntityAtImmediate(int /*x*/, int /*y*/) { return 0; }
    virtual void requestPick(int /*screenX*/, int /*screenY*/) {}
    virtual unsigned int getSelectedEntity() const { return 0; }
    virtual void setSelectedEntity(unsigned int /*entity*/) {}

    // --- Gizmo ---
    virtual void setGizmoMode(GizmoMode /*mode*/) {}
    virtual GizmoMode getGizmoMode() const { return GizmoMode::None; }
    virtual bool beginGizmoDrag(int /*screenX*/, int /*screenY*/) { return false; }
    virtual void updateGizmoDrag(int /*screenX*/, int /*screenY*/) {}
    virtual void endGizmoDrag() {}
    virtual bool isGizmoDragging() const { return false; }

    // --- Editor tabs ---
    virtual void addTab(const std::string& /*id*/, const std::string& /*name*/, bool /*closable*/) {}
    virtual void removeTab(const std::string& /*id*/) {}
    virtual void setActiveTab(const std::string& /*id*/) {}
    virtual const std::string& getActiveTabId() const { static const std::string s; return s; }

    // --- Popup windows ---
    virtual PopupWindow* openPopupWindow(const std::string& /*id*/, const std::string& /*title*/, int /*width*/, int /*height*/) { return nullptr; }
    virtual void closePopupWindow(const std::string& /*id*/) {}
    virtual PopupWindow* getPopupWindow(const std::string& /*id*/) { return nullptr; }
    virtual bool routeEventToPopup(SDL_Event& /*event*/) { return false; }

    // --- Mesh viewer ---
    virtual MeshViewerWindow* openMeshViewer(const std::string& /*assetPath*/) { return nullptr; }
    virtual void closeMeshViewer(const std::string& /*assetPath*/) {}
    virtual MeshViewerWindow* getMeshViewer(const std::string& /*assetPath*/) { return nullptr; }

    // --- Viewport & visuals ---
    virtual Vec2 getViewportSize() const { return {}; }
    virtual void setClearColor(const Vec4& /*color*/) {}
    virtual Vec4 getClearColor() const { return {}; }
    virtual void setSkyboxPath(const std::string& /*pathOrFolder*/) {}
    virtual std::string getSkyboxPath() const { return {}; }
    virtual void queueText(const std::string& /*text*/, const Vec2& /*screenPos*/, float /*scale*/, const Vec4& /*color*/) {}
    virtual std::shared_ptr<Widget> createWidgetFromAsset(const std::shared_ptr<AssetData>& /*asset*/) { return nullptr; }
    virtual unsigned int preloadUITexture(const std::string& /*path*/) { return 0; }

    // --- Scene management ---
    virtual void refreshEntity(unsigned int /*entity*/) {}

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
};
