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
/// Shows a 3D preview viewport (with axis lines) rendering the actor's
/// mesh hierarchy and a sidebar for editing child actors and scripts.
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
        std::string selectedSection;    // "ActorClass", "ChildActors", "Script"
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

    /// Rebuild both panels.
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

private:
    // ── Layout builders ──────────────────────────────────────────────
    void buildToolbar(WidgetElement& root);
    void buildSectionListPanel(WidgetElement& parent);
    void buildDetailsPanel(WidgetElement& parent);

    // ── Per-section detail builders ──────────────────────────────────
    void buildActorClassDetails(WidgetElement& parent);
    void buildChildActorList(WidgetElement& parent);
    void buildScriptDetails(WidgetElement& parent);

    // ── Helpers ──────────────────────────────────────────────────────
    void addChildActor(const std::string& actorClass);
    void removeChildActor(size_t index);
    void selectSection(const std::string& section);

    /// Collect registered actor class names for dropdown.
    std::vector<std::string> getActorClassNames() const;

    /// Build the preview runtime level from the current actor data.
    void rebuildPreviewLevel();

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;

    // ── Preview viewport ─────────────────────────────────────────────
    std::unique_ptr<EngineLevel> m_runtimeLevel;
    Vec3 m_previewCamPos{ 0.0f, 1.0f, 3.0f };
    Vec2 m_previewCamRot{ -90.0f, -10.0f }; // yaw, pitch (looking at origin)
};
