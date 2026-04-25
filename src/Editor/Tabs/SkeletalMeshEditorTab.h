#pragma once

#include "IEditorTab.h"
#include <string>
#include <vector>
#include <memory>
#include "../../Core/MathTypes.h"
#include "../../Core/SkeletalData.h"
#include "../../AssetManager/json.hpp"

class UIManager;
class Renderer;
class EngineLevel;
struct WidgetElement;
struct TreeViewNode;

/// Editor tab for SkeletalMesh assets with hierarchical bone visualization.
/// Layout: Left panel shows bone tree, middle panel shows 3D preview, right panel shows stats/animations.
class SkeletalMeshEditorTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        std::string assetPath;
        std::string assetName;
        nlohmann::json meshData;
        bool isOpen{ false };
        float refreshTimer{ 0.0f };
        std::string selectedBoneId;
    };

    explicit SkeletalMeshEditorTab(UIManager* uiManager, Renderer* renderer);
    ~SkeletalMeshEditorTab();

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

    // ── Runtime level for preview rendering ──
    std::unique_ptr<EngineLevel> takeRuntimeLevel();
    void giveRuntimeLevel(std::unique_ptr<EngineLevel> level);
    EngineLevel* getRuntimeLevel() const { return m_runtimeLevel.get(); }

    Vec3 getInitialCameraPosition() const { return m_previewCamPos; }
    Vec2 getInitialCameraRotation() const { return m_previewCamRot; }

private:
    void buildToolbar(WidgetElement& root);
    void buildBodyPanel(WidgetElement& root);
    void buildBoneHierarchy(std::vector<TreeViewNode>& nodes);
    void buildStatsSection(WidgetElement& parent);
    void buildAnimationsSection(WidgetElement& parent);
    void onBoneSelected(const std::string& boneId);
    void rebuildPreviewLevel();
    void parseSkeleton();             ///< fills m_skeleton from m_state.meshData
    void pushSkeletonOverlay();       ///< calls renderer->setSkeletonTabOverlay

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;

    // ── Preview viewport ──
    std::unique_ptr<EngineLevel> m_runtimeLevel;
    Skeleton m_skeleton;                           ///< parsed bind-pose skeleton for overlay
    Vec3 m_previewCamPos{ 2.0f, 1.5f, 2.0f };
    Vec2 m_previewCamRot{ 225.0f, -20.0f };
};
