#pragma once

#include "IEditorTab.h"
#include <string>
#include <memory>
#include "../../AssetManager/json.hpp"

class UIManager;
class Renderer;
struct WidgetElement;
class AssetData;

/// Editor tab: Entity Asset Editor.
/// Allows editing an entity template asset — adding/removing components,
/// editing properties (transform, mesh, material, physics, script, etc.),
/// and saving the result as a reusable .asset file.
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

    /// Rebuild the component list UI.
    void refresh();

    /// Save the entity asset to disk.
    void save();

    /// Read-only access to the state.
    const State& getState() const { return m_state; }

private:
    void buildToolbar(WidgetElement& root);
    void buildComponentList(WidgetElement& root);
    void buildAddComponentMenu(WidgetElement& root);

    void buildTransformSection(WidgetElement& parent);
    void buildMeshSection(WidgetElement& parent);
    void buildMaterialSection(WidgetElement& parent);
    void buildLightSection(WidgetElement& parent);
    void buildCameraSection(WidgetElement& parent);
    void buildPhysicsSection(WidgetElement& parent);
    void buildCollisionSection(WidgetElement& parent);
    void buildScriptSection(WidgetElement& parent);
    void buildNameSection(WidgetElement& parent);
    void buildAnimationSection(WidgetElement& parent);
    void buildParticleEmitterSection(WidgetElement& parent);

    void addComponent(const std::string& componentName);
    void removeComponent(const std::string& componentName);

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
