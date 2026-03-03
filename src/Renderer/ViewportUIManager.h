#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Core/MathTypes.h"
#include "UIWidget.h"
#include "../AssetManager/json.hpp"

class ViewportUIManager
{
public:
    using SelectionChangedCallback = std::function<void(const std::string& elementId)>;

    ViewportUIManager();
    ~ViewportUIManager();

    void setViewportRect(float x, float y, float width, float height);
    Vec4 getViewportRect() const;
    Vec2 getViewportSize() const;

    // --- Multi-widget management ---
    bool createWidget(const std::string& name, int zOrder = 0);
    bool removeWidget(const std::string& name);
    Widget* getWidget(const std::string& name);
    const Widget* getWidget(const std::string& name) const;
    void clearAllWidgets();
    bool hasWidgets() const;

    struct WidgetEntry
    {
        std::string name;
        std::shared_ptr<Widget> widget;
    };
    const std::vector<WidgetEntry>& getSortedWidgets() const;

    // --- Element access (searches all widgets) ---
    WidgetElement* findElementById(const std::string& elementId);
    WidgetElement* findElementById(const std::string& widgetName, const std::string& elementId);

    void updateLayout(const std::function<Vec2(const std::string&, float)>& measureText);
    bool needsLayoutUpdate() const;
    void markLayoutDirty();

    bool handleMouseDown(const Vec2& windowPos, int button);
    bool handleMouseUp(const Vec2& windowPos, int button);
    bool handleScroll(const Vec2& windowPos, float delta);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key);
    void setMousePosition(const Vec2& windowPos);
    bool isPointerOverViewportUI(const Vec2& windowPos) const;

    bool isRenderDirty() const;
    void clearRenderDirty();

    void setSelectedElementId(const std::string& elementId);
    const std::string& getSelectedElementId() const;
    void setOnSelectionChanged(SelectionChangedCallback callback);

    void setVisible(bool visible);
    bool isVisible() const;

    // --- Gameplay cursor control ---
    void setGameplayCursorVisible(bool visible);
    bool isGameplayCursorVisible() const;

    nlohmann::json toJson() const;
    bool loadFromJson(const nlohmann::json& data);

private:
    Vec2 windowToViewport(const Vec2& windowPos) const;
    bool isInsideViewport(const Vec2& windowPos) const;
    WidgetElement* hitTest(const Vec2& viewportLocalPos);
    const WidgetElement* hitTestConst(const Vec2& viewportLocalPos) const;
    void sortWidgetsIfNeeded();

private:
    Vec4 m_viewportRect{};
    std::vector<WidgetEntry> m_widgets;
    bool m_widgetOrderDirty{ false };

    std::string m_selectedElementId;
    std::string m_pressedElementId;
    SelectionChangedCallback m_onSelectionChanged;
    bool m_visible{ true };
    bool m_layoutDirty{ true };
    bool m_renderDirty{ true };
    Vec2 m_mousePosition{};
    bool m_gameplayCursorVisible{ false };
};
