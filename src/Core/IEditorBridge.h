#pragma once

/// @file IEditorBridge.h
/// @brief Abstract interface that the engine exposes to the editor module.
///
/// The editor only depends on this interface — never on engine internals
/// (ECS, PhysicsWorld, AssetManager singletons) directly.  The engine side
/// provides a concrete implementation (EditorBridgeImpl) that delegates to
/// the real subsystems.
///
/// This is the core of the editor-separation architecture: by programming
/// against IEditorBridge the entire editor can be compiled out for runtime
/// builds without touching engine code.

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <optional>
#include <utility>

#include "MathTypes.h"

// Forward declarations — the editor may use these via pointers/references
// returned by the bridge, but never #includes the implementing headers.
class Renderer;
class UIManager;
class ViewportUIManager;
class AssetData;
class Widget;
class EditorWidget;
struct SDL_Window;
union SDL_Event;

class IEditorBridge
{
public:
    virtual ~IEditorBridge() = default;

    // ═══════════════════════════════════════════════════════════════════
    //  Renderer / Window
    // ═══════════════════════════════════════════════════════════════════

    /// Raw renderer pointer.  The editor needs this for widget creation,
    /// texture preloading, gizmo/picking APIs, etc.
    virtual Renderer* getRenderer() = 0;

    /// The engine's UIManager (shared between editor and runtime).
    virtual UIManager& getUIManager() = 0;

    /// Optional ViewportUIManager for in-game UI overlay.
    virtual ViewportUIManager* getViewportUIManager() = 0;

    /// The main SDL window.
    virtual SDL_Window* getWindow() = 0;

    /// Preload an editor UI texture (e.g. Play.tga, Stop.tga) and return
    /// its GPU texture ID.
    virtual unsigned int preloadUITexture(const std::string& path) = 0;

    /// Create a renderable widget from a loaded asset.
    virtual std::shared_ptr<Widget> createWidgetFromAsset(const std::shared_ptr<AssetData>& asset) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Camera
    // ═══════════════════════════════════════════════════════════════════

    virtual Vec3 getCameraPosition() const = 0;
    virtual Vec2 getCameraRotation() const = 0;
    virtual void setCameraPosition(const Vec3& pos) = 0;
    virtual void setCameraRotation(float yawDeg, float pitchDeg) = 0;
    virtual void moveCamera(float forward, float right, float up) = 0;
    virtual void rotateCamera(float yawDelta, float pitchDelta) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Entity Operations (wraps ECS + DiagnosticsManager)
    // ═══════════════════════════════════════════════════════════════════

    /// Create a new entity with a display name.  Returns the entity ID.
    virtual unsigned int createEntity(const std::string& name) = 0;

    /// Remove an entity from the ECS and active level.
    virtual void removeEntity(unsigned int entity) = 0;

    /// Select an entity in the editor (updates UI + renderer highlight).
    virtual void selectEntity(unsigned int entity) = 0;

    /// Get the currently selected entity (0 = none).
    virtual unsigned int getSelectedEntity() const = 0;

    /// Mark an entity as dirty so the renderer rebuilds its renderables.
    virtual void invalidateEntity(unsigned int entity) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Asset Management
    // ═══════════════════════════════════════════════════════════════════

    /// Load an asset by content-relative path.  Returns asset ID (0 = failure).
    virtual int loadAsset(const std::string& path, int type) = 0;

    /// Get a loaded asset by ID.
    virtual std::shared_ptr<AssetData> getLoadedAssetByID(unsigned int id) = 0;

    /// Save all unsaved assets.
    virtual bool saveAssets() = 0;

    /// Open the native file dialog for asset import.
    virtual bool importAsset(SDL_Window* window) = 0;

    /// Import from a specific file path (e.g. OS drag-and-drop).
    virtual bool importAssetFromPath(const std::string& filePath) = 0;

    /// Delete an asset by content-relative path.
    virtual bool deleteAsset(const std::string& path) = 0;

    /// Move/rename an asset.
    virtual bool moveAsset(const std::string& from, const std::string& to) = 0;

    /// Resolve a content-relative path to an absolute filesystem path.
    virtual std::string getAbsoluteContentPath(const std::string& rel) const = 0;

    /// Get the absolute project root path.
    virtual std::string getProjectPath() const = 0;

    /// Resolve an editor widget asset name to its filesystem path.
    virtual std::string getEditorWidgetPath(const std::string& name) const = 0;

    /// Get the number of currently unsaved assets.
    virtual size_t getUnsavedAssetCount() const = 0;

    /// Find assets that reference the given asset.
    struct AssetReference { std::string sourcePath; std::string sourceType; };
    virtual std::vector<AssetReference> findReferencesTo(const std::string& assetPath) const = 0;

    /// Get assets that the given asset depends on.
    virtual std::vector<std::string> getAssetDependencies(const std::string& assetPath) const = 0;

    /// Set a callback invoked when an async import completes.
    virtual void setOnImportCompleted(std::function<void()> callback) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Level Management
    // ═══════════════════════════════════════════════════════════════════

    /// Load a level from a content-relative path.
    virtual bool loadLevel(const std::string& relPath, std::string& outError) = 0;

    /// Save the currently active level.
    virtual bool saveActiveLevel() = 0;

    /// Get the name of the currently active level (empty if none).
    virtual std::string getActiveLevelName() const = 0;

    /// Store the editor camera position/rotation in the active level.
    virtual void captureEditorCameraToLevel() = 0;

    /// Restore the editor camera from the active level's saved position.
    virtual void restoreEditorCameraFromLevel() = 0;

    /// Get/set the skybox path on the active level.
    virtual std::string getLevelSkyboxPath() const = 0;
    virtual void setLevelSkyboxPath(const std::string& path) = 0;

    /// Mark the scene as unprepared (forces rebuild on next render).
    virtual void setScenePrepared(bool prepared) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Play-in-Editor (PIE)
    // ═══════════════════════════════════════════════════════════════════

    virtual bool isPIEActive() const = 0;
    virtual void setPIEActive(bool active) = 0;

    /// Initialize physics for PIE with the configured backend and settings.
    virtual void initializePhysicsForPIE() = 0;

    /// Shutdown physics (called when PIE stops).
    virtual void shutdownPhysics() = 0;

    /// Snapshot / restore ECS state for PIE.
    virtual void snapshotEcsState() = 0;
    virtual void restoreEcsSnapshot() = 0;

    /// Find the first active camera entity for PIE.
    virtual unsigned int findActiveCameraEntity() const = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Physics (for editor tools like Drop-to-Surface)
    // ═══════════════════════════════════════════════════════════════════

    struct RaycastResult { bool hit{ false }; float hitY{ 0.0f }; };
    virtual RaycastResult raycastDown(float ox, float oy, float oz, float maxDist) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Diagnostics / Configuration
    // ═══════════════════════════════════════════════════════════════════

    virtual void setState(const std::string& key, const std::string& value) = 0;
    virtual std::optional<std::string> getState(const std::string& key) const = 0;
    virtual void requestShutdown() = 0;
    virtual bool isShutdownRequested() const = 0;
    virtual bool isProjectLoaded() const = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Scripting
    // ═══════════════════════════════════════════════════════════════════

    virtual void reloadScripts() = 0;
    virtual void loadEditorPlugins(const std::string& projectPath) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Undo/Redo
    // ═══════════════════════════════════════════════════════════════════

    struct UndoCommand
    {
        std::string description;
        std::function<void()> execute;
        std::function<void()> undo;
    };
    virtual void pushUndoCommand(UndoCommand cmd) = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void clearUndoHistory() = 0;
    virtual void setOnUndoRedoChanged(std::function<void()> callback) = 0;

    // ═══════════════════════════════════════════════════════════════════
    //  Audio
    // ═══════════════════════════════════════════════════════════════════

    virtual void stopAllAudio() = 0;
};
