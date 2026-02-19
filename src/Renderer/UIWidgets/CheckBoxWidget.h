#pragma once

#include <functional>
#include <string>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class CheckBoxWidget
{
public:
    void setChecked(bool checked) { m_checked = checked; }
    bool isChecked() const { return m_checked; }

    void setLabel(const std::string& label) { m_label = label; }
    const std::string& getLabel() const { return m_label; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setBoxColor(const Vec4& color) { m_boxColor = color; }
    const Vec4& getBoxColor() const { return m_boxColor; }

    void setCheckColor(const Vec4& color) { m_checkColor = color; }
    const Vec4& getCheckColor() const { return m_checkColor; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setOnCheckedChanged(std::function<void(bool)> callback) { m_onCheckedChanged = std::move(callback); }
    const std::function<void(bool)>& getOnCheckedChanged() const { return m_onCheckedChanged; }

    WidgetElement toElement() const
    {
        WidgetElement element{};
        element.type = WidgetElementType::CheckBox;
        element.isChecked = m_checked;
        element.text = m_label;
        element.font = m_font;
        element.fontSize = m_fontSize;
        element.minSize = m_minSize;
        element.padding = m_padding;
        element.color = m_boxColor;
        element.fillColor = m_checkColor;
        element.textColor = m_textColor;
        element.isHitTestable = true;
        if (m_onCheckedChanged)
        {
            element.onCheckedChanged = m_onCheckedChanged;
        }
        return element;
    }

private:
    bool m_checked{ false };
    std::string m_label;
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 14.0f };
    Vec2 m_minSize{ 0.0f, 22.0f };
    Vec2 m_padding{ 4.0f, 2.0f };
    Vec4 m_boxColor{ 0.18f, 0.18f, 0.22f, 0.95f };
    Vec4 m_checkColor{ 0.25f, 0.55f, 0.85f, 1.0f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.92f, 1.0f };
    std::function<void(bool)> m_onCheckedChanged;
};
