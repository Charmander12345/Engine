#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

struct TreeViewNode
{
    std::string id;
    std::string label;
    bool isExpanded{ false };
    std::vector<TreeViewNode> children;
};

class TreeViewWidget
{
public:
    void setNodes(const std::vector<TreeViewNode>& nodes) { m_nodes = nodes; }
    const std::vector<TreeViewNode>& getNodes() const { return m_nodes; }

    void addNode(const TreeViewNode& node) { m_nodes.push_back(node); }
    void clearNodes() { m_nodes.clear(); }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setIndentSize(float size) { m_indentSize = size; }
    float getIndentSize() const { return m_indentSize; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setItemColor(const Vec4& color) { m_itemColor = color; }
    const Vec4& getItemColor() const { return m_itemColor; }

    void setHoverColor(const Vec4& color) { m_hoverColor = color; }
    const Vec4& getHoverColor() const { return m_hoverColor; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setOnNodeClicked(std::function<void(const std::string&)> callback) { m_onNodeClicked = std::move(callback); }

    WidgetElement toElement() const
    {
        WidgetElement container{};
        container.type = WidgetElementType::TreeView;
        container.minSize = m_minSize;
        container.padding = m_padding;
        container.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        container.fillX = true;
        container.sizeToContent = true;
        container.scrollable = true;
        container.runtimeOnly = true;

        buildChildren(container.children, m_nodes, 0);
        return container;
    }

private:
    void buildChildren(std::vector<WidgetElement>& target, const std::vector<TreeViewNode>& nodes, int depth) const
    {
        for (const auto& node : nodes)
        {
            WidgetElement row{};
            row.type = WidgetElementType::Button;
            row.id = "TreeView.Node." + node.id;
            row.fillX = true;
            row.font = m_font;
            row.fontSize = m_fontSize;
            row.textAlignH = TextAlignH::Left;
            row.textAlignV = TextAlignV::Center;
            row.minSize = Vec2{ 0.0f, 22.0f };
            row.hitTestMode = HitTestMode::Enabled;
            row.runtimeOnly = true;
            row.color = m_itemColor;
            row.hoverColor = m_hoverColor;
            row.textColor = m_textColor;
            row.shaderVertex = "button_vertex.glsl";
            row.shaderFragment = "button_fragment.glsl";

            std::string indent(static_cast<size_t>(depth) * 2, ' ');
            if (!node.children.empty())
            {
                row.text = indent + (node.isExpanded ? "v " : "> ") + node.label;
            }
            else
            {
                row.text = indent + "  " + node.label;
            }
            row.padding = Vec2{ 6.0f + m_indentSize * static_cast<float>(depth), 2.0f };

            if (m_onNodeClicked)
            {
                auto callback = m_onNodeClicked;
                std::string nodeId = node.id;
                row.onClicked = [callback, nodeId]() { callback(nodeId); };
            }

            target.push_back(std::move(row));

            if (node.isExpanded && !node.children.empty())
            {
                buildChildren(target, node.children, depth + 1);
            }
        }
    }

    std::vector<TreeViewNode> m_nodes;
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 13.0f };
    float m_indentSize{ 16.0f };
    Vec2 m_minSize{ 0.0f, 0.0f };
    Vec2 m_padding{ 2.0f, 2.0f };
    Vec4 m_itemColor{ 0.12f, 0.12f, 0.14f, 0.9f };
    Vec4 m_hoverColor{ 0.18f, 0.18f, 0.22f, 0.95f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.92f, 1.0f };
    std::function<void(const std::string&)> m_onNodeClicked;
};
