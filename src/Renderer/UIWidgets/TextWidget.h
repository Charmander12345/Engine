#pragma once

#include <string>

#include "../../Core/MathTypes.h"

class TextWidget
{
public:
    void setText(const std::string& text) { m_text = text; }
    const std::string& getText() const { return m_text; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setColor(const Vec4& color) { m_color = color; }
    const Vec4& getColor() const { return m_color; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

private:
    std::string m_text;
    std::string m_font;
    Vec4 m_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    float m_fontSize{ 0.0f };
};
