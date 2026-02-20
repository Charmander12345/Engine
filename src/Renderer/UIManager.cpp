#include "UIManager.h"

#include <algorithm>
#include <numeric>
#include <functional>
#include <cmath>
#include <filesystem>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <SDL3/SDL.h>
#include "../Logger/Logger.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Core/EngineLevel.h"
#include "../Core/ECS/ECS.h"
#include "UIWidgets/SeparatorWidget.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"

namespace
{
    UIManager* s_activeUIManager = nullptr;

    WidgetElement* FindElementById(WidgetElement& element, const std::string& id)
    {
        if (element.id == id)
        {
            return &element;
        }

        for (auto& child : element.children)
        {
            if (auto* match = FindElementById(child, id))
            {
                return match;
            }
        }
        return nullptr;
    }

    WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
    {
        for (auto& element : elements)
        {
            if (auto* match = FindElementById(element, id))
            {
                return match;
            }
        }
        return nullptr;
    }

    WidgetElement* FindFirstStackPanel(std::vector<WidgetElement>& elements)
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

    void LogWidgetElementIds(const WidgetElement& element, const std::string& widgetId)
    {
        if (!element.id.empty())
        {
            Logger::Instance().log(Logger::Category::UI,
                "UIManager widget=" + widgetId + " element id=" + element.id,
                Logger::LogLevel::INFO);
        }
        for (const auto& child : element.children)
        {
            LogWidgetElementIds(child, widgetId);
        }
    }

    void computeElementBounds(WidgetElement& element)
    {
        element.hasBounds = false;
        Vec2 minBounds{};
        Vec2 maxBounds{};
        bool hasAny = false;

        auto includeRect = [&](const Vec2& pos, const Vec2& size)
        {
            if (size.x <= 0.0f || size.y <= 0.0f)
            {
                return;
            }
            Vec2 rectMin{ pos.x, pos.y };
            Vec2 rectMax{ pos.x + size.x, pos.y + size.y };
            if (!hasAny)
            {
                minBounds = rectMin;
                maxBounds = rectMax;
                hasAny = true;
            }
            else
            {
                minBounds.x = std::min(minBounds.x, rectMin.x);
                minBounds.y = std::min(minBounds.y, rectMin.y);
                maxBounds.x = std::max(maxBounds.x, rectMax.x);
                maxBounds.y = std::max(maxBounds.y, rectMax.y);
            }
        };

        if (element.hasComputedPosition && element.hasComputedSize)
        {
            includeRect(element.computedPositionPixels, element.computedSizePixels);
        }

        for (auto& child : element.children)
        {
            computeElementBounds(child);
            if (child.hasBounds)
            {
                Vec2 childSize{ child.boundsMaxPixels.x - child.boundsMinPixels.x,
                    child.boundsMaxPixels.y - child.boundsMinPixels.y };
                includeRect(child.boundsMinPixels, childSize);
            }
        }

        if (hasAny)
        {
            element.boundsMinPixels = minBounds;
            element.boundsMaxPixels = maxBounds;
            element.hasBounds = true;
        }
    }

    void computeGridDimensions(size_t count, int& outColumns, int& outRows)
    {
        outColumns = 0;
        outRows = 0;
        if (count == 0)
        {
            return;
        }

        const float root = std::sqrt(static_cast<float>(count));
        outColumns = std::max(1, static_cast<int>(std::ceil(root)));
        outRows = static_cast<int>((count + static_cast<size_t>(outColumns) - 1) / static_cast<size_t>(outColumns));
    }

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
        case WidgetElementType::ProgressBar:
        case WidgetElementType::Slider:
        {
            const float width = (element.minSize.x > 0.0f) ? element.minSize.x : 140.0f;
            const float height = (element.minSize.y > 0.0f) ? element.minSize.y : 18.0f;
            size = Vec2{ width, height };
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
            if (!element.imagePath.empty() && element.text.empty())
            {
                const float imgDefault = 24.0f;
                size.x = std::max(size.x, imgDefault + element.padding.x * 2.0f);
                size.y = std::max(size.y, imgDefault + element.padding.y * 2.0f);
            }
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::EntryBar:
        {
            const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
            const float scale = fontSize / 48.0f;
            std::string display = element.value;
            if (element.isPassword)
            {
                display.assign(element.value.size(), '*');
            }
            const Vec2 textSize = (!display.empty() && measureText) ? measureText(display, scale) : Vec2{};
            size.x = textSize.x + element.padding.x * 2.0f;
            size.y = textSize.y + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::ColorPicker:
        {
            if (element.isCompact && !element.children.empty())
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
                size.x = std::max(element.minSize.x, 0.0f);
                size.y = std::max(element.minSize.y, 0.0f);
            }
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::CheckBox:
        {
            const float boxSize = 16.0f;
            const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
            const float scale = fontSize / 48.0f;
            const Vec2 textSize = (!element.text.empty() && measureText) ? measureText(element.text, scale) : Vec2{};
            size.x = element.padding.x + boxSize + 6.0f + textSize.x + element.padding.x;
            size.y = std::max(boxSize, textSize.y) + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::DropDown:
        {
            const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
            const float scale = fontSize / 48.0f;
            std::string display = element.text;
            if (display.empty() && element.selectedIndex >= 0 &&
                element.selectedIndex < static_cast<int>(element.items.size()))
            {
                display = element.items[static_cast<size_t>(element.selectedIndex)];
            }
            const Vec2 textSize = (!display.empty() && measureText) ? measureText(display, scale) : Vec2{};
            size.x = textSize.x + element.padding.x * 2.0f + 16.0f;
            size.y = textSize.y + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::TreeView:
        case WidgetElementType::TabView:
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
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::StackPanel:
        case WidgetElementType::Grid:
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

        if (element.type == WidgetElementType::Grid)
        {
            int columns = 0;
            int rows = 0;
            computeGridDimensions(childSizes.size(), columns, rows);

            float maxWidth = 0.0f;
            float maxHeight = 0.0f;
            for (const auto& childSize : childSizes)
            {
                maxWidth = std::max(maxWidth, childSize.x);
                maxHeight = std::max(maxHeight, childSize.y);
            }

            if (columns > 0 && rows > 0)
            {
                size.x = maxWidth * static_cast<float>(columns);
                size.y = maxHeight * static_cast<float>(rows);
                size.x += element.padding.x * 2.0f + element.padding.x * static_cast<float>(columns - 1);
                size.y += element.padding.y * 2.0f + element.padding.y * static_cast<float>(rows - 1);
            }
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        element.hasContentSize = false;
        element.contentSizePixels = {};
        return size;
    }

    Vec4 hsvToRgb(float hue, float saturation, float value)
    {
        hue = std::fmod(std::max(0.0f, hue), 1.0f) * 6.0f;
        const float c = value * saturation;
        const float x = c * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
        const float m = value - c;

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        if (hue < 1.0f)
        {
            r = c;
            g = x;
        }
        else if (hue < 2.0f)
        {
            r = x;
            g = c;
        }
        else if (hue < 3.0f)
        {
            g = c;
            b = x;
        }
        else if (hue < 4.0f)
        {
            g = x;
            b = c;
        }
        else if (hue < 5.0f)
        {
            r = x;
            b = c;
        }
        else
        {
            r = c;
            b = x;
        }

        return Vec4{ r + m, g + m, b + m, 1.0f };
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
            if (element.from.y > 0.0f)
            {
                height = std::max(0.0f, (parentY + parentH) - y0);
            }
            else
            {
                y0 = parentY;
                height = parentH;
            }
        }

        element.computedPositionPixels = { x0, y0 };
        element.computedSizePixels = { std::max(width, element.minSize.x), std::max(height, element.minSize.y) };
        element.hasComputedPosition = true;
        element.hasComputedSize = true;

        if (element.children.empty())
        {
            return;
        }

        float contentX = x0 + element.padding.x;
        float contentY = y0 + element.padding.y;
        const float contentW = std::max(0.0f, width - element.padding.x * 2.0f);
        const float contentH = std::max(0.0f, height - element.padding.y * 2.0f);

        if (element.type == WidgetElementType::ColorPicker && element.isCompact)
        {
            for (auto& child : element.children)
            {
                layoutElement(child, contentX, contentY, contentW, contentH, measureText);
            }
            return;
        }

        if (element.type == WidgetElementType::StackPanel && element.orientation == StackOrientation::Horizontal)
        {
            float spacing = element.padding.x;
            float totalSpacing = spacing * static_cast<float>(element.children.size() > 0 ? (element.children.size() - 1) : 0);
            float fixedWidth = 0.0f;
            size_t fillCount = 0;
            std::vector<float> fixedSlots;
            fixedSlots.reserve(element.children.size());
            for (auto& child : element.children)
            {
                if (child.fillX)
                {
                    fixedSlots.push_back(0.0f);
                    ++fillCount;
                    continue;
                }
                float childMinW = child.minSize.x;
                if (child.hasContentSize)
                {
                    childMinW = std::max(childMinW, child.contentSizePixels.x);
                }
                float slotW = childMinW + child.margin.x * 2.0f;
                fixedSlots.push_back(slotW);
                fixedWidth += slotW;
            }

            float availableW = std::max(0.0f, contentW - totalSpacing - fixedWidth);
            float fillSlotW = (fillCount > 0) ? (availableW / static_cast<float>(fillCount)) : 0.0f;
            float totalWidth = 0.0f;
            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                float slotW = fixedSlots[index];
                if (child.fillX)
                {
                    slotW = fillSlotW;
                }
                totalWidth += slotW;
                fixedSlots[index] = slotW;
            }
            if (!element.children.empty())
            {
                totalWidth += totalSpacing;
            }

            if (element.scrollable)
            {
                const float maxScroll = std::max(0.0f, totalWidth - contentW);
                element.scrollOffset = std::clamp(element.scrollOffset, 0.0f, maxScroll);
                contentX -= element.scrollOffset;
            }
            float cursorX = contentX;

            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                float slotW = fixedSlots[index];
                float slotH = child.fillY ? contentH : ((child.hasContentSize ? std::max(child.minSize.y, child.contentSizePixels.y) : child.minSize.y) + child.margin.y * 2.0f);
                const float childX = cursorX + child.margin.x;
                const float childY = contentY + child.margin.y;
                const float childW = std::max(0.0f, slotW - child.margin.x * 2.0f);
                const float childH = std::max(0.0f, slotH - child.margin.y * 2.0f);
                layoutElement(child, childX, childY, childW, childH, measureText);
                cursorX += slotW + spacing;
            }
        }
        else if (element.type == WidgetElementType::StackPanel)
        {
            float spacing = element.padding.y;
            float totalSpacing = spacing * static_cast<float>(element.children.size() > 0 ? (element.children.size() - 1) : 0);
            float fixedHeight = 0.0f;
            size_t fillCount = 0;
            std::vector<float> fixedSlots;
            fixedSlots.reserve(element.children.size());
            for (auto& child : element.children)
            {
                if (child.fillY)
                {
                    fixedSlots.push_back(0.0f);
                    ++fillCount;
                    continue;
                }
                float childMinH = child.minSize.y;
                if (child.hasContentSize)
                {
                    childMinH = std::max(childMinH, child.contentSizePixels.y);
                }
                float slotH = childMinH + child.margin.y * 2.0f;
                fixedSlots.push_back(slotH);
                fixedHeight += slotH;
            }

            float availableH = std::max(0.0f, contentH - totalSpacing - fixedHeight);
            float fillSlotH = (fillCount > 0) ? (availableH / static_cast<float>(fillCount)) : 0.0f;
            float totalHeight = 0.0f;
            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                float slotH = fixedSlots[index];
                if (child.fillY)
                {
                    slotH = fillSlotH;
                }
                totalHeight += slotH;
                fixedSlots[index] = slotH;
            }
            if (!element.children.empty())
            {
                totalHeight += totalSpacing;
            }

            if (element.scrollable)
            {
                const float maxScroll = std::max(0.0f, totalHeight - contentH);
                element.scrollOffset = std::clamp(element.scrollOffset, 0.0f, maxScroll);
                contentY -= element.scrollOffset;
            }

            float cursorY = contentY;

            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                float slotH = fixedSlots[index];
                float slotW = child.fillX ? contentW : ((child.hasContentSize ? std::max(child.minSize.x, child.contentSizePixels.x) : child.minSize.x) + child.margin.x * 2.0f);
                const float childX = contentX + child.margin.x;
                const float childY = cursorY + child.margin.y;
                const float childW = std::max(0.0f, slotW - child.margin.x * 2.0f);
                const float childH = std::max(0.0f, slotH - child.margin.y * 2.0f);
                layoutElement(child, childX, childY, childW, childH, measureText);
                cursorY += slotH + spacing;
            }
        }
        else if (element.type == WidgetElementType::Grid)
        {
            if (element.children.empty())
            {
                return;
            }

            const float spacingX = element.padding.x;
            const float spacingY = element.padding.y;

            // Determine tile size from the first child's minSize, or default to 80px
            const float tileSize = (element.children.front().minSize.x > 0.0f)
                ? std::max(element.children.front().minSize.x, element.children.front().minSize.y)
                : 80.0f;

            // Compute columns from available width
            int columns = std::max(1, static_cast<int>((contentW + spacingX) / (tileSize + spacingX)));
            int rows = static_cast<int>((element.children.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));

            // Square cell size, capped at tileSize
            const float cellSize = std::min(tileSize, std::max(0.0f, (contentW - spacingX * static_cast<float>(columns - 1)) / static_cast<float>(columns)));

            if (element.scrollable)
            {
                const float totalHeight = cellSize * static_cast<float>(rows) + spacingY * static_cast<float>(std::max(0, rows - 1));
                const float maxScroll = std::max(0.0f, totalHeight - contentH);
                element.scrollOffset = std::clamp(element.scrollOffset, 0.0f, maxScroll);
                contentY -= element.scrollOffset;
            }

            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                const int col = static_cast<int>(index % static_cast<size_t>(columns));
                const int row = static_cast<int>(index / static_cast<size_t>(columns));

                const float slotX = contentX + static_cast<float>(col) * (cellSize + spacingX);
                const float slotY = contentY + static_cast<float>(row) * (cellSize + spacingY);
                const float childX = slotX + child.margin.x;
                const float childY = slotY + child.margin.y;
                const float childW = std::max(0.0f, cellSize - child.margin.x * 2.0f);
                const float childH = std::max(0.0f, cellSize - child.margin.y * 2.0f);
                layoutElement(child, childX, childY, childW, childH, measureText);
            }
        }
        else if (element.type == WidgetElementType::TreeView || element.type == WidgetElementType::TabView)
        {
            float spacing = element.padding.y;
            float totalSpacing = spacing * static_cast<float>(element.children.size() > 0 ? (element.children.size() - 1) : 0);
            float fixedHeight = 0.0f;
            size_t fillCount = 0;
            std::vector<float> fixedSlots;
            fixedSlots.reserve(element.children.size());
            for (auto& child : element.children)
            {
                if (child.fillY)
                {
                    fixedSlots.push_back(0.0f);
                    ++fillCount;
                    continue;
                }
                float childMinH = child.minSize.y;
                if (child.hasContentSize)
                {
                    childMinH = std::max(childMinH, child.contentSizePixels.y);
                }
                float slotH = childMinH + child.margin.y * 2.0f;
                fixedSlots.push_back(slotH);
                fixedHeight += slotH;
            }

            float availableH = std::max(0.0f, contentH - totalSpacing - fixedHeight);
            float fillSlotH = (fillCount > 0) ? (availableH / static_cast<float>(fillCount)) : 0.0f;
            float totalHeight = 0.0f;
            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                float slotH = fixedSlots[index];
                if (child.fillY)
                {
                    slotH = fillSlotH;
                }
                totalHeight += slotH;
                fixedSlots[index] = slotH;
            }
            if (!element.children.empty())
            {
                totalHeight += totalSpacing;
            }

            if (element.scrollable)
            {
                const float maxScroll = std::max(0.0f, totalHeight - contentH);
                element.scrollOffset = std::clamp(element.scrollOffset, 0.0f, maxScroll);
                contentY -= element.scrollOffset;
            }

            float cursorY = contentY;

            for (size_t index = 0; index < element.children.size(); ++index)
            {
                auto& child = element.children[index];
                float slotH = fixedSlots[index];
                float slotW = child.fillX ? contentW : ((child.hasContentSize ? std::max(child.minSize.x, child.contentSizePixels.x) : child.minSize.x) + child.margin.x * 2.0f);
                const float childX = contentX + child.margin.x;
                const float childY = cursorY + child.margin.y;
                const float childW = std::max(0.0f, slotW - child.margin.x * 2.0f);
                const float childH = std::max(0.0f, slotH - child.margin.y * 2.0f);
                layoutElement(child, childX, childY, childW, childH, measureText);
                cursorY += slotH + spacing;
            }
        }
        else
        {
            // Default: lay out children with relative from/to positioning
            for (auto& child : element.children)
            {
                layoutElement(child, contentX, contentY, contentW, contentH, measureText);
            }
        }
    }
}

UIManager::WidgetEntry* UIManager::findWidgetEntry(const std::string& id)
{
    for (auto& entry : m_widgets)
    {
        if (entry.id == id)
        {
            return &entry;
        }
    }
    return nullptr;
}

const UIManager::WidgetEntry* UIManager::findWidgetEntry(const std::string& id) const
{
    for (const auto& entry : m_widgets)
    {
        if (entry.id == id)
        {
            return &entry;
        }
    }
    return nullptr;
}

UIManager* UIManager::GetActiveInstance()
{
    return s_activeUIManager;
}

void UIManager::SetActiveInstance(UIManager* instance)
{
    s_activeUIManager = instance;
}

UIManager::UIManager()
{
    SetActiveInstance(this);
    DiagnosticsManager::Instance().registerActiveLevelChangedCallback(
        [this](EngineLevel* level)
        {
            m_outlinerLevel = level;
            if (m_outlinerLevel)
            {
                m_outlinerLevel->registerEntityListChangedCallback([this]()
                    {
                        refreshWorldOutliner();
                    });
            }
            refreshWorldOutliner();
        });
}

void UIManager::refreshWorldOutliner()
{
    if (auto* entry = findWidgetEntry("WorldOutliner"))
    {
        if (entry->widget)
        {
            Logger::Instance().log(Logger::Category::UI, "Refreshing WorldOutliner widget.", Logger::LogLevel::INFO);
            populateOutlinerWidget(entry->widget);
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
    m_pointerCacheDirty = true;
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
    WidgetEntry* entry = findWidgetEntry(id);
    if (!entry)
    {
        m_widgets.push_back(WidgetEntry{ id, widget, m_nextWidgetRuntimeId++ });
        entry = &m_widgets.back();
    }
    else
    {
        entry->widget = widget;
    }

    if (entry->widget)
    {
        if (id == "WorldOutliner")
        {
            populateOutlinerWidget(entry->widget);
        }
        else if (id == "EntityDetails")
        {
            populateOutlinerDetails(m_outlinerSelectedEntity);
        }
        else if (id == "ContentBrowser")
        {
            populateContentBrowserWidget(entry->widget);
        }
        entry->widget->markLayoutDirty();
    }
    m_widgetOrderDirty = true;
    m_pointerCacheDirty = true;
    m_renderDirty = true;
}

void UIManager::showModalMessage(const std::string& message, std::function<void()> onClosed)
{
    if (message.empty())
    {
        return;
    }

    if (m_modalVisible)
    {
        m_modalQueue.push_back(ModalRequest{ message, std::move(onClosed) });
        return;
    }

    m_modalMessage = message;
    m_modalOnClosed = std::move(onClosed);
    ensureModalWidget();
    registerWidget("ModalMessage", m_modalWidget);
    m_modalVisible = true;
}

void UIManager::closeModalMessage()
{
    if (!m_modalVisible)
    {
        return;
    }
    unregisterWidget("ModalMessage");
    m_modalVisible = false;
    if (m_modalOnClosed)
    {
        auto callback = std::move(m_modalOnClosed);
        m_modalOnClosed = {};
        callback();
    }
    if (!m_modalQueue.empty())
    {
        ModalRequest next = std::move(m_modalQueue.front());
        m_modalQueue.erase(m_modalQueue.begin());
        m_modalMessage = std::move(next.message);
        m_modalOnClosed = std::move(next.onClosed);
        ensureModalWidget();
        registerWidget("ModalMessage", m_modalWidget);
        m_modalVisible = true;
    }
}

void UIManager::showToastMessage(const std::string& message, float durationSeconds)
{
    if (message.empty())
    {
        return;
    }

    ToastNotification toast{};
    toast.duration = std::max(0.1f, durationSeconds);
    toast.timer = toast.duration;
    toast.id = "ToastMessage." + std::to_string(m_nextToastId++);
    toast.widget = createToastWidget(message, toast.id);
    if (toast.widget)
    {
        registerWidget(toast.id, toast.widget);
        m_toasts.push_back(std::move(toast));
        updateToastStackLayout();
    }
}

void UIManager::updateNotifications(float deltaSeconds)
{
    m_notificationPollTimer += deltaSeconds;
    if (m_notificationPollTimer >= 1.0f)
    {
        m_notificationPollTimer = 0.0f;
        auto& diagnostics = DiagnosticsManager::Instance();
        const auto modalNotifications = diagnostics.consumeModalNotifications();
        for (const auto& message : modalNotifications)
        {
            showModalMessage(message);
        }
        const auto toastNotifications = diagnostics.consumeToastNotifications();
        for (const auto& toast : toastNotifications)
        {
            showToastMessage(toast.message, toast.durationSeconds);
        }
    }

    bool removed = false;
    for (auto it = m_toasts.begin(); it != m_toasts.end();)
    {
        it->timer = std::max(0.0f, it->timer - deltaSeconds);
        if (it->timer <= 0.0f)
        {
            unregisterWidget(it->id);
            it = m_toasts.erase(it);
            removed = true;
            continue;
        }
        ++it;
    }
    if (removed)
    {
        updateToastStackLayout();
    }
}

void UIManager::populateOutlinerWidget(const std::shared_ptr<Widget>& widget)
{
    if (!widget)
    {
		Logger::Instance().log(Logger::Category::UI, "WorldOutliner widget is null.", Logger::LogLevel::WARNING);
        return;
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    auto* level = m_outlinerLevel ? m_outlinerLevel : diagnostics.getActiveLevelSoft();
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
    if (listPanel->to.y <= listPanel->from.y)
    {
        listPanel->to.y = 0.98f;
    }
    listPanel->scrollable = true;
    listPanel->fillX = true;
    listPanel->fillY = false;
    listPanel->sizeToContent = false;
    listPanel->padding = Vec2{ 2.0f, 2.0f };

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

        WidgetElement button{};
        button.id = "Outliner.Entity." + std::to_string(entity);
        button.type = WidgetElementType::Button;
        button.from = Vec2{ 0.0f, 0.0f };
        button.to = Vec2{ 1.0f, 1.0f };
        button.fillX = true;
        button.text = label;
        button.font = "default.ttf";
        button.fontSize = 14.0f;
        button.textAlignH = TextAlignH::Center;
        button.textAlignV = TextAlignV::Center;
        button.padding = Vec2{ 6.0f, 4.0f };
        button.minSize = Vec2{ 0.0f, 24.0f };
        button.color = Vec4{ 0.12f, 0.12f, 0.14f, 0.9f };
        button.hoverColor = Vec4{ 0.18f, 0.18f, 0.22f, 0.95f };
        button.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
        button.shaderVertex = "button_vertex.glsl";
        button.shaderFragment = "button_fragment.glsl";
        button.isHitTestable = true;
        button.onClicked = [this, entity]()
            {
                m_outlinerSelectedEntity = entity;
                populateOutlinerDetails(entity);
            };
        button.runtimeOnly = true;
        listPanel->children.push_back(std::move(button));
        Logger::Instance().log(Logger::Category::UI,
            "WorldOutliner created button for entity " + std::to_string(entity) + " label=" + label,
            Logger::LogLevel::INFO);
        if (entity == m_outlinerSelectedEntity)
        {
            hasSelectedEntity = true;
        }
    }

    if (!hasSelectedEntity)
    {
        m_outlinerSelectedEntity = 0;
    }
    populateOutlinerDetails(m_outlinerSelectedEntity);

    widget->markLayoutDirty();
}

void UIManager::selectEntity(unsigned int entity)
{
    if (entity == m_outlinerSelectedEntity)
        return;
    m_outlinerSelectedEntity = entity;
    populateOutlinerDetails(entity);
}

void UIManager::populateOutlinerDetails(unsigned int entity)
{
    auto* detailsEntry = findWidgetEntry("EntityDetails");
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

    const auto formatVec3 = [](const float values[3])
        {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(2)
                << values[0] << ", " << values[1] << ", " << values[2];
            return stream.str();
        };

    const auto makeTextLine = [](const std::string& text) -> WidgetElement
        {
            WidgetElement line{};
            line.type = WidgetElementType::Text;
            line.text = text;
            line.font = "default.ttf";
            line.fontSize = 12.0f;
            line.textAlignH = TextAlignH::Left;
            line.textAlignV = TextAlignV::Center;
            line.textColor = Vec4{ 0.85f, 0.86f, 0.9f, 1.0f };
            line.fillX = true;
            line.minSize = Vec2{ 0.0f, 18.0f };
            line.runtimeOnly = true;
            return line;
        };

    const auto sanitizeId = [](const std::string& text)
        {
            std::string result;
            result.reserve(text.size());
            for (unsigned char c : text)
            {
                result.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
            }
            if (result.empty())
            {
                result = "Section";
            }
            return result;
        };

    const auto addSeparator = [&](const std::string& title, const std::vector<WidgetElement>& lines)
        {
            SeparatorWidget separator;
            separator.setId(sanitizeId(title));
            separator.setTitle(title);
            separator.setChildren(lines);
            detailsPanel->children.push_back(separator.toElement());
        };

    if (entity == 0)
    {
        detailsPanel->children.push_back(makeTextLine("Select an entity to see details."));
        detailsEntry->widget->markLayoutDirty();
        return;
    }

    auto& ecs = ECS::ECSManager::Instance();

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
        entityLines.push_back(makeTextLine("Name: " + nameValue));
    }
    addSeparator("Entity", entityLines);

    if (const auto* nameComponent = ecs.getComponent<ECS::NameComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Display Name: " + nameComponent->displayName));
        addSeparator("Name", lines);
    }

    if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Position: " + formatVec3(transform->position)));
        lines.push_back(makeTextLine("Rotation: " + formatVec3(transform->rotation)));
        lines.push_back(makeTextLine("Scale: " + formatVec3(transform->scale)));
        addSeparator("Transform", lines);
    }

    if (const auto* mesh = ecs.getComponent<ECS::MeshComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Asset Path: " + mesh->meshAssetPath));
        lines.push_back(makeTextLine("Asset Id: " + std::to_string(mesh->meshAssetId)));
        addSeparator("Mesh", lines);
    }

    if (const auto* material = ecs.getComponent<ECS::MaterialComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Asset Path: " + material->materialAssetPath));
        lines.push_back(makeTextLine("Asset Id: " + std::to_string(material->materialAssetId)));
        addSeparator("Material", lines);
    }

    if (const auto* light = ecs.getComponent<ECS::LightComponent>(entity))
    {
        std::string typeLabel = "Point";
        switch (light->type)
        {
        case ECS::LightComponent::LightType::Directional:
            typeLabel = "Directional";
            break;
        case ECS::LightComponent::LightType::Spot:
            typeLabel = "Spot";
            break;
        default:
            break;
        }
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Type: " + typeLabel));
        lines.push_back(makeTextLine("Color: " + formatVec3(light->color)));
        lines.push_back(makeTextLine("Intensity: " + std::to_string(light->intensity)));
        lines.push_back(makeTextLine("Range: " + std::to_string(light->range)));
        lines.push_back(makeTextLine("Spot Angle: " + std::to_string(light->spotAngle)));
        addSeparator("Light", lines);
    }

    if (const auto* camera = ecs.getComponent<ECS::CameraComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("FOV: " + std::to_string(camera->fov)));
        lines.push_back(makeTextLine("Near Clip: " + std::to_string(camera->nearClip)));
        lines.push_back(makeTextLine("Far Clip: " + std::to_string(camera->farClip)));
        addSeparator("Camera", lines);
    }

    if (const auto* physics = ecs.getComponent<ECS::PhysicsComponent>(entity))
    {
        std::string colliderType = "Box";
        switch (physics->colliderType)
        {
        case ECS::PhysicsComponent::ColliderType::Sphere:
            colliderType = "Sphere";
            break;
        case ECS::PhysicsComponent::ColliderType::Mesh:
            colliderType = "Mesh";
            break;
        default:
            break;
        }
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Collider: " + colliderType));
        lines.push_back(makeTextLine(std::string("Static: ") + (physics->isStatic ? "true" : "false")));
        lines.push_back(makeTextLine("Mass: " + std::to_string(physics->mass)));
        addSeparator("Physics", lines);
    }

    if (const auto* script = ecs.getComponent<ECS::ScriptComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Script Path: " + script->scriptPath));
        lines.push_back(makeTextLine("Asset Id: " + std::to_string(script->scriptAssetId)));
        addSeparator("Script", lines);
    }

    detailsEntry->widget->markLayoutDirty();
}

void UIManager::refreshContentBrowser(const std::string& subfolder)
{
    auto& log = Logger::Instance();
    log.log(Logger::Category::UI, "[ContentBrowser] refreshContentBrowser called, subfolder='" + subfolder + "' current m_contentBrowserPath='" + m_contentBrowserPath + "'", Logger::LogLevel::INFO);
    if (!subfolder.empty())
    {
        m_contentBrowserPath = subfolder;
    }
    bool found = false;
    for (auto& entry : m_widgets)
    {
        if (entry.id == "ContentBrowser" && entry.widget)
        {
            log.log(Logger::Category::UI, "[ContentBrowser] refreshContentBrowser: found widget entry, calling populateContentBrowserWidget", Logger::LogLevel::INFO);
            populateContentBrowserWidget(entry.widget);
            markAllWidgetsDirty();
            found = true;
            return;
        }
    }
    if (!found)
    {
        log.log(Logger::Category::UI, "[ContentBrowser] refreshContentBrowser: no 'ContentBrowser' widget found in m_widgets (count=" + std::to_string(m_widgets.size()) + ")", Logger::LogLevel::WARNING);
    }
}

// Returns the icon filename (inside Editor/Textures/) for a given AssetType.
static const char* iconForAssetType(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture:  return "texture.png";
    case AssetType::Material: return "material.png";
    case AssetType::Model2D:  return "model2d.png";
    case AssetType::Model3D:  return "model3d.png";
    case AssetType::Audio:    return "sound.png";
    case AssetType::Script:   return "script.png";
    case AssetType::Shader:   return "shader.png";
    default:                  return "texture.png";
    }
}

// Returns a tint color for each asset type icon.
static Vec4 iconTintForAssetType(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture:  return Vec4{ 0.45f, 0.65f, 1.00f, 1.0f }; // blue
    case AssetType::Material: return Vec4{ 1.00f, 0.60f, 0.25f, 1.0f }; // orange
    case AssetType::Audio:    return Vec4{ 1.00f, 0.35f, 0.35f, 1.0f }; // red
    case AssetType::Script:   return Vec4{ 0.40f, 0.90f, 0.40f, 1.0f }; // green
    case AssetType::Model2D:  return Vec4{ 0.55f, 0.85f, 0.95f, 1.0f }; // light cyan
    case AssetType::Model3D:  return Vec4{ 0.50f, 0.80f, 0.90f, 1.0f }; // cyan
    case AssetType::Shader:   return Vec4{ 0.75f, 0.50f, 1.00f, 1.0f }; // purple
    case AssetType::Level:    return Vec4{ 0.95f, 0.85f, 0.40f, 1.0f }; // gold
    case AssetType::Widget:   return Vec4{ 0.70f, 0.70f, 0.90f, 1.0f }; // lavender
    default:                  return Vec4{ 0.85f, 0.85f, 0.85f, 1.0f }; // light grey
    }
}

// Build a Button row for the tree panel.
// Icon is rendered as a child Image on the left, label as a child Text on the right.
// Both use from/to coordinates relative to the button; no layout pass needed.
static WidgetElement makeTreeRow(const std::string& id,
                                 const std::string& label,
                                 const std::string& iconPath,
                                 bool isFolder,
                                 int indentLevel = 0,
                                 const Vec4& iconTint = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f })
{
    WidgetElement btn{};
    btn.id = id;
    btn.type = WidgetElementType::Button;
    btn.fillX = true;
    btn.minSize = Vec2{ 0.0f, 22.0f };
    btn.color = Vec4{ 0.10f, 0.11f, 0.14f, 0.0f };   // transparent default
    btn.hoverColor = Vec4{ 0.22f, 0.24f, 0.30f, 0.95f };
    btn.shaderVertex = "button_vertex.glsl";
    btn.shaderFragment = "button_fragment.glsl";
    btn.isHitTestable = true;
    btn.runtimeOnly = true;
    // No own text/image — children handle rendering
    btn.text = "";

    const float indentFrac = static_cast<float>(indentLevel) * 0.04f; // 4% per level

    const float rowHeight = 22.0f;
    const float iconPad  = 0.1f;                                     // 10% vertical padding
    const float iconSize = rowHeight * (1.0f - 2.0f * iconPad);      // square icon in pixels

    // Icon child (left side, square — pixel-sized so it stays 1:1 regardless of button width)
    if (!iconPath.empty())
    {
        WidgetElement icon{};
        icon.id = id + ".Icon";
        icon.type = WidgetElementType::Image;
        icon.imagePath = iconPath;
        icon.color = iconTint;
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
        lbl.font = "default.ttf";
        lbl.fontSize = 13.0f;
        lbl.textAlignH = TextAlignH::Left;
        lbl.textAlignV = TextAlignV::Center;
        lbl.textColor = isFolder
            ? Vec4{ 0.90f, 0.85f, 0.55f, 1.0f }
            : Vec4{ 0.92f, 0.92f, 0.92f, 1.0f };
        lbl.from = Vec2{ textFrom, 0.0f };
        lbl.to   = Vec2{ 1.0f, 1.0f };
        lbl.padding = Vec2{ 3.0f, 2.0f };
        lbl.runtimeOnly = true;
        btn.children.push_back(std::move(lbl));
    }

    return btn;
}

// Build a grid tile for the Content Browser grid view.
// A tile is a small Button with an icon on top and a label below.
static WidgetElement makeGridTile(const std::string& id,
                                  const std::string& label,
                                  const std::string& iconPath,
                                  const Vec4& iconTint,
                                  bool isFolder)
{
    WidgetElement tile{};
    tile.id = id;
    tile.type = WidgetElementType::Button;
    tile.minSize = Vec2{ 80.0f, 80.0f };
    tile.color = Vec4{ 0.12f, 0.13f, 0.16f, 0.0f };
    tile.hoverColor = Vec4{ 0.22f, 0.24f, 0.30f, 0.85f };
    tile.shaderVertex = "button_vertex.glsl";
    tile.shaderFragment = "button_fragment.glsl";
    tile.isHitTestable = true;
    tile.runtimeOnly = true;
    tile.text = "";

    // Icon (top portion)
    if (!iconPath.empty())
    {
        WidgetElement icon{};
        icon.id = id + ".Icon";
        icon.type = WidgetElementType::Image;
        icon.imagePath = iconPath;
        icon.color = iconTint;
        icon.from = Vec2{ 0.15f, 0.05f };
        icon.to   = Vec2{ 0.85f, 0.62f };
        icon.runtimeOnly = true;
        tile.children.push_back(std::move(icon));
    }

    // Label (bottom portion)
    {
        WidgetElement lbl{};
        lbl.id = id + ".Label";
        lbl.type = WidgetElementType::Text;
        lbl.text = label;
        lbl.font = "default.ttf";
        lbl.fontSize = 11.0f;
        lbl.textAlignH = TextAlignH::Center;
        lbl.textAlignV = TextAlignV::Top;
        lbl.textColor = isFolder
            ? Vec4{ 0.90f, 0.85f, 0.55f, 1.0f }
            : Vec4{ 0.88f, 0.88f, 0.88f, 1.0f };
        lbl.from = Vec2{ 0.0f, 0.65f };
        lbl.to   = Vec2{ 1.0f, 1.0f };
        lbl.padding = Vec2{ 2.0f, 1.0f };
        lbl.runtimeOnly = true;
        tile.children.push_back(std::move(lbl));
    }

    return tile;
}

void UIManager::populateContentBrowserWidget(const std::shared_ptr<Widget>& widget)
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
        UIManager* self;
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
                    row.color = Vec4{ 0.20f, 0.25f, 0.35f, 0.9f };
                }

                row.onClicked = [uiMgr = self, newSub]()
                {
                    // Toggle expand/collapse
                    if (uiMgr->m_expandedFolders.count(newSub))
                    {
                        // Collapse only if already selected; otherwise just select
                        if (uiMgr->m_selectedBrowserFolder == newSub)
                        {
                            uiMgr->m_expandedFolders.erase(newSub);
                        }
                    }
                    else
                    {
                        uiMgr->m_expandedFolders.insert(newSub);
                    }
                    uiMgr->m_selectedBrowserFolder = newSub;
                    uiMgr->refreshContentBrowser();
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

                row.onClicked = [uiMgr = self, relPath, itemType]()
                {
                    uiMgr->showToastMessage("Asset: " + relPath, 2.5f);
                };
                treePanel->children.push_back(std::move(row));
                }
        }
    };

    Builder builder{ this, contentRoot, registry, treePanel };

    // "Content" root node
    {
        const std::string rootId = "ContentBrowser.Dir.Root";
        WidgetElement rootRow = makeTreeRow(rootId, "Content", "folder.png", true, 0, Vec4{ 0.95f, 0.85f, 0.35f, 1.0f });
        rootRow.isExpanded = true;
        if (m_selectedBrowserFolder.empty())
        {
            rootRow.color = Vec4{ 0.20f, 0.25f, 0.35f, 0.9f };
        }
        rootRow.onClicked = [this]()
        {
            m_selectedBrowserFolder.clear();
            refreshContentBrowser();
        };
        treePanel->children.push_back(std::move(rootRow));
    }

    builder.build(m_contentBrowserPath, 1);

    // ---- Populate Path Bar with breadcrumb buttons ----
    WidgetElement* pathBar = FindElementById(elements, "ContentBrowser.PathBar");
    if (pathBar)
    {
        pathBar->children.clear();

        // Back button (navigate up one level)
        {
            WidgetElement backBtn{};
            backBtn.id = "ContentBrowser.PathBar.Back";
            backBtn.type = WidgetElementType::Button;
            backBtn.text = "<";
            backBtn.font = "default.ttf";
            backBtn.fontSize = 13.0f;
            backBtn.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
            backBtn.textAlignH = TextAlignH::Center;
            backBtn.textAlignV = TextAlignV::Center;
            backBtn.minSize = Vec2{ 24.0f, 20.0f };
            backBtn.color = Vec4{ 0.14f, 0.15f, 0.19f, 0.9f };
            backBtn.hoverColor = Vec4{ 0.25f, 0.27f, 0.33f, 0.95f };
            backBtn.shaderVertex = "button_vertex.glsl";
            backBtn.shaderFragment = "button_fragment.glsl";
            backBtn.isHitTestable = true;
            backBtn.runtimeOnly = true;
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
                    refreshContentBrowser();
                }
            };
            pathBar->children.push_back(std::move(backBtn));
        }

        // Breadcrumb segments: Content > Folder > SubFolder > ...
        std::vector<std::pair<std::string, std::string>> crumbs; // (label, path)
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
                sep.font = "default.ttf";
                sep.fontSize = 12.0f;
                sep.textColor = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
                sep.textAlignH = TextAlignH::Center;
                sep.textAlignV = TextAlignV::Center;
                sep.minSize = Vec2{ 14.0f, 20.0f };
                sep.runtimeOnly = true;
                pathBar->children.push_back(std::move(sep));
            }

            const bool isActive = (crumbs[i].second == m_selectedBrowserFolder);
            WidgetElement crumbBtn{};
            crumbBtn.id = "ContentBrowser.PathBar.Crumb." + std::to_string(i);
            crumbBtn.type = WidgetElementType::Button;
            crumbBtn.text = crumbs[i].first;
            crumbBtn.font = "default.ttf";
            crumbBtn.fontSize = 12.0f;
            crumbBtn.textColor = isActive
                ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                : Vec4{ 0.7f, 0.7f, 0.7f, 1.0f };
            crumbBtn.textAlignH = TextAlignH::Center;
            crumbBtn.textAlignV = TextAlignV::Center;
            crumbBtn.minSize = Vec2{ 0.0f, 20.0f };
            crumbBtn.sizeToContent = true;
            crumbBtn.padding = Vec2{ 6.0f, 2.0f };
            crumbBtn.color = isActive
                ? Vec4{ 0.18f, 0.22f, 0.30f, 0.9f }
                : Vec4{ 0.12f, 0.13f, 0.17f, 0.0f };
            crumbBtn.hoverColor = Vec4{ 0.22f, 0.25f, 0.33f, 0.95f };
            crumbBtn.shaderVertex = "button_vertex.glsl";
            crumbBtn.shaderFragment = "button_fragment.glsl";
            crumbBtn.isHitTestable = true;
            crumbBtn.runtimeOnly = true;

            const std::string crumbPath = crumbs[i].second;
            crumbBtn.onClicked = [this, crumbPath]()
            {
                m_selectedBrowserFolder = crumbPath;
                if (!crumbPath.empty() && !m_expandedFolders.count(crumbPath))
                {
                    m_expandedFolders.insert(crumbPath);
                }
                refreshContentBrowser();
            };
            pathBar->children.push_back(std::move(crumbBtn));
        }
    }

    // ---- Populate Grid panel with contents of selected folder ----
    WidgetElement* gridPanel = FindElementById(elements, "ContentBrowser.Grid");
    if (gridPanel)
    {
        gridPanel->children.clear();
        gridPanel->scrollable = true;

        const std::string& gridFolder = m_selectedBrowserFolder;
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
                tile.onDoubleClicked = [uiMgr = this, folderSub]()
                {
                    uiMgr->m_selectedBrowserFolder = folderSub;
                    if (!uiMgr->m_expandedFolders.count(folderSub))
                    {
                        uiMgr->m_expandedFolders.insert(folderSub);
                    }
                    uiMgr->refreshContentBrowser();
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
                gridAssets.push_back({ e.name, e.path, e.type });
            }
        }
        std::sort(gridAssets.begin(), gridAssets.end(),
            [](const GridAssetItem& a, const GridAssetItem& b) { return a.name < b.name; });

        for (const auto& item : gridAssets)
        {
            const std::string iconFile = iconForAssetType(item.type);
            const std::string relPath = item.relPath;

            WidgetElement tile = makeGridTile(
                "ContentBrowser.GridAsset." + relPath,
                item.name, iconFile,
                iconTintForAssetType(item.type), false);

            tile.onDoubleClicked = [uiMgr = this, relPath]()
            {
                Logger::Instance().log(Logger::Category::UI,
                    "Content Browser: open asset '" + relPath + "'",
                    Logger::LogLevel::INFO);
                uiMgr->showToastMessage("Open: " + relPath, 2.5f);
            };

            gridPanel->children.push_back(std::move(tile));
        }
    }

    widget->markLayoutDirty();
}

const std::vector<UIManager::WidgetEntry>& UIManager::getRegisteredWidgets() const
{
    return m_widgets;
}

const std::vector<const UIManager::WidgetEntry*>& UIManager::getWidgetsOrderedByZ() const
{
    if (m_widgetOrderDirty)
    {
        m_widgetOrderCache.clear();
        m_widgetOrderCache.reserve(m_widgets.size());
        for (const auto& entry : m_widgets)
        {
            if (entry.widget)
            {
                m_widgetOrderCache.push_back(&entry);
            }
        }
        std::sort(m_widgetOrderCache.begin(), m_widgetOrderCache.end(), [](const WidgetEntry* a, const WidgetEntry* b)
        {
            const int za = (a && a->widget) ? a->widget->getZOrder() : 0;
            const int zb = (b && b->widget) ? b->widget->getZOrder() : 0;
            return za < zb;
        });
        m_widgetOrderDirty = false;
    }
    return m_widgetOrderCache;
}

void UIManager::unregisterWidget(const std::string& id)
{
    m_widgets.erase(
        std::remove_if(m_widgets.begin(), m_widgets.end(),
            [&](const WidgetEntry& entry) { return entry.id == id; }),
        m_widgets.end());
    m_widgetOrderDirty = true;
    m_pointerCacheDirty = true;
    m_renderDirty = true;
}

WidgetElement* UIManager::findElementById(const std::string& elementId)
{
    const std::function<WidgetElement*(WidgetElement&)> search =
        [&](WidgetElement& el) -> WidgetElement*
        {
            if (el.id == elementId)
            {
                return &el;
            }
            for (auto& child : el.children)
            {
                if (auto* found = search(child))
                {
                    return found;
                }
            }
            return nullptr;
        };

    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }
        for (auto& element : entry.widget->getElementsMutable())
        {
            if (auto* found = search(element))
            {
                return found;
            }
        }
    }
    return nullptr;
}

void UIManager::updateLayouts(const std::function<Vec2(const std::string&, float)>& measureText)
{
    bool anyDirty = false;
    for (const auto& entry : m_widgets)
    {
        if (entry.widget && entry.widget->isLayoutDirty())
        {
            anyDirty = true;
            break;
        }
    }

    if (!anyDirty)
    {
        return;
    }

    enum class DockSide
    {
        None,
        Top,
        Bottom,
        Left,
        Right
    };

    const auto getDockSide = [](const Widget& widget) -> DockSide
        {
            const bool fillX = widget.getFillX();
            const bool fillY = widget.getFillY();
            const WidgetAnchor anchor = widget.getAnchor();

            if (fillX && !fillY)
            {
                return (anchor == WidgetAnchor::BottomLeft || anchor == WidgetAnchor::BottomRight) ? DockSide::Bottom : DockSide::Top;
            }
            if (fillY && !fillX)
            {
                return (anchor == WidgetAnchor::TopRight || anchor == WidgetAnchor::BottomRight) ? DockSide::Right : DockSide::Left;
            }
            return DockSide::None;
        };

    std::vector<WidgetEntry*> orderedEntries;
    orderedEntries.reserve(m_widgets.size());
    std::vector<WidgetEntry*> topEntries;
    std::vector<WidgetEntry*> bottomEntries;
    std::vector<WidgetEntry*> leftEntries;
    std::vector<WidgetEntry*> rightEntries;
    std::vector<WidgetEntry*> otherEntries;

    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }
        switch (getDockSide(*entry.widget))
        {
        case DockSide::Top:
            topEntries.push_back(&entry);
            break;
        case DockSide::Bottom:
            bottomEntries.push_back(&entry);
            break;
        case DockSide::Left:
            leftEntries.push_back(&entry);
            break;
        case DockSide::Right:
            rightEntries.push_back(&entry);
            break;
        default:
            otherEntries.push_back(&entry);
            break;
        }
    }

    orderedEntries.insert(orderedEntries.end(), topEntries.begin(), topEntries.end());
    orderedEntries.insert(orderedEntries.end(), bottomEntries.begin(), bottomEntries.end());
    orderedEntries.insert(orderedEntries.end(), leftEntries.begin(), leftEntries.end());
    orderedEntries.insert(orderedEntries.end(), rightEntries.begin(), rightEntries.end());
    orderedEntries.insert(orderedEntries.end(), otherEntries.begin(), otherEntries.end());

    struct LayoutRect
    {
        float x{ 0.0f };
        float y{ 0.0f };
        float w{ 0.0f };
        float h{ 0.0f };
    };

    LayoutRect available{ 0.0f, 0.0f, m_availableViewportSize.x, m_availableViewportSize.y };

    for (auto* entryPtr : orderedEntries)
    {
        if (!entryPtr || !entryPtr->widget)
        {
            continue;
        }
        auto& widget = entryPtr->widget;

        bindClickEventsForWidget(widget);

        Vec2 computedWidgetSize{};
        bool hasComputedWidgetSize = false;

        for (auto& element : widget->getElementsMutable())
        {
            const Vec2 elementSize = measureElementSize(element, measureText);
            if (element.hasContentSize)
            {
                computedWidgetSize.x = std::max(computedWidgetSize.x, elementSize.x + element.margin.x * 2.0f);
                computedWidgetSize.y = std::max(computedWidgetSize.y, elementSize.y + element.margin.y * 2.0f);
                hasComputedWidgetSize = true;
            }
        }

        Vec2 widgetSize = widget->getSizePixels();
        const Vec2 widgetOffset = widget->getPositionPixels();
        const WidgetAnchor widgetAnchor = widget->getAnchor();

        if (widget->getFillX())
        {
            widgetSize.x = std::max(0.0f, available.w - widgetOffset.x);
        }
        if (widget->getFillY())
        {
            widgetSize.y = std::max(0.0f, available.h - widgetOffset.y);
        }
        if (widgetSize.x <= 0.0f)
        {
            widgetSize.x = available.w;
        }
        if (widgetSize.y <= 0.0f)
        {
            widgetSize.y = hasComputedWidgetSize ? computedWidgetSize.y : available.h;
        }

        Vec2 widgetPosition = widgetOffset;
        switch (widgetAnchor)
        {
        case WidgetAnchor::TopRight:
            widgetPosition.x = available.x + available.w - widgetSize.x - widgetOffset.x;
            widgetPosition.y = available.y + widgetOffset.y;
            break;
        case WidgetAnchor::BottomLeft:
            widgetPosition.x = available.x + widgetOffset.x;
            widgetPosition.y = available.y + available.h - widgetSize.y - widgetOffset.y;
            break;
        case WidgetAnchor::BottomRight:
            widgetPosition.x = available.x + available.w - widgetSize.x - widgetOffset.x;
            widgetPosition.y = available.y + available.h - widgetSize.y - widgetOffset.y;
            break;
        default:
            widgetPosition.x = available.x + widgetOffset.x;
            widgetPosition.y = available.y + widgetOffset.y;
            break;
        }
        widget->setComputedSizePixels(widgetSize, true);
        widget->setComputedPositionPixels(widgetPosition, true);

        for (auto& element : widget->getElementsMutable())
        {
            layoutElement(element, widgetPosition.x, widgetPosition.y, widgetSize.x, widgetSize.y, measureText);
        }

        for (auto& element : widget->getElementsMutable())
        {
            computeElementBounds(element);
        }

        switch (getDockSide(*widget))
        {
        case DockSide::Top:
        {
            const float consumed = widgetSize.y + widgetOffset.y;
            available.y += consumed;
            available.h = std::max(0.0f, available.h - consumed);
            break;
        }
        case DockSide::Bottom:
        {
            const float consumed = widgetSize.y + widgetOffset.y;
            available.h = std::max(0.0f, available.h - consumed);
            break;
        }
        case DockSide::Left:
        {
            const float consumed = widgetSize.x + widgetOffset.x;
            available.x += consumed;
            available.w = std::max(0.0f, available.w - consumed);
            break;
        }
        case DockSide::Right:
        {
            const float consumed = widgetSize.x + widgetOffset.x;
            available.w = std::max(0.0f, available.w - consumed);
            break;
        }
        default:
            break;
        }

        widget->setLayoutDirty(false);
    }

    {
        const auto* outlinerEntry = findWidgetEntry("WorldOutliner");
        auto* detailsEntry = findWidgetEntry("EntityDetails");
        const auto* contentBrowserEntry = findWidgetEntry("ContentBrowser");
        if (outlinerEntry && outlinerEntry->widget && outlinerEntry->widget->hasComputedPosition() &&
            detailsEntry && detailsEntry->widget)
        {
            const Vec2 outlinerPos = outlinerEntry->widget->getComputedPositionPixels();
            const Vec2 outlinerSize = outlinerEntry->widget->getComputedSizePixels();

            // Bottom limit: top edge of the ContentBrowser, or the bottom of the outliner
            float bottomLimit = outlinerPos.y + outlinerSize.y;
            if (contentBrowserEntry && contentBrowserEntry->widget && contentBrowserEntry->widget->hasComputedPosition())
            {
                bottomLimit = contentBrowserEntry->widget->getComputedPositionPixels().y;
            }

            const float splitRatio = 0.45f;
            const float detailsTop = outlinerPos.y + outlinerSize.y * splitRatio;
            const float detailsHeight = std::max(0.0f, bottomLimit - detailsTop);
            const Vec2 detailsPos{ outlinerPos.x, detailsTop };
            const Vec2 detailsSize{ outlinerSize.x, detailsHeight };

            detailsEntry->widget->setComputedPositionPixels(detailsPos, true);
            detailsEntry->widget->setComputedSizePixels(detailsSize, true);

            for (auto& element : detailsEntry->widget->getElementsMutable())
            {
                measureElementSize(element, measureText);
            }
            for (auto& element : detailsEntry->widget->getElementsMutable())
            {
                layoutElement(element, detailsPos.x, detailsPos.y, detailsSize.x, detailsSize.y, measureText);
            }
            for (auto& element : detailsEntry->widget->getElementsMutable())
            {
                computeElementBounds(element);
            }
            detailsEntry->widget->setLayoutDirty(false);
        }
    }

    m_pointerCacheDirty = true;

}

bool UIManager::needsLayoutUpdate() const
{
    for (const auto& entry : m_widgets)
    {
        if (entry.widget && entry.widget->isLayoutDirty())
        {
            return true;
        }
    }
    return false;
}

bool UIManager::handleMouseDown(const Vec2& screenPos, int button)
{
    if (button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    Logger::Instance().log(Logger::Category::UI,
        "MouseDown at (" + std::to_string(screenPos.x) + ", " + std::to_string(screenPos.y) + ")",
        Logger::LogLevel::INFO);

    WidgetElement* target = hitTest(screenPos, true);
    if (!target)
    {
        Logger::Instance().log(Logger::Category::UI, "Click miss at (" + std::to_string(screenPos.x) + ", " + std::to_string(screenPos.y) + ")", Logger::LogLevel::INFO);
        setFocusedEntry(nullptr);
        return false;
    }

    if (target->type == WidgetElementType::EntryBar)
    {
        setFocusedEntry(target);
        return true;
    }

    if (m_focusedEntry)
    {
        setFocusedEntry(nullptr);
    }

    target->isPressed = true;
    if (target->type == WidgetElementType::Button && !target->id.empty())
    {
        Logger::Instance().log(Logger::Category::UI, "Click: " + target->id, Logger::LogLevel::INFO);
    }
    const std::string outlinerPrefix = "Outliner.Entity.";
    if (target->id.rfind(outlinerPrefix, 0) == 0)
    {
        const char* start = target->id.c_str() + outlinerPrefix.size();
        char* end = nullptr;
        const unsigned long entityValue = std::strtoul(start, &end, 10);
        if (end && end != start)
        {
            m_outlinerSelectedEntity = static_cast<unsigned int>(entityValue);
            populateOutlinerDetails(m_outlinerSelectedEntity);
        }
    }
    const std::string separatorPrefix = "Separator.Toggle.";
    if (target->id.rfind(separatorPrefix, 0) == 0)
    {
        const std::string separatorId = target->id.substr(separatorPrefix.size());
        const std::string contentId = "Separator.Content." + separatorId;
        for (auto& entry : m_widgets)
        {
            if (!entry.widget)
                continue;
            auto& elements = entry.widget->getElementsMutable();
            if (auto* content = FindElementById(elements, contentId))
            {
                if (content->isCollapsed)
                {
                    content->children = content->cachedChildren;
                    content->isCollapsed = false;
                    if (target->text.size() >= 2 && target->text[0] == '>' && target->text[1] == ' ')
                    {
                        target->text = "v " + target->text.substr(2);
                    }
                }
                else
                {
                    content->cachedChildren = content->children;
                    content->children.clear();
                    content->isCollapsed = true;
                    if (target->text.size() >= 2 && target->text[0] == 'v' && target->text[1] == ' ')
                    {
                        target->text = "> " + target->text.substr(2);
                    }
                }
                entry.widget->markLayoutDirty();
                break;
            }
        }
    }
    if (target->type == WidgetElementType::ColorPicker && !target->isCompact)
    {
        const float relX = (screenPos.x - target->computedPositionPixels.x) / std::max(1.0f, target->computedSizePixels.x);
        const float relY = (screenPos.y - target->computedPositionPixels.y) / std::max(1.0f, target->computedSizePixels.y);
        const float hue = std::clamp(relX, 0.0f, 1.0f);
        const float value = std::clamp(1.0f - relY, 0.0f, 1.0f);
        target->color = hsvToRgb(hue, 1.0f, value);
        if (target->onColorChanged)
        {
            target->onColorChanged(target->color);
        }
    }
    if (target->type == WidgetElementType::CheckBox)
    {
        target->isChecked = !target->isChecked;
        if (target->onCheckedChanged)
        {
            target->onCheckedChanged(target->isChecked);
        }
        markAllWidgetsDirty();
    }
    if (target->type == WidgetElementType::DropDown)
    {
        if (target->isExpanded)
        {
            const float itemHeight = std::max(20.0f, target->computedSizePixels.y);
            const float dropY0 = target->computedPositionPixels.y + target->computedSizePixels.y;
            const float relY = screenPos.y - dropY0;
            if (relY >= 0.0f && screenPos.x >= target->computedPositionPixels.x &&
                screenPos.x <= target->computedPositionPixels.x + target->computedSizePixels.x)
            {
                const int clickedIndex = static_cast<int>(relY / itemHeight);
                if (clickedIndex >= 0 && clickedIndex < static_cast<int>(target->items.size()))
                {
                    target->selectedIndex = clickedIndex;
                    target->text = target->items[static_cast<size_t>(clickedIndex)];
                    if (target->onSelectionChanged)
                    {
                        target->onSelectionChanged(clickedIndex);
                    }
                }
            }
            target->isExpanded = false;
        }
        else
        {
            target->isExpanded = true;
        }
        markAllWidgetsDirty();
    }
    // Copy target data before callbacks — onClicked/onDoubleClicked may rebuild
    // the widget tree (e.g. refreshContentBrowser), invalidating the target pointer.
    const std::string targetId = target->id;
    const std::string targetClickEvent = target->clickEvent;
    const auto targetOnClicked = target->onClicked;
    const auto targetOnDoubleClicked = target->onDoubleClicked;

    if (targetOnClicked)
    {
        targetOnClicked();
    }

    // Double-click detection (uses local copies only)
    {
        const uint64_t now = SDL_GetTicks();
        const bool sameElement = (!targetId.empty() && targetId == m_lastClickedElementId);
        const bool withinTime = (now - m_lastClickTimeMs) < 400;
        if (sameElement && withinTime && targetOnDoubleClicked)
        {
            targetOnDoubleClicked();
            m_lastClickedElementId.clear();
            m_lastClickTimeMs = 0;
        }
        else
        {
            m_lastClickedElementId = targetId;
            m_lastClickTimeMs = now;
        }
    }

    if (!targetOnClicked)
    {
        std::string eventId = targetClickEvent;
        if (eventId.empty())
        {
            eventId = targetId;
        }

        if (!eventId.empty())
        {
            auto it = m_clickEvents.find(eventId);
            if (it != m_clickEvents.end() && it->second)
            {
                it->second();
            }
        }
    }

    m_renderDirty = true;
    return true;
}

bool UIManager::handleTextInput(const std::string& text)
{
    if (!m_focusedEntry || text.empty())
    {
        return false;
    }

    m_focusedEntry->value += text;
    markAllWidgetsDirty();
    return true;
}

bool UIManager::handleKeyDown(int key)
{
    if (!m_focusedEntry)
    {
        return false;
    }

    if (key == SDLK_BACKSPACE)
    {
        if (!m_focusedEntry->value.empty())
        {
            m_focusedEntry->value.pop_back();
            markAllWidgetsDirty();
        }
        return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
    {
        if (m_focusedEntry->onValueChanged)
        {
            m_focusedEntry->onValueChanged(m_focusedEntry->value);
        }
        return true;
    }
    if (key == SDLK_ESCAPE)
    {
        setFocusedEntry(nullptr);
        return true;
    }

    return false;
}

bool UIManager::handleScroll(const Vec2& screenPos, float delta)
{
    if (delta == 0.0f)
    {
        return false;
    }

    const auto pointInRect = [](const Vec2& pos, const Vec2& size, const Vec2& point)
        {
            return point.x >= pos.x && point.x <= (pos.x + size.x) &&
                point.y >= pos.y && point.y <= (pos.y + size.y);
        };

    const std::function<WidgetElement*(WidgetElement&)> findScrollable =
        [&](WidgetElement& element) -> WidgetElement*
        {
            for (auto it = element.children.rbegin(); it != element.children.rend(); ++it)
            {
                if (auto* hitChild = findScrollable(*it))
                {
                    return hitChild;
                }
            }

            if (!element.hasComputedPosition || !element.hasComputedSize)
            {
                return nullptr;
            }
            if (!element.scrollable || (element.type != WidgetElementType::StackPanel && element.type != WidgetElementType::Grid && element.type != WidgetElementType::TreeView))
            {
                return nullptr;
            }
            if (!pointInRect(element.computedPositionPixels, element.computedSizePixels, screenPos))
            {
                return nullptr;
            }
            return &element;
        };

    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }
        for (auto it = entry.widget->getElementsMutable().rbegin(); it != entry.widget->getElementsMutable().rend(); ++it)
        {
            if (auto* target = findScrollable(*it))
            {
                const float scrollStep = 30.0f;
                float maxScroll = 0.0f;
                const bool horizontal = (target->type == WidgetElementType::StackPanel &&
                    target->orientation == StackOrientation::Horizontal);
                if (target->hasContentSize && target->hasComputedSize)
                {
                    maxScroll = horizontal
                        ? std::max(0.0f, target->contentSizePixels.x - target->computedSizePixels.x)
                        : std::max(0.0f, target->contentSizePixels.y - target->computedSizePixels.y);
                }
                target->scrollOffset = std::clamp(target->scrollOffset - delta * scrollStep, 0.0f, maxScroll);
                entry.widget->markLayoutDirty();
                m_renderDirty = true;
                return true;
            }
        }
    }

    return false;
}

void UIManager::setMousePosition(const Vec2& screenPos)
{
    if (m_hasMousePosition && m_mousePosition.x == screenPos.x && m_mousePosition.y == screenPos.y)
    {
        return;
    }
    m_mousePosition = screenPos;
    m_hasMousePosition = true;
    updateHoverStates();
}

void UIManager::setFocusedEntry(WidgetElement* element)
{
    if (m_focusedEntry == element)
    {
        return;
    }

    if (m_focusedEntry)
    {
        m_focusedEntry->isFocused = false;
    }

    m_focusedEntry = element;
    if (m_focusedEntry)
    {
        m_focusedEntry->isFocused = true;
    }

    markAllWidgetsDirty();
}

void UIManager::markAllWidgetsDirty()
{
    for (auto& entry : m_widgets)
    {
        if (entry.widget)
        {
            entry.widget->markLayoutDirty();
        }
    }
    m_lastHoveredElement = nullptr;
    m_renderDirty = true;
}

bool UIManager::isRenderDirty() const
{
    return m_renderDirty;
}

void UIManager::clearRenderDirty()
{
    m_renderDirty = false;
}

void UIManager::ensureModalWidget()
{
    if (!m_modalWidget)
    {
        m_modalWidget = std::make_shared<Widget>();
        m_modalWidget->setName("ModalMessage");
        m_modalWidget->setAnchor(WidgetAnchor::TopLeft);
        m_modalWidget->setFillX(true);
        m_modalWidget->setFillY(true);
        m_modalWidget->setZOrder(10000);
    }

    WidgetElement overlay{};
    overlay.id = "Modal.Overlay";
    overlay.type = WidgetElementType::Panel;
    overlay.from = Vec2{ 0.0f, 0.0f };
    overlay.to = Vec2{ 1.0f, 1.0f };
    overlay.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.45f };
    overlay.isHitTestable = true;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.3f, 0.35f };
    panel.to = Vec2{ 0.7f, 0.65f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.color = Vec4{ 0.15f, 0.16f, 0.2f, 0.95f };
    panel.isHitTestable = false;
    panel.runtimeOnly = true;

    WidgetElement message{};
    message.id = "Modal.Text";
    message.type = WidgetElementType::Text;
    message.text = m_modalMessage;
    message.font = "default.ttf";
    message.fontSize = 18.0f;
    message.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    message.wrapText = true;
    message.fillX = true;
    message.fillY = true;
    message.minSize = Vec2{ 0.0f, 28.0f };
    message.runtimeOnly = true;

    WidgetElement closeButton{};
    closeButton.id = "Modal.Close";
    closeButton.type = WidgetElementType::Button;
    closeButton.text = "Close";
    closeButton.font = "default.ttf";
    closeButton.fontSize = 16.0f;
    closeButton.textAlignH = TextAlignH::Center;
    closeButton.textAlignV = TextAlignV::Center;
    closeButton.padding = Vec2{ 8.0f, 6.0f };
    closeButton.minSize = Vec2{ 0.0f, 32.0f };
    closeButton.color = Vec4{ 0.25f, 0.26f, 0.32f, 0.95f };
    closeButton.hoverColor = Vec4{ 0.35f, 0.36f, 0.42f, 0.98f };
    closeButton.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    closeButton.shaderVertex = "button_vertex.glsl";
    closeButton.shaderFragment = "button_fragment.glsl";
    closeButton.isHitTestable = true;
    closeButton.runtimeOnly = true;
    closeButton.onClicked = [this]()
        {
            closeModalMessage();
        };

    WidgetElement buttonRow{};
    buttonRow.id = "Modal.ButtonRow";
    buttonRow.type = WidgetElementType::StackPanel;
    buttonRow.from = Vec2{ 0.0f, 0.0f };
    buttonRow.to = Vec2{ 1.0f, 1.0f };
    buttonRow.fillX = true;
    buttonRow.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    buttonRow.minSize = Vec2{ 0.0f, 32.0f };
    buttonRow.orientation = StackOrientation::Horizontal;
    buttonRow.padding = Vec2{ 8.0f, 0.0f };
    buttonRow.runtimeOnly = true;

    WidgetElement spacerLeft{};
    spacerLeft.id = "Modal.ButtonSpacerLeft";
    spacerLeft.type = WidgetElementType::Panel;
    spacerLeft.fillX = true;
    spacerLeft.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerLeft.runtimeOnly = true;

    WidgetElement spacerRight{};
    spacerRight.id = "Modal.ButtonSpacerRight";
    spacerRight.type = WidgetElementType::Panel;
    spacerRight.fillX = true;
    spacerRight.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerRight.runtimeOnly = true;

    closeButton.minSize = Vec2{ 120.0f, 32.0f };
    buttonRow.children.clear();
    buttonRow.children.push_back(std::move(spacerLeft));
    buttonRow.children.push_back(std::move(closeButton));
    buttonRow.children.push_back(std::move(spacerRight));

    panel.children.clear();
    panel.children.push_back(std::move(message));
    panel.children.push_back(std::move(buttonRow));

    std::vector<WidgetElement> elements;
    elements.reserve(2);
    elements.push_back(std::move(overlay));
    elements.push_back(std::move(panel));
    m_modalWidget->setElements(std::move(elements));
    m_modalWidget->markLayoutDirty();
}

std::shared_ptr<Widget> UIManager::createToastWidget(const std::string& message, const std::string& name) const
{
    auto widget = std::make_shared<Widget>();
    widget->setName(name);
    widget->setAnchor(WidgetAnchor::BottomRight);
    widget->setPositionPixels(Vec2{ 20.0f, 20.0f });
    widget->setSizePixels(Vec2{ 320.0f, 70.0f });
    widget->setZOrder(9000);

    WidgetElement panel{};
    panel.id = name + ".Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.0f, 0.0f };
    panel.to = Vec2{ 1.0f, 1.0f };
    panel.padding = Vec2{ 12.0f, 10.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.92f };
    panel.isHitTestable = false;
    panel.runtimeOnly = true;

    WidgetElement messageElement{};
    messageElement.id = name + ".Text";
    messageElement.type = WidgetElementType::Text;
    messageElement.text = message;
    messageElement.font = "default.ttf";
    messageElement.fontSize = 14.0f;
    messageElement.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    messageElement.fillX = true;
    messageElement.minSize = Vec2{ 0.0f, 22.0f };
    messageElement.runtimeOnly = true;

    panel.children.clear();
    panel.children.push_back(std::move(messageElement));

    std::vector<WidgetElement> elements;
    elements.reserve(1);
    elements.push_back(std::move(panel));
    widget->setElements(std::move(elements));
    widget->markLayoutDirty();
    return widget;
}

void UIManager::updateToastStackLayout()
{
    const float spacing = 10.0f;
    Vec2 offset{ 20.0f, 20.0f };
    for (size_t index = 0; index < m_toasts.size(); ++index)
    {
        auto& toast = m_toasts[index];
        if (!toast.widget)
        {
            continue;
        }
        const Vec2 size = toast.widget->getSizePixels();
        Vec2 position = offset;
        position.y += static_cast<float>(index) * (size.y + spacing);
        toast.widget->setPositionPixels(position);
        toast.widget->setZOrder(9000 + static_cast<int>(index));
    }
}

bool UIManager::hasClickEvent(const std::string& eventId) const
{
    return m_clickEvents.find(eventId) != m_clickEvents.end();
}

void UIManager::updateHoverStates()
{
    if (!m_hasMousePosition)
    {
        return;
    }

    WidgetElement* newHovered = hitTest(m_mousePosition, false);

    // Unhover previous element if it changed
    if (m_lastHoveredElement && m_lastHoveredElement != newHovered)
    {
        m_lastHoveredElement->isHovered = false;
        if (m_lastHoveredElement->onUnhovered)
        {
            m_lastHoveredElement->onUnhovered();
        }
        m_renderDirty = true;
    }

    // Hover new element
    if (newHovered && newHovered->isHitTestable && newHovered != m_lastHoveredElement)
    {
        newHovered->isHovered = true;
        if (newHovered->onHovered)
        {
            newHovered->onHovered();
        }
        m_renderDirty = true;
    }

    m_lastHoveredElement = (newHovered && newHovered->isHitTestable) ? newHovered : nullptr;
}

void UIManager::registerClickEvent(const std::string& eventId, std::function<void()> callback)
{
    if (eventId.empty())
    {
        return;
    }
    m_clickEvents[eventId] = std::move(callback);

    // Re-bind existing widget elements that reference this event id
    for (auto& entry : m_widgets)
    {
        if (!entry.widget)
        {
            continue;
        }
        bindClickEventsForWidget(entry.widget);
    }
}

bool UIManager::isPointerOverUI(const Vec2& screenPos) const
{
    if (!m_pointerCacheDirty && m_hasPointerQueryPos &&
        m_lastPointerQueryPos.x == screenPos.x && m_lastPointerQueryPos.y == screenPos.y)
    {
        return m_lastPointerOverUI;
    }
    const auto pointInRect = [](const Vec2& pos, const Vec2& size, const Vec2& point)
        {
            return point.x >= pos.x && point.x <= (pos.x + size.x) &&
                point.y >= pos.y && point.y <= (pos.y + size.y);
        };
    const auto pointInBounds = [&](const WidgetElement& element, const Vec2& point)
        {
            if (!element.hasBounds)
            {
                return true;
            }
            return point.x >= element.boundsMinPixels.x && point.x <= element.boundsMaxPixels.x &&
                point.y >= element.boundsMinPixels.y && point.y <= element.boundsMaxPixels.y;
        };

    const std::function<bool(const WidgetElement&)> hitAny =
        [&](const WidgetElement& element) -> bool
        {
            if (!pointInBounds(element, screenPos))
            {
                return false;
            }
            if (element.hasComputedPosition && element.hasComputedSize)
            {
                if (pointInRect(element.computedPositionPixels, element.computedSizePixels, screenPos))
                {
                    return true;
                }
            }

            for (const auto& child : element.children)
            {
                if (hitAny(child))
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
            if (hitAny(element))
            {
                m_lastPointerQueryPos = screenPos;
                m_hasPointerQueryPos = true;
                m_lastPointerOverUI = true;
                m_pointerCacheDirty = false;
                return true;
            }
        }
    }
    m_lastPointerQueryPos = screenPos;
    m_hasPointerQueryPos = true;
    m_lastPointerOverUI = false;
    m_pointerCacheDirty = false;
    return false;
}

WidgetElement* UIManager::hitTest(const Vec2& screenPos, bool /*logDetails*/) const
{

    const auto pointInRect = [](const Vec2& pos, const Vec2& size, const Vec2& point)
        {
            return point.x >= pos.x && point.x <= (pos.x + size.x) &&
                point.y >= pos.y && point.y <= (pos.y + size.y);
        };
    const auto pointInBounds = [&](const WidgetElement& element, const Vec2& point)
        {
            if (!element.hasBounds)
            {
                return true;
            }
            return point.x >= element.boundsMinPixels.x && point.x <= element.boundsMaxPixels.x &&
                point.y >= element.boundsMinPixels.y && point.y <= element.boundsMaxPixels.y;
        };

    const std::function<WidgetElement*(const WidgetElement&)> testElement =
        [&](const WidgetElement& element) -> WidgetElement*
        {
            if (!pointInBounds(element, screenPos))
            {
                return nullptr;
            }
            for (auto it = element.children.rbegin(); it != element.children.rend(); ++it)
            {
                if (auto* hitChild = testElement(*it))
                {
                    return hitChild;
                }
            }

            if (!element.hasComputedPosition || !element.hasComputedSize)
            {
                return nullptr;
            }

            if (!element.isHitTestable)
            {
                return nullptr;
            }

            const bool inside = pointInRect(element.computedPositionPixels, element.computedSizePixels, screenPos);

            if (!inside && element.type == WidgetElementType::DropDown && element.isExpanded && !element.items.empty())
            {
                const float itemHeight = std::max(20.0f, element.computedSizePixels.y);
                const float dropHeight = itemHeight * static_cast<float>(element.items.size());
                const Vec2 dropPos{ element.computedPositionPixels.x, element.computedPositionPixels.y + element.computedSizePixels.y };
                const Vec2 dropSize{ element.computedSizePixels.x, dropHeight };
                if (pointInRect(dropPos, dropSize, screenPos))
                {
                    return const_cast<WidgetElement*>(&element);
                }
            }

            if (inside)
            {
                return const_cast<WidgetElement*>(&element);
            }

            return nullptr;
        };

    const auto& orderedAsc = getWidgetsOrderedByZ();

    for (auto it = orderedAsc.rbegin(); it != orderedAsc.rend(); ++it)
    {
        const auto* entry = *it;
        if (!entry || !entry->widget)
        {
            continue;
        }

        const auto& elements = entry->widget->getElements();
        for (auto eit = elements.rbegin(); eit != elements.rend(); ++eit)
        {
            if (auto* hit = testElement(*eit))
            {
                return hit;
            }
        }
    }

    return nullptr;
}

void UIManager::bindClickEventsForWidget(const std::shared_ptr<Widget>& widget)
{
    if (!widget)
    {
        return;
    }
    for (auto& element : widget->getElementsMutable())
    {
        bindClickEventsForElement(element);
    }
}

void UIManager::bindClickEventsForElement(WidgetElement& element)
{
    // Prefer explicit clickEvent; fall back to id for convenience.
    std::string eventId = element.clickEvent.empty() ? element.id : element.clickEvent;
    if (!eventId.empty())
    {
        auto it = m_clickEvents.find(eventId);
        if (it != m_clickEvents.end())
        {
            element.onClicked = it->second;
        }
    }

    for (auto& child : element.children)
    {
        bindClickEventsForElement(child);
    }
}
