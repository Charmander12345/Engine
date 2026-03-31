#include "EntityEditorTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"

#include <filesystem>
#include <fstream>
#include <string>

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

        // Scrollable component area
        {
            WidgetElement paramsArea{};
            paramsArea.id          = "EntityEditor.ParamsArea";
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
void EntityEditorTab::refresh()
{
    if (!m_state.isOpen || !m_ui)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    WidgetElement* paramsArea = nullptr;

    // Find the ParamsArea
    std::function<WidgetElement*(std::vector<WidgetElement>&)> findById =
        [&](std::vector<WidgetElement>& elems) -> WidgetElement*
        {
            for (auto& el : elems)
            {
                if (el.id == "EntityEditor.ParamsArea") return &el;
                if (auto* found = findById(el.children)) return found;
            }
            return nullptr;
        };
    paramsArea = findById(elements);
    if (!paramsArea)
        return;

    paramsArea->children.clear();

    // Update title
    WidgetElement* title = nullptr;
    std::function<WidgetElement*(std::vector<WidgetElement>&)> findTitle =
        [&](std::vector<WidgetElement>& elems) -> WidgetElement*
        {
            for (auto& el : elems)
            {
                if (el.id == "EntityEditor.Title") return &el;
                if (auto* found = findTitle(el.children)) return found;
            }
            return nullptr;
        };
    title = findTitle(elements);
    if (title)
        title->text = m_state.assetName + (m_state.isDirty ? " *" : "");

    // Build component sections
    buildComponentList(*paramsArea);

    // Add "Add Component" button area
    buildAddComponentMenu(*paramsArea);

    entry->widget->markLayoutDirty();
    m_ui->markRenderDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildComponentList(WidgetElement& root)
{
    auto& data = m_state.entityData;

    if (data.contains("Name"))        buildNameSection(root);
    if (data.contains("Transform"))   buildTransformSection(root);
    if (data.contains("Mesh"))        buildMeshSection(root);
    if (data.contains("Material"))    buildMaterialSection(root);
    if (data.contains("Light"))       buildLightSection(root);
    if (data.contains("Camera"))      buildCameraSection(root);
    if (data.contains("Physics"))     buildPhysicsSection(root);
    if (data.contains("Collision"))   buildCollisionSection(root);
    if (data.contains("Script"))      buildScriptSection(root);
    if (data.contains("Animation"))   buildAnimationSection(root);
    if (data.contains("ParticleEmitter")) buildParticleEmitterSection(root);
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildAddComponentMenu(WidgetElement& root)
{
    root.children.push_back(EditorUIBuilder::makeDivider());

    // List of all possible components and whether they already exist
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

    auto& data = m_state.entityData;

    for (const auto& comp : allComps)
    {
        if (data.contains(comp.key))
            continue;

        auto btn = EditorUIBuilder::makeButton(
            "EntityEditor.Add." + comp.key,
            "+ " + comp.name,
            [this, key = comp.key]() { addComponent(key); },
            EditorTheme::Scaled(Vec2{ 200.0f, 26.0f }));
        root.children.push_back(std::move(btn));
    }
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
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::removeComponent(const std::string& componentName)
{
    m_state.entityData.erase(componentName);
    m_state.isDirty = true;
    refresh();
}

// ═══════════════════════════════════════════════════════════════════════════
// Component Section Builders
// ═══════════════════════════════════════════════════════════════════════════

static WidgetElement makeSectionHeader(const std::string& id, const std::string& title,
    std::function<void()> onRemove)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement row{};
    row.id          = id + ".Header";
    row.type        = WidgetElementType::StackPanel;
    row.fillX       = true;
    row.orientation = StackOrientation::Horizontal;
    row.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
    row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 28.0f });
    row.style.color = Vec4{ 0.12f, 0.13f, 0.17f, 1.0f };
    row.runtimeOnly = true;

    // Section title
    {
        WidgetElement lbl{};
        lbl.type           = WidgetElementType::Text;
        lbl.text           = title;
        lbl.fontSize       = theme.fontSizeSubheading;
        lbl.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
        lbl.textAlignV     = TextAlignV::Center;
        lbl.fillX          = true;
        lbl.minSize        = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        lbl.runtimeOnly    = true;
        row.children.push_back(std::move(lbl));
    }

    // Remove button
    {
        auto removeBtn = EditorUIBuilder::makeDangerButton(
            id + ".Remove", "X", std::move(onRemove),
            EditorTheme::Scaled(Vec2{ 26.0f, 22.0f }));
        row.children.push_back(std::move(removeBtn));
    }

    return row;
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildNameSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Name";
    parent.children.push_back(makeSectionHeader(secId, "Name",
        [this]() { removeComponent("Name"); }));

    std::string displayName = m_state.entityData["Name"].value("displayName", "");
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        secId + ".displayName", "Display Name", displayName,
        [this](const std::string& v) {
            m_state.entityData["Name"]["displayName"] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildTransformSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Transform";
    parent.children.push_back(makeSectionHeader(secId, "Transform",
        [this]() { removeComponent("Transform"); }));

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
        secId + ".Pos", "Position", pos,
        [this](int axis, float v) {
            m_state.entityData["Transform"]["position"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        secId + ".Rot", "Rotation", rot,
        [this](int axis, float v) {
            m_state.entityData["Transform"]["rotation"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        secId + ".Scl", "Scale", scl,
        [this](int axis, float v) {
            m_state.entityData["Transform"]["scale"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildMeshSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Mesh";
    parent.children.push_back(makeSectionHeader(secId, "Mesh",
        [this]() { removeComponent("Mesh"); }));

    std::string meshPath = m_state.entityData["Mesh"].value("meshAssetPath", "");
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        secId + ".meshAssetPath", "Mesh Asset", meshPath,
        [this](const std::string& v) {
            m_state.entityData["Mesh"]["meshAssetPath"] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildMaterialSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Material";
    parent.children.push_back(makeSectionHeader(secId, "Material",
        [this]() { removeComponent("Material"); }));

    std::string matPath = m_state.entityData["Material"].value("materialAssetPath", "");
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        secId + ".materialAssetPath", "Material Asset", matPath,
        [this](const std::string& v) {
            m_state.entityData["Material"]["materialAssetPath"] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildLightSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Light";
    parent.children.push_back(makeSectionHeader(secId, "Light",
        [this]() { removeComponent("Light"); }));

    auto& l = m_state.entityData["Light"];

    int lightType = l.value("type", 0);
    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        secId + ".type", "Type", {"Point", "Directional", "Spot"}, lightType,
        [this](int v) {
            m_state.entityData["Light"]["type"] = v;
            m_state.isDirty = true;
        }));

    float color[3] = { 1, 1, 1 };
    if (l.contains("color") && l["color"].is_array() && l["color"].size() >= 3)
        for (int i = 0; i < 3; ++i) color[i] = l["color"][i].get<float>();
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        secId + ".Color", "Color", color,
        [this](int axis, float v) {
            m_state.entityData["Light"]["color"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".intensity", "Intensity", l.value("intensity", 1.0f),
        [this](float v) { m_state.entityData["Light"]["intensity"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".range", "Range", l.value("range", 10.0f),
        [this](float v) { m_state.entityData["Light"]["range"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".spotAngle", "Spot Angle", l.value("spotAngle", 30.0f),
        [this](float v) { m_state.entityData["Light"]["spotAngle"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildCameraSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Camera";
    parent.children.push_back(makeSectionHeader(secId, "Camera",
        [this]() { removeComponent("Camera"); }));

    auto& cm = m_state.entityData["Camera"];

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".fov", "FOV", cm.value("fov", 60.0f),
        [this](float v) { m_state.entityData["Camera"]["fov"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".nearClip", "Near Clip", cm.value("nearClip", 0.1f),
        [this](float v) { m_state.entityData["Camera"]["nearClip"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".farClip", "Far Clip", cm.value("farClip", 1000.0f),
        [this](float v) { m_state.entityData["Camera"]["farClip"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildPhysicsSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Physics";
    parent.children.push_back(makeSectionHeader(secId, "Physics",
        [this]() { removeComponent("Physics"); }));

    auto& p = m_state.entityData["Physics"];

    int motionType = p.value("motionType", 2);
    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        secId + ".motionType", "Motion Type", {"Static", "Kinematic", "Dynamic"}, motionType,
        [this](int v) { m_state.entityData["Physics"]["motionType"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".mass", "Mass", p.value("mass", 1.0f),
        [this](float v) { m_state.entityData["Physics"]["mass"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".gravityFactor", "Gravity Factor", p.value("gravityFactor", 1.0f),
        [this](float v) { m_state.entityData["Physics"]["gravityFactor"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".linearDamping", "Linear Damping", p.value("linearDamping", 0.05f),
        [this](float v) { m_state.entityData["Physics"]["linearDamping"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".angularDamping", "Angular Damping", p.value("angularDamping", 0.05f),
        [this](float v) { m_state.entityData["Physics"]["angularDamping"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildCollisionSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Collision";
    parent.children.push_back(makeSectionHeader(secId, "Collision",
        [this]() { removeComponent("Collision"); }));

    auto& cl = m_state.entityData["Collision"];

    int colliderType = cl.value("colliderType", 0);
    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        secId + ".colliderType", "Collider Type",
        {"Box", "Sphere", "Capsule", "Cylinder", "Mesh", "HeightField"}, colliderType,
        [this](int v) { m_state.entityData["Collision"]["colliderType"] = v; m_state.isDirty = true; }));

    float size[3] = { 0.5f, 0.5f, 0.5f };
    if (cl.contains("colliderSize") && cl["colliderSize"].is_array() && cl["colliderSize"].size() >= 3)
        for (int i = 0; i < 3; ++i) size[i] = cl["colliderSize"][i].get<float>();
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        secId + ".Size", "Size", size,
        [this](int axis, float v) {
            m_state.entityData["Collision"]["colliderSize"][axis] = v;
            m_state.isDirty = true;
        }));

    float offset[3] = { 0, 0, 0 };
    if (cl.contains("colliderOffset") && cl["colliderOffset"].is_array() && cl["colliderOffset"].size() >= 3)
        for (int i = 0; i < 3; ++i) offset[i] = cl["colliderOffset"][i].get<float>();
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        secId + ".Offset", "Offset", offset,
        [this](int axis, float v) {
            m_state.entityData["Collision"]["colliderOffset"][axis] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".restitution", "Restitution", cl.value("restitution", 0.3f),
        [this](float v) { m_state.entityData["Collision"]["restitution"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".friction", "Friction", cl.value("friction", 0.5f),
        [this](float v) { m_state.entityData["Collision"]["friction"] = v; m_state.isDirty = true; }));

    bool isSensor = cl.value("isSensor", false);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        secId + ".isSensor", "Is Sensor", isSensor,
        [this](bool v) { m_state.entityData["Collision"]["isSensor"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildScriptSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Script";
    parent.children.push_back(makeSectionHeader(secId, "Script",
        [this]() { removeComponent("Script"); }));

    std::string scriptPath = m_state.entityData["Script"].value("scriptPath", "");
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        secId + ".scriptPath", "Script Path", scriptPath,
        [this](const std::string& v) {
            m_state.entityData["Script"]["scriptPath"] = v;
            m_state.isDirty = true;
        }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildAnimationSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Animation";
    parent.children.push_back(makeSectionHeader(secId, "Animation",
        [this]() { removeComponent("Animation"); }));

    auto& a = m_state.entityData["Animation"];

    parent.children.push_back(EditorUIBuilder::makeIntRow(
        secId + ".clipIndex", "Clip Index", a.value("currentClipIndex", -1),
        [this](int v) { m_state.entityData["Animation"]["currentClipIndex"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".speed", "Speed", a.value("speed", 1.0f),
        [this](float v) { m_state.entityData["Animation"]["speed"] = v; m_state.isDirty = true; }));

    bool playing = a.value("playing", false);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        secId + ".playing", "Playing", playing,
        [this](bool v) { m_state.entityData["Animation"]["playing"] = v; m_state.isDirty = true; }));

    bool loop = a.value("loop", true);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        secId + ".loop", "Loop", loop,
        [this](bool v) { m_state.entityData["Animation"]["loop"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}

// ───────────────────────────────────────────────────────────────────────────
void EntityEditorTab::buildParticleEmitterSection(WidgetElement& parent)
{
    const std::string secId = "EntityEditor.Sec.Particle";
    parent.children.push_back(makeSectionHeader(secId, "Particle Emitter",
        [this]() { removeComponent("ParticleEmitter"); }));

    auto& pe = m_state.entityData["ParticleEmitter"];

    parent.children.push_back(EditorUIBuilder::makeIntRow(
        secId + ".maxParticles", "Max Particles", pe.value("maxParticles", 100),
        [this](int v) { m_state.entityData["ParticleEmitter"]["maxParticles"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".emissionRate", "Emission Rate", pe.value("emissionRate", 20.0f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["emissionRate"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".lifetime", "Lifetime", pe.value("lifetime", 2.0f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["lifetime"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".speed", "Speed", pe.value("speed", 2.0f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["speed"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".size", "Size", pe.value("size", 0.2f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["size"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        secId + ".gravity", "Gravity", pe.value("gravity", -9.81f),
        [this](float v) { m_state.entityData["ParticleEmitter"]["gravity"] = v; m_state.isDirty = true; }));

    bool enabled = pe.value("enabled", true);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        secId + ".enabled", "Enabled", enabled,
        [this](bool v) { m_state.entityData["ParticleEmitter"]["enabled"] = v; m_state.isDirty = true; }));

    bool loop = pe.value("loop", true);
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        secId + ".loop", "Loop", loop,
        [this](bool v) { m_state.entityData["ParticleEmitter"]["loop"] = v; m_state.isDirty = true; }));

    parent.children.push_back(EditorUIBuilder::makeDivider());
}
