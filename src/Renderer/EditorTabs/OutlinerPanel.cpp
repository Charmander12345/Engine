#include "OutlinerPanel.h"
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
#include "../UIWidgets/EntryBarWidget.h"
#include "../UIWidgets/DropDownWidget.h"
#include "../UIWidgets/ColorPickerWidget.h"
#include "../../Core/UndoRedoManager.h"
#include <fstream>
#include <filesystem>

#if ENGINE_EDITOR

namespace {

static WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
{
    for (auto& el : elements)
    {
        if (el.id == id) return &el;
        auto* found = FindElementById(el.children, id);
        if (found) return found;
    }
    return nullptr;
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
    // No own text/image
    btn.text = "";

    const float indentFrac = static_cast<float>(indentLevel) * 0.04f; // 4% per level

    const float rowHeight = theme.rowHeightSmall;
    const float iconPad  = 0.1f;                                     // 10% vertical padding
    const float iconSize = rowHeight * (1.0f - 2.0f * iconPad);      // square icon in pixels

    // Icon child (left side, square ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â pixel-sized so it stays 1:1 regardless of button width)
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
        icon.to   = Vec2{ indentFrac + 0.01f, 1.0f - iconPad };     // width comes from minSize
        icon.runtimeOnly = true;
        btn.children.push_back(std::move(icon));
    }

    // Label child (rest of the button)
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

static WidgetElement* FindFirstStackPanel(std::vector<WidgetElement>& elements)
{
    const std::function<WidgetElement*(WidgetElement&)> findRecursive =
        [&](WidgetElement& element) -> WidgetElement*
    {
        if (element.type == WidgetElementType::StackPanel)
        {
            return &element;
        }
        for (auto& child : element.children)
        {
            if (auto* match = findRecursive(child))
            {
                return match;
            }
        }
        return nullptr;
    };

    for (auto& element : elements)
    {
        if (auto* match = findRecursive(element))
        {
            return match;
        }
    }
    return nullptr;
}

static const char* iconForEntity(ECS::Entity entity)
{
    auto& ecs = ECS::ECSManager::Instance();
    if (ecs.hasComponent<ECS::LightComponent>(entity))       return "light.png";
    if (ecs.hasComponent<ECS::CameraComponent>(entity))      return "camera.png";
    if (ecs.hasComponent<ECS::MeshComponent>(entity))        return "model3d.png";
    if (ecs.hasComponent<ECS::LogicComponent>(entity))       return "script.png";
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity)) return "level.png";
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))     return "entity.png";
    return "entity.png";
}

static Vec4 iconTintForEntity(ECS::Entity entity)
{
    auto& ecs = ECS::ECSManager::Instance();
    if (ecs.hasComponent<ECS::LightComponent>(entity))       return Vec4{ 1.00f, 0.90f, 0.30f, 1.0f };
    if (ecs.hasComponent<ECS::CameraComponent>(entity))      return Vec4{ 0.40f, 0.85f, 0.40f, 1.0f };
    if (ecs.hasComponent<ECS::MeshComponent>(entity))        return Vec4{ 0.50f, 0.80f, 0.90f, 1.0f };
    if (ecs.hasComponent<ECS::LogicComponent>(entity))       return Vec4{ 0.40f, 0.90f, 0.40f, 1.0f };
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity)) return Vec4{ 0.65f, 0.85f, 0.45f, 1.0f };
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))     return Vec4{ 0.75f, 0.50f, 1.00f, 1.0f };
    return Vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
}

template<typename CompT>
void setCompFieldWithUndo(unsigned int entity, const std::string& desc,
    std::function<void(CompT&)> applyChange)
{
    auto invalidatePhysics = [entity]()
    {
        if constexpr (std::is_same_v<CompT, ECS::CollisionComponent>
            || std::is_same_v<CompT, ECS::PhysicsComponent>
            || std::is_same_v<CompT, ECS::ConstraintComponent>
            || std::is_same_v<CompT, ECS::TransformComponent>
            || std::is_same_v<CompT, ECS::HeightFieldComponent>)
        {
            DiagnosticsManager::Instance().invalidatePhysicsEntity(entity);
        }
    };

    auto e = static_cast<ECS::Entity>(entity);
    auto& ecs = ECS::ECSManager::Instance();
    auto* comp = ecs.getComponent<CompT>(e);
    if (!comp) return;
    CompT oldComp = *comp;
    CompT newComp = *comp;
    applyChange(newComp);
    ecs.setComponent<CompT>(e, newComp);
    invalidatePhysics();
    DiagnosticsManager::Instance().invalidateEntity(entity);
    UndoRedoManager::Instance().pushCommand({
        desc,
        [entity, newComp, invalidatePhysics]() {
            auto e2 = static_cast<ECS::Entity>(entity);
            auto& ecs2 = ECS::ECSManager::Instance();
            if (ecs2.hasComponent<CompT>(e2))
                ecs2.setComponent<CompT>(e2, newComp);
            invalidatePhysics();
            DiagnosticsManager::Instance().invalidateEntity(entity);
        },
        [entity, oldComp, invalidatePhysics]() {
            auto e2 = static_cast<ECS::Entity>(entity);
            auto& ecs2 = ECS::ECSManager::Instance();
            if (ecs2.hasComponent<CompT>(e2))
                ecs2.setComponent<CompT>(e2, oldComp);
            invalidatePhysics();
            DiagnosticsManager::Instance().invalidateEntity(entity);
        }
    });
}

} // namespace

OutlinerPanel::OutlinerPanel(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer) {}

void OutlinerPanel::setLevel(EngineLevel* level)
{
    m_level = level;
    if (m_level)
    {
        m_level->registerEntityListChangedCallback([this]()
            {
                refresh();
            });
    }
    refresh();
}

void OutlinerPanel::refresh()
{
    if (auto* entry = m_uiManager->findWidgetEntry("WorldOutliner"))
    {
        if (entry->widget)
        {
            Logger::Instance().log(Logger::Category::UI, "Refreshing WorldOutliner widget.", Logger::LogLevel::INFO);
            populateWidget(entry->widget);
        }
    }
}


void OutlinerPanel::populateWidget(const std::shared_ptr<EditorWidget>& widget)
{
	if (!widget)
	{
		Logger::Instance().log(Logger::Category::UI, "WorldOutliner widget is null.", Logger::LogLevel::WARNING);
		return;
	}

	auto& diagnostics = DiagnosticsManager::Instance();
	auto* level = m_level ? m_level : diagnostics.getActiveLevelSoft();
    if (!level)
    {
		Logger::Instance().log(Logger::Category::UI, "No active level for WorldOutliner.", Logger::LogLevel::WARNING);
        auto& elements = widget->getElementsMutable();
        if (auto* listPanel = FindElementById(elements, "Outliner.EntityList"))
        {
            listPanel->children.clear();
            widget->markLayoutDirty();
        }
        return;
    }

    if (!diagnostics.isScenePrepared())
    {
		Logger::Instance().log(Logger::Category::UI, "Scene not prepared for WorldOutliner.", Logger::LogLevel::WARNING);
    }
    auto& elements = widget->getElementsMutable();
    WidgetElement* listPanel = FindElementById(elements, "Outliner.EntityList");
    if (!listPanel)
    {
        listPanel = FindFirstStackPanel(elements);
        if (listPanel && listPanel->id.empty())
        {
            listPanel->id = "Outliner.EntityList";
        }
    }
    if (!listPanel)
    {
        Logger::Instance().log(Logger::Category::UI, "WorldOutliner list panel not found.", Logger::LogLevel::WARNING);
        return;
    }

    listPanel->children.clear();
    if (listPanel->from.y <= 0.1f)
    {
        listPanel->from.y = 0.12f;
    }
    // Limit list to the top portion; EntityDetails occupies the bottom half (splitRatio = 0.45)
    listPanel->to.y = 0.44f;
    listPanel->scrollable = true;
    listPanel->fillX = true;
    listPanel->fillY = false;
    listPanel->sizeToContent = false;
    listPanel->padding = EditorTheme::Scaled(Vec2{ 2.0f, 2.0f });

    auto& ecs = ECS::ECSManager::Instance();
    ECS::Schema schema;
    const auto entities = ecs.getEntitiesMatchingSchema(schema);
    bool hasSelectedEntity = false;
    for (const auto entity : entities)
    {
        std::string label = "Entity " + std::to_string(entity);
        if (const auto* nameComponent = ecs.getComponent<ECS::NameComponent>(entity))
        {
            if (!nameComponent->displayName.empty())
            {
                label = nameComponent->displayName;
            }
        }

        WidgetElement button = makeTreeRow(
            "Outliner.Entity." + std::to_string(entity), label,
            iconForEntity(entity), false, 0, iconTintForEntity(entity));
        button.from = Vec2{ 0.0f, 0.0f };
        button.to = Vec2{ 1.0f, 1.0f };
        button.onClicked = [this, entity]()
            {
                m_selectedEntity = entity;
                populateDetails(entity);
            };
        listPanel->children.push_back(std::move(button));
        Logger::Instance().log(Logger::Category::UI,
            "WorldOutliner created button for entity " + std::to_string(entity) + " label=" + label,
            Logger::LogLevel::INFO);
        if (entity == m_selectedEntity)
        {
            hasSelectedEntity = true;
        }
    }

    if (!hasSelectedEntity)
    {
        m_selectedEntity = 0;
    }
    populateDetails(m_selectedEntity);

    widget->markLayoutDirty();
}


void OutlinerPanel::selectEntity(unsigned int entity)
{
    if (entity == m_selectedEntity)
        return;
    m_selectedEntity = entity;
    populateDetails(entity);
}


void OutlinerPanel::populateDetails(unsigned int entity)
{
    auto* detailsEntry = m_uiManager->findWidgetEntry("EntityDetails");
    if (!detailsEntry || !detailsEntry->widget)
    {
        return;
    }

    auto& elements = detailsEntry->widget->getElementsMutable();
    WidgetElement* detailsPanel = FindElementById(elements, "Details.Content");
    if (!detailsPanel)
    {
        return;
    }

    detailsPanel->children.clear();

    // Invalidate cached hover pointer ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ the old elements are destroyed.
    m_uiManager->invalidateHoveredElement();

    const auto makeTextLine = [](const std::string& text) -> WidgetElement
        {
            return EditorUIBuilder::makeLabel(text);
        };

    const auto sanitizeId = [](const std::string& text)
        {
            return EditorUIBuilder::sanitizeId(text);
        };

    const auto addSeparator = [&](const std::string& title, const std::vector<WidgetElement>& lines,
        std::function<void()> onRemove = {})
        {
            WidgetElement separatorEl = EditorUIBuilder::makeSection(sanitizeId(title), title, lines);

            if (onRemove)
            {
                // Find the header button (child index 1: divider=0, header=1, content=2)
                // and wrap it in a horizontal StackPanel with a remove button
                if (separatorEl.children.size() >= 2)
                {
                    WidgetElement originalHeader = std::move(separatorEl.children[1]);
                    originalHeader.fillX = true;

                    WidgetElement removeBtn = EditorUIBuilder::makeDangerButton(
                        "Details.Remove." + sanitizeId(title), "X", {}, EditorTheme::Scaled(Vec2{ 22.0f, 22.0f }));
                    removeBtn.fillX = false;
                    removeBtn.fontSize = EditorTheme::Get().fontSizeSmall;
                    removeBtn.tooltipText = "Remove " + title;

                    const std::string compTitle = title;
                    removeBtn.onClicked = [this, compTitle, onRemove]()
                    {
                        m_uiManager->showConfirmDialog("Remove " + compTitle + " component?",
                            [onRemove]() { onRemove(); },
                            []() {});
                    };

                    WidgetElement headerRow = EditorUIBuilder::makeHorizontalRow(
                        "Details.HeaderRow." + sanitizeId(title));
                    headerRow.children.push_back(std::move(originalHeader));
                    headerRow.children.push_back(std::move(removeBtn));

                    separatorEl.children[1] = std::move(headerRow);
                }
            }

            detailsPanel->children.push_back(std::move(separatorEl));
        };

    auto fmtF = [](float v) -> std::string {
        return EditorUIBuilder::fmtFloat(v);
    };

    const auto makeFloatEntry = [&](const std::string& id, const std::string& label, float value,
        std::function<void(float)> onChange) -> WidgetElement
    {
        return EditorUIBuilder::makeFloatRow(id, label, value,
            [onChange = std::move(onChange)](float v) {
                onChange(v);
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });
    };

    const auto makeVec3Row = [&](const std::string& idPrefix, const std::string& label, const float values[3],
        std::function<void(int, float)> onChange) -> WidgetElement
    {
        return EditorUIBuilder::makeVec3Row(idPrefix, label, values,
            [onChange = std::move(onChange)](int axis, float v) {
                onChange(axis, v);
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });
    };

    const auto makeCheckBoxRow = [&](const std::string& id, const std::string& label, bool checked,
        std::function<void(bool)> onChange) -> WidgetElement
    {
        return EditorUIBuilder::makeCheckBox(id, label, checked,
            [onChange = std::move(onChange)](bool val) {
                onChange(val);
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });
    };

    if (entity == 0)
    {
        detailsPanel->children.push_back(makeTextLine("Select an entity to see details."));
        detailsEntry->widget->markLayoutDirty();
        return;
    }

    auto& ecs = ECS::ECSManager::Instance();

    const auto getEntityLabel = [&ecs](ECS::Entity id) -> std::string
    {
        if (id == 0)
            return "(None)";

        std::string label = "Entity " + std::to_string(id);
        if (const auto* nc = ecs.getComponent<ECS::NameComponent>(id))
        {
            if (!nc->displayName.empty())
                label = nc->displayName;
        }
        return label + " (#" + std::to_string(id) + ")";
    };

    std::vector<WidgetElement> entityLines;
    entityLines.push_back(makeTextLine("ID: " + std::to_string(entity)));
    {
        std::string nameValue = "<unnamed>";
        if (const auto* nameComponent = ecs.getComponent<ECS::NameComponent>(entity))
        {
            if (!nameComponent->displayName.empty())
            {
                nameValue = nameComponent->displayName;
            }
        }
        WidgetElement nameLine = makeTextLine("Name: " + nameValue);
        nameLine.id = "Details.Entity.NameLabel";
        entityLines.push_back(std::move(nameLine));
    }
    addSeparator("Entity", entityLines);

    if (const auto* nameComponent = ecs.getComponent<ECS::NameComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        auto& theme = EditorTheme::Get();
        EntryBarWidget nameEntry;
        nameEntry.setValue(nameComponent->displayName);
        nameEntry.setFont(theme.fontDefault);
        nameEntry.setFontSize(theme.fontSizeSmall);
        nameEntry.setMinSize(Vec2{ 0.0f, theme.rowHeightSmall });
        nameEntry.setPadding(theme.paddingNormal);
        nameEntry.setOnValueChanged([this, entity](const std::string& val) {
            auto& ecs = ECS::ECSManager::Instance();
            auto* comp = ecs.getComponent<ECS::NameComponent>(entity);
            if (!comp) return;
            ECS::NameComponent oldComp = *comp;
            ECS::NameComponent newComp = *comp;
            newComp.displayName = val;
            ecs.setComponent<ECS::NameComponent>(entity, newComp);
            // Update the entity header label in the details panel
            if (auto* lbl = m_uiManager->findElementById("Details.Entity.NameLabel"))
            {
                lbl->text = "Name: " + (val.empty() ? std::string("<unnamed>") : val);
            }
            refresh();
            UndoRedoManager::Instance().pushCommand({
                "Rename Entity",
                [this, entity, newComp]() {
                    auto& ecs2 = ECS::ECSManager::Instance();
                    if (ecs2.hasComponent<ECS::NameComponent>(entity))
                        ecs2.setComponent<ECS::NameComponent>(entity, newComp);
                    refresh();
                },
                [this, entity, oldComp]() {
                    auto& ecs2 = ECS::ECSManager::Instance();
                    if (ecs2.hasComponent<ECS::NameComponent>(entity))
                        ecs2.setComponent<ECS::NameComponent>(entity, oldComp);
                    refresh();
                }
            });
        });
        WidgetElement nameEl = nameEntry.toElement();
        nameEl.id = "Details.Name.Entry";
        nameEl.fillX = true;
        nameEl.runtimeOnly = true;
        lines.push_back(std::move(nameEl));

        addSeparator("Name", lines, [this, entity, saved = *nameComponent]() {
            ECS::ECSManager::Instance().removeComponent<ECS::NameComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            refresh();
            UndoRedoManager::Instance().pushCommand({
                "Remove Name",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::NameComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::NameComponent>(entity, saved); }
            });
        });
    }

    if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        lines.push_back(makeVec3Row("Details.Transform.Pos", "Position", transform->position,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::TransformComponent>(entity, "Change Position",
                    [axis, val](ECS::TransformComponent& c) { c.position[axis] = val; });
            }));

        lines.push_back(makeVec3Row("Details.Transform.Rot", "Rotation", transform->rotation,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::TransformComponent>(entity, "Change Rotation",
                    [axis, val](ECS::TransformComponent& c) { c.rotation[axis] = val; });
            }));

        lines.push_back(makeVec3Row("Details.Transform.Scale", "Scale", transform->scale,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::TransformComponent>(entity, "Change Scale",
                    [axis, val](ECS::TransformComponent& c) { c.scale[axis] = val; });
            }));

        addSeparator("Transform", lines, [this, entity, saved = *transform]() {
            ECS::ECSManager::Instance().removeComponent<ECS::TransformComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Transform",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::TransformComponent>(entity); DiagnosticsManager::Instance().invalidateEntity(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::TransformComponent>(entity, saved); DiagnosticsManager::Instance().invalidateEntity(entity); }
            });
        });
    }

    if (const auto* mesh = ecs.getComponent<ECS::MeshComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Asset Path: " + mesh->meshAssetPath));
        lines.push_back(makeTextLine("Asset Id: " + std::to_string(mesh->meshAssetId)));

        // Dropdown to select a different mesh asset
        {
            DropdownButtonWidget dropdown;
            dropdown.setText(mesh->meshAssetPath.empty() ? "Select Mesh..." : mesh->meshAssetPath);
            dropdown.setFont(EditorTheme::Get().fontDefault);
            dropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            dropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            dropdown.setPadding(EditorTheme::Get().paddingNormal);
            dropdown.setBackgroundColor(EditorTheme::Get().dropdownBackground);
            dropdown.setHoverColor(EditorTheme::Get().dropdownHover);
            dropdown.setTextColor(EditorTheme::Get().dropdownText);

            const auto& registry = AssetManager::Instance().getAssetRegistry();
            for (const auto& reg : registry)
            {
                if (reg.type == AssetType::Model3D)
                {
                    const std::string assetPath = reg.path;
                    dropdown.addItem(reg.name.empty() ? reg.path : reg.name, [this, entity, assetPath]()
                    {
                        applyAssetToEntity(AssetType::Model3D, assetPath, entity);
                    });
                }
            }
            WidgetElement dropdownEl = dropdown.toElement();
            dropdownEl.id = "Details.Mesh.Dropdown";
            dropdownEl.fillX = true;
            dropdownEl.runtimeOnly = true;
            lines.push_back(std::move(dropdownEl));
        }

        addSeparator("Mesh", lines, [this, entity, saved = *mesh]() {
            ECS::ECSManager::Instance().removeComponent<ECS::MeshComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Mesh",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::MeshComponent>(entity); DiagnosticsManager::Instance().invalidateEntity(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::MeshComponent>(entity, saved); DiagnosticsManager::Instance().invalidateEntity(entity); }
            });
        });
    }

    if (const auto* material = ecs.getComponent<ECS::MaterialComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Asset Path: " + material->materialAssetPath));
        lines.push_back(makeTextLine("Asset Id: " + std::to_string(material->materialAssetId)));

        // Dropdown to select a different material asset
        {
            DropdownButtonWidget dropdown;
            dropdown.setText(material->materialAssetPath.empty() ? "Select Material..." : material->materialAssetPath);
            dropdown.setFont(EditorTheme::Get().fontDefault);
            dropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            dropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            dropdown.setPadding(EditorTheme::Get().paddingNormal);
            dropdown.setBackgroundColor(EditorTheme::Get().dropdownBackground);
            dropdown.setHoverColor(EditorTheme::Get().dropdownHover);
            dropdown.setTextColor(EditorTheme::Get().dropdownText);

            const auto& registry = AssetManager::Instance().getAssetRegistry();
            for (const auto& reg : registry)
            {
                if (reg.type == AssetType::Material)
                {
                    const std::string assetPath = reg.path;
                    dropdown.addItem(reg.name.empty() ? reg.path : reg.name, [this, entity, assetPath]()
                    {
                        applyAssetToEntity(AssetType::Material, assetPath, entity);
                    });
                }
            }
            WidgetElement dropdownEl = dropdown.toElement();
            dropdownEl.id = "Details.Material.Dropdown";
            dropdownEl.fillX = true;
            dropdownEl.runtimeOnly = true;
            lines.push_back(std::move(dropdownEl));
        }

        // Material property overrides (Metallic, Roughness, Specular Multiplier)
        lines.push_back(makeFloatEntry("Details.Material.Metallic", "Metallic", material->overrides.metallic,
            [entity](float val) {
                setCompFieldWithUndo<ECS::MaterialComponent>(entity, "Change Metallic",
                    [val](ECS::MaterialComponent& c) { c.overrides.metallic = val; c.overrides.hasMetallic = true; });
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }));

        lines.push_back(makeFloatEntry("Details.Material.Roughness", "Roughness", material->overrides.roughness,
            [entity](float val) {
                setCompFieldWithUndo<ECS::MaterialComponent>(entity, "Change Roughness",
                    [val](ECS::MaterialComponent& c) { c.overrides.roughness = val; c.overrides.hasRoughness = true; });
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }));

        lines.push_back(makeFloatEntry("Details.Material.SpecularMultiplier", "Specular Multiplier", material->overrides.specularMultiplier,
            [entity](float val) {
                setCompFieldWithUndo<ECS::MaterialComponent>(entity, "Change Specular Multiplier",
                    [val](ECS::MaterialComponent& c) { c.overrides.specularMultiplier = val; c.overrides.hasSpecularMultiplier = true; });
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }));

        addSeparator("Material", lines, [this, entity, saved = *material]() {
            ECS::ECSManager::Instance().removeComponent<ECS::MaterialComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Material",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::MaterialComponent>(entity); DiagnosticsManager::Instance().invalidateEntity(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::MaterialComponent>(entity, saved); DiagnosticsManager::Instance().invalidateEntity(entity); }
            });
        });
    }

    if (const auto* light = ecs.getComponent<ECS::LightComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        // Light Type dropdown
        {
            int currentIdx = static_cast<int>(light->type);
            DropDownWidget typeDropdown;
            typeDropdown.setItems({ "Point", "Directional", "Spot" });
            typeDropdown.setSelectedIndex(currentIdx);
            typeDropdown.setFont(EditorTheme::Get().fontDefault);
            typeDropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            typeDropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            typeDropdown.setPadding(EditorTheme::Get().paddingNormal);
            typeDropdown.setOnSelectionChanged([entity](int idx) {
                setCompFieldWithUndo<ECS::LightComponent>(entity, "Change Light Type",
                    [idx](ECS::LightComponent& c) { c.type = static_cast<ECS::LightComponent::LightType>(idx); });
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Type");
            lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            WidgetElement ddEl = typeDropdown.toElement();
            ddEl.id = "Details.Light.Type";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));
            lines.push_back(std::move(row));
        }

        // Light Color picker
        {
            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Color");
            lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            ColorPickerWidget cp;
            cp.setColor(Vec4{ light->color[0], light->color[1], light->color[2], 1.0f });
            cp.setCompact(true);
            cp.setMinSize(EditorTheme::Scaled(Vec2{ 0.0f, 20.0f }));
            cp.setOnColorChanged([entity](const Vec4& c) {
                setCompFieldWithUndo<ECS::LightComponent>(entity, "Change Light Color",
                    [c](ECS::LightComponent& comp) { comp.color[0] = c.x; comp.color[1] = c.y; comp.color[2] = c.z; });
            });

            WidgetElement cpEl = cp.toElement();
            cpEl.id = "Details.Light.Color";
            cpEl.fillX = true;
            cpEl.runtimeOnly = true;
            row.children.push_back(std::move(cpEl));
            lines.push_back(std::move(row));
        }

        lines.push_back(makeFloatEntry("Details.Light.Intensity", "Intensity", light->intensity,
            [entity](float val) {
                setCompFieldWithUndo<ECS::LightComponent>(entity, "Change Intensity",
                    [val](ECS::LightComponent& c) { c.intensity = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Light.Range", "Range", light->range,
            [entity](float val) {
                setCompFieldWithUndo<ECS::LightComponent>(entity, "Change Range",
                    [val](ECS::LightComponent& c) { c.range = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Light.SpotAngle", "Spot Angle", light->spotAngle,
            [entity](float val) {
                setCompFieldWithUndo<ECS::LightComponent>(entity, "Change Spot Angle",
                    [val](ECS::LightComponent& c) { c.spotAngle = val; });
            }));

        addSeparator("Light", lines, [this, entity, saved = *light]() {
            ECS::ECSManager::Instance().removeComponent<ECS::LightComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Light",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::LightComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::LightComponent>(entity, saved); }
            });
        });
    }

    if (const auto* camera = ecs.getComponent<ECS::CameraComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        lines.push_back(makeFloatEntry("Details.Camera.FOV", "FOV", camera->fov,
            [entity](float val) {
                setCompFieldWithUndo<ECS::CameraComponent>(entity, "Change FOV",
                    [val](ECS::CameraComponent& c) { c.fov = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Camera.NearClip", "Near Clip", camera->nearClip,
            [entity](float val) {
                setCompFieldWithUndo<ECS::CameraComponent>(entity, "Change Near Clip",
                    [val](ECS::CameraComponent& c) { c.nearClip = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Camera.FarClip", "Far Clip", camera->farClip,
            [entity](float val) {
                setCompFieldWithUndo<ECS::CameraComponent>(entity, "Change Far Clip",
                    [val](ECS::CameraComponent& c) { c.farClip = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Camera.IsActive", "Active", camera->isActive,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::CameraComponent>(entity, "Change Camera Active",
                    [val](ECS::CameraComponent& c) { c.isActive = val; });
            }));

        addSeparator("Camera", lines, [this, entity, saved = *camera]() {
            ECS::ECSManager::Instance().removeComponent<ECS::CameraComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Camera",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::CameraComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::CameraComponent>(entity, saved); }
            });
        });
    }

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Collision Component ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    if (const auto* collision = ecs.getComponent<ECS::CollisionComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        // Collider Type dropdown
        {
            int currentIdx = static_cast<int>(collision->colliderType);
            DropDownWidget colliderDropdown;
            colliderDropdown.setItems({ "Box", "Sphere", "Capsule", "Cylinder", "Mesh", "HeightField" });
            colliderDropdown.setSelectedIndex(currentIdx);
            colliderDropdown.setFont(EditorTheme::Get().fontDefault);
            colliderDropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            colliderDropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            colliderDropdown.setPadding(EditorTheme::Get().paddingNormal);
            colliderDropdown.setOnSelectionChanged([entity](int idx) {
                setCompFieldWithUndo<ECS::CollisionComponent>(entity, "Change Collider Type",
                    [idx](ECS::CollisionComponent& c) { c.colliderType = static_cast<ECS::CollisionComponent::ColliderType>(idx); });
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Collider");
            lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            WidgetElement ddEl = colliderDropdown.toElement();
            ddEl.id = "Details.Collision.Collider";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));
            lines.push_back(std::move(row));
        }

        lines.push_back(makeVec3Row("Details.Collision.Size", "Size", collision->colliderSize,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::CollisionComponent>(entity, "Change Collider Size",
                    [axis, val](ECS::CollisionComponent& c) { c.colliderSize[axis] = val; });
            }));

        lines.push_back(makeVec3Row("Details.Collision.Offset", "Offset", collision->colliderOffset,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::CollisionComponent>(entity, "Change Collider Offset",
                    [axis, val](ECS::CollisionComponent& c) { c.colliderOffset[axis] = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Collision.Restitution", "Restitution", collision->restitution,
            [entity](float val) {
                setCompFieldWithUndo<ECS::CollisionComponent>(entity, "Change Restitution",
                    [val](ECS::CollisionComponent& c) { c.restitution = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Collision.Friction", "Friction", collision->friction,
            [entity](float val) {
                setCompFieldWithUndo<ECS::CollisionComponent>(entity, "Change Friction",
                    [val](ECS::CollisionComponent& c) { c.friction = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Collision.Sensor", "Is Sensor", collision->isSensor,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::CollisionComponent>(entity, "Change Is Sensor",
                    [val](ECS::CollisionComponent& c) { c.isSensor = val; });
            }));

        // Auto-Fit Collider button
        if (ecs.hasComponent<ECS::MeshComponent>(entity))
        {
            WidgetElement autoFitEl = EditorUIBuilder::makeButton(
                "Details.Collision.AutoFit", "Auto-Fit Collider",
                [this, entity]() {
                    if (m_uiManager->autoFitColliderForEntity(entity))
                    {
                        DiagnosticsManager::Instance().invalidateEntity(entity);
                        if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
                        populateDetails(entity);
                        m_uiManager->showToastMessage("Auto-fitted collider from mesh AABB.", UIManager::kToastShort);
                    }
                    else
                    {
                        m_uiManager->showToastMessage("No mesh data available for auto-fit.", UIManager::kToastMedium);
                    }
                },
                Vec2{ 0.0f, 22.0f * EditorTheme::Get().dpiScale });
            autoFitEl.fillX = true;
            autoFitEl.runtimeOnly = true;
            lines.push_back(std::move(autoFitEl));
        }

        addSeparator("Collision", lines, [this, entity, saved = *collision]() {
            ECS::ECSManager::Instance().removeComponent<ECS::CollisionComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Collision",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::CollisionComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::CollisionComponent>(entity, saved); }
            });
        });
    }

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Physics Component ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    if (const auto* physics = ecs.getComponent<ECS::PhysicsComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        // Motion Type dropdown
        {
            int currentIdx = static_cast<int>(physics->motionType);
            DropDownWidget motionDropdown;
            motionDropdown.setItems({ "Static", "Kinematic", "Dynamic" });
            motionDropdown.setSelectedIndex(currentIdx);
            motionDropdown.setFont(EditorTheme::Get().fontDefault);
            motionDropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            motionDropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            motionDropdown.setPadding(EditorTheme::Get().paddingNormal);
            motionDropdown.setOnSelectionChanged([entity](int idx) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Motion Type",
                    [idx](ECS::PhysicsComponent& c) { c.motionType = static_cast<ECS::PhysicsComponent::MotionType>(idx); });
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Motion Type");
            lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            WidgetElement ddEl = motionDropdown.toElement();
            ddEl.id = "Details.Physics.MotionType";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));
            lines.push_back(std::move(row));
        }

        lines.push_back(makeFloatEntry("Details.Physics.Mass", "Mass", physics->mass,
            [entity](float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Mass",
                    [val](ECS::PhysicsComponent& c) { c.mass = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Physics.GravityFactor", "Gravity Factor", physics->gravityFactor,
            [entity](float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Gravity Factor",
                    [val](ECS::PhysicsComponent& c) { c.gravityFactor = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Physics.LinearDamping", "Linear Damping", physics->linearDamping,
            [entity](float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Linear Damping",
                    [val](ECS::PhysicsComponent& c) { c.linearDamping = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Physics.AngularDamping", "Angular Damping", physics->angularDamping,
            [entity](float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Angular Damping",
                    [val](ECS::PhysicsComponent& c) { c.angularDamping = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Physics.MaxLinVel", "Max Linear Vel", physics->maxLinearVelocity,
            [entity](float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Max Linear Vel",
                    [val](ECS::PhysicsComponent& c) { c.maxLinearVelocity = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Physics.MaxAngVel", "Max Angular Vel", physics->maxAngularVelocity,
            [entity](float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Max Angular Vel",
                    [val](ECS::PhysicsComponent& c) { c.maxAngularVelocity = val; });
            }));

        // Motion Quality dropdown
        {
            int currentIdx = static_cast<int>(physics->motionQuality);
            DropDownWidget mqDropdown;
            mqDropdown.setItems({ "Discrete", "LinearCast (CCD)" });
            mqDropdown.setSelectedIndex(currentIdx);
            mqDropdown.setFont(EditorTheme::Get().fontDefault);
            mqDropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            mqDropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            mqDropdown.setPadding(EditorTheme::Get().paddingNormal);
            mqDropdown.setOnSelectionChanged([entity](int idx) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Motion Quality",
                    [idx](ECS::PhysicsComponent& c) { c.motionQuality = static_cast<ECS::PhysicsComponent::MotionQuality>(idx); });
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Motion Quality");
            lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            WidgetElement ddEl = mqDropdown.toElement();
            ddEl.id = "Details.Physics.MotionQuality";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));
            lines.push_back(std::move(row));
        }

        lines.push_back(makeCheckBoxRow("Details.Physics.AllowSleep", "Allow Sleeping", physics->allowSleeping,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Allow Sleeping",
                    [val](ECS::PhysicsComponent& c) { c.allowSleeping = val; });
            }));

        lines.push_back(makeVec3Row("Details.Physics.Velocity", "Velocity", physics->velocity,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Velocity",
                    [axis, val](ECS::PhysicsComponent& c) { c.velocity[axis] = val; });
            }));

        lines.push_back(makeVec3Row("Details.Physics.AngularVel", "Angular Vel", physics->angularVelocity,
            [entity](int axis, float val) {
                setCompFieldWithUndo<ECS::PhysicsComponent>(entity, "Change Angular Velocity",
                    [axis, val](ECS::PhysicsComponent& c) { c.angularVelocity[axis] = val; });
            }));

        addSeparator("Physics", lines, [this, entity, saved = *physics]() {
            ECS::ECSManager::Instance().removeComponent<ECS::PhysicsComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Physics",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::PhysicsComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::PhysicsComponent>(entity, saved); }
            });
        });
    }

    if (const auto* logic = ecs.getComponent<ECS::LogicComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        if (!logic->scriptPath.empty())
            lines.push_back(makeTextLine("Python Script: " + logic->scriptPath));
        if (!logic->nativeClassName.empty())
            lines.push_back(makeTextLine("C++ Class: " + logic->nativeClassName));
        if (logic->scriptPath.empty() && logic->nativeClassName.empty())
            lines.push_back(makeTextLine("No script or class assigned."));

        // Dropdown to select a different Python script asset
        {
            DropdownButtonWidget dropdown;
            dropdown.setText(logic->scriptPath.empty() ? "Select Script..." : logic->scriptPath);
            dropdown.setFont(EditorTheme::Get().fontDefault);
            dropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            dropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            dropdown.setPadding(EditorTheme::Get().paddingNormal);
            dropdown.setBackgroundColor(EditorTheme::Get().dropdownBackground);
            dropdown.setHoverColor(EditorTheme::Get().dropdownHover);
            dropdown.setTextColor(EditorTheme::Get().dropdownText);

            const auto& registry = AssetManager::Instance().getAssetRegistry();
            for (const auto& reg : registry)
            {
                if (reg.type == AssetType::Script)
                {
                    const std::string assetPath = reg.path;
                    dropdown.addItem(reg.name.empty() ? reg.path : reg.name, [this, entity, assetPath]()
                    {
                        applyAssetToEntity(AssetType::Script, assetPath, entity);
                    });
                }
            }
            WidgetElement dropdownEl = dropdown.toElement();
            dropdownEl.id = "Details.Logic.ScriptDropdown";
            dropdownEl.fillX = true;
            dropdownEl.runtimeOnly = true;
            lines.push_back(std::move(dropdownEl));
        }

        // Dropdown to select a C++ native script class
        {
            const auto availableClasses = m_uiManager->getAvailableNativeClassNames();

            DropdownButtonWidget cppDropdown;
            cppDropdown.setText(logic->nativeClassName.empty() ? "Select C++ Class..." : logic->nativeClassName);
            cppDropdown.setFont(EditorTheme::Get().fontDefault);
            cppDropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
            cppDropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
            cppDropdown.setPadding(EditorTheme::Get().paddingNormal);
            cppDropdown.setBackgroundColor(EditorTheme::Get().dropdownBackground);
            cppDropdown.setHoverColor(EditorTheme::Get().dropdownHover);
            cppDropdown.setTextColor(EditorTheme::Get().dropdownText);

            // Option to clear the assigned class
            if (!logic->nativeClassName.empty())
            {
                cppDropdown.addItem("(None)", [this, entity]()
                {
                    auto& e = ECS::ECSManager::Instance();
                    auto* comp = e.getComponent<ECS::LogicComponent>(entity);
                    if (!comp) return;
                    ECS::LogicComponent oldComp = *comp;
                    ECS::LogicComponent newComp = *comp;
                    newComp.nativeClassName.clear();
                    e.setComponent<ECS::LogicComponent>(entity, newComp);
                    DiagnosticsManager::Instance().invalidateEntity(entity);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
                    populateDetails(entity);
                    UndoRedoManager::Instance().pushCommand({
                        "Clear C++ Class",
                        [entity, newComp]() {
                            auto& e2 = ECS::ECSManager::Instance();
                            if (e2.hasComponent<ECS::LogicComponent>(entity)) e2.setComponent(entity, newComp);
                        },
                        [entity, oldComp]() {
                            auto& e2 = ECS::ECSManager::Instance();
                            if (e2.hasComponent<ECS::LogicComponent>(entity)) e2.setComponent(entity, oldComp);
                        }
                    });
                });
            }

            for (const auto& className : availableClasses)
            {
                cppDropdown.addItem(className, [this, entity, className]()
                {
                    auto& e = ECS::ECSManager::Instance();
                    auto* comp = e.getComponent<ECS::LogicComponent>(entity);
                    if (!comp) return;
                    ECS::LogicComponent oldComp = *comp;
                    ECS::LogicComponent newComp = *comp;
                    newComp.nativeClassName = className;
                    e.setComponent<ECS::LogicComponent>(entity, newComp);
                    DiagnosticsManager::Instance().invalidateEntity(entity);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
                    populateDetails(entity);
                    m_uiManager->showToastMessage("C++ Class: " + className, UIManager::kToastShort);
                    UndoRedoManager::Instance().pushCommand({
                        "Assign C++ Class",
                        [entity, newComp]() {
                            auto& e2 = ECS::ECSManager::Instance();
                            if (e2.hasComponent<ECS::LogicComponent>(entity)) e2.setComponent(entity, newComp);
                            DiagnosticsManager::Instance().invalidateEntity(entity);
                        },
                        [entity, oldComp]() {
                            auto& e2 = ECS::ECSManager::Instance();
                            if (e2.hasComponent<ECS::LogicComponent>(entity)) e2.setComponent(entity, oldComp);
                            DiagnosticsManager::Instance().invalidateEntity(entity);
                        }
                    });
                });
            }

            if (availableClasses.empty())
            {
                cppDropdown.addItem("(Build C++ scripts first)", []() {});
            }

            WidgetElement cppDropdownEl = cppDropdown.toElement();
            cppDropdownEl.id = "Details.Logic.CppClassDropdown";
            cppDropdownEl.fillX = true;
            cppDropdownEl.runtimeOnly = true;
            lines.push_back(std::move(cppDropdownEl));
        }

        addSeparator("Logic", lines, [this, entity, saved = *logic]() {
            ECS::ECSManager::Instance().removeComponent<ECS::LogicComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Logic",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::LogicComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::LogicComponent>(entity, saved); }
            });
        });
    }

    // -- Particle Emitter Component ----------------------------------------
    if (const auto* emitter = ecs.getComponent<ECS::ParticleEmitterComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        lines.push_back(makeCheckBoxRow("Details.Particle.Enabled", "Enabled", emitter->enabled,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Enabled",
                    [val](ECS::ParticleEmitterComponent& c) { c.enabled = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Particle.Loop", "Loop", emitter->loop,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Loop",
                    [val](ECS::ParticleEmitterComponent& c) { c.loop = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.MaxParticles", "Max Particles", static_cast<float>(emitter->maxParticles),
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Max Particles",
                    [val](ECS::ParticleEmitterComponent& c) { c.maxParticles = static_cast<int>(val); });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.EmissionRate", "Emission Rate", emitter->emissionRate,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Emission Rate",
                    [val](ECS::ParticleEmitterComponent& c) { c.emissionRate = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.Lifetime", "Lifetime", emitter->lifetime,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Lifetime",
                    [val](ECS::ParticleEmitterComponent& c) { c.lifetime = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.Speed", "Speed", emitter->speed,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Speed",
                    [val](ECS::ParticleEmitterComponent& c) { c.speed = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.SpeedVariance", "Speed Variance", emitter->speedVariance,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Speed Variance",
                    [val](ECS::ParticleEmitterComponent& c) { c.speedVariance = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.Size", "Size", emitter->size,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Size",
                    [val](ECS::ParticleEmitterComponent& c) { c.size = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.SizeEnd", "Size End", emitter->sizeEnd,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Size End",
                    [val](ECS::ParticleEmitterComponent& c) { c.sizeEnd = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.Gravity", "Gravity", emitter->gravity,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Gravity",
                    [val](ECS::ParticleEmitterComponent& c) { c.gravity = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ConeAngle", "Cone Angle", emitter->coneAngle,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Cone Angle",
                    [val](ECS::ParticleEmitterComponent& c) { c.coneAngle = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorR", "Color R", emitter->colorR,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Color R",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorR = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorG", "Color G", emitter->colorG,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Color G",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorG = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorB", "Color B", emitter->colorB,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Color B",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorB = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorA", "Color A", emitter->colorA,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle Color A",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorA = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorEndR", "End Color R", emitter->colorEndR,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle End Color R",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorEndR = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorEndG", "End Color G", emitter->colorEndG,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle End Color G",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorEndG = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorEndB", "End Color B", emitter->colorEndB,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle End Color B",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorEndB = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Particle.ColorEndA", "End Color A", emitter->colorEndA,
            [entity](float val) {
                setCompFieldWithUndo<ECS::ParticleEmitterComponent>(entity, "Change Particle End Color A",
                    [val](ECS::ParticleEmitterComponent& c) { c.colorEndA = val; });
            }));

        // "Edit Particles" button ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ opens dedicated Particle Editor tab
        {
            WidgetElement editBtn = EditorUIBuilder::makePrimaryButton(
                "Details.Particle.EditBtn", "Edit Particles",
                [this, entity]() { m_uiManager->openParticleEditorTab(entity); },
                EditorTheme::Scaled(Vec2{ 0.0f, 26.0f }));
            editBtn.fillX = true;
            editBtn.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
            lines.push_back(std::move(editBtn));
        }

        addSeparator("Particle Emitter", lines, [this, entity, saved = *emitter]() {
            ECS::ECSManager::Instance().removeComponent<ECS::ParticleEmitterComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Particle Emitter",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ParticleEmitterComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::ParticleEmitterComponent>(entity, saved); }
            });
        });
    }

    // -- Animation Component ------------------------------------------------
    if (const auto* animComp = ecs.getComponent<ECS::AnimationComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        lines.push_back(makeFloatEntry("Details.Animation.Speed", "Speed", animComp->speed,
            [entity](float val) {
                setCompFieldWithUndo<ECS::AnimationComponent>(entity, "Change Animation Speed",
                    [val](ECS::AnimationComponent& c) { c.speed = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Animation.Loop", "Loop", animComp->loop,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::AnimationComponent>(entity, "Change Animation Loop",
                    [val](ECS::AnimationComponent& c) { c.loop = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Animation.Playing", "Playing", animComp->playing,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::AnimationComponent>(entity, "Change Animation Playing",
                    [val](ECS::AnimationComponent& c) { c.playing = val; });
            }));

        lines.push_back(makeFloatEntry("Details.Animation.ClipIndex", "Clip Index", static_cast<float>(animComp->currentClipIndex),
            [entity](float val) {
                setCompFieldWithUndo<ECS::AnimationComponent>(entity, "Change Clip Index",
                    [val](ECS::AnimationComponent& c) { c.currentClipIndex = static_cast<int>(val); });
            }));

        // "Edit Animation" button -> opens dedicated Animation Editor tab
        if (m_renderer && m_renderer->isEntitySkinned(entity))
        {
            WidgetElement editBtn = EditorUIBuilder::makePrimaryButton(
                "Details.Animation.EditBtn", "Edit Animation",
                [this, entity]() { m_uiManager->openAnimationEditorTab(entity); },
                EditorTheme::Scaled(Vec2{ 0.0f, 26.0f }));
            editBtn.fillX = true;
            editBtn.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
            lines.push_back(std::move(editBtn));
        }

        addSeparator("Animation", lines, [this, entity, saved = *animComp]() {
            ECS::ECSManager::Instance().removeComponent<ECS::AnimationComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Animation",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::AnimationComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::AnimationComponent>(entity, saved); }
            });
        });
    }

    // -- Audio Source Component ------------------------------------------------
    if (const auto* audioSrc = ecs.getComponent<ECS::AudioSourceComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        // Asset Path (read-only display)
        if (!audioSrc->assetPath.empty())
        {
            auto pathLabel = EditorUIBuilder::makeLabel("Asset: " + audioSrc->assetPath);
            pathLabel.padding = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
            lines.push_back(std::move(pathLabel));
        }

        lines.push_back(makeCheckBoxRow("Details.Audio.Is3D", "3D Audio", audioSrc->is3D,
            [this, entity](bool val) {
                setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio 3D",
                    [val](ECS::AudioSourceComponent& c) { c.is3D = val; });
                populateDetails(entity);
            }));

        if (audioSrc->is3D)
        {
            lines.push_back(makeFloatEntry("Details.Audio.MinDistance", "Min Distance", audioSrc->minDistance,
                [entity](float val) {
                    setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio Min Distance",
                        [val](ECS::AudioSourceComponent& c) { c.minDistance = val; });
                }));

            lines.push_back(makeFloatEntry("Details.Audio.MaxDistance", "Max Distance", audioSrc->maxDistance,
                [entity](float val) {
                    setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio Max Distance",
                        [val](ECS::AudioSourceComponent& c) { c.maxDistance = val; });
                }));

            lines.push_back(makeFloatEntry("Details.Audio.Rolloff", "Rolloff Factor", audioSrc->rolloffFactor,
                [entity](float val) {
                    setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio Rolloff",
                        [val](ECS::AudioSourceComponent& c) { c.rolloffFactor = val; });
                }));
        }

        lines.push_back(makeFloatEntry("Details.Audio.Gain", "Volume", audioSrc->gain,
            [entity](float val) {
                setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio Volume",
                    [val](ECS::AudioSourceComponent& c) { c.gain = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Audio.Loop", "Loop", audioSrc->loop,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio Loop",
                    [val](ECS::AudioSourceComponent& c) { c.loop = val; });
            }));

        lines.push_back(makeCheckBoxRow("Details.Audio.AutoPlay", "Auto Play", audioSrc->autoPlay,
            [entity](bool val) {
                setCompFieldWithUndo<ECS::AudioSourceComponent>(entity, "Change Audio Auto Play",
                    [val](ECS::AudioSourceComponent& c) { c.autoPlay = val; });
            }));

        addSeparator("Audio Source", lines, [this, entity, saved = *audioSrc]() {
            ECS::ECSManager::Instance().removeComponent<ECS::AudioSourceComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Audio Source",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::AudioSourceComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::AudioSourceComponent>(entity, saved); }
            });
        });
    }

    if (const auto* constraintComponent = ecs.getComponent<ECS::ConstraintComponent>(entity))
    {
        std::vector<WidgetElement> summaryLines;
        summaryLines.push_back(makeTextLine("Constraint Count: " + std::to_string(constraintComponent->constraints.size())));
        {
            WidgetElement addBtn = EditorUIBuilder::makePrimaryButton(
                "Details.Constraint.Add", "Add Constraint",
                [this, entity]() {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Add Constraint",
                        [](ECS::ConstraintComponent& c) { c.constraints.emplace_back(); });
                    populateDetails(entity);
                },
                EditorTheme::Scaled(Vec2{ 0.0f, 26.0f }));
            addBtn.fillX = true;
            addBtn.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
            summaryLines.push_back(std::move(addBtn));
        }

        addSeparator("Constraints", summaryLines, [this, entity, saved = *constraintComponent]() {
            ECS::ECSManager::Instance().removeComponent<ECS::ConstraintComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Constraints",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ConstraintComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::ConstraintComponent>(entity, saved); }
            });
        });

        for (size_t constraintIndex = 0; constraintIndex < constraintComponent->constraints.size(); ++constraintIndex)
        {
            const auto& constraint = constraintComponent->constraints[constraintIndex];
            std::vector<WidgetElement> lines;
            const std::string suffix = "." + std::to_string(constraintIndex);

            {
                int currentIdx = static_cast<int>(constraint.type);
                DropDownWidget typeDropdown;
                typeDropdown.setItems({ "Hinge", "Ball Socket", "Fixed", "Slider", "Distance", "Spring", "Cone" });
                typeDropdown.setSelectedIndex(currentIdx);
                typeDropdown.setFont(EditorTheme::Get().fontDefault);
                typeDropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
                typeDropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
                typeDropdown.setPadding(EditorTheme::Get().paddingNormal);
                typeDropdown.setOnSelectionChanged([entity, constraintIndex](int idx) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Constraint Type",
                        [constraintIndex, idx](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].type = static_cast<ECS::ConstraintComponent::ConstraintType>(idx);
                        });
                });

                WidgetElement row{};
                row.type = WidgetElementType::StackPanel;
                row.orientation = StackOrientation::Horizontal;
                row.fillX = true;
                row.sizeToContent = true;
                row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                row.runtimeOnly = true;

                WidgetElement lbl = makeTextLine("Type");
                lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
                lbl.fillX = false;
                row.children.push_back(std::move(lbl));

                WidgetElement ddEl = typeDropdown.toElement();
                ddEl.id = "Details.Constraint.Type" + suffix;
                ddEl.fillX = true;
                ddEl.runtimeOnly = true;
                row.children.push_back(std::move(ddEl));
                lines.push_back(std::move(row));
            }

            {
                DropdownButtonWidget dropdown;
                dropdown.setText(getEntityLabel(static_cast<ECS::Entity>(constraint.connectedEntity)));
                dropdown.setFont(EditorTheme::Get().fontDefault);
                dropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
                dropdown.setMinSize(Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall });
                dropdown.setPadding(EditorTheme::Get().paddingNormal);
                dropdown.setBackgroundColor(EditorTheme::Get().dropdownBackground);
                dropdown.setHoverColor(EditorTheme::Get().dropdownHover);
                dropdown.setTextColor(EditorTheme::Get().dropdownText);

                dropdown.addItem("(None)", [entity, constraintIndex, this]() {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Clear Connected Entity",
                        [constraintIndex](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].connectedEntity = 0;
                        });
                    populateDetails(entity);
                });

                ECS::Schema schema;
                for (const auto otherEntity : ecs.getEntitiesMatchingSchema(schema))
                {
                    if (otherEntity == entity)
                        continue;

                    const std::string label = getEntityLabel(otherEntity);
                    dropdown.addItem(label, [entity, constraintIndex, otherEntity, this]() {
                        setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Connected Entity",
                            [constraintIndex, otherEntity](ECS::ConstraintComponent& c) {
                                if (constraintIndex < c.constraints.size())
                                    c.constraints[constraintIndex].connectedEntity = otherEntity;
                            });
                        populateDetails(entity);
                    });
                }

                WidgetElement row{};
                row.type = WidgetElementType::StackPanel;
                row.orientation = StackOrientation::Horizontal;
                row.fillX = true;
                row.sizeToContent = true;
                row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.padding = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                row.runtimeOnly = true;

                WidgetElement lbl = makeTextLine("Attached Entity");
                lbl.minSize = EditorTheme::Scaled(Vec2{ 90.0f, 20.0f });
                lbl.fillX = false;
                row.children.push_back(std::move(lbl));

                WidgetElement ddEl = dropdown.toElement();
                ddEl.id = "Details.Constraint.ConnectedEntity" + suffix;
                ddEl.fillX = true;
                ddEl.runtimeOnly = true;
                row.children.push_back(std::move(ddEl));
                lines.push_back(std::move(row));
            }

            lines.push_back(makeTextLine("Attached Entity ID: " + std::to_string(constraint.connectedEntity)));

            lines.push_back(makeVec3Row("Details.Constraint.Anchor" + suffix, "Anchor", constraint.anchor,
                [entity, constraintIndex](int axis, float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Constraint Anchor",
                        [constraintIndex, axis, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].anchor[axis] = val;
                        });
                }));

            lines.push_back(makeVec3Row("Details.Constraint.ConnectedAnchor" + suffix, "Connected Anchor", constraint.connectedAnchor,
                [entity, constraintIndex](int axis, float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Connected Anchor",
                        [constraintIndex, axis, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].connectedAnchor[axis] = val;
                        });
                }));

            lines.push_back(makeVec3Row("Details.Constraint.Axis" + suffix, "Axis", constraint.axis,
                [entity, constraintIndex](int axis, float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Constraint Axis",
                        [constraintIndex, axis, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].axis[axis] = val;
                        });
                }));

            lines.push_back(makeFloatEntry("Details.Constraint.MinLimit" + suffix, "Limit Min", constraint.limits[0],
                [entity, constraintIndex](float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Constraint Min Limit",
                        [constraintIndex, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].limits[0] = val;
                        });
                }));

            lines.push_back(makeFloatEntry("Details.Constraint.MaxLimit" + suffix, "Limit Max", constraint.limits[1],
                [entity, constraintIndex](float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Constraint Max Limit",
                        [constraintIndex, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].limits[1] = val;
                        });
                }));

            lines.push_back(makeFloatEntry("Details.Constraint.SpringStiffness" + suffix, "Spring Stiffness", constraint.springStiffness,
                [entity, constraintIndex](float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Spring Stiffness",
                        [constraintIndex, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].springStiffness = val;
                        });
                }));

            lines.push_back(makeFloatEntry("Details.Constraint.SpringDamping" + suffix, "Spring Damping", constraint.springDamping,
                [entity, constraintIndex](float val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Spring Damping",
                        [constraintIndex, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].springDamping = val;
                        });
                }));

            lines.push_back(makeCheckBoxRow("Details.Constraint.Breakable" + suffix, "Breakable", constraint.breakable,
                [this, entity, constraintIndex](bool val) {
                    setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Constraint Breakable",
                        [constraintIndex, val](ECS::ConstraintComponent& c) {
                            if (constraintIndex < c.constraints.size())
                                c.constraints[constraintIndex].breakable = val;
                        });
                    populateDetails(entity);
                }));

            if (constraint.breakable)
            {
                lines.push_back(makeFloatEntry("Details.Constraint.BreakForce" + suffix, "Break Force", constraint.breakForce,
                    [entity, constraintIndex](float val) {
                        setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Break Force",
                            [constraintIndex, val](ECS::ConstraintComponent& c) {
                                if (constraintIndex < c.constraints.size())
                                    c.constraints[constraintIndex].breakForce = val;
                            });
                    }));

                lines.push_back(makeFloatEntry("Details.Constraint.BreakTorque" + suffix, "Break Torque", constraint.breakTorque,
                    [entity, constraintIndex](float val) {
                        setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Change Break Torque",
                            [constraintIndex, val](ECS::ConstraintComponent& c) {
                                if (constraintIndex < c.constraints.size())
                                    c.constraints[constraintIndex].breakTorque = val;
                            });
                    }));
            }

            addSeparator("Constraint " + std::to_string(constraintIndex + 1), lines, [this, entity, constraintIndex]() {
                setCompFieldWithUndo<ECS::ConstraintComponent>(entity, "Remove Constraint Entry",
                    [constraintIndex](ECS::ConstraintComponent& c) {
                        if (constraintIndex < c.constraints.size())
                            c.constraints.erase(c.constraints.begin() + static_cast<std::ptrdiff_t>(constraintIndex));
                    });
                populateDetails(entity);
            });
        }
    }


    // "Add Component" dropdown button
    {
        DropdownButtonWidget dropdown;
        dropdown.setText("+ Add Component");
        dropdown.setFont(EditorTheme::Get().fontDefault);
        dropdown.setFontSize(EditorTheme::Get().fontSizeSmall);
        dropdown.setMinSize(Vec2{ 0.0f, 26.0f });
        dropdown.setPadding(EditorTheme::Get().paddingLarge);
        dropdown.setBackgroundColor(Vec4{ EditorTheme::Get().successColor.x, EditorTheme::Get().successColor.y, EditorTheme::Get().successColor.z, 0.85f });
        dropdown.setHoverColor(EditorTheme::Get().successColor);
        dropdown.setTextColor(EditorTheme::Get().textPrimary);

        struct CompOption { std::string label; bool present; std::function<void()> addFn; std::function<void()> removeFn; };
        std::vector<CompOption> options = {
            { "Transform", ecs.hasComponent<ECS::TransformComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::TransformComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::TransformComponent>(entity); } },
            { "Mesh", ecs.hasComponent<ECS::MeshComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::MeshComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::MeshComponent>(entity); } },
            { "Material", ecs.hasComponent<ECS::MaterialComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::MaterialComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::MaterialComponent>(entity); } },
            { "Light", ecs.hasComponent<ECS::LightComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::LightComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::LightComponent>(entity); } },
            { "Camera", ecs.hasComponent<ECS::CameraComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::CameraComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::CameraComponent>(entity); } },
            { "Collision", ecs.hasComponent<ECS::CollisionComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::CollisionComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::CollisionComponent>(entity); } },
            { "Physics", ecs.hasComponent<ECS::PhysicsComponent>(entity),
              [this, entity]() {
                  auto& e = ECS::ECSManager::Instance();
                  e.addComponent<ECS::PhysicsComponent>(entity);
                  // Auto-add CollisionComponent with fitted size from mesh AABB
                  if (!e.hasComponent<ECS::CollisionComponent>(entity))
                  {
                      m_uiManager->autoFitColliderForEntity(entity);
                  }
              },
              [entity]() {
                  ECS::ECSManager::Instance().removeComponent<ECS::PhysicsComponent>(entity);
              } },
            { "Constraint", ecs.hasComponent<ECS::ConstraintComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::ConstraintComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ConstraintComponent>(entity); } },
            { "Logic", ecs.hasComponent<ECS::LogicComponent>(entity),
              [this, entity]() {
                  auto& e = ECS::ECSManager::Instance();
                  ECS::LogicComponent comp{};
                  const auto& info = DiagnosticsManager::Instance().getProjectInfo();
                  const auto mode = info.scriptingMode;
                  const std::string projectPath = info.projectPath;

                  // Determine entity name for file naming
                  std::string baseName = "Entity" + std::to_string(entity);
                  if (const auto* nc = e.getComponent<ECS::NameComponent>(entity))
                  {
                      if (!nc->displayName.empty())
                      {
                          std::string safe = nc->displayName;
                          for (auto& ch : safe) { if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') ch = '_'; }
                          baseName = safe;
                      }
                  }

                  namespace fs = std::filesystem;
                  const fs::path scriptsDir = fs::path(projectPath) / "Content" / "Scripts";
                  std::error_code ec;
                  fs::create_directories(scriptsDir, ec);

                  // Auto-create Python script
                  if (mode == DiagnosticsManager::ScriptingMode::PythonOnly || mode == DiagnosticsManager::ScriptingMode::Both)
                  {
                      const fs::path pyFile = scriptsDir / (baseName + ".py");
                      if (!fs::exists(pyFile))
                      {
                          std::ofstream out(pyFile);
                          if (out.is_open())
                          {
                              out << "import engine\n\n";
                              out << "def on_loaded(entity):\n";
                              out << "    pass\n\n";
                              out << "def tick(entity, dt):\n";
                              out << "    pass\n\n";
                              out << "def on_begin_overlap(entity, other_entity):\n";
                              out << "    pass\n\n";
                              out << "def on_end_overlap(entity, other_entity):\n";
                              out << "    pass\n";
                              out.close();
                              Logger::Instance().log(Logger::Category::Engine,
                                  "Auto-created Python script: " + pyFile.string(), Logger::LogLevel::INFO);
                          }
                      }
                      comp.scriptPath = (fs::path("Scripts") / (baseName + ".py")).generic_string();
                  }

                  // Auto-create C++ class header
                  if (mode == DiagnosticsManager::ScriptingMode::CppOnly || mode == DiagnosticsManager::ScriptingMode::Both)
                  {
                      const fs::path cppDir = scriptsDir / "Native";
                      fs::create_directories(cppDir, ec);

                      const fs::path headerFile = cppDir / (baseName + ".h");
                      if (!fs::exists(headerFile))
                      {
                          std::ofstream out(headerFile);
                          if (out.is_open())
                          {
                              out << "#pragma once\n";
                              out << "#include \"INativeScript.h\"\n\n";
                              out << "class " << baseName << " : public INativeScript\n";
                              out << "{\n";
                              out << "public:\n";
                              out << "    void onLoaded() override;\n";
                              out << "    void tick(float deltaTime) override;\n";
                              out << "    void onBeginOverlap(ECS::Entity other) override;\n";
                              out << "    void onEndOverlap(ECS::Entity other) override;\n";
                              out << "    void onDestroy() override;\n";
                              out << "};\n";
                              out.close();
                          }
                      }

                      const fs::path cppFile = cppDir / (baseName + ".cpp");
                      if (!fs::exists(cppFile))
                      {
                          std::ofstream out(cppFile);
                          if (out.is_open())
                          {
                              out << "#include \"" << baseName << ".h\"\n\n";
                              out << "void " << baseName << "::onLoaded()\n";
                              out << "{\n}\n\n";
                              out << "void " << baseName << "::tick(float deltaTime)\n";
                              out << "{\n}\n\n";
                              out << "void " << baseName << "::onBeginOverlap(ECS::Entity other)\n";
                              out << "{\n}\n\n";
                              out << "void " << baseName << "::onEndOverlap(ECS::Entity other)\n";
                              out << "{\n}\n\n";
                              out << "void " << baseName << "::onDestroy()\n";
                              out << "{\n}\n";
                              out.close();
                              Logger::Instance().log(Logger::Category::Engine,
                                  "Auto-created C++ class: " + cppFile.string(), Logger::LogLevel::INFO);
                          }
                      }
                      comp.nativeClassName = baseName;
                  }

                  e.addComponent<ECS::LogicComponent>(entity, comp);
              },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::LogicComponent>(entity); } },
            { "Name", ecs.hasComponent<ECS::NameComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::NameComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::NameComponent>(entity); } },
            { "Particle Emitter", ecs.hasComponent<ECS::ParticleEmitterComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::ParticleEmitterComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ParticleEmitterComponent>(entity); } },
            { "Animation", ecs.hasComponent<ECS::AnimationComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::AnimationComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::AnimationComponent>(entity); } },
            { "Audio Source", ecs.hasComponent<ECS::AudioSourceComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::AudioSourceComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::AudioSourceComponent>(entity); } },
        };

        for (const auto& opt : options)
        {
            if (!opt.present)
            {
                auto addFn = opt.addFn;
                auto removeFn = opt.removeFn;
                dropdown.addItem(opt.label, [this, addFn, removeFn, label = opt.label, entity]() {
                    addFn();
                    DiagnosticsManager::Instance().invalidateEntity(entity);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
                    populateDetails(entity);
                    if (label == "Name")
                    {
                        refresh();
                    }
                    m_uiManager->showToastMessage("Added " + label + " component.", UIManager::kToastShort);
                    UndoRedoManager::Instance().pushCommand({
                        "Add " + label,
                        [addFn, entity]() {
                            addFn();
                            DiagnosticsManager::Instance().invalidateEntity(entity);
                        },
                        [removeFn, entity]() {
                            removeFn();
                            DiagnosticsManager::Instance().invalidateEntity(entity);
                        }
                    });
                });
            }
        }

        WidgetElement dropdownEl = dropdown.toElement();
        dropdownEl.id = "Details.AddComponent";
        dropdownEl.tooltipText = "Add a new component to this entity";
        dropdownEl.fillX = true;
        dropdownEl.runtimeOnly = true;
        detailsPanel->children.push_back(std::move(dropdownEl));
    }

    detailsEntry->widget->markLayoutDirty();
}


void OutlinerPanel::applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity)
{
    if (entity == 0 || assetPath.empty())
    {
        return;
    }

    auto& ecs = ECS::ECSManager::Instance();

    switch (type)
    {
    case AssetType::Model3D:
    {
        // Capture old state for undo
        std::optional<ECS::MeshComponent> oldMesh;
        std::optional<ECS::MaterialComponent> oldMat;
        if (auto* m = ecs.getComponent<ECS::MeshComponent>(entity)) oldMesh = *m;
        if (auto* m = ecs.getComponent<ECS::MaterialComponent>(entity)) oldMat = *m;

        ECS::MeshComponent comp{};
        comp.meshAssetPath = assetPath;
        if (!ecs.hasComponent<ECS::MeshComponent>(entity))
        {
            ecs.addComponent<ECS::MeshComponent>(entity, comp);
        }
        else
        {
            ecs.setComponent<ECS::MeshComponent>(entity, comp);
        }

        // Auto-add MaterialComponent if the mesh asset references a material
        {
            auto meshAsset = AssetManager::Instance().getLoadedAssetByPath(assetPath);
            if (!meshAsset)
            {
                int id = AssetManager::Instance().loadAsset(assetPath, AssetType::Model3D);
                if (id > 0)
                    meshAsset = AssetManager::Instance().getLoadedAssetByID(static_cast<unsigned int>(id));
            }
            if (meshAsset)
            {
                auto& assetData = meshAsset->getData();
                if (assetData.contains("m_materialAssetPaths") && assetData["m_materialAssetPaths"].is_array()
                    && !assetData["m_materialAssetPaths"].empty())
                {
                    std::string matPath = assetData["m_materialAssetPaths"][0].get<std::string>();
                    if (!matPath.empty())
                    {
                        ECS::MaterialComponent matComp{};
                        matComp.materialAssetPath = matPath;
                        if (!ecs.hasComponent<ECS::MaterialComponent>(entity))
                        {
                            ecs.addComponent<ECS::MaterialComponent>(entity, matComp);
                        }
                        else
                        {
                            ecs.setComponent<ECS::MaterialComponent>(entity, matComp);
                        }
                        Logger::Instance().log(Logger::Category::UI,
                            "Auto-assigned material '" + matPath + "' to entity " + std::to_string(entity),
                            Logger::LogLevel::INFO);
                    }
                }
            }
        }

        // Capture new state for redo
        std::optional<ECS::MeshComponent> newMesh;
        std::optional<ECS::MaterialComponent> newMat;
        if (auto* m = ecs.getComponent<ECS::MeshComponent>(entity)) newMesh = *m;
        if (auto* m = ecs.getComponent<ECS::MaterialComponent>(entity)) newMat = *m;

        UndoRedoManager::Instance().pushCommand({
            "Assign Mesh",
            [entity, newMesh, newMat]() {
                auto& e = ECS::ECSManager::Instance();
                if (newMesh) { if (e.hasComponent<ECS::MeshComponent>(entity)) e.setComponent(entity, *newMesh); else e.addComponent(entity, *newMesh); }
                if (newMat)  { if (e.hasComponent<ECS::MaterialComponent>(entity)) e.setComponent(entity, *newMat); else e.addComponent(entity, *newMat); }
                DiagnosticsManager::Instance().invalidateEntity(entity);
            },
            [entity, oldMesh, oldMat]() {
                auto& e = ECS::ECSManager::Instance();
                if (oldMesh) { if (e.hasComponent<ECS::MeshComponent>(entity)) e.setComponent(entity, *oldMesh); else e.addComponent(entity, *oldMesh); }
                else { e.removeComponent<ECS::MeshComponent>(entity); }
                if (oldMat) { if (e.hasComponent<ECS::MaterialComponent>(entity)) e.setComponent(entity, *oldMat); else e.addComponent(entity, *oldMat); }
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }
        });

        Logger::Instance().log(Logger::Category::UI,
            "Applied mesh '" + assetPath + "' to entity " + std::to_string(entity),
            Logger::LogLevel::INFO);
        m_uiManager->showToastMessage("Mesh assigned: " + assetPath, UIManager::kToastMedium);
        break;
    }
    case AssetType::Material:
    {
        std::optional<ECS::MaterialComponent> oldMat;
        if (auto* m = ecs.getComponent<ECS::MaterialComponent>(entity)) oldMat = *m;

        ECS::MaterialComponent comp{};
        comp.materialAssetPath = assetPath;
        if (!ecs.hasComponent<ECS::MaterialComponent>(entity))
        {
            ecs.addComponent<ECS::MaterialComponent>(entity, comp);
        }
        else
        {
            ecs.setComponent<ECS::MaterialComponent>(entity, comp);
        }

        UndoRedoManager::Instance().pushCommand({
            "Assign Material",
            [entity, comp]() {
                auto& e = ECS::ECSManager::Instance();
                if (e.hasComponent<ECS::MaterialComponent>(entity)) e.setComponent(entity, comp); else e.addComponent(entity, comp);
                DiagnosticsManager::Instance().invalidateEntity(entity);
            },
            [entity, oldMat]() {
                auto& e = ECS::ECSManager::Instance();
                if (oldMat) { if (e.hasComponent<ECS::MaterialComponent>(entity)) e.setComponent(entity, *oldMat); else e.addComponent(entity, *oldMat); }
                else { e.removeComponent<ECS::MaterialComponent>(entity); }
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }
        });

        Logger::Instance().log(Logger::Category::UI,
            "Applied material '" + assetPath + "' to entity " + std::to_string(entity),
            Logger::LogLevel::INFO);
        m_uiManager->showToastMessage("Material assigned: " + assetPath, UIManager::kToastMedium);
        break;
    }
    case AssetType::Script:
    {
        std::optional<ECS::LogicComponent> oldLogic;
        if (auto* s = ecs.getComponent<ECS::LogicComponent>(entity)) oldLogic = *s;

        ECS::LogicComponent comp{};
        if (oldLogic)
            comp = *oldLogic;
        comp.scriptPath = assetPath;
        if (!ecs.hasComponent<ECS::LogicComponent>(entity))
        {
            ecs.addComponent<ECS::LogicComponent>(entity, comp);
        }
        else
        {
            ecs.setComponent<ECS::LogicComponent>(entity, comp);
        }

        UndoRedoManager::Instance().pushCommand({
            "Assign Script",
            [entity, comp]() {
                auto& e = ECS::ECSManager::Instance();
                if (e.hasComponent<ECS::LogicComponent>(entity)) e.setComponent(entity, comp); else e.addComponent(entity, comp);
                DiagnosticsManager::Instance().invalidateEntity(entity);
            },
            [entity, oldLogic]() {
                auto& e = ECS::ECSManager::Instance();
                if (oldLogic) { if (e.hasComponent<ECS::LogicComponent>(entity)) e.setComponent(entity, *oldLogic); else e.addComponent(entity, *oldLogic); }
                else { e.removeComponent<ECS::LogicComponent>(entity); }
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }
        });

        Logger::Instance().log(Logger::Category::UI,
            "Applied script '" + assetPath + "' to entity " + std::to_string(entity),
            Logger::LogLevel::INFO);
        m_uiManager->showToastMessage("Script assigned: " + assetPath, UIManager::kToastMedium);
        break;
    }
    default:
        m_uiManager->showToastMessage("Unsupported asset type for entity assignment.", UIManager::kToastMedium);
        return;
    }

    auto& diag = DiagnosticsManager::Instance();
    diag.invalidateEntity(entity);
    if (auto* level = diag.getActiveLevelSoft())
    {
        level->setIsSaved(false);
    }

    populateDetails(entity);
}


void OutlinerPanel::copySelectedEntity()
{
    const auto entity = static_cast<ECS::Entity>(m_selectedEntity);
    if (entity == 0) return;

    auto& ecs = ECS::ECSManager::Instance();
    m_entityClipboard = {};
    m_entityClipboard.valid = true;

    if (ecs.hasComponent<ECS::TransformComponent>(entity))
        m_entityClipboard.transform = *ecs.getComponent<ECS::TransformComponent>(entity);
    if (ecs.hasComponent<ECS::MeshComponent>(entity))
        m_entityClipboard.mesh = *ecs.getComponent<ECS::MeshComponent>(entity);
    if (ecs.hasComponent<ECS::MaterialComponent>(entity))
        m_entityClipboard.material = *ecs.getComponent<ECS::MaterialComponent>(entity);
    if (ecs.hasComponent<ECS::LightComponent>(entity))
        m_entityClipboard.light = *ecs.getComponent<ECS::LightComponent>(entity);
    if (ecs.hasComponent<ECS::CameraComponent>(entity))
        m_entityClipboard.camera = *ecs.getComponent<ECS::CameraComponent>(entity);
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))
        m_entityClipboard.physics = *ecs.getComponent<ECS::PhysicsComponent>(entity);
    if (ecs.hasComponent<ECS::LogicComponent>(entity))
        m_entityClipboard.logic = *ecs.getComponent<ECS::LogicComponent>(entity);
    if (ecs.hasComponent<ECS::NameComponent>(entity))
        m_entityClipboard.name = *ecs.getComponent<ECS::NameComponent>(entity);
    if (ecs.hasComponent<ECS::CollisionComponent>(entity))
        m_entityClipboard.collision = *ecs.getComponent<ECS::CollisionComponent>(entity);
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity))
        m_entityClipboard.heightField = *ecs.getComponent<ECS::HeightFieldComponent>(entity);
    if (ecs.hasComponent<ECS::LodComponent>(entity))
        m_entityClipboard.lod = *ecs.getComponent<ECS::LodComponent>(entity);
    if (ecs.hasComponent<ECS::AnimationComponent>(entity))
        m_entityClipboard.animation = *ecs.getComponent<ECS::AnimationComponent>(entity);
    if (ecs.hasComponent<ECS::ParticleEmitterComponent>(entity))
        m_entityClipboard.particleEmitter = *ecs.getComponent<ECS::ParticleEmitterComponent>(entity);

    std::string entityName = "Entity " + std::to_string(entity);
    if (m_entityClipboard.name)
        entityName = m_entityClipboard.name->displayName;
    m_uiManager->showToastMessage("Copied: " + entityName, UIManager::kToastShort);
}


bool OutlinerPanel::pasteEntity()
{
    if (!m_entityClipboard.valid) return false;

    auto& ecs = ECS::ECSManager::Instance();
    const ECS::Entity newEntity = ecs.createEntity();

    // Add all snapshotted components
    if (m_entityClipboard.transform)
    {
        auto t = *m_entityClipboard.transform;
        t.position[0] += 1.0f; // offset so it doesn't overlap
        ecs.addComponent<ECS::TransformComponent>(newEntity, t);
    }
    if (m_entityClipboard.name)
    {
        auto n = *m_entityClipboard.name;
        n.displayName += " (Copy)";
        ecs.addComponent<ECS::NameComponent>(newEntity, n);
    }
    if (m_entityClipboard.mesh)
        ecs.addComponent<ECS::MeshComponent>(newEntity, *m_entityClipboard.mesh);
    if (m_entityClipboard.material)
        ecs.addComponent<ECS::MaterialComponent>(newEntity, *m_entityClipboard.material);
    if (m_entityClipboard.light)
        ecs.addComponent<ECS::LightComponent>(newEntity, *m_entityClipboard.light);
    if (m_entityClipboard.camera)
        ecs.addComponent<ECS::CameraComponent>(newEntity, *m_entityClipboard.camera);
    if (m_entityClipboard.physics)
        ecs.addComponent<ECS::PhysicsComponent>(newEntity, *m_entityClipboard.physics);
    if (m_entityClipboard.logic)
        ecs.addComponent<ECS::LogicComponent>(newEntity, *m_entityClipboard.logic);
    if (m_entityClipboard.collision)
        ecs.addComponent<ECS::CollisionComponent>(newEntity, *m_entityClipboard.collision);
    if (m_entityClipboard.heightField)
        ecs.addComponent<ECS::HeightFieldComponent>(newEntity, *m_entityClipboard.heightField);
    if (m_entityClipboard.lod)
        ecs.addComponent<ECS::LodComponent>(newEntity, *m_entityClipboard.lod);
    if (m_entityClipboard.animation)
        ecs.addComponent<ECS::AnimationComponent>(newEntity, *m_entityClipboard.animation);
    if (m_entityClipboard.particleEmitter)
        ecs.addComponent<ECS::ParticleEmitterComponent>(newEntity, *m_entityClipboard.particleEmitter);

    // Register with the level
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
    if (level) level->onEntityAdded(newEntity);

    // Select the new entity
    selectEntity(newEntity);
    if (m_renderer) m_renderer->setSelectedEntity(newEntity);
    refresh();

    std::string entityName = "Entity " + std::to_string(newEntity);
    if (m_entityClipboard.name)
        entityName = m_entityClipboard.name->displayName + " (Copy)";
    m_uiManager->showToastMessage("Pasted: " + entityName, UIManager::kToastShort);

    Logger::Instance().log(Logger::Category::Engine,
        "Pasted entity " + std::to_string(newEntity) + " (" + entityName + ")",
        Logger::LogLevel::INFO);

    // Undo/Redo
    UndoRedoManager::Command cmd;
    cmd.description = "Paste " + entityName;
    cmd.execute = [newEntity, cb = m_entityClipboard]()
    {
        auto& e = ECS::ECSManager::Instance();
        e.createEntity(newEntity);
        if (cb.transform)        { auto t = *cb.transform; t.position[0] += 1.0f; e.addComponent<ECS::TransformComponent>(newEntity, t); }
        if (cb.name)             { auto n = *cb.name; n.displayName += " (Copy)"; e.addComponent<ECS::NameComponent>(newEntity, n); }
        if (cb.mesh)             e.addComponent<ECS::MeshComponent>(newEntity, *cb.mesh);
        if (cb.material)         e.addComponent<ECS::MaterialComponent>(newEntity, *cb.material);
        if (cb.light)            e.addComponent<ECS::LightComponent>(newEntity, *cb.light);
        if (cb.camera)           e.addComponent<ECS::CameraComponent>(newEntity, *cb.camera);
        if (cb.physics)          e.addComponent<ECS::PhysicsComponent>(newEntity, *cb.physics);
        if (cb.logic)            e.addComponent<ECS::LogicComponent>(newEntity, *cb.logic);
        if (cb.collision)        e.addComponent<ECS::CollisionComponent>(newEntity, *cb.collision);
        if (cb.heightField)      e.addComponent<ECS::HeightFieldComponent>(newEntity, *cb.heightField);
        if (cb.lod)              e.addComponent<ECS::LodComponent>(newEntity, *cb.lod);
        if (cb.animation)        e.addComponent<ECS::AnimationComponent>(newEntity, *cb.animation);
        if (cb.particleEmitter)  e.addComponent<ECS::ParticleEmitterComponent>(newEntity, *cb.particleEmitter);
        auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
        if (lvl) lvl->onEntityAdded(newEntity);
    };
    cmd.undo = [newEntity]()
    {
        auto& e = ECS::ECSManager::Instance();
        auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
        if (lvl) lvl->onEntityRemoved(newEntity);
        e.removeEntity(newEntity);
    };
    UndoRedoManager::Instance().pushCommand(std::move(cmd));

    return true;
}

bool OutlinerPanel::duplicateEntity()
{
    const auto entity = static_cast<ECS::Entity>(m_selectedEntity);
    if (entity == 0) return false;

    // Snapshot directly from the source entity (bypass clipboard so Ctrl+D doesn't overwrite it)
    EntityClipboard snapshot;
    snapshot.valid = true;
    auto& ecs = ECS::ECSManager::Instance();

    if (ecs.hasComponent<ECS::TransformComponent>(entity))       snapshot.transform       = *ecs.getComponent<ECS::TransformComponent>(entity);
    if (ecs.hasComponent<ECS::MeshComponent>(entity))            snapshot.mesh            = *ecs.getComponent<ECS::MeshComponent>(entity);
    if (ecs.hasComponent<ECS::MaterialComponent>(entity))        snapshot.material        = *ecs.getComponent<ECS::MaterialComponent>(entity);
    if (ecs.hasComponent<ECS::LightComponent>(entity))           snapshot.light           = *ecs.getComponent<ECS::LightComponent>(entity);
    if (ecs.hasComponent<ECS::CameraComponent>(entity))          snapshot.camera          = *ecs.getComponent<ECS::CameraComponent>(entity);
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))         snapshot.physics         = *ecs.getComponent<ECS::PhysicsComponent>(entity);
    if (ecs.hasComponent<ECS::LogicComponent>(entity))          snapshot.logic           = *ecs.getComponent<ECS::LogicComponent>(entity);
    if (ecs.hasComponent<ECS::NameComponent>(entity))            snapshot.name            = *ecs.getComponent<ECS::NameComponent>(entity);
    if (ecs.hasComponent<ECS::CollisionComponent>(entity))       snapshot.collision       = *ecs.getComponent<ECS::CollisionComponent>(entity);
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity))     snapshot.heightField     = *ecs.getComponent<ECS::HeightFieldComponent>(entity);
    if (ecs.hasComponent<ECS::LodComponent>(entity))             snapshot.lod             = *ecs.getComponent<ECS::LodComponent>(entity);
    if (ecs.hasComponent<ECS::AnimationComponent>(entity))       snapshot.animation       = *ecs.getComponent<ECS::AnimationComponent>(entity);
    if (ecs.hasComponent<ECS::ParticleEmitterComponent>(entity)) snapshot.particleEmitter = *ecs.getComponent<ECS::ParticleEmitterComponent>(entity);

    // Temporarily put snapshot into clipboard, paste, then restore
    EntityClipboard savedClipboard = m_entityClipboard;
    m_entityClipboard = snapshot;
    const bool ok = pasteEntity();
    m_entityClipboard = savedClipboard;
    return ok;
}


void OutlinerPanel::navigateByArrow(int direction)
{
    auto& ecs = ECS::ECSManager::Instance();
    ECS::Schema schema;
    const auto entities = ecs.getEntitiesMatchingSchema(schema);
    if (entities.empty())
        return;

    // Find current selection index
    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(entities.size()); ++i)
    {
        if (entities[i] == m_selectedEntity)
        {
            currentIdx = i;
            break;
        }
    }

    int nextIdx;
    if (currentIdx < 0)
    {
        nextIdx = (direction > 0) ? 0 : static_cast<int>(entities.size()) - 1;
    }
    else
    {
        nextIdx = currentIdx + direction;
        if (nextIdx < 0) nextIdx = static_cast<int>(entities.size()) - 1;
        if (nextIdx >= static_cast<int>(entities.size())) nextIdx = 0;
    }

    m_selectedEntity = entities[nextIdx];
    populateDetails(m_selectedEntity);
}

#endif // ENGINE_EDITOR
