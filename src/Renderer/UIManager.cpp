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
#include "../Core/UndoRedoManager.h"
#include "UIWidgets/SeparatorWidget.h"
#include "UIWidgets/DropdownButtonWidget.h"
#include "UIWidgets/EntryBarWidget.h"
#include "UIWidgets/SliderWidget.h"
#include "UIWidgets/CheckBoxWidget.h"
#include "UIWidgets/DropDownWidget.h"
#include "UIWidgets/ColorPickerWidget.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "Renderer.h"
#include "ViewportUIManager.h"
#include "EditorWindows/PopupWindow.h"
#include "../Landscape/LandscapeManager.h"

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
            // For scrollable elements, don't expand bounds with children
            // (they may be scrolled outside the visible area)
            if (!element.scrollable && child.hasBounds)
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
        case WidgetElementType::Label:
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
        case WidgetElementType::ToggleButton:
        case WidgetElementType::RadioButton:
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
                    Vec2 childSize = measureElementSize(child, measureText);
                    childSize.x = std::max(childSize.x, child.minSize.x);
                    childSize.y = std::max(childSize.y, child.minSize.y);
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
        case WidgetElementType::DropdownButton:
        {
            const float scale = (element.fontSize > 0.0f) ? (element.fontSize / 48.0f) : 14.0f / 48.0f;
            const Vec2 textSize = (!element.text.empty() && measureText) ? measureText(element.text, scale) : Vec2{};
            // Text + padding + arrow indicator space (12px)
            size.x = textSize.x + element.padding.x * 2.0f + 12.0f;
            size.y = textSize.y + element.padding.y * 2.0f;
            if (!element.imagePath.empty() && element.text.empty())
            {
                const float imgDefault = 24.0f;
                size.x = std::max(size.x, imgDefault + element.padding.x * 2.0f + 12.0f);
                size.y = std::max(size.y, imgDefault + element.padding.y * 2.0f);
            }
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
                Vec2 childSize = measureElementSize(child, measureText);
                childSize.x = std::max(childSize.x, child.minSize.x);
                childSize.y = std::max(childSize.y, child.minSize.y);
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
        case WidgetElementType::ScrollView:
        case WidgetElementType::Grid:
        case WidgetElementType::WrapBox:
        case WidgetElementType::UniformGrid:
        case WidgetElementType::SizeBox:
        case WidgetElementType::ScaleBox:
        case WidgetElementType::WidgetSwitcher:
        case WidgetElementType::Overlay:
        {
            childSizes.reserve(element.children.size());
            for (auto& child : element.children)
            {
                Vec2 childSize = measureElementSize(child, measureText);
                childSize.x = std::max(childSize.x, child.minSize.x);
                childSize.y = std::max(childSize.y, child.minSize.y);
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

        if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView)
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

        // WrapBox: children flow in rows/columns and wrap
        if (element.type == WidgetElementType::WrapBox)
        {
            // Measure assumes horizontal orientation by default (wraps into rows)
            float rowWidth = 0.0f;
            float rowHeight = 0.0f;
            float totalHeight = element.padding.y * 2.0f;
            float maxRowWidth = 0.0f;
            float spacing = element.spacing;
            bool firstInRow = true;
            // We don't know parent width during measure, so just report content size
            // as if all children were in a single row (actual wrapping happens at layout time)
            float widthSum = 0.0f;
            float maxHeight = 0.0f;
            for (const auto& cs : childSizes)
            {
                widthSum += cs.x;
                maxHeight = std::max(maxHeight, cs.y);
            }
            if (!childSizes.empty())
            {
                widthSum += spacing * static_cast<float>(childSizes.size() - 1);
                widthSum += element.padding.x * 2.0f;
                maxHeight += element.padding.y * 2.0f;
            }
            size = Vec2{ widthSum, maxHeight };
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // UniformGrid: all cells have equal size
        if (element.type == WidgetElementType::UniformGrid)
        {
            int cols = element.columns;
            int rowCount = element.rows;
            const int childCount = static_cast<int>(childSizes.size());
            if (cols <= 0 && rowCount <= 0)
            {
                cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(childCount)))));
                rowCount = (childCount + cols - 1) / cols;
            }
            else if (cols <= 0)
            {
                cols = (childCount + rowCount - 1) / rowCount;
            }
            else if (rowCount <= 0)
            {
                rowCount = (childCount + cols - 1) / cols;
            }
            float maxChildW = 0.0f;
            float maxChildH = 0.0f;
            for (const auto& cs : childSizes)
            {
                maxChildW = std::max(maxChildW, cs.x);
                maxChildH = std::max(maxChildH, cs.y);
            }
            float spacing = element.spacing;
            size.x = maxChildW * static_cast<float>(cols) + spacing * static_cast<float>(std::max(0, cols - 1)) + element.padding.x * 2.0f;
            size.y = maxChildH * static_cast<float>(rowCount) + spacing * static_cast<float>(std::max(0, rowCount - 1)) + element.padding.y * 2.0f;
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // SizeBox: single child with overridden size constraints
        if (element.type == WidgetElementType::SizeBox)
        {
            float childW = 0.0f;
            float childH = 0.0f;
            if (!childSizes.empty())
            {
                childW = childSizes[0].x;
                childH = childSizes[0].y;
            }
            size.x = (element.widthOverride > 0.0f) ? element.widthOverride : childW;
            size.y = (element.heightOverride > 0.0f) ? element.heightOverride : childH;
            size.x += element.padding.x * 2.0f;
            size.y += element.padding.y * 2.0f;
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // ScaleBox: single child, reported at child's natural size
        if (element.type == WidgetElementType::ScaleBox)
        {
            if (!childSizes.empty())
            {
                size = Vec2{ childSizes[0].x + element.padding.x * 2.0f,
                             childSizes[0].y + element.padding.y * 2.0f };
            }
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // WidgetSwitcher: size of the largest child (only one visible at a time)
        if (element.type == WidgetElementType::WidgetSwitcher)
        {
            float maxW = 0.0f;
            float maxH = 0.0f;
            for (const auto& cs : childSizes)
            {
                maxW = std::max(maxW, cs.x);
                maxH = std::max(maxH, cs.y);
            }
            size = Vec2{ maxW + element.padding.x * 2.0f, maxH + element.padding.y * 2.0f };
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // Overlay: size of the largest child (all stacked)
        if (element.type == WidgetElementType::Overlay)
        {
            float maxW = 0.0f;
            float maxH = 0.0f;
            for (const auto& cs : childSizes)
            {
                maxW = std::max(maxW, cs.x);
                maxH = std::max(maxH, cs.y);
            }
            size = Vec2{ maxW + element.padding.x * 2.0f, maxH + element.padding.y * 2.0f };
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        element.hasContentSize = false;
        element.contentSizePixels = {};
        return size;
    }

    // Recursively call measureElementSize on every element in the tree,
    // including children of Panel elements that measureElementSize itself
    // does not recurse into.  This ensures hasContentSize is set for all
    // elements so that content-based sizing works in layoutElement.
    void measureAllElements(WidgetElement& element,
        const std::function<Vec2(const std::string&, float)>& measureText)
    {
        measureElementSize(element, measureText);
        for (auto& child : element.children)
        {
            measureAllElements(child, measureText);
        }
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
            if ((element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView)
                && element.orientation == StackOrientation::Horizontal && baseH > 0.0f)
            {
                width = element.contentSizePixels.x;
                height = baseH;
            }
            else if ((element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView)
                && element.orientation == StackOrientation::Vertical && baseW > 0.0f)
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
        else if ((element.type == WidgetElementType::Text || element.type == WidgetElementType::Label
            || element.type == WidgetElementType::Button || element.type == WidgetElementType::ToggleButton
            || element.type == WidgetElementType::RadioButton || element.type == WidgetElementType::DropdownButton) && element.hasContentSize)
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
        if (element.sizeToContent && (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView) && element.to.x > element.from.x)
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

        if ((element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView)
            && element.orientation == StackOrientation::Horizontal)
        {
            float spacing = (element.spacing > 0.0f) ? element.spacing : element.padding.x;
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
        else if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView)
        {
            float spacing = (element.spacing > 0.0f) ? element.spacing : element.padding.y;
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

            if (element.scrollable || element.type == WidgetElementType::ScrollView)
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
        // ── WrapBox: children flow and wrap ──
        else if (element.type == WidgetElementType::WrapBox)
        {
            const float spacing = element.spacing;
            const bool horizontal = (element.orientation == StackOrientation::Horizontal);
            if (horizontal)
            {
                float cursorX = contentX;
                float cursorY = contentY;
                float rowMaxH = 0.0f;
                for (auto& child : element.children)
                {
                    float childW = child.hasContentSize ? std::max(child.minSize.x, child.contentSizePixels.x) : child.minSize.x;
                    float childH = child.hasContentSize ? std::max(child.minSize.y, child.contentSizePixels.y) : child.minSize.y;
                    childW += child.margin.x * 2.0f;
                    childH += child.margin.y * 2.0f;
                    // Wrap to next row if we exceed available width
                    if (cursorX + childW > contentX + contentW + 0.5f && cursorX > contentX + 0.5f)
                    {
                        cursorX = contentX;
                        cursorY += rowMaxH + spacing;
                        rowMaxH = 0.0f;
                    }
                    layoutElement(child, cursorX + child.margin.x, cursorY + child.margin.y,
                        std::max(0.0f, childW - child.margin.x * 2.0f),
                        std::max(0.0f, childH - child.margin.y * 2.0f), measureText);
                    cursorX += childW + spacing;
                    rowMaxH = std::max(rowMaxH, childH);
                }
            }
            else
            {
                float cursorX = contentX;
                float cursorY = contentY;
                float colMaxW = 0.0f;
                for (auto& child : element.children)
                {
                    float childW = child.hasContentSize ? std::max(child.minSize.x, child.contentSizePixels.x) : child.minSize.x;
                    float childH = child.hasContentSize ? std::max(child.minSize.y, child.contentSizePixels.y) : child.minSize.y;
                    childW += child.margin.x * 2.0f;
                    childH += child.margin.y * 2.0f;
                    if (cursorY + childH > contentY + contentH + 0.5f && cursorY > contentY + 0.5f)
                    {
                        cursorY = contentY;
                        cursorX += colMaxW + spacing;
                        colMaxW = 0.0f;
                    }
                    layoutElement(child, cursorX + child.margin.x, cursorY + child.margin.y,
                        std::max(0.0f, childW - child.margin.x * 2.0f),
                        std::max(0.0f, childH - child.margin.y * 2.0f), measureText);
                    cursorY += childH + spacing;
                    colMaxW = std::max(colMaxW, childW);
                }
            }
        }
        // ── UniformGrid: all cells equal size ──
        else if (element.type == WidgetElementType::UniformGrid)
        {
            const int childCount = static_cast<int>(element.children.size());
            if (childCount == 0) return;

            int cols = element.columns;
            int rowCount = element.rows;
            if (cols <= 0 && rowCount <= 0)
            {
                cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(childCount)))));
                rowCount = (childCount + cols - 1) / cols;
            }
            else if (cols <= 0)
            {
                cols = (childCount + rowCount - 1) / rowCount;
            }
            else if (rowCount <= 0)
            {
                rowCount = (childCount + cols - 1) / cols;
            }

            const float spacing = element.spacing;
            const float cellW = std::max(0.0f, (contentW - spacing * static_cast<float>(std::max(0, cols - 1))) / static_cast<float>(cols));
            const float cellH = std::max(0.0f, (contentH - spacing * static_cast<float>(std::max(0, rowCount - 1))) / static_cast<float>(rowCount));

            for (int i = 0; i < childCount; ++i)
            {
                auto& child = element.children[static_cast<size_t>(i)];
                const int col = i % cols;
                const int row = i / cols;
                const float slotX = contentX + static_cast<float>(col) * (cellW + spacing);
                const float slotY = contentY + static_cast<float>(row) * (cellH + spacing);
                layoutElement(child, slotX + child.margin.x, slotY + child.margin.y,
                    std::max(0.0f, cellW - child.margin.x * 2.0f),
                    std::max(0.0f, cellH - child.margin.y * 2.0f), measureText);
            }
        }
        // ── SizeBox: single child with size override ──
        else if (element.type == WidgetElementType::SizeBox)
        {
            if (!element.children.empty())
            {
                auto& child = element.children[0];
                float childW = (element.widthOverride > 0.0f) ? element.widthOverride : contentW;
                float childH = (element.heightOverride > 0.0f) ? element.heightOverride : contentH;
                layoutElement(child, contentX, contentY, childW, childH, measureText);
            }
        }
        // ── ScaleBox: single child, scaled to fit ──
        else if (element.type == WidgetElementType::ScaleBox)
        {
            if (!element.children.empty())
            {
                auto& child = element.children[0];
                float childNatW = child.hasContentSize ? std::max(child.minSize.x, child.contentSizePixels.x) : child.minSize.x;
                float childNatH = child.hasContentSize ? std::max(child.minSize.y, child.contentSizePixels.y) : child.minSize.y;
                if (childNatW <= 0.0f) childNatW = contentW;
                if (childNatH <= 0.0f) childNatH = contentH;

                float scale = 1.0f;
                switch (element.scaleMode)
                {
                case ScaleMode::Contain:
                    scale = std::min(contentW / childNatW, contentH / childNatH);
                    break;
                case ScaleMode::Cover:
                    scale = std::max(contentW / childNatW, contentH / childNatH);
                    break;
                case ScaleMode::Fill:
                    // Non-uniform: just use content area directly
                    layoutElement(child, contentX, contentY, contentW, contentH, measureText);
                    return;
                case ScaleMode::ScaleDown:
                    scale = std::min(1.0f, std::min(contentW / childNatW, contentH / childNatH));
                    break;
                case ScaleMode::UserSpecified:
                    scale = element.userScale;
                    break;
                }

                float scaledW = childNatW * scale;
                float scaledH = childNatH * scale;
                // Center the scaled child within the content area
                float offsetX = (contentW - scaledW) * 0.5f;
                float offsetY = (contentH - scaledH) * 0.5f;
                layoutElement(child, contentX + offsetX, contentY + offsetY, scaledW, scaledH, measureText);
            }
        }
        // ── WidgetSwitcher: only active child ──
        else if (element.type == WidgetElementType::WidgetSwitcher)
        {
            const int activeIdx = element.activeChildIndex;
            for (size_t i = 0; i < element.children.size(); ++i)
            {
                auto& child = element.children[i];
                if (static_cast<int>(i) == activeIdx)
                {
                    layoutElement(child, contentX, contentY, contentW, contentH, measureText);
                }
                else
                {
                    // Non-active children: still compute position/size but mark invisible for rendering
                    child.computedPositionPixels = { contentX, contentY };
                    child.computedSizePixels = { 0.0f, 0.0f };
                    child.hasComputedPosition = true;
                    child.hasComputedSize = true;
                }
            }
        }
        // ── Overlay: all children stacked, each aligned within the same area ──
        else if (element.type == WidgetElementType::Overlay)
        {
            for (auto& child : element.children)
            {
                float childW = contentW;
                float childH = contentH;
                float childX = contentX;
                float childY = contentY;

                // If child has content size and doesn't fill, use its natural size
                if (!child.fillX && child.hasContentSize)
                {
                    childW = std::max(child.minSize.x, child.contentSizePixels.x);
                    // Horizontal alignment
                    if (child.textAlignH == TextAlignH::Center)
                        childX = contentX + (contentW - childW) * 0.5f;
                    else if (child.textAlignH == TextAlignH::Right)
                        childX = contentX + contentW - childW;
                }
                if (!child.fillY && child.hasContentSize)
                {
                    childH = std::max(child.minSize.y, child.contentSizePixels.y);
                    // Vertical alignment
                    if (child.textAlignV == TextAlignV::Center)
                        childY = contentY + (contentH - childH) * 0.5f;
                    else if (child.textAlignV == TextAlignV::Bottom)
                        childY = contentY + contentH - childH;
                }

                layoutElement(child, childX, childY, childW, childH, measureText);
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
    m_levelChangedCallbackToken = DiagnosticsManager::Instance().registerActiveLevelChangedCallback(
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

UIManager::~UIManager()
{
    if (m_levelChangedCallbackToken != 0)
    {
        DiagnosticsManager::Instance().unregisterActiveLevelChangedCallback(m_levelChangedCallbackToken);
    }
    if (GetActiveInstance() == this)
    {
        SetActiveInstance(nullptr);
    }
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
    registerWidget(id, widget, std::string{});
}

void UIManager::registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget, const std::string& tabId)
{
    WidgetEntry* entry = findWidgetEntry(id);
    if (!entry)
    {
        m_widgets.push_back(WidgetEntry{ id, widget, m_nextWidgetRuntimeId++, tabId });
        entry = &m_widgets.back();
    }
    else
    {
        entry->widget = widget;
        entry->tabId = tabId;
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
        else if (id == "StatusBar")
        {
            refreshStatusBar();
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

void UIManager::showConfirmDialog(const std::string& message, std::function<void()> onConfirm, std::function<void()> onCancel)
{
    if (message.empty())
    {
        return;
    }

    auto confirmCb = std::make_shared<std::function<void()>>(std::move(onConfirm));
    auto cancelCb = std::make_shared<std::function<void()>>(std::move(onCancel));

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
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.25f, 0.32f };
    panel.to = Vec2{ 0.75f, 0.68f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.color = Vec4{ 0.15f, 0.16f, 0.2f, 0.95f };
    panel.hitTestMode = HitTestMode::DisabledSelf;
    panel.runtimeOnly = true;

    WidgetElement msgText{};
    msgText.id = "Modal.Text";
    msgText.type = WidgetElementType::Text;
    msgText.text = message;
    msgText.font = "default.ttf";
    msgText.fontSize = 16.0f;
    msgText.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    msgText.wrapText = true;
    msgText.fillX = true;
    msgText.fillY = true;
    msgText.minSize = Vec2{ 0.0f, 36.0f };
    msgText.runtimeOnly = true;

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
    spacerLeft.id = "Modal.SpacerL";
    spacerLeft.type = WidgetElementType::Panel;
    spacerLeft.fillX = true;
    spacerLeft.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerLeft.runtimeOnly = true;

    WidgetElement spacerMid{};
    spacerMid.id = "Modal.SpacerM";
    spacerMid.type = WidgetElementType::Panel;
    spacerMid.fillX = true;
    spacerMid.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerMid.runtimeOnly = true;

    WidgetElement spacerRight{};
    spacerRight.id = "Modal.SpacerR";
    spacerRight.type = WidgetElementType::Panel;
    spacerRight.fillX = true;
    spacerRight.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerRight.runtimeOnly = true;

    auto makeBtn = [](const std::string& id, const std::string& label, const Vec4& bgColor, const Vec4& hoverColor) {
        WidgetElement btn{};
        btn.id = id;
        btn.type = WidgetElementType::Button;
        btn.text = label;
        btn.font = "default.ttf";
        btn.fontSize = 14.0f;
        btn.textAlignH = TextAlignH::Center;
        btn.textAlignV = TextAlignV::Center;
        btn.padding = Vec2{ 8.0f, 6.0f };
        btn.minSize = Vec2{ 110.0f, 32.0f };
        btn.color = bgColor;
        btn.hoverColor = hoverColor;
        btn.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
        btn.shaderVertex = "button_vertex.glsl";
        btn.shaderFragment = "button_fragment.glsl";
        btn.hitTestMode = HitTestMode::Enabled;
        btn.runtimeOnly = true;
        return btn;
    };

    WidgetElement yesBtn = makeBtn("Modal.Yes", "Delete",
        Vec4{ 0.55f, 0.18f, 0.18f, 0.95f },
        Vec4{ 0.70f, 0.22f, 0.22f, 1.0f });
    yesBtn.onClicked = [this, confirmCb]()
    {
        closeModalMessage();
        if (*confirmCb) (*confirmCb)();
    };

    WidgetElement noBtn = makeBtn("Modal.No", "Cancel",
        Vec4{ 0.25f, 0.26f, 0.32f, 0.95f },
        Vec4{ 0.35f, 0.36f, 0.42f, 0.98f });
    noBtn.onClicked = [this, cancelCb]()
    {
        closeModalMessage();
        if (*cancelCb) (*cancelCb)();
    };

    buttonRow.children.push_back(std::move(spacerLeft));
    buttonRow.children.push_back(std::move(yesBtn));
    buttonRow.children.push_back(std::move(spacerMid));
    buttonRow.children.push_back(std::move(noBtn));
    buttonRow.children.push_back(std::move(spacerRight));

    panel.children.push_back(std::move(msgText));
    panel.children.push_back(std::move(buttonRow));

    std::vector<WidgetElement> elements;
    elements.reserve(2);
    elements.push_back(std::move(overlay));
    elements.push_back(std::move(panel));
    m_modalWidget->setElements(std::move(elements));
    m_modalWidget->markLayoutDirty();

    if (m_modalVisible)
    {
        unregisterWidget("ModalMessage");
    }
    registerWidget("ModalMessage", m_modalWidget);
    m_modalVisible = true;
    m_modalOnClosed = {};
}

void UIManager::showConfirmDialogWithCheckbox(const std::string& message, const std::string& checkboxLabel, bool checkedByDefault,
    std::function<void(bool checked)> onConfirm, std::function<void()> onCancel)
{
    if (message.empty())
    {
        return;
    }

    auto confirmCb = std::make_shared<std::function<void(bool)>>(std::move(onConfirm));
    auto cancelCb = std::make_shared<std::function<void()>>(std::move(onCancel));
    auto checkboxState = std::make_shared<bool>(checkedByDefault);

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
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.25f, 0.30f };
    panel.to = Vec2{ 0.75f, 0.70f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.color = Vec4{ 0.15f, 0.16f, 0.2f, 0.95f };
    panel.hitTestMode = HitTestMode::DisabledSelf;
    panel.runtimeOnly = true;

    WidgetElement msgText{};
    msgText.id = "Modal.Text";
    msgText.type = WidgetElementType::Text;
    msgText.text = message;
    msgText.font = "default.ttf";
    msgText.fontSize = 16.0f;
    msgText.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    msgText.wrapText = true;
    msgText.fillX = true;
    msgText.fillY = true;
    msgText.minSize = Vec2{ 0.0f, 36.0f };
    msgText.runtimeOnly = true;

    WidgetElement checkbox{};
    checkbox.id = "Modal.Checkbox";
    checkbox.type = WidgetElementType::CheckBox;
    checkbox.text = checkboxLabel;
    checkbox.font = "default.ttf";
    checkbox.fontSize = 13.0f;
    checkbox.isChecked = checkedByDefault;
    checkbox.fillX = true;
    checkbox.minSize = Vec2{ 0.0f, 26.0f };
    checkbox.padding = Vec2{ 2.0f, 2.0f };
    checkbox.color = Vec4{ 0.18f, 0.18f, 0.22f, 0.9f };
    checkbox.hoverColor = Vec4{ 0.24f, 0.24f, 0.30f, 1.0f };
    checkbox.fillColor = Vec4{ 0.30f, 0.55f, 0.95f, 1.0f };
    checkbox.textColor = Vec4{ 0.90f, 0.90f, 0.94f, 1.0f };
    checkbox.hitTestMode = HitTestMode::Enabled;
    checkbox.runtimeOnly = true;
    checkbox.onCheckedChanged = [checkboxState](bool checked)
    {
        *checkboxState = checked;
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
    spacerLeft.id = "Modal.SpacerL";
    spacerLeft.type = WidgetElementType::Panel;
    spacerLeft.fillX = true;
    spacerLeft.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerLeft.runtimeOnly = true;

    WidgetElement spacerMid{};
    spacerMid.id = "Modal.SpacerM";
    spacerMid.type = WidgetElementType::Panel;
    spacerMid.fillX = true;
    spacerMid.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerMid.runtimeOnly = true;

    WidgetElement spacerRight{};
    spacerRight.id = "Modal.SpacerR";
    spacerRight.type = WidgetElementType::Panel;
    spacerRight.fillX = true;
    spacerRight.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerRight.runtimeOnly = true;

    auto makeBtn = [](const std::string& id, const std::string& label, const Vec4& bgColor, const Vec4& hoverColor) {
        WidgetElement btn{};
        btn.id = id;
        btn.type = WidgetElementType::Button;
        btn.text = label;
        btn.font = "default.ttf";
        btn.fontSize = 14.0f;
        btn.textAlignH = TextAlignH::Center;
        btn.textAlignV = TextAlignV::Center;
        btn.padding = Vec2{ 8.0f, 6.0f };
        btn.minSize = Vec2{ 110.0f, 32.0f };
        btn.color = bgColor;
        btn.hoverColor = hoverColor;
        btn.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
        btn.shaderVertex = "button_vertex.glsl";
        btn.shaderFragment = "button_fragment.glsl";
        btn.hitTestMode = HitTestMode::Enabled;
        btn.runtimeOnly = true;
        return btn;
    };

    WidgetElement yesBtn = makeBtn("Modal.Yes", "Delete",
        Vec4{ 0.55f, 0.18f, 0.18f, 0.95f },
        Vec4{ 0.70f, 0.22f, 0.22f, 1.0f });
    yesBtn.onClicked = [this, confirmCb, checkboxState]()
    {
        closeModalMessage();
        if (*confirmCb) (*confirmCb)(*checkboxState);
    };

    WidgetElement noBtn = makeBtn("Modal.No", "Cancel",
        Vec4{ 0.25f, 0.26f, 0.32f, 0.95f },
        Vec4{ 0.35f, 0.36f, 0.42f, 0.98f });
    noBtn.onClicked = [this, cancelCb]()
    {
        closeModalMessage();
        if (*cancelCb) (*cancelCb)();
    };

    buttonRow.children.push_back(std::move(spacerLeft));
    buttonRow.children.push_back(std::move(yesBtn));
    buttonRow.children.push_back(std::move(spacerMid));
    buttonRow.children.push_back(std::move(noBtn));
    buttonRow.children.push_back(std::move(spacerRight));

    panel.children.push_back(std::move(msgText));
    panel.children.push_back(std::move(checkbox));
    panel.children.push_back(std::move(buttonRow));

    std::vector<WidgetElement> elements;
    elements.reserve(2);
    elements.push_back(std::move(overlay));
    elements.push_back(std::move(panel));
    m_modalWidget->setElements(std::move(elements));
    m_modalWidget->markLayoutDirty();

    if (m_modalVisible)
    {
        unregisterWidget("ModalMessage");
    }
    registerWidget("ModalMessage", m_modalWidget);
    m_modalVisible = true;
    m_modalOnClosed = {};
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
    // Detect when the asset registry becomes ready and refresh the Content Browser
    {
        const bool registryReady = DiagnosticsManager::Instance().isAssetRegistryReady();
        if (registryReady && !m_registryWasReady)
        {
            Logger::Instance().log(Logger::Category::UI, "Asset registry became ready, refreshing Content Browser.", Logger::LogLevel::INFO);
            refreshContentBrowser();
        }
        m_registryWasReady = registryReady;
    }

    // Detect ECS component changes and refresh EntityDetails for the selected entity
    {
        const uint64_t currentVersion = ECS::ECSManager::Instance().getComponentVersion();
        if (currentVersion != m_lastEcsComponentVersion)
        {
            m_lastEcsComponentVersion = currentVersion;
            if (m_outlinerSelectedEntity != 0)
            {
                populateOutlinerDetails(m_outlinerSelectedEntity);
            }
        }
    }

    // Detect asset registry changes (new assets created/imported) and refresh EntityDetails dropdowns
    {
        const uint64_t currentRegVer = AssetManager::Instance().getRegistryVersion();
        if (currentRegVer != m_lastRegistryVersion)
        {
            m_lastRegistryVersion = currentRegVer;
            if (m_outlinerSelectedEntity != 0)
            {
                populateOutlinerDetails(m_outlinerSelectedEntity);
            }
        }
    }

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
    // Limit list to the top portion; EntityDetails occupies the bottom half (splitRatio = 0.45)
    listPanel->to.y = 0.44f;
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
        button.hitTestMode = HitTestMode::Enabled;
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

    // Invalidate cached hover pointer – the old elements are destroyed.
    m_lastHoveredElement = nullptr;

    const auto makeTextLine = [](const std::string& text) -> WidgetElement
        {
            WidgetElement line{};
            line.type = WidgetElementType::Text;
            line.text = text;
            line.font = "default.ttf";
            line.fontSize = 13.0f;
            line.textAlignH = TextAlignH::Left;
            line.textAlignV = TextAlignV::Center;
            line.textColor = Vec4{ 0.85f, 0.86f, 0.9f, 1.0f };
            line.fillX = true;
            line.minSize = Vec2{ 0.0f, 20.0f };
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

    const auto addSeparator = [&](const std::string& title, const std::vector<WidgetElement>& lines,
        std::function<void()> onRemove = {})
        {
            SeparatorWidget separator;
            separator.setId(sanitizeId(title));
            separator.setTitle(title);
            separator.setChildren(lines);
            WidgetElement separatorEl = separator.toElement();

            if (onRemove)
            {
                // Find the header button (child index 1: divider=0, header=1, content=2)
                // and wrap it in a horizontal StackPanel with a remove button
                if (separatorEl.children.size() >= 2)
                {
                    WidgetElement originalHeader = std::move(separatorEl.children[1]);
                    originalHeader.fillX = true;

                    WidgetElement removeBtn{};
                    removeBtn.id = "Details.Remove." + sanitizeId(title);
                    removeBtn.type = WidgetElementType::Button;
                    removeBtn.text = "X";
                    removeBtn.font = "default.ttf";
                    removeBtn.fontSize = 11.0f;
                    removeBtn.textAlignH = TextAlignH::Center;
                    removeBtn.textAlignV = TextAlignV::Center;
                    removeBtn.minSize = Vec2{ 22.0f, 22.0f };
                    removeBtn.padding = Vec2{ 2.0f, 2.0f };
                    removeBtn.color = Vec4{ 0.45f, 0.15f, 0.15f, 0.9f };
                    removeBtn.hoverColor = Vec4{ 0.65f, 0.20f, 0.20f, 1.0f };
                    removeBtn.textColor = Vec4{ 0.95f, 0.80f, 0.80f, 1.0f };
                    removeBtn.shaderVertex = "button_vertex.glsl";
                    removeBtn.shaderFragment = "button_fragment.glsl";
                    removeBtn.hitTestMode = HitTestMode::Enabled;
                    removeBtn.runtimeOnly = true;

                    const std::string compTitle = title;
                    removeBtn.onClicked = [this, compTitle, onRemove]()
                    {
                        showConfirmDialog("Remove " + compTitle + " component?",
                            [onRemove]() { onRemove(); },
                            []() {});
                    };

                    WidgetElement headerRow{};
                    headerRow.id = "Details.HeaderRow." + sanitizeId(title);
                    headerRow.type = WidgetElementType::StackPanel;
                    headerRow.orientation = StackOrientation::Horizontal;
                    headerRow.fillX = true;
                    headerRow.sizeToContent = true;
                    headerRow.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                    headerRow.padding = Vec2{ 0.0f, 0.0f };
                    headerRow.runtimeOnly = true;
                    headerRow.children.push_back(std::move(originalHeader));
                    headerRow.children.push_back(std::move(removeBtn));

                    separatorEl.children[1] = std::move(headerRow);
                }
            }

            detailsPanel->children.push_back(std::move(separatorEl));
        };

    auto fmtF = [](float v) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };

    const auto makeFloatEntry = [&](const std::string& id, const std::string& label, float value,
        std::function<void(float)> onChange) -> WidgetElement
    {
        WidgetElement row{};
        row.type = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX = true;
        row.sizeToContent = true;
        row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.padding = Vec2{ 0.0f, 1.0f };
        row.runtimeOnly = true;

        WidgetElement lbl = makeTextLine(label);
        lbl.minSize = Vec2{ 100.0f, 22.0f };
        lbl.fillX = false;
        row.children.push_back(std::move(lbl));

        EntryBarWidget entry;
        entry.setValue(fmtF(value));
        entry.setFont("default.ttf");
        entry.setFontSize(12.0f);
        entry.setMinSize(Vec2{ 0.0f, 22.0f });
        entry.setPadding(Vec2{ 4.0f, 2.0f });
        entry.setOnValueChanged([onChange](const std::string& val) {
            try { onChange(std::stof(val)); } catch (...) {}
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
        });

        WidgetElement entryEl = entry.toElement();
        entryEl.id = id;
        entryEl.fillX = true;
        entryEl.runtimeOnly = true;
        row.children.push_back(std::move(entryEl));

        return row;
    };

    const auto makeVec3Row = [&](const std::string& idPrefix, const std::string& label, const float values[3],
        std::function<void(int, float)> onChange) -> WidgetElement
    {
        WidgetElement row{};
        row.type = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX = true;
        row.sizeToContent = true;
        row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.padding = Vec2{ 0.0f, 1.0f };
        row.runtimeOnly = true;

        WidgetElement lbl = makeTextLine(label);
        lbl.minSize = Vec2{ 100.0f, 22.0f };
        lbl.fillX = false;
        row.children.push_back(std::move(lbl));

        const char* axes[] = { "X", "Y", "Z" };
        const Vec4 axisColors[] = {
            { 0.22f, 0.10f, 0.10f, 0.9f },
            { 0.10f, 0.20f, 0.10f, 0.9f },
            { 0.10f, 0.10f, 0.22f, 0.9f },
        };

        for (int i = 0; i < 3; ++i)
        {
            EntryBarWidget entry;
            entry.setValue(fmtF(values[i]));
            entry.setFont("default.ttf");
            entry.setFontSize(12.0f);
            entry.setMinSize(Vec2{ 0.0f, 22.0f });
            entry.setPadding(Vec2{ 4.0f, 2.0f });
            entry.setBackgroundColor(axisColors[i]);

            int axis = i;
            entry.setOnValueChanged([onChange, axis](const std::string& val) {
                try { onChange(axis, std::stof(val)); } catch (...) {}
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });

            WidgetElement entryEl = entry.toElement();
            entryEl.id = idPrefix + "." + axes[i];
            entryEl.fillX = true;
            entryEl.runtimeOnly = true;
            row.children.push_back(std::move(entryEl));
        }

        return row;
    };

    const auto makeCheckBoxRow = [&](const std::string& id, const std::string& label, bool checked,
        std::function<void(bool)> onChange) -> WidgetElement
    {
        CheckBoxWidget cb;
        cb.setChecked(checked);
        cb.setLabel(label);
        cb.setFont("default.ttf");
        cb.setFontSize(12.0f);
        cb.setMinSize(Vec2{ 0.0f, 22.0f });
        cb.setPadding(Vec2{ 4.0f, 2.0f });
        cb.setOnCheckedChanged([onChange = std::move(onChange)](bool val) {
            onChange(val);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
        });

        WidgetElement el = cb.toElement();
        el.id = id;
        el.fillX = true;
        el.runtimeOnly = true;
        return el;
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
        WidgetElement nameLine = makeTextLine("Name: " + nameValue);
        nameLine.id = "Details.Entity.NameLabel";
        entityLines.push_back(std::move(nameLine));
    }
    addSeparator("Entity", entityLines);

    if (const auto* nameComponent = ecs.getComponent<ECS::NameComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        EntryBarWidget nameEntry;
        nameEntry.setValue(nameComponent->displayName);
        nameEntry.setFont("default.ttf");
        nameEntry.setFontSize(12.0f);
        nameEntry.setMinSize(Vec2{ 0.0f, 22.0f });
        nameEntry.setPadding(Vec2{ 6.0f, 3.0f });
        nameEntry.setOnValueChanged([this, entity](const std::string& val) {
            auto& ecs = ECS::ECSManager::Instance();
            if (auto* comp = ecs.getComponent<ECS::NameComponent>(entity))
            {
                ECS::NameComponent updated = *comp;
                updated.displayName = val;
                ecs.setComponent<ECS::NameComponent>(entity, updated);
            }
            // Update the entity header label in the details panel
            if (auto* lbl = findElementById("Details.Entity.NameLabel"))
            {
                lbl->text = "Name: " + (val.empty() ? std::string("<unnamed>") : val);
            }
            refreshWorldOutliner();
            auto& diag = DiagnosticsManager::Instance();
            if (auto* level = diag.getActiveLevelSoft())
            {
                level->setIsSaved(false);
            }
        });
        WidgetElement nameEl = nameEntry.toElement();
        nameEl.id = "Details.Name.Entry";
        nameEl.fillX = true;
        nameEl.runtimeOnly = true;
        lines.push_back(std::move(nameEl));

        addSeparator("Name", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::NameComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
            refreshWorldOutliner();
        });
    }

    if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        lines.push_back(makeVec3Row("Details.Transform.Pos", "Position", transform->position,
            [entity](int axis, float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::TransformComponent>(entity))
                {
                    ECS::TransformComponent updated = *comp;
                    updated.position[axis] = val;
                    ecs.setComponent<ECS::TransformComponent>(entity, updated);
                }
            }));

        lines.push_back(makeVec3Row("Details.Transform.Rot", "Rotation", transform->rotation,
            [entity](int axis, float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::TransformComponent>(entity))
                {
                    ECS::TransformComponent updated = *comp;
                    updated.rotation[axis] = val;
                    ecs.setComponent<ECS::TransformComponent>(entity, updated);
                }
            }));

        lines.push_back(makeVec3Row("Details.Transform.Scale", "Scale", transform->scale,
            [entity](int axis, float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::TransformComponent>(entity))
                {
                    ECS::TransformComponent updated = *comp;
                    updated.scale[axis] = val;
                    ecs.setComponent<ECS::TransformComponent>(entity, updated);
                }
            }));

        addSeparator("Transform", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::TransformComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
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
            dropdown.setFont("default.ttf");
            dropdown.setFontSize(12.0f);
            dropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            dropdown.setPadding(Vec2{ 6.0f, 3.0f });
            dropdown.setBackgroundColor(Vec4{ 0.16f, 0.18f, 0.24f, 0.95f });
            dropdown.setHoverColor(Vec4{ 0.22f, 0.26f, 0.34f, 1.0f });
            dropdown.setTextColor(Vec4{ 0.8f, 0.85f, 0.95f, 1.0f });

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

        addSeparator("Mesh", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::MeshComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
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
            dropdown.setFont("default.ttf");
            dropdown.setFontSize(12.0f);
            dropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            dropdown.setPadding(Vec2{ 6.0f, 3.0f });
            dropdown.setBackgroundColor(Vec4{ 0.16f, 0.18f, 0.24f, 0.95f });
            dropdown.setHoverColor(Vec4{ 0.22f, 0.26f, 0.34f, 1.0f });
            dropdown.setTextColor(Vec4{ 0.8f, 0.85f, 0.95f, 1.0f });

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

        addSeparator("Material", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::MaterialComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
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
            typeDropdown.setFont("default.ttf");
            typeDropdown.setFontSize(12.0f);
            typeDropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            typeDropdown.setPadding(Vec2{ 6.0f, 3.0f });
            typeDropdown.setOnSelectionChanged([entity](int idx) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::LightComponent>(entity))
                {
                    ECS::LightComponent updated = *comp;
                    updated.type = static_cast<ECS::LightComponent::LightType>(idx);
                    ecs.setComponent<ECS::LightComponent>(entity, updated);
                }
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Type");
            lbl.minSize = Vec2{ 90.0f, 20.0f };
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
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Color");
            lbl.minSize = Vec2{ 90.0f, 20.0f };
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            ColorPickerWidget cp;
            cp.setColor(Vec4{ light->color[0], light->color[1], light->color[2], 1.0f });
            cp.setCompact(true);
            cp.setMinSize(Vec2{ 0.0f, 20.0f });
            cp.setOnColorChanged([entity](const Vec4& c) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::LightComponent>(entity))
                {
                    ECS::LightComponent updated = *comp;
                    updated.color[0] = c.x;
                    updated.color[1] = c.y;
                    updated.color[2] = c.z;
                    ecs.setComponent<ECS::LightComponent>(entity, updated);
                }
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
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
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::LightComponent>(entity))
                {
                    ECS::LightComponent updated = *comp;
                    updated.intensity = val;
                    ecs.setComponent<ECS::LightComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Light.Range", "Range", light->range,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::LightComponent>(entity))
                {
                    ECS::LightComponent updated = *comp;
                    updated.range = val;
                    ecs.setComponent<ECS::LightComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Light.SpotAngle", "Spot Angle", light->spotAngle,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::LightComponent>(entity))
                {
                    ECS::LightComponent updated = *comp;
                    updated.spotAngle = val;
                    ecs.setComponent<ECS::LightComponent>(entity, updated);
                }
            }));

        addSeparator("Light", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::LightComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
        });
    }

    if (const auto* camera = ecs.getComponent<ECS::CameraComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        lines.push_back(makeFloatEntry("Details.Camera.FOV", "FOV", camera->fov,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CameraComponent>(entity))
                {
                    ECS::CameraComponent updated = *comp;
                    updated.fov = val;
                    ecs.setComponent<ECS::CameraComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Camera.NearClip", "Near Clip", camera->nearClip,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CameraComponent>(entity))
                {
                    ECS::CameraComponent updated = *comp;
                    updated.nearClip = val;
                    ecs.setComponent<ECS::CameraComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Camera.FarClip", "Far Clip", camera->farClip,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CameraComponent>(entity))
                {
                    ECS::CameraComponent updated = *comp;
                    updated.farClip = val;
                    ecs.setComponent<ECS::CameraComponent>(entity, updated);
                }
            }));

        lines.push_back(makeCheckBoxRow("Details.Camera.IsActive", "Active", camera->isActive,
            [entity](bool val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CameraComponent>(entity))
                {
                    ECS::CameraComponent updated = *comp;
                    updated.isActive = val;
                    ecs.setComponent<ECS::CameraComponent>(entity, updated);
                }
            }));

        addSeparator("Camera", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::CameraComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
        });
    }

    // ── Collision Component ──────────────────────────────────────────────
    if (const auto* collision = ecs.getComponent<ECS::CollisionComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        // Collider Type dropdown
        {
            int currentIdx = static_cast<int>(collision->colliderType);
            DropDownWidget colliderDropdown;
            colliderDropdown.setItems({ "Box", "Sphere", "Capsule", "Cylinder", "Mesh", "HeightField" });
            colliderDropdown.setSelectedIndex(currentIdx);
            colliderDropdown.setFont("default.ttf");
            colliderDropdown.setFontSize(12.0f);
            colliderDropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            colliderDropdown.setPadding(Vec2{ 6.0f, 3.0f });
            colliderDropdown.setOnSelectionChanged([entity](int idx) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CollisionComponent>(entity))
                {
                    ECS::CollisionComponent updated = *comp;
                    updated.colliderType = static_cast<ECS::CollisionComponent::ColliderType>(idx);
                    ecs.setComponent<ECS::CollisionComponent>(entity, updated);
                }
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Collider");
            lbl.minSize = Vec2{ 90.0f, 20.0f };
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
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CollisionComponent>(entity))
                {
                    ECS::CollisionComponent updated = *comp;
                    updated.colliderSize[axis] = val;
                    ecs.setComponent<ECS::CollisionComponent>(entity, updated);
                }
            }));

        lines.push_back(makeVec3Row("Details.Collision.Offset", "Offset", collision->colliderOffset,
            [entity](int axis, float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CollisionComponent>(entity))
                {
                    ECS::CollisionComponent updated = *comp;
                    updated.colliderOffset[axis] = val;
                    ecs.setComponent<ECS::CollisionComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Collision.Restitution", "Restitution", collision->restitution,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CollisionComponent>(entity))
                {
                    ECS::CollisionComponent updated = *comp;
                    updated.restitution = val;
                    ecs.setComponent<ECS::CollisionComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Collision.Friction", "Friction", collision->friction,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CollisionComponent>(entity))
                {
                    ECS::CollisionComponent updated = *comp;
                    updated.friction = val;
                    ecs.setComponent<ECS::CollisionComponent>(entity, updated);
                }
            }));

        lines.push_back(makeCheckBoxRow("Details.Collision.Sensor", "Is Sensor", collision->isSensor,
            [entity](bool val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::CollisionComponent>(entity))
                {
                    ECS::CollisionComponent updated = *comp;
                    updated.isSensor = val;
                    ecs.setComponent<ECS::CollisionComponent>(entity, updated);
                }
            }));

        addSeparator("Collision", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::CollisionComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
        });
    }

    // ── Physics Component ────────────────────────────────────────────────
    if (const auto* physics = ecs.getComponent<ECS::PhysicsComponent>(entity))
    {
        std::vector<WidgetElement> lines;

        // Motion Type dropdown
        {
            int currentIdx = static_cast<int>(physics->motionType);
            DropDownWidget motionDropdown;
            motionDropdown.setItems({ "Static", "Kinematic", "Dynamic" });
            motionDropdown.setSelectedIndex(currentIdx);
            motionDropdown.setFont("default.ttf");
            motionDropdown.setFontSize(12.0f);
            motionDropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            motionDropdown.setPadding(Vec2{ 6.0f, 3.0f });
            motionDropdown.setOnSelectionChanged([entity](int idx) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.motionType = static_cast<ECS::PhysicsComponent::MotionType>(idx);
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Motion Type");
            lbl.minSize = Vec2{ 90.0f, 20.0f };
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
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.mass = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Physics.GravityFactor", "Gravity Factor", physics->gravityFactor,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.gravityFactor = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Physics.LinearDamping", "Linear Damping", physics->linearDamping,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.linearDamping = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Physics.AngularDamping", "Angular Damping", physics->angularDamping,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.angularDamping = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Physics.MaxLinVel", "Max Linear Vel", physics->maxLinearVelocity,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.maxLinearVelocity = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeFloatEntry("Details.Physics.MaxAngVel", "Max Angular Vel", physics->maxAngularVelocity,
            [entity](float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.maxAngularVelocity = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        // Motion Quality dropdown
        {
            int currentIdx = static_cast<int>(physics->motionQuality);
            DropDownWidget mqDropdown;
            mqDropdown.setItems({ "Discrete", "LinearCast (CCD)" });
            mqDropdown.setSelectedIndex(currentIdx);
            mqDropdown.setFont("default.ttf");
            mqDropdown.setFontSize(12.0f);
            mqDropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            mqDropdown.setPadding(Vec2{ 6.0f, 3.0f });
            mqDropdown.setOnSelectionChanged([entity](int idx) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.motionQuality = static_cast<ECS::PhysicsComponent::MotionQuality>(idx);
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            });

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeTextLine("Motion Quality");
            lbl.minSize = Vec2{ 90.0f, 20.0f };
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
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.allowSleeping = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeVec3Row("Details.Physics.Velocity", "Velocity", physics->velocity,
            [entity](int axis, float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.velocity[axis] = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        lines.push_back(makeVec3Row("Details.Physics.AngularVel", "Angular Vel", physics->angularVelocity,
            [entity](int axis, float val) {
                auto& ecs = ECS::ECSManager::Instance();
                if (auto* comp = ecs.getComponent<ECS::PhysicsComponent>(entity))
                {
                    ECS::PhysicsComponent updated = *comp;
                    updated.angularVelocity[axis] = val;
                    ecs.setComponent<ECS::PhysicsComponent>(entity, updated);
                }
            }));

        addSeparator("Physics", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::PhysicsComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
        });
    }

    if (const auto* script = ecs.getComponent<ECS::ScriptComponent>(entity))
    {
        std::vector<WidgetElement> lines;
        lines.push_back(makeTextLine("Script Path: " + script->scriptPath));
        lines.push_back(makeTextLine("Asset Id: " + std::to_string(script->scriptAssetId)));

        // Dropdown to select a different script asset
        {
            DropdownButtonWidget dropdown;
            dropdown.setText(script->scriptPath.empty() ? "Select Script..." : script->scriptPath);
            dropdown.setFont("default.ttf");
            dropdown.setFontSize(12.0f);
            dropdown.setMinSize(Vec2{ 0.0f, 22.0f });
            dropdown.setPadding(Vec2{ 6.0f, 3.0f });
            dropdown.setBackgroundColor(Vec4{ 0.16f, 0.18f, 0.24f, 0.95f });
            dropdown.setHoverColor(Vec4{ 0.22f, 0.26f, 0.34f, 1.0f });
            dropdown.setTextColor(Vec4{ 0.8f, 0.85f, 0.95f, 1.0f });

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
            dropdownEl.id = "Details.Script.Dropdown";
            dropdownEl.fillX = true;
            dropdownEl.runtimeOnly = true;
            lines.push_back(std::move(dropdownEl));
        }

        addSeparator("Script", lines, [this, entity]() {
            ECS::ECSManager::Instance().removeComponent<ECS::ScriptComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
        });
    }

    // "Add Component" dropdown button
    {
        DropdownButtonWidget dropdown;
        dropdown.setText("+ Add Component");
        dropdown.setFont("default.ttf");
        dropdown.setFontSize(12.0f);
        dropdown.setMinSize(Vec2{ 0.0f, 26.0f });
        dropdown.setPadding(Vec2{ 8.0f, 4.0f });
        dropdown.setBackgroundColor(Vec4{ 0.14f, 0.30f, 0.14f, 0.95f });
        dropdown.setHoverColor(Vec4{ 0.18f, 0.42f, 0.18f, 1.0f });
        dropdown.setTextColor(Vec4{ 0.90f, 0.95f, 0.90f, 1.0f });

        struct CompOption { std::string label; bool present; std::function<void()> addFn; };
        std::vector<CompOption> options = {
            { "Transform", ecs.hasComponent<ECS::TransformComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::TransformComponent>(entity); } },
            { "Mesh", ecs.hasComponent<ECS::MeshComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::MeshComponent>(entity); } },
            { "Material", ecs.hasComponent<ECS::MaterialComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::MaterialComponent>(entity); } },
            { "Light", ecs.hasComponent<ECS::LightComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::LightComponent>(entity); } },
            { "Camera", ecs.hasComponent<ECS::CameraComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::CameraComponent>(entity); } },
            { "Collision", ecs.hasComponent<ECS::CollisionComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::CollisionComponent>(entity); } },
            { "Physics", ecs.hasComponent<ECS::PhysicsComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::PhysicsComponent>(entity); } },
            { "Script", ecs.hasComponent<ECS::ScriptComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::ScriptComponent>(entity); } },
            { "Name", ecs.hasComponent<ECS::NameComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::NameComponent>(entity); } },
        };

        for (const auto& opt : options)
        {
            if (!opt.present)
            {
                auto addFn = opt.addFn;
                dropdown.addItem(opt.label, [this, addFn, label = opt.label, entity]() {
                    addFn();
                    DiagnosticsManager::Instance().invalidateEntity(entity);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
                    populateOutlinerDetails(entity);
                    if (label == "Name")
                    {
                        refreshWorldOutliner();
                    }
                    showToastMessage("Added " + label + " component.", 2.0f);
                });
            }
        }

        WidgetElement dropdownEl = dropdown.toElement();
        dropdownEl.id = "Details.AddComponent";
        dropdownEl.fillX = true;
        dropdownEl.runtimeOnly = true;
        detailsPanel->children.push_back(std::move(dropdownEl));
    }

    detailsEntry->widget->markLayoutDirty();
}

void UIManager::applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity)
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

        Logger::Instance().log(Logger::Category::UI,
            "Applied mesh '" + assetPath + "' to entity " + std::to_string(entity),
            Logger::LogLevel::INFO);
        showToastMessage("Mesh assigned: " + assetPath, 2.5f);
        break;
    }
    case AssetType::Material:
    {
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
        Logger::Instance().log(Logger::Category::UI,
            "Applied material '" + assetPath + "' to entity " + std::to_string(entity),
            Logger::LogLevel::INFO);
        showToastMessage("Material assigned: " + assetPath, 2.5f);
        break;
    }
    case AssetType::Script:
    {
        ECS::ScriptComponent comp{};
        comp.scriptPath = assetPath;
        if (!ecs.hasComponent<ECS::ScriptComponent>(entity))
        {
            ecs.addComponent<ECS::ScriptComponent>(entity, comp);
        }
        else
        {
            ecs.setComponent<ECS::ScriptComponent>(entity, comp);
        }
        Logger::Instance().log(Logger::Category::UI,
            "Applied script '" + assetPath + "' to entity " + std::to_string(entity),
            Logger::LogLevel::INFO);
        showToastMessage("Script assigned: " + assetPath, 2.5f);
        break;
    }
    default:
        showToastMessage("Unsupported asset type for entity assignment.", 3.0f);
        return;
    }

    auto& diag = DiagnosticsManager::Instance();
    diag.invalidateEntity(entity);
    if (auto* level = diag.getActiveLevelSoft())
    {
        level->setIsSaved(false);
    }

    populateOutlinerDetails(entity);
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
            // If we are in rename mode, auto-focus the rename EntryBar so keyboard input reaches it
            if (m_renamingGridAsset)
            {
                if (auto* renameEntry = findElementById("ContentBrowser.RenameEntry"))
                {
                    setFocusedEntry(renameEntry);
                }
            }
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
    case AssetType::Widget:   return "texture.png";
    case AssetType::Skybox:   return "texture.png";
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
    case AssetType::Skybox:   return Vec4{ 0.40f, 0.75f, 0.95f, 1.0f }; // sky blue
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
    btn.hitTestMode = HitTestMode::Enabled;
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
    tile.hitTestMode = HitTestMode::Enabled;
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
            loadingRow.font = "default.ttf";
            loadingRow.fontSize = 13.0f;
            loadingRow.textColor = Vec4{ 0.6f, 0.6f, 0.65f, 1.0f };
            loadingRow.textAlignH = TextAlignH::Left;
            loadingRow.textAlignV = TextAlignV::Center;
            loadingRow.fillX = true;
            loadingRow.minSize = Vec2{ 0.0f, 24.0f };
            loadingRow.padding = Vec2{ 8.0f, 4.0f };
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

                // Make asset tree rows draggable
                row.isDraggable = true;
                row.dragPayload = std::to_string(static_cast<int>(itemType)) + "|" + relPath;

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
                shadersRow.color = Vec4{ 0.20f, 0.25f, 0.35f, 0.9f };
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
                refreshContentBrowser();
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
            backBtn.hitTestMode = HitTestMode::Enabled;
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

        // Import button
        {
            WidgetElement importBtn{};
            importBtn.id = "ContentBrowser.PathBar.Import";
            importBtn.type = WidgetElementType::Button;
            importBtn.text = "+ Import";
            importBtn.font = "default.ttf";
            importBtn.fontSize = 13.0f;
            importBtn.textColor = Vec4{ 0.9f, 0.95f, 1.0f, 1.0f };
            importBtn.textAlignH = TextAlignH::Center;
            importBtn.textAlignV = TextAlignV::Center;
            importBtn.minSize = Vec2{ 64.0f, 20.0f };
            importBtn.color = Vec4{ 0.15f, 0.35f, 0.55f, 0.95f };
            importBtn.hoverColor = Vec4{ 0.20f, 0.45f, 0.70f, 1.0f };
            importBtn.shaderVertex = "button_vertex.glsl";
            importBtn.shaderFragment = "button_fragment.glsl";
            importBtn.hitTestMode = HitTestMode::Enabled;
            importBtn.runtimeOnly = true;
            pathBar->children.push_back(std::move(importBtn));
        }

        // Rename button (enabled only when a grid asset is selected)
        {
            WidgetElement renameBtn{};
            renameBtn.id = "ContentBrowser.PathBar.Rename";
            renameBtn.type = WidgetElementType::Button;
            renameBtn.text = "Rename";
            renameBtn.font = "default.ttf";
            renameBtn.fontSize = 13.0f;
            renameBtn.textAlignH = TextAlignH::Center;
            renameBtn.textAlignV = TextAlignV::Center;
            renameBtn.minSize = Vec2{ 60.0f, 20.0f };
            renameBtn.shaderVertex = "button_vertex.glsl";
            renameBtn.shaderFragment = "button_fragment.glsl";
            renameBtn.hitTestMode = HitTestMode::Enabled;
            renameBtn.runtimeOnly = true;
            if (!m_selectedGridAsset.empty())
            {
                renameBtn.color = Vec4{ 0.35f, 0.30f, 0.15f, 0.95f };
                renameBtn.hoverColor = Vec4{ 0.50f, 0.42f, 0.18f, 1.0f };
                renameBtn.textColor = Vec4{ 0.95f, 0.92f, 0.8f, 1.0f };
                renameBtn.onClicked = [this]()
                {
                    if (!m_selectedGridAsset.empty())
                    {
                        m_renamingGridAsset = true;
                        m_renameOriginalPath = m_selectedGridAsset;
                        refreshContentBrowser();
                    }
                };
            }
            else
            {
                renameBtn.color = Vec4{ 0.18f, 0.18f, 0.20f, 0.6f };
                renameBtn.hoverColor = Vec4{ 0.18f, 0.18f, 0.20f, 0.6f };
                renameBtn.textColor = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
            }
            pathBar->children.push_back(std::move(renameBtn));
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

        // Handle Shaders virtual folder
        const bool isShadersView = (gridFolder == "__Shaders__");
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
                tile.onDoubleClicked = [uiMgr = this, folderSub]()
                {
                    uiMgr->m_selectedGridAsset.clear();
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

            // Highlight selected asset
            if (relPath == m_selectedGridAsset)
            {
                tile.color = Vec4{ 0.18f, 0.30f, 0.50f, 0.9f };
            }

            // Inline rename: replace the label child with an EntryBar
            if (m_renamingGridAsset && relPath == m_renameOriginalPath && tile.children.size() >= 2)
            {
                // Extract current name without extension as default value
                const std::string stem = std::filesystem::path(relPath).stem().string();
                WidgetElement entry{};
                entry.id = "ContentBrowser.RenameEntry";
                entry.type = WidgetElementType::EntryBar;
                entry.value = item.name;
                entry.font = "default.ttf";
                entry.fontSize = 11.0f;
                entry.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
                entry.color = Vec4{ 0.10f, 0.10f, 0.14f, 1.0f };
                entry.hoverColor = Vec4{ 0.14f, 0.14f, 0.18f, 1.0f };
                entry.from = Vec2{ 0.0f, 0.65f };
                entry.to = Vec2{ 1.0f, 1.0f };
                entry.padding = Vec2{ 2.0f, 1.0f };
                entry.hitTestMode = HitTestMode::Enabled;
                entry.isFocused = true;
                entry.runtimeOnly = true;
                entry.onValueChanged = [this, relPath](const std::string& newName)
                {
                    setFocusedEntry(nullptr);
                    m_renamingGridAsset = false;
                    if (!newName.empty() && newName != std::filesystem::path(relPath).stem().string())
                    {
                        if (AssetManager::Instance().renameAsset(relPath, newName))
                        {
                            showToastMessage("Renamed to: " + newName, 2.5f);
                            m_selectedGridAsset.clear();
                        }
                        else
                        {
                            showToastMessage("Rename failed.", 3.0f);
                        }
                    }
                    m_renameOriginalPath.clear();
                    refreshContentBrowser();
                };
                // Replace the label child (index 1) with the entry bar
                tile.children.back() = std::move(entry);
            }

            // Make asset tiles draggable
            tile.isDraggable = true;
            tile.dragPayload = std::to_string(static_cast<int>(item.type)) + "|" + relPath;

            // Single-click selects; double-click opens
            tile.onClicked = [uiMgr = this, relPath]()
            {
                uiMgr->m_selectedGridAsset = relPath;
                uiMgr->m_renamingGridAsset = false;
                uiMgr->m_renameOriginalPath.clear();
                uiMgr->refreshContentBrowser();
            };

            tile.onDoubleClicked = [uiMgr = this, relPath, assetType = item.type]()
            {
                if (assetType == AssetType::Model3D && uiMgr->getRenderer())
                {
                    uiMgr->getRenderer()->openMeshViewer(relPath);
                    return;
                }
                if (assetType == AssetType::Widget)
                {
                    uiMgr->openWidgetEditorPopup(relPath);
                    return;
                }
                Logger::Instance().log(Logger::Category::UI,
                    "Content Browser: open asset '" + relPath + "'",
                    Logger::LogLevel::INFO);
                uiMgr->showToastMessage("Open: " + relPath, 2.5f);
            };

            gridPanel->children.push_back(std::move(tile));
        }
        } // end else (not shaders)
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
                // Tab-scoped filtering: skip widgets not matching the active tab
                if (!entry.tabId.empty() && entry.tabId != m_activeTabId)
                    continue;
                m_widgetOrderCache.push_back(&entry);
            }
        }
        std::stable_sort(m_widgetOrderCache.begin(), m_widgetOrderCache.end(), [](const WidgetEntry* a, const WidgetEntry* b)
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

void UIManager::setActiveTabId(const std::string& tabId)
{
    if (m_activeTabId == tabId)
        return;
    m_activeTabId = tabId;
    m_widgetOrderDirty = true;
    m_pointerCacheDirty = true;
    m_renderDirty = true;
}

const std::string& UIManager::getActiveTabId() const
{
    return m_activeTabId;
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

    // Also check widget editor preview dirty flags
    if (!anyDirty)
    {
        for (const auto& [tabId, state] : m_widgetEditorStates)
        {
            if (state.previewDirty && state.editedWidget)
            {
                anyDirty = true;
                break;
            }
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
        // Tab-scoped filtering: skip widgets not matching the active tab
        if (!entry.tabId.empty() && entry.tabId != m_activeTabId)
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

        // EntityDetails is positioned by a second pass below (based on
        // WorldOutliner split).  If we lay it out here with the wrong
        // widget size the scroll offset gets clamped to a too-small
        // maxScroll every frame, preventing full scrolling.
        if (entryPtr->id == "EntityDetails")
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
            const float maxH = std::max(0.0f, available.h);
            widgetSize.y = hasComputedWidgetSize ? std::min(computedWidgetSize.y, maxH) : maxH;
        }

        Vec2 widgetPosition = widgetOffset;
        if (widget->isAbsolutePositioned())
        {
            widgetPosition = widgetOffset;
        }
        else
        {
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

    // Store the remaining rect as the viewport content area (used by the renderer
    // to set up projection / glViewport so the 3D scene is not distorted).
    m_viewportContentRect = Vec4{ available.x, available.y, available.w, available.h };

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

            bindClickEventsForWidget(detailsEntry->widget);

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

    // Layout widget editor preview widgets (not registered in UI system)
    for (auto& [tabId, state] : m_widgetEditorStates)
    {
        if (!state.editedWidget || tabId != m_activeTabId)
            continue;

        Vec2 wSize = state.editedWidget->getSizePixels();
        if (wSize.x <= 0.0f) wSize.x = 400.0f;
        if (wSize.y <= 0.0f) wSize.y = 300.0f;

        state.editedWidget->setComputedSizePixels(wSize, true);
        state.editedWidget->setComputedPositionPixels(Vec2{ 0.0f, 0.0f }, true);

        for (auto& element : state.editedWidget->getElementsMutable())
        {
            measureAllElements(element, measureText);
        }
        for (auto& element : state.editedWidget->getElementsMutable())
        {
            layoutElement(element, 0.0f, 0.0f, wSize.x, wSize.y, measureText);
        }
        for (auto& element : state.editedWidget->getElementsMutable())
        {
            computeElementBounds(element);
        }
        state.editedWidget->setLayoutDirty(false);
        state.previewDirty = true;  // trigger FBO re-render
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

    // Laptop mode: left-click on widget editor canvas starts panning
    {
        auto v = DiagnosticsManager::Instance().getState("LaptopMode");
        const bool laptopMode = v && *v == "1";
        if (laptopMode)
        {
            if (auto* weState = getActiveWidgetEditorState())
            {
                if (isOverWidgetEditorCanvas(screenPos))
                {
                    weState->isPanning = true;
                    weState->panStartMouse = screenPos;
                    weState->panStartOffset = weState->panOffset;
                    return true;
                }
            }
        }
    }

    // Non-laptop: left-click on widget editor canvas selects elements
    if (selectWidgetEditorElementAtPos(screenPos))
    {
        return true;
    }

    // Cancel any active drag on fresh mouse down
    if (m_dragging)
    {
        cancelDrag();
    }

    WidgetElement* target = hitTest(screenPos, true);

    // Dismiss dropdown menu on click outside
    if (m_dropdownVisible)
    {
        const bool clickedInsideMenu = target && target->id.rfind("Dropdown.", 0) == 0;
        const bool clickedSourceButton = target && target->type == WidgetElementType::DropdownButton;
        if (!clickedInsideMenu && !clickedSourceButton)
        {
            closeDropdownMenu();
            if (!target)
            {
                return true; // consume click that dismissed the menu
            }
        }
    }

    // Check if this element is draggable — set up pending drag
    if (target && target->isDraggable && !target->dragPayload.empty())
    {
        m_dragPending = true;
        m_dragStartPos = screenPos;
        m_dragPayload = target->dragPayload;
        m_dragSourceId = target->id;
        m_dragLabel = target->text;
    }
    else
    {
        m_dragPending = false;
    }

    if (!target)
    {
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
        // UTF-8: ▾ = \xe2\x96\xbe, ▸ = \xe2\x96\xb8
        const std::string expandedPrefix = std::string("\xe2\x96\xbe") + "  ";
        const std::string collapsedPrefix = std::string("\xe2\x96\xb8") + "  ";
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
                    if (target->text.rfind(collapsedPrefix, 0) == 0)
                    {
                        target->text = expandedPrefix + target->text.substr(collapsedPrefix.size());
                    }
                    else if (target->text.size() >= 2 && target->text[0] == '>' && target->text[1] == ' ')
                    {
                        target->text = expandedPrefix + target->text.substr(2);
                    }
                }
                else
                {
                    content->cachedChildren = content->children;
                    content->children.clear();
                    content->isCollapsed = true;
                    if (target->text.rfind(expandedPrefix, 0) == 0)
                    {
                        target->text = collapsedPrefix + target->text.substr(expandedPrefix.size());
                    }
                    else if (target->text.size() >= 2 && target->text[0] == 'v' && target->text[1] == ' ')
                    {
                        target->text = collapsedPrefix + target->text.substr(2);
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
    if (target->type == WidgetElementType::DropdownButton)
    {
        // Toggle: if clicking the same button that opened the menu, just close it
        if (m_dropdownVisible && target->id == m_dropdownSourceId)
        {
            closeDropdownMenu();
        }
        else
        {
            // Close any open dropdown first
            if (m_dropdownVisible)
            {
                closeDropdownMenu();
            }

            // Build menu items from dropdownItems (runtime callbacks) or items (labels)
            std::vector<DropdownMenuItem> menuItems;
            if (!target->dropdownItems.empty())
            {
                for (const auto& di : target->dropdownItems)
                {
                    menuItems.push_back({ di.label, di.onClick });
                }
            }
            else if (!target->items.empty())
            {
                auto selCallback = target->onSelectionChanged;
                for (int i = 0; i < static_cast<int>(target->items.size()); ++i)
                {
                    menuItems.push_back({ target->items[static_cast<size_t>(i)], [selCallback, i]()
                    {
                        if (selCallback) selCallback(i);
                    }});
                }
            }

            // If no items available, show a placeholder
            if (menuItems.empty())
            {
                menuItems.push_back({ "(No assets available)", {} });
            }

            // Anchor below the button, match button width
            const Vec2 anchor{
                target->computedPositionPixels.x,
                target->computedPositionPixels.y + target->computedSizePixels.y + 2.0f
            };
            m_dropdownSourceId = target->id;
            showDropdownMenu(anchor, menuItems, target->computedSizePixels.x);
        }
        markAllWidgetsDirty();
    }
    // Copy target data before callbacks — onClicked/onDoubleClicked may rebuild
    // the widget tree (e.g. refreshContentBrowser), invalidating the target pointer.
    const std::string targetId = target->id;
    const std::string targetClickEvent = target->clickEvent;
    const auto targetOnClicked = target->onClicked;
    const auto targetOnDoubleClicked = target->onDoubleClicked;
    const bool targetIsDraggable = target->isDraggable;

    // Suppress click for draggable elements — click will fire on mouse-up if no drag occurred
    if (!targetIsDraggable)
    {
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
    // F2: trigger inline rename on selected Content Browser asset
    if (key == SDLK_F2)
    {
        if (!m_selectedGridAsset.empty() && !m_renamingGridAsset)
        {
            m_renamingGridAsset = true;
            m_renameOriginalPath = m_selectedGridAsset;
            refreshContentBrowser();
            return true;
        }
        return false;
    }

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
        if (m_renamingGridAsset)
        {
            m_renamingGridAsset = false;
            m_renameOriginalPath.clear();
            refreshContentBrowser();
        }
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

    // Iterate widgets in descending z-order (front-to-back) so that
    // overlapping higher-z widgets receive scroll events first.
    // Filter by active tab to match getWidgetsOrderedByZ behaviour.
    std::vector<WidgetEntry*> zOrdered;
    zOrdered.reserve(m_widgets.size());
    for (auto& entry : m_widgets)
    {
        if (entry.widget)
        {
            if (!entry.tabId.empty() && entry.tabId != m_activeTabId)
                continue;
            zOrdered.push_back(&entry);
        }
    }
    std::sort(zOrdered.begin(), zOrdered.end(), [](const WidgetEntry* a, const WidgetEntry* b)
    {
        return a->widget->getZOrder() > b->widget->getZOrder();
    });

    for (auto* entryPtr : zOrdered)
    {
        // Skip widgets whose computed bounds do not contain the pointer
        if (entryPtr->widget->hasComputedPosition() && entryPtr->widget->hasComputedSize())
        {
            const Vec2 wPos = entryPtr->widget->getComputedPositionPixels();
            const Vec2 wSize = entryPtr->widget->getComputedSizePixels();
            if (!pointInRect(wPos, wSize, screenPos))
            {
                continue;
            }
        }

        for (auto it = entryPtr->widget->getElementsMutable().rbegin(); it != entryPtr->widget->getElementsMutable().rend(); ++it)
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
                entryPtr->widget->markLayoutDirty();
                m_renderDirty = true;
                return true;
            }
        }
    }

    // Widget editor canvas zoom — only if no scrollable widget claimed the event
    if (auto* weState = getActiveWidgetEditorState())
    {
        if (isOverWidgetEditorCanvas(screenPos))
        {
            const float zoomStep = 0.1f;
            float newZoom = weState->zoom + delta * zoomStep;
            newZoom = std::clamp(newZoom, 0.1f, 5.0f);
            weState->zoom = newZoom;
            weState->previewDirty = true;
            return true;
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

    // Drag threshold detection: start actual drag after 5px movement
    if (m_dragPending && !m_dragging)
    {
        const float dx = screenPos.x - m_dragStartPos.x;
        const float dy = screenPos.y - m_dragStartPos.y;
        if ((dx * dx + dy * dy) > 25.0f) // 5px threshold
        {
            m_dragging = true;
            m_dragPending = false;
            Logger::Instance().log(Logger::Category::UI,
                "Drag started: " + m_dragPayload, Logger::LogLevel::INFO);
        }
    }

    updateHoverStates();

    // Update widget editor hover preview
    updateWidgetEditorHover(screenPos);
}

void UIManager::cancelDrag()
{
    m_dragging = false;
    m_dragPending = false;
    m_dragPayload.clear();
    m_dragSourceId.clear();
    m_dragLabel.clear();
}

bool UIManager::handleMouseUp(const Vec2& screenPos, int button)
{
    if (button != SDL_BUTTON_LEFT)
    {
        return false;
    }

    // End laptop-mode left-click panning
    if (auto* weState = getActiveWidgetEditorState())
    {
        if (weState->isPanning)
        {
            weState->isPanning = false;
            return true;
        }
    }

    // If we had a pending drag that never started, fire the deferred click
    if (m_dragPending && !m_dragging)
    {
        const std::string sourceId = m_dragSourceId;
        m_dragPending = false;
        m_dragPayload.clear();
        m_dragSourceId.clear();
        m_dragLabel.clear();

        // Fire the deferred click on the original element
        WidgetElement* target = hitTest(screenPos, false);
        if (target && target->id == sourceId)
        {
            if (target->onClicked)
            {
                const auto cb = target->onClicked;
                const auto dblCb = target->onDoubleClicked;
                const std::string targetId = target->id;
                cb();

                // Double-click detection
                const uint64_t now = SDL_GetTicks();
                const bool sameElement = (!targetId.empty() && targetId == m_lastClickedElementId);
                const bool withinTime = (now - m_lastClickTimeMs) < 400;
                if (sameElement && withinTime && dblCb)
                {
                    dblCb();
                    m_lastClickedElementId.clear();
                    m_lastClickTimeMs = 0;
                }
                else
                {
                    m_lastClickedElementId = targetId;
                    m_lastClickTimeMs = now;
                }
            }
        }
        return false;
    }

    if (!m_dragging)
    {
        return false;
    }

    // We are dropping — figure out where
    const std::string payload = m_dragPayload;
    const std::string sourceId = m_dragSourceId;
    cancelDrag();

    Logger::Instance().log(Logger::Category::UI,
        "Drop at (" + std::to_string(screenPos.x) + ", " + std::to_string(screenPos.y) + ") payload=" + payload,
        Logger::LogLevel::INFO);

    // Widget editor: drop a control onto the canvas
    const std::string weControlPrefix = "WidgetControl|";
    if (payload.rfind(weControlPrefix, 0) == 0)
    {
        if (auto* weState = getActiveWidgetEditorState())
        {
            if (isOverWidgetEditorCanvas(screenPos))
            {
                const std::string elementType = payload.substr(weControlPrefix.size());
                addElementToEditedWidget(weState->tabId, elementType);
                return true;
            }
        }
    }

    // Widget editor: hierarchy drag-and-drop reordering
    const std::string weHierarchyPrefix = "WidgetHierarchy|";
    if (payload.rfind(weHierarchyPrefix, 0) == 0)
    {
        if (auto* weState = getActiveWidgetEditorState())
        {
            const std::string draggedElId = payload.substr(weHierarchyPrefix.size());
            WidgetElement* dropTarget = hitTest(screenPos, false);
            if (dropTarget)
            {
                // Check if dropped on a hierarchy tree row
                const std::string treeRowPrefix = "WidgetEditor.Left.TreeRow.";
                if (dropTarget->id.rfind(treeRowPrefix, 0) == 0)
                {
                    // Resolve which element the drop target row represents
                    // by finding the element whose label matches the row text
                    const std::string targetElId = resolveHierarchyRowElementId(
                        weState->tabId, dropTarget->id);
                    if (!targetElId.empty() && targetElId != draggedElId)
                    {
                        moveWidgetEditorElement(weState->tabId, draggedElId, targetElId);
                        return true;
                    }
                }
            }
        }
    }

    // Check if dropped on a content browser folder (grid folder tile)
    WidgetElement* dropTarget = hitTest(screenPos, false);
    if (dropTarget)
    {
        const std::string gridDirPrefix = "ContentBrowser.GridDir.";
        const std::string treeDirPrefix = "ContentBrowser.Dir.";
        const std::string outlinerPrefix = "Outliner.Entity.";
        if (dropTarget->id.rfind(gridDirPrefix, 0) == 0)
        {
            const std::string folderPath = dropTarget->id.substr(gridDirPrefix.size());
            if (m_onDropOnFolder)
            {
                m_onDropOnFolder(payload, folderPath);
            }
            return true;
        }
        if (dropTarget->id.rfind(treeDirPrefix, 0) == 0)
        {
            std::string folderPath;
            if (dropTarget->id != "ContentBrowser.Dir.Root")
            {
                folderPath = dropTarget->id.substr(treeDirPrefix.size());
            }
            if (m_onDropOnFolder)
            {
                m_onDropOnFolder(payload, folderPath);
            }
            return true;
        }
        // Drop on Outliner entity row → apply asset to entity
        if (dropTarget->id.rfind(outlinerPrefix, 0) == 0)
        {
            const char* start = dropTarget->id.c_str() + outlinerPrefix.size();
            char* end = nullptr;
            const unsigned long entityValue = std::strtoul(start, &end, 10);
            if (end && end != start && entityValue != 0 && m_onDropOnEntity)
            {
                m_onDropOnEntity(payload, static_cast<unsigned int>(entityValue));
            }
            return true;
        }

        // Drop on EntityDetails dropdown buttons (Mesh, Material, Script)
        const std::string detailsDropPrefix = "Details.";
        const std::string dropSuffix = ".Dropdown";
        if (dropTarget->id.rfind(detailsDropPrefix, 0) == 0 &&
            dropTarget->id.size() > dropSuffix.size() &&
            dropTarget->id.compare(dropTarget->id.size() - dropSuffix.size(), dropSuffix.size(), dropSuffix) == 0)
        {
            // Parse payload: "typeInt|relPath"
            const auto pipePos = payload.find('|');
            if (pipePos != std::string::npos && m_outlinerSelectedEntity != 0)
            {
                const int payloadTypeInt = std::atoi(payload.substr(0, pipePos).c_str());
                const std::string assetPath = payload.substr(pipePos + 1);
                const AssetType droppedType = static_cast<AssetType>(payloadTypeInt);

                // Determine which dropdown was targeted
                AssetType expectedType = AssetType::Unknown;
                if (dropTarget->id == "Details.Mesh.Dropdown")
                {
                    expectedType = AssetType::Model3D;
                }
                else if (dropTarget->id == "Details.Material.Dropdown")
                {
                    expectedType = AssetType::Material;
                }
                else if (dropTarget->id == "Details.Script.Dropdown")
                {
                    expectedType = AssetType::Script;
                }

                if (expectedType != AssetType::Unknown && droppedType == expectedType)
                {
                    applyAssetToEntity(droppedType, assetPath, m_outlinerSelectedEntity);
                }
                else
                {
                    showToastMessage("Wrong asset type for this slot.", 3.0f);
                }
                return true;
            }
        }
    }

    // If not dropped on UI at all, it's a viewport drop
    if (!isPointerOverUI(screenPos))
    {
        if (m_onDropOnViewport)
        {
            m_onDropOnViewport(payload, screenPos);
        }
        return true;
    }

    return false;
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
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.3f, 0.35f };
    panel.to = Vec2{ 0.7f, 0.65f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.color = Vec4{ 0.15f, 0.16f, 0.2f, 0.95f };
    panel.hitTestMode = HitTestMode::DisabledSelf;
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
    closeButton.hitTestMode = HitTestMode::Enabled;
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
    panel.hitTestMode = HitTestMode::DisabledSelf;
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
    if (newHovered && newHovered->hitTestMode == HitTestMode::Enabled && newHovered != m_lastHoveredElement)
    {
        newHovered->isHovered = true;
        if (newHovered->onHovered)
        {
            newHovered->onHovered();
        }
        m_renderDirty = true;
    }

    m_lastHoveredElement = (newHovered && newHovered->hitTestMode == HitTestMode::Enabled) ? newHovered : nullptr;
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
        // Tab-scoped filtering
        if (!entry.tabId.empty() && entry.tabId != m_activeTabId)
            continue;
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

            // DisabledAll: skip self AND all children
            if (element.hitTestMode == HitTestMode::DisabledAll)
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

            if (element.hitTestMode != HitTestMode::Enabled)
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

    // Pre-pass: expanded DropDown items render on top (deferred), so they must
    // win hit-testing over sibling elements that occupy the same screen area.
    const std::function<WidgetElement*(const WidgetElement&)> findExpandedDropDown =
        [&](const WidgetElement& element) -> WidgetElement*
        {
            for (auto& child : element.children)
            {
                if (auto* found = findExpandedDropDown(child))
                {
                    return found;
                }
            }
            if (element.type == WidgetElementType::DropDown && element.isExpanded && !element.items.empty()
                && element.hasComputedPosition && element.hasComputedSize)
            {
                const float itemHeight = std::max(20.0f, element.computedSizePixels.y);
                const float dropHeight = itemHeight * static_cast<float>(element.items.size());
                const Vec2 dropPos{ element.computedPositionPixels.x, element.computedPositionPixels.y + element.computedSizePixels.y };
                const Vec2 dropSize{ element.computedSizePixels.x, dropHeight };
                if (pointInRect(dropPos, dropSize, screenPos))
                {
                    return const_cast<WidgetElement*>(&element);
                }
                // Also match clicks on the collapsed header area itself
                if (pointInRect(element.computedPositionPixels, element.computedSizePixels, screenPos))
                {
                    return const_cast<WidgetElement*>(&element);
                }
            }
            return nullptr;
        };

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
            if (auto* hit = findExpandedDropDown(*eit))
            {
                return hit;
            }
        }
    }

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

void UIManager::refreshStatusBar()
{
    auto* entry = findWidgetEntry("StatusBar");
    if (!entry || !entry->widget)
    {
        return;
    }

    auto& elements = entry->widget->getElementsMutable();
    WidgetElement* dirtyLabel = FindElementById(elements, "StatusBar.DirtyLabel");
    if (dirtyLabel)
    {
        const size_t count = AssetManager::Instance().getUnsavedAssetCount();
        if (count == 0)
        {
            dirtyLabel->text = "No unsaved changes";
            dirtyLabel->textColor = Vec4{ 0.6f, 0.6f, 0.65f, 1.0f };
        }
        else
        {
            dirtyLabel->text = std::to_string(count) + " unsaved change" + (count > 1 ? "s" : "");
            dirtyLabel->textColor = Vec4{ 1.0f, 0.85f, 0.3f, 1.0f };
        }
    }

    WidgetElement* undoBtn = FindElementById(elements, "StatusBar.Undo");
    if (undoBtn)
    {
        auto& undo = UndoRedoManager::Instance();
        const bool canUndo = undo.canUndo();
        undoBtn->textColor = canUndo
            ? Vec4{ 0.95f, 0.95f, 0.95f, 1.0f }
            : Vec4{ 0.45f, 0.45f, 0.5f, 1.0f };
        undoBtn->text = canUndo
            ? ("Undo: " + undo.lastUndoDescription())
            : "Undo";
        if (undoBtn->text.size() > 20)
        {
            undoBtn->text = undoBtn->text.substr(0, 17) + "...";
        }
    }

    WidgetElement* redoBtn = FindElementById(elements, "StatusBar.Redo");
    if (redoBtn)
    {
        auto& undo = UndoRedoManager::Instance();
        const bool canRedo = undo.canRedo();
        redoBtn->textColor = canRedo
            ? Vec4{ 0.95f, 0.95f, 0.95f, 1.0f }
            : Vec4{ 0.45f, 0.45f, 0.5f, 1.0f };
        redoBtn->text = canRedo
            ? ("Redo: " + undo.lastRedoDescription())
            : "Redo";
        if (redoBtn->text.size() > 20)
        {
            redoBtn->text = redoBtn->text.substr(0, 17) + "...";
        }
    }

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

void UIManager::showSaveProgressModal(size_t total)
{
    m_saveProgressTotal = total;
    m_saveProgressSaved = 0;
    m_saveProgressVisible = true;

    if (!m_saveProgressWidget)
    {
        m_saveProgressWidget = std::make_shared<Widget>();
        m_saveProgressWidget->setName("SaveProgress");
        m_saveProgressWidget->setAnchor(WidgetAnchor::TopLeft);
        m_saveProgressWidget->setFillX(true);
        m_saveProgressWidget->setFillY(true);
        m_saveProgressWidget->setZOrder(10001);
    }

    WidgetElement overlay{};
    overlay.id = "SaveProgress.Overlay";
    overlay.type = WidgetElementType::Panel;
    overlay.from = Vec2{ 0.0f, 0.0f };
    overlay.to = Vec2{ 1.0f, 1.0f };
    overlay.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.4f };
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "SaveProgress.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.3f, 0.38f };
    panel.to = Vec2{ 0.7f, 0.62f };
    panel.padding = Vec2{ 20.0f, 14.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.color = Vec4{ 0.15f, 0.16f, 0.2f, 0.96f };
    panel.runtimeOnly = true;

    WidgetElement title{};
    title.id = "SaveProgress.Title";
    title.type = WidgetElementType::Text;
    title.text = "Saving...";
    title.font = "default.ttf";
    title.fontSize = 18.0f;
    title.textAlignH = TextAlignH::Center;
    title.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    title.fillX = true;
    title.minSize = Vec2{ 0.0f, 24.0f };
    title.runtimeOnly = true;

    WidgetElement counter{};
    counter.id = "SaveProgress.Counter";
    counter.type = WidgetElementType::Text;
    counter.text = "0 / " + std::to_string(total);
    counter.font = "default.ttf";
    counter.fontSize = 14.0f;
    counter.textAlignH = TextAlignH::Center;
    counter.textColor = Vec4{ 0.8f, 0.85f, 0.9f, 1.0f };
    counter.fillX = true;
    counter.minSize = Vec2{ 0.0f, 20.0f };
    counter.runtimeOnly = true;

    WidgetElement progress{};
    progress.id = "SaveProgress.Bar";
    progress.type = WidgetElementType::ProgressBar;
    progress.fillX = true;
    progress.minSize = Vec2{ 0.0f, 18.0f };
    progress.minValue = 0.0f;
    progress.maxValue = static_cast<float>(total);
    progress.valueFloat = 0.0f;
    progress.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
    progress.fillColor = Vec4{ 0.2f, 0.55f, 0.2f, 0.95f };
    progress.runtimeOnly = true;

    panel.children.push_back(std::move(title));
    panel.children.push_back(std::move(counter));
    panel.children.push_back(std::move(progress));

    std::vector<WidgetElement> elements;
    elements.push_back(std::move(overlay));
    elements.push_back(std::move(panel));
    m_saveProgressWidget->setElements(std::move(elements));
    m_saveProgressWidget->markLayoutDirty();

    registerWidget("SaveProgress", m_saveProgressWidget);
}

void UIManager::updateSaveProgress(size_t saved, size_t total)
{
    m_saveProgressSaved = saved;
    m_saveProgressTotal = total;

    if (!m_saveProgressWidget)
    {
        return;
    }

    auto& elements = m_saveProgressWidget->getElementsMutable();
    WidgetElement* counter = FindElementById(elements, "SaveProgress.Counter");
    if (counter)
    {
        counter->text = std::to_string(saved) + " / " + std::to_string(total);
    }

    WidgetElement* bar = FindElementById(elements, "SaveProgress.Bar");
    if (bar)
    {
        bar->maxValue = static_cast<float>(total);
        bar->valueFloat = static_cast<float>(saved);
    }

    m_saveProgressWidget->markLayoutDirty();
    m_renderDirty = true;
}

void UIManager::closeSaveProgressModal(bool success)
{
    m_saveProgressVisible = false;
    unregisterWidget("SaveProgress");

    if (success)
    {
        showToastMessage("All assets saved successfully.", 3.0f);
    }
    else
    {
        showToastMessage("Some assets failed to save.", 4.0f);
    }

    refreshStatusBar();
}

void UIManager::showDropdownMenu(const Vec2& anchorPixels, const std::vector<DropdownMenuItem>& items, float minWidth)
{
    closeDropdownMenu();

    if (items.empty()) return;

    constexpr float kItemH = 28.0f;
    constexpr float kPadY = 4.0f;
    constexpr float kDefaultMenuW = 180.0f;
    const float menuW = std::max(kDefaultMenuW, minWidth);
    const float menuH = kPadY * 2.0f + static_cast<float>(items.size()) * kItemH;

    auto widget = std::make_shared<Widget>();
    widget->setName("DropdownMenu");
    widget->setSizePixels(Vec2{ menuW, menuH });
    widget->setPositionPixels(Vec2{ anchorPixels.x, anchorPixels.y });
    widget->setAnchor(WidgetAnchor::TopLeft);
    widget->setAbsolutePosition(true);
    widget->setZOrder(9000);

    std::vector<WidgetElement> elements;

    // Background
    WidgetElement bg;
    bg.type  = WidgetElementType::Panel;
    bg.id    = "Dropdown.Bg";
    bg.from  = Vec2{ 0.0f, 0.0f };
    bg.to    = Vec2{ 1.0f, 1.0f };
    bg.color = Vec4{ 0.14f, 0.14f, 0.18f, 1.0f };
    elements.push_back(bg);

    for (size_t i = 0; i < items.size(); ++i)
    {
        const float y0 = kPadY + static_cast<float>(i) * kItemH;
        const float y1 = y0 + kItemH;

        if (items[i].isSeparator)
        {
            WidgetElement sep;
            sep.type = WidgetElementType::Panel;
            sep.id = "Dropdown.Sep." + std::to_string(i);
            sep.from = Vec2{ 0.08f, (y0 + kItemH * 0.5f) / menuH };
            sep.to = Vec2{ 0.92f, (y0 + kItemH * 0.5f + 1.0f) / menuH };
            sep.color = Vec4{ 0.30f, 0.32f, 0.38f, 1.0f };
            sep.hitTestMode = HitTestMode::DisabledSelf;
            elements.push_back(std::move(sep));
            continue;
        }

        WidgetElement item;
        item.type          = WidgetElementType::Button;
        item.id            = "Dropdown.Item." + std::to_string(i);
        item.from          = Vec2{ 0.0f, y0 / menuH };
        item.to            = Vec2{ 1.0f, y1 / menuH };
        item.text          = items[i].label;
        item.fontSize      = 13.0f;
        item.color         = Vec4{ 0.14f, 0.14f, 0.18f, 0.0f };
        item.hoverColor    = Vec4{ 0.24f, 0.24f, 0.30f, 1.0f };
        item.textColor     = Vec4{ 0.88f, 0.88f, 0.92f, 1.0f };
        item.textAlignH    = TextAlignH::Left;
        item.textAlignV    = TextAlignV::Center;
        item.padding       = Vec2{ 12.0f, 4.0f };
        item.hitTestMode = HitTestMode::Enabled;

        auto callback = items[i].onClick;
        if (callback)
        {
            item.onClicked = [this, callback]()
            {
                closeDropdownMenu();
                callback();
            };
        }
        else
        {
            // Placeholder item with no callback — just close the menu
            item.textColor = Vec4{ 0.55f, 0.55f, 0.6f, 1.0f };
            item.hoverColor = item.color;
            item.onClicked = [this]()
            {
                closeDropdownMenu();
            };
        }

        elements.push_back(item);
    }

    widget->setElements(std::move(elements));
    registerWidget("__DropdownMenu__", widget);
    m_dropdownWidget = widget;
    m_dropdownVisible = true;
    markAllWidgetsDirty();
}

void UIManager::closeDropdownMenu()
{
    if (!m_dropdownVisible) return;
    unregisterWidget("__DropdownMenu__");
    m_dropdownWidget.reset();
    m_dropdownVisible = false;
    m_dropdownSourceId.clear();
    markAllWidgetsDirty();
}

bool UIManager::isOverContentBrowserGrid(const Vec2& screenPos) const
{
    const auto* entry = findWidgetEntry("ContentBrowser");
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

void UIManager::openWidgetEditorPopup(const std::string& relativeAssetPath)
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
    if (m_widgetEditorStates.count(tabId))
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
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
        showToastMessage("Failed to load widget: " + relativeAssetPath, 3.0f);
        m_renderer->removeTab(tabId);
        return;
    }

    auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
    if (!asset)
    {
        showToastMessage("Widget asset missing after load: " + relativeAssetPath, 3.0f);
        m_renderer->removeTab(tabId);
        return;
    }

    auto widget = m_renderer->createWidgetFromAsset(asset);
    if (!widget)
    {
        showToastMessage("Failed to create widget from asset: " + relativeAssetPath, 3.0f);
        m_renderer->removeTab(tabId);
        return;
    }

    const std::string contentWidgetId = "WidgetEditor.Content." + tabId;
    const std::string leftWidgetId = "WidgetEditor.Left." + tabId;
    const std::string rightWidgetId = "WidgetEditor.Right." + tabId;
    const std::string canvasWidgetId = "WidgetEditor.Canvas." + tabId;
    const std::string toolbarWidgetId = "WidgetEditor.Toolbar." + tabId;

    unregisterWidget(contentWidgetId);
    unregisterWidget(leftWidgetId);
    unregisterWidget(rightWidgetId);
    unregisterWidget(canvasWidgetId);
    unregisterWidget(toolbarWidgetId);

    // Store editor state
    WidgetEditorState state;
    state.tabId = tabId;
    state.assetPath = relativeAssetPath;
    state.editedWidget = widget;
    state.contentWidgetId = contentWidgetId;
    state.leftWidgetId = leftWidgetId;
    state.rightWidgetId = rightWidgetId;
    state.canvasWidgetId = canvasWidgetId;
    state.toolbarWidgetId = toolbarWidgetId;
    state.assetId = static_cast<unsigned int>(assetId);
    state.isDirty = false;
    m_widgetEditorStates[tabId] = std::move(state);

    // --- Top toolbar: save button + dirty indicator ---
    {
        auto toolbarWidget = std::make_shared<Widget>();
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
        root.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
        root.runtimeOnly = true;

        // Save button
        {
            WidgetElement saveBtn{};
            saveBtn.id = "WidgetEditor.Toolbar.Save";
            saveBtn.type = WidgetElementType::Button;
            saveBtn.text = "Save";
            saveBtn.font = "default.ttf";
            saveBtn.fontSize = 13.0f;
            saveBtn.textColor = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
            saveBtn.color = Vec4{ 0.22f, 0.24f, 0.30f, 1.0f };
            saveBtn.hoverColor = Vec4{ 0.30f, 0.34f, 0.42f, 1.0f };
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
            dirtyLabel.font = "default.ttf";
            dirtyLabel.fontSize = 13.0f;
            dirtyLabel.textColor = Vec4{ 0.85f, 0.65f, 0.20f, 1.0f };
            dirtyLabel.textAlignH = TextAlignH::Left;
            dirtyLabel.textAlignV = TextAlignV::Center;
            dirtyLabel.minSize = Vec2{ 0.0f, 24.0f };
            dirtyLabel.padding = Vec2{ 8.0f, 0.0f };
            dirtyLabel.runtimeOnly = true;
            root.children.push_back(std::move(dirtyLabel));
        }

        toolbarWidget->setElements({ std::move(root) });
        registerWidget(toolbarWidgetId, toolbarWidget, tabId);

        const std::string capturedTabId = tabId;
        registerClickEvent("WidgetEditor.Toolbar.Save." + tabId, [this, capturedTabId]()
        {
            saveWidgetEditorAsset(capturedTabId);
        });
    }

    // --- Left panel: available controls + hierarchy ---
    {
        auto leftWidget = std::make_shared<Widget>();
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
        root.color = Vec4{ 0.12f, 0.13f, 0.17f, 0.96f };
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
            controlsSection.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            controlsSection.runtimeOnly = true;

            // Title: Controls
            {
                WidgetElement title{};
                title.id = "WidgetEditor.Left.Title";
                title.type = WidgetElementType::Text;
                title.text = "Controls";
                title.font = "default.ttf";
                title.fontSize = 16.0f;
                title.textColor = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
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
                "WrapBox", "UniformGrid", "SizeBox", "ScaleBox", "WidgetSwitcher", "Overlay"
            };
            for (size_t i = 0; i < controls.size(); ++i)
            {
                WidgetElement item{};
                item.id = "WidgetEditor.Left.Control." + std::to_string(i);
                item.type = WidgetElementType::Button;
                item.text = "  " + controls[i];
                item.font = "default.ttf";
                item.fontSize = 14.0f;
                item.textColor = Vec4{ 0.78f, 0.80f, 0.85f, 1.0f };
                item.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                item.hoverColor = Vec4{ 0.18f, 0.20f, 0.28f, 0.8f };
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
            sep.minSize = Vec2{ 0.0f, 1.0f };
            sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.8f };
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
            hierarchySection.color = Vec4{ 0.08f, 0.09f, 0.12f, 0.75f };
            hierarchySection.runtimeOnly = true;

            // Title: Hierarchy
            {
                WidgetElement treeTitle{};
                treeTitle.id = "WidgetEditor.Left.TreeTitle";
                treeTitle.type = WidgetElementType::Text;
                treeTitle.text = "Hierarchy";
                treeTitle.font = "default.ttf";
                treeTitle.fontSize = 16.0f;
                treeTitle.textColor = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
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
                hierarchyStack.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                hierarchyStack.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(hierarchyStack));
            }

            root.children.push_back(std::move(hierarchySection));
        }

        leftWidget->setElements({ std::move(root) });
        registerWidget(leftWidgetId, leftWidget, tabId);
    }

    // --- Right panel: element details (populated by refreshWidgetEditorDetails) ---
    {
        auto rightWidget = std::make_shared<Widget>();
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
        root.color = Vec4{ 0.12f, 0.13f, 0.17f, 0.96f };
        root.scrollable = true;
        root.runtimeOnly = true;

        {
            WidgetElement title{};
            title.id = "WidgetEditor.Right.Title";
            title.type = WidgetElementType::Text;
            title.text = "Details";
            title.font = "default.ttf";
            title.fontSize = 16.0f;
            title.textColor = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
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
            hint.font = "default.ttf";
            hint.fontSize = 13.0f;
            hint.textColor = Vec4{ 0.62f, 0.66f, 0.75f, 1.0f };
            hint.textAlignH = TextAlignH::Left;
            hint.textAlignV = TextAlignV::Center;
            hint.fillX = true;
            hint.minSize = Vec2{ 0.0f, 36.0f };
            hint.runtimeOnly = true;
            root.children.push_back(std::move(hint));
        }

        rightWidget->setElements({ std::move(root) });
        registerWidget(rightWidgetId, rightWidget, tabId);
    }

    // --- Center canvas background ---
    {
        auto canvasWidget = std::make_shared<Widget>();
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
        root.color = Vec4{ 0.08f, 0.09f, 0.12f, 1.0f };
        root.runtimeOnly = true;

        canvasWidget->setElements({ std::move(root) });
        registerWidget(canvasWidgetId, canvasWidget, tabId);
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

        auto& edState = m_widgetEditorStates[tabId];
        edState.basePreviewSize = designSize;
        edState.previewDirty = true;
    }

    // Tab and close button events
    const std::string tabBtnId = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
        {
            m_renderer->setActiveTab(tabId);
        }
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this, tabId, tabBtnId, closeBtnId, leftWidgetId, rightWidgetId, canvasWidgetId, toolbarWidgetId]()
    {
        if (!m_renderer)
        {
            return;
        }
        if (m_renderer->getActiveTabId() == tabId)
        {
            m_renderer->setActiveTab("Viewport");
        }

        unregisterWidget(leftWidgetId);
        unregisterWidget(rightWidgetId);
        unregisterWidget(canvasWidgetId);
        unregisterWidget(toolbarWidgetId);

        m_renderer->cleanupWidgetEditorPreview(tabId);
        m_widgetEditorStates.erase(tabId);

        m_renderer->removeTab(tabId);
        markAllWidgetsDirty();
    });

    // Populate the hierarchy tree and initial details
    refreshWidgetEditorHierarchy(tabId);
    refreshWidgetEditorDetails(tabId);
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: select an element by id
// ---------------------------------------------------------------------------
void UIManager::selectWidgetEditorElement(const std::string& tabId, const std::string& elementId)
{
    auto it = m_widgetEditorStates.find(tabId);
    if (it == m_widgetEditorStates.end())
        return;

    it->second.selectedElementId = elementId;
    it->second.previewDirty = true;
    refreshWidgetEditorHierarchy(tabId);
    refreshWidgetEditorDetails(tabId);
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: apply zoom + pan transform to preview widget
// ---------------------------------------------------------------------------
void UIManager::applyWidgetEditorTransform(const std::string& tabId)
{
    auto it = m_widgetEditorStates.find(tabId);
    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
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
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: get state for the currently active tab (if it's a widget editor)
// ---------------------------------------------------------------------------
UIManager::WidgetEditorState* UIManager::getActiveWidgetEditorState()
{
    const std::string& activeTab = m_activeTabId;
    auto it = m_widgetEditorStates.find(activeTab);
    if (it == m_widgetEditorStates.end())
        return nullptr;
    return &it->second;
}

// ---------------------------------------------------------------------------
// Widget Editor: check if screenPos is over the canvas widget area
// ---------------------------------------------------------------------------
bool UIManager::isOverWidgetEditorCanvas(const Vec2& screenPos) const
{
    auto it = m_widgetEditorStates.find(m_activeTabId);
    if (it == m_widgetEditorStates.end())
        return false;

    const auto* entry = findWidgetEntry(it->second.canvasWidgetId);
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
void UIManager::saveWidgetEditorAsset(const std::string& tabId)
{
    auto it = m_widgetEditorStates.find(tabId);
    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
        return;

    auto& state = it->second;
    auto& assetManager = AssetManager::Instance();

    auto assetData = assetManager.getLoadedAssetByID(state.assetId);
    if (!assetData)
    {
        showToastMessage("Cannot save: asset not found.", 3.0f);
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
        refreshWidgetEditorToolbar(tabId);
        showToastMessage("Widget saved.", 2.0f);
    }
    else
    {
        showToastMessage("Failed to save widget.", 3.0f);
    }
}

// ---------------------------------------------------------------------------
// Widget Editor: mark the current editor as having unsaved changes
// ---------------------------------------------------------------------------
void UIManager::markWidgetEditorDirty(const std::string& tabId)
{
    auto it = m_widgetEditorStates.find(tabId);
    if (it == m_widgetEditorStates.end())
        return;

    it->second.previewDirty = true;

    if (!it->second.isDirty)
    {
        it->second.isDirty = true;
        refreshWidgetEditorToolbar(tabId);
    }
}

// ---------------------------------------------------------------------------
// Widget Editor: update the toolbar dirty indicator text
// ---------------------------------------------------------------------------
void UIManager::refreshWidgetEditorToolbar(const std::string& tabId)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end())
        return;

    const auto* entry = findWidgetEntry(stateIt->second.toolbarWidgetId);
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
            break;
        }
    }

    entry->widget->markLayoutDirty();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: delete the currently selected element (with undo/redo)
// ---------------------------------------------------------------------------
void UIManager::deleteSelectedWidgetEditorElement(const std::string& tabId)
{
    auto it = m_widgetEditorStates.find(tabId);
    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
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
    markWidgetEditorDirty(tabId);
    refreshWidgetEditorHierarchy(tabId);
    refreshWidgetEditorDetails(tabId);
    markAllWidgetsDirty();

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
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 == m_widgetEditorStates.end() || !it2->second.editedWidget)
            return;

        auto& elements = it2->second.editedWidget->getElementsMutable();

        // Re-add click handler
        const std::string elId = capturedElement->id;
        capturedElement->onClicked = [this, capturedTabId, elId]()
        {
            selectWidgetEditorElement(capturedTabId, elId);
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
        markWidgetEditorDirty(capturedTabId);
        refreshWidgetEditorHierarchy(capturedTabId);
        refreshWidgetEditorDetails(capturedTabId);
        markAllWidgetsDirty();
    };

    cmd.execute = [this, capturedTabId, capturedElId]()
    {
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 == m_widgetEditorStates.end() || !it2->second.editedWidget)
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
        markWidgetEditorDirty(capturedTabId);
        refreshWidgetEditorHierarchy(capturedTabId);
        refreshWidgetEditorDetails(capturedTabId);
        markAllWidgetsDirty();
    };

    UndoRedoManager::Instance().pushCommand(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Widget Editor: public entry point – delete selected element if in editor tab
// ---------------------------------------------------------------------------
bool UIManager::tryDeleteWidgetEditorElement()
{
    auto* state = getActiveWidgetEditorState();
    if (!state || state->selectedElementId.empty())
        return false;

    deleteSelectedWidgetEditorElement(state->tabId);
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: get the canvas clip rect (x, y, w, h) for the active editor
// ---------------------------------------------------------------------------
bool UIManager::getWidgetEditorCanvasRect(Vec4& outRect) const
{
    auto it = m_widgetEditorStates.find(m_activeTabId);
    if (it == m_widgetEditorStates.end())
        return false;

    const auto* entry = findWidgetEntry(it->second.canvasWidgetId);
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
bool UIManager::isWidgetEditorContentWidget(const std::string& widgetId) const
{
    for (const auto& [tabId, state] : m_widgetEditorStates)
    {
        if (state.contentWidgetId == widgetId)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Widget Editor: get preview info for FBO rendering
// ---------------------------------------------------------------------------
bool UIManager::getWidgetEditorPreviewInfo(WidgetEditorPreviewInfo& out) const
{
    auto it = m_widgetEditorStates.find(m_activeTabId);
    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
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
void UIManager::clearWidgetEditorPreviewDirty()
{
    auto it = m_widgetEditorStates.find(m_activeTabId);
    if (it != m_widgetEditorStates.end())
        it->second.previewDirty = false;
}

// ---------------------------------------------------------------------------
// Widget Editor: select element at screen position (click in canvas)
// ---------------------------------------------------------------------------
bool UIManager::selectWidgetEditorElementAtPos(const Vec2& screenPos)
{
    auto* state = getActiveWidgetEditorState();
    if (!state || !state->editedWidget)
        return false;

    Vec4 canvasRect{};
    if (!getWidgetEditorCanvasRect(canvasRect))
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
        selectWidgetEditorElement(state->tabId, "");
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

            // Recurse into children – a matching child takes priority over
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
    selectWidgetEditorElement(state->tabId, hitId);
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: update hovered element based on mouse position
// ---------------------------------------------------------------------------
void UIManager::updateWidgetEditorHover(const Vec2& screenPos)
{
    auto* state = getActiveWidgetEditorState();
    if (!state || !state->editedWidget)
        return;

    Vec4 canvasRect{};
    if (!getWidgetEditorCanvasRect(canvasRect))
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
// Widget Editor: right-mouse-down starts panning on the canvas
// ---------------------------------------------------------------------------
bool UIManager::handleRightMouseDown(const Vec2& screenPos)
{
    auto* state = getActiveWidgetEditorState();
    if (!state)
        return false;

    if (!isOverWidgetEditorCanvas(screenPos))
        return false;

    state->isPanning = true;
    state->panStartMouse = screenPos;
    state->panStartOffset = state->panOffset;
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: right-mouse-up ends panning
// ---------------------------------------------------------------------------
bool UIManager::handleRightMouseUp(const Vec2& screenPos)
{
    (void)screenPos;
    auto* state = getActiveWidgetEditorState();
    if (!state || !state->isPanning)
        return false;

    state->isPanning = false;
    return true;
}

// ---------------------------------------------------------------------------
// Widget Editor: mouse motion updates pan offset
// ---------------------------------------------------------------------------
void UIManager::handleMouseMotionForPan(const Vec2& screenPos)
{
    auto* state = getActiveWidgetEditorState();
    if (!state || !state->isPanning)
        return;

    state->panOffset.x = state->panStartOffset.x + (screenPos.x - state->panStartMouse.x);
    state->panOffset.y = state->panStartOffset.y + (screenPos.y - state->panStartMouse.y);
}

// ---------------------------------------------------------------------------
// Widget Editor: add a new element of the given type to the edited widget
// ---------------------------------------------------------------------------
void UIManager::addElementToEditedWidget(const std::string& tabId, const std::string& elementType)
{
    auto it = m_widgetEditorStates.find(tabId);
    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
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
        newEl.color = Vec4{ 0.15f, 0.15f, 0.20f, 0.8f };
    }
    else if (elementType == "Text")
    {
        newEl.type = WidgetElementType::Text;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.15f };
        newEl.text = "New Text";
        newEl.font = "default.ttf";
        newEl.fontSize = 16.0f;
        newEl.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
    }
    else if (elementType == "Button")
    {
        newEl.type = WidgetElementType::Button;
        newEl.from = Vec2{ 0.3f, 0.4f };
        newEl.to = Vec2{ 0.7f, 0.55f };
        newEl.text = "Button";
        newEl.font = "default.ttf";
        newEl.fontSize = 14.0f;
        newEl.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        newEl.textAlignH = TextAlignH::Center;
        newEl.textAlignV = TextAlignV::Center;
        newEl.color = Vec4{ 0.2f, 0.4f, 0.7f, 0.95f };
        newEl.hoverColor = Vec4{ 0.3f, 0.5f, 0.8f, 1.0f };
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
        newEl.fontSize = 14.0f;
        newEl.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
        newEl.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
    }
    else if (elementType == "StackPanel")
    {
        newEl.type = WidgetElementType::StackPanel;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.orientation = StackOrientation::Vertical;
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.6f };
    }
    else if (elementType == "Grid")
    {
        newEl.type = WidgetElementType::Grid;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
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
        newEl.fontSize = 14.0f;
        newEl.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
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
        newEl.color = Vec4{ 1.0f, 0.5f, 0.2f, 1.0f };
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
        newEl.color = Vec4{ 0.3f, 0.32f, 0.38f, 0.8f };
    }
    else if (elementType == "Label")
    {
        newEl.type = WidgetElementType::Label;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.15f };
        newEl.text = "Label";
        newEl.font = "default.ttf";
        newEl.fontSize = 14.0f;
        newEl.textColor = Vec4{ 0.85f, 0.85f, 0.90f, 1.0f };
        newEl.hitTestMode = HitTestMode::DisabledSelf;
    }
    else if (elementType == "ToggleButton")
    {
        newEl.type = WidgetElementType::ToggleButton;
        newEl.from = Vec2{ 0.3f, 0.4f };
        newEl.to = Vec2{ 0.7f, 0.55f };
        newEl.text = "Toggle";
        newEl.font = "default.ttf";
        newEl.fontSize = 14.0f;
        newEl.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        newEl.textAlignH = TextAlignH::Center;
        newEl.textAlignV = TextAlignV::Center;
        newEl.color = Vec4{ 0.2f, 0.2f, 0.3f, 0.95f };
        newEl.hoverColor = Vec4{ 0.3f, 0.3f, 0.4f, 1.0f };
        newEl.fillColor = Vec4{ 0.2f, 0.5f, 0.8f, 0.95f };
    }
    else if (elementType == "RadioButton")
    {
        newEl.type = WidgetElementType::RadioButton;
        newEl.from = Vec2{ 0.3f, 0.4f };
        newEl.to = Vec2{ 0.7f, 0.55f };
        newEl.text = "Radio";
        newEl.font = "default.ttf";
        newEl.fontSize = 14.0f;
        newEl.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        newEl.textAlignH = TextAlignH::Center;
        newEl.textAlignV = TextAlignV::Center;
        newEl.color = Vec4{ 0.2f, 0.2f, 0.3f, 0.95f };
        newEl.hoverColor = Vec4{ 0.3f, 0.3f, 0.4f, 1.0f };
        newEl.fillColor = Vec4{ 0.2f, 0.5f, 0.8f, 0.95f };
        newEl.radioGroup = "default";
    }
    else if (elementType == "ScrollView")
    {
        newEl.type = WidgetElementType::ScrollView;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.orientation = StackOrientation::Vertical;
        newEl.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.6f };
        newEl.scrollable = true;
    }
    else if (elementType == "WrapBox")
    {
        newEl.type = WidgetElementType::WrapBox;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.orientation = StackOrientation::Horizontal;
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "UniformGrid")
    {
        newEl.type = WidgetElementType::UniformGrid;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.columns = 3;
        newEl.rows = 3;
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "SizeBox")
    {
        newEl.type = WidgetElementType::SizeBox;
        newEl.from = Vec2{ 0.1f, 0.1f };
        newEl.to = Vec2{ 0.6f, 0.6f };
        newEl.widthOverride = 200.0f;
        newEl.heightOverride = 100.0f;
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "ScaleBox")
    {
        newEl.type = WidgetElementType::ScaleBox;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.scaleMode = ScaleMode::Contain;
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "WidgetSwitcher")
    {
        newEl.type = WidgetElementType::WidgetSwitcher;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.activeChildIndex = 0;
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "Overlay")
    {
        newEl.type = WidgetElementType::Overlay;
        newEl.from = Vec2{ 0.05f, 0.05f };
        newEl.to = Vec2{ 0.95f, 0.95f };
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else
    {
        newEl.type = WidgetElementType::Panel;
        newEl.from = Vec2{ 0.1f, 0.1f };
        newEl.to = Vec2{ 0.5f, 0.5f };
        newEl.color = Vec4{ 0.2f, 0.2f, 0.25f, 0.8f };
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

    markWidgetEditorDirty(tabId);
    refreshWidgetEditorHierarchy(tabId);
    refreshWidgetEditorDetails(tabId);
    markAllWidgetsDirty();

    // Push undo/redo command
    UndoRedoManager::Command cmd;
    cmd.description = "Add " + elementType;

    cmd.undo = [this, capturedTabId, capturedElId, addedAsRoot]()
    {
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 == m_widgetEditorStates.end() || !it2->second.editedWidget)
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
        markWidgetEditorDirty(capturedTabId);
        refreshWidgetEditorHierarchy(capturedTabId);
        refreshWidgetEditorDetails(capturedTabId);
        markAllWidgetsDirty();
    };

    cmd.execute = [this, capturedTabId, capturedElement, addedAsRoot]()
    {
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 == m_widgetEditorStates.end() || !it2->second.editedWidget)
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
        markWidgetEditorDirty(capturedTabId);
        refreshWidgetEditorHierarchy(capturedTabId);
        refreshWidgetEditorDetails(capturedTabId);
        markAllWidgetsDirty();
    };

    UndoRedoManager::Instance().pushCommand(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Widget Editor: resolve which element a hierarchy tree row represents
// ---------------------------------------------------------------------------
std::string UIManager::resolveHierarchyRowElementId(const std::string& tabId, const std::string& rowId) const
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end() || !stateIt->second.editedWidget)
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
void UIManager::moveWidgetEditorElement(const std::string& tabId,
    const std::string& draggedId, const std::string& targetId)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end() || !stateIt->second.editedWidget)
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
    markWidgetEditorDirty(tabId);
    refreshWidgetEditorHierarchy(tabId);
    refreshWidgetEditorDetails(tabId);
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// Widget Editor: rebuild the hierarchy tree in the left panel
// ---------------------------------------------------------------------------
void UIManager::refreshWidgetEditorHierarchy(const std::string& tabId)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end())
        return;

    const auto& editorState = stateIt->second;
    auto* leftEntry = findWidgetEntry(editorState.leftWidgetId);
    if (!leftEntry || !leftEntry->widget)
        return;

    auto& leftElements = leftEntry->widget->getElementsMutable();
    WidgetElement* treePanel = FindElementById(leftElements, "WidgetEditor.Left.Tree");
    if (!treePanel)
        return;

    treePanel->children.clear();
    m_lastHoveredElement = nullptr;

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
            row.font = "default.ttf";
            row.fontSize = 11.0f;
            row.textAlignH = TextAlignH::Left;
            row.textAlignV = TextAlignV::Center;
            row.fillX = true;
            row.minSize = Vec2{ 0.0f, 20.0f };
            row.padding = Vec2{ 4.0f, 1.0f };
            row.hitTestMode = HitTestMode::Enabled;
            row.runtimeOnly = true;

            if (isSelected)
            {
                row.color = Vec4{ 0.20f, 0.30f, 0.55f, 0.9f };
                row.hoverColor = Vec4{ 0.25f, 0.35f, 0.60f, 1.0f };
                row.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            }
            else
            {
                row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.hoverColor = Vec4{ 0.18f, 0.20f, 0.28f, 0.8f };
                row.textColor = Vec4{ 0.75f, 0.78f, 0.84f, 1.0f };
            }

            const std::string capturedTabId = tabId;
            row.onClicked = [this, capturedTabId, elId]()
            {
                selectWidgetEditorElement(capturedTabId, elId);
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
void UIManager::refreshWidgetEditorDetails(const std::string& tabId)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end())
        return;

    auto& editorState = stateIt->second;
    auto* rightEntry = findWidgetEntry(editorState.rightWidgetId);
    if (!rightEntry || !rightEntry->widget)
        return;

    auto& rightElements = rightEntry->widget->getElementsMutable();
    WidgetElement* rootPanel = FindElementById(rightElements, "WidgetEditor.Right.Root");
    if (!rootPanel)
        return;

    // Keep only the title (first child)
    if (rootPanel->children.size() > 1)
        rootPanel->children.erase(rootPanel->children.begin() + 1, rootPanel->children.end());

    m_lastHoveredElement = nullptr;

    const auto makeLabel = [](const std::string& id, const std::string& text, float fontSize = 12.0f,
        const Vec4& color = Vec4{ 0.78f, 0.80f, 0.85f, 1.0f }, float minH = 20.0f) -> WidgetElement
    {
        WidgetElement lbl{};
        lbl.id = id;
        lbl.type = WidgetElementType::Text;
        lbl.text = text;
        lbl.font = "default.ttf";
        lbl.fontSize = fontSize;
        lbl.textColor = color;
        lbl.textAlignH = TextAlignH::Left;
        lbl.textAlignV = TextAlignV::Center;
        lbl.fillX = true;
        lbl.minSize = Vec2{ 0.0f, minH };
        lbl.runtimeOnly = true;
        return lbl;
    };

    auto fmtF = [](float v) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };

    // If no element is selected, show a hint
    if (editorState.selectedElementId.empty() || !editorState.editedWidget)
    {
        rootPanel->children.push_back(makeLabel("WidgetEditor.Right.Hint",
            "Select an element to view its properties.", 11.0f,
            Vec4{ 0.62f, 0.66f, 0.75f, 1.0f }, 36.0f));
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
        rootPanel->children.push_back(makeLabel("WidgetEditor.Right.NotFound",
            "Element not found: " + editorState.selectedElementId, 11.0f,
            Vec4{ 0.85f, 0.55f, 0.55f, 1.0f }, 24.0f));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // --- Section: Identity ---
    {
        rootPanel->children.push_back(makeLabel("WE.Det.SecIdentity", "Identity", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        // Type (read-only)
        std::string typeName;
        switch (selected->type)
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
        case WidgetElementType::Label:       typeName = "Label"; break;
        case WidgetElementType::Separator:   typeName = "Separator"; break;
        case WidgetElementType::ScrollView:  typeName = "ScrollView"; break;
        case WidgetElementType::ToggleButton: typeName = "ToggleButton"; break;
        case WidgetElementType::RadioButton: typeName = "RadioButton"; break;
        case WidgetElementType::WrapBox:      typeName = "WrapBox"; break;
        case WidgetElementType::UniformGrid:  typeName = "UniformGrid"; break;
        case WidgetElementType::SizeBox:      typeName = "SizeBox"; break;
        case WidgetElementType::ScaleBox:     typeName = "ScaleBox"; break;
        case WidgetElementType::WidgetSwitcher: typeName = "WidgetSwitcher"; break;
        case WidgetElementType::Overlay:      typeName = "Overlay"; break;
        default:                             typeName = "Unknown"; break;
        }
        rootPanel->children.push_back(makeLabel("WE.Det.Type", "Type: " + typeName));
    }

    // Helper to build an editable entry row
    const std::string capturedTabId = tabId;
    WidgetElement* sel = selected;

    // Helper to propagate property changes to the FBO preview
    const auto applyChange = [this, capturedTabId]() {
        markWidgetEditorDirty(capturedTabId);
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 != m_widgetEditorStates.end() && it2->second.editedWidget)
            it2->second.editedWidget->markLayoutDirty();
        markAllWidgetsDirty();
    };

    const auto makePropertyRow = [&](const std::string& id, const std::string& label,
        const std::string& value, std::function<void(const std::string&)> onChange) -> WidgetElement
    {
        WidgetElement row{};
        row.type = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX = true;
        row.sizeToContent = true;
        row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.padding = Vec2{ 0.0f, 1.0f };
        row.runtimeOnly = true;

        WidgetElement lbl = makeLabel(id + ".Lbl", label);
        lbl.minSize = Vec2{ 90.0f, 22.0f };
        lbl.fillX = false;
        row.children.push_back(std::move(lbl));

        EntryBarWidget entry;
        entry.setValue(value);
        entry.setFont("default.ttf");
        entry.setFontSize(12.0f);
        entry.setMinSize(Vec2{ 0.0f, 22.0f });
        entry.setPadding(Vec2{ 4.0f, 2.0f });
        entry.setOnValueChanged(onChange);

        WidgetElement entryEl = entry.toElement();
        entryEl.id = id + ".Entry";
        entryEl.fillX = true;
        entryEl.runtimeOnly = true;
        row.children.push_back(std::move(entryEl));

        return row;
    };

    // Editable ID field (part of Identity section)
    rootPanel->children.push_back(makePropertyRow("WE.Det.Id", "ID", sel->id.empty() ? "" : sel->id,
        [sel, applyChange, this, capturedTabId](const std::string& v) {
            sel->id = v;
            auto it2 = m_widgetEditorStates.find(capturedTabId);
            if (it2 != m_widgetEditorStates.end())
                it2->second.selectedElementId = v;
            applyChange();
            refreshWidgetEditorHierarchy(capturedTabId);
        }));

    // --- Section: Transform ---
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecTransform", "Transform", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("WE.Det.FromX", "From X", fmtF(sel->from.x),
            [sel, applyChange](const std::string& v) {
                try { sel->from.x = std::stof(v); } catch (...) {}
                applyChange();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.FromY", "From Y", fmtF(sel->from.y),
            [sel, applyChange](const std::string& v) {
                try { sel->from.y = std::stof(v); } catch (...) {}
                applyChange();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.ToX", "To X", fmtF(sel->to.x),
            [sel, applyChange](const std::string& v) {
                try { sel->to.x = std::stof(v); } catch (...) {}
                applyChange();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.ToY", "To Y", fmtF(sel->to.y),
            [sel, applyChange](const std::string& v) {
                try { sel->to.y = std::stof(v); } catch (...) {}
                applyChange();
            }));
    }

    // --- Section: Anchor ---
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecAnchor", "Anchor", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        // Anchor dropdown
        {
            int anchorIndex = static_cast<int>(sel->anchor);

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeLabel("WE.Det.Anchor.Lbl", "Anchor");
            lbl.minSize = Vec2{ 90.0f, 22.0f };
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            DropDownWidget anchorDd;
            anchorDd.setItems({ "TopLeft", "TopRight", "BottomLeft", "BottomRight", "Top", "Bottom", "Left", "Right", "Center", "Stretch" });
            anchorDd.setSelectedIndex(anchorIndex);
            anchorDd.setFont("default.ttf");
            anchorDd.setFontSize(12.0f);
            anchorDd.setMinSize(Vec2{ 0.0f, 22.0f });
            anchorDd.setPadding(Vec2{ 4.0f, 2.0f });
            anchorDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
            anchorDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
            anchorDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
            anchorDd.setOnSelectionChanged([sel, applyChange](int idx) {
                sel->anchor = static_cast<WidgetAnchor>(idx);
                applyChange();
            });
            WidgetElement ddEl = anchorDd.toElement();
            ddEl.id = "WE.Det.Anchor.DD";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));

            rootPanel->children.push_back(std::move(row));
        }

        rootPanel->children.push_back(makePropertyRow("WE.Det.AnchorOffX", "Offset X", fmtF(sel->anchorOffset.x),
            [sel, applyChange](const std::string& v) {
                try { sel->anchorOffset.x = std::stof(v); } catch (...) {}
                applyChange();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.AnchorOffY", "Offset Y", fmtF(sel->anchorOffset.y),
            [sel, applyChange](const std::string& v) {
                try { sel->anchorOffset.y = std::stof(v); } catch (...) {}
                applyChange();
            }));
    }

    // --- Section: Hit Test ---
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecHitTest", "Hit Test", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        {
            int htIndex = static_cast<int>(sel->hitTestMode);

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeLabel("WE.Det.HitTest.Lbl", "Mode");
            lbl.minSize = Vec2{ 90.0f, 22.0f };
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            DropDownWidget htDd;
            htDd.setItems({ "Enabled", "Disabled (Self)", "Disabled (Self + Children)" });
            htDd.setSelectedIndex(htIndex);
            htDd.setFont("default.ttf");
            htDd.setFontSize(12.0f);
            htDd.setMinSize(Vec2{ 0.0f, 22.0f });
            htDd.setPadding(Vec2{ 4.0f, 2.0f });
            htDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
            htDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
            htDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
            htDd.setOnSelectionChanged([sel, applyChange](int idx) {
                sel->hitTestMode = static_cast<HitTestMode>(idx);
                applyChange();
            });
            WidgetElement ddEl = htDd.toElement();
            ddEl.id = "WE.Det.HitTest.DD";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));

            rootPanel->children.push_back(std::move(row));
        }
    }

    // --- Section: Layout ---
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecLayout", "Layout", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("WE.Det.MinW", "Min Width", fmtF(sel->minSize.x),
            [sel, applyChange](const std::string& v) {
                try { sel->minSize.x = std::stof(v); } catch (...) {}
                applyChange();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.MinH", "Min Height", fmtF(sel->minSize.y),
            [sel, applyChange](const std::string& v) {
                try { sel->minSize.y = std::stof(v); } catch (...) {}
                applyChange();
            }));

        rootPanel->children.push_back(makePropertyRow("WE.Det.PadX", "Padding X", fmtF(sel->padding.x),
            [sel, applyChange](const std::string& v) {
                try { sel->padding.x = std::stof(v); } catch (...) {}
                applyChange();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.PadY", "Padding Y", fmtF(sel->padding.y),
            [sel, applyChange](const std::string& v) {
                try { sel->padding.y = std::stof(v); } catch (...) {}
                applyChange();
            }));

        // Horizontal alignment dropdown
        {
            int hAlignIndex = 0;
            if (sel->fillX) hAlignIndex = 3;
            else if (sel->textAlignH == TextAlignH::Center) hAlignIndex = 1;
            else if (sel->textAlignH == TextAlignH::Right) hAlignIndex = 2;
            else hAlignIndex = 0;

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeLabel("WE.Det.HAlign.Lbl", "H Align");
            lbl.minSize = Vec2{ 90.0f, 22.0f };
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            DropDownWidget hAlignDd;
            hAlignDd.setItems({ "Left", "Center", "Right", "Fill" });
            hAlignDd.setSelectedIndex(hAlignIndex);
            hAlignDd.setFont("default.ttf");
            hAlignDd.setFontSize(12.0f);
            hAlignDd.setMinSize(Vec2{ 0.0f, 22.0f });
            hAlignDd.setPadding(Vec2{ 4.0f, 2.0f });
            hAlignDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
            hAlignDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
            hAlignDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
            hAlignDd.setOnSelectionChanged([sel, applyChange](int idx) {
                if (idx == 3) { sel->fillX = true; }
                else {
                    sel->fillX = false;
                    if (idx == 1) sel->textAlignH = TextAlignH::Center;
                    else if (idx == 2) sel->textAlignH = TextAlignH::Right;
                    else sel->textAlignH = TextAlignH::Left;
                }
                applyChange();
            });
            WidgetElement ddEl = hAlignDd.toElement();
            ddEl.id = "WE.Det.HAlign.DD";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));

            rootPanel->children.push_back(std::move(row));
        }

        // Vertical alignment dropdown
        {
            int vAlignIndex = 0;
            if (sel->fillY) vAlignIndex = 3;
            else if (sel->textAlignV == TextAlignV::Center) vAlignIndex = 1;
            else if (sel->textAlignV == TextAlignV::Bottom) vAlignIndex = 2;
            else vAlignIndex = 0;

            WidgetElement row{};
            row.type = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX = true;
            row.sizeToContent = true;
            row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeLabel("WE.Det.VAlign.Lbl", "V Align");
            lbl.minSize = Vec2{ 90.0f, 22.0f };
            lbl.fillX = false;
            row.children.push_back(std::move(lbl));

            DropDownWidget vAlignDd;
            vAlignDd.setItems({ "Top", "Center", "Bottom", "Fill" });
            vAlignDd.setSelectedIndex(vAlignIndex);
            vAlignDd.setFont("default.ttf");
            vAlignDd.setFontSize(12.0f);
            vAlignDd.setMinSize(Vec2{ 0.0f, 22.0f });
            vAlignDd.setPadding(Vec2{ 4.0f, 2.0f });
            vAlignDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
            vAlignDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
            vAlignDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
            vAlignDd.setOnSelectionChanged([sel, applyChange](int idx) {
                if (idx == 3) { sel->fillY = true; }
                else {
                    sel->fillY = false;
                    if (idx == 1) sel->textAlignV = TextAlignV::Center;
                    else if (idx == 2) sel->textAlignV = TextAlignV::Bottom;
                    else sel->textAlignV = TextAlignV::Top;
                }
                applyChange();
            });
            WidgetElement ddEl = vAlignDd.toElement();
            ddEl.id = "WE.Det.VAlign.DD";
            ddEl.fillX = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));

            rootPanel->children.push_back(std::move(row));
        }

        // Size to content
        {
            CheckBoxWidget stcCb;
            stcCb.setLabel("Size to Content");
            stcCb.setChecked(sel->sizeToContent);
            stcCb.setOnCheckedChanged([sel, this](bool c) { sel->sizeToContent = c; markAllWidgetsDirty(); });
            WidgetElement stcEl = stcCb.toElement();
            stcEl.id = "WE.Det.SizeToContent";
            stcEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(stcEl));
        }

        // Max size constraints
        rootPanel->children.push_back(makePropertyRow("WE.Det.MaxW", "Max Width", fmtF(sel->maxSize.x),
            [sel, this](const std::string& v) {
                try { sel->maxSize.x = std::stof(v); } catch (...) {}
                markAllWidgetsDirty();
            }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.MaxH", "Max Height", fmtF(sel->maxSize.y),
            [sel, this](const std::string& v) {
                try { sel->maxSize.y = std::stof(v); } catch (...) {}
                markAllWidgetsDirty();
            }));

        // Spacing (for containers)
        if (sel->type == WidgetElementType::StackPanel || sel->type == WidgetElementType::ScrollView
            || sel->type == WidgetElementType::WrapBox || sel->type == WidgetElementType::UniformGrid)
        {
            rootPanel->children.push_back(makePropertyRow("WE.Det.Spacing", "Spacing", fmtF(sel->spacing),
                [sel, this](const std::string& v) {
                    try { sel->spacing = std::stof(v); } catch (...) {}
                    markAllWidgetsDirty();
                }));
        }

        // UniformGrid specific: columns / rows
        if (sel->type == WidgetElementType::UniformGrid)
        {
            rootPanel->children.push_back(makePropertyRow("WE.Det.Columns", "Columns", std::to_string(sel->columns),
                [sel, applyChange](const std::string& v) {
                    try { sel->columns = std::max(0, std::stoi(v)); } catch (...) {}
                    applyChange();
                }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.Rows", "Rows", std::to_string(sel->rows),
                [sel, applyChange](const std::string& v) {
                    try { sel->rows = std::max(0, std::stoi(v)); } catch (...) {}
                    applyChange();
                }));
        }

        // SizeBox specific: widthOverride / heightOverride
        if (sel->type == WidgetElementType::SizeBox)
        {
            rootPanel->children.push_back(makePropertyRow("WE.Det.WidthOvr", "Width Override", fmtF(sel->widthOverride),
                [sel, applyChange](const std::string& v) {
                    try { sel->widthOverride = std::max(0.0f, std::stof(v)); } catch (...) {}
                    applyChange();
                }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.HeightOvr", "Height Override", fmtF(sel->heightOverride),
                [sel, applyChange](const std::string& v) {
                    try { sel->heightOverride = std::max(0.0f, std::stof(v)); } catch (...) {}
                    applyChange();
                }));
        }

        // ScaleBox specific: scaleMode / userScale
        if (sel->type == WidgetElementType::ScaleBox)
        {
            {
                int smIndex = static_cast<int>(sel->scaleMode);
                WidgetElement row{};
                row.type = WidgetElementType::StackPanel;
                row.orientation = StackOrientation::Horizontal;
                row.fillX = true;
                row.sizeToContent = true;
                row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.runtimeOnly = true;

                WidgetElement lbl = makeLabel("WE.Det.ScaleMode.Lbl", "Scale Mode");
                lbl.minSize = Vec2{ 90.0f, 22.0f };
                lbl.fillX = false;
                row.children.push_back(std::move(lbl));

                DropDownWidget smDd;
                smDd.setItems({ "Contain", "Cover", "Fill", "ScaleDown", "UserSpecified" });
                smDd.setSelectedIndex(smIndex);
                smDd.setFont("default.ttf");
                smDd.setFontSize(12.0f);
                smDd.setMinSize(Vec2{ 0.0f, 22.0f });
                smDd.setPadding(Vec2{ 4.0f, 2.0f });
                smDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
                smDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
                smDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
                smDd.setOnSelectionChanged([sel, applyChange](int idx) {
                    sel->scaleMode = static_cast<ScaleMode>(idx);
                    applyChange();
                });
                WidgetElement ddEl = smDd.toElement();
                ddEl.id = "WE.Det.ScaleMode.DD";
                ddEl.fillX = true;
                ddEl.runtimeOnly = true;
                row.children.push_back(std::move(ddEl));

                rootPanel->children.push_back(std::move(row));
            }

            rootPanel->children.push_back(makePropertyRow("WE.Det.UserScale", "User Scale", fmtF(sel->userScale),
                [sel, applyChange](const std::string& v) {
                    try { sel->userScale = std::max(0.01f, std::stof(v)); } catch (...) {}
                    applyChange();
                }));
        }

        // WidgetSwitcher specific: activeChildIndex
        if (sel->type == WidgetElementType::WidgetSwitcher)
        {
            rootPanel->children.push_back(makePropertyRow("WE.Det.ActiveIdx", "Active Index", std::to_string(sel->activeChildIndex),
                [sel, applyChange](const std::string& v) {
                    try { sel->activeChildIndex = std::max(0, std::stoi(v)); } catch (...) {}
                    applyChange();
                }));
        }
    }

    // --- Section: Appearance ---
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecAppearance", "Appearance", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        // Color (RGBA as individual fields)
        rootPanel->children.push_back(makePropertyRow("WE.Det.ColR", "Color R", fmtF(sel->color.x),
            [sel, applyChange](const std::string& v) { try { sel->color.x = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.ColG", "Color G", fmtF(sel->color.y),
            [sel, applyChange](const std::string& v) { try { sel->color.y = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.ColB", "Color B", fmtF(sel->color.z),
            [sel, applyChange](const std::string& v) { try { sel->color.z = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.ColA", "Color A", fmtF(sel->color.w),
            [sel, this](const std::string& v) { try { sel->color.w = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));

        // Opacity
        rootPanel->children.push_back(makePropertyRow("WE.Det.Opacity", "Opacity", fmtF(sel->opacity),
            [sel, this](const std::string& v) { try { sel->opacity = std::max(0.0f, std::min(1.0f, std::stof(v))); } catch (...) {} markAllWidgetsDirty(); }));

        // Visibility
        {
            CheckBoxWidget visCb;
            visCb.setLabel("Visible");
            visCb.setChecked(sel->isVisible);
            visCb.setOnCheckedChanged([sel, this](bool c) { sel->isVisible = c; markAllWidgetsDirty(); });
            WidgetElement visEl = visCb.toElement();
            visEl.id = "WE.Det.Visible";
            visEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(visEl));
        }

        // Border
        rootPanel->children.push_back(makePropertyRow("WE.Det.BorderW", "Border Width", fmtF(sel->borderThickness),
            [sel, this](const std::string& v) { try { sel->borderThickness = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.BorderR", "Border Radius", fmtF(sel->borderRadius),
            [sel, this](const std::string& v) { try { sel->borderRadius = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));

        // Tooltip
        rootPanel->children.push_back(makePropertyRow("WE.Det.Tooltip", "Tooltip", sel->tooltipText,
            [sel, this](const std::string& v) { sel->tooltipText = v; markAllWidgetsDirty(); }));

        // ── Phase 2: Brush & Transform ────────────────────────────────────
        {
            WidgetElement sep{};
            sep.type = WidgetElementType::Panel;
            sep.fillX = true;
            sep.minSize = Vec2{ 0.0f, 1.0f };
            sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
            sep.runtimeOnly = true;
            rootPanel->children.push_back(std::move(sep));

            rootPanel->children.push_back(makeLabel("WE.Det.SecBrush", "Brush / Transform", 13.0f,
                Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

            // Background BrushType
            {
                int btIdx = static_cast<int>(sel->background.type);
                WidgetElement row{};
                row.type = WidgetElementType::StackPanel;
                row.orientation = StackOrientation::Horizontal;
                row.fillX = true;
                row.sizeToContent = true;
                row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.runtimeOnly = true;

                WidgetElement lbl = makeLabel("WE.Det.BgBrush.Lbl", "Bg Brush");
                lbl.minSize = Vec2{ 90.0f, 22.0f };
                lbl.fillX = false;
                row.children.push_back(std::move(lbl));

                DropDownWidget dd;
                dd.setItems({ "None", "SolidColor", "Image", "NineSlice", "LinearGradient" });
                dd.setSelectedIndex(btIdx);
                dd.setFont("default.ttf");
                dd.setFontSize(12.0f);
                dd.setMinSize(Vec2{ 0.0f, 22.0f });
                dd.setPadding(Vec2{ 4.0f, 2.0f });
                dd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
                dd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
                dd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
                dd.setOnSelectionChanged([sel, this](int idx) {
                    sel->background.type = static_cast<BrushType>(idx);
                    markAllWidgetsDirty();
                });
                WidgetElement ddEl = dd.toElement();
                ddEl.id = "WE.Det.BgBrush.DD";
                ddEl.fillX = true;
                ddEl.runtimeOnly = true;
                row.children.push_back(std::move(ddEl));
                rootPanel->children.push_back(std::move(row));
            }

            // Background brush color
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgColR", "Bg Color R", fmtF(sel->background.color.x),
                [sel, this](const std::string& v) { try { sel->background.color.x = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgColG", "Bg Color G", fmtF(sel->background.color.y),
                [sel, this](const std::string& v) { try { sel->background.color.y = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgColB", "Bg Color B", fmtF(sel->background.color.z),
                [sel, this](const std::string& v) { try { sel->background.color.z = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgColA", "Bg Color A", fmtF(sel->background.color.w),
                [sel, this](const std::string& v) { try { sel->background.color.w = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));

            // Background gradient end color (only relevant for LinearGradient)
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgEndR", "Bg End R", fmtF(sel->background.colorEnd.x),
                [sel, this](const std::string& v) { try { sel->background.colorEnd.x = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgEndG", "Bg End G", fmtF(sel->background.colorEnd.y),
                [sel, this](const std::string& v) { try { sel->background.colorEnd.y = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgEndB", "Bg End B", fmtF(sel->background.colorEnd.z),
                [sel, this](const std::string& v) { try { sel->background.colorEnd.z = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgEndA", "Bg End A", fmtF(sel->background.colorEnd.w),
                [sel, this](const std::string& v) { try { sel->background.colorEnd.w = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));

            // Gradient angle
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgAngle", "Gradient Angle", fmtF(sel->background.gradientAngle),
                [sel, this](const std::string& v) { try { sel->background.gradientAngle = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));

            // Background image path
            rootPanel->children.push_back(makePropertyRow("WE.Det.BgImage", "Bg Image", sel->background.imagePath,
                [sel, this](const std::string& v) { sel->background.imagePath = v; sel->background.textureId = 0; markAllWidgetsDirty(); }));

            // ClipMode
            {
                int cmIdx = static_cast<int>(sel->clipMode);
                WidgetElement row{};
                row.type = WidgetElementType::StackPanel;
                row.orientation = StackOrientation::Horizontal;
                row.fillX = true;
                row.sizeToContent = true;
                row.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                row.runtimeOnly = true;

                WidgetElement lbl = makeLabel("WE.Det.ClipMode.Lbl", "Clip Mode");
                lbl.minSize = Vec2{ 90.0f, 22.0f };
                lbl.fillX = false;
                row.children.push_back(std::move(lbl));

                DropDownWidget cmDd;
                cmDd.setItems({ "None", "ClipToBounds", "InheritFromParent" });
                cmDd.setSelectedIndex(cmIdx);
                cmDd.setFont("default.ttf");
                cmDd.setFontSize(12.0f);
                cmDd.setMinSize(Vec2{ 0.0f, 22.0f });
                cmDd.setPadding(Vec2{ 4.0f, 2.0f });
                cmDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
                cmDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
                cmDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
                cmDd.setOnSelectionChanged([sel, this](int idx) {
                    sel->clipMode = static_cast<ClipMode>(idx);
                    markAllWidgetsDirty();
                });
                WidgetElement ddEl = cmDd.toElement();
                ddEl.id = "WE.Det.ClipMode.DD";
                ddEl.fillX = true;
                ddEl.runtimeOnly = true;
                row.children.push_back(std::move(ddEl));
                rootPanel->children.push_back(std::move(row));
            }

            // RenderTransform
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.TX", "Translate X", fmtF(sel->renderTransform.translation.x),
                [sel, this](const std::string& v) { try { sel->renderTransform.translation.x = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.TY", "Translate Y", fmtF(sel->renderTransform.translation.y),
                [sel, this](const std::string& v) { try { sel->renderTransform.translation.y = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.Rot", "Rotation", fmtF(sel->renderTransform.rotation),
                [sel, this](const std::string& v) { try { sel->renderTransform.rotation = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.SX", "Scale X", fmtF(sel->renderTransform.scale.x),
                [sel, this](const std::string& v) { try { sel->renderTransform.scale.x = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.SY", "Scale Y", fmtF(sel->renderTransform.scale.y),
                [sel, this](const std::string& v) { try { sel->renderTransform.scale.y = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.ShX", "Shear X", fmtF(sel->renderTransform.shear.x),
                [sel, this](const std::string& v) { try { sel->renderTransform.shear.x = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.ShY", "Shear Y", fmtF(sel->renderTransform.shear.y),
                [sel, this](const std::string& v) { try { sel->renderTransform.shear.y = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.PX", "Pivot X", fmtF(sel->renderTransform.pivot.x),
                [sel, this](const std::string& v) { try { sel->renderTransform.pivot.x = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
            rootPanel->children.push_back(makePropertyRow("WE.Det.RT.PY", "Pivot Y", fmtF(sel->renderTransform.pivot.y),
                [sel, this](const std::string& v) { try { sel->renderTransform.pivot.y = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));
        }
    }

    // --- Section: Text (for elements with text) ---
    if (sel->type == WidgetElementType::Text || sel->type == WidgetElementType::Button ||
        sel->type == WidgetElementType::EntryBar || sel->type == WidgetElementType::DropdownButton ||
        sel->type == WidgetElementType::Label || sel->type == WidgetElementType::ToggleButton ||
        sel->type == WidgetElementType::RadioButton || sel->type == WidgetElementType::CheckBox)
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecText", "Text", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("WE.Det.Text", "Text", sel->text,
            [sel, applyChange](const std::string& v) { sel->text = v; applyChange(); }));

        rootPanel->children.push_back(makePropertyRow("WE.Det.Font", "Font", sel->font,
            [sel, applyChange](const std::string& v) { sel->font = v; applyChange(); }));

        rootPanel->children.push_back(makePropertyRow("WE.Det.FontSz", "Font Size", fmtF(sel->fontSize),
            [sel, applyChange](const std::string& v) { try { sel->fontSize = std::stof(v); } catch (...) {} applyChange(); }));

        // Text color
        rootPanel->children.push_back(makePropertyRow("WE.Det.TColR", "Text R", fmtF(sel->textColor.x),
            [sel, applyChange](const std::string& v) { try { sel->textColor.x = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.TColG", "Text G", fmtF(sel->textColor.y),
            [sel, applyChange](const std::string& v) { try { sel->textColor.y = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.TColB", "Text B", fmtF(sel->textColor.z),
            [sel, applyChange](const std::string& v) { try { sel->textColor.z = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.TColA", "Text A", fmtF(sel->textColor.w),
            [sel, this](const std::string& v) { try { sel->textColor.w = std::stof(v); } catch (...) {} markAllWidgetsDirty(); }));

        // Bold / Italic
        {
            CheckBoxWidget boldCb;
            boldCb.setLabel("Bold");
            boldCb.setChecked(sel->isBold);
            boldCb.setOnCheckedChanged([sel, this](bool c) { sel->isBold = c; markAllWidgetsDirty(); });
            WidgetElement boldEl = boldCb.toElement();
            boldEl.id = "WE.Det.Bold";
            boldEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(boldEl));
        }
        {
            CheckBoxWidget italicCb;
            italicCb.setLabel("Italic");
            italicCb.setChecked(sel->isItalic);
            italicCb.setOnCheckedChanged([sel, this](bool c) { sel->isItalic = c; markAllWidgetsDirty(); });
            WidgetElement italicEl = italicCb.toElement();
            italicEl.id = "WE.Det.Italic";
            italicEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(italicEl));
        }

        // RadioGroup for RadioButton
        if (sel->type == WidgetElementType::RadioButton)
        {
            rootPanel->children.push_back(makePropertyRow("WE.Det.RadioGrp", "Radio Group", sel->radioGroup,
                [sel, this](const std::string& v) { sel->radioGroup = v; markAllWidgetsDirty(); }));
        }
    }

    // --- Section: Image ---
    if (sel->type == WidgetElementType::Image)
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecImage", "Image", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("WE.Det.ImgPath", "Image Path", sel->imagePath,
            [sel, applyChange](const std::string& v) { sel->imagePath = v; applyChange(); }));
    }

    // --- Section: Slider ---
    if (sel->type == WidgetElementType::Slider || sel->type == WidgetElementType::ProgressBar)
    {
        WidgetElement sep{};
        sep.type = WidgetElementType::Panel;
        sep.fillX = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("WE.Det.SecValue", "Value", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("WE.Det.MinVal", "Min", fmtF(sel->minValue),
            [sel, applyChange](const std::string& v) { try { sel->minValue = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.MaxVal", "Max", fmtF(sel->maxValue),
            [sel, applyChange](const std::string& v) { try { sel->maxValue = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("WE.Det.CurVal", "Value", fmtF(sel->valueFloat),
            [sel, applyChange](const std::string& v) { try { sel->valueFloat = std::stof(v); } catch (...) {} applyChange(); }));
    }

    rightEntry->widget->markLayoutDirty();
}

void UIManager::openLandscapeManagerPopup()
{
    if (!m_renderer)
        return;

    Logger::Instance().log(Logger::Category::Input, "WorldSettings: Tools -> Landscape Manager.", Logger::LogLevel::INFO);

    if (LandscapeManager::hasExistingLandscape())
    {
        showToastMessage("A landscape already exists in the scene.", 3.0f);
        return;
    }

    constexpr int kPopupW = 420;
    constexpr int kPopupH = 340;
    PopupWindow* popup = m_renderer->openPopupWindow(
        "LandscapeManager", "Landscape Manager", kPopupW, kPopupH);
    if (!popup) return;

    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    struct LandscapeFormState
    {
        std::string name        = "Landscape";
        std::string widthStr    = "100";
        std::string depthStr    = "100";
        std::string subdivXStr  = "32";
        std::string subdivZStr  = "32";
    };
    auto state = std::make_shared<LandscapeFormState>();

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);

    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    const auto makeLabel = [&](const std::string& id, const std::string& text,
        float x0, float y0, float x1, float y1) -> WidgetElement
    {
        WidgetElement e;
        e.type      = WidgetElementType::Text;
        e.id        = id;
        e.from      = Vec2{ nx(x0), ny(y0) };
        e.to        = Vec2{ nx(x1), ny(y1) };
        e.text      = text;
        e.fontSize  = 13.0f;
        e.textColor = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
        e.textAlignV = TextAlignV::Center;
        e.padding   = Vec2{ 6.0f, 0.0f };
        return e;
    };

    const auto makeEntry = [&](const std::string& id, const std::string& val,
        float x0, float y0, float x1, float y1) -> WidgetElement
    {
        WidgetElement e;
        e.type         = WidgetElementType::EntryBar;
        e.id           = id;
        e.from         = Vec2{ nx(x0), ny(y0) };
        e.to           = Vec2{ nx(x1), ny(y1) };
        e.value        = val;
        e.fontSize     = 13.0f;
        e.color        = Vec4{ 0.18f, 0.18f, 0.22f, 1.0f };
        e.hoverColor   = Vec4{ 0.22f, 0.22f, 0.27f, 1.0f };
        e.textColor    = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
        e.padding      = Vec2{ 6.0f, 4.0f };
        e.hitTestMode = HitTestMode::Enabled;
        return e;
    };

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "LM.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.color = Vec4{ 0.13f, 0.13f, 0.16f, 1.0f };
        elements.push_back(bg);
    }

    // Title bar
    {
        WidgetElement title;
        title.type  = WidgetElementType::Panel;
        title.id    = "LM.TitleBg";
        title.from  = Vec2{ 0.0f, 0.0f };
        title.to    = Vec2{ 1.0f, ny(44.0f) };
        title.color = Vec4{ 0.09f, 0.09f, 0.13f, 1.0f };
        elements.push_back(title);

        elements.push_back(makeLabel("LM.TitleText", "Landscape Manager",
            8.0f, 0.0f, W - 8.0f, 44.0f));
    }

    constexpr float kRowH = 28.0f;
    constexpr float kRowGap = 8.0f;
    constexpr float kFormY0 = 54.0f;
    constexpr float kLabelX1 = 130.0f;
    constexpr float kEntryX0 = 138.0f;
    const float kEntryX1 = W - 12.0f;

    const auto rowY0 = [&](int row) { return kFormY0 + row * (kRowH + kRowGap); };
    const auto rowY1 = [&](int row) { return rowY0(row) + kRowH; };

    // Row 0: Name
    elements.push_back(makeLabel("LM.NameLabel", "Name:", 12.0f, rowY0(0), kLabelX1, rowY1(0)));
    {
        auto e = makeEntry("LM.NameEntry", state->name, kEntryX0, rowY0(0), kEntryX1, rowY1(0));
        e.onValueChanged = [state](const std::string& v) { state->name = v; };
        elements.push_back(e);
    }

    // Row 1: Width
    elements.push_back(makeLabel("LM.WidthLabel", "Width:", 12.0f, rowY0(1), kLabelX1, rowY1(1)));
    {
        auto e = makeEntry("LM.WidthEntry", state->widthStr, kEntryX0, rowY0(1), kEntryX1, rowY1(1));
        e.onValueChanged = [state](const std::string& v) { state->widthStr = v; };
        elements.push_back(e);
    }

    // Row 2: Depth
    elements.push_back(makeLabel("LM.DepthLabel", "Depth:", 12.0f, rowY0(2), kLabelX1, rowY1(2)));
    {
        auto e = makeEntry("LM.DepthEntry", state->depthStr, kEntryX0, rowY0(2), kEntryX1, rowY1(2));
        e.onValueChanged = [state](const std::string& v) { state->depthStr = v; };
        elements.push_back(e);
    }

    // Row 3: Subdivisions X
    elements.push_back(makeLabel("LM.SubXLabel", "Subdiv X:", 12.0f, rowY0(3), kLabelX1, rowY1(3)));
    {
        auto e = makeEntry("LM.SubXEntry", state->subdivXStr, kEntryX0, rowY0(3), kEntryX1, rowY1(3));
        e.onValueChanged = [state](const std::string& v) { state->subdivXStr = v; };
        elements.push_back(e);
    }

    // Row 4: Subdivisions Z
    elements.push_back(makeLabel("LM.SubZLabel", "Subdiv Z:", 12.0f, rowY0(4), kLabelX1, rowY1(4)));
    {
        auto e = makeEntry("LM.SubZEntry", state->subdivZStr, kEntryX0, rowY0(4), kEntryX1, rowY1(4));
        e.onValueChanged = [state](const std::string& v) { state->subdivZStr = v; };
        elements.push_back(e);
    }

    // Divider
    {
        WidgetElement sep;
        sep.type  = WidgetElementType::Panel;
        sep.id    = "LM.Sep";
        sep.from  = Vec2{ nx(8.0f), ny(rowY1(4) + 12.0f) };
        sep.to    = Vec2{ nx(W - 8.0f), ny(rowY1(4) + 14.0f) };
        sep.color = Vec4{ 0.28f, 0.28f, 0.32f, 1.0f };
        elements.push_back(sep);
    }

    const float btnY0 = rowY1(4) + 20.0f;
    const float btnY1 = btnY0 + 34.0f;

    // Create button
    {
        WidgetElement btn;
        btn.type          = WidgetElementType::Button;
        btn.id            = "LM.CreateBtn";
        btn.from          = Vec2{ nx(W - 220.0f), ny(btnY0) };
        btn.to            = Vec2{ nx(W - 114.0f), ny(btnY1) };
        btn.text          = "Create";
        btn.fontSize      = 13.0f;
        btn.color         = Vec4{ 0.12f, 0.32f, 0.12f, 1.0f };
        btn.hoverColor    = Vec4{ 0.18f, 0.46f, 0.18f, 1.0f };
        btn.textColor     = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.padding       = Vec2{ 8.0f, 4.0f };
        btn.hitTestMode = HitTestMode::Enabled;
        btn.onClicked     = [state, this]()
        {
            LandscapeParams p;
            p.name   = state->name.empty() ? "Landscape" : state->name;
            try { p.width  = std::stof(state->widthStr); }  catch(...) { p.width  = 100.0f; }
            try { p.depth  = std::stof(state->depthStr); }  catch(...) { p.depth  = 100.0f; }
            try { p.subdivisionsX = std::stoi(state->subdivXStr); } catch(...) { p.subdivisionsX = 32; }
            try { p.subdivisionsZ = std::stoi(state->subdivZStr); } catch(...) { p.subdivisionsZ = 32; }

            const ECS::Entity entity = LandscapeManager::spawnLandscape(p);
            if (entity != 0)
            {
                refreshWorldOutliner();
                selectEntity(entity);
                showToastMessage("Landscape created: " + p.name, 3.0f);

                // Push undo/redo for landscape creation
                UndoRedoManager::Command cmd;
                cmd.description = "Create Landscape " + p.name;
                cmd.execute = [entity]() {};
                cmd.undo = [entity]()
                    {
                        auto& e = ECS::ECSManager::Instance();
                        auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
                        if (lvl) lvl->onEntityRemoved(entity);
                        e.removeEntity(entity);
                    };
                UndoRedoManager::Instance().pushCommand(std::move(cmd));
            }
            else
            {
                showToastMessage("Failed to create landscape.", 3.0f);
            }
            m_renderer->closePopupWindow("LandscapeManager");
        };
        elements.push_back(btn);
    }

    // Cancel button
    {
        WidgetElement btn;
        btn.type          = WidgetElementType::Button;
        btn.id            = "LM.CancelBtn";
        btn.from          = Vec2{ nx(W - 104.0f), ny(btnY0) };
        btn.to            = Vec2{ nx(W - 12.0f),  ny(btnY1) };
        btn.text          = "Cancel";
        btn.fontSize      = 13.0f;
        btn.color         = Vec4{ 0.22f, 0.22f, 0.25f, 1.0f };
        btn.hoverColor    = Vec4{ 0.32f, 0.32f, 0.36f, 1.0f };
        btn.textColor     = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.padding       = Vec2{ 8.0f, 4.0f };
        btn.hitTestMode = HitTestMode::Enabled;
        btn.onClicked     = [this]()
        {
            m_renderer->closePopupWindow("LandscapeManager");
        };
        elements.push_back(btn);
    }

    auto widget = std::make_shared<Widget>();
    widget->setName("LandscapeManagerWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("LandscapeManager.Main", widget);
}

void UIManager::openEngineSettingsPopup()
{
    if (!m_renderer)
        return;

    constexpr int kPopupW = 620;
    constexpr int kPopupH = 480;
    PopupWindow* popup = m_renderer->openPopupWindow(
        "EngineSettings", "Engine Settings", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    struct SettingsState { int activeCategory{ 0 }; };
    auto state = std::make_shared<SettingsState>();

    const std::vector<std::string> categories = { "General", "Rendering", "Debug", "Physics" };
    constexpr float kSidebarW = 140.0f;
    constexpr float kTitleH = 44.0f;

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "ES.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.color = Vec4{ 0.13f, 0.13f, 0.16f, 1.0f };
        elements.push_back(bg);
    }

    // Title bar
    {
        WidgetElement title;
        title.type  = WidgetElementType::Panel;
        title.id    = "ES.TitleBg";
        title.from  = Vec2{ 0.0f, 0.0f };
        title.to    = Vec2{ 1.0f, ny(kTitleH) };
        title.color = Vec4{ 0.09f, 0.09f, 0.13f, 1.0f };
        elements.push_back(title);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "ES.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(kTitleH) };
        titleText.text      = "Engine Settings";
        titleText.fontSize  = 14.0f;
        titleText.textColor = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 6.0f, 0.0f };
        elements.push_back(titleText);
    }

    // Sidebar background
    {
        WidgetElement sidebarBg;
        sidebarBg.type  = WidgetElementType::Panel;
        sidebarBg.id    = "ES.SidebarBg";
        sidebarBg.from  = Vec2{ 0.0f, ny(kTitleH) };
        sidebarBg.to    = Vec2{ nx(kSidebarW), 1.0f };
        sidebarBg.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
        elements.push_back(sidebarBg);
    }

    // Sidebar separator line
    {
        WidgetElement sep;
        sep.type  = WidgetElementType::Panel;
        sep.id    = "ES.SidebarSep";
        sep.from  = Vec2{ nx(kSidebarW - 1.0f), ny(kTitleH) };
        sep.to    = Vec2{ nx(kSidebarW), 1.0f };
        sep.color = Vec4{ 0.25f, 0.25f, 0.30f, 1.0f };
        elements.push_back(sep);
    }

    constexpr float kCatBtnH = 32.0f;
    constexpr float kCatBtnGap = 2.0f;
    constexpr float kCatBtnY0 = kTitleH + 8.0f;

    Renderer* renderer = m_renderer;
    auto rebuildContent = [state, popup, categories, nx, ny, W, H, kSidebarW, kTitleH, renderer]()
    {
        auto& pMgr = popup->uiManager();
        auto* entry = pMgr.findElementById("ES.ContentArea");
        if (!entry) return;

        entry->children.clear();

        constexpr float kRowH = 30.0f;
        constexpr float kRowGap = 6.0f;
        constexpr float kContentPad = 16.0f;
        const float contentW = W - kSidebarW;
        int row = 0;

        const auto addSectionLabel = [&](const std::string& id, const std::string& label)
        {
            WidgetElement sec;
            sec.type      = WidgetElementType::Text;
            sec.id        = id;
            sec.text      = label;
            sec.fontSize  = 13.0f;
            sec.textColor = Vec4{ 0.55f, 0.60f, 0.70f, 1.0f };
            sec.textAlignV = TextAlignV::Center;
            sec.padding   = Vec2{ 4.0f, 0.0f };
            sec.minSize   = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            entry->children.push_back(sec);
            ++row;
        };

        const auto addSeparator = [&](const std::string& id)
        {
            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = id;
            sep.color   = Vec4{ 0.28f, 0.28f, 0.32f, 1.0f };
            sep.minSize = Vec2{ contentW - kContentPad * 2.0f, 2.0f };
            entry->children.push_back(sep);
            ++row;
        };

        const auto addFloatEntry = [&](const std::string& id, const std::string& label,
            const std::string& value, std::function<void(const std::string&)> onChange)
        {
            WidgetElement rowPanel;
            rowPanel.type        = WidgetElementType::StackPanel;
            rowPanel.id          = id + ".Row";
            rowPanel.orientation = StackOrientation::Horizontal;
            rowPanel.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            rowPanel.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            rowPanel.padding     = Vec2{ 6.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = 13.0f;
            lbl.textColor = Vec4{ 0.88f, 0.88f, 0.92f, 1.0f };
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ 140.0f, kRowH };
            lbl.padding   = Vec2{ 0.0f, 0.0f };
            rowPanel.children.push_back(lbl);

            WidgetElement eb;
            eb.type          = WidgetElementType::EntryBar;
            eb.id            = id;
            eb.value         = value;
            eb.fontSize      = 12.0f;
            eb.color         = Vec4{ 0.18f, 0.18f, 0.22f, 1.0f };
            eb.hoverColor    = Vec4{ 0.22f, 0.22f, 0.27f, 1.0f };
            eb.textColor     = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
            eb.padding       = Vec2{ 6.0f, 4.0f };
            eb.hitTestMode = HitTestMode::Enabled;
            eb.minSize       = Vec2{ contentW - kContentPad * 2.0f - 140.0f - 12.0f, kRowH };
            eb.onValueChanged = std::move(onChange);
            rowPanel.children.push_back(eb);

            entry->children.push_back(rowPanel);
            ++row;
        };

        const auto addCheckbox = [&](const std::string& id, const std::string& label,
            bool checked, std::function<void(bool)> onChange)
        {
            WidgetElement cb;
            cb.type          = WidgetElementType::CheckBox;
            cb.id            = id;
            cb.text          = label;
            cb.fontSize      = 13.0f;
            cb.isChecked     = checked;
            cb.color         = Vec4{ 0.18f, 0.18f, 0.22f, 1.0f };
            cb.hoverColor    = Vec4{ 0.24f, 0.24f, 0.30f, 1.0f };
            cb.fillColor     = Vec4{ 0.25f, 0.6f, 0.95f, 1.0f };
            cb.textColor     = Vec4{ 0.88f, 0.88f, 0.92f, 1.0f };
            cb.padding       = Vec2{ 6.0f, 4.0f };
            cb.hitTestMode = HitTestMode::Enabled;
            cb.minSize       = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            cb.onCheckedChanged = std::move(onChange);
            entry->children.push_back(cb);
            ++row;
        };

        const auto addDropdown = [&](const std::string& id, const std::string& label,
            const std::vector<std::string>& items, int selected,
            std::function<void(int)> onChange)
        {
            WidgetElement rowPanel;
            rowPanel.type        = WidgetElementType::StackPanel;
            rowPanel.id          = id + ".Row";
            rowPanel.orientation = StackOrientation::Horizontal;
            rowPanel.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            rowPanel.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            rowPanel.padding     = Vec2{ 6.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = 13.0f;
            lbl.textColor = Vec4{ 0.88f, 0.88f, 0.92f, 1.0f };
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ 140.0f, kRowH };
            lbl.padding   = Vec2{ 0.0f, 0.0f };
            rowPanel.children.push_back(lbl);

            WidgetElement dd;
            dd.type          = WidgetElementType::DropDown;
            dd.id            = id;
            dd.items         = items;
            dd.selectedIndex = selected;
            dd.fontSize      = 12.0f;
            dd.color         = Vec4{ 0.18f, 0.18f, 0.22f, 1.0f };
            dd.hoverColor    = Vec4{ 0.22f, 0.22f, 0.27f, 1.0f };
            dd.textColor     = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
            dd.padding       = Vec2{ 6.0f, 4.0f };
            dd.hitTestMode = HitTestMode::Enabled;
            dd.minSize       = Vec2{ contentW - kContentPad * 2.0f - 140.0f - 12.0f, kRowH };
            dd.onSelectionChanged = std::move(onChange);
            rowPanel.children.push_back(dd);

            entry->children.push_back(rowPanel);
            ++row;
        };

        if (state->activeCategory == 0) // General
        {
            addSectionLabel("ES.C.Sec.Startup", "Startup");
            const bool isSplash = []() {
                if (auto v = DiagnosticsManager::Instance().getState("StartupMode"))
                    return *v == "normal";
                return true; // default to normal (splash)
            }();
            addCheckbox("ES.C.SplashScreen", "Splash Screen (Normal Startup)", isSplash,
                [](bool v) {
                    DiagnosticsManager::Instance().setState("StartupMode", v ? "normal" : "fast");
                });

            addSeparator("ES.C.Sep.Input");
            addSectionLabel("ES.C.Sec.Input", "Input");
            const bool isLaptop = []() {
                if (auto v = DiagnosticsManager::Instance().getState("LaptopMode"))
                    return *v == "1";
                return false;
            }();
            addCheckbox("ES.C.LaptopMode", "Laptop Mode (WASD without right-click)", isLaptop,
                [](bool v) {
                    DiagnosticsManager::Instance().setState("LaptopMode", v ? "1" : "0");
                });

            addSeparator("ES.C.Sep.Project");
            addSectionLabel("ES.C.Sec.Project", "Project");
            const bool hasDefault = []() {
                auto v = DiagnosticsManager::Instance().getState("DefaultProject");
                return v && !v->empty();
            }();
            addCheckbox("ES.C.DefaultProject", "Remember project on startup", hasDefault,
                [](bool v) {
                    if (!v)
                    {
                        DiagnosticsManager::Instance().setState("DefaultProject", "");
                    }
                    else
                    {
                        const auto& info = DiagnosticsManager::Instance().getProjectInfo();
                        if (!info.projectPath.empty())
                            DiagnosticsManager::Instance().setState("DefaultProject", info.projectPath);
                    }
                });
        }
        else if (state->activeCategory == 1) // Rendering
        {
            addSectionLabel("ES.C.Sec.Lighting", "Lighting");
            addCheckbox("ES.C.Shadows", "Shadows",
                renderer->isShadowsEnabled(),
                [renderer](bool v) {
                    renderer->setShadowsEnabled(v);
                    DiagnosticsManager::Instance().setState("ShadowsEnabled", v ? "1" : "0");
                });
            addSeparator("ES.C.Sep1");
            addSectionLabel("ES.C.Sec.Display", "Display");
            addCheckbox("ES.C.VSync", "VSync",
                renderer->isVSyncEnabled(),
                [renderer](bool v) {
                    renderer->setVSyncEnabled(v);
                    DiagnosticsManager::Instance().setState("VSyncEnabled", v ? "1" : "0");
                });
            addCheckbox("ES.C.Wireframe", "Wireframe Mode",
                renderer->isWireframeEnabled(),
                [renderer](bool v) {
                    renderer->setWireframeEnabled(v);
                    DiagnosticsManager::Instance().setState("WireframeEnabled", v ? "1" : "0");
                });
            addSeparator("ES.C.Sep2");
            addSectionLabel("ES.C.Sec.Perf", "Performance");
            addCheckbox("ES.C.Occlusion", "Occlusion Culling",
                renderer->isOcclusionCullingEnabled(),
                [renderer](bool v) {
                    renderer->setOcclusionCullingEnabled(v);
                    DiagnosticsManager::Instance().setState("OcclusionCullingEnabled", v ? "1" : "0");
                });
        }
        else if (state->activeCategory == 2) // Debug
        {
            addSectionLabel("ES.C.Sec.Vis", "Visualizations");
            addCheckbox("ES.C.UIDebug", "UI Debug Outlines",
                renderer->isUIDebugEnabled(),
                [renderer](bool v) {
                    if (v != renderer->isUIDebugEnabled()) renderer->toggleUIDebug();
                    DiagnosticsManager::Instance().setState("UIDebugEnabled", v ? "1" : "0");
                });
            addCheckbox("ES.C.BoundsDebug", "Bounding Box Debug",
                renderer->isBoundsDebugEnabled(),
                [renderer](bool v) {
                    if (v != renderer->isBoundsDebugEnabled()) renderer->toggleBoundsDebug();
                    DiagnosticsManager::Instance().setState("BoundsDebugEnabled", v ? "1" : "0");
                });
            addCheckbox("ES.C.HFDebug", "HeightField Debug",
                renderer->isHeightFieldDebugEnabled(),
                [renderer](bool v) {
                    renderer->setHeightFieldDebugEnabled(v);
                    DiagnosticsManager::Instance().setState("HeightFieldDebugEnabled", v ? "1" : "0");
                });
        }
        else if (state->activeCategory == 3) // Physics
        {
            auto& diag = DiagnosticsManager::Instance();

            // ── Backend selector ────────────────────────────────────
            addSectionLabel("ES.C.Sec.Backend", "Backend");
            {
                std::vector<std::string> backendItems;
                backendItems.push_back("Jolt");
#ifdef ENGINE_PHYSX_BACKEND_AVAILABLE
                backendItems.push_back("PhysX (experimental)");
#endif
                // Determine current selection from persisted state
                int selectedBackend = 0; // default: Jolt
                if (auto v = diag.getState("PhysicsBackend"))
                {
#ifdef ENGINE_PHYSX_BACKEND_AVAILABLE
                    if (*v == "PhysX") selectedBackend = 1;
#endif
                }

                addDropdown("ES.C.Backend", "Physics Backend",
                    backendItems, selectedBackend,
                    [](int index) {
                        std::string name = "Jolt";
#ifdef ENGINE_PHYSX_BACKEND_AVAILABLE
                        if (index == 1) name = "PhysX";
#endif
                        DiagnosticsManager::Instance().setState("PhysicsBackend", name);
                    });
            }

            addSeparator("ES.C.Sep.PhysBackend");

            addSectionLabel("ES.C.Sec.Gravity", "Gravity");

            // Read current values from DiagnosticsManager (persisted) or PhysicsWorld defaults
            auto readFloat = [&](const std::string& key, float fallback) -> std::string {
                if (auto v = diag.getState(key)) return *v;
                std::ostringstream ss; ss << fallback; return ss.str();
            };

            addFloatEntry("ES.C.GravityX", "Gravity X",
                readFloat("PhysicsGravityX", 0.0f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsGravityX", v);
                });
            addFloatEntry("ES.C.GravityY", "Gravity Y",
                readFloat("PhysicsGravityY", -9.81f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsGravityY", v);
                });
            addFloatEntry("ES.C.GravityZ", "Gravity Z",
                readFloat("PhysicsGravityZ", 0.0f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsGravityZ", v);
                });

            addSeparator("ES.C.Sep.Phys1");
            addSectionLabel("ES.C.Sec.Simulation", "Simulation");

            addFloatEntry("ES.C.FixedTimestep", "Fixed Timestep (s)",
                readFloat("PhysicsFixedTimestep", 1.0f / 60.0f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsFixedTimestep", v);
                });

            addSeparator("ES.C.Sep.Phys2");
            addSectionLabel("ES.C.Sec.Sleep", "Sleep / Deactivation");

            addFloatEntry("ES.C.SleepThreshold", "Sleep Threshold",
                readFloat("PhysicsSleepThreshold", 0.05f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsSleepThreshold", v);
                });
        }

        for (size_t ci = 0; ci < categories.size(); ++ci)
        {
            const std::string btnId = "ES.Cat." + std::to_string(ci);
            auto* catBtn = pMgr.findElementById(btnId);
            if (catBtn)
            {
                catBtn->color = (static_cast<int>(ci) == state->activeCategory)
                    ? Vec4{ 0.20f, 0.20f, 0.28f, 1.0f }
                    : Vec4{ 0.10f, 0.10f, 0.13f, 0.0f };
            }
        }

        pMgr.markAllWidgetsDirty();
    };

    for (size_t ci = 0; ci < categories.size(); ++ci)
    {
        const float by0 = kCatBtnY0 + static_cast<float>(ci) * (kCatBtnH + kCatBtnGap);
        const float by1 = by0 + kCatBtnH;

        WidgetElement catBtn;
        catBtn.type          = WidgetElementType::Button;
        catBtn.id            = "ES.Cat." + std::to_string(ci);
        catBtn.from          = Vec2{ nx(4.0f), ny(by0) };
        catBtn.to            = Vec2{ nx(kSidebarW - 4.0f), ny(by1) };
        catBtn.text          = categories[ci];
        catBtn.fontSize      = 13.0f;
        catBtn.color         = (static_cast<int>(ci) == state->activeCategory)
            ? Vec4{ 0.20f, 0.20f, 0.28f, 1.0f }
            : Vec4{ 0.10f, 0.10f, 0.13f, 0.0f };
        catBtn.hoverColor    = Vec4{ 0.22f, 0.22f, 0.28f, 1.0f };
        catBtn.textColor     = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
        catBtn.textAlignH    = TextAlignH::Left;
        catBtn.textAlignV    = TextAlignV::Center;
        catBtn.padding       = Vec2{ 12.0f, 4.0f };
        catBtn.hitTestMode = HitTestMode::Enabled;

        const int catIndex = static_cast<int>(ci);
        catBtn.onClicked = [state, catIndex, rebuildContent]()
        {
            if (state->activeCategory != catIndex)
            {
                state->activeCategory = catIndex;
                rebuildContent();
            }
        };
        elements.push_back(catBtn);
    }

    // Content area
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "ES.ContentArea";
        content.from        = Vec2{ nx(kSidebarW + 16.0f), ny(kTitleH + 16.0f) };
        content.to          = Vec2{ nx(W - 8.0f), ny(H - 8.0f) };
        content.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;
        elements.push_back(content);
    }

    auto widget = std::make_shared<Widget>();
    widget->setName("EngineSettingsWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("EngineSettings.Main", widget);

    rebuildContent();
}

// ─────────────────────────────────────────────────────────────────────────────
// Project Selection Screen
// ─────────────────────────────────────────────────────────────────────────────
void UIManager::openProjectScreen(std::function<void(const std::string& projectPath, bool isNew, bool setAsDefault, bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI)> onProjectChosen)
{
    if (!m_renderer)
        return;

    if (findWidgetEntry("ProjectScreen.Main"))
        return;

    constexpr float kScreenW = 720.0f;
    constexpr float kScreenH = 540.0f;
    Renderer* renderer = m_renderer;
    UIManager* screenMgr = this;
    SDL_Window* hostWindow = renderer ? renderer->window() : nullptr;

    const float W = kScreenW;
    const float H = kScreenH;
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    struct PSState
    {
        int activeCategory{ 0 };
        std::string newProjectName{ "MyProject" };
        std::string newProjectLocation;
        bool setAsDefault{ false };
        bool includeDefaultContent{ true };
        int selectedRHI{ 0 };
    };
    auto state = std::make_shared<PSState>();

#if defined(_WIN32)
    if (const char* userProfile = std::getenv("USERPROFILE"))
        state->newProjectLocation = (std::filesystem::path(userProfile) / "Documents").string();
#else
    if (const char* home = std::getenv("HOME"))
        state->newProjectLocation = (std::filesystem::path(home) / "Documents").string();
#endif
    if (state->newProjectLocation.empty())
        state->newProjectLocation = std::filesystem::current_path().string();

    const std::vector<std::string> categories = { "Recent Projects", "Open Project", "New Project" };
    constexpr float kSidebarW = 170.0f;
    constexpr float kTitleH = 48.0f;
    constexpr float kFooterH = 40.0f;

    std::vector<WidgetElement> elements;

    // ── Background ─────────────────────────────────────────────────────
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "PS.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.color = Vec4{ 0.11f, 0.11f, 0.14f, 1.0f };
        elements.push_back(bg);
    }

    // ── Title bar (accent stripe) ──────────────────────────────────────
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "PS.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.color = Vec4{ 0.14f, 0.16f, 0.22f, 1.0f };
        elements.push_back(titleBg);

        WidgetElement accent;
        accent.type  = WidgetElementType::Panel;
        accent.id    = "PS.TitleAccent";
        accent.from  = Vec2{ 0.0f, ny(kTitleH - 2.0f) };
        accent.to    = Vec2{ 1.0f, ny(kTitleH) };
        accent.color = Vec4{ 0.30f, 0.55f, 0.95f, 1.0f };
        elements.push_back(accent);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "PS.TitleText";
        titleText.from      = Vec2{ nx(12.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 12.0f), ny(kTitleH - 2.0f) };
        titleText.text      = "HorizonEngine  —  Project Selection";
        titleText.fontSize  = 16.0f;
        titleText.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 6.0f, 0.0f };
        elements.push_back(titleText);
    }

    // ── Sidebar ────────────────────────────────────────────────────────
    {
        WidgetElement sidebarBg;
        sidebarBg.type  = WidgetElementType::Panel;
        sidebarBg.id    = "PS.SidebarBg";
        sidebarBg.from  = Vec2{ 0.0f, ny(kTitleH) };
        sidebarBg.to    = Vec2{ nx(kSidebarW), 1.0f };
        sidebarBg.color = Vec4{ 0.09f, 0.09f, 0.11f, 1.0f };
        elements.push_back(sidebarBg);

        WidgetElement sep;
        sep.type  = WidgetElementType::Panel;
        sep.id    = "PS.SidebarSep";
        sep.from  = Vec2{ nx(kSidebarW - 1.0f), ny(kTitleH) };
        sep.to    = Vec2{ nx(kSidebarW), 1.0f };
        sep.color = Vec4{ 0.28f, 0.30f, 0.38f, 1.0f };
        elements.push_back(sep);
    }

    // ── Footer background (for checkbox) ───────────────────────────────
    {
        WidgetElement footerSep;
        footerSep.type  = WidgetElementType::Panel;
        footerSep.id    = "PS.FooterSep";
        footerSep.from  = Vec2{ nx(kSidebarW), ny(H - kFooterH - 1.0f) };
        footerSep.to    = Vec2{ 1.0f, ny(H - kFooterH) };
        footerSep.color = Vec4{ 0.28f, 0.30f, 0.38f, 1.0f };
        elements.push_back(footerSep);

        WidgetElement footerBg;
        footerBg.type  = WidgetElementType::Panel;
        footerBg.id    = "PS.FooterBg";
        footerBg.from  = Vec2{ nx(kSidebarW), ny(H - kFooterH) };
        footerBg.to    = Vec2{ 1.0f, 1.0f };
        footerBg.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
        elements.push_back(footerBg);
    }

    // ── "Set as default" checkbox (always visible in footer) ───────────
    {
        WidgetElement cb;
        cb.type          = WidgetElementType::CheckBox;
        cb.id            = "PS.DefaultCB";
        cb.text          = "Set as default project (skip this screen on next start)";
        cb.fontSize      = 12.0f;
        cb.isChecked     = state->setAsDefault;
        cb.color         = Vec4{ 0.22f, 0.22f, 0.28f, 1.0f };
        cb.hoverColor    = Vec4{ 0.28f, 0.30f, 0.38f, 1.0f };
        cb.fillColor     = Vec4{ 0.30f, 0.55f, 0.95f, 1.0f };
        cb.textColor     = Vec4{ 0.88f, 0.90f, 0.95f, 1.0f };
        cb.padding       = Vec2{ 8.0f, 4.0f };
        cb.hitTestMode = HitTestMode::Enabled;
        cb.from          = Vec2{ nx(kSidebarW + 12.0f), ny(H - kFooterH + 4.0f) };
        cb.to            = Vec2{ nx(W - 12.0f), ny(H - 4.0f) };
        cb.onCheckedChanged = [state](bool v) { state->setAsDefault = v; };
        elements.push_back(cb);
    }

    constexpr float kCatBtnH = 34.0f;
    constexpr float kCatBtnGap = 2.0f;
    constexpr float kCatBtnY0 = kTitleH + 10.0f;

    auto callbackPtr = std::make_shared<std::function<void(const std::string&, bool, bool, bool, DiagnosticsManager::RHIType)>>(std::move(onProjectChosen));
    auto closeScreen = [screenMgr]()
    {
        if (screenMgr)
        {
            screenMgr->unregisterWidget("ProjectScreen.Main");
        }
    };
    auto rebuildContentPtr = std::make_shared<std::function<void()>>();

    // ── Content builder ────────────────────────────────────────────────
    *rebuildContentPtr = [state, screenMgr, hostWindow, categories, nx, ny, W, H, kSidebarW, kTitleH, kFooterH, renderer, callbackPtr, closeScreen, rebuildContentPtr]()
    {
        auto& pMgr = *screenMgr;
        auto* entry = pMgr.findElementById("PS.ContentArea");
        if (!entry) return;

        entry->children.clear();

        constexpr float kRowH = 32.0f;
        constexpr float kContentPad = 16.0f;
        const float contentW = W - kSidebarW;

        const auto addSectionLabel = [&](const std::string& id, const std::string& label)
        {
            WidgetElement sec;
            sec.type      = WidgetElementType::Text;
            sec.id        = id;
            sec.text      = label;
            sec.fontSize  = 14.0f;
            sec.textColor = Vec4{ 0.45f, 0.60f, 0.90f, 1.0f };
            sec.textAlignV = TextAlignV::Center;
            sec.padding   = Vec2{ 4.0f, 2.0f };
            sec.minSize   = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            entry->children.push_back(sec);
        };

        const auto addSeparator = [&](const std::string& id)
        {
            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = id;
            sep.color   = Vec4{ 0.28f, 0.30f, 0.38f, 1.0f };
            sep.minSize = Vec2{ contentW - kContentPad * 2.0f, 1.0f };
            entry->children.push_back(sep);
        };

        const auto addActionButton = [&](const std::string& id, const std::string& label,
            const Vec4& bgColor, const Vec4& hoverColor, std::function<void()> onClick)
        {
            WidgetElement btn;
            btn.type          = WidgetElementType::Button;
            btn.id            = id;
            btn.text          = label;
            btn.fontSize      = 14.0f;
            btn.color         = bgColor;
            btn.hoverColor    = hoverColor;
            btn.textColor     = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            btn.textAlignH    = TextAlignH::Center;
            btn.textAlignV    = TextAlignV::Center;
            btn.padding       = Vec2{ 16.0f, 8.0f };
            btn.hitTestMode = HitTestMode::Enabled;
            btn.minSize       = Vec2{ contentW - kContentPad * 2.0f, 38.0f };
            btn.shaderVertex   = "button_vertex.glsl";
            btn.shaderFragment = "button_fragment.glsl";
            btn.onClicked     = std::move(onClick);
            entry->children.push_back(btn);
        };

        const auto addSmallButton = [&](const std::string& id, const std::string& label,
            std::function<void()> onClick)
        {
            WidgetElement btn;
            btn.type          = WidgetElementType::Button;
            btn.id            = id;
            btn.text          = label;
            btn.fontSize      = 12.0f;
            btn.color         = Vec4{ 0.18f, 0.20f, 0.26f, 1.0f };
            btn.hoverColor    = Vec4{ 0.25f, 0.28f, 0.36f, 1.0f };
            btn.textColor     = Vec4{ 0.85f, 0.88f, 0.95f, 1.0f };
            btn.textAlignH    = TextAlignH::Center;
            btn.textAlignV    = TextAlignV::Center;
            btn.padding       = Vec2{ 12.0f, 6.0f };
            btn.hitTestMode = HitTestMode::Enabled;
            btn.minSize       = Vec2{ 160.0f, 30.0f };
            btn.shaderVertex   = "button_vertex.glsl";
            btn.shaderFragment = "button_fragment.glsl";
            btn.onClicked     = std::move(onClick);
            entry->children.push_back(btn);
        };

        const auto addEntryRow = [&](const std::string& id, const std::string& label,
            const std::string& value, std::function<void(const std::string&)> onChange)
        {
            WidgetElement rowPanel;
            rowPanel.type        = WidgetElementType::StackPanel;
            rowPanel.id          = id + ".Row";
            rowPanel.orientation = StackOrientation::Horizontal;
            rowPanel.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            rowPanel.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            rowPanel.padding     = Vec2{ 6.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = 13.0f;
            lbl.textColor = Vec4{ 0.80f, 0.82f, 0.88f, 1.0f };
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ 120.0f, kRowH };
            rowPanel.children.push_back(lbl);

            WidgetElement eb;
            eb.type          = WidgetElementType::EntryBar;
            eb.id            = id;
            eb.value         = value;
            eb.fontSize      = 13.0f;
            eb.color         = Vec4{ 0.15f, 0.16f, 0.20f, 1.0f };
            eb.hoverColor    = Vec4{ 0.20f, 0.22f, 0.28f, 1.0f };
            eb.textColor     = Vec4{ 0.95f, 0.95f, 0.97f, 1.0f };
            eb.padding       = Vec2{ 8.0f, 5.0f };
            eb.hitTestMode = HitTestMode::Enabled;
            eb.minSize       = Vec2{ contentW - kContentPad * 2.0f - 120.0f - 12.0f, kRowH };
            eb.onValueChanged = std::move(onChange);
            rowPanel.children.push_back(eb);

            entry->children.push_back(rowPanel);
        };

        // ── Category: Recent Projects ──────────────────────────────────
        if (state->activeCategory == 0)
        {
            addSectionLabel("PS.C.Sec.Recent", "Known Projects");
            addSeparator("PS.C.Sep.Recent");

            const auto knownProjects = DiagnosticsManager::Instance().getKnownProjects();
            if (knownProjects.empty())
            {
                WidgetElement noProjects;
                noProjects.type      = WidgetElementType::Text;
                noProjects.id        = "PS.C.NoProjects";
                noProjects.text      = "No known projects yet.\nOpen or create a project to get started.";
                noProjects.fontSize  = 13.0f;
                noProjects.textColor = Vec4{ 0.45f, 0.47f, 0.55f, 1.0f };
                noProjects.textAlignV = TextAlignV::Center;
                noProjects.padding   = Vec2{ 10.0f, 16.0f };
                noProjects.minSize   = Vec2{ contentW - kContentPad * 2.0f, 80.0f };
                entry->children.push_back(noProjects);
            }
            else
            {
                // Column header row
                {
                    WidgetElement hdrRow;
                    hdrRow.type        = WidgetElementType::StackPanel;
                    hdrRow.id          = "PS.C.ListHdr";
                    hdrRow.orientation = StackOrientation::Horizontal;
                    hdrRow.minSize     = Vec2{ contentW - kContentPad * 2.0f, 26.0f };
                    hdrRow.color       = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
                    hdrRow.padding     = Vec2{ 18.0f, 2.0f };

                    WidgetElement hdrName;
                    hdrName.type      = WidgetElementType::Text;
                    hdrName.id        = "PS.C.ListHdr.Name";
                    hdrName.text      = "Project";
                    hdrName.fontSize  = 11.0f;
                    hdrName.textColor = Vec4{ 0.45f, 0.50f, 0.65f, 1.0f };
                    hdrName.textAlignV = TextAlignV::Center;
                    hdrName.minSize   = Vec2{ 180.0f, 24.0f };
                    hdrRow.children.push_back(hdrName);

                    WidgetElement hdrPath;
                    hdrPath.type      = WidgetElementType::Text;
                    hdrPath.id        = "PS.C.ListHdr.Path";
                    hdrPath.text      = "Location";
                    hdrPath.fontSize  = 11.0f;
                    hdrPath.textColor = Vec4{ 0.45f, 0.50f, 0.65f, 1.0f };
                    hdrPath.textAlignV = TextAlignV::Center;
                    hdrPath.fillX     = true;
                    hdrPath.minSize   = Vec2{ 0.0f, 24.0f };
                    hdrRow.children.push_back(hdrPath);

                    entry->children.push_back(hdrRow);
                }

                addSeparator("PS.C.Sep.ListHdr");

                for (size_t i = 0; i < knownProjects.size(); ++i)
                {
                    const std::string& projPath = knownProjects[i];
                    const std::string projName = std::filesystem::path(projPath).filename().string();
                    const bool exists = std::filesystem::exists(projPath);
                    const std::string rowId = "PS.C.Proj." + std::to_string(i);
                    const bool isEven = (i % 2 == 0);

                    // Outer row container – holds accent bar + content
                    WidgetElement row;
                    row.type        = WidgetElementType::StackPanel;
                    row.id          = rowId + ".Row";
                    row.orientation = StackOrientation::Horizontal;
                    row.minSize     = Vec2{ contentW - kContentPad * 2.0f, 44.0f };
                    row.color       = isEven
                        ? Vec4{ 0.15f, 0.15f, 0.20f, 1.0f }
                        : Vec4{ 0.11f, 0.11f, 0.14f, 1.0f };
                    row.hoverColor  = exists
                        ? Vec4{ 0.20f, 0.26f, 0.40f, 1.0f }
                        : row.color;
                    row.padding     = Vec2{ 0.0f, 0.0f };
                    row.hitTestMode = exists ? HitTestMode::Enabled : HitTestMode::DisabledSelf;

                    // Left accent bar
                    WidgetElement accent;
                    accent.type    = WidgetElementType::Panel;
                    accent.id      = rowId + ".Accent";
                    accent.color   = exists
                        ? Vec4{ 0.30f, 0.55f, 0.95f, 1.0f }
                        : Vec4{ 0.45f, 0.28f, 0.28f, 0.6f };
                    accent.minSize = Vec2{ 4.0f, 44.0f };
                    row.children.push_back(accent);

                    // Project name column
                    WidgetElement nameCol;
                    nameCol.type      = WidgetElementType::Text;
                    nameCol.id        = rowId + ".Name";
                    nameCol.text      = projName;
                    nameCol.fontSize  = 14.0f;
                    nameCol.textColor = exists
                        ? Vec4{ 0.95f, 0.97f, 1.0f, 1.0f }
                        : Vec4{ 0.50f, 0.38f, 0.38f, 1.0f };
                    nameCol.textAlignV = TextAlignV::Center;
                    nameCol.padding   = Vec2{ 12.0f, 0.0f };
                    nameCol.minSize   = Vec2{ 180.0f, 44.0f };
                    row.children.push_back(nameCol);

                    // Path column
                    WidgetElement pathCol;
                    pathCol.type      = WidgetElementType::Text;
                    pathCol.id        = rowId + ".Path";
                    pathCol.text      = exists ? projPath : (projPath + "  (not found)");
                    pathCol.fontSize  = 11.0f;
                    pathCol.textColor = exists
                        ? Vec4{ 0.48f, 0.54f, 0.68f, 1.0f }
                        : Vec4{ 0.45f, 0.30f, 0.30f, 0.8f };
                    pathCol.textAlignV = TextAlignV::Center;
                    pathCol.fillX     = true;
                    pathCol.padding   = Vec2{ 4.0f, 0.0f };
                    pathCol.minSize   = Vec2{ 0.0f, 44.0f };
                    row.children.push_back(pathCol);

                    WidgetElement removeBtn;
                    removeBtn.type = WidgetElementType::Button;
                    removeBtn.id = rowId + ".Remove";
                    removeBtn.text = "X";
                    removeBtn.fontSize = 12.0f;
                    removeBtn.color = Vec4{ 0.35f, 0.16f, 0.16f, 0.95f };
                    removeBtn.hoverColor = Vec4{ 0.55f, 0.22f, 0.22f, 1.0f };
                    removeBtn.textColor = Vec4{ 0.95f, 0.88f, 0.88f, 1.0f };
                    removeBtn.textAlignH = TextAlignH::Center;
                    removeBtn.textAlignV = TextAlignV::Center;
                    removeBtn.padding = Vec2{ 0.0f, 0.0f };
                    removeBtn.fillY = true;
                    removeBtn.minSize = Vec2{ 44.0f, 44.0f };
                    removeBtn.hitTestMode = HitTestMode::Enabled;
                    removeBtn.shaderVertex = "button_vertex.glsl";
                    removeBtn.shaderFragment = "button_fragment.glsl";
                    removeBtn.onClicked = [projPath, projName, exists, screenMgr, rebuildContentPtr]()
                    {
                        const auto removeEntry = [projPath, screenMgr, rebuildContentPtr](bool deleteFromFilesystem)
                        {
                            if (deleteFromFilesystem)
                            {
                                std::error_code removeEc;
                                std::filesystem::remove_all(std::filesystem::path(projPath), removeEc);
                                if (screenMgr)
                                {
                                    if (removeEc)
                                    {
                                        screenMgr->showToastMessage("Failed to delete project folder.", 3.0f);
                                    }
                                    else
                                    {
                                        screenMgr->showToastMessage("Deleted project folder.", 2.5f);
                                    }
                                }
                            }

                            DiagnosticsManager::Instance().removeKnownProject(projPath);
                            if (rebuildContentPtr && *rebuildContentPtr)
                            {
                                (*rebuildContentPtr)();
                            }
                        };

                        if (!exists)
                        {
                            removeEntry(false);
                            return;
                        }

                        if (screenMgr)
                        {
                            screenMgr->showConfirmDialogWithCheckbox(
                                "Remove project from list?\n" + projName,
                                "Delete from filesystem",
                                false,
                                removeEntry,
                                []() {});
                        }
                    };
                    row.children.push_back(removeBtn);

                    if (exists)
                    {
                        row.onClicked = [callbackPtr, projPath, state, closeScreen]()
                        {
                            if (*callbackPtr)
                                (*callbackPtr)(projPath, false, state->setAsDefault, state->includeDefaultContent, DiagnosticsManager::RHIType::OpenGL);
                            closeScreen();
                        };
                    }

                    entry->children.push_back(row);

                    // Separator line between rows
                    {
                        WidgetElement rowSep;
                        rowSep.type    = WidgetElementType::Panel;
                        rowSep.id      = rowId + ".Sep";
                        rowSep.color   = Vec4{ 0.22f, 0.24f, 0.30f, 1.0f };
                        rowSep.minSize = Vec2{ contentW - kContentPad * 2.0f, 1.0f };
                        entry->children.push_back(rowSep);
                    }
                }
            }
        }
        // ── Category: Open Project ─────────────────────────────────────
        else if (state->activeCategory == 1)
        {
            addSectionLabel("PS.C.Sec.Open", "Open Existing Project");
            addSeparator("PS.C.Sep.Open");

            WidgetElement desc;
            desc.type      = WidgetElementType::Text;
            desc.id        = "PS.C.OpenDesc";
            desc.text      = "Select a .project file to open an existing project.";
            desc.fontSize  = 13.0f;
            desc.textColor = Vec4{ 0.65f, 0.68f, 0.75f, 1.0f };
            desc.padding   = Vec2{ 10.0f, 12.0f };
            desc.minSize   = Vec2{ contentW - kContentPad * 2.0f, 44.0f };
            entry->children.push_back(desc);

            addActionButton("PS.C.BrowseBtn", "Browse for .project file...",
                Vec4{ 0.22f, 0.35f, 0.60f, 1.0f },
                Vec4{ 0.28f, 0.42f, 0.72f, 1.0f },
                [callbackPtr, hostWindow, state, closeScreen]()
                {
                    SDL_DialogFileFilter filters[] = {
                        { "Project Files", "project" },
                        { "All Files", "*" }
                    };

                    struct BrowseCtx
                    {
                        std::shared_ptr<std::function<void(const std::string&, bool, bool, bool, DiagnosticsManager::RHIType)>> callback;
                        std::shared_ptr<PSState> state;
                        std::function<void()> closeScreen;
                    };
                    auto* ctx = new BrowseCtx{ callbackPtr, state, closeScreen };

                    SDL_ShowOpenFileDialog(
                        [](void* userdata, const char* const* filelist, int filter)
                        {
                            auto* c = static_cast<BrowseCtx*>(userdata);
                            if (filelist && filelist[0])
                            {
                                std::filesystem::path projFile(filelist[0]);
                                std::string projDir = projFile.parent_path().string();
                                if (*(c->callback))
                                    (*(c->callback))(projDir, false, c->state->setAsDefault, c->state->includeDefaultContent, DiagnosticsManager::RHIType::OpenGL);
                                if (c->closeScreen)
                                    c->closeScreen();
                            }
                            delete c;
                        },
                        ctx,
                        hostWindow,
                        filters,
                        SDL_arraysize(filters),
                        nullptr,
                        false
                    );
                });
        }
        // ── Category: New Project ──────────────────────────────────────
        else if (state->activeCategory == 2)
        {
            addSectionLabel("PS.C.Sec.New", "Create New Project");
            addSeparator("PS.C.Sep.New");

            const auto sanitizeProjectName = [](const std::string& input) -> std::string
            {
                std::string out;
                out.reserve(input.size());
                for (char c : input)
                {
                    const unsigned char uc = static_cast<unsigned char>(c);
                    if (uc < 32)
                        continue;
                    if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                        continue;
                    out.push_back(c);
                }
                while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
                {
                    out.pop_back();
                }
                return out;
            };

            const auto updatePreviewText = [state, &pMgr]()
            {
                const std::string previewPath = (std::filesystem::path(state->newProjectLocation) / state->newProjectName).string();
                if (auto* previewEl = pMgr.findElementById("PS.C.Preview"))
                {
                    previewEl->text = "Target:  " + previewPath;
                    pMgr.markAllWidgetsDirty();
                }
            };

            addEntryRow("PS.C.ProjName", "Project Name", state->newProjectName,
                [state, updatePreviewText](const std::string& v)
                {
                    state->newProjectName = v;
                    updatePreviewText();
                });

            addEntryRow("PS.C.ProjLoc", "Location", state->newProjectLocation,
                [state, updatePreviewText](const std::string& v)
                {
                    state->newProjectLocation = v;
                    updatePreviewText();
                });

            addSmallButton("PS.C.BrowseLocBtn", "Browse...",
                [state, hostWindow, screenMgr]()
                {
                    struct FolderCtx
                    {
                        std::shared_ptr<PSState> state;
                        UIManager* ui;
                    };
                    auto* ctx = new FolderCtx{ state, screenMgr };

                    SDL_ShowOpenFolderDialog(
                        [](void* userdata, const char* const* filelist, int filter)
                        {
                            auto* c = static_cast<FolderCtx*>(userdata);
                            if (filelist && filelist[0])
                            {
                                c->state->newProjectLocation = filelist[0];
                                auto& pMgr = *c->ui;
                                if (auto* el = pMgr.findElementById("PS.C.ProjLoc"))
                                {
                                    el->value = filelist[0];
                                }
                                const std::string previewPath = (std::filesystem::path(c->state->newProjectLocation) / c->state->newProjectName).string();
                                if (auto* previewEl = pMgr.findElementById("PS.C.Preview"))
                                {
                                    previewEl->text = "Target:  " + previewPath;
                                }
                                pMgr.markAllWidgetsDirty();
                            }
                            delete c;
                        },
                        ctx,
                        hostWindow,
                        nullptr,
                        false
                    );
                });

            addSeparator("PS.C.Sep.New2");

            // Preview path
            {
                const std::string previewPath = (std::filesystem::path(state->newProjectLocation) / state->newProjectName).string();
                WidgetElement preview;
                preview.type      = WidgetElementType::Text;
                preview.id        = "PS.C.Preview";
                preview.text      = "Target:  " + previewPath;
                preview.fontSize  = 11.0f;
                preview.textColor = Vec4{ 0.45f, 0.55f, 0.70f, 1.0f };
                preview.padding   = Vec2{ 10.0f, 6.0f };
                preview.minSize   = Vec2{ contentW - kContentPad * 2.0f, 26.0f };
                entry->children.push_back(preview);
            }

            {
                WidgetElement spacer;
                spacer.type    = WidgetElementType::Panel;
                spacer.id      = "PS.C.Spacer.New";
                spacer.color   = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.minSize = Vec2{ contentW - kContentPad * 2.0f, 8.0f };
                entry->children.push_back(spacer);
            }

            {
                WidgetElement includeDefaultContentCb;
                includeDefaultContentCb.type = WidgetElementType::CheckBox;
                includeDefaultContentCb.id = "PS.C.IncludeDefaultContent";
                includeDefaultContentCb.text = "Include default content";
                includeDefaultContentCb.fontSize = 12.0f;
                includeDefaultContentCb.isChecked = state->includeDefaultContent;
                includeDefaultContentCb.color = Vec4{ 0.22f, 0.22f, 0.28f, 1.0f };
                includeDefaultContentCb.hoverColor = Vec4{ 0.28f, 0.30f, 0.38f, 1.0f };
                includeDefaultContentCb.fillColor = Vec4{ 0.30f, 0.55f, 0.95f, 1.0f };
                includeDefaultContentCb.textColor = Vec4{ 0.88f, 0.90f, 0.95f, 1.0f };
                includeDefaultContentCb.padding = Vec2{ 8.0f, 4.0f };
                includeDefaultContentCb.hitTestMode = HitTestMode::Enabled;
                includeDefaultContentCb.minSize = Vec2{ contentW - kContentPad * 2.0f, 24.0f };
                includeDefaultContentCb.onCheckedChanged = [state](bool v)
                {
                    state->includeDefaultContent = v;
                };
                entry->children.push_back(includeDefaultContentCb);
            }

            // ── RHI Type dropdown ──────────────────────────────────────
            {
                WidgetElement rhiRow;
                rhiRow.type        = WidgetElementType::StackPanel;
                rhiRow.id          = "PS.C.RHI.Row";
                rhiRow.orientation = StackOrientation::Horizontal;
                rhiRow.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
                rhiRow.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                rhiRow.padding     = Vec2{ 6.0f, 2.0f };

                WidgetElement rhiLbl;
                rhiLbl.type      = WidgetElementType::Text;
                rhiLbl.id        = "PS.C.RHI.Lbl";
                rhiLbl.text      = "Graphics API";
                rhiLbl.fontSize  = 13.0f;
                rhiLbl.textColor = Vec4{ 0.80f, 0.82f, 0.88f, 1.0f };
                rhiLbl.textAlignV = TextAlignV::Center;
                rhiLbl.minSize   = Vec2{ 120.0f, kRowH };
                rhiRow.children.push_back(rhiLbl);

                WidgetElement rhiDd;
                rhiDd.type          = WidgetElementType::DropDown;
                rhiDd.id            = "PS.C.RHI";
                rhiDd.items         = { "OpenGL" };
                rhiDd.selectedIndex = state->selectedRHI;
                rhiDd.fontSize      = 12.0f;
                rhiDd.color         = Vec4{ 0.18f, 0.18f, 0.22f, 1.0f };
                rhiDd.hoverColor    = Vec4{ 0.22f, 0.22f, 0.27f, 1.0f };
                rhiDd.textColor     = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
                rhiDd.padding       = Vec2{ 6.0f, 4.0f };
                rhiDd.hitTestMode = HitTestMode::Enabled;
                rhiDd.minSize       = Vec2{ contentW - kContentPad * 2.0f - 120.0f - 12.0f, kRowH };
                rhiDd.onSelectionChanged = [state](int idx)
                {
                    state->selectedRHI = idx;
                };
                rhiRow.children.push_back(rhiDd);

                entry->children.push_back(rhiRow);
            }

            addActionButton("PS.C.CreateBtn", "Create Project",
                Vec4{ 0.18f, 0.50f, 0.28f, 1.0f },
                Vec4{ 0.22f, 0.62f, 0.35f, 1.0f },
                [state, callbackPtr, closeScreen, sanitizeProjectName, screenMgr]()
                {
                    const std::string sanitized = sanitizeProjectName(state->newProjectName);
                    if (state->newProjectName.empty() || state->newProjectName != sanitized)
                    {
                        if (screenMgr)
                        {
                            screenMgr->showToastMessage("Invalid project name (forbidden characters).", 3.0f);
                        }
                        return;
                    }
                    const std::string fullPath = (std::filesystem::path(state->newProjectLocation) / state->newProjectName).string();

                    // Check if directory already exists to prevent crashes or unwanted overwrites
                    std::error_code existsEc;
                    if (std::filesystem::exists(std::filesystem::path(fullPath), existsEc))
                    {
                        if (screenMgr)
                        {
                            screenMgr->showConfirmDialog(
                                "A folder with this name already exists at the target location:\n\n" + fullPath + "\n\nDo you want to load the project instead?",
                                [callbackPtr, fullPath, state, closeScreen, screenMgr]()
                                {
                                    if (*callbackPtr)
                                    {
                                        constexpr DiagnosticsManager::RHIType rhiMap[] = { DiagnosticsManager::RHIType::OpenGL };
                                        const auto rhi = (state->selectedRHI >= 0 && state->selectedRHI < static_cast<int>(std::size(rhiMap))) ? rhiMap[state->selectedRHI] : DiagnosticsManager::RHIType::OpenGL;
                                        (*callbackPtr)(fullPath, false, state->setAsDefault, state->includeDefaultContent, rhi);
                                    }
                                    if (screenMgr) screenMgr->unregisterWidget("ProjectScreen.Main");
                                },
                                []() { /* Cancel */ }
                            );
                        }
                        return;
                    }

                    if (*callbackPtr)
                    {
                        constexpr DiagnosticsManager::RHIType rhiMap[] = { DiagnosticsManager::RHIType::OpenGL };
                        const auto rhi = (state->selectedRHI >= 0 && state->selectedRHI < static_cast<int>(std::size(rhiMap))) ? rhiMap[state->selectedRHI] : DiagnosticsManager::RHIType::OpenGL;
                        (*callbackPtr)(fullPath, true, state->setAsDefault, state->includeDefaultContent, rhi);
                    }
                    closeScreen();
                });
        }

        // Update sidebar highlights
        for (size_t ci = 0; ci < categories.size(); ++ci)
        {
            const std::string btnId = "PS.Cat." + std::to_string(ci);
            auto* catBtn = pMgr.findElementById(btnId);
            if (catBtn)
            {
                const bool active = (static_cast<int>(ci) == state->activeCategory);
                catBtn->color     = active
                    ? Vec4{ 0.20f, 0.26f, 0.40f, 1.0f }
                    : Vec4{ 0.09f, 0.09f, 0.11f, 0.0f };
                catBtn->textColor = active
                    ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                    : Vec4{ 0.65f, 0.68f, 0.72f, 1.0f };
            }
        }

        pMgr.markAllWidgetsDirty();
    };

    // ── Sidebar category buttons ───────────────────────────────────────
    for (size_t ci = 0; ci < categories.size(); ++ci)
    {
        const float by0 = kCatBtnY0 + static_cast<float>(ci) * (kCatBtnH + kCatBtnGap);
        const float by1 = by0 + kCatBtnH;

        const bool active = (static_cast<int>(ci) == state->activeCategory);
        WidgetElement catBtn;
        catBtn.type          = WidgetElementType::Button;
        catBtn.id            = "PS.Cat." + std::to_string(ci);
        catBtn.from          = Vec2{ nx(6.0f), ny(by0) };
        catBtn.to            = Vec2{ nx(kSidebarW - 6.0f), ny(by1) };
        catBtn.text          = categories[ci];
        catBtn.fontSize      = 13.0f;
        catBtn.color         = active
            ? Vec4{ 0.20f, 0.26f, 0.40f, 1.0f }
            : Vec4{ 0.09f, 0.09f, 0.11f, 0.0f };
        catBtn.hoverColor    = Vec4{ 0.22f, 0.28f, 0.42f, 1.0f };
        catBtn.textColor     = active
            ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
            : Vec4{ 0.65f, 0.68f, 0.72f, 1.0f };
        catBtn.textAlignH    = TextAlignH::Left;
        catBtn.textAlignV    = TextAlignV::Center;
        catBtn.padding       = Vec2{ 14.0f, 4.0f };
        catBtn.hitTestMode = HitTestMode::Enabled;
        catBtn.shaderVertex   = "button_vertex.glsl";
        catBtn.shaderFragment = "button_fragment.glsl";

        const int catIndex = static_cast<int>(ci);
        catBtn.onClicked = [state, catIndex, rebuildContentPtr]()
        {
            if (state->activeCategory != catIndex)
            {
                state->activeCategory = catIndex;
                if (rebuildContentPtr && *rebuildContentPtr)
                {
                    (*rebuildContentPtr)();
                }
            }
        };
        elements.push_back(catBtn);
    }

    // ── Content area (above footer) ────────────────────────────────────
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "PS.ContentArea";
        content.from        = Vec2{ nx(kSidebarW + 16.0f), ny(kTitleH + 12.0f) };
        content.to          = Vec2{ nx(W - 12.0f), ny(H - kFooterH - 6.0f) };
        content.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;
        elements.push_back(content);
    }

    auto widget = std::make_shared<Widget>();
    widget->setName("ProjectScreenWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    screenMgr->registerWidget("ProjectScreen.Main", widget);

    if (rebuildContentPtr && *rebuildContentPtr)
    {
        (*rebuildContentPtr)();
    }
}

// ===========================================================================
// UI Designer Tab  (Gameplay UI — operates on ViewportUIManager)
// ===========================================================================

ViewportUIManager* UIManager::getViewportUIManager() const
{
    return m_renderer ? m_renderer->getViewportUIManagerPtr() : nullptr;
}

bool UIManager::isUIDesignerOpen() const
{
    return m_uiDesignerState.isOpen;
}

// ---------------------------------------------------------------------------
// openUIDesignerTab  — creates the tab + left / right / toolbar widgets
// ---------------------------------------------------------------------------
void UIManager::openUIDesignerTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "UIDesigner";

    // If already open, just switch to it
    if (m_uiDesignerState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "UI Designer", true);
    m_renderer->setActiveTab(tabId);

    const std::string leftWidgetId    = "UIDesigner.Left";
    const std::string rightWidgetId   = "UIDesigner.Right";
    const std::string toolbarWidgetId = "UIDesigner.Toolbar";

    // Clean up any stale registrations
    unregisterWidget(leftWidgetId);
    unregisterWidget(rightWidgetId);
    unregisterWidget(toolbarWidgetId);

    // Store state
    m_uiDesignerState = {};
    m_uiDesignerState.tabId           = tabId;
    m_uiDesignerState.leftWidgetId    = leftWidgetId;
    m_uiDesignerState.rightWidgetId   = rightWidgetId;
    m_uiDesignerState.toolbarWidgetId = toolbarWidgetId;
    m_uiDesignerState.isOpen          = true;

    // --- Top toolbar: widget selector + new / delete buttons ---
    {
        auto toolbarWidget = std::make_shared<Widget>();
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
        root.color       = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
        root.runtimeOnly = true;

        // "New Widget" button
        {
            WidgetElement btn{};
            btn.id            = "UIDesigner.Toolbar.NewWidget";
            btn.type          = WidgetElementType::Button;
            btn.text          = "+ Widget";
            btn.font          = "default.ttf";
            btn.fontSize      = 13.0f;
            btn.textColor     = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
            btn.color         = Vec4{ 0.22f, 0.24f, 0.30f, 1.0f };
            btn.hoverColor    = Vec4{ 0.30f, 0.34f, 0.42f, 1.0f };
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
            btn.font          = "default.ttf";
            btn.fontSize      = 13.0f;
            btn.textColor     = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
            btn.color         = Vec4{ 0.22f, 0.24f, 0.30f, 1.0f };
            btn.hoverColor    = Vec4{ 0.50f, 0.25f, 0.25f, 1.0f };
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
            spacer.minSize     = Vec2{ 0.0f, 1.0f };
            spacer.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            spacer.runtimeOnly = true;
            root.children.push_back(std::move(spacer));
        }

        // Status label
        {
            WidgetElement lbl{};
            lbl.id          = "UIDesigner.Toolbar.Status";
            lbl.type        = WidgetElementType::Text;
            lbl.text        = "";
            lbl.font        = "default.ttf";
            lbl.fontSize    = 12.0f;
            lbl.textColor   = Vec4{ 0.65f, 0.68f, 0.75f, 1.0f };
            lbl.textAlignH  = TextAlignH::Right;
            lbl.textAlignV  = TextAlignV::Center;
            lbl.minSize     = Vec2{ 0.0f, 24.0f };
            lbl.padding     = Vec2{ 8.0f, 0.0f };
            lbl.runtimeOnly = true;
            root.children.push_back(std::move(lbl));
        }

        toolbarWidget->setElements({ std::move(root) });
        registerWidget(toolbarWidgetId, toolbarWidget, tabId);

        // Click events
        registerClickEvent("UIDesigner.Toolbar.NewWidget", [this]()
        {
            auto* vpUI = getViewportUIManager();
            if (!vpUI) return;
            static int s_newWidgetCounter = 0;
            std::string name = "Widget_" + std::to_string(++s_newWidgetCounter);
            vpUI->createWidget(name, s_newWidgetCounter * 10);
            m_uiDesignerState.selectedWidgetName = name;
            m_uiDesignerState.selectedElementId.clear();
            refreshUIDesignerHierarchy();
            refreshUIDesignerDetails();
            markAllWidgetsDirty();
        });

        registerClickEvent("UIDesigner.Toolbar.DeleteWidget", [this]()
        {
            auto* vpUI = getViewportUIManager();
            if (!vpUI || m_uiDesignerState.selectedWidgetName.empty()) return;
            vpUI->removeWidget(m_uiDesignerState.selectedWidgetName);
            m_uiDesignerState.selectedWidgetName.clear();
            m_uiDesignerState.selectedElementId.clear();
            refreshUIDesignerHierarchy();
            refreshUIDesignerDetails();
            markAllWidgetsDirty();
        });
    }

    // --- Left panel: control palette + hierarchy ---
    {
        auto leftWidget = std::make_shared<Widget>();
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
        root.color       = Vec4{ 0.12f, 0.13f, 0.17f, 0.96f };
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
            controlsSection.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            controlsSection.runtimeOnly = true;

            // Title: Controls
            {
                WidgetElement title{};
                title.id         = "UIDesigner.Left.Title";
                title.type       = WidgetElementType::Text;
                title.text       = "Controls";
                title.font       = "default.ttf";
                title.fontSize   = 16.0f;
                title.textColor  = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
                title.textAlignH = TextAlignH::Left;
                title.textAlignV = TextAlignV::Center;
                title.fillX      = true;
                title.minSize    = Vec2{ 0.0f, 28.0f };
                title.runtimeOnly = true;
                controlsSection.children.push_back(std::move(title));
            }

            // Gameplay-UI element types (subset — only those supported by ViewportUIManager)
            const std::vector<std::string> controls = {
                "Panel", "Text", "Label", "Button", "Image", "ProgressBar", "Slider",
                "WrapBox", "UniformGrid", "SizeBox", "ScaleBox", "WidgetSwitcher", "Overlay"
            };
            for (size_t i = 0; i < controls.size(); ++i)
            {
                WidgetElement item{};
                item.id            = "UIDesigner.Left.Control." + std::to_string(i);
                item.type          = WidgetElementType::Button;
                item.text          = "  " + controls[i];
                item.font          = "default.ttf";
                item.fontSize      = 14.0f;
                item.textColor     = Vec4{ 0.78f, 0.80f, 0.85f, 1.0f };
                item.color         = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                item.hoverColor    = Vec4{ 0.18f, 0.20f, 0.28f, 0.8f };
                item.textAlignH    = TextAlignH::Left;
                item.textAlignV    = TextAlignV::Center;
                item.fillX         = true;
                item.minSize       = Vec2{ 0.0f, 24.0f };
                item.hitTestMode = HitTestMode::Enabled;
                item.runtimeOnly   = true;
                item.clickEvent    = "UIDesigner.Left.Control." + std::to_string(i);
                controlsSection.children.push_back(std::move(item));

                const std::string controlType = controls[i];
                registerClickEvent("UIDesigner.Left.Control." + std::to_string(i), [this, controlType]()
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
            sep.minSize    = Vec2{ 0.0f, 1.0f };
            sep.color      = Vec4{ 0.26f, 0.28f, 0.34f, 0.8f };
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
            hierarchySection.padding     = Vec2{ 10.0f, 8.0f };
            hierarchySection.color       = Vec4{ 0.08f, 0.09f, 0.12f, 0.75f };
            hierarchySection.runtimeOnly = true;

            // Title: Hierarchy
            {
                WidgetElement treeTitle{};
                treeTitle.id         = "UIDesigner.Left.TreeTitle";
                treeTitle.type       = WidgetElementType::Text;
                treeTitle.text       = "Hierarchy";
                treeTitle.font       = "default.ttf";
                treeTitle.fontSize   = 16.0f;
                treeTitle.textColor  = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
                treeTitle.textAlignH = TextAlignH::Left;
                treeTitle.textAlignV = TextAlignV::Center;
                treeTitle.fillX      = true;
                treeTitle.minSize    = Vec2{ 0.0f, 28.0f };
                treeTitle.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(treeTitle));
            }

            // Hierarchy tree container (populated by refreshUIDesignerHierarchy)
            {
                WidgetElement hierarchyStack{};
                hierarchyStack.id          = "UIDesigner.Left.Tree";
                hierarchyStack.type        = WidgetElementType::StackPanel;
                hierarchyStack.fillX       = true;
                hierarchyStack.orientation = StackOrientation::Vertical;
                hierarchyStack.padding     = Vec2{ 2.0f, 2.0f };
                hierarchyStack.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                hierarchyStack.runtimeOnly = true;
                hierarchySection.children.push_back(std::move(hierarchyStack));
            }

            root.children.push_back(std::move(hierarchySection));
        }

        leftWidget->setElements({ std::move(root) });
        registerWidget(leftWidgetId, leftWidget, tabId);
    }

    // --- Right panel: element details (populated by refreshUIDesignerDetails) ---
    {
        auto rightWidget = std::make_shared<Widget>();
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
        root.color       = Vec4{ 0.12f, 0.13f, 0.17f, 0.96f };
        root.scrollable  = true;
        root.runtimeOnly = true;

        {
            WidgetElement title{};
            title.id         = "UIDesigner.Right.Title";
            title.type       = WidgetElementType::Text;
            title.text       = "Properties";
            title.font       = "default.ttf";
            title.fontSize   = 16.0f;
            title.textColor  = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
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
            hint.font       = "default.ttf";
            hint.fontSize   = 13.0f;
            hint.textColor  = Vec4{ 0.62f, 0.66f, 0.75f, 1.0f };
            hint.textAlignH = TextAlignH::Left;
            hint.textAlignV = TextAlignV::Center;
            hint.fillX      = true;
            hint.minSize    = Vec2{ 0.0f, 36.0f };
            hint.runtimeOnly = true;
            root.children.push_back(std::move(hint));
        }

        rightWidget->setElements({ std::move(root) });
        registerWidget(rightWidgetId, rightWidget, tabId);
    }

    // Tab and close button events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        // Refresh hierarchy in case scripts changed things while tab was inactive
        refreshUIDesignerHierarchy();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeUIDesignerTab();
    });

    // --- Bidirectional sync: viewport click → designer selection ---
    auto* vpUI = getViewportUIManager();
    if (vpUI)
    {
        vpUI->setOnSelectionChanged([this](const std::string& elementId)
        {
            if (!m_uiDesignerState.isOpen) return;
            // Only sync if the UI Designer tab is the active tab
            if (m_renderer && m_renderer->getActiveTabId() != m_uiDesignerState.tabId) return;

            // Find which widget owns this element
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
                m_uiDesignerState.selectedWidgetName = ownerWidget;
                m_uiDesignerState.selectedElementId = elementId;
                refreshUIDesignerHierarchy();
                refreshUIDesignerDetails();
                markAllWidgetsDirty();
            }
        });
    }

    // Initial population
    refreshUIDesignerHierarchy();
    refreshUIDesignerDetails();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// closeUIDesignerTab
// ---------------------------------------------------------------------------
void UIManager::closeUIDesignerTab()
{
    if (!m_uiDesignerState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_uiDesignerState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_uiDesignerState.leftWidgetId);
    unregisterWidget(m_uiDesignerState.rightWidgetId);
    unregisterWidget(m_uiDesignerState.toolbarWidgetId);

    // Clear the selection callback
    auto* vpUI = getViewportUIManager();
    if (vpUI)
        vpUI->setOnSelectionChanged(nullptr);

    m_renderer->removeTab(tabId);
    m_uiDesignerState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// selectUIDesignerElement
// ---------------------------------------------------------------------------
void UIManager::selectUIDesignerElement(const std::string& widgetName, const std::string& elementId)
{
    m_uiDesignerState.selectedWidgetName = widgetName;
    m_uiDesignerState.selectedElementId  = elementId;

    // Also inform the ViewportUIManager so it can show a selection highlight
    auto* vpUI = getViewportUIManager();
    if (vpUI)
        vpUI->setSelectedElementId(elementId);

    refreshUIDesignerHierarchy();
    refreshUIDesignerDetails();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// addElementToViewportWidget — adds a new element from the control palette
// ---------------------------------------------------------------------------
void UIManager::addElementToViewportWidget(const std::string& elementType)
{
    auto* vpUI = getViewportUIManager();
    if (!vpUI) return;

    // If no widget is selected, create one automatically
    if (m_uiDesignerState.selectedWidgetName.empty())
    {
        if (!vpUI->hasWidgets())
        {
            vpUI->createWidget("Default", 0);
            m_uiDesignerState.selectedWidgetName = "Default";
        }
        else
        {
            // Select the first widget
            const auto& sorted = vpUI->getSortedWidgets();
            if (!sorted.empty())
                m_uiDesignerState.selectedWidgetName = sorted.front().name;
        }
    }

    Widget* w = vpUI->getWidget(m_uiDesignerState.selectedWidgetName);
    if (!w) return;

    // Find the canvas panel (root element)
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
        newEl.color = Vec4{ 0.25f, 0.25f, 0.30f, 0.8f };
    }
    else if (elementType == "Text")
    {
        newEl.type      = WidgetElementType::Text;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 150.0f, 30.0f };
        newEl.text      = "Text";
        newEl.font      = "default.ttf";
        newEl.fontSize  = 14.0f;
        newEl.textColor = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
    }
    else if (elementType == "Label")
    {
        newEl.type      = WidgetElementType::Label;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 100.0f, 24.0f };
        newEl.text      = "Label";
        newEl.font      = "default.ttf";
        newEl.fontSize  = 13.0f;
        newEl.textColor = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
    }
    else if (elementType == "Button")
    {
        newEl.type          = WidgetElementType::Button;
        newEl.from          = Vec2{ 0.0f, 0.0f };
        newEl.to            = Vec2{ 120.0f, 36.0f };
        newEl.text          = "Button";
        newEl.font          = "default.ttf";
        newEl.fontSize      = 14.0f;
        newEl.textColor     = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
        newEl.color         = Vec4{ 0.22f, 0.24f, 0.32f, 1.0f };
        newEl.hoverColor    = Vec4{ 0.30f, 0.34f, 0.44f, 1.0f };
        newEl.hitTestMode = HitTestMode::Enabled;
    }
    else if (elementType == "Image")
    {
        newEl.type  = WidgetElementType::Image;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 100.0f, 100.0f };
        newEl.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
    }
    else if (elementType == "ProgressBar")
    {
        newEl.type       = WidgetElementType::ProgressBar;
        newEl.from       = Vec2{ 0.0f, 0.0f };
        newEl.to         = Vec2{ 200.0f, 24.0f };
        newEl.valueFloat = 0.5f;
        newEl.minValue   = 0.0f;
        newEl.maxValue   = 1.0f;
        newEl.color      = Vec4{ 0.15f, 0.15f, 0.20f, 1.0f };
    }
    else if (elementType == "Slider")
    {
        newEl.type       = WidgetElementType::Slider;
        newEl.from       = Vec2{ 0.0f, 0.0f };
        newEl.to         = Vec2{ 200.0f, 24.0f };
        newEl.valueFloat = 0.5f;
        newEl.minValue   = 0.0f;
        newEl.maxValue   = 1.0f;
        newEl.color      = Vec4{ 0.15f, 0.15f, 0.20f, 1.0f };
        newEl.hitTestMode = HitTestMode::Enabled;
    }
    else if (elementType == "WrapBox")
    {
        newEl.type        = WidgetElementType::WrapBox;
        newEl.from        = Vec2{ 0.0f, 0.0f };
        newEl.to          = Vec2{ 300.0f, 200.0f };
        newEl.orientation  = StackOrientation::Horizontal;
        newEl.color        = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "UniformGrid")
    {
        newEl.type    = WidgetElementType::UniformGrid;
        newEl.from    = Vec2{ 0.0f, 0.0f };
        newEl.to      = Vec2{ 300.0f, 300.0f };
        newEl.columns = 3;
        newEl.rows    = 3;
        newEl.color   = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "SizeBox")
    {
        newEl.type           = WidgetElementType::SizeBox;
        newEl.from           = Vec2{ 0.0f, 0.0f };
        newEl.to             = Vec2{ 200.0f, 100.0f };
        newEl.widthOverride  = 200.0f;
        newEl.heightOverride = 100.0f;
        newEl.color          = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "ScaleBox")
    {
        newEl.type      = WidgetElementType::ScaleBox;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 300.0f, 300.0f };
        newEl.scaleMode = ScaleMode::Contain;
        newEl.color     = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "WidgetSwitcher")
    {
        newEl.type             = WidgetElementType::WidgetSwitcher;
        newEl.from             = Vec2{ 0.0f, 0.0f };
        newEl.to               = Vec2{ 300.0f, 200.0f };
        newEl.activeChildIndex = 0;
        newEl.color            = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "Overlay")
    {
        newEl.type  = WidgetElementType::Overlay;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
        newEl.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
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

    m_uiDesignerState.selectedElementId = newId;
    refreshUIDesignerHierarchy();
    refreshUIDesignerDetails();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// deleteSelectedUIDesignerElement
// ---------------------------------------------------------------------------
void UIManager::deleteSelectedUIDesignerElement()
{
    auto* vpUI = getViewportUIManager();
    if (!vpUI || m_uiDesignerState.selectedElementId.empty())
        return;

    Widget* w = vpUI->getWidget(m_uiDesignerState.selectedWidgetName);
    if (!w) return;

    auto& elements = w->getElementsMutable();
    if (elements.empty()) return;
    auto& canvas = elements[0];

    // Find and remove the element from canvas children
    const std::string& targetId = m_uiDesignerState.selectedElementId;
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
        m_uiDesignerState.selectedElementId.clear();
        vpUI->setSelectedElementId("");
        w->markLayoutDirty();
        vpUI->markLayoutDirty();
        refreshUIDesignerHierarchy();
        refreshUIDesignerDetails();
        markAllWidgetsDirty();
    }
}

// ---------------------------------------------------------------------------
// refreshUIDesignerHierarchy — traverses ViewportUIManager widgets
// ---------------------------------------------------------------------------
void UIManager::refreshUIDesignerHierarchy()
{
    if (!m_uiDesignerState.isOpen) return;

    auto* leftEntry = findWidgetEntry(m_uiDesignerState.leftWidgetId);
    if (!leftEntry || !leftEntry->widget) return;

    auto& leftElements = leftEntry->widget->getElementsMutable();
    WidgetElement* treePanel = FindElementById(leftElements, "UIDesigner.Left.Tree");
    if (!treePanel) return;

    treePanel->children.clear();
    m_lastHoveredElement = nullptr;

    auto* vpUI = getViewportUIManager();
    if (!vpUI) return;

    const auto& sortedWidgets = vpUI->getSortedWidgets();
    const std::string& selectedWidget  = m_uiDesignerState.selectedWidgetName;
    const std::string& selectedElement = m_uiDesignerState.selectedElementId;
    int lineIndex = 0;

    // Update toolbar status
    {
        auto* tbEntry = findWidgetEntry(m_uiDesignerState.toolbarWidgetId);
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
            row.font     = "default.ttf";
            row.fontSize = 13.0f;
            row.textAlignH    = TextAlignH::Left;
            row.textAlignV    = TextAlignV::Center;
            row.fillX         = true;
            row.minSize       = Vec2{ 0.0f, 22.0f };
            row.padding       = Vec2{ 4.0f, 1.0f };
            row.hitTestMode = HitTestMode::Enabled;
            row.runtimeOnly   = true;

            if (isWidgetSelected && selectedElement.empty())
            {
                row.color     = Vec4{ 0.20f, 0.30f, 0.55f, 0.9f };
                row.hoverColor = Vec4{ 0.25f, 0.35f, 0.60f, 1.0f };
                row.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
            }
            else
            {
                row.color     = Vec4{ 0.14f, 0.15f, 0.20f, 0.6f };
                row.hoverColor = Vec4{ 0.20f, 0.22f, 0.30f, 0.8f };
                row.textColor = Vec4{ 0.90f, 0.90f, 0.95f, 1.0f };
            }

            const std::string capturedName = widgetEntry.name;
            row.onClicked = [this, capturedName]()
            {
                selectUIDesignerElement(capturedName, "");
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
                    row.font     = "default.ttf";
                    row.fontSize = 11.0f;
                    row.textAlignH    = TextAlignH::Left;
                    row.textAlignV    = TextAlignV::Center;
                    row.fillX         = true;
                    row.minSize       = Vec2{ 0.0f, 20.0f };
                    row.padding       = Vec2{ 4.0f, 1.0f };
                    row.hitTestMode = HitTestMode::Enabled;
                    row.runtimeOnly   = true;

                    if (isElementSelected)
                    {
                        row.color     = Vec4{ 0.20f, 0.30f, 0.55f, 0.9f };
                        row.hoverColor = Vec4{ 0.25f, 0.35f, 0.60f, 1.0f };
                        row.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
                    }
                    else
                    {
                        row.color     = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                        row.hoverColor = Vec4{ 0.18f, 0.20f, 0.28f, 0.8f };
                        row.textColor = Vec4{ 0.75f, 0.78f, 0.84f, 1.0f };
                    }

                    const std::string capturedWidgetName = widgetEntry.name;
                    row.onClicked = [this, capturedWidgetName, elId]()
                    {
                        selectUIDesignerElement(capturedWidgetName, elId);
                    };

                    treePanel->children.push_back(std::move(row));
                    ++lineIndex;
                }
            }
        }
    }

    leftEntry->widget->markLayoutDirty();
}

// ---------------------------------------------------------------------------
// refreshUIDesignerDetails — properties panel for selected element
// ---------------------------------------------------------------------------
void UIManager::refreshUIDesignerDetails()
{
    if (!m_uiDesignerState.isOpen) return;

    auto* rightEntry = findWidgetEntry(m_uiDesignerState.rightWidgetId);
    if (!rightEntry || !rightEntry->widget) return;

    auto& rightElements = rightEntry->widget->getElementsMutable();
    WidgetElement* rootPanel = FindElementById(rightElements, "UIDesigner.Right.Root");
    if (!rootPanel) return;

    // Keep only the title (first child)
    if (rootPanel->children.size() > 1)
        rootPanel->children.erase(rootPanel->children.begin() + 1, rootPanel->children.end());

    m_lastHoveredElement = nullptr;

    const auto makeLabel = [](const std::string& id, const std::string& text, float fontSize = 12.0f,
        const Vec4& color = Vec4{ 0.78f, 0.80f, 0.85f, 1.0f }, float minH = 20.0f) -> WidgetElement
    {
        WidgetElement lbl{};
        lbl.id         = id;
        lbl.type       = WidgetElementType::Text;
        lbl.text       = text;
        lbl.font       = "default.ttf";
        lbl.fontSize   = fontSize;
        lbl.textColor  = color;
        lbl.textAlignH = TextAlignH::Left;
        lbl.textAlignV = TextAlignV::Center;
        lbl.fillX      = true;
        lbl.minSize    = Vec2{ 0.0f, minH };
        lbl.runtimeOnly = true;
        return lbl;
    };

    auto fmtF = [](float v) -> std::string {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };

    auto* vpUI = getViewportUIManager();
    if (!vpUI)
    {
        rootPanel->children.push_back(makeLabel("UID.Hint",
            "No ViewportUIManager available.", 11.0f,
            Vec4{ 0.85f, 0.55f, 0.55f, 1.0f }, 36.0f));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Widget-level properties if a widget is selected but no element
    if (!m_uiDesignerState.selectedWidgetName.empty() && m_uiDesignerState.selectedElementId.empty())
    {
        Widget* w = vpUI->getWidget(m_uiDesignerState.selectedWidgetName);
        if (!w)
        {
            rootPanel->children.push_back(makeLabel("UID.NotFound",
                "Widget not found.", 11.0f,
                Vec4{ 0.85f, 0.55f, 0.55f, 1.0f }, 24.0f));
            rightEntry->widget->markLayoutDirty();
            return;
        }

        rootPanel->children.push_back(makeLabel("UID.SecWidget", "Widget", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));
        rootPanel->children.push_back(makeLabel("UID.WidgetName",
            "Name: " + m_uiDesignerState.selectedWidgetName));

        int childCount = 0;
        const auto& elems = w->getElements();
        if (!elems.empty())
            childCount = static_cast<int>(elems[0].children.size());
        rootPanel->children.push_back(makeLabel("UID.WidgetChildren",
            "Elements: " + std::to_string(childCount)));

        rightEntry->widget->markLayoutDirty();
        return;
    }

    // No selection
    if (m_uiDesignerState.selectedElementId.empty())
    {
        rootPanel->children.push_back(makeLabel("UID.Hint",
            "Select an element in the hierarchy\nto see its properties.", 11.0f,
            Vec4{ 0.62f, 0.66f, 0.75f, 1.0f }, 36.0f));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Find the selected element
    WidgetElement* selected = vpUI->findElementById(
        m_uiDesignerState.selectedWidgetName, m_uiDesignerState.selectedElementId);
    if (!selected)
    {
        rootPanel->children.push_back(makeLabel("UID.NotFound",
            "Element not found: " + m_uiDesignerState.selectedElementId, 11.0f,
            Vec4{ 0.85f, 0.55f, 0.55f, 1.0f }, 24.0f));
        rightEntry->widget->markLayoutDirty();
        return;
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
        default:                             return "Element";
        }
    };

    // Lambda to apply changes and refresh
    const auto applyChange = [this]() {
        auto* vp = getViewportUIManager();
        if (vp) vp->markLayoutDirty();
        markAllWidgetsDirty();
    };

    const auto makePropertyRow = [&](const std::string& id, const std::string& label,
        const std::string& value, std::function<void(const std::string&)> onChange) -> WidgetElement
    {
        WidgetElement row{};
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.sizeToContent = true;
        row.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.padding     = Vec2{ 0.0f, 1.0f };
        row.runtimeOnly = true;

        WidgetElement lbl = makeLabel(id + ".Lbl", label);
        lbl.minSize = Vec2{ 80.0f, 22.0f };
        lbl.fillX   = false;
        row.children.push_back(std::move(lbl));

        EntryBarWidget entry;
        entry.setValue(value);
        entry.setFont("default.ttf");
        entry.setFontSize(12.0f);
        entry.setMinSize(Vec2{ 0.0f, 22.0f });
        entry.setPadding(Vec2{ 4.0f, 2.0f });
        entry.setOnValueChanged(onChange);

        WidgetElement entryEl = entry.toElement();
        entryEl.id         = id + ".Entry";
        entryEl.fillX      = true;
        entryEl.runtimeOnly = true;
        row.children.push_back(std::move(entryEl));

        return row;
    };

    WidgetElement* sel = selected;

    // --- Section: Identity ---
    {
        rootPanel->children.push_back(makeLabel("UID.SecIdentity", "Identity", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));
        rootPanel->children.push_back(makeLabel("UID.Type", "Type: " + getTypeName(sel->type)));

        rootPanel->children.push_back(makePropertyRow("UID.Id", "ID", sel->id,
            [sel, applyChange, this](const std::string& v) {
                sel->id = v;
                m_uiDesignerState.selectedElementId = v;
                applyChange();
                refreshUIDesignerHierarchy();
            }));
    }

    // --- Section: Anchor ---
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color   = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("UID.SecAnchor", "Anchor & Position", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        // Anchor dropdown
        {
            int anchorIdx = static_cast<int>(sel->anchor);

            WidgetElement row{};
            row.type        = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX       = true;
            row.sizeToContent = true;
            row.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding     = Vec2{ 0.0f, 1.0f };
            row.runtimeOnly = true;

            WidgetElement lbl = makeLabel("UID.Anchor.Lbl", "Anchor");
            lbl.minSize = Vec2{ 80.0f, 22.0f };
            lbl.fillX   = false;
            row.children.push_back(std::move(lbl));

            DropDownWidget anchorDd;
            anchorDd.setItems({ "TopLeft", "TopRight", "BottomLeft", "BottomRight",
                                "Top", "Bottom", "Left", "Right", "Center", "Stretch" });
            anchorDd.setSelectedIndex(anchorIdx);
            anchorDd.setFont("default.ttf");
            anchorDd.setFontSize(12.0f);
            anchorDd.setMinSize(Vec2{ 0.0f, 22.0f });
            anchorDd.setPadding(Vec2{ 4.0f, 2.0f });
            anchorDd.setBackgroundColor(Vec4{ 0.16f, 0.16f, 0.20f, 1.0f });
            anchorDd.setHoverColor(Vec4{ 0.22f, 0.22f, 0.27f, 1.0f });
            anchorDd.setTextColor(Vec4{ 0.92f, 0.92f, 0.95f, 1.0f });
            anchorDd.setOnSelectionChanged([sel, applyChange](int idx) {
                sel->anchor = static_cast<WidgetAnchor>(idx);
                applyChange();
            });
            WidgetElement ddEl = anchorDd.toElement();
            ddEl.id         = "UID.Anchor.DD";
            ddEl.fillX      = true;
            ddEl.runtimeOnly = true;
            row.children.push_back(std::move(ddEl));

            rootPanel->children.push_back(std::move(row));
        }

        rootPanel->children.push_back(makePropertyRow("UID.OffsetX", "Offset X", fmtF(sel->anchorOffset.x),
            [sel, applyChange](const std::string& v) { try { sel->anchorOffset.x = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.OffsetY", "Offset Y", fmtF(sel->anchorOffset.y),
            [sel, applyChange](const std::string& v) { try { sel->anchorOffset.y = std::stof(v); } catch (...) {} applyChange(); }));
    }

    // --- Section: Size ---
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color   = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("UID.SecSize", "Size", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("UID.Width", "Width", fmtF(sel->to.x - sel->from.x),
            [sel, applyChange](const std::string& v) { try { sel->to.x = sel->from.x + std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.Height", "Height", fmtF(sel->to.y - sel->from.y),
            [sel, applyChange](const std::string& v) { try { sel->to.y = sel->from.y + std::stof(v); } catch (...) {} applyChange(); }));
    }

    // --- Section: Appearance ---
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color   = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("UID.SecAppearance", "Appearance", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("UID.ColR", "Color R", fmtF(sel->color.x),
            [sel, applyChange](const std::string& v) { try { sel->color.x = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.ColG", "Color G", fmtF(sel->color.y),
            [sel, applyChange](const std::string& v) { try { sel->color.y = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.ColB", "Color B", fmtF(sel->color.z),
            [sel, applyChange](const std::string& v) { try { sel->color.z = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.ColA", "Color A", fmtF(sel->color.w),
            [sel, applyChange](const std::string& v) { try { sel->color.w = std::stof(v); } catch (...) {} applyChange(); }));

        rootPanel->children.push_back(makePropertyRow("UID.Opacity", "Opacity", fmtF(sel->opacity),
            [sel, applyChange](const std::string& v) { try { sel->opacity = std::max(0.0f, std::min(1.0f, std::stof(v))); } catch (...) {} applyChange(); }));

        // Visibility
        {
            CheckBoxWidget visCb;
            visCb.setLabel("Visible");
            visCb.setChecked(sel->isVisible);
            visCb.setOnCheckedChanged([sel, applyChange](bool c) { sel->isVisible = c; applyChange(); });
            WidgetElement visEl = visCb.toElement();
            visEl.id         = "UID.Visible";
            visEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(visEl));
        }

        rootPanel->children.push_back(makePropertyRow("UID.BorderW", "Border", fmtF(sel->borderThickness),
            [sel, applyChange](const std::string& v) { try { sel->borderThickness = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.BorderR", "Radius", fmtF(sel->borderRadius),
            [sel, applyChange](const std::string& v) { try { sel->borderRadius = std::stof(v); } catch (...) {} applyChange(); }));
    }

    // --- Section: Text (for elements with text) ---
    if (sel->type == WidgetElementType::Text || sel->type == WidgetElementType::Button ||
        sel->type == WidgetElementType::Label)
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color   = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("UID.SecText", "Text", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));

        rootPanel->children.push_back(makePropertyRow("UID.Text", "Text", sel->text,
            [sel, applyChange](const std::string& v) { sel->text = v; applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.FontSz", "Font Size", fmtF(sel->fontSize),
            [sel, applyChange](const std::string& v) { try { sel->fontSize = std::stof(v); } catch (...) {} applyChange(); }));

        rootPanel->children.push_back(makePropertyRow("UID.TColR", "Text R", fmtF(sel->textColor.x),
            [sel, applyChange](const std::string& v) { try { sel->textColor.x = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.TColG", "Text G", fmtF(sel->textColor.y),
            [sel, applyChange](const std::string& v) { try { sel->textColor.y = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.TColB", "Text B", fmtF(sel->textColor.z),
            [sel, applyChange](const std::string& v) { try { sel->textColor.z = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.TColA", "Text A", fmtF(sel->textColor.w),
            [sel, applyChange](const std::string& v) { try { sel->textColor.w = std::stof(v); } catch (...) {} applyChange(); }));

        // Bold / Italic
        {
            CheckBoxWidget boldCb;
            boldCb.setLabel("Bold");
            boldCb.setChecked(sel->isBold);
            boldCb.setOnCheckedChanged([sel, applyChange](bool c) { sel->isBold = c; applyChange(); });
            WidgetElement boldEl = boldCb.toElement();
            boldEl.id         = "UID.Bold";
            boldEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(boldEl));
        }
        {
            CheckBoxWidget italicCb;
            italicCb.setLabel("Italic");
            italicCb.setChecked(sel->isItalic);
            italicCb.setOnCheckedChanged([sel, applyChange](bool c) { sel->isItalic = c; applyChange(); });
            WidgetElement italicEl = italicCb.toElement();
            italicEl.id         = "UID.Italic";
            italicEl.runtimeOnly = true;
            rootPanel->children.push_back(std::move(italicEl));
        }
    }

    // --- Section: Image ---
    if (sel->type == WidgetElementType::Image)
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color   = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("UID.SecImage", "Image", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));
        rootPanel->children.push_back(makePropertyRow("UID.ImgPath", "Path", sel->imagePath,
            [sel, applyChange](const std::string& v) { sel->imagePath = v; applyChange(); }));
    }

    // --- Section: Value (Slider / ProgressBar) ---
    if (sel->type == WidgetElementType::Slider || sel->type == WidgetElementType::ProgressBar)
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 1.0f };
        sep.color   = Vec4{ 0.26f, 0.28f, 0.34f, 0.6f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        rootPanel->children.push_back(makeLabel("UID.SecValue", "Value", 13.0f,
            Vec4{ 0.95f, 0.85f, 0.55f, 1.0f }, 26.0f));
        rootPanel->children.push_back(makePropertyRow("UID.MinVal", "Min", fmtF(sel->minValue),
            [sel, applyChange](const std::string& v) { try { sel->minValue = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.MaxVal", "Max", fmtF(sel->maxValue),
            [sel, applyChange](const std::string& v) { try { sel->maxValue = std::stof(v); } catch (...) {} applyChange(); }));
        rootPanel->children.push_back(makePropertyRow("UID.CurVal", "Value", fmtF(sel->valueFloat),
            [sel, applyChange](const std::string& v) { try { sel->valueFloat = std::stof(v); } catch (...) {} applyChange(); }));
    }

    // --- Delete button ---
    {
        WidgetElement sep{};
        sep.type    = WidgetElementType::Panel;
        sep.fillX   = true;
        sep.minSize = Vec2{ 0.0f, 8.0f };
        sep.color   = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        sep.runtimeOnly = true;
        rootPanel->children.push_back(std::move(sep));

        WidgetElement delBtn{};
        delBtn.id            = "UID.Delete";
        delBtn.type          = WidgetElementType::Button;
        delBtn.text          = "Delete Element";
        delBtn.font          = "default.ttf";
        delBtn.fontSize      = 13.0f;
        delBtn.textColor     = Vec4{ 1.0f, 0.85f, 0.85f, 1.0f };
        delBtn.color         = Vec4{ 0.45f, 0.18f, 0.18f, 1.0f };
        delBtn.hoverColor    = Vec4{ 0.60f, 0.22f, 0.22f, 1.0f };
        delBtn.textAlignH    = TextAlignH::Center;
        delBtn.textAlignV    = TextAlignV::Center;
        delBtn.fillX         = true;
        delBtn.minSize       = Vec2{ 0.0f, 28.0f };
        delBtn.padding       = Vec2{ 8.0f, 4.0f };
        delBtn.hitTestMode = HitTestMode::Enabled;
        delBtn.runtimeOnly   = true;
        delBtn.onClicked     = [this]() { deleteSelectedUIDesignerElement(); };
        rootPanel->children.push_back(std::move(delBtn));
    }

    rightEntry->widget->markLayoutDirty();
}
