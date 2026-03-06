#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../Core/MathTypes.h"
#include "UIWidget.h"
#include "GameplayUI/GameplayWidget.h"
#include "ViewportUITheme.h"
#include "../AssetManager/json.hpp"

/// Manages gameplay / viewport UI widgets (GameplayWidget).
/// Fully customisable at runtime by game developers via Python scripting.
class ViewportUIManager
{
public:
    using SelectionChangedCallback = std::function<void(const std::string& elementId)>;

    ViewportUIManager();
    ~ViewportUIManager();

    /// Access the viewport UI theme (customisable by game developers at runtime).
    ViewportUITheme& getTheme() { return m_theme; }
    const ViewportUITheme& getTheme() const { return m_theme; }

    void setViewportRect(float x, float y, float width, float height);
    Vec4 getViewportRect() const;
    Vec2 getViewportSize() const;

    // --- Multi-widget management ---
    bool createWidget(const std::string& name, int zOrder = 0);
    bool removeWidget(const std::string& name);
    GameplayWidget* getWidget(const std::string& name);
    const GameplayWidget* getWidget(const std::string& name) const;
    void clearAllWidgets();
    bool hasWidgets() const;

    struct WidgetEntry
    {
        std::string name;
        std::shared_ptr<GameplayWidget> widget;
    };
    const std::vector<WidgetEntry>& getSortedWidgets() const;

    // --- Element access (searches all widgets) ---
    WidgetElement* findElementById(const std::string& elementId);
    WidgetElement* findElementById(const std::string& widgetName, const std::string& elementId);

    void updateLayout(const std::function<Vec2(const std::string&, float)>& measureText);
    void tickAnimations(float deltaTime);
    bool needsLayoutUpdate() const;
    void markLayoutDirty();

    bool handleMouseDown(const Vec2& windowPos, int button);
    bool handleMouseUp(const Vec2& windowPos, int button);
    bool handleScroll(const Vec2& windowPos, float delta);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key, int modifiers = 0);
    bool handleGamepadButton(int button, bool pressed);
    bool handleGamepadAxis(int axis, float value);
    void setMousePosition(const Vec2& windowPos);
    bool isPointerOverViewportUI(const Vec2& windowPos) const;

    bool isRenderDirty() const;
    void clearRenderDirty();

    void setSelectedElementId(const std::string& elementId);
    const std::string& getSelectedElementId() const;
    void setOnSelectionChanged(SelectionChangedCallback callback);

    // --- Focus management (Phase 5) ---
    void setFocus(const std::string& elementId);
    void clearFocus();
    const std::string& getFocusedElementId() const;
    void setFocusable(const std::string& elementId, bool focusable);

    // --- Runtime Drag & Drop (Phase 5) ---
    bool handleMouseMove(const Vec2& windowPos);
    bool isDragging() const;
    const DragDropOperation& getCurrentDragOperation() const;
    const std::string& getDragOverElementId() const;
    void cancelDrag();

    void setVisible(bool visible);
    bool isVisible() const;

    // --- Gameplay cursor control ---
    void setGameplayCursorVisible(bool visible);
    bool isGameplayCursorVisible() const;

    nlohmann::json toJson() const;
    bool loadFromJson(const nlohmann::json& data);

private:
    Vec2 windowToViewport(const Vec2& windowPos) const;
    bool isInsideViewport(const Vec2& windowPos) const;
    WidgetElement* hitTest(const Vec2& viewportLocalPos);
    const WidgetElement* hitTestConst(const Vec2& viewportLocalPos) const;
    void sortWidgetsIfNeeded();

    // Focus helpers
    struct FocusableEntry
    {
        std::string id;
        int tabIndex;
        Vec2 center;    // centre of the element in viewport space
    };
    std::vector<FocusableEntry> collectFocusableElements() const;
    void tabToNext();
    void tabToPrevious();
    void moveFocusInDirection(int dirX, int dirY);
    void activateFocusedElement();

private:
    ViewportUITheme m_theme;
    Vec4 m_viewportRect{};
    std::vector<WidgetEntry> m_widgets;
    bool m_widgetOrderDirty{ false };

    std::string m_selectedElementId;
    std::string m_pressedElementId;
    std::string m_focusedElementId;
    SelectionChangedCallback m_onSelectionChanged;
    bool m_visible{ true };
    bool m_layoutDirty{ true };
    bool m_renderDirty{ true };
    Vec2 m_mousePosition{};
    bool m_gameplayCursorVisible{ false };

    // Gamepad stick navigation state
    int  m_gpStickDirX{ 0 };
    int  m_gpStickDirY{ 0 };
    float m_gpStickRepeatTimer{ 0.0f };
    bool  m_gpStickFirstMove{ true };
    static constexpr float kGpDeadzone       = 0.25f;
    static constexpr float kGpRepeatDelay    = 0.35f;
    static constexpr float kGpRepeatInterval = 0.12f;

    // Runtime Drag & Drop state
    bool m_isDragging{ false };
    bool m_dragPending{ false };             // mouse down on draggable, waiting for threshold
    Vec2 m_dragStartPos{};                   // window position at mouse-down
    DragDropOperation m_dragOp;
    std::string m_dragOverElementId;         // element currently hovered during drag
    static constexpr float kDragThreshold = 5.0f;
};
