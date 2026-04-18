#pragma once

#include "IEditorTab.h"
#include "../../Core/Actor/ActorAssetData.h"
#include "../../Core/EngineLevel.h"
#include <string>
#include <vector>
#include <memory>

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab: Actor Asset Editor.
/// Shows a 3D preview viewport (rendering only the actor's mesh hierarchy)
/// and a scrollable sidebar with collapsible sections exposing all actor
/// capabilities: identity, transform, tick settings, visuals, child actors,
/// default components, and embedded script.
class ActorEditorTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        std::string assetPath;          // content-relative path of the .asset file
        std::string assetName;          // display name
        ActorAssetData actorData;       // in-memory actor data
        bool        isOpen{ false };
        bool        isDirty{ false };
    };

    explicit ActorEditorTab(UIManager* uiManager, Renderer* renderer);

    // IEditorTab
    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Open for a specific actor asset path (content-relative).
    void open(const std::string& assetPath);

    /// Rebuild the sidebar.
    void refresh();

    /// Save the actor asset to disk.
    void save();

    /// Read-only access to the state.
    const State& getState() const { return m_state; }

    // ── Runtime level for preview rendering (like MeshViewerWindow) ──
    std::unique_ptr<EngineLevel> takeRuntimeLevel();
    void giveRuntimeLevel(std::unique_ptr<EngineLevel> level);
    EngineLevel* getRuntimeLevel() const { return m_runtimeLevel.get(); }

    /// Initial camera placement for the preview.
    Vec3 getInitialCameraPosition() const { return m_previewCamPos; }
    Vec2 getInitialCameraRotation() const { return m_previewCamRot; }

    /// Handle a drag-and-drop from the Content Browser onto the viewport area.
    /// Returns true if the drop was consumed.
    bool handleViewportDrop(const std::string& payload, const Vec2& screenPos);

private:
    // ── Layout builders ──────────────────────────────────────────────
    void buildToolbar(WidgetElement& root);
    void buildSidebar(WidgetElement& sidebar);

    // ── Collapsible section builders ─────────────────────────────────
    void buildIdentitySection(WidgetElement& parent);
    void buildTransformSection(WidgetElement& parent);
    void buildTickSettingsSection(WidgetElement& parent);
    void buildVisualsSection(WidgetElement& parent);
    void buildChildActorList(WidgetElement& parent);
    void buildComponentsInfo(WidgetElement& parent);
    void buildScriptDetails(WidgetElement& parent);

    // ── Helpers ──────────────────────────────────────────────────────
    void addChildActor(const std::string& actorClass);
    void removeChildActor(size_t index);

    /// Collect registered actor class names for dropdown.
    std::vector<std::string> getActorClassNames() const;

    /// Get human-readable list of default components for an actor class.
    std::vector<std::string> getComponentsForClass(const std::string& actorClass) const;

    /// Build the preview runtime level from the current actor data.
    void rebuildPreviewLevel();

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;

    // ── Preview viewport ─────────────────────────────────────────────
    std::unique_ptr<EngineLevel> m_runtimeLevel;
    Vec3 m_previewCamPos{ 0.0f, 1.0f, 3.0f };
    Vec2 m_previewCamRot{ -90.0f, -10.0f }; // yaw, pitch (looking at origin)

    // Entity ID mapping: index 0 = root, 1..N = child actors (depth-first order)
    // Used to sync gizmo transform edits back to ActorAssetData.
    std::vector<unsigned int> m_previewEntities;
};
