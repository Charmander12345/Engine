#pragma once

#include <string>
#include <vector>
#include <functional>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Scrollable container that wraps a StackPanel or Grid.
class ScrollViewWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setOrientation(StackOrientation orientation) { m_orientation = orientation; }
    StackOrientation getOrientation() const { return m_orientation; }

    void setColor(const Vec4& color) { m_color = color; }
    const Vec4& getColor() const { return m_color; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setFillX(bool fill) { m_fillX = fill; }
    bool getFillX() const { return m_fillX; }

    void setFillY(bool fill) { m_fillY = fill; }
    bool getFillY() const { return m_fillY; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setMaxSize(const Vec2& size) { m_maxSize = size; }
    const Vec2& getMaxSize() const { return m_maxSize; }

    void setSpacing(float spacing) { m_spacing = spacing; }
    float getSpacing() const { return m_spacing; }

    void setChildren(const std::vector<WidgetElement>& children) { m_children = children; }
    const std::vector<WidgetElement>& getChildren() const { return m_children; }
    void addChild(const WidgetElement& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::ScrollView;
        el.id = m_id;
        el.orientation = m_orientation;
        el.style.color = m_color;
        el.padding = m_padding;
        el.fillX = m_fillX;
        el.fillY = m_fillY;
        el.minSize = m_minSize;
        el.maxSize = m_maxSize;
        el.spacing = m_spacing;
        el.scrollable = true;
        el.sizeToContent = false;
        el.children = m_children;
        return el;
    }

private:
    std::string m_id;
    StackOrientation m_orientation{ StackOrientation::Vertical };
    Vec4 m_color{ 0.08f, 0.08f, 0.10f, 0.8f };
    Vec2 m_padding{ 4.0f, 4.0f };
    bool m_fillX{ true };
    bool m_fillY{ false };
    Vec2 m_minSize{ 0.0f, 0.0f };
    Vec2 m_maxSize{ 0.0f, 0.0f };
    float m_spacing{ 0.0f };
    std::vector<WidgetElement> m_children;
};
