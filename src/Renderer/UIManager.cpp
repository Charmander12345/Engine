#include "UIManager.h"

#include <algorithm>
#include <numeric>
#include <SDL3/SDL.h>
#include "../Diagnostics/DiagnosticsManager.h"

namespace
{
    Vec2 measureElementSize(WidgetElement& element, const std::function<Vec2(const std::string&, float)>& measureText)
    {
        Vec2 size{};
        std::vector<Vec2> childSizes;
        switch (element.type)
        {
        case WidgetElementType::Text:
        {
            float scale = (element.fontSize > 0.0f) ? (element.fontSize / 48.0f) : 1.0f;
            const Vec2 textSize = measureText ? measureText(element.text, scale) : Vec2{};
            size.x = textSize.x + element.padding.x * 2.0f;
            size.y = textSize.y + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::Button:
        {
            float scale = (element.fontSize > 0.0f) ? (element.fontSize / 48.0f) : 1.0f;
            const Vec2 textSize = (!element.text.empty() && measureText) ? measureText(element.text, scale) : Vec2{};
            size.x = textSize.x + element.padding.x * 2.0f;
            size.y = textSize.y + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::StackPanel:
        {
            childSizes.reserve(element.children.size());
            for (auto& child : element.children)
            {
                const Vec2 childSize = measureElementSize(child, measureText);
                Vec2 withMargin{
                    childSize.x + child.margin.x * 2.0f,
                    childSize.y + child.margin.y * 2.0f
                };
                childSizes.push_back(withMargin);
            }
            break;
        }
        default:
            break;
        }

        if (element.type == WidgetElementType::StackPanel)
        {
            if (element.orientation == StackOrientation::Horizontal)
            {
                float widthSum = 0.0f;
                float maxHeight = 0.0f;
                for (const auto& childSize : childSizes)
                {
                    widthSum += childSize.x;
                    maxHeight = std::max(maxHeight, childSize.y);
                }
                if (!childSizes.empty())
                {
                    widthSum += element.padding.x * 2.0f + element.padding.x * static_cast<float>(childSizes.size() - 1);
                    maxHeight += element.padding.y * 2.0f;
                }
                size = Vec2{ widthSum, maxHeight };
            }
            else
            {
                float heightSum = 0.0f;
                float maxWidth = 0.0f;
                for (const auto& childSize : childSizes)
                {
                    heightSum += childSize.y;
                    maxWidth = std::max(maxWidth, childSize.x);
                }
                if (!childSizes.empty())
                {
                    heightSum += element.padding.y * 2.0f + element.padding.y * static_cast<float>(childSizes.size() - 1);
                    maxWidth += element.padding.x * 2.0f;
                }
                size = Vec2{ maxWidth, heightSum };
            }
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        element.hasContentSize = false;
        element.contentSizePixels = {};
        return size;
    }

    void layoutElement(WidgetElement& element,
        float parentX,
        float parentY,
        float parentW,
        float parentH,
        const std::function<Vec2(const std::string&, float)>& measureText)
    {
        const float baseW = parentW * (element.to.x - element.from.x);
        const float baseH = parentH * (element.to.y - element.from.y);
        float width = baseW;
        float height = baseH;

        if (element.sizeToContent && element.hasContentSize)
        {
            if (element.type == WidgetElementType::StackPanel && element.orientation == StackOrientation::Horizontal && baseH > 0.0f)
            {
                width = element.contentSizePixels.x;
                height = baseH;
            }
            else if (element.type == WidgetElementType::StackPanel && element.orientation == StackOrientation::Vertical && baseW > 0.0f)
            {
                width = baseW;
                height = element.contentSizePixels.y;
            }
            else
            {
                width = element.contentSizePixels.x;
                height = element.contentSizePixels.y;
            }
        }
        else if ((element.type == WidgetElementType::Text || element.type == WidgetElementType::Button) && element.hasContentSize)
        {
            if (baseW <= 0.0f)
            {
                width = element.contentSizePixels.x;
            }
            if (baseH <= 0.0f)
            {
                height = element.contentSizePixels.y;
            }
        }

        float x0 = parentX + element.from.x * parentW;
        float y0 = parentY + element.from.y * parentH;
        if (element.sizeToContent && element.type == WidgetElementType::StackPanel && element.to.x > element.from.x)
        {
            x0 = parentX + element.to.x * parentW - width;
        }
        if (element.fillX)
        {
            x0 = parentX;
            width = parentW;
        }
        if (element.fillY)
        {
            y0 = parentY;
            height = parentH;
        }

        element.computedPositionPixels = { x0, y0 };
        element.computedSizePixels = { width, height };
        element.hasComputedPosition = true;
        element.hasComputedSize = true;

        if (element.type != WidgetElementType::StackPanel || element.children.empty())
        {
            return;
        }

        const float contentX = x0 + element.padding.x;
        const float contentY = y0 + element.padding.y;
        const float contentW = std::max(0.0f, width - element.padding.x * 2.0f);
        const float contentH = std::max(0.0f, height - element.padding.y * 2.0f);

        if (element.orientation == StackOrientation::Horizontal)
        {
            float spacing = element.padding.x;
            float totalSpacing = spacing * static_cast<float>(element.children.size() > 0 ? (element.children.size() - 1) : 0);
            float availableW = std::max(0.0f, contentW - totalSpacing);
            float defaultW = (element.children.size() > 0) ? (availableW / static_cast<float>(element.children.size())) : 0.0f;
            float cursorX = contentX;

            for (auto& child : element.children)
            {
                float slotW = defaultW;
                float slotH = contentH;
                if (element.sizeToContent && child.hasContentSize)
                {
                    slotW = child.contentSizePixels.x + child.margin.x * 2.0f;
                }
                slotH = std::max(slotH, child.minSize.y);
                const float childX = cursorX + child.margin.x;
                const float childY = contentY + child.margin.y;
                const float childW = std::max(0.0f, slotW - child.margin.x * 2.0f);
                const float childH = std::max(0.0f, slotH - child.margin.y * 2.0f);
                layoutElement(child, childX, childY, childW, childH, measureText);
                cursorX += slotW + spacing;
            }
        }
        else
        {
            float spacing = element.padding.y;
            float totalSpacing = spacing * static_cast<float>(element.children.size() > 0 ? (element.children.size() - 1) : 0);
            float availableH = std::max(0.0f, contentH - totalSpacing);
            float defaultH = (element.children.size() > 0) ? (availableH / static_cast<float>(element.children.size())) : 0.0f;
            float cursorY = contentY;

            for (auto& child : element.children)
            {
                float slotH = defaultH;
                float slotW = contentW;
                if (element.sizeToContent && child.hasContentSize)
                {
                    slotH = child.contentSizePixels.y + child.margin.y * 2.0f;
                }
                slotW = std::max(slotW, child.minSize.x);
                const float childX = contentX + child.margin.x;
                const float childY = cursorY + child.margin.y;
                const float childW = std::max(0.0f, slotW - child.margin.x * 2.0f);
                const float childH = std::max(0.0f, slotH - child.margin.y * 2.0f);
                layoutElement(child, childX, childY, childW, childH, measureText);
                cursorY += slotH + spacing;
            }
        }
    }
}

Vec2 UIManager::getAvailableViewportSize() const
{
    return m_availableViewportSize;
}

void UIManager::setAvailableViewportSize(const Vec2& size)
{
    if (m_availableViewportSize.x == size.x && m_availableViewportSize.y == size.y)
    {
        return;
    }
    m_availableViewportSize = size;
    for (auto& entry : m_widgets)
    {
        if (entry.widget)
        {
            entry.widget->markLayoutDirty();
        }
    }
}

void UIManager::registerUI(const std::string& id)
{
    m_entries.push_back(UIEntry{ id });
}

const std::vector<UIManager::UIEntry>& UIManager::getRegisteredUI() const
{
    return m_entries;
}

void UIManager::registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget)
{
    for (auto& entry : m_widgets)
    {
        if (entry.id == id)
        {
            entry.widget = widget;
            if (entry.widget)
            {
                entry.widget->markLayoutDirty();
            }
            return;
        }
    }
    m_widgets.push_back(WidgetEntry{ id, widget });
    if (widget)
    {
        widget->markLayoutDirty();
    }
}

const std::vector<UIManager::WidgetEntry>& UIManager::getRegisteredWidgets() const
{
    return m_widgets;
}

void UIManager::unregisterWidget(const std::string& id)
{
    m_widgets.erase(
        std::remove_if(m_widgets.begin(), m_widgets.end(),
            [&](const WidgetEntry& entry) { return entry.id == id; }),
        m_widgets.end());
}

void UIManager::updateLayouts(const std::function<Vec2(const std::string&, float)>& measureText)
{
    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }
        if (!entry.widget->isLayoutDirty())
        {
            continue;
        }

        Vec2 computedWidgetSize{};
        bool hasComputedWidgetSize = false;

        for (auto& element : entry.widget->getElementsMutable())
        {
            const Vec2 elementSize = measureElementSize(element, measureText);
            if (element.hasContentSize)
            {
                computedWidgetSize.x = std::max(computedWidgetSize.x, elementSize.x + element.margin.x * 2.0f);
                computedWidgetSize.y = std::max(computedWidgetSize.y, elementSize.y + element.margin.y * 2.0f);
                hasComputedWidgetSize = true;
            }
        }

        Vec2 widgetSize = entry.widget->getSizePixels();
        const Vec2 widgetOffset = entry.widget->getPositionPixels();
        const WidgetAnchor widgetAnchor = entry.widget->getAnchor();

        if (entry.widget->getFillX())
        {
            widgetSize.x = std::max(0.0f, m_availableViewportSize.x - widgetOffset.x);
        }
        if (entry.widget->getFillY())
        {
            widgetSize.y = std::max(0.0f, m_availableViewportSize.y - widgetOffset.y);
        }
        if (widgetSize.x <= 0.0f)
        {
            widgetSize.x = m_availableViewportSize.x;
        }
        if (widgetSize.y <= 0.0f)
        {
            widgetSize.y = hasComputedWidgetSize ? computedWidgetSize.y : m_availableViewportSize.y;
        }

        Vec2 widgetPosition = widgetOffset;
        switch (widgetAnchor)
        {
        case WidgetAnchor::TopRight:
            widgetPosition.x = m_availableViewportSize.x - widgetSize.x - widgetOffset.x;
            widgetPosition.y = widgetOffset.y;
            break;
        case WidgetAnchor::BottomLeft:
            widgetPosition.x = widgetOffset.x;
            widgetPosition.y = m_availableViewportSize.y - widgetSize.y - widgetOffset.y;
            break;
        case WidgetAnchor::BottomRight:
            widgetPosition.x = m_availableViewportSize.x - widgetSize.x - widgetOffset.x;
            widgetPosition.y = m_availableViewportSize.y - widgetSize.y - widgetOffset.y;
            break;
        default:
            widgetPosition.x = widgetOffset.x;
            widgetPosition.y = widgetOffset.y;
            break;
        }
        entry.widget->setComputedSizePixels(widgetSize, true);
        entry.widget->setComputedPositionPixels(widgetPosition, true);

        for (auto& element : entry.widget->getElementsMutable())
        {
            layoutElement(element, widgetPosition.x, widgetPosition.y, widgetSize.x, widgetSize.y, measureText);
        }

        entry.widget->setLayoutDirty(false);
    }

    if (!m_hasMousePosition)
    {
        return;
    }

    const auto pointInRect = [](const Vec2& pos, const Vec2& size, const Vec2& point)
        {
            return point.x >= pos.x && point.x <= (pos.x + size.x) &&
                point.y >= pos.y && point.y <= (pos.y + size.y);
        };

    const std::function<bool(WidgetElement&, bool)> updateHover =
        [&](WidgetElement& element, bool allowHover) -> bool
        {
            bool consumed = false;
            for (auto it = element.children.rbegin(); it != element.children.rend(); ++it)
            {
                if (updateHover(*it, allowHover && !consumed))
                {
                    consumed = true;
                }
            }

            bool isHover = false;
            if (allowHover && element.isHitTestable && element.hasComputedPosition && element.hasComputedSize)
            {
                isHover = pointInRect(element.computedPositionPixels, element.computedSizePixels, m_mousePosition);
            }

            if (element.isHovered != isHover)
            {
                element.isHovered = isHover;
                if (isHover)
                {
                    if (element.onHovered)
                    {
                        element.onHovered();
                    }
                }
                else
                {
                    if (element.onUnhovered)
                    {
                        element.onUnhovered();
                    }
                }
            }

            return consumed || isHover;
        };

    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }
        auto& elements = entry.widget->getElementsMutable();
        bool consumed = false;
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            if (updateHover(*it, !consumed))
            {
                consumed = true;
            }
        }
    }

    if (!m_pendingClick || m_pendingClickButton != SDL_BUTTON_LEFT)
    {
        return;
    }

    std::vector<WidgetEntry*> ordered;
    ordered.reserve(m_widgets.size());
    for (auto& entry : m_widgets)
    {
        ordered.push_back(&entry);
    }

    std::sort(ordered.begin(), ordered.end(), [](const WidgetEntry* a, const WidgetEntry* b)
        {
            const int za = (a && a->widget) ? a->widget->getZOrder() : 0;
            const int zb = (b && b->widget) ? b->widget->getZOrder() : 0;
            return za > zb;
        });

    const std::function<bool(WidgetElement&)> handleClick =
        [&](WidgetElement& element) -> bool
        {
            for (auto it = element.children.rbegin(); it != element.children.rend(); ++it)
            {
                if (handleClick(*it))
                {
                    return true;
                }
            }

            if (!element.isHitTestable || !element.hasComputedPosition || !element.hasComputedSize)
            {
                return false;
            }

            if (!pointInRect(element.computedPositionPixels, element.computedSizePixels, m_pendingClickPos))
            {
                return false;
            }

            element.isPressed = true;
            if (element.onClicked)
            {
                element.onClicked();
            }
            else if (!element.clickEvent.empty())
            {
                auto it = m_clickEvents.find(element.clickEvent);
                if (it != m_clickEvents.end() && it->second)
                {
                    it->second();
                }
            }
            return true;
        };

    for (auto* entry : ordered)
    {
        if (!entry || !entry->widget)
        {
            continue;
        }

        auto& elements = entry->widget->getElementsMutable();
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            if (handleClick(*it))
            {
                m_pendingClick = false;
                return;
            }
        }
    }

    m_pendingClick = false;
}

bool UIManager::handleMouseDown(const Vec2& screenPos, int button)
{
    if (button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    std::vector<const WidgetEntry*> ordered;
    ordered.reserve(m_widgets.size());
    for (const auto& entry : m_widgets)
    {
        ordered.push_back(&entry);
    }

    std::sort(ordered.begin(), ordered.end(), [](const WidgetEntry* a, const WidgetEntry* b)
        {
            const int za = (a && a->widget) ? a->widget->getZOrder() : 0;
            const int zb = (b && b->widget) ? b->widget->getZOrder() : 0;
            return za > zb;
        });

    const auto pointInRect = [](const Vec2& pos, const Vec2& size, const Vec2& point)
        {
            return point.x >= pos.x && point.x <= (pos.x + size.x) &&
                point.y >= pos.y && point.y <= (pos.y + size.y);
        };

    const std::function<bool(const WidgetElement&, const std::string&)> hitTestElement =
        [&](const WidgetElement& element, const std::string& widgetId) -> bool
        {
            if (!element.hasComputedPosition || !element.hasComputedSize)
            {
                return false;
            }

            for (auto it = element.children.rbegin(); it != element.children.rend(); ++it)
            {
                if (hitTestElement(*it, widgetId))
                {
                    return true;
                }
            }

            if (!pointInRect(element.computedPositionPixels, element.computedSizePixels, screenPos))
            {
                return false;
            }

            if (!element.isHitTestable)
            {
                return false;
            }

            if (element.onClicked)
            {
                element.onClicked();
            }
            else if (!element.clickEvent.empty())
            {
                auto it = m_clickEvents.find(element.clickEvent);
                if (it != m_clickEvents.end() && it->second)
                {
                    it->second();
                }
            }

            return true;
        };

    for (const auto* entry : ordered)
    {
        if (!entry || !entry->widget)
        {
            continue;
        }

        const auto& elements = entry->widget->getElements();
        for (auto it = elements.rbegin(); it != elements.rend(); ++it)
        {
            if (hitTestElement(*it, entry->id))
            {
                return true;
            }
        }
    }

    return false;
}

void UIManager::setMousePosition(const Vec2& screenPos)
{
    m_mousePosition = screenPos;
    m_hasMousePosition = true;
}

bool UIManager::hasClickEvent(const std::string& eventId) const
{
    return m_clickEvents.find(eventId) != m_clickEvents.end();
}

void UIManager::registerClickEvent(const std::string& eventId, std::function<void()> callback)
{
    if (eventId.empty())
    {
        return;
    }
    m_clickEvents[eventId] = std::move(callback);
}

bool UIManager::isPointerOverUI(const Vec2& screenPos) const
{
    const auto pointInRect = [](const Vec2& pos, const Vec2& size, const Vec2& point)
        {
            return point.x >= pos.x && point.x <= (pos.x + size.x) &&
                point.y >= pos.y && point.y <= (pos.y + size.y);
        };

    const std::function<bool(const WidgetElement&)> hitTestElement =
        [&](const WidgetElement& element) -> bool
        {
            if (element.isHitTestable && element.hasComputedPosition && element.hasComputedSize)
            {
                if (pointInRect(element.computedPositionPixels, element.computedSizePixels, screenPos))
                {
                    return true;
                }
            }

            for (const auto& child : element.children)
            {
                if (hitTestElement(child))
                {
                    return true;
                }
            }

            return false;
        };

    for (const auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }

        for (const auto& element : entry.widget->getElements())
        {
            if (hitTestElement(element))
            {
                return true;
            }
        }
    }

    return false;
}
