#include "WidgetEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/WidgetDetailSchema.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Core/UndoRedoManager.h"
#include <filesystem>
#include <algorithm>

// File-scope helper: recursively find a WidgetElement by id.
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

// File-scope helper: convert AnimatableProperty enum to display string.
static std::string AnimatablePropertyToString(AnimatableProperty prop)
{
 switch (prop)
 {
 case AnimatableProperty::RenderTranslationX: return "TranslationX";
 case AnimatableProperty::RenderTranslationY: return "TranslationY";
 case AnimatableProperty::RenderRotation:     return "Rotation";
 case AnimatableProperty::RenderScaleX:       return "ScaleX";
 case AnimatableProperty::RenderScaleY:       return "ScaleY";
 case AnimatableProperty::RenderShearX:       return "ShearX";
 case AnimatableProperty::RenderShearY:       return "ShearY";
 case AnimatableProperty::Opacity:            return "Opacity";
 case AnimatableProperty::ColorR:             return "ColorR";
 case AnimatableProperty::ColorG:             return "ColorG";
 case AnimatableProperty::ColorB:             return "ColorB";
 case AnimatableProperty::ColorA:             return "ColorA";
 case AnimatableProperty::PositionX:          return "PositionX";
 case AnimatableProperty::PositionY:          return "PositionY";
 case AnimatableProperty::SizeX:              return "SizeX";
 case AnimatableProperty::SizeY:              return "SizeY";
 case AnimatableProperty::FontSize:           return "FontSize";
 default:                                     return "Unknown";
 }
}

void WidgetEditorTab::openTab(const std::string& relativeAssetPath)
{
    if (!m_renderer || relativeAssetPath.empty())
    {
        return;
    }

    std::string tabId = "WidgetEditor." + relativeAssetPath;
    std::replace_if(tabId.begin(), tabId.end(), [](char c)
        {
            return c == '/' || c == '\\' || c == ':' || c == ' ' || c == '.';
        }, '_');

    // If this tab is already open, just switch to it
    if (m_states.count(tabId))
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    const std::string fileName = std::filesystem::path(relativeAssetPath).filename().string();
    const std::string tabName = fileName.empty() ? "Widget Editor" : ("Widget: " + fileName);
    m_renderer->addTab(tabId, tabName, true);
    m_renderer->setActiveTab(tabId);

    auto& assetManager = AssetManager::Instance();
    const int assetId = assetManager.loadAsset(relativeAssetPath, AssetType::Widget, AssetManager::Sync);
    if (assetId == 0)
    {
        m_uiManager->showToastMessage("Failed to load widget: " + relativeAssetPath, UIManager::kToastMedium, UIManager::NotificationLevel::Error);
        m_renderer->removeTab(tabId);
        return;
    }

    auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
    if (!asset)
    {
        m_uiManager->showToastMessage("Widget asset missing after load: " + relativeAssetPath, UIManager::kToastMedium);
        m_renderer->removeTab(tabId);
        return;
    }

    auto widget = m_renderer->createWidgetFromAsset(asset);
    if (!widget)
    {
        m_uiManager->showToastMessage("Failed to create widget from asset: " + relativeAssetPath, UIManager::kToastMedium, UIManager::NotificationLevel::Error);
        m_renderer->removeTab(tabId);
        return;
    }

    const std::string contentWidgetId = "WidgetEditor.Content." + tabId;
    const std::string leftWidgetId = "WidgetEditor.Left." + tabId;
    const std::string rightWidgetId = "WidgetEditor.Right." + tabId;
    const std::string canvasWidgetId = "WidgetEditor.Canvas." + tabId;
    const std::string toolbarWidgetId = "WidgetEditor.Toolbar." + tabId;
    const std::string bottomWidgetId = "WidgetEditor.Bottom." + tabId;

    m_uiManager->unregisterWidget(contentWidgetId);
    m_uiManager->unregisterWidget(leftWidgetId);
    m_uiManager->unregisterWidget(rightWidgetId);
    m_uiManager->unregisterWidget(canvasWidgetId);
    m_uiManager->unregisterWidget(toolbarWidgetId);
    m_uiManager->unregisterWidget(bottomWidgetId);

    // Store editor state
    State state;
    state.tabId = tabId;
    state.assetPath = relativeAssetPath;
    state.editedWidget = widget;
    state.contentWidgetId = contentWidgetId;
    state.leftWidgetId = leftWidgetId;
    state.rightWidgetId = rightWidgetId;
    state.canvasWidgetId = canvasWidgetId;
    state.toolbarWidgetId = toolbarWidgetId;
    state.bottomWidgetId = bottomWidgetId;
    state.assetId = static_cast<unsigned int>(assetId);
    state.isDirty = false;
    m_states[tabId] = std::move(state);

    // --- Top toolbar: save button + dirty indicator ---
    {
        auto toolbarWidget = std::make_shared<EditorWidget>();
        toolbarWidget->setName(toolbarWidgetId);
        toolbarWidget->setAnchor(WidgetAnchor::TopLeft);
        toolbarWidget->setFillX(true);
        toolbarWidget->setSizePixels(Vec2{ 0.0f, 32.0f });
        toolbarWidget->setZOrder(3);

        WidgetElement root{};
        root.id = "WidgetEditor.Toolbar.Root";
        root.type = WidgetElementType::StackPanel;
        root.from = Vec2{ 0.0f, 0.0f };
        root.to = Vec2{ 1.0f, 1.0f };
        root.fillX = true;
        root.fillY = true;
        root.orientation = StackOrientation::Horizontal;
        root.padding = Vec2{ 8.0f, 4.0f };
        root.style.color = EditorTheme::Get().panelHeader;
        root.runtimeOnly = true;

        // Save button
        {
            WidgetElement saveBtn{};
            saveBtn.id = "WidgetEditor.Toolbar.Save";
            saveBtn.type = WidgetElementType::Button;
            saveBtn.text = "Save";
            saveBtn.font = EditorTheme::Get().fontDefault;
            saveBtn.fontSize = EditorTheme::Get().fontSizeBody;
            saveBtn.style.textColor = EditorTheme::Get().textPrimary;
            saveBtn.style.color = EditorTheme::Get().buttonDefault;
            saveBtn.style.hoverColor = EditorTheme::Get().buttonHover;
            saveBtn.textAlignH = TextAlignH::Center;
            saveBtn.textAlignV = TextAlignV::Center;
            saveBtn.minSize = Vec2{ 60.0f, 24.0f };
            saveBtn.padding = Vec2{ 10.0f, 2.0f };
            saveBtn.hitTestMode = HitTestMode::Enabled;
            saveBtn.runtimeOnly = true;
            saveBtn.clickEvent = "WidgetEditor.Toolbar.Save." + tabId;
            root.children.push_back(std::move(saveBtn));
        }

        // Dirty indicator label
        {
            WidgetElement dirtyLabel{};
            dirtyLabel.id = "WidgetEditor.Toolbar.DirtyLabel";
            dirtyLabel.type = WidgetElementType::Text;
            dirtyLabel.text = "";
            dirtyLabel.font = EditorTheme::Get().fontDefault;
            dirtyLabel.fontSize = EditorTheme::Get().fontSizeBody;
            dirtyLabel.style.textColor = EditorTheme::Get().warningColor;
            dirtyLabel.textAlignH = TextAlignH::Left;
            dirtyLabel.textAlignV = TextAlignV::Center;
            dirtyLabel.minSize = Vec2{ 0.0f, 24.0f };
            dirtyLabel.padding = Vec2{ 8.0f, 0.0f };
            dirtyLabel.runtimeOnly = true;
            root.children.push_back(std::move(dirtyLabel));
        }

        // Timeline toggle button
        {
            WidgetElement timelineBtn{};
            timelineBtn.id = "WidgetEditor.Toolbar.Timeline";
            timelineBtn.type = WidgetElementType::Button;
            timelineBtn.text = "Timeline";
            timelineBtn.font = EditorTheme::Get().fontDefault;
            timelineBtn.fontSize = EditorTheme::Get().fontSizeBody;
            timelineBtn.style.textColor = EditorTheme::Get().textPrimary;
            timelineBtn.style.color = EditorTheme::Get().buttonDefault;
            timelineBtn.style.hoverColor = EditorTheme::Get().buttonHover;
            timelineBtn.textAlignH = TextAlignH::Center;
            timelineBtn.textAlignV = TextAlignV::Center;
            timelineBtn.minSize = Vec2{ 80.0f, 24.0f };
            timelineBtn.padding = Vec2{ 10.0f, 2.0f };
            timelineBtn.hitTestMode = HitTestMode::Enabled;
            timelineBtn.runtimeOnly = true;
            timelineBtn.clickEvent = "WidgetEditor.Toolbar.Timeline." + tabId;
            root.children.push_back(std::move(timelineBtn));
        }

        toolbarWidget->setElements({ std::move(root) });
        m_uiManager->registerWidget(toolbarWidgetId, toolbarWidget, tabId);

        const std::string capturedTabId = tabId;
        m_uiManager->registerClickEvent("WidgetEditor.Toolbar.Save." + tabId, [this, capturedTabId]()
        {
            saveAsset(capturedTabId);
        });

        m_uiManager->registerClickEvent("WidgetEditor.Toolbar.Timeline." + tabId, [this, capturedTabId]()
        {
            auto it = m_states.find(capturedTabId);
            if (it == m_states.end())
                return;
            it->second.showAnimationsPanel = !it->second.showAnimationsPanel;
            refreshTimeline(capturedTabId);
            refreshToolbar(capturedTabId);
        });
    }

    // --- Left panel: available controls + hierarchy ---
    {
        auto leftWidget = std::make_shared<EditorWidget>();
        leftWidget->setName(leftWidgetId);
        leftWidget->setAnchor(WidgetAnchor::TopLeft);
        leftWidget->setFillY(true);
        leftWidget->setSizePixels(Vec2{ 280.0f, 0.0f });
        leftWidget->setZOrder(2);

        WidgetElement root{};
        root.id = "WidgetEditor.Left.Root";
        root.type = WidgetElementType::StackPanel;
        root.from = Vec2{ 0.0f, 0.0f };
        root.to = Vec2{ 1.0f, 1.0f };
        root.fillX = true;
        root.fillY = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = EditorTheme::Get().panelBackground;
        root.runtimeOnly = true;

        // --- Controls section (scrollable) ---
        {
            WidgetElement controlsSection{};
            controlsSection.id = "WidgetEditor.Left.ControlsSection";
            controlsSection.type = WidgetElementType::StackPanel;
            controlsSection.fillX = true;
            controlsSection.fillY = true;
            controlsSection.scrollable = true;
            controlsSection.orientation = StackOrientation::Vertical;
            controlsSection.padding = Vec2{ 10.0f, 8.0f };
            controlsSection.style.color = EditorTheme::Get().transparent;
            controlsSection.runtimeOnly = true;

            // Title: Controls
            {
                WidgetElement title{};
                title.id = "WidgetEditor.Left.Title";
                title.type = WidgetElementType::Text;
                title.text = "Controls";
                title.font = EditorTheme::Get().fontDefault;
                title.fontSize = EditorTheme::Get().fontSizeHeading;
                title.style.textColor = EditorTheme::Get().textPrimary;
                title.textAlignH = TextAlignH::Left;
                title.textAlignV = TextAlignV::Center;
                title.fillX = true;
                title.minSize = Vec2{ 0.0f, 28.0f };
                title.runtimeOnly = true;
                controlsSection.children.push_back(std::move(title));
            }

            const std::vector<std::string> controls = {
                "Panel", "Text", "Label", "Button", "ToggleButton", "RadioButton",
                "Image", "EntryBar", "StackPanel", "ScrollView",
                "Grid", "Slider", "CheckBox", "DropDown", "ColorPicker", "ProgressBar", "Separator",
                "WrapBox", "UniformGrid", "SizeBox", "ScaleBox", "WidgetSwitcher", "Overlay",
                "Border", "Spinner", "RichText", "ListView", "TileView"
            };
            for (size_t i = 0; i < controls.size(); ++i)
            {
                WidgetElement item{};
                item.id = "WidgetEditor.Left.Control." + std::to_string(i);
                item.type = WidgetElementType::Button;
                item.text = "  " + controls[i];
                item.font = EditorTheme::Get().fontDefault;
                item.fontSize = EditorTheme::Get().fontSizeSubheading;
                item.style.textColor = EditorTheme::Get().textSecondary;
                item.style.color = EditorTheme::Get().transparent;
                item.style.hoverColor = EditorTheme::Get().buttonSubtleHover;
                item.textAlignH = TextAlignH::Left;
                item.textAlignV = TextAlignV::Center;
                item.fillX = true;
                item.minSize = Vec2{ 0.0f, 24.0f };
                item.hitTestMode = HitTestMode::Enabled;
                item.isDraggable = true;
                item.dragPayload = "WidgetControl|" + controls[i];
                item.runtimeOnly = true;
                controlsSection.children.push_back(std::move(item));
            }

            root.children.push_back(std::move(controlsSection));
        }

        // Separator between sections
        {
            WidgetElement sep{};
            sep.id = "WidgetEditor.Left.Sep";
            sep.type = WidgetElementType::Panel;
            sep.fillX = true;
            sep.minSize = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = EditorTheme::Get().panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // --- Hierarchy section (scrollable) ---
        {
            WidgetElement hierarchySection{};
            hierarchySection.id = "WidgetEditor.Left.HierarchySection";
            hierarchySection.type = WidgetElementType::StackPanel;
            hierarchySection.fillX = true;
            hierarchySection.fillY = true;
            hierarchySection.scrollable = true;
            hierarchySection.orientation = StackOrientation::Vertical;
            hierarchySection.padding = Vec2{ 10.0f, 8.0f };
            hierarchySection.style.color = EditorTheme::Get().panelBackgroundAlt;
            hierarchySection.runtimeOnly = true;

            // Title: Hierarchy
            {
                WidgetElement treeTitle{};
                treeTitle.id = "WidgetEditor.Left.TreeTitle";
                treeTitle.type = WidgetElementType::Text;
                treeTitle.text = "Hierarchy";
                treeTitle.font = EditorTheme::Get().fontDefault;
                treeTitle.fontSize = EditorTheme::Get().fontSizeHeading;
                treeTitle.style.textColor = EditorTheme::Get().textPrimary;
                treeTitle.textAlignH = TextAlignH::Left;
                treeTitle.textAlignV = TextAlignV::Center;
                treeTitle.fillX = true;
                treeTitle.minSize = Vec2{ 0.0f, 28.0f };
                treeTitle.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(treeTitle));
            }

            // Hierarchy tree container (will be populated by refreshWidgetEditorHierarchy)
            {
                WidgetElement hierarchyStack{};
                hierarchyStack.id = "WidgetEditor.Left.Tree";
                hierarchyStack.type = WidgetElementType::StackPanel;
                hierarchyStack.fillX = true;
                hierarchyStack.orientation = StackOrientation::Vertical;
                hierarchyStack.padding = Vec2{ 2.0f, 2.0f };
                hierarchyStack.style.color = EditorTheme::Get().transparent;
                hierarchyStack.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(hierarchyStack));
            }

            root.children.push_back(std::move(hierarchySection));
        }

        leftWidget->setElements({ std::move(root) });
        m_uiManager->registerWidget(leftWidgetId, leftWidget, tabId);
    }

    // --- Right panel: element details (populated by refreshWidgetEditorDetails) ---
    {
        auto rightWidget = std::make_shared<EditorWidget>();
        rightWidget->setName(rightWidgetId);
        rightWidget->setAnchor(WidgetAnchor::TopRight);
        rightWidget->setFillY(true);
        rightWidget->setSizePixels(Vec2{ 300.0f, 0.0f });
        rightWidget->setZOrder(2);

        WidgetElement root{};
        root.id = "WidgetEditor.Right.Root";
        root.type = WidgetElementType::StackPanel;
        root.from = Vec2{ 0.0f, 0.0f };
        root.to = Vec2{ 1.0f, 1.0f };
        root.fillX = true;
        root.fillY = true;
        root.orientation = StackOrientation::Vertical;
        root.padding = Vec2{ 10.0f, 8.0f };
        root.style.color = EditorTheme::Get().panelBackground;
        root.scrollable = true;
        root.runtimeOnly = true;

        {
            WidgetElement title{};
            title.id = "WidgetEditor.Right.Title";
            title.type = WidgetElementType::Text;
            title.text = "Details";
            title.font = EditorTheme::Get().fontDefault;
            title.fontSize = EditorTheme::Get().fontSizeHeading;
            title.style.textColor = EditorTheme::Get().textPrimary;
            title.textAlignH = TextAlignH::Left;
            title.textAlignV = TextAlignV::Center;
            title.fillX = true;
            title.minSize = Vec2{ 0.0f, 28.0f };
            title.runtimeOnly = true;
            root.children.push_back(std::move(title));
        }

        // Placeholder hint (replaced when an element is selected)
        {
            WidgetElement hint{};
            hint.id = "WidgetEditor.Right.Hint";
            hint.type = WidgetElementType::Text;
            hint.text = "Select an element in the hierarchy or preview to see its properties.";
            hint.font = EditorTheme::Get().fontDefault;
            hint.fontSize = EditorTheme::Get().fontSizeBody;
            hint.style.textColor = EditorTheme::Get().textMuted;
            hint.textAlignH = TextAlignH::Left;
            hint.textAlignV = TextAlignV::Center;
            hint.fillX = true;
            hint.minSize = Vec2{ 0.0f, 36.0f };
            hint.runtimeOnly = true;
            root.children.push_back(std::move(hint));
        }

        rightWidget->setElements({ std::move(root) });
        m_uiManager->registerWidget(rightWidgetId, rightWidget, tabId);
    }

    // --- Center canvas background ---
    {
        auto canvasWidget = std::make_shared<EditorWidget>();
        canvasWidget->setName(canvasWidgetId);
        canvasWidget->setAnchor(WidgetAnchor::TopLeft);
        canvasWidget->setFillX(true);
        canvasWidget->setFillY(true);
        canvasWidget->setZOrder(0);

        WidgetElement root{};
        root.id = "WidgetEditor.Canvas.Root";
        root.type = WidgetElementType::Panel;
        root.from = Vec2{ 0.0f, 0.0f };
        root.to = Vec2{ 1.0f, 1.0f };
        root.fillX = true;
        root.fillY = true;
        root.style.color = EditorTheme::Get().panelBackgroundAlt;
        root.runtimeOnly = true;

        canvasWidget->setElements({ root });
        m_uiManager->registerWidget(canvasWidgetId, canvasWidget, tabId);
    }

    // Ensure the edited widget has a valid design size for FBO rendering
    {
        Vec2 designSize = widget->getSizePixels();
        if (designSize.x <= 0.0f) designSize.x = 400.0f;
        if (designSize.y <= 0.0f) designSize.y = 300.0f;
        widget->setSizePixels(designSize);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setAbsolutePosition(true);
        widget->setPositionPixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(0);

        // Auto-assign IDs to any elements that lack one so they are selectable
        // in the widget editor preview.
        {
            static int s_autoIdCounter = 0;
            const std::function<void(WidgetElement&)> assignIds =
                [&](WidgetElement& el) {
                    if (el.id.empty())
                    {
                        std::string typeName;
                        switch (el.type)
                        {
                        case WidgetElementType::Panel:       typeName = "Panel"; break;
                        case WidgetElementType::Text:        typeName = "Text"; break;
                        case WidgetElementType::Button:      typeName = "Button"; break;
                        case WidgetElementType::Image:       typeName = "Image"; break;
                        case WidgetElementType::EntryBar:    typeName = "EntryBar"; break;
                        case WidgetElementType::StackPanel:  typeName = "StackPanel"; break;
                        case WidgetElementType::Grid:        typeName = "Grid"; break;
                        case WidgetElementType::Slider:      typeName = "Slider"; break;
                        case WidgetElementType::CheckBox:    typeName = "CheckBox"; break;
                        case WidgetElementType::DropDown:    typeName = "DropDown"; break;
                        case WidgetElementType::ColorPicker: typeName = "ColorPicker"; break;
                        case WidgetElementType::ProgressBar: typeName = "ProgressBar"; break;
                        case WidgetElementType::Separator:   typeName = "Separator"; break;
                        case WidgetElementType::ScrollView:  typeName = "ScrollView"; break;
                        case WidgetElementType::Label:       typeName = "Label"; break;
                        case WidgetElementType::ToggleButton:typeName = "ToggleButton"; break;
                        case WidgetElementType::RadioButton: typeName = "RadioButton"; break;
                        case WidgetElementType::WrapBox:     typeName = "WrapBox"; break;
                        case WidgetElementType::UniformGrid: typeName = "UniformGrid"; break;
                        case WidgetElementType::SizeBox:     typeName = "SizeBox"; break;
                        case WidgetElementType::ScaleBox:    typeName = "ScaleBox"; break;
                        case WidgetElementType::WidgetSwitcher: typeName = "WidgetSwitcher"; break;
                        case WidgetElementType::Overlay:     typeName = "Overlay"; break;
                        case WidgetElementType::Border:      typeName = "Border"; break;
                        case WidgetElementType::Spinner:     typeName = "Spinner"; break;
                        case WidgetElementType::RichText:    typeName = "RichText"; break;
                        case WidgetElementType::ListView:    typeName = "ListView"; break;
                        case WidgetElementType::TileView:    typeName = "TileView"; break;
                        default:                             typeName = "Element"; break;
                        }
                        el.id = typeName + "_auto_" + std::to_string(++s_autoIdCounter);
                    }
                    for (auto& child : el.children)
                        assignIds(child);
                };
            for (auto& el : widget->getElementsMutable())
                assignIds(el);
        }

        auto& edState = m_states[tabId];
        edState.basePreviewSize = designSize;
        edState.previewDirty = true;
    }

    // Tab and close button events
    const std::string tabBtnId = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_uiManager->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
        {
            m_renderer->setActiveTab(tabId);
        }
        m_uiManager->markAllWidgetsDirty();
    });

    m_uiManager->registerClickEvent(closeBtnId, [this, tabId, tabBtnId, closeBtnId, leftWidgetId, rightWidgetId, canvasWidgetId, toolbarWidgetId]()
    {
        if (!m_renderer)
        {
            return;
        }
        if (m_renderer->getActiveTabId() == tabId)
        {
            m_renderer->setActiveTab("Viewport");
        }

        m_uiManager->unregisterWidget(leftWidgetId);
        m_uiManager->unregisterWidget(rightWidgetId);
        m_uiManager->unregisterWidget(canvasWidgetId);
        m_uiManager->unregisterWidget(toolbarWidgetId);

        m_renderer->cleanupWidgetEditorPreview(tabId);
        m_states.erase(tabId);

        m_renderer->removeTab(tabId);
        m_uiManager->markAllWidgetsDirty();
    });

    // Populate the hierarchy tree and initial details
    refreshHierarchy(tabId);
    refreshDetails(tabId);
}

// ---------------------------------------------------------------------------
// Widget Editor: select an element by id
// ---------------------------------------------------------------------------
void WidgetEditorTab::selectElement(const std::string& tabId, const std::string& elementId)
{
    auto it = m_states.find(tabId);
    if (it == m_states.end())
        return;

    it->second.selectedElementId = elementId;
    it->second.previewDirty = true;
    refreshHierarchy(tabId);
    refreshDetails(tabId);
}

// ---------------------------------------------------------------------------
// Widget Editor: apply zoom + pan transform to preview widget
// ---------------------------------------------------------------------------
void WidgetEditorTab::applyTransform(const std::string& tabId)
{
    auto it = m_states.find(tabId);
    if (it == m_states.end() || !it->second.editedWidget)
        return;

    auto& state = it->second;
    const Vec2 scaledSize{
        state.basePreviewSize.x * state.zoom,
        state.basePreviewSize.y * state.zoom
    };

    // Compute the center of the canvas area for zoom-toward-center behaviour
    float canvasCenterX = state.basePreviewPos.x + state.basePreviewSize.x * 0.5f;
    float canvasCenterY = state.basePreviewPos.y + state.basePreviewSize.y * 0.5f;

    const Vec2 newPos{
        canvasCenterX - scaledSize.x * 0.5f + state.panOffset.x,
        canvasCenterY - scaledSize.y * 0.5f + state.panOffset.y
    };

    state.editedWidget->setSizePixels(scaledSize);
    state.editedWidget->setPositionPixels(newPos);
    state.editedWidget->markLayoutDirty();
    m_uiManager->markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: get state for the currently active tab (if it's a widget editor)
// ---------------------------------------------------------------------------
WidgetEditorTab::State* WidgetEditorTab::getActiveState()
{
    const std::string& activeTab = m_uiManager->getActiveTabId();
    auto it = m_states.find(activeTab);
    if (it == m_states.end())
        return nullptr;
    return &it->second;
}

// ---------------------------------------------------------------------------
// Widget Editor: check if screenPos is over the canvas widget area
// ---------------------------------------------------------------------------
bool WidgetEditorTab::isOverCanvas(const Vec2& screenPos) const
{
    auto it = m_states.find(m_uiManager->getActiveTabId());
    if (it == m_states.end())
        return false;

    const auto* entry = m_uiManager->findWidgetEntry(it->second.canvasWidgetId);
    if (!entry || !entry->widget || !entry->widget->hasComputedPosition() || !entry->widget->hasComputedSize())
        return false;

    const Vec2 pos = entry->widget->getComputedPositionPixels();
    const Vec2 size = entry->widget->getComputedSizePixels();
    return screenPos.x >= pos.x && screenPos.x <= pos.x + size.x &&
           screenPos.y >= pos.y && screenPos.y <= pos.y + size.y;
}

// ---------------------------------------------------------------------------
// Widget Editor: save the edited widget asset to disk
// ---------------------------------------------------------------------------
void WidgetEditorTab::saveAsset(const std::string& tabId)
{
    auto it = m_states.find(tabId);
    if (it == m_states.end() || !it->second.editedWidget)
        return;

    auto& state = it->second;
    auto& assetManager = AssetManager::Instance();

    auto assetData = assetManager.getLoadedAssetByID(state.assetId);
    if (!assetData)
    {
        m_uiManager->showToastMessage("Cannot save: asset not found.", UIManager::kToastMedium);
        return;
    }

    // Sync the widget's current state back into the asset data
    assetData->setData(state.editedWidget->toJson());

    Asset asset;
    asset.ID = state.assetId;
    asset.type = AssetType::Widget;
    if (assetManager.saveAsset(asset, AssetManager::Sync))
    {
        state.isDirty = false;
        refreshToolbar(tabId);
        m_uiManager->showToastMessage("Widget saved.", UIManager::kToastShort, UIManager::NotificationLevel::Success);
    }
    else
    {
        m_uiManager->showToastMessage("Failed to save widget.", UIManager::kToastMedium, UIManager::NotificationLevel::Error);
    }
}

// ---------------------------------------------------------------------------
// Widget Editor: mark the current editor as having unsaved changes
// ---------------------------------------------------------------------------
void WidgetEditorTab::markDirty(const std::string& tabId)
{
    auto it = m_states.find(tabId);
    if (it == m_states.end())
        return;

    it->second.previewDirty = true;

    if (!it->second.isDirty)
    {
        it->second.isDirty = true;
        refreshToolbar(tabId);
    }
}

// ---------------------------------------------------------------------------
// Widget Editor: update the toolbar dirty indicator text
// ---------------------------------------------------------------------------
void WidgetEditorTab::refreshToolbar(const std::string& tabId)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end())
        return;

    const auto* entry = m_uiManager->findWidgetEntry(stateIt->second.toolbarWidgetId);
    if (!entry || !entry->widget)
        return;

    const std::function<WidgetElement*(WidgetElement&, const std::string&)> findById =
        [&](WidgetElement& el, const std::string& id) -> WidgetElement*
        {
            if (el.id == id) return &el;
            for (auto& child : el.children)
            {
                if (auto* match = findById(child, id))
                    return match;
            }
            return nullptr;
        };

    for (auto& el : entry->widget->getElementsMutable())
    {
        if (auto* dirtyLabel = findById(el, "WidgetEditor.Toolbar.DirtyLabel"))
        {
            dirtyLabel->text = stateIt->second.isDirty ? "  * Unsaved changes" : "";
        }
        if (auto* timelineBtn = findById(el, "WidgetEditor.Toolbar.Timeline"))
        {
            timelineBtn->text = stateIt->second.showAnimationsPanel ? "Hide Timeline" : "Timeline";
        }
    }

    entry->widget->markLayoutDirty();
    m_uiManager->markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: delete the currently selected element (with undo/redo)
// ---------------------------------------------------------------------------
void WidgetEditorTab::deleteSelectedElement(const std::string& tabId)
{
    auto it = m_states.find(tabId);
    if (it == m_states.end() || !it->second.editedWidget)
        return;

    auto& state = it->second;
    const std::string& selectedId = state.selectedElementId;
    if (selectedId.empty())
        return;

    // Prevent deletion of the canvas root element
    {
        auto& elems = state.editedWidget->getElementsMutable();
        WidgetElement* sel = FindElementById(elems, selectedId);
        if (sel && sel->isCanvasRoot)
            return;
    }

    // Find and remove the element from the tree
    auto& rootElements = state.editedWidget->getElementsMutable();

    // Recursive search: returns (parent's children vector, index) or (empty, -1)
    struct RemoveResult { bool found; WidgetElement removed; size_t parentIndex; };

    const std::function<bool(std::vector<WidgetElement>&, const std::string&, WidgetElement&, size_t&)> findAndRemove =
        [&](std::vector<WidgetElement>& elements, const std::string& id, WidgetElement& outRemoved, size_t& outIndex) -> bool
    {
        for (size_t i = 0; i < elements.size(); ++i)
        {
            if (elements[i].id == id)
            {
                outRemoved = std::move(elements[i]);
                outIndex = i;
                elements.erase(elements.begin() + static_cast<ptrdiff_t>(i));
                return true;
            }
            if (findAndRemove(elements[i].children, id, outRemoved, outIndex))
                return true;
        }
        return false;
    };

    // We need to know the parent element id for undo reinsertion
    std::string parentId;
    const std::function<bool(const std::vector<WidgetElement>&, const std::string&)> findParent =
        [&](const std::vector<WidgetElement>& elements, const std::string& id) -> bool
    {
        for (const auto& el : elements)
        {
            for (const auto& child : el.children)
            {
                if (child.id == id)
                {
                    parentId = el.id;
                    return true;
                }
            }
            if (findParent(el.children, id))
                return true;
        }
        return false;
    };

    findParent(rootElements, selectedId);

    WidgetElement removedElement{};
    size_t removedIndex = 0;
    if (!findAndRemove(rootElements, selectedId, removedElement, removedIndex))
        return;

    state.selectedElementId.clear();
    state.editedWidget->markLayoutDirty();
    markDirty(tabId);
    refreshHierarchy(tabId);
    refreshDetails(tabId);

    // Push undo/redo command
    const std::string capturedTabId = tabId;
    const std::string capturedParentId = parentId;
    const std::string capturedElId = removedElement.id;
    auto capturedElement = std::make_shared<WidgetElement>(std::move(removedElement));
    const size_t capturedIndex = removedIndex;

    UndoRedoManager::Command cmd;
    cmd.description = "Delete " + capturedElId;

    cmd.undo = [this, capturedTabId, capturedParentId, capturedElement, capturedIndex]()
    {
        auto it2 = m_states.find(capturedTabId);
        if (it2 == m_states.end() || !it2->second.editedWidget)
            return;

        auto& elements = it2->second.editedWidget->getElementsMutable();

        // Re-add click handler
        const std::string elId = capturedElement->id;
        capturedElement->onClicked = [this, capturedTabId, elId]()
        {
            selectElement(capturedTabId, elId);
        };

        if (capturedParentId.empty())
        {
            // Was a root element
            const size_t idx = std::min(capturedIndex, elements.size());
            elements.insert(elements.begin() + static_cast<ptrdiff_t>(idx), *capturedElement);
        }
        else
        {
            // Find parent and reinsert
            const std::function<bool(std::vector<WidgetElement>&)> reinsert =
                [&](std::vector<WidgetElement>& els) -> bool
            {
                for (auto& el : els)
                {
                    if (el.id == capturedParentId)
                    {
                        const size_t idx = std::min(capturedIndex, el.children.size());
                        el.children.insert(el.children.begin() + static_cast<ptrdiff_t>(idx), *capturedElement);
                        return true;
                    }
                    if (reinsert(el.children))
                        return true;
                }
                return false;
            };
            reinsert(elements);
        }

        it2->second.editedWidget->markLayoutDirty();
        markDirty(capturedTabId);
        refreshHierarchy(capturedTabId);
        refreshDetails(capturedTabId);
    };

    cmd.execute = [this, capturedTabId, capturedElId]()
    {
        auto it2 = m_states.find(capturedTabId);
        if (it2 == m_states.end() || !it2->second.editedWidget)
            return;

        auto& elements = it2->second.editedWidget->getElementsMutable();
        WidgetElement dummy{};
        size_t dummyIdx = 0;
        const std::function<bool(std::vector<WidgetElement>&, const std::string&, WidgetElement&, size_t&)> removeEl =
            [&](std::vector<WidgetElement>& els, const std::string& id, WidgetElement& out, size_t& outIdx) -> bool
        {
            for (size_t i = 0; i < els.size(); ++i)
            {
                if (els[i].id == id)
                {
                    out = std::move(els[i]);
                    outIdx = i;
                    els.erase(els.begin() + static_cast<ptrdiff_t>(i));
                    return true;
                }
                if (removeEl(els[i].children, id, out, outIdx))
                    return true;
            }
            return false;
        };
        removeEl(elements, capturedElId, dummy, dummyIdx);

        if (it2->second.selectedElementId == capturedElId)
            it2->second.selectedElementId.clear();

        it2->second.editedWidget->markLayoutDirty();
        markDirty(capturedTabId);
        refreshHierarchy(capturedTabId);
        refreshDetails(capturedTabId);
    };

    UndoRedoManager::Instance().pushCommand(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Widget Editor: public entry point ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ delete selected element if in editor tab
// ---------------------------------------------------------------------------
bool WidgetEditorTab::tryDeleteSelectedElement()
{
    auto* state = getActiveState();
    if (!state || state->selectedElementId.empty())
        return false;

    deleteSelectedElement(state->tabId);
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: get the canvas clip rect (x, y, w, h) for the active editor
// ---------------------------------------------------------------------------
bool WidgetEditorTab::getCanvasRect(Vec4& outRect) const
{
    auto it = m_states.find(m_uiManager->getActiveTabId());
    if (it == m_states.end())
        return false;

    const auto* entry = m_uiManager->findWidgetEntry(it->second.canvasWidgetId);
    if (!entry || !entry->widget || !entry->widget->hasComputedPosition() || !entry->widget->hasComputedSize())
        return false;

    const Vec2 pos = entry->widget->getComputedPositionPixels();
    const Vec2 size = entry->widget->getComputedSizePixels();
    outRect = Vec4{ pos.x, pos.y, size.x, size.y };
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: check if a widget id belongs to a content preview widget
// ---------------------------------------------------------------------------
bool WidgetEditorTab::isContentWidget(const std::string& widgetId) const
{
    for (const auto& [tabId, state] : m_states)
    {
        if (state.contentWidgetId == widgetId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Widget Editor: get preview info for FBO rendering
// ---------------------------------------------------------------------------
bool WidgetEditorTab::getPreviewInfo(PreviewInfo& out) const
{
    auto it = m_states.find(m_uiManager->getActiveTabId());
    if (it == m_states.end() || !it->second.editedWidget)
        return false;

    const auto& state = it->second;
    out.editedWidget = state.editedWidget;
    out.selectedElementId = state.selectedElementId;
    out.hoveredElementId = state.hoveredElementId;
    out.zoom = state.zoom;
    out.panOffset = state.panOffset;
    out.dirty = state.previewDirty;
    out.tabId = state.tabId;
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: clear preview dirty flag after FBO re-render
// ---------------------------------------------------------------------------
void WidgetEditorTab::clearPreviewDirty()
{
    auto it = m_states.find(m_uiManager->getActiveTabId());
    if (it != m_states.end())
        it->second.previewDirty = false;
}

// ---------------------------------------------------------------------------
// Widget Editor: select element at screen position (click in canvas)
// ---------------------------------------------------------------------------
bool WidgetEditorTab::selectElementAtPos(const Vec2& screenPos)
{
    auto* state = getActiveState();
    if (!state || !state->editedWidget)
        return false;

    Vec4 canvasRect{};
    if (!getCanvasRect(canvasRect))
        return false;

    // Check if inside canvas
    if (screenPos.x < canvasRect.x || screenPos.x > canvasRect.x + canvasRect.z ||
        screenPos.y < canvasRect.y || screenPos.y > canvasRect.y + canvasRect.w)
        return false;

    // Transform screen position to widget-local coordinates
    const Vec2 wSize = state->editedWidget->getSizePixels();
    const float fboW = (wSize.x > 0.0f) ? wSize.x : 400.0f;
    const float fboH = (wSize.y > 0.0f) ? wSize.y : 300.0f;

    const float displayW = fboW * state->zoom;
    const float displayH = fboH * state->zoom;
    const float cx = canvasRect.x + canvasRect.z * 0.5f;
    const float cy = canvasRect.y + canvasRect.w * 0.5f;
    const float dx0 = cx - displayW * 0.5f + state->panOffset.x;
    const float dy0 = cy - displayH * 0.5f + state->panOffset.y;

    const float localX = (screenPos.x - dx0) / state->zoom;
    const float localY = (screenPos.y - dy0) / state->zoom;

    if (localX < 0.0f || localX > fboW || localY < 0.0f || localY > fboH)
    {
        selectElement(state->tabId, "");
        return true;
    }

    // Walk the element tree to find the topmost (deepest child / last sibling)
    // element whose visual rect contains the click point.
    std::string hitId;
    const auto pointInElement = [&](const WidgetElement& el) -> bool
    {
        if (!el.hasComputedPosition || !el.hasComputedSize)
            return false;
        const float ex0 = el.computedPositionPixels.x;
        const float ey0 = el.computedPositionPixels.y;
        const float ex1 = ex0 + el.computedSizePixels.x;
        const float ey1 = ey0 + el.computedSizePixels.y;
        return localX >= ex0 && localX <= ex1 && localY >= ey0 && localY <= ey1;
    };

    // Depth-first, reverse-sibling-order traversal.  The LAST match wins,
    // which corresponds to the topmost rendered element (later siblings and
    // deeper children are rendered on top).
    const std::function<void(const std::vector<WidgetElement>&)> walkElements =
        [&](const std::vector<WidgetElement>& elements)
    {
        for (auto it2 = elements.rbegin(); it2 != elements.rend(); ++it2)
        {
            const auto& el = *it2;

            // First check if the click is even inside this element's area
            // (skip subtrees that can't contain the click).
            const bool insideSelf = pointInElement(el);

            // Recurse into children ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ a matching child takes priority over
            // the parent so we check children first.
            if (!el.children.empty())
            {
                walkElements(el.children);
                if (!hitId.empty())
                    return;
            }

            // If no child was hit, check the element itself.
            if (insideSelf && !el.id.empty())
            {
                hitId = el.id;
                return;
            }
        }
    };
    walkElements(state->editedWidget->getElements());
    selectElement(state->tabId, hitId);
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: update hovered element based on mouse position
// ---------------------------------------------------------------------------
void WidgetEditorTab::updateHover(const Vec2& screenPos)
{
    auto* state = getActiveState();
    if (!state || !state->editedWidget)
        return;

    Vec4 canvasRect{};
    if (!getCanvasRect(canvasRect))
    {
        if (!state->hoveredElementId.empty())
        {
            state->hoveredElementId.clear();
            state->previewDirty = true;
        }
        return;
    }

    // Check if inside canvas
    if (screenPos.x < canvasRect.x || screenPos.x > canvasRect.x + canvasRect.z ||
        screenPos.y < canvasRect.y || screenPos.y > canvasRect.y + canvasRect.w)
    {
        if (!state->hoveredElementId.empty())
        {
            state->hoveredElementId.clear();
            state->previewDirty = true;
        }
        return;
    }

    // Transform screen position to widget-local coordinates
    const Vec2 wSize = state->editedWidget->getSizePixels();
    const float fboW = (wSize.x > 0.0f) ? wSize.x : 400.0f;
    const float fboH = (wSize.y > 0.0f) ? wSize.y : 300.0f;

    const float displayW = fboW * state->zoom;
    const float displayH = fboH * state->zoom;
    const float cx = canvasRect.x + canvasRect.z * 0.5f;
    const float cy = canvasRect.y + canvasRect.w * 0.5f;
    const float dx0 = cx - displayW * 0.5f + state->panOffset.x;
    const float dy0 = cy - displayH * 0.5f + state->panOffset.y;

    const float localX = (screenPos.x - dx0) / state->zoom;
    const float localY = (screenPos.y - dy0) / state->zoom;

    std::string hoverId;
    if (localX >= 0.0f && localX <= fboW && localY >= 0.0f && localY <= fboH)
    {
        const auto pointInElement = [&](const WidgetElement& el) -> bool
        {
            if (!el.hasComputedPosition || !el.hasComputedSize)
                return false;
            const float ex0 = el.computedPositionPixels.x;
            const float ey0 = el.computedPositionPixels.y;
            const float ex1 = ex0 + el.computedSizePixels.x;
            const float ey1 = ey0 + el.computedSizePixels.y;
            return localX >= ex0 && localX <= ex1 && localY >= ey0 && localY <= ey1;
        };

        const std::function<void(const std::vector<WidgetElement>&)> walk =
            [&](const std::vector<WidgetElement>& elements)
        {
            for (auto it2 = elements.rbegin(); it2 != elements.rend(); ++it2)
            {
                const auto& el = *it2;
                const bool insideSelf = pointInElement(el);
                if (!el.children.empty())
                {
                    walk(el.children);
                    if (!hoverId.empty())
                        return;
                }
                if (insideSelf && !el.id.empty())
                {
                    hoverId = el.id;
                    return;
                }
            }
        };
        walk(state->editedWidget->getElements());
    }

    if (hoverId != state->hoveredElementId)
    {
        state->hoveredElementId = hoverId;
        state->previewDirty = true;
    }
}


// ---------------------------------------------------------------------------
// Widget Editor: add a new element of the given type to the edited widget
// ---------------------------------------------------------------------------
void WidgetEditorTab::addElement(const std::string& tabId, const std::string& elementType)
{
    auto it = m_states.find(tabId);
    if (it == m_states.end() || !it->second.editedWidget)
        return;

    auto& state = it->second;
    auto& elements = state.editedWidget->getElementsMutable();

    // Generate a unique element id
    static int s_newElementCounter = 0;
    const std::string newId = elementType + "_" + std::to_string(++s_newElementCounter);

    WidgetElement newEl{};
    newEl.id = newId;
    newEl.hitTestMode = HitTestMode::Enabled;

    // Set type-specific defaults
    if (elementType == "Panel")
    {
        newEl.type = WidgetElementType::Panel;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.25f };
        newEl.style.color = Vec4{ 0.15f, 0.15f, 0.20f, 0.8f };
    }
    else if (elementType == "Text")
    {
        newEl.type = WidgetElementType::Text;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.15f };
        newEl.text = "New Text";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeHeading;
        newEl.style.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    }
    else if (elementType == "Button")
    {
        newEl.type = WidgetElementType::Button;
        newEl.from = Vec2{ 0.3f, 0.4f };
        newEl.to = Vec2{ 0.7f, 0.55f };
        newEl.text = "Button";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        newEl.textAlignH = TextAlignH::Center;
        newEl.textAlignV = TextAlignV::Center;
        newEl.style.color = Vec4{ 0.2f, 0.4f, 0.7f, 0.95f };
        newEl.style.hoverColor = Vec4{ 0.3f, 0.5f, 0.8f, 1.0f };
    }
    else if (elementType == "Image")
    {
        newEl.type = WidgetElementType::Image;
        newEl.from = Vec2{ 0.1f, 0.1f };
        newEl.to = Vec2{ 0.5f, 0.5f };
    }
    else if (elementType == "EntryBar")
    {
        newEl.type = WidgetElementType::EntryBar;
        newEl.from = Vec2{ 0.1f, 0.4f };
        newEl.to = Vec2{ 0.9f, 0.52f };
        newEl.value = "";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
        newEl.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
    }
    else if (elementType == "StackPanel")
    {
        newEl.type = WidgetElementType::StackPanel;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.orientation = StackOrientation::Vertical;
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.6f };
    }
    else if (elementType == "Grid")
    {
        newEl.type = WidgetElementType::Grid;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "Slider")
    {
        newEl.type = WidgetElementType::Slider;
        newEl.from = Vec2{ 0.1f, 0.45f };
        newEl.to = Vec2{ 0.9f, 0.55f };
        newEl.minValue = 0.0f;
        newEl.maxValue = 1.0f;
        newEl.valueFloat = 0.5f;
    }
    else if (elementType == "CheckBox")
    {
        newEl.type = WidgetElementType::CheckBox;
        newEl.from = Vec2{ 0.1f, 0.4f };
        newEl.to = Vec2{ 0.5f, 0.52f };
        newEl.text = "Checkbox";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
    }
    else if (elementType == "DropDown")
    {
        newEl.type = WidgetElementType::DropDown;
        newEl.from = Vec2{ 0.1f, 0.4f };
        newEl.to = Vec2{ 0.6f, 0.52f };
        newEl.items = { "Option 1", "Option 2", "Option 3" };
        newEl.selectedIndex = 0;
    }
    else if (elementType == "ColorPicker")
    {
        newEl.type = WidgetElementType::ColorPicker;
        newEl.from = Vec2{ 0.1f, 0.1f };
        newEl.to = Vec2{ 0.5f, 0.5f };
        newEl.style.color = Vec4{ 1.0f, 0.5f, 0.2f, 1.0f };
    }
    else if (elementType == "ProgressBar")
    {
        newEl.type = WidgetElementType::ProgressBar;
        newEl.from = Vec2{ 0.1f, 0.45f };
        newEl.to = Vec2{ 0.9f, 0.52f };
        newEl.minValue = 0.0f;
        newEl.maxValue = 100.0f;
        newEl.valueFloat = 50.0f;
    }
    else if (elementType == "Separator")
    {
        newEl.type = WidgetElementType::Separator;
        newEl.from = Vec2{ 0.05f, 0.49f };
        newEl.to = Vec2{ 0.95f, 0.51f };
        newEl.style.color = Vec4{ 0.3f, 0.32f, 0.38f, 0.8f };
    }
    else if (elementType == "Label")
    {
        newEl.type = WidgetElementType::Label;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.15f };
        newEl.text = "Label";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 0.85f, 0.85f, 0.90f, 1.0f };
        newEl.hitTestMode = HitTestMode::DisabledSelf;
    }
    else if (elementType == "ToggleButton")
    {
        newEl.type = WidgetElementType::ToggleButton;
        newEl.from = Vec2{ 0.3f, 0.4f };
        newEl.to = Vec2{ 0.7f, 0.55f };
        newEl.text = "Toggle";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        newEl.textAlignH = TextAlignH::Center;
        newEl.textAlignV = TextAlignV::Center;
        newEl.style.color = Vec4{ 0.2f, 0.2f, 0.3f, 0.95f };
        newEl.style.hoverColor = Vec4{ 0.3f, 0.3f, 0.4f, 1.0f };
        newEl.style.fillColor = Vec4{ 0.2f, 0.5f, 0.8f, 0.95f };
    }
    else if (elementType == "RadioButton")
    {
        newEl.type = WidgetElementType::RadioButton;
        newEl.from = Vec2{ 0.3f, 0.4f };
        newEl.to = Vec2{ 0.7f, 0.55f };
        newEl.text = "Radio";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        newEl.textAlignH = TextAlignH::Center;
        newEl.textAlignV = TextAlignV::Center;
        newEl.style.color = Vec4{ 0.2f, 0.2f, 0.3f, 0.95f };
        newEl.style.hoverColor = Vec4{ 0.3f, 0.3f, 0.4f, 1.0f };
        newEl.style.fillColor = Vec4{ 0.2f, 0.5f, 0.8f, 0.95f };
        newEl.radioGroup = "default";
    }
    else if (elementType == "ScrollView")
    {
        newEl.type = WidgetElementType::ScrollView;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.orientation = StackOrientation::Vertical;
        newEl.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.6f };
        newEl.scrollable = true;
    }
    else if (elementType == "WrapBox")
    {
        newEl.type = WidgetElementType::WrapBox;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.orientation = StackOrientation::Horizontal;
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "UniformGrid")
    {
        newEl.type = WidgetElementType::UniformGrid;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.columns = 3;
        newEl.rows = 3;
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "SizeBox")
    {
        newEl.type = WidgetElementType::SizeBox;
        newEl.from = Vec2{ 0.1f, 0.1f };
        newEl.to = Vec2{ 0.6f, 0.6f };
        newEl.widthOverride = 200.0f;
        newEl.heightOverride = 100.0f;
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "ScaleBox")
    {
        newEl.type = WidgetElementType::ScaleBox;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.scaleMode = ScaleMode::Contain;
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "WidgetSwitcher")
    {
        newEl.type = WidgetElementType::WidgetSwitcher;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.activeChildIndex = 0;
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "Overlay")
    {
        newEl.type = WidgetElementType::Overlay;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "Border")
    {
        newEl.type = WidgetElementType::Border;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
        newEl.borderThicknessLeft = 2.0f;
        newEl.borderThicknessTop = 2.0f;
        newEl.borderThicknessRight = 2.0f;
        newEl.borderThicknessBottom = 2.0f;
        newEl.borderBrush.type = BrushType::SolidColor;
        newEl.borderBrush.color = Vec4{ 0.5f, 0.5f, 0.6f, 1.0f };
        newEl.contentPadding = Vec2{ 4.0f, 4.0f };
    }
    else if (elementType == "Spinner")
    {
        newEl.type = WidgetElementType::Spinner;
        newEl.from = Vec2{ 0.4f, 0.3f };
        newEl.to = Vec2{ 0.6f, 0.6f };
        newEl.minSize = Vec2{ 32.0f, 32.0f };
        newEl.spinnerDotCount = 8;
        newEl.spinnerSpeed = 1.0f;
        newEl.style.color = Vec4{ 0.8f, 0.8f, 0.9f, 1.0f };
    }
    else if (elementType == "RichText")
    {
        newEl.type = WidgetElementType::RichText;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.5f };
        newEl.richText = "<b>Bold</b> and <i>italic</i> text";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
    }
    else if (elementType == "ListView")
    {
        newEl.type = WidgetElementType::ListView;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.totalItemCount = 10;
        newEl.itemHeight = 32.0f;
        newEl.scrollable = true;
        newEl.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.6f };
    }
    else if (elementType == "TileView")
    {
        newEl.type = WidgetElementType::TileView;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.totalItemCount = 12;
        newEl.itemHeight = 80.0f;
        newEl.itemWidth = 100.0f;
        newEl.columnsPerRow = 4;
        newEl.scrollable = true;
        newEl.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.6f };
    }
    else
    {
        newEl.type = WidgetElementType::Panel;
        newEl.from = Vec2{ 0.1f, 0.1f };
        newEl.to = Vec2{ 0.5f, 0.5f };
        newEl.style.color = Vec4{ 0.2f, 0.2f, 0.25f, 0.8f };
    }

    // Add to widget: if empty, add as root element; otherwise append to first root's children
    const std::string capturedTabId = tabId;
    const std::string capturedElId = newId;
    const bool addedAsRoot = elements.empty();
    auto capturedElement = std::make_shared<WidgetElement>(newEl);
    if (addedAsRoot)
        elements.push_back(std::move(newEl));
    else
        elements.front().children.push_back(std::move(newEl));
    state.editedWidget->markLayoutDirty();

    markDirty(tabId);
    refreshHierarchy(tabId);
    refreshDetails(tabId);

    // Push undo/redo command
    UndoRedoManager::Command cmd;
    cmd.description = "Add " + elementType;

    cmd.undo = [this, capturedTabId, capturedElId, addedAsRoot]()
    {
        auto it2 = m_states.find(capturedTabId);
        if (it2 == m_states.end() || !it2->second.editedWidget)
            return;
        auto& els = it2->second.editedWidget->getElementsMutable();
        if (addedAsRoot)
        {
            els.erase(
                std::remove_if(els.begin(), els.end(),
                    [&](const WidgetElement& e) { return e.id == capturedElId; }),
                els.end());
        }
        else
        {
            if (els.empty()) return;
            auto& children = els.front().children;
            children.erase(
                std::remove_if(children.begin(), children.end(),
                    [&](const WidgetElement& e) { return e.id == capturedElId; }),
                children.end());
        }
        if (it2->second.selectedElementId == capturedElId)
            it2->second.selectedElementId.clear();
        it2->second.editedWidget->markLayoutDirty();
        markDirty(capturedTabId);
        refreshHierarchy(capturedTabId);
        refreshDetails(capturedTabId);
    };

    cmd.execute = [this, capturedTabId, capturedElement, addedAsRoot]()
    {
        auto it2 = m_states.find(capturedTabId);
        if (it2 == m_states.end() || !it2->second.editedWidget)
            return;
        auto& els = it2->second.editedWidget->getElementsMutable();
        if (addedAsRoot)
            els.push_back(*capturedElement);
        else
        {
            if (els.empty()) return;
            els.front().children.push_back(*capturedElement);
        }
        it2->second.editedWidget->markLayoutDirty();
        markDirty(capturedTabId);
        refreshHierarchy(capturedTabId);
        refreshDetails(capturedTabId);
    };

    UndoRedoManager::Instance().pushCommand(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Widget Editor: resolve which element a hierarchy tree row represents
// ---------------------------------------------------------------------------
std::string WidgetEditorTab::resolveHierarchyRowElementId(const std::string& tabId, const std::string& rowId) const
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end() || !stateIt->second.editedWidget)
        return {};

    // Row ids are "WidgetEditor.Left.TreeRow.<index>", extract the index
    const std::string prefix = "WidgetEditor.Left.TreeRow.";
    if (rowId.rfind(prefix, 0) != 0)
        return {};
    int targetIndex = -1;
    try { targetIndex = std::stoi(rowId.substr(prefix.size())); }
    catch (...) { return {}; }

    // Walk the element tree in the same order as buildTree to find the element at that index
    int currentIndex = 0;
    std::string result;
    const std::function<bool(const std::vector<WidgetElement>&)> findAtIndex =
        [&](const std::vector<WidgetElement>& elements) -> bool
    {
        for (const auto& el : elements)
        {
            if (currentIndex == targetIndex)
            {
                result = el.id;
                return true;
            }
            ++currentIndex;
            if (findAtIndex(el.children))
                return true;
        }
        return false;
    };

    findAtIndex(stateIt->second.editedWidget->getElements());
    return result;
}

// ---------------------------------------------------------------------------
// Widget Editor: move an element to a new position in the hierarchy
// (inserts as sibling after the target element, or as child if target is a container)
// ---------------------------------------------------------------------------
void WidgetEditorTab::moveElement(const std::string& tabId,
    const std::string& draggedId, const std::string& targetId)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end() || !stateIt->second.editedWidget)
        return;

    auto& elements = stateIt->second.editedWidget->getElementsMutable();

    // Helper: recursively remove an element by id from a vector, returning the removed element
    WidgetElement removed{};
    bool found = false;
    const std::function<bool(std::vector<WidgetElement>&)> removeById =
        [&](std::vector<WidgetElement>& els) -> bool
    {
        for (auto it = els.begin(); it != els.end(); ++it)
        {
            if (it->id == draggedId)
            {
                removed = std::move(*it);
                els.erase(it);
                found = true;
                return true;
            }
            if (removeById(it->children))
                return true;
        }
        return false;
    };

    // Helper: check if targetId is a descendant of an element
    const std::function<bool(const WidgetElement&)> isDescendant =
        [&](const WidgetElement& el) -> bool
    {
        for (const auto& child : el.children)
        {
            if (child.id == targetId)
                return true;
            if (isDescendant(child))
                return true;
        }
        return false;
    };

    // Prevent dropping an element onto its own descendant
    {
        const std::function<const WidgetElement*(const std::vector<WidgetElement>&, const std::string&)> findEl =
            [&](const std::vector<WidgetElement>& els, const std::string& id) -> const WidgetElement*
        {
            for (const auto& el : els)
            {
                if (el.id == id)
                    return &el;
                if (auto* r = findEl(el.children, id))
                    return r;
            }
            return nullptr;
        };
        if (const auto* draggedEl = findEl(elements, draggedId))
        {
            if (isDescendant(*draggedEl))
                return; // would create a cycle
        }
    }

    // Remove the dragged element from its current position
    removeById(elements);
    if (!found)
        return;

    // Insert as sibling after the target element (same parent)
    const std::function<bool(std::vector<WidgetElement>&)> insertAfter =
        [&](std::vector<WidgetElement>& els) -> bool
    {
        for (auto it = els.begin(); it != els.end(); ++it)
        {
            if (it->id == targetId)
            {
                els.insert(it + 1, std::move(removed));
                return true;
            }
            if (insertAfter(it->children))
                return true;
        }
        return false;
    };

    if (!insertAfter(elements))
    {
        // Fallback: if target not found (shouldn't happen), put back as last root child
        if (!elements.empty())
            elements.front().children.push_back(std::move(removed));
        else
            elements.push_back(std::move(removed));
    }

    stateIt->second.editedWidget->markLayoutDirty();
    markDirty(tabId);
    refreshHierarchy(tabId);
    refreshDetails(tabId);
}

// ---------------------------------------------------------------------------
// Widget Editor: rebuild the hierarchy tree in the left panel
// ---------------------------------------------------------------------------
void WidgetEditorTab::refreshHierarchy(const std::string& tabId)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end())
        return;

    const auto& editorState = stateIt->second;
    auto* leftEntry = m_uiManager->findWidgetEntry(editorState.leftWidgetId);
    if (!leftEntry || !leftEntry->widget)
        return;

    auto& leftElements = leftEntry->widget->getElementsMutable();
    WidgetElement* treePanel = FindElementById(leftElements, "WidgetEditor.Left.Tree");
    if (!treePanel)
        return;

    treePanel->children.clear();
    m_uiManager->clearLastHoveredElement();

    if (!editorState.editedWidget)
        return;

    const std::string& selectedId = editorState.selectedElementId;
    int lineIndex = 0;

    const std::function<void(const std::vector<WidgetElement>&, int)> buildTree =
        [&](const std::vector<WidgetElement>& elements, int depth)
    {
        for (const auto& el : elements)
        {
            const std::string elId = el.id;
            const bool isSelected = (!elId.empty() && elId == selectedId);

            std::string indent(depth * 2, ' ');
            std::string typeName;
            switch (el.type)
            {
            case WidgetElementType::Panel:       typeName = "Panel"; break;
            case WidgetElementType::Text:        typeName = "Text"; break;
            case WidgetElementType::Button:      typeName = "Button"; break;
            case WidgetElementType::Image:       typeName = "Image"; break;
            case WidgetElementType::EntryBar:    typeName = "EntryBar"; break;
            case WidgetElementType::StackPanel:  typeName = "StackPanel"; break;
            case WidgetElementType::Grid:        typeName = "Grid"; break;
            case WidgetElementType::Slider:      typeName = "Slider"; break;
            case WidgetElementType::CheckBox:    typeName = "CheckBox"; break;
            case WidgetElementType::DropDown:    typeName = "DropDown"; break;
            case WidgetElementType::ColorPicker: typeName = "ColorPicker"; break;
            case WidgetElementType::ProgressBar: typeName = "ProgressBar"; break;
            case WidgetElementType::DropdownButton: typeName = "DropdownButton"; break;
            case WidgetElementType::TreeView:    typeName = "TreeView"; break;
            case WidgetElementType::TabView:     typeName = "TabView"; break;
            case WidgetElementType::Separator:   typeName = "Separator"; break;
            case WidgetElementType::ScrollView:  typeName = "ScrollView"; break;
            case WidgetElementType::Label:       typeName = "Label"; break;
            case WidgetElementType::ToggleButton:typeName = "ToggleButton"; break;
            case WidgetElementType::RadioButton: typeName = "RadioButton"; break;
            case WidgetElementType::WrapBox:     typeName = "WrapBox"; break;
            case WidgetElementType::UniformGrid: typeName = "UniformGrid"; break;
            case WidgetElementType::SizeBox:     typeName = "SizeBox"; break;
            case WidgetElementType::ScaleBox:    typeName = "ScaleBox"; break;
            case WidgetElementType::WidgetSwitcher: typeName = "WidgetSwitcher"; break;
            case WidgetElementType::Overlay:     typeName = "Overlay"; break;
            case WidgetElementType::Border:      typeName = "Border"; break;
            case WidgetElementType::Spinner:     typeName = "Spinner"; break;
            case WidgetElementType::RichText:    typeName = "RichText"; break;
            case WidgetElementType::ListView:    typeName = "ListView"; break;
            case WidgetElementType::TileView:    typeName = "TileView"; break;
            default:                             typeName = "Unknown"; break;
            }

            std::string label = indent;
            if (!el.children.empty())
                label += "> ";
            label += "[" + typeName + "]";
            if (!elId.empty())
                label += " " + elId;

            WidgetElement row{};
            row.id = "WidgetEditor.Left.TreeRow." + std::to_string(lineIndex);
            row.type = WidgetElementType::Button;
            row.text = label;
            row.font = EditorTheme::Get().fontDefault;
            row.fontSize = EditorTheme::Get().fontSizeSmall;
            row.textAlignH = TextAlignH::Left;
            row.textAlignV = TextAlignV::Center;
            row.fillX = true;
            row.minSize = Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall };
            row.padding = Vec2{ 4.0f, 1.0f };
            row.hitTestMode = HitTestMode::Enabled;
            row.runtimeOnly = true;

            if (isSelected)
            {
                row.style.color = EditorTheme::Get().selectionHighlight;
                row.style.hoverColor = EditorTheme::Get().selectionHighlightHover;
                row.style.textColor = EditorTheme::Get().textPrimary;
            }
            else
            {
                row.style.color = EditorTheme::Get().transparent;
                row.style.hoverColor = EditorTheme::Get().buttonSubtleHover;
                row.style.textColor = EditorTheme::Get().textSecondary;
            }

            const std::string capturedTabId = tabId;
            row.onClicked = [this, capturedTabId, elId]()
            {
                selectElement(capturedTabId, elId);
            };

            // Make row draggable for hierarchy reordering
            row.isDraggable = true;
            row.dragPayload = "WidgetHierarchy|" + elId;

            treePanel->children.push_back(std::move(row));
            ++lineIndex;

            buildTree(el.children, depth + 1);
        }
    };

    buildTree(editorState.editedWidget->getElements(), 0);
    leftEntry->widget->markLayoutDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: rebuild the details panel for the selected element
// ---------------------------------------------------------------------------
void WidgetEditorTab::refreshDetails(const std::string& tabId)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end())
        return;

    auto& editorState = stateIt->second;
    auto* rightEntry = m_uiManager->findWidgetEntry(editorState.rightWidgetId);
    if (!rightEntry || !rightEntry->widget)
        return;

    auto& rightElements = rightEntry->widget->getElementsMutable();
    WidgetElement* rootPanel = FindElementById(rightElements, "WidgetEditor.Right.Root");
    if (!rootPanel)
        return;

    // Keep only the title (first child)
    if (rootPanel->children.size() > 1)
        rootPanel->children.erase(rootPanel->children.begin() + 1, rootPanel->children.end());

    m_uiManager->clearLastHoveredElement();

    // If no element is selected, show a hint
    if (editorState.selectedElementId.empty() || !editorState.editedWidget)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Select an element to view its properties."));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Find the selected element in the edited widget
    WidgetElement* selected = nullptr;
    {
        auto& elems = editorState.editedWidget->getElementsMutable();
        selected = FindElementById(elems, editorState.selectedElementId);
    }

    if (!selected)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Element not found: " + editorState.selectedElementId));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    const std::string capturedTabId = tabId;

    const auto applyChange = [this, capturedTabId]() {
        markDirty(capturedTabId);
        auto it2 = m_states.find(capturedTabId);
        if (it2 != m_states.end() && it2->second.editedWidget)
            it2->second.editedWidget->markLayoutDirty();
        m_uiManager->markAllWidgetsDirty();
    };

    WidgetDetailSchema::Options opts;
    opts.showEditableId = true;
    opts.onIdRenamed = [this, capturedTabId](const std::string& newId) {
        auto it2 = m_states.find(capturedTabId);
        if (it2 != m_states.end())
            it2->second.selectedElementId = newId;
    };
    opts.onRefreshHierarchy = [this, capturedTabId]() {
        refreshHierarchy(capturedTabId);
    };

    WidgetDetailSchema::buildDetailPanel("WE.Det", selected, applyChange, rootPanel, opts);

    rightEntry->widget->markLayoutDirty();
    m_uiManager->markAllWidgetsDirty();
}


void WidgetEditorTab::refreshTimeline(const std::string& tabId)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end())
        return;

    auto& edState = stateIt->second;
    const std::string& bottomId = edState.bottomWidgetId;

    // If panel is hidden, remove the bottom widget entirely
    if (!edState.showAnimationsPanel)
    {
        m_uiManager->unregisterWidget(bottomId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    // Create or update the bottom animation panel widget
    auto bottomWidget = std::make_shared<EditorWidget>();
    bottomWidget->setName(bottomId);
    bottomWidget->setAnchor(WidgetAnchor::BottomLeft);
    bottomWidget->setFillX(true);
    bottomWidget->setSizePixels(Vec2{ 0.0f, 260.0f });
    bottomWidget->setZOrder(2);

    WidgetElement root{};
    root.id = "WE.Timeline.Root";
    root.type = WidgetElementType::StackPanel;
    root.from = Vec2{ 0.0f, 0.0f };
    root.to = Vec2{ 1.0f, 1.0f };
    root.fillX = true;
    root.fillY = true;
    root.orientation = StackOrientation::Horizontal;
    root.style.color = Vec4{ 0.10f, 0.11f, 0.14f, 0.98f };
    root.runtimeOnly = true;

    // --- Left side: animation list (150px) ---
    {
        WidgetElement leftPanel{};
        leftPanel.id = "WE.Timeline.Left";
        leftPanel.type = WidgetElementType::StackPanel;
        leftPanel.orientation = StackOrientation::Vertical;
        leftPanel.minSize = Vec2{ 150.0f, 0.0f };
        leftPanel.fillY = true;
        leftPanel.padding = Vec2{ 6.0f, 6.0f };
        leftPanel.style.color = Vec4{ 0.12f, 0.13f, 0.17f, 1.0f };
        leftPanel.scrollable = true;
        leftPanel.runtimeOnly = true;

        // Header row: "Animations" + "+" button
        {
            WidgetElement headerRow{};
            headerRow.id = "WE.Timeline.Left.Header";
            headerRow.type = WidgetElementType::StackPanel;
            headerRow.orientation = StackOrientation::Horizontal;
            headerRow.fillX = true;
            headerRow.minSize = Vec2{ 0.0f, 24.0f };
            headerRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            headerRow.runtimeOnly = true;

            WidgetElement title{};
            title.id = "WE.Timeline.Left.Title";
            title.type = WidgetElementType::Text;
            title.text = "Animations";
            title.font = EditorTheme::Get().fontDefault;
            title.fontSize = EditorTheme::Get().fontSizeBody;
            title.fillX = true;
            title.style.textColor = EditorTheme::Get().textPrimary;
            title.textAlignH = TextAlignH::Left;
            title.textAlignV = TextAlignV::Center;
            title.runtimeOnly = true;
            headerRow.children.push_back(std::move(title));

            WidgetElement addBtn{};
            addBtn.id = "WE.Timeline.Left.Add";
            addBtn.type = WidgetElementType::Button;
            addBtn.text = "+";
            addBtn.font = EditorTheme::Get().fontDefault;
            addBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
            addBtn.minSize = Vec2{ 24.0f, EditorTheme::Get().rowHeight };
            addBtn.style.color = EditorTheme::Get().buttonDefault;
            addBtn.style.hoverColor = EditorTheme::Get().buttonHover;
            addBtn.style.textColor = EditorTheme::Get().accentGreen;
            addBtn.textAlignH = TextAlignH::Center;
            addBtn.textAlignV = TextAlignV::Center;
            addBtn.hitTestMode = HitTestMode::Enabled;
            addBtn.runtimeOnly = true;
            const std::string capturedTabId = tabId;
            addBtn.onClicked = [this, capturedTabId]()
            {
                auto it = m_states.find(capturedTabId);
                if (it == m_states.end() || !it->second.editedWidget)
                    return;
                auto& anims = it->second.editedWidget->getAnimationsMutable();
                std::string newName = "Anim_" + std::to_string(anims.size());
                WidgetAnimation newAnim;
                newAnim.name = newName;
                newAnim.duration = 1.0f;
                anims.push_back(std::move(newAnim));
                it->second.selectedAnimationName = newName;
                markDirty(capturedTabId);
                refreshTimeline(capturedTabId);
            };
            headerRow.children.push_back(std::move(addBtn));

            leftPanel.children.push_back(std::move(headerRow));
        }

        // Animation entries
        if (edState.editedWidget)
        {
            const auto& anims = edState.editedWidget->getAnimations();
            for (size_t i = 0; i < anims.size(); ++i)
            {
                const auto& anim = anims[i];
                const bool selected = (anim.name == edState.selectedAnimationName);

                WidgetElement row{};
                row.id = "WE.Timeline.Left.Anim." + std::to_string(i);
                row.type = WidgetElementType::StackPanel;
                row.orientation = StackOrientation::Horizontal;
                row.fillX = true;
                row.minSize = Vec2{ 0.0f, 22.0f };
                row.style.color = selected ? Vec4{ 0.20f, 0.30f, 0.55f, 0.9f } : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.style.hoverColor = selected ? Vec4{ 0.25f, 0.35f, 0.60f, 1.0f } : Vec4{ 0.18f, 0.20f, 0.28f, 0.8f };
                row.hitTestMode = HitTestMode::Enabled;
                row.runtimeOnly = true;

                WidgetElement nameLabel{};
                nameLabel.id = "WE.Timeline.Left.AnimName." + std::to_string(i);
                nameLabel.type = WidgetElementType::Text;
                nameLabel.text = "  " + anim.name;
                nameLabel.font = EditorTheme::Get().fontDefault;
                nameLabel.fontSize = EditorTheme::Get().fontSizeSmall;
                nameLabel.fillX = true;
                nameLabel.style.textColor = selected ? EditorTheme::Get().textPrimary : EditorTheme::Get().textSecondary;
                nameLabel.textAlignH = TextAlignH::Left;
                nameLabel.textAlignV = TextAlignV::Center;
                nameLabel.runtimeOnly = true;
                row.children.push_back(std::move(nameLabel));

                // Delete button
                WidgetElement delBtn{};
                delBtn.id = "WE.Timeline.Left.AnimDel." + std::to_string(i);
                delBtn.type = WidgetElementType::Button;
                delBtn.text = "x";
                delBtn.font = EditorTheme::Get().fontDefault;
                delBtn.fontSize = EditorTheme::Get().fontSizeSmall;
                delBtn.minSize = Vec2{ 20.0f, EditorTheme::Get().rowHeightSmall };
                delBtn.style.color = EditorTheme::Get().transparent;
                delBtn.style.hoverColor = EditorTheme::Get().buttonDangerHover;
                delBtn.style.textColor = EditorTheme::Get().buttonDanger;
                delBtn.textAlignH = TextAlignH::Center;
                delBtn.textAlignV = TextAlignV::Center;
                delBtn.hitTestMode = HitTestMode::Enabled;
                delBtn.runtimeOnly = true;
                const std::string capturedTabId2 = tabId;
                const std::string animName = anim.name;
                delBtn.onClicked = [this, capturedTabId2, animName]()
                {
                    auto it = m_states.find(capturedTabId2);
                    if (it == m_states.end() || !it->second.editedWidget)
                        return;
                    auto& anims2 = it->second.editedWidget->getAnimationsMutable();
                    anims2.erase(std::remove_if(anims2.begin(), anims2.end(),
                        [&](const WidgetAnimation& a) { return a.name == animName; }), anims2.end());
                    if (it->second.selectedAnimationName == animName)
                        it->second.selectedAnimationName.clear();
                    markDirty(capturedTabId2);
                    refreshTimeline(capturedTabId2);
                };
                row.children.push_back(std::move(delBtn));

                // Click to select
                const std::string capturedTabId3 = tabId;
                const std::string capturedAnimName = anim.name;
                row.onClicked = [this, capturedTabId3, capturedAnimName]()
                {
                    auto it = m_states.find(capturedTabId3);
                    if (it != m_states.end())
                    {
                        it->second.selectedAnimationName = capturedAnimName;
                        refreshTimeline(capturedTabId3);
                    }
                };
                leftPanel.children.push_back(std::move(row));
            }
        }

        root.children.push_back(std::move(leftPanel));
    }

    // --- Right side: timeline content ---
    {
        WidgetElement rightPanel{};
        rightPanel.id = "WE.Timeline.Right";
        rightPanel.type = WidgetElementType::StackPanel;
        rightPanel.orientation = StackOrientation::Vertical;
        rightPanel.fillX = true;
        rightPanel.fillY = true;
        rightPanel.padding = Vec2{ 6.0f, 6.0f };
        rightPanel.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        rightPanel.runtimeOnly = true;

        // Toolbar row: +Track, Play, Stop, Duration, Loop
        {
            WidgetElement toolbar{};
            toolbar.id = "WE.Timeline.Right.Toolbar";
            toolbar.type = WidgetElementType::StackPanel;
            toolbar.orientation = StackOrientation::Horizontal;
            toolbar.fillX = true;
            toolbar.minSize = Vec2{ 0.0f, 26.0f };
            toolbar.spacing = 4.0f;
            toolbar.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            toolbar.runtimeOnly = true;

            auto makeToolBtn = [&](const std::string& id, const std::string& text, std::function<void()> onClick) {
                WidgetElement btn{};
                btn.id = id;
                btn.type = WidgetElementType::Button;
                btn.text = text;
                btn.font = EditorTheme::Get().fontDefault;
                btn.fontSize = EditorTheme::Get().fontSizeBody;
                btn.minSize = Vec2{ 28.0f, EditorTheme::Get().rowHeight };
                btn.padding = Vec2{ 6.0f, 2.0f };
                btn.style.color = EditorTheme::Get().buttonDefault;
                btn.style.hoverColor = EditorTheme::Get().buttonHover;
                btn.style.textColor = EditorTheme::Get().textPrimary;
                btn.textAlignH = TextAlignH::Center;
                btn.textAlignV = TextAlignV::Center;
                btn.hitTestMode = HitTestMode::Enabled;
                btn.runtimeOnly = true;
                btn.onClicked = std::move(onClick);
                return btn;
            };

            const std::string capturedTabId = tabId;

            // + Track button
            toolbar.children.push_back(makeToolBtn("WE.Timeline.AddTrack", "+ Track", [this, capturedTabId]()
            {
                auto it = m_states.find(capturedTabId);
                if (it == m_states.end() || !it->second.editedWidget)
                    return;
                auto* anim = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                if (!anim) return;
                AnimationTrack track;
                track.targetElementId = "root";
                track.property = AnimatableProperty::Opacity;
                anim->tracks.push_back(std::move(track));
                markDirty(capturedTabId);
                refreshTimeline(capturedTabId);
            }));

            // Play button
            toolbar.children.push_back(makeToolBtn("WE.Timeline.Play", "\xe2\x96\xb6", [this, capturedTabId]()
            {
                auto it = m_states.find(capturedTabId);
                if (it == m_states.end() || !it->second.editedWidget)
                    return;
                auto& player = it->second.editedWidget->animationPlayer();
                if (!player.isPlaying())
                    player.play(it->second.selectedAnimationName);
                else
                    player.pause();
                it->second.previewDirty = true;
            }));

            // Stop button
            toolbar.children.push_back(makeToolBtn("WE.Timeline.Stop", "\xe2\x96\xa0", [this, capturedTabId]()
            {
                auto it = m_states.find(capturedTabId);
                if (it == m_states.end() || !it->second.editedWidget)
                    return;
                it->second.editedWidget->animationPlayer().stop();
                it->second.timelineScrubTime = 0.0f;
                it->second.previewDirty = true;
                refreshTimeline(capturedTabId);
            }));

            // Duration label + value
            {
                WidgetElement durLabel{};
                durLabel.id = "WE.Timeline.DurLabel";
                durLabel.type = WidgetElementType::Text;
                durLabel.text = "Duration:";
                durLabel.font = EditorTheme::Get().fontDefault;
                durLabel.fontSize = EditorTheme::Get().fontSizeSmall;
                durLabel.style.textColor = EditorTheme::Get().textMuted;
                durLabel.textAlignV = TextAlignV::Center;
                durLabel.minSize = Vec2{ 60.0f, 24.0f };
                durLabel.runtimeOnly = true;
                toolbar.children.push_back(std::move(durLabel));
            }

            if (edState.editedWidget)
            {
                const auto* anim = edState.editedWidget->findAnimationByName(edState.selectedAnimationName);
                float dur = anim ? anim->duration : 1.0f;

                WidgetElement durEntry{};
                durEntry.id = "WE.Timeline.DurEntry";
                durEntry.type = WidgetElementType::EntryBar;
                durEntry.text = std::to_string(dur).substr(0, 5);
                durEntry.font = EditorTheme::Get().fontDefault;
                durEntry.fontSize = EditorTheme::Get().fontSizeSmall;
                durEntry.minSize = Vec2{ 50.0f, EditorTheme::Get().rowHeightSmall };
                durEntry.style.color = EditorTheme::Get().inputBackground;
                durEntry.style.textColor = EditorTheme::Get().inputText;
                durEntry.hitTestMode = HitTestMode::Enabled;
                durEntry.runtimeOnly = true;
                const std::string capturedTabId2 = tabId;
                durEntry.onValueChanged = [this, capturedTabId2](const std::string& val)
                {
                    auto it = m_states.find(capturedTabId2);
                    if (it == m_states.end() || !it->second.editedWidget)
                        return;
                    auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                    if (a) { try { a->duration = std::max(0.01f, std::stof(val)); } catch (...) {} }
                    markDirty(capturedTabId2);
                    refreshTimeline(capturedTabId2);
                };
                toolbar.children.push_back(std::move(durEntry));

                // Loop checkbox
                bool isLoop = anim ? anim->isLooping : false;
                WidgetElement loopCb{};
                loopCb.id = "WE.Timeline.Loop";
                loopCb.type = WidgetElementType::CheckBox;
                loopCb.text = "Loop";
                loopCb.font = EditorTheme::Get().fontDefault;
                loopCb.fontSize = EditorTheme::Get().fontSizeSmall;
                loopCb.isChecked = isLoop;
                loopCb.minSize = Vec2{ 55.0f, EditorTheme::Get().rowHeightSmall };
                loopCb.style.color = EditorTheme::Get().inputBackground;
                loopCb.style.textColor = EditorTheme::Get().textSecondary;
                loopCb.hitTestMode = HitTestMode::Enabled;
                loopCb.runtimeOnly = true;
                const std::string capturedTabId3 = tabId;
                loopCb.onCheckedChanged = [this, capturedTabId3](bool checked)
                {
                    auto it = m_states.find(capturedTabId3);
                    if (it == m_states.end() || !it->second.editedWidget)
                        return;
                    auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                    if (a) a->isLooping = checked;
                    markDirty(capturedTabId3);
                };
                toolbar.children.push_back(std::move(loopCb));
            }

            rightPanel.children.push_back(std::move(toolbar));
        }

        // Track area: left = track tree, right = keyframe ruler
        if (edState.editedWidget && !edState.selectedAnimationName.empty())
        {
            const auto* anim = edState.editedWidget->findAnimationByName(edState.selectedAnimationName);
            if (anim)
            {
                WidgetElement trackArea{};
                trackArea.id = "WE.Timeline.TrackArea";
                trackArea.type = WidgetElementType::StackPanel;
                trackArea.orientation = StackOrientation::Horizontal;
                trackArea.fillX = true;
                trackArea.fillY = true;
                trackArea.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                trackArea.runtimeOnly = true;

                // Track tree (left, 200px)
                {
                    WidgetElement trackTree{};
                    trackTree.id = "WE.Timeline.TrackTree";
                    trackTree.type = WidgetElementType::StackPanel;
                    trackTree.orientation = StackOrientation::Vertical;
                    trackTree.minSize = Vec2{ 200.0f, 0.0f };
                    trackTree.fillY = true;
                    trackTree.scrollable = true;
                    trackTree.style.color = Vec4{ 0.11f, 0.12f, 0.15f, 1.0f };
                    trackTree.runtimeOnly = true;

                    buildTimelineTrackRows(tabId, trackTree);
                    trackArea.children.push_back(std::move(trackTree));
                }

                // Keyframe ruler + diamonds (right, fills remaining)
                {
                    WidgetElement rulerArea{};
                    rulerArea.id = "WE.Timeline.Ruler";
                    rulerArea.type = WidgetElementType::Panel;
                    rulerArea.fillX = true;
                    rulerArea.fillY = true;
                    rulerArea.style.color = Vec4{ 0.08f, 0.09f, 0.12f, 1.0f };
                    rulerArea.runtimeOnly = true;

                    buildTimelineRulerAndKeyframes(tabId, rulerArea);
                    trackArea.children.push_back(std::move(rulerArea));
                }

                rightPanel.children.push_back(std::move(trackArea));
            }
        }

        root.children.push_back(std::move(rightPanel));
    }

    bottomWidget->setElements({ std::move(root) });
    m_uiManager->unregisterWidget(bottomId);
    m_uiManager->registerWidget(bottomId, bottomWidget, tabId);
    m_uiManager->markAllWidgetsDirty();
}

void WidgetEditorTab::buildTimelineTrackRows(const std::string& tabId, WidgetElement& container)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end() || !stateIt->second.editedWidget)
        return;

    auto& edState = stateIt->second;
    const auto* anim = edState.editedWidget->findAnimationByName(edState.selectedAnimationName);
    if (!anim)
        return;

    int rowIndex = 0;
    for (size_t ti = 0; ti < anim->tracks.size(); ++ti)
    {
        const auto& track = anim->tracks[ti];
        bool isExpanded = edState.expandedTimelineElements.count(track.targetElementId) > 0;

        // Element header row
        {
            WidgetElement headerRow{};
            headerRow.id = "WE.TL.Track." + std::to_string(ti);
            headerRow.type = WidgetElementType::StackPanel;
            headerRow.orientation = StackOrientation::Horizontal;
            headerRow.fillX = true;
            headerRow.minSize = Vec2{ 0.0f, 20.0f };
            bool evenRow = (rowIndex % 2 == 0);
            headerRow.style.color = evenRow ? Vec4{ 0.13f, 0.14f, 0.18f, 1.0f } : Vec4{ 0.11f, 0.12f, 0.15f, 1.0f };
            headerRow.style.hoverColor = Vec4{ 0.18f, 0.20f, 0.28f, 0.8f };
            headerRow.hitTestMode = HitTestMode::Enabled;
            headerRow.runtimeOnly = true;

            // Expand/collapse chevron
            WidgetElement chevron{};
            chevron.id = "WE.TL.Chev." + std::to_string(ti);
            chevron.type = WidgetElementType::Text;
            chevron.text = isExpanded ? " \xe2\x96\xbe " : " \xe2\x96\xb8 ";
            chevron.font = EditorTheme::Get().fontDefault;
            chevron.fontSize = EditorTheme::Get().fontSizeSmall;
            chevron.style.textColor = EditorTheme::Get().textMuted;
            chevron.minSize = Vec2{ 20.0f, 20.0f };
            chevron.textAlignV = TextAlignV::Center;
            chevron.runtimeOnly = true;
            headerRow.children.push_back(std::move(chevron));

            // Element ID + property name
            WidgetElement label{};
            label.id = "WE.TL.Label." + std::to_string(ti);
            label.type = WidgetElementType::Text;
            label.text = track.targetElementId + " : " + AnimatablePropertyToString(track.property);
            label.font = EditorTheme::Get().fontDefault;
            label.fontSize = EditorTheme::Get().fontSizeSmall;
            label.fillX = true;
            label.style.textColor = EditorTheme::Get().textSecondary;
            label.textAlignH = TextAlignH::Left;
            label.textAlignV = TextAlignV::Center;
            label.runtimeOnly = true;
            headerRow.children.push_back(std::move(label));

            // Remove track button
            WidgetElement removeBtn{};
            removeBtn.id = "WE.TL.Rem." + std::to_string(ti);
            removeBtn.type = WidgetElementType::Button;
            removeBtn.text = "x";
            removeBtn.font = EditorTheme::Get().fontDefault;
            removeBtn.fontSize = EditorTheme::Get().fontSizeSmall;
            removeBtn.minSize = Vec2{ 18.0f, 18.0f };
            removeBtn.style.color = EditorTheme::Get().transparent;
            removeBtn.style.hoverColor = EditorTheme::Get().buttonDangerHover;
            removeBtn.style.textColor = EditorTheme::Get().buttonDanger;
            removeBtn.textAlignH = TextAlignH::Center;
            removeBtn.textAlignV = TextAlignV::Center;
            removeBtn.hitTestMode = HitTestMode::Enabled;
            removeBtn.runtimeOnly = true;
            const std::string capturedTabId = tabId;
            const size_t trackIdx = ti;
            removeBtn.onClicked = [this, capturedTabId, trackIdx]()
            {
                auto it = m_states.find(capturedTabId);
                if (it == m_states.end() || !it->second.editedWidget)
                    return;
                auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                if (a && trackIdx < a->tracks.size())
                {
                    a->tracks.erase(a->tracks.begin() + static_cast<ptrdiff_t>(trackIdx));
                    markDirty(capturedTabId);
                    refreshTimeline(capturedTabId);
                }
            };
            headerRow.children.push_back(std::move(removeBtn));

            // Toggle expand on click
            const std::string capturedTabId2 = tabId;
            const std::string elemId = track.targetElementId;
            headerRow.onClicked = [this, capturedTabId2, elemId]()
            {
                auto it = m_states.find(capturedTabId2);
                if (it == m_states.end()) return;
                auto& expanded = it->second.expandedTimelineElements;
                if (expanded.count(elemId))
                    expanded.erase(elemId);
                else
                    expanded.insert(elemId);
                refreshTimeline(capturedTabId2);
            };

            container.children.push_back(std::move(headerRow));
            ++rowIndex;
        }

        // Expanded keyframe rows
        if (isExpanded)
        {
            for (size_t ki = 0; ki < track.keyframes.size(); ++ki)
            {
                const auto& kf = track.keyframes[ki];
                WidgetElement kfRow{};
                kfRow.id = "WE.TL.KF." + std::to_string(ti) + "." + std::to_string(ki);
                kfRow.type = WidgetElementType::StackPanel;
                kfRow.orientation = StackOrientation::Horizontal;
                kfRow.fillX = true;
                kfRow.minSize = Vec2{ 0.0f, 18.0f };
                kfRow.padding = Vec2{ 20.0f, 0.0f };
                bool evenRow = (rowIndex % 2 == 0);
                kfRow.style.color = evenRow ? Vec4{ 0.14f, 0.15f, 0.19f, 1.0f } : Vec4{ 0.12f, 0.13f, 0.16f, 1.0f };
                kfRow.runtimeOnly = true;

                // Diamond marker
                WidgetElement diamond{};
                diamond.id = "WE.TL.KFD." + std::to_string(ti) + "." + std::to_string(ki);
                diamond.type = WidgetElementType::Text;
                diamond.text = "\xe2\x97\x86";
                diamond.font = EditorTheme::Get().fontDefault;
                diamond.fontSize = EditorTheme::Scaled(7.0f);
                diamond.style.textColor = EditorTheme::Get().tlKeyframeDiamond;
                diamond.minSize = Vec2{ 14.0f, 18.0f };
                diamond.textAlignH = TextAlignH::Center;
                diamond.textAlignV = TextAlignV::Center;
                diamond.runtimeOnly = true;
                kfRow.children.push_back(std::move(diamond));

                // Time label (prefix)
                WidgetElement timePre{};
                timePre.id = "WE.TL.KFTpre." + std::to_string(ti) + "." + std::to_string(ki);
                timePre.type = WidgetElementType::Text;
                timePre.text = "t=";
                timePre.font = EditorTheme::Get().fontDefault;
                timePre.fontSize = EditorTheme::Get().fontSizeCaption;
                timePre.minSize = Vec2{ 14.0f, 18.0f };
                timePre.style.textColor = EditorTheme::Get().textMuted;
                timePre.textAlignH = TextAlignH::Right;
                timePre.textAlignV = TextAlignV::Center;
                timePre.runtimeOnly = true;
                kfRow.children.push_back(std::move(timePre));

                // Time entry bar (editable)
                std::string timeStr = std::to_string(kf.time);
                if (timeStr.size() > 5) timeStr = timeStr.substr(0, 5);
                WidgetElement timeEntry{};
                timeEntry.id = "WE.TL.KFT." + std::to_string(ti) + "." + std::to_string(ki);
                timeEntry.type = WidgetElementType::EntryBar;
                timeEntry.value = timeStr;
                timeEntry.font = EditorTheme::Get().fontDefault;
                timeEntry.fontSize = EditorTheme::Get().fontSizeCaption;
                timeEntry.fillX = true;
                timeEntry.minSize = Vec2{ 40.0f, 18.0f };
                timeEntry.padding = Vec2{ 2.0f, 1.0f };
                timeEntry.style.color = EditorTheme::Get().inputBackground;
                timeEntry.style.textColor = EditorTheme::Get().inputText;
                timeEntry.hitTestMode = HitTestMode::Enabled;
                timeEntry.runtimeOnly = true;
                {
                    const std::string capturedTabIdT = tabId;
                    const size_t trackIdxT = ti;
                    const size_t kfIdxT = ki;
                    timeEntry.onValueChanged = [this, capturedTabIdT, trackIdxT, kfIdxT](const std::string& newVal)
                    {
                        auto it = m_states.find(capturedTabIdT);
                        if (it == m_states.end() || !it->second.editedWidget)
                            return;
                        auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                        if (a && trackIdxT < a->tracks.size() && kfIdxT < a->tracks[trackIdxT].keyframes.size())
                        {
                            try { a->tracks[trackIdxT].keyframes[kfIdxT].time = std::stof(newVal); }
                            catch (...) {}
                            std::sort(a->tracks[trackIdxT].keyframes.begin(), a->tracks[trackIdxT].keyframes.end(),
                                [](const AnimationKeyframe& a2, const AnimationKeyframe& b) { return a2.time < b.time; });
                            markDirty(capturedTabIdT);
                            refreshTimeline(capturedTabIdT);
                        }
                    };
                }
                kfRow.children.push_back(std::move(timeEntry));

                // Value label (prefix)
                WidgetElement valPre{};
                valPre.id = "WE.TL.KFVpre." + std::to_string(ti) + "." + std::to_string(ki);
                valPre.type = WidgetElementType::Text;
                valPre.text = "v=";
                valPre.font = EditorTheme::Get().fontDefault;
                valPre.fontSize = EditorTheme::Get().fontSizeCaption;
                valPre.minSize = Vec2{ 14.0f, 18.0f };
                valPre.style.textColor = EditorTheme::Get().textMuted;
                valPre.textAlignH = TextAlignH::Right;
                valPre.textAlignV = TextAlignV::Center;
                valPre.runtimeOnly = true;
                kfRow.children.push_back(std::move(valPre));

                // Value entry bar (editable)
                std::string valStr = std::to_string(kf.value.x);
                if (valStr.size() > 6) valStr = valStr.substr(0, 6);
                WidgetElement valEntry{};
                valEntry.id = "WE.TL.KFV." + std::to_string(ti) + "." + std::to_string(ki);
                valEntry.type = WidgetElementType::EntryBar;
                valEntry.value = valStr;
                valEntry.font = EditorTheme::Get().fontDefault;
                valEntry.fontSize = EditorTheme::Get().fontSizeCaption;
                valEntry.minSize = Vec2{ 50.0f, 18.0f };
                valEntry.padding = Vec2{ 2.0f, 1.0f };
                valEntry.style.color = EditorTheme::Get().inputBackground;
                valEntry.style.textColor = EditorTheme::Get().inputText;
                valEntry.hitTestMode = HitTestMode::Enabled;
                valEntry.runtimeOnly = true;
                {
                    const std::string capturedTabIdV = tabId;
                    const size_t trackIdxV = ti;
                    const size_t kfIdxV = ki;
                    valEntry.onValueChanged = [this, capturedTabIdV, trackIdxV, kfIdxV](const std::string& newVal)
                    {
                        auto it = m_states.find(capturedTabIdV);
                        if (it == m_states.end() || !it->second.editedWidget)
                            return;
                        auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                        if (a && trackIdxV < a->tracks.size() && kfIdxV < a->tracks[trackIdxV].keyframes.size())
                        {
                            try { a->tracks[trackIdxV].keyframes[kfIdxV].value.x = std::stof(newVal); }
                            catch (...) {}
                            markDirty(capturedTabIdV);
                            refreshTimeline(capturedTabIdV);
                        }
                    };
                }
                kfRow.children.push_back(std::move(valEntry));

                // Delete keyframe button
                WidgetElement delBtn{};
                delBtn.id = "WE.TL.KFDel." + std::to_string(ti) + "." + std::to_string(ki);
                delBtn.type = WidgetElementType::Button;
                delBtn.text = "\xc3\x97"; // 
                delBtn.font = EditorTheme::Get().fontDefault;
                delBtn.fontSize = EditorTheme::Get().fontSizeCaption;
                delBtn.minSize = Vec2{ 18.0f, 18.0f };
                delBtn.style.color = EditorTheme::Get().transparent;
                delBtn.style.hoverColor = EditorTheme::Get().buttonDangerHover;
                delBtn.style.textColor = EditorTheme::Get().buttonDanger;
                delBtn.textAlignH = TextAlignH::Center;
                delBtn.textAlignV = TextAlignV::Center;
                delBtn.hitTestMode = HitTestMode::Enabled;
                delBtn.runtimeOnly = true;
                {
                    const std::string capturedTabIdD = tabId;
                    const size_t trackIdxD = ti;
                    const size_t kfIdxD = ki;
                    delBtn.onClicked = [this, capturedTabIdD, trackIdxD, kfIdxD]()
                    {
                        auto it = m_states.find(capturedTabIdD);
                        if (it == m_states.end() || !it->second.editedWidget)
                            return;
                        auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                        if (a && trackIdxD < a->tracks.size() && kfIdxD < a->tracks[trackIdxD].keyframes.size())
                        {
                            a->tracks[trackIdxD].keyframes.erase(a->tracks[trackIdxD].keyframes.begin() + static_cast<ptrdiff_t>(kfIdxD));
                            markDirty(capturedTabIdD);
                            refreshTimeline(capturedTabIdD);
                        }
                    };
                }
                kfRow.children.push_back(std::move(delBtn));

                container.children.push_back(std::move(kfRow));
                ++rowIndex;
            }

            // + Keyframe button
            {
                WidgetElement addKfBtn{};
                addKfBtn.id = "WE.TL.AddKF." + std::to_string(ti);
                addKfBtn.type = WidgetElementType::Button;
                addKfBtn.text = "  + Keyframe";
                addKfBtn.font = EditorTheme::Get().fontDefault;
                addKfBtn.fontSize = EditorTheme::Get().fontSizeCaption;
                addKfBtn.fillX = true;
                addKfBtn.minSize = Vec2{ 0.0f, 18.0f };
                addKfBtn.padding = Vec2{ 20.0f, 0.0f };
                addKfBtn.style.color = EditorTheme::Get().transparent;
                addKfBtn.style.hoverColor = EditorTheme::Get().buttonSubtleHover;
                addKfBtn.style.textColor = EditorTheme::Get().accentGreen;
                addKfBtn.textAlignH = TextAlignH::Left;
                addKfBtn.textAlignV = TextAlignV::Center;
                addKfBtn.hitTestMode = HitTestMode::Enabled;
                addKfBtn.runtimeOnly = true;
                const std::string capturedTabId3 = tabId;
                const size_t trackIdx2 = ti;
                addKfBtn.onClicked = [this, capturedTabId3, trackIdx2]()
                {
                    auto it = m_states.find(capturedTabId3);
                    if (it == m_states.end() || !it->second.editedWidget)
                        return;
                    auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                    if (a && trackIdx2 < a->tracks.size())
                    {
                        AnimationKeyframe kf;
                        kf.time = it->second.timelineScrubTime;
                        kf.value = Vec4{ 1.0f, 0.0f, 0.0f, 0.0f };
                        a->tracks[trackIdx2].keyframes.push_back(std::move(kf));
                        std::sort(a->tracks[trackIdx2].keyframes.begin(), a->tracks[trackIdx2].keyframes.end(),
                            [](const AnimationKeyframe& a2, const AnimationKeyframe& b) { return a2.time < b.time; });
                        markDirty(capturedTabId3);
                        refreshTimeline(capturedTabId3);
                    }
                };
                container.children.push_back(std::move(addKfBtn));
                ++rowIndex;
            }
        }
    }
}

void WidgetEditorTab::buildTimelineRulerAndKeyframes(const std::string& tabId, WidgetElement& container)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end() || !stateIt->second.editedWidget)
        return;

    auto& edState = stateIt->second;
    const auto* anim = edState.editedWidget->findAnimationByName(edState.selectedAnimationName);
    if (!anim)
        return;

    const float duration = std::max(0.1f, anim->duration);

    // Count total rows to compute normalized Y positions for the flat layout.
    // All elements are direct children of the container Panel using from/to.
    int totalRows = 0;
    for (size_t ti = 0; ti < anim->tracks.size(); ++ti)
    {
        const auto& track = anim->tracks[ti];
        ++totalRows; // track header
        if (edState.expandedTimelineElements.count(track.targetElementId) > 0)
        {
            totalRows += static_cast<int>(track.keyframes.size()); // keyframe detail rows
            ++totalRows; // "+ Keyframe" row
        }
    }
    if (totalRows == 0)
        totalRows = 1;

    const float rulerFrac = 0.08f; // top 8% for ruler ticks
    const float lanesFrac = 1.0f - rulerFrac;
    const float laneFrac = lanesFrac / static_cast<float>(totalRows);

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Ruler background ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    {
        WidgetElement rulerBg{};
        rulerBg.id = "WE.TL.RulerBg";
        rulerBg.type = WidgetElementType::Panel;
        rulerBg.from = Vec2{ 0.0f, 0.0f };
        rulerBg.to = Vec2{ 1.0f, rulerFrac };
        rulerBg.style.color = Vec4{ 0.06f, 0.07f, 0.10f, 1.0f };
        rulerBg.hitTestMode = HitTestMode::DisabledAll;
        rulerBg.runtimeOnly = true;
        container.children.push_back(std::move(rulerBg));
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Ruler tick marks ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    {
        float step = 0.25f;
        if (duration > 5.0f) step = 1.0f;
        else if (duration > 2.0f) step = 0.5f;

        for (float t = 0.0f; t <= duration + 0.001f; t += step)
        {
            const float frac = std::clamp(t / duration, 0.0f, 1.0f);
            const float halfW = 0.03f;

            WidgetElement tick{};
            tick.id = "WE.TL.Tick." + std::to_string(static_cast<int>(t * 100));
            tick.type = WidgetElementType::Text;
            std::string tickStr = std::to_string(t);
            if (tickStr.size() > 4) tickStr = tickStr.substr(0, 4);
            tick.text = tickStr;
            tick.font = EditorTheme::Get().fontDefault;
            tick.fontSize = EditorTheme::Scaled(9.0f);
            tick.style.textColor = EditorTheme::Get().textMuted;
            tick.from = Vec2{ std::max(0.0f, frac - halfW), 0.0f };
            tick.to = Vec2{ std::min(1.0f, frac + halfW), rulerFrac };
            tick.textAlignH = TextAlignH::Center;
            tick.textAlignV = TextAlignV::Center;
            tick.hitTestMode = HitTestMode::DisabledAll;
            tick.runtimeOnly = true;
            container.children.push_back(std::move(tick));
        }
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Lane backgrounds + keyframe markers ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    int rowIndex = 0;
    for (size_t ti = 0; ti < anim->tracks.size(); ++ti)
    {
        const auto& track = anim->tracks[ti];
        const bool isExpanded = edState.expandedTimelineElements.count(track.targetElementId) > 0;

        // Track header lane background
        {
            const float laneTop = rulerFrac + static_cast<float>(rowIndex) * laneFrac;
            const float laneBot = laneTop + laneFrac;
            const bool evenRow = (rowIndex % 2 == 0);

            WidgetElement laneBg{};
            laneBg.id = "WE.TL.Lane." + std::to_string(ti);
            laneBg.type = WidgetElementType::Panel;
            laneBg.from = Vec2{ 0.0f, laneTop };
            laneBg.to = Vec2{ 1.0f, laneBot };
            laneBg.style.color = evenRow
                ? Vec4{ 0.10f, 0.11f, 0.14f, 0.5f }
                : Vec4{ 0.08f, 0.09f, 0.12f, 0.5f };
            laneBg.hitTestMode = HitTestMode::Enabled;
            laneBg.runtimeOnly = true;
            container.children.push_back(std::move(laneBg));

            // Keyframe diamond markers (colored Panel blocks)
            for (size_t ki = 0; ki < track.keyframes.size(); ++ki)
            {
                const auto& kf = track.keyframes[ki];
                const float timeFrac = std::clamp(kf.time / duration, 0.0f, 1.0f);
                const float halfDia = 0.006f;
                const float diaTop = laneTop + laneFrac * 0.15f;
                const float diaBot = laneBot - laneFrac * 0.15f;

                WidgetElement diamond{};
                diamond.id = "WE.TL.Dia." + std::to_string(ti) + "." + std::to_string(ki);
                diamond.type = WidgetElementType::Panel;
                diamond.from = Vec2{ std::max(0.0f, timeFrac - halfDia), diaTop };
                diamond.to = Vec2{ std::min(1.0f, timeFrac + halfDia), diaBot };
                diamond.style.color = Vec4{ 0.95f, 0.75f, 0.15f, 1.0f };
                diamond.style.hoverColor = Vec4{ 1.0f, 0.90f, 0.40f, 1.0f };
                diamond.hitTestMode = HitTestMode::Enabled;
                diamond.runtimeOnly = true;
                container.children.push_back(std::move(diamond));
            }

            ++rowIndex;
        }

        // Expanded: spacer lanes for detail rows + "Add Keyframe"
        if (isExpanded)
        {
            for (size_t ki = 0; ki < track.keyframes.size(); ++ki)
            {
                const float laneTop = rulerFrac + static_cast<float>(rowIndex) * laneFrac;
                const float laneBot = laneTop + laneFrac;

                WidgetElement spacer{};
                spacer.id = "WE.TL.LaneKF." + std::to_string(ti) + "." + std::to_string(ki);
                spacer.type = WidgetElementType::Panel;
                spacer.from = Vec2{ 0.0f, laneTop };
                spacer.to = Vec2{ 1.0f, laneBot };
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.hitTestMode = HitTestMode::DisabledAll;
                spacer.runtimeOnly = true;
                container.children.push_back(std::move(spacer));

                // Show a small diamond marker at the keyframe time on this detail row too
                const auto& kf = anim->tracks[ti].keyframes[ki];
                const float timeFrac = std::clamp(kf.time / duration, 0.0f, 1.0f);
                const float halfDia = 0.004f;

                WidgetElement kfMark{};
                kfMark.id = "WE.TL.DiaKF." + std::to_string(ti) + "." + std::to_string(ki);
                kfMark.type = WidgetElementType::Panel;
                kfMark.from = Vec2{ std::max(0.0f, timeFrac - halfDia), laneTop + laneFrac * 0.25f };
                kfMark.to = Vec2{ std::min(1.0f, timeFrac + halfDia), laneBot - laneFrac * 0.25f };
                kfMark.style.color = Vec4{ 0.7f, 0.55f, 0.1f, 0.8f };
                kfMark.hitTestMode = HitTestMode::DisabledAll;
                kfMark.runtimeOnly = true;
                container.children.push_back(std::move(kfMark));

                ++rowIndex;
            }

            // + Keyframe row spacer
            {
                const float laneTop = rulerFrac + static_cast<float>(rowIndex) * laneFrac;
                const float laneBot = laneTop + laneFrac;

                WidgetElement spacer{};
                spacer.id = "WE.TL.LaneAddKF." + std::to_string(ti);
                spacer.type = WidgetElementType::Panel;
                spacer.from = Vec2{ 0.0f, laneTop };
                spacer.to = Vec2{ 1.0f, laneBot };
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.hitTestMode = HitTestMode::DisabledAll;
                spacer.runtimeOnly = true;
                container.children.push_back(std::move(spacer));
                ++rowIndex;
            }
        }
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Scrubber indicator (orange line) ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    {
        const float scrubFrac = (duration > 0.0f) ? std::clamp(edState.timelineScrubTime / duration, 0.0f, 1.0f) : 0.0f;

        WidgetElement scrubber{};
        scrubber.id = "WE.TL.Scrubber";
        scrubber.type = WidgetElementType::Panel;
        scrubber.from = Vec2{ scrubFrac, 0.0f };
        scrubber.to = Vec2{ scrubFrac + 0.003f, 1.0f };
        scrubber.style.color = Vec4{ 1.0f, 0.6f, 0.1f, 0.9f };
        scrubber.hitTestMode = HitTestMode::DisabledAll;
        scrubber.runtimeOnly = true;
        container.children.push_back(std::move(scrubber));
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ End-of-animation line (red) ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    {
        WidgetElement endLine{};
        endLine.id = "WE.TL.EndLine";
        endLine.type = WidgetElementType::Panel;
        endLine.from = Vec2{ 1.0f - 0.003f, 0.0f };
        endLine.to = Vec2{ 1.0f, 1.0f };
        endLine.style.color = Vec4{ 0.9f, 0.2f, 0.2f, 0.8f };
        endLine.hitTestMode = HitTestMode::DisabledAll;
        endLine.runtimeOnly = true;
        container.children.push_back(std::move(endLine));
    }
}

void WidgetEditorTab::handleTimelineMouseDown(const std::string& tabId, const Vec2& localPos, float trackAreaWidth)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end() || !stateIt->second.editedWidget)
        return;

    auto& edState = stateIt->second;
    const auto* anim = edState.editedWidget->findAnimationByName(edState.selectedAnimationName);
    if (!anim || trackAreaWidth <= 0.0f)
        return;

    float duration = std::max(0.1f, anim->duration);
    float timeFrac = std::clamp(localPos.x / trackAreaWidth, 0.0f, 1.0f);
    edState.timelineScrubTime = timeFrac * duration;
    edState.isDraggingScrubber = true;

    // Apply the animation at the scrub time for preview
    edState.editedWidget->applyAnimationAtTime(edState.selectedAnimationName, edState.timelineScrubTime);
    edState.previewDirty = true;
    refreshTimeline(tabId);
}

void WidgetEditorTab::handleTimelineMouseMove(const std::string& tabId, const Vec2& localPos, float trackAreaWidth)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end() || !stateIt->second.editedWidget)
        return;

    auto& edState = stateIt->second;
    if (!edState.isDraggingScrubber)
        return;

    const auto* anim = edState.editedWidget->findAnimationByName(edState.selectedAnimationName);
    if (!anim || trackAreaWidth <= 0.0f)
        return;

    float duration = std::max(0.1f, anim->duration);
    float timeFrac = std::clamp(localPos.x / trackAreaWidth, 0.0f, 1.0f);
    edState.timelineScrubTime = timeFrac * duration;

    edState.editedWidget->applyAnimationAtTime(edState.selectedAnimationName, edState.timelineScrubTime);
    edState.previewDirty = true;
    refreshTimeline(tabId);
}

void WidgetEditorTab::handleTimelineMouseUp(const std::string& tabId)
{
    auto stateIt = m_states.find(tabId);
    if (stateIt == m_states.end())
        return;

    stateIt->second.isDraggingScrubber = false;
    stateIt->second.isDraggingEndLine = false;
    stateIt->second.draggingKeyframeTrack = -1;
    stateIt->second.draggingKeyframeIndex = -1;
}