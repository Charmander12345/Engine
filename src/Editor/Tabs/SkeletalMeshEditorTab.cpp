#include "SkeletalMeshEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Renderer/UIWidgets/TreeViewWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../AssetManager/AssetTypes.h"
#include "../../Logger/Logger.h"
#include "../../Core/EngineLevel.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <algorithm>

using json = nlohmann::json;

SkeletalMeshEditorTab::SkeletalMeshEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

SkeletalMeshEditorTab::~SkeletalMeshEditorTab()
{
    if (m_state.isOpen)
        close();
}

void SkeletalMeshEditorTab::open()
{
    // Default open (no asset) – no-op. Use open(assetPath).
}

void SkeletalMeshEditorTab::open(const std::string& assetPath)
{
    if (!m_renderer || !m_ui)
        return;

    const std::string tabId = "SkeletalMeshEditor";

    if (m_state.isOpen && m_state.assetPath == assetPath)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    if (m_state.isOpen)
        close();

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
        m_ui->showToastMessage("Skeletal mesh asset not found: " + assetPath, UIManager::kToastMedium);
        return;
    }

    std::ifstream in(absPath, std::ios::binary);
    if (!in.is_open())
    {
        m_ui->showToastMessage("Failed to open skeletal mesh asset.", UIManager::kToastMedium);
        return;
    }

    json fileJson = json::parse(in, nullptr, false);
    in.close();
    if (fileJson.is_discarded() || !fileJson.contains("data"))
    {
        m_ui->showToastMessage("Invalid skeletal mesh asset format.", UIManager::kToastMedium);
        return;
    }

    const std::string widgetId = "SkeletalMeshEditor.Main";
    m_ui->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId     = tabId;
    m_state.widgetId  = widgetId;
    m_state.assetPath = assetPath;
    m_state.assetName = fileJson.value("name", std::filesystem::path(assetPath).stem().string());
    m_state.meshData  = fileJson["data"];
    m_state.isOpen    = true;

    parseSkeleton();
    rebuildPreviewLevel();

    m_renderer->addTab(tabId, "Skeletal Mesh", true);
    m_renderer->setActiveTab(tabId);
    pushSkeletonOverlay();

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
        root.id          = "SkeletalMeshEditor.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent – 3D viewport shows through
        root.hitTestMode = HitTestMode::DisabledSelf;        // root itself doesn't block, but children may
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

        buildBodyPanel(root);

        widget->setElements({ std::move(root) });
        m_ui->registerWidget(widgetId, widget, tabId);
    }

    const std::string tabBtnId   = "TitleBar.Tab."      + tabId;
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

    refresh();
}

void SkeletalMeshEditorTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    const std::string tabId = m_state.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_renderer->setSkeletonTabOverlay("", "");
    m_ui->unregisterWidget(m_state.widgetId);
    m_renderer->removeTab(tabId);
    m_runtimeLevel.reset();
    m_skeleton = Skeleton{};
    m_state = {};
    m_ui->markAllWidgetsDirty();
}

void SkeletalMeshEditorTab::update(float deltaSeconds)
{
    if (!m_state.isOpen) return;
    m_state.refreshTimer += deltaSeconds;
    if (m_state.refreshTimer >= 0.5f)
        m_state.refreshTimer = 0.0f;
}

void SkeletalMeshEditorTab::refresh()
{
    if (!m_state.isOpen || !m_ui) return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget) return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty()) return;

    auto& root = elements[0];

    if (auto* titleEl = m_ui->findElementById("SkeletalMeshEditor.Title"))
        titleEl->text = "Skeletal Mesh: " + m_state.assetName;

    // Rebuild body panel from scratch
    for (auto it = root.children.begin(); it != root.children.end(); ++it)
    {
        if (it->id == "SkeletalMeshEditor.Body")
        {
            root.children.erase(it);
            break;
        }
    }
    buildBodyPanel(root);
    entry->widget->markLayoutDirty();
}

void SkeletalMeshEditorTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "SkeletalMeshEditor.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
    toolbar.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 1.0f };
    toolbar.runtimeOnly = true;

    {
        WidgetElement title{};
        title.id              = "SkeletalMeshEditor.Title";
        title.type            = WidgetElementType::Text;
        title.text            = "Skeletal Mesh: " + m_state.assetName;
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.textAlignH      = TextAlignH::Left;
        title.textAlignV      = TextAlignV::Center;
        title.style.textColor = theme.textPrimary;
        title.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, 24.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    root.children.push_back(std::move(toolbar));
}

void SkeletalMeshEditorTab::buildBodyPanel(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement body{};
    body.id          = "SkeletalMeshEditor.Body";
    body.type        = WidgetElementType::StackPanel;
    body.fillX       = true;
    body.fillY       = true;
    body.orientation = StackOrientation::Horizontal;
    body.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent – 3D viewport shows through
    body.hitTestMode = HitTestMode::DisabledSelf;       // children opt-in individually
    body.runtimeOnly = true;

    // Left panel: Bone hierarchy tree view
    {
        WidgetElement leftPanel{};
        leftPanel.id          = "SkeletalMeshEditor.LeftPanel";
        leftPanel.type        = WidgetElementType::StackPanel;
        leftPanel.fillY       = true;
        leftPanel.minSize     = EditorTheme::Scaled(Vec2{ 200.0f, 0.0f });
        leftPanel.maxSize     = EditorTheme::Scaled(Vec2{ 300.0f, 99999.0f });
        leftPanel.orientation = StackOrientation::Vertical;
        leftPanel.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 6.0f });
        leftPanel.spacing     = EditorTheme::Scaled(4.0f);
        leftPanel.style.color = Vec4{ 0.07f, 0.08f, 0.10f, 1.0f };
        leftPanel.runtimeOnly = true;

        // Title
        {
            WidgetElement title{};
            title.type            = WidgetElementType::Text;
            title.text            = "Bones";
            title.font            = theme.fontDefault;
            title.fontSize        = theme.fontSizeSubheading;
            title.style.textColor = theme.textPrimary;
            title.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
            title.runtimeOnly     = true;
            leftPanel.children.push_back(std::move(title));
        }

        // Build bone tree
        {
            std::vector<TreeViewNode> boneNodes;
            buildBoneHierarchy(boneNodes);

            WidgetElement treeContainer{};
            treeContainer.type        = WidgetElementType::StackPanel;
            treeContainer.fillX       = true;
            treeContainer.fillY       = true;
            treeContainer.scrollable  = true;
            treeContainer.orientation = StackOrientation::Vertical;
            treeContainer.padding     = EditorTheme::Scaled(Vec2{ 2.0f, 2.0f });
            treeContainer.style.color = Vec4{ 0.06f, 0.07f, 0.09f, 1.0f };
            treeContainer.runtimeOnly = true;

            for (const auto& node : boneNodes)
            {
                std::function<void(std::vector<WidgetElement>&, const TreeViewNode&, int)> addNode =
                    [&](std::vector<WidgetElement>& container, const TreeViewNode& n, int depth)
                {
                    WidgetElement item{};
                    item.type        = WidgetElementType::Button;
                    item.id          = "SkeletalMeshEditor.Bone." + n.id;
                    item.fillX       = true;
                    item.text        = std::string(depth * 2, ' ') + (n.children.empty() ? "• " : "► ") + n.label;
                    item.font        = theme.fontDefault;
                    item.fontSize    = theme.fontSizeBody;
                    item.textAlignH  = TextAlignH::Left;
                    item.textAlignV  = TextAlignV::Center;
                    item.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
                    item.padding     = EditorTheme::Scaled(Vec2{ 4.0f + static_cast<float>(depth) * 8.0f, 2.0f });
                    const bool isSel = (n.id == m_state.selectedBoneId);
                    item.style.color      = isSel ? Vec4{ 0.80f, 0.40f, 0.0f, 0.85f }
                                                  : Vec4{ 0.12f, 0.13f, 0.15f, 1.0f };
                    item.style.hoverColor = isSel ? Vec4{ 0.90f, 0.50f, 0.0f, 0.90f }
                                                  : Vec4{ 0.18f, 0.19f, 0.21f, 1.0f };
                    item.style.textColor  = isSel ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                                                  : theme.textPrimary;
                    item.runtimeOnly = true;
                    item.hitTestMode = HitTestMode::Enabled;

                    auto boneId = n.id;
                    item.onClicked = [this, boneId]() { onBoneSelected(boneId); };

                    container.push_back(std::move(item));

                    for (const auto& child : n.children)
                        addNode(container, child, depth + 1);
                };

                addNode(treeContainer.children, node, 0);
            }

            leftPanel.children.push_back(std::move(treeContainer));
        }

        body.children.push_back(std::move(leftPanel));
    }

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillY       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 0.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        body.children.push_back(std::move(sep));
    }

    // Middle panel: 3D Preview
    {
        WidgetElement previewPanel{};
        previewPanel.id          = "SkeletalMeshEditor.PreviewPanel";
        previewPanel.type        = WidgetElementType::Panel;
        previewPanel.fillX       = true;
        previewPanel.fillY       = true;
        previewPanel.minSize     = EditorTheme::Scaled(Vec2{ 300.0f, 0.0f });
        previewPanel.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; // transparent – 3D render shows through
        previewPanel.hitTestMode = HitTestMode::DisabledAll;       // never blocks viewport input
        previewPanel.runtimeOnly = true;
        body.children.push_back(std::move(previewPanel));
    }

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillY       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 0.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        body.children.push_back(std::move(sep));
    }

    // Right panel: Stats and animations
    {
        WidgetElement rightPanel{};
        rightPanel.id          = "SkeletalMeshEditor.RightPanel";
        rightPanel.type        = WidgetElementType::StackPanel;
        rightPanel.fillY       = true;
        rightPanel.minSize     = EditorTheme::Scaled(Vec2{ 250.0f, 0.0f });
        rightPanel.maxSize     = EditorTheme::Scaled(Vec2{ 400.0f, 99999.0f });
        rightPanel.scrollable  = true;
        rightPanel.orientation = StackOrientation::Vertical;
        rightPanel.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
        rightPanel.spacing     = EditorTheme::Scaled(6.0f);
        rightPanel.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
        rightPanel.runtimeOnly = true;

        buildStatsSection(rightPanel);
        buildAnimationsSection(rightPanel);

        body.children.push_back(std::move(rightPanel));
    }

    root.children.push_back(std::move(body));
}

void SkeletalMeshEditorTab::buildBoneHierarchy(std::vector<TreeViewNode>& nodes)
{
    const json& d = m_state.meshData;

    if (!d.contains("m_bones") || !d["m_bones"].is_array() || d["m_bones"].empty())
    {
        return;
    }

    const auto& bones = d["m_bones"];

    // Build parent index map from m_nodes
    std::unordered_map<int, int> boneParent;
    if (d.contains("m_nodes") && d["m_nodes"].is_array())
    {
        const auto& nodeList = d["m_nodes"];
        std::vector<int> parentOf(nodeList.size(), -1);
        std::vector<int> boneOf(nodeList.size(), -1);
        for (size_t i = 0; i < nodeList.size(); ++i)
        {
            const auto& n = nodeList[i];
            parentOf[i] = n.value("parent", -1);
            boneOf[i]   = n.value("boneIndex", -1);
        }
        for (size_t i = 0; i < nodeList.size(); ++i)
        {
            const int bi = boneOf[i];
            if (bi < 0) continue;
            int p = parentOf[i];
            int pBone = -1;
            while (p >= 0)
            {
                if (boneOf[p] >= 0) { pBone = boneOf[p]; break; }
                p = parentOf[p];
            }
            boneParent[bi] = pBone;
        }
    }

    // Build tree structure
    std::vector<bool> hasParent(bones.size(), false);
    for (const auto& [child, parent] : boneParent)
    {
        if (parent >= 0) hasParent[child] = true;
    }

    std::function<void(int, std::vector<TreeViewNode>&)> buildSubtree =
        [&](int boneIdx, std::vector<TreeViewNode>& parentNodes)
    {
        TreeViewNode node{};
        node.id = std::to_string(boneIdx);
        node.label = bones[boneIdx].value("name", std::string{"<unnamed>"});
        node.isExpanded = true;

        // Find children
        for (size_t i = 0; i < bones.size(); ++i)
        {
            auto it = boneParent.find(static_cast<int>(i));
            if (it != boneParent.end() && it->second == boneIdx)
            {
                buildSubtree(static_cast<int>(i), node.children);
            }
        }

        parentNodes.push_back(std::move(node));
    };

    // Find root bones (those without parents)
    for (size_t i = 0; i < bones.size(); ++i)
    {
        if (!hasParent[i])
        {
            buildSubtree(static_cast<int>(i), nodes);
        }
    }
}

void SkeletalMeshEditorTab::buildStatsSection(WidgetElement& parent)
{
    const json& d = m_state.meshData;
    const auto& theme = EditorTheme::Get();

    {
        WidgetElement h{};
        h.type            = WidgetElementType::Text;
        h.text            = "Mesh Statistics";
        h.font            = theme.fontDefault;
        h.fontSize        = theme.fontSizeSubheading;
        h.style.textColor = theme.textPrimary;
        h.fillX           = true;
        h.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        h.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        h.runtimeOnly     = true;
        parent.children.push_back(std::move(h));
    }

    size_t vertCount = 0;
    if (d.contains("m_vertices") && d["m_vertices"].is_array())
    {
        vertCount = d["m_vertices"].size() / 5;
    }

    size_t indexCount = 0;
    if (d.contains("m_indices") && d["m_indices"].is_array())
        indexCount = d["m_indices"].size();

    size_t subMeshCount = 0;
    if (d.contains("m_subMeshes") && d["m_subMeshes"].is_array())
        subMeshCount = d["m_subMeshes"].size();

    const bool hasBones = d.value("m_hasBones", false);
    const size_t boneCount = (d.contains("m_bones") && d["m_bones"].is_array())
        ? d["m_bones"].size() : 0;

    const size_t boneIdsLen = (d.contains("m_boneIds") && d["m_boneIds"].is_array())
        ? d["m_boneIds"].size() : 0;

    auto makeLine = [&](const std::string& text, bool muted = false) -> WidgetElement
    {
        WidgetElement t{};
        t.type            = WidgetElementType::Text;
        t.text            = text;
        t.font            = theme.fontDefault;
        t.fontSize        = theme.fontSizeBody;
        t.style.textColor = muted ? theme.textMuted : theme.textPrimary;
        t.fillX           = true;
        t.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        t.runtimeOnly     = true;
        return t;
    };

    parent.children.push_back(makeLine("Path: " + m_state.assetPath, true));
    parent.children.push_back(makeLine("Vertices: " + std::to_string(vertCount)));
    parent.children.push_back(makeLine("Indices: " + std::to_string(indexCount)));
    parent.children.push_back(makeLine("Sub-Meshes: " + std::to_string(subMeshCount)));

    const std::string bonesLine = "Skeletal: " + std::string(hasBones ? "Yes" : "No")
        + " (" + std::to_string(boneCount) + " bones, "
        + std::to_string(boneIdsLen / 4) + " skinned verts)";
    parent.children.push_back(makeLine(bonesLine));
}

void SkeletalMeshEditorTab::buildAnimationsSection(WidgetElement& parent)
{
    const json& d = m_state.meshData;
    const auto& theme = EditorTheme::Get();

    {
        WidgetElement h{};
        h.type            = WidgetElementType::Text;
        h.text            = "Animations";
        h.font            = theme.fontDefault;
        h.fontSize        = theme.fontSizeSubheading;
        h.style.textColor = theme.textPrimary;
        h.fillX           = true;
        h.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        h.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        h.runtimeOnly     = true;
        parent.children.push_back(std::move(h));
    }

    if (!d.contains("m_animations") || !d["m_animations"].is_array() || d["m_animations"].empty())
    {
        WidgetElement t{};
        t.type            = WidgetElementType::Text;
        t.text            = "(none)";
        t.font            = theme.fontDefault;
        t.fontSize        = theme.fontSizeBody;
        t.style.textColor = theme.textMuted;
        t.fillX           = true;
        t.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        t.runtimeOnly     = true;
        parent.children.push_back(std::move(t));
        return;
    }

    const auto& anims = d["m_animations"];
    for (size_t i = 0; i < anims.size(); ++i)
    {
        const auto& a = anims[i];
        const std::string name = a.value("name", std::string{"<unnamed>"});
        const double duration = a.value("duration", 0.0);
        const double tps = a.value("ticksPerSecond", 25.0);
        const size_t channelCount = (a.contains("channels") && a["channels"].is_array())
            ? a["channels"].size() : 0;

        const double seconds = (tps > 0.0) ? (duration / tps) : 0.0;

        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s (%.2fs, %zu channels)",
            name.c_str(), seconds, channelCount);

        WidgetElement t{};
        t.type            = WidgetElementType::Text;
        t.text            = buf;
        t.font            = theme.fontDefault;
        t.fontSize        = theme.fontSizeBody;
        t.style.textColor = theme.textPrimary;
        t.fillX           = true;
        t.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        t.runtimeOnly     = true;
        parent.children.push_back(std::move(t));
    }
}

void SkeletalMeshEditorTab::onBoneSelected(const std::string& boneId)
{
    // Toggle: clicking the same bone deselects it
    if (m_state.selectedBoneId == boneId)
        m_state.selectedBoneId.clear();
    else
        m_state.selectedBoneId = boneId;

    pushSkeletonOverlay();
    refresh();
}

void SkeletalMeshEditorTab::parseSkeleton()
{
    m_skeleton = Skeleton{};
    const auto& data = m_state.meshData;
    if (!data.is_object() || !data.contains("m_bones"))
        return;

    // Bones
    const auto& bonesJson = data.at("m_bones");
    for (const auto& bj : bonesJson)
    {
        BoneInfo bone;
        bone.name = bj.value("name", "");
        bone.parentIndex = -1; // filled from node hierarchy below
        if (bj.contains("offsetMatrix"))
        {
            const auto& om = bj.at("offsetMatrix");
            for (int i = 0; i < 16 && i < static_cast<int>(om.size()); ++i)
                bone.offsetMatrix.m[i] = om[i].get<float>();
        }
        m_skeleton.boneNameToIndex[bone.name] = static_cast<int>(m_skeleton.bones.size());
        m_skeleton.bones.push_back(std::move(bone));
    }

    // Node hierarchy – used to resolve parentIndex per bone
    if (data.contains("m_nodes"))
    {
        for (const auto& nj : data.at("m_nodes"))
        {
            const std::string nodeName  = nj.value("name", "");
            const int         parentIdx = nj.value("parent", -1);
            const int         boneIdx   = nj.value("boneIndex", -1);
            if (boneIdx >= 0 && boneIdx < static_cast<int>(m_skeleton.bones.size()))
            {
                // Resolve parent bone index from parent node's boneIndex
                if (parentIdx >= 0 && data.contains("m_nodes"))
                {
                    const auto& nodes = data.at("m_nodes");
                    if (parentIdx < static_cast<int>(nodes.size()))
                    {
                        const int parentBone = nodes[parentIdx].value("boneIndex", -1);
                        m_skeleton.bones[boneIdx].parentIndex = parentBone;
                    }
                }
            }
        }
    }
}

void SkeletalMeshEditorTab::pushSkeletonOverlay()
{
    if (!m_renderer) return;
    m_renderer->setSkeletonTabOverlay(m_state.assetPath, m_state.selectedBoneId);
}

void SkeletalMeshEditorTab::rebuildPreviewLevel()
{
    auto& diag = DiagnosticsManager::Instance();
    const bool isActiveTab = m_state.isOpen && m_renderer &&
                             m_renderer->getActiveTabId() == m_state.tabId;
    if (isActiveTab)
    {
        m_previewCamPos = m_renderer->getCameraPosition();
        m_previewCamRot = m_renderer->getCameraRotationDegrees();
    }

    m_runtimeLevel.reset();

    if (m_state.assetPath.empty())
        return;

    auto& assetMgr = AssetManager::Instance();
    std::string materialPath;
    {
        auto meshAsset = assetMgr.getLoadedAssetByPath(m_state.assetPath);
        if (!meshAsset)
        {
            const std::string resolvedPath = assetMgr.getAbsoluteContentPath(m_state.assetPath);
            if (!resolvedPath.empty())
                meshAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
        }
        if (meshAsset)
        {
            const auto& data = meshAsset->getData();
            if (data.contains("m_materialAssetPaths") && data["m_materialAssetPaths"].is_array()
                && !data["m_materialAssetPaths"].empty())
            {
                materialPath = data["m_materialAssetPaths"][0].get<std::string>();
            }
        }
    }

    m_runtimeLevel = std::make_unique<EngineLevel>();
    m_runtimeLevel->setName("__SkeletalMeshViewer__");
    m_runtimeLevel->setAssetType(AssetType::Level);

    json entities = json::array();

    // Skeletal mesh entity
    {
        json meshEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, 0.0f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({1.0f, 1.0f, 1.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", m_state.assetPath} };
        if (!materialPath.empty())
        {
            comps["Material"] = json{ {"materialAssetPath", materialPath} };
        }
        comps["Name"] = json{ {"displayName", "SkeletalMeshPreview"} };
        meshEntity["components"] = std::move(comps);
        entities.push_back(std::move(meshEntity));
    }

    // Directional light
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

    // Ground plane
    {
        json groundEntity = json::object();
        json comps = json::object();
        comps["Transform"] = json{
            {"position", json::array({0.0f, -0.5f, 0.0f})},
            {"rotation", json::array({0.0f, 0.0f, 0.0f})},
            {"scale",    json::array({20.0f, 0.01f, 20.0f})}
        };
        comps["Mesh"] = json{ {"meshAssetPath", "default_quad3d.asset"} };
        comps["Material"] = json{ {"materialAssetPath", "Materials/WorldGrid.asset"} };
        comps["Name"] = json{ {"displayName", "PreviewGround"} };
        groundEntity["components"] = std::move(comps);
        entities.push_back(std::move(groundEntity));
    }

    json levelData = json::object();
    levelData["Entities"] = std::move(entities);
    levelData["EditorCamera"] = json{
        {"position", json::array({m_previewCamPos.x, m_previewCamPos.y, m_previewCamPos.z})},
        {"rotation", json::array({m_previewCamRot.x, m_previewCamRot.y})}
    };

    m_runtimeLevel->setLevelData(levelData);
    m_runtimeLevel->setEditorCameraPosition(m_previewCamPos);
    m_runtimeLevel->setEditorCameraRotation(m_previewCamRot);
    m_runtimeLevel->setHasEditorCamera(true);

    // If this tab is already active, swap the new level in immediately
    if (isActiveTab)
    {
        m_runtimeLevel->resetPreparedState();
        auto old = diag.swapActiveLevel(std::move(m_runtimeLevel));
        diag.setScenePrepared(false);
        // old level discarded
    }
}

std::unique_ptr<EngineLevel> SkeletalMeshEditorTab::takeRuntimeLevel()
{
    return std::move(m_runtimeLevel);
}

void SkeletalMeshEditorTab::giveRuntimeLevel(std::unique_ptr<EngineLevel> level)
{
    m_runtimeLevel = std::move(level);
}
