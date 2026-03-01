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

    void setOnClicked(std::function<void()> callback) { m_onClicked = std::move(callback); }
    void setOnHovered(std::function<void()> callback) { m_onHovered = std::move(callback); }
    void setOnUnhovered(std::function<void()> callback) { m_onUnhovered = std::move(callback); }

    void triggerClicked() { if (m_onClicked) m_onClicked(); }
    void triggerHovered() { if (m_onHovered) m_onHovered(); }
    void triggerUnhovered() { if (m_onUnhovered) m_onUnhovered(); }

private:
    State m_state{ State::Normal };
    Vec2 m_size{};
    std::shared_ptr<TextWidget> m_child;
    std::function<void()> m_onClicked;
    std::function<void()> m_onHovered;
    std::function<void()> m_onUnhovered;
};
