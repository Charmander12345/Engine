#include "ActorEditorTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"
#include "../../Core/Actor/ActorRegistry.h"
#include "../../Core/EngineLevel.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>

using json = nlohmann::json;

// ── Helpers ──────────────────────────────────────────────────────────────
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
ActorEditorTab::ActorEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::open()
{
    // Default open with no asset — no-op
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::open(const std::string& assetPath)
{
    if (!m_renderer || !m_ui)
        return;

    const std::string tabId = "ActorEditor";

    // If already open for this asset, just switch
    if (m_state.isOpen && m_state.assetPath == assetPath)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    // If open for a different asset, close first
    if (m_state.isOpen)
        close();

    // Load the actor asset from disk
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
        m_ui->showToastMessage("Actor asset not found: " + assetPath, UIManager::kToastMedium);
        return;
    }

    std::ifstream in(absPath);
    if (!in.is_open())
    {
        m_ui->showToastMessage("Failed to open actor asset.", UIManager::kToastMedium);
        return;
    }

    json fileJson = json::parse(in, nullptr, false);
    in.close();
    if (fileJson.is_discarded() || !fileJson.contains("data"))
    {
        m_ui->showToastMessage("Invalid actor asset format.", UIManager::kToastMedium);
        return;
    }

    m_renderer->addTab(tabId, "Actor Editor", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "ActorEditor.Main";
    m_ui->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId     = tabId;
    m_state.widgetId  = widgetId;
    m_state.assetPath = assetPath;
    m_state.assetName = fileJson.value("name", std::filesystem::path(assetPath).stem().string());
    m_state.isOpen    = true;
    m_state.isDirty   = false;

    // Parse the actor data
    m_state.actorData = ActorAssetData::fromJson(fileJson["data"]);
    m_state.actorData.name = m_state.assetName;

    // Default section selection
    m_state.selectedSection = "ActorClass";

    // Build the preview runtime level
    rebuildPreviewLevel();

    // Build the main widget (sidebar overlay on the right)
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
        root.id          = "ActorEditor.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent — viewport shows through
        root.runtimeOnly = true;

        // Toolbar at the top
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

        // Horizontal split: viewport (left fills) | sidebar (right fixed width)
        {
            WidgetElement splitRow{};
            splitRow.id          = "ActorEditor.SplitRow";
            splitRow.type        = WidgetElementType::StackPanel;
            splitRow.fillX       = true;
            splitRow.fillY       = true;
            splitRow.orientation = StackOrientation::Horizontal;
            splitRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            splitRow.runtimeOnly = true;

            // Left panel — viewport area (just a transparent placeholder so the
            // renderer's 3D pass shows through the tab FBO)
            {
                WidgetElement viewportPanel{};
                viewportPanel.id          = "ActorEditor.ViewportArea";
                viewportPanel.type        = WidgetElementType::Panel;
                viewportPanel.fillX       = true;
                viewportPanel.fillY       = true;
                viewportPanel.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent
                viewportPanel.runtimeOnly = true;
                splitRow.children.push_back(std::move(viewportPanel));
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

            // Right sidebar — section list + details
            {
                WidgetElement sidebar{};
                sidebar.id          = "ActorEditor.Sidebar";
                sidebar.type        = WidgetElementType::StackPanel;
                sidebar.fillY       = true;
                sidebar.scrollable  = true;
                sidebar.orientation = StackOrientation::Vertical;
                sidebar.minSize     = EditorTheme::Scaled(Vec2{ 280.0f, 0.0f });
                sidebar.maxSize     = EditorTheme::Scaled(Vec2{ 320.0f, 0.0f });
                sidebar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
                sidebar.style.color = Vec4{ 0.06f, 0.06f, 0.08f, 0.92f };
                sidebar.runtimeOnly = true;
                splitRow.children.push_back(std::move(sidebar));
            }

            root.children.push_back(std::move(splitRow));
        }

        widget->setElements({ std::move(root) });
        m_ui->registerWidget(widgetId, widget, tabId);
    }

    // Click events
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

    m_ui->registerClickEvent("ActorEditor.Save", [this]()
    {
        save();
    });

    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    const std::string tabId = m_state.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_ui->unregisterWidget(m_state.widgetId);
    m_renderer->removeTab(tabId);
    m_runtimeLevel.reset();
    m_state = {};
    m_ui->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;
    (void)deltaSeconds;
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::save()
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
    fileJson["type"]    = static_cast<int>(AssetType::ActorAsset);
    fileJson["name"]    = m_state.assetName;
    fileJson["data"]    = m_state.actorData.toJson();

    std::error_code ec;
    std::filesystem::create_directories(absPath.parent_path(), ec);

    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        m_ui->showToastMessage("Failed to save actor asset.", UIManager::kToastMedium);
        return;
    }

    out << fileJson.dump(4);
    out.close();

    m_state.isDirty = false;
    m_ui->showToastMessage("Actor saved: " + m_state.assetName, UIManager::kToastMedium);

    Logger::Instance().log(Logger::Category::AssetManagement,
        "Saved actor asset '" + m_state.assetName + "' to " + m_state.assetPath,
        Logger::LogLevel::INFO);

    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::refresh()
{
    if (!m_state.isOpen || !m_ui)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();

    // Update title
    if (auto* title = findElementById(elements, "ActorEditor.Title"))
        title->text = m_state.assetName + (m_state.isDirty ? " *" : "");

    // Populate sidebar
    if (auto* sidebar = findElementById(elements, "ActorEditor.Sidebar"))
    {
        sidebar->children.clear();
        buildSectionListPanel(*sidebar);

        // Separator
        const auto& theme = EditorTheme::Get();
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        sidebar->children.push_back(std::move(sep));

        buildDetailsPanel(*sidebar);
    }

    entry->widget->markLayoutDirty();
    m_ui->markRenderDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::selectSection(const std::string& section)
{
    m_state.selectedSection = section;
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "ActorEditor.Toolbar";
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
        title.type            = WidgetElementType::Text;
        title.id              = "ActorEditor.Title";
        title.text            = m_state.assetName + (m_state.isDirty ? " *" : "");
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = Vec4{ 0.55f, 0.85f, 1.00f, 1.0f };
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 160.0f, 28.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Save button
    toolbar.children.push_back(EditorUIBuilder::makePrimaryButton(
        "ActorEditor.Save", "Save", {}, EditorTheme::Scaled(Vec2{ 60.0f, 26.0f })));

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::buildSectionListPanel(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();

    // Section heading
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Sections";
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textMuted;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        heading.runtimeOnly     = true;
        parent.children.push_back(std::move(heading));
    }

    // Section entries — no Transform section (transform only in level)
    const std::vector<std::string> sections = { "ActorClass", "ChildActors", "Script" };

    for (const auto& section : sections)
    {
        const bool selected = (section == m_state.selectedSection);

        WidgetElement row{};
        row.id              = "ActorEditor.Section." + section;
        row.type            = WidgetElementType::Button;
        row.text            = section;
        row.fontSize        = theme.fontSizeBody;
        row.style.textColor = selected ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.textMuted;
        row.style.color     = selected ? Vec4{ 0.22f, 0.28f, 0.40f, 1.0f }
                                        : Vec4{ 0.10f, 0.10f, 0.12f, 0.0f };
        row.style.hoverColor = Vec4{ 0.18f, 0.22f, 0.30f, 1.0f };
        row.fillX           = true;
        row.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.hitTestMode     = HitTestMode::Enabled;
        row.runtimeOnly     = true;

        std::string sectionCopy = section;
        row.onClicked = [this, sectionCopy]() { selectSection(sectionCopy); };

        parent.children.push_back(std::move(row));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::buildDetailsPanel(WidgetElement& parent)
{
    if (m_state.selectedSection == "ActorClass")
        buildActorClassDetails(parent);
    else if (m_state.selectedSection == "ChildActors")
        buildChildActorList(parent);
    else if (m_state.selectedSection == "Script")
        buildScriptDetails(parent);
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::buildActorClassDetails(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();

    // Heading
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Actor Class";
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = Vec4{ 0.55f, 0.85f, 1.00f, 1.0f };
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        heading.runtimeOnly     = true;
        parent.children.push_back(std::move(heading));
    }

    // Current class label
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Class: " + (m_state.actorData.actorClass.empty() ? "(none)" : m_state.actorData.actorClass);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textPrimary;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Root mesh path
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Mesh: " + (m_state.actorData.meshPath.empty() ? "(none)" : m_state.actorData.meshPath);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textPrimary;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Root material path
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Material: " + (m_state.actorData.materialPath.empty() ? "(none)" : m_state.actorData.materialPath);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textPrimary;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Tag
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Tag: " + (m_state.actorData.tag.empty() ? "(none)" : m_state.actorData.tag);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textPrimary;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Available classes list as buttons
    {
        WidgetElement subHeading{};
        subHeading.type            = WidgetElementType::Text;
        subHeading.text            = "Available Classes";
        subHeading.fontSize        = theme.fontSizeBody;
        subHeading.style.textColor = theme.textMuted;
        subHeading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        subHeading.runtimeOnly     = true;
        parent.children.push_back(std::move(subHeading));
    }

    for (const auto& className : getActorClassNames())
    {
        const bool selected = (className == m_state.actorData.actorClass);
        WidgetElement btn{};
        btn.id              = "ActorEditor.Class." + className;
        btn.type            = WidgetElementType::Button;
        btn.text            = className;
        btn.fontSize        = theme.fontSizeBody;
        btn.style.textColor = selected ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f } : theme.textPrimary;
        btn.style.color     = selected ? Vec4{ 0.2f, 0.4f, 0.3f, 1.0f }
                                        : Vec4{ 0.12f, 0.13f, 0.16f, 1.0f };
        btn.style.hoverColor = Vec4{ 0.18f, 0.32f, 0.25f, 1.0f };
        btn.fillX           = true;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        btn.textAlignH      = TextAlignH::Left;
        btn.textAlignV      = TextAlignV::Center;
        btn.hitTestMode     = HitTestMode::Enabled;
        btn.runtimeOnly     = true;

        std::string cls = className;
        btn.onClicked = [this, cls]()
        {
            m_state.actorData.actorClass = cls;
            m_state.isDirty = true;
            rebuildPreviewLevel();
            refresh();
        };

        parent.children.push_back(std::move(btn));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::buildChildActorList(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();

    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Child Actors (" + std::to_string(m_state.actorData.childActors.size()) + ")";
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = Vec4{ 0.55f, 0.85f, 1.00f, 1.0f };
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        heading.runtimeOnly     = true;
        parent.children.push_back(std::move(heading));
    }

    for (size_t i = 0; i < m_state.actorData.childActors.size(); ++i)
    {
        const auto& child = m_state.actorData.childActors[i];

        WidgetElement row{};
        row.id              = "ActorEditor.Child." + std::to_string(i);
        row.type            = WidgetElementType::StackPanel;
        row.fillX           = true;
        row.orientation     = StackOrientation::Vertical;
        row.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        row.style.color     = Vec4{ 0.10f, 0.10f, 0.12f, 1.0f };
        row.runtimeOnly     = true;

        // Header row: name + class + remove button
        {
            WidgetElement headerRow{};
            headerRow.type        = WidgetElementType::StackPanel;
            headerRow.fillX       = true;
            headerRow.orientation = StackOrientation::Horizontal;
            headerRow.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
            headerRow.runtimeOnly = true;

            // Name/class label
            {
                WidgetElement label{};
                label.type            = WidgetElementType::Text;
                label.text            = (child.name.empty() ? child.actorClass : child.name);
                label.fontSize        = theme.fontSizeBody;
                label.style.textColor = theme.textPrimary;
                label.fillX           = true;
                label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
                label.textAlignV      = TextAlignV::Center;
                label.runtimeOnly     = true;
                headerRow.children.push_back(std::move(label));
            }

            // Remove button
            {
                WidgetElement removeBtn{};
                removeBtn.id              = "ActorEditor.RemoveChild." + std::to_string(i);
                removeBtn.type            = WidgetElementType::Button;
                removeBtn.text            = "X";
                removeBtn.fontSize        = theme.fontSizeBody;
                removeBtn.style.textColor = Vec4{ 1.0f, 0.4f, 0.4f, 1.0f };
                removeBtn.style.color     = Vec4{ 0.2f, 0.1f, 0.1f, 1.0f };
                removeBtn.style.hoverColor = Vec4{ 0.4f, 0.15f, 0.15f, 1.0f };
                removeBtn.minSize         = EditorTheme::Scaled(Vec2{ 24.0f, 24.0f });
                removeBtn.textAlignH      = TextAlignH::Center;
                removeBtn.textAlignV      = TextAlignV::Center;
                removeBtn.hitTestMode     = HitTestMode::Enabled;
                removeBtn.runtimeOnly     = true;

                size_t idx = i;
                removeBtn.onClicked = [this, idx]() { removeChildActor(idx); };
                headerRow.children.push_back(std::move(removeBtn));
            }

            row.children.push_back(std::move(headerRow));
        }

        // Info labels (mesh + material)
        if (!child.meshPath.empty())
        {
            WidgetElement meshLabel{};
            meshLabel.type            = WidgetElementType::Text;
            meshLabel.text            = "  Mesh: " + child.meshPath;
            meshLabel.fontSize        = theme.fontSizeSmall;
            meshLabel.style.textColor = theme.textMuted;
            meshLabel.fillX           = true;
            meshLabel.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
            meshLabel.runtimeOnly     = true;
            row.children.push_back(std::move(meshLabel));
        }
        if (!child.materialPath.empty())
        {
            WidgetElement matLabel{};
            matLabel.type            = WidgetElementType::Text;
            matLabel.text            = "  Material: " + child.materialPath;
            matLabel.fontSize        = theme.fontSizeSmall;
            matLabel.style.textColor = theme.textMuted;
            matLabel.fillX           = true;
            matLabel.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
            matLabel.runtimeOnly     = true;
            row.children.push_back(std::move(matLabel));
        }
        if (!child.children.empty())
        {
            WidgetElement childrenLabel{};
            childrenLabel.type            = WidgetElementType::Text;
            childrenLabel.text            = "  Children: " + std::to_string(child.children.size());
            childrenLabel.fontSize        = theme.fontSizeSmall;
            childrenLabel.style.textColor = theme.textMuted;
            childrenLabel.fillX           = true;
            childrenLabel.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
            childrenLabel.runtimeOnly     = true;
            row.children.push_back(std::move(childrenLabel));
        }

        parent.children.push_back(std::move(row));
    }

    // Add child actor buttons
    {
        WidgetElement addHeading{};
        addHeading.type            = WidgetElementType::Text;
        addHeading.text            = "Add Child Actor";
        addHeading.fontSize        = theme.fontSizeBody;
        addHeading.style.textColor = theme.textMuted;
        addHeading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        addHeading.runtimeOnly     = true;
        parent.children.push_back(std::move(addHeading));
    }

    const std::vector<std::pair<std::string, std::string>> availableActors = {
        { "StaticMeshActor",      "Static Mesh" },
        { "PointLightActor",      "Point Light" },
        { "DirectionalLightActor","Dir. Light" },
        { "SpotLightActor",       "Spot Light" },
        { "CameraActor",          "Camera" },
        { "AudioActor",           "Audio" },
        { "ParticleActor",        "Particle" },
        { "Actor",                "Empty Actor" },
    };

    for (const auto& [actorClass, displayName] : availableActors)
    {
        WidgetElement btn{};
        btn.id              = "ActorEditor.AddChild." + actorClass;
        btn.type            = WidgetElementType::Button;
        btn.text            = "+ " + displayName;
        btn.fontSize        = theme.fontSizeBody;
        btn.style.textColor = Vec4{ 0.5f, 0.9f, 0.5f, 1.0f };
        btn.style.color     = Vec4{ 0.10f, 0.14f, 0.10f, 1.0f };
        btn.style.hoverColor = Vec4{ 0.15f, 0.22f, 0.15f, 1.0f };
        btn.fillX           = true;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        btn.textAlignH      = TextAlignH::Left;
        btn.textAlignV      = TextAlignV::Center;
        btn.hitTestMode     = HitTestMode::Enabled;
        btn.runtimeOnly     = true;

        std::string cls = actorClass;
        btn.onClicked = [this, cls]() { addChildActor(cls); };
        parent.children.push_back(std::move(btn));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::buildScriptDetails(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();

    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Embedded Script";
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = Vec4{ 0.55f, 0.85f, 1.00f, 1.0f };
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        heading.runtimeOnly     = true;
        parent.children.push_back(std::move(heading));
    }

    // Script class name
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Class: " + (m_state.actorData.scriptClassName.empty() ? "(none)" : m_state.actorData.scriptClassName);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textPrimary;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Script header path
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Header: " + (m_state.actorData.scriptHeaderPath.empty() ? "(auto-generated)" : m_state.actorData.scriptHeaderPath);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textMuted;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Script cpp path
    {
        WidgetElement label{};
        label.type            = WidgetElementType::Text;
        label.text            = "Source: " + (m_state.actorData.scriptCppPath.empty() ? "(auto-generated)" : m_state.actorData.scriptCppPath);
        label.fontSize        = theme.fontSizeBody;
        label.style.textColor = theme.textMuted;
        label.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        label.runtimeOnly     = true;
        parent.children.push_back(std::move(label));
    }

    // Enabled toggle
    {
        WidgetElement enableBtn{};
        enableBtn.id              = "ActorEditor.ScriptToggle";
        enableBtn.type            = WidgetElementType::Button;
        enableBtn.text            = m_state.actorData.scriptEnabled ? "Script: Enabled" : "Script: Disabled";
        enableBtn.fontSize        = theme.fontSizeBody;
        enableBtn.style.textColor = m_state.actorData.scriptEnabled
            ? Vec4{ 0.5f, 1.0f, 0.5f, 1.0f }
            : Vec4{ 1.0f, 0.5f, 0.5f, 1.0f };
        enableBtn.style.color     = Vec4{ 0.12f, 0.13f, 0.16f, 1.0f };
        enableBtn.style.hoverColor = Vec4{ 0.18f, 0.20f, 0.25f, 1.0f };
        enableBtn.fillX           = true;
        enableBtn.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        enableBtn.textAlignH      = TextAlignH::Center;
        enableBtn.textAlignV      = TextAlignV::Center;
        enableBtn.hitTestMode     = HitTestMode::Enabled;
        enableBtn.runtimeOnly     = true;

        enableBtn.onClicked = [this]()
        {
            m_state.actorData.scriptEnabled = !m_state.actorData.scriptEnabled;
            m_state.isDirty = true;
            refresh();
        };

        parent.children.push_back(std::move(enableBtn));
    }
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::addChildActor(const std::string& actorClass)
{
    ChildActorEntry entry;
    entry.actorClass = actorClass;
    entry.name       = actorClass;
    entry.scale[0]   = 1.0f;
    entry.scale[1]   = 1.0f;
    entry.scale[2]   = 1.0f;
    m_state.actorData.childActors.push_back(std::move(entry));
    m_state.isDirty = true;
    rebuildPreviewLevel();
    refresh();
}

void ActorEditorTab::removeChildActor(size_t index)
{
    if (index < m_state.actorData.childActors.size())
    {
        m_state.actorData.childActors.erase(m_state.actorData.childActors.begin() + static_cast<ptrdiff_t>(index));
        m_state.isDirty = true;
        rebuildPreviewLevel();
        refresh();
    }
}

// ───────────────────────────────────────────────────────────────────────────
std::vector<std::string> ActorEditorTab::getActorClassNames() const
{
    return ActorRegistry::Instance().getRegisteredClassNames();
}

// ───────────────────────────────────────────────────────────────────────────
std::unique_ptr<EngineLevel> ActorEditorTab::takeRuntimeLevel()
{
    return std::move(m_runtimeLevel);
}

void ActorEditorTab::giveRuntimeLevel(std::unique_ptr<EngineLevel> level)
{
    m_runtimeLevel = std::move(level);
}

// ───────────────────────────────────────────────────────────────────────────
void ActorEditorTab::rebuildPreviewLevel()
{
    m_runtimeLevel.reset();
    m_runtimeLevel = std::make_unique<EngineLevel>();
    m_runtimeLevel->setName("__ActorPreview__");
    m_runtimeLevel->setAssetType(AssetType::Level);

    json entities = json::array();

    // Root actor entity (mesh + material if set)
    if (!m_state.actorData.meshPath.empty())
    {
        json rootEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, 0.0f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({1.0f, 1.0f, 1.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", m_state.actorData.meshPath} };
        if (!m_state.actorData.materialPath.empty())
            comps["Material"] = json{ {"materialAssetPath", m_state.actorData.materialPath} };
        comps["Name"] = json{ {"displayName", m_state.actorData.name} };
        rootEntity["components"] = std::move(comps);
        entities.push_back(std::move(rootEntity));
    }

    // Child actor entities (recursive helper via lambda)
    std::function<void(const std::vector<ChildActorEntry>&, float, float, float)> addChildren;
    addChildren = [&](const std::vector<ChildActorEntry>& children, float px, float py, float pz)
    {
        for (const auto& child : children)
        {
            const float cx = px + child.position[0];
            const float cy = py + child.position[1];
            const float cz = pz + child.position[2];

            json childEntity = json::object();
            json comps = json::object();
            comps["Transform"] = json{
                {"position", json::array({cx, cy, cz})},
                {"rotation", json::array({child.rotation[0], child.rotation[1], child.rotation[2]})},
                {"scale",    json::array({child.scale[0], child.scale[1], child.scale[2]})}
            };
            if (!child.meshPath.empty())
                comps["Mesh"] = json{ {"meshAssetPath", child.meshPath} };
            if (!child.materialPath.empty())
                comps["Material"] = json{ {"materialAssetPath", child.materialPath} };
            comps["Name"] = json{ {"displayName", child.name.empty() ? child.actorClass : child.name} };
            childEntity["components"] = std::move(comps);
            entities.push_back(std::move(childEntity));

            // Recurse for nested children
            if (!child.children.empty())
                addChildren(child.children, cx, cy, cz);
        }
    };
    addChildren(m_state.actorData.childActors, 0.0f, 0.0f, 0.0f);

    // Directional light for preview
    {
        json lightEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, 0.0f, 0.0f})},
            {"rotation", json::array({50.0f, 30.0f, 0.0f})},
            {"scale",    json::array({1.0f, 1.0f, 1.0f})}
        };
        comps["Light"] = json{
            {"type", 1},
            {"color", json::array({0.9f, 0.85f, 0.78f})},
            {"intensity", 0.8f},
            {"range", 100.0f},
            {"spotAngle", 0.0f}
        };
        comps["Name"] = json{ {"displayName", "PreviewLight"} };
        lightEntity["components"] = std::move(comps);
        entities.push_back(std::move(lightEntity));
    }

    json levelData = json::object();
    levelData["Entities"] = std::move(entities);

    // Camera
    levelData["EditorCamera"] = json{
        {"position", json::array({m_previewCamPos.x, m_previewCamPos.y, m_previewCamPos.z})},
        {"rotation", json::array({m_previewCamRot.x, m_previewCamRot.y})}
    };

    m_runtimeLevel->setLevelData(levelData);
    m_runtimeLevel->setEditorCameraPosition(m_previewCamPos);
    m_runtimeLevel->setEditorCameraRotation(m_previewCamRot);
    m_runtimeLevel->setHasEditorCamera(true);
}
