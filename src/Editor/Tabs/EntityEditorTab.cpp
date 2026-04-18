#include "EntityEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Renderer/UIWidgets/DropdownButtonWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>

using json = nlohmann::json;

// ───────────────────────────────────────────────────────────────────────────
EntityEditorTab::EntityEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::open()
{
    // Default open with no asset — no-op (use open(assetPath) instead)
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::open(const std::string& assetPath)
{
    if (!m_renderer || !m_ui)
        return;

    const std::string tabId = "EntityEditor";

    // If already open for this asset, just switch to it
    if (m_state.isOpen && m_state.assetPath == assetPath)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    // If open for a different asset, close first
    if (m_state.isOpen)
        close();

    // Load the entity asset from disk
    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        m_ui->showToastMessage("No project loaded.", UIManager::kToastMedium);
        return;
    }

    const std::filesystem::path contentDir =
        std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
    const std::filesystem::path absPath = contentDir / assetPath;

    if (!std::filesystem::exists(absPath))
    {
        m_ui->showToastMessage("Entity asset not found: " + assetPath, UIManager::kToastMedium);
        return;
    }

    std::ifstream in(absPath);
    if (!in.is_open())
    {
        m_ui->showToastMessage("Failed to open entity asset.", UIManager::kToastMedium);
        return;
    }

    json fileJson = json::parse(in, nullptr, false);
    in.close();
    if (fileJson.is_discarded() || !fileJson.contains("data"))
    {
        m_ui->showToastMessage("Invalid entity asset format.", UIManager::kToastMedium);
        return;
    }

    m_renderer->addTab(tabId, "Entity Editor", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "EntityEditor.Main";
    m_ui->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId     = tabId;
    m_state.widgetId  = widgetId;
    m_state.assetPath = assetPath;
    m_state.assetName = fileJson.value("name", std::filesystem::path(assetPath).stem().string());
    m_state.isOpen    = true;
    m_state.isDirty   = false;

    // Extract entity data (components object)
    if (fileJson["data"].contains("components"))
        m_state.entityData = fileJson["data"]["components"];
    else
        m_state.entityData = json::object();

    // Auto-select the first component if any exist
    if (!m_state.entityData.empty())
        m_state.selectedComponent = m_state.entityData.begin().key();

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
        root.id          = "EntityEditor.Root";
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

        // Horizontal split: component list (left) | details panel (right)
        {
            WidgetElement splitRow{};
            splitRow.id          = "EntityEditor.SplitRow";
            splitRow.type        = WidgetElementType::StackPanel;
            splitRow.fillX       = true;
            splitRow.fillY       = true;
            splitRow.orientation = StackOrientation::Horizontal;
            splitRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            splitRow.runtimeOnly = true;

            // Left panel — component list
            {
                WidgetElement leftPanel{};
                leftPanel.id          = "EntityEditor.LeftPanel";
                leftPanel.type        = WidgetElementType::StackPanel;
                leftPanel.fillY       = true;
                leftPanel.scrollable  = true;
                leftPanel.orientation = StackOrientation::Vertical;
                leftPanel.minSize     = EditorTheme::Scaled(Vec2{ 200.0f, 0.0f });
                leftPanel.maxSize     = EditorTheme::Scaled(Vec2{ 220.0f, 0.0f });
                leftPanel.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 6.0f });
                leftPanel.style.color = Vec4{ 0.06f, 0.06f, 0.08f, 1.0f };
                leftPanel.runtimeOnly = true;
                splitRow.children.push_back(std::move(leftPanel));
            }

            // Vertical separator
            {
                WidgetElement sep{};
                sep.type        = WidgetElementType::Panel;
                sep.fillY       = true;
                sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 0.0f });
                sep.style.color = theme.panelBorder;
                sep.runtimeOnly = true;
                splitRow.children.push_back(std::move(sep));
            }

            // Right panel — details
            {
                WidgetElement rightPanel{};
                rightPanel.id          = "EntityEditor.RightPanel";
                rightPanel.type        = WidgetElementType::StackPanel;
                rightPanel.fillX       = true;
                rightPanel.fillY       = true;
                rightPanel.scrollable  = true;
                rightPanel.orientation = StackOrientation::Vertical;
                rightPanel.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
                rightPanel.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
                rightPanel.runtimeOnly = true;
                splitRow.children.push_back(std::move(rightPanel));
            }

            root.children.push_back(std::move(splitRow));
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

    // Save button
    m_ui->registerClickEvent("EntityEditor.Save", [this]()
    {
        save();
    });

    // Initial population
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::close()
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
void EntityEditorTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;
    // Could add auto-refresh logic here if needed
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::save()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.isProjectLoaded())
    {
        m_ui->showToastMessage("No project loaded.", UIManager::kToastMedium);
        return;
    }

    const std::filesystem::path contentDir =
        std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
    const std::filesystem::path absPath = contentDir / m_state.assetPath;

    json fileJson;
    fileJson["magic"]   = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"]    = static_cast<int>(AssetType::Entity);
    fileJson["name"]    = m_state.assetName;
    fileJson["data"]    = json{ {"components", m_state.entityData} };

    std::error_code ec;
    std::filesystem::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        m_ui->showToastMessage("Failed to save entity asset.", UIManager::kToastMedium);
        return;
    }

    out << fileJson.dump(4);
    out.close();

    m_state.isDirty = false;
    m_ui->showToastMessage("Entity saved: " + m_state.assetName, UIManager::kToastMedium);

    Logger::Instance().log(Logger::Category::AssetManagement,
        "Saved entity asset '" + m_state.assetName + "' to " + m_state.assetPath,
        Logger::LogLevel::INFO);

    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "EntityEditor.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title
    {
        WidgetElement title{};
        title.type           = WidgetElementType::Text;
        title.id             = "EntityEditor.Title";
        title.text           = m_state.assetName + (m_state.isDirty ? " *" : "");
        title.fontSize       = theme.fontSizeSubheading;
        title.style.textColor = Vec4{ 0.85f, 0.55f, 1.00f, 1.0f };
        title.textAlignV     = TextAlignV::Center;
        title.minSize        = EditorTheme::Scaled(Vec2{ 160.0f, 28.0f });
        title.runtimeOnly    = true;
        toolbar.children.push_back(std::move(title));
    }

    // Save button
    toolbar.children.push_back(EditorUIBuilder::makePrimaryButton(
        "EntityEditor.Save", "Save", {}, EditorTheme::Scaled(Vec2{ 60.0f, 26.0f })));

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// Helper to find an element by id in a tree
static WidgetElement* findElementById(std::vector<WidgetElement>& elems, const std::string& id)
{
    for (auto& el : elems)
    {
        if (el.id == id) return &el;
        if (auto* found = findElementById(el.children, id)) return found;
    }
    return nullptr;
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::refresh()
{
    if (!m_state.isOpen || !m_ui)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();

    // Update title
    if (auto* title = findElementById(elements, "EntityEditor.Title"))
        title->text = m_state.assetName + (m_state.isDirty ? " *" : "");

    // Populate left panel
    if (auto* leftPanel = findElementById(elements, "EntityEditor.LeftPanel"))
    {
        leftPanel->children.clear();
        buildComponentListPanel(*leftPanel);
        buildAddComponentMenu(*leftPanel);
    }

    // Populate right panel
    if (auto* rightPanel = findElementById(elements, "EntityEditor.RightPanel"))
    {
        rightPanel->children.clear();
        buildDetailsPanel(*rightPanel);
    }

    entry->widget->markLayoutDirty();
    m_ui->markRenderDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::selectComponent(const std::string& componentName)
{
    m_state.selectedComponent = componentName;
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildComponentListPanel(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();
    auto& data = m_state.entityData;

    // Section heading
    {
        WidgetElement heading{};
        heading.type           = WidgetElementType::Text;
        heading.text           = "Components";
        heading.fontSize       = theme.fontSizeSubheading;
        heading.style.textColor = theme.textSecondary;
        heading.fillX          = true;
        heading.minSize        = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        heading.padding        = EditorTheme::Scaled(Vec2{ 2.0f, 2.0f });
        heading.runtimeOnly    = true;
        parent.children.push_back(std::move(heading));
    }

    parent.children.push_back(EditorUIBuilder::makeDivider());

    // Display-friendly names for component keys
    static const std::vector<std::pair<std::string, std::string>> componentNames = {
        {"Name",             "Name"},
        {"Transform",        "Transform"},
        {"Mesh",             "Mesh"},
        {"Material",         "Material"},
        {"Light",            "Light"},
        {"Camera",           "Camera"},
        {"Physics",          "Physics"},
        {"Collision",        "Collision"},
        {"Script",           "Script"},
        {"Animation",        "Animation"},
        {"ParticleEmitter",  "Particle Emitter"}
    };

    for (const auto& [key, displayName] : componentNames)
    {
        if (!data.contains(key))
            continue;

        const bool isSelected = (m_state.selectedComponent == key);

        WidgetElement row{};
        row.id          = "EntityEditor.CompItem." + key;
        row.type        = WidgetElementType::Button;
        row.text        = displayName;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        row.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        row.fontSize    = theme.fontSizeBody;
        row.textAlignH  = TextAlignH::Left;
        row.textAlignV  = TextAlignV::Center;
        row.hitTestMode = HitTestMode::Enabled;
        row.runtimeOnly = true;

        if (isSelected)
        {
            row.style.color      = theme.selectionHighlight;
            row.style.hoverColor = theme.selectionHighlightHover;
            row.style.textColor  = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
        else
        {
            row.style.color      = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.style.hoverColor = theme.treeRowHover;
            row.style.textColor  = theme.textPrimary;
        }

        row.onClicked = [this, k = key]() { selectComponent(k); };
        parent.children.push_back(std::move(row));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildAddComponentMenu(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();
    auto& data = m_state.entityData;

    parent.children.push_back(EditorUIBuilder::makeDivider());

    struct CompEntry { std::string name; std::string key; };
    std::vector<CompEntry> allComps = {
        {"Name",             "Name"},
        {"Transform",        "Transform"},
        {"Mesh",             "Mesh"},
        {"Material",         "Material"},
        {"Light",            "Light"},
        {"Camera",           "Camera"},
        {"Physics",          "Physics"},
        {"Collision",        "Collision"},
        {"Script",           "Script"},
        {"Animation",        "Animation"},
        {"Particle Emitter", "ParticleEmitter"}
    };

    // Collect components not yet added
    std::vector<CompEntry> missing;
    for (const auto& comp : allComps)
    {
        if (!data.contains(comp.key))
            missing.push_back(comp);
    }

    if (missing.empty())
        return;

    // Build a DropdownButton with all addable components
    DropdownButtonWidget dropdown;
    dropdown.setText("+ Add Component");
    dropdown.setFont(theme.fontDefault);
    dropdown.setFontSize(theme.fontSizeBody);
    dropdown.setMinSize(EditorTheme::Scaled(Vec2{ 0.0f, 28.0f }));
    dropdown.setPadding(EditorTheme::Scaled(Vec2{ 8.0f, 4.0f }));
    dropdown.setBackgroundColor(Vec4{ 0.12f, 0.18f, 0.12f, 1.0f });
    dropdown.setHoverColor(theme.accentGreen);
    dropdown.setTextColor(Vec4{ 0.8f, 1.0f, 0.8f, 1.0f });

    for (const auto& comp : missing)
    {
        dropdown.addItem(comp.name, [this, key = comp.key]() { addComponent(key); });
    }

    WidgetElement dropEl = dropdown.toElement();
    dropEl.id          = "EntityEditor.AddComponent";
    dropEl.fillX       = true;
    dropEl.runtimeOnly = true;
    parent.children.push_back(std::move(dropEl));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::addComponent(const std::string& componentName)
{
    auto& data = m_state.entityData;
    if (data.contains(componentName))
        return;

    // Initialize with defaults
    if (componentName == "Name")
        data["Name"] = json{ {"displayName", m_state.assetName} };
    else if (componentName == "Transform")
        data["Transform"] = json{ {"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}} };
    else if (componentName == "Mesh")
        data["Mesh"] = json{ {"meshAssetPath", ""} };
    else if (componentName == "Material")
        data["Material"] = json{ {"materialAssetPath", ""} };
    else if (componentName == "Light")
        data["Light"] = json{ {"type", 0}, {"color", {1.0f, 1.0f, 1.0f}}, {"intensity", 1.0f}, {"range", 10.0f}, {"spotAngle", 30.0f} };
    else if (componentName == "Camera")
        data["Camera"] = json{ {"fov", 60.0f}, {"nearClip", 0.1f}, {"farClip", 1000.0f} };
    else if (componentName == "Physics")
        data["Physics"] = json{ {"motionType", 2}, {"mass", 1.0f}, {"gravityFactor", 1.0f}, {"linearDamping", 0.05f}, {"angularDamping", 0.05f} };
    else if (componentName == "Collision")
        data["Collision"] = json{ {"colliderType", 0}, {"colliderSize", {0.5f, 0.5f, 0.5f}}, {"colliderOffset", {0.0f, 0.0f, 0.0f}}, {"restitution", 0.3f}, {"friction", 0.5f}, {"isSensor", false} };
    else if (componentName == "Script")
        data["Script"] = json{ {"scriptPath", ""} };
    else if (componentName == "Animation")
        data["Animation"] = json{ {"currentClipIndex", -1}, {"speed", 1.0f}, {"playing", false}, {"loop", true} };
    else if (componentName == "ParticleEmitter")
        data["ParticleEmitter"] = json{
            {"maxParticles", 100}, {"emissionRate", 20.0f}, {"lifetime", 2.0f},
            {"speed", 2.0f}, {"speedVariance", 0.5f}, {"size", 0.2f}, {"sizeEnd", 0.0f},
            {"gravity", -9.81f},
            {"colorR", 1.0f}, {"colorG", 0.8f}, {"colorB", 0.2f}, {"colorA", 1.0f},
            {"colorEndR", 1.0f}, {"colorEndG", 0.1f}, {"colorEndB", 0.0f}, {"colorEndA", 0.0f},
            {"coneAngle", 30.0f}, {"enabled", true}, {"loop", true}
        };
    else
        data[componentName] = json::object();

    m_state.isDirty = true;
    m_state.selectedComponent = componentName;
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::removeComponent(const std::string& componentName)
{
    m_state.entityData.erase(componentName);
    m_state.isDirty = true;

    // If the removed component was selected, select the first remaining one
    if (m_state.selectedComponent == componentName)
    {
        if (!m_state.entityData.empty())
            m_state.selectedComponent = m_state.entityData.begin().key();
        else
            m_state.selectedComponent.clear();
    }
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
std::vector<std::string> EntityEditorTab::getAssetPathsByType(int assetType) const
{
    std::vector<std::string> result;
    const auto& registry = AssetManager::Instance().getAssetRegistry();
    for (const auto& entry : registry)
    {
        if (static_cast<int>(entry.type) == assetType)
            result.push_back(entry.path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Details Panel — dispatches to the right builder based on selection
// ═══════════════════════════════════════════════════════════════════════════

void EntityEditorTab::buildDetailsPanel(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();
    const auto& sel = m_state.selectedComponent;

    if (sel.empty() || !m_state.entityData.contains(sel))
    {
        // Show hint when nothing is selected
        WidgetElement hint{};
        hint.type           = WidgetElementType::Text;
        hint.text           = "Select a component on the left to edit its properties.";
        hint.fontSize       = theme.fontSizeBody;
        hint.style.textColor = theme.textMuted;
        hint.fillX          = true;
        hint.wrapText       = true;
        hint.padding        = EditorTheme::Scaled(Vec2{ 8.0f, 16.0f });
        hint.runtimeOnly    = true;
        parent.children.push_back(std::move(hint));
        return;
    }

    // Header with component name + remove button
    {
        static const std::vector<std::pair<std::string, std::string>> displayNames = {
            {"Name", "Name"}, {"Transform", "Transform"}, {"Mesh", "Mesh"},
            {"Material", "Material"}, {"Light", "Light"}, {"Camera", "Camera"},
            {"Physics", "Physics"}, {"Collision", "Collision"}, {"Script", "Script"},
            {"Animation", "Animation"}, {"ParticleEmitter", "Particle Emitter"}
        };
        std::string displayName = sel;
        for (const auto& [k, v] : displayNames)
            if (k == sel) { displayName = v; break; }

        WidgetElement headerRow{};
        headerRow.id          = "EntityEditor.Detail.Header";
        headerRow.type        = WidgetElementType::StackPanel;
        headerRow.fillX       = true;
        headerRow.orientation = StackOrientation::Horizontal;
        headerRow.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
        headerRow.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 30.0f });
        headerRow.style.color = Vec4{ 0.12f, 0.13f, 0.17f, 1.0f };
        headerRow.runtimeOnly = true;

        {
            WidgetElement lbl{};
            lbl.type           = WidgetElementType::Text;
            lbl.text           = displayName;
            lbl.fontSize       = theme.fontSizeSubheading;
            lbl.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
            lbl.textAlignV     = TextAlignV::Center;
            lbl.fillX          = true;
            lbl.minSize        = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
            lbl.runtimeOnly    = true;
            headerRow.children.push_back(std::move(lbl));
        }

        {
            auto removeBtn = EditorUIBuilder::makeDangerButton(
                "EntityEditor.Remove." + sel, "Remove",
                [this, key = sel]() { removeComponent(key); },
                EditorTheme::Scaled(Vec2{ 60.0f, 24.0f }));
            headerRow.children.push_back(std::move(removeBtn));
        }

        parent.children.push_back(std::move(headerRow));
        parent.children.push_back(EditorUIBuilder::makeDivider());
    }

    // Dispatch to per-component detail builder
    if (sel == "Name")             buildNameDetails(parent);
    else if (sel == "Transform")   buildTransformDetails(parent);
    else if (sel == "Mesh")        buildMeshDetails(parent);
    else if (sel == "Material")    buildMaterialDetails(parent);
    else if (sel == "Light")       buildLightDetails(parent);
    else if (sel == "Camera")      buildCameraDetails(parent);
    else if (sel == "Physics")     buildPhysicsDetails(parent);
    else if (sel == "Collision")   buildCollisionDetails(parent);
    else if (sel == "Script")      buildScriptDetails(parent);
    else if (sel == "Animation")   buildAnimationDetails(parent);
    else if (sel == "ParticleEmitter") buildParticleEmitterDetails(parent);
}

// ═══════════════════════════════════════════════════════════════════════════
// Per-Component Detail Builders
// ═══════════════════════════════════════════════════════════════════════════

void EntityEditorTab::buildNameDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Name";
    std::string displayName = m_state.entityData["Name"].value("displayName", "");
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        id + ".displayName", "Display Name", displayName,
        [this](const std::string& v) {
            m_state.entityData["Name"]["displayName"] = v;
            m_state.isDirty = true;
        }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildTransformDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Transform";
    auto& t = m_state.entityData["Transform"];

    float pos[3] = { 0, 0, 0 };
    float rot[3] = { 0, 0, 0 };
    float scl[3] = { 1, 1, 1 };
    if (t.contains("position") && t["position"].is_array() && t["position"].size() >= 3)
        for (int i = 0; i < 3; ++i) pos[i] = t["position"][i].get<float>();
    if (t.contains("rotation") && t["rotation"].is_array() && t["rotation"].size() >= 3)
        for (int i = 0; i < 3; ++i) rot[i] = t["rotation"][i].get<float>();
    if (t.contains("scale") && t["scale"].is_array() && t["scale"].size() >= 3)
        for (int i = 0; i < 3; ++i) scl[i] = t["scale"][i].get<float>();

    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        id + ".Pos", "Position", pos,
        [this](int axis, float v) {
            m_state.entityData["Transform"]["position"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        id + ".Rot", "Rotation", rot,
        [this](int axis, float v) {
            m_state.entityData["Transform"]["rotation"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        id + ".Scl", "Scale", scl,
        [this](int axis, float v) {
            m_state.entityData["Transform"]["scale"][axis] = v;
            m_state.isDirty = true;
        }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildMeshDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Mesh";
    const auto& theme = EditorTheme::Get();
    std::string meshPath = m_state.entityData["Mesh"].value("meshAssetPath", "");

    // Label
    {
        WidgetElement lbl{};
        lbl.type           = WidgetElementType::Text;
        lbl.text           = "Mesh Asset";
        lbl.fontSize       = theme.fontSizeBody;
        lbl.style.textColor = theme.textSecondary;
        lbl.minSize        = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        lbl.padding        = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        lbl.runtimeOnly    = true;
        parent.children.push_back(std::move(lbl));
    }

    // Dropdown for mesh asset selection
    {
        DropdownButtonWidget dropdown;
        dropdown.setText(meshPath.empty() ? "Select Mesh..." : meshPath);
        dropdown.setFont(theme.fontDefault);
        dropdown.setFontSize(theme.fontSizeBody);
        dropdown.setMinSize(EditorTheme::Scaled(Vec2{ 0.0f, 28.0f }));
        dropdown.setPadding(EditorTheme::Scaled(Vec2{ 8.0f, 4.0f }));
        dropdown.setBackgroundColor(theme.dropdownBackground);
        dropdown.setHoverColor(theme.dropdownHover);
        dropdown.setTextColor(theme.dropdownText);

        // "None" option
        dropdown.addItem("(None)", [this]() {
            m_state.entityData["Mesh"]["meshAssetPath"] = "";
            m_state.isDirty = true;
            refresh();
        });

        auto meshAssets = getAssetPathsByType(static_cast<int>(AssetType::Model3D));
        for (const auto& path : meshAssets)
        {
            dropdown.addItem(path, [this, p = path]() {
                m_state.entityData["Mesh"]["meshAssetPath"] = p;
                m_state.isDirty = true;
                refresh();
            });
        }

        WidgetElement dropEl = dropdown.toElement();
        dropEl.id          = id + ".Dropdown";
        dropEl.fillX       = true;
        dropEl.runtimeOnly = true;
        parent.children.push_back(std::move(dropEl));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildMaterialDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Material";
    const auto& theme = EditorTheme::Get();
    std::string matPath = m_state.entityData["Material"].value("materialAssetPath", "");

    // Label
    {
        WidgetElement lbl{};
        lbl.type           = WidgetElementType::Text;
        lbl.text           = "Material Asset";
        lbl.fontSize       = theme.fontSizeBody;
        lbl.style.textColor = theme.textSecondary;
        lbl.minSize        = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        lbl.padding        = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        lbl.runtimeOnly    = true;
        parent.children.push_back(std::move(lbl));
    }

    // Dropdown for material asset selection
    {
        DropdownButtonWidget dropdown;
        dropdown.setText(matPath.empty() ? "Select Material..." : matPath);
        dropdown.setFont(theme.fontDefault);
        dropdown.setFontSize(theme.fontSizeBody);
        dropdown.setMinSize(EditorTheme::Scaled(Vec2{ 0.0f, 28.0f }));
        dropdown.setPadding(EditorTheme::Scaled(Vec2{ 8.0f, 4.0f }));
        dropdown.setBackgroundColor(theme.dropdownBackground);
        dropdown.setHoverColor(theme.dropdownHover);
        dropdown.setTextColor(theme.dropdownText);

        dropdown.addItem("(None)", [this]() {
            m_state.entityData["Material"]["materialAssetPath"] = "";
            m_state.isDirty = true;
            refresh();
        });

        auto matAssets = getAssetPathsByType(static_cast<int>(AssetType::Material));
        for (const auto& path : matAssets)
        {
            dropdown.addItem(path, [this, p = path]() {
                m_state.entityData["Material"]["materialAssetPath"] = p;
                m_state.isDirty = true;
                refresh();
            });
        }

        WidgetElement dropEl = dropdown.toElement();
        dropEl.id          = id + ".Dropdown";
        dropEl.fillX       = true;
        dropEl.runtimeOnly = true;
        parent.children.push_back(std::move(dropEl));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildLightDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Light";
    auto& l = m_state.entityData["Light"];

    int lightType = l.value("type", 0);
    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        id + ".type", "Type", {"Point", "Directional", "Spot"}, lightType,
        [this](int v) {
            m_state.entityData["Light"]["type"] = v;
            m_state.isDirty = true;
        }));

    float color[3] = { 1, 1, 1 };
    if (l.contains("color") && l["color"].is_array() && l["color"].size() >= 3)
        for (int i = 0; i < 3; ++i) color[i] = l["color"][i].get<float>();
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        id + ".Color", "Color", color,
        [this](int axis, float v) {
            m_state.entityData["Light"]["color"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".intensity", "Intensity", l.value("intensity", 1.0f),
        [this](float v) { m_state.entityData["Light"]["intensity"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".range", "Range", l.value("range", 10.0f),
        [this](float v) { m_state.entityData["Light"]["range"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".spotAngle", "Spot Angle", l.value("spotAngle", 30.0f),
        [this](float v) { m_state.entityData["Light"]["spotAngle"] = v; m_state.isDirty = true; }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildCameraDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Camera";
    auto& cm = m_state.entityData["Camera"];

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".fov", "FOV", cm.value("fov", 60.0f),
        [this](float v) { m_state.entityData["Camera"]["fov"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".nearClip", "Near Clip", cm.value("nearClip", 0.1f),
        [this](float v) { m_state.entityData["Camera"]["nearClip"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".farClip", "Far Clip", cm.value("farClip", 1000.0f),
        [this](float v) { m_state.entityData["Camera"]["farClip"] = v; m_state.isDirty = true; }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildPhysicsDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Physics";
    auto& p = m_state.entityData["Physics"];

    int motionType = p.value("motionType", 2);
    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        id + ".motionType", "Motion Type", {"Static", "Kinematic", "Dynamic"}, motionType,
        [this](int v) { m_state.entityData["Physics"]["motionType"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".mass", "Mass", p.value("mass", 1.0f),
        [this](float v) { m_state.entityData["Physics"]["mass"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".gravityFactor", "Gravity Factor", p.value("gravityFactor", 1.0f),
        [this](float v) { m_state.entityData["Physics"]["gravityFactor"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".linearDamping", "Linear Damping", p.value("linearDamping", 0.05f),
        [this](float v) { m_state.entityData["Physics"]["linearDamping"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".angularDamping", "Angular Damping", p.value("angularDamping", 0.05f),
        [this](float v) { m_state.entityData["Physics"]["angularDamping"] = v; m_state.isDirty = true; }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildCollisionDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Collision";
    auto& cl = m_state.entityData["Collision"];

    int colliderType = cl.value("colliderType", 0);
    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        id + ".colliderType", "Collider Type",
        {"Box", "Sphere", "Capsule", "Cylinder", "Mesh", "HeightField"}, colliderType,
        [this](int v) { m_state.entityData["Collision"]["colliderType"] = v; m_state.isDirty = true; }));

    float size[3] = { 0.5f, 0.5f, 0.5f };
    if (cl.contains("colliderSize") && cl["colliderSize"].is_array() && cl["colliderSize"].size() >= 3)
        for (int i = 0; i < 3; ++i) size[i] = cl["colliderSize"][i].get<float>();
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        id + ".Size", "Size", size,
        [this](int axis, float v) {
            m_state.entityData["Collision"]["colliderSize"][axis] = v;
            m_state.isDirty = true;
        }));

    float offset[3] = { 0, 0, 0 };
    if (cl.contains("colliderOffset") && cl["colliderOffset"].is_array() && cl["colliderOffset"].size() >= 3)
        for (int i = 0; i < 3; ++i) offset[i] = cl["colliderOffset"][i].get<float>();
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        id + ".Offset", "Offset", offset,
        [this](int axis, float v) {
            m_state.entityData["Collision"]["colliderOffset"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".restitution", "Restitution", cl.value("restitution", 0.3f),
        [this](float v) { m_state.entityData["Collision"]["restitution"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".friction", "Friction", cl.value("friction", 0.5f),
        [this](float v) { m_state.entityData["Collision"]["friction"] = v; m_state.isDirty = true; }));

    bool isSensor = cl.value("isSensor", false);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        id + ".isSensor", "Is Sensor", isSensor,
        [this](bool v) { m_state.entityData["Collision"]["isSensor"] = v; m_state.isDirty = true; }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildScriptDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Script";
    const auto& theme = EditorTheme::Get();
    std::string scriptPath = m_state.entityData["Script"].value("scriptPath", "");

    // Label
    {
        WidgetElement lbl{};
        lbl.type           = WidgetElementType::Text;
        lbl.text           = "Script Asset";
        lbl.fontSize       = theme.fontSizeBody;
        lbl.style.textColor = theme.textSecondary;
        lbl.minSize        = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        lbl.padding        = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        lbl.runtimeOnly    = true;
        parent.children.push_back(std::move(lbl));
    }

    // Dropdown for script asset selection
    {
        DropdownButtonWidget dropdown;
        dropdown.setText(scriptPath.empty() ? "Select Script..." : scriptPath);
        dropdown.setFont(theme.fontDefault);
        dropdown.setFontSize(theme.fontSizeBody);
        dropdown.setMinSize(EditorTheme::Scaled(Vec2{ 0.0f, 28.0f }));
        dropdown.setPadding(EditorTheme::Scaled(Vec2{ 8.0f, 4.0f }));
        dropdown.setBackgroundColor(theme.dropdownBackground);
        dropdown.setHoverColor(theme.dropdownHover);
        dropdown.setTextColor(theme.dropdownText);

        dropdown.addItem("(None)", [this]() {
            m_state.entityData["Script"]["scriptPath"] = "";
            m_state.isDirty = true;
            refresh();
        });

        auto scriptAssets = getAssetPathsByType(static_cast<int>(AssetType::Script));
        for (const auto& path : scriptAssets)
        {
            dropdown.addItem(path, [this, p = path]() {
                m_state.entityData["Script"]["scriptPath"] = p;
                m_state.isDirty = true;
                refresh();
            });
        }

        WidgetElement dropEl = dropdown.toElement();
        dropEl.id          = id + ".Dropdown";
        dropEl.fillX       = true;
        dropEl.runtimeOnly = true;
        parent.children.push_back(std::move(dropEl));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildAnimationDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Animation";
    auto& a = m_state.entityData["Animation"];

    parent.children.push_back(EditorUIBuilder::makeIntRow(
        id + ".clipIndex", "Clip Index", a.value("currentClipIndex", -1),
        [this](int v) { m_state.entityData["Animation"]["currentClipIndex"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".speed", "Speed", a.value("speed", 1.0f),
        [this](float v) { m_state.entityData["Animation"]["speed"] = v; m_state.isDirty = true; }));

    bool playing = a.value("playing", false);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        id + ".playing", "Playing", playing,
        [this](bool v) { m_state.entityData["Animation"]["playing"] = v; m_state.isDirty = true; }));

    bool loop = a.value("loop", true);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        id + ".loop", "Loop", loop,
        [this](bool v) { m_state.entityData["Animation"]["loop"] = v; m_state.isDirty = true; }));
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildParticleEmitterDetails(WidgetElement& parent)
{
    const std::string id = "EntityEditor.Det.Particle";
    auto& pe = m_state.entityData["ParticleEmitter"];

    parent.children.push_back(EditorUIBuilder::makeIntRow(
        id + ".maxParticles", "Max Particles", pe.value("maxParticles", 100),
        [this](int v) { m_state.entityData["ParticleEmitter"]["maxParticles"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".emissionRate", "Emission Rate", pe.value("emissionRate", 20.0f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["emissionRate"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".lifetime", "Lifetime", pe.value("lifetime", 2.0f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["lifetime"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".speed", "Speed", pe.value("speed", 2.0f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["speed"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".size", "Size", pe.value("size", 0.2f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["size"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        id + ".gravity", "Gravity", pe.value("gravity", -9.81f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["gravity"] = v; m_state.isDirty = true; }));

    bool enabled = pe.value("enabled", true);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        id + ".enabled", "Enabled", enabled,
        [this](bool v) { m_state.entityData["ParticleEmitter"]["enabled"] = v; m_state.isDirty = true; }));

    bool loop = pe.value("loop", true);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        id + ".loop", "Loop", loop,
        [this](bool v) { m_state.entityData["ParticleEmitter"]["loop"] = v; m_state.isDirty = true; }));
}
