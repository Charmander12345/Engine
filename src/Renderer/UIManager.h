#pragma once

#include <string>
#include <vector>
#include <memory>

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

private:
    Vec2 m_availableViewportSize{};
    std::vector<UIEntry> m_entries;
    std::vector<WidgetEntry> m_widgets;
};
