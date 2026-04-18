#pragma once
#include "IEditorTab.h"
#include "../../Core/ECS/ECS.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

class AnimationEditorTab : public IEditorTab
{
public:
    AnimationEditorTab(UIManager* uiManager, Renderer* renderer);
    ~AnimationEditorTab() override = default;

    void open() override;
    void open(ECS::Entity entity);
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float /*deltaSeconds*/) override {}
    const std::string& getTabId() const override { return m_state.tabId; }

    ECS::Entity getLinkedEntity() const { return m_state.linkedEntity; }

    void refresh();

private:
    void buildToolbar(WidgetElement& root);
    void buildClipList(WidgetElement& root);
    void buildControls(WidgetElement& root);
    void buildBoneTree(WidgetElement& root);

    struct State
    {
        std::string tabId;
        std::string widgetId;
        ECS::Entity linkedEntity{ 0 };
        bool isOpen{ false };
        int selectedClip{ -1 };
    };
    State m_state;

    UIManager* m_uiManager{ nullptr };
    Renderer*  m_renderer{ nullptr };
};
