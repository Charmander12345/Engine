#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Container that stacks all its children on top of each other (same space).
// Each child can use its own H/V alignment to position itself within the
// overlay area. Unlike Canvas Panel, children are aligned rather than
// absolutely positioned.
class OverlayWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChildren(const std::vector<WidgetElement>& children) { m_children = children; }
    const std::vector<WidgetElement>& getChildren() const { return m_children; }

    void addChild(const WidgetElement& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::Overlay;
        el.id = m_id;
        el.padding = m_padding;
        el.children = m_children;
        return el;
    }

private:
    std::string m_id;
    Vec2 m_padding{ 0.0f, 0.0f };
    std::vector<WidgetElement> m_children;
};
