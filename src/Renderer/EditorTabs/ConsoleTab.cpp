#include "ConsoleTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Logger/Logger.h"

#include <algorithm>
#include <cctype>

// ───────────────────────────────────────────────────────────────────────────
ConsoleTab::ConsoleTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ───────────────────────────────────────────────────────────────────────────
void ConsoleTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Console";

    // If already open, just switch to it
    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Console", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Console.Main";

    // Clean up any stale registration
    m_ui->unregisterWidget(widgetId);

    // Initialise state
    m_state = {};
    m_state.tabId     = tabId;
    m_state.widgetId  = widgetId;
    m_state.isOpen    = true;
    m_state.levelFilter = 0xFF;  // show all levels
    m_state.autoScroll  = true;
    m_state.lastSeenSequenceId = 0;

    // Build the main widget (fills entire tab area)
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "Console.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // ── Toolbar row ──────────────────────────────────────────────
        buildToolbar(root);

        // ── Separator ────────────────────────────────────────────────
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // ── Scrollable log area ──────────────────────────────────────
        {
            WidgetElement logArea{};
            logArea.id          = "Console.LogArea";
            logArea.type        = WidgetElementType::StackPanel;
            logArea.fillX       = true;
            logArea.fillY       = true;
            logArea.scrollable  = true;
            logArea.orientation = StackOrientation::Vertical;
            logArea.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
            logArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            logArea.runtimeOnly = true;
            root.children.push_back(std::move(logArea));
        }

        widget->setElements({ std::move(root) });
        m_ui->registerWidget(widgetId, widget, tabId);
    }

    // ── Tab / close click events ─────────────────────────────────────
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_ui->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshLog();
    });

    m_ui->registerClickEvent(closeBtnId, [this]()
    {
        close();
    });

    // ── Filter & action click events ─────────────────────────────────
    m_ui->registerClickEvent("Console.Filter.All", [this]()
    {
        m_state.levelFilter = 0xFF;
        refreshLog();
    });
    m_ui->registerClickEvent("Console.Filter.Info", [this]()
    {
        m_state.levelFilter ^= (1 << 0);
        refreshLog();
    });
    m_ui->registerClickEvent("Console.Filter.Warning", [this]()
    {
        m_state.levelFilter ^= (1 << 1);
        refreshLog();
    });
    m_ui->registerClickEvent("Console.Filter.Error", [this]()
    {
        m_state.levelFilter ^= (1 << 2);
        refreshLog();
    });
    m_ui->registerClickEvent("Console.Clear", [this]()
    {
        Logger::Instance().clearConsoleBuffer();
        m_state.lastSeenSequenceId = 0;
        refreshLog();
    });
    m_ui->registerClickEvent("Console.AutoScroll", [this]()
    {
        m_state.autoScroll = !m_state.autoScroll;
        refreshLog();
    });

    // Initial population
    refreshLog();
}

// ───────────────────────────────────────────────────────────────────────────
void ConsoleTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "Console.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    auto makeFilterBtn = [&](const std::string& id, const std::string& label, bool active) -> WidgetElement
    {
        WidgetElement btn{};
        btn.id            = id;
        btn.type          = WidgetElementType::Button;
        btn.text          = label;
        btn.font          = theme.fontDefault;
        btn.fontSize      = theme.fontSizeSmall;
        btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
        btn.style.color     = active ? theme.accent : theme.buttonDefault;
        btn.style.hoverColor = theme.buttonHover;
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.minSize       = EditorTheme::Scaled(Vec2{ 60.0f, 24.0f });
        btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        btn.hitTestMode   = HitTestMode::Enabled;
        btn.runtimeOnly   = true;
        btn.clickEvent    = id;
        return btn;
    };

    const bool allActive    = (m_state.levelFilter == 0xFF);
    const bool infoActive   = (m_state.levelFilter & (1 << 0)) != 0;
    const bool warnActive   = (m_state.levelFilter & (1 << 1)) != 0;
    const bool errorActive  = (m_state.levelFilter & (1 << 2)) != 0;

    toolbar.children.push_back(makeFilterBtn("Console.Filter.All",     "All",     allActive));
    toolbar.children.push_back(makeFilterBtn("Console.Filter.Info",    "Info",    infoActive));
    toolbar.children.push_back(makeFilterBtn("Console.Filter.Warning", "Warning", warnActive));
    toolbar.children.push_back(makeFilterBtn("Console.Filter.Error",   "Error",   errorActive));

    // ── Spacer ───────────────────────────────────────────────────────
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // ── Search entry bar ─────────────────────────────────────────────
    {
        WidgetElement search = EditorUIBuilder::makeEntryBar(
            "Console.Search", m_state.searchText,
            [this](const std::string& text)
            {
                m_state.searchText = text;
                refreshLog();
            },
            EditorTheme::Scaled(180.0f));
        search.minSize = EditorTheme::Scaled(Vec2{ 180.0f, 24.0f });
        toolbar.children.push_back(std::move(search));
    }

    // ── Clear button ─────────────────────────────────────────────────
    toolbar.children.push_back(makeFilterBtn("Console.Clear", "Clear", false));

    // ── Auto-scroll toggle ───────────────────────────────────────────
    {
        WidgetElement toggle{};
        toggle.id           = "Console.AutoScroll";
        toggle.type         = WidgetElementType::Button;
        toggle.text         = m_state.autoScroll ? "Auto-Scroll: ON" : "Auto-Scroll: OFF";
        toggle.font         = theme.fontDefault;
        toggle.fontSize     = theme.fontSizeSmall;
        toggle.style.textColor = m_state.autoScroll ? theme.accentGreen : theme.textMuted;
        toggle.style.color     = theme.buttonDefault;
        toggle.style.hoverColor = theme.buttonHover;
        toggle.textAlignH   = TextAlignH::Center;
        toggle.textAlignV   = TextAlignV::Center;
        toggle.minSize      = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
        toggle.padding      = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        toggle.hitTestMode  = HitTestMode::Enabled;
        toggle.runtimeOnly  = true;
        toggle.clickEvent   = "Console.AutoScroll";
        toolbar.children.push_back(std::move(toggle));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
void ConsoleTab::refreshLog()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find the log-area container
    WidgetElement* logArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "Console.LogArea")
        {
            logArea = &child;
            break;
        }
    }
    if (!logArea)
        return;

    logArea->children.clear();

    // Also rebuild the toolbar to reflect filter state changes
    {
        WidgetElement* toolbar = nullptr;
        for (auto& child : elements[0].children)
        {
            if (child.id == "Console.Toolbar")
            {
                toolbar = &child;
                break;
            }
        }
        if (toolbar)
        {
            toolbar->children.clear();
            const auto& theme = EditorTheme::Get();

            auto makeFilterBtn = [&](const std::string& id, const std::string& label, bool active) -> WidgetElement
            {
                WidgetElement btn{};
                btn.id            = id;
                btn.type          = WidgetElementType::Button;
                btn.text          = label;
                btn.font          = theme.fontDefault;
                btn.fontSize      = theme.fontSizeSmall;
                btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
                btn.style.color     = active ? theme.accent : theme.buttonDefault;
                btn.style.hoverColor = theme.buttonHover;
                btn.textAlignH    = TextAlignH::Center;
                btn.textAlignV    = TextAlignV::Center;
                btn.minSize       = EditorTheme::Scaled(Vec2{ 60.0f, 24.0f });
                btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
                btn.hitTestMode   = HitTestMode::Enabled;
                btn.runtimeOnly   = true;
                btn.clickEvent    = id;
                return btn;
            };

            const bool allActive    = (m_state.levelFilter == 0xFF);
            const bool infoActive   = (m_state.levelFilter & (1 << 0)) != 0;
            const bool warnActive   = (m_state.levelFilter & (1 << 1)) != 0;
            const bool errorActive  = (m_state.levelFilter & (1 << 2)) != 0;

            toolbar->children.push_back(makeFilterBtn("Console.Filter.All",     "All",     allActive));
            toolbar->children.push_back(makeFilterBtn("Console.Filter.Info",    "Info",    infoActive));
            toolbar->children.push_back(makeFilterBtn("Console.Filter.Warning", "Warning", warnActive));
            toolbar->children.push_back(makeFilterBtn("Console.Filter.Error",   "Error",   errorActive));

            // Spacer
            {
                WidgetElement spacer{};
                spacer.type        = WidgetElementType::Panel;
                spacer.fillX       = true;
                spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.runtimeOnly = true;
                toolbar->children.push_back(std::move(spacer));
            }

            // Search entry bar
            {
                WidgetElement search = EditorUIBuilder::makeEntryBar(
                    "Console.Search", m_state.searchText,
                    [this](const std::string& text)
                    {
                        m_state.searchText = text;
                        refreshLog();
                    },
                    EditorTheme::Scaled(180.0f));
                search.minSize = EditorTheme::Scaled(Vec2{ 180.0f, 24.0f });
                toolbar->children.push_back(std::move(search));
            }

            toolbar->children.push_back(makeFilterBtn("Console.Clear", "Clear", false));

            // Auto-scroll toggle
            {
                WidgetElement toggle{};
                toggle.id           = "Console.AutoScroll";
                toggle.type         = WidgetElementType::Button;
                toggle.text         = m_state.autoScroll ? "Auto-Scroll: ON" : "Auto-Scroll: OFF";
                toggle.font         = theme.fontDefault;
                toggle.fontSize     = theme.fontSizeSmall;
                toggle.style.textColor = m_state.autoScroll ? theme.accentGreen : theme.textMuted;
                toggle.style.color     = theme.buttonDefault;
                toggle.style.hoverColor = theme.buttonHover;
                toggle.textAlignH   = TextAlignH::Center;
                toggle.textAlignV   = TextAlignV::Center;
                toggle.minSize      = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
                toggle.padding      = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
                toggle.hitTestMode  = HitTestMode::Enabled;
                toggle.runtimeOnly  = true;
                toggle.clickEvent   = "Console.AutoScroll";
                toolbar->children.push_back(std::move(toggle));
            }
        }
    }

    const auto& theme = EditorTheme::Get();
    const auto& entries = Logger::Instance().getConsoleEntries();

    // Level-to-bit mapping
    auto levelBit = [](Logger::LogLevel lvl) -> uint8_t
    {
        switch (lvl)
        {
        case Logger::LogLevel::INFO:    return (1 << 0);
        case Logger::LogLevel::WARNING: return (1 << 1);
        case Logger::LogLevel::ERROR:   return (1 << 2);
        case Logger::LogLevel::FATAL:   return (1 << 2); // FATAL shares the ERROR filter bit
        default: return 0xFF;
        }
    };

    // Level-to-color mapping
    auto levelColor = [&](Logger::LogLevel lvl) -> Vec4
    {
        switch (lvl)
        {
        case Logger::LogLevel::WARNING: return theme.warningColor;
        case Logger::LogLevel::ERROR:   return theme.errorColor;
        case Logger::LogLevel::FATAL:   return Vec4{ 1.0f, 0.15f, 0.15f, 1.0f };
        default:                        return theme.textSecondary;
        }
    };

    auto levelTag = [](Logger::LogLevel lvl) -> const char*
    {
        switch (lvl)
        {
        case Logger::LogLevel::INFO:    return "[INFO]";
        case Logger::LogLevel::WARNING: return "[WARN]";
        case Logger::LogLevel::ERROR:   return "[ERR ]";
        case Logger::LogLevel::FATAL:   return "[FATL]";
        default:                        return "[    ]";
        }
    };

    uint64_t maxSeq = 0;
    const float rowH = EditorTheme::Scaled(18.0f);

    for (const auto& e : entries)
    {
        // Filter by level
        if (!(m_state.levelFilter & levelBit(e.level)))
            continue;

        // Filter by search text
        if (!m_state.searchText.empty())
        {
            // Case-insensitive substring search
            std::string msgLower = e.message;
            std::string queryLower = m_state.searchText;
            for (auto& c : msgLower)  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : queryLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (msgLower.find(queryLower) == std::string::npos)
                continue;
        }

        if (e.sequenceId > maxSeq)
            maxSeq = e.sequenceId;

        // Build the row: "[TIME] [LEVEL] [CATEGORY] message"
        std::string rowText = e.timestamp + "  " + levelTag(e.level) + "  "
            + Logger::categoryToString(e.category) + "  " + e.message;

        WidgetElement row{};
        row.id           = "Console.Row." + std::to_string(e.sequenceId);
        row.type         = WidgetElementType::Text;
        row.text         = std::move(rowText);
        row.font         = theme.fontDefault;
        row.fontSize     = theme.fontSizeSmall;
        row.style.textColor = levelColor(e.level);
        row.textAlignH   = TextAlignH::Left;
        row.textAlignV   = TextAlignV::Center;
        row.fillX        = true;
        row.minSize      = Vec2{ 0.0f, rowH };
        row.runtimeOnly  = true;

        logArea->children.push_back(std::move(row));
    }

    m_state.lastSeenSequenceId = maxSeq;

    // Auto-scroll: set scroll to bottom
    if (m_state.autoScroll)
        logArea->scrollOffset = 999999.0f;

    m_ui->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void ConsoleTab::close()
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

// ───────────────────────────────────────────────────────────────────────────
void ConsoleTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;

    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.5f)
    {
        m_state.refreshTimer = 0.0f;
        const uint64_t latestSeq = Logger::Instance().getLatestSequenceId();
        if (latestSeq != m_state.lastSeenSequenceId)
        {
            refreshLog();
        }
    }
}
