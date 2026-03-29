#pragma once

#include "IEditorTab.h"
#include <string>
#include <cstdint>

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab: Console / Log-Viewer.
/// Extracted from UIManager.cpp – owns the ConsoleState and all
/// open/close/refresh/build logic that was previously inline.
class ConsoleTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        uint64_t    lastSeenSequenceId{ 0 };
        uint8_t     levelFilter{ 0xFF }; // bitmask: bit0=INFO, bit1=WARNING, bit2=ERROR, bit3=FATAL
        std::string searchText;
        bool        autoScroll{ true };
        bool        isOpen{ false };
        float       refreshTimer{ 0.0f };
    };

    explicit ConsoleTab(UIManager* uiManager, Renderer* renderer);

    // IEditorTab
    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Rebuild the log area from Logger ring-buffer.
    void refreshLog();

    /// Read-only access to the state (e.g. for external queries).
    const State& getState() const { return m_state; }

private:
    void buildToolbar(WidgetElement& root);

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
