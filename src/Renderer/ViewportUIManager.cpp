#include "ViewportUIManager.h"
#include <SDL3/SDL.h>

namespace
{
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

            if (!IsPointInElementRect(point, x0, y0, x1, y1))
            {
                continue;
            }

            if (!element.children.empty())
            {
                if (auto* childHit = HitTestRecursive(element.children, point, x0, y0, widthPx, heightPx))
                {
                    return childHit;
                }
            }

            if (element.isHitTestable)
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

            if (!IsPointInElementRect(point, x0, y0, x1, y1))
            {
                continue;
            }

            if (!element.children.empty())
            {
                if (auto* childHit = HitTestRecursiveConst(element.children, point, x0, y0, widthPx, heightPx))
                {
                    return childHit;
                }
            }

            if (element.isHitTestable)
            {
                return &element;
            }
        }

        return nullptr;
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

void ViewportUIManager::setRootWidget(const std::shared_ptr<Widget>& widget)
{
    m_rootWidget = widget;
    m_layoutDirty = true;
    m_renderDirty = true;
}

std::shared_ptr<Widget> ViewportUIManager::getRootWidget() const
{
    return m_rootWidget;
}

void ViewportUIManager::clearRootWidget()
{
    m_rootWidget.reset();
    m_selectedElementId.clear();
    m_pressedElementId.clear();
    m_layoutDirty = true;
    m_renderDirty = true;
}

WidgetElement* ViewportUIManager::findElementById(const std::string& elementId)
{
    if (!m_rootWidget || elementId.empty())
    {
        return nullptr;
    }

    auto& elements = m_rootWidget->getElementsMutable();
    return FindElementByIdRecursive(elements, elementId);
}

WidgetElement* ViewportUIManager::getRootElement()
{
    if (!m_rootWidget)
    {
        return nullptr;
    }

    auto& elements = m_rootWidget->getElementsMutable();
    if (elements.empty())
    {
        return nullptr;
    }

    return &elements.front();
}

void ViewportUIManager::updateLayout(const std::function<Vec2(const std::string&, float)>& /*measureText*/)
{
    if (!m_rootWidget)
    {
        m_layoutDirty = false;
        return;
    }

    m_rootWidget->setLayoutDirty(false);
    m_layoutDirty = false;
}

bool ViewportUIManager::needsLayoutUpdate() const
{
    if (m_layoutDirty)
    {
        return true;
    }

    return m_rootWidget && m_rootWidget->isLayoutDirty();
}

void ViewportUIManager::markLayoutDirty()
{
    m_layoutDirty = true;
    m_renderDirty = true;
    if (m_rootWidget)
    {
        m_rootWidget->markLayoutDirty();
    }
}

bool ViewportUIManager::handleMouseDown(const Vec2& windowPos, int button)
{
    if (!m_visible || !m_rootWidget || button != SDL_BUTTON_LEFT)
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
    return true;
}

bool ViewportUIManager::handleMouseUp(const Vec2& windowPos, int button)
{
    if (!m_visible || !m_rootWidget || button != SDL_BUTTON_LEFT)
    {
        return false;
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

bool ViewportUIManager::handleKeyDown(int /*key*/)
{
    return false;
}

void ViewportUIManager::setMousePosition(const Vec2& windowPos)
{
    m_mousePosition = windowPos;
}

bool ViewportUIManager::isPointerOverViewportUI(const Vec2& windowPos) const
{
    if (!m_visible || !m_rootWidget || !isInsideViewport(windowPos))
    {
        return false;
    }

    const Vec2 localPos = windowToViewport(windowPos);
    return hitTestConst(localPos) != nullptr;
}

bool ViewportUIManager::isRenderDirty() const
{
    return m_renderDirty;
}

void ViewportUIManager::clearRenderDirty()
{
    m_renderDirty = false;
}

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

nlohmann::json ViewportUIManager::toJson() const
{
    nlohmann::json data = nlohmann::json::object();
    data["viewportRect"] = {
        { "x", m_viewportRect.x },
        { "y", m_viewportRect.y },
        { "w", m_viewportRect.z },
        { "h", m_viewportRect.w }
    };

    if (m_rootWidget)
    {
        data["rootWidget"] = m_rootWidget->toJson();
    }

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

    if (data.contains("rootWidget") && data["rootWidget"].is_object())
    {
        auto widget = std::make_shared<Widget>();
        if (!widget->loadFromJson(data["rootWidget"]))
        {
            return false;
        }
        m_rootWidget = std::move(widget);
    }

    m_layoutDirty = true;
    m_renderDirty = true;
    return true;
}

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

WidgetElement* ViewportUIManager::hitTest(const Vec2& viewportLocalPos)
{
    if (!m_rootWidget)
    {
        return nullptr;
    }

    auto& elements = m_rootWidget->getElementsMutable();
    if (elements.empty())
    {
        return nullptr;
    }

    Vec2 rootSize = m_rootWidget->getSizePixels();
    if (rootSize.x <= 0.0f)
    {
        rootSize.x = m_viewportRect.z;
    }
    if (rootSize.y <= 0.0f)
    {
        rootSize.y = m_viewportRect.w;
    }

    return HitTestRecursive(elements, viewportLocalPos, 0.0f, 0.0f, rootSize.x, rootSize.y);
}

const WidgetElement* ViewportUIManager::hitTestConst(const Vec2& viewportLocalPos) const
{
    if (!m_rootWidget)
    {
        return nullptr;
    }

    const auto& elements = m_rootWidget->getElements();
    if (elements.empty())
    {
        return nullptr;
    }

    Vec2 rootSize = m_rootWidget->getSizePixels();
    if (rootSize.x <= 0.0f)
    {
        rootSize.x = m_viewportRect.z;
    }
    if (rootSize.y <= 0.0f)
    {
        rootSize.y = m_viewportRect.w;
    }

    return HitTestRecursiveConst(elements, viewportLocalPos, 0.0f, 0.0f, rootSize.x, rootSize.y);
}

// ---------------------------------------------------------------------------
// Script-spawned widgets
// ---------------------------------------------------------------------------

std::string ViewportUIManager::registerScriptWidget(const std::shared_ptr<Widget>& widget)
{
    if (!widget)
        return {};

    const std::string id = "sw_" + std::to_string(m_nextScriptWidgetId++);
    m_scriptWidgets[id] = widget;
    m_layoutDirty = true;
    m_renderDirty = true;
    return id;
}

bool ViewportUIManager::unregisterScriptWidget(const std::string& widgetId)
{
    auto it = m_scriptWidgets.find(widgetId);
    if (it == m_scriptWidgets.end())
        return false;

    m_scriptWidgets.erase(it);
    m_renderDirty = true;
    return true;
}

void ViewportUIManager::clearAllScriptWidgets()
{
    if (!m_scriptWidgets.empty())
    {
        m_scriptWidgets.clear();
        m_renderDirty = true;
    }
    m_nextScriptWidgetId = 1;
}

const std::unordered_map<std::string, std::shared_ptr<Widget>>& ViewportUIManager::getScriptWidgets() const
{
    return m_scriptWidgets;
}

bool ViewportUIManager::hasScriptWidgets() const
{
    return !m_scriptWidgets.empty();
}
