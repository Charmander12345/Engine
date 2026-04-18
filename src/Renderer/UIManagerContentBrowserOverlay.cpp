// UIManagerContentBrowserOverlay.cpp
// Content Browser Overlay (Ctrl+Space) implementation.
// Split from UIManagerEditor.cpp to keep translation units within MSVC limits.
#if ENGINE_EDITOR

#include "UIManager.h"
#include "EditorTheme.h"
#include "EditorUI/EditorWidget.h"
#include "../Editor/Tabs/ContentBrowserPanel.h"

void UIManager::openContentBrowserOverlay()
{
    // The overlay is not needed in the Viewport tab — the docked content
    // browser is already visible there.
    if (m_activeTabId == "Viewport")
        return;

    if (m_contentBrowserOverlayOpen)
    {
        // Already open — just (re-)focus the search bar
        if (m_contentBrowserPanel)
            m_contentBrowserPanel->focusSearch();
        return;
    }

    m_contentBrowserOverlayOpen = true;

    const auto& theme = EditorTheme::Get();

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("ContentBrowserOverlay");
    widget->setAnchor(WidgetAnchor::TopLeft);
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setZOrder(50);

    // ── Semi-transparent backdrop — click anywhere outside the panel to close ──
    WidgetElement backdrop{};
    backdrop.id          = "CBOverlay.Backdrop";
    backdrop.type        = WidgetElementType::Panel;
    backdrop.from        = Vec2{ 0.0f, 0.0f };
    backdrop.to          = Vec2{ 1.0f, 1.0f };
    backdrop.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.55f };
    backdrop.hitTestMode = HitTestMode::Enabled;
    backdrop.runtimeOnly = true;
    backdrop.onClicked   = [this]() { closeContentBrowserOverlay(); };

    // ── Floating panel (84 % of the editor area) ──────────────────────────
    WidgetElement panel{};
    panel.id             = "CBOverlay.Panel";
    panel.type           = WidgetElementType::StackPanel;
    panel.from           = Vec2{ 0.08f, 0.08f };
    panel.to             = Vec2{ 0.92f, 0.92f };
    panel.orientation    = StackOrientation::Vertical;
    panel.style.color    = Vec4{ 0.09f, 0.10f, 0.13f, 0.98f };
    panel.hitTestMode    = HitTestMode::DisabledSelf;
    panel.elevation      = 4;
    panel.style.applyElevation(4, theme.shadowColor, theme.shadowOffset);
    panel.style.borderRadius = theme.borderRadius;
    panel.runtimeOnly    = true;

    // ── Header row: title + shortcut hint + close button ─────────────────
    {
        WidgetElement header{};
        header.id          = "CBOverlay.Header";
        header.type        = WidgetElementType::StackPanel;
        header.fillX       = true;
        header.orientation = StackOrientation::Horizontal;
        header.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 36.0f });
        header.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 4.0f });
        header.style.color = Vec4{ 0.12f, 0.14f, 0.18f, 1.0f };
        header.runtimeOnly = true;

        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.id              = "CBOverlay.Title";
        title.text            = "Content Browser";
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = Vec4{ 0.55f, 0.85f, 1.0f, 1.0f };
        title.textAlignV      = TextAlignV::Center;
        title.fillX           = true;
        title.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 28.0f });
        title.runtimeOnly     = true;
        header.children.push_back(std::move(title));

        WidgetElement hint{};
        hint.type            = WidgetElementType::Text;
        hint.id              = "CBOverlay.Hint";
        hint.text            = "Ctrl+Space";
        hint.fontSize        = theme.fontSizeSmall;
        hint.style.textColor = Vec4{ 0.40f, 0.42f, 0.52f, 1.0f };
        hint.textAlignV      = TextAlignV::Center;
        hint.minSize         = EditorTheme::Scaled(Vec2{ 72.0f, 28.0f });
        hint.runtimeOnly     = true;
        header.children.push_back(std::move(hint));

        WidgetElement closeBtn{};
        closeBtn.id               = "CBOverlay.CloseBtn";
        closeBtn.type             = WidgetElementType::Button;
        closeBtn.text             = "\xC3\x97";  // UTF-8 ×
        closeBtn.fontSize         = theme.fontSizeSubheading;
        closeBtn.style.textColor  = Vec4{ 0.9f, 0.5f, 0.5f, 1.0f };
        closeBtn.style.color      = Vec4{ 0.18f, 0.10f, 0.10f, 0.8f };
        closeBtn.style.hoverColor = Vec4{ 0.36f, 0.15f, 0.15f, 1.0f };
        closeBtn.minSize          = EditorTheme::Scaled(Vec2{ 32.0f, 28.0f });
        closeBtn.textAlignH       = TextAlignH::Center;
        closeBtn.textAlignV       = TextAlignV::Center;
        closeBtn.hitTestMode      = HitTestMode::Enabled;
        closeBtn.runtimeOnly      = true;
        closeBtn.onClicked        = [this]() { closeContentBrowserOverlay(); };
        header.children.push_back(std::move(closeBtn));

        panel.children.push_back(std::move(header));
    }

    // ── Header separator ─────────────────────────────────────────────────
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        panel.children.push_back(std::move(sep));
    }

    // ── Content row: tree | vsep | right side (pathbar + grid) ───────────
    {
        WidgetElement contentRow{};
        contentRow.id          = "CBOverlay.ContentRow";
        contentRow.type        = WidgetElementType::StackPanel;
        contentRow.fillX       = true;
        contentRow.fillY       = true;
        contentRow.orientation = StackOrientation::Horizontal;
        contentRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        contentRow.runtimeOnly = true;

        // Tree panel — skeleton; populated by ContentBrowserPanel::refresh()
        {
            WidgetElement tree{};
            tree.id          = "ContentBrowser.Tree";
            tree.type        = WidgetElementType::StackPanel;
            tree.orientation = StackOrientation::Vertical;
            tree.fillY       = true;
            tree.scrollable  = true;
            tree.minSize     = EditorTheme::Scaled(Vec2{ 210.0f, 0.0f });
            tree.maxSize     = EditorTheme::Scaled(Vec2{ 210.0f, 0.0f });
            tree.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
            tree.style.color = Vec4{ 0.07f, 0.08f, 0.10f, 1.0f };
            tree.runtimeOnly = true;
            contentRow.children.push_back(std::move(tree));
        }

        // Vertical separator
        {
            WidgetElement vsep{};
            vsep.type        = WidgetElementType::Panel;
            vsep.fillY       = true;
            vsep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 0.0f });
            vsep.style.color = theme.panelBorder;
            vsep.runtimeOnly = true;
            contentRow.children.push_back(std::move(vsep));
        }

        // Right side: PathBar + separator + Grid
        {
            WidgetElement rightSide{};
            rightSide.id          = "CBOverlay.RightSide";
            rightSide.type        = WidgetElementType::StackPanel;
            rightSide.orientation = StackOrientation::Vertical;
            rightSide.fillX       = true;
            rightSide.fillY       = true;
            rightSide.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            rightSide.runtimeOnly = true;

            // PathBar — populated by ContentBrowserPanel::refresh()
            {
                WidgetElement pathBar{};
                pathBar.id          = "ContentBrowser.PathBar";
                pathBar.type        = WidgetElementType::StackPanel;
                pathBar.orientation = StackOrientation::Horizontal;
                pathBar.fillX       = true;
                pathBar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
                pathBar.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
                pathBar.style.color = Vec4{ 0.10f, 0.11f, 0.14f, 1.0f };
                pathBar.runtimeOnly = true;
                rightSide.children.push_back(std::move(pathBar));
            }

            // PathBar bottom separator
            {
                WidgetElement sep{};
                sep.type        = WidgetElementType::Panel;
                sep.fillX       = true;
                sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                sep.style.color = theme.panelBorder;
                sep.runtimeOnly = true;
                rightSide.children.push_back(std::move(sep));
            }

            // Grid — populated by ContentBrowserPanel::refresh()
            {
                WidgetElement grid{};
                grid.id          = "ContentBrowser.Grid";
                grid.type        = WidgetElementType::WrapBox;
                grid.orientation = StackOrientation::Horizontal;
                grid.fillX       = true;
                grid.fillY       = true;
                grid.scrollable  = true;
                grid.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 6.0f });
                grid.spacing     = EditorTheme::Scaled(4.0f);
                grid.style.color = Vec4{ 0.09f, 0.10f, 0.12f, 1.0f };
                grid.runtimeOnly = true;
                rightSide.children.push_back(std::move(grid));
            }

            contentRow.children.push_back(std::move(rightSide));
        }

        panel.children.push_back(std::move(contentRow));
    }

    widget->setElements({ std::move(backdrop), std::move(panel) });
    registerWidget("ContentBrowserOverlay", widget);

    if (m_contentBrowserPanel)
    {
        m_contentBrowserPanel->refresh();
        m_contentBrowserPanel->focusSearch();
    }

    markRenderDirty();
}

void UIManager::closeContentBrowserOverlay()
{
    if (!m_contentBrowserOverlayOpen)
        return;

    unregisterWidget("ContentBrowserOverlay");
    m_contentBrowserOverlayOpen = false;
    m_contentBrowserOverlayHiddenForDrag = false;
    markRenderDirty();
}

void UIManager::toggleContentBrowserOverlay()
{
    if (m_contentBrowserOverlayOpen)
        closeContentBrowserOverlay();
    else
        openContentBrowserOverlay();
}

bool UIManager::isContentBrowserOverlayOpen() const
{
    return m_contentBrowserOverlayOpen;
}

// ── Drag-hide / restore ───────────────────────────────────────────────────

void UIManager::onOverlayDragStarted()
{
    // Unregister the overlay widget so the viewport is fully unobstructed
    // while the user drags an asset.  The overlay state (folder selection,
    // search text, type filter, expanded folders) lives in ContentBrowserPanel
    // and is automatically reused when the overlay is rebuilt after the drop.
    unregisterWidget("ContentBrowserOverlay");
    m_contentBrowserOverlayHiddenForDrag = true;
    markRenderDirty();
}

void UIManager::onOverlayDragEnded()
{
    if (!m_contentBrowserOverlayHiddenForDrag)
        return;

    // Reset flags before calling openContentBrowserOverlay so it does not
    // see m_contentBrowserOverlayOpen == true and bail out early.
    m_contentBrowserOverlayHiddenForDrag = false;
    m_contentBrowserOverlayOpen = false;
    openContentBrowserOverlay();
}

#endif // ENGINE_EDITOR
