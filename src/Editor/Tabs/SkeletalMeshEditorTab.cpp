#include "SkeletalMeshEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../AssetManager/AssetTypes.h"
#include "../../Logger/Logger.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;

SkeletalMeshEditorTab::SkeletalMeshEditorTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

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

    // The asset file is JSON (magic/version/type/name/data). We only need
    // the data object for inspection here.
    json fileJson = json::parse(in, nullptr, false);
    in.close();
    if (fileJson.is_discarded() || !fileJson.contains("data"))
    {
        m_ui->showToastMessage("Invalid skeletal mesh asset format.", UIManager::kToastMedium);
        return;
    }

    m_renderer->addTab(tabId, "Skeletal Mesh", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "SkeletalMeshEditor.Main";
    m_ui->unregisterWidget(widgetId);

    m_state = {};
    m_state.tabId     = tabId;
    m_state.widgetId  = widgetId;
    m_state.assetPath = assetPath;
    m_state.assetName = fileJson.value("name", std::filesystem::path(assetPath).stem().string());
    m_state.meshData  = fileJson["data"];
    m_state.isOpen    = true;

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

    m_ui->unregisterWidget(m_state.widgetId);
    m_renderer->removeTab(tabId);
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

    // Rebuild body panel from scratch.
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
    WidgetElement body{};
    body.id          = "SkeletalMeshEditor.Body";
    body.type        = WidgetElementType::StackPanel;
    body.fillX       = true;
    body.fillY       = true;
    body.scrollable  = true;
    body.orientation = StackOrientation::Vertical;
    body.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
    body.spacing     = EditorTheme::Scaled(6.0f);
    body.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
    body.runtimeOnly = true;

    buildStatsSection(body);
    buildBonesSection(body);
    buildAnimationsSection(body);

    root.children.push_back(std::move(body));
}

namespace {
    WidgetElement makeHeading(const std::string& id, const std::string& text)
    {
        const auto& theme = EditorTheme::Get();
        WidgetElement h{};
        h.id              = id;
        h.type            = WidgetElementType::Text;
        h.text            = text;
        h.font            = theme.fontDefault;
        h.fontSize        = theme.fontSizeSubheading;
        h.style.textColor = theme.textPrimary;
        h.fillX           = true;
        h.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        h.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        h.runtimeOnly     = true;
        return h;
    }

    WidgetElement makeLine(const std::string& id, const std::string& text, bool muted = false)
    {
        const auto& theme = EditorTheme::Get();
        WidgetElement t{};
        t.id              = id;
        t.type            = WidgetElementType::Text;
        t.text            = text;
        t.font            = theme.fontDefault;
        t.fontSize        = theme.fontSizeBody;
        t.style.textColor = muted ? theme.textMuted : theme.textPrimary;
        t.fillX           = true;
        t.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        t.runtimeOnly     = true;
        return t;
    }
}

void SkeletalMeshEditorTab::buildStatsSection(WidgetElement& parent)
{
    const json& d = m_state.meshData;

    parent.children.push_back(makeHeading("SkeletalMeshEditor.StatsHeading", "Mesh"));

    size_t vertCount = 0;
    if (d.contains("m_vertices") && d["m_vertices"].is_array())
    {
        // Vertices are stored as a flat float array (pos3 + uv2 = 5 floats).
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
    const size_t boneWeightsLen = (d.contains("m_boneWeights") && d["m_boneWeights"].is_array())
        ? d["m_boneWeights"].size() : 0;

    parent.children.push_back(makeLine("SkeletalMeshEditor.Stats.Path", "Path: " + m_state.assetPath, true));
    parent.children.push_back(makeLine("SkeletalMeshEditor.Stats.Verts", "Vertices: " + std::to_string(vertCount)));
    parent.children.push_back(makeLine("SkeletalMeshEditor.Stats.Indices", "Indices: " + std::to_string(indexCount)));
    parent.children.push_back(makeLine("SkeletalMeshEditor.Stats.Sub", "Sub-meshes: " + std::to_string(subMeshCount)));

    const std::string bonesLine = "Skeletal: " + std::string(hasBones ? "yes" : "no")
        + " (" + std::to_string(boneCount) + " bones, "
        + std::to_string(boneIdsLen / 4) + " skinned verts)";
    parent.children.push_back(makeLine("SkeletalMeshEditor.Stats.Bones", bonesLine));

    if (hasBones && boneIdsLen != boneWeightsLen)
    {
        parent.children.push_back(makeLine(
            "SkeletalMeshEditor.Stats.Warn",
            "Warning: bone ids/weights arrays size mismatch.",
            true));
    }
}

void SkeletalMeshEditorTab::buildBonesSection(WidgetElement& parent)
{
    const json& d = m_state.meshData;

    parent.children.push_back(makeHeading("SkeletalMeshEditor.BonesHeading", "Bones"));

    if (!d.contains("m_bones") || !d["m_bones"].is_array() || d["m_bones"].empty())
    {
        parent.children.push_back(makeLine(
            "SkeletalMeshEditor.Bones.Empty",
            "No bones stored in this mesh asset.",
            true));
        return;
    }

    // Build parent index map from m_nodes (if present). Each node references
    // its bone via "boneIndex". We derive a bone's parent by walking up the
    // node hierarchy until another node with a boneIndex != -1 is found.
    std::unordered_map<int, int> boneParent; // boneIndex -> parentBoneIndex
    if (d.contains("m_nodes") && d["m_nodes"].is_array())
    {
        const auto& nodes = d["m_nodes"];
        std::vector<int> parentOf(nodes.size(), -1);
        std::vector<int> boneOf(nodes.size(), -1);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& n = nodes[i];
            parentOf[i] = n.value("parent", -1);
            boneOf[i]   = n.value("boneIndex", -1);
        }
        for (size_t i = 0; i < nodes.size(); ++i)
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

    const auto& bones = d["m_bones"];
    for (size_t i = 0; i < bones.size(); ++i)
    {
        const auto& b = bones[i];
        const std::string boneName = b.value("name", std::string{"<unnamed>"});

        int parentBi = -1;
        auto it = boneParent.find(static_cast<int>(i));
        if (it != boneParent.end()) parentBi = it->second;

        std::string line = "[" + std::to_string(i) + "] " + boneName;
        if (parentBi >= 0 && parentBi < static_cast<int>(bones.size()))
        {
            const std::string parentName = bones[parentBi].value("name", std::string{"?"});
            line += "  ← parent: " + parentName + " (" + std::to_string(parentBi) + ")";
        }
        else
        {
            line += "  (root)";
        }

        parent.children.push_back(makeLine(
            "SkeletalMeshEditor.Bone." + std::to_string(i),
            line));
    }
}

void SkeletalMeshEditorTab::buildAnimationsSection(WidgetElement& parent)
{
    const json& d = m_state.meshData;

    parent.children.push_back(makeHeading("SkeletalMeshEditor.AnimsHeading", "Animations"));

    if (!d.contains("m_animations") || !d["m_animations"].is_array() || d["m_animations"].empty())
    {
        parent.children.push_back(makeLine(
            "SkeletalMeshEditor.Anims.Empty",
            "No animation clips stored in this mesh asset.",
            true));
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
            "[%zu] %s  duration=%.2fs (%.1f ticks @ %.1f tps)  channels=%zu",
            i, name.c_str(), seconds, duration, tps, channelCount);

        parent.children.push_back(makeLine(
            "SkeletalMeshEditor.Anim." + std::to_string(i),
            buf));
    }
}
