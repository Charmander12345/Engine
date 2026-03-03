#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

struct TabPage
{
    std::string label;
    std::vector<WidgetElement> content;
};

class TabViewWidget
{
public:
    void setTabs(const std::vector<TabPage>& tabs) { m_tabs = tabs; }
    const std::vector<TabPage>& getTabs() const { return m_tabs; }

    void addTab(const TabPage& tab) { m_tabs.push_back(tab); }
    void clearTabs() { m_tabs.clear(); }

    void setActiveTab(int index) { m_activeTab = index; }
    int getActiveTab() const { return m_activeTab; }

    void setFont(const std::string& font) { m_font = font; }
    const std::string& getFont() const { return m_font; }

    void setFontSize(float size) { m_fontSize = size; }
    float getFontSize() const { return m_fontSize; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setPadding(const Vec2& padding) { m_padding = padding; }
    const Vec2& getPadding() const { return m_padding; }

    void setTabColor(const Vec4& color) { m_tabColor = color; }
    const Vec4& getTabColor() const { return m_tabColor; }

    void setActiveTabColor(const Vec4& color) { m_activeTabColor = color; }
    const Vec4& getActiveTabColor() const { return m_activeTabColor; }

    void setHoverColor(const Vec4& color) { m_hoverColor = color; }
    const Vec4& getHoverColor() const { return m_hoverColor; }

    void setTextColor(const Vec4& color) { m_textColor = color; }
    const Vec4& getTextColor() const { return m_textColor; }

    void setContentColor(const Vec4& color) { m_contentColor = color; }
    const Vec4& getContentColor() const { return m_contentColor; }

    void setOnTabChanged(std::function<void(int)> callback) { m_onTabChanged = std::move(callback); }

    WidgetElement toElement() const
    {
        WidgetElement container{};
        container.type = WidgetElementType::TabView;
        container.activeTab = m_activeTab;
        container.minSize = m_minSize;
        container.padding = m_padding;
        container.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        container.fillX = true;
        container.sizeToContent = true;
        container.runtimeOnly = true;

        if (m_onTabChanged)
        {
            container.onTabChanged = m_onTabChanged;
        }

        WidgetElement tabBar{};
        tabBar.type = WidgetElementType::StackPanel;
        tabBar.id = "TabView.TabBar";
        tabBar.orientation = StackOrientation::Horizontal;
        tabBar.fillX = true;
        tabBar.sizeToContent = true;
        tabBar.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        tabBar.padding = Vec2{ 2.0f, 0.0f };
        tabBar.runtimeOnly = true;

        for (size_t i = 0; i < m_tabs.size(); ++i)
        {
            WidgetElement tabButton{};
            tabButton.type = WidgetElementType::Button;
            tabButton.id = "TabView.Tab." + std::to_string(i);
            tabButton.text = m_tabs[i].label;
            tabButton.font = m_font;
            tabButton.fontSize = m_fontSize;
            tabButton.textAlignH = TextAlignH::Center;
            tabButton.textAlignV = TextAlignV::Center;
            tabButton.padding = Vec2{ 10.0f, 4.0f };
            tabButton.minSize = Vec2{ 0.0f, 26.0f };
            tabButton.textColor = m_textColor;
            tabButton.shaderVertex = "button_vertex.glsl";
            tabButton.shaderFragment = "button_fragment.glsl";
            tabButton.hitTestMode = HitTestMode::Enabled;
            tabButton.runtimeOnly = true;

            if (static_cast<int>(i) == m_activeTab)
            {
                tabButton.color = m_activeTabColor;
                tabButton.hoverColor = m_activeTabColor;
            }
            else
            {
                tabButton.color = m_tabColor;
                tabButton.hoverColor = m_hoverColor;
            }

            tabBar.children.push_back(std::move(tabButton));
        }
        container.children.push_back(std::move(tabBar));

        if (m_activeTab >= 0 && m_activeTab < static_cast<int>(m_tabs.size()))
        {
            WidgetElement contentPanel{};
            contentPanel.type = WidgetElementType::StackPanel;
            contentPanel.id = "TabView.Content";
            contentPanel.orientation = StackOrientation::Vertical;
            contentPanel.fillX = true;
            contentPanel.sizeToContent = true;
            contentPanel.color = m_contentColor;
            contentPanel.padding = Vec2{ 4.0f, 4.0f };
            contentPanel.runtimeOnly = true;
            contentPanel.children = m_tabs[static_cast<size_t>(m_activeTab)].content;
            container.children.push_back(std::move(contentPanel));
        }

        return container;
    }

private:
    std::vector<TabPage> m_tabs;
    int m_activeTab{ 0 };
    std::string m_font{ "default.ttf" };
    float m_fontSize{ 13.0f };
    Vec2 m_minSize{ 0.0f, 0.0f };
    Vec2 m_padding{ 0.0f, 0.0f };
    Vec4 m_tabColor{ 0.14f, 0.14f, 0.18f, 0.9f };
    Vec4 m_activeTabColor{ 0.22f, 0.22f, 0.28f, 0.98f };
    Vec4 m_hoverColor{ 0.18f, 0.18f, 0.24f, 0.95f };
    Vec4 m_textColor{ 0.9f, 0.9f, 0.92f, 1.0f };
    Vec4 m_contentColor{ 0.1f, 0.1f, 0.13f, 0.8f };
    std::function<void(int)> m_onTabChanged;
};
