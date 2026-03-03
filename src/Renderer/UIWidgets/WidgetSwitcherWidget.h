#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Container that shows only one child at a time, selected by activeChildIndex.
// All children are measured but only the active one is arranged and rendered.
class WidgetSwitcherWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setActiveChildIndex(int index) { m_activeChildIndex = index; }
    int getActiveChildIndex() const { return m_activeChildIndex; }

    void setChildren(const std::vector<WidgetElement>& children) { m_children = children; }
    const std::vector<WidgetElement>& getChildren() const { return m_children; }

    void addChild(const WidgetElement& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::WidgetSwitcher;
        el.id = m_id;
        el.activeChildIndex = m_activeChildIndex;
        el.children = m_children;
        return el;
    }

private:
    std::string m_id;
    int m_activeChildIndex{ 0 };
    std::vector<WidgetElement> m_children;
};
