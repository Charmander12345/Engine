#pragma once

#include "IEditorTab.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

class NotificationsTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        bool        isOpen{ false };
        float       refreshTimer{ 0.0f };
    };

    NotificationsTab(UIManager* uiManager, Renderer* renderer);

    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

private:
    void buildToolbar(WidgetElement& root);
    void refresh();

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
