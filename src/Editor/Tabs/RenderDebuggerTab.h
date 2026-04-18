#pragma once

#include "IEditorTab.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

// ===========================================================================
// RenderDebuggerTab – extracted from UIManager (Section 1.1)
// Displays render-pass pipeline state, frame timings, and flow diagram.
// ===========================================================================
class RenderDebuggerTab : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        bool        isOpen{ false };
        float       refreshTimer{ 0.0f };
    };

    RenderDebuggerTab(UIManager* uiManager, Renderer* renderer);
    ~RenderDebuggerTab() override = default;

    // IEditorTab interface
    void               open() override;
    void               close() override;
    bool               isOpen() const override;
    void               update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

private:
    void refresh();
    void buildToolbar(WidgetElement& root);

    UIManager* m_uiManager = nullptr;
    Renderer*  m_renderer  = nullptr;
    State      m_state;
};
