#pragma once

#include <string>
#include <functional>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Button with persistent checked/unchecked toggle state.
class ToggleButtonWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }

    void setChecked(bool checked) { m_isChecked = checked; }
    bool isChecked() const { return m_isChecked; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setColor(const Vec4& color) { m_color = color; }
    const Vec4& getColor() const { return m_color; }

    void setCheckedColor(const Vec4& color) { m_checkedColor = color; }
    const Vec4& getCheckedColor() const { return m_checkedColor; }

    void setHoverColor(const Vec4& color) { m_hoverColor = color; }
    const Vec4& getHoverColor() const { return m_hoverColor; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setOnCheckedChanged(std::function<void(bool)> cb) { m_onCheckedChanged = std::move(cb); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::ToggleButton;
        el.id = m_id;
        el.text = m_text;
        el.font = m_font;
        el.fontSize = m_fontSize;
        el.color = m_isChecked ? m_checkedColor : m_color;
        el.hoverColor = m_hoverColor;
        el.textColor = m_textColor;
        el.padding = m_padding;
        el.minSize = m_minSize;
        el.isChecked = m_isChecked;
        el.hitTestMode = HitTestMode::Enabled;
        el.fillX = true;
        el.onCheckedChanged = m_onCheckedChanged;
        return el;
    }

private:
    std::string m_id;
    std::string m_text;
    std::string m_font;
    float m_fontSize{ 0.0f };
    Vec4 m_color{ 0.18f, 0.19f, 0.22f, 1.0f };
    Vec4 m_checkedColor{ 0.24f, 0.48f, 0.76f, 1.0f };
    Vec4 m_hoverColor{ 0.25f, 0.26f, 0.30f, 1.0f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.9f, 1.0f };
    Vec2 m_padding{ 6.0f, 4.0f };
    Vec2 m_minSize{ 0.0f, 22.0f };
    bool m_isChecked{ false };
    std::function<void(bool)> m_onCheckedChanged;
};
