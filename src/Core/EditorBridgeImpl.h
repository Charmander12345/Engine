#pragma once
#if ENGINE_EDITOR

/// @file EditorBridgeImpl.h
/// @brief Concrete implementation of IEditorBridge that delegates to engine subsystems.

#include "IEditorBridge.h"

class Renderer;

/// Bridges the editor module to the real engine subsystems.
/// Instantiated in main() when ENGINE_EDITOR=1 and passed to EditorApp.
class EditorBridgeImpl final : public IEditorBridge
{
public:
    explicit EditorBridgeImpl(Renderer* renderer);
    ~EditorBridgeImpl() override = default;

    // ── Renderer / Window ───────────────────────────────────────────
    Renderer*            getRenderer() override;
    UIManager&           getUIManager() override;
    ViewportUIManager*   getViewportUIManager() override;
    SDL_Window*          getWindow() override;
    unsigned int         preloadUITexture(const std::string& path) override;
    std::shared_ptr<Widget> createWidgetFromAsset(const std::shared_ptr<AssetData>& asset) override;

    // ── Camera ──────────────────────────────────────────────────────
    Vec3 getCameraPosition() const override;
    Vec2 getCameraRotation() const override;
    void setCameraPosition(const Vec3& pos) override;
    void setCameraRotation(float yawDeg, float pitchDeg) override;
    void moveCamera(float forward, float right, float up) override;
    void rotateCamera(float yawDelta, float pitchDelta) override;

    // ── Entity ──────────────────────────────────────────────────────
    unsigned int createEntity(const std::string& name) override;
    void         removeEntity(unsigned int entity) override;
    void         selectEntity(unsigned int entity) override;
    unsigned int getSelectedEntity() const override;
    void         invalidateEntity(unsigned int entity) override;

    // ── Assets ──────────────────────────────────────────────────────
    int  loadAsset(const std::string& path, int type) override;
    std::shared_ptr<AssetData> getLoadedAssetByID(unsigned int id) override;
    bool saveAssets() override;
    bool importAsset(SDL_Window* window) override;
    bool importAssetFromPath(const std::string& filePath) override;
    bool deleteAsset(const std::string& path) override;
    bool moveAsset(const std::string& from, const std::string& to) override;
    std::string getAbsoluteContentPath(const std::string& rel) const override;
    std::string getProjectPath() const override;
    std::string getEditorWidgetPath(const std::string& name) const override;
    size_t getUnsavedAssetCount() const override;
    std::vector<AssetReference> findReferencesTo(const std::string& assetPath) const override;
    std::vector<std::string> getAssetDependencies(const std::string& assetPath) const override;
    void setOnImportCompleted(std::function<void()> callback) override;

    // ── Level ───────────────────────────────────────────────────────
    bool loadLevel(const std::string& relPath, std::string& outError) override;
    bool saveActiveLevel() override;
    std::string getActiveLevelName() const override;
    void captureEditorCameraToLevel() override;
    void restoreEditorCameraFromLevel() override;
    std::string getLevelSkyboxPath() const override;
    void setLevelSkyboxPath(const std::string& path) override;
    void setScenePrepared(bool prepared) override;

    // ── PIE ─────────────────────────────────────────────────────────
    bool isPIEActive() const override;
    void setPIEActive(bool active) override;
    void initializePhysicsForPIE() override;
    void shutdownPhysics() override;
    void snapshotEcsState() override;
    void restoreEcsSnapshot() override;
    unsigned int findActiveCameraEntity() const override;

    // ── Physics ─────────────────────────────────────────────────────
    RaycastResult raycastDown(float ox, float oy, float oz, float maxDist) override;

    // ── Diagnostics ─────────────────────────────────────────────────
    void setState(const std::string& key, const std::string& value) override;
    std::optional<std::string> getState(const std::string& key) const override;
    void requestShutdown() override;
    bool isShutdownRequested() const override;
    bool isProjectLoaded() const override;

    // ── Scripting ───────────────────────────────────────────────────
    void reloadScripts() override;
    void loadEditorPlugins(const std::string& projectPath) override;

    // ── Undo/Redo ───────────────────────────────────────────────────
    void pushUndoCommand(UndoCommand cmd) override;
    bool canUndo() const override;
    bool canRedo() const override;
    void undo() override;
    void redo() override;
    void clearUndoHistory() override;
    void setOnUndoRedoChanged(std::function<void()> callback) override;

    // ── Audio ───────────────────────────────────────────────────────
    void stopAllAudio() override;

private:
    Renderer* m_renderer{ nullptr };
};

#endif // ENGINE_EDITOR
