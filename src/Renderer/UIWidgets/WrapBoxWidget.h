#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Container that lays out children in a row/column and wraps to the next
// line when the available space is exceeded (like a text flow).
class WrapBoxWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setOrientation(StackOrientation orientation) { m_orientation = orientation; }
    StackOrientation getOrientation() const { return m_orientation; }

    void setSpacing(float spacing) { m_spacing = spacing; }
    float getSpacing() const { return m_spacing; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChildren(const std::vector<WidgetElement>& children) { m_children = children; }
    const std::vector<WidgetElement>& getChildren() const { return m_children; }

    void addChild(const WidgetElement& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::WrapBox;
        el.id = m_id;
        el.orientation = m_orientation;
        el.spacing = m_spacing;
        el.padding = m_padding;
        el.children = m_children;
        return el;
    }

private:
    std::string m_id;
    StackOrientation m_orientation{ StackOrientation::Horizontal };
    float m_spacing{ 0.0f };
    Vec2 m_padding{ 0.0f, 0.0f };
    std::vector<WidgetElement> m_children;
};
