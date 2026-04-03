#include "ContentBrowserPanel.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Logger/Logger.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Core/ECS/ECS.h"
#include "../UIWidgets/DropdownButtonWidget.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <cctype>
#include <functional>
#include <cmath>

// ===========================================================================
// Static helpers (duplicated from UIManager.cpp for self-containment)
// ===========================================================================

static WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
{
    for (auto& el : elements)
    {
        if (el.id == id) return &el;
        std::function<WidgetElement*(std::vector<WidgetElement>&)> recurse =
            [&](std::vector<WidgetElement>& children) -> WidgetElement*
            {
                for (auto& child : children)
                {
                    if (child.id == id) return &child;
                    if (auto* found = recurse(child.children)) return found;
                }
                return nullptr;
            };
        if (auto* found = recurse(el.children)) return found;
    }
    return nullptr;
}

static const char* iconForAssetType(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture:  return "texture.png";
    case AssetType::Material: return "material.png";
    case AssetType::Model2D:  return "model2d.png";
    case AssetType::Model3D:  return "model3d.png";
    case AssetType::Audio:    return "sound.png";
    case AssetType::Script:       return "script.png";
    case AssetType::NativeScript: return "script.png";
    case AssetType::Shader:       return "shader.png";
    case AssetType::Widget:   return "widget.png";
    case AssetType::Skybox:   return "skybox.png";
    case AssetType::Level:    return "level.png";
    case AssetType::Prefab:   return "entity.png";
    case AssetType::Entity:   return "entity.png";
    case AssetType::InputAction:  return "entity.png";
    case AssetType::InputMapping: return "entity.png";
    default:                  return "entity.png";
    }
}

static Vec4 iconTintForAssetType(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture:  return Vec4{ 0.45f, 0.65f, 1.00f, 1.0f };
    case AssetType::Material: return Vec4{ 1.00f, 0.60f, 0.25f, 1.0f };
    case AssetType::Audio:    return Vec4{ 1.00f, 0.35f, 0.35f, 1.0f };
    case AssetType::Script:       return Vec4{ 0.40f, 0.90f, 0.40f, 1.0f };
    case AssetType::NativeScript: return Vec4{ 0.30f, 0.70f, 1.00f, 1.0f };
    case AssetType::Model2D:      return Vec4{ 0.55f, 0.85f, 0.95f, 1.0f };
    case AssetType::Model3D:  return Vec4{ 0.50f, 0.80f, 0.90f, 1.0f };
    case AssetType::Shader:   return Vec4{ 0.75f, 0.50f, 1.00f, 1.0f };
    case AssetType::Level:    return Vec4{ 0.95f, 0.85f, 0.40f, 1.0f };
    case AssetType::Widget:   return Vec4{ 0.70f, 0.70f, 0.90f, 1.0f };
    case AssetType::Skybox:   return Vec4{ 0.40f, 0.75f, 0.95f, 1.0f };
    case AssetType::Prefab:   return Vec4{ 0.30f, 0.90f, 0.70f, 1.0f };
    case AssetType::Entity:   return Vec4{ 0.85f, 0.55f, 1.00f, 1.0f };
    case AssetType::InputAction:  return Vec4{ 1.00f, 0.80f, 0.30f, 1.0f };
    case AssetType::InputMapping: return Vec4{ 0.30f, 1.00f, 0.80f, 1.0f };
    default:                  return Vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
    }
}

static WidgetElement makeTreeRow(const std::string& id,
                                 const std::string& label,
                                 const std::string& iconPath,
                                 bool isFolder,
                                 int indentLevel = 0,
                                 const Vec4& iconTint = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f })
{
    const auto& theme = EditorTheme::Get();

    WidgetElement btn{};
    btn.id = id;
    btn.type = WidgetElementType::Button;
    btn.fillX = true;
    btn.minSize = Vec2{ 0.0f, theme.rowHeightSmall };
    btn.style.color = theme.buttonSubtle;
    btn.style.hoverColor = theme.buttonSubtleHover;
    btn.shaderVertex = "button_vertex.glsl";
    btn.shaderFragment = "button_fragment.glsl";
    btn.hitTestMode = HitTestMode::Enabled;
    btn.runtimeOnly = true;
    btn.style.transitionDuration = theme.hoverTransitionSpeed;
    btn.text = "";

    const float indentFrac = static_cast<float>(indentLevel) * 0.04f;

    const float rowHeight = theme.rowHeightSmall;
    const float iconPad  = 0.1f;
    const float iconSize = rowHeight * (1.0f - 2.0f * iconPad);

    if (!iconPath.empty())
    {
        WidgetElement icon{};
        icon.id = id + ".Icon";
        icon.type = WidgetElementType::Image;
        icon.imagePath = iconPath;
        icon.style.color = iconTint;
        icon.minSize = Vec2{ iconSize, iconSize };
        icon.sizeToContent = true;
        icon.from = Vec2{ indentFrac + 0.01f, iconPad };
        icon.to   = Vec2{ indentFrac + 0.01f, 1.0f - iconPad };
        icon.runtimeOnly = true;
        btn.children.push_back(std::move(icon));
    }

    {
        const float textFrom = iconPath.empty() ? (indentFrac + 0.01f) : (indentFrac + 0.08f);
        WidgetElement lbl{};
        lbl.id = id + ".Label";
        lbl.type = WidgetElementType::Text;
        lbl.text = label;
        lbl.font = theme.fontDefault;
        lbl.fontSize = theme.fontSizeBody;
        lbl.textAlignH = TextAlignH::Left;
        lbl.textAlignV = TextAlignV::Center;
        lbl.style.textColor = theme.textPrimary;
        lbl.from = Vec2{ textFrom, 0.0f };
        lbl.to   = Vec2{ 1.0f, 1.0f };
        lbl.padding = EditorTheme::Scaled(Vec2{ 3.0f, 2.0f });
        lbl.runtimeOnly = true;
        btn.children.push_back(std::move(lbl));
    }

    return btn;
}

static std::string resolveTextureSourcePath(const std::string& assetRelPath)
{
    auto asset = AssetManager::Instance().getLoadedAssetByPath(assetRelPath);
    if (!asset) return {};
    const auto& data = asset->getData();
    if (!data.is_object() || !data.contains("m_sourcePath")) return {};
    const std::string sourcePath = data["m_sourcePath"].get<std::string>();
    if (sourcePath.empty()) return {};
    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return {};
    const std::filesystem::path absPath = std::filesystem::path(projPath) / sourcePath;
    if (std::filesystem::exists(absPath))
        return absPath.string();
    return {};
}

static WidgetElement makeGridTile(const std::string& id,
                                  const std::string& label,
                                  const std::string& iconPath,
                                  const Vec4& iconTint,
                                  bool isFolder,
                                  unsigned int thumbnailTextureId = 0)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement tile{};
    tile.id = id;
    tile.type = WidgetElementType::Button;
    tile.minSize = EditorTheme::Scaled(Vec2{ 80.0f, 80.0f });
    tile.style.color = theme.buttonSubtle;
    tile.style.hoverColor = theme.cbTileHover;
    tile.shaderVertex = "button_vertex.glsl";
    tile.shaderFragment = "button_fragment.glsl";
    tile.hitTestMode = HitTestMode::Enabled;
    tile.runtimeOnly = true;
    tile.text = "";
    tile.margin = Vec2{ theme.gridTileSpacing * 0.5f, theme.gridTileSpacing * 0.5f };

    if (thumbnailTextureId != 0)
    {
        WidgetElement icon{};
        icon.id = id + ".Icon";
        icon.type = WidgetElementType::Image;
        icon.textureId = thumbnailTextureId;
        icon.style.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        icon.from = Vec2{ 0.05f, 0.02f };
        icon.to   = Vec2{ 0.95f, 0.62f };
        icon.runtimeOnly = true;
        tile.children.push_back(std::move(icon));
    }
    else if (!iconPath.empty())
    {
        WidgetElement icon{};
        icon.id = id + ".Icon";
        icon.type = WidgetElementType::Image;
        icon.imagePath = iconPath;
        icon.style.color = iconTint;
        icon.from = Vec2{ 0.15f, 0.05f };
        icon.to   = Vec2{ 0.85f, 0.62f };
        icon.runtimeOnly = true;
        tile.children.push_back(std::move(icon));
    }

    {
        WidgetElement lbl{};
        lbl.id = id + ".Label";
        lbl.type = WidgetElementType::Text;
        lbl.text = label;
        lbl.font = theme.fontDefault;
        lbl.fontSize = theme.fontSizeSmall;
        lbl.textAlignH = TextAlignH::Center;
        lbl.textAlignV = TextAlignV::Top;
        lbl.style.textColor = theme.textPrimary;
        lbl.from = Vec2{ 0.0f, 0.65f };
        lbl.to   = Vec2{ 1.0f, 1.0f };
        lbl.padding = EditorTheme::Scaled(Vec2{ 2.0f, 1.0f });
        lbl.runtimeOnly = true;
        tile.children.push_back(std::move(lbl));
    }

    return tile;
}

// ===========================================================================
// ContentBrowserPanel implementation
// ===========================================================================

void ContentBrowserPanel::cancelRename()
{
    m_renamingGridAsset = false;
    m_renameOriginalPath.clear();
}

void ContentBrowserPanel::startRename()
{
    if (!m_selectedGridAsset.empty() && !m_renamingGridAsset)
    {
        m_renamingGridAsset = true;
        m_renameOriginalPath = m_selectedGridAsset;
        refresh();
    }
}

void ContentBrowserPanel::refresh(const std::string& subfolder)
{
    auto& log = Logger::Instance();
    log.log(Logger::Category::UI, "[ContentBrowser] refreshContentBrowser called, subfolder='" + subfolder + "' current m_contentBrowserPath='" + m_contentBrowserPath + "'", Logger::LogLevel::INFO);
    if (!subfolder.empty())
    {
        m_contentBrowserPath = subfolder;
    }
    bool found = false;
    for (auto& entry : m_uiManager->getRegisteredWidgetsMutable())
    {
        if (entry.id == "ContentBrowser" && entry.widget)
        {
            log.log(Logger::Category::UI, "[ContentBrowser] refreshContentBrowser: found widget entry, calling populateWidget", Logger::LogLevel::INFO);
            populateWidget(entry.widget);
            m_uiManager->markAllWidgetsDirty();
            if (m_renamingGridAsset)
            {
                if (auto* renameEntry = m_uiManager->findElementById("ContentBrowser.RenameEntry"))
                {
                    m_uiManager->setFocusedEntry(renameEntry);
                }
            }
            found = true;
            return;
        }
    }
    if (!found)
    {
        log.log(Logger::Category::UI, "[ContentBrowser] refreshContentBrowser: no 'ContentBrowser' widget found in m_widgets (count=" + std::to_string(m_uiManager->getRegisteredWidgets().size()) + ")", Logger::LogLevel::WARNING);
    }
}

void ContentBrowserPanel::focusSearch()
{
    if (auto* searchBar = m_uiManager->findElementById("ContentBrowser.Search"))
    {
        m_uiManager->setFocusedEntry(searchBar);
        m_uiManager->markAllWidgetsDirty();
    }
}

std::unordered_set<std::string> ContentBrowserPanel::buildReferencedAssetSet() const
{
    std::unordered_set<std::string> refs;
    auto& ecs = ECS::ECSManager::Instance();

    {
        ECS::Schema schema;
        schema.require<ECS::MeshComponent>();
        for (const auto e : ecs.getEntitiesMatchingSchema(schema))
        {
            const auto* mesh = ecs.getComponent<ECS::MeshComponent>(e);
            if (mesh && !mesh->meshAssetPath.empty())
                refs.insert(mesh->meshAssetPath);
        }
    }
    {
        ECS::Schema schema;
        schema.require<ECS::MaterialComponent>();
        for (const auto e : ecs.getEntitiesMatchingSchema(schema))
        {
            const auto* mat = ecs.getComponent<ECS::MaterialComponent>(e);
            if (mat && !mat->materialAssetPath.empty())
                refs.insert(mat->materialAssetPath);
        }
    }
    {
        ECS::Schema schema;
        schema.require<ECS::LogicComponent>();
        for (const auto e : ecs.getEntitiesMatchingSchema(schema))
        {
            const auto* sc = ecs.getComponent<ECS::LogicComponent>(e);
            if (sc && !sc->scriptPath.empty())
                refs.insert(sc->scriptPath);
        }
    }
    return refs;
}

void ContentBrowserPanel::populateWidget(const std::shared_ptr<EditorWidget>& widget)
{
    auto& log = Logger::Instance();

    if (!widget)
    {
        log.log(Logger::Category::UI, "[ContentBrowser] ABORT: widget is null", Logger::LogLevel::WARNING);
        return;
    }

    const auto& diagnostics = DiagnosticsManager::Instance();
    const auto& projectInfo = diagnostics.getProjectInfo();
    if (projectInfo.projectPath.empty())
    {
        log.log(Logger::Category::UI, "[ContentBrowser] ABORT: projectInfo.projectPath is empty", Logger::LogLevel::WARNING);
        return;
    }

    const std::filesystem::path contentRoot =
        std::filesystem::path(projectInfo.projectPath) / "Content";
    if (!std::filesystem::exists(contentRoot))
    {
        log.log(Logger::Category::UI, "[ContentBrowser] ABORT: contentRoot does not exist: " + contentRoot.string(), Logger::LogLevel::WARNING);
        return;
    }

    // Show placeholder while the asset registry is still building
    if (!DiagnosticsManager::Instance().isAssetRegistryReady())
    {
        log.log(Logger::Category::UI, "[ContentBrowser] Registry not ready yet, showing loading placeholder.", Logger::LogLevel::INFO);
        auto& elements = widget->getElementsMutable();
        WidgetElement* treePanel = FindElementById(elements, "ContentBrowser.Tree");
        if (treePanel)
        {
            treePanel->children.clear();
            WidgetElement loadingRow{};
            loadingRow.id = "ContentBrowser.Loading";
            loadingRow.type = WidgetElementType::Text;
            loadingRow.text = "Building asset registry...";
            loadingRow.font = EditorTheme::Get().fontDefault;
            loadingRow.fontSize = EditorTheme::Get().fontSizeBody;
            loadingRow.style.textColor = EditorTheme::Get().textMuted;
            loadingRow.textAlignH = TextAlignH::Left;
            loadingRow.textAlignV = TextAlignV::Center;
            loadingRow.fillX = true;
            loadingRow.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
            loadingRow.padding = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
            loadingRow.runtimeOnly = true;
            treePanel->children.push_back(std::move(loadingRow));
        }
        WidgetElement* gridPanel = FindElementById(elements, "ContentBrowser.Grid");
        if (gridPanel)
        {
            gridPanel->children.clear();
        }
        WidgetElement* pathBar = FindElementById(elements, "ContentBrowser.PathBar");
        if (pathBar)
        {
            pathBar->children.clear();
        }
        widget->markLayoutDirty();
        return;
    }

    auto& elements = widget->getElementsMutable();
    WidgetElement* treePanel = FindElementById(elements, "ContentBrowser.Tree");
    if (!treePanel)
    {
        log.log(Logger::Category::UI, "[ContentBrowser] ABORT: 'ContentBrowser.Tree' element not found in widget", Logger::LogLevel::WARNING);
        return;
    }

    treePanel->children.clear();
    treePanel->scrollable = true;

    const auto& registry = AssetManager::Instance().getAssetRegistry();

    // Recursive helper: add folders + assets for a given subpath
    struct Builder
    {
        ContentBrowserPanel* self;
        UIManager* uiMgr;
        const std::filesystem::path& contentRoot;
        const std::vector<AssetRegistryEntry>& registry;
        WidgetElement* treePanel;

        void build(const std::string& subPath, int depth)
        {
            const std::filesystem::path browseDir = subPath.empty()
                ? contentRoot
                : contentRoot / subPath;

            // Subfolders
            std::vector<std::string> subFolders;
            {
                std::error_code ec;
                if (std::filesystem::exists(browseDir, ec))
                {
                    for (const auto& fsEntry : std::filesystem::directory_iterator(browseDir, ec))
                    {
                        if (fsEntry.is_directory())
                        {
                            subFolders.push_back(fsEntry.path().filename().string());
                        }
                    }
                    if (ec)
                    {
                        Logger::Instance().log(Logger::Category::UI, "[ContentBrowser] directory_iterator error: " + ec.message(), Logger::LogLevel::WARNING);
                    }
                }
                else
                {
                    Logger::Instance().log(Logger::Category::UI, "[ContentBrowser] browseDir does not exist: " + browseDir.string() + (ec ? " ec=" + ec.message() : ""), Logger::LogLevel::WARNING);
                }
            }
            std::sort(subFolders.begin(), subFolders.end());

            for (const auto& folderName : subFolders)
            {
                const std::string newSub = subPath.empty()
                    ? folderName
                    : (subPath + "/" + folderName);

                const std::string rowId = "ContentBrowser.Dir." + newSub;
                const bool expanded = self->m_expandedFolders.count(newSub) > 0;

                WidgetElement row = makeTreeRow(rowId, folderName, "folder.png", true, depth, Vec4{ 0.95f, 0.85f, 0.35f, 1.0f });
                row.isExpanded = expanded;

                // Highlight if this is the currently viewed folder
                if (newSub == self->m_selectedBrowserFolder)
                {
                    row.style.color = EditorTheme::Get().treeRowSelected;
                }

                row.onClicked = [cbPanel = self, newSub]()
                {
                    // Toggle expand/collapse
                    if (cbPanel->m_expandedFolders.count(newSub))
                    {
                        // Collapse only if already selected; otherwise just select
                        if (cbPanel->m_selectedBrowserFolder == newSub)
                        {
                            cbPanel->m_expandedFolders.erase(newSub);
                        }
                    }
                    else
                    {
                        cbPanel->m_expandedFolders.insert(newSub);
                    }
                    cbPanel->m_selectedBrowserFolder = newSub;
                    cbPanel->refresh();
                };
                treePanel->children.push_back(std::move(row));

                // If expanded, recurse
                if (expanded)
                {
                    build(newSub, depth + 1);
                }
            }

            // Assets directly inside subPath
            struct AssetItem { std::string name; std::string relPath; AssetType type; };
            std::vector<AssetItem> assetItems;
            for (const auto& e : registry)
            {
                const std::filesystem::path regPath(e.path);
                const std::string parentStr = regPath.parent_path().generic_string();
                if (parentStr == subPath)
                {
                    assetItems.push_back({ e.name, e.path, e.type });
                }
            }
            std::sort(assetItems.begin(), assetItems.end(),
                [](const AssetItem& a, const AssetItem& b) { return a.name < b.name; });

            for (const auto& item : assetItems)
            {
                const std::string iconFile = iconForAssetType(item.type);
                const std::string relPath  = item.relPath;
                const AssetType   itemType = item.type;

                WidgetElement row = makeTreeRow(
                    "ContentBrowser.Asset." + relPath,
                    item.name, iconFile, false, depth, iconTintForAssetType(itemType));

                // Make asset tree rows draggable
                row.isDraggable = true;
                row.dragPayload = std::to_string(static_cast<int>(itemType)) + "|" + relPath;

                row.onClicked = [uiMgr = self->m_uiManager, relPath, itemType]()
                {
                    uiMgr->showToastMessage("Asset: " + relPath, UIManager::kToastMedium);
                };
                treePanel->children.push_back(std::move(row));
                }
        }
    };

    Builder builder{ this, m_uiManager, contentRoot, registry, treePanel };

    // "Content" root node
    {
        const std::string rootId = "ContentBrowser.Dir.Root";
        WidgetElement rootRow = makeTreeRow(rootId, "Content", "folder.png", true, 0, Vec4{ 0.95f, 0.85f, 0.35f, 1.0f });
        rootRow.isExpanded = true;
        if (m_selectedBrowserFolder.empty())
        {
            rootRow.style.color = EditorTheme::Get().treeRowSelected;
        }
        rootRow.onClicked = [this]()
        {
            m_selectedBrowserFolder.clear();
            refresh();
        };
        treePanel->children.push_back(std::move(rootRow));
    }

    builder.build(m_contentBrowserPath, 1);

    // "Shaders" root node (project shaders directory, outside Content)
    {
        const std::filesystem::path shadersRoot =
            std::filesystem::path(projectInfo.projectPath) / "Shaders";
        if (std::filesystem::exists(shadersRoot))
        {
            const std::string shadersVirtualPath = "__Shaders__";
            const std::string shadersId = "ContentBrowser.Dir.Shaders";
            const bool expanded = m_expandedFolders.count(shadersVirtualPath) > 0;
            WidgetElement shadersRow = makeTreeRow(shadersId, "Shaders", "folder.png", true, 0, Vec4{ 0.75f, 0.50f, 1.00f, 1.0f });
            shadersRow.isExpanded = expanded;
            if (m_selectedBrowserFolder == shadersVirtualPath)
            {
                shadersRow.style.color = EditorTheme::Get().treeRowSelected;
            }
            shadersRow.onClicked = [this, shadersVirtualPath]()
            {
                if (m_expandedFolders.count(shadersVirtualPath))
                {
                    if (m_selectedBrowserFolder == shadersVirtualPath)
                        m_expandedFolders.erase(shadersVirtualPath);
                }
                else
                {
                    m_expandedFolders.insert(shadersVirtualPath);
                }
                m_selectedBrowserFolder = shadersVirtualPath;
                refresh();
            };
            treePanel->children.push_back(std::move(shadersRow));

            // If expanded, show shader files as tree rows
            if (expanded)
            {
                std::vector<std::string> shaderFiles;
                std::error_code ec;
                for (const auto& fsEntry : std::filesystem::directory_iterator(shadersRoot, ec))
                {
                    if (fsEntry.is_regular_file())
                    {
                        shaderFiles.push_back(fsEntry.path().filename().string());
                    }
                }
                std::sort(shaderFiles.begin(), shaderFiles.end());
                for (const auto& fileName : shaderFiles)
                {
                    WidgetElement row = makeTreeRow(
                        "ContentBrowser.Shader." + fileName,
                        fileName, "shader.png", false, 1,
                        Vec4{ 0.75f, 0.50f, 1.00f, 1.0f });
                    treePanel->children.push_back(std::move(row));
                }
            }
        }
    }

    // ---- Populate Path Bar with breadcrumb buttons ----
    WidgetElement* pathBar = FindElementById(elements, "ContentBrowser.PathBar");
    if (pathBar)
    {
        pathBar->children.clear();

        // Back button (navigate up one level)
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement backBtn = EditorUIBuilder::makeSubtleButton(
                "ContentBrowser.PathBar.Back", "<", {}, EditorTheme::Scaled(Vec2{ 24.0f, theme.rowHeightSmall }));
            backBtn.fillX = false;
            backBtn.sizeToContent = false;
            backBtn.onClicked = [this]()
            {
                if (!m_selectedBrowserFolder.empty())
                {
                    const auto slashPos = m_selectedBrowserFolder.rfind('/');
                    if (slashPos != std::string::npos)
                    {
                        m_selectedBrowserFolder = m_selectedBrowserFolder.substr(0, slashPos);
                    }
                    else
                    {
                        m_selectedBrowserFolder.clear();
                    }
                    refresh();
                }
            };
            pathBar->children.push_back(std::move(backBtn));
        }

        // Import button
        {
            WidgetElement importBtn = EditorUIBuilder::makePrimaryButton(
                "ContentBrowser.PathBar.Import", "+ Import", {}, EditorTheme::Scaled(Vec2{ 64.0f, EditorTheme::Get().rowHeightSmall }));
            importBtn.fillX = false;
            pathBar->children.push_back(std::move(importBtn));
        }

        // New Level dropdown button
        {
            const auto& theme = EditorTheme::Get();
            DropdownButtonWidget newLevelDropdown;
            newLevelDropdown.setText("+ Level");
            newLevelDropdown.setFont(theme.fontDefault);
            newLevelDropdown.setFontSize(theme.fontSizeSmall);
            newLevelDropdown.setMinSize(EditorTheme::Scaled(Vec2{ 58.0f, theme.rowHeightSmall }));
            newLevelDropdown.setPadding(theme.paddingSmall);
            newLevelDropdown.setBackgroundColor(Vec4{ theme.successColor.x * 0.55f, theme.successColor.y * 0.55f, theme.successColor.z * 0.55f, 0.85f });
            newLevelDropdown.setHoverColor(Vec4{ theme.successColor.x * 0.75f, theme.successColor.y * 0.75f, theme.successColor.z * 0.75f, 0.95f });
            newLevelDropdown.setTextColor(theme.textPrimary);

            struct LevelTemplate { std::string label; UIManager::SceneTemplate tmpl; };
            const LevelTemplate templates[] = {
                { "Empty",         UIManager::SceneTemplate::Empty },
                { "Basic Outdoor", UIManager::SceneTemplate::BasicOutdoor },
                { "Prototype",     UIManager::SceneTemplate::Prototype },
            };
            for (const auto& t : templates)
            {
                newLevelDropdown.addItem(t.label, [uiMgr = m_uiManager, tmpl = t.tmpl, label = t.label]() {
                    // Build unique level name
                    std::string baseName = "NewLevel";
                    int suffix = 1;
                    std::string levelName = baseName;
                    const auto& reg = AssetManager::Instance().getAssetRegistry();
                    while (true)
                    {
                        bool found = false;
                        for (const auto& e : reg)
                        {
                            if (e.type == AssetType::Level && e.name == levelName) { found = true; break; }
                        }
                        if (!found) break;
                        levelName = baseName + std::to_string(suffix++);
                    }
                    uiMgr->createNewLevelWithTemplate(tmpl, levelName);
                });
            }

            WidgetElement newLevelEl = newLevelDropdown.toElement();
            newLevelEl.id = "ContentBrowser.PathBar.NewLevel";
            newLevelEl.fillX = false;
            newLevelEl.runtimeOnly = false;
            pathBar->children.push_back(std::move(newLevelEl));
        }

        // Rename button (enabled only when a grid asset is selected)
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement renameBtn = EditorUIBuilder::makeButton(
                "ContentBrowser.PathBar.Rename", "Rename", {}, EditorTheme::Scaled(Vec2{ 60.0f, theme.rowHeightSmall }));
            renameBtn.fillX = false;
            if (!m_selectedGridAsset.empty())
            {
                renameBtn.style.color = Vec4{ theme.warningColor.x, theme.warningColor.y, theme.warningColor.z, 0.6f };
                renameBtn.style.hoverColor = Vec4{ theme.warningColor.x, theme.warningColor.y, theme.warningColor.z, 0.8f };
                renameBtn.style.textColor = theme.textPrimary;
                renameBtn.onClicked = [this]()
                {
                    if (!m_selectedGridAsset.empty())
                    {
                        m_renamingGridAsset = true;
                        m_renameOriginalPath = m_selectedGridAsset;
                        refresh();
                    }
                };
            }
            else
            {
                renameBtn.style.color = Vec4{ theme.buttonDefault.x, theme.buttonDefault.y, theme.buttonDefault.z, 0.4f };
                renameBtn.style.hoverColor = Vec4{ theme.buttonDefault.x, theme.buttonDefault.y, theme.buttonDefault.z, 0.4f };
                renameBtn.style.textColor = theme.textMuted;
            }
            pathBar->children.push_back(std::move(renameBtn));
        }

        // "Refs" button
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement refsBtn = EditorUIBuilder::makeButton(
                "ContentBrowser.PathBar.Refs", "Refs", {}, EditorTheme::Scaled(Vec2{ 42.0f, theme.rowHeightSmall }));
            refsBtn.fillX = false;
            refsBtn.tooltipText = "Find References \xe2\x80\x93 who uses this asset?";
            if (!m_selectedGridAsset.empty())
            {
                refsBtn.style.color = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.5f };
                refsBtn.style.hoverColor = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.7f };
                refsBtn.style.textColor = theme.textPrimary;
                refsBtn.onClicked = [uiMgr = m_uiManager, selectedAsset = m_selectedGridAsset]()
                {
                    if (selectedAsset.empty()) return;
                    const auto refs = AssetManager::Instance().findReferencesTo(selectedAsset);
                    std::string msg = "References to:\n  " + selectedAsset + "\n\n";
                    if (refs.empty())
                    {
                        msg += "No references found.\nThis asset is not used by any other asset or entity.";
                    }
                    else
                    {
                        msg += std::to_string(refs.size()) + " reference(s) found:\n\n";
                        for (const auto& r : refs)
                        {
                            msg += "  [" + r.sourceType + "]  " + r.sourcePath + "\n";
                        }
                    }
                    uiMgr->showModalMessage(msg);
                };
            }
            else
            {
                refsBtn.style.color = Vec4{ theme.buttonDefault.x, theme.buttonDefault.y, theme.buttonDefault.z, 0.4f };
                refsBtn.style.hoverColor = Vec4{ theme.buttonDefault.x, theme.buttonDefault.y, theme.buttonDefault.z, 0.4f };
                refsBtn.style.textColor = theme.textMuted;
            }
            pathBar->children.push_back(std::move(refsBtn));
        }

        // "Deps" button
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement depsBtn = EditorUIBuilder::makeButton(
                "ContentBrowser.PathBar.Deps", "Deps", {}, EditorTheme::Scaled(Vec2{ 46.0f, theme.rowHeightSmall }));
            depsBtn.fillX = false;
            depsBtn.tooltipText = "Show Dependencies \xe2\x80\x93 what does this asset use?";
            if (!m_selectedGridAsset.empty())
            {
                depsBtn.style.color = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.5f };
                depsBtn.style.hoverColor = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.7f };
                depsBtn.style.textColor = theme.textPrimary;
                depsBtn.onClicked = [uiMgr = m_uiManager, selectedAsset = m_selectedGridAsset]()
                {
                    if (selectedAsset.empty()) return;
                    const auto deps = AssetManager::Instance().getAssetDependencies(selectedAsset);
                    std::string msg = "Dependencies of:\n  " + selectedAsset + "\n\n";
                    if (deps.empty())
                    {
                        msg += "No dependencies found.\nThis asset does not reference any other assets.";
                    }
                    else
                    {
                        msg += std::to_string(deps.size()) + " dependency(ies):\n\n";
                        for (const auto& dep : deps)
                        {
                            msg += "  " + dep + "\n";
                        }
                    }
                    uiMgr->showModalMessage(msg);
                };
            }
            else
            {
                depsBtn.style.color = Vec4{ theme.buttonDefault.x, theme.buttonDefault.y, theme.buttonDefault.z, 0.4f };
                depsBtn.style.hoverColor = Vec4{ theme.buttonDefault.x, theme.buttonDefault.y, theme.buttonDefault.z, 0.4f };
                depsBtn.style.textColor = theme.textMuted;
            }
            pathBar->children.push_back(std::move(depsBtn));
        }

        // Breadcrumb segments: Content > Folder > SubFolder > ...
        std::vector<std::pair<std::string, std::string>> crumbs;
        crumbs.push_back({ "Content", "" });

        if (!m_selectedBrowserFolder.empty())
        {
            std::string accumulated;
            std::istringstream stream(m_selectedBrowserFolder);
            std::string segment;
            while (std::getline(stream, segment, '/'))
            {
                if (segment.empty()) continue;
                accumulated = accumulated.empty() ? segment : (accumulated + "/" + segment);
                crumbs.push_back({ segment, accumulated });
            }
        }

        for (size_t i = 0; i < crumbs.size(); ++i)
        {
            // Separator ">"
            if (i > 0)
            {
                WidgetElement sep{};
                sep.id = "ContentBrowser.PathBar.Sep." + std::to_string(i);
                sep.type = WidgetElementType::Text;
                sep.text = ">";
                sep.font = EditorTheme::Get().fontDefault;
                sep.fontSize = EditorTheme::Get().fontSizeSmall;
                sep.style.textColor = EditorTheme::Get().textMuted;
                sep.textAlignH = TextAlignH::Center;
                sep.textAlignV = TextAlignV::Center;
                sep.minSize = EditorTheme::Scaled(Vec2{ 14.0f, 20.0f });
                sep.runtimeOnly = true;
                pathBar->children.push_back(std::move(sep));
            }

            const bool isActive = (crumbs[i].second == m_selectedBrowserFolder);
            WidgetElement crumbBtn{};
            crumbBtn.id = "ContentBrowser.PathBar.Crumb." + std::to_string(i);
            crumbBtn.type = WidgetElementType::Button;
            crumbBtn.text = crumbs[i].first;
            const auto& theme = EditorTheme::Get();
            crumbBtn.font = theme.fontDefault;
            crumbBtn.fontSize = theme.fontSizeSmall;
            crumbBtn.style.textColor = isActive
                ? theme.textPrimary
                : theme.textSecondary;
            crumbBtn.textAlignH = TextAlignH::Center;
            crumbBtn.textAlignV = TextAlignV::Center;
            crumbBtn.minSize = Vec2{ 0.0f, theme.rowHeightSmall };
            crumbBtn.sizeToContent = true;
            crumbBtn.padding = theme.paddingNormal;
            crumbBtn.style.color = isActive
                ? theme.selectionHighlight
                : theme.transparent;
            crumbBtn.style.hoverColor = theme.buttonSubtleHover;
            crumbBtn.shaderVertex = "button_vertex.glsl";
            crumbBtn.shaderFragment = "button_fragment.glsl";
            crumbBtn.hitTestMode = HitTestMode::Enabled;
            crumbBtn.runtimeOnly = true;

            const std::string crumbPath = crumbs[i].second;
            crumbBtn.onClicked = [this, crumbPath]()
            {
                m_selectedBrowserFolder = crumbPath;
                if (!crumbPath.empty() && !m_expandedFolders.count(crumbPath))
                {
                    m_expandedFolders.insert(crumbPath);
                }
                refresh();
            };
            pathBar->children.push_back(std::move(crumbBtn));
        }

        // "+ Entity" template dropdown
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement addBtn{};
            addBtn.id            = "ContentBrowser.AddEntity";
            addBtn.type          = WidgetElementType::Button;
            addBtn.text          = "+ Entity";
            addBtn.font          = theme.fontDefault;
            addBtn.fontSize      = theme.fontSizeSmall;
            addBtn.style.textColor = theme.textPrimary;
            addBtn.style.color     = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.25f };
            addBtn.style.hoverColor = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.45f };
            addBtn.style.borderRadius = theme.borderRadius;
            addBtn.textAlignH    = TextAlignH::Center;
            addBtn.textAlignV    = TextAlignV::Center;
            addBtn.minSize       = EditorTheme::Scaled(Vec2{ 58.0f, 20.0f });
            addBtn.padding       = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            addBtn.sizeToContent = true;
            addBtn.hitTestMode   = HitTestMode::Enabled;
            addBtn.runtimeOnly   = true;
            addBtn.onClicked = [uiMgr = m_uiManager]()
            {
                auto* renderer = uiMgr->getRenderer();
                if (!renderer) return;
                Vec3 spawnPos{ 0.0f, 0.0f, 0.0f };
                const Vec3 camPos = renderer->getCameraPosition();
                const Vec2 camRot = renderer->getCameraRotationDegrees();
                const float yaw = camRot.x * 3.14159265f / 180.0f;
                const float pitch = camRot.y * 3.14159265f / 180.0f;
                spawnPos.x = camPos.x + cosf(yaw) * cosf(pitch) * 5.0f;
                spawnPos.y = camPos.y + sinf(pitch) * 5.0f;
                spawnPos.z = camPos.z + sinf(yaw) * cosf(pitch) * 5.0f;

                std::vector<UIManager::DropdownMenuItem> items;
                const char* templates[] = {
                    "Empty Entity", "Point Light", "Directional Light",
                    "Camera", "Static Mesh", "Physics Object", "Particle Emitter"
                };
                for (const char* t : templates)
                {
                    const std::string tmplName = t;
                    const Vec3 pos = spawnPos;
                    items.push_back({ tmplName, [uiMgr, tmplName, pos]()
                    {
                        uiMgr->spawnBuiltinTemplate(tmplName, pos);
                    }});
                }
                // Compute dropdown anchor from the button's layout
                const auto* elem = uiMgr->findElementById("ContentBrowser.AddEntity");
                Vec2 anchor = uiMgr->getMousePosition();
                if (elem)
                {
                    anchor.x = elem->computedPositionPixels.x;
                    anchor.y = elem->computedPositionPixels.y + elem->computedSizePixels.y;
                }
                uiMgr->showDropdownMenu(anchor, items, EditorTheme::Scaled(130.0f));
            };
            pathBar->children.push_back(std::move(addBtn));
        }

        // Spacer to push search to the right
        {
            WidgetElement spacer{};
            spacer.type        = WidgetElementType::Panel;
            spacer.fillX       = true;
            spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            spacer.runtimeOnly = true;
            pathBar->children.push_back(std::move(spacer));
        }

        // Type filter buttons
        {
            const auto& theme = EditorTheme::Get();
            struct FilterDef { const char* label; AssetType type; };
            const FilterDef filters[] = {
                { "Mesh",     AssetType::Model3D  },
                { "Mat",      AssetType::Material },
                { "Tex",      AssetType::Texture  },
                { "Script",   AssetType::Script   },
                { "C++",      AssetType::NativeScript },
                { "Audio",    AssetType::Audio    },
                { "Level",    AssetType::Level    },
                { "Widget",   AssetType::Widget   },
                { "Prefab",   AssetType::Prefab   },
                { "Action",   AssetType::InputAction },
                { "Mapping",  AssetType::InputMapping },
            };
            for (const auto& f : filters)
            {
                const uint16_t bit = static_cast<uint16_t>(1 << static_cast<int>(f.type));
                const bool active = (m_browserTypeFilter & bit) != 0;
                WidgetElement btn{};
                btn.id            = std::string("ContentBrowser.Filter.") + f.label;
                btn.type          = WidgetElementType::Button;
                btn.text          = f.label;
                btn.font          = theme.fontDefault;
                btn.fontSize      = theme.fontSizeCaption;
                btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
                btn.style.color     = active ? Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.35f } : theme.transparent;
                btn.style.hoverColor = theme.buttonSubtleHover;
                btn.style.borderRadius = theme.borderRadius;
                btn.textAlignH    = TextAlignH::Center;
                btn.textAlignV    = TextAlignV::Center;
                btn.minSize       = EditorTheme::Scaled(Vec2{ 38.0f, 18.0f });
                btn.padding       = EditorTheme::Scaled(Vec2{ 4.0f, 1.0f });
                btn.sizeToContent = true;
                btn.hitTestMode   = HitTestMode::Enabled;
                btn.runtimeOnly   = true;
                const uint16_t filterBit = bit;
                btn.onClicked = [this, filterBit]()
                {
                    m_browserTypeFilter ^= filterBit;
                    refresh();
                };
                pathBar->children.push_back(std::move(btn));
            }
        }

        // Search entry bar
        {
            WidgetElement search = EditorUIBuilder::makeEntryBar(
                "ContentBrowser.Search", m_browserSearchText,
                [this](const std::string& text)
                {
                    m_browserSearchText = text;
                    refresh();
                },
                EditorTheme::Scaled(140.0f));
            search.minSize = EditorTheme::Scaled(Vec2{ 140.0f, 20.0f });
            pathBar->children.push_back(std::move(search));
        }
    }

    // ---- Populate Grid panel with contents of selected folder ----
    WidgetElement* gridPanel = FindElementById(elements, "ContentBrowser.Grid");
    if (gridPanel)
    {
        gridPanel->children.clear();
        gridPanel->scrollable = true;

        // Build set of all asset paths referenced by ECS entities (for unreferenced indicator)
        const auto referencedAssets = buildReferencedAssetSet();

        const std::string& gridFolder = m_selectedBrowserFolder;

        // Helper: case-insensitive substring match
        auto matchesSearch = [this](const std::string& name) -> bool
        {
            if (m_browserSearchText.empty()) return true;
            std::string nameLower = name;
            std::string queryLower = m_browserSearchText;
            for (auto& c : nameLower)  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : queryLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return nameLower.find(queryLower) != std::string::npos;
        };

        // Helper: check if an asset type passes the type filter
        auto matchesTypeFilter = [this](AssetType type) -> bool
        {
            const uint16_t bit = static_cast<uint16_t>(1 << static_cast<int>(type));
            return (m_browserTypeFilter & bit) != 0;
        };

        const bool isSearchMode = !m_browserSearchText.empty();

        // Handle Shaders virtual folder
        const bool isShadersView = (gridFolder == "__Shaders__") && !isSearchMode;
        if (isShadersView)
        {
            const std::filesystem::path shadersDir =
                std::filesystem::path(projectInfo.projectPath) / "Shaders";
            if (std::filesystem::exists(shadersDir))
            {
                std::vector<std::string> shaderFiles;
                std::error_code ec;
                for (const auto& fsEntry : std::filesystem::directory_iterator(shadersDir, ec))
                {
                    if (fsEntry.is_regular_file())
                    {
                        shaderFiles.push_back(fsEntry.path().filename().string());
                    }
                }
                std::sort(shaderFiles.begin(), shaderFiles.end());
                for (const auto& fileName : shaderFiles)
                {
                    WidgetElement tile = makeGridTile(
                        "ContentBrowser.Shader." + fileName,
                        fileName, "shader.png",
                        Vec4{ 0.75f, 0.50f, 1.00f, 1.0f }, false);
                    gridPanel->children.push_back(std::move(tile));
                }
            }
        }
        else if (isSearchMode)
        {
            // Search mode: flat list of ALL matching assets across all folders
            struct GridAssetItem { std::string name; std::string relPath; AssetType type; };
            std::vector<GridAssetItem> gridAssets;
            for (const auto& e : registry)
            {
                if (!matchesSearch(e.name)) continue;
                if (!matchesTypeFilter(e.type)) continue;
                gridAssets.push_back({ e.name, e.path, e.type });
            }
            std::sort(gridAssets.begin(), gridAssets.end(),
                [](const GridAssetItem& a, const GridAssetItem& b) { return a.name < b.name; });

            for (const auto& item : gridAssets)
            {
                const std::string iconFile = iconForAssetType(item.type);
                const std::string relPath = item.relPath;

                // Resolve texture thumbnail for texture assets
                unsigned int thumbTex = 0;
                if (item.type == AssetType::Texture && m_renderer)
                {
                    const std::string srcPath = resolveTextureSourcePath(relPath);
                    if (!srcPath.empty())
                        thumbTex = m_renderer->preloadUITexture(srcPath);
                }
                else if ((item.type == AssetType::Model3D || item.type == AssetType::Material) && m_renderer)
                {
                    thumbTex = m_renderer->generateAssetThumbnail(relPath, static_cast<int>(item.type));
                }

                // Show path as label in search mode for disambiguation
                const std::string displayName = item.name + "  (" +
                    std::filesystem::path(item.relPath).parent_path().generic_string() + ")";

                WidgetElement tile = makeGridTile(
                    "ContentBrowser.GridAsset." + relPath,
                    displayName, iconFile,
                    iconTintForAssetType(item.type), false, thumbTex);

                if (relPath == m_selectedGridAsset)
                    tile.style.color = EditorTheme::Get().cbTileSelected;

                // Unreferenced asset indicator
                if (referencedAssets.count(relPath) == 0 &&
                    item.type != AssetType::Level && item.type != AssetType::Shader &&
                    item.type != AssetType::NativeScript &&
                    item.type != AssetType::Unknown)
                {
                    WidgetElement badge{};
                    badge.id = tile.id + ".Unref";
                    badge.type = WidgetElementType::Text;
                    badge.text = "\xe2\x97\x8f";
                    badge.font = EditorTheme::Get().fontDefault;
                    badge.fontSize = EditorTheme::Get().fontSizeSmall;
                    badge.textAlignH = TextAlignH::Right;
                    badge.textAlignV = TextAlignV::Top;
                    badge.style.textColor = EditorTheme::Get().textMuted;
                    badge.from = Vec2{ 0.75f, 0.0f };
                    badge.to   = Vec2{ 1.0f, 0.15f };
                    badge.runtimeOnly = true;
                    tile.children.push_back(std::move(badge));
                }

                tile.isDraggable = true;
                tile.dragPayload = std::to_string(static_cast<int>(item.type)) + "|" + relPath;

                tile.onClicked = [this, relPath]()
                {
                    m_selectedGridAsset = relPath;
                    refresh();
                };

                tile.onDoubleClicked = [this, relPath, assetType = item.type]()
                {
                    // Navigate to the asset's folder and clear search
                    const std::string folder = std::filesystem::path(relPath).parent_path().generic_string();
                    m_browserSearchText.clear();
                    m_selectedBrowserFolder = folder;
                    m_selectedGridAsset = relPath;
                    if (!folder.empty() && !m_expandedFolders.count(folder))
                        m_expandedFolders.insert(folder);

                    // Also open the asset if it has a dedicated editor
                    if (assetType == AssetType::Model3D && m_uiManager->getRenderer())
                    {
                        m_uiManager->getRenderer()->openMeshViewer(relPath);
                    }
                    else if (assetType == AssetType::Texture && m_uiManager->getRenderer())
                    {
                        m_uiManager->getRenderer()->openTextureViewer(relPath);
                    }
                    else if (assetType == AssetType::Widget)
                    {
                        m_uiManager->openWidgetEditorPopup(relPath);
                    }
                    else if (assetType == AssetType::Material)
                    {
                        if (m_uiManager->getRenderer())
                            m_uiManager->getRenderer()->openMaterialEditorTab(relPath);
                    }
                    else if (assetType == AssetType::Prefab)
                    {
                        m_uiManager->spawnPrefabAtPosition(relPath, Vec3{ 0.0f, 0.0f, 0.0f });
                    }
                    else if (assetType == AssetType::Audio)
                    {
                        m_uiManager->openAudioPreviewTab(relPath);
                    }
                    else if (assetType == AssetType::Level)
                    {
                        m_uiManager->requestLevelLoad(relPath);
                    }
                    refresh();
                };

                gridPanel->children.push_back(std::move(tile));
            }
        }
        else
        {
        const std::filesystem::path gridDir = gridFolder.empty()
            ? contentRoot
            : contentRoot / gridFolder;

        // Subfolders as grid tiles
        if (std::filesystem::exists(gridDir))
        {
            std::vector<std::string> subFolders;
            std::error_code ec;
            for (const auto& fsEntry : std::filesystem::directory_iterator(gridDir, ec))
            {
                if (fsEntry.is_directory())
                {
                    subFolders.push_back(fsEntry.path().filename().string());
                }
            }
            std::sort(subFolders.begin(), subFolders.end());

            for (const auto& folderName : subFolders)
            {
                const std::string folderSub = gridFolder.empty()
                    ? folderName
                    : (gridFolder + "/" + folderName);

                WidgetElement tile = makeGridTile(
                    "ContentBrowser.GridDir." + folderSub,
                    folderName, "folder.png",
                    Vec4{ 0.95f, 0.85f, 0.35f, 1.0f }, true);

                // Double-click opens the folder
                tile.onDoubleClicked = [this, folderSub]()
                {
                    m_selectedGridAsset.clear();
                    m_selectedBrowserFolder = folderSub;
                    if (!m_expandedFolders.count(folderSub))
                    {
                        m_expandedFolders.insert(folderSub);
                    }
                    refresh();
                };

                gridPanel->children.push_back(std::move(tile));
            }
        }

        // Assets in the selected folder
        struct GridAssetItem { std::string name; std::string relPath; AssetType type; };
        std::vector<GridAssetItem> gridAssets;
        for (const auto& e : registry)
        {
            const std::filesystem::path regPath(e.path);
            const std::string parentStr = regPath.parent_path().generic_string();
            if (parentStr == gridFolder)
            {
                if (!matchesTypeFilter(e.type)) continue;
                gridAssets.push_back({ e.name, e.path, e.type });
            }
        }
        std::sort(gridAssets.begin(), gridAssets.end(),
            [](const GridAssetItem& a, const GridAssetItem& b) { return a.name < b.name; });

        for (const auto& item : gridAssets)
        {
            const std::string iconFile = iconForAssetType(item.type);
            const std::string relPath = item.relPath;

            // Resolve texture thumbnail for texture assets
            unsigned int thumbTex = 0;
            if (item.type == AssetType::Texture && m_renderer)
            {
                const std::string srcPath = resolveTextureSourcePath(relPath);
                if (!srcPath.empty())
                    thumbTex = m_renderer->preloadUITexture(srcPath);
            }
            else if ((item.type == AssetType::Model3D || item.type == AssetType::Material) && m_renderer)
            {
                thumbTex = m_renderer->generateAssetThumbnail(relPath, static_cast<int>(item.type));
            }

            WidgetElement tile = makeGridTile(
                "ContentBrowser.GridAsset." + relPath,
                item.name, iconFile,
                iconTintForAssetType(item.type), false, thumbTex);

            // Highlight selected asset
            if (relPath == m_selectedGridAsset)
            {
                tile.style.color = EditorTheme::Get().cbTileSelected;
            }

            // Unreferenced asset indicator
            if (referencedAssets.count(relPath) == 0 &&
                item.type != AssetType::Level && item.type != AssetType::Shader &&
                item.type != AssetType::NativeScript &&
                item.type != AssetType::Unknown)
            {
                WidgetElement badge{};
                badge.id = tile.id + ".Unref";
                badge.type = WidgetElementType::Text;
                badge.text = "\xe2\x97\x8f";
                badge.font = EditorTheme::Get().fontDefault;
                badge.fontSize = EditorTheme::Get().fontSizeSmall;
                badge.textAlignH = TextAlignH::Right;
                badge.textAlignV = TextAlignV::Top;
                badge.style.textColor = EditorTheme::Get().textMuted;
                badge.from = Vec2{ 0.75f, 0.0f };
                badge.to   = Vec2{ 1.0f, 0.15f };
                badge.runtimeOnly = true;
                tile.children.push_back(std::move(badge));
            }

            // Inline rename:
            if (m_renamingGridAsset && relPath == m_renameOriginalPath && tile.children.size() >= 2)
            {
                const std::string stem = std::filesystem::path(relPath).stem().string();
                WidgetElement entry{};
                entry.id = "ContentBrowser.RenameEntry";
                entry.type = WidgetElementType::EntryBar;
                entry.value = item.name;
                entry.font = EditorTheme::Get().fontDefault;
                entry.fontSize = EditorTheme::Get().fontSizeSmall;
                entry.style.textColor = EditorTheme::Get().textPrimary;
                entry.style.color = EditorTheme::Get().inputBackground;
                entry.style.hoverColor = EditorTheme::Get().inputBackgroundHover;
                entry.from = Vec2{ 0.0f, 0.65f };
                entry.to = Vec2{ 1.0f, 1.0f };
                entry.padding = Vec2{ 2.0f, 1.0f };
                entry.hitTestMode = HitTestMode::Enabled;
                entry.isFocused = true;
                entry.runtimeOnly = true;
                entry.onValueChanged = [this, relPath](const std::string& newName)
                {
                    m_uiManager->setFocusedEntry(nullptr);
                    m_renamingGridAsset = false;
                    if (!newName.empty() && newName != std::filesystem::path(relPath).stem().string())
                    {
                        if (AssetManager::Instance().renameAsset(relPath, newName))
                        {
                            m_uiManager->showToastMessage("Renamed to: " + newName, UIManager::kToastMedium);
                            m_selectedGridAsset.clear();
                        }
                        else
                        {
                            m_uiManager->showToastMessage("Rename failed.", UIManager::kToastMedium);
                        }
                    }
                    m_renameOriginalPath.clear();
                    refresh();
                };
                // Replace the label child (index 1) with the entry bar
                tile.children.back() = std::move(entry);
            }

            // Make asset tiles draggable
            tile.isDraggable = true;
            tile.dragPayload = std::to_string(static_cast<int>(item.type)) + "|" + relPath;

            // Single-click selects; double-click opens
            tile.onClicked = [this, relPath]()
            {
                m_selectedGridAsset = relPath;
                m_renamingGridAsset = false;
                m_renameOriginalPath.clear();
                refresh();
            };

            tile.onDoubleClicked = [this, relPath, assetType = item.type]()
            {
                if (assetType == AssetType::Model3D && m_uiManager->getRenderer())
                {
                    m_uiManager->getRenderer()->openMeshViewer(relPath);
                    return;
                }
                if (assetType == AssetType::Texture && m_uiManager->getRenderer())
                {
                    m_uiManager->getRenderer()->openTextureViewer(relPath);
                    return;
                }
                if (assetType == AssetType::Widget)
                {
                    m_uiManager->openWidgetEditorPopup(relPath);
                    return;
                }
                if (assetType == AssetType::Material)
                {
                    if (m_uiManager->getRenderer())
                        m_uiManager->getRenderer()->openMaterialEditorTab(relPath);
                    return;
                }
                if (assetType == AssetType::Prefab)
                {
                    m_uiManager->spawnPrefabAtPosition(relPath, Vec3{ 0.0f, 0.0f, 0.0f });
                    return;
                }
                if (assetType == AssetType::Entity)
                {
                    m_uiManager->openEntityEditorTab(relPath);
                    return;
                }
                if (assetType == AssetType::InputAction)
                {
                    m_uiManager->openInputActionEditorTab(relPath);
                    return;
                }
                if (assetType == AssetType::InputMapping)
                {
                    m_uiManager->openInputMappingEditorTab(relPath);
                    return;
                }
                if (assetType == AssetType::Audio)
                {
                    m_uiManager->openAudioPreviewTab(relPath);
                    return;
                }
                if (assetType == AssetType::Level)
                {
                    m_uiManager->requestLevelLoad(relPath);
                    return;
                }
                Logger::Instance().log(Logger::Category::UI,
                    "Content Browser: open asset '" + relPath + "'",
                    Logger::LogLevel::INFO);
                m_uiManager->showToastMessage("Open: " + relPath, UIManager::kToastMedium);
            };

            gridPanel->children.push_back(std::move(tile));
        }
        } // end else (not shaders)
    }

    widget->markLayoutDirty();
}

void ContentBrowserPanel::navigateByArrow(int dCol, int dRow)
{
    // Collect grid tile IDs (relPaths) from active Content Browser widget
    std::vector<std::string> tilePaths;
    const std::string tilePrefix = "ContentBrowser.GridAsset.";
    for (auto& we : m_uiManager->getRegisteredWidgetsMutable())
    {
        if (!we.widget)
            continue;
        if (!we.tabId.empty() && we.tabId != m_uiManager->getActiveTabId())
            continue;
        const std::function<void(WidgetElement&)> collect = [&](WidgetElement& el)
        {
            if (el.id.rfind(tilePrefix, 0) == 0)
            {
                tilePaths.push_back(el.id.substr(tilePrefix.size()));
            }
            for (auto& child : el.children)
            {
                collect(child);
            }
        };
        for (auto& el : we.widget->getElementsMutable())
        {
            collect(el);
        }
    }
    if (tilePaths.empty())
        return;

    // Find current selection index
    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(tilePaths.size()); ++i)
    {
        if (tilePaths[i] == m_selectedGridAsset)
        {
            currentIdx = i;
            break;
        }
    }
    if (currentIdx < 0)
    {
        m_selectedGridAsset = tilePaths[0];
        refresh();
        return;
    }

    // Estimate columns per row from the grid (default 4, but could vary)
    int cols = 4;
    if (auto* grid = m_uiManager->findElementById("ContentBrowser.Grid"))
    {
        if (grid->columns > 0)
            cols = grid->columns;
        else if (grid->computedSizePixels.x > 0.0f)
        {
            const float tileW = EditorTheme::Scaled(100.0f);
            if (tileW > 0.0f)
                cols = std::max(1, static_cast<int>(grid->computedSizePixels.x / tileW));
        }
    }

    int newIdx = currentIdx + dCol + dRow * cols;
    newIdx = std::clamp(newIdx, 0, static_cast<int>(tilePaths.size()) - 1);
    if (newIdx != currentIdx)
    {
        m_selectedGridAsset = tilePaths[newIdx];
        refresh();
    }
}

bool ContentBrowserPanel::isOverGrid(const Vec2& screenPos) const
{
    const auto* entry = m_uiManager->findWidgetEntry("ContentBrowser");
    if (!entry || !entry->widget)
        return false;

    for (const auto& element : entry->widget->getElements())
    {
        const std::function<bool(const WidgetElement&)> findGrid =
            [&](const WidgetElement& el) -> bool
            {
                if (el.id == "ContentBrowser.Grid" && el.hasComputedPosition && el.hasComputedSize)
                {
                    return screenPos.x >= el.computedPositionPixels.x &&
                           screenPos.x <= el.computedPositionPixels.x + el.computedSizePixels.x &&
                           screenPos.y >= el.computedPositionPixels.y &&
                           screenPos.y <= el.computedPositionPixels.y + el.computedSizePixels.y;
                }
                for (const auto& child : el.children)
                {
                    if (findGrid(child))
                        return true;
                }
                return false;
            };
        if (findGrid(element))
            return true;
    }
    return false;
}
