#pragma once

#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

class SeparatorWidget
{
public:
    void setId(const std::string& id) { m_id = id; }
    const std::string& getId() const { return m_id; }

    void setTitle(const std::string& title) { m_title = title; }
    const std::string& getTitle() const { return m_title; }

    void setHeaderColors(const Vec4& normal, const Vec4& hover)
    {
        m_headerColor = normal;
        m_headerHoverColor = hover;
    }

    void setTitleColor(const Vec4& color) { m_titleColor = color; }
    const Vec4& getTitleColor() const { return m_titleColor; }

    void setContentPadding(const Vec2& padding) { m_contentPadding = padding; }
    const Vec2& getContentPadding() const { return m_contentPadding; }

    void setContentColor(const Vec4& color) { m_contentColor = color; }
    const Vec4& getContentColor() const { return m_contentColor; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setChildren(const std::vector<WidgetElement>& children) { m_children = children; }
    const std::vector<WidgetElement>& getChildren() const { return m_children; }

    void addChild(const WidgetElement& child) { m_children.push_back(child); }
    void clearChildren() { m_children.clear(); }

    WidgetElement toElement() const
    {
        WidgetElement container{};
        container.type = WidgetElementType::StackPanel;
        container.id = m_id.empty() ? "Separator" : "Separator." + m_id;
        container.orientation = StackOrientation::Vertical;
        container.fillX = true;
        container.sizeToContent = true;
        container.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        container.runtimeOnly = true;

        WidgetElement header{};
        header.type = WidgetElementType::Button;
        header.id = "Separator.Toggle." + m_id;
        header.text = "v " + m_title;
        header.font = m_font;
        header.fontSize = m_fontSize;
        header.textAlignH = TextAlignH::Left;
        header.textAlignV = TextAlignV::Center;
        header.padding = Vec2{ 6.0f, 4.0f };
        header.minSize = Vec2{ 0.0f, 22.0f };
        header.color = m_headerColor;
        header.hoverColor = m_headerHoverColor;
        header.textColor = m_titleColor;
        header.shaderVertex = "button_vertex.glsl";
        header.shaderFragment = "button_fragment.glsl";
        header.isHitTestable = true;
        header.runtimeOnly = true;

        WidgetElement content{};
        content.type = WidgetElementType::StackPanel;
        content.id = "Separator.Content." + m_id;
        content.orientation = StackOrientation::Vertical;
        content.padding = m_contentPadding;
        content.fillX = true;
        content.sizeToContent = true;
        content.color = m_contentColor;
        content.children = m_children;
        content.cachedChildren = m_children;
        content.runtimeOnly = true;

        container.children.push_back(std::move(header));
        container.children.push_back(std::move(content));
        return container;
    }

private:
    std::string m_id{ "Section" };
    std::string m_title{ "Section" };
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 14.0f };
    Vec4 m_headerColor{ 0.18f, 0.18f, 0.22f, 0.95f };
    Vec4 m_headerHoverColor{ 0.24f, 0.24f, 0.28f, 0.98f };
    Vec4 m_titleColor{ 0.95f, 0.95f, 0.95f, 1.0f };
    Vec2 m_contentPadding{ 4.0f, 4.0f };
    Vec4 m_contentColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<WidgetElement> m_children;
};
