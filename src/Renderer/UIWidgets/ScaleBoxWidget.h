#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Container that scales its single child to fit the available area using the
// specified ScaleMode (Contain, Cover, Fill, ScaleDown, UserSpecified).
class ScaleBoxWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setScaleMode(ScaleMode mode) { m_scaleMode = mode; }
    ScaleMode getScaleMode() const { return m_scaleMode; }

    void setUserScale(float scale) { m_userScale = scale; }
    float getUserScale() const { return m_userScale; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChild(const WidgetElement& child) { m_child = child; m_hasChild = true; }
    bool hasChild() const { return m_hasChild; }
    const WidgetElement& getChild() const { return m_child; }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::ScaleBox;
        el.id = m_id;
        el.scaleMode = m_scaleMode;
        el.userScale = m_userScale;
        el.padding = m_padding;
        if (m_hasChild)
            el.children.push_back(m_child);
        return el;
    }

private:
    std::string m_id;
    ScaleMode m_scaleMode{ ScaleMode::Contain };
    float m_userScale{ 1.0f };
    Vec2 m_padding{ 0.0f, 0.0f };
    WidgetElement m_child;
    bool m_hasChild{ false };
};
