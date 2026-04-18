#include "RenderDebuggerTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Logger/Logger.h"

#include <string>
#include <cstdio>

// ───────────────────────────────────────────────────────────────────────────
RenderDebuggerTab::RenderDebuggerTab(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer)
{
}

// ───────────────────────────────────────────────────────────────────────────
bool RenderDebuggerTab::isOpen() const
{
    return m_state.isOpen;
}

// ───────────────────────────────────────────────────────────────────────────
// update – periodic refresh (0.5 s)
// ───────────────────────────────────────────────────────────────────────────
void RenderDebuggerTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;

    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.5f)
    {
        m_state.refreshTimer = 0.0f;
        refresh();
    }
}

// ───────────────────────────────────────────────────────────────────────────
// open
// ───────────────────────────────────────────────────────────────────────────
void RenderDebuggerTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "RenderDebugger";

    // If already open, just switch to it
    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Render Debugger", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "RenderDebugger.Main";

    // Clean up any stale registration
    m_uiManager->unregisterWidget(widgetId);

    // Initialise state
    m_state = {};
    m_state.tabId    = tabId;
    m_state.widgetId = widgetId;
    m_state.isOpen   = true;

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
        root.id          = "RenderDebugger.Root";
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

        // ── Scrollable pass list area ────────────────────────────────────
        {
            WidgetElement passArea{};
            passArea.id          = "RenderDebugger.PassArea";
            passArea.type        = WidgetElementType::StackPanel;
            passArea.fillX       = true;
            passArea.fillY       = true;
            passArea.scrollable  = true;
            passArea.orientation = StackOrientation::Vertical;
            passArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            passArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            passArea.runtimeOnly = true;
            root.children.push_back(std::move(passArea));
        }

        widget->setElements({ std::move(root) });
        m_uiManager->registerWidget(widgetId, widget, tabId);
    }

    // ── Tab / close click events ─────────────────────────────────────────
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_uiManager->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refresh();
    });

    m_uiManager->registerClickEvent(closeBtnId, [this]()
    {
        close();
    });

    // Initial population
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
// close
// ───────────────────────────────────────────────────────────────────────────
void RenderDebuggerTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    const std::string tabId = m_state.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_uiManager->unregisterWidget(m_state.widgetId);

    m_renderer->removeTab(tabId);
    m_state = {};
    m_uiManager->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
// buildToolbar
// ───────────────────────────────────────────────────────────────────────────
void RenderDebuggerTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "RenderDebugger.Toolbar";
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
        title.text            = "Render-Pass Debugger";
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignH      = TextAlignH::Left;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
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

    // Pass count summary
    if (m_renderer)
    {
        const auto passes = m_renderer->getRenderPassInfo();
        int enabledCount = 0;
        for (const auto& p : passes)
            if (p.enabled) ++enabledCount;

        WidgetElement summary{};
        summary.type            = WidgetElementType::Text;
        summary.text            = std::to_string(enabledCount) + " / " + std::to_string(passes.size()) + " passes active";
        summary.font            = theme.fontDefault;
        summary.fontSize        = theme.fontSizeSmall;
        summary.style.textColor = theme.textMuted;
        summary.textAlignH      = TextAlignH::Right;
        summary.textAlignV      = TextAlignV::Center;
        summary.minSize         = EditorTheme::Scaled(Vec2{ 140.0f, 24.0f });
        summary.runtimeOnly     = true;
        toolbar.children.push_back(std::move(summary));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// refresh – rebuilds the pass list with current pipeline state
// ───────────────────────────────────────────────────────────────────────────
void RenderDebuggerTab::refresh()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    auto* entry = m_uiManager->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find the pass-area container
    WidgetElement* passArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "RenderDebugger.PassArea")
        {
            passArea = &child;
            break;
        }
    }
    if (!passArea)
        return;

    // Rebuild toolbar
    {
        WidgetElement* toolbar = nullptr;
        for (auto& child : elements[0].children)
        {
            if (child.id == "RenderDebugger.Toolbar")
            {
                toolbar = &child;
                break;
            }
        }
        if (toolbar)
        {
            toolbar->children.clear();
            const auto& theme = EditorTheme::Get();

            // Title
            {
                WidgetElement title{};
                title.type            = WidgetElementType::Text;
                title.text            = "Render-Pass Debugger";
                title.font            = theme.fontDefault;
                title.fontSize        = theme.fontSizeSubheading;
                title.style.textColor = theme.textPrimary;
                title.textAlignH      = TextAlignH::Left;
                title.textAlignV      = TextAlignV::Center;
                title.minSize         = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
                title.runtimeOnly     = true;
                toolbar->children.push_back(std::move(title));
            }

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

            // Summary
            {
                const auto passes = m_renderer->getRenderPassInfo();
                int enabledCount = 0;
                for (const auto& p : passes)
                    if (p.enabled) ++enabledCount;

                WidgetElement summary{};
                summary.type            = WidgetElementType::Text;
                summary.text            = std::to_string(enabledCount) + " / " + std::to_string(passes.size()) + " passes active";
                summary.font            = theme.fontDefault;
                summary.fontSize        = theme.fontSizeSmall;
                summary.style.textColor = theme.textMuted;
                summary.textAlignH      = TextAlignH::Right;
                summary.textAlignV      = TextAlignV::Center;
                summary.minSize         = EditorTheme::Scaled(Vec2{ 140.0f, 24.0f });
                summary.runtimeOnly     = true;
                toolbar->children.push_back(std::move(summary));
            }
        }
    }

    passArea->children.clear();

    const auto& theme = EditorTheme::Get();
    const auto passes = m_renderer->getRenderPassInfo();

    // Timing summary from DiagnosticsManager
    const auto& metrics = DiagnosticsManager::Instance().getLatestMetrics();

    // ── Frame timing header ──────────────────────────────────────────────
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Frame: %.1f FPS | CPU World: %.2f ms | CPU UI: %.2f ms | GPU: %.2f ms",
            metrics.fps, metrics.cpuWorldMs, metrics.cpuUiMs, metrics.gpuFrameMs);

        WidgetElement header{};
        header.type            = WidgetElementType::Text;
        header.text            = buf;
        header.font            = theme.fontDefault;
        header.fontSize        = theme.fontSizeSmall;
        header.style.textColor = theme.accent;
        header.textAlignH      = TextAlignH::Left;
        header.textAlignV      = TextAlignV::Center;
        header.fillX           = true;
        header.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        header.runtimeOnly     = true;
        passArea->children.push_back(std::move(header));
    }

    // Object count row
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Objects: %u visible, %u culled, %u total",
            metrics.visibleCount, metrics.hiddenCount, metrics.totalCount);

        WidgetElement row{};
        row.type            = WidgetElementType::Text;
        row.text            = buf;
        row.font            = theme.fontDefault;
        row.fontSize        = theme.fontSizeSmall;
        row.style.textColor = theme.textSecondary;
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.fillX           = true;
        row.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        row.runtimeOnly     = true;
        passArea->children.push_back(std::move(row));
    }

    // ── Divider ──────────────────────────────────────────────────────────
    {
        WidgetElement div{};
        div.type        = WidgetElementType::Panel;
        div.fillX       = true;
        div.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        div.style.color = theme.panelBorder;
        div.runtimeOnly = true;
        passArea->children.push_back(std::move(div));
    }

    // ── Pipeline passes ──────────────────────────────────────────────────
    std::string lastCategory;
    int passIndex = 0;

    // Category colour mapping
    auto categoryColor = [&](const std::string& cat) -> Vec4
    {
        if (cat == "Shadow")       return Vec4{ 0.60f, 0.45f, 0.80f, 1.0f };
        if (cat == "Geometry")     return Vec4{ 0.40f, 0.70f, 0.90f, 1.0f };
        if (cat == "Post-Process") return Vec4{ 0.85f, 0.65f, 0.30f, 1.0f };
        if (cat == "Overlay")      return Vec4{ 0.40f, 0.80f, 0.55f, 1.0f };
        if (cat == "Utility")      return Vec4{ 0.65f, 0.65f, 0.65f, 1.0f };
        if (cat == "UI")           return Vec4{ 0.90f, 0.55f, 0.55f, 1.0f };
        return theme.textSecondary;
    };

    const float rowH = EditorTheme::Scaled(20.0f);

    for (const auto& pass : passes)
    {
        // Category header when it changes
        if (pass.category != lastCategory)
        {
            lastCategory = pass.category;

            if (passIndex > 0)
            {
                WidgetElement space{};
                space.type        = WidgetElementType::Panel;
                space.fillX       = true;
                space.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
                space.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                space.runtimeOnly = true;
                passArea->children.push_back(std::move(space));
            }

            WidgetElement catLabel{};
            catLabel.type            = WidgetElementType::Text;
            catLabel.text            = "\xe2\x94\x80\xe2\x94\x80 " + pass.category + " \xe2\x94\x80\xe2\x94\x80";
            catLabel.font            = theme.fontDefault;
            catLabel.fontSize        = theme.fontSizeSmall;
            catLabel.style.textColor = categoryColor(pass.category);
            catLabel.textAlignH      = TextAlignH::Left;
            catLabel.textAlignV      = TextAlignV::Center;
            catLabel.fillX           = true;
            catLabel.minSize         = Vec2{ 0.0f, rowH };
            catLabel.padding         = EditorTheme::Scaled(Vec2{ 2.0f, 2.0f });
            catLabel.runtimeOnly     = true;
            passArea->children.push_back(std::move(catLabel));
        }

        // Pass row: [status] [name] [FBO info] [details]
        {
            WidgetElement passRow{};
            passRow.id          = "RenderDebugger.Pass." + std::to_string(passIndex);
            passRow.type        = WidgetElementType::StackPanel;
            passRow.fillX       = true;
            passRow.orientation = StackOrientation::Horizontal;
            passRow.minSize     = Vec2{ 0.0f, rowH };
            passRow.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 1.0f });
            passRow.style.color = (passIndex % 2 == 0)
                                  ? Vec4{ 0.07f, 0.07f, 0.09f, 1.0f }
                                  : Vec4{ 0.09f, 0.09f, 0.11f, 1.0f };
            passRow.runtimeOnly = true;

            // Status indicator
            {
                WidgetElement status{};
                status.type            = WidgetElementType::Text;
                status.text            = pass.enabled ? "\xe2\x97\x8f" : "\xe2\x97\x8b";
                status.font            = theme.fontDefault;
                status.fontSize        = theme.fontSizeSmall;
                status.style.textColor = pass.enabled ? theme.successColor : theme.textMuted;
                status.textAlignH      = TextAlignH::Center;
                status.textAlignV      = TextAlignV::Center;
                status.minSize         = EditorTheme::Scaled(Vec2{ 20.0f, rowH });
                status.runtimeOnly     = true;
                passRow.children.push_back(std::move(status));
            }

            // Pass name
            {
                WidgetElement name{};
                name.type            = WidgetElementType::Text;
                name.text            = pass.name;
                name.font            = theme.fontDefault;
                name.fontSize        = theme.fontSizeSmall;
                name.style.textColor = pass.enabled ? theme.textPrimary : theme.textMuted;
                name.textAlignH      = TextAlignH::Left;
                name.textAlignV      = TextAlignV::Center;
                name.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, rowH });
                name.runtimeOnly     = true;
                passRow.children.push_back(std::move(name));
            }

            // FBO format / resolution
            {
                std::string fboText = pass.fboFormat;
                if (pass.fboWidth > 0 && pass.fboHeight > 0)
                    fboText = std::to_string(pass.fboWidth) + "x" + std::to_string(pass.fboHeight) + " " + pass.fboFormat;

                WidgetElement fbo{};
                fbo.type            = WidgetElementType::Text;
                fbo.text            = fboText;
                fbo.font            = theme.fontDefault;
                fbo.fontSize        = theme.fontSizeCaption;
                fbo.style.textColor = categoryColor(pass.category);
                fbo.textAlignH      = TextAlignH::Left;
                fbo.textAlignV      = TextAlignV::Center;
                fbo.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, rowH });
                fbo.runtimeOnly     = true;
                passRow.children.push_back(std::move(fbo));
            }

            // Details
            {
                WidgetElement det{};
                det.type            = WidgetElementType::Text;
                det.text            = pass.details;
                det.font            = theme.fontDefault;
                det.fontSize        = theme.fontSizeCaption;
                det.style.textColor = theme.textSecondary;
                det.textAlignH      = TextAlignH::Left;
                det.textAlignV      = TextAlignV::Center;
                det.fillX           = true;
                det.minSize         = Vec2{ 0.0f, rowH };
                det.runtimeOnly     = true;
                passRow.children.push_back(std::move(det));
            }

            passArea->children.push_back(std::move(passRow));
        }

        ++passIndex;
    }

    // ── Pipeline flow diagram ────────────────────────────────────────────
    {
        WidgetElement space{};
        space.type        = WidgetElementType::Panel;
        space.fillX       = true;
        space.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 10.0f });
        space.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        space.runtimeOnly = true;
        passArea->children.push_back(std::move(space));
    }

    {
        WidgetElement flowTitle{};
        flowTitle.type            = WidgetElementType::Text;
        flowTitle.text            = "\xe2\x94\x80\xe2\x94\x80 Pipeline Flow \xe2\x94\x80\xe2\x94\x80";
        flowTitle.font            = theme.fontDefault;
        flowTitle.fontSize        = theme.fontSizeSmall;
        flowTitle.style.textColor = theme.textMuted;
        flowTitle.textAlignH      = TextAlignH::Left;
        flowTitle.textAlignV      = TextAlignV::Center;
        flowTitle.fillX           = true;
        flowTitle.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        flowTitle.runtimeOnly     = true;
        passArea->children.push_back(std::move(flowTitle));
    }

    // Build a simplified flow text
    {
        std::string flow;
        flow += "Shadows -> Skybox -> Geometry (Opaque)";
        flow += " -> Particles -> OIT -> HZB";
        flow += " -> Resolve (Bloom + SSAO + ToneMap + Gamma)";
        flow += " -> Grid -> Colliders -> Bones -> Outline -> Gizmo -> FXAA -> UI";

        WidgetElement flowLine{};
        flowLine.type            = WidgetElementType::Text;
        flowLine.text            = flow;
        flowLine.font            = theme.fontDefault;
        flowLine.fontSize        = theme.fontSizeCaption;
        flowLine.style.textColor = theme.accent;
        flowLine.textAlignH      = TextAlignH::Left;
        flowLine.textAlignV      = TextAlignV::Center;
        flowLine.fillX           = true;
        flowLine.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        flowLine.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
        flowLine.runtimeOnly     = true;
        passArea->children.push_back(std::move(flowLine));
    }

    m_uiManager->markAllWidgetsDirty();
}
