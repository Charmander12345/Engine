#pragma once
#if ENGINE_EDITOR

/// @file IEditorRenderer.h
/// @brief Pure virtual interface for editor-only renderer functionality.
///
/// Extracted from Renderer.h (Phase 10 of the editor-separation plan).
/// The runtime Renderer base class conditionally inherits this interface
/// when ENGINE_EDITOR=1, making the editor methods part of the vtable.
/// Concrete backends (OpenGLRenderer, etc.) override these methods.

#include "RendererEnums.h"
#include "../Core/MathTypes.h"

#include <string>
#include <unordered_set>

// Forward declarations
union SDL_Event;
class PopupWindow;
class MeshViewerWindow;
class MaterialEditorWindow;
class TextureViewerWindow;

class IEditorRenderer
{
public:
    virtual ~IEditorRenderer() = default;

    // --- Debug render mode ---
    virtual DebugRenderMode getDebugRenderMode() const = 0;
    virtual void setDebugRenderMode(DebugRenderMode mode) = 0;

    // --- Debug visualizations ---
    virtual bool isUIDebugEnabled() const = 0;
    virtual void toggleUIDebug() = 0;
    virtual bool isBoundsDebugEnabled() const = 0;
    virtual void toggleBoundsDebug() = 0;
    virtual bool isHeightFieldDebugEnabled() const = 0;
    virtual void setHeightFieldDebugEnabled(bool enabled) = 0;
    virtual void setCollidersVisible(bool visible) = 0;
    virtual bool isCollidersVisible() const = 0;
    virtual void setBonesVisible(bool visible) = 0;
    virtual bool isBonesVisible() const = 0;

    // --- Entity picking ---
    virtual unsigned int pickEntityAt(int x, int y) = 0;
    virtual unsigned int pickEntityAtImmediate(int x, int y) = 0;
    virtual void requestPick(int screenX, int screenY, bool ctrlHeld = false) = 0;
    virtual unsigned int getSelectedEntity() const = 0;
    virtual void setSelectedEntity(unsigned int entity) = 0;

    // --- Multi-select ---
    virtual const std::unordered_set<unsigned int>& getSelectedEntities() const = 0;
    virtual void addSelectedEntity(unsigned int entity) = 0;
    virtual void removeSelectedEntity(unsigned int entity) = 0;
    virtual void clearSelection() = 0;
    virtual bool isEntitySelected(unsigned int entity) const = 0;
    virtual void focusOnSelectedEntity() = 0;

    // --- Rubber-band (marquee) selection ---
    virtual void beginRubberBand(int screenX, int screenY) = 0;
    virtual void updateRubberBand(int screenX, int screenY) = 0;
    virtual void endRubberBand(bool ctrlHeld = false) = 0;
    virtual void cancelRubberBand() = 0;
    virtual bool isRubberBandActive() const = 0;
    virtual Vec2 getRubberBandStart() const = 0;

    // --- Gizmo ---
    virtual void setGizmoMode(GizmoMode mode) = 0;
    virtual GizmoMode getGizmoMode() const = 0;
    virtual bool beginGizmoDrag(int screenX, int screenY) = 0;
    virtual void updateGizmoDrag(int screenX, int screenY) = 0;
    virtual void endGizmoDrag() = 0;
    virtual bool isGizmoDragging() const = 0;

    // --- Snap & Grid ---
    virtual void setSnapEnabled(bool enabled) = 0;
    virtual bool isSnapEnabled() const = 0;
    virtual void setGridVisible(bool visible) = 0;
    virtual bool isGridVisible() const = 0;
    virtual void setGridSize(float size) = 0;
    virtual float getGridSize() const = 0;
    virtual void setRotationSnapDeg(float deg) = 0;
    virtual float getRotationSnapDeg() const = 0;
    virtual void setScaleSnapStep(float step) = 0;
    virtual float getScaleSnapStep() const = 0;

    // --- Editor tabs ---
    virtual void addTab(const std::string& id, const std::string& name, bool closable) = 0;
    virtual void removeTab(const std::string& id) = 0;
    virtual void setActiveTab(const std::string& id) = 0;
    virtual const std::string& getActiveTabId() const = 0;
    virtual void cleanupWidgetEditorPreview(const std::string& tabId) = 0;

    // --- Popup windows ---
    virtual PopupWindow* openPopupWindow(const std::string& id, const std::string& title, int width, int height) = 0;
    virtual void closePopupWindow(const std::string& id) = 0;
    virtual PopupWindow* getPopupWindow(const std::string& id) = 0;
    virtual bool routeEventToPopup(SDL_Event& event) = 0;

    // --- Mesh viewer ---
    virtual MeshViewerWindow* openMeshViewer(const std::string& assetPath) = 0;
    virtual void closeMeshViewer(const std::string& assetPath) = 0;
    virtual MeshViewerWindow* getMeshViewer(const std::string& assetPath) = 0;

    // --- Texture viewer ---
    virtual TextureViewerWindow* openTextureViewer(const std::string& assetPath) = 0;
    virtual void closeTextureViewer(const std::string& assetPath) = 0;
    virtual TextureViewerWindow* getTextureViewer(const std::string& assetPath) = 0;

    // --- Material editor ---
    virtual MaterialEditorWindow* openMaterialEditorTab(const std::string& assetPath) = 0;
    virtual void closeMaterialEditorTab(const std::string& assetPath) = 0;
    virtual MaterialEditorWindow* getMaterialEditor(const std::string& assetPath) = 0;

    // --- Multi-Viewport ---
    virtual void setViewportLayout(ViewportLayout layout) = 0;
    virtual ViewportLayout getViewportLayout() const = 0;
    virtual int getActiveSubViewport() const = 0;
    virtual void setActiveSubViewport(int index) = 0;
    virtual int getSubViewportCount() const = 0;
    virtual SubViewportCamera getSubViewportCamera(int index) const = 0;
    virtual void setSubViewportCamera(int index, const SubViewportCamera& cam) = 0;
    virtual int subViewportHitTest(int screenX, int screenY) const = 0;
};

#endif // ENGINE_EDITOR
