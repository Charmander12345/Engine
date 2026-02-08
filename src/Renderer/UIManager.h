#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

#include "../Core/MathTypes.h"
#include "UIWidget.h"

class UIManager
{
public:
    struct UIEntry
    {
        std::string id;
    };

    struct WidgetEntry
    {
        std::string id;
        std::shared_ptr<Widget> widget;
    };

    UIManager() = default;
    ~UIManager() = default;

    Vec2 getAvailableViewportSize() const;
    void setAvailableViewportSize(const Vec2& size);

    void registerUI(const std::string& id);
    const std::vector<UIEntry>& getRegisteredUI() const;

    void registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget);
    const std::vector<WidgetEntry>& getRegisteredWidgets() const;
    void unregisterWidget(const std::string& id);

    void updateLayouts(const std::function<Vec2(const std::string&, float)>& measureText);
    bool handleMouseDown(const Vec2& screenPos, int button);
    void setMousePosition(const Vec2& screenPos);
    bool isPointerOverUI(const Vec2& screenPos) const;
    bool hasClickEvent(const std::string& eventId) const;
    void registerClickEvent(const std::string& eventId, std::function<void()> callback);

private:
    WidgetElement* hitTest(const Vec2& screenPos, bool logDetails = false) const;
    void populateOutlinerWidget(const std::shared_ptr<Widget>& widget);

    Vec2 m_availableViewportSize{};
    Vec2 m_mousePosition{};
    bool m_hasMousePosition{ false };
    std::unordered_map<std::string, std::function<void()>> m_clickEvents;
    std::vector<UIEntry> m_entries;
    std::vector<WidgetEntry> m_widgets;

    void bindClickEventsForWidget(const std::shared_ptr<Widget>& widget);
    void bindClickEventsForElement(WidgetElement& element);
};
