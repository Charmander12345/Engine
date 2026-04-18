#include "NotificationsTab.h"

#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUI/EditorWidget.h"

#include <SDL3/SDL.h>
#include <functional>

namespace
{
    WidgetElement* FindElementById(WidgetElement& element, const std::string& id)
    {
        if (element.id == id)
            return &element;

        for (auto& child : element.children)
        {
            if (auto* match = FindElementById(child, id))
                return match;
        }

        return nullptr;
    }

    WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
    {
        for (auto& element : elements)
        {
            if (auto* match = FindElementById(element, id))
                return match;
        }

        return nullptr;
    }

    const char* LevelText(UIManager::NotificationLevel level)
    {
        switch (level)
        {
        case UIManager::NotificationLevel::Success: return "SUCCESS";
        case UIManager::NotificationLevel::Warning: return "WARNING";
        case UIManager::NotificationLevel::Error:   return "ERROR";
        case UIManager::NotificationLevel::Info:
        default:
            return "INFO";
        }
    }

    Vec4 LevelColor(UIManager::NotificationLevel level)
    {
        switch (level)
        {
        case UIManager::NotificationLevel::Success: return Vec4{ 0.25f, 0.78f, 0.42f, 1.0f };
        case UIManager::NotificationLevel::Warning: return Vec4{ 0.95f, 0.74f, 0.24f, 1.0f };
        case UIManager::NotificationLevel::Error:   return Vec4{ 0.90f, 0.30f, 0.30f, 1.0f };
        case UIManager::NotificationLevel::Info:
        default:
            return Vec4{ 0.40f, 0.70f, 1.0f, 1.0f };
        }
    }

    std::string FormatAge(uint64_t timestampMs)
    {
        const uint64_t now = SDL_GetTicks();
        const uint64_t ageMs = now > timestampMs ? (now - timestampMs) : 0;
        if (ageMs < 1000)
            return "just now";
        if (ageMs < 60000)
            return std::to_string(ageMs / 1000) + "s ago";
        return std::to_string(ageMs / 60000) + "m ago";
    }
}

NotificationsTab::NotificationsTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{
}

void NotificationsTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Notifications";

    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        refresh();
        return;
    }

    m_renderer->addTab(tabId, "Notifications", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Notifications.Main";
    m_ui->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId = tabId;
    m_state.widgetId = widgetId;
    m_state.isOpen = true;

    auto widget = std::make_shared<EditorWidget>();
    widget->setName(widgetId);
    widget->setAnchor(WidgetAnchor::TopLeft);
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setSizePixels(Vec2{ 0.0f, 0.0f });
    widget->setZOrder(2);

    const auto& theme = EditorTheme::Get();

    WidgetElement root{};
    root.id = "Notifications.Root";
    root.type = WidgetElementType::StackPanel;
    root.from = Vec2{ 0.0f, 0.0f };
    root.to = Vec2{ 1.0f, 1.0f };
    root.fillX = true;
    root.fillY = true;
    root.orientation = StackOrientation::Vertical;
    root.style.color = theme.panelBackground;
    root.runtimeOnly = true;

    buildToolbar(root);

    WidgetElement sep{};
    sep.type = WidgetElementType::Panel;
    sep.fillX = true;
    sep.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
    sep.style.color = theme.panelBorder;
    sep.runtimeOnly = true;
    root.children.push_back(std::move(sep));

    WidgetElement content{};
    content.id = "Notifications.Content";
    content.type = WidgetElementType::StackPanel;
    content.fillX = true;
    content.fillY = true;
    content.scrollable = true;
    content.orientation = StackOrientation::Vertical;
    content.padding = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
    content.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
    content.runtimeOnly = true;
    root.children.push_back(std::move(content));

    widget->setElements({ std::move(root) });
    m_ui->registerWidget(widgetId, widget, tabId);

    const std::string tabBtnId = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_ui->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refresh();
    });

    m_ui->registerClickEvent(closeBtnId, [this]()
    {
        close();
    });

    m_ui->registerClickEvent("Notifications.Refresh", [this]()
    {
        refresh();
    });

    m_ui->registerClickEvent("Notifications.MarkRead", [this]()
    {
        m_ui->clearUnreadNotifications();
        m_ui->refreshNotificationBadge();
        refresh();
    });

    refresh();
}

void NotificationsTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    const std::string tabId = m_state.tabId;
    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_ui->unregisterWidget(m_state.widgetId);
    m_renderer->removeTab(tabId);
    m_state = {};
    m_ui->markAllWidgetsDirty();
}

void NotificationsTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;

    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.25f)
    {
        m_state.refreshTimer = 0.0f;
        refresh();
    }
}

void NotificationsTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id = "Notifications.Toolbar";
    toolbar.type = WidgetElementType::StackPanel;
    toolbar.fillX = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    WidgetElement title{};
    title.type = WidgetElementType::Text;
    title.text = "Notification Center";
    title.font = theme.fontDefault;
    title.fontSize = theme.fontSizeSubheading;
    title.style.textColor = theme.textPrimary;
    title.textAlignV = TextAlignV::Center;
    title.minSize = EditorTheme::Scaled(Vec2{ 170.0f, 24.0f });
    title.padding = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
    title.runtimeOnly = true;
    toolbar.children.push_back(std::move(title));

    WidgetElement spacer{};
    spacer.type = WidgetElementType::Panel;
    spacer.fillX = true;
    spacer.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
    spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacer.runtimeOnly = true;
    toolbar.children.push_back(std::move(spacer));

    auto addButton = [&](const std::string& id, const std::string& text, float width)
    {
        WidgetElement button{};
        button.id = id;
        button.type = WidgetElementType::Button;
        button.text = text;
        button.font = theme.fontDefault;
        button.fontSize = theme.fontSizeSmall;
        button.style.textColor = theme.textPrimary;
        button.style.color = theme.buttonDefault;
        button.style.hoverColor = theme.buttonHover;
        button.textAlignH = TextAlignH::Center;
        button.textAlignV = TextAlignV::Center;
        button.minSize = EditorTheme::Scaled(Vec2{ width, 24.0f });
        button.padding = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        button.hitTestMode = HitTestMode::Enabled;
        button.runtimeOnly = true;
        button.clickEvent = id;
        toolbar.children.push_back(std::move(button));
    };

    addButton("Notifications.Refresh", "Refresh", 74.0f);
    addButton("Notifications.MarkRead", "Mark All Read", 112.0f);

    root.children.push_back(std::move(toolbar));
}

void NotificationsTab::refresh()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    WidgetElement* content = FindElementById(elements, "Notifications.Content");
    if (!content)
        return;

    content->children.clear();

    const auto& theme = EditorTheme::Get();
    const auto& history = m_ui->getNotificationHistory();
    const size_t unreadCount = m_ui->getUnreadNotificationCount();

    auto addText = [&](const std::string& text, const Vec4& color, float fontSize, float minHeight = 22.0f)
    {
        WidgetElement label{};
        label.type = WidgetElementType::Text;
        label.text = text;
        label.font = theme.fontDefault;
        label.fontSize = fontSize;
        label.style.textColor = color;
        label.textAlignV = TextAlignV::Center;
        label.minSize = EditorTheme::Scaled(Vec2{ 0.0f, minHeight });
        label.fillX = true;
        label.runtimeOnly = true;
        content->children.push_back(std::move(label));
    };

    addText("Latest editor notifications and toast history.", theme.textMuted, theme.fontSizeSmall, 20.0f);
    addText("Unread: " + std::to_string(unreadCount) + " | Stored: " + std::to_string(history.size()), theme.textSecondary, theme.fontSizeBody, 22.0f);

    WidgetElement sep{};
    sep.type = WidgetElementType::Panel;
    sep.fillX = true;
    sep.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
    sep.style.color = theme.panelBorder;
    sep.runtimeOnly = true;
    content->children.push_back(std::move(sep));

    if (history.empty())
    {
        addText("No notifications recorded yet.", theme.textMuted, theme.fontSizeBody, 24.0f);
    }
    else
    {
        for (auto it = history.rbegin(); it != history.rend(); ++it)
        {
            WidgetElement card{};
            card.type = WidgetElementType::StackPanel;
            card.orientation = StackOrientation::Vertical;
            card.fillX = true;
            card.padding = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
            card.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 54.0f });
            card.style.color = Vec4{ 0.10f, 0.11f, 0.14f, 0.85f };
            card.runtimeOnly = true;

            WidgetElement header{};
            header.type = WidgetElementType::StackPanel;
            header.orientation = StackOrientation::Horizontal;
            header.fillX = true;
            header.runtimeOnly = true;

            WidgetElement level{};
            level.type = WidgetElementType::Text;
            level.text = LevelText(it->level);
            level.font = theme.fontDefault;
            level.fontSize = theme.fontSizeSmall;
            level.style.textColor = LevelColor(it->level);
            level.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 18.0f });
            level.textAlignV = TextAlignV::Center;
            level.runtimeOnly = true;
            header.children.push_back(std::move(level));

            WidgetElement age{};
            age.type = WidgetElementType::Text;
            age.text = FormatAge(it->timestampMs);
            age.font = theme.fontDefault;
            age.fontSize = theme.fontSizeSmall;
            age.style.textColor = theme.textMuted;
            age.fillX = true;
            age.textAlignH = TextAlignH::Right;
            age.textAlignV = TextAlignV::Center;
            age.runtimeOnly = true;
            header.children.push_back(std::move(age));

            card.children.push_back(std::move(header));

            WidgetElement message{};
            message.type = WidgetElementType::Text;
            message.text = it->message;
            message.font = theme.fontDefault;
            message.fontSize = theme.fontSizeBody;
            message.style.textColor = theme.textPrimary;
            message.fillX = true;
            message.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
            message.runtimeOnly = true;
            card.children.push_back(std::move(message));

            content->children.push_back(std::move(card));
        }
    }

    entry->widget->markLayoutDirty();
    m_ui->markRenderDirty();
}
