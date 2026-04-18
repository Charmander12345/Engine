#include "ActorEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"
#include "../../Core/Actor/ActorRegistry.h"
#include "../../Core/EngineLevel.h"
#include "../../Core/ECS/ECS.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>

using json = nlohmann::json;

// ÔöÇÔöÇ Helpers ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
static WidgetElement* findElementById(std::vector<WidgetElement>& elems, const std::string& id)
{
    for (auto& el : elems)
    {
        if (el.id == id) return &el;
        if (auto* found = findElementById(el.children, id)) return found;
    }
    return nullptr;
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
ActorEditorTab::ActorEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::open()
{
    // Default open with no asset ÔÇö no-op
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
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

    // Initialise state BEFORE addTab/setActiveTab so that the tab-switch
    // logic can find isOpen()==true, getTabId()=="ActorEditor", and a valid
    // runtime level for the level-swap.
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

    // Build the preview runtime level so it is available for the level-swap
    rebuildPreviewLevel();

    // Now create the tab and switch ÔÇö setActiveTab will find the actor tab
    // fully initialised and swap in the preview level.
    m_renderer->addTab(tabId, "Actor Editor", true);
    m_renderer->setActiveTab(tabId);

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
        root.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent ÔÇö viewport shows through
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

            // Left panel ÔÇö viewport area (just a transparent placeholder so the
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

            // Right sidebar ÔÇö section list + details
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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::update(float deltaSeconds)
{
    if (!m_state.isOpen)
        return;
    (void)deltaSeconds;

    // ÔöÇÔöÇ Gizmo ÔåÆ ActorAssetData sync ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
    // If the actor editor is the active tab, check if any preview entity
    // has been moved by the gizmo and write the transform back to the
    // in-memory ActorAssetData so the sidebar stays in sync.
    if (!m_renderer || m_renderer->getActiveTabId() != m_state.tabId)
        return;

    if (m_previewEntities.empty())
    {
        // Entities may not be ready yet (prepareEcs hasn't run).
        // Try to capture them from the level's entity list.
        auto& diag = DiagnosticsManager::Instance();
        EngineLevel* level = diag.getActiveLevelSoft();
        if (level)
        {
            const auto& ents = level->getEntities();
            if (!ents.empty())
            {
                // Order: root (if mesh), child actors depth-first, preview light (last).
                m_previewEntities.reserve(ents.size());
                for (auto e : ents)
                    m_previewEntities.push_back(static_cast<unsigned int>(e));
            }
        }
        return; // first frame after capture ÔÇö skip sync
    }

    auto& ecs = ECS::ECSManager::Instance();
    auto& ad  = m_state.actorData;

    // Index into m_previewEntities.  Order matches rebuildPreviewLevel:
    // [0] = root entity (only if ad.meshPath is non-empty)
    // [1..N] = child actors (depth-first)
    // [last]  = preview light ÔÇö skip
    size_t idx = 0;

    // --- Sync root entity transform ---
    if (!ad.meshPath.empty() && idx < m_previewEntities.size())
    {
        const auto entity = static_cast<ECS::Entity>(m_previewEntities[idx]);
        if (const auto* tc = ecs.getComponent<ECS::TransformComponent>(entity))
        {
            ad.rootPosition[0] = tc->position[0];
            ad.rootPosition[1] = tc->position[1];
            ad.rootPosition[2] = tc->position[2];
            ad.rootRotation[0] = tc->rotation[0];
            ad.rootRotation[1] = tc->rotation[1];
            ad.rootRotation[2] = tc->rotation[2];
            ad.rootScale[0]    = tc->scale[0];
            ad.rootScale[1]    = tc->scale[1];
            ad.rootScale[2]    = tc->scale[2];
        }
        ++idx;
    }

    // --- Sync child actors (depth-first, matching rebuildPreviewLevel order) ---
    std::function<void(std::vector<ChildActorEntry>&, float, float, float)> syncChildren;
    syncChildren = [&](std::vector<ChildActorEntry>& children, float px, float py, float pz)
    {
        for (auto& child : children)
        {
            if (idx >= m_previewEntities.size())
                return;

            const auto entity = static_cast<ECS::Entity>(m_previewEntities[idx]);
            if (const auto* tc = ecs.getComponent<ECS::TransformComponent>(entity))
            {
                // Entity world position ÔåÆ local offset relative to parent
                child.position[0] = tc->position[0] - px;
                child.position[1] = tc->position[1] - py;
                child.position[2] = tc->position[2] - pz;
                child.rotation[0] = tc->rotation[0];
                child.rotation[1] = tc->rotation[1];
                child.rotation[2] = tc->rotation[2];
                child.scale[0]    = tc->scale[0];
                child.scale[1]    = tc->scale[1];
                child.scale[2]    = tc->scale[2];
            }
            ++idx;

            if (!child.children.empty())
            {
                const float cx = px + child.position[0];
                const float cy = py + child.position[1];
                const float cz = pz + child.position[2];
                syncChildren(child.children, cx, cy, cz);
            }
        }
    };
    syncChildren(ad.childActors, 0.0f, 0.0f, 0.0f);
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
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
        buildSidebar(*sidebar);
    }

    entry->widget->markLayoutDirty();
    m_ui->markRenderDirty();
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildSidebar(WidgetElement& sidebar)
{
    buildIdentitySection(sidebar);
    sidebar.children.push_back(EditorUIBuilder::makeDivider());
    buildTransformSection(sidebar);
    sidebar.children.push_back(EditorUIBuilder::makeDivider());
    buildTickSettingsSection(sidebar);
    sidebar.children.push_back(EditorUIBuilder::makeDivider());
    buildVisualsSection(sidebar);
    sidebar.children.push_back(EditorUIBuilder::makeDivider());
    buildChildActorList(sidebar);
    sidebar.children.push_back(EditorUIBuilder::makeDivider());
    buildComponentsInfo(sidebar);
    sidebar.children.push_back(EditorUIBuilder::makeDivider());
    buildScriptDetails(sidebar);
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildIdentitySection(WidgetElement& parent)
{
    parent.children.push_back(EditorUIBuilder::makeHeading("Identity"));

    // Editable name
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        "ActorEditor.Name", "Name", m_state.actorData.name,
        [this](const std::string& val) {
            m_state.actorData.name = val;
            m_state.assetName = val;
            m_state.isDirty = true;
            refresh();
        }));

    // Editable tag
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        "ActorEditor.Tag", "Tag", m_state.actorData.tag,
        [this](const std::string& val) {
            m_state.actorData.tag = val;
            m_state.isDirty = true;
        }));

    // Actor class dropdown
    auto classNames = getActorClassNames();
    int selectedIdx = 0;
    for (int i = 0; i < static_cast<int>(classNames.size()); ++i)
    {
        if (classNames[i] == m_state.actorData.actorClass)
        {
            selectedIdx = i;
            break;
        }
    }

    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        "ActorEditor.ActorClass", "Class", classNames, selectedIdx,
        [this, classNames](int idx) {
            if (idx >= 0 && idx < static_cast<int>(classNames.size()))
            {
                m_state.actorData.actorClass = classNames[idx];
                m_state.isDirty = true;
                rebuildPreviewLevel();
                refresh();
            }
        }));
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildTransformSection(WidgetElement& parent)
{
    parent.children.push_back(EditorUIBuilder::makeHeading("Root Transform"));

    // Position
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        "ActorEditor.Pos", "Position", m_state.actorData.rootPosition,
        [this](int axis, float val) {
            m_state.actorData.rootPosition[axis] = val;
            m_state.isDirty = true;
            rebuildPreviewLevel();
        }));

    // Rotation
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        "ActorEditor.Rot", "Rotation", m_state.actorData.rootRotation,
        [this](int axis, float val) {
            m_state.actorData.rootRotation[axis] = val;
            m_state.isDirty = true;
            rebuildPreviewLevel();
        }));

    // Scale
    parent.children.push_back(EditorUIBuilder::makeVec3Row(
        "ActorEditor.Scl", "Scale", m_state.actorData.rootScale,
        [this](int axis, float val) {
            m_state.actorData.rootScale[axis] = val;
            m_state.isDirty = true;
            rebuildPreviewLevel();
        }));
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildTickSettingsSection(WidgetElement& parent)
{
    parent.children.push_back(EditorUIBuilder::makeHeading("Tick Settings"));

    // Can ever tick
    parent.children.push_back(EditorUIBuilder::makeCheckBox(
        "ActorEditor.CanEverTick", "Can Ever Tick", m_state.actorData.canEverTick,
        [this](bool val) {
            m_state.actorData.canEverTick = val;
            m_state.isDirty = true;
        }));

    // Tick group dropdown
    const std::vector<std::string> tickGroups = {
        "PrePhysics", "DuringPhysics", "PostPhysics", "PostUpdateWork"
    };
    int groupIdx = std::clamp(m_state.actorData.tickGroup, 0, 3);

    parent.children.push_back(EditorUIBuilder::makeDropDownRow(
        "ActorEditor.TickGroup", "Tick Group", tickGroups, groupIdx,
        [this](int idx) {
            m_state.actorData.tickGroup = idx;
            m_state.isDirty = true;
        }));

    // Tick interval
    parent.children.push_back(EditorUIBuilder::makeFloatRow(
        "ActorEditor.TickInterval", "Interval (s)", m_state.actorData.tickInterval,
        [this](float val) {
            m_state.actorData.tickInterval = std::max(0.0f, val);
            m_state.isDirty = true;
        }));
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildVisualsSection(WidgetElement& parent)
{
    parent.children.push_back(EditorUIBuilder::makeHeading("Visuals"));

    // Root mesh path
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        "ActorEditor.MeshPath", "Mesh", m_state.actorData.meshPath,
        [this](const std::string& val) {
            m_state.actorData.meshPath = val;
            m_state.isDirty = true;
            rebuildPreviewLevel();
        }));

    // Root material path
    parent.children.push_back(EditorUIBuilder::makeStringRow(
        "ActorEditor.MaterialPath", "Material", m_state.actorData.materialPath,
        [this](const std::string& val) {
            m_state.actorData.materialPath = val;
            m_state.isDirty = true;
            rebuildPreviewLevel();
        }));
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildChildActorList(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();

    parent.children.push_back(EditorUIBuilder::makeHeading(
        "Child Actors (" + std::to_string(m_state.actorData.childActors.size()) + ")"));

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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildComponentsInfo(WidgetElement& parent)
{
    parent.children.push_back(EditorUIBuilder::makeHeading("Default Components"));

    auto comps = getComponentsForClass(m_state.actorData.actorClass);
    if (comps.empty())
    {
        parent.children.push_back(EditorUIBuilder::makeSecondaryLabel("(no default components)"));
    }
    else
    {
        for (const auto& comp : comps)
            parent.children.push_back(EditorUIBuilder::makeLabel(comp));
    }
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::buildScriptDetails(WidgetElement& parent)
{
    const auto& theme = EditorTheme::Get();

    parent.children.push_back(EditorUIBuilder::makeHeading("Embedded Script"));

    // Script class name
    parent.children.push_back(EditorUIBuilder::makeLabel(
        "Class: " + (m_state.actorData.scriptClassName.empty() ? "(none)" : m_state.actorData.scriptClassName)));

    // Script header path
    parent.children.push_back(EditorUIBuilder::makeSecondaryLabel(
        "Header: " + (m_state.actorData.scriptHeaderPath.empty() ? "(auto-generated)" : m_state.actorData.scriptHeaderPath)));

    // Script cpp path
    parent.children.push_back(EditorUIBuilder::makeSecondaryLabel(
        "Source: " + (m_state.actorData.scriptCppPath.empty() ? "(auto-generated)" : m_state.actorData.scriptCppPath)));

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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
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

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
std::vector<std::string> ActorEditorTab::getActorClassNames() const
{
    return ActorRegistry::Instance().getRegisteredClassNames();
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
std::vector<std::string> ActorEditorTab::getComponentsForClass(const std::string& actorClass) const
{
    // Map known actor classes to their default component set.
    // This is static knowledge ÔÇö matches BuiltinActors.h beginPlay() definitions.
    if (actorClass == "StaticMeshActor")
        return { "StaticMeshComponent" };
    if (actorClass == "PointLightActor" || actorClass == "DirectionalLightActor" || actorClass == "SpotLightActor")
        return { "LightComponent" };
    if (actorClass == "CameraActor")
        return { "CameraComponent" };
    if (actorClass == "PhysicsActor")
        return { "StaticMeshComponent", "PhysicsBodyComponent" };
    if (actorClass == "CharacterActor")
        return { "StaticMeshComponent", "CharacterControllerComponent" };
    if (actorClass == "AudioActor")
        return { "AudioComponent" };
    if (actorClass == "ParticleActor")
        return { "ParticleComponent" };
    return {};
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
std::unique_ptr<EngineLevel> ActorEditorTab::takeRuntimeLevel()
{
    return std::move(m_runtimeLevel);
}

void ActorEditorTab::giveRuntimeLevel(std::unique_ptr<EngineLevel> level)
{
    m_runtimeLevel = std::move(level);
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
bool ActorEditorTab::handleViewportDrop(const std::string& payload, const Vec2& screenPos)
{
    (void)screenPos;
    if (!m_state.isOpen)
        return false;

    // Payload format: "<AssetType int>|<content-relative path>"
    const auto sep = payload.find('|');
    if (sep == std::string::npos)
        return false;

    const int typeInt = std::atoi(payload.substr(0, sep).c_str());
    const std::string relPath = payload.substr(sep + 1);
    const AssetType assetType = static_cast<AssetType>(typeInt);
    const std::string assetName = std::filesystem::path(relPath).stem().string();

    if (assetType == AssetType::Model3D)
    {
        // If the root has no mesh yet, assign to root; otherwise add as child.
        if (m_state.actorData.meshPath.empty())
        {
            m_state.actorData.meshPath = relPath;

            // Try to pick up the mesh's default material
            auto meshAsset = AssetManager::Instance().getLoadedAssetByPath(relPath);
            if (!meshAsset)
            {
                int id = AssetManager::Instance().loadAsset(relPath, AssetType::Model3D);
                if (id > 0)
                    meshAsset = AssetManager::Instance().getLoadedAssetByID(static_cast<unsigned int>(id));
            }
            if (meshAsset)
            {
                auto& ad = meshAsset->getData();
                if (ad.contains("m_materialAssetPaths") && ad["m_materialAssetPaths"].is_array() && !ad["m_materialAssetPaths"].empty())
                {
                    std::string mp = ad["m_materialAssetPaths"][0].get<std::string>();
                    if (!mp.empty())
                        m_state.actorData.materialPath = mp;
                }
            }

            if (m_ui)
                m_ui->showToastMessage("Set root mesh: " + assetName, UIManager::kToastMedium);
        }
        else
        {
            // Add as a new child actor with the dropped mesh
            ChildActorEntry entry;
            entry.actorClass = "StaticMeshActor";
            entry.name       = assetName;
            entry.meshPath   = relPath;
            entry.scale[0]   = 1.0f;
            entry.scale[1]   = 1.0f;
            entry.scale[2]   = 1.0f;

            // Try to pick up the mesh's default material
            auto meshAsset = AssetManager::Instance().getLoadedAssetByPath(relPath);
            if (!meshAsset)
            {
                int id = AssetManager::Instance().loadAsset(relPath, AssetType::Model3D);
                if (id > 0)
                    meshAsset = AssetManager::Instance().getLoadedAssetByID(static_cast<unsigned int>(id));
            }
            if (meshAsset)
            {
                auto& ad = meshAsset->getData();
                if (ad.contains("m_materialAssetPaths") && ad["m_materialAssetPaths"].is_array() && !ad["m_materialAssetPaths"].empty())
                {
                    std::string mp = ad["m_materialAssetPaths"][0].get<std::string>();
                    if (!mp.empty())
                        entry.materialPath = mp;
                }
            }

            m_state.actorData.childActors.push_back(std::move(entry));
            if (m_ui)
                m_ui->showToastMessage("Added child: " + assetName, UIManager::kToastMedium);
        }

        m_state.isDirty = true;
        rebuildPreviewLevel();
        refresh();
        return true;
    }

    if (assetType == AssetType::Material)
    {
        // Apply material to the root actor
        m_state.actorData.materialPath = relPath;
        m_state.isDirty = true;
        rebuildPreviewLevel();
        refresh();
        if (m_ui)
            m_ui->showToastMessage("Set root material: " + assetName, UIManager::kToastMedium);
        return true;
    }

    return false;
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
void ActorEditorTab::rebuildPreviewLevel()
{
    // If the actor editor tab is currently active, save the camera from
    // the renderer so we can restore it after the level swap.
    auto& diag = DiagnosticsManager::Instance();
    const bool isActiveTab = m_state.isOpen && m_renderer &&
                             m_renderer->getActiveTabId() == m_state.tabId;
    if (isActiveTab)
    {
        m_previewCamPos = m_renderer->getCameraPosition();
        m_previewCamRot = m_renderer->getCameraRotationDegrees();
    }

    m_previewEntities.clear();

    m_runtimeLevel.reset();
    m_runtimeLevel = std::make_unique<EngineLevel>();
    m_runtimeLevel->setName("__ActorPreview__");
    m_runtimeLevel->setAssetType(AssetType::Level);

    json entities = json::array();

    const auto& ad = m_state.actorData;

    // Root actor entity (mesh + material if set) ÔÇö uses root transform
    if (!ad.meshPath.empty())
    {
        json rootEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({ad.rootPosition[0], ad.rootPosition[1], ad.rootPosition[2]})},
            {"rotation", json::array({ad.rootRotation[0], ad.rootRotation[1], ad.rootRotation[2]})},
            {"scale",    json::array({ad.rootScale[0],    ad.rootScale[1],    ad.rootScale[2]})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", ad.meshPath} };
        if (!ad.materialPath.empty())
            comps["Material"] = json{ {"materialAssetPath", ad.materialPath} };
        comps["Name"] = json{ {"displayName", ad.name} };
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

    // If the actor editor is the active tab, swap the new level into
    // DiagnosticsManager so the renderer picks it up immediately.
    if (isActiveTab)
    {
        m_runtimeLevel->resetPreparedState();
        auto old = diag.swapActiveLevel(std::move(m_runtimeLevel));
        // old preview level is discarded
        diag.setScenePrepared(false);
    }
}