#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Grid where every cell has the same size. Children are placed left-to-right,
// top-to-bottom. Set columns (or rows) to 0 for automatic calculation.
class UniformGridWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setColumns(int columns) { m_columns = columns; }
    int getColumns() const { return m_columns; }

    void setRows(int rows) { m_rows = rows; }
    int getRows() const { return m_rows; }

    void setSpacing(float spacing) { m_spacing = spacing; }
    float getSpacing() const { return m_spacing; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setChildren(const std::vector<WidgetElement>& children) { m_children = children; }
    const std::vector<WidgetElement>& getChildren() const { return m_children; }

    void addChild(const WidgetElement& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::UniformGrid;
        el.id = m_id;
        el.columns = m_columns;
        el.rows = m_rows;
        el.spacing = m_spacing;
        el.padding = m_padding;
        el.children = m_children;
        return el;
    }

private:
    std::string m_id;
    int m_columns{ 0 };
    int m_rows{ 0 };
    float m_spacing{ 0.0f };
    Vec2 m_padding{ 0.0f, 0.0f };
    std::vector<WidgetElement> m_children;
};
