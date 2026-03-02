#pragma once

#include <string>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

// Lightweight, non-interactive text display element.
// Use Label instead of Text when no click/hover interaction is needed.
class LabelWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setTextAlignH(TextAlignH align) { m_textAlignH = align; }
    TextAlignH getTextAlignH() const { return m_textAlignH; }

    void setTextAlignV(TextAlignV align) { m_textAlignV = align; }
    TextAlignV getTextAlignV() const { return m_textAlignV; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setWrapText(bool wrap) { m_wrapText = wrap; }
    bool getWrapText() const { return m_wrapText; }

    void setBold(bool bold) { m_isBold = bold; }
    bool isBold() const { return m_isBold; }

    void setItalic(bool italic) { m_isItalic = italic; }
    bool isItalic() const { return m_isItalic; }

    WidgetElement toElement() const
    {
        WidgetElement el{};
        el.type = WidgetElementType::Label;
        el.id = m_id;
        el.text = m_text;
        el.font = m_font;
        el.fontSize = m_fontSize;
        el.textColor = m_textColor;
        el.textAlignH = m_textAlignH;
        el.textAlignV = m_textAlignV;
        el.padding = m_padding;
        el.wrapText = m_wrapText;
        el.isBold = m_isBold;
        el.isItalic = m_isItalic;
        el.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };  // transparent background
        el.isHitTestable = false;
        return el;
    }

private:
    std::string m_id;
    std::string m_text;
    std::string m_font;
    float m_fontSize{ 0.0f };
    Vec4 m_textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    TextAlignH m_textAlignH{ TextAlignH::Left };
    TextAlignV m_textAlignV{ TextAlignV::Top };
    Vec2 m_padding{ 0.0f, 0.0f };
    bool m_wrapText{ false };
    bool m_isBold{ false };
    bool m_isItalic{ false };
};
