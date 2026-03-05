#pragma once

#include <string>
#include <functional>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Virtualised scrollable list that renders only the visible range of items.
// Items are generated on demand via an onGenerateItem callback.
class ListViewWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setTotalItemCount(int count) { m_totalItemCount = count; }
    int getTotalItemCount() const { return m_totalItemCount; }

    void setItemHeight(float height) { m_itemHeight = height; }
    float getItemHeight() const { return m_itemHeight; }

    void setBackgroundColor(const Vec4& color) { m_backgroundColor = color; }
    const Vec4& getBackgroundColor() const { return m_backgroundColor; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setOnGenerateItem(std::function<void(int, WidgetElement&)> cb) { m_onGenerateItem = std::move(cb); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::ListView;
        el.id = m_id;
        el.totalItemCount = m_totalItemCount;
        el.itemHeight = m_itemHeight;
        el.style.color = m_backgroundColor;
        el.style.textColor = m_textColor;
        el.minSize = m_minSize;
        el.scrollable = true;
        el.onGenerateItem = m_onGenerateItem;
        return el;
    }

private:
    std::string m_id;
    int m_totalItemCount{ 0 };
    float m_itemHeight{ 32.0f };
    Vec4 m_backgroundColor{ 0.12f, 0.12f, 0.15f, 0.8f };
    Vec4 m_textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec2 m_minSize{ 200.0f, 200.0f };
    std::function<void(int, WidgetElement&)> m_onGenerateItem;
};
