#pragma once

#include <memory>
#include <vector>

#include "../../Core/MathTypes.h"

class Widget;

class StackPanelWidget
{
public:
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    void setOrientation(Orientation orientation) { m_orientation = orientation; }
    Orientation getOrientation() const { return m_orientation; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChildren(const std::vector<std::shared_ptr<Widget>>& children) { m_children = children; }
    const std::vector<std::shared_ptr<Widget>>& getChildren() const { return m_children; }

    void addChild(const std::shared_ptr<Widget>& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

private:
    Orientation m_orientation{ Orientation::Vertical };
    Vec2 m_padding{};
    std::vector<std::shared_ptr<Widget>> m_children;
};
