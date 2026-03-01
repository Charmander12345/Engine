#pragma once

#include <memory>
#include <vector>

#include "../../Core/MathTypes.h"

class Widget;

class GridWidget
{
public:
    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChildren(const std::vector<std::shared_ptr<Widget>>& children) { m_children = children; }
    const std::vector<std::shared_ptr<Widget>>& getChildren() const { return m_children; }

    void addChild(const std::shared_ptr<Widget>& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

private:
    Vec2 m_padding{};
    std::vector<std::shared_ptr<Widget>> m_children;
};
