#include "AnimationEditorTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Core/ECS/ECS.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Logger/Logger.h"

#include <string>
#include <cstdio>
#include <vector>

AnimationEditorTab::AnimationEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer) {}

void AnimationEditorTab::open()
{
    open(ECS::Entity{ 0 });
}

void AnimationEditorTab::open(ECS::Entity entity)
{
    if (!m_renderer)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    if (!ecs.hasComponent<ECS::AnimationComponent>(entity))
        return;

    if (!m_renderer->isEntitySkinned(entity))
        return;

    const std::string tabId = "AnimationEditor";

    // If already open for this entity, just switch to it
    if (m_state.isOpen && m_state.linkedEntity == entity)
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    // If open for a different entity, close first
    if (m_state.isOpen)
        close();

    m_renderer->addTab(tabId, "Animation Editor", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "AnimationEditor.Main";
    m_uiManager->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId        = tabId;
    m_state.widgetId     = widgetId;
    m_state.linkedEntity = entity;
    m_state.isOpen       = true;
    m_state.selectedClip = m_renderer->getEntityAnimatorCurrentClip(entity);

    // Build the main widget
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
        root.id          = "AnimationEditor.Root";
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

        // Scrollable content area
        {
            WidgetElement contentArea{};
            contentArea.id          = "AnimationEditor.ContentArea";
            contentArea.type        = WidgetElementType::StackPanel;
            contentArea.fillX       = true;
            contentArea.fillY       = true;
            contentArea.scrollable  = true;
            contentArea.orientation = StackOrientation::Vertical;
            contentArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            contentArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            contentArea.runtimeOnly = true;
            root.children.push_back(std::move(contentArea));
        }

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

    // Stop button
    m_uiManager->registerClickEvent("AnimationEditor.Stop", [this]()
    {
        if (m_renderer)
        {
            m_renderer->stopEntityAnimation(m_state.linkedEntity);
            refresh();
        }
    });

    // Initial population
    refresh();
}

void AnimationEditorTab::close()
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

void AnimationEditorTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "AnimationEditor.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
    toolbar.spacing     = EditorTheme::Scaled(4.0f);
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title
    {
        auto& ecs = ECS::ECSManager::Instance();
        std::string titleText = "Animation Editor";
        auto* nameComp = ecs.getComponent<ECS::NameComponent>(m_state.linkedEntity);
        if (nameComp && !nameComp->displayName.empty())
            titleText += " - " + nameComp->displayName;

        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = titleText;
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, 24.0f });
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

    // Stop button
    {
        WidgetElement stopBtn = EditorUIBuilder::makeButton(
            "AnimationEditor.Stop", "Stop", {}, EditorTheme::Scaled(Vec2{ 60.0f, 24.0f }));
        stopBtn.tooltipText = "Stop playback";
        toolbar.children.push_back(std::move(stopBtn));
    }

    root.children.push_back(std::move(toolbar));
}

void AnimationEditorTab::refresh()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_uiManager->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find ContentArea inside root
    WidgetElement* contentArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "AnimationEditor.ContentArea")
        {
            contentArea = &child;
            break;
        }
    }
    if (!contentArea)
        return;

    contentArea->children.clear();

    auto& ecs = ECS::ECSManager::Instance();
    const ECS::Entity entity = m_state.linkedEntity;
    const auto* animComp = ecs.getComponent<ECS::AnimationComponent>(entity);
    if (!animComp || !m_renderer)
    {
        contentArea->children.push_back(EditorUIBuilder::makeLabel("Entity no longer has an Animation component."));
        entry->widget->markLayoutDirty();
        m_uiManager->markRenderDirty();
        return;
    }

    // Build sections
    buildClipList(*contentArea);
    buildControls(*contentArea);
    buildBoneTree(*contentArea);

    m_uiManager->markAllWidgetsDirty();
}

void AnimationEditorTab::buildClipList(WidgetElement& root)
{
    if (!m_renderer) return;
    const ECS::Entity entity = m_state.linkedEntity;
    const int clipCount = m_renderer->getEntityAnimationClipCount(entity);

    // Heading
    WidgetElement heading = EditorUIBuilder::makeHeading("Animation Clips (" + std::to_string(clipCount) + ")");
    heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
    root.children.push_back(std::move(heading));

    if (clipCount == 0)
    {
        root.children.push_back(EditorUIBuilder::makeLabel("No clips found."));
        return;
    }

    const auto& theme = EditorTheme::Get();
    for (int i = 0; i < clipCount; ++i)
    {
        auto clipInfo = m_renderer->getEntityAnimationClipInfo(entity, i);
        std::string label = clipInfo.name.empty() ? ("Clip " + std::to_string(i)) : clipInfo.name;
        float tps = clipInfo.ticksPerSecond > 0.0f ? clipInfo.ticksPerSecond : 25.0f;
        float durationSec = clipInfo.duration / tps;
        label += "  (" + std::to_string(static_cast<int>(durationSec * 100.0f) / 100.0f).substr(0, 5) + "s)";

        Vec4 btnColor = (i == m_state.selectedClip)
            ? theme.accent
            : theme.inputBackground;

        WidgetElement btn = EditorUIBuilder::makeButton(
            "AnimationEditor.Clip." + std::to_string(i), label,
            [this, i, entity]()
            {
                if (m_renderer)
                {
                    auto& ecs2 = ECS::ECSManager::Instance();
                    auto* comp = ecs2.getComponent<ECS::AnimationComponent>(entity);
                    bool loop = comp ? comp->loop : true;
                    m_renderer->playEntityAnimation(entity, i, loop);
                    if (comp)
                    {
                        comp->currentClipIndex = i;
                        comp->playing = true;
                    }
                    m_state.selectedClip = i;
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                    refresh();
                }
            },
            EditorTheme::Scaled(Vec2{ 0.0f, 24.0f }));
        btn.fillX = true;
        btn.style.color = btnColor;
        btn.runtimeOnly = true;
        root.children.push_back(std::move(btn));
    }
}

void AnimationEditorTab::buildControls(WidgetElement& root)
{
    if (!m_renderer) return;
    const ECS::Entity entity = m_state.linkedEntity;
    auto& ecs = ECS::ECSManager::Instance();
    const auto* animComp = ecs.getComponent<ECS::AnimationComponent>(entity);
    if (!animComp) return;

    // Heading
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        root.children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading("Playback Controls");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    // Status info
    {
        bool playing = m_renderer->isEntityAnimatorPlaying(entity);
        float currentTime = m_renderer->getEntityAnimatorCurrentTime(entity);
        int currentClip = m_renderer->getEntityAnimatorCurrentClip(entity);
        std::string status = playing ? "Playing" : "Stopped";
        if (currentClip >= 0)
        {
            auto clipInfo = m_renderer->getEntityAnimationClipInfo(entity, currentClip);
            float tps = clipInfo.ticksPerSecond > 0.0f ? clipInfo.ticksPerSecond : 25.0f;
            status += "  |  Clip: " + (clipInfo.name.empty() ? std::to_string(currentClip) : clipInfo.name);
            status += "  |  Time: " + std::to_string(static_cast<int>(currentTime / tps * 100.0f) / 100.0f).substr(0, 5) + "s";
        }
        root.children.push_back(EditorUIBuilder::makeLabel(status));
    }

    // Speed slider
    {
        WidgetElement speedRow = EditorUIBuilder::makeSliderRow(
            "AnimationEditor.Speed", "Speed", animComp->speed, 0.0f, 5.0f,
            [this, entity](float v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::AnimationComponent>(entity);
                if (comp)
                    comp->speed = v;
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                    level->setIsSaved(false);
            });
        root.children.push_back(std::move(speedRow));
    }

    // Loop checkbox
    {
        WidgetElement loopRow = EditorUIBuilder::makeCheckBox(
            "AnimationEditor.Loop", "Loop", animComp->loop,
            [this, entity](bool v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::AnimationComponent>(entity);
                if (comp)
                    comp->loop = v;
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                    level->setIsSaved(false);
            });
        root.children.push_back(std::move(loopRow));
    }
}

void AnimationEditorTab::buildBoneTree(WidgetElement& root)
{
    if (!m_renderer) return;
    const ECS::Entity entity = m_state.linkedEntity;
    const int boneCount = m_renderer->getEntityBoneCount(entity);

    // Heading
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        root.children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading("Bone Hierarchy (" + std::to_string(boneCount) + " bones)");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    if (boneCount == 0)
    {
        root.children.push_back(EditorUIBuilder::makeLabel("No bones found."));
        return;
    }

    // Build indented bone list
    // First pass: determine depth for each bone
    std::vector<int> depth(boneCount, 0);
    for (int i = 0; i < boneCount; ++i)
    {
        int parent = m_renderer->getEntityBoneParent(entity, i);
        int d = 0;
        int curr = parent;
        while (curr >= 0 && d < 20)
        {
            ++d;
            curr = m_renderer->getEntityBoneParent(entity, curr);
        }
        depth[i] = d;
    }

    const auto& theme = EditorTheme::Get();
    for (int i = 0; i < boneCount; ++i)
    {
        std::string boneName = m_renderer->getEntityBoneName(entity, i);
        if (boneName.empty())
            boneName = "Bone " + std::to_string(i);

        std::string indent;
        for (int d = 0; d < depth[i]; ++d)
            indent += "  ";

        std::string prefix = (depth[i] > 0) ? "|- " : "";

        WidgetElement lbl{};
        lbl.type            = WidgetElementType::Text;
        lbl.text            = indent + prefix + boneName;
        lbl.font            = theme.fontDefault;
        lbl.fontSize        = theme.fontSizeSmall;
        lbl.style.textColor = theme.textSecondary;
        lbl.fillX           = true;
        lbl.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        lbl.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 1.0f });
        lbl.runtimeOnly     = true;
        root.children.push_back(std::move(lbl));
    }
}
