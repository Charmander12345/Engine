#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class DropDownWidget
{
public:
    void setItems(const std::vector<std::string>& items) { m_items = items; }
    const std::vector<std::string>& getItems() const { return m_items; }

    void setSelectedIndex(int index) { m_selectedIndex = index; }
    int getSelectedIndex() const { return m_selectedIndex; }

    std::string getSelectedItem() const
    {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size()))
        {
            return m_items[static_cast<size_t>(m_selectedIndex)];
        }
        return {};
    }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setBackgroundColor(const Vec4& color) { m_backgroundColor = color; }
    const Vec4& getBackgroundColor() const { return m_backgroundColor; }

    void setHoverColor(const Vec4& color) { m_hoverColor = color; }
    const Vec4& getHoverColor() const { return m_hoverColor; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setOnSelectionChanged(std::function<void(int)> callback) { m_onSelectionChanged = std::move(callback); }
    const std::function<void(int)>& getOnSelectionChanged() const { return m_onSelectionChanged; }

    WidgetElement toElement() const
    {
        WidgetElement element{};
        element.type = WidgetElementType::DropDown;
        element.items = m_items;
        element.selectedIndex = m_selectedIndex;
        element.font = m_font;
        element.fontSize = m_fontSize;
        element.minSize = m_minSize;
        element.padding = m_padding;
        element.style.color = m_backgroundColor;
        element.style.hoverColor = m_hoverColor;
        element.style.textColor = m_textColor;
        element.hitTestMode = HitTestMode::Enabled;
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_items.size()))
        {
            element.text = m_items[static_cast<size_t>(m_selectedIndex)];
        }
        if (m_onSelectionChanged)
        {
            element.onSelectionChanged = m_onSelectionChanged;
        }
        return element;
    }

private:
    std::vector<std::string> m_items;
    int m_selectedIndex{ -1 };
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 14.0f };
    Vec2 m_minSize{ 140.0f, 24.0f };
    Vec2 m_padding{ 6.0f, 4.0f };
    Vec4 m_backgroundColor{ 0.14f, 0.14f, 0.18f, 0.95f };
    Vec4 m_hoverColor{ 0.2f, 0.2f, 0.26f, 0.98f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.92f, 1.0f };
    std::function<void(int)> m_onSelectionChanged;
};
