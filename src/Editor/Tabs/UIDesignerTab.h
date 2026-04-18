#pragma once
#include "IEditorTab.h"
#include <string>
#include <functional>
#include <vector>

class UIManager;
class Renderer;
class ViewportUIManager;
struct WidgetElement;

class UIDesignerTab : public IEditorTab
{
public:
    UIDesignerTab(UIManager* uiManager, Renderer* renderer);
    ~UIDesignerTab() override = default;

    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float /*deltaSeconds*/) override {}
    const std::string& getTabId() const override { return m_state.tabId; }

    void selectElement(const std::string& widgetName, const std::string& elementId);
    void addElementToViewportWidget(const std::string& elementType);
    void deleteSelectedElement();
    void refreshHierarchy();
    void refreshDetails();

    const std::string& getSelectedWidgetName() const { return m_state.selectedWidgetName; }
    const std::string& getSelectedElementId()  const { return m_state.selectedElementId; }

private:
    ViewportUIManager* getViewportUIManager() const;

    struct State
    {
        std::string tabId;
        std::string leftWidgetId;
        std::string rightWidgetId;
        std::string toolbarWidgetId;
        std::string selectedWidgetName;
        std::string selectedElementId;
        bool isOpen{ false };
    };
    State m_state;

    UIManager* m_uiManager{ nullptr };
    Renderer*  m_renderer{ nullptr };
};
