#pragma once

#include <memory>

#include "../../Core/MathTypes.h"
#include "TextWidget.h"

class ButtonWidget
{
public:
    enum class State
    {
        Normal,
        Hovered,
        Pressed,
        Disabled
    };

    void setState(State state) { m_state = state; }
    State getState() const { return m_state; }

    void setSize(const Vec2& size) { m_size = size; }
    const Vec2& getSize() const { return m_size; }

    void setChild(const std::shared_ptr<TextWidget>& child) { m_child = child; }
    const std::shared_ptr<TextWidget>& getChild() const { return m_child; }

private:
    State m_state{ State::Normal };
    Vec2 m_size{};
    std::shared_ptr<TextWidget> m_child;
};
