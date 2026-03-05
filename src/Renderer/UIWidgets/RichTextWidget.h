#pragma once

#include <string>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Displays formatted text with inline markup for bold, italic, color, and images.
// Supports word-wrap and per-segment styling via a simple HTML-like tag syntax.
class RichTextWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setRichText(const std::string& markup) { m_richText = markup; }
    const std::string& getRichText() const { return m_richText; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setBackgroundColor(const Vec4& color) { m_backgroundColor = color; }
    const Vec4& getBackgroundColor() const { return m_backgroundColor; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::RichText;
        el.id = m_id;
        el.richText = m_richText;
        el.fontSize = m_fontSize;
        el.style.textColor = m_textColor;
        el.style.color = m_backgroundColor;
        el.padding = m_padding;
        el.minSize = m_minSize;
        return el;
    }

private:
    std::string m_id;
    std::string m_richText;
    float m_fontSize{ 14.0f };
    Vec4 m_textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec4 m_backgroundColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    Vec2 m_padding{ 4.0f, 4.0f };
    Vec2 m_minSize{ 200.0f, 40.0f };
};
