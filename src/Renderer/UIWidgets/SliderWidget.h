#pragma once

#include <functional>
#include <string>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class SliderWidget
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

    void setTrackColor(const Vec4& color) { m_trackColor = color; }
    const Vec4& getTrackColor() const { return m_trackColor; }

    void setFillColor(const Vec4& color) { m_fillColor = color; }
    const Vec4& getFillColor() const { return m_fillColor; }

    void setHandleColor(const Vec4& color) { m_handleColor = color; }
    const Vec4& getHandleColor() const { return m_handleColor; }

    void setOnValueChanged(std::function<void(float)> callback) { m_onValueChanged = std::move(callback); }
    const std::function<void(float)>& getOnValueChanged() const { return m_onValueChanged; }

    WidgetElement toElement() const
    {
        WidgetElement element{};
        element.type = WidgetElementType::Slider;
        element.valueFloat = m_value;
        element.minValue = m_minValue;
        element.maxValue = m_maxValue;
        element.minSize = m_minSize;
        element.padding = m_padding;
        element.color = m_trackColor;
        element.fillColor = m_fillColor;
        element.textColor = m_handleColor;
        element.hitTestMode = HitTestMode::Enabled;
        if (m_onValueChanged)
        {
            element.onValueChanged = [callback = m_onValueChanged](const std::string& value)
            {
                callback(std::stof(value));
            };
        }
        return element;
    }

private:
    float m_value{ 0.0f };
    float m_minValue{ 0.0f };
    float m_maxValue{ 1.0f };
    Vec2 m_minSize{ 160.0f, 20.0f };
    Vec2 m_padding{};
    Vec4 m_trackColor{ 0.14f, 0.14f, 0.18f, 0.9f };
    Vec4 m_fillColor{ 0.25f, 0.55f, 0.85f, 0.95f };
    Vec4 m_handleColor{ 0.85f, 0.85f, 0.9f, 1.0f };
    std::function<void(float)> m_onValueChanged;
};
