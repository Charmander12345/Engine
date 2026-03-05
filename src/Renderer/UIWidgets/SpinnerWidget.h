#pragma once

#include <string>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Animated loading indicator that renders a ring of dots with fading opacity.
// The animation is driven by spinnerElapsed which is ticked every frame.
class SpinnerWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setDotCount(int count) { m_dotCount = count; }
    int getDotCount() const { return m_dotCount; }

    void setSpeed(float rotationsPerSecond) { m_speed = rotationsPerSecond; }
    float getSpeed() const { return m_speed; }

    void setColor(const Vec4& color) { m_color = color; }
    const Vec4& getColor() const { return m_color; }

    void setSize(const Vec2& size) { m_size = size; }
    const Vec2& getSize() const { return m_size; }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::Spinner;
        el.id = m_id;
        el.spinnerDotCount = m_dotCount;
        el.spinnerSpeed = m_speed;
        el.style.color = m_color;
        el.minSize = m_size;
        return el;
    }

private:
    std::string m_id;
    int m_dotCount{ 8 };
    float m_speed{ 1.0f };
    Vec4 m_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec2 m_size{ 32.0f, 32.0f };
};
