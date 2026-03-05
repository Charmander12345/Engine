#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class DropdownButtonWidget
{
public:
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }

    void setImagePath(const std::string& path) { m_imagePath = path; }
    const std::string& getImagePath() const { return m_imagePath; }

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

    // Add a dropdown item with label and click callback
    void addItem(const std::string& label, std::function<void()> onClick)
    {
        m_dropdownItems.push_back({ label, std::move(onClick) });
    }

    // Set all items from string labels (use onSelectionChanged for callbacks)
    void setItems(const std::vector<std::string>& items) { m_items = items; }
    const std::vector<std::string>& getItems() const { return m_items; }

    void setOnSelectionChanged(std::function<void(int)> callback) { m_onSelectionChanged = std::move(callback); }

    WidgetElement toElement() const
    {
        WidgetElement element{};
        element.type = WidgetElementType::DropdownButton;
        element.text = m_text;
        element.imagePath = m_imagePath;
        element.font = m_font;
        element.fontSize = m_fontSize;
        element.minSize = m_minSize;
        element.padding = m_padding;
        element.style.color = m_backgroundColor;
        element.style.hoverColor = m_hoverColor;
        element.style.textColor = m_textColor;
        element.hitTestMode = HitTestMode::Enabled;
        element.items = m_items;

        // Copy runtime dropdown items (label + callback)
        for (const auto& di : m_dropdownItems)
        {
            element.dropdownItems.push_back({ di.label, di.onClick });
        }

        if (m_onSelectionChanged)
        {
            element.onSelectionChanged = m_onSelectionChanged;
        }

        return element;
    }

private:
    struct Item
    {
        std::string label;
        std::function<void()> onClick;
    };

    std::string m_text;
    std::string m_imagePath;
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 14.0f };
    Vec2 m_minSize{ 100.0f, 28.0f };
    Vec2 m_padding{ 8.0f, 4.0f };
    Vec4 m_backgroundColor{ 0.16f, 0.16f, 0.20f, 0.95f };
    Vec4 m_hoverColor{ 0.22f, 0.22f, 0.28f, 0.98f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.92f, 1.0f };
    std::vector<Item> m_dropdownItems;
    std::vector<std::string> m_items;
    std::function<void(int)> m_onSelectionChanged;
};
