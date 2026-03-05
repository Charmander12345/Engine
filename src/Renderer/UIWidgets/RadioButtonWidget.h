#pragma once

#include <string>
#include <functional>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Radio button that belongs to a named group; only one button per group is
// checked at a time.  The group logic is enforced at runtime by the UI manager.
class RadioButtonWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }

    void setGroup(const std::string& group) { m_group = group; }
    const std::string& getGroup() const { return m_group; }

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

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setOnCheckedChanged(std::function<void(bool)> cb) { m_onCheckedChanged = std::move(cb); }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::RadioButton;
        el.id = m_id;
        el.text = m_text;
        el.font = m_font;
        el.fontSize = m_fontSize;
        el.style.color = m_isChecked ? m_checkedColor : m_color;
        el.style.hoverColor = Vec4{
            std::min(1.0f, m_color.x + 0.1f),
            std::min(1.0f, m_color.y + 0.1f),
            std::min(1.0f, m_color.z + 0.1f),
            m_color.w };
        el.style.textColor = m_textColor;
        el.padding = m_padding;
        el.isChecked = m_isChecked;
        el.radioGroup = m_group;
        el.hitTestMode = HitTestMode::Enabled;
        el.fillX = true;
        el.onCheckedChanged = m_onCheckedChanged;
        return el;
    }

private:
    std::string m_id;
    std::string m_text;
    std::string m_group;
    std::string m_font;
    float m_fontSize{ 0.0f };
    Vec4 m_color{ 0.15f, 0.16f, 0.19f, 1.0f };
    Vec4 m_checkedColor{ 0.20f, 0.42f, 0.68f, 1.0f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.9f, 1.0f };
    Vec2 m_padding{ 6.0f, 4.0f };
    bool m_isChecked{ false };
    std::function<void(bool)> m_onCheckedChanged;
};
