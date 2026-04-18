#pragma once
#include "IEditorTab.h"
#include <string>
#include <memory>

class UIManager;
class Renderer;
struct WidgetElement;

class LevelCompositionTab : public IEditorTab
{
public:
    LevelCompositionTab(UIManager* uiManager, Renderer* renderer);
    ~LevelCompositionTab() override = default;

    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float /*deltaSeconds*/) override {}
    const std::string& getTabId() const override { return m_state.tabId; }

    void refresh();

private:
    void buildToolbar(WidgetElement& root);
    void buildSubLevelList(WidgetElement& root);
    void buildVolumeList(WidgetElement& root);

    struct State
    {
        std::string tabId;
        std::string widgetId;
        bool isOpen{ false };
        int selectedSubLevel{ -1 };
    };
    State m_state;

    UIManager* m_uiManager{ nullptr };
    Renderer*  m_renderer{ nullptr };
};
