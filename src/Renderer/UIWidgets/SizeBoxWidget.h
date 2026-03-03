#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Container for exactly one child that enforces explicit width/height constraints.
// Set widthOverride / heightOverride to 0 to leave that axis unconstrained.
class SizeBoxWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setWidthOverride(float width) { m_widthOverride = width; }
    float getWidthOverride() const { return m_widthOverride; }

    void setHeightOverride(float height) { m_heightOverride = height; }
    float getHeightOverride() const { return m_heightOverride; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChild(const WidgetElement& child) { m_child = child; m_hasChild = true; }
    bool hasChild() const { return m_hasChild; }
    const WidgetElement& getChild() const { return m_child; }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::SizeBox;
        el.id = m_id;
        el.widthOverride = m_widthOverride;
        el.heightOverride = m_heightOverride;
        el.padding = m_padding;
        if (m_hasChild)
            el.children.push_back(m_child);
        return el;
    }

private:
    std::string m_id;
    float m_widthOverride{ 0.0f };
    float m_heightOverride{ 0.0f };
    Vec2 m_padding{ 0.0f, 0.0f };
    WidgetElement m_child;
    bool m_hasChild{ false };
};
