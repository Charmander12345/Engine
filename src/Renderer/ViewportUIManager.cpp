#include "ViewportUIManager.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <algorithm>
#include <limits>

namespace
{
    // Inverse of the same RenderTransform matrix built by the renderer:
    //   T(pivot) * T(translation) * R(rotation) * S(scale) * Shear * T(-pivot)
    // Returns the point in the element's untransformed local coordinate space.
    Vec2 InverseTransformPoint(const RenderTransform& rt,
                               float x0, float y0, float w, float h,
                               const Vec2& point)
    {
        const float pivotX = x0 + rt.pivot.x * w;
        const float pivotY = y0 + rt.pivot.y * h;

        // Step 1: T(-pivot)
        float px = point.x - pivotX;
        float py = point.y - pivotY;

        // Step 2: T(-translation)
        px -= rt.translation.x;
        py -= rt.translation.y;

        // Step 3: R(-angle)
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        const float rad = -rt.rotation * kDegToRad;
        const float cosA = std::cos(rad);
        const float sinA = std::sin(rad);
        const float rx = cosA * px - sinA * py;
        const float ry = sinA * px + cosA * py;
        px = rx;
        py = ry;

        // Step 4: S^-1
        if (rt.scale.x != 0.0f) px /= rt.scale.x;
        if (rt.scale.y != 0.0f) py /= rt.scale.y;

        // Step 5: Shear^-1
        // Forward shear: x' = x + shear.x*y, y' = shear.y*x + y
        if (rt.shear.x != 0.0f || rt.shear.y != 0.0f)
        {
            const float det = 1.0f - rt.shear.x * rt.shear.y;
            if (std::abs(det) > 1e-6f)
            {
                const float sx = (px - rt.shear.x * py) / det;
                const float sy = (-rt.shear.y * px + py) / det;
                px = sx;
                py = sy;
            }
        }

        // Step 6: T(pivot)
        px += pivotX;
        py += pivotY;

        return { px, py };
    }

    WidgetElement* FindElementByIdRecursive(std::vector<WidgetElement>& elements, const std::string& elementId)
    {
        for (auto& element : elements)
        {
            if (element.id == elementId)
            {
                return &element;
            }

            if (!element.children.empty())
            {
                if (auto* child = FindElementByIdRecursive(element.children, elementId))
                {
                    return child;
                }
            }
        }

        return nullptr;
    }

    bool IsPointInElementRect(const Vec2& point, float x0, float y0, float x1, float y1)
    {
        return point.x >= x0 && point.x <= x1 && point.y >= y0 && point.y <= y1;
    }

    WidgetElement* HitTestRecursive(std::vector<WidgetElement>& elements,
        const Vec2& point,
        float parentX,
        float parentY,
        float parentW,
        float parentH)
    {
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            WidgetElement& element = *it;

            const float x0 = element.hasComputedPosition ? element.computedPositionPixels.x : (parentX + element.from.x * parentW);
            const float y0 = element.hasComputedPosition ? element.computedPositionPixels.y : (parentY + element.from.y * parentH);
            const float widthPx = element.hasComputedSize ? element.computedSizePixels.x : (parentW * (element.to.x - element.from.x));
            const float heightPx = element.hasComputedSize ? element.computedSizePixels.y : (parentH * (element.to.y - element.from.y));
            const float x1 = x0 + widthPx;
            const float y1 = y0 + heightPx;

            // Transform the point into local (untransformed) space when a
            // RenderTransform is active.  Children are positioned relative to
            // the untransformed bounds, so localPoint is also passed down.
            Vec2 localPoint = point;
            if (!element.renderTransform.isIdentity())
            {
                localPoint = InverseTransformPoint(element.renderTransform, x0, y0, widthPx, heightPx, point);
            }

            if (!IsPointInElementRect(localPoint, x0, y0, x1, y1))
            {
                continue;
            }

            // DisabledAll: skip self AND all children
            if (element.hitTestMode == HitTestMode::DisabledAll)
            {
                continue;
            }

            if (!element.children.empty())
            {
                if (auto* childHit = HitTestRecursive(element.children, localPoint, x0, y0, widthPx, heightPx))
                {
                    return childHit;
                }
            }

            if (element.hitTestMode == HitTestMode::Enabled)
            {
                return &element;
            }
        }

        return nullptr;
    }

    const WidgetElement* HitTestRecursiveConst(const std::vector<WidgetElement>& elements,
        const Vec2& point,
        float parentX,
        float parentY,
        float parentW,
        float parentH)
    {
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            const WidgetElement& element = *it;

            const float x0 = element.hasComputedPosition ? element.computedPositionPixels.x : (parentX + element.from.x * parentW);
            const float y0 = element.hasComputedPosition ? element.computedPositionPixels.y : (parentY + element.from.y * parentH);
            const float widthPx = element.hasComputedSize ? element.computedSizePixels.x : (parentW * (element.to.x - element.from.x));
            const float heightPx = element.hasComputedSize ? element.computedSizePixels.y : (parentH * (element.to.y - element.from.y));
            const float x1 = x0 + widthPx;
            const float y1 = y0 + heightPx;

            Vec2 localPoint = point;
            if (!element.renderTransform.isIdentity())
            {
                localPoint = InverseTransformPoint(element.renderTransform, x0, y0, widthPx, heightPx, point);
            }

            if (!IsPointInElementRect(localPoint, x0, y0, x1, y1))
            {
                continue;
            }

            // DisabledAll: skip self AND all children
            if (element.hitTestMode == HitTestMode::DisabledAll)
            {
                continue;
            }

            if (!element.children.empty())
            {
                if (auto* childHit = HitTestRecursiveConst(element.children, localPoint, x0, y0, widthPx, heightPx))
                {
                    return childHit;
                }
            }

            if (element.hitTestMode == HitTestMode::Enabled)
            {
                return &element;
            }
        }

        return nullptr;
    }

    struct FocusableInfo
    {
        std::string id;
        int tabIndex;
        Vec2 center;
    };

    void CollectFocusableRecursive(const std::vector<WidgetElement>& elements,
                                    float parentX, float parentY,
                                    float parentW, float parentH,
                                    std::vector<FocusableInfo>& out,
                                    int& autoIndex)
    {
        for (const auto& element : elements)
        {
            const float x0 = element.hasComputedPosition ? element.computedPositionPixels.x : (parentX + element.from.x * parentW);
            const float y0 = element.hasComputedPosition ? element.computedPositionPixels.y : (parentY + element.from.y * parentH);
            const float w  = element.hasComputedSize ? element.computedSizePixels.x : (parentW * (element.to.x - element.from.x));
            const float h  = element.hasComputedSize ? element.computedSizePixels.y : (parentH * (element.to.y - element.from.y));

            if (element.focusConfig.isFocusable && !element.id.empty())
            {
                const int idx = element.focusConfig.tabIndex >= 0 ? element.focusConfig.tabIndex : autoIndex;
                out.push_back({ element.id, idx, Vec2{ x0 + w * 0.5f, y0 + h * 0.5f } });
                ++autoIndex;
            }

            if (!element.children.empty())
            {
                CollectFocusableRecursive(element.children, x0, y0, w, h, out, autoIndex);
            }
        }
    }
}

ViewportUIManager::ViewportUIManager() = default;

ViewportUIManager::~ViewportUIManager() = default;

void ViewportUIManager::setViewportRect(float x, float y, float width, float height)
{
    m_viewportRect = Vec4{ x, y, width, height };
    m_layoutDirty = true;
    m_renderDirty = true;
}

Vec4 ViewportUIManager::getViewportRect() const
{
    return m_viewportRect;
}

Vec2 ViewportUIManager::getViewportSize() const
{
    return Vec2{ m_viewportRect.z, m_viewportRect.w };
}

// ---------------------------------------------------------------------------
// Multi-widget management
// ---------------------------------------------------------------------------

bool ViewportUIManager::createWidget(const std::string& name, int zOrder)
{
    // Check for duplicate name
    for (const auto& entry : m_widgets)
    {
        if (entry.name == name)
            return false;
    }

    auto widget = std::make_shared<Widget>();
    widget->setName(name);
    widget->setZOrder(zOrder);

    // Create implicit Canvas Panel as root element
    WidgetElement canvas{};
    canvas.type = WidgetElementType::Panel;
    canvas.id = name + "_canvas";
    canvas.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    canvas.style.hoverColor = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    canvas.fillX = true;
    canvas.fillY = true;
    canvas.from = Vec2{ 0.0f, 0.0f };
    canvas.to = Vec2{ 1.0f, 1.0f };
    widget->setElements({ std::move(canvas) });

    m_widgets.push_back({ name, std::move(widget) });
    m_widgetOrderDirty = true;
    m_layoutDirty = true;
    m_renderDirty = true;
    return true;
}

bool ViewportUIManager::removeWidget(const std::string& name)
{
    for (auto it = m_widgets.begin(); it != m_widgets.end(); ++it)
    {
        if (it->name == name)
        {
            m_widgets.erase(it);
            m_renderDirty = true;
            return true;
        }
    }
    return false;
}

Widget* ViewportUIManager::getWidget(const std::string& name)
{
    for (auto& entry : m_widgets)
    {
        if (entry.name == name)
            return entry.widget.get();
    }
    return nullptr;
}

const Widget* ViewportUIManager::getWidget(const std::string& name) const
{
    for (const auto& entry : m_widgets)
    {
        if (entry.name == name)
            return entry.widget.get();
    }
    return nullptr;
}

void ViewportUIManager::clearAllWidgets()
{
    if (!m_widgets.empty())
    {
        m_widgets.clear();
        m_renderDirty = true;
    }
    m_selectedElementId.clear();
    m_pressedElementId.clear();
    m_focusedElementId.clear();
    m_gameplayCursorVisible = false;
    m_layoutDirty = true;
}

bool ViewportUIManager::hasWidgets() const
{
    return !m_widgets.empty();
}

const std::vector<ViewportUIManager::WidgetEntry>& ViewportUIManager::getSortedWidgets() const
{
    const_cast<ViewportUIManager*>(this)->sortWidgetsIfNeeded();
    return m_widgets;
}

void ViewportUIManager::sortWidgetsIfNeeded()
{
    if (!m_widgetOrderDirty)
        return;

    std::stable_sort(m_widgets.begin(), m_widgets.end(),
        [](const WidgetEntry& a, const WidgetEntry& b)
        {
            return a.widget->getZOrder() < b.widget->getZOrder();
        });
    m_widgetOrderDirty = false;
}

// ---------------------------------------------------------------------------
// Element access
// ---------------------------------------------------------------------------

WidgetElement* ViewportUIManager::findElementById(const std::string& elementId)
{
    if (elementId.empty())
        return nullptr;

    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
            continue;
        auto& elements = entry.widget->getElementsMutable();
        if (auto* found = FindElementByIdRecursive(elements, elementId))
            return found;
    }
    return nullptr;
}

WidgetElement* ViewportUIManager::findElementById(const std::string& widgetName, const std::string& elementId)
{
    if (widgetName.empty() || elementId.empty())
        return nullptr;

    Widget* w = getWidget(widgetName);
    if (!w)
        return nullptr;

    auto& elements = w->getElementsMutable();
    return FindElementByIdRecursive(elements, elementId);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

static Vec2 computeAnchorPivot(WidgetAnchor anchor, float viewportW, float viewportH)
{
    switch (anchor)
    {
    case WidgetAnchor::TopLeft:     return Vec2{ 0.0f, 0.0f };
    case WidgetAnchor::TopRight:    return Vec2{ viewportW, 0.0f };
    case WidgetAnchor::BottomLeft:  return Vec2{ 0.0f, viewportH };
    case WidgetAnchor::BottomRight: return Vec2{ viewportW, viewportH };
    case WidgetAnchor::Top:         return Vec2{ viewportW * 0.5f, 0.0f };
    case WidgetAnchor::Bottom:      return Vec2{ viewportW * 0.5f, viewportH };
    case WidgetAnchor::Left:        return Vec2{ 0.0f, viewportH * 0.5f };
    case WidgetAnchor::Right:       return Vec2{ viewportW, viewportH * 0.5f };
    case WidgetAnchor::Center:      return Vec2{ viewportW * 0.5f, viewportH * 0.5f };
    case WidgetAnchor::Stretch:     return Vec2{ 0.0f, 0.0f };
    default:                        return Vec2{ 0.0f, 0.0f };
    }
}

static bool isNormalized(const Vec2& from, const Vec2& to)
{
    return from.x >= 0.0f && from.x <= 1.0f &&
           from.y >= 0.0f && from.y <= 1.0f &&
           to.x   >= 0.0f && to.x   <= 1.0f &&
           to.y   >= 0.0f && to.y   <= 1.0f;
}

static void ResolveAnchorsRecursive(std::vector<WidgetElement>& elements, float parentW, float parentH)
{
    for (auto& element : elements)
    {
        if (element.anchor == WidgetAnchor::Stretch)
        {
            element.computedPositionPixels = Vec2{ element.anchorOffset.x, element.anchorOffset.y };
            element.computedSizePixels = Vec2{
                parentW - 2.0f * element.anchorOffset.x,
                parentH - 2.0f * element.anchorOffset.y
            };
        }
        else if (element.anchor == WidgetAnchor::TopLeft &&
                 element.anchorOffset.x == 0.0f && element.anchorOffset.y == 0.0f &&
                 isNormalized(element.from, element.to))
        {
            // Widget asset elements use normalized (0..1) from/to relative to parent
            const float px = element.from.x * parentW;
            const float py = element.from.y * parentH;
            const float w  = (element.to.x - element.from.x) * parentW;
            const float h  = (element.to.y - element.from.y) * parentH;
            element.computedPositionPixels = Vec2{ px, py };
            element.computedSizePixels = Vec2{ w > 0.0f ? w : 0.0f, h > 0.0f ? h : 0.0f };
        }
        else
        {
            Vec2 pivot = computeAnchorPivot(element.anchor, parentW, parentH);
            element.computedPositionPixels = Vec2{
                pivot.x + element.anchorOffset.x,
                pivot.y + element.anchorOffset.y
            };
            const float w = (element.to.x - element.from.x);
            const float h = (element.to.y - element.from.y);
            element.computedSizePixels = Vec2{ w > 0.0f ? w : 0.0f, h > 0.0f ? h : 0.0f };
        }
        element.hasComputedPosition = true;
        element.hasComputedSize = true;

        if (!element.children.empty())
        {
            ResolveAnchorsRecursive(element.children,
                element.computedSizePixels.x, element.computedSizePixels.y);
        }
    }
}

void ViewportUIManager::updateLayout(const std::function<Vec2(const std::string&, float)>& /*measureText*/)
{
    sortWidgetsIfNeeded();

    const float vpW = m_viewportRect.z;
    const float vpH = m_viewportRect.w;

    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
            continue;

        Vec2 widgetSize = entry.widget->getSizePixels();
        if (widgetSize.x <= 0.0f) widgetSize.x = vpW;
        if (widgetSize.y <= 0.0f) widgetSize.y = vpH;
        entry.widget->setComputedSizePixels(widgetSize, true);
        entry.widget->setComputedPositionPixels(Vec2{ 0.0f, 0.0f }, true);

        auto& elements = entry.widget->getElementsMutable();
        if (!elements.empty())
        {
            // The canvas root element fills the viewport
            auto& canvas = elements.front();
            canvas.computedPositionPixels = Vec2{ 0.0f, 0.0f };
            canvas.computedSizePixels = widgetSize;
            canvas.hasComputedPosition = true;
            canvas.hasComputedSize = true;

            // Resolve anchors for children of the canvas
            ResolveAnchorsRecursive(canvas.children, widgetSize.x, widgetSize.y);
        }

        entry.widget->setLayoutDirty(false);
    }

    m_layoutDirty = false;
}

static void tickSpinnersRecursive(std::vector<WidgetElement>& elements, float dt, bool& changed)
{
    for (auto& el : elements)
    {
        if (el.type == WidgetElementType::Spinner)
        {
            el.spinnerElapsed += dt;
            changed = true;
        }
        if (!el.children.empty())
        {
            tickSpinnersRecursive(el.children, dt, changed);
        }
    }
}

void ViewportUIManager::tickAnimations(float deltaTime)
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    bool changed = false;
    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }

        auto& player = entry.widget->animationPlayer();
        const float beforeTime = player.getCurrentTime();
        const bool beforePlaying = player.isPlaying();
        player.tick(deltaTime);

        if (beforePlaying || player.isPlaying() || std::abs(player.getCurrentTime() - beforeTime) > 1e-6f)
        {
            changed = true;
        }

        tickSpinnersRecursive(entry.widget->getElementsMutable(), deltaTime, changed);
    }

    if (changed)
    {
        m_layoutDirty = true;
        m_renderDirty = true;
    }

    // Gamepad left-stick repeat navigation
    if (m_gpStickDirX != 0 || m_gpStickDirY != 0)
    {
        if (m_gpStickFirstMove)
        {
            moveFocusInDirection(m_gpStickDirX, m_gpStickDirY);
            m_gpStickFirstMove = false;
            m_gpStickRepeatTimer = 0.0f;
        }
        else
        {
            m_gpStickRepeatTimer += deltaTime;
            const float threshold = (m_gpStickRepeatTimer < kGpRepeatDelay)
                ? kGpRepeatDelay : kGpRepeatInterval;
            if (m_gpStickRepeatTimer >= threshold)
            {
                moveFocusInDirection(m_gpStickDirX, m_gpStickDirY);
                m_gpStickRepeatTimer = (m_gpStickRepeatTimer < kGpRepeatDelay)
                    ? 0.0f : m_gpStickRepeatTimer - kGpRepeatInterval;
            }
        }
    }
}

bool ViewportUIManager::needsLayoutUpdate() const
{
    if (m_layoutDirty)
        return true;

    for (const auto& entry : m_widgets)
    {
        if (entry.widget && entry.widget->isLayoutDirty())
            return true;
    }
    return false;
}

void ViewportUIManager::markLayoutDirty()
{
    m_layoutDirty = true;
    m_renderDirty = true;
    for (auto& entry : m_widgets)
    {
        if (entry.widget)
            entry.widget->markLayoutDirty();
    }
}

// ---------------------------------------------------------------------------
// Input handling
// ---------------------------------------------------------------------------

bool ViewportUIManager::handleMouseDown(const Vec2& windowPos, int button)
{
    if (!m_visible || m_widgets.empty() || button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    if (!isInsideViewport(windowPos))
    {
        m_pressedElementId.clear();
        return false;
    }

    const Vec2 localPos = windowToViewport(windowPos);
    WidgetElement* hit = hitTest(localPos);
    if (!hit)
    {
        m_pressedElementId.clear();
        return false;
    }

    m_pressedElementId = hit->id;
    hit->isPressed = true;
    m_renderDirty = true;
    setSelectedElementId(hit->id);

    // Set focus on clicked element if it is focusable
    if (hit->focusConfig.isFocusable)
        setFocus(hit->id);
    else
        clearFocus();

    // Begin pending drag if element is draggable
    if (hit->isDraggable)
    {
        m_dragPending = true;
        m_dragStartPos = windowPos;
    }

    return true;
}

bool ViewportUIManager::handleMouseUp(const Vec2& windowPos, int button)
{
    if (!m_visible || m_widgets.empty() || button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    // Cancel pending drag on mouse up (didn't meet threshold)
    m_dragPending = false;

    // Complete active drag
    if (m_isDragging)
    {
        // Find drop target
        if (isInsideViewport(windowPos))
        {
            WidgetElement* dropTarget = hitTest(windowToViewport(windowPos));
            if (dropTarget && dropTarget->acceptsDrop && dropTarget->onDrop)
            {
                bool accepted = true;
                if (dropTarget->onDragOver)
                    accepted = dropTarget->onDragOver(m_dragOp);

                if (accepted)
                    dropTarget->onDrop(m_dragOp);
            }
        }
        cancelDrag();
        m_pressedElementId.clear();
        return true;
    }

    bool consumed = false;
    WidgetElement* hit = nullptr;
    if (isInsideViewport(windowPos))
    {
        hit = hitTest(windowToViewport(windowPos));
    }

    if (!m_pressedElementId.empty())
    {
        if (auto* pressed = findElementById(m_pressedElementId))
        {
            pressed->isPressed = false;
            consumed = true;

            if (hit && hit->id == m_pressedElementId && pressed->onClicked)
            {
                pressed->onClicked();
                m_renderDirty = true;
            }
        }
    }

    m_pressedElementId.clear();
    return consumed || hit != nullptr;
}

bool ViewportUIManager::handleScroll(const Vec2& /*windowPos*/, float /*delta*/)
{
    return false;
}

bool ViewportUIManager::handleTextInput(const std::string& /*text*/)
{
    return false;
}

bool ViewportUIManager::handleKeyDown(int key, int modifiers)
{
    if (!m_visible || m_widgets.empty())
        return false;

    // Tab / Shift+Tab: cycle focus
    if (key == SDLK_TAB)
    {
        if (modifiers & SDL_KMOD_SHIFT)
            tabToPrevious();
        else
            tabToNext();
        return true;
    }

    // Arrow keys: spatial navigation
    if (key == SDLK_UP)    { moveFocusInDirection( 0, -1); return true; }
    if (key == SDLK_DOWN)  { moveFocusInDirection( 0,  1); return true; }
    if (key == SDLK_LEFT)  { moveFocusInDirection(-1,  0); return true; }
    if (key == SDLK_RIGHT) { moveFocusInDirection( 1,  0); return true; }

    // Enter / Space: activate focused element
    if ((key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) && !m_focusedElementId.empty())
    {
        activateFocusedElement();
        return true;
    }

    // Escape: clear focus
    if (key == SDLK_ESCAPE && !m_focusedElementId.empty())
    {
        clearFocus();
        return true;
    }

    return false;
}

bool ViewportUIManager::handleGamepadButton(int button, bool pressed)
{
    if (!m_visible || m_widgets.empty() || !pressed)
        return false;

    // D-Pad → spatial navigation
    if (button == SDL_GAMEPAD_BUTTON_DPAD_UP)    { moveFocusInDirection( 0, -1); return true; }
    if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)  { moveFocusInDirection( 0,  1); return true; }
    if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)  { moveFocusInDirection(-1,  0); return true; }
    if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) { moveFocusInDirection( 1,  0); return true; }

    // A / Cross → activate
    if (button == SDL_GAMEPAD_BUTTON_SOUTH && !m_focusedElementId.empty())
    {
        activateFocusedElement();
        return true;
    }

    // B / Circle → back / clear focus
    if (button == SDL_GAMEPAD_BUTTON_EAST && !m_focusedElementId.empty())
    {
        clearFocus();
        return true;
    }

    // LB → tab previous, RB → tab next
    if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)  { tabToPrevious(); return true; }
    if (button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) { tabToNext();     return true; }

    return false;
}

bool ViewportUIManager::handleGamepadAxis(int axis, float value)
{
    if (!m_visible || m_widgets.empty())
        return false;

    // Only process left stick
    if (axis == SDL_GAMEPAD_AXIS_LEFTX)
    {
        if (std::abs(value) < kGpDeadzone)
            m_gpStickDirX = 0;
        else
            m_gpStickDirX = (value > 0.0f) ? 1 : -1;
    }
    else if (axis == SDL_GAMEPAD_AXIS_LEFTY)
    {
        if (std::abs(value) < kGpDeadzone)
            m_gpStickDirY = 0;
        else
            m_gpStickDirY = (value > 0.0f) ? 1 : -1;
    }
    else
    {
        return false;
    }

    // Reset repeat timer when direction changes
    if (m_gpStickDirX == 0 && m_gpStickDirY == 0)
    {
        m_gpStickRepeatTimer = 0.0f;
        m_gpStickFirstMove = true;
    }

    return m_gpStickDirX != 0 || m_gpStickDirY != 0;
}

bool ViewportUIManager::handleMouseMove(const Vec2& windowPos)
{
    if (!m_visible || m_widgets.empty())
        return false;

    // Check if we should start a drag (pending from mouseDown, threshold met)
    if (m_dragPending && !m_isDragging)
    {
        const float dx = windowPos.x - m_dragStartPos.x;
        const float dy = windowPos.y - m_dragStartPos.y;
        if (dx * dx + dy * dy >= kDragThreshold * kDragThreshold)
        {
            auto* source = findElementById(m_pressedElementId);
            if (source && source->isDraggable)
            {
                m_isDragging = true;
                m_dragPending = false;
                m_dragOp.sourceElementId = source->id;
                m_dragOp.payload = source->dragPayload;
                m_dragOp.dragPosition = windowToViewport(windowPos);

                if (source->onDragStart)
                    source->onDragStart();

                source->isPressed = false;
                m_renderDirty = true;
            }
            else
            {
                m_dragPending = false;
            }
        }
    }

    // Update active drag
    if (m_isDragging)
    {
        m_dragOp.dragPosition = windowToViewport(windowPos);

        // Find element under cursor for drop-target feedback
        std::string newOverId;
        if (isInsideViewport(windowPos))
        {
            if (auto* over = hitTest(windowToViewport(windowPos)))
            {
                if (over->acceptsDrop && over->id != m_dragOp.sourceElementId)
                    newOverId = over->id;
            }
        }

        if (newOverId != m_dragOverElementId)
        {
            m_dragOverElementId = newOverId;
            m_renderDirty = true;
        }

        return true;
    }

    return false;
}

bool ViewportUIManager::isDragging() const
{
    return m_isDragging;
}

const DragDropOperation& ViewportUIManager::getCurrentDragOperation() const
{
    return m_dragOp;
}

const std::string& ViewportUIManager::getDragOverElementId() const
{
    return m_dragOverElementId;
}

void ViewportUIManager::cancelDrag()
{
    m_isDragging = false;
    m_dragPending = false;
    m_dragOp = DragDropOperation{};
    m_dragOverElementId.clear();
    m_renderDirty = true;
}

void ViewportUIManager::setMousePosition(const Vec2& windowPos)
{
    m_mousePosition = windowPos;
}

bool ViewportUIManager::isPointerOverViewportUI(const Vec2& windowPos) const
{
    if (!m_visible || m_widgets.empty() || !isInsideViewport(windowPos))
    {
        return false;
    }

    const Vec2 localPos = windowToViewport(windowPos);
    return hitTestConst(localPos) != nullptr;
}

// ---------------------------------------------------------------------------
// Render dirty
// ---------------------------------------------------------------------------

bool ViewportUIManager::isRenderDirty() const
{
    return m_renderDirty;
}

void ViewportUIManager::clearRenderDirty()
{
    m_renderDirty = false;
}

// ---------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------

void ViewportUIManager::setSelectedElementId(const std::string& elementId)
{
    if (m_selectedElementId == elementId)
    {
        return;
    }

    m_selectedElementId = elementId;
    if (m_onSelectionChanged)
    {
        m_onSelectionChanged(m_selectedElementId);
    }
}

const std::string& ViewportUIManager::getSelectedElementId() const
{
    return m_selectedElementId;
}

void ViewportUIManager::setOnSelectionChanged(SelectionChangedCallback callback)
{
    m_onSelectionChanged = std::move(callback);
}

// ---------------------------------------------------------------------------
// Focus management (Phase 5)
// ---------------------------------------------------------------------------

void ViewportUIManager::setFocus(const std::string& elementId)
{
    if (m_focusedElementId == elementId)
        return;

    // Clear old focus
    if (auto* prev = findElementById(m_focusedElementId))
        prev->isFocused = false;

    m_focusedElementId = elementId;

    // Set new focus
    if (auto* elem = findElementById(m_focusedElementId))
        elem->isFocused = true;

    m_renderDirty = true;
}

void ViewportUIManager::clearFocus()
{
    if (m_focusedElementId.empty())
        return;

    if (auto* prev = findElementById(m_focusedElementId))
        prev->isFocused = false;

    m_focusedElementId.clear();
    m_renderDirty = true;
}

const std::string& ViewportUIManager::getFocusedElementId() const
{
    return m_focusedElementId;
}

void ViewportUIManager::setFocusable(const std::string& elementId, bool focusable)
{
    if (auto* elem = findElementById(elementId))
    {
        elem->focusConfig.isFocusable = focusable;
    }
}

std::vector<ViewportUIManager::FocusableEntry> ViewportUIManager::collectFocusableElements() const
{
    std::vector<FocusableEntry> result;
    const float vpW = m_viewportRect.z;
    const float vpH = m_viewportRect.w;

    for (const auto& entry : m_widgets)
    {
        if (!entry.widget)
            continue;
        const auto& elements = entry.widget->getElements();
        if (elements.empty())
            continue;

        Vec2 widgetSize = entry.widget->getSizePixels();
        if (widgetSize.x <= 0.0f) widgetSize.x = vpW;
        if (widgetSize.y <= 0.0f) widgetSize.y = vpH;

        int autoIndex = 0;
        std::vector<FocusableInfo> infos;
        CollectFocusableRecursive(elements, 0.0f, 0.0f, widgetSize.x, widgetSize.y, infos, autoIndex);
        for (auto& fi : infos)
        {
            result.push_back({ std::move(fi.id), fi.tabIndex, fi.center });
        }
    }

    // Sort by tabIndex (stable, so document order is preserved for equal indices)
    std::stable_sort(result.begin(), result.end(),
        [](const FocusableEntry& a, const FocusableEntry& b)
        {
            return a.tabIndex < b.tabIndex;
        });

    return result;
}

void ViewportUIManager::tabToNext()
{
    auto focusables = collectFocusableElements();
    if (focusables.empty())
        return;

    if (m_focusedElementId.empty())
    {
        setFocus(focusables.front().id);
        return;
    }

    for (size_t i = 0; i < focusables.size(); ++i)
    {
        if (focusables[i].id == m_focusedElementId)
        {
            const size_t next = (i + 1) % focusables.size();
            setFocus(focusables[next].id);
            return;
        }
    }
    // Current focus not found, go to first
    setFocus(focusables.front().id);
}

void ViewportUIManager::tabToPrevious()
{
    auto focusables = collectFocusableElements();
    if (focusables.empty())
        return;

    if (m_focusedElementId.empty())
    {
        setFocus(focusables.back().id);
        return;
    }

    for (size_t i = 0; i < focusables.size(); ++i)
    {
        if (focusables[i].id == m_focusedElementId)
        {
            const size_t prev = (i == 0) ? focusables.size() - 1 : i - 1;
            setFocus(focusables[prev].id);
            return;
        }
    }
    setFocus(focusables.back().id);
}

void ViewportUIManager::moveFocusInDirection(int dirX, int dirY)
{
    // First check explicit directional overrides
    if (auto* focused = findElementById(m_focusedElementId))
    {
        const std::string& target =
            (dirY < 0) ? focused->focusConfig.focusUp :
            (dirY > 0) ? focused->focusConfig.focusDown :
            (dirX < 0) ? focused->focusConfig.focusLeft :
            (dirX > 0) ? focused->focusConfig.focusRight : focused->focusConfig.focusUp;

        if (!target.empty())
        {
            setFocus(target);
            return;
        }
    }

    // Spatial navigation: find nearest focusable element in the given direction
    auto focusables = collectFocusableElements();
    if (focusables.empty())
        return;

    Vec2 currentCenter{};
    bool foundCurrent = false;
    for (const auto& fe : focusables)
    {
        if (fe.id == m_focusedElementId)
        {
            currentCenter = fe.center;
            foundCurrent = true;
            break;
        }
    }
    if (!foundCurrent)
    {
        if (!focusables.empty())
            setFocus(focusables.front().id);
        return;
    }

    float bestDist = std::numeric_limits<float>::max();
    std::string bestId;
    const float dx = static_cast<float>(dirX);
    const float dy = static_cast<float>(dirY);

    for (const auto& fe : focusables)
    {
        if (fe.id == m_focusedElementId)
            continue;

        const float offX = fe.center.x - currentCenter.x;
        const float offY = fe.center.y - currentCenter.y;

        // Check the candidate is in the correct direction
        const float dot = offX * dx + offY * dy;
        if (dot <= 0.0f)
            continue;

        const float dist = offX * offX + offY * offY;
        if (dist < bestDist)
        {
            bestDist = dist;
            bestId = fe.id;
        }
    }

    if (!bestId.empty())
        setFocus(bestId);
}

void ViewportUIManager::activateFocusedElement()
{
    auto* elem = findElementById(m_focusedElementId);
    if (!elem)
        return;

    // Toggle checkboxes
    if (elem->type == WidgetElementType::CheckBox)
    {
        elem->isChecked = !elem->isChecked;
        if (elem->onCheckedChanged)
            elem->onCheckedChanged(elem->isChecked);
        m_renderDirty = true;
        return;
    }

    // Toggle buttons
    if (elem->type == WidgetElementType::ToggleButton)
    {
        elem->isChecked = !elem->isChecked;
        if (elem->onCheckedChanged)
            elem->onCheckedChanged(elem->isChecked);
        m_renderDirty = true;
        return;
    }

    // Click callback for all other types
    if (elem->onClicked)
    {
        elem->onClicked();
        m_renderDirty = true;
    }
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------

void ViewportUIManager::setVisible(bool visible)
{
    if (m_visible == visible)
    {
        return;
    }

    m_visible = visible;
    m_renderDirty = true;
}

bool ViewportUIManager::isVisible() const
{
    return m_visible;
}

// ---------------------------------------------------------------------------
// Gameplay cursor control
// ---------------------------------------------------------------------------

void ViewportUIManager::setGameplayCursorVisible(bool visible)
{
    m_gameplayCursorVisible = visible;
}

bool ViewportUIManager::isGameplayCursorVisible() const
{
    return m_gameplayCursorVisible;
}

// ---------------------------------------------------------------------------
// JSON (debug / snapshot)
// ---------------------------------------------------------------------------

nlohmann::json ViewportUIManager::toJson() const
{
    nlohmann::json data = nlohmann::json::object();
    data["viewportRect"] = {
        { "x", m_viewportRect.x },
        { "y", m_viewportRect.y },
        { "w", m_viewportRect.z },
        { "h", m_viewportRect.w }
    };

    nlohmann::json widgetsArr = nlohmann::json::array();
    for (const auto& entry : m_widgets)
    {
        if (entry.widget)
        {
            nlohmann::json wj = entry.widget->toJson();
            wj["_name"] = entry.name;
            widgetsArr.push_back(std::move(wj));
        }
    }
    data["widgets"] = std::move(widgetsArr);

    return data;
}

bool ViewportUIManager::loadFromJson(const nlohmann::json& data)
{
    if (!data.is_object())
    {
        return false;
    }

    if (data.contains("viewportRect") && data["viewportRect"].is_object())
    {
        const auto& rect = data["viewportRect"];
        m_viewportRect = Vec4{
            rect.value("x", 0.0f),
            rect.value("y", 0.0f),
            rect.value("w", 0.0f),
            rect.value("h", 0.0f)
        };
    }

    m_layoutDirty = true;
    m_renderDirty = true;
    return true;
}

// ---------------------------------------------------------------------------
// Coordinate helpers
// ---------------------------------------------------------------------------

Vec2 ViewportUIManager::windowToViewport(const Vec2& windowPos) const
{
    return Vec2{ windowPos.x - m_viewportRect.x, windowPos.y - m_viewportRect.y };
}

bool ViewportUIManager::isInsideViewport(const Vec2& windowPos) const
{
    if (m_viewportRect.z <= 0.0f || m_viewportRect.w <= 0.0f)
    {
        return false;
    }

    return windowPos.x >= m_viewportRect.x &&
           windowPos.x <= (m_viewportRect.x + m_viewportRect.z) &&
           windowPos.y >= m_viewportRect.y &&
           windowPos.y <= (m_viewportRect.y + m_viewportRect.w);
}

// ---------------------------------------------------------------------------
// Hit testing (multi-widget, z-order aware)
// ---------------------------------------------------------------------------

WidgetElement* ViewportUIManager::hitTest(const Vec2& viewportLocalPos)
{
    const_cast<ViewportUIManager*>(this)->sortWidgetsIfNeeded();

    const float vpW = m_viewportRect.z;
    const float vpH = m_viewportRect.w;

    // Iterate in reverse z-order (highest/frontmost first)
    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it)
    {
        if (!it->widget)
            continue;

        auto& elements = it->widget->getElementsMutable();
        if (elements.empty())
            continue;

        Vec2 widgetSize = it->widget->getSizePixels();
        if (widgetSize.x <= 0.0f) widgetSize.x = vpW;
        if (widgetSize.y <= 0.0f) widgetSize.y = vpH;

        if (auto* hit = HitTestRecursive(elements, viewportLocalPos, 0.0f, 0.0f, widgetSize.x, widgetSize.y))
            return hit;
    }
    return nullptr;
}

const WidgetElement* ViewportUIManager::hitTestConst(const Vec2& viewportLocalPos) const
{
    const_cast<ViewportUIManager*>(this)->sortWidgetsIfNeeded();

    const float vpW = m_viewportRect.z;
    const float vpH = m_viewportRect.w;

    for (auto it = m_widgets.rbegin(); it != m_widgets.rend(); ++it)
    {
        if (!it->widget)
            continue;

        const auto& elements = it->widget->getElements();
        if (elements.empty())
            continue;

        Vec2 widgetSize = it->widget->getSizePixels();
        if (widgetSize.x <= 0.0f) widgetSize.x = vpW;
        if (widgetSize.y <= 0.0f) widgetSize.y = vpH;

        if (auto* hit = HitTestRecursiveConst(elements, viewportLocalPos, 0.0f, 0.0f, widgetSize.x, widgetSize.y))
            return hit;
    }
    return nullptr;
}
