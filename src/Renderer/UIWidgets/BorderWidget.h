#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// A dedicated container element that wraps a single child with a configurable
// border (separate brush for background and border, per-side thickness).
class BorderWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setBackgroundBrush(const UIBrush& brush) { m_background = brush; }
    const UIBrush& getBackgroundBrush() const { return m_background; }

    void setBorderBrush(const UIBrush& brush) { m_borderBrush = brush; }
    const UIBrush& getBorderBrush() const { return m_borderBrush; }

    void setBorderThickness(float left, float top, float right, float bottom)
    {
        m_thicknessLeft = left;
        m_thicknessTop = top;
        m_thicknessRight = right;
        m_thicknessBottom = bottom;
    }

    void setContentPadding(const Vec2& padding) { m_contentPadding = padding; }
    const Vec2& getContentPadding() const { return m_contentPadding; }

    void setChild(const WidgetElement& child) { m_child = child; m_hasChild = true; }
    bool hasChild() const { return m_hasChild; }
    const WidgetElement& getChild() const { return m_child; }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::Border;
        el.id = m_id;
        el.background = m_background;
        el.borderBrush = m_borderBrush;
        el.borderThicknessLeft = m_thicknessLeft;
        el.borderThicknessTop = m_thicknessTop;
        el.borderThicknessRight = m_thicknessRight;
        el.borderThicknessBottom = m_thicknessBottom;
        el.contentPadding = m_contentPadding;
        if (m_hasChild)
            el.children.push_back(m_child);
        return el;
    }

private:
    std::string m_id;
    UIBrush m_background;
    UIBrush m_borderBrush;
    float m_thicknessLeft{ 2.0f };
    float m_thicknessTop{ 2.0f };
    float m_thicknessRight{ 2.0f };
    float m_thicknessBottom{ 2.0f };
    Vec2 m_contentPadding{ 4.0f, 4.0f };
    WidgetElement m_child;
    bool m_hasChild{ false };
};
