#pragma once

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class ProgressBarWidget
{
public:
    void setValue(float value) { m_value = value; }
    float getValue() const { return m_value; }

    void setRange(float minValue, float maxValue)
    {
        m_minValue = minValue;
        m_maxValue = maxValue;
    }
    float getMinValue() const { return m_minValue; }
    float getMaxValue() const { return m_maxValue; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setBackgroundColor(const Vec4& color) { m_backgroundColor = color; }
    const Vec4& getBackgroundColor() const { return m_backgroundColor; }

    void setFillColor(const Vec4& color) { m_fillColor = color; }
    const Vec4& getFillColor() const { return m_fillColor; }

    WidgetElement toElement() const
    {
        WidgetElement element{};
        element.type = WidgetElementType::ProgressBar;
        element.valueFloat = m_value;
        element.minValue = m_minValue;
        element.maxValue = m_maxValue;
        element.minSize = m_minSize;
        element.padding = m_padding;
        element.color = m_backgroundColor;
        element.fillColor = m_fillColor;
        element.isHitTestable = false;
        return element;
    }

private:
    float m_value{ 0.0f };
    float m_minValue{ 0.0f };
    float m_maxValue{ 1.0f };
    Vec2 m_minSize{ 140.0f, 18.0f };
    Vec2 m_padding{};
    Vec4 m_backgroundColor{ 0.14f, 0.14f, 0.18f, 0.9f };
    Vec4 m_fillColor{ 0.3f, 0.7f, 0.35f, 0.95f };
};
