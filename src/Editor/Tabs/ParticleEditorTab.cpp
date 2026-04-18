#include "ParticleEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Core/ECS/ECS.h"
#include "../../Core/UndoRedoManager.h"
#include "../../Logger/Logger.h"

#include <string>

// ───────────────────────────────────────────────────────────────────────────
ParticleEditorTab::ParticleEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ───────────────────────────────────────────────────────────────────────────
void ParticleEditorTab::open()
{
    // Default open with no entity – no-op (use open(entity) instead)
}

// ───────────────────────────────────────────────────────────────────────────
void ParticleEditorTab::open(ECS::Entity entity)
{
    if (!m_renderer)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    if (!ecs.hasComponent<ECS::ParticleEmitterComponent>(entity))
        return;

    const std::string tabId = "ParticleEditor";

    // If already open for this entity, just switch to it
    if (m_state.isOpen && m_state.linkedEntity == entity)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    // If open for a different entity, close first
    if (m_state.isOpen)
        close();

    m_renderer->addTab(tabId, "Particle Editor", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "ParticleEditor.Main";
    m_ui->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId        = tabId;
    m_state.widgetId     = widgetId;
    m_state.linkedEntity = entity;
    m_state.isOpen       = true;
    m_state.presetIndex  = -1;

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
        root.id          = "ParticleEditor.Root";
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

        // Scrollable parameter area
        {
            WidgetElement paramsArea{};
            paramsArea.id          = "ParticleEditor.ParamsArea";
            paramsArea.type        = WidgetElementType::StackPanel;
            paramsArea.fillX       = true;
            paramsArea.fillY       = true;
            paramsArea.scrollable  = true;
            paramsArea.orientation = StackOrientation::Vertical;
            paramsArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            paramsArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            paramsArea.runtimeOnly = true;
            root.children.push_back(std::move(paramsArea));
        }

        widget->setElements({ std::move(root) });
        m_ui->registerWidget(widgetId, widget, tabId);
    }

    // Tab / close click events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
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

    // Reset button – restore default ParticleEmitterComponent values
    m_ui->registerClickEvent("ParticleEditor.Reset", [this]()
    {
        auto& ecs2 = ECS::ECSManager::Instance();
        auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(m_state.linkedEntity);
        if (!comp)
            return;
        const ECS::Entity ent = m_state.linkedEntity;
        const ECS::ParticleEmitterComponent saved = *comp;
        *comp = ECS::ParticleEmitterComponent{}; // reset to defaults
        m_state.presetIndex = -1;
        if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
            level->setIsSaved(false);
        UndoRedoManager::Instance().pushCommand({
            "Reset Particle Emitter",
            [ent]() {
                auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(ent);
                if (c) *c = ECS::ParticleEmitterComponent{};
            },
            [ent, saved]() {
                auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(ent);
                if (c) *c = saved;
            }
        });
        refresh();
        m_ui->showToastMessage("Particle emitter reset to defaults", UIManager::kToastShort);
    });

    // Initial population
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void ParticleEditorTab::close()
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
void ParticleEditorTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "ParticleEditor.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title
    {
        std::string titleText = "Particle Editor";
        auto& ecs = ECS::ECSManager::Instance();
        if (const auto* name = ecs.getComponent<ECS::NameComponent>(m_state.linkedEntity))
        {
            if (!name->displayName.empty())
                titleText += "  -  " + name->displayName;
        }
        titleText += " (Entity " + std::to_string(m_state.linkedEntity) + ")";

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

    // Preset dropdown
    {
        static const std::vector<std::string> presetNames = {
            "Custom", "Fire", "Smoke", "Sparks", "Rain", "Snow", "Magic"
        };
        int idx = m_state.presetIndex < 0 ? 0 : (m_state.presetIndex + 1);
        WidgetElement dropdown = EditorUIBuilder::makeDropDown(
            "ParticleEditor.Preset", presetNames, idx,
            [this](int sel)
            {
                if (sel <= 0)
                    return; // "Custom" selected – do nothing
                applyPreset(sel - 1); // 0=Fire, 1=Smoke, ...
            });
        dropdown.minSize = EditorTheme::Scaled(Vec2{ 100.0f, 24.0f });
        dropdown.tooltipText = "Apply Preset";
        toolbar.children.push_back(std::move(dropdown));
    }

    // Reset button
    {
        WidgetElement resetBtn = EditorUIBuilder::makeButton(
            "ParticleEditor.Reset", "Reset", {}, EditorTheme::Scaled(Vec2{ 60.0f, 24.0f }));
        resetBtn.tooltipText = "Reset to defaults";
        toolbar.children.push_back(std::move(resetBtn));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// refresh – rebuilds the parameter area from the linked entity
// ───────────────────────────────────────────────────────────────────────────
void ParticleEditorTab::refresh()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find ParamsArea inside root
    WidgetElement* paramsArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "ParticleEditor.ParamsArea")
        {
            paramsArea = &child;
            break;
        }
    }
    if (!paramsArea)
        return;

    paramsArea->children.clear();

    auto& ecs = ECS::ECSManager::Instance();
    const auto* emitter = ecs.getComponent<ECS::ParticleEmitterComponent>(m_state.linkedEntity);
    if (!emitter)
    {
        paramsArea->children.push_back(EditorUIBuilder::makeLabel("Entity no longer has a Particle Emitter component."));
        entry->widget->markLayoutDirty();
        m_ui->markRenderDirty();
        return;
    }

    const ECS::Entity entity = m_state.linkedEntity;

    // Helper: slider row that writes back to the ECS component with undo
    auto addSlider = [&](const std::string& id, const std::string& label,
        float value, float minVal, float maxVal,
        std::function<void(ECS::ParticleEmitterComponent&, float)> setter)
    {
        WidgetElement row = EditorUIBuilder::makeSliderRow(
            "ParticleEditor." + id, label, value, minVal, maxVal,
            [this, entity, setter = std::move(setter)](float v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(entity);
                if (comp)
                {
                    setter(*comp, v);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                }
                m_state.presetIndex = -1;
            });
        paramsArea->children.push_back(std::move(row));
    };

    auto addCheckBox = [&](const std::string& id, const std::string& label, bool value,
        std::function<void(ECS::ParticleEmitterComponent&, bool)> setter)
    {
        WidgetElement row = EditorUIBuilder::makeCheckBox(
            "ParticleEditor." + id, label, value,
            [this, entity, setter = std::move(setter)](bool v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(entity);
                if (comp)
                {
                    setter(*comp, v);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                }
            });
        paramsArea->children.push_back(std::move(row));
    };

    auto addIntSlider = [&](const std::string& id, const std::string& label,
        int value, int minVal, int maxVal,
        std::function<void(ECS::ParticleEmitterComponent&, int)> setter)
    {
        WidgetElement row = EditorUIBuilder::makeSliderRow(
            "ParticleEditor." + id, label,
            static_cast<float>(value), static_cast<float>(minVal), static_cast<float>(maxVal),
            [this, entity, setter = std::move(setter)](float v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(entity);
                if (comp)
                {
                    setter(*comp, static_cast<int>(v));
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                }
                m_state.presetIndex = -1;
            });
        paramsArea->children.push_back(std::move(row));
    };

    auto addHeading = [&](const std::string& text)
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        paramsArea->children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading(text);
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        paramsArea->children.push_back(std::move(heading));
    };

    // ── General ──────────────────────────────────────────────────────────
    addHeading("General");
    addCheckBox("Enabled", "Enabled", emitter->enabled,
        [](ECS::ParticleEmitterComponent& c, bool v) { c.enabled = v; });
    addCheckBox("Loop", "Loop", emitter->loop,
        [](ECS::ParticleEmitterComponent& c, bool v) { c.loop = v; });
    addIntSlider("MaxParticles", "Max Particles", emitter->maxParticles, 1, 10000,
        [](ECS::ParticleEmitterComponent& c, int v) { c.maxParticles = v; });

    // ── Emission ─────────────────────────────────────────────────────────
    addHeading("Emission");
    addSlider("EmissionRate", "Rate (p/s)", emitter->emissionRate, 0.1f, 500.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.emissionRate = v; });
    addSlider("Lifetime", "Lifetime (s)", emitter->lifetime, 0.1f, 30.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.lifetime = v; });
    addSlider("ConeAngle", "Cone Angle", emitter->coneAngle, 0.0f, 180.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.coneAngle = v; });

    // ── Motion ───────────────────────────────────────────────────────────
    addHeading("Motion");
    addSlider("Speed", "Speed (m/s)", emitter->speed, 0.0f, 50.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.speed = v; });
    addSlider("SpeedVariance", "Speed Variance", emitter->speedVariance, 0.0f, 20.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.speedVariance = v; });
    addSlider("Gravity", "Gravity", emitter->gravity, -30.0f, 30.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.gravity = v; });

    // ── Size ─────────────────────────────────────────────────────────────
    addHeading("Size");
    addSlider("Size", "Start Size", emitter->size, 0.01f, 5.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.size = v; });
    addSlider("SizeEnd", "End Size", emitter->sizeEnd, 0.0f, 5.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.sizeEnd = v; });

    // ── Start Color ──────────────────────────────────────────────────────
    addHeading("Start Color");
    addSlider("ColorR", "R", emitter->colorR, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorR = v; });
    addSlider("ColorG", "G", emitter->colorG, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorG = v; });
    addSlider("ColorB", "B", emitter->colorB, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorB = v; });
    addSlider("ColorA", "A", emitter->colorA, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorA = v; });

    // ── End Color ────────────────────────────────────────────────────────
    addHeading("End Color");
    addSlider("ColorEndR", "R", emitter->colorEndR, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndR = v; });
    addSlider("ColorEndG", "G", emitter->colorEndG, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndG = v; });
    addSlider("ColorEndB", "B", emitter->colorEndB, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndB = v; });
    addSlider("ColorEndA", "A", emitter->colorEndA, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndA = v; });

    m_ui->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
// applyPreset – applies a named preset to the linked entity
// ───────────────────────────────────────────────────────────────────────────
void ParticleEditorTab::applyPreset(int presetIndex)
{
    if (!m_state.isOpen)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    auto* comp = ecs.getComponent<ECS::ParticleEmitterComponent>(m_state.linkedEntity);
    if (!comp)
        return;

    // Save for undo
    const ECS::Entity entity = m_state.linkedEntity;
    const ECS::ParticleEmitterComponent saved = *comp;

    // Preset definitions: Fire(0), Smoke(1), Sparks(2), Rain(3), Snow(4), Magic(5)
    switch (presetIndex)
    {
    case 0: // Fire
        comp->maxParticles   = 200;
        comp->emissionRate   = 60.0f;
        comp->lifetime       = 1.5f;
        comp->speed          = 3.0f;
        comp->speedVariance  = 1.0f;
        comp->size           = 0.3f;
        comp->sizeEnd        = 0.0f;
        comp->gravity        = 1.5f;
        comp->coneAngle      = 15.0f;
        comp->colorR = 1.0f; comp->colorG = 0.6f; comp->colorB = 0.1f; comp->colorA = 1.0f;
        comp->colorEndR = 1.0f; comp->colorEndG = 0.1f; comp->colorEndB = 0.0f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 1: // Smoke
        comp->maxParticles   = 150;
        comp->emissionRate   = 30.0f;
        comp->lifetime       = 4.0f;
        comp->speed          = 1.5f;
        comp->speedVariance  = 0.5f;
        comp->size           = 0.4f;
        comp->sizeEnd        = 1.2f;
        comp->gravity        = 0.8f;
        comp->coneAngle      = 20.0f;
        comp->colorR = 0.5f; comp->colorG = 0.5f; comp->colorB = 0.5f; comp->colorA = 0.6f;
        comp->colorEndR = 0.3f; comp->colorEndG = 0.3f; comp->colorEndB = 0.3f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 2: // Sparks
        comp->maxParticles   = 300;
        comp->emissionRate   = 100.0f;
        comp->lifetime       = 0.8f;
        comp->speed          = 8.0f;
        comp->speedVariance  = 4.0f;
        comp->size           = 0.05f;
        comp->sizeEnd        = 0.0f;
        comp->gravity        = -9.81f;
        comp->coneAngle      = 45.0f;
        comp->colorR = 1.0f; comp->colorG = 0.9f; comp->colorB = 0.4f; comp->colorA = 1.0f;
        comp->colorEndR = 1.0f; comp->colorEndG = 0.3f; comp->colorEndB = 0.0f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 3: // Rain
        comp->maxParticles   = 500;
        comp->emissionRate   = 200.0f;
        comp->lifetime       = 1.0f;
        comp->speed          = 15.0f;
        comp->speedVariance  = 2.0f;
        comp->size           = 0.02f;
        comp->sizeEnd        = 0.02f;
        comp->gravity        = -9.81f;
        comp->coneAngle      = 5.0f;
        comp->colorR = 0.7f; comp->colorG = 0.8f; comp->colorB = 1.0f; comp->colorA = 0.5f;
        comp->colorEndR = 0.5f; comp->colorEndG = 0.6f; comp->colorEndB = 0.9f; comp->colorEndA = 0.2f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 4: // Snow
        comp->maxParticles   = 300;
        comp->emissionRate   = 80.0f;
        comp->lifetime       = 5.0f;
        comp->speed          = 0.5f;
        comp->speedVariance  = 0.3f;
        comp->size           = 0.08f;
        comp->sizeEnd        = 0.06f;
        comp->gravity        = -1.0f;
        comp->coneAngle      = 60.0f;
        comp->colorR = 1.0f; comp->colorG = 1.0f; comp->colorB = 1.0f; comp->colorA = 0.9f;
        comp->colorEndR = 0.9f; comp->colorEndG = 0.9f; comp->colorEndB = 1.0f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 5: // Magic
        comp->maxParticles   = 200;
        comp->emissionRate   = 50.0f;
        comp->lifetime       = 2.0f;
        comp->speed          = 2.0f;
        comp->speedVariance  = 1.5f;
        comp->size           = 0.15f;
        comp->sizeEnd        = 0.0f;
        comp->gravity        = 0.5f;
        comp->coneAngle      = 90.0f;
        comp->colorR = 0.4f; comp->colorG = 0.2f; comp->colorB = 1.0f; comp->colorA = 1.0f;
        comp->colorEndR = 1.0f; comp->colorEndG = 0.4f; comp->colorEndB = 0.8f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    default:
        return;
    }

    m_state.presetIndex = presetIndex;

    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
        level->setIsSaved(false);

    // Push grouped undo
    static const char* presetNamesList[] = { "Fire", "Smoke", "Sparks", "Rain", "Snow", "Magic" };
    const std::string cmdName = std::string("Apply Preset: ") + presetNamesList[presetIndex];
    const ECS::ParticleEmitterComponent applied = *comp;
    UndoRedoManager::Instance().pushCommand({
        cmdName,
        [entity, applied]() {
            auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
            if (c) *c = applied;
        },
        [entity, saved]() {
            auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
            if (c) *c = saved;
        }
    });

    refresh();

    static const char* presetToasts[] = { "Fire", "Smoke", "Sparks", "Rain", "Snow", "Magic" };
    m_ui->showToastMessage(std::string("Applied preset: ") + presetToasts[presetIndex], UIManager::kToastShort);
}

// ───────────────────────────────────────────────────────────────────────────
void ParticleEditorTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;

    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.3f)
    {
        m_state.refreshTimer = 0.0f;
        // Verify the linked entity still exists and has a ParticleEmitterComponent
        auto& ecs = ECS::ECSManager::Instance();
        if (!ecs.hasComponent<ECS::ParticleEmitterComponent>(m_state.linkedEntity))
        {
            close();
        }
    }
}
