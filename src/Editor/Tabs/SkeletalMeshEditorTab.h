#pragma once

#include "IEditorTab.h"
#include <string>
#include "../../AssetManager/json.hpp"

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab dedicated to SkeletalMesh assets.
/// Shows mesh statistics, the embedded bone hierarchy and the list of
/// animation clips stored alongside the skeletal mesh. Read-only for now –
/// skeletal data is authored by the importer, the tab exposes it so the
/// user can verify that bones, weights and animations were persisted.
class SkeletalMeshEditorTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        std::string assetPath;          // content-relative path
        std::string assetName;          // display name
        nlohmann::json meshData;        // raw "data" object from the asset file
        bool        isOpen{ false };
        float       refreshTimer{ 0.0f };
    };

    explicit SkeletalMeshEditorTab(UIManager* uiManager, Renderer* renderer);

    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Open the tab for a specific skeletal mesh asset (content-relative path).
    void open(const std::string& assetPath);

    /// Rebuild the panels from the current meshData.
    void refresh();

    const State& getState() const { return m_state; }

private:
    void buildToolbar(WidgetElement& root);
    void buildBodyPanel(WidgetElement& root);
    void buildStatsSection(WidgetElement& parent);
    void buildBonesSection(WidgetElement& parent);
    void buildAnimationsSection(WidgetElement& parent);

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
