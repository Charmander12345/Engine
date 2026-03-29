#pragma once

#include "IEditorTab.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab: Profiler / Performance-Monitor.
/// Extracted from UIManager.cpp – owns the ProfilerState and all
/// open/close/refresh/build logic that was previously inline.
class ProfilerTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        bool        isOpen{ false };
        bool        frozen{ false };
        float       refreshTimer{ 0.0f };
    };

    explicit ProfilerTab(UIManager* uiManager, Renderer* renderer);

    // IEditorTab
    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Rebuild the metrics area from DiagnosticsManager.
    void refreshMetrics();

    /// Read-only access to the state.
    const State& getState() const { return m_state; }

private:
    void buildToolbar(WidgetElement& root);

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
