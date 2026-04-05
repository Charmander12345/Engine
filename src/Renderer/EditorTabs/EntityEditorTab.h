#pragma once

#include "IEditorTab.h"
#include <string>
#include <memory>
#include <vector>
#include "../../AssetManager/json.hpp"

class UIManager;
class Renderer;
struct WidgetElement;
class AssetData;

/// Editor tab: Entity Asset Editor.
/// Split layout: component list on the left, detail properties on the right.
/// Asset-referencing properties (mesh, material, script, texture) use dropdowns
/// populated from the project's asset registry.
class EntityEditorTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        std::string assetPath;          // content-relative path of the .asset file
        std::string assetName;          // display name
        nlohmann::json entityData;      // in-memory entity component data
        bool        isOpen{ false };
        bool        isDirty{ false };
        float       refreshTimer{ 0.0f };
        std::string selectedComponent;  // currently selected component key (e.g. "Transform")
    };

    explicit EntityEditorTab(UIManager* uiManager, Renderer* renderer);

    // IEditorTab
    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Open for a specific entity asset path (content-relative).
    void open(const std::string& assetPath);

    /// Rebuild both panels.
    void refresh();

    /// Save the entity asset to disk.
    void save();

    /// Read-only access to the state.
    const State& getState() const { return m_state; }

private:
    // ── Layout builders ──────────────────────────────────────────────
    void buildToolbar(WidgetElement& root);
    void buildComponentListPanel(WidgetElement& parent);
    void buildDetailsPanel(WidgetElement& parent);
    void buildAddComponentMenu(WidgetElement& parent);

    // ── Per-component detail builders ────────────────────────────────
    void buildTransformDetails(WidgetElement& parent);
    void buildMeshDetails(WidgetElement& parent);
    void buildMaterialDetails(WidgetElement& parent);
    void buildLightDetails(WidgetElement& parent);
    void buildCameraDetails(WidgetElement& parent);
    void buildPhysicsDetails(WidgetElement& parent);
    void buildCollisionDetails(WidgetElement& parent);
    void buildScriptDetails(WidgetElement& parent);
    void buildNameDetails(WidgetElement& parent);
    void buildAnimationDetails(WidgetElement& parent);
    void buildParticleEmitterDetails(WidgetElement& parent);

    // ── Helpers ──────────────────────────────────────────────────────
    void addComponent(const std::string& componentName);
    void removeComponent(const std::string& componentName);
    void selectComponent(const std::string& componentName);

    /// Collect asset paths of a given type from the registry for dropdown use.
    std::vector<std::string> getAssetPathsByType(int assetType) const;

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
