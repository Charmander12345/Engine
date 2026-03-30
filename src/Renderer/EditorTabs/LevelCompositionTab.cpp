#include "LevelCompositionTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"

#include <string>
#include <cstdio>

LevelCompositionTab::LevelCompositionTab(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer) {}

void LevelCompositionTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "LevelComposition";

    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Level Composition", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "LevelComposition.Main";
    m_uiManager->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId    = tabId;
    m_state.widgetId = widgetId;
    m_state.isOpen   = true;

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
        root.id          = "LC.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        buildToolbar(root);

        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        buildSubLevelList(root);

        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        buildVolumeList(root);

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
    m_uiManager->registerClickEvent("LC.AddSubLevel", [this]()
    {
        if (!m_renderer) return;
        const auto& subs = m_renderer->getSubLevels();
        std::string name = "SubLevel_" + std::to_string(subs.size());
        m_renderer->addSubLevel(name, "");
        refresh();
        m_uiManager->showToastMessage("Sub-Level '" + name + "' added", UIManager::kToastShort);
    });

    m_uiManager->registerClickEvent("LC.RemoveSubLevel", [this]()
    {
        if (!m_renderer) return;
        int sel = m_state.selectedSubLevel;
        if (sel < 0) return;
        m_renderer->removeSubLevel(sel);
        m_state.selectedSubLevel = -1;
        refresh();
        m_uiManager->showToastMessage("Sub-Level removed", UIManager::kToastShort);
    });

    m_uiManager->registerClickEvent("LC.AddVolume", [this]()
    {
        if (!m_renderer) return;
        int sel = m_state.selectedSubLevel;
        if (sel < 0)
        {
            m_uiManager->showToastMessage("Select a Sub-Level first", UIManager::kToastShort);
            return;
        }
        m_renderer->addStreamingVolume(Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 10.0f, 10.0f, 10.0f }, sel);
        refresh();
        m_uiManager->showToastMessage("Streaming Volume added", UIManager::kToastShort);
    });

    m_uiManager->registerClickEvent("LC.ToggleVolumesVisible", [this]()
    {
        if (!m_renderer) return;
        m_renderer->m_streamingVolumesVisible = !m_renderer->m_streamingVolumesVisible;
        refresh();
    });

    refresh();
}

void LevelCompositionTab::close()
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

void LevelCompositionTab::refresh()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    UIManager::WidgetEntry* entry = nullptr;
    for (auto& w : const_cast<std::vector<UIManager::WidgetEntry>&>(m_uiManager->getRegisteredWidgets()))
    {
        if (w.id == m_state.widgetId)
        {
            entry = &w;
            break;
        }
    }
    if (!entry || !entry->widget) return;

    auto* editorWidget = dynamic_cast<EditorWidget*>(entry->widget.get());
    if (!editorWidget) return;

    auto elems = editorWidget->getElements();
    if (elems.empty()) return;

    WidgetElement& rootEl = elems[0];
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
    buildSubLevelList(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildVolumeList(rootEl);

    m_uiManager->markAllWidgetsDirty();

    // Dynamic click events for sub-levels
    const auto& subLevels = m_renderer->getSubLevels();
    for (int i = 0; i < static_cast<int>(subLevels.size()); ++i)
    {
        const int idx = i;
        const bool loaded = subLevels[i].loaded;
        const bool visible = subLevels[i].visible;

        m_uiManager->registerClickEvent("LC.ToggleLoaded." + std::to_string(i), [this, idx, loaded]()
        {
            if (!m_renderer) return;
            m_renderer->setSubLevelLoaded(idx, !loaded);
            refresh();
        });

        m_uiManager->registerClickEvent("LC.ToggleVisible." + std::to_string(i), [this, idx, visible]()
        {
            if (!m_renderer) return;
            m_renderer->setSubLevelVisible(idx, !visible);
            refresh();
        });

        m_uiManager->registerClickEvent("LC.SubLevel." + std::to_string(i), [this, idx]()
        {
            m_state.selectedSubLevel = idx;
            refresh();
        });
    }

    // Dynamic click events for streaming volumes
    const auto& volumes = m_renderer->getStreamingVolumes();
    for (int i = 0; i < static_cast<int>(volumes.size()); ++i)
    {
        const int idx = i;
        m_uiManager->registerClickEvent("LC.RemoveVolume." + std::to_string(i), [this, idx]()
        {
            if (!m_renderer) return;
            m_renderer->removeStreamingVolume(idx);
            refresh();
            m_uiManager->showToastMessage("Streaming Volume removed", UIManager::kToastShort);
        });
    }
}

void LevelCompositionTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.fillX       = true;
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
    toolbar.spacing     = EditorTheme::Scaled(4.0f);
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Add Sub-Level button
    {
        WidgetElement btn{};
        btn.id          = "LC.AddSubLevel";
        btn.type        = WidgetElementType::Button;
        btn.text        = "+ Sub-Level";
        btn.style.color = theme.accentGreen;
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    // Remove Sub-Level button
    {
        WidgetElement btn{};
        btn.id          = "LC.RemoveSubLevel";
        btn.type        = WidgetElementType::Button;
        btn.text        = "- Remove";
        btn.style.color = Vec4{ 0.6f, 0.2f, 0.2f, 1.0f };
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    // Add Streaming Volume button
    {
        WidgetElement btn{};
        btn.id          = "LC.AddVolume";
        btn.type        = WidgetElementType::Button;
        btn.text        = "+ Volume";
        btn.style.color = theme.accentGreen;
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    // Toggle volume visibility
    {
        bool vis = m_renderer ? m_renderer->m_streamingVolumesVisible : true;
        WidgetElement btn{};
        btn.id          = "LC.ToggleVolumesVisible";
        btn.type        = WidgetElementType::Button;
        btn.text        = vis ? "Volumes: ON" : "Volumes: OFF";
        btn.style.color = vis ? theme.accent : Vec4{ 0.4f, 0.4f, 0.4f, 1.0f };
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    root.children.push_back(std::move(toolbar));
}

void LevelCompositionTab::buildSubLevelList(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement header{};
    header.type        = WidgetElementType::Text;
    header.text        = "Sub-Levels";
    header.style.color = theme.textSecondary;
    header.fontSize    = EditorTheme::Scaled(12.0f);
    header.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
    header.runtimeOnly = true;
    root.children.push_back(std::move(header));

    if (!m_renderer) return;

    const auto& subLevels = m_renderer->getSubLevels();
    if (subLevels.empty())
    {
        WidgetElement empty{};
        empty.type        = WidgetElementType::Text;
        empty.text        = "No sub-levels. Click '+ Sub-Level' to add one.";
        empty.style.color = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
        empty.fontSize    = EditorTheme::Scaled(11.0f);
        empty.padding     = EditorTheme::Scaled(Vec2{ 12.0f, 4.0f });
        empty.runtimeOnly = true;
        root.children.push_back(std::move(empty));
        return;
    }

    for (int i = 0; i < static_cast<int>(subLevels.size()); ++i)
    {
        const auto& sub = subLevels[i];
        const bool selected = (i == m_state.selectedSubLevel);

        WidgetElement row{};
        row.id          = "LC.SubLevel." + std::to_string(i);
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        row.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        row.spacing     = EditorTheme::Scaled(4.0f);
        row.style.color = selected ? theme.selectionHighlight : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.runtimeOnly = true;

        // Name label
        {
            WidgetElement lbl{};
            lbl.type        = WidgetElementType::Text;
            lbl.text        = sub.name;
            lbl.style.color = theme.textPrimary;
            lbl.fontSize    = EditorTheme::Scaled(12.0f);
            lbl.fillX       = true;
            lbl.runtimeOnly = true;
            row.children.push_back(std::move(lbl));
        }

        // Loaded label
        {
            WidgetElement loadedLbl{};
            loadedLbl.type        = WidgetElementType::Text;
            loadedLbl.text        = sub.loaded ? "[Loaded]" : "[Unloaded]";
            loadedLbl.style.color = sub.loaded
                ? Vec4{ 0.2f, 0.9f, 0.3f, 1.0f }
                : Vec4{ 0.6f, 0.6f, 0.6f, 1.0f };
            loadedLbl.fontSize    = EditorTheme::Scaled(11.0f);
            loadedLbl.runtimeOnly = true;
            row.children.push_back(std::move(loadedLbl));
        }

        // Loaded toggle button
        {
            WidgetElement toggleBtn{};
            toggleBtn.id          = "LC.ToggleLoaded." + std::to_string(i);
            toggleBtn.type        = WidgetElementType::Button;
            toggleBtn.text        = sub.loaded ? "Unload" : "Load";
            toggleBtn.style.color = theme.inputBackground;
            toggleBtn.style.borderRadius = EditorTheme::Scaled(3.0f);
            toggleBtn.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            toggleBtn.fontSize    = EditorTheme::Scaled(10.0f);
            toggleBtn.runtimeOnly = true;
            row.children.push_back(std::move(toggleBtn));
        }

        // Visible toggle button
        {
            WidgetElement visBtn{};
            visBtn.id          = "LC.ToggleVisible." + std::to_string(i);
            visBtn.type        = WidgetElementType::Button;
            visBtn.text        = sub.visible ? "Vis" : "Hid";
            visBtn.style.color = sub.visible ? theme.accent : Vec4{ 0.4f, 0.4f, 0.4f, 1.0f };
            visBtn.style.borderRadius = EditorTheme::Scaled(3.0f);
            visBtn.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            visBtn.fontSize    = EditorTheme::Scaled(10.0f);
            visBtn.runtimeOnly = true;
            row.children.push_back(std::move(visBtn));
        }

        root.children.push_back(std::move(row));
    }
}

void LevelCompositionTab::buildVolumeList(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement header{};
    header.type        = WidgetElementType::Text;
    header.text        = "Streaming Volumes";
    header.style.color = theme.textSecondary;
    header.fontSize    = EditorTheme::Scaled(12.0f);
    header.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
    header.runtimeOnly = true;
    root.children.push_back(std::move(header));

    if (!m_renderer) return;

    const auto& volumes = m_renderer->getStreamingVolumes();
    if (volumes.empty())
    {
        WidgetElement empty{};
        empty.type        = WidgetElementType::Text;
        empty.text        = "No volumes. Add one via '+ Volume'.";
        empty.style.color = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
        empty.fontSize    = EditorTheme::Scaled(11.0f);
        empty.padding     = EditorTheme::Scaled(Vec2{ 12.0f, 4.0f });
        empty.runtimeOnly = true;
        root.children.push_back(std::move(empty));
        return;
    }

    for (int i = 0; i < static_cast<int>(volumes.size()); ++i)
    {
        const auto& vol = volumes[i];

        WidgetElement row{};
        row.id          = "LC.Volume." + std::to_string(i);
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        row.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        row.spacing     = EditorTheme::Scaled(4.0f);
        row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.runtimeOnly = true;

        // Sub-level index
        {
            WidgetElement lbl{};
            lbl.type        = WidgetElementType::Text;
            lbl.text        = "SubLvl " + std::to_string(vol.subLevelIndex);
            lbl.style.color = theme.accent;
            lbl.fontSize    = EditorTheme::Scaled(11.0f);
            lbl.runtimeOnly = true;
            row.children.push_back(std::move(lbl));
        }

        // Position + size
        {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Pos(%.0f,%.0f,%.0f) Size(%.0f,%.0f,%.0f)",
                vol.center.x, vol.center.y, vol.center.z,
                vol.halfExtents.x * 2.0f, vol.halfExtents.y * 2.0f, vol.halfExtents.z * 2.0f);

            WidgetElement lbl{};
            lbl.type        = WidgetElementType::Text;
            lbl.text        = buf;
            lbl.style.color = theme.textPrimary;
            lbl.fontSize    = EditorTheme::Scaled(11.0f);
            lbl.fillX       = true;
            lbl.runtimeOnly = true;
            row.children.push_back(std::move(lbl));
        }

        // Remove button
        {
            WidgetElement removeBtn{};
            removeBtn.id          = "LC.RemoveVolume." + std::to_string(i);
            removeBtn.type        = WidgetElementType::Button;
            removeBtn.text        = "X";
            removeBtn.style.color = Vec4{ 0.6f, 0.2f, 0.2f, 1.0f };
            removeBtn.style.borderRadius = EditorTheme::Scaled(3.0f);
            removeBtn.padding     = EditorTheme::Scaled(Vec2{ 5.0f, 1.0f });
            removeBtn.fontSize    = EditorTheme::Scaled(10.0f);
            removeBtn.runtimeOnly = true;
            row.children.push_back(std::move(removeBtn));
        }

        root.children.push_back(std::move(row));
    }
}
