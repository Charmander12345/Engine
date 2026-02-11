#pragma once

#include <functional>
#include <string>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class EntryBarWidget
{
public:
    void setValue(const std::string& value) { m_value = value; }
    const std::string& getValue() const { return m_value; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setBackgroundColor(const Vec4& color) { m_backgroundColor = color; }
    const Vec4& getBackgroundColor() const { return m_backgroundColor; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setPassword(bool isPassword) { m_isPassword = isPassword; }
    bool isPassword() const { return m_isPassword; }

    void setOnValueChanged(std::function<void(const std::string&)> callback) { m_onValueChanged = std::move(callback); }
    const std::function<void(const std::string&)>& getOnValueChanged() const { return m_onValueChanged; }

    WidgetElement toElement() const
    {
        WidgetElement element{};
        element.type = WidgetElementType::EntryBar;
        element.value = m_value;
        element.minSize = m_minSize;
        element.padding = m_padding;
        element.textColor = m_textColor;
        element.color = m_backgroundColor;
        element.font = m_font;
        element.fontSize = m_fontSize;
        element.isPassword = m_isPassword;
        element.isHitTestable = true;
        element.onValueChanged = m_onValueChanged;
        return element;
    }

private:
    std::string m_value;
    Vec2 m_minSize{ 80.0f, 26.0f };
    Vec2 m_padding{ 6.0f, 4.0f };
    Vec4 m_textColor{ 0.95f, 0.95f, 0.95f, 1.0f };
    Vec4 m_backgroundColor{ 0.12f, 0.12f, 0.15f, 0.9f };
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 14.0f };
    bool m_isPassword{ false };
    std::function<void(const std::string&)> m_onValueChanged;
};
