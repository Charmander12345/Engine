#include "ProfilerTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"

#include <algorithm>
#include <cstdio>
#include <functional>

// ───────────────────────────────────────────────────────────────────────────
ProfilerTab::ProfilerTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ───────────────────────────────────────────────────────────────────────────
void ProfilerTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Profiler";

    // If already open, just switch to it
    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Profiler", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Profiler.Main";

    // Clean up any stale registration
    m_ui->unregisterWidget(widgetId);

    // Initialise state
    m_state = {};
    m_state.tabId    = tabId;
    m_state.widgetId = widgetId;
    m_state.isOpen   = true;
    m_state.frozen   = false;

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
        root.id          = "Profiler.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // ── Toolbar row ──────────────────────────────────────────────────
        buildToolbar(root);

        // ── Separator ────────────────────────────────────────────────────
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // ── Scrollable metrics area ──────────────────────────────────────
        {
            WidgetElement metricsArea{};
            metricsArea.id          = "Profiler.MetricsArea";
            metricsArea.type        = WidgetElementType::StackPanel;
            metricsArea.fillX       = true;
            metricsArea.fillY       = true;
            metricsArea.scrollable  = true;
            metricsArea.orientation = StackOrientation::Vertical;
            metricsArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            metricsArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            metricsArea.runtimeOnly = true;
            root.children.push_back(std::move(metricsArea));
        }

        widget->setElements({ std::move(root) });
        m_ui->registerWidget(widgetId, widget, tabId);
    }

    // ── Tab / close click events ─────────────────────────────────────────
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_ui->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshMetrics();
    });

    m_ui->registerClickEvent(closeBtnId, [this]()
    {
        close();
    });

    // ── Toolbar button events ────────────────────────────────────────────
    m_ui->registerClickEvent("Profiler.Freeze", [this]()
    {
        m_state.frozen = !m_state.frozen;
        // Update button text and color in-place
        auto* btn = m_ui->findElementById("Profiler.Freeze");
        if (btn)
        {
            const auto& theme = EditorTheme::Get();
            btn->text            = m_state.frozen ? "Resume" : "Freeze";
            btn->style.textColor = m_state.frozen ? theme.accentGreen : theme.textPrimary;
            btn->style.color     = m_state.frozen ? Vec4{ 0.15f, 0.35f, 0.20f, 0.95f } : theme.buttonDefault;
        }
        refreshMetrics();
        auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
        if (entry && entry->widget)
            entry->widget->markLayoutDirty();
        m_ui->markRenderDirty();
    });

    // Initial population
    refreshMetrics();
}

// ───────────────────────────────────────────────────────────────────────────
void ProfilerTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "Profiler.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = "Performance Profiler";
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 140.0f, 24.0f });
        title.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Freeze / Resume button
    {
        WidgetElement freezeBtn{};
        freezeBtn.id            = "Profiler.Freeze";
        freezeBtn.type          = WidgetElementType::Button;
        freezeBtn.text          = m_state.frozen ? "Resume" : "Freeze";
        freezeBtn.font          = theme.fontDefault;
        freezeBtn.fontSize      = theme.fontSizeSmall;
        freezeBtn.style.textColor = m_state.frozen ? theme.accentGreen : theme.textPrimary;
        freezeBtn.style.color     = m_state.frozen ? Vec4{ 0.15f, 0.35f, 0.20f, 0.95f } : theme.buttonDefault;
        freezeBtn.style.hoverColor = theme.buttonHover;
        freezeBtn.textAlignH    = TextAlignH::Center;
        freezeBtn.textAlignV    = TextAlignV::Center;
        freezeBtn.minSize       = EditorTheme::Scaled(Vec2{ 70.0f, 24.0f });
        freezeBtn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        freezeBtn.hitTestMode   = HitTestMode::Enabled;
        freezeBtn.runtimeOnly   = true;
        freezeBtn.clickEvent    = "Profiler.Freeze";
        toolbar.children.push_back(std::move(freezeBtn));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// refreshMetrics – rebuilds the metrics area from DiagnosticsManager
// ───────────────────────────────────────────────────────────────────────────
void ProfilerTab::refreshMetrics()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    // Find the metrics area
    WidgetElement* metricsArea = nullptr;
    auto& elements = entry->widget->getElementsMutable();
    const std::function<WidgetElement*(WidgetElement&)> findRecursive =
        [&](WidgetElement& el) -> WidgetElement*
        {
            if (el.id == "Profiler.MetricsArea")
                return &el;
            for (auto& child : el.children)
            {
                if (auto* hit = findRecursive(child))
                    return hit;
            }
            return nullptr;
        };
    for (auto& el : elements)
    {
        if (auto* hit = findRecursive(el))
        {
            metricsArea = hit;
            break;
        }
    }
    if (!metricsArea)
        return;

    metricsArea->children.clear();

    const auto& theme = EditorTheme::Get();
    const auto& diag = DiagnosticsManager::Instance();
    const auto& metrics = diag.getLatestMetrics();
    const auto& history = diag.getFrameHistory();
    const size_t historySize = diag.getFrameHistorySize();

    // Helper: color-code a timing value (green < 8.3ms, yellow < 16.6ms, red >= 16.6ms)
    auto timingColor = [&](double ms) -> Vec4
    {
        if (ms < 8.3)
            return Vec4{ 0.4f, 1.0f, 0.4f, 1.0f };   // green
        if (ms < 16.6)
            return Vec4{ 1.0f, 0.85f, 0.3f, 1.0f };  // yellow
        return Vec4{ 1.0f, 0.35f, 0.35f, 1.0f };     // red
    };

    // Helper: format a timing value
    auto fmtMs = [](double ms) -> std::string
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f ms", ms);
        return buf;
    };

    // ── FPS bar chart (mini-graph using colored panels) ──────────────────
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Frame History (" + std::to_string(historySize) + " frames)";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        // Bar chart container
        const float barHeight = EditorTheme::Scaled(60.0f);
        WidgetElement barContainer{};
        barContainer.id          = "Profiler.BarChart";
        barContainer.type        = WidgetElementType::StackPanel;
        barContainer.orientation = StackOrientation::Horizontal;
        barContainer.fillX       = true;
        barContainer.minSize     = Vec2{ 0.0f, barHeight };
        barContainer.style.color = Vec4{ 0.05f, 0.06f, 0.08f, 1.0f };
        barContainer.padding     = Vec2{ 0.0f, 0.0f };
        barContainer.runtimeOnly = true;

        if (historySize > 0)
        {
            // Find max frame time for scaling
            double maxFrameMs = 16.6; // at least show 60fps baseline
            for (size_t i = 0; i < historySize; ++i)
                maxFrameMs = std::max(maxFrameMs, history[i].cpuFrameMs);
            maxFrameMs = std::max(maxFrameMs, 1.0); // avoid div by zero

            // Render bars in chronological order
            size_t startIdx = 0;
            if (historySize >= DiagnosticsManager::kMaxFrameHistory)
            {
                startIdx = (diag.getFrameHistorySize() == DiagnosticsManager::kMaxFrameHistory)
                    ? 0 : 0;
            }

            const size_t barCount = std::min(historySize, static_cast<size_t>(150)); // show last 150
            const size_t skipStart = (historySize > barCount) ? (historySize - barCount) : 0;

            for (size_t i = skipStart; i < historySize; ++i)
            {
                const auto& frame = history[i];
                const float ratio = static_cast<float>(std::min(frame.cpuFrameMs / maxFrameMs, 1.0));

                WidgetElement bar{};
                bar.type        = WidgetElementType::Panel;
                bar.fillX       = true;
                bar.minSize     = Vec2{ 0.0f, barHeight * ratio };
                bar.style.color = timingColor(frame.cpuFrameMs);
                bar.runtimeOnly = true;
                barContainer.children.push_back(std::move(bar));
            }
        }

        metricsArea->children.push_back(std::move(barContainer));

        // 60fps / 30fps reference labels
        {
            WidgetElement refLine{};
            refLine.type            = WidgetElementType::Text;
            refLine.text            = "--- 16.6ms (60fps) --- 33.3ms (30fps) ---";
            refLine.font            = theme.fontDefault;
            refLine.fontSize        = theme.fontSizeSmall;
            refLine.style.textColor = theme.textMuted;
            refLine.fillX           = true;
            refLine.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 16.0f });
            refLine.runtimeOnly     = true;
            metricsArea->children.push_back(std::move(refLine));
        }
    }

    // ── Divider ──────────────────────────────────────────────────────────
    metricsArea->children.push_back(EditorUIBuilder::makeDivider());

    // ── Summary section ──────────────────────────────────────────────────
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Current Frame";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        // FPS
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeLabel("FPS", EditorTheme::Scaled(120.0f));
            WidgetElement val = EditorUIBuilder::makeLabel(std::to_string(static_cast<int>(metrics.fps + 0.5)));
            val.style.textColor = timingColor(metrics.cpuFrameMs);
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));
            metricsArea->children.push_back(std::move(row));
        }

        // Helper to add a metric row
        auto addRow = [&](const std::string& label, double valueMs)
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeLabel(label, EditorTheme::Scaled(120.0f));
            lbl.fillX = false;
            WidgetElement val = EditorUIBuilder::makeLabel(fmtMs(valueMs));
            val.fillX = false;
            val.minSize.x = EditorTheme::Scaled(80.0f);
            val.style.textColor = timingColor(valueMs);
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));

            // Mini bar showing proportion of frame time
            const float maxBar = EditorTheme::Scaled(200.0f);
            const float barW = static_cast<float>(std::min(valueMs / 33.3, 1.0)) * maxBar;
            WidgetElement bar{};
            bar.type        = WidgetElementType::Panel;
            bar.minSize     = Vec2{ std::max(barW, 1.0f), EditorTheme::Scaled(8.0f) };
            bar.style.color = timingColor(valueMs);
            bar.runtimeOnly = true;
            row.children.push_back(std::move(bar));

            metricsArea->children.push_back(std::move(row));
        };

        addRow("CPU Frame", metrics.cpuFrameMs);
        addRow("GPU Frame", metrics.gpuFrameMs);
    }

    metricsArea->children.push_back(EditorUIBuilder::makeDivider());

    // ── Breakdown section ────────────────────────────────────────────────
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "CPU Breakdown";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        auto addBreakdownRow = [&](const std::string& label, double valueMs)
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeSecondaryLabel(label, EditorTheme::Scaled(120.0f));
            lbl.fillX = false;
            WidgetElement val = EditorUIBuilder::makeLabel(fmtMs(valueMs));
            val.fillX = false;
            val.minSize.x = EditorTheme::Scaled(80.0f);
            val.style.textColor = timingColor(valueMs);
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));

            // Proportion bar
            const float maxBar = EditorTheme::Scaled(200.0f);
            const float barW = (metrics.cpuFrameMs > 0.001)
                ? static_cast<float>(valueMs / metrics.cpuFrameMs) * maxBar
                : 0.0f;
            WidgetElement bar{};
            bar.type        = WidgetElementType::Panel;
            bar.minSize     = Vec2{ std::max(barW, 1.0f), EditorTheme::Scaled(6.0f) };
            bar.style.color = timingColor(valueMs);
            bar.style.borderRadius = EditorTheme::Scaled(2.0f);
            bar.runtimeOnly = true;
            row.children.push_back(std::move(bar));

            metricsArea->children.push_back(std::move(row));
        };

        addBreakdownRow("World Render", metrics.cpuWorldMs);
        addBreakdownRow("UI Render", metrics.cpuUiMs);
        addBreakdownRow("UI Layout", metrics.cpuUiLayoutMs);
        addBreakdownRow("UI Draw", metrics.cpuUiDrawMs);
        addBreakdownRow("ECS", metrics.cpuEcsMs);
        addBreakdownRow("Input", metrics.cpuInputMs);
        addBreakdownRow("Events", metrics.cpuEventMs);
        addBreakdownRow("Render Pass", metrics.cpuRenderMs);
        addBreakdownRow("GC", metrics.cpuGcMs);
        addBreakdownRow("Other", metrics.cpuOtherMs);
    }

    metricsArea->children.push_back(EditorUIBuilder::makeDivider());

    // ── Occlusion culling stats ──────────────────────────────────────────
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Occlusion Culling";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        auto addStatRow = [&](const std::string& label, uint32_t value)
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeSecondaryLabel(label, EditorTheme::Scaled(120.0f));
            WidgetElement val = EditorUIBuilder::makeLabel(std::to_string(value));
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));
            metricsArea->children.push_back(std::move(row));
        };

        addStatRow("Visible", metrics.visibleCount);
        addStatRow("Hidden", metrics.hiddenCount);
        addStatRow("Total", metrics.totalCount);

        if (metrics.totalCount > 0)
        {
            const float cullPercent = 100.0f * static_cast<float>(metrics.hiddenCount) / static_cast<float>(metrics.totalCount);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f%%", cullPercent);
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeSecondaryLabel("Cull Rate", EditorTheme::Scaled(120.0f));
            WidgetElement val = EditorUIBuilder::makeLabel(buf);
            val.style.textColor = Vec4{ 0.5f, 0.9f, 1.0f, 1.0f };
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));
            metricsArea->children.push_back(std::move(row));
        }
    }

    // ── Status line ──────────────────────────────────────────────────────
    {
        WidgetElement status{};
        status.type            = WidgetElementType::Text;
        status.text            = m_state.frozen ? "[ FROZEN ]" : "Live";
        status.font            = theme.fontDefault;
        status.fontSize        = theme.fontSizeSmall;
        status.style.textColor = m_state.frozen
            ? Vec4{ 1.0f, 0.6f, 0.3f, 1.0f }
            : Vec4{ 0.4f, 1.0f, 0.4f, 0.7f };
        status.fillX           = true;
        status.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        status.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        status.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(status));
    }

    m_ui->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void ProfilerTab::close()
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
void ProfilerTab::update(float deltaSeconds)
{
    if (!m_state.isOpen || m_state.frozen)
        return;

    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.25f)
    {
        m_state.refreshTimer = 0.0f;
        refreshMetrics();
    }
}
