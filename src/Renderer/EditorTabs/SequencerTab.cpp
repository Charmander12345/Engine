#include "SequencerTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Logger/Logger.h"

#include <string>
#include <cstdio>

// ───────────────────────────────────────────────────────────────────────────
SequencerTab::SequencerTab(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer)
{
}

// ───────────────────────────────────────────────────────────────────────────
bool SequencerTab::isOpen() const
{
    return m_state.isOpen;
}

// ───────────────────────────────────────────────────────────────────────────
// update – periodic refresh while playing (0.1 s)
// ───────────────────────────────────────────────────────────────────────────
void SequencerTab::update(float deltaSeconds)
{
    if (!m_state.isOpen || !m_state.playing)
        return;

    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.1f)
    {
        m_state.refreshTimer = 0.0f;
        refresh();
    }
}

// ───────────────────────────────────────────────────────────────────────────
// open
// ───────────────────────────────────────────────────────────────────────────
void SequencerTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Sequencer";

    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Sequencer", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Sequencer.Main";
    m_uiManager->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId    = tabId;
    m_state.widgetId = widgetId;
    m_state.isOpen   = true;

    // Seed from existing camera path if one is set
    {
        auto pts = m_renderer->getCameraPathPoints();
        if (!pts.empty())
        {
            m_state.pathDuration = m_renderer->getCameraPathDuration();
            m_state.loopPlayback = m_renderer->getCameraPathLoop();
        }
    }

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
        root.id          = "Sequencer.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // Toolbar
        buildToolbar(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Timeline area
        buildTimeline(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Keyframe list (scrollable)
        buildKeyframeList(root);

        widget->setElements({ std::move(root) });
        m_uiManager->registerWidget(widgetId, widget, tabId);
    }

    // Tab / close click events
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

    // Toolbar button events
    m_uiManager->registerClickEvent("Sequencer.AddKeyframe", [this]()
    {
        if (!m_renderer) return;
        auto pts = m_renderer->getCameraPathPoints();
        CameraPathPoint pt;
        pt.position = m_renderer->getCameraPosition();
        auto rot = m_renderer->getCameraRotationDegrees();
        pt.yaw   = rot.x;
        pt.pitch = rot.y;
        pts.push_back(pt);
        m_renderer->setCameraPathPoints(pts);
        m_state.selectedKeyframe = static_cast<int>(pts.size()) - 1;
        refresh();
        m_uiManager->showToastMessage("Keyframe " + std::to_string(pts.size()) + " added", UIManager::kToastShort);
    });

    m_uiManager->registerClickEvent("Sequencer.RemoveKeyframe", [this]()
    {
        if (!m_renderer) return;
        auto pts = m_renderer->getCameraPathPoints();
        int sel = m_state.selectedKeyframe;
        if (sel < 0 || sel >= static_cast<int>(pts.size())) return;
        pts.erase(pts.begin() + sel);
        m_renderer->setCameraPathPoints(pts);
        if (sel >= static_cast<int>(pts.size()))
            m_state.selectedKeyframe = static_cast<int>(pts.size()) - 1;
        refresh();
        m_uiManager->showToastMessage("Keyframe removed", UIManager::kToastShort);
    });

    m_uiManager->registerClickEvent("Sequencer.Play", [this]()
    {
        if (!m_renderer) return;
        auto pts = m_renderer->getCameraPathPoints();
        if (pts.size() < 2)
        {
            m_uiManager->showToastMessage("Need at least 2 keyframes", UIManager::kToastShort);
            return;
        }
        if (m_renderer->isCameraPathPlaying())
        {
            m_renderer->pauseCameraPath();
            m_state.playing = false;
        }
        else if (m_state.playing)
        {
            m_renderer->resumeCameraPath();
            m_state.playing = true;
        }
        else
        {
            m_renderer->setCameraPathDuration(m_state.pathDuration);
            m_renderer->setCameraPathLoop(m_state.loopPlayback);
            m_renderer->startCameraPath(pts, m_state.pathDuration, m_state.loopPlayback);
            m_state.playing = true;
        }
        refresh();
    });

    m_uiManager->registerClickEvent("Sequencer.Stop", [this]()
    {
        if (!m_renderer) return;
        m_renderer->stopCameraPath();
        m_state.playing = false;
        m_state.scrubberT = 0.0f;
        refresh();
    });

    m_uiManager->registerClickEvent("Sequencer.Loop", [this]()
    {
        m_state.loopPlayback = !m_state.loopPlayback;
        if (m_renderer)
            m_renderer->setCameraPathLoop(m_state.loopPlayback);
        refresh();
    });

    m_uiManager->registerClickEvent("Sequencer.ShowSpline", [this]()
    {
        m_state.showSplineInViewport = !m_state.showSplineInViewport;
        refresh();
    });

    // Keyframe click events (registered dynamically during refresh)

    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
// close
// ───────────────────────────────────────────────────────────────────────────
void SequencerTab::close()
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
void SequencerTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "Sequencer.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.fillX       = true;
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
    toolbar.spacing     = EditorTheme::Scaled(6.0f);
    toolbar.style.color = theme.titleBarBackground;
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement lbl{};
        lbl.type             = WidgetElementType::Label;
        lbl.text             = "Sequencer";
        lbl.style.textColor  = theme.textPrimary;
        lbl.fontSize         = EditorTheme::Scaled(13.0f);
        lbl.runtimeOnly      = true;
        toolbar.children.push_back(std::move(lbl));
    }

    // Spacer
    {
        WidgetElement sp{};
        sp.type        = WidgetElementType::Panel;
        sp.fillX       = true;
        sp.minSize     = EditorTheme::Scaled(Vec2{ 4.0f, 1.0f });
        sp.style.color = Vec4{ 0,0,0,0 };
        sp.runtimeOnly = true;
        toolbar.children.push_back(std::move(sp));
    }

    auto makeBtn = [&](const std::string& id, const std::string& text, const std::string& tooltip, const Vec4& color) {
        WidgetElement btn{};
        btn.id              = id;
        btn.type            = WidgetElementType::Button;
        btn.clickEvent      = id;
        btn.text            = text;
        btn.tooltipText     = tooltip;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 26.0f, 24.0f });
        btn.style.color     = theme.inputBackground;
        btn.style.textColor = color;
        btn.fontSize        = EditorTheme::Scaled(13.0f);
        btn.runtimeOnly     = true;
        return btn;
    };

    toolbar.children.push_back(makeBtn("Sequencer.AddKeyframe", "+", "Add Keyframe at Camera", theme.accentGreen));
    toolbar.children.push_back(makeBtn("Sequencer.RemoveKeyframe", "-", "Remove Selected Keyframe", Vec4{1.0f, 0.4f, 0.4f, 1.0f}));

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 20.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        toolbar.children.push_back(std::move(sep));
    }

    toolbar.children.push_back(makeBtn("Sequencer.Play",
        m_state.playing ? "\xe2\x8f\xb8" : "\xe2\x96\xb6",
        m_state.playing ? "Pause" : "Play",
        theme.accent));
    toolbar.children.push_back(makeBtn("Sequencer.Stop", "\xe2\x96\xa0", "Stop", theme.textPrimary));
    toolbar.children.push_back(makeBtn("Sequencer.Loop",
        m_state.loopPlayback ? "\xe2\x86\xbb" : "\xe2\x86\xbb",
        m_state.loopPlayback ? "Loop: ON" : "Loop: OFF",
        m_state.loopPlayback ? theme.accent : Vec4{0.45f, 0.45f, 0.45f, 1.0f}));

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 20.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        toolbar.children.push_back(std::move(sep));
    }

    toolbar.children.push_back(makeBtn("Sequencer.ShowSpline",
        "\xe2\x97\x86",
        m_state.showSplineInViewport ? "Spline Visible" : "Spline Hidden",
        m_state.showSplineInViewport ? theme.accent : Vec4{0.45f, 0.45f, 0.45f, 1.0f}));

    // Duration label + value
    {
        WidgetElement durLbl{};
        durLbl.type             = WidgetElementType::Label;
        durLbl.text             = "Duration:";
        durLbl.style.textColor  = theme.textSecondary;
        durLbl.fontSize         = EditorTheme::Scaled(12.0f);
        durLbl.runtimeOnly      = true;
        toolbar.children.push_back(std::move(durLbl));
    }
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(m_state.pathDuration));
        WidgetElement durVal{};
        durVal.id              = "Sequencer.Duration";
        durVal.type            = WidgetElementType::Label;
        durVal.text            = buf;
        durVal.style.textColor = theme.textPrimary;
        durVal.fontSize        = EditorTheme::Scaled(12.0f);
        durVal.runtimeOnly     = true;
        toolbar.children.push_back(std::move(durVal));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// buildTimeline
// ───────────────────────────────────────────────────────────────────────────
void SequencerTab::buildTimeline(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement timelineArea{};
    timelineArea.id          = "Sequencer.TimelineArea";
    timelineArea.type        = WidgetElementType::Panel;
    timelineArea.fillX       = true;
    timelineArea.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 60.0f });
    timelineArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 6.0f });
    timelineArea.style.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
    timelineArea.runtimeOnly = true;

    // Track label
    {
        WidgetElement lbl{};
        lbl.type            = WidgetElementType::Label;
        lbl.text            = "Camera Path";
        lbl.style.textColor = theme.textSecondary;
        lbl.fontSize        = EditorTheme::Scaled(11.0f);
        lbl.from            = Vec2{ 0.0f, 0.0f };
        lbl.to              = Vec2{ 0.0f, 0.0f };
        lbl.runtimeOnly     = true;
        timelineArea.children.push_back(std::move(lbl));
    }

    // Timeline bar
    {
        WidgetElement bar{};
        bar.id          = "Sequencer.TimelineBar";
        bar.type        = WidgetElementType::Panel;
        bar.fillX       = true;
        bar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        bar.style.color = Vec4{ 0.15f, 0.16f, 0.19f, 1.0f };
        bar.runtimeOnly = true;
        bar.style.borderRadius = EditorTheme::Scaled(3.0f);

        // Draw keyframe markers on the bar
        if (m_renderer)
        {
            auto pts = m_renderer->getCameraPathPoints();
            const int n = static_cast<int>(pts.size());
            for (int i = 0; i < n; ++i)
            {
                float tNorm = (n <= 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
                WidgetElement marker{};
                marker.id   = "Sequencer.KF." + std::to_string(i);
                marker.type = WidgetElementType::Panel;
                marker.minSize = EditorTheme::Scaled(Vec2{ 8.0f, 18.0f });
                marker.from = Vec2{ tNorm, 0.1f };
                marker.to   = Vec2{ tNorm, 0.9f };
                bool selected = (i == m_state.selectedKeyframe);
                marker.style.color = selected
                    ? theme.accent
                    : Vec4{ 0.7f, 0.7f, 0.7f, 1.0f };
                marker.style.borderRadius = EditorTheme::Scaled(2.0f);
                marker.runtimeOnly = true;
                marker.clickEvent  = "Sequencer.SelectKF." + std::to_string(i);
                bar.children.push_back(std::move(marker));
            }

            // Scrubber position indicator
            if (m_state.playing || m_renderer->isCameraPathPlaying())
            {
                float progress = m_renderer->getCameraPathProgress();
                WidgetElement scrubber{};
                scrubber.id   = "Sequencer.Scrubber";
                scrubber.type = WidgetElementType::Panel;
                scrubber.minSize = EditorTheme::Scaled(Vec2{ 3.0f, 24.0f });
                scrubber.from = Vec2{ progress, 0.0f };
                scrubber.to   = Vec2{ progress, 1.0f };
                scrubber.style.color = Vec4{ 1.0f, 0.3f, 0.3f, 1.0f };
                scrubber.runtimeOnly = true;
                bar.children.push_back(std::move(scrubber));
            }
        }

        timelineArea.children.push_back(std::move(bar));
    }

    root.children.push_back(std::move(timelineArea));
}

// ───────────────────────────────────────────────────────────────────────────
// buildKeyframeList
// ───────────────────────────────────────────────────────────────────────────
void SequencerTab::buildKeyframeList(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement listArea{};
    listArea.id          = "Sequencer.KeyframeList";
    listArea.type        = WidgetElementType::StackPanel;
    listArea.orientation = StackOrientation::Vertical;
    listArea.fillX       = true;
    listArea.fillY       = true;
    listArea.scrollable  = true;
    listArea.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
    listArea.spacing     = EditorTheme::Scaled(2.0f);
    listArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
    listArea.runtimeOnly = true;

    // Header
    {
        WidgetElement hdr{};
        hdr.type            = WidgetElementType::Label;
        hdr.text            = "Keyframes";
        hdr.style.textColor = theme.textSecondary;
        hdr.fontSize        = EditorTheme::Scaled(12.0f);
        hdr.runtimeOnly     = true;
        listArea.children.push_back(std::move(hdr));
    }

    if (m_renderer)
    {
        auto pts = m_renderer->getCameraPathPoints();
        for (int i = 0; i < static_cast<int>(pts.size()); ++i)
        {
            const auto& pt = pts[i];
            bool selected = (i == m_state.selectedKeyframe);

            WidgetElement row{};
            row.id          = "Sequencer.Row." + std::to_string(i);
            row.type        = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX       = true;
            row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
            row.spacing     = EditorTheme::Scaled(8.0f);
            row.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            row.style.color = selected
                ? Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.15f }
                : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.clickEvent  = "Sequencer.SelectKF." + std::to_string(i);
            row.runtimeOnly = true;

            // Index
            {
                WidgetElement idx{};
                idx.type            = WidgetElementType::Label;
                idx.text            = std::to_string(i + 1) + ".";
                idx.style.textColor = selected ? theme.accent : theme.textSecondary;
                idx.fontSize        = EditorTheme::Scaled(12.0f);
                idx.minSize         = EditorTheme::Scaled(Vec2{ 24.0f, 0.0f });
                idx.runtimeOnly     = true;
                row.children.push_back(std::move(idx));
            }

            // Position
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Pos (%.1f, %.1f, %.1f)",
                    static_cast<double>(pt.position.x),
                    static_cast<double>(pt.position.y),
                    static_cast<double>(pt.position.z));
                WidgetElement posLbl{};
                posLbl.type            = WidgetElementType::Label;
                posLbl.text            = buf;
                posLbl.style.textColor = theme.textPrimary;
                posLbl.fontSize        = EditorTheme::Scaled(11.0f);
                posLbl.runtimeOnly     = true;
                row.children.push_back(std::move(posLbl));
            }

            // Rotation
            {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Yaw %.0f  Pitch %.0f",
                    static_cast<double>(pt.yaw), static_cast<double>(pt.pitch));
                WidgetElement rotLbl{};
                rotLbl.type            = WidgetElementType::Label;
                rotLbl.text            = buf;
                rotLbl.style.textColor = theme.textSecondary;
                rotLbl.fontSize        = EditorTheme::Scaled(11.0f);
                rotLbl.runtimeOnly     = true;
                row.children.push_back(std::move(rotLbl));
            }

            listArea.children.push_back(std::move(row));
        }

        if (pts.empty())
        {
            WidgetElement hint{};
            hint.type            = WidgetElementType::Label;
            hint.text            = "No keyframes. Click + to add one at the current camera position.";
            hint.style.textColor = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
            hint.fontSize        = EditorTheme::Scaled(11.0f);
            hint.runtimeOnly     = true;
            listArea.children.push_back(std::move(hint));
        }
    }

    root.children.push_back(std::move(listArea));
}

// ───────────────────────────────────────────────────────────────────────────
// refresh – rebuilds toolbar + timeline + keyframe list
// ───────────────────────────────────────────────────────────────────────────
void SequencerTab::refresh()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    auto* entry = m_uiManager->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget) return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty()) return;

    auto& rootEl = elements[0];

    // Re-register dynamic keyframe click events
    if (m_renderer)
    {
        auto pts = m_renderer->getCameraPathPoints();
        for (int i = 0; i < static_cast<int>(pts.size()); ++i)
        {
            std::string evtId = "Sequencer.SelectKF." + std::to_string(i);
            m_uiManager->registerClickEvent(evtId, [this, i]()
            {
                m_state.selectedKeyframe = i;
                // Move camera to selected keyframe position
                if (m_renderer)
                {
                    auto pts2 = m_renderer->getCameraPathPoints();
                    if (i >= 0 && i < static_cast<int>(pts2.size()))
                    {
                        const auto& kf = pts2[i];
                        m_renderer->startCameraTransition(kf.position, kf.yaw, kf.pitch, 0.3f);
                    }
                }
                refresh();
            });
        }
    }

    // Update playback state
    if (m_state.playing && m_renderer)
    {
        if (!m_renderer->isCameraPathPlaying())
        {
            m_state.playing = false;
            m_state.scrubberT = 0.0f;
        }
        else
        {
            m_state.scrubberT = m_renderer->getCameraPathProgress();
        }
    }

    // Rebuild toolbar + timeline + keyframe list
    rootEl.children.clear();
    buildToolbar(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildTimeline(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildKeyframeList(rootEl);

    m_uiManager->markAllWidgetsDirty();
}
