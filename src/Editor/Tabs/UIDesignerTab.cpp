#include "UIDesignerTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Renderer/ViewportUIManager.h"
#include "../../Renderer/WidgetDetailSchema.h"

#include <string>
#include <vector>
#include <functional>

// File-local helper matching the one in UIManager.cpp
namespace
{
    WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
    {
        for (auto& element : elements)
        {
            if (element.id == id)
                return &element;
            for (auto& child : element.children)
            {
                std::function<WidgetElement*(WidgetElement&)> search =
                    [&](WidgetElement& el) -> WidgetElement*
                {
                    if (el.id == id) return &el;
                    for (auto& c : el.children)
                        if (auto* m = search(c)) return m;
                    return nullptr;
                };
                if (auto* m = search(child)) return m;
            }
        }
        return nullptr;
    }
}

UIDesignerTab::UIDesignerTab(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer) {}

ViewportUIManager* UIDesignerTab::getViewportUIManager() const
{
    return m_renderer ? m_renderer->getViewportUIManagerPtr() : nullptr;
}

void UIDesignerTab::open()
{
    if (!m_renderer)
        return;

    const std::string tabId = "UIDesigner";

    // If already open, just switch to it
    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "UI Designer", true);
    m_renderer->setActiveTab(tabId);

    const std::string leftWidgetId    = "UIDesigner.Left";
    const std::string rightWidgetId   = "UIDesigner.Right";
    const std::string toolbarWidgetId = "UIDesigner.Toolbar";

    // Clean up any stale registrations
    m_uiManager->unregisterWidget(leftWidgetId);
    m_uiManager->unregisterWidget(rightWidgetId);
    m_uiManager->unregisterWidget(toolbarWidgetId);

    // Store state
    m_state = {};
    m_state.tabId           = tabId;
    m_state.leftWidgetId    = leftWidgetId;
    m_state.rightWidgetId   = rightWidgetId;
    m_state.toolbarWidgetId = toolbarWidgetId;
    m_state.isOpen          = true;

    // --- Top toolbar: widget selector + new / delete buttons ---
    {
        auto toolbarWidget = std::make_shared<EditorWidget>();
        toolbarWidget->setName(toolbarWidgetId);
        toolbarWidget->setAnchor(WidgetAnchor::TopLeft);
        toolbarWidget->setFillX(true);
        toolbarWidget->setSizePixels(Vec2{ 0.0f, 32.0f });
        toolbarWidget->setZOrder(3);

        WidgetElement root{};
        root.id          = "UIDesigner.Toolbar.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Horizontal;
        root.padding     = Vec2{ 8.0f, 4.0f };
        root.style.color       = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
        root.runtimeOnly = true;

        // "New Widget" button
        {
            WidgetElement btn{};
            btn.id            = "UIDesigner.Toolbar.NewWidget";
            btn.type          = WidgetElementType::Button;
            btn.text          = "+ Widget";
            btn.font          = EditorTheme::Get().fontDefault;
            btn.fontSize      = EditorTheme::Get().fontSizeBody;
            btn.style.textColor     = EditorTheme::Get().textPrimary;
            btn.style.color         = EditorTheme::Get().buttonDefault;
            btn.style.hoverColor    = EditorTheme::Get().buttonHover;
            btn.textAlignH    = TextAlignH::Center;
            btn.textAlignV    = TextAlignV::Center;
            btn.minSize       = Vec2{ 80.0f, 24.0f };
            btn.padding       = Vec2{ 10.0f, 2.0f };
            btn.hitTestMode = HitTestMode::Enabled;
            btn.runtimeOnly   = true;
            btn.clickEvent    = "UIDesigner.Toolbar.NewWidget";
            root.children.push_back(std::move(btn));
        }

        // "Delete Widget" button
        {
            WidgetElement btn{};
            btn.id            = "UIDesigner.Toolbar.DeleteWidget";
            btn.type          = WidgetElementType::Button;
            btn.text          = "- Widget";
            btn.font          = EditorTheme::Get().fontDefault;
            btn.fontSize      = EditorTheme::Get().fontSizeBody;
            btn.style.textColor     = EditorTheme::Get().textPrimary;
            btn.style.color         = EditorTheme::Get().buttonDefault;
            btn.style.hoverColor    = EditorTheme::Get().buttonDangerHover;
            btn.textAlignH    = TextAlignH::Center;
            btn.textAlignV    = TextAlignV::Center;
            btn.minSize       = Vec2{ 80.0f, 24.0f };
            btn.padding       = Vec2{ 10.0f, 2.0f };
            btn.hitTestMode = HitTestMode::Enabled;
            btn.runtimeOnly   = true;
            btn.clickEvent    = "UIDesigner.Toolbar.DeleteWidget";
            root.children.push_back(std::move(btn));
        }

        // Spacer
        {
            WidgetElement spacer{};
            spacer.type        = WidgetElementType::Panel;
            spacer.fillX       = true;
            spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            spacer.style.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            spacer.runtimeOnly = true;
            root.children.push_back(std::move(spacer));
        }

        // Status label
        {
            WidgetElement lbl{};
            lbl.id          = "UIDesigner.Toolbar.Status";
            lbl.type        = WidgetElementType::Text;
            lbl.text        = "";
            lbl.font        = EditorTheme::Get().fontDefault;
            lbl.fontSize    = EditorTheme::Get().fontSizeSmall;
            lbl.style.textColor   = EditorTheme::Get().textMuted;
            lbl.textAlignH  = TextAlignH::Right;
            lbl.textAlignV  = TextAlignV::Center;
            lbl.minSize     = Vec2{ 0.0f, 24.0f };
            lbl.padding     = Vec2{ 8.0f, 0.0f };
            lbl.runtimeOnly = true;
            root.children.push_back(std::move(lbl));
        }

        toolbarWidget->setElements({ std::move(root) });
        m_uiManager->registerWidget(toolbarWidgetId, toolbarWidget, tabId);

        m_uiManager->registerClickEvent("UIDesigner.Toolbar.NewWidget", [this]()
        {
            auto* vpUI = getViewportUIManager();
            if (!vpUI) return;
            static int s_newWidgetCounter = 0;
            std::string name = "Widget_" + std::to_string(++s_newWidgetCounter);
            vpUI->createWidget(name, s_newWidgetCounter * 10);
            m_state.selectedWidgetName = name;
            m_state.selectedElementId.clear();
            refreshHierarchy();
            refreshDetails();
        });

        m_uiManager->registerClickEvent("UIDesigner.Toolbar.DeleteWidget", [this]()
        {
            auto* vpUI = getViewportUIManager();
            if (!vpUI || m_state.selectedWidgetName.empty()) return;
            vpUI->removeWidget(m_state.selectedWidgetName);
            m_state.selectedWidgetName.clear();
            m_state.selectedElementId.clear();
            refreshHierarchy();
            refreshDetails();
        });
    }

    // --- Left panel: control palette + hierarchy ---
    {
        auto leftWidget = std::make_shared<EditorWidget>();
        leftWidget->setName(leftWidgetId);
        leftWidget->setAnchor(WidgetAnchor::TopLeft);
        leftWidget->setFillY(true);
        leftWidget->setSizePixels(Vec2{ 250.0f, 0.0f });
        leftWidget->setZOrder(2);

        WidgetElement root{};
        root.id          = "UIDesigner.Left.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color       = Vec4{ 0.12f, 0.13f, 0.17f, 0.96f };
        root.runtimeOnly = true;

        // --- Controls section (scrollable) ---
        {
            WidgetElement controlsSection{};
            controlsSection.id          = "UIDesigner.Left.ControlsSection";
            controlsSection.type        = WidgetElementType::StackPanel;
            controlsSection.fillX       = true;
            controlsSection.fillY       = true;
            controlsSection.scrollable  = true;
            controlsSection.orientation = StackOrientation::Vertical;
            controlsSection.padding     = Vec2{ 10.0f, 8.0f };
            controlsSection.style.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            controlsSection.runtimeOnly = true;

            // Title: Controls
            {
                WidgetElement title{};
                title.id         = "UIDesigner.Left.Title";
                title.type       = WidgetElementType::Text;
                title.text       = "Controls";
                title.font       = EditorTheme::Get().fontDefault;
                title.fontSize   = EditorTheme::Get().fontSizeHeading;
                title.style.textColor  = EditorTheme::Get().textPrimary;
                title.textAlignH = TextAlignH::Left;
                title.textAlignV = TextAlignV::Center;
                title.fillX      = true;
                title.minSize    = Vec2{ 0.0f, 28.0f };
                title.runtimeOnly = true;
                controlsSection.children.push_back(std::move(title));
            }

            // Gameplay-UI element types
            const std::vector<std::string> controls = {
                "Panel", "Text", "Label", "Button", "Image", "ProgressBar", "Slider",
                "WrapBox", "UniformGrid", "SizeBox", "ScaleBox", "WidgetSwitcher", "Overlay",
                "Border", "Spinner", "RichText", "ListView", "TileView"
            };
            for (size_t i = 0; i < controls.size(); ++i)
            {
                WidgetElement item{};
                item.id            = "UIDesigner.Left.Control." + std::to_string(i);
                item.type          = WidgetElementType::Button;
                item.text          = "  " + controls[i];
                item.font          = EditorTheme::Get().fontDefault;
                item.fontSize      = EditorTheme::Get().fontSizeSubheading;
                item.style.textColor     = EditorTheme::Get().textSecondary;
                item.style.color         = EditorTheme::Get().transparent;
                item.style.hoverColor    = EditorTheme::Get().buttonSubtleHover;
                item.textAlignH    = TextAlignH::Left;
                item.textAlignV    = TextAlignV::Center;
                item.fillX         = true;
                item.minSize       = Vec2{ 0.0f, 24.0f };
                item.hitTestMode = HitTestMode::Enabled;
                item.runtimeOnly   = true;
                item.clickEvent    = "UIDesigner.Left.Control." + std::to_string(i);
                controlsSection.children.push_back(std::move(item));

                const std::string controlType = controls[i];
                m_uiManager->registerClickEvent("UIDesigner.Left.Control." + std::to_string(i), [this, controlType]()
                {
                    addElementToViewportWidget(controlType);
                });
            }

            root.children.push_back(std::move(controlsSection));
        }

        // Separator between sections
        {
            WidgetElement sep{};
            sep.id         = "UIDesigner.Left.Sep";
            sep.type       = WidgetElementType::Panel;
            sep.fillX      = true;
            sep.minSize    = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color      = EditorTheme::Get().panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // --- Hierarchy section (scrollable) ---
        {
            WidgetElement hierarchySection{};
            hierarchySection.id          = "UIDesigner.Left.HierarchySection";
            hierarchySection.type        = WidgetElementType::StackPanel;
            hierarchySection.fillX       = true;
            hierarchySection.fillY       = true;
            hierarchySection.scrollable  = true;
            hierarchySection.orientation = StackOrientation::Vertical;
            hierarchySection.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            hierarchySection.style.color       = Vec4{ 0.08f, 0.09f, 0.12f, 0.75f };
            hierarchySection.runtimeOnly = true;

            // Title: Hierarchy
            {
                WidgetElement treeTitle{};
                treeTitle.id         = "UIDesigner.Left.TreeTitle";
                treeTitle.type       = WidgetElementType::Text;
                treeTitle.text       = "Hierarchy";
                treeTitle.font       = EditorTheme::Get().fontDefault;
                treeTitle.fontSize   = EditorTheme::Get().fontSizeHeading;
                treeTitle.style.textColor  = EditorTheme::Get().textPrimary;
                treeTitle.textAlignH = TextAlignH::Left;
                treeTitle.textAlignV = TextAlignV::Center;
                treeTitle.fillX      = true;
                treeTitle.minSize    = Vec2{ 0.0f, 28.0f };
                treeTitle.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(treeTitle));
            }

            // Tree container
            {
                WidgetElement hierarchyStack{};
                hierarchyStack.id          = "UIDesigner.Left.Tree";
                hierarchyStack.type        = WidgetElementType::StackPanel;
                hierarchyStack.fillX       = true;
                hierarchyStack.orientation = StackOrientation::Vertical;
                hierarchyStack.padding     = Vec2{ 2.0f, 2.0f };
                hierarchyStack.style.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                hierarchyStack.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(hierarchyStack));
            }

            root.children.push_back(std::move(hierarchySection));
        }

        leftWidget->setElements({ std::move(root) });
        m_uiManager->registerWidget(leftWidgetId, leftWidget, tabId);
    }

    // --- Right panel: element details ---
    {
        auto rightWidget = std::make_shared<EditorWidget>();
        rightWidget->setName(rightWidgetId);
        rightWidget->setAnchor(WidgetAnchor::TopRight);
        rightWidget->setFillY(true);
        rightWidget->setSizePixels(Vec2{ 280.0f, 0.0f });
        rightWidget->setZOrder(2);

        WidgetElement root{};
        root.id          = "UIDesigner.Right.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.padding     = Vec2{ 10.0f, 8.0f };
        root.style.color       = Vec4{ 0.12f, 0.13f, 0.17f, 0.96f };
        root.scrollable  = true;
        root.runtimeOnly = true;

        {
            WidgetElement title{};
            title.id         = "UIDesigner.Right.Title";
            title.type       = WidgetElementType::Text;
            title.text       = "Properties";
            title.font       = EditorTheme::Get().fontDefault;
            title.fontSize   = EditorTheme::Get().fontSizeHeading;
            title.style.textColor  = EditorTheme::Get().textPrimary;
            title.textAlignH = TextAlignH::Left;
            title.textAlignV = TextAlignV::Center;
            title.fillX      = true;
            title.minSize    = Vec2{ 0.0f, 28.0f };
            title.runtimeOnly = true;
            root.children.push_back(std::move(title));
        }

        // Placeholder hint
        {
            WidgetElement hint{};
            hint.id         = "UIDesigner.Right.Hint";
            hint.type       = WidgetElementType::Text;
            hint.text       = "Select an element in the hierarchy\nto see its properties.";
            hint.font       = EditorTheme::Get().fontDefault;
            hint.fontSize   = EditorTheme::Get().fontSizeBody;
            hint.style.textColor  = EditorTheme::Get().textMuted;
            hint.textAlignH = TextAlignH::Left;
            hint.textAlignV = TextAlignV::Center;
            hint.fillX      = true;
            hint.minSize    = Vec2{ 0.0f, 36.0f };
            hint.runtimeOnly = true;
            root.children.push_back(std::move(hint));
        }

        rightWidget->setElements({ std::move(root) });
        m_uiManager->registerWidget(rightWidgetId, rightWidget, tabId);
    }

    // Tab and close button events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_uiManager->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshHierarchy();
    });

    m_uiManager->registerClickEvent(closeBtnId, [this]()
    {
        close();
    });

    // --- Bidirectional sync: viewport click -> designer selection ---
    auto* vpUI = getViewportUIManager();
    if (vpUI)
    {
        vpUI->setOnSelectionChanged([this](const std::string& elementId)
        {
            if (!m_state.isOpen) return;
            if (m_renderer && m_renderer->getActiveTabId() != m_state.tabId) return;

            auto* vp = getViewportUIManager();
            if (!vp) return;
            std::string ownerWidget;
            for (const auto& we : vp->getSortedWidgets())
            {
                if (we.widget)
                {
                    auto& elems = we.widget->getElementsMutable();
                    if (!elems.empty())
                    {
                        for (const auto& child : elems[0].children)
                        {
                            if (child.id == elementId)
                            {
                                ownerWidget = we.name;
                                break;
                            }
                        }
                        if (!ownerWidget.empty()) break;
                    }
                }
            }

            if (!ownerWidget.empty())
            {
                m_state.selectedWidgetName = ownerWidget;
                m_state.selectedElementId = elementId;
                refreshHierarchy();
                refreshDetails();
            }
        });
    }

    // Initial population
    refreshHierarchy();
    refreshDetails();
}

void UIDesignerTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    const std::string tabId = m_state.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_uiManager->unregisterWidget(m_state.leftWidgetId);
    m_uiManager->unregisterWidget(m_state.rightWidgetId);
    m_uiManager->unregisterWidget(m_state.toolbarWidgetId);

    // Clear the selection callback
    auto* vpUI = getViewportUIManager();
    if (vpUI)
        vpUI->setOnSelectionChanged(nullptr);

    m_renderer->removeTab(tabId);
    m_state = {};
    m_uiManager->markAllWidgetsDirty();
}

void UIDesignerTab::selectElement(const std::string& widgetName, const std::string& elementId)
{
    m_state.selectedWidgetName = widgetName;
    m_state.selectedElementId  = elementId;

    auto* vpUI = getViewportUIManager();
    if (vpUI)
        vpUI->setSelectedElementId(elementId);

    refreshHierarchy();
    refreshDetails();
}

void UIDesignerTab::addElementToViewportWidget(const std::string& elementType)
{
    auto* vpUI = getViewportUIManager();
    if (!vpUI) return;

    if (m_state.selectedWidgetName.empty())
    {
        if (!vpUI->hasWidgets())
        {
            vpUI->createWidget("Default", 0);
            m_state.selectedWidgetName = "Default";
        }
        else
        {
            const auto& sorted = vpUI->getSortedWidgets();
            if (!sorted.empty())
                m_state.selectedWidgetName = sorted.front().name;
        }
    }

    Widget* w = vpUI->getWidget(m_state.selectedWidgetName);
    if (!w) return;

    auto& elements = w->getElementsMutable();
    if (elements.empty()) return;
    auto& canvas = elements[0];

    static int s_autoElementId = 0;
    std::string newId = elementType + "_" + std::to_string(++s_autoElementId);

    WidgetElement newEl{};
    newEl.id = newId;

    if (elementType == "Panel")
    {
        newEl.type  = WidgetElementType::Panel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 120.0f, 80.0f };
        newEl.style.color = Vec4{ 0.25f, 0.25f, 0.30f, 0.8f };
    }
    else if (elementType == "Text")
    {
        newEl.type      = WidgetElementType::Text;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 150.0f, 30.0f };
        newEl.text      = "Text";
        newEl.style.textColor = Vec4{ 1, 1, 1, 1 };
    }
    else if (elementType == "Label")
    {
        newEl.type      = WidgetElementType::Label;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 150.0f, 30.0f };
        newEl.text      = "Label";
        newEl.style.textColor = Vec4{ 1, 1, 1, 1 };
    }
    else if (elementType == "Button")
    {
        newEl.type        = WidgetElementType::Button;
        newEl.from        = Vec2{ 0.0f, 0.0f };
        newEl.to          = Vec2{ 120.0f, 36.0f };
        newEl.text        = "Button";
        newEl.style.color = Vec4{ 0.20f, 0.45f, 0.75f, 1.0f };
        newEl.style.hoverColor = Vec4{ 0.25f, 0.50f, 0.80f, 1.0f };
        newEl.hitTestMode = HitTestMode::Enabled;
    }
    else if (elementType == "Image")
    {
        newEl.type  = WidgetElementType::Image;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 100.0f, 100.0f };
        newEl.style.color = Vec4{ 1, 1, 1, 1 };
    }
    else if (elementType == "ProgressBar")
    {
        newEl.type        = WidgetElementType::ProgressBar;
        newEl.from        = Vec2{ 0.0f, 0.0f };
        newEl.to          = Vec2{ 200.0f, 20.0f };
        newEl.valueFloat  = 0.5f;
        newEl.style.color = Vec4{ 0.2f, 0.6f, 0.3f, 1.0f };
    }
    else if (elementType == "Slider")
    {
        newEl.type        = WidgetElementType::Slider;
        newEl.from        = Vec2{ 0.0f, 0.0f };
        newEl.to          = Vec2{ 200.0f, 20.0f };
        newEl.valueFloat  = 0.5f;
        newEl.style.color = Vec4{ 0.3f, 0.5f, 0.8f, 1.0f };
    }
    else if (elementType == "WrapBox")
    {
        newEl.type  = WidgetElementType::StackPanel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
        newEl.orientation = StackOrientation::Horizontal;
        newEl.style.color = Vec4{ 0.15f, 0.15f, 0.20f, 0.6f };
    }
    else if (elementType == "UniformGrid")
    {
        newEl.type  = WidgetElementType::Grid;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
        newEl.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.6f };
    }
    else if (elementType == "SizeBox")
    {
        newEl.type  = WidgetElementType::Panel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 200.0f, 200.0f };
        newEl.style.color = Vec4{ 0.10f, 0.10f, 0.14f, 0.4f };
    }
    else if (elementType == "ScaleBox")
    {
        newEl.type  = WidgetElementType::Panel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 200.0f, 200.0f };
        newEl.style.color = Vec4{ 0.10f, 0.10f, 0.14f, 0.4f };
    }
    else if (elementType == "WidgetSwitcher")
    {
        newEl.type  = WidgetElementType::StackPanel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
        newEl.orientation = StackOrientation::Vertical;
        newEl.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.6f };
    }
    else if (elementType == "Overlay")
    {
        newEl.type  = WidgetElementType::Panel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
        newEl.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.3f };
    }
    else if (elementType == "Border")
    {
        newEl.type  = WidgetElementType::Panel;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 150.0f, 100.0f };
        newEl.style.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        newEl.style.borderColor = Vec4{ 1.0f, 1.0f, 1.0f, 0.5f };
        newEl.style.borderThickness = 2.0f;
    }
    else if (elementType == "Spinner")
    {
        newEl.type        = WidgetElementType::ProgressBar;
        newEl.from        = Vec2{ 0.0f, 0.0f };
        newEl.to          = Vec2{ 40.0f, 40.0f };
        newEl.valueFloat  = -1.0f;
        newEl.style.color = Vec4{ 0.3f, 0.5f, 0.8f, 1.0f };
    }
    else if (elementType == "RichText")
    {
        newEl.type      = WidgetElementType::Text;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 250.0f, 60.0f };
        newEl.text      = "Rich Text Block";
        newEl.style.textColor = Vec4{ 1, 1, 1, 1 };
    }
    else if (elementType == "ListView")
    {
        newEl.type  = WidgetElementType::ListView;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 300.0f };
        newEl.totalItemCount = 10;
        newEl.itemHeight = 32.0f;
        newEl.scrollable = true;
        newEl.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.6f };
    }
    else if (elementType == "TileView")
    {
        newEl.type  = WidgetElementType::TileView;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 400.0f, 300.0f };
        newEl.totalItemCount = 12;
        newEl.itemHeight = 80.0f;
        newEl.itemWidth = 100.0f;
        newEl.columnsPerRow = 4;
        newEl.scrollable = true;
        newEl.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.6f };
    }
    else
    {
        return;
    }

    newEl.anchor       = WidgetAnchor::TopLeft;
    newEl.anchorOffset = Vec2{ 20.0f, 20.0f };
    newEl.runtimeOnly  = true;

    canvas.children.push_back(std::move(newEl));
    w->markLayoutDirty();
    vpUI->markLayoutDirty();

    m_state.selectedElementId = newId;
    refreshHierarchy();
    refreshDetails();
}

void UIDesignerTab::deleteSelectedElement()
{
    auto* vpUI = getViewportUIManager();
    if (!vpUI || m_state.selectedElementId.empty())
        return;

    Widget* w = vpUI->getWidget(m_state.selectedWidgetName);
    if (!w) return;

    auto& elements = w->getElementsMutable();
    if (elements.empty()) return;
    auto& canvas = elements[0];

    const std::string& targetId = m_state.selectedElementId;
    const std::function<bool(std::vector<WidgetElement>&)> removeElement =
        [&](std::vector<WidgetElement>& elems) -> bool
    {
        for (auto it = elems.begin(); it != elems.end(); ++it)
        {
            if (it->id == targetId)
            {
                elems.erase(it);
                return true;
            }
            if (removeElement(it->children))
                return true;
        }
        return false;
    };

    if (removeElement(canvas.children))
    {
        m_state.selectedElementId.clear();
        vpUI->setSelectedElementId("");
        w->markLayoutDirty();
        vpUI->markLayoutDirty();
        refreshHierarchy();
        refreshDetails();
    }
}

void UIDesignerTab::refreshHierarchy()
{
    if (!m_state.isOpen) return;

    auto* leftEntry = m_uiManager->findWidgetEntry(m_state.leftWidgetId);
    if (!leftEntry || !leftEntry->widget) return;

    auto& leftElements = leftEntry->widget->getElementsMutable();
    WidgetElement* treePanel = FindElementById(leftElements, "UIDesigner.Left.Tree");
    if (!treePanel) return;

    treePanel->children.clear();
    m_uiManager->clearLastHoveredElement();

    auto* vpUI = getViewportUIManager();
    if (!vpUI) return;

    const auto& sortedWidgets = vpUI->getSortedWidgets();
    const std::string& selectedWidget  = m_state.selectedWidgetName;
    const std::string& selectedElement = m_state.selectedElementId;
    int lineIndex = 0;

    // Update toolbar status
    {
        auto* tbEntry = m_uiManager->findWidgetEntry(m_state.toolbarWidgetId);
        if (tbEntry && tbEntry->widget)
        {
            auto& tbElements = tbEntry->widget->getElementsMutable();
            WidgetElement* statusLabel = FindElementById(tbElements, "UIDesigner.Toolbar.Status");
            if (statusLabel)
            {
                int widgetCount = static_cast<int>(sortedWidgets.size());
                int elemCount = 0;
                for (const auto& we : sortedWidgets)
                {
                    if (we.widget)
                    {
                        const auto& elems = we.widget->getElements();
                        if (!elems.empty())
                            elemCount += static_cast<int>(elems[0].children.size());
                    }
                }
                statusLabel->text = std::to_string(widgetCount) + " Widget" +
                    (widgetCount != 1 ? "s" : "") + ", " +
                    std::to_string(elemCount) + " Element" +
                    (elemCount != 1 ? "s" : "");
            }
            tbEntry->widget->markLayoutDirty();
        }
    }

    const auto getTypeName = [](WidgetElementType t) -> std::string
    {
        switch (t)
        {
        case WidgetElementType::Panel:       return "Panel";
        case WidgetElementType::Text:        return "Text";
        case WidgetElementType::Button:      return "Button";
        case WidgetElementType::Image:       return "Image";
        case WidgetElementType::Label:       return "Label";
        case WidgetElementType::Slider:      return "Slider";
        case WidgetElementType::ProgressBar: return "ProgressBar";
        case WidgetElementType::EntryBar:    return "EntryBar";
        case WidgetElementType::StackPanel:  return "StackPanel";
        case WidgetElementType::CheckBox:    return "CheckBox";
        case WidgetElementType::DropDown:    return "DropDown";
        default:                             return "Element";
        }
    };

    for (const auto& widgetEntry : sortedWidgets)
    {
        const bool isWidgetSelected = (widgetEntry.name == selectedWidget);

        // Widget header row
        {
            WidgetElement row{};
            row.id   = "UIDesigner.HRow." + std::to_string(lineIndex);
            row.type = WidgetElementType::Button;
            row.text = (isWidgetSelected ? "v " : "> ") + widgetEntry.name;
            row.font     = EditorTheme::Get().fontDefault;
            row.fontSize = EditorTheme::Get().fontSizeBody;
            row.textAlignH    = TextAlignH::Left;
            row.textAlignV    = TextAlignV::Center;
            row.fillX         = true;
            row.minSize       = Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall };
            row.padding       = Vec2{ 4.0f, 1.0f };
            row.hitTestMode = HitTestMode::Enabled;
            row.runtimeOnly   = true;

            if (isWidgetSelected && selectedElement.empty())
            {
                row.style.color     = EditorTheme::Get().selectionHighlight;
                row.style.hoverColor = EditorTheme::Get().selectionHighlightHover;
                row.style.textColor = EditorTheme::Get().textPrimary;
            }
            else
            {
                row.style.color     = EditorTheme::Get().transparent;
                row.style.hoverColor = EditorTheme::Get().buttonSubtleHover;
                row.style.textColor = isWidgetSelected
                    ? EditorTheme::Get().textPrimary
                    : EditorTheme::Get().textSecondary;
            }

            const std::string capturedName = widgetEntry.name;
            row.onClicked = [this, capturedName]()
            {
                selectElement(capturedName, "");
            };

            treePanel->children.push_back(std::move(row));
            ++lineIndex;
        }

        // Children of the canvas panel (only shown if this widget is selected)
        if (isWidgetSelected && widgetEntry.widget)
        {
            const auto& elements = widgetEntry.widget->getElements();
            if (!elements.empty())
            {
                const auto& canvasChildren = elements[0].children;
                for (const auto& el : canvasChildren)
                {
                    const std::string elId = el.id;
                    const bool isElementSelected = (!elId.empty() && elId == selectedElement);

                    std::string label = "    [" + getTypeName(el.type) + "]";
                    if (!elId.empty())
                        label += " " + elId;

                    WidgetElement row{};
                    row.id   = "UIDesigner.HRow." + std::to_string(lineIndex);
                    row.type = WidgetElementType::Button;
                    row.text = label;
                    row.font     = EditorTheme::Get().fontDefault;
                    row.fontSize = EditorTheme::Get().fontSizeSmall;
                    row.textAlignH    = TextAlignH::Left;
                    row.textAlignV    = TextAlignV::Center;
                    row.fillX         = true;
                    row.minSize       = Vec2{ 0.0f, EditorTheme::Get().rowHeightSmall };
                    row.padding       = Vec2{ 4.0f, 1.0f };
                    row.hitTestMode = HitTestMode::Enabled;
                    row.runtimeOnly   = true;

                    if (isElementSelected)
                    {
                        row.style.color     = EditorTheme::Get().selectionHighlight;
                        row.style.hoverColor = EditorTheme::Get().selectionHighlightHover;
                        row.style.textColor = EditorTheme::Get().textPrimary;
                    }
                    else
                    {
                        row.style.color     = EditorTheme::Get().transparent;
                        row.style.hoverColor = EditorTheme::Get().buttonSubtleHover;
                        row.style.textColor = EditorTheme::Get().textSecondary;
                    }

                    const std::string capturedWidgetName = widgetEntry.name;
                    row.onClicked = [this, capturedWidgetName, elId]()
                    {
                        selectElement(capturedWidgetName, elId);
                    };

                    treePanel->children.push_back(std::move(row));
                    ++lineIndex;
                }
            }
        }
    }

    leftEntry->widget->markLayoutDirty();
    m_uiManager->markAllWidgetsDirty();
}

void UIDesignerTab::refreshDetails()
{
    if (!m_state.isOpen) return;

    auto* rightEntry = m_uiManager->findWidgetEntry(m_state.rightWidgetId);
    if (!rightEntry || !rightEntry->widget) return;

    auto& rightElements = rightEntry->widget->getElementsMutable();
    WidgetElement* rootPanel = FindElementById(rightElements, "UIDesigner.Right.Root");
    if (!rootPanel) return;

    // Keep only the title (first child)
    if (rootPanel->children.size() > 1)
        rootPanel->children.erase(rootPanel->children.begin() + 1, rootPanel->children.end());

    m_uiManager->clearLastHoveredElement();

    auto* vpUI = getViewportUIManager();
    if (!vpUI)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "No ViewportUIManager available."));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Widget-level properties if a widget is selected but no element
    if (!m_state.selectedWidgetName.empty() && m_state.selectedElementId.empty())
    {
        Widget* w = vpUI->getWidget(m_state.selectedWidgetName);
        if (!w)
        {
            rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
                "Widget not found."));
            rightEntry->widget->markLayoutDirty();
            return;
        }

        rootPanel->children.push_back(EditorUIBuilder::makeHeading("Widget"));
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Name: " + m_state.selectedWidgetName));

        int childCount = 0;
        const auto& elems = w->getElements();
        if (!elems.empty())
            childCount = static_cast<int>(elems[0].children.size());
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Elements: " + std::to_string(childCount)));

        rightEntry->widget->markLayoutDirty();
        return;
    }

    // No selection
    if (m_state.selectedElementId.empty())
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Select an element in the hierarchy\nto see its properties."));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Find the selected element
    WidgetElement* selected = vpUI->findElementById(
        m_state.selectedWidgetName, m_state.selectedElementId);
    if (!selected)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Element not found: " + m_state.selectedElementId));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    const auto applyChange = [this]() {
        auto* vp = getViewportUIManager();
        if (vp) vp->markLayoutDirty();
        m_uiManager->markAllWidgetsDirty();
    };

    WidgetDetailSchema::Options opts;
    opts.showEditableId    = true;
    opts.showDeleteButton  = true;
    opts.onIdRenamed = [this](const std::string& newId) {
        m_state.selectedElementId = newId;
    };
    opts.onRefreshHierarchy = [this]() {
        refreshHierarchy();
    };
    opts.onDelete = [this]() {
        deleteSelectedElement();
    };

    WidgetDetailSchema::buildDetailPanel("UID.Det", selected, applyChange, rootPanel, opts);

    rightEntry->widget->markLayoutDirty();
    m_uiManager->markAllWidgetsDirty();
}
