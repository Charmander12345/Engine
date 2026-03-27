#include "UIManager.h"

#include <algorithm>
#include <numeric>
#include <functional>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <SDL3/SDL.h>
#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <Windows.h>
#   include <shellapi.h>
#endif
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
#include "EditorTheme.h"
#include "Renderer.h"
#include "ViewportUIManager.h"
#include "../AssetManager/AssetTypes.h"
#if ENGINE_EDITOR
#include "EditorUIBuilder.h"
#include "WidgetDetailSchema.h"
#include "../Core/ShortcutManager.h"
#include "../Core/AudioManager.h"
#include "../AssetManager/AssetManager.h"
#include "EditorWindows/PopupWindow.h"
#include "../Landscape/LandscapeManager.h"
#include "../AssetManager/json.hpp"
#endif // ENGINE_EDITOR

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
            const float width = (element.minSize.x > 0.0f) ? element.minSize.x : EditorTheme::Scaled(140.0f);
            const float height = (element.minSize.y > 0.0f) ? element.minSize.y : EditorTheme::Scaled(18.0f);
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
                const float imgDefault = EditorTheme::Scaled(24.0f);
            }
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::EntryBar:
        {
            const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : EditorTheme::Get().fontSizeSubheading;
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
            const float boxSize = EditorTheme::Scaled(16.0f);
            const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : EditorTheme::Get().fontSizeSubheading;
            const float scale = fontSize / 48.0f;
            const Vec2 textSize = (!element.text.empty() && measureText) ? measureText(element.text, scale) : Vec2{};
            size.x = element.padding.x + boxSize + EditorTheme::Scaled(6.0f) + textSize.x + element.padding.x;
            size.y = std::max(boxSize, textSize.y) + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::DropDown:
        {
            const float fontSize = (element.fontSize > 0.0f) ? element.fontSize : EditorTheme::Get().fontSizeSubheading;
            const float scale = fontSize / 48.0f;
            std::string display = element.text;
            if (display.empty() && element.selectedIndex >= 0 &&
                element.selectedIndex < static_cast<int>(element.items.size()))
            {
                display = element.items[static_cast<size_t>(element.selectedIndex)];
            }
            const Vec2 textSize = (!display.empty() && measureText) ? measureText(display, scale) : Vec2{};
            size.x = textSize.x + element.padding.x * 2.0f + EditorTheme::Scaled(16.0f);
            size.y = textSize.y + element.padding.y * 2.0f;
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }
        case WidgetElementType::DropdownButton:
        {
            const float scale = (element.fontSize > 0.0f) ? (element.fontSize / 48.0f) : EditorTheme::Get().fontSizeSubheading / 48.0f;
            const Vec2 textSize = (!element.text.empty() && measureText) ? measureText(element.text, scale) : Vec2{};
            // Text + padding + arrow indicator space
            size.x = textSize.x + element.padding.x * 2.0f + EditorTheme::Scaled(12.0f);
            size.y = textSize.y + element.padding.y * 2.0f;
            if (!element.imagePath.empty() && element.text.empty())
            {
                const float imgDefault = EditorTheme::Scaled(24.0f);
                size.x = std::max(size.x, imgDefault + element.padding.x * 2.0f + EditorTheme::Scaled(12.0f));
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
        case WidgetElementType::Border:
        case WidgetElementType::ListView:
        case WidgetElementType::TileView:
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

        // Border: single child with border insets
        if (element.type == WidgetElementType::Border)
        {
            float childW = 0.0f;
            float childH = 0.0f;
            if (!childSizes.empty())
            {
                childW = childSizes[0].x;
                childH = childSizes[0].y;
            }
            size.x = childW + element.borderThicknessLeft + element.borderThicknessRight + element.contentPadding.x * 2.0f;
            size.y = childH + element.borderThicknessTop + element.borderThicknessBottom + element.contentPadding.y * 2.0f;
            size.x += element.padding.x * 2.0f;
            size.y += element.padding.y * 2.0f;
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // Spinner: fixed size based on minSize (no children to measure)
        if (element.type == WidgetElementType::Spinner)
        {
            size = Vec2{ std::max(element.minSize.x, 32.0f), std::max(element.minSize.y, 32.0f) };
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // RichText: use text measurement as approximation
        if (element.type == WidgetElementType::RichText)
        {
            const std::string& displayText = element.richText.empty() ? element.text : element.richText;
            if (!displayText.empty() && measureText)
            {
                float fSize = (element.fontSize > 0.0f) ? element.fontSize : 14.0f;
                Vec2 textSize = measureText(displayText, fSize);
                size.x = textSize.x + element.padding.x * 2.0f;
                size.y = textSize.y + element.padding.y * 2.0f;
            }
            size.x = std::max(size.x, element.minSize.x);
            size.y = std::max(size.y, element.minSize.y);
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // ListView: height = itemHeight * totalItemCount
        if (element.type == WidgetElementType::ListView)
        {
            float contentH = element.itemHeight * static_cast<float>(element.totalItemCount);
            float maxChildW = 0.0f;
            for (const auto& cs : childSizes)
            {
                maxChildW = std::max(maxChildW, cs.x);
            }
            size.x = std::max(maxChildW, element.minSize.x) + element.padding.x * 2.0f;
            size.y = contentH + element.padding.y * 2.0f;
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // TileView: grid of tiles
        if (element.type == WidgetElementType::TileView)
        {
            int cols = std::max(1, element.columnsPerRow);
            int rowCount = (element.totalItemCount + cols - 1) / std::max(1, cols);
            size.x = element.itemWidth * static_cast<float>(cols) + element.padding.x * 2.0f;
            size.y = element.itemHeight * static_cast<float>(rowCount) + element.padding.y * 2.0f;
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ WrapBox: children flow and wrap Ã¢â€â‚¬Ã¢â€â‚¬
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ UniformGrid: all cells equal size Ã¢â€â‚¬Ã¢â€â‚¬
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ SizeBox: single child with size override Ã¢â€â‚¬Ã¢â€â‚¬
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ ScaleBox: single child, scaled to fit Ã¢â€â‚¬Ã¢â€â‚¬
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ WidgetSwitcher: only active child Ã¢â€â‚¬Ã¢â€â‚¬
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ Overlay: all children stacked, each aligned within the same area Ã¢â€â‚¬Ã¢â€â‚¬
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ Border: single child inset by border thickness + content padding Ã¢â€â‚¬Ã¢â€â‚¬
        else if (element.type == WidgetElementType::Border)
        {
            float insetL = element.borderThicknessLeft + element.contentPadding.x;
            float insetT = element.borderThicknessTop + element.contentPadding.y;
            float insetR = element.borderThicknessRight + element.contentPadding.x;
            float insetB = element.borderThicknessBottom + element.contentPadding.y;
            float childX = contentX + insetL;
            float childY = contentY + insetT;
            float childW = std::max(0.0f, contentW - insetL - insetR);
            float childH = std::max(0.0f, contentH - insetT - insetB);
            for (auto& child : element.children)
            {
                layoutElement(child, childX, childY, childW, childH, measureText);
            }
        }
        // Ã¢â€â‚¬Ã¢â€â‚¬ ListView: vertical stack of items Ã¢â€â‚¬Ã¢â€â‚¬
        else if (element.type == WidgetElementType::ListView)
        {
            float curY = contentY;
            float itemH = element.itemHeight > 0.0f ? element.itemHeight : 32.0f;
            for (auto& child : element.children)
            {
                layoutElement(child, contentX, curY, contentW, itemH, measureText);
                curY += itemH;
            }
        }
        // Ã¢â€â‚¬Ã¢â€â‚¬ TileView: grid of tiles Ã¢â€â‚¬Ã¢â€â‚¬
        else if (element.type == WidgetElementType::TileView)
        {
            int cols = element.columnsPerRow > 0 ? element.columnsPerRow : 4;
            float tileW = element.itemWidth > 0.0f ? element.itemWidth : (contentW / static_cast<float>(cols));
            float tileH = element.itemHeight > 0.0f ? element.itemHeight : 80.0f;
            for (size_t i = 0; i < element.children.size(); ++i)
            {
                int col = static_cast<int>(i) % cols;
                int row = static_cast<int>(i) / cols;
                float childX = contentX + col * tileW;
                float childY = contentY + row * tileH;
                layoutElement(element.children[i], childX, childY, tileW, tileH, measureText);
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
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR
}

UIManager::~UIManager()
{
#if ENGINE_EDITOR
    if (m_buildThread.joinable())
    {
        m_buildThread.join();
    }
    if (m_levelChangedCallbackToken != 0)
    {
        DiagnosticsManager::Instance().unregisterActiveLevelChangedCallback(m_levelChangedCallbackToken);
    }
#endif // ENGINE_EDITOR
    if (GetActiveInstance() == this)
    {
        SetActiveInstance(nullptr);
    }
}

#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

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

void UIManager::registerWidget(const std::string& id, const std::shared_ptr<EditorWidget>& widget)
{
    registerWidget(id, widget, std::string{});
}

void UIManager::registerWidget(const std::string& id, const std::shared_ptr<EditorWidget>& widget, const std::string& tabId)
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
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR
        entry->widget->markLayoutDirty();
    }
    m_widgetOrderDirty = true;
    m_pointerCacheDirty = true;
    m_renderDirty = true;
}

// Transition overloads: accept gameplay Widget, convert to EditorWidget
void UIManager::registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget)
{
    registerWidget(id, EditorWidget::fromWidget(widget));
}

void UIManager::registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget, const std::string& tabId)
{
    registerWidget(id, EditorWidget::fromWidget(widget), tabId);
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

#if ENGINE_EDITOR
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
        m_modalWidget = std::make_shared<EditorWidget>();
        m_modalWidget->setName("ModalMessage");
        m_modalWidget->setAnchor(WidgetAnchor::TopLeft);
        m_modalWidget->setFillX(true);
        m_modalWidget->setFillY(true);
        m_modalWidget->setZOrder(10000);
    }

    const auto& theme = EditorTheme::Get();

    WidgetElement overlay{};
    overlay.id = "Modal.Overlay";
    overlay.type = WidgetElementType::Panel;
    overlay.from = Vec2{ 0.0f, 0.0f };
    overlay.to = Vec2{ 1.0f, 1.0f };
    overlay.style.color = theme.modalOverlay;
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.25f, 0.32f };
    panel.to = Vec2{ 0.75f, 0.68f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = theme.modalBackground;
    panel.elevation = 3;
    panel.style.applyElevation(3, theme.shadowColor, theme.shadowOffset);
    panel.style.borderRadius = theme.borderRadius;
    panel.hitTestMode = HitTestMode::DisabledSelf;
    panel.runtimeOnly = true;

    WidgetElement msgText{};
    msgText.id = "Modal.Text";
    msgText.type = WidgetElementType::Text;
    msgText.text = message;
    msgText.font = theme.fontDefault;
    msgText.fontSize = theme.fontSizeHeading;
    msgText.style.textColor = theme.modalText;
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
    buttonRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    buttonRow.minSize = Vec2{ 0.0f, 32.0f };
    buttonRow.orientation = StackOrientation::Horizontal;
    buttonRow.padding = Vec2{ 8.0f, 0.0f };
    buttonRow.runtimeOnly = true;

    WidgetElement spacerLeft{};
    spacerLeft.id = "Modal.SpacerL";
    spacerLeft.type = WidgetElementType::Panel;
    spacerLeft.fillX = true;
    spacerLeft.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerLeft.runtimeOnly = true;

    WidgetElement spacerMid{};
    spacerMid.id = "Modal.SpacerM";
    spacerMid.type = WidgetElementType::Panel;
    spacerMid.fillX = true;
    spacerMid.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerMid.runtimeOnly = true;

    WidgetElement spacerRight{};
    spacerRight.id = "Modal.SpacerR";
    spacerRight.type = WidgetElementType::Panel;
    spacerRight.fillX = true;
    spacerRight.style.color = theme.transparent;
    spacerRight.runtimeOnly = true;

    WidgetElement yesBtn = EditorUIBuilder::makeDangerButton("Modal.Yes", "Delete", {}, Vec2{ 110.0f, 32.0f });
    yesBtn.onClicked = [this, confirmCb]()
    {
        closeModalMessage();
        if (*confirmCb) (*confirmCb)();
    };

    WidgetElement noBtn = EditorUIBuilder::makeButton("Modal.No", "Cancel", {}, Vec2{ 110.0f, 32.0f });
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
        m_modalWidget = std::make_shared<EditorWidget>();
        m_modalWidget->setName("ModalMessage");
        m_modalWidget->setAnchor(WidgetAnchor::TopLeft);
        m_modalWidget->setFillX(true);
        m_modalWidget->setFillY(true);
        m_modalWidget->setZOrder(10000);
    }

    const auto& theme = EditorTheme::Get();

    WidgetElement overlay{};
    overlay.id = "Modal.Overlay";
    overlay.type = WidgetElementType::Panel;
    overlay.from = Vec2{ 0.0f, 0.0f };
    overlay.to = Vec2{ 1.0f, 1.0f };
    overlay.style.color = theme.modalOverlay;
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.25f, 0.30f };
    panel.to = Vec2{ 0.75f, 0.70f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = theme.modalBackground;
    panel.elevation = 3;
    panel.style.applyElevation(3, theme.shadowColor, theme.shadowOffset);
    panel.style.borderRadius = theme.borderRadius;
    panel.hitTestMode = HitTestMode::DisabledSelf;
    panel.runtimeOnly = true;

    WidgetElement msgText{};
    msgText.id = "Modal.Text";
    msgText.type = WidgetElementType::Text;
    msgText.text = message;
    msgText.font = theme.fontDefault;
    msgText.fontSize = theme.fontSizeHeading;
    msgText.style.textColor = theme.modalText;
    msgText.wrapText = true;
    msgText.fillX = true;
    msgText.fillY = true;
    msgText.minSize = Vec2{ 0.0f, 36.0f };
    msgText.runtimeOnly = true;

    WidgetElement checkbox{};
    checkbox.id = "Modal.Checkbox";
    checkbox.type = WidgetElementType::CheckBox;
    checkbox.text = checkboxLabel;
    checkbox.font = theme.fontDefault;
    checkbox.fontSize = theme.fontSizeBody;
    checkbox.isChecked = checkedByDefault;
    checkbox.fillX = true;
    checkbox.minSize = Vec2{ 0.0f, 26.0f };
    checkbox.padding = theme.paddingSmall;
    checkbox.style.color = theme.checkboxDefault;
    checkbox.style.hoverColor = theme.checkboxHover;
    checkbox.style.fillColor = theme.checkboxChecked;
    checkbox.style.textColor = theme.checkboxText;
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
    buttonRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    buttonRow.minSize = Vec2{ 0.0f, 32.0f };
    buttonRow.orientation = StackOrientation::Horizontal;
    buttonRow.padding = Vec2{ 8.0f, 0.0f };
    buttonRow.runtimeOnly = true;

    WidgetElement spacerLeft{};
    spacerLeft.id = "Modal.SpacerL";
    spacerLeft.type = WidgetElementType::Panel;
    spacerLeft.fillX = true;
    spacerLeft.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerLeft.runtimeOnly = true;

    WidgetElement spacerMid{};
    spacerMid.id = "Modal.SpacerM";
    spacerMid.type = WidgetElementType::Panel;
    spacerMid.fillX = true;
    spacerMid.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerMid.runtimeOnly = true;

    WidgetElement spacerRight{};
    spacerRight.id = "Modal.SpacerR";
    spacerRight.type = WidgetElementType::Panel;
    spacerRight.fillX = true;
    spacerRight.style.color = theme.transparent;
    spacerRight.runtimeOnly = true;

    WidgetElement yesBtn = EditorUIBuilder::makeDangerButton("Modal.Yes", "Delete", {}, Vec2{ 110.0f, 32.0f });
    yesBtn.onClicked = [this, confirmCb, checkboxState]()
    {
        closeModalMessage();
        if (*confirmCb) (*confirmCb)(*checkboxState);
    };

    WidgetElement noBtn = EditorUIBuilder::makeButton("Modal.No", "Cancel", {}, Vec2{ 110.0f, 32.0f });
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
#endif // ENGINE_EDITOR

void UIManager::showToastMessage(const std::string& message, float durationSeconds)
{
    showToastMessage(message, durationSeconds, NotificationLevel::Info);
}

void UIManager::showToastMessage(const std::string& message, float durationSeconds, NotificationLevel level)
{
    if (message.empty())
    {
        return;
    }

    // Error notifications stay visible longer
    float effectiveDuration = durationSeconds;
    if (level == NotificationLevel::Error)
        effectiveDuration = std::max(effectiveDuration, 5.0f);
    else if (level == NotificationLevel::Warning)
        effectiveDuration = std::max(effectiveDuration, 4.0f);

    ToastNotification toast{};
    toast.duration = std::max(0.1f, effectiveDuration);
    toast.timer = toast.duration;
    toast.id = "ToastMessage." + std::to_string(m_nextToastId++);
    toast.level = level;
    toast.widget = createToastWidget(message, toast.id, level);
    if (toast.widget)
    {
        registerWidget(toast.id, toast.widget);
        m_toasts.push_back(std::move(toast));
        updateToastStackLayout();
    }

#if ENGINE_EDITOR
    // Record in notification history
    NotificationHistoryEntry entry{};
    entry.message = message;
    entry.level = level;
    entry.timestampMs = SDL_GetTicks();
    m_notificationHistory.push_front(std::move(entry));
    while (m_notificationHistory.size() > kMaxNotificationHistory)
        m_notificationHistory.pop_back();
    ++m_unreadNotificationCount;

    // Update badge in StatusBar
    refreshNotificationBadge();
#endif // ENGINE_EDITOR
}

void UIManager::updateNotifications(float deltaSeconds)
{
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

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
            showToastMessage(toast.message, toast.durationSeconds, toast.level);
        }
    }

    bool removed = false;
    constexpr float toastFadeDuration = 0.3f;
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
        // Fade in/out: first 0.3s fade in, last 0.3s fade out
        float fadeAlpha = 1.0f;
        const float age = it->duration - it->timer;
        if (age < toastFadeDuration)
            fadeAlpha = age / toastFadeDuration;
        if (it->timer < toastFadeDuration)
            fadeAlpha = std::min(fadeAlpha, it->timer / toastFadeDuration);
        if (it->widget)
        {
            for (auto& el : it->widget->getElementsMutable())
                el.style.opacity = fadeAlpha;
            m_renderDirty = true;
        }
        ++it;
    }
    if (removed)
    {
        updateToastStackLayout();
    }

    // â”€â”€ Hover transition interpolation (Phase 1.5) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    updateHoverTransitions(deltaSeconds);

    // â”€â”€ Scrollbar auto-hide (Phase 1.6) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    updateScrollbarVisibility(deltaSeconds);

#if ENGINE_EDITOR
    if (m_consoleState.isOpen)
    {
        m_consoleState.refreshTimer += deltaSeconds;
        if (m_consoleState.refreshTimer >= 0.5f)
        {
            m_consoleState.refreshTimer = 0.0f;
            const uint64_t latestSeq = Logger::Instance().getLatestSequenceId();
            if (latestSeq != m_consoleState.lastSeenSequenceId)
            {
                refreshConsoleLog();
            }
        }
    }

    // â”€â”€ Profiler metrics refresh â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (m_profilerState.isOpen && !m_profilerState.frozen)
    {
        m_profilerState.refreshTimer += deltaSeconds;
        if (m_profilerState.refreshTimer >= 0.25f)
        {
            m_profilerState.refreshTimer = 0.0f;
            refreshProfilerMetrics();
        }
    }

    // â”€â”€ Particle editor refresh â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (m_particleEditorState.isOpen)
    {
        m_particleEditorState.refreshTimer += deltaSeconds;
        if (m_particleEditorState.refreshTimer >= 0.3f)
        {
            m_particleEditorState.refreshTimer = 0.0f;
            // Verify the linked entity still exists and has a ParticleEmitterComponent
            auto& ecs = ECS::ECSManager::Instance();
            if (!ecs.hasComponent<ECS::ParticleEmitterComponent>(m_particleEditorState.linkedEntity))
            {
                closeParticleEditorTab();
            }
        }
    }

    // â”€â”€ Render debugger refresh â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (m_renderDebuggerState.isOpen)
    {
        m_renderDebuggerState.refreshTimer += deltaSeconds;
        if (m_renderDebuggerState.refreshTimer >= 0.5f)
        {
            m_renderDebuggerState.refreshTimer = 0.0f;
            refreshRenderDebugger();
        }
    }

    // â”€â”€ Sequencer refresh (while playing) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (m_sequencerState.isOpen && m_sequencerState.playing)
    {
        m_sequencerState.refreshTimer += deltaSeconds;
        if (m_sequencerState.refreshTimer >= 0.1f)
        {
            m_sequencerState.refreshTimer = 0.0f;
            refreshSequencerTimeline();
        }
    }
#endif // ENGINE_EDITOR

    // â”€â”€ Tooltip timer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (!m_tooltipText.empty() && !m_tooltipVisible)
    {
        m_tooltipTimer += deltaSeconds;
        if (m_tooltipTimer >= kTooltipDelay)
        {
            m_tooltipVisible = true;
            m_renderDirty = true;

            // Create the tooltip widget
            const auto& theme = EditorTheme::Get();
            auto widget = std::make_shared<EditorWidget>();
            widget->setName("_Tooltip");
            widget->setAnchor(WidgetAnchor::TopLeft);
            widget->setAbsolutePosition(true);
            widget->setZOrder(10000);

            const float padX = EditorTheme::Scaled(8.0f);
            const float padY = EditorTheme::Scaled(4.0f);
            const float fontSize = theme.fontSizeSmall;

            // Estimate width based on character count (rough heuristic)
            const float charW = fontSize * 0.55f;
            const float estW = padX * 2.0f + charW * static_cast<float>(m_tooltipText.size());
            const float maxW = EditorTheme::Scaled(320.0f);
            const float tipW = std::min(std::max(estW, EditorTheme::Scaled(40.0f)), maxW);
            const float tipH = fontSize + padY * 2.0f + EditorTheme::Scaled(4.0f);

            // Clamp position so the tooltip stays on screen
            float posX = m_tooltipPosition.x;
            float posY = m_tooltipPosition.y;
            if (posX + tipW > m_availableViewportSize.x)
                posX = m_availableViewportSize.x - tipW;
            if (posY + tipH > m_availableViewportSize.y)
                posY = m_tooltipPosition.y - tipH - EditorTheme::Scaled(4.0f);
            if (posX < 0.0f) posX = 0.0f;
            if (posY < 0.0f) posY = 0.0f;

            widget->setPositionPixels(Vec2{ posX, posY });
            widget->setSizePixels(Vec2{ tipW, tipH });

            WidgetElement bg{};
            bg.type = WidgetElementType::Panel;
            bg.from = Vec2{ 0.0f, 0.0f };
            bg.to = Vec2{ 1.0f, 1.0f };
            bg.style.color = theme.toastBackground;
            bg.style.borderRadius = theme.borderRadius * 0.6f;
            bg.runtimeOnly = true;

            WidgetElement text{};
            text.type = WidgetElementType::Text;
            text.id = "_Tooltip.Text";
            text.text = m_tooltipText;
            text.font = theme.fontDefault;
            text.fontSize = fontSize;
            text.style.textColor = theme.toastText;
            text.textAlignH = TextAlignH::Left;
            text.textAlignV = TextAlignV::Center;
            text.from = Vec2{ 0.0f, 0.0f };
            text.to = Vec2{ 1.0f, 1.0f };
            text.padding = Vec2{ padX, padY };
            text.runtimeOnly = true;

            std::vector<WidgetElement> elements;
            elements.push_back(std::move(bg));
            elements.push_back(std::move(text));
            widget->setElements(std::move(elements));
            widget->markLayoutDirty();

            // Remove previous tooltip widget if any
            unregisterWidget("_Tooltip");
            registerWidget("_Tooltip", widget);
        }
    }
    else if (m_tooltipVisible && m_tooltipText.empty())
    {
        // Tooltip was hidden by updateHoverStates â€” remove the widget
        m_tooltipVisible = false;
        unregisterWidget("_Tooltip");
	}
}

#if ENGINE_EDITOR
// Forward declarations for static helpers defined later in this file
static const char* iconForEntity(ECS::Entity entity);
static Vec4 iconTintForEntity(ECS::Entity entity);
static WidgetElement makeTreeRow(const std::string& id,
								 const std::string& label,
								 const std::string& iconPath,
								 bool isFolder,
								 int indentLevel,
								 const Vec4& iconTint);

void UIManager::populateOutlinerWidget(const std::shared_ptr<EditorWidget>& widget)
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
                m_outlinerSelectedEntity = entity;
                populateOutlinerDetails(entity);
            };
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

// â”€â”€ Undo/Redo helper: capture old component state, apply mutation, push command â”€â”€
namespace {
    template<typename CompT>
    void setCompFieldWithUndo(unsigned int entity, const std::string& desc,
        std::function<void(CompT&)> applyChange)
    {
        auto e = static_cast<ECS::Entity>(entity);
        auto& ecs = ECS::ECSManager::Instance();
        auto* comp = ecs.getComponent<CompT>(e);
        if (!comp) return;
        CompT oldComp = *comp;
        CompT newComp = *comp;
        applyChange(newComp);
        ecs.setComponent<CompT>(e, newComp);
        DiagnosticsManager::Instance().invalidateEntity(entity);
        UndoRedoManager::Instance().pushCommand({
            desc,
            [entity, newComp]() {
                auto e2 = static_cast<ECS::Entity>(entity);
                auto& ecs2 = ECS::ECSManager::Instance();
                if (ecs2.hasComponent<CompT>(e2))
                    ecs2.setComponent<CompT>(e2, newComp);
                DiagnosticsManager::Instance().invalidateEntity(entity);
            },
            [entity, oldComp]() {
                auto e2 = static_cast<ECS::Entity>(entity);
                auto& ecs2 = ECS::ECSManager::Instance();
                if (ecs2.hasComponent<CompT>(e2))
                    ecs2.setComponent<CompT>(e2, oldComp);
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }
        });
    }
} // anonymous namespace

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

    // Invalidate cached hover pointer Ã¢â‚¬â€œ the old elements are destroyed.
    m_lastHoveredElement = nullptr;

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
                        showConfirmDialog("Remove " + compTitle + " component?",
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
            if (auto* lbl = findElementById("Details.Entity.NameLabel"))
            {
                lbl->text = "Name: " + (val.empty() ? std::string("<unnamed>") : val);
            }
            refreshWorldOutliner();
            UndoRedoManager::Instance().pushCommand({
                "Rename Entity",
                [this, entity, newComp]() {
                    auto& ecs2 = ECS::ECSManager::Instance();
                    if (ecs2.hasComponent<ECS::NameComponent>(entity))
                        ecs2.setComponent<ECS::NameComponent>(entity, newComp);
                    refreshWorldOutliner();
                },
                [this, entity, oldComp]() {
                    auto& ecs2 = ECS::ECSManager::Instance();
                    if (ecs2.hasComponent<ECS::NameComponent>(entity))
                        ecs2.setComponent<ECS::NameComponent>(entity, oldComp);
                    refreshWorldOutliner();
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
            populateOutlinerDetails(entity);
            refreshWorldOutliner();
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
            populateOutlinerDetails(entity);
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
            populateOutlinerDetails(entity);
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

        addSeparator("Material", lines, [this, entity, saved = *material]() {
            ECS::ECSManager::Instance().removeComponent<ECS::MaterialComponent>(entity);
            DiagnosticsManager::Instance().invalidateEntity(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
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
            populateOutlinerDetails(entity);
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
            populateOutlinerDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Camera",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::CameraComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::CameraComponent>(entity, saved); }
            });
        });
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ Collision Component Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
                    if (autoFitColliderForEntity(entity))
                    {
                        DiagnosticsManager::Instance().invalidateEntity(entity);
                        if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
                        populateOutlinerDetails(entity);
                        showToastMessage("Auto-fitted collider from mesh AABB.", 2.0f);
                    }
                    else
                    {
                        showToastMessage("No mesh data available for auto-fit.", 2.5f);
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
            populateOutlinerDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Collision",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::CollisionComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::CollisionComponent>(entity, saved); }
            });
        });
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ Physics Component Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
            populateOutlinerDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Physics",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::PhysicsComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::PhysicsComponent>(entity, saved); }
            });
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
            dropdownEl.id = "Details.Script.Dropdown";
            dropdownEl.fillX = true;
            dropdownEl.runtimeOnly = true;
            lines.push_back(std::move(dropdownEl));
        }

        addSeparator("Script", lines, [this, entity, saved = *script]() {
            ECS::ECSManager::Instance().removeComponent<ECS::ScriptComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Script",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ScriptComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::ScriptComponent>(entity, saved); }
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

        // "Edit Particles" button â†’ opens dedicated Particle Editor tab
        {
            WidgetElement editBtn = EditorUIBuilder::makePrimaryButton(
                "Details.Particle.EditBtn", "Edit Particles",
                [this, entity]() { openParticleEditorTab(entity); },
                EditorTheme::Scaled(Vec2{ 0.0f, 26.0f }));
            editBtn.fillX = true;
            editBtn.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
            lines.push_back(std::move(editBtn));
        }

        addSeparator("Particle Emitter", lines, [this, entity, saved = *emitter]() {
            ECS::ECSManager::Instance().removeComponent<ECS::ParticleEmitterComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
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
                [this, entity]() { openAnimationEditorTab(entity); },
                EditorTheme::Scaled(Vec2{ 0.0f, 26.0f }));
            editBtn.fillX = true;
            editBtn.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
            lines.push_back(std::move(editBtn));
        }

        addSeparator("Animation", lines, [this, entity, saved = *animComp]() {
            ECS::ECSManager::Instance().removeComponent<ECS::AnimationComponent>(entity);
            if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft()) level->setIsSaved(false);
            populateOutlinerDetails(entity);
            UndoRedoManager::Instance().pushCommand({
                "Remove Animation",
                [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::AnimationComponent>(entity); },
                [entity, saved]() { ECS::ECSManager::Instance().setComponent<ECS::AnimationComponent>(entity, saved); }
            });
        });
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
                      autoFitColliderForEntity(entity);
                  }
              },
              [entity]() {
                  ECS::ECSManager::Instance().removeComponent<ECS::PhysicsComponent>(entity);
              } },
            { "Script", ecs.hasComponent<ECS::ScriptComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::ScriptComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ScriptComponent>(entity); } },
            { "Name", ecs.hasComponent<ECS::NameComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::NameComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::NameComponent>(entity); } },
            { "Particle Emitter", ecs.hasComponent<ECS::ParticleEmitterComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::ParticleEmitterComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::ParticleEmitterComponent>(entity); } },
            { "Animation", ecs.hasComponent<ECS::AnimationComponent>(entity),
              [entity]() { ECS::ECSManager::Instance().addComponent<ECS::AnimationComponent>(entity); },
              [entity]() { ECS::ECSManager::Instance().removeComponent<ECS::AnimationComponent>(entity); } },
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
                    populateOutlinerDetails(entity);
                    if (label == "Name")
                    {
                        refreshWorldOutliner();
                    }
                    showToastMessage("Added " + label + " component.", 2.0f);
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
        showToastMessage("Mesh assigned: " + assetPath, 2.5f);
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
        showToastMessage("Material assigned: " + assetPath, 2.5f);
        break;
    }
    case AssetType::Script:
    {
        std::optional<ECS::ScriptComponent> oldScript;
        if (auto* s = ecs.getComponent<ECS::ScriptComponent>(entity)) oldScript = *s;

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

        UndoRedoManager::Instance().pushCommand({
            "Assign Script",
            [entity, comp]() {
                auto& e = ECS::ECSManager::Instance();
                if (e.hasComponent<ECS::ScriptComponent>(entity)) e.setComponent(entity, comp); else e.addComponent(entity, comp);
                DiagnosticsManager::Instance().invalidateEntity(entity);
            },
            [entity, oldScript]() {
                auto& e = ECS::ECSManager::Instance();
                if (oldScript) { if (e.hasComponent<ECS::ScriptComponent>(entity)) e.setComponent(entity, *oldScript); else e.addComponent(entity, *oldScript); }
                else { e.removeComponent<ECS::ScriptComponent>(entity); }
                DiagnosticsManager::Instance().invalidateEntity(entity);
            }
        });

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

void UIManager::focusContentBrowserSearch()
{
    if (auto* searchBar = findElementById("ContentBrowser.Search"))
    {
        setFocusedEntry(searchBar);
        markAllWidgetsDirty();
    }
}

// ---------------------------------------------------------------------------
// Entity clipboard: Copy / Paste / Duplicate
// ---------------------------------------------------------------------------

void UIManager::copySelectedEntity()
{
    const auto entity = static_cast<ECS::Entity>(m_outlinerSelectedEntity);
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
    if (ecs.hasComponent<ECS::ScriptComponent>(entity))
        m_entityClipboard.script = *ecs.getComponent<ECS::ScriptComponent>(entity);
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
    showToastMessage("Copied: " + entityName, 2.0f);
}

bool UIManager::pasteEntity()
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
    if (m_entityClipboard.script)
        ecs.addComponent<ECS::ScriptComponent>(newEntity, *m_entityClipboard.script);
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
    refreshWorldOutliner();

    std::string entityName = "Entity " + std::to_string(newEntity);
    if (m_entityClipboard.name)
        entityName = m_entityClipboard.name->displayName + " (Copy)";
    showToastMessage("Pasted: " + entityName, 2.0f);

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
        if (cb.script)           e.addComponent<ECS::ScriptComponent>(newEntity, *cb.script);
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

bool UIManager::duplicateSelectedEntity()
{
    const auto entity = static_cast<ECS::Entity>(m_outlinerSelectedEntity);
    if (entity == 0) return false;

    // Snapshot directly from the source entity (bypass clipboard so Ctrl+D doesn't overwrite it)
    EntityClipboard snapshot;
    snapshot.valid = true;
    auto& ecs = ECS::ECSManager::Instance();

    if (ecs.hasComponent<ECS::TransformComponent>(entity))   snapshot.transform       = *ecs.getComponent<ECS::TransformComponent>(entity);
    if (ecs.hasComponent<ECS::MeshComponent>(entity))        snapshot.mesh            = *ecs.getComponent<ECS::MeshComponent>(entity);
    if (ecs.hasComponent<ECS::MaterialComponent>(entity))    snapshot.material        = *ecs.getComponent<ECS::MaterialComponent>(entity);
    if (ecs.hasComponent<ECS::LightComponent>(entity))       snapshot.light           = *ecs.getComponent<ECS::LightComponent>(entity);
    if (ecs.hasComponent<ECS::CameraComponent>(entity))      snapshot.camera          = *ecs.getComponent<ECS::CameraComponent>(entity);
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))     snapshot.physics         = *ecs.getComponent<ECS::PhysicsComponent>(entity);
    if (ecs.hasComponent<ECS::ScriptComponent>(entity))      snapshot.script          = *ecs.getComponent<ECS::ScriptComponent>(entity);
    if (ecs.hasComponent<ECS::NameComponent>(entity))        snapshot.name            = *ecs.getComponent<ECS::NameComponent>(entity);
    if (ecs.hasComponent<ECS::CollisionComponent>(entity))   snapshot.collision       = *ecs.getComponent<ECS::CollisionComponent>(entity);
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity)) snapshot.heightField     = *ecs.getComponent<ECS::HeightFieldComponent>(entity);
    if (ecs.hasComponent<ECS::LodComponent>(entity))         snapshot.lod             = *ecs.getComponent<ECS::LodComponent>(entity);
    if (ecs.hasComponent<ECS::AnimationComponent>(entity))   snapshot.animation       = *ecs.getComponent<ECS::AnimationComponent>(entity);
    if (ecs.hasComponent<ECS::ParticleEmitterComponent>(entity)) snapshot.particleEmitter = *ecs.getComponent<ECS::ParticleEmitterComponent>(entity);

    // Temporarily put snapshot into clipboard, paste, then restore
    EntityClipboard savedClipboard = m_entityClipboard;
    m_entityClipboard = snapshot;
    const bool ok = pasteEntity();
    m_entityClipboard = savedClipboard;
    return ok;
}

bool UIManager::hasEntityClipboard() const
{
    return m_entityClipboard.valid;
}

// â”€â”€ Prefab / Entity Templates â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

static json prefabSerializeFloat3(const float v[3])
{
    return nlohmann::json::array({ v[0], v[1], v[2] });
}

static void prefabDeserializeFloat3(const nlohmann::json& j, float out[3])
{
    if (!j.is_array() || j.size() < 3) return;
    for (size_t i = 0; i < 3; ++i) out[i] = j.at(i).get<float>();
}

static nlohmann::json prefabSerializeEntity(ECS::Entity entity, ECS::ECSManager& ecs)
{
    using json = nlohmann::json;
    json comps = json::object();

    if (const auto* c = ecs.getComponent<ECS::NameComponent>(entity))
        comps["Name"] = json{ {"displayName", c->displayName} };

    if (const auto* c = ecs.getComponent<ECS::TransformComponent>(entity))
        comps["Transform"] = json{
            {"position", prefabSerializeFloat3(c->position)},
            {"rotation", prefabSerializeFloat3(c->rotation)},
            {"scale",    prefabSerializeFloat3(c->scale)}
        };

    if (const auto* c = ecs.getComponent<ECS::MeshComponent>(entity))
        comps["Mesh"] = json{ {"meshAssetPath", c->meshAssetPath} };

    if (const auto* c = ecs.getComponent<ECS::MaterialComponent>(entity))
    {
        json j = json{ {"materialAssetPath", c->materialAssetPath} };
        const auto& ov = c->overrides;
        if (ov.hasAnyOverride())
        {
            json ovJson;
            if (ov.hasColorTint)  ovJson["colorTint"]  = prefabSerializeFloat3(ov.colorTint);
            if (ov.hasMetallic)   ovJson["metallic"]    = ov.metallic;
            if (ov.hasRoughness)  ovJson["roughness"]   = ov.roughness;
            if (ov.hasShininess)  ovJson["shininess"]   = ov.shininess;
            if (ov.hasEmissive)   ovJson["emissive"]    = prefabSerializeFloat3(ov.emissiveColor);
            j["overrides"] = ovJson;
        }
        comps["Material"] = j;
    }

    if (const auto* c = ecs.getComponent<ECS::LightComponent>(entity))
        comps["Light"] = json{
            {"type",      static_cast<int>(c->type)},
            {"color",     prefabSerializeFloat3(c->color)},
            {"intensity", c->intensity},
            {"range",     c->range},
            {"spotAngle", c->spotAngle}
        };

    if (const auto* c = ecs.getComponent<ECS::CameraComponent>(entity))
        comps["Camera"] = json{ {"fov", c->fov}, {"nearClip", c->nearClip}, {"farClip", c->farClip} };

    if (const auto* c = ecs.getComponent<ECS::PhysicsComponent>(entity))
        comps["Physics"] = json{
            {"motionType",          static_cast<int>(c->motionType)},
            {"mass",                c->mass},
            {"gravityFactor",       c->gravityFactor},
            {"linearDamping",       c->linearDamping},
            {"angularDamping",      c->angularDamping},
            {"maxLinearVelocity",   c->maxLinearVelocity},
            {"maxAngularVelocity",  c->maxAngularVelocity},
            {"motionQuality",       static_cast<int>(c->motionQuality)},
            {"allowSleeping",       c->allowSleeping},
            {"velocity",            prefabSerializeFloat3(c->velocity)},
            {"angularVelocity",     prefabSerializeFloat3(c->angularVelocity)}
        };

    if (const auto* c = ecs.getComponent<ECS::CollisionComponent>(entity))
        comps["Collision"] = json{
            {"colliderType",   static_cast<int>(c->colliderType)},
            {"colliderSize",   prefabSerializeFloat3(c->colliderSize)},
            {"colliderOffset", prefabSerializeFloat3(c->colliderOffset)},
            {"restitution",    c->restitution},
            {"friction",       c->friction},
            {"isSensor",       c->isSensor}
        };

    if (const auto* c = ecs.getComponent<ECS::ScriptComponent>(entity))
        comps["Script"] = json{ {"scriptPath", c->scriptPath} };

    if (const auto* c = ecs.getComponent<ECS::LodComponent>(entity))
    {
        json levels = json::array();
        for (const auto& lv : c->levels)
            levels.push_back(json{ {"meshAssetPath", lv.meshAssetPath}, {"maxDistance", lv.maxDistance} });
        comps["Lod"] = json{ {"levels", levels} };
    }

    if (const auto* c = ecs.getComponent<ECS::AnimationComponent>(entity))
        comps["Animation"] = json{
            {"currentClipIndex", c->currentClipIndex},
            {"speed",            c->speed},
            {"playing",          c->playing},
            {"loop",             c->loop}
        };

    if (const auto* c = ecs.getComponent<ECS::ParticleEmitterComponent>(entity))
        comps["ParticleEmitter"] = json{
            {"maxParticles",  c->maxParticles},  {"emissionRate",  c->emissionRate},
            {"lifetime",      c->lifetime},      {"speed",         c->speed},
            {"speedVariance", c->speedVariance}, {"size",          c->size},
            {"sizeEnd",       c->sizeEnd},       {"gravity",       c->gravity},
            {"colorR", c->colorR}, {"colorG", c->colorG}, {"colorB", c->colorB}, {"colorA", c->colorA},
            {"colorEndR", c->colorEndR}, {"colorEndG", c->colorEndG}, {"colorEndB", c->colorEndB}, {"colorEndA", c->colorEndA},
            {"coneAngle", c->coneAngle}, {"enabled", c->enabled}, {"loop", c->loop}
        };

    return json{ {"components", comps} };
}

static void prefabDeserializeEntity(const nlohmann::json& entityJson, ECS::Entity entity, ECS::ECSManager& ecs, const Vec3& posOverride, bool overridePos)
{
    using json = nlohmann::json;
    if (!entityJson.contains("components")) return;
    const auto& c = entityJson.at("components");
    if (!c.is_object()) return;

    if (c.contains("Name"))
    {
        ECS::NameComponent comp;
        if (c.at("Name").contains("displayName")) comp.displayName = c.at("Name").at("displayName").get<std::string>();
        ecs.addComponent<ECS::NameComponent>(entity, comp);
    }

    if (c.contains("Transform"))
    {
        ECS::TransformComponent comp;
        const auto& t = c.at("Transform");
        if (t.contains("position")) prefabDeserializeFloat3(t.at("position"), comp.position);
        if (t.contains("rotation")) prefabDeserializeFloat3(t.at("rotation"), comp.rotation);
        if (t.contains("scale"))    prefabDeserializeFloat3(t.at("scale"),    comp.scale);
        if (overridePos) { comp.position[0] = posOverride.x; comp.position[1] = posOverride.y; comp.position[2] = posOverride.z; }
        ecs.addComponent<ECS::TransformComponent>(entity, comp);
    }
    else if (overridePos)
    {
        ECS::TransformComponent comp;
        comp.position[0] = posOverride.x; comp.position[1] = posOverride.y; comp.position[2] = posOverride.z;
        ecs.addComponent<ECS::TransformComponent>(entity, comp);
    }

    if (c.contains("Mesh"))
    {
        ECS::MeshComponent comp;
        if (c.at("Mesh").contains("meshAssetPath")) comp.meshAssetPath = c.at("Mesh").at("meshAssetPath").get<std::string>();
        ecs.addComponent<ECS::MeshComponent>(entity, comp);
    }

    if (c.contains("Material"))
    {
        ECS::MaterialComponent comp;
        const auto& m = c.at("Material");
        if (m.contains("materialAssetPath")) comp.materialAssetPath = m.at("materialAssetPath").get<std::string>();
        if (m.contains("overrides"))
        {
            const auto& ov = m.at("overrides");
            if (ov.contains("colorTint"))  { prefabDeserializeFloat3(ov.at("colorTint"), comp.overrides.colorTint); comp.overrides.hasColorTint = true; }
            if (ov.contains("metallic"))   { comp.overrides.metallic  = ov.at("metallic").get<float>();  comp.overrides.hasMetallic = true; }
            if (ov.contains("roughness"))  { comp.overrides.roughness = ov.at("roughness").get<float>(); comp.overrides.hasRoughness = true; }
            if (ov.contains("shininess"))  { comp.overrides.shininess = ov.at("shininess").get<float>(); comp.overrides.hasShininess = true; }
            if (ov.contains("emissive"))   { prefabDeserializeFloat3(ov.at("emissive"), comp.overrides.emissiveColor); comp.overrides.hasEmissive = true; }
        }
        ecs.addComponent<ECS::MaterialComponent>(entity, comp);
    }

    if (c.contains("Light"))
    {
        ECS::LightComponent comp;
        const auto& l = c.at("Light");
        if (l.contains("type"))      comp.type      = static_cast<ECS::LightComponent::LightType>(l.at("type").get<int>());
        if (l.contains("color"))     prefabDeserializeFloat3(l.at("color"), comp.color);
        if (l.contains("intensity")) comp.intensity  = l.at("intensity").get<float>();
        if (l.contains("range"))     comp.range      = l.at("range").get<float>();
        if (l.contains("spotAngle")) comp.spotAngle  = l.at("spotAngle").get<float>();
        ecs.addComponent<ECS::LightComponent>(entity, comp);
    }

    if (c.contains("Camera"))
    {
        ECS::CameraComponent comp;
        const auto& cm = c.at("Camera");
        if (cm.contains("fov"))      comp.fov      = cm.at("fov").get<float>();
        if (cm.contains("nearClip")) comp.nearClip  = cm.at("nearClip").get<float>();
        if (cm.contains("farClip"))  comp.farClip   = cm.at("farClip").get<float>();
        ecs.addComponent<ECS::CameraComponent>(entity, comp);
    }

    if (c.contains("Physics"))
    {
        ECS::PhysicsComponent comp;
        const auto& p = c.at("Physics");
        if (p.contains("motionType"))         comp.motionType         = static_cast<ECS::PhysicsComponent::MotionType>(p.at("motionType").get<int>());
        if (p.contains("mass"))               comp.mass               = p.at("mass").get<float>();
        if (p.contains("gravityFactor"))      comp.gravityFactor      = p.at("gravityFactor").get<float>();
        if (p.contains("linearDamping"))      comp.linearDamping      = p.at("linearDamping").get<float>();
        if (p.contains("angularDamping"))     comp.angularDamping     = p.at("angularDamping").get<float>();
        if (p.contains("maxLinearVelocity"))  comp.maxLinearVelocity  = p.at("maxLinearVelocity").get<float>();
        if (p.contains("maxAngularVelocity")) comp.maxAngularVelocity = p.at("maxAngularVelocity").get<float>();
        if (p.contains("motionQuality"))      comp.motionQuality      = static_cast<ECS::PhysicsComponent::MotionQuality>(p.at("motionQuality").get<int>());
        if (p.contains("allowSleeping"))      comp.allowSleeping      = p.at("allowSleeping").get<bool>();
        if (p.contains("velocity"))           prefabDeserializeFloat3(p.at("velocity"), comp.velocity);
        if (p.contains("angularVelocity"))    prefabDeserializeFloat3(p.at("angularVelocity"), comp.angularVelocity);
        ecs.addComponent<ECS::PhysicsComponent>(entity, comp);
    }

    if (c.contains("Collision"))
    {
        ECS::CollisionComponent comp;
        const auto& cl = c.at("Collision");
        if (cl.contains("colliderType"))   comp.colliderType = static_cast<ECS::CollisionComponent::ColliderType>(cl.at("colliderType").get<int>());
        if (cl.contains("colliderSize"))   prefabDeserializeFloat3(cl.at("colliderSize"), comp.colliderSize);
        if (cl.contains("colliderOffset")) prefabDeserializeFloat3(cl.at("colliderOffset"), comp.colliderOffset);
        if (cl.contains("restitution"))    comp.restitution  = cl.at("restitution").get<float>();
        if (cl.contains("friction"))       comp.friction     = cl.at("friction").get<float>();
        if (cl.contains("isSensor"))       comp.isSensor     = cl.at("isSensor").get<bool>();
        ecs.addComponent<ECS::CollisionComponent>(entity, comp);
    }

    if (c.contains("Script"))
    {
        ECS::ScriptComponent comp;
        if (c.at("Script").contains("scriptPath")) comp.scriptPath = c.at("Script").at("scriptPath").get<std::string>();
        ecs.addComponent<ECS::ScriptComponent>(entity, comp);
    }

    if (c.contains("Lod"))
    {
        ECS::LodComponent comp;
        if (c.at("Lod").contains("levels") && c.at("Lod").at("levels").is_array())
        {
            for (const auto& lv : c.at("Lod").at("levels"))
            {
                ECS::LodComponent::LodLevel level;
                if (lv.contains("meshAssetPath")) level.meshAssetPath = lv.at("meshAssetPath").get<std::string>();
                if (lv.contains("maxDistance"))    level.maxDistance    = lv.at("maxDistance").get<float>();
                comp.levels.push_back(std::move(level));
            }
        }
        ecs.addComponent<ECS::LodComponent>(entity, comp);
    }

    if (c.contains("Animation"))
    {
        ECS::AnimationComponent comp;
        const auto& a = c.at("Animation");
        if (a.contains("currentClipIndex")) comp.currentClipIndex = a.at("currentClipIndex").get<int>();
        if (a.contains("speed"))            comp.speed            = a.at("speed").get<float>();
        if (a.contains("playing"))          comp.playing          = a.at("playing").get<bool>();
        if (a.contains("loop"))             comp.loop             = a.at("loop").get<bool>();
        ecs.addComponent<ECS::AnimationComponent>(entity, comp);
    }

    if (c.contains("ParticleEmitter"))
    {
        ECS::ParticleEmitterComponent comp;
        const auto& pe = c.at("ParticleEmitter");
        if (pe.contains("maxParticles"))  comp.maxParticles  = pe.at("maxParticles").get<int>();
        if (pe.contains("emissionRate"))  comp.emissionRate  = pe.at("emissionRate").get<float>();
        if (pe.contains("lifetime"))      comp.lifetime      = pe.at("lifetime").get<float>();
        if (pe.contains("speed"))         comp.speed         = pe.at("speed").get<float>();
        if (pe.contains("speedVariance")) comp.speedVariance = pe.at("speedVariance").get<float>();
        if (pe.contains("size"))          comp.size          = pe.at("size").get<float>();
        if (pe.contains("sizeEnd"))       comp.sizeEnd       = pe.at("sizeEnd").get<float>();
        if (pe.contains("gravity"))       comp.gravity       = pe.at("gravity").get<float>();
        if (pe.contains("colorR"))        comp.colorR        = pe.at("colorR").get<float>();
        if (pe.contains("colorG"))        comp.colorG        = pe.at("colorG").get<float>();
        if (pe.contains("colorB"))        comp.colorB        = pe.at("colorB").get<float>();
        if (pe.contains("colorA"))        comp.colorA        = pe.at("colorA").get<float>();
        if (pe.contains("colorEndR"))     comp.colorEndR     = pe.at("colorEndR").get<float>();
        if (pe.contains("colorEndG"))     comp.colorEndG     = pe.at("colorEndG").get<float>();
        if (pe.contains("colorEndB"))     comp.colorEndB     = pe.at("colorEndB").get<float>();
        if (pe.contains("colorEndA"))     comp.colorEndA     = pe.at("colorEndA").get<float>();
        if (pe.contains("coneAngle"))     comp.coneAngle     = pe.at("coneAngle").get<float>();
        if (pe.contains("enabled"))       comp.enabled       = pe.at("enabled").get<bool>();
        if (pe.contains("loop"))          comp.loop          = pe.at("loop").get<bool>();
        ecs.addComponent<ECS::ParticleEmitterComponent>(entity, comp);
    }
}

bool UIManager::savePrefabFromEntity(ECS::Entity entity, const std::string& name, const std::string& folder)
{
    using json = nlohmann::json;
    auto& ecs = ECS::ECSManager::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();

    if (!diagnostics.isProjectLoaded())
    {
        showToastMessage("No project loaded.", 3.0f);
        return false;
    }

    // Serialize the entity
    json entityData = prefabSerializeEntity(entity, ecs);

    // Build the prefab asset file
    json fileJson;
    fileJson["magic"]   = 0x41535453;
    fileJson["version"] = 2;
    fileJson["type"]    = static_cast<int>(AssetType::Prefab);
    fileJson["name"]    = name;
    fileJson["data"]    = json{ {"entities", json::array({ entityData })} };

    // Write to disk
    const std::filesystem::path contentDir =
        std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
    const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);

    // Find unique filename
    std::string fileName = name + ".asset";
    int counter = 1;
    while (std::filesystem::exists(targetDir / fileName))
    {
        fileName = name + std::to_string(counter++) + ".asset";
    }

    const std::filesystem::path absPath = targetDir / fileName;
    std::ofstream out(absPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
    {
        showToastMessage("Failed to create prefab file.", 3.0f);
        return false;
    }

    out << fileJson.dump(4);
    out.close();

    // Register in asset registry
    const std::string relPath = std::filesystem::relative(absPath, contentDir).generic_string();
    AssetRegistryEntry entry;
    entry.name = std::filesystem::path(fileName).stem().string();
    entry.path = relPath;
    entry.type = AssetType::Prefab;
    AssetManager::Instance().registerAssetInRegistry(entry);

    refreshContentBrowser();
    showToastMessage("Prefab saved: " + entry.name, 3.0f);

    Logger::Instance().log(Logger::Category::AssetManagement,
        "Saved prefab '" + name + "' to " + relPath, Logger::LogLevel::INFO);
    return true;
}

bool UIManager::spawnPrefabAtPosition(const std::string& prefabRelPath, const Vec3& pos)
{
    using json = nlohmann::json;
    auto& ecs = ECS::ECSManager::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();

    if (!diagnostics.isProjectLoaded())
    {
        showToastMessage("No project loaded.", 3.0f);
        return false;
    }

    // Load prefab JSON from disk
    const std::filesystem::path absPath =
        std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content" / prefabRelPath;

    if (!std::filesystem::exists(absPath))
    {
        showToastMessage("Prefab file not found.", 3.0f);
        return false;
    }

    std::ifstream in(absPath);
    if (!in.is_open())
    {
        showToastMessage("Failed to open prefab file.", 3.0f);
        return false;
    }

    json fileJson;
    try { in >> fileJson; } catch (...) { showToastMessage("Invalid prefab file.", 3.0f); return false; }

    if (!fileJson.is_object() || !fileJson.contains("data"))
    {
        showToastMessage("Invalid prefab format.", 3.0f);
        return false;
    }

    const auto& data = fileJson.at("data");
    if (!data.contains("entities") || !data.at("entities").is_array())
    {
        showToastMessage("Prefab has no entities.", 3.0f);
        return false;
    }

    const std::string prefabName = fileJson.value("name", std::filesystem::path(prefabRelPath).stem().string());
    auto* level = diagnostics.getActiveLevelSoft();

    for (const auto& entJson : data.at("entities"))
    {
        const ECS::Entity newEntity = ecs.createEntity();
        prefabDeserializeEntity(entJson, newEntity, ecs, pos, true);

        // Ensure the entity has a name
        if (!ecs.hasComponent<ECS::NameComponent>(newEntity))
        {
            ECS::NameComponent nc;
            nc.displayName = prefabName;
            ecs.addComponent<ECS::NameComponent>(newEntity, nc);
        }

        if (level) level->onEntityAdded(newEntity);

        // Select the last spawned entity
        selectEntity(static_cast<unsigned int>(newEntity));
        if (m_renderer) m_renderer->setSelectedEntity(newEntity);

        // Undo/Redo: snapshot the JSON for redo so we can recreate identically
        const json snapshot = entJson;
        const Vec3 spawnPos = pos;
        const std::string entityName = ecs.hasComponent<ECS::NameComponent>(newEntity)
            ? ecs.getComponent<ECS::NameComponent>(newEntity)->displayName
            : ("Entity " + std::to_string(newEntity));

        UndoRedoManager::Command cmd;
        cmd.description = "Spawn Prefab: " + entityName;
        cmd.execute = [newEntity, snapshot, spawnPos]()
        {
            auto& e = ECS::ECSManager::Instance();
            e.createEntity(newEntity);
            prefabDeserializeEntity(snapshot, newEntity, e, spawnPos, true);
            if (!e.hasComponent<ECS::NameComponent>(newEntity))
            {
                ECS::NameComponent nc;
                nc.displayName = "Entity " + std::to_string(newEntity);
                e.addComponent<ECS::NameComponent>(newEntity, nc);
            }
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
    }

    refreshWorldOutliner();
    showToastMessage("Spawned: " + prefabName, 2.0f);
    Logger::Instance().log(Logger::Category::Engine,
        "Spawned prefab '" + prefabName + "' at (" +
        std::to_string(pos.x) + ", " + std::to_string(pos.y) + ", " + std::to_string(pos.z) + ")",
        Logger::LogLevel::INFO);
    return true;
}

bool UIManager::spawnBuiltinTemplate(const std::string& templateName, const Vec3& pos)
{
    using json = nlohmann::json;
    auto& ecs = ECS::ECSManager::Instance();
    auto& diagnostics = DiagnosticsManager::Instance();
    auto* level = diagnostics.getActiveLevelSoft();

    const ECS::Entity newEntity = ecs.createEntity();

    // Transform at spawn position
    ECS::TransformComponent transform{};
    transform.position[0] = pos.x;
    transform.position[1] = pos.y;
    transform.position[2] = pos.z;
    ecs.addComponent<ECS::TransformComponent>(newEntity, transform);

    // Name
    ECS::NameComponent nameComp;
    nameComp.displayName = templateName;
    ecs.addComponent<ECS::NameComponent>(newEntity, nameComp);

    if (templateName == "Point Light")
    {
        ECS::LightComponent light;
        light.type = ECS::LightComponent::LightType::Point;
        light.color[0] = 1.0f; light.color[1] = 1.0f; light.color[2] = 1.0f;
        light.intensity = 1.0f;
        light.range = 10.0f;
        ecs.addComponent<ECS::LightComponent>(newEntity, light);
    }
    else if (templateName == "Directional Light")
    {
        ECS::LightComponent light;
        light.type = ECS::LightComponent::LightType::Directional;
        light.color[0] = 1.0f; light.color[1] = 0.95f; light.color[2] = 0.85f;
        light.intensity = 1.0f;
        ecs.addComponent<ECS::LightComponent>(newEntity, light);
        // Point downward
        auto* tc = ecs.getComponent<ECS::TransformComponent>(newEntity);
        if (tc) { tc->rotation[0] = -45.0f; tc->rotation[1] = 30.0f; }
    }
    else if (templateName == "Camera")
    {
        ECS::CameraComponent cam;
        cam.fov = 60.0f;
        cam.nearClip = 0.1f;
        cam.farClip = 1000.0f;
        ecs.addComponent<ECS::CameraComponent>(newEntity, cam);
    }
    else if (templateName == "Static Mesh")
    {
        ECS::MeshComponent mesh;
        mesh.meshAssetPath = "";
        ecs.addComponent<ECS::MeshComponent>(newEntity, mesh);
        ECS::MaterialComponent mat;
        mat.materialAssetPath = "";
        ecs.addComponent<ECS::MaterialComponent>(newEntity, mat);
    }
    else if (templateName == "Physics Object")
    {
        ECS::MeshComponent mesh;
        mesh.meshAssetPath = "";
        ecs.addComponent<ECS::MeshComponent>(newEntity, mesh);
        ECS::MaterialComponent mat;
        mat.materialAssetPath = "";
        ecs.addComponent<ECS::MaterialComponent>(newEntity, mat);
        ECS::PhysicsComponent phys;
        phys.motionType = ECS::PhysicsComponent::MotionType::Dynamic;
        phys.mass = 1.0f;
        phys.gravityFactor = 1.0f;
        ecs.addComponent<ECS::PhysicsComponent>(newEntity, phys);
        ECS::CollisionComponent coll;
        coll.colliderType = ECS::CollisionComponent::ColliderType::Box;
        coll.colliderSize[0] = 1.0f; coll.colliderSize[1] = 1.0f; coll.colliderSize[2] = 1.0f;
        ecs.addComponent<ECS::CollisionComponent>(newEntity, coll);
    }
    else if (templateName == "Particle Emitter")
    {
        ECS::ParticleEmitterComponent pe;
        pe.enabled = true;
        pe.loop = true;
        pe.maxParticles = 500;
        pe.emissionRate = 50.0f;
        pe.lifetime = 2.0f;
        pe.speed = 2.0f;
        pe.size = 0.1f;
        pe.sizeEnd = 0.0f;
        pe.gravity = -1.0f;
        pe.colorR = 1.0f; pe.colorG = 0.8f; pe.colorB = 0.3f; pe.colorA = 1.0f;
        pe.colorEndR = 1.0f; pe.colorEndG = 0.2f; pe.colorEndB = 0.0f; pe.colorEndA = 0.0f;
        ecs.addComponent<ECS::ParticleEmitterComponent>(newEntity, pe);
    }
    // else: "Empty Entity" â€“ just Transform + Name, already added above

    if (level) level->onEntityAdded(newEntity);

    selectEntity(static_cast<unsigned int>(newEntity));
    if (m_renderer) m_renderer->setSelectedEntity(newEntity);
    refreshWorldOutliner();
    showToastMessage("Created: " + templateName, 2.0f);

    // Undo/Redo
    const json snapshot = prefabSerializeEntity(newEntity, ecs);
    const Vec3 spawnPos = pos;
    UndoRedoManager::Command cmd;
    cmd.description = "Create " + templateName;
    cmd.execute = [newEntity, snapshot, spawnPos]()
    {
        auto& e = ECS::ECSManager::Instance();
        e.createEntity(newEntity);
        prefabDeserializeEntity(snapshot, newEntity, e, spawnPos, false);
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

    Logger::Instance().log(Logger::Category::Engine,
        "Spawned built-in template '" + templateName + "' entity=" + std::to_string(newEntity),
        Logger::LogLevel::INFO);
    return true;
}

bool UIManager::autoFitColliderForEntity(ECS::Entity entity)
{
    auto& ecs = ECS::ECSManager::Instance();

    // Need a mesh to compute AABB
    if (!ecs.hasComponent<ECS::MeshComponent>(entity))
        return false;

    const auto* meshComp = ecs.getComponent<ECS::MeshComponent>(entity);
    if (!meshComp || meshComp->meshAssetPath.empty())
        return false;

    // Load or get the mesh asset to read vertex data
    auto& assetMgr = AssetManager::Instance();
    auto meshAsset = assetMgr.getLoadedAssetByPath(meshComp->meshAssetPath);
    if (!meshAsset)
    {
        int id = assetMgr.loadAsset(meshComp->meshAssetPath, AssetType::Model3D);
        if (id > 0)
            meshAsset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(id));
    }
    if (!meshAsset)
        return false;

    const auto& data = meshAsset->getData();
    if (!data.contains("m_vertices") || !data["m_vertices"].is_array())
        return false;

    const auto& verts = data["m_vertices"];
    if (verts.size() < 5)
        return false;

    // Compute AABB from vertices (layout: pos3 + uv2 = stride 5)
    float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max(), maxY = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max(), maxZ = std::numeric_limits<float>::lowest();

    const size_t stride = 5;
    const size_t vertCount = verts.size() / stride;
    for (size_t i = 0; i < vertCount; ++i)
    {
        const float x = verts[i * stride + 0].get<float>();
        const float y = verts[i * stride + 1].get<float>();
        const float z = verts[i * stride + 2].get<float>();
        if (x < minX) minX = x; if (x > maxX) maxX = x;
        if (y < minY) minY = y; if (y > maxY) maxY = y;
        if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
    }

    // Apply entity scale to AABB
    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
    if (ecs.hasComponent<ECS::TransformComponent>(entity))
    {
        const auto* t = ecs.getComponent<ECS::TransformComponent>(entity);
        scaleX = t->scale[0]; scaleY = t->scale[1]; scaleZ = t->scale[2];
    }

    const float halfX = (maxX - minX) * 0.5f * std::abs(scaleX);
    const float halfY = (maxY - minY) * 0.5f * std::abs(scaleY);
    const float halfZ = (maxZ - minZ) * 0.5f * std::abs(scaleZ);

    // Compute center offset (in local space)
    const float centerX = (minX + maxX) * 0.5f;
    const float centerY = (minY + maxY) * 0.5f;
    const float centerZ = (minZ + maxZ) * 0.5f;

    // Heuristic: determine best collider type
    // Aspect ratio checks
    const float maxHalf = std::max({ halfX, halfY, halfZ });
    const float minHalf = std::min({ halfX, halfY, halfZ });
    const float aspectRatio = (minHalf > 0.001f) ? (maxHalf / minHalf) : 10.0f;

    ECS::CollisionComponent collision{};
    collision.colliderOffset[0] = centerX;
    collision.colliderOffset[1] = centerY;
    collision.colliderOffset[2] = centerZ;

    // Nearly cubic â†’ Sphere if all dimensions similar, else Box
    const float midHalf = halfX + halfY + halfZ - maxHalf - minHalf;
    const float sphereRatio = (minHalf > 0.001f) ? (maxHalf / minHalf) : 10.0f;

    if (sphereRatio < 1.4f)
    {
        // Approximately cube-like â†’ Sphere
        collision.colliderType = ECS::CollisionComponent::ColliderType::Sphere;
        collision.colliderSize[0] = maxHalf; // radius
        collision.colliderSize[1] = 0.0f;
        collision.colliderSize[2] = 0.0f;
    }
    else if (aspectRatio > 2.5f && halfY > halfX && halfY > halfZ)
    {
        // Tall and thin â†’ Capsule (vertical)
        collision.colliderType = ECS::CollisionComponent::ColliderType::Capsule;
        collision.colliderSize[0] = std::max(halfX, halfZ); // radius
        collision.colliderSize[1] = halfY;                   // half-height
        collision.colliderSize[2] = 0.0f;
    }
    else
    {
        // Default â†’ Box
        collision.colliderType = ECS::CollisionComponent::ColliderType::Box;
        collision.colliderSize[0] = halfX;
        collision.colliderSize[1] = halfY;
        collision.colliderSize[2] = halfZ;
    }

    // Add or update the CollisionComponent
    if (!ecs.hasComponent<ECS::CollisionComponent>(entity))
        ecs.addComponent<ECS::CollisionComponent>(entity, collision);
    else
        ecs.setComponent<ECS::CollisionComponent>(entity, collision);

    return true;
}

// ---------------------------------------------------------------------------
// computeEntityBottomOffset â€“ distance from entity pivot to bottom of mesh
// ---------------------------------------------------------------------------
float UIManager::computeEntityBottomOffset(ECS::Entity entity) const
{
    auto& ecs = ECS::ECSManager::Instance();

    if (!ecs.hasComponent<ECS::MeshComponent>(entity))
        return 0.0f;

    const auto* meshComp = ecs.getComponent<ECS::MeshComponent>(entity);
    if (!meshComp || meshComp->meshAssetPath.empty())
        return 0.0f;

    auto& assetMgr = AssetManager::Instance();
    auto meshAsset = assetMgr.getLoadedAssetByPath(meshComp->meshAssetPath);
    if (!meshAsset)
    {
        int id = assetMgr.loadAsset(meshComp->meshAssetPath, AssetType::Model3D);
        if (id > 0)
            meshAsset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(id));
    }
    if (!meshAsset)
        return 0.0f;

    const auto& data = meshAsset->getData();
    if (!data.contains("m_vertices") || !data["m_vertices"].is_array())
        return 0.0f;

    const auto& verts = data["m_vertices"];
    const size_t stride = 5;
    const size_t vertCount = verts.size() / stride;
    if (vertCount == 0)
        return 0.0f;

    float minY = std::numeric_limits<float>::max();
    for (size_t i = 0; i < vertCount; ++i)
    {
        const float vy = verts[i * stride + 1].get<float>();
        if (vy < minY) minY = vy;
    }

    float scaleY = 1.0f;
    if (ecs.hasComponent<ECS::TransformComponent>(entity))
    {
        const auto* t = ecs.getComponent<ECS::TransformComponent>(entity);
        if (t) scaleY = std::abs(t->scale[1]);
    }

    return -minY * scaleY;
}

// ---------------------------------------------------------------------------
// dropSelectedEntitiesToSurface â€“ raycast each selected entity downward and
// place it on the first hit surface.
// ---------------------------------------------------------------------------
void UIManager::dropSelectedEntitiesToSurface(const RaycastDownFn& raycastDown)
{
    if (!m_renderer || !raycastDown)
        return;

    auto& ecs = ECS::ECSManager::Instance();

    // Gather entities to process (multi-select or single)
    std::vector<ECS::Entity> entities;
    const auto& sel = m_renderer->getSelectedEntities();
    if (!sel.empty())
    {
        for (auto e : sel)
            entities.push_back(static_cast<ECS::Entity>(e));
    }
    else if (m_outlinerSelectedEntity != 0)
    {
        entities.push_back(static_cast<ECS::Entity>(m_outlinerSelectedEntity));
    }

    if (entities.empty())
    {
        showToastMessage("No entity selected.", 2.0f);
        return;
    }

    // Store old transforms for undo
    struct DropRecord
    {
        ECS::Entity entity;
        float oldY;
        float newY;
    };
    std::vector<DropRecord> records;

    for (auto entity : entities)
    {
        if (!ecs.hasComponent<ECS::TransformComponent>(entity))
            continue;

        auto* tc = ecs.getComponent<ECS::TransformComponent>(entity);
        if (!tc)
            continue;

        const float posX = tc->position[0];
        const float posY = tc->position[1];
        const float posZ = tc->position[2];

        // Compute distance from pivot to bottom of mesh
        const float bottomOffset = computeEntityBottomOffset(entity);

        // Raycast straight down from a bit above the entity
        auto [hit, hitY] = raycastDown(posX, posY + 0.1f, posZ);
        if (!hit)
            continue;

        const float newY = hitY + bottomOffset;

        records.push_back({ entity, posY, newY });

        // Apply immediately
        tc->position[1] = newY;
    }

    if (records.empty())
    {
        showToastMessage("No surface found below selection.", 2.5f);
        return;
    }

    // Undo/Redo
    UndoRedoManager::Command cmd;
    cmd.description = (records.size() == 1) ? "Drop to Surface" : ("Drop " + std::to_string(records.size()) + " entities to Surface");
    cmd.execute = [records]()
    {
        auto& e = ECS::ECSManager::Instance();
        for (const auto& r : records)
        {
            if (auto* t = e.getComponent<ECS::TransformComponent>(r.entity))
                t->position[1] = r.newY;
        }
    };
    cmd.undo = [records]()
    {
        auto& e = ECS::ECSManager::Instance();
        for (const auto& r : records)
        {
            if (auto* t = e.getComponent<ECS::TransformComponent>(r.entity))
                t->position[1] = r.oldY;
        }
    };
    UndoRedoManager::Instance().pushCommand(std::move(cmd));

    const std::string msg = (records.size() == 1)
        ? "Dropped entity to surface"
        : ("Dropped " + std::to_string(records.size()) + " entities to surface");
    showToastMessage(msg, 2.0f);
    Logger::Instance().log(Logger::Category::Engine, msg, Logger::LogLevel::INFO);

    markAllWidgetsDirty();
    if (m_outlinerSelectedEntity != 0)
        populateOutlinerDetails(m_outlinerSelectedEntity);
}

void UIManager::createNewLevelWithTemplate(SceneTemplate tmpl, const std::string& levelName, const std::string& relFolder)
{
    auto& diagnostics = DiagnosticsManager::Instance();
    auto& ecs = ECS::ECSManager::Instance();
    auto& logger = Logger::Instance();

    if (!diagnostics.isProjectLoaded())
    {
        showToastMessage("No project loaded.", 2.5f);
        return;
    }

    // Prepare a fresh level
    const std::string levelRelPath = relFolder + "/" + levelName + ".map";
    auto level = std::make_unique<EngineLevel>();
    level->setName(levelName);
    level->setPath(levelRelPath);
    level->setAssetType(AssetType::Level);
    level->setIsSaved(false);

    // Reset ECS for fresh level
    ecs.initialize({});

    // Common asset paths used by templates
    const std::string cubeMesh = "default_quad3d.asset";
    const std::string lightMesh = "Lights/PointLight.asset";
    const std::string wallMat = "Materials/wall.asset";

    auto addEntity = [&](const std::string& name, float px, float py, float pz,
        float rx, float ry, float rz, float sx, float sy, float sz) -> ECS::Entity
    {
        ECS::Entity e = ecs.createEntity();
        ECS::TransformComponent t{};
        t.position[0] = px; t.position[1] = py; t.position[2] = pz;
        t.rotation[0] = rx; t.rotation[1] = ry; t.rotation[2] = rz;
        t.scale[0] = sx; t.scale[1] = sy; t.scale[2] = sz;
        ecs.addComponent<ECS::TransformComponent>(e, t);
        ECS::NameComponent n; n.displayName = name;
        ecs.addComponent<ECS::NameComponent>(e, n);
        level->onEntityAdded(e);
        return e;
    };

    auto addMesh = [&](ECS::Entity e, const std::string& meshPath, const std::string& matPath)
    {
        ECS::MeshComponent m; m.meshAssetPath = meshPath;
        ecs.addComponent<ECS::MeshComponent>(e, m);
        ECS::MaterialComponent mat; mat.materialAssetPath = matPath;
        ecs.addComponent<ECS::MaterialComponent>(e, mat);
    };

    auto addLight = [&](ECS::Entity e, ECS::LightComponent::LightType type,
        float r, float g, float b, float intensity, float range, float spotAngle)
    {
        ECS::LightComponent l{};
        l.type = type;
        l.color[0] = r; l.color[1] = g; l.color[2] = b;
        l.intensity = intensity;
        l.range = range;
        l.spotAngle = spotAngle;
        ecs.addComponent<ECS::LightComponent>(e, l);
    };

    switch (tmpl)
    {
    case SceneTemplate::Empty:
    {
        // Just a directional light so the scene isn't pitch black
        auto dirLight = addEntity("Directional Light", 0.0f, 5.0f, 0.0f, 50.0f, -30.0f, 0.0f, 0.15f, 0.15f, 0.15f);
        addMesh(dirLight, lightMesh, wallMat);
        addLight(dirLight, ECS::LightComponent::LightType::Directional, 0.9f, 0.85f, 0.7f, 0.6f, 0.0f, 0.0f);

        level->setEditorCameraPosition(Vec3{ 0.0f, 3.0f, 8.0f });
        level->setEditorCameraRotation(Vec2{ -15.0f, 0.0f });
        level->setHasEditorCamera(true);
        break;
    }

    case SceneTemplate::BasicOutdoor:
    {
        // Directional light (sun)
        auto sun = addEntity("Sun", 0.0f, 10.0f, 0.0f, 50.0f, -30.0f, 0.0f, 0.15f, 0.15f, 0.15f);
        addMesh(sun, lightMesh, wallMat);
        addLight(sun, ECS::LightComponent::LightType::Directional, 1.0f, 0.95f, 0.8f, 0.7f, 0.0f, 0.0f);

        // Ground plane (flat scaled cube)
        auto ground = addEntity("Ground", 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 50.0f, 0.1f, 50.0f);
        addMesh(ground, cubeMesh, wallMat);

        // Point light for fill
        auto fill = addEntity("Fill Light", 5.0f, 3.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.15f, 0.15f, 0.15f);
        addMesh(fill, lightMesh, wallMat);
        addLight(fill, ECS::LightComponent::LightType::Point, 0.6f, 0.7f, 1.0f, 0.5f, 15.0f, 0.0f);

        // Skybox â€” try the first available skybox asset
        {
            const auto& registry = AssetManager::Instance().getAssetRegistry();
            for (const auto& entry : registry)
            {
                if (entry.type == AssetType::Skybox)
                {
                    level->setSkyboxPath(entry.path);
                    break;
                }
            }
        }

        level->setEditorCameraPosition(Vec3{ 0.0f, 5.0f, 15.0f });
        level->setEditorCameraRotation(Vec2{ -20.0f, 0.0f });
        level->setHasEditorCamera(true);
        break;
    }

    case SceneTemplate::Prototype:
    {
        // Directional light
        auto sun = addEntity("Sun", 0.0f, 8.0f, 0.0f, 50.0f, -30.0f, 0.0f, 0.15f, 0.15f, 0.15f);
        addMesh(sun, lightMesh, wallMat);
        addLight(sun, ECS::LightComponent::LightType::Directional, 0.9f, 0.9f, 0.85f, 0.6f, 0.0f, 0.0f);

        // Grid-like floor (large flat box)
        auto floor = addEntity("Floor", 0.0f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 30.0f, 0.1f, 30.0f);
        addMesh(floor, cubeMesh, wallMat);

        // Scatter some reference cubes
        auto cubeA = addEntity("Cube A", 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        addMesh(cubeA, cubeMesh, wallMat);
        auto cubeB = addEntity("Cube B", 3.0f, 0.5f, 0.0f, 0.0f, 45.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        addMesh(cubeB, cubeMesh, wallMat);
        auto cubeC = addEntity("Cube C", -3.0f, 0.5f, 2.0f, 0.0f, -30.0f, 0.0f, 1.0f, 1.0f, 1.0f);
        addMesh(cubeC, cubeMesh, wallMat);

        // A tall column as height reference
        auto column = addEntity("Column", 0.0f, 2.5f, -5.0f, 0.0f, 0.0f, 0.0f, 0.5f, 5.0f, 0.5f);
        addMesh(column, cubeMesh, wallMat);

        // Point light
        auto ptLight = addEntity("Point Light", 2.0f, 3.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.15f, 0.15f, 0.15f);
        addMesh(ptLight, lightMesh, wallMat);
        addLight(ptLight, ECS::LightComponent::LightType::Point, 1.0f, 0.9f, 0.8f, 1.5f, 15.0f, 0.0f);

        level->setEditorCameraPosition(Vec3{ 5.0f, 5.0f, 12.0f });
        level->setEditorCameraRotation(Vec2{ -20.0f, -15.0f });
        level->setHasEditorCamera(true);
        break;
    }
    }

    diagnostics.setActiveLevel(std::move(level));
    diagnostics.setScenePrepared(false);

    // Register in asset registry so the content browser shows the new level
    {
        AssetRegistryEntry entry;
        entry.name = levelName;
        entry.path = levelRelPath;
        entry.type = AssetType::Level;
        AssetManager::Instance().registerAssetInRegistry(entry);
    }

    // Refresh UI
    m_outlinerSelectedEntity = 0;
    refreshWorldOutliner();
    refreshContentBrowser();

    const char* templateNames[] = { "Empty", "Basic Outdoor", "Prototype" };
    showToastMessage("Created level '" + levelName + "' (" + templateNames[static_cast<int>(tmpl)] + ") â€“ unsaved", 3.0f);
    logger.log(Logger::Category::UI, "Created new level: " + levelName + " with template " + templateNames[static_cast<int>(tmpl)], Logger::LogLevel::INFO);
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
    case AssetType::Widget:   return "widget.png";
    case AssetType::Skybox:   return "skybox.png";
    case AssetType::Level:    return "level.png";
    case AssetType::Prefab:   return "entity.png";
    default:                  return "entity.png";
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
    case AssetType::Prefab:   return Vec4{ 0.30f, 0.90f, 0.70f, 1.0f }; // teal
    default:                  return Vec4{ 0.85f, 0.85f, 0.85f, 1.0f }; // light grey
    }
}

// Returns the icon filename for an entity based on its most prominent component.
static const char* iconForEntity(ECS::Entity entity)
{
    auto& ecs = ECS::ECSManager::Instance();
    if (ecs.hasComponent<ECS::LightComponent>(entity))       return "light.png";
    if (ecs.hasComponent<ECS::CameraComponent>(entity))      return "camera.png";
    if (ecs.hasComponent<ECS::MeshComponent>(entity))        return "model3d.png";
    if (ecs.hasComponent<ECS::ScriptComponent>(entity))      return "script.png";
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity)) return "level.png";
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))     return "entity.png";
    return "entity.png";
}

// Returns a tint color for an entity based on its most prominent component.
static Vec4 iconTintForEntity(ECS::Entity entity)
{
    auto& ecs = ECS::ECSManager::Instance();
    if (ecs.hasComponent<ECS::LightComponent>(entity))       return Vec4{ 1.00f, 0.90f, 0.30f, 1.0f }; // yellow
    if (ecs.hasComponent<ECS::CameraComponent>(entity))      return Vec4{ 0.40f, 0.85f, 0.40f, 1.0f }; // green
    if (ecs.hasComponent<ECS::MeshComponent>(entity))        return Vec4{ 0.50f, 0.80f, 0.90f, 1.0f }; // cyan
    if (ecs.hasComponent<ECS::ScriptComponent>(entity))      return Vec4{ 0.40f, 0.90f, 0.40f, 1.0f }; // green
    if (ecs.hasComponent<ECS::HeightFieldComponent>(entity)) return Vec4{ 0.65f, 0.85f, 0.45f, 1.0f }; // lime
    if (ecs.hasComponent<ECS::PhysicsComponent>(entity))     return Vec4{ 0.75f, 0.50f, 1.00f, 1.0f }; // purple
    return Vec4{ 0.85f, 0.85f, 0.85f, 1.0f }; // light grey
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

    // Icon child (left side, square Ã¢â‚¬â€ pixel-sized so it stays 1:1 regardless of button width)
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

// Resolve the absolute source image path for a texture asset so it can be used as a thumbnail.
// Returns an empty string if the asset is not loaded or has no source path.
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

// Build a grid tile for the Content Browser grid view.
// A tile is a small Button with an icon on top and a label below.
// If thumbnailTextureId is non-zero, it is used as the icon texture directly (e.g. for texture asset thumbnails).
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

    // Icon / thumbnail (top portion)
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

    // Label (bottom portion)
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

// ---------------------------------------------------------------------------
// buildReferencedAssetSet – fast ECS-based scan
// ---------------------------------------------------------------------------
std::unordered_set<std::string> UIManager::buildReferencedAssetSet() const
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
        schema.require<ECS::ScriptComponent>();
        for (const auto e : ecs.getEntitiesMatchingSchema(schema))
        {
            const auto* sc = ecs.getComponent<ECS::ScriptComponent>(e);
            if (sc && !sc->scriptPath.empty())
                refs.insert(sc->scriptPath);
        }
    }
    return refs;
}

void UIManager::populateContentBrowserWidget(const std::shared_ptr<EditorWidget>& widget)
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
                    row.style.color = EditorTheme::Get().treeRowSelected;
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
            rootRow.style.color = EditorTheme::Get().treeRowSelected;
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
                    refreshContentBrowser();
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

            struct LevelTemplate { std::string label; SceneTemplate tmpl; };
            const LevelTemplate templates[] = {
                { "Empty",         SceneTemplate::Empty },
                { "Basic Outdoor", SceneTemplate::BasicOutdoor },
                { "Prototype",     SceneTemplate::Prototype },
            };
            for (const auto& t : templates)
            {
                newLevelDropdown.addItem(t.label, [this, tmpl = t.tmpl, label = t.label]() {
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
                    createNewLevelWithTemplate(tmpl, levelName);
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
                        refreshContentBrowser();
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

        // "Refs" button – Find References (enabled only when a grid asset is selected)
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement refsBtn = EditorUIBuilder::makeButton(
                "ContentBrowser.PathBar.Refs", "Refs", {}, EditorTheme::Scaled(Vec2{ 42.0f, theme.rowHeightSmall }));
            refsBtn.fillX = false;
            refsBtn.tooltipText = "Find References – who uses this asset?";
            if (!m_selectedGridAsset.empty())
            {
                refsBtn.style.color = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.5f };
                refsBtn.style.hoverColor = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.7f };
                refsBtn.style.textColor = theme.textPrimary;
                refsBtn.onClicked = [this]()
                {
                    if (m_selectedGridAsset.empty()) return;
                    const auto refs = AssetManager::Instance().findReferencesTo(m_selectedGridAsset);
                    std::string msg = "References to:\n  " + m_selectedGridAsset + "\n\n";
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
                    showModalMessage(msg);
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

        // "Deps" button – Show Dependencies (enabled only when a grid asset is selected)
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement depsBtn = EditorUIBuilder::makeButton(
                "ContentBrowser.PathBar.Deps", "Deps", {}, EditorTheme::Scaled(Vec2{ 46.0f, theme.rowHeightSmall }));
            depsBtn.fillX = false;
            depsBtn.tooltipText = "Show Dependencies – what does this asset use?";
            if (!m_selectedGridAsset.empty())
            {
                depsBtn.style.color = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.5f };
                depsBtn.style.hoverColor = Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.7f };
                depsBtn.style.textColor = theme.textPrimary;
                depsBtn.onClicked = [this]()
                {
                    if (m_selectedGridAsset.empty()) return;
                    const auto deps = AssetManager::Instance().getAssetDependencies(m_selectedGridAsset);
                    std::string msg = "Dependencies of:\n  " + m_selectedGridAsset + "\n\n";
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
                    showModalMessage(msg);
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
                refreshContentBrowser();
            };
            pathBar->children.push_back(std::move(crumbBtn));
        }

        // â”€â”€ "+ Entity" template dropdown â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
            addBtn.onClicked = [this]()
            {
                auto* renderer = getRenderer();
                if (!renderer) return;
                Vec3 spawnPos{ 0.0f, 0.0f, 0.0f };
                const Vec3 camPos = renderer->getCameraPosition();
                const Vec2 camRot = renderer->getCameraRotationDegrees();
                const float yaw = camRot.x * 3.14159265f / 180.0f;
                const float pitch = camRot.y * 3.14159265f / 180.0f;
                spawnPos.x = camPos.x + cosf(yaw) * cosf(pitch) * 5.0f;
                spawnPos.y = camPos.y + sinf(pitch) * 5.0f;
                spawnPos.z = camPos.z + sinf(yaw) * cosf(pitch) * 5.0f;

                std::vector<DropdownMenuItem> items;
                const char* templates[] = {
                    "Empty Entity", "Point Light", "Directional Light",
                    "Camera", "Static Mesh", "Physics Object", "Particle Emitter"
                };
                for (const char* t : templates)
                {
                    const std::string tmplName = t;
                    const Vec3 pos = spawnPos;
                    items.push_back({ tmplName, [this, tmplName, pos]()
                    {
                        spawnBuiltinTemplate(tmplName, pos);
                    }});
                }
                // Compute dropdown anchor from the button's layout
                const auto* elem = findElementById("ContentBrowser.AddEntity");
                Vec2 anchor = m_mousePosition;
                if (elem)
                {
                    anchor.x = elem->computedPositionPixels.x;
                    anchor.y = elem->computedPositionPixels.y + elem->computedSizePixels.y;
                }
                showDropdownMenu(anchor, items, EditorTheme::Scaled(130.0f));
            };
            pathBar->children.push_back(std::move(addBtn));
        }

        // â”€â”€ Spacer to push search to the right â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement spacer{};
            spacer.type        = WidgetElementType::Panel;
            spacer.fillX       = true;
            spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            spacer.runtimeOnly = true;
            pathBar->children.push_back(std::move(spacer));
        }

        // â”€â”€ Type filter buttons â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            const auto& theme = EditorTheme::Get();
            struct FilterDef { const char* label; AssetType type; };
            const FilterDef filters[] = {
                { "Mesh",     AssetType::Model3D  },
                { "Mat",      AssetType::Material },
                { "Tex",      AssetType::Texture  },
                { "Script",   AssetType::Script   },
                { "Audio",    AssetType::Audio    },
                { "Level",    AssetType::Level    },
                { "Widget",   AssetType::Widget   },
                { "Prefab",   AssetType::Prefab   },
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
                    refreshContentBrowser();
                };
                pathBar->children.push_back(std::move(btn));
            }
        }

        // â”€â”€ Search entry bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement search = EditorUIBuilder::makeEntryBar(
                "ContentBrowser.Search", m_browserSearchText,
                [this](const std::string& text)
                {
                    m_browserSearchText = text;
                    refreshContentBrowser();
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
            // â”€â”€ Search mode: flat list of ALL matching assets across all folders â”€â”€
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
                    item.type != AssetType::Unknown)
                {
                    WidgetElement badge{};
                    badge.id = tile.id + ".Unref";
                    badge.type = WidgetElementType::Text;
                    badge.text = "\xe2\x97\x8f";  // Unicode bullet
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

                tile.onClicked = [uiMgr = this, relPath]()
                {
                    uiMgr->m_selectedGridAsset = relPath;
                    uiMgr->refreshContentBrowser();
                };

                tile.onDoubleClicked = [uiMgr = this, relPath, assetType = item.type]()
                {
                    // Navigate to the asset's folder and clear search
                    const std::string folder = std::filesystem::path(relPath).parent_path().generic_string();
                    uiMgr->m_browserSearchText.clear();
                    uiMgr->m_selectedBrowserFolder = folder;
                    uiMgr->m_selectedGridAsset = relPath;
                    if (!folder.empty() && !uiMgr->m_expandedFolders.count(folder))
                        uiMgr->m_expandedFolders.insert(folder);

                    // Also open the asset if it has a dedicated editor
                    if (assetType == AssetType::Model3D && uiMgr->getRenderer())
                    {
                        uiMgr->getRenderer()->openMeshViewer(relPath);
                    }
                    else if (assetType == AssetType::Texture && uiMgr->getRenderer())
                    {
                        uiMgr->getRenderer()->openTextureViewer(relPath);
                    }
                    else if (assetType == AssetType::Widget)
                    {
                        uiMgr->openWidgetEditorPopup(relPath);
                    }
                    else if (assetType == AssetType::Material)
                    {
                        if (uiMgr->getRenderer())
                            uiMgr->getRenderer()->openMaterialEditorTab(relPath);
                    }
                    else if (assetType == AssetType::Prefab)
                    {
                        uiMgr->spawnPrefabAtPosition(relPath, Vec3{ 0.0f, 0.0f, 0.0f });
                    }
                    else if (assetType == AssetType::Audio)
                    {
                        uiMgr->openAudioPreviewTab(relPath);
                    }
                    else if (assetType == AssetType::Level && uiMgr->m_onLevelLoadRequested)
                    {
                        uiMgr->m_onLevelLoadRequested(relPath);
                    }
                    uiMgr->refreshContentBrowser();
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
                item.type != AssetType::Unknown)
            {
                WidgetElement badge{};
                badge.id = tile.id + ".Unref";
                badge.type = WidgetElementType::Text;
                badge.text = "\xe2\x97\x8f";  // Unicode bullet
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

            // Inline rename: replace the label child with an EntryBar
            if (m_renamingGridAsset && relPath == m_renameOriginalPath && tile.children.size() >= 2)
            {
                // Extract current name without extension as default value
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
                if (assetType == AssetType::Texture && uiMgr->getRenderer())
                {
                    uiMgr->getRenderer()->openTextureViewer(relPath);
                    return;
                }
                if (assetType == AssetType::Widget)
                {
                    uiMgr->openWidgetEditorPopup(relPath);
                    return;
                }
                if (assetType == AssetType::Material)
                {
                    if (uiMgr->getRenderer())
                        uiMgr->getRenderer()->openMaterialEditorTab(relPath);
                    return;
                }
                if (assetType == AssetType::Prefab)
                {
                    uiMgr->spawnPrefabAtPosition(relPath, Vec3{ 0.0f, 0.0f, 0.0f });
                    return;
                }
                if (assetType == AssetType::Audio)
                {
                    uiMgr->openAudioPreviewTab(relPath);
                    return;
                }
                if (assetType == AssetType::Level)
                {
                    if (uiMgr->m_onLevelLoadRequested)
                    {
                        uiMgr->m_onLevelLoadRequested(relPath);
                    }
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
#endif // ENGINE_EDITOR

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
    // Apply deferred theme color updates before layout calculation
#if ENGINE_EDITOR
    applyPendingThemeUpdate();
#endif // ENGINE_EDITOR

    bool anyDirty = false;
    for (const auto& entry : m_widgets)
    {
        if (entry.widget && entry.widget->isLayoutDirty())
        {
            anyDirty = true;
            break;
        }
    }

#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

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

    const auto getDockSide = [](const EditorWidget& widget) -> DockSide
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
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

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

#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

    // Cancel any active drag on fresh mouse down
    if (m_dragging)
    {
        cancelDrag();
    }

    WidgetElement* target = hitTest(screenPos, true);

#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

    // Check if this element is draggable Ã¢â‚¬â€ set up pending drag
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
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR
    const std::string separatorPrefix = "Separator.Toggle.";
    if (target->id.rfind(separatorPrefix, 0) == 0)
    {
        const std::string separatorId = target->id.substr(separatorPrefix.size());
        const std::string contentId = "Separator.Content." + separatorId;
        // UTF-8: Ã¢â€“Â¾ = \xe2\x96\xbe, Ã¢â€“Â¸ = \xe2\x96\xb8
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
        target->style.color = hsvToRgb(hue, 1.0f, value);
        if (target->onColorChanged)
        {
            target->onColorChanged(target->style.color);
        }
    }
    if (target->type == WidgetElementType::Slider)
    {
        const float relX = (screenPos.x - target->computedPositionPixels.x) / std::max(1.0f, target->computedSizePixels.x);
        const float t = std::clamp(relX, 0.0f, 1.0f);
        const float newVal = target->minValue + t * (target->maxValue - target->minValue);
        target->valueFloat = newVal;
        if (target->onValueChanged)
        {
            target->onValueChanged(std::to_string(newVal));
        }
        m_sliderDragElementId = target->id;
        markAllWidgetsDirty();
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
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR
    // Copy target data before callbacks Ã¢â‚¬â€ onClicked/onDoubleClicked may rebuild
    // the widget tree (e.g. refreshContentBrowser), invalidating the target pointer.
    const std::string targetId = target->id;
    const std::string targetClickEvent = target->clickEvent;
    const auto targetOnClicked = target->onClicked;
    const auto targetOnDoubleClicked = target->onDoubleClicked;
    const bool targetIsDraggable = target->isDraggable;

    // Suppress click for draggable elements Ã¢â‚¬â€ click will fire on mouse-up if no drag occurred
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
#if ENGINE_EDITOR
    // Key capture callback (e.g. shortcut rebinding)
    if (m_keyCaptureCallback)
    {
        const uint16_t mods = static_cast<uint16_t>(SDL_GetModState());
        if (m_keyCaptureCallback(static_cast<uint32_t>(key), mods))
            return true;
    }
#endif // ENGINE_EDITOR

    // ── Escape: close dropdowns → modals → unfocus entry → cancel rename ──
    if (key == SDLK_ESCAPE)
    {
#if ENGINE_EDITOR
        if (m_dropdownVisible)
        {
            closeDropdownMenu();
            return true;
        }
#endif // ENGINE_EDITOR
        if (m_modalVisible)
        {
            closeModalMessage();
            return true;
        }
        if (m_focusedEntry)
        {
            setFocusedEntry(nullptr);
#if ENGINE_EDITOR
            if (m_renamingGridAsset)
            {
                m_renamingGridAsset = false;
                m_renameOriginalPath.clear();
                refreshContentBrowser();
            }
#endif // ENGINE_EDITOR
            return true;
        }
        return false;
    }

    // ── Tab / Shift+Tab: cycle focus through EntryBar elements ────────────
    if (key == SDLK_TAB)
    {
        const bool reverse = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        cycleFocusedEntry(reverse);
        return true;
    }

#if ENGINE_EDITOR
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

    // ── Arrow keys: navigate Outliner entity list ─────────────────────────
    if ((key == SDLK_UP || key == SDLK_DOWN) && !m_focusedEntry)
    {
        // Only navigate if Outliner is the relevant context (entity is selected or exists)
        if (m_outlinerSelectedEntity != 0 || m_outlinerLevel)
        {
            navigateOutlinerByArrow(key == SDLK_UP ? -1 : 1);
            return true;
        }
    }

    // ── Arrow keys: navigate Content Browser grid ─────────────────────────
    if (!m_focusedEntry && !m_selectedGridAsset.empty())
    {
        int dCol = 0, dRow = 0;
        if (key == SDLK_LEFT)  dCol = -1;
        if (key == SDLK_RIGHT) dCol =  1;
        if (key == SDLK_UP)    dRow = -1;
        if (key == SDLK_DOWN)  dRow =  1;
        if (dCol != 0 || dRow != 0)
        {
            navigateContentBrowserByArrow(dCol, dRow);
            return true;
        }
    }
#endif // ENGINE_EDITOR

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
                target->scrollbarActivityTimer = 0.0f;
                target->scrollbarOpacity = 1.0f;
                entryPtr->widget->markLayoutDirty();
                m_renderDirty = true;
                return true;
            }
        }
    }

#if ENGINE_EDITOR
    // Widget editor canvas zoom
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
#endif // ENGINE_EDITOR

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

#if ENGINE_EDITOR
    // Update widget editor hover preview
    updateWidgetEditorHover(screenPos);
#endif // ENGINE_EDITOR
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

    // End slider drag
    if (!m_sliderDragElementId.empty())
    {
        m_sliderDragElementId.clear();
    }

#if ENGINE_EDITOR
    // End laptop-mode left-click panning
    if (auto* weState = getActiveWidgetEditorState())
    {
        if (weState->isPanning)
        {
            weState->isPanning = false;
            return true;
        }
    }
#endif // ENGINE_EDITOR

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

    // We are dropping Ã¢â‚¬â€ figure out where
    const std::string payload = m_dragPayload;
    const std::string sourceId = m_dragSourceId;
    cancelDrag();

    Logger::Instance().log(Logger::Category::UI,
        "Drop at (" + std::to_string(screenPos.x) + ", " + std::to_string(screenPos.y) + ") payload=" + payload,
        Logger::LogLevel::INFO);

    // Widget editor: drop a control onto the canvas
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR

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
        // Drop on Outliner entity row Ã¢â€ â€™ apply asset to entity
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

#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR
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

void UIManager::collectFocusableEntries(WidgetElement& element, std::vector<WidgetElement*>& out)
{
    if (element.type == WidgetElementType::EntryBar && !element.id.empty())
    {
        out.push_back(&element);
    }
    for (auto& child : element.children)
    {
        collectFocusableEntries(child, out);
    }
}

void UIManager::cycleFocusedEntry(bool reverse)
{
    // Collect all visible EntryBar elements across active widgets
    std::vector<WidgetElement*> entries;
    for (auto& we : m_widgets)
    {
        if (!we.widget)
            continue;
        // Only include widgets visible for the active tab
        if (!we.tabId.empty() && we.tabId != m_activeTabId)
            continue;
        for (auto& el : we.widget->getElementsMutable())
        {
            collectFocusableEntries(el, entries);
        }
    }
    if (entries.empty())
        return;

    // Find current focused index
    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i)
    {
        if (entries[i] == m_focusedEntry)
        {
            currentIdx = i;
            break;
        }
    }

    int nextIdx;
    if (currentIdx < 0)
    {
        nextIdx = reverse ? static_cast<int>(entries.size()) - 1 : 0;
    }
    else
    {
        nextIdx = reverse ? currentIdx - 1 : currentIdx + 1;
        if (nextIdx < 0) nextIdx = static_cast<int>(entries.size()) - 1;
        if (nextIdx >= static_cast<int>(entries.size())) nextIdx = 0;
    }
    setFocusedEntry(entries[nextIdx]);
}

#if ENGINE_EDITOR
void UIManager::navigateOutlinerByArrow(int direction)
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
        if (entities[i] == m_outlinerSelectedEntity)
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

    m_outlinerSelectedEntity = entities[nextIdx];
    populateOutlinerDetails(m_outlinerSelectedEntity);
}

void UIManager::navigateContentBrowserByArrow(int dCol, int dRow)
{
    // Collect grid tile IDs (relPaths) from active Content Browser widget
    std::vector<std::string> tilePaths;
    const std::string tilePrefix = "ContentBrowser.GridAsset.";
    for (auto& we : m_widgets)
    {
        if (!we.widget)
            continue;
        if (!we.tabId.empty() && we.tabId != m_activeTabId)
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
        refreshContentBrowser();
        return;
    }

    // Estimate columns per row from the grid (default 4, but could vary)
    int cols = 4;
    // Try to find the WrapPanel that holds grid tiles and check its column count
    if (auto* grid = findElementById("ContentBrowser.Grid"))
    {
        if (grid->columns > 0)
            cols = grid->columns;
        else if (grid->computedSizePixels.x > 0.0f)
        {
            // Estimate from tile width (~100px default)
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
        refreshContentBrowser();
    }
}
#endif // ENGINE_EDITOR

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

#if ENGINE_EDITOR
void UIManager::rebuildAllEditorUI()
{
    // Defer the actual color update to the next frame via updateLayouts().
    // This avoids heavy synchronous work inside dropdown callbacks that can
    // freeze or crash the editor.
    m_themeDirty = true;
    markAllWidgetsDirty();
}

void UIManager::rebuildEditorUIForDpi(float newDpi)
{
    m_uiRenderingPaused = true;

    // 1. Apply new DPI to theme (scales fonts, spacing, borderRadius, etc.)
    EditorTheme::Get().applyDpiScale(newDpi);

    // 2. Regenerate widget asset files with the new DPI baked into dimensions.
    //    ensureEditorWidgetsCreated() checks _dpiScale in each file; since the
    //    theme now carries the new scale the comparison will fail and every
    //    editor widget asset is rewritten.
    auto& am = AssetManager::Instance();
    am.ensureEditorWidgetsCreated();

    // 3. Fully reload each editor widget from the regenerated asset files.
    //    This refreshes m_sizePixels AND all element minSize/padding/fontSize
    //    values that were baked with the new DPI.
    struct WidgetFileMapping { const char* widgetId; const char* assetFile; };
    static constexpr WidgetFileMapping kMappings[] = {
        { "TitleBar",        "TitleBar.asset" },
        { "ViewportOverlay", "ViewportOverlay.asset" },
        { "WorldSettings",   "WorldSettings.asset" },
        { "WorldOutliner",   "WorldOutliner.asset" },
        { "EntityDetails",   "EntityDetails.asset" },
        { "StatusBar",       "StatusBar.asset" },
        { "ContentBrowser",  "ContentBrowser.asset" },
    };

    for (const auto& mapping : kMappings)
    {
        auto* entry = findWidgetEntry(mapping.widgetId);
        if (!entry || !entry->widget) continue;

        const std::string path = am.getEditorWidgetPath(mapping.assetFile);
        std::ifstream in(path);
        if (!in.is_open()) continue;

        auto fileJson = nlohmann::json::parse(in, nullptr, false);
        in.close();
        if (fileJson.is_discarded() || !fileJson.is_object() || !fileJson.contains("data"))
            continue;

        // Reload through a temporary Widget so all element pixel values
        // (minSize, padding, fontSize) are refreshed from the new-DPI JSON.
        auto tempWidget = std::make_shared<Widget>();
        if (tempWidget->loadFromJson(fileJson.at("data")))
        {
            entry->widget->setSizePixels(tempWidget->getSizePixels());
            entry->widget->setPositionPixels(tempWidget->getPositionPixels());
            entry->widget->setAnchor(tempWidget->getAnchor());
            entry->widget->setFillX(tempWidget->getFillX());
            entry->widget->setFillY(tempWidget->getFillY());
            entry->widget->setElements(tempWidget->getElements());
        }
    }

    // 4. Apply theme colours & DPI-scaled fonts/spacing to all widget elements.
    applyThemeToAllEditorWidgets();

    // 5. Re-populate dynamic widgets with new DPI-scaled values.
    //    Runtime elements (outliner rows, detail rows, content browser tiles)
    //    cached their old DPI-scaled sizes; we need to rebuild them.
    refreshWorldOutliner();
    populateOutlinerDetails(m_outlinerSelectedEntity);
    if (auto* cb = findWidgetEntry("ContentBrowser"))
    {
        if (cb->widget) populateContentBrowserWidget(cb->widget);
    }
    refreshStatusBar();

    markAllWidgetsDirty();

    m_uiRenderingPaused = false;
}

void UIManager::applyThemeToAllEditorWidgets()
{
    // Close any open dropdown â€“ its Panel/Button elements would receive
    // generic panelBackground/buttonDefault colors instead of the
    // dropdown-specific ones.  Next open will pick up the new theme.
    closeDropdownMenu();

    const auto& theme = EditorTheme::Get();
    for (auto& entry : m_widgets)
    {
        if (!entry.widget) continue;
        for (auto& element : entry.widget->getElementsMutable())
            EditorTheme::ApplyThemeToElement(element, theme);
        entry.widget->markLayoutDirty();
    }

    // Also theme transient widgets not in the main list.
    auto applyToTransient = [&](const std::shared_ptr<EditorWidget>& w)
    {
        if (!w) return;
        for (auto& element : w->getElementsMutable())
            EditorTheme::ApplyThemeToElement(element, theme);
        w->markLayoutDirty();
    };
    applyToTransient(m_modalWidget);
    applyToTransient(m_saveProgressWidget);
    for (auto& toast : m_toasts)
        applyToTransient(toast.widget);
}

void UIManager::applyPendingThemeUpdate()
{
    if (!m_themeDirty) return;
    m_themeDirty = false;

    // Apply theme colors to ALL editor widget elements (backgrounds,
    // buttons, text, inputs, etc.) based on their element type and ID.
    applyThemeToAllEditorWidgets();

    markAllWidgetsDirty();
}
#endif // ENGINE_EDITOR

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
        m_modalWidget = std::make_shared<EditorWidget>();
        m_modalWidget->setName("ModalMessage");
        m_modalWidget->setAnchor(WidgetAnchor::TopLeft);
        m_modalWidget->setFillX(true);
        m_modalWidget->setFillY(true);
        m_modalWidget->setZOrder(10000);
    }

    const auto& theme = EditorTheme::Get();

    WidgetElement overlay{};
    overlay.id = "Modal.Overlay";
    overlay.type = WidgetElementType::Panel;
    overlay.from = Vec2{ 0.0f, 0.0f };
    overlay.to = Vec2{ 1.0f, 1.0f };
    overlay.style.color = theme.modalOverlay;
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "Modal.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.3f, 0.35f };
    panel.to = Vec2{ 0.7f, 0.65f };
    panel.padding = Vec2{ 20.0f, 16.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = theme.modalBackground;
    panel.elevation = 3;
    panel.style.applyElevation(3, theme.shadowColor, theme.shadowOffset);
    panel.style.borderRadius = theme.borderRadius;
    panel.hitTestMode = HitTestMode::DisabledSelf;
    panel.runtimeOnly = true;

    WidgetElement message{};
    message.id = "Modal.Text";
    message.type = WidgetElementType::Text;
    message.text = m_modalMessage;
    message.font = theme.fontDefault;
    message.fontSize = theme.fontSizeHeading + 2.0f;
    message.style.textColor = theme.modalText;
    message.wrapText = true;
    message.fillX = true;
    message.fillY = true;
    message.minSize = Vec2{ 0.0f, 28.0f };
    message.runtimeOnly = true;

    WidgetElement closeButton{};
    closeButton.id = "Modal.Close";
    closeButton.type = WidgetElementType::Button;
    closeButton.text = "Close";
    closeButton.font = theme.fontDefault;
    closeButton.fontSize = theme.fontSizeBody;
    closeButton.textAlignH = TextAlignH::Center;
    closeButton.textAlignV = TextAlignV::Center;
    closeButton.padding = theme.paddingNormal;
    closeButton.minSize = Vec2{ 0.0f, 32.0f };
    closeButton.style.color = theme.buttonDefault;
    closeButton.style.hoverColor = theme.buttonHover;
    closeButton.style.textColor = theme.buttonText;
    closeButton.style.borderRadius = theme.borderRadius;
    closeButton.style.transitionDuration = theme.hoverTransitionSpeed;
    closeButton.shaderVertex = "button_vertex.glsl";
    closeButton.shaderFragment = "button_fragment.glsl";
    closeButton.hitTestMode = HitTestMode::Enabled;
    closeButton.fillX = true;
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
    buttonRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    buttonRow.minSize = Vec2{ 0.0f, 32.0f };
    buttonRow.orientation = StackOrientation::Horizontal;
    buttonRow.padding = Vec2{ 8.0f, 0.0f };
    buttonRow.runtimeOnly = true;

    WidgetElement spacerLeft{};
    spacerLeft.id = "Modal.ButtonSpacerLeft";
    spacerLeft.type = WidgetElementType::Panel;
    spacerLeft.fillX = true;
    spacerLeft.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    spacerLeft.runtimeOnly = true;

    WidgetElement spacerRight{};
    spacerRight.id = "Modal.ButtonSpacerRight";
    spacerRight.type = WidgetElementType::Panel;
    spacerRight.fillX = true;
    spacerRight.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
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

std::shared_ptr<EditorWidget> UIManager::createToastWidget(const std::string& message, const std::string& name, NotificationLevel level) const
{
    auto widget = std::make_shared<EditorWidget>();
    widget->setName(name);
    widget->setAnchor(WidgetAnchor::BottomRight);
    widget->setPositionPixels(Vec2{ 20.0f, 20.0f });
    widget->setSizePixels(Vec2{ 320.0f, 70.0f });
    widget->setZOrder(9000);

    const auto& theme = EditorTheme::Get();

    // Priority-based left accent color
    Vec4 accentColor = theme.toastText;
    switch (level)
    {
    case NotificationLevel::Error:   accentColor = theme.errorColor;   break;
    case NotificationLevel::Warning: accentColor = theme.warningColor; break;
    case NotificationLevel::Success: accentColor = theme.successColor; break;
    case NotificationLevel::Info:    accentColor = Vec4{ 0.4f, 0.6f, 0.9f, 1.0f }; break;
    }

    WidgetElement panel{};
    panel.id = name + ".Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.0f, 0.0f };
    panel.to = Vec2{ 1.0f, 1.0f };
    panel.padding = Vec2{ 12.0f, 10.0f };
    panel.orientation = StackOrientation::Horizontal;
    panel.style.color = theme.toastBackground;
    panel.elevation = 3;
    panel.style.applyElevation(3, theme.shadowColor, theme.shadowOffset);
    panel.style.borderRadius = theme.borderRadius;
    panel.hitTestMode = HitTestMode::DisabledSelf;
    panel.runtimeOnly = true;

    // Accent bar on the left side
    WidgetElement accent{};
    accent.id = name + ".Accent";
    accent.type = WidgetElementType::Panel;
    accent.minSize = Vec2{ 4.0f, 0.0f };
    accent.fillY = true;
    accent.style.color = accentColor;
    accent.style.borderRadius = 2.0f;
    accent.runtimeOnly = true;

    WidgetElement messageElement{};
    messageElement.id = name + ".Text";
    messageElement.type = WidgetElementType::Text;
    messageElement.text = message;
    messageElement.font = theme.fontDefault;
    messageElement.fontSize = theme.fontSizeSubheading;
    messageElement.style.textColor = theme.toastText;
    messageElement.fillX = true;
    messageElement.minSize = Vec2{ 0.0f, 22.0f };
    messageElement.padding = Vec2{ 8.0f, 0.0f };
    messageElement.runtimeOnly = true;

    panel.children.clear();
    panel.children.push_back(std::move(accent));
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

    // â”€â”€ Tooltip tracking â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    if (m_lastHoveredElement && !m_lastHoveredElement->tooltipText.empty())
    {
        // Same element still hovered â€” accumulate time (caller must pass dt)
        if (!m_tooltipVisible && m_tooltipText == m_lastHoveredElement->tooltipText)
        {
            // Timer is advanced externally in updateTooltip(dt)
        }
        else if (m_tooltipText != m_lastHoveredElement->tooltipText)
        {
            // Switched to a new tooltip element â€” reset timer
            m_tooltipTimer = 0.0f;
            m_tooltipVisible = false;
            m_tooltipText = m_lastHoveredElement->tooltipText;
        }
        m_tooltipPosition = Vec2{ m_mousePosition.x + EditorTheme::Scaled(16.0f),
                                   m_mousePosition.y + EditorTheme::Scaled(16.0f) };
    }
    else
    {
        // No tooltip element hovered â€” hide immediately
        if (m_tooltipVisible || !m_tooltipText.empty())
        {
            m_tooltipVisible = false;
            m_tooltipText.clear();
            m_tooltipTimer = 0.0f;
            unregisterWidget("_Tooltip");
            m_renderDirty = true;
        }
    }
}

void UIManager::updateHoverTransitionsRecursive(WidgetElement& element, float deltaSeconds)
{
    // Use explicit transitionDuration if set; otherwise fall back to the
    // theme's hoverTransitionSpeed for any hittable (interactive) element.
    const float duration = element.style.transitionDuration > 0.0f
        ? element.style.transitionDuration
        : (element.hitTestMode == HitTestMode::Enabled
            ? EditorTheme::Get().hoverTransitionSpeed
            : 0.0f);
    if (duration > 0.0f)
    {
        const float target = element.isHovered ? 1.0f : 0.0f;
        if (element.hoverTransitionT != target)
        {
            const float speed = deltaSeconds / duration;
            if (element.hoverTransitionT < target)
                element.hoverTransitionT = std::min(target, element.hoverTransitionT + speed);
            else
                element.hoverTransitionT = std::max(target, element.hoverTransitionT - speed);
            m_renderDirty = true;
        }
    }
    else
    {
        element.hoverTransitionT = element.isHovered ? 1.0f : 0.0f;
    }
    for (auto& child : element.children)
        updateHoverTransitionsRecursive(child, deltaSeconds);
}

void UIManager::updateHoverTransitions(float deltaSeconds)
{
    for (auto& entry : m_widgets)
    {
        if (!entry.widget) continue;
        for (auto& el : entry.widget->getElementsMutable())
            updateHoverTransitionsRecursive(el, deltaSeconds);
    }
}

// â”€â”€ Scrollbar auto-hide (Phase 1.6) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void UIManager::updateScrollbarVisibilityRecursive(WidgetElement& element, float deltaSeconds)
{
    if (element.scrollable || element.type == WidgetElementType::ScrollView)
    {
        const auto& theme = EditorTheme::Get();
        const float delay = theme.scrollbarAutoHideDelay;
        const float fadeDuration = 0.3f;

        element.scrollbarActivityTimer += deltaSeconds;

        // Scrollbar hover detection
        bool sbHovered = false;
        if (m_hasMousePosition && element.hasComputedPosition && element.hasComputedSize)
        {
            const float sbWidth = theme.scrollbarWidthHover;  // use hover width for hit area
            const float ex = element.computedPositionPixels.x;
            const float ey = element.computedPositionPixels.y;
            const float ew = element.computedSizePixels.x;
            const float eh = element.computedSizePixels.y;
            const float sbX0 = ex + ew - sbWidth;
            const float sbX1 = ex + ew;
            const float sbY0 = ey;
            const float sbY1 = ey + eh;
            sbHovered = m_mousePosition.x >= sbX0 && m_mousePosition.x <= sbX1 &&
                        m_mousePosition.y >= sbY0 && m_mousePosition.y <= sbY1;
        }
        if (sbHovered != element.scrollbarHovered)
        {
            element.scrollbarHovered = sbHovered;
            m_renderDirty = true;
        }

        if (!theme.scrollbarAutoHide)
        {
            element.scrollbarOpacity = 1.0f;
        }
        else if (element.scrollbarHovered)
        {
            element.scrollbarOpacity = 1.0f;
            element.scrollbarActivityTimer = 0.0f;
        }
        else if (element.scrollbarActivityTimer <= delay)
        {
            element.scrollbarOpacity = 1.0f;
        }
        else
        {
            const float fadeElapsed = element.scrollbarActivityTimer - delay;
            element.scrollbarOpacity = std::max(0.0f, 1.0f - fadeElapsed / fadeDuration);
        }

        if (element.scrollbarOpacity > 0.0f)
            m_renderDirty = true;
    }

    for (auto& child : element.children)
        updateScrollbarVisibilityRecursive(child, deltaSeconds);
}

void UIManager::updateScrollbarVisibility(float deltaSeconds)
{
    for (auto& entry : m_widgets)
    {
        if (!entry.widget) continue;
        for (auto& el : entry.widget->getElementsMutable())
            updateScrollbarVisibilityRecursive(el, deltaSeconds);
    }
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

void UIManager::bindClickEventsForWidget(const std::shared_ptr<EditorWidget>& widget)
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

#if ENGINE_EDITOR
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
            dirtyLabel->style.textColor = EditorTheme::Get().textMuted;
        }
        else
        {
            dirtyLabel->text = std::to_string(count) + " unsaved change" + (count > 1 ? "s" : "");
            dirtyLabel->style.textColor = EditorTheme::Get().warningColor;
        }
    }

    WidgetElement* undoBtn = FindElementById(elements, "StatusBar.Undo");
    if (undoBtn)
    {
        auto& undo = UndoRedoManager::Instance();
        const bool canUndo = undo.canUndo();
        undoBtn->style.textColor = canUndo
            ? EditorTheme::Get().textPrimary
            : EditorTheme::Get().textMuted;
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
        redoBtn->style.textColor = canRedo
            ? EditorTheme::Get().textPrimary
            : EditorTheme::Get().textMuted;
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
        m_saveProgressWidget = std::make_shared<EditorWidget>();
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
    overlay.style.color = EditorTheme::Get().modalOverlay;
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "SaveProgress.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.3f, 0.38f };
    panel.to = Vec2{ 0.7f, 0.62f };
    panel.padding = Vec2{ 20.0f, 14.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = EditorTheme::Get().modalBackground;
    panel.runtimeOnly = true;

    WidgetElement title{};
    title.id = "SaveProgress.Title";
    title.type = WidgetElementType::Text;
    title.text = "Saving...";
    title.font = EditorTheme::Get().fontDefault;
    title.fontSize = EditorTheme::Get().fontSizeHeading;
    title.textAlignH = TextAlignH::Center;
    title.style.textColor = EditorTheme::Get().textPrimary;
    title.fillX = true;
    title.minSize = Vec2{ 0.0f, 24.0f };
    title.runtimeOnly = true;

    WidgetElement counter{};
    counter.id = "SaveProgress.Counter";
    counter.type = WidgetElementType::Text;
    counter.text = "0 / " + std::to_string(total);
    counter.font = EditorTheme::Get().fontDefault;
    counter.fontSize = EditorTheme::Get().fontSizeSubheading;
    counter.textAlignH = TextAlignH::Center;
    counter.style.textColor = EditorTheme::Get().textSecondary;
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
    progress.style.color = EditorTheme::Get().sliderTrack;
    progress.style.fillColor = Vec4{ EditorTheme::Get().successColor.x, EditorTheme::Get().successColor.y, EditorTheme::Get().successColor.z, 0.95f };
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
        showToastMessage("All assets saved successfully.", 3.0f, NotificationLevel::Success);
    }
    else
    {
        showToastMessage("Some assets failed to save.", 4.0f, NotificationLevel::Error);
    }

    refreshStatusBar();
}

void UIManager::showUnsavedChangesDialog(std::function<void()> onDone)
{
    auto& assetMgr = AssetManager::Instance();
    const auto unsavedList = assetMgr.getUnsavedAssetList();

    // If nothing unsaved, proceed immediately
    if (unsavedList.empty())
    {
        if (onDone) onDone();
        return;
    }

    if (!m_renderer) { if (onDone) onDone(); return; }

    const float kBaseW = 500.0f;
    const float kBaseH = 420.0f;
    const int kPopupW = static_cast<int>(EditorTheme::Scaled(kBaseW));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(kBaseH));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "UnsavedChanges", "Unsaved Changes", kPopupW, kPopupH);
    if (!popup) { if (onDone) onDone(); return; }
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    // Shared state: which items are selected for saving
    struct DialogState
    {
        std::vector<AssetManager::UnsavedAssetInfo> items;
        std::vector<bool> selected;
    };
    auto state = std::make_shared<DialogState>();
    state->items = unsavedList;
    state->selected.resize(unsavedList.size(), true); // all selected by default

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type = WidgetElementType::Panel;
        bg.id = "UC.Bg";
        bg.from = Vec2{ 0.0f, 0.0f };
        bg.to = Vec2{ 1.0f, 1.0f };
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(bg);
    }

    // Title
    {
        WidgetElement title;
        title.type = WidgetElementType::Text;
        title.id = "UC.Title";
        title.from = Vec2{ nx(12.0f), 0.0f };
        title.to = Vec2{ 1.0f, ny(40.0f) };
        title.text = "Save Changes?";
        title.fontSize = EditorTheme::Get().fontSizeHeading;
        title.style.textColor = EditorTheme::Get().titleBarText;
        title.textAlignV = TextAlignV::Center;
        title.padding = Vec2{ 6.0f, 0.0f };
        elements.push_back(title);
    }

    // Subtitle
    {
        WidgetElement sub;
        sub.type = WidgetElementType::Text;
        sub.id = "UC.Sub";
        sub.from = Vec2{ nx(12.0f), ny(40.0f) };
        sub.to = Vec2{ 1.0f, ny(62.0f) };
        sub.text = "The following assets have unsaved changes:";
        sub.fontSize = EditorTheme::Get().fontSizeBody;
        sub.style.textColor = EditorTheme::Get().textSecondary;
        sub.textAlignV = TextAlignV::Center;
        sub.padding = Vec2{ 6.0f, 0.0f };
        elements.push_back(sub);
    }

    // Scrollable list of checkboxes
    WidgetElement listStack;
    listStack.type = WidgetElementType::StackPanel;
    listStack.id = "UC.List";
    listStack.from = Vec2{ nx(12.0f), ny(66.0f) };
    listStack.to = Vec2{ nx(W - 12.0f), ny(H - 56.0f) };
    listStack.padding = Vec2{ 4.0f, 4.0f };
    listStack.scrollable = true;
    listStack.style.color = EditorTheme::Get().panelBackgroundAlt;

    for (size_t i = 0; i < unsavedList.size(); ++i)
    {
        const auto& item = unsavedList[i];
        std::string displayName = item.name;
        if (displayName.empty()) displayName = item.path;
        if (item.isLevel) displayName = "[Level] " + displayName;

        WidgetElement row;
        row.type = WidgetElementType::CheckBox;
        row.id = "UC.Check." + std::to_string(i);
        row.text = displayName;
        row.isChecked = true;
        row.font = EditorTheme::Get().fontDefault;
        row.fontSize = EditorTheme::Get().fontSizeBody;
        row.style.color = EditorTheme::Get().checkboxDefault;
        row.style.hoverColor = EditorTheme::Get().checkboxHover;
        row.style.fillColor = EditorTheme::Get().checkboxChecked;
        row.style.textColor = EditorTheme::Get().checkboxText;
        row.fillX = true;
        row.minSize = Vec2{ 0.0f, EditorTheme::Scaled(26.0f) };
        row.padding = EditorTheme::Get().paddingSmall;
        row.hitTestMode = HitTestMode::Enabled;
        row.runtimeOnly = true;
        row.onCheckedChanged = [state, i](bool checked) { state->selected[i] = checked; };

        listStack.children.push_back(std::move(row));
    }
    elements.push_back(std::move(listStack));

    Renderer* renderer = m_renderer;

    // Save & Continue button
    {
        WidgetElement saveBtn;
        saveBtn.type = WidgetElementType::Button;
        saveBtn.id = "UC.Save";
        saveBtn.from = Vec2{ nx(W - 310.0f), ny(H - 46.0f) };
        saveBtn.to = Vec2{ nx(W - 196.0f), ny(H - 14.0f) };
        saveBtn.text = "Save Selected";
        saveBtn.fontSize = EditorTheme::Get().fontSizeBody;
        saveBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        saveBtn.textAlignH = TextAlignH::Center;
        saveBtn.textAlignV = TextAlignV::Center;
        saveBtn.style.color = Vec4{ 0.15f, 0.45f, 0.25f, 0.95f };
        saveBtn.style.hoverColor = Vec4{ 0.2f, 0.6f, 0.35f, 1.0f };
        saveBtn.shaderVertex = "button_vertex.glsl";
        saveBtn.shaderFragment = "button_fragment.glsl";
        saveBtn.hitTestMode = HitTestMode::Enabled;
        saveBtn.onClicked = [state, popup, renderer, onDone]()
        {
            // Collect selected IDs
            std::vector<unsigned int> ids;
            bool includeLevel = false;
            for (size_t i = 0; i < state->items.size(); ++i)
            {
                if (!state->selected[i]) continue;
                if (state->items[i].isLevel)
                    includeLevel = true;
                else
                    ids.push_back(state->items[i].id);
            }

            if (ids.empty() && !includeLevel)
            {
                popup->close();
                if (onDone) onDone();
                return;
            }

            auto& uiMgr = renderer->getUIManager();
            const size_t total = ids.size() + (includeLevel ? 1 : 0);
            uiMgr.showSaveProgressModal(total);

            AssetManager::Instance().saveSelectedAssetsAsync(ids, includeLevel,
                [renderer](size_t saved, size_t total)
                {
                    renderer->getUIManager().updateSaveProgress(saved, total);
                },
                [renderer, popup, onDone](bool success)
                {
                    UndoRedoManager::Instance().clear();
                    renderer->getUIManager().closeSaveProgressModal(success);
                    popup->close();
                    if (onDone) onDone();
                });
        };
        elements.push_back(std::move(saveBtn));
    }

    // Don't Save button
    {
        WidgetElement skipBtn;
        skipBtn.type = WidgetElementType::Button;
        skipBtn.id = "UC.Skip";
        skipBtn.from = Vec2{ nx(W - 190.0f), ny(H - 46.0f) };
        skipBtn.to = Vec2{ nx(W - 100.0f), ny(H - 14.0f) };
        skipBtn.text = "Don't Save";
        skipBtn.fontSize = EditorTheme::Get().fontSizeBody;
        skipBtn.style.textColor = Vec4{ 0.9f, 0.7f, 0.7f, 1.0f };
        skipBtn.textAlignH = TextAlignH::Center;
        skipBtn.textAlignV = TextAlignV::Center;
        skipBtn.style.color = Vec4{ 0.35f, 0.15f, 0.15f, 0.9f };
        skipBtn.style.hoverColor = Vec4{ 0.5f, 0.2f, 0.2f, 1.0f };
        skipBtn.shaderVertex = "button_vertex.glsl";
        skipBtn.shaderFragment = "button_fragment.glsl";
        skipBtn.hitTestMode = HitTestMode::Enabled;
        skipBtn.onClicked = [popup, onDone]()
        {
            popup->close();
            if (onDone) onDone();
        };
        elements.push_back(std::move(skipBtn));
    }

    // Cancel button
    {
        WidgetElement cancelBtn;
        cancelBtn.type = WidgetElementType::Button;
        cancelBtn.id = "UC.Cancel";
        cancelBtn.from = Vec2{ nx(W - 92.0f), ny(H - 46.0f) };
        cancelBtn.to = Vec2{ nx(W - 12.0f), ny(H - 14.0f) };
        cancelBtn.text = "Cancel";
        cancelBtn.fontSize = EditorTheme::Get().fontSizeBody;
        cancelBtn.style.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
        cancelBtn.textAlignH = TextAlignH::Center;
        cancelBtn.textAlignV = TextAlignV::Center;
        cancelBtn.style.color = Vec4{ 0.25f, 0.25f, 0.3f, 0.95f };
        cancelBtn.style.hoverColor = Vec4{ 0.35f, 0.35f, 0.42f, 1.0f };
        cancelBtn.shaderVertex = "button_vertex.glsl";
        cancelBtn.shaderFragment = "button_fragment.glsl";
        cancelBtn.hitTestMode = HitTestMode::Enabled;
        cancelBtn.onClicked = [popup]() { popup->close(); };
        elements.push_back(std::move(cancelBtn));
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("UnsavedChanges");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("UnsavedChanges", widget);
}

void UIManager::showLevelLoadProgress(const std::string& levelName)
{
    if (!m_levelLoadProgressWidget)
    {
        m_levelLoadProgressWidget = std::make_shared<EditorWidget>();
        m_levelLoadProgressWidget->setName("LevelLoadProgress");
        m_levelLoadProgressWidget->setAnchor(WidgetAnchor::TopLeft);
        m_levelLoadProgressWidget->setFillX(true);
        m_levelLoadProgressWidget->setFillY(true);
        m_levelLoadProgressWidget->setZOrder(10002);
    }

    WidgetElement overlay{};
    overlay.id = "LLP.Overlay";
    overlay.type = WidgetElementType::Panel;
    overlay.from = Vec2{ 0.0f, 0.0f };
    overlay.to = Vec2{ 1.0f, 1.0f };
    overlay.style.color = EditorTheme::Get().modalOverlay;
    overlay.hitTestMode = HitTestMode::Enabled;
    overlay.runtimeOnly = true;

    WidgetElement panel{};
    panel.id = "LLP.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.3f, 0.38f };
    panel.to = Vec2{ 0.7f, 0.62f };
    panel.padding = Vec2{ 20.0f, 14.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = EditorTheme::Get().modalBackground;
    panel.runtimeOnly = true;

    WidgetElement title{};
    title.id = "LLP.Title";
    title.type = WidgetElementType::Text;
    title.text = "Loading Level...";
    title.font = EditorTheme::Get().fontDefault;
    title.fontSize = EditorTheme::Get().fontSizeHeading;
    title.textAlignH = TextAlignH::Center;
    title.style.textColor = EditorTheme::Get().textPrimary;
    title.fillX = true;
    title.minSize = Vec2{ 0.0f, 24.0f };
    title.runtimeOnly = true;

    WidgetElement nameLabel{};
    nameLabel.id = "LLP.Name";
    nameLabel.type = WidgetElementType::Text;
    nameLabel.text = levelName;
    nameLabel.font = EditorTheme::Get().fontDefault;
    nameLabel.fontSize = EditorTheme::Get().fontSizeSubheading;
    nameLabel.textAlignH = TextAlignH::Center;
    nameLabel.style.textColor = EditorTheme::Get().textSecondary;
    nameLabel.fillX = true;
    nameLabel.minSize = Vec2{ 0.0f, 20.0f };
    nameLabel.runtimeOnly = true;

    WidgetElement status{};
    status.id = "LLP.Status";
    status.type = WidgetElementType::Text;
    status.text = "Preparing...";
    status.font = EditorTheme::Get().fontDefault;
    status.fontSize = EditorTheme::Get().fontSizeBody;
    status.textAlignH = TextAlignH::Center;
    status.style.textColor = EditorTheme::Get().textMuted;
    status.fillX = true;
    status.minSize = Vec2{ 0.0f, 18.0f };
    status.runtimeOnly = true;

    panel.children.push_back(std::move(title));
    panel.children.push_back(std::move(nameLabel));
    panel.children.push_back(std::move(status));

    std::vector<WidgetElement> elements;
    elements.push_back(std::move(overlay));
    elements.push_back(std::move(panel));
    m_levelLoadProgressWidget->setElements(std::move(elements));
    m_levelLoadProgressWidget->markLayoutDirty();

    registerWidget("LevelLoadProgress", m_levelLoadProgressWidget);
}

void UIManager::updateLevelLoadProgress(const std::string& statusText)
{
    if (!m_levelLoadProgressWidget) return;

    auto& elements = m_levelLoadProgressWidget->getElementsMutable();
    WidgetElement* statusEl = FindElementById(elements, "LLP.Status");
    if (statusEl)
    {
        statusEl->text = statusText;
    }
    m_levelLoadProgressWidget->markLayoutDirty();
    m_renderDirty = true;
}

void UIManager::closeLevelLoadProgress()
{
    unregisterWidget("LevelLoadProgress");
}

void UIManager::showDropdownMenu(const Vec2& anchorPixels, const std::vector<DropdownMenuItem>& items, float minWidth)
{
    closeDropdownMenu();

    if (items.empty()) return;

    const auto& theme = EditorTheme::Get();
    const float kItemH = EditorTheme::Scaled(28.0f);
    const float kPadY = EditorTheme::Scaled(4.0f);
    const float kDefaultMenuW = EditorTheme::Scaled(180.0f);
    const float menuW = std::max(kDefaultMenuW, minWidth);
    const float menuH = kPadY * 2.0f + static_cast<float>(items.size()) * kItemH;

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("DropdownMenu");
    widget->setSizePixels(Vec2{ menuW, menuH });

    // â”€â”€ Clamp position so the menu stays on screen â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    float posX = anchorPixels.x;
    float posY = anchorPixels.y;
    const float screenW = m_availableViewportSize.x;
    const float screenH = m_availableViewportSize.y;

    // Flip upward if the menu would overflow the bottom
    if (posY + menuH > screenH && posY - menuH >= 0.0f)
    {
        posY = posY - menuH;
    }
    // Still overflows bottom (and can't flip) â€“ clamp to bottom edge
    if (posY + menuH > screenH)
    {
        posY = screenH - menuH;
    }
    // Clamp to top
    if (posY < 0.0f) posY = 0.0f;

    // Shift left if the menu would overflow the right edge
    if (posX + menuW > screenW)
    {
        posX = screenW - menuW;
    }
    if (posX < 0.0f) posX = 0.0f;

    widget->setPositionPixels(Vec2{ posX, posY });
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
    bg.style.color = theme.dropdownBackground;
    bg.elevation = 2;
    bg.style.applyElevation(2, theme.shadowColor, theme.shadowOffset);
    bg.style.borderRadius = theme.borderRadius;
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
            sep.style.color = theme.panelBorder;
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
        item.fontSize      = theme.fontSizeBody;
        item.style.color         = Vec4{ theme.dropdownBackground.x, theme.dropdownBackground.y, theme.dropdownBackground.z, 0.0f };
        item.style.hoverColor    = theme.dropdownHover;
        item.style.textColor     = theme.textPrimary;
        item.textAlignH    = TextAlignH::Left;
        item.textAlignV    = TextAlignV::Center;
        item.padding       = EditorTheme::Scaled(Vec2{ 12.0f, 4.0f });
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
            // Placeholder item with no callback Ã¢â‚¬â€ just close the menu
            item.style.textColor = theme.textMuted;
            item.style.hoverColor = item.style.color;
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
        showToastMessage("Failed to load widget: " + relativeAssetPath, 3.0f, NotificationLevel::Error);
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
        showToastMessage("Failed to create widget from asset: " + relativeAssetPath, 3.0f, NotificationLevel::Error);
        m_renderer->removeTab(tabId);
        return;
    }

    const std::string contentWidgetId = "WidgetEditor.Content." + tabId;
    const std::string leftWidgetId = "WidgetEditor.Left." + tabId;
    const std::string rightWidgetId = "WidgetEditor.Right." + tabId;
    const std::string canvasWidgetId = "WidgetEditor.Canvas." + tabId;
    const std::string toolbarWidgetId = "WidgetEditor.Toolbar." + tabId;
    const std::string bottomWidgetId = "WidgetEditor.Bottom." + tabId;

    unregisterWidget(contentWidgetId);
    unregisterWidget(leftWidgetId);
    unregisterWidget(rightWidgetId);
    unregisterWidget(canvasWidgetId);
    unregisterWidget(toolbarWidgetId);
    unregisterWidget(bottomWidgetId);

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
    state.bottomWidgetId = bottomWidgetId;
    state.assetId = static_cast<unsigned int>(assetId);
    state.isDirty = false;
    m_widgetEditorStates[tabId] = std::move(state);

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
        registerWidget(toolbarWidgetId, toolbarWidget, tabId);

        const std::string capturedTabId = tabId;
        registerClickEvent("WidgetEditor.Toolbar.Save." + tabId, [this, capturedTabId]()
        {
            saveWidgetEditorAsset(capturedTabId);
        });

        registerClickEvent("WidgetEditor.Toolbar.Timeline." + tabId, [this, capturedTabId]()
        {
            auto it = m_widgetEditorStates.find(capturedTabId);
            if (it == m_widgetEditorStates.end())
                return;
            it->second.showAnimationsPanel = !it->second.showAnimationsPanel;
            refreshWidgetEditorTimeline(capturedTabId);
            refreshWidgetEditorToolbar(capturedTabId);
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
        registerWidget(leftWidgetId, leftWidget, tabId);
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
        registerWidget(rightWidgetId, rightWidget, tabId);
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
        showToastMessage("Widget saved.", 2.0f, NotificationLevel::Success);
    }
    else
    {
        showToastMessage("Failed to save widget.", 3.0f, NotificationLevel::Error);
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
        }
        if (auto* timelineBtn = findById(el, "WidgetEditor.Toolbar.Timeline"))
        {
            timelineBtn->text = stateIt->second.showAnimationsPanel ? "Hide Timeline" : "Timeline";
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
// Widget Editor: public entry point Ã¢â‚¬â€œ delete selected element if in editor tab
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

            // Recurse into children Ã¢â‚¬â€œ a matching child takes priority over
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

#endif // ENGINE_EDITOR

// ---------------------------------------------------------------------------
// Widget Editor: right-mouse-down starts panning on the canvas
// ---------------------------------------------------------------------------
bool UIManager::handleRightMouseDown(const Vec2& screenPos)
{
#if ENGINE_EDITOR
    auto* state = getActiveWidgetEditorState();
    if (!state)
        return false;

    if (!isOverWidgetEditorCanvas(screenPos))
        return false;

    state->isPanning = true;
    state->panStartMouse = screenPos;
    state->panStartOffset = state->panOffset;
    return true;
#else
    (void)screenPos;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Widget Editor: right-mouse-up ends panning
// ---------------------------------------------------------------------------
bool UIManager::handleRightMouseUp(const Vec2& screenPos)
{
#if ENGINE_EDITOR
    (void)screenPos;
    auto* state = getActiveWidgetEditorState();
    if (!state || !state->isPanning)
        return false;

    state->isPanning = false;
    return true;
#else
    (void)screenPos;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// General mouse motion â€“ handles slider drag and other continuous interactions
// ---------------------------------------------------------------------------
void UIManager::handleMouseMotion(const Vec2& screenPos)
{
#if ENGINE_EDITOR
    // Widget Editor: mouse motion updates pan offset
    {
        auto* state = getActiveWidgetEditorState();
        if (state && state->isPanning)
        {
            state->panOffset.x = state->panStartOffset.x + (screenPos.x - state->panStartMouse.x);
            state->panOffset.y = state->panStartOffset.y + (screenPos.y - state->panStartMouse.y);
        }
    }
#endif

    if (!m_sliderDragElementId.empty())
    {
        WidgetElement* slider = findElementById(m_sliderDragElementId);
        if (slider && slider->type == WidgetElementType::Slider)
        {
            const float relX = (screenPos.x - slider->computedPositionPixels.x)
                              / std::max(1.0f, slider->computedSizePixels.x);
            const float t = std::clamp(relX, 0.0f, 1.0f);
            const float newVal = slider->minValue + t * (slider->maxValue - slider->minValue);
            slider->valueFloat = newVal;
            if (slider->onValueChanged)
            {
                slider->onValueChanged(std::to_string(newVal));
            }
            m_renderDirty = true;
        }
        else
        {
            m_sliderDragElementId.clear();
        }
    }
}

#if ENGINE_EDITOR
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
        markWidgetEditorDirty(capturedTabId);
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 != m_widgetEditorStates.end() && it2->second.editedWidget)
            it2->second.editedWidget->markLayoutDirty();
        markAllWidgetsDirty();
    };

    WidgetDetailSchema::Options opts;
    opts.showEditableId = true;
    opts.onIdRenamed = [this, capturedTabId](const std::string& newId) {
        auto it2 = m_widgetEditorStates.find(capturedTabId);
        if (it2 != m_widgetEditorStates.end())
            it2->second.selectedElementId = newId;
    };
    opts.onRefreshHierarchy = [this, capturedTabId]() {
        refreshWidgetEditorHierarchy(capturedTabId);
    };

    WidgetDetailSchema::buildDetailPanel("WE.Det", selected, applyChange, rootPanel, opts);

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

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(420.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(340.0f));
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

    constexpr float W = 420.0f;
    constexpr float H = 340.0f;

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
        e.fontSize  = EditorTheme::Get().fontSizeBody;
        e.style.textColor = EditorTheme::Get().textPrimary;
        e.textAlignV = TextAlignV::Center;
        e.padding   = EditorTheme::Scaled(Vec2{ 6.0f, 0.0f });
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
        e.fontSize     = EditorTheme::Get().fontSizeBody;
        e.style.color        = EditorTheme::Get().inputBackground;
        e.style.hoverColor   = EditorTheme::Get().inputBackgroundHover;
        e.style.textColor    = EditorTheme::Get().inputText;
        e.padding      = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
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
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(bg);
    }

    // Title bar
    {
        WidgetElement title;
        title.type  = WidgetElementType::Panel;
        title.id    = "LM.TitleBg";
        title.from  = Vec2{ 0.0f, 0.0f };
        title.to    = Vec2{ 1.0f, ny(44.0f) };
        title.style.color = EditorTheme::Get().titleBarBackground;
        elements.push_back(title);

        elements.push_back(makeLabel("LM.TitleText", "Landscape Manager",
            8.0f, 0.0f, W - 8.0f, 44.0f));
    }

    constexpr float kRowH = 28.0f;
    constexpr float kRowGap = 8.0f;
    constexpr float kFormY0 = 54.0f;
    constexpr float kLabelX1 = 130.0f;
    constexpr float kEntryX0 = 138.0f;
    constexpr float kEntryX1 = W - 12.0f;

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
        sep.style.color = EditorTheme::Get().panelBorder;
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
        btn.fontSize      = EditorTheme::Get().fontSizeBody;
        btn.style.color         = EditorTheme::Get().accentGreen;
        btn.style.hoverColor    = Vec4{ 0.40f, 0.78f, 0.44f, 1.0f };
        btn.style.textColor     = EditorTheme::Get().buttonPrimaryText;
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
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
                showToastMessage("Landscape created: " + p.name, 3.0f, NotificationLevel::Success);

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
                showToastMessage("Failed to create landscape.", 3.0f, NotificationLevel::Error);
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
        btn.fontSize      = EditorTheme::Get().fontSizeBody;
        btn.style.color         = EditorTheme::Get().buttonDefault;
        btn.style.hoverColor    = EditorTheme::Get().buttonHover;
        btn.style.textColor     = EditorTheme::Get().buttonText;
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
        btn.hitTestMode = HitTestMode::Enabled;
        btn.onClicked     = [this]()
        {
            m_renderer->closePopupWindow("LandscapeManager");
        };
        elements.push_back(btn);
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("LandscapeManagerWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("LandscapeManager.Main", widget);
}

// ---------------------------------------------------------------------------
// Material Editor Popup
// ---------------------------------------------------------------------------
void UIManager::openMaterialEditorPopup(const std::string& materialAssetPath)
{
    if (!m_renderer)
        return;

    Logger::Instance().log(Logger::Category::Input, "Material Editor opened.", Logger::LogLevel::INFO);

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(480.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(560.0f));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "MaterialEditor", "Material Editor", kPopupW, kPopupH);
    if (!popup) return;

    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    // â”€â”€ Collect material assets from the registry â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    auto& assetMgr = AssetManager::Instance();
    const auto& registry = assetMgr.getAssetRegistry();

    struct MatEntry { std::string name; std::string path; };
    std::vector<MatEntry> matEntries;
    std::vector<std::string> matNames;
    int initialSelection = 0;

    for (const auto& entry : registry)
    {
        if (entry.type == AssetType::Material)
        {
            matEntries.push_back({ entry.name, entry.path });
            matNames.push_back(entry.name);
            if (!materialAssetPath.empty() && entry.path == materialAssetPath)
                initialSelection = static_cast<int>(matEntries.size()) - 1;
        }
    }

    if (matEntries.empty())
    {
        showToastMessage("No material assets found.", 3.0f);
        m_renderer->closePopupWindow("MaterialEditor");
        return;
    }

    // â”€â”€ Shared form state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    struct MaterialFormState
    {
        int selectedIndex{ 0 };
        float shininess{ 32.0f };
        float metallic{ 0.0f };
        float roughness{ 0.5f };
        bool pbrEnabled{ false };
        std::string texDiffuse;
        std::string texSpecular;
        std::string texNormal;
        std::string texEmissive;
        std::string texMetallicRoughness;
    };
    auto state = std::make_shared<MaterialFormState>();
    state->selectedIndex = initialSelection;

    // â”€â”€ Helper: load values from the selected asset â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    auto loadFromAsset = [matEntries](MaterialFormState& s)
    {
        if (s.selectedIndex < 0 || s.selectedIndex >= static_cast<int>(matEntries.size()))
            return;

        auto& mgr = AssetManager::Instance();
        const auto& path = matEntries[s.selectedIndex].path;
        auto asset = mgr.getLoadedAssetByPath(path);
        if (!asset)
        {
            int id = mgr.loadAsset(path, AssetType::Material);
            if (id != 0)
                asset = mgr.getLoadedAssetByID(static_cast<unsigned int>(id));
        }
        if (!asset) return;

        const json& d = asset->getData();
        s.shininess = d.value("m_shininess", 32.0f);
        s.metallic  = d.value("m_metallic", 0.0f);
        s.roughness = d.value("m_roughness", 0.5f);
        s.pbrEnabled = d.value("m_pbrEnabled", false);

        // Texture asset paths
        s.texDiffuse.clear();
        s.texSpecular.clear();
        s.texNormal.clear();
        s.texEmissive.clear();
        s.texMetallicRoughness.clear();
        if (d.contains("m_textureAssetPaths") && d["m_textureAssetPaths"].is_array())
        {
            const auto& arr = d["m_textureAssetPaths"];
            auto getSlot = [&](size_t idx) -> std::string
            {
                if (idx < arr.size() && arr[idx].is_string())
                    return arr[idx].get<std::string>();
                return {};
            };
            s.texDiffuse            = getSlot(0);
            s.texSpecular           = getSlot(1);
            s.texNormal             = getSlot(2);
            s.texEmissive           = getSlot(3);
            s.texMetallicRoughness  = getSlot(4);
        }
    };

    loadFromAsset(*state);

    // â”€â”€ Build UI â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    constexpr float W = 480.0f;
    constexpr float H = 560.0f;
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "ME.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(bg);
    }

    // Title bar
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "ME.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(44.0f) };
        titleBg.style.color = EditorTheme::Get().titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "ME.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(44.0f) };
        titleText.text      = "Material Editor";
        titleText.fontSize  = EditorTheme::Get().fontSizeSubheading;
        titleText.style.textColor = EditorTheme::Get().titleBarText;
        titleText.textAlignV = TextAlignV::Center;
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 6.0f, 0.0f };
        elements.push_back(titleText);
    }

    // â”€â”€ Content area (vertical StackPanel) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    WidgetElement contentStack;
    contentStack.type        = WidgetElementType::StackPanel;
    contentStack.id          = "ME.ContentArea";
    contentStack.orientation = StackOrientation::Vertical;
    contentStack.from        = Vec2{ nx(12.0f), ny(54.0f) };
    contentStack.to          = Vec2{ nx(W - 12.0f), ny(H - 60.0f) };
    contentStack.style.color = EditorTheme::Get().transparent;
    contentStack.padding     = Vec2{ 0.0f, 4.0f };
    contentStack.scrollable  = true;

    // Material selector dropdown
    {
        auto row = EditorUIBuilder::makeDropDownRow("ME.MatSelect", "Material",
            matNames, state->selectedIndex,
            [state, popup, matEntries, loadFromAsset](int idx)
            {
                state->selectedIndex = idx;
                loadFromAsset(*state);

                // Rebuild the content area from the new state
                auto& pMgr = popup->uiManager();
                auto* content = pMgr.findElementById("ME.ContentArea");
                if (!content) return;

                // Update controls by finding them
                auto* shininessSlider = pMgr.findElementById("ME.Shininess.Slider");
                if (shininessSlider) shininessSlider->valueFloat = state->shininess;

                auto* metallicSlider = pMgr.findElementById("ME.Metallic.Slider");
                if (metallicSlider) metallicSlider->valueFloat = state->metallic;

                auto* roughnessSlider = pMgr.findElementById("ME.Roughness.Slider");
                if (roughnessSlider) roughnessSlider->valueFloat = state->roughness;

                auto* pbrCheck = pMgr.findElementById("ME.PBR");
                if (pbrCheck) pbrCheck->isChecked = state->pbrEnabled;

                auto* diffEntry = pMgr.findElementById("ME.TexDiffuse.Value");
                if (diffEntry) diffEntry->value = state->texDiffuse;
                auto* specEntry = pMgr.findElementById("ME.TexSpecular.Value");
                if (specEntry) specEntry->value = state->texSpecular;
                auto* normEntry = pMgr.findElementById("ME.TexNormal.Value");
                if (normEntry) normEntry->value = state->texNormal;
                auto* emisEntry = pMgr.findElementById("ME.TexEmissive.Value");
                if (emisEntry) emisEntry->value = state->texEmissive;
                auto* mrEntry = pMgr.findElementById("ME.TexMR.Value");
                if (mrEntry) mrEntry->value = state->texMetallicRoughness;
            });
        contentStack.children.push_back(std::move(row));
    }

    contentStack.children.push_back(EditorUIBuilder::makeDivider());

    // â”€â”€ PBR Section â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        auto pbrCheck = EditorUIBuilder::makeCheckBox("ME.PBR", "PBR Enabled",
            state->pbrEnabled, [state](bool checked) { state->pbrEnabled = checked; });
        contentStack.children.push_back(std::move(pbrCheck));
    }

    {
        auto row = EditorUIBuilder::makeSliderRow("ME.Metallic", "Metallic",
            state->metallic, 0.0f, 1.0f,
            [state](float v) { state->metallic = v; });
        contentStack.children.push_back(std::move(row));
    }

    {
        auto row = EditorUIBuilder::makeSliderRow("ME.Roughness", "Roughness",
            state->roughness, 0.0f, 1.0f,
            [state](float v) { state->roughness = v; });
        contentStack.children.push_back(std::move(row));
    }

    {
        auto row = EditorUIBuilder::makeSliderRow("ME.Shininess", "Shininess",
            state->shininess, 1.0f, 128.0f,
            [state](float v) { state->shininess = v; });
        contentStack.children.push_back(std::move(row));
    }

    contentStack.children.push_back(EditorUIBuilder::makeDivider());

    // â”€â”€ Texture Slots â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        auto row = EditorUIBuilder::makeStringRow("ME.TexDiffuse", "Diffuse",
            state->texDiffuse, [state](const std::string& v) { state->texDiffuse = v; });
        contentStack.children.push_back(std::move(row));
    }
    {
        auto row = EditorUIBuilder::makeStringRow("ME.TexSpecular", "Specular",
            state->texSpecular, [state](const std::string& v) { state->texSpecular = v; });
        contentStack.children.push_back(std::move(row));
    }
    {
        auto row = EditorUIBuilder::makeStringRow("ME.TexNormal", "Normal",
            state->texNormal, [state](const std::string& v) { state->texNormal = v; });
        contentStack.children.push_back(std::move(row));
    }
    {
        auto row = EditorUIBuilder::makeStringRow("ME.TexEmissive", "Emissive",
            state->texEmissive, [state](const std::string& v) { state->texEmissive = v; });
        contentStack.children.push_back(std::move(row));
    }
    {
        auto row = EditorUIBuilder::makeStringRow("ME.TexMR", "Metallic/Rough",
            state->texMetallicRoughness, [state](const std::string& v) { state->texMetallicRoughness = v; });
        contentStack.children.push_back(std::move(row));
    }

    elements.push_back(std::move(contentStack));

    // â”€â”€ Buttons: Save & Close â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const float btnY0 = H - 48.0f;
    const float btnY1 = btnY0 + 34.0f;

    // Save button
    {
        WidgetElement btn;
        btn.type          = WidgetElementType::Button;
        btn.id            = "ME.SaveBtn";
        btn.from          = Vec2{ nx(W - 220.0f), ny(btnY0) };
        btn.to            = Vec2{ nx(W - 114.0f), ny(btnY1) };
        btn.text          = "Save";
        btn.fontSize      = EditorTheme::Get().fontSizeBody;
        btn.style.color         = EditorTheme::Get().accentGreen;
        btn.style.hoverColor    = Vec4{ 0.40f, 0.78f, 0.44f, 1.0f };
        btn.style.textColor     = EditorTheme::Get().buttonPrimaryText;
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
        btn.hitTestMode = HitTestMode::Enabled;
        btn.onClicked     = [state, matEntries, this]()
        {
            if (state->selectedIndex < 0 || state->selectedIndex >= static_cast<int>(matEntries.size()))
                return;

            auto& assetMgr = AssetManager::Instance();
            const auto& path = matEntries[state->selectedIndex].path;
            auto asset = assetMgr.getLoadedAssetByPath(path);
            if (!asset)
            {
                int id = assetMgr.loadAsset(path, AssetType::Material);
                if (id != 0)
                    asset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(id));
            }
            if (!asset)
            {
                showToastMessage("Failed to load material for saving.", 3.0f);
                return;
            }

            // Update the asset JSON data from form state
            json& d = asset->getData();
            d["m_shininess"]  = state->shininess;
            d["m_metallic"]   = state->metallic;
            d["m_roughness"]  = state->roughness;
            d["m_pbrEnabled"] = state->pbrEnabled;

            // Build texture paths array (5 fixed slots)
            json texArr = json::array();
            const std::string* slotPtrs[] = {
                &state->texDiffuse, &state->texSpecular, &state->texNormal,
                &state->texEmissive, &state->texMetallicRoughness };

            int lastSlot = -1;
            for (int i = 4; i >= 0; --i)
            {
                if (!slotPtrs[i]->empty()) { lastSlot = i; break; }
            }
            for (int i = 0; i <= lastSlot; ++i)
            {
                if (!slotPtrs[i]->empty())
                    texArr.push_back(*slotPtrs[i]);
                else
                    texArr.push_back(nullptr);
            }
            if (!texArr.empty())
                d["m_textureAssetPaths"] = texArr;
            else
                d.erase("m_textureAssetPaths");

            asset->setIsSaved(false);
            Asset a;
            a.type = AssetType::Material;
            a.ID   = asset->getId();
            assetMgr.saveAsset(a, AssetManager::Sync);

            showToastMessage("Material saved: " + matEntries[state->selectedIndex].name, 2.5f);
            m_renderer->closePopupWindow("MaterialEditor");
        };
        elements.push_back(btn);
    }

    // Close button
    {
        WidgetElement btn;
        btn.type          = WidgetElementType::Button;
        btn.id            = "ME.CloseBtn";
        btn.from          = Vec2{ nx(W - 104.0f), ny(btnY0) };
        btn.to            = Vec2{ nx(W - 12.0f),  ny(btnY1) };
        btn.text          = "Close";
        btn.fontSize      = EditorTheme::Get().fontSizeBody;
        btn.style.color         = EditorTheme::Get().buttonDefault;
        btn.style.hoverColor    = EditorTheme::Get().buttonHover;
        btn.style.textColor     = EditorTheme::Get().buttonText;
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
        btn.hitTestMode = HitTestMode::Enabled;
        btn.onClicked     = [this]()
        {
            m_renderer->closePopupWindow("MaterialEditor");
        };
        elements.push_back(btn);
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("MaterialEditorWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("MaterialEditor.Main", widget);
}

void UIManager::openEngineSettingsPopup()
{
    if (!m_renderer)
        return;

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(620.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(480.0f));
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

    const std::vector<std::string> categories = { "General", "Rendering", "Debug", "Physics", "Info" };
    const float kSidebarW = EditorTheme::Scaled(140.0f);
    const float kTitleH = EditorTheme::Scaled(44.0f);

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "ES.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(bg);
    }

    // Title bar
    {
        WidgetElement title;
        title.type  = WidgetElementType::Panel;
        title.id    = "ES.TitleBg";
        title.from  = Vec2{ 0.0f, 0.0f };
        title.to    = Vec2{ 1.0f, ny(kTitleH) };
        title.style.color = EditorTheme::Get().titleBarBackground;
        elements.push_back(title);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "ES.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(kTitleH) };
        titleText.text      = "Engine Settings";
        titleText.fontSize  = EditorTheme::Get().fontSizeSubheading;
        titleText.style.textColor = EditorTheme::Get().titleBarText;
        titleText.textAlignV = TextAlignV::Center;
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
        sidebarBg.style.color = EditorTheme::Get().panelBackgroundAlt;
        elements.push_back(sidebarBg);
    }

    // Sidebar separator line
    {
        WidgetElement sep;
        sep.type  = WidgetElementType::Panel;
        sep.id    = "ES.SidebarSep";
        sep.from  = Vec2{ nx(kSidebarW - 1.0f), ny(kTitleH) };
        sep.to    = Vec2{ nx(kSidebarW), 1.0f };
        sep.style.color = Vec4{ EditorTheme::Get().panelBorder.x, EditorTheme::Get().panelBorder.y, EditorTheme::Get().panelBorder.z, 1.0f };
        elements.push_back(sep);
    }

    const float kCatBtnH = EditorTheme::Scaled(32.0f);
    const float kCatBtnGap = EditorTheme::Scaled(2.0f);
    const float kCatBtnY0 = kTitleH + EditorTheme::Scaled(8.0f);

    Renderer* renderer = m_renderer;
    auto rebuildContent = [state, popup, categories, nx, ny, W, H, kSidebarW, kTitleH, renderer]()
    {
        auto& pMgr = popup->uiManager();
        auto* entry = pMgr.findElementById("ES.ContentArea");
        if (!entry) return;

        entry->children.clear();

        const float kRowH = EditorTheme::Scaled(30.0f);
        const float kRowGap = EditorTheme::Scaled(6.0f);
        const float kContentPad = EditorTheme::Scaled(16.0f);
        const float kLabelW = EditorTheme::Scaled(140.0f);
        const float kGapW = EditorTheme::Scaled(12.0f);
        const float contentW = W - kSidebarW;
        int row = 0;

        const auto addSectionLabel = [&](const std::string& id, const std::string& label)
        {
            WidgetElement sec;
            sec.type      = WidgetElementType::Text;
            sec.id        = id;
            sec.text      = label;
            sec.fontSize  = EditorTheme::Get().fontSizeBody;
            sec.style.textColor = EditorTheme::Get().textMuted;
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
            sep.style.color   = EditorTheme::Get().panelBorder;
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
            rowPanel.style.color       = EditorTheme::Get().transparent;
            rowPanel.padding     = Vec2{ 6.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = EditorTheme::Get().fontSizeBody;
            lbl.style.textColor = EditorTheme::Get().textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            lbl.padding   = Vec2{ 0.0f, 0.0f };
            rowPanel.children.push_back(lbl);

            WidgetElement eb;
            eb.type          = WidgetElementType::EntryBar;
            eb.id            = id;
            eb.value         = value;
            eb.fontSize      = EditorTheme::Get().fontSizeMonospace;
            eb.style.color         = EditorTheme::Get().inputBackground;
            eb.style.hoverColor    = EditorTheme::Get().inputBackgroundHover;
            eb.style.textColor     = EditorTheme::Get().inputText;
            eb.padding       = Vec2{ 6.0f, 4.0f };
            eb.hitTestMode = HitTestMode::Enabled;
            eb.minSize       = Vec2{ contentW - kContentPad * 2.0f - kLabelW - kGapW, kRowH };
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
            cb.fontSize      = EditorTheme::Get().fontSizeBody;
            cb.isChecked     = checked;
            cb.style.color         = EditorTheme::Get().inputBackground;
            cb.style.hoverColor    = EditorTheme::Get().checkboxHover;
            cb.style.fillColor     = EditorTheme::Get().checkboxChecked;
            cb.style.textColor     = EditorTheme::Get().textPrimary;
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
            rowPanel.style.color       = EditorTheme::Get().transparent;
            rowPanel.padding     = Vec2{ 6.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = EditorTheme::Get().fontSizeBody;
            lbl.style.textColor = EditorTheme::Get().textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            lbl.padding   = Vec2{ 0.0f, 0.0f };
            rowPanel.children.push_back(lbl);

            WidgetElement dd;
            dd.type          = WidgetElementType::DropDown;
            dd.id            = id;
            dd.items         = items;
            dd.selectedIndex = selected;
            dd.fontSize      = EditorTheme::Get().fontSizeMonospace;
            dd.style.color         = EditorTheme::Get().inputBackground;
            dd.style.hoverColor    = EditorTheme::Get().inputBackgroundHover;
            dd.style.textColor     = EditorTheme::Get().inputText;
            dd.padding       = Vec2{ 6.0f, 4.0f };
            dd.hitTestMode = HitTestMode::Enabled;
            dd.minSize       = Vec2{ contentW - kContentPad * 2.0f - kLabelW - kGapW, kRowH };
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
                    DiagnosticsManager::Instance().saveConfig();
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
                    DiagnosticsManager::Instance().saveConfig();
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
                    DiagnosticsManager::Instance().saveConfig();
                });

            addSeparator("ES.C.Sep.Scripting");
            addSectionLabel("ES.C.Sec.Scripting", "Scripting");
            const bool isHotReload = []() {
                if (auto v = DiagnosticsManager::Instance().getState("ScriptHotReloadEnabled"))
                    return *v != "false";
                return true; // default enabled
            }();
            addCheckbox("ES.C.ScriptHotReload", "Script Hot-Reload",
                isHotReload,
                [](bool v) {
                    DiagnosticsManager::Instance().setState("ScriptHotReloadEnabled", v ? "true" : "false");
                    DiagnosticsManager::Instance().saveConfig();
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
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.CSM", "Cascaded Shadow Maps",
                renderer->isCsmEnabled(),
                [renderer](bool v) {
                    renderer->setCsmEnabled(v);
                    DiagnosticsManager::Instance().setState("CsmEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addSeparator("ES.C.Sep1");
            addSectionLabel("ES.C.Sec.Display", "Display");
            addCheckbox("ES.C.VSync", "VSync",
                renderer->isVSyncEnabled(),
                [renderer](bool v) {
                    renderer->setVSyncEnabled(v);
                    DiagnosticsManager::Instance().setState("VSyncEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.Wireframe", "Wireframe Mode",
                renderer->isWireframeEnabled(),
                [renderer](bool v) {
                    renderer->setWireframeEnabled(v);
                    DiagnosticsManager::Instance().setState("WireframeEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addSeparator("ES.C.Sep2");
            addSectionLabel("ES.C.Sec.PostProcess", "Post-Processing");
            addCheckbox("ES.C.PostProcess", "Post Processing",
                renderer->isPostProcessingEnabled(),
                [renderer](bool v) {
                    renderer->setPostProcessingEnabled(v);
                    DiagnosticsManager::Instance().setState("PostProcessingEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.Gamma", "Gamma Correction",
                renderer->isGammaCorrectionEnabled(),
                [renderer](bool v) {
                    renderer->setGammaCorrectionEnabled(v);
                    DiagnosticsManager::Instance().setState("GammaCorrectionEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.ToneMapping", "Tone Mapping (ACES)",
                renderer->isToneMappingEnabled(),
                [renderer](bool v) {
                    renderer->setToneMappingEnabled(v);
                    DiagnosticsManager::Instance().setState("ToneMappingEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            {
                const std::vector<std::string> aaItems = { "None", "FXAA", "MSAA 2x", "MSAA 4x" };
                const int aaIdx = static_cast<int>(renderer->getAntiAliasingMode());
                addDropdown("ES.C.AA", "Anti-Aliasing",
                    aaItems, aaIdx,
                    [renderer](int index) {
                        renderer->setAntiAliasingMode(static_cast<Renderer::AntiAliasingMode>(index));
                        DiagnosticsManager::Instance().setState("AntiAliasingMode", std::to_string(index));
                        DiagnosticsManager::Instance().saveConfig();
                    });
            }
            addCheckbox("ES.C.Bloom", "Bloom",
                renderer->isBloomEnabled(),
                [renderer](bool v) {
                    renderer->setBloomEnabled(v);
                    DiagnosticsManager::Instance().setState("BloomEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.SSAO", "SSAO (Ambient Occlusion)",
                renderer->isSsaoEnabled(),
                [renderer](bool v) {
                    renderer->setSsaoEnabled(v);
                    DiagnosticsManager::Instance().setState("SsaoEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addSeparator("ES.C.Sep2b");
            addSectionLabel("ES.C.Sec.Effects", "Scene Effects");
            addCheckbox("ES.C.Fog", "Fog",
                renderer->isFogEnabled(),
                [renderer](bool v) {
                    renderer->setFogEnabled(v);
                    DiagnosticsManager::Instance().setState("FogEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addSeparator("ES.C.Sep3");
            addSectionLabel("ES.C.Sec.Perf", "Performance");
            addCheckbox("ES.C.Occlusion", "Occlusion Culling",
                renderer->isOcclusionCullingEnabled(),
                [renderer](bool v) {
                    renderer->setOcclusionCullingEnabled(v);
                    DiagnosticsManager::Instance().setState("OcclusionCullingEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.TexCompress", "Texture Compression (S3TC)",
                renderer->isTextureCompressionEnabled(),
                [renderer](bool v) {
                    renderer->setTextureCompressionEnabled(v);
                    DiagnosticsManager::Instance().setState("TextureCompressionEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.TexStream", "Texture Streaming",
                renderer->isTextureStreamingEnabled(),
                [renderer](bool v) {
                    renderer->setTextureStreamingEnabled(v);
                    DiagnosticsManager::Instance().setState("TextureStreamingEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.Displacement", "Displacement Mapping (Tessellation)",
                renderer->isDisplacementMappingEnabled(),
                [renderer](bool v) {
                    renderer->setDisplacementMappingEnabled(v);
                    DiagnosticsManager::Instance().setState("DisplacementMappingEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addFloatEntry("ES.C.DispScale", "Displacement Scale",
                std::to_string(renderer->getDisplacementScale()),
                [renderer](const std::string& v) {
                    try {
                        float fv = std::stof(v);
                        renderer->setDisplacementScale(fv);
                        DiagnosticsManager::Instance().setState("DisplacementScale", v);
                        DiagnosticsManager::Instance().saveConfig();
                    } catch (...) {}
                });
            addFloatEntry("ES.C.TessLevel", "Tessellation Level",
                std::to_string(renderer->getTessellationLevel()),
                [renderer](const std::string& v) {
                    try {
                        float fv = std::max(1.0f, std::min(std::stof(v), 64.0f));
                        renderer->setTessellationLevel(fv);
                        DiagnosticsManager::Instance().setState("TessellationLevel", std::to_string(fv));
                        DiagnosticsManager::Instance().saveConfig();
                    } catch (...) {}
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
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.BoundsDebug", "Bounding Box Debug",
                renderer->isBoundsDebugEnabled(),
                [renderer](bool v) {
                    if (v != renderer->isBoundsDebugEnabled()) renderer->toggleBoundsDebug();
                    DiagnosticsManager::Instance().setState("BoundsDebugEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
            addCheckbox("ES.C.HFDebug", "HeightField Debug",
                renderer->isHeightFieldDebugEnabled(),
                [renderer](bool v) {
                    renderer->setHeightFieldDebugEnabled(v);
                    DiagnosticsManager::Instance().setState("HeightFieldDebugEnabled", v ? "1" : "0");
                    DiagnosticsManager::Instance().saveConfig();
                });
        }
        else if (state->activeCategory == 3) // Physics
        {
            auto& diag = DiagnosticsManager::Instance();

            // Ã¢â€â‚¬Ã¢â€â‚¬ Backend selector Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
                        DiagnosticsManager::Instance().saveConfig();
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
                    DiagnosticsManager::Instance().saveConfig();
                });
            addFloatEntry("ES.C.GravityY", "Gravity Y",
                readFloat("PhysicsGravityY", -9.81f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsGravityY", v);
                    DiagnosticsManager::Instance().saveConfig();
                });
            addFloatEntry("ES.C.GravityZ", "Gravity Z",
                readFloat("PhysicsGravityZ", 0.0f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsGravityZ", v);
                    DiagnosticsManager::Instance().saveConfig();
                });

            addSeparator("ES.C.Sep.Phys1");
            addSectionLabel("ES.C.Sec.Simulation", "Simulation");

            addFloatEntry("ES.C.FixedTimestep", "Fixed Timestep (s)",
                readFloat("PhysicsFixedTimestep", 1.0f / 60.0f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsFixedTimestep", v);
                    DiagnosticsManager::Instance().saveConfig();
                });

            addSeparator("ES.C.Sep.Phys2");
            addSectionLabel("ES.C.Sec.Sleep", "Sleep / Deactivation");

            addFloatEntry("ES.C.SleepThreshold", "Sleep Threshold",
                readFloat("PhysicsSleepThreshold", 0.05f),
                [](const std::string& v) {
                    DiagnosticsManager::Instance().setState("PhysicsSleepThreshold", v);
                    DiagnosticsManager::Instance().saveConfig();
                });
        }
        else if (state->activeCategory == 4) // Info
        {
            const auto& hw = DiagnosticsManager::Instance().getHardwareInfo();

            const auto addInfoRow = [&](const std::string& id, const std::string& label, const std::string& value)
            {
                WidgetElement rowPanel;
                rowPanel.type        = WidgetElementType::StackPanel;
                rowPanel.id          = id + ".Row";
                rowPanel.orientation = StackOrientation::Horizontal;
                rowPanel.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
                rowPanel.style.color = EditorTheme::Get().transparent;
                rowPanel.padding     = Vec2{ 6.0f, 2.0f };

                WidgetElement lbl;
                lbl.type      = WidgetElementType::Text;
                lbl.id        = id + ".Lbl";
                lbl.text      = label;
                lbl.fontSize  = EditorTheme::Get().fontSizeBody;
                lbl.style.textColor = EditorTheme::Get().textPrimary;
                lbl.textAlignV = TextAlignV::Center;
                lbl.minSize   = Vec2{ kLabelW, kRowH };
                rowPanel.children.push_back(lbl);

                WidgetElement val;
                val.type      = WidgetElementType::Text;
                val.id        = id + ".Val";
                val.text      = value;
                val.fontSize  = EditorTheme::Get().fontSizeMonospace;
                val.style.textColor = EditorTheme::Get().textSecondary;
                val.textAlignV = TextAlignV::Center;
                val.minSize   = Vec2{ contentW - kContentPad * 2.0f - kLabelW - kGapW, kRowH };
                rowPanel.children.push_back(val);

                entry->children.push_back(rowPanel);
                ++row;
            };

            // CPU
            addSectionLabel("ES.C.Sec.CPU", "CPU");
            addSeparator("ES.C.Sep.CPU");
            addInfoRow("ES.C.CpuBrand",    "Brand",          hw.cpu.brand.empty() ? "Unknown" : hw.cpu.brand);
            addInfoRow("ES.C.CpuPhys",     "Physical Cores", std::to_string(hw.cpu.physicalCores));
            addInfoRow("ES.C.CpuLogical",  "Logical Cores",  std::to_string(hw.cpu.logicalCores));

            // GPU
            addSeparator("ES.C.Sep.GPU");
            addSectionLabel("ES.C.Sec.GPU", "GPU");
            addSeparator("ES.C.Sep.GPU2");
            addInfoRow("ES.C.GpuRenderer", "Renderer",       hw.gpu.renderer.empty() ? "Unknown" : hw.gpu.renderer);
            addInfoRow("ES.C.GpuVendor",   "Vendor",         hw.gpu.vendor.empty() ? "Unknown" : hw.gpu.vendor);
            addInfoRow("ES.C.GpuDriver",   "Driver Version", hw.gpu.driverVersion.empty() ? "Unknown" : hw.gpu.driverVersion);
            if (hw.gpu.vramTotalMB > 0)
                addInfoRow("ES.C.GpuVram", "VRAM Total",     std::to_string(hw.gpu.vramTotalMB) + " MB");
            if (hw.gpu.vramFreeMB > 0)
                addInfoRow("ES.C.GpuVramFree", "VRAM Free",  std::to_string(hw.gpu.vramFreeMB) + " MB");

            // RAM
            addSeparator("ES.C.Sep.RAM");
            addSectionLabel("ES.C.Sec.RAM", "RAM");
            addSeparator("ES.C.Sep.RAM2");
            addInfoRow("ES.C.RamTotal",    "Total",          std::to_string(hw.ram.totalMB) + " MB");
            addInfoRow("ES.C.RamAvail",    "Available",      std::to_string(hw.ram.availableMB) + " MB");

            // Monitors
            addSeparator("ES.C.Sep.Mon");
            addSectionLabel("ES.C.Sec.Mon", "Monitors");
            addSeparator("ES.C.Sep.Mon2");
            for (size_t mi = 0; mi < hw.monitors.size(); ++mi)
            {
                const auto& mon = hw.monitors[mi];
                const std::string idx = std::to_string(mi);
                std::string label = mon.name;
                if (mon.primary) label += " (Primary)";
                addInfoRow("ES.C.Mon" + idx + ".Name", "Display " + std::to_string(mi + 1), label);
                addInfoRow("ES.C.Mon" + idx + ".Res",  "Resolution",
                    std::to_string(mon.width) + " x " + std::to_string(mon.height)
                    + " @ " + std::to_string(mon.refreshRate) + " Hz");
                std::ostringstream dpiStr;
                dpiStr << std::fixed << std::setprecision(0) << (mon.dpiScale * 100.0f) << "%";
                addInfoRow("ES.C.Mon" + idx + ".DPI",  "DPI Scale", dpiStr.str());
            }
        }

        for (size_t ci = 0; ci < categories.size(); ++ci)
        {
            const std::string btnId = "ES.Cat." + std::to_string(ci);
            auto* catBtn = pMgr.findElementById(btnId);
            if (catBtn)
            {
                catBtn->style.color = (static_cast<int>(ci) == state->activeCategory)
                    ? EditorTheme::Get().accentMuted
                    : EditorTheme::Get().transparent;
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
        catBtn.fontSize      = EditorTheme::Get().fontSizeBody;
        catBtn.style.color         = (static_cast<int>(ci) == state->activeCategory)
            ? EditorTheme::Get().accentMuted
            : EditorTheme::Get().transparent;
        catBtn.style.hoverColor    = EditorTheme::Get().buttonSubtleHover;
        catBtn.style.textColor     = EditorTheme::Get().textPrimary;
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
        content.style.color       = EditorTheme::Get().transparent;
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;
        elements.push_back(content);
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("EngineSettingsWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("EngineSettings.Main", widget);

    rebuildContent();
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
// Editor Settings Popup (font size, theme tuning)
// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
void UIManager::openEditorSettingsPopup()
{
    if (!m_renderer)
        return;

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(480.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(600.0f));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "EditorSettings", "Editor Settings", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    auto& theme = EditorTheme::Get();

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "EdS.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = theme.panelBackground;
        elements.push_back(bg);
    }

    const float kTitleH = EditorTheme::Scaled(40.0f);

    // Title bar
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "EdS.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.style.color = theme.titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "EdS.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(kTitleH) };
        titleText.text      = "Editor Settings";
        titleText.fontSize  = theme.fontSizeHeading;
        titleText.style.textColor = theme.titleBarText;
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 8.0f, 0.0f };
        elements.push_back(titleText);
    }

    // Content area (scrollable stack)
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "EdS.Content";
        content.from        = Vec2{ nx(16.0f), ny(kTitleH + 12.0f) };
        content.to          = Vec2{ nx(W - 16.0f), ny(H - 16.0f) };
        content.style.color = theme.transparent;
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;

        const float contentW = W - 32.0f;
        const float kRowH = EditorTheme::Scaled(28.0f);
        const float kLabelW = EditorTheme::Scaled(150.0f);

        // -- Section: Theme --
        {
            const std::string themesDir = EditorTheme::GetThemesDirectory();
            std::vector<std::string> themeNames = EditorTheme::discoverThemes(themesDir);
            if (themeNames.empty()) themeNames.push_back("Dark");

            int selectedIdx = 0;
            for (int i = 0; i < static_cast<int>(themeNames.size()); ++i)
            {
                if (themeNames[i] == theme.themeName)
                { selectedIdx = i; break; }
            }

            WidgetElement secLabel;
            secLabel.type      = WidgetElementType::Text;
            secLabel.id        = "EdS.Sec.Theme";
            secLabel.text      = "Theme";
            secLabel.fontSize  = theme.fontSizeSubheading;
            secLabel.style.textColor = theme.textSecondary;
            secLabel.textAlignV = TextAlignV::Center;
            secLabel.minSize   = Vec2{ contentW - 8.0f, kRowH };
            secLabel.padding   = Vec2{ 2.0f, 0.0f };
            content.children.push_back(secLabel);

            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = "EdS.Sep.Theme";
            sep.style.color   = theme.panelBorder;
            sep.minSize = Vec2{ contentW - 8.0f, 1.0f };
            content.children.push_back(sep);

            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = "EdS.ThemeDD.Row";
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - 8.0f, kRowH };
            row.style.color = theme.transparent;
            row.padding     = Vec2{ 4.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = "EdS.ThemeDD.Lbl";
            lbl.text      = "Active Theme";
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            WidgetElement dd;
            dd.type          = WidgetElementType::DropDown;
            dd.id            = "EdS.ThemeDD";
            dd.items         = themeNames;
            dd.selectedIndex = selectedIdx;
            dd.fontSize      = theme.fontSizeSmall;
            dd.style.color         = theme.inputBackground;
            dd.style.hoverColor    = theme.inputBackgroundHover;
            dd.style.textColor     = theme.inputText;
            dd.padding       = Vec2{ 6.0f, 4.0f };
            dd.hitTestMode   = HitTestMode::Enabled;
            dd.minSize       = Vec2{ contentW - 8.0f - kLabelW - 12.0f, kRowH };

            UIManager* self = this;
            dd.onSelectionChanged = [self, themeNames](int idx) {
                if (idx < 0 || idx >= static_cast<int>(themeNames.size())) return;
                const std::string& name = themeNames[idx];
                auto& t = EditorTheme::Get();
                if (t.themeName == name) return;

                t.loadThemeByName(name);
                DiagnosticsManager::Instance().setState("EditorTheme", name);
                self->rebuildAllEditorUI();
            };
            row.children.push_back(dd);
            content.children.push_back(row);
        }
        {
            WidgetElement spacer;
            spacer.type    = WidgetElementType::Panel;
            spacer.id      = "EdS.Spacer.Theme";
            spacer.style.color   = theme.transparent;
            spacer.minSize = Vec2{ contentW - 8.0f, 8.0f };
            content.children.push_back(spacer);
        }

        // â”€â”€ Section: UI Scale â”€â”€
        {
            WidgetElement secLabel;
            secLabel.type      = WidgetElementType::Text;
            secLabel.id        = "EdS.Sec.Scale";
            secLabel.text      = "UI Scale";
            secLabel.fontSize  = theme.fontSizeSubheading;
            secLabel.style.textColor = theme.textSecondary;
            secLabel.textAlignV = TextAlignV::Center;
            secLabel.minSize   = Vec2{ contentW - 8.0f, kRowH };
            secLabel.padding   = Vec2{ 2.0f, 0.0f };
            content.children.push_back(secLabel);

            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = "EdS.Sep.Scale";
            sep.style.color   = theme.panelBorder;
            sep.minSize = Vec2{ contentW - 8.0f, 1.0f };
            content.children.push_back(sep);

            const std::vector<std::string> scaleItems = {
                "Auto", "100%", "125%", "150%", "175%", "200%", "250%", "300%"
            };
            const float scaleValues[] = { 0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f };

            // Determine current selection
            int scaleIdx = 0; // default: Auto
            {
                auto savedScale = DiagnosticsManager::Instance().getState("UIScale");
                if (savedScale && !savedScale->empty())
                {
                    try {
                        float sv = std::stof(*savedScale);
                        for (int i = 1; i < static_cast<int>(std::size(scaleValues)); ++i)
                        {
                            if (std::abs(sv - scaleValues[i]) < 0.01f)
                            { scaleIdx = i; break; }
                        }
                    } catch (...) {}
                }
            }

            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = "EdS.ScaleDD.Row";
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - 8.0f, kRowH };
            row.style.color = theme.transparent;
            row.padding     = Vec2{ 4.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = "EdS.ScaleDD.Lbl";
            lbl.text      = "DPI Scale";
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            WidgetElement dd;
            dd.type          = WidgetElementType::DropDown;
            dd.id            = "EdS.ScaleDD";
            dd.items         = scaleItems;
            dd.selectedIndex = scaleIdx;
            dd.fontSize      = theme.fontSizeSmall;
            dd.style.color         = theme.inputBackground;
            dd.style.hoverColor    = theme.inputBackgroundHover;
            dd.style.textColor     = theme.inputText;
            dd.padding       = Vec2{ 6.0f, 4.0f };
            dd.hitTestMode   = HitTestMode::Enabled;
            dd.minSize       = Vec2{ contentW - 8.0f - kLabelW - 12.0f, kRowH };

            UIManager* selfScale = this;
            dd.onSelectionChanged = [selfScale, scaleItems](int idx) {
                if (idx < 0 || idx >= static_cast<int>(scaleItems.size())) return;

                float newScale = 1.0f;
                if (idx == 0) // Auto â€” detect from primary monitor
                {
                    DiagnosticsManager::Instance().setState("UIScale", "");
                    const auto& hwInfo = DiagnosticsManager::Instance().getHardwareInfo();
                    for (const auto& mon : hwInfo.monitors)
                    {
                        if (mon.primary && mon.dpiScale > 0.0f)
                        { newScale = mon.dpiScale; break; }
                    }
                }
                else
                {
                    const float scaleVals[] = { 0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f };
                    newScale = scaleVals[idx];
                    DiagnosticsManager::Instance().setState("UIScale", std::to_string(newScale));
                }

                auto& t = EditorTheme::Get();
                selfScale->rebuildEditorUIForDpi(newScale);
                DiagnosticsManager::Instance().saveConfig();
            };
            row.children.push_back(dd);
            content.children.push_back(row);
        }
        {
            WidgetElement spacer;
            spacer.type    = WidgetElementType::Panel;
            spacer.id      = "EdS.Spacer.Scale";
            spacer.style.color   = theme.transparent;
            spacer.minSize = Vec2{ contentW - 8.0f, 8.0f };
            content.children.push_back(spacer);
        }


        // Ã¢â€â‚¬Ã¢â€â‚¬ Section: Font Sizes Ã¢â€â‚¬Ã¢â€â‚¬
        {
            WidgetElement secLabel;
            secLabel.type      = WidgetElementType::Text;
            secLabel.id        = "EdS.Sec.Fonts";
            secLabel.text      = "Font Sizes";
            secLabel.fontSize  = theme.fontSizeSubheading;
            secLabel.style.textColor = theme.textSecondary;
            secLabel.textAlignV = TextAlignV::Center;
            secLabel.minSize   = Vec2{ contentW - 8.0f, kRowH };
            secLabel.padding   = Vec2{ 2.0f, 0.0f };
            content.children.push_back(secLabel);
        }

        {
            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = "EdS.Sep.Fonts";
            sep.style.color   = theme.panelBorder;
            sep.minSize = Vec2{ contentW - 8.0f, 1.0f };
            content.children.push_back(sep);
        }

        struct FontSizeEntry { const char* label; const char* id; float* target; };
        FontSizeEntry fontEntries[] = {
            { "Heading",     "EdS.FontHeading",     &theme.fontSizeHeading },
            { "Subheading",  "EdS.FontSubheading",  &theme.fontSizeSubheading },
            { "Body",        "EdS.FontBody",        &theme.fontSizeBody },
            { "Small",       "EdS.FontSmall",       &theme.fontSizeSmall },
            { "Caption",     "EdS.FontCaption",     &theme.fontSizeCaption },
            { "Monospace",   "EdS.FontMono",        &theme.fontSizeMonospace },
        };

        UIManager* self = this;
        for (auto& fe : fontEntries)
        {
            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = std::string(fe.id) + ".Row";
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - 8.0f, kRowH };
            row.style.color = theme.transparent;
            row.padding     = Vec2{ 4.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = std::string(fe.id) + ".Lbl";
            lbl.text      = fe.label;
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            std::ostringstream ss;
            ss << *fe.target;

            WidgetElement eb;
            eb.type          = WidgetElementType::EntryBar;
            eb.id            = fe.id;
            eb.value         = ss.str();
            eb.fontSize      = theme.fontSizeSmall;
            eb.style.color         = theme.inputBackground;
            eb.style.hoverColor    = theme.inputBackgroundHover;
            eb.style.textColor     = theme.inputText;
            eb.padding       = Vec2{ 6.0f, 4.0f };
            eb.hitTestMode   = HitTestMode::Enabled;
            eb.minSize       = Vec2{ contentW - 8.0f - kLabelW - 12.0f, kRowH };

            float* target = fe.target;
            eb.onValueChanged = [target, self](const std::string& v) {
                try {
                    float val = std::stof(v);
                    if (val >= 6.0f && val <= 48.0f)
                    {
                        *target = val;
                        self->markAllWidgetsDirty();
                        EditorTheme::Get().saveActiveTheme();
                    }
                } catch (...) {}
            };
            row.children.push_back(eb);
            content.children.push_back(row);
        }

        // Ã¢â€â‚¬Ã¢â€â‚¬ Section: Spacing Ã¢â€â‚¬Ã¢â€â‚¬
        {
            WidgetElement spacer;
            spacer.type    = WidgetElementType::Panel;
            spacer.id      = "EdS.Spacer1";
            spacer.style.color   = theme.transparent;
            spacer.minSize = Vec2{ contentW - 8.0f, 8.0f };
            content.children.push_back(spacer);
        }
        {
            WidgetElement secLabel;
            secLabel.type      = WidgetElementType::Text;
            secLabel.id        = "EdS.Sec.Spacing";
            secLabel.text      = "Spacing";
            secLabel.fontSize  = theme.fontSizeSubheading;
            secLabel.style.textColor = theme.textSecondary;
            secLabel.textAlignV = TextAlignV::Center;
            secLabel.minSize   = Vec2{ contentW - 8.0f, kRowH };
            secLabel.padding   = Vec2{ 2.0f, 0.0f };
            content.children.push_back(secLabel);
        }
        {
            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = "EdS.Sep.Spacing";
            sep.style.color   = theme.panelBorder;
            sep.minSize = Vec2{ contentW - 8.0f, 1.0f };
            content.children.push_back(sep);
        }

        struct SpacingEntry { const char* label; const char* id; float* target; float minVal; float maxVal; };
        SpacingEntry spacingEntries[] = {
            { "Row Height",       "EdS.RowHeight",      &theme.rowHeight,       16.0f, 48.0f },
            { "Row Height Small", "EdS.RowHeightSmall", &theme.rowHeightSmall,  14.0f, 40.0f },
            { "Row Height Large", "EdS.RowHeightLarge", &theme.rowHeightLarge,  20.0f, 56.0f },
            { "Toolbar Height",   "EdS.ToolbarHeight",  &theme.toolbarHeight,   24.0f, 64.0f },
            { "Border Radius",    "EdS.BorderRadius",   &theme.borderRadius,    0.0f,  12.0f },
        };

        for (auto& se : spacingEntries)
        {
            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = std::string(se.id) + ".Row";
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - 8.0f, kRowH };
            row.style.color = theme.transparent;
            row.padding     = Vec2{ 4.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = std::string(se.id) + ".Lbl";
            lbl.text      = se.label;
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            std::ostringstream ss;
            ss << *se.target;

            WidgetElement eb;
            eb.type          = WidgetElementType::EntryBar;
            eb.id            = se.id;
            eb.value         = ss.str();
            eb.fontSize      = theme.fontSizeSmall;
            eb.style.color         = theme.inputBackground;
            eb.style.hoverColor    = theme.inputBackgroundHover;
            eb.style.textColor     = theme.inputText;
            eb.padding       = Vec2{ 6.0f, 4.0f };
            eb.hitTestMode   = HitTestMode::Enabled;
            eb.minSize       = Vec2{ contentW - 8.0f - kLabelW - 12.0f, kRowH };

            float* target = se.target;
            float minV = se.minVal, maxV = se.maxVal;
            eb.onValueChanged = [target, minV, maxV, self](const std::string& v) {
                try {
                    float val = std::stof(v);
                    if (val >= minV && val <= maxV)
                    {
                        *target = val;
                        self->markAllWidgetsDirty();
                        EditorTheme::Get().saveActiveTheme();
                    }
                } catch (...) {}
            };
            row.children.push_back(eb);
            content.children.push_back(row);
        }

        // â”€â”€ Section: Keyboard Shortcuts â”€â”€
        {
            WidgetElement spacer;
            spacer.type    = WidgetElementType::Panel;
            spacer.id      = "EdS.Spacer.Keys";
            spacer.style.color   = theme.transparent;
            spacer.minSize = Vec2{ contentW - 8.0f, 8.0f };
            content.children.push_back(spacer);
        }
        {
            WidgetElement secLabel;
            secLabel.type      = WidgetElementType::Text;
            secLabel.id        = "EdS.Sec.Shortcuts";
            secLabel.text      = "Keyboard Shortcuts";
            secLabel.fontSize  = theme.fontSizeSubheading;
            secLabel.style.textColor = theme.textSecondary;
            secLabel.textAlignV = TextAlignV::Center;
            secLabel.minSize   = Vec2{ contentW - 8.0f, kRowH };
            secLabel.padding   = Vec2{ 2.0f, 0.0f };
            content.children.push_back(secLabel);
        }
        {
            WidgetElement sep;
            sep.type    = WidgetElementType::Panel;
            sep.id      = "EdS.Sep.Shortcuts";
            sep.style.color   = theme.panelBorder;
            sep.minSize = Vec2{ contentW - 8.0f, 1.0f };
            content.children.push_back(sep);
        }

        // Build a row per registered shortcut showing [Category] Name : [Keybind Button]
        {
            auto& sm = ShortcutManager::Instance();
            const auto& actions = sm.getActions();

            // State shared by all capture buttons
            struct CaptureState
            {
                std::string capturingId;  // id of the action being rebound (empty = not capturing)
            };
            auto captureState = std::make_shared<CaptureState>();

            std::string lastCategory;
            int shortcutIdx = 0;
            for (const auto& action : actions)
            {
                // Category sub-header when category changes
                if (action.category != lastCategory)
                {
                    lastCategory = action.category;
                    WidgetElement catLabel;
                    catLabel.type      = WidgetElementType::Text;
                    catLabel.id        = "EdS.SC.Cat." + action.category;
                    catLabel.text      = action.category;
                    catLabel.fontSize  = theme.fontSizeSmall;
                    catLabel.style.textColor = Vec4{ 0.55f, 0.65f, 0.85f, 1.0f };
                    catLabel.textAlignV = TextAlignV::Center;
                    catLabel.minSize   = Vec2{ contentW - 8.0f, kRowH * 0.85f };
                    catLabel.padding   = Vec2{ 2.0f, 4.0f };
                    content.children.push_back(catLabel);
                }

                const std::string rowId = "EdS.SC." + std::to_string(shortcutIdx);
                const std::string actionId = action.id;

                WidgetElement row;
                row.type        = WidgetElementType::StackPanel;
                row.id          = rowId + ".Row";
                row.orientation = StackOrientation::Horizontal;
                row.minSize     = Vec2{ contentW - 8.0f, kRowH };
                row.style.color = theme.transparent;
                row.padding     = Vec2{ 4.0f, 2.0f };

                // Action display name
                WidgetElement lbl;
                lbl.type      = WidgetElementType::Text;
                lbl.id        = rowId + ".Lbl";
                lbl.text      = action.displayName;
                lbl.fontSize  = theme.fontSizeBody;
                lbl.style.textColor = theme.textPrimary;
                lbl.textAlignV = TextAlignV::Center;
                lbl.minSize   = Vec2{ kLabelW, kRowH };
                row.children.push_back(lbl);

                // Keybind button â€“ click to start capture, next key press rebinds
                WidgetElement btn;
                btn.type          = WidgetElementType::Button;
                btn.id            = rowId + ".Btn";
                btn.text          = action.currentCombo.toString();
                btn.fontSize      = theme.fontSizeSmall;
                btn.style.textColor     = theme.inputText;
                btn.textAlignH    = TextAlignH::Center;
                btn.textAlignV    = TextAlignV::Center;
                btn.style.color         = theme.inputBackground;
                btn.style.hoverColor    = theme.inputBackgroundHover;
                btn.shaderVertex  = "button_vertex.glsl";
                btn.shaderFragment = "button_fragment.glsl";
                btn.hitTestMode   = HitTestMode::Enabled;
                btn.minSize       = Vec2{ contentW - 8.0f - kLabelW - 12.0f, kRowH };
                btn.padding       = Vec2{ 6.0f, 4.0f };

                UIManager* popupUi = &popup->uiManager();
                btn.onClicked = [captureState, actionId, rowId, popupUi]() {
                    // Toggle capture mode
                    if (captureState->capturingId == actionId)
                    {
                        captureState->capturingId.clear();
                        ShortcutManager::Instance().setEnabled(true);
                        popupUi->clearKeyCaptureCallback();
                        // Restore button text
                        if (auto* b = popupUi->findElementById(rowId + ".Btn"))
                        {
                            const auto& acts = ShortcutManager::Instance().getActions();
                            for (const auto& a : acts)
                            {
                                if (a.id == actionId) { b->text = a.currentCombo.toString(); break; }
                            }
                            b->style.textColor = EditorTheme::Get().inputText;
                            popupUi->markAllWidgetsDirty();
                        }
                        return;
                    }
                    captureState->capturingId = actionId;
                    ShortcutManager::Instance().setEnabled(false);
                    if (auto* b = popupUi->findElementById(rowId + ".Btn"))
                    {
                        b->text = "Press key...";
                        b->style.textColor = Vec4{ 1.0f, 0.85f, 0.3f, 1.0f };
                        popupUi->markAllWidgetsDirty();
                    }

                    // Install key capture callback on the popup's UIManager
                    popupUi->setKeyCaptureCallback([captureState, actionId, rowId, popupUi](uint32_t sdlKey, uint16_t sdlMod) -> bool {
                        if (captureState->capturingId != actionId)
                            return false;

                        // Ignore bare modifier keys
                        if (sdlKey == SDLK_LCTRL || sdlKey == SDLK_RCTRL ||
                            sdlKey == SDLK_LSHIFT || sdlKey == SDLK_RSHIFT ||
                            sdlKey == SDLK_LALT || sdlKey == SDLK_RALT)
                            return true; // consume but don't finish

                        uint8_t mods = ShortcutManager::Mod::None;
                        if (sdlMod & (SDL_KMOD_LCTRL | SDL_KMOD_RCTRL))  mods |= ShortcutManager::Mod::Ctrl;
                        if (sdlMod & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)) mods |= ShortcutManager::Mod::Shift;
                        if (sdlMod & (SDL_KMOD_LALT | SDL_KMOD_RALT))    mods |= ShortcutManager::Mod::Alt;

                        ShortcutManager::KeyCombo newCombo{ sdlKey, mods };
                        auto& sm = ShortcutManager::Instance();

                        // Check for Escape â€” cancel capture
                        if (sdlKey == SDLK_ESCAPE && mods == 0)
                        {
                            captureState->capturingId.clear();
                            sm.setEnabled(true);
                            popupUi->clearKeyCaptureCallback();
                            if (auto* b = popupUi->findElementById(rowId + ".Btn"))
                            {
                                for (const auto& a : sm.getActions())
                                {
                                    if (a.id == actionId) { b->text = a.currentCombo.toString(); break; }
                                }
                                b->style.textColor = EditorTheme::Get().inputText;
                                popupUi->markAllWidgetsDirty();
                            }
                            return true;
                        }

                        // Check for conflicts
                        const auto& acts = sm.getActions();
                        ShortcutManager::Phase actionPhase = ShortcutManager::Phase::KeyDown;
                        for (const auto& a : acts) { if (a.id == actionId) { actionPhase = a.phase; break; } }
                        std::string conflict = sm.findConflict(newCombo, actionPhase, actionId);

                        sm.rebind(actionId, newCombo);
                        captureState->capturingId.clear();
                        sm.setEnabled(true);
                        popupUi->clearKeyCaptureCallback();

                        if (auto* b = popupUi->findElementById(rowId + ".Btn"))
                        {
                            b->text = newCombo.toString();
                            b->style.textColor = EditorTheme::Get().inputText;
                            popupUi->markAllWidgetsDirty();
                        }

                        if (!conflict.empty())
                        {
                            Logger::Instance().log(Logger::Category::Input,
                                "Shortcut conflict: " + newCombo.toString() + " was also bound to " + conflict,
                                Logger::LogLevel::WARNING);
                        }
                        return true;
                    });
                };

                row.children.push_back(btn);
                content.children.push_back(row);
                ++shortcutIdx;
            }

            // Reset All button
            {
                WidgetElement spacer;
                spacer.type    = WidgetElementType::Panel;
                spacer.id      = "EdS.SC.SpacerReset";
                spacer.style.color   = theme.transparent;
                spacer.minSize = Vec2{ contentW - 8.0f, 6.0f };
                content.children.push_back(spacer);

                WidgetElement resetBtn;
                resetBtn.type          = WidgetElementType::Button;
                resetBtn.id            = "EdS.SC.ResetAll";
                resetBtn.text          = "Reset All to Defaults";
                resetBtn.fontSize      = theme.fontSizeSmall;
                resetBtn.style.textColor     = Vec4{ 0.9f, 0.7f, 0.7f, 1.0f };
                resetBtn.textAlignH    = TextAlignH::Center;
                resetBtn.textAlignV    = TextAlignV::Center;
                resetBtn.style.color         = Vec4{ 0.3f, 0.15f, 0.15f, 0.8f };
                resetBtn.style.hoverColor    = Vec4{ 0.45f, 0.2f, 0.2f, 0.95f };
                resetBtn.shaderVertex  = "button_vertex.glsl";
                resetBtn.shaderFragment = "button_fragment.glsl";
                resetBtn.hitTestMode   = HitTestMode::Enabled;
                resetBtn.minSize       = Vec2{ EditorTheme::Scaled(180.0f), kRowH };
                resetBtn.padding       = Vec2{ 6.0f, 4.0f };

                UIManager* popupUi = &popup->uiManager();
                resetBtn.onClicked = [popupUi]() {
                    auto& sm = ShortcutManager::Instance();
                    sm.resetAllToDefaults();
                    // Update all keybind buttons
                    const auto& acts = sm.getActions();
                    int idx = 0;
                    for (const auto& a : acts)
                    {
                        std::string bid = "EdS.SC." + std::to_string(idx) + ".Btn";
                        if (auto* b = popupUi->findElementById(bid))
                        {
                            b->text = a.currentCombo.toString();
                        }
                        ++idx;
                    }
                    popupUi->markAllWidgetsDirty();
                };
                content.children.push_back(resetBtn);
            }
        }

        elements.push_back(content);
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("EditorSettingsWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("EditorSettings.Main", widget);
}

// ---------------------------------------------------------------------------------
// Shortcut Help Popup (F1 - lists all registered shortcuts)
// ---------------------------------------------------------------------------------
void UIManager::openShortcutHelpPopup()
{
    if (!m_renderer)
        return;

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(420.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(500.0f));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "ShortcutHelp", "Keyboard Shortcuts", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    auto& theme = EditorTheme::Get();
    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "SKH.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = theme.panelBackground;
        elements.push_back(bg);
    }

    const float kTitleH = EditorTheme::Scaled(40.0f);

    // Title bar
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "SKH.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.style.color = theme.titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "SKH.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(kTitleH) };
        titleText.text      = "Keyboard Shortcuts";
        titleText.fontSize  = theme.fontSizeHeading;
        titleText.style.textColor = theme.titleBarText;
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 8.0f, 0.0f };
        elements.push_back(titleText);
    }

    // Content area (scrollable stack)
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "SKH.Content";
        content.from        = Vec2{ nx(16.0f), ny(kTitleH + 12.0f) };
        content.to          = Vec2{ nx(W - 16.0f), ny(H - 16.0f) };
        content.style.color = theme.transparent;
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;

        const float contentW = W - 32.0f;
        const float kRowH = EditorTheme::Scaled(24.0f);
        const float kLabelW = EditorTheme::Scaled(180.0f);

        auto& sm = ShortcutManager::Instance();
        const auto& actions = sm.getActions();
        std::string lastCategory;

        for (size_t i = 0; i < actions.size(); ++i)
        {
            const auto& action = actions[i];

            if (action.category != lastCategory)
            {
                lastCategory = action.category;

                if (!content.children.empty())
                {
                    WidgetElement spacer;
                    spacer.type    = WidgetElementType::Panel;
                    spacer.id      = "SKH.Sp." + std::to_string(i);
                    spacer.style.color   = theme.transparent;
                    spacer.minSize = Vec2{ contentW - 8.0f, 4.0f };
                    content.children.push_back(spacer);
                }

                WidgetElement catLabel;
                catLabel.type      = WidgetElementType::Text;
                catLabel.id        = "SKH.Cat." + std::to_string(i);
                catLabel.text      = action.category;
                catLabel.fontSize  = theme.fontSizeSubheading;
                catLabel.style.textColor = Vec4{ 0.55f, 0.65f, 0.85f, 1.0f };
                catLabel.textAlignV = TextAlignV::Center;
                catLabel.minSize   = Vec2{ contentW - 8.0f, kRowH };
                catLabel.padding   = Vec2{ 2.0f, 2.0f };
                content.children.push_back(catLabel);

                WidgetElement sep;
                sep.type    = WidgetElementType::Panel;
                sep.id      = "SKH.Sep." + std::to_string(i);
                sep.style.color   = theme.panelBorder;
                sep.minSize = Vec2{ contentW - 8.0f, 1.0f };
                content.children.push_back(sep);
            }

            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = "SKH.R." + std::to_string(i);
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - 8.0f, kRowH };
            row.style.color = theme.transparent;
            row.padding     = Vec2{ 4.0f, 1.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = "SKH.L." + std::to_string(i);
            lbl.text      = action.displayName;
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            WidgetElement key;
            key.type      = WidgetElementType::Text;
            key.id        = "SKH.K." + std::to_string(i);
            key.text      = action.currentCombo.toString();
            key.fontSize  = theme.fontSizeSmall;
            key.style.textColor = Vec4{ 0.7f, 0.85f, 1.0f, 1.0f };
            key.textAlignV = TextAlignV::Center;
            key.textAlignH = TextAlignH::Right;
            key.minSize   = Vec2{ contentW - 8.0f - kLabelW - 12.0f, kRowH };
            row.children.push_back(key);

            content.children.push_back(row);
        }

        elements.push_back(content);
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("ShortcutHelpWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("ShortcutHelp.Main", widget);
}

// ---------------------------------------------------------------------------------
// Notification History Popup
// ---------------------------------------------------------------------------------
void UIManager::openNotificationHistoryPopup()
{
    if (!m_renderer)
        return;

    // Clear unread count on open
    m_unreadNotificationCount = 0;
    refreshNotificationBadge();

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(440.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(480.0f));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "NotificationHistory", "Notification History", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    auto& theme = EditorTheme::Get();
    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "NH.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = theme.panelBackground;
        elements.push_back(bg);
    }

    const float kTitleH = EditorTheme::Scaled(40.0f);

    // Title bar
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "NH.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.style.color = theme.titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "NH.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(kTitleH) };
        titleText.text      = "Notification History";
        titleText.fontSize  = theme.fontSizeHeading;
        titleText.style.textColor = theme.titleBarText;
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 8.0f, 0.0f };
        elements.push_back(titleText);
    }

    // Content area (scrollable)
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "NH.Content";
        content.from        = Vec2{ nx(12.0f), ny(kTitleH + 8.0f) };
        content.to          = Vec2{ nx(W - 12.0f), ny(H - 12.0f) };
        content.style.color = theme.transparent;
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;

        const float contentW = W - 24.0f;
        const float kRowH = EditorTheme::Scaled(28.0f);

        if (m_notificationHistory.empty())
        {
            WidgetElement emptyLabel;
            emptyLabel.type      = WidgetElementType::Text;
            emptyLabel.id        = "NH.Empty";
            emptyLabel.text      = "No notifications yet.";
            emptyLabel.fontSize  = theme.fontSizeBody;
            emptyLabel.style.textColor = theme.textMuted;
            emptyLabel.textAlignV = TextAlignV::Center;
            emptyLabel.textAlignH = TextAlignH::Center;
            emptyLabel.minSize   = Vec2{ contentW - 8.0f, EditorTheme::Scaled(40.0f) };
            content.children.push_back(emptyLabel);
        }
        else
        {
            for (size_t i = 0; i < m_notificationHistory.size(); ++i)
            {
                const auto& entry = m_notificationHistory[i];

                // Level accent color
                Vec4 levelColor = Vec4{ 0.4f, 0.6f, 0.9f, 1.0f }; // info
                switch (entry.level)
                {
                case NotificationLevel::Error:   levelColor = theme.errorColor;   break;
                case NotificationLevel::Warning: levelColor = theme.warningColor; break;
                case NotificationLevel::Success: levelColor = theme.successColor; break;
                default: break;
                }

                WidgetElement row;
                row.type        = WidgetElementType::StackPanel;
                row.id          = "NH.R." + std::to_string(i);
                row.orientation = StackOrientation::Horizontal;
                row.minSize     = Vec2{ contentW - 8.0f, kRowH };
                row.style.color = (i % 2 == 0)
                    ? Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }
                    : Vec4{ 1.0f, 1.0f, 1.0f, 0.03f };
                row.padding     = Vec2{ 4.0f, 2.0f };

                // Level indicator dot
                WidgetElement dot;
                dot.type    = WidgetElementType::Panel;
                dot.id      = "NH.D." + std::to_string(i);
                dot.minSize = Vec2{ 6.0f, 6.0f };
                dot.style.color   = levelColor;
                dot.style.borderRadius = 3.0f;
                dot.padding = Vec2{ 2.0f, static_cast<float>((kRowH - 6.0f) * 0.5f) };
                row.children.push_back(dot);

                // Timestamp
                const uint64_t elapsed = SDL_GetTicks() - entry.timestampMs;
                std::string timeStr;
                if (elapsed < 60000)
                    timeStr = std::to_string(elapsed / 1000) + "s ago";
                else if (elapsed < 3600000)
                    timeStr = std::to_string(elapsed / 60000) + "m ago";
                else
                    timeStr = std::to_string(elapsed / 3600000) + "h ago";

                WidgetElement timeLabel;
                timeLabel.type      = WidgetElementType::Text;
                timeLabel.id        = "NH.T." + std::to_string(i);
                timeLabel.text      = timeStr;
                timeLabel.fontSize  = theme.fontSizeSmall;
                timeLabel.style.textColor = theme.textMuted;
                timeLabel.textAlignV = TextAlignV::Center;
                timeLabel.minSize   = Vec2{ EditorTheme::Scaled(56.0f), kRowH };
                timeLabel.padding   = Vec2{ 4.0f, 0.0f };
                row.children.push_back(timeLabel);

                // Message
                WidgetElement msg;
                msg.type      = WidgetElementType::Text;
                msg.id        = "NH.M." + std::to_string(i);
                msg.text      = entry.message;
                msg.fontSize  = theme.fontSizeBody;
                msg.style.textColor = theme.textPrimary;
                msg.textAlignV = TextAlignV::Center;
                msg.fillX     = true;
                msg.minSize   = Vec2{ 0.0f, kRowH };
                msg.padding   = Vec2{ 4.0f, 0.0f };
                row.children.push_back(msg);

                content.children.push_back(row);
            }
        }

        elements.push_back(content);
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("NotificationHistoryWidget");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("NotificationHistory.Main", widget);
}

void UIManager::openAssetReferencesPopup(const std::string& title, const std::string& assetPath,
    const std::vector<std::pair<std::string, std::string>>& items)
{
    if (!m_renderer) return;

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(500.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(420.0f));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "AssetReferences", title, kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    auto& theme = EditorTheme::Get();
    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "AR.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = theme.panelBackground;
        elements.push_back(bg);
    }

    const float kTitleH = EditorTheme::Scaled(40.0f);

    // Title bar
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "AR.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.style.color = theme.titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "AR.TitleText";
        titleText.from      = Vec2{ nx(8.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 8.0f), ny(kTitleH) };
        titleText.text      = title;
        titleText.fontSize  = theme.fontSizeHeading;
        titleText.style.textColor = theme.titleBarText;
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 8.0f, 0.0f };
        elements.push_back(titleText);
    }

    // Subtitle: asset name
    const float kSubH = EditorTheme::Scaled(24.0f);
    {
        WidgetElement sub;
        sub.type      = WidgetElementType::Text;
        sub.id        = "AR.Sub";
        sub.from      = Vec2{ nx(12.0f), ny(kTitleH + 4.0f) };
        sub.to        = Vec2{ nx(W - 12.0f), ny(kTitleH + 4.0f + kSubH) };
        sub.text      = assetPath;
        sub.fontSize  = theme.fontSizeSmall;
        sub.style.textColor = theme.textMuted;
        sub.textAlignV = TextAlignV::Center;
        sub.padding   = Vec2{ 4.0f, 0.0f };
        elements.push_back(sub);
    }

    // Content area (scrollable)
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "AR.Content";
        content.from        = Vec2{ nx(12.0f), ny(kTitleH + kSubH + 12.0f) };
        content.to          = Vec2{ nx(W - 12.0f), ny(H - 12.0f) };
        content.style.color = theme.transparent;
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;

        const float contentW = W - 24.0f;
        const float kRowH = EditorTheme::Scaled(26.0f);

        if (items.empty())
        {
            WidgetElement emptyLabel;
            emptyLabel.type      = WidgetElementType::Text;
            emptyLabel.id        = "AR.Empty";
            emptyLabel.text      = "No references found.";
            emptyLabel.fontSize  = theme.fontSizeBody;
            emptyLabel.style.textColor = theme.textMuted;
            emptyLabel.textAlignV = TextAlignV::Center;
            emptyLabel.textAlignH = TextAlignH::Center;
            emptyLabel.minSize   = Vec2{ contentW - 8.0f, EditorTheme::Scaled(40.0f) };
            content.children.push_back(emptyLabel);
        }
        else
        {
            for (size_t i = 0; i < items.size(); ++i)
            {
                const auto& [path, type] = items[i];

                WidgetElement row;
                row.type        = WidgetElementType::StackPanel;
                row.id          = "AR.R." + std::to_string(i);
                row.orientation = StackOrientation::Horizontal;
                row.minSize     = Vec2{ contentW - 8.0f, kRowH };
                row.style.color = (i % 2 == 0)
                    ? Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }
                    : Vec4{ 1.0f, 1.0f, 1.0f, 0.03f };
                row.padding     = Vec2{ 4.0f, 2.0f };

                // Type badge
                WidgetElement badge;
                badge.type      = WidgetElementType::Text;
                badge.id        = "AR.B." + std::to_string(i);
                badge.text      = "[" + type + "]";
                badge.fontSize  = theme.fontSizeSmall;
                badge.style.textColor = theme.accent;
                badge.textAlignV = TextAlignV::Center;
                badge.minSize   = Vec2{ EditorTheme::Scaled(100.0f), kRowH };
                badge.padding   = Vec2{ 2.0f, 0.0f };
                row.children.push_back(badge);

                // Path / name
                WidgetElement pathLabel;
                pathLabel.type      = WidgetElementType::Text;
                pathLabel.id        = "AR.P." + std::to_string(i);
                pathLabel.text      = path;
                pathLabel.fontSize  = theme.fontSizeBody;
                pathLabel.style.textColor = theme.textPrimary;
                pathLabel.textAlignV = TextAlignV::Center;
                pathLabel.fillX     = true;
                pathLabel.minSize   = Vec2{ 0.0f, kRowH };
                pathLabel.padding   = Vec2{ 4.0f, 0.0f };
                row.children.push_back(pathLabel);

                content.children.push_back(row);
            }
        }

        elements.push_back(content);
    }

    auto arWidget = std::make_shared<EditorWidget>();
    arWidget->setName("AssetReferencesWidget");
    arWidget->setFillX(true);
    arWidget->setFillY(true);
    arWidget->setElements(std::move(elements));
    popup->uiManager().registerWidget("AssetReferences.Main", arWidget);
}

void UIManager::refreshNotificationBadge()
{
    auto* entry = findWidgetEntry("StatusBar");
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    WidgetElement* notifBtn = FindElementById(elements, "StatusBar.Notifications");
    if (!notifBtn)
        return;

    if (m_unreadNotificationCount > 0)
    {
        std::string badge = "\xF0\x9F\x94\x94 " + std::to_string(m_unreadNotificationCount);
        notifBtn->text = badge;
        notifBtn->style.textColor = EditorTheme::Get().warningColor;
    }
    else
    {
        notifBtn->text = "\xF0\x9F\x94\x94";
        notifBtn->style.textColor = Vec4{ 0.7f, 0.7f, 0.75f, 1.0f };
    }

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ---------------------------------------------------------------------------------
// Non-blocking Progress Bars
// ---------------------------------------------------------------------------------
UIManager::ProgressBarHandle UIManager::beginProgress(const std::string& label, float total)
{
    ProgressEntry pe;
    pe.id = m_nextProgressId++;
    pe.label = label;
    pe.current = 0.0f;
    pe.total = std::max(0.01f, total);
    m_progressBars.push_back(std::move(pe));

    showToastMessage(label + "...", 1.5f, NotificationLevel::Info);

    ProgressBarHandle handle;
    handle.id = m_progressBars.back().id;
    return handle;
}

void UIManager::updateProgress(ProgressBarHandle handle, float current, const std::string& label)
{
    for (auto& pb : m_progressBars)
    {
        if (pb.id == handle.id)
        {
            pb.current = current;
            if (!label.empty())
                pb.label = label;
            break;
        }
    }
}

void UIManager::endProgress(ProgressBarHandle handle)
{
    for (auto it = m_progressBars.begin(); it != m_progressBars.end(); ++it)
    {
        if (it->id == handle.id)
        {
            showToastMessage(it->label + " complete", 2.0f, NotificationLevel::Success);
            m_progressBars.erase(it);
            break;
        }
    }
}

// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
// Project Selection Screen
// Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
void UIManager::openProjectScreen(std::function<void(const std::string& projectPath, bool isNew, bool setAsDefault, bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI)> onProjectChosen)
{
    if (!m_renderer)
        return;

    if (findWidgetEntry("ProjectScreen.Main"))
        return;

    const float kScreenW = EditorTheme::Scaled(720.0f);
    const float kScreenH = EditorTheme::Scaled(540.0f);
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
    const float kSidebarW = EditorTheme::Scaled(170.0f);
    const float kTitleH = EditorTheme::Scaled(48.0f);
    const float kFooterH = EditorTheme::Scaled(40.0f);

    std::vector<WidgetElement> elements;

    // Ã¢â€â‚¬Ã¢â€â‚¬ Background Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    {
        WidgetElement bg;
        bg.type  = WidgetElementType::Panel;
        bg.id    = "PS.Bg";
        bg.from  = Vec2{ 0.0f, 0.0f };
        bg.to    = Vec2{ 1.0f, 1.0f };
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(bg);
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ Title bar (accent stripe) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    {
        WidgetElement titleBg;
        titleBg.type  = WidgetElementType::Panel;
        titleBg.id    = "PS.TitleBg";
        titleBg.from  = Vec2{ 0.0f, 0.0f };
        titleBg.to    = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.style.color = EditorTheme::Get().titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement accent;
        accent.type  = WidgetElementType::Panel;
        accent.id    = "PS.TitleAccent";
        accent.from  = Vec2{ 0.0f, ny(kTitleH - 2.0f) };
        accent.to    = Vec2{ 1.0f, ny(kTitleH) };
        accent.style.color = EditorTheme::Get().accent;

        WidgetElement titleText;
        titleText.type      = WidgetElementType::Text;
        titleText.id        = "PS.TitleText";
        titleText.from      = Vec2{ nx(12.0f), 0.0f };
        titleText.to        = Vec2{ nx(W - 46.0f), ny(kTitleH - 2.0f) };
        titleText.text      = "HorizonEngine  Ã¢â‚¬â€  Project Selection";
        titleText.fontSize  = EditorTheme::Get().fontSizeHeading;
        titleText.style.textColor = EditorTheme::Get().buttonPrimaryText;
        titleText.textAlignV = TextAlignV::Center;
        titleText.padding   = Vec2{ 6.0f, 0.0f };
        elements.push_back(titleText);

        WidgetElement closeBtn;
        closeBtn.type  = WidgetElementType::Button;
        closeBtn.id    = "PS.TitleBar.Close";
        closeBtn.from  = Vec2{ nx(W - 46.0f), 0.0f };
        closeBtn.to    = Vec2{ 1.0f, ny(kTitleH - 2.0f) };
        closeBtn.text  = "X";
        closeBtn.fontSize = EditorTheme::Get().fontSizeMonospace;
        closeBtn.textAlignH = TextAlignH::Center;
        closeBtn.textAlignV = TextAlignV::Center;
        closeBtn.style.color = Vec4{ 0.1f, 0.1f, 0.1f, 1.0f };
        closeBtn.style.hoverColor = Vec4{ 0.7f, 0.15f, 0.15f, 1.0f };
        closeBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        closeBtn.shaderVertex = "button_vertex.glsl";
        closeBtn.shaderFragment = "button_fragment.glsl";
        closeBtn.hitTestMode = HitTestMode::Enabled;
        closeBtn.onClicked = [screenMgr]()
        {
            DiagnosticsManager::Instance().requestShutdown();
            if (screenMgr)
                screenMgr->unregisterWidget("ProjectScreen.Main");
        };
        elements.push_back(closeBtn);
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ Sidebar Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    {
        WidgetElement sidebarBg;
        sidebarBg.type  = WidgetElementType::Panel;
        sidebarBg.id    = "PS.SidebarBg";
        sidebarBg.from  = Vec2{ 0.0f, ny(kTitleH) };
        sidebarBg.to    = Vec2{ nx(kSidebarW), 1.0f };
        sidebarBg.style.color = EditorTheme::Get().panelBackgroundAlt;
        elements.push_back(sidebarBg);

        WidgetElement sep;
        sep.type  = WidgetElementType::Panel;
        sep.id    = "PS.SidebarSep";
        sep.from  = Vec2{ nx(kSidebarW - 1.0f), ny(kTitleH) };
        sep.to    = Vec2{ nx(kSidebarW), 1.0f };
        sep.style.color = EditorTheme::Get().panelBorder;
        elements.push_back(sep);
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ Footer background (for checkbox) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    {
        WidgetElement footerSep;
        footerSep.type  = WidgetElementType::Panel;
        footerSep.id    = "PS.FooterSep";
        footerSep.from  = Vec2{ nx(kSidebarW), ny(H - kFooterH - 1.0f) };
        footerSep.to    = Vec2{ 1.0f, ny(H - kFooterH) };
        footerSep.style.color = EditorTheme::Get().panelBorder;
        elements.push_back(footerSep);

        WidgetElement footerBg;
        footerBg.type  = WidgetElementType::Panel;
        footerBg.id    = "PS.FooterBg";
        footerBg.from  = Vec2{ nx(kSidebarW), ny(H - kFooterH) };
        footerBg.to    = Vec2{ 1.0f, 1.0f };
        footerBg.style.color = EditorTheme::Get().panelBackgroundAlt;
        elements.push_back(footerBg);
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ "Set as default" checkbox (always visible in footer) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    {
        WidgetElement cb;
        cb.type          = WidgetElementType::CheckBox;
        cb.id            = "PS.DefaultCB";
        cb.text          = "Set as default project (skip this screen on next start)";
        cb.fontSize      = EditorTheme::Get().fontSizeMonospace;
        cb.isChecked     = state->setAsDefault;
        cb.style.color         = EditorTheme::Get().checkboxDefault;
        cb.style.hoverColor    = EditorTheme::Get().checkboxHover;
        cb.style.fillColor     = EditorTheme::Get().checkboxChecked;
        cb.style.textColor     = EditorTheme::Get().textPrimary;
        cb.padding       = Vec2{ 8.0f, 4.0f };
        cb.hitTestMode = HitTestMode::Enabled;
        cb.from          = Vec2{ nx(kSidebarW + 12.0f), ny(H - kFooterH + 4.0f) };
        cb.to            = Vec2{ nx(W - 12.0f), ny(H - 4.0f) };
        cb.onCheckedChanged = [state](bool v) { state->setAsDefault = v; };
        elements.push_back(cb);
    }

    const float kCatBtnH = EditorTheme::Scaled(34.0f);
    const float kCatBtnGap = EditorTheme::Scaled(2.0f);
    const float kCatBtnY0 = kTitleH + EditorTheme::Scaled(10.0f);

    auto callbackPtr = std::make_shared<std::function<void(const std::string&, bool, bool, bool, DiagnosticsManager::RHIType)>>(std::move(onProjectChosen));
    auto closeScreen = [screenMgr]()
    {
        if (screenMgr)
        {
            screenMgr->unregisterWidget("ProjectScreen.Main");
        }
    };
    auto rebuildContentPtr = std::make_shared<std::function<void()>>();

    // Ã¢â€â‚¬Ã¢â€â‚¬ Content builder Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    *rebuildContentPtr = [state, screenMgr, hostWindow, categories, nx, ny, W, H, kSidebarW, kTitleH, kFooterH, renderer, callbackPtr, closeScreen, rebuildContentPtr]()
    {
        auto& pMgr = *screenMgr;
        auto* entry = pMgr.findElementById("PS.ContentArea");
        if (!entry) return;

        entry->children.clear();

        const float kRowH = EditorTheme::Scaled(32.0f);
        const float kContentPad = EditorTheme::Scaled(16.0f);
        const float contentW = W - kSidebarW;

        const auto addSectionLabel = [&](const std::string& id, const std::string& label)
        {
            WidgetElement sec;
            sec.type      = WidgetElementType::Text;
            sec.id        = id;
            sec.text      = label;
            sec.fontSize  = EditorTheme::Get().fontSizeSubheading;
            sec.style.textColor = EditorTheme::Get().textLink;
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
            sep.style.color   = EditorTheme::Get().panelBorder;
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
            btn.fontSize      = EditorTheme::Get().fontSizeSubheading;
            btn.style.color         = bgColor;
            btn.style.hoverColor    = hoverColor;
            btn.style.textColor     = EditorTheme::Get().textPrimary;
            btn.textAlignH    = TextAlignH::Center;
            btn.textAlignV    = TextAlignV::Center;
            btn.padding       = Vec2{ EditorTheme::Scaled(16.0f), EditorTheme::Scaled(8.0f) };
            btn.hitTestMode = HitTestMode::Enabled;
            btn.minSize       = Vec2{ contentW - kContentPad * 2.0f, EditorTheme::Scaled(38.0f) };
            btn.shaderVertex   = "button_vertex.glsl";
            btn.shaderFragment = "button_fragment.glsl";
            btn.onClicked     = std::move(onClick);
            entry->children.push_back(btn);
        };

        const auto addSmallButton = [&](const std::string& id, const std::string& label,
            std::function<void()> onClick)
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement btn;
            btn.type          = WidgetElementType::Button;
            btn.id            = id;
            btn.text          = label;
            btn.fontSize      = theme.fontSizeSmall;
            btn.style.color         = theme.buttonDefault;
            btn.style.hoverColor    = theme.buttonHover;
            btn.style.textColor     = theme.textPrimary;
            btn.textAlignH    = TextAlignH::Center;
            btn.textAlignV    = TextAlignV::Center;
            btn.padding       = Vec2{ 12.0f, 6.0f };
            btn.hitTestMode = HitTestMode::Enabled;
            btn.minSize       = Vec2{ EditorTheme::Scaled(160.0f), EditorTheme::Scaled(30.0f) };
            btn.shaderVertex   = "button_vertex.glsl";
            btn.shaderFragment = "button_fragment.glsl";
            btn.onClicked     = std::move(onClick);
            entry->children.push_back(btn);
        };

        const auto addEntryRow = [&](const std::string& id, const std::string& label,
            const std::string& value, std::function<void(const std::string&)> onChange)
        {
            const auto& theme = EditorTheme::Get();
            WidgetElement rowPanel;
            rowPanel.type        = WidgetElementType::StackPanel;
            rowPanel.id          = id + ".Row";
            rowPanel.orientation = StackOrientation::Horizontal;
            rowPanel.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
            rowPanel.style.color       = theme.transparent;
            rowPanel.padding     = Vec2{ 6.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textSecondary;
            lbl.textAlignV = TextAlignV::Center;
            lbl.minSize   = Vec2{ EditorTheme::Scaled(120.0f), kRowH };
            rowPanel.children.push_back(lbl);

            WidgetElement eb;
            eb.type          = WidgetElementType::EntryBar;
            eb.id            = id;
            eb.value         = value;
            eb.fontSize      = theme.fontSizeBody;
            eb.style.color         = theme.inputBackground;
            eb.style.hoverColor    = theme.buttonHover;
            eb.style.textColor     = theme.inputText;
            eb.padding       = Vec2{ 8.0f, 5.0f };
            eb.hitTestMode = HitTestMode::Enabled;
            eb.minSize       = Vec2{ contentW - kContentPad * 2.0f - EditorTheme::Scaled(120.0f) - EditorTheme::Scaled(12.0f), kRowH };
            eb.onValueChanged = std::move(onChange);
            rowPanel.children.push_back(eb);

            entry->children.push_back(rowPanel);
        };

        // Ã¢â€â‚¬Ã¢â€â‚¬ Category: Recent Projects Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
                noProjects.fontSize  = EditorTheme::Get().fontSizeBody;
                noProjects.style.textColor = EditorTheme::Get().textMuted;
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
                    hdrRow.style.color       = EditorTheme::Get().panelBackgroundAlt;
                    hdrRow.padding     = Vec2{ 18.0f, 2.0f };

                    WidgetElement hdrName;
                    hdrName.type      = WidgetElementType::Text;
                    hdrName.id        = "PS.C.ListHdr.Name";
                    hdrName.text      = "Project";
                    hdrName.fontSize  = EditorTheme::Get().fontSizeSmall;
                    hdrName.style.textColor = EditorTheme::Get().textMuted;
                    hdrName.textAlignV = TextAlignV::Center;
                    hdrName.minSize   = Vec2{ 180.0f, 24.0f };
                    hdrRow.children.push_back(hdrName);

                    WidgetElement hdrPath;
                    hdrPath.type      = WidgetElementType::Text;
                    hdrPath.id        = "PS.C.ListHdr.Path";
                    hdrPath.text      = "Location";
                    hdrPath.fontSize  = EditorTheme::Get().fontSizeSmall;
                    hdrPath.style.textColor = EditorTheme::Get().textMuted;
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

                    // Outer row container Ã¢â‚¬â€œ holds accent bar + content
                    WidgetElement row;
                    row.type        = WidgetElementType::StackPanel;
                    row.id          = rowId + ".Row";
                    row.orientation = StackOrientation::Horizontal;
                    row.minSize     = Vec2{ contentW - kContentPad * 2.0f, EditorTheme::Scaled(44.0f) };
                    row.style.color       = isEven
                        ? EditorTheme::Get().treeRowEven
                        : EditorTheme::Get().treeRowOdd;
                    row.style.hoverColor  = exists
                        ? EditorTheme::Get().selectionHighlightHover
                        : row.style.color;
                    row.padding     = Vec2{ 0.0f, 0.0f };
                    row.hitTestMode = exists ? HitTestMode::Enabled : HitTestMode::DisabledSelf;

                    // Left accent bar
                    WidgetElement accent;
                    accent.type    = WidgetElementType::Panel;
                    accent.id      = rowId + ".Accent";
                    accent.style.color   = exists
                        ? EditorTheme::Get().accent
                        : Vec4{ EditorTheme::Get().errorColor.x, EditorTheme::Get().errorColor.y, EditorTheme::Get().errorColor.z, 0.6f };
                    accent.minSize = Vec2{ EditorTheme::Scaled(4.0f), EditorTheme::Scaled(44.0f) };
                    row.children.push_back(accent);

                    // Project name column
                    WidgetElement nameCol;
                    nameCol.type      = WidgetElementType::Text;
                    nameCol.id        = rowId + ".Name";
                    nameCol.text      = projName;
                    nameCol.fontSize  = EditorTheme::Get().fontSizeSubheading;
                    nameCol.style.textColor = exists
                        ? EditorTheme::Get().textPrimary
                        : Vec4{ EditorTheme::Get().errorColor.x, 0.38f, 0.38f, 1.0f };
                    nameCol.textAlignV = TextAlignV::Center;
                    nameCol.padding   = Vec2{ 12.0f, 0.0f };
                    nameCol.minSize   = Vec2{ EditorTheme::Scaled(180.0f), EditorTheme::Scaled(44.0f) };
                    row.children.push_back(nameCol);

                    // Path column
                    WidgetElement pathCol;
                    pathCol.type      = WidgetElementType::Text;
                    pathCol.id        = rowId + ".Path";
                    pathCol.text      = exists ? projPath : (projPath + "  (not found)");
                    pathCol.fontSize  = EditorTheme::Get().fontSizeSmall;
                    pathCol.style.textColor = exists
                        ? EditorTheme::Get().textMuted
                        : Vec4{ 0.45f, 0.30f, 0.30f, 0.8f };
                    pathCol.textAlignV = TextAlignV::Center;
                    pathCol.fillX     = true;
                    pathCol.padding   = Vec2{ 4.0f, 0.0f };
                    pathCol.minSize   = Vec2{ 0.0f, EditorTheme::Scaled(44.0f) };
                    row.children.push_back(pathCol);

                    WidgetElement removeBtn;
                    removeBtn.type = WidgetElementType::Button;
                    removeBtn.id = rowId + ".Remove";
                    removeBtn.text = "X";
                    removeBtn.fontSize = EditorTheme::Get().fontSizeSmall;
                    removeBtn.style.color = EditorTheme::Get().buttonDanger;
                    removeBtn.style.hoverColor = EditorTheme::Get().buttonDangerHover;
                    removeBtn.style.textColor = EditorTheme::Get().buttonDangerText;
                    removeBtn.textAlignH = TextAlignH::Center;
                    removeBtn.textAlignV = TextAlignV::Center;
                    removeBtn.padding = Vec2{ 0.0f, 0.0f };
                    removeBtn.fillY = true;
                    removeBtn.minSize = Vec2{ EditorTheme::Scaled(44.0f), EditorTheme::Scaled(44.0f) };
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
                        rowSep.style.color   = Vec4{ 0.22f, 0.24f, 0.30f, 1.0f };
                        rowSep.minSize = Vec2{ contentW - kContentPad * 2.0f, 1.0f };
                        entry->children.push_back(rowSep);
                    }
                }
            }
        }
        // Ã¢â€â‚¬Ã¢â€â‚¬ Category: Open Project Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
        else if (state->activeCategory == 1)
        {
            addSectionLabel("PS.C.Sec.Open", "Open Existing Project");
            addSeparator("PS.C.Sep.Open");

            WidgetElement desc;
            desc.type      = WidgetElementType::Text;
            desc.id        = "PS.C.OpenDesc";
            desc.text      = "Select a .project file to open an existing project.";
            desc.fontSize  = EditorTheme::Get().fontSizeBody;
            desc.style.textColor = EditorTheme::Get().textSecondary;
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
        // Ã¢â€â‚¬Ã¢â€â‚¬ Category: New Project Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
                preview.fontSize  = EditorTheme::Get().fontSizeSmall;
                preview.style.textColor = EditorTheme::Get().textLink;
                preview.padding   = Vec2{ 10.0f, 6.0f };
                preview.minSize   = Vec2{ contentW - kContentPad * 2.0f, EditorTheme::Scaled(26.0f) };
                entry->children.push_back(preview);
            }

            {
                WidgetElement spacer;
                spacer.type    = WidgetElementType::Panel;
                spacer.id      = "PS.C.Spacer.New";
                spacer.style.color   = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.minSize = Vec2{ contentW - kContentPad * 2.0f, 8.0f };
                entry->children.push_back(spacer);
            }

            {
                WidgetElement includeDefaultContentCb;
                includeDefaultContentCb.type = WidgetElementType::CheckBox;
                includeDefaultContentCb.id = "PS.C.IncludeDefaultContent";
                includeDefaultContentCb.text = "Include default content";
                includeDefaultContentCb.fontSize = EditorTheme::Get().fontSizeMonospace;
                includeDefaultContentCb.isChecked = state->includeDefaultContent;
                includeDefaultContentCb.style.color = EditorTheme::Get().checkboxDefault;
                includeDefaultContentCb.style.hoverColor = EditorTheme::Get().checkboxHover;
                includeDefaultContentCb.style.fillColor = EditorTheme::Get().checkboxChecked;
                includeDefaultContentCb.style.textColor = EditorTheme::Get().textPrimary;
                includeDefaultContentCb.padding = Vec2{ 8.0f, 4.0f };
                includeDefaultContentCb.hitTestMode = HitTestMode::Enabled;
                includeDefaultContentCb.minSize = Vec2{ contentW - kContentPad * 2.0f, EditorTheme::Scaled(24.0f) };
                includeDefaultContentCb.onCheckedChanged = [state](bool v)
                {
                    state->includeDefaultContent = v;
                };
                entry->children.push_back(includeDefaultContentCb);
            }

            // Ã¢â€â‚¬Ã¢â€â‚¬ RHI Type dropdown Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
            {
                WidgetElement rhiRow;
                rhiRow.type        = WidgetElementType::StackPanel;
                rhiRow.id          = "PS.C.RHI.Row";
                rhiRow.orientation = StackOrientation::Horizontal;
                rhiRow.minSize     = Vec2{ contentW - kContentPad * 2.0f, kRowH };
                rhiRow.style.color       = EditorTheme::Get().transparent;
                rhiRow.padding     = Vec2{ 6.0f, 2.0f };

                WidgetElement rhiLbl;
                rhiLbl.type      = WidgetElementType::Text;
                rhiLbl.id        = "PS.C.RHI.Lbl";
                rhiLbl.text      = "Graphics API";
                rhiLbl.fontSize  = EditorTheme::Get().fontSizeBody;
                rhiLbl.style.textColor = EditorTheme::Get().textSecondary;
                rhiLbl.textAlignV = TextAlignV::Center;
                rhiLbl.minSize   = Vec2{ EditorTheme::Scaled(120.0f), kRowH };
                rhiRow.children.push_back(rhiLbl);

                WidgetElement rhiDd;
                rhiDd.type          = WidgetElementType::DropDown;
                rhiDd.id            = "PS.C.RHI";
                rhiDd.items         = { "OpenGL" };
                rhiDd.selectedIndex = state->selectedRHI;
                rhiDd.fontSize      = EditorTheme::Get().fontSizeMonospace;
                rhiDd.style.color         = EditorTheme::Get().inputBackground;
                rhiDd.style.hoverColor    = EditorTheme::Get().inputBackgroundHover;
                rhiDd.style.textColor     = EditorTheme::Get().textPrimary;
                rhiDd.padding       = Vec2{ 6.0f, 4.0f };
                rhiDd.hitTestMode = HitTestMode::Enabled;
                rhiDd.minSize       = Vec2{ contentW - kContentPad * 2.0f - EditorTheme::Scaled(120.0f) - EditorTheme::Scaled(12.0f), kRowH };
                rhiDd.onSelectionChanged = [state](int idx)
                {
                    state->selectedRHI = idx;
                };
                rhiRow.children.push_back(rhiDd);

                entry->children.push_back(rhiRow);
            }

            addActionButton("PS.C.CreateBtn", "Create Project",
                EditorTheme::Get().accentGreen,
                Vec4{ 0.40f, 0.78f, 0.44f, 1.0f },
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
                catBtn->style.color     = active
                    ? EditorTheme::Get().selectionHighlight
                    : EditorTheme::Get().transparent;
                catBtn->style.textColor = active
                    ? EditorTheme::Get().textPrimary
                    : EditorTheme::Get().textSecondary;
            }
        }

        pMgr.markAllWidgetsDirty();
    };

    // Ã¢â€â‚¬Ã¢â€â‚¬ Sidebar category buttons Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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
        catBtn.fontSize      = EditorTheme::Get().fontSizeBody;
        catBtn.style.color         = active
            ? EditorTheme::Get().selectionHighlight
            : EditorTheme::Get().transparent;
        catBtn.style.hoverColor    = EditorTheme::Get().buttonSubtleHover;
        catBtn.style.textColor     = active
            ? EditorTheme::Get().textPrimary
            : EditorTheme::Get().textSecondary;
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Content area (above footer) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    {
        WidgetElement content;
        content.type        = WidgetElementType::StackPanel;
        content.id          = "PS.ContentArea";
        content.from        = Vec2{ nx(kSidebarW + 16.0f), ny(kTitleH + 12.0f) };
        content.to          = Vec2{ nx(W - 12.0f), ny(H - kFooterH - 6.0f) };
        content.style.color       = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        content.orientation = StackOrientation::Vertical;
        content.padding     = Vec2{ 4.0f, 4.0f };
        content.scrollable  = true;
        elements.push_back(content);
    }

    auto widget = std::make_shared<EditorWidget>();
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
// Widget Editor: Animation Timeline
// ===========================================================================

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

void UIManager::refreshWidgetEditorTimeline(const std::string& tabId)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end())
        return;

    auto& edState = stateIt->second;
    const std::string& bottomId = edState.bottomWidgetId;

    // If panel is hidden, remove the bottom widget entirely
    if (!edState.showAnimationsPanel)
    {
        unregisterWidget(bottomId);
        markAllWidgetsDirty();
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
                auto it = m_widgetEditorStates.find(capturedTabId);
                if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                    return;
                auto& anims = it->second.editedWidget->getAnimationsMutable();
                std::string newName = "Anim_" + std::to_string(anims.size());
                WidgetAnimation newAnim;
                newAnim.name = newName;
                newAnim.duration = 1.0f;
                anims.push_back(std::move(newAnim));
                it->second.selectedAnimationName = newName;
                markWidgetEditorDirty(capturedTabId);
                refreshWidgetEditorTimeline(capturedTabId);
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
                    auto it = m_widgetEditorStates.find(capturedTabId2);
                    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                        return;
                    auto& anims2 = it->second.editedWidget->getAnimationsMutable();
                    anims2.erase(std::remove_if(anims2.begin(), anims2.end(),
                        [&](const WidgetAnimation& a) { return a.name == animName; }), anims2.end());
                    if (it->second.selectedAnimationName == animName)
                        it->second.selectedAnimationName.clear();
                    markWidgetEditorDirty(capturedTabId2);
                    refreshWidgetEditorTimeline(capturedTabId2);
                };
                row.children.push_back(std::move(delBtn));

                // Click to select
                const std::string capturedTabId3 = tabId;
                const std::string capturedAnimName = anim.name;
                row.onClicked = [this, capturedTabId3, capturedAnimName]()
                {
                    auto it = m_widgetEditorStates.find(capturedTabId3);
                    if (it != m_widgetEditorStates.end())
                    {
                        it->second.selectedAnimationName = capturedAnimName;
                        refreshWidgetEditorTimeline(capturedTabId3);
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
                auto it = m_widgetEditorStates.find(capturedTabId);
                if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                    return;
                auto* anim = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                if (!anim) return;
                AnimationTrack track;
                track.targetElementId = "root";
                track.property = AnimatableProperty::Opacity;
                anim->tracks.push_back(std::move(track));
                markWidgetEditorDirty(capturedTabId);
                refreshWidgetEditorTimeline(capturedTabId);
            }));

            // Play button
            toolbar.children.push_back(makeToolBtn("WE.Timeline.Play", "\xe2\x96\xb6", [this, capturedTabId]()
            {
                auto it = m_widgetEditorStates.find(capturedTabId);
                if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
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
                auto it = m_widgetEditorStates.find(capturedTabId);
                if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                    return;
                it->second.editedWidget->animationPlayer().stop();
                it->second.timelineScrubTime = 0.0f;
                it->second.previewDirty = true;
                refreshWidgetEditorTimeline(capturedTabId);
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
                    auto it = m_widgetEditorStates.find(capturedTabId2);
                    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                        return;
                    auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                    if (a) { try { a->duration = std::max(0.01f, std::stof(val)); } catch (...) {} }
                    markWidgetEditorDirty(capturedTabId2);
                    refreshWidgetEditorTimeline(capturedTabId2);
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
                    auto it = m_widgetEditorStates.find(capturedTabId3);
                    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                        return;
                    auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                    if (a) a->isLooping = checked;
                    markWidgetEditorDirty(capturedTabId3);
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
    unregisterWidget(bottomId);
    registerWidget(bottomId, bottomWidget, tabId);
    markAllWidgetsDirty();
}

void UIManager::buildTimelineTrackRows(const std::string& tabId, WidgetElement& container)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end() || !stateIt->second.editedWidget)
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
                auto it = m_widgetEditorStates.find(capturedTabId);
                if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                    return;
                auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                if (a && trackIdx < a->tracks.size())
                {
                    a->tracks.erase(a->tracks.begin() + static_cast<ptrdiff_t>(trackIdx));
                    markWidgetEditorDirty(capturedTabId);
                    refreshWidgetEditorTimeline(capturedTabId);
                }
            };
            headerRow.children.push_back(std::move(removeBtn));

            // Toggle expand on click
            const std::string capturedTabId2 = tabId;
            const std::string elemId = track.targetElementId;
            headerRow.onClicked = [this, capturedTabId2, elemId]()
            {
                auto it = m_widgetEditorStates.find(capturedTabId2);
                if (it == m_widgetEditorStates.end()) return;
                auto& expanded = it->second.expandedTimelineElements;
                if (expanded.count(elemId))
                    expanded.erase(elemId);
                else
                    expanded.insert(elemId);
                refreshWidgetEditorTimeline(capturedTabId2);
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
                        auto it = m_widgetEditorStates.find(capturedTabIdT);
                        if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                            return;
                        auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                        if (a && trackIdxT < a->tracks.size() && kfIdxT < a->tracks[trackIdxT].keyframes.size())
                        {
                            try { a->tracks[trackIdxT].keyframes[kfIdxT].time = std::stof(newVal); }
                            catch (...) {}
                            std::sort(a->tracks[trackIdxT].keyframes.begin(), a->tracks[trackIdxT].keyframes.end(),
                                [](const AnimationKeyframe& a2, const AnimationKeyframe& b) { return a2.time < b.time; });
                            markWidgetEditorDirty(capturedTabIdT);
                            refreshWidgetEditorTimeline(capturedTabIdT);
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
                        auto it = m_widgetEditorStates.find(capturedTabIdV);
                        if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                            return;
                        auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                        if (a && trackIdxV < a->tracks.size() && kfIdxV < a->tracks[trackIdxV].keyframes.size())
                        {
                            try { a->tracks[trackIdxV].keyframes[kfIdxV].value.x = std::stof(newVal); }
                            catch (...) {}
                            markWidgetEditorDirty(capturedTabIdV);
                            refreshWidgetEditorTimeline(capturedTabIdV);
                        }
                    };
                }
                kfRow.children.push_back(std::move(valEntry));

                // Delete keyframe button
                WidgetElement delBtn{};
                delBtn.id = "WE.TL.KFDel." + std::to_string(ti) + "." + std::to_string(ki);
                delBtn.type = WidgetElementType::Button;
                delBtn.text = "\xc3\x97"; // Ãƒâ€” symbol
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
                        auto it = m_widgetEditorStates.find(capturedTabIdD);
                        if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
                            return;
                        auto* a = it->second.editedWidget->findAnimationByNameMutable(it->second.selectedAnimationName);
                        if (a && trackIdxD < a->tracks.size() && kfIdxD < a->tracks[trackIdxD].keyframes.size())
                        {
                            a->tracks[trackIdxD].keyframes.erase(a->tracks[trackIdxD].keyframes.begin() + static_cast<ptrdiff_t>(kfIdxD));
                            markWidgetEditorDirty(capturedTabIdD);
                            refreshWidgetEditorTimeline(capturedTabIdD);
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
                    auto it = m_widgetEditorStates.find(capturedTabId3);
                    if (it == m_widgetEditorStates.end() || !it->second.editedWidget)
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
                        markWidgetEditorDirty(capturedTabId3);
                        refreshWidgetEditorTimeline(capturedTabId3);
                    }
                };
                container.children.push_back(std::move(addKfBtn));
                ++rowIndex;
            }
        }
    }
}

void UIManager::buildTimelineRulerAndKeyframes(const std::string& tabId, WidgetElement& container)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end() || !stateIt->second.editedWidget)
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Ruler background Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Ruler tick marks Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Lane backgrounds + keyframe markers Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ Scrubber indicator (orange line) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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

    // Ã¢â€â‚¬Ã¢â€â‚¬ End-of-animation line (red) Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
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

void UIManager::handleTimelineMouseDown(const std::string& tabId, const Vec2& localPos, float trackAreaWidth)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end() || !stateIt->second.editedWidget)
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
    refreshWidgetEditorTimeline(tabId);
}

void UIManager::handleTimelineMouseMove(const std::string& tabId, const Vec2& localPos, float trackAreaWidth)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end() || !stateIt->second.editedWidget)
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
    refreshWidgetEditorTimeline(tabId);
}

void UIManager::handleTimelineMouseUp(const std::string& tabId)
{
    auto stateIt = m_widgetEditorStates.find(tabId);
    if (stateIt == m_widgetEditorStates.end())
        return;

    stateIt->second.isDraggingScrubber = false;
    stateIt->second.isDraggingEndLine = false;
    stateIt->second.draggingKeyframeTrack = -1;
    stateIt->second.draggingKeyframeIndex = -1;
}

// ===========================================================================
// UI Designer Tab  (Gameplay UI Ã¢â‚¬â€ operates on ViewportUIManager)
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
// openUIDesignerTab  Ã¢â‚¬â€ creates the tab + left / right / toolbar widgets
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

            // Gameplay-UI element types (subset Ã¢â‚¬â€ only those supported by ViewportUIManager)
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

            // Hierarchy tree container (populated by refreshUIDesignerHierarchy)
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
        registerWidget(leftWidgetId, leftWidget, tabId);
    }

    // --- Right panel: element details (populated by refreshUIDesignerDetails) ---
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

    // --- Bidirectional sync: viewport click Ã¢â€ â€™ designer selection ---
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
// addElementToViewportWidget Ã¢â‚¬â€ adds a new element from the control palette
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
        newEl.style.color = Vec4{ 0.25f, 0.25f, 0.30f, 0.8f };
    }
    else if (elementType == "Text")
    {
        newEl.type      = WidgetElementType::Text;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 150.0f, 30.0f };
        newEl.text      = "Text";
        newEl.font      = "default.ttf";
        newEl.fontSize  = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
    }
    else if (elementType == "Label")
    {
        newEl.type      = WidgetElementType::Label;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 100.0f, 24.0f };
        newEl.text      = "Label";
        newEl.font      = "default.ttf";
        newEl.fontSize  = EditorTheme::Get().fontSizeBody;
        newEl.style.textColor = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
    }
    else if (elementType == "Button")
    {
        newEl.type          = WidgetElementType::Button;
        newEl.from          = Vec2{ 0.0f, 0.0f };
        newEl.to            = Vec2{ 120.0f, 36.0f };
        newEl.text          = "Button";
        newEl.font          = "default.ttf";
        newEl.fontSize      = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor     = Vec4{ 0.95f, 0.95f, 0.98f, 1.0f };
        newEl.style.color         = Vec4{ 0.22f, 0.24f, 0.32f, 1.0f };
        newEl.style.hoverColor    = Vec4{ 0.30f, 0.34f, 0.44f, 1.0f };
        newEl.hitTestMode = HitTestMode::Enabled;
    }
    else if (elementType == "Image")
    {
        newEl.type  = WidgetElementType::Image;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 100.0f, 100.0f };
        newEl.style.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
    }
    else if (elementType == "ProgressBar")
    {
        newEl.type       = WidgetElementType::ProgressBar;
        newEl.from       = Vec2{ 0.0f, 0.0f };
        newEl.to         = Vec2{ 200.0f, 24.0f };
        newEl.valueFloat = 0.5f;
        newEl.minValue   = 0.0f;
        newEl.maxValue   = 1.0f;
        newEl.style.color      = Vec4{ 0.15f, 0.15f, 0.20f, 1.0f };
    }
    else if (elementType == "Slider")
    {
        newEl.type       = WidgetElementType::Slider;
        newEl.from       = Vec2{ 0.0f, 0.0f };
        newEl.to         = Vec2{ 200.0f, 24.0f };
        newEl.valueFloat = 0.5f;
        newEl.minValue   = 0.0f;
        newEl.maxValue   = 1.0f;
        newEl.style.color      = Vec4{ 0.15f, 0.15f, 0.20f, 1.0f };
        newEl.hitTestMode = HitTestMode::Enabled;
    }
    else if (elementType == "WrapBox")
    {
        newEl.type        = WidgetElementType::WrapBox;
        newEl.from        = Vec2{ 0.0f, 0.0f };
        newEl.to          = Vec2{ 300.0f, 200.0f };
        newEl.orientation  = StackOrientation::Horizontal;
        newEl.style.color        = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "UniformGrid")
    {
        newEl.type    = WidgetElementType::UniformGrid;
        newEl.from    = Vec2{ 0.0f, 0.0f };
        newEl.to      = Vec2{ 300.0f, 300.0f };
        newEl.columns = 3;
        newEl.rows    = 3;
        newEl.style.color   = Vec4{ 0.1f, 0.1f, 0.13f, 0.5f };
    }
    else if (elementType == "SizeBox")
    {
        newEl.type           = WidgetElementType::SizeBox;
        newEl.from           = Vec2{ 0.0f, 0.0f };
        newEl.to             = Vec2{ 200.0f, 100.0f };
        newEl.widthOverride  = 200.0f;
        newEl.heightOverride = 100.0f;
        newEl.style.color          = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "ScaleBox")
    {
        newEl.type      = WidgetElementType::ScaleBox;
        newEl.from      = Vec2{ 0.0f, 0.0f };
        newEl.to        = Vec2{ 300.0f, 300.0f };
        newEl.scaleMode = ScaleMode::Contain;
        newEl.style.color     = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "WidgetSwitcher")
    {
        newEl.type             = WidgetElementType::WidgetSwitcher;
        newEl.from             = Vec2{ 0.0f, 0.0f };
        newEl.to               = Vec2{ 300.0f, 200.0f };
        newEl.activeChildIndex = 0;
        newEl.style.color            = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "Overlay")
    {
        newEl.type  = WidgetElementType::Overlay;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
        newEl.style.color = Vec4{ 0.1f, 0.1f, 0.13f, 0.4f };
    }
    else if (elementType == "Border")
    {
        newEl.type  = WidgetElementType::Border;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 200.0f };
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
        newEl.type  = WidgetElementType::Spinner;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 64.0f, 64.0f };
        newEl.minSize = Vec2{ 32.0f, 32.0f };
        newEl.spinnerDotCount = 8;
        newEl.spinnerSpeed = 1.0f;
        newEl.style.color = Vec4{ 0.8f, 0.8f, 0.9f, 1.0f };
    }
    else if (elementType == "RichText")
    {
        newEl.type  = WidgetElementType::RichText;
        newEl.from  = Vec2{ 0.0f, 0.0f };
        newEl.to    = Vec2{ 300.0f, 100.0f };
        newEl.richText = "<b>Bold</b> and <i>italic</i> text";
        newEl.font = "default.ttf";
        newEl.fontSize = EditorTheme::Get().fontSizeSubheading;
        newEl.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
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
// refreshUIDesignerHierarchy Ã¢â‚¬â€ traverses ViewportUIManager widgets
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
                row.style.color     = Vec4{ 0.14f, 0.15f, 0.20f, 0.6f };
                row.style.hoverColor = EditorTheme::Get().buttonSubtleHover;
                row.style.textColor = EditorTheme::Get().textPrimary;
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
// refreshUIDesignerDetails - properties panel for selected element
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

    auto* vpUI = getViewportUIManager();
    if (!vpUI)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "No ViewportUIManager available."));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Widget-level properties if a widget is selected but no element
    if (!m_uiDesignerState.selectedWidgetName.empty() && m_uiDesignerState.selectedElementId.empty())
    {
        Widget* w = vpUI->getWidget(m_uiDesignerState.selectedWidgetName);
        if (!w)
        {
            rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
                "Widget not found."));
            rightEntry->widget->markLayoutDirty();
            return;
        }

        rootPanel->children.push_back(EditorUIBuilder::makeHeading("Widget"));
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Name: " + m_uiDesignerState.selectedWidgetName));

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
    if (m_uiDesignerState.selectedElementId.empty())
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Select an element in the hierarchy\nto see its properties."));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    // Find the selected element
    WidgetElement* selected = vpUI->findElementById(
        m_uiDesignerState.selectedWidgetName, m_uiDesignerState.selectedElementId);
    if (!selected)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeSecondaryLabel(
            "Element not found: " + m_uiDesignerState.selectedElementId));
        rightEntry->widget->markLayoutDirty();
        return;
    }

    const auto applyChange = [this]() {
        auto* vp = getViewportUIManager();
        if (vp) vp->markLayoutDirty();
        markAllWidgetsDirty();
    };

    WidgetDetailSchema::Options opts;
    opts.showEditableId    = true;
    opts.showDeleteButton  = true;
    opts.onIdRenamed = [this](const std::string& newId) {
        m_uiDesignerState.selectedElementId = newId;
    };
    opts.onRefreshHierarchy = [this]() {
        refreshUIDesignerHierarchy();
    };
    opts.onDelete = [this]() {
        deleteSelectedUIDesignerElement();
    };

    WidgetDetailSchema::buildDetailPanel("UID.Det", selected, applyChange, rootPanel, opts);

    rightEntry->widget->markLayoutDirty();
}

// ===========================================================================
//  Console / Log-Viewer Tab
// ===========================================================================

bool UIManager::isConsoleOpen() const
{
    return m_consoleState.isOpen;
}

// ---------------------------------------------------------------------------
// openConsoleTab  â€“ creates the Console tab with toolbar + log view
// ---------------------------------------------------------------------------
void UIManager::openConsoleTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Console";

    // If already open, just switch to it
    if (m_consoleState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Console", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Console.Main";

    // Clean up any stale registration
    unregisterWidget(widgetId);

    // Initialise state
    m_consoleState = {};
    m_consoleState.tabId     = tabId;
    m_consoleState.widgetId  = widgetId;
    m_consoleState.isOpen    = true;
    m_consoleState.levelFilter = 0xFF;  // show all levels
    m_consoleState.autoScroll  = true;
    m_consoleState.lastSeenSequenceId = 0;

    // Build the main widget (fills entire tab area)
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "Console.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // â”€â”€ Toolbar row â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        buildConsoleToolbar(root);

        // â”€â”€ Separator â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // â”€â”€ Scrollable log area â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement logArea{};
            logArea.id          = "Console.LogArea";
            logArea.type        = WidgetElementType::StackPanel;
            logArea.fillX       = true;
            logArea.fillY       = true;
            logArea.scrollable  = true;
            logArea.orientation = StackOrientation::Vertical;
            logArea.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
            logArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            logArea.runtimeOnly = true;
            root.children.push_back(std::move(logArea));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // â”€â”€ Tab / close click events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshConsoleLog();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeConsoleTab();
    });

    // â”€â”€ Filter & action click events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    registerClickEvent("Console.Filter.All", [this]()
    {
        m_consoleState.levelFilter = 0xFF;
        refreshConsoleLog();
    });
    registerClickEvent("Console.Filter.Info", [this]()
    {
        m_consoleState.levelFilter ^= (1 << 0);
        refreshConsoleLog();
    });
    registerClickEvent("Console.Filter.Warning", [this]()
    {
        m_consoleState.levelFilter ^= (1 << 1);
        refreshConsoleLog();
    });
    registerClickEvent("Console.Filter.Error", [this]()
    {
        m_consoleState.levelFilter ^= (1 << 2);
        refreshConsoleLog();
    });
    registerClickEvent("Console.Clear", [this]()
    {
        Logger::Instance().clearConsoleBuffer();
        m_consoleState.lastSeenSequenceId = 0;
        refreshConsoleLog();
    });
    registerClickEvent("Console.AutoScroll", [this]()
    {
        m_consoleState.autoScroll = !m_consoleState.autoScroll;
        refreshConsoleLog();
    });

    // Initial population
    refreshConsoleLog();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildConsoleToolbar  â€“ filter buttons, search bar, clear, auto-scroll
// ---------------------------------------------------------------------------
void UIManager::buildConsoleToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "Console.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    auto makeFilterBtn = [&](const std::string& id, const std::string& label, bool active) -> WidgetElement
    {
        WidgetElement btn{};
        btn.id            = id;
        btn.type          = WidgetElementType::Button;
        btn.text          = label;
        btn.font          = theme.fontDefault;
        btn.fontSize      = theme.fontSizeSmall;
        btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
        btn.style.color     = active ? theme.accent : theme.buttonDefault;
        btn.style.hoverColor = theme.buttonHover;
        btn.textAlignH    = TextAlignH::Center;
        btn.textAlignV    = TextAlignV::Center;
        btn.minSize       = EditorTheme::Scaled(Vec2{ 60.0f, 24.0f });
        btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        btn.hitTestMode   = HitTestMode::Enabled;
        btn.runtimeOnly   = true;
        btn.clickEvent    = id;
        return btn;
    };

    const bool allActive    = (m_consoleState.levelFilter == 0xFF);
    const bool infoActive   = (m_consoleState.levelFilter & (1 << 0)) != 0;
    const bool warnActive   = (m_consoleState.levelFilter & (1 << 1)) != 0;
    const bool errorActive  = (m_consoleState.levelFilter & (1 << 2)) != 0;

    toolbar.children.push_back(makeFilterBtn("Console.Filter.All",     "All",     allActive));
    toolbar.children.push_back(makeFilterBtn("Console.Filter.Info",    "Info",    infoActive));
    toolbar.children.push_back(makeFilterBtn("Console.Filter.Warning", "Warning", warnActive));
    toolbar.children.push_back(makeFilterBtn("Console.Filter.Error",   "Error",   errorActive));

    // â”€â”€ Spacer â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // â”€â”€ Search entry bar â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement search = EditorUIBuilder::makeEntryBar(
            "Console.Search", m_consoleState.searchText,
            [this](const std::string& text)
            {
                m_consoleState.searchText = text;
                refreshConsoleLog();
            },
            EditorTheme::Scaled(180.0f));
        search.minSize = EditorTheme::Scaled(Vec2{ 180.0f, 24.0f });
        toolbar.children.push_back(std::move(search));
    }

    // â”€â”€ Clear button â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    toolbar.children.push_back(makeFilterBtn("Console.Clear", "Clear", false));

    // â”€â”€ Auto-scroll toggle â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement toggle{};
        toggle.id           = "Console.AutoScroll";
        toggle.type         = WidgetElementType::Button;
        toggle.text         = m_consoleState.autoScroll ? "Auto-Scroll: ON" : "Auto-Scroll: OFF";
        toggle.font         = theme.fontDefault;
        toggle.fontSize     = theme.fontSizeSmall;
        toggle.style.textColor = m_consoleState.autoScroll ? theme.accentGreen : theme.textMuted;
        toggle.style.color     = theme.buttonDefault;
        toggle.style.hoverColor = theme.buttonHover;
        toggle.textAlignH   = TextAlignH::Center;
        toggle.textAlignV   = TextAlignV::Center;
        toggle.minSize      = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
        toggle.padding      = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        toggle.hitTestMode  = HitTestMode::Enabled;
        toggle.runtimeOnly  = true;
        toggle.clickEvent   = "Console.AutoScroll";
        toolbar.children.push_back(std::move(toggle));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// refreshConsoleLog  â€“ rebuilds the log area from Logger ring-buffer
// ---------------------------------------------------------------------------
void UIManager::refreshConsoleLog()
{
    if (!m_consoleState.isOpen)
        return;

    auto* entry = findWidgetEntry(m_consoleState.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find the log-area container
    WidgetElement* logArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "Console.LogArea")
        {
            logArea = &child;
            break;
        }
    }
    if (!logArea)
        return;

    logArea->children.clear();

    // Also rebuild the toolbar to reflect filter state changes
    {
        WidgetElement* toolbar = nullptr;
        for (auto& child : elements[0].children)
        {
            if (child.id == "Console.Toolbar")
            {
                toolbar = &child;
                break;
            }
        }
        if (toolbar)
        {
            toolbar->children.clear();
            // Rebuild toolbar inline (reuse same structure as buildConsoleToolbar but into existing element)
            const auto& theme = EditorTheme::Get();

            auto makeFilterBtn = [&](const std::string& id, const std::string& label, bool active) -> WidgetElement
            {
                WidgetElement btn{};
                btn.id            = id;
                btn.type          = WidgetElementType::Button;
                btn.text          = label;
                btn.font          = theme.fontDefault;
                btn.fontSize      = theme.fontSizeSmall;
                btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
                btn.style.color     = active ? theme.accent : theme.buttonDefault;
                btn.style.hoverColor = theme.buttonHover;
                btn.textAlignH    = TextAlignH::Center;
                btn.textAlignV    = TextAlignV::Center;
                btn.minSize       = EditorTheme::Scaled(Vec2{ 60.0f, 24.0f });
                btn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
                btn.hitTestMode   = HitTestMode::Enabled;
                btn.runtimeOnly   = true;
                btn.clickEvent    = id;
                return btn;
            };

            const bool allActive    = (m_consoleState.levelFilter == 0xFF);
            const bool infoActive   = (m_consoleState.levelFilter & (1 << 0)) != 0;
            const bool warnActive   = (m_consoleState.levelFilter & (1 << 1)) != 0;
            const bool errorActive  = (m_consoleState.levelFilter & (1 << 2)) != 0;

            toolbar->children.push_back(makeFilterBtn("Console.Filter.All",     "All",     allActive));
            toolbar->children.push_back(makeFilterBtn("Console.Filter.Info",    "Info",    infoActive));
            toolbar->children.push_back(makeFilterBtn("Console.Filter.Warning", "Warning", warnActive));
            toolbar->children.push_back(makeFilterBtn("Console.Filter.Error",   "Error",   errorActive));

            // Spacer
            {
                WidgetElement spacer{};
                spacer.type        = WidgetElementType::Panel;
                spacer.fillX       = true;
                spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.runtimeOnly = true;
                toolbar->children.push_back(std::move(spacer));
            }

            // Search entry bar
            {
                WidgetElement search = EditorUIBuilder::makeEntryBar(
                    "Console.Search", m_consoleState.searchText,
                    [this](const std::string& text)
                    {
                        m_consoleState.searchText = text;
                        refreshConsoleLog();
                    },
                    EditorTheme::Scaled(180.0f));
                search.minSize = EditorTheme::Scaled(Vec2{ 180.0f, 24.0f });
                toolbar->children.push_back(std::move(search));
            }

            toolbar->children.push_back(makeFilterBtn("Console.Clear", "Clear", false));

            // Auto-scroll toggle
            {
                WidgetElement toggle{};
                toggle.id           = "Console.AutoScroll";
                toggle.type         = WidgetElementType::Button;
                toggle.text         = m_consoleState.autoScroll ? "Auto-Scroll: ON" : "Auto-Scroll: OFF";
                toggle.font         = theme.fontDefault;
                toggle.fontSize     = theme.fontSizeSmall;
                toggle.style.textColor = m_consoleState.autoScroll ? theme.accentGreen : theme.textMuted;
                toggle.style.color     = theme.buttonDefault;
                toggle.style.hoverColor = theme.buttonHover;
                toggle.textAlignH   = TextAlignH::Center;
                toggle.textAlignV   = TextAlignV::Center;
                toggle.minSize      = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
                toggle.padding      = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
                toggle.hitTestMode  = HitTestMode::Enabled;
                toggle.runtimeOnly  = true;
                toggle.clickEvent   = "Console.AutoScroll";
                toolbar->children.push_back(std::move(toggle));
            }
        }
    }

    const auto& theme = EditorTheme::Get();
    const auto& entries = Logger::Instance().getConsoleEntries();

    // Level-to-bit mapping
    auto levelBit = [](Logger::LogLevel lvl) -> uint8_t
    {
        switch (lvl)
        {
        case Logger::LogLevel::INFO:    return (1 << 0);
        case Logger::LogLevel::WARNING: return (1 << 1);
        case Logger::LogLevel::ERROR:   return (1 << 2);
        case Logger::LogLevel::FATAL:   return (1 << 2); // FATAL shares the ERROR filter bit
        default: return 0xFF;
        }
    };

    // Level-to-color mapping
    auto levelColor = [&](Logger::LogLevel lvl) -> Vec4
    {
        switch (lvl)
        {
        case Logger::LogLevel::WARNING: return theme.warningColor;
        case Logger::LogLevel::ERROR:   return theme.errorColor;
        case Logger::LogLevel::FATAL:   return Vec4{ 1.0f, 0.15f, 0.15f, 1.0f };
        default:                        return theme.textSecondary;
        }
    };

    auto levelTag = [](Logger::LogLevel lvl) -> const char*
    {
        switch (lvl)
        {
        case Logger::LogLevel::INFO:    return "[INFO]";
        case Logger::LogLevel::WARNING: return "[WARN]";
        case Logger::LogLevel::ERROR:   return "[ERR ]";
        case Logger::LogLevel::FATAL:   return "[FATL]";
        default:                        return "[    ]";
        }
    };

    uint64_t maxSeq = 0;
    const float rowH = EditorTheme::Scaled(18.0f);

    for (const auto& e : entries)
    {
        // Filter by level
        if (!(m_consoleState.levelFilter & levelBit(e.level)))
            continue;

        // Filter by search text
        if (!m_consoleState.searchText.empty())
        {
            // Case-insensitive substring search
            std::string msgLower = e.message;
            std::string queryLower = m_consoleState.searchText;
            for (auto& c : msgLower)  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto& c : queryLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (msgLower.find(queryLower) == std::string::npos)
                continue;
        }

        if (e.sequenceId > maxSeq)
            maxSeq = e.sequenceId;

        // Build the row: "[TIME] [LEVEL] [CATEGORY] message"
        std::string rowText = e.timestamp + "  " + levelTag(e.level) + "  "
            + Logger::categoryToString(e.category) + "  " + e.message;

        WidgetElement row{};
        row.id           = "Console.Row." + std::to_string(e.sequenceId);
        row.type         = WidgetElementType::Text;
        row.text         = std::move(rowText);
        row.font         = theme.fontDefault;
        row.fontSize     = theme.fontSizeSmall;
        row.style.textColor = levelColor(e.level);
        row.textAlignH   = TextAlignH::Left;
        row.textAlignV   = TextAlignV::Center;
        row.fillX        = true;
        row.minSize      = Vec2{ 0.0f, rowH };
        row.runtimeOnly  = true;

        logArea->children.push_back(std::move(row));
    }

    m_consoleState.lastSeenSequenceId = maxSeq;

    // Auto-scroll: set scroll to bottom
    if (m_consoleState.autoScroll)
        logArea->scrollOffset = 999999.0f;

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ---------------------------------------------------------------------------
// closeConsoleTab
// ---------------------------------------------------------------------------
void UIManager::closeConsoleTab()
{
    if (!m_consoleState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_consoleState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_consoleState.widgetId);

    m_renderer->removeTab(tabId);
    m_consoleState = {};
    markAllWidgetsDirty();
}

// ===========================================================================
// Profiler / Performance-Monitor Tab
// ===========================================================================

// ---------------------------------------------------------------------------
// openProfilerTab
// ---------------------------------------------------------------------------
void UIManager::openProfilerTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Profiler";

    // If already open, just switch to it
    if (m_profilerState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Profiler", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Profiler.Main";

    // Clean up any stale registration
    unregisterWidget(widgetId);

    // Initialise state
    m_profilerState = {};
    m_profilerState.tabId    = tabId;
    m_profilerState.widgetId = widgetId;
    m_profilerState.isOpen   = true;
    m_profilerState.frozen   = false;

    // Build the main widget (fills entire tab area)
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "Profiler.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // â”€â”€ Toolbar row â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        buildProfilerToolbar(root);

        // â”€â”€ Separator â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // â”€â”€ Scrollable metrics area â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement metricsArea{};
            metricsArea.id          = "Profiler.MetricsArea";
            metricsArea.type        = WidgetElementType::StackPanel;
            metricsArea.fillX       = true;
            metricsArea.fillY       = true;
            metricsArea.scrollable  = true;
            metricsArea.orientation = StackOrientation::Vertical;
            metricsArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            metricsArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            metricsArea.runtimeOnly = true;
            root.children.push_back(std::move(metricsArea));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // â”€â”€ Tab / close click events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshProfilerMetrics();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeProfilerTab();
    });

    // â”€â”€ Toolbar button events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    registerClickEvent("Profiler.Freeze", [this]()
    {
        m_profilerState.frozen = !m_profilerState.frozen;
        // Update button text and color in-place (safe â€” no element deletion during click callback)
        auto* btn = findElementById("Profiler.Freeze");
        if (btn)
        {
            const auto& theme = EditorTheme::Get();
            btn->text            = m_profilerState.frozen ? "Resume" : "Freeze";
            btn->style.textColor = m_profilerState.frozen ? theme.accentGreen : theme.textPrimary;
            btn->style.color     = m_profilerState.frozen ? Vec4{ 0.15f, 0.35f, 0.20f, 0.95f } : theme.buttonDefault;
        }
        refreshProfilerMetrics();
        auto* entry = findWidgetEntry(m_profilerState.widgetId);
        if (entry && entry->widget)
            entry->widget->markLayoutDirty();
        m_renderDirty = true;
    });

    // Initial population
    refreshProfilerMetrics();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildProfilerToolbar
// ---------------------------------------------------------------------------
void UIManager::buildProfilerToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "Profiler.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = "Performance Profiler";
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 140.0f, 24.0f });
        title.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Freeze / Resume button
    {
        WidgetElement freezeBtn{};
        freezeBtn.id            = "Profiler.Freeze";
        freezeBtn.type          = WidgetElementType::Button;
        freezeBtn.text          = m_profilerState.frozen ? "Resume" : "Freeze";
        freezeBtn.font          = theme.fontDefault;
        freezeBtn.fontSize      = theme.fontSizeSmall;
        freezeBtn.style.textColor = m_profilerState.frozen ? theme.accentGreen : theme.textPrimary;
        freezeBtn.style.color     = m_profilerState.frozen ? Vec4{ 0.15f, 0.35f, 0.20f, 0.95f } : theme.buttonDefault;
        freezeBtn.style.hoverColor = theme.buttonHover;
        freezeBtn.textAlignH    = TextAlignH::Center;
        freezeBtn.textAlignV    = TextAlignV::Center;
        freezeBtn.minSize       = EditorTheme::Scaled(Vec2{ 70.0f, 24.0f });
        freezeBtn.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        freezeBtn.hitTestMode   = HitTestMode::Enabled;
        freezeBtn.runtimeOnly   = true;
        freezeBtn.clickEvent    = "Profiler.Freeze";
        toolbar.children.push_back(std::move(freezeBtn));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// refreshProfilerMetrics  â€“ rebuilds the metrics area from DiagnosticsManager
// ---------------------------------------------------------------------------
void UIManager::refreshProfilerMetrics()
{
    if (!m_profilerState.isOpen)
        return;

    auto* entry = findWidgetEntry(m_profilerState.widgetId);
    if (!entry || !entry->widget)
        return;

    // Find the metrics area
    WidgetElement* metricsArea = nullptr;
    auto& elements = entry->widget->getElementsMutable();
    const std::function<WidgetElement*(WidgetElement&)> findRecursive =
        [&](WidgetElement& el) -> WidgetElement*
        {
            if (el.id == "Profiler.MetricsArea")
                return &el;
            for (auto& child : el.children)
            {
                if (auto* hit = findRecursive(child))
                    return hit;
            }
            return nullptr;
        };
    for (auto& el : elements)
    {
        if (auto* hit = findRecursive(el))
        {
            metricsArea = hit;
            break;
        }
    }
    if (!metricsArea)
        return;

    metricsArea->children.clear();

    const auto& theme = EditorTheme::Get();
    const auto& diag = DiagnosticsManager::Instance();
    const auto& metrics = diag.getLatestMetrics();
    const auto& history = diag.getFrameHistory();
    const size_t historySize = diag.getFrameHistorySize();

    // Helper: color-code a timing value (green < 8.3ms, yellow < 16.6ms, red >= 16.6ms)
    auto timingColor = [&](double ms) -> Vec4
    {
        if (ms < 8.3)
            return Vec4{ 0.4f, 1.0f, 0.4f, 1.0f };   // green
        if (ms < 16.6)
            return Vec4{ 1.0f, 0.85f, 0.3f, 1.0f };  // yellow
        return Vec4{ 1.0f, 0.35f, 0.35f, 1.0f };     // red
    };

    // Helper: format a timing value
    auto fmtMs = [](double ms) -> std::string
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.3f ms", ms);
        return buf;
    };

    // â”€â”€ FPS bar chart (mini-graph using colored panels) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Frame History (" + std::to_string(historySize) + " frames)";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 2.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        // Bar chart container
        const float barHeight = EditorTheme::Scaled(60.0f);
        WidgetElement barContainer{};
        barContainer.id          = "Profiler.BarChart";
        barContainer.type        = WidgetElementType::StackPanel;
        barContainer.orientation = StackOrientation::Horizontal;
        barContainer.fillX       = true;
        barContainer.minSize     = Vec2{ 0.0f, barHeight };
        barContainer.style.color = Vec4{ 0.05f, 0.06f, 0.08f, 1.0f };
        barContainer.padding     = Vec2{ 0.0f, 0.0f };
        barContainer.runtimeOnly = true;

        if (historySize > 0)
        {
            // Find max frame time for scaling
            double maxFrameMs = 16.6; // at least show 60fps baseline
            for (size_t i = 0; i < historySize; ++i)
                maxFrameMs = std::max(maxFrameMs, history[i].cpuFrameMs);
            maxFrameMs = std::max(maxFrameMs, 1.0); // avoid div by zero

            // Render bars in chronological order
            size_t startIdx = 0;
            if (historySize >= DiagnosticsManager::kMaxFrameHistory)
            {
                // Ring buffer: oldest is at (index+1) % size
                startIdx = (diag.getFrameHistorySize() == DiagnosticsManager::kMaxFrameHistory)
                    ? 0 : 0;
            }

            const size_t barCount = std::min(historySize, static_cast<size_t>(150)); // show last 150
            const size_t skipStart = (historySize > barCount) ? (historySize - barCount) : 0;

            for (size_t i = skipStart; i < historySize; ++i)
            {
                const auto& frame = history[i];
                const float ratio = static_cast<float>(std::min(frame.cpuFrameMs / maxFrameMs, 1.0));

                WidgetElement bar{};
                bar.type        = WidgetElementType::Panel;
                bar.fillX       = true;
                bar.minSize     = Vec2{ 0.0f, barHeight * ratio };
                bar.style.color = timingColor(frame.cpuFrameMs);
                bar.runtimeOnly = true;
                barContainer.children.push_back(std::move(bar));
            }
        }

        metricsArea->children.push_back(std::move(barContainer));

        // 60fps / 30fps reference labels
        {
            WidgetElement refLine{};
            refLine.type            = WidgetElementType::Text;
            refLine.text            = "--- 16.6ms (60fps) --- 33.3ms (30fps) ---";
            refLine.font            = theme.fontDefault;
            refLine.fontSize        = theme.fontSizeSmall;
            refLine.style.textColor = theme.textMuted;
            refLine.fillX           = true;
            refLine.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 16.0f });
            refLine.runtimeOnly     = true;
            metricsArea->children.push_back(std::move(refLine));
        }
    }

    // â”€â”€ Divider â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    metricsArea->children.push_back(EditorUIBuilder::makeDivider());

    // â”€â”€ Summary section â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Current Frame";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        // FPS
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeLabel("FPS", EditorTheme::Scaled(120.0f));
            WidgetElement val = EditorUIBuilder::makeLabel(std::to_string(static_cast<int>(metrics.fps + 0.5)));
            val.style.textColor = timingColor(metrics.cpuFrameMs);
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));
            metricsArea->children.push_back(std::move(row));
        }

        // Helper to add a metric row
        auto addRow = [&](const std::string& label, double valueMs)
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeLabel(label, EditorTheme::Scaled(120.0f));
            lbl.fillX = false;
            WidgetElement val = EditorUIBuilder::makeLabel(fmtMs(valueMs));
            val.fillX = false;
            val.minSize.x = EditorTheme::Scaled(80.0f);
            val.style.textColor = timingColor(valueMs);
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));

            // Mini bar showing proportion of frame time
            const float maxBar = EditorTheme::Scaled(200.0f);
            const float barW = static_cast<float>(std::min(valueMs / 33.3, 1.0)) * maxBar;
            WidgetElement bar{};
            bar.type        = WidgetElementType::Panel;
            bar.minSize     = Vec2{ std::max(barW, 1.0f), EditorTheme::Scaled(8.0f) };
            bar.style.color = timingColor(valueMs);
            bar.runtimeOnly = true;
            row.children.push_back(std::move(bar));

            metricsArea->children.push_back(std::move(row));
        };

        addRow("CPU Frame", metrics.cpuFrameMs);
        addRow("GPU Frame", metrics.gpuFrameMs);
    }

    metricsArea->children.push_back(EditorUIBuilder::makeDivider());

    // â”€â”€ Breakdown section â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "CPU Breakdown";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        auto addBreakdownRow = [&](const std::string& label, double valueMs)
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeSecondaryLabel(label, EditorTheme::Scaled(120.0f));
            lbl.fillX = false;
            WidgetElement val = EditorUIBuilder::makeLabel(fmtMs(valueMs));
            val.fillX = false;
            val.minSize.x = EditorTheme::Scaled(80.0f);
            val.style.textColor = timingColor(valueMs);
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));

            // Proportion bar
            const float maxBar = EditorTheme::Scaled(200.0f);
            const float barW = (metrics.cpuFrameMs > 0.001)
                ? static_cast<float>(valueMs / metrics.cpuFrameMs) * maxBar
                : 0.0f;
            WidgetElement bar{};
            bar.type        = WidgetElementType::Panel;
            bar.minSize     = Vec2{ std::max(barW, 1.0f), EditorTheme::Scaled(6.0f) };
            bar.style.color = timingColor(valueMs);
            bar.style.borderRadius = EditorTheme::Scaled(2.0f);
            bar.runtimeOnly = true;
            row.children.push_back(std::move(bar));

            metricsArea->children.push_back(std::move(row));
        };

        addBreakdownRow("World Render", metrics.cpuWorldMs);
        addBreakdownRow("UI Render", metrics.cpuUiMs);
        addBreakdownRow("UI Layout", metrics.cpuUiLayoutMs);
        addBreakdownRow("UI Draw", metrics.cpuUiDrawMs);
        addBreakdownRow("ECS", metrics.cpuEcsMs);
        addBreakdownRow("Input", metrics.cpuInputMs);
        addBreakdownRow("Events", metrics.cpuEventMs);
        addBreakdownRow("Render Pass", metrics.cpuRenderMs);
        addBreakdownRow("GC", metrics.cpuGcMs);
        addBreakdownRow("Other", metrics.cpuOtherMs);
    }

    metricsArea->children.push_back(EditorUIBuilder::makeDivider());

    // â”€â”€ Occlusion culling stats â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Occlusion Culling";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSubheading;
        heading.style.textColor = theme.textPrimary;
        heading.fillX           = true;
        heading.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        heading.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        heading.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(heading));

        auto addStatRow = [&](const std::string& label, uint32_t value)
        {
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeSecondaryLabel(label, EditorTheme::Scaled(120.0f));
            WidgetElement val = EditorUIBuilder::makeLabel(std::to_string(value));
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));
            metricsArea->children.push_back(std::move(row));
        };

        addStatRow("Visible", metrics.visibleCount);
        addStatRow("Hidden", metrics.hiddenCount);
        addStatRow("Total", metrics.totalCount);

        if (metrics.totalCount > 0)
        {
            const float cullPercent = 100.0f * static_cast<float>(metrics.hiddenCount) / static_cast<float>(metrics.totalCount);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.1f%%", cullPercent);
            WidgetElement row = EditorUIBuilder::makeHorizontalRow();
            WidgetElement lbl = EditorUIBuilder::makeSecondaryLabel("Cull Rate", EditorTheme::Scaled(120.0f));
            WidgetElement val = EditorUIBuilder::makeLabel(buf);
            val.style.textColor = Vec4{ 0.5f, 0.9f, 1.0f, 1.0f };
            row.children.push_back(std::move(lbl));
            row.children.push_back(std::move(val));
            metricsArea->children.push_back(std::move(row));
        }
    }

    // â”€â”€ Status line â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement status{};
        status.type            = WidgetElementType::Text;
        status.text            = m_profilerState.frozen ? "[ FROZEN ]" : "Live";
        status.font            = theme.fontDefault;
        status.fontSize        = theme.fontSizeSmall;
        status.style.textColor = m_profilerState.frozen
            ? Vec4{ 1.0f, 0.6f, 0.3f, 1.0f }
            : Vec4{ 0.4f, 1.0f, 0.4f, 0.7f };
        status.fillX           = true;
        status.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        status.padding         = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        status.runtimeOnly     = true;
        metricsArea->children.push_back(std::move(status));
    }

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ---------------------------------------------------------------------------
// closeProfilerTab
// ---------------------------------------------------------------------------
void UIManager::closeProfilerTab()
{
    if (!m_profilerState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_profilerState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_profilerState.widgetId);

    m_renderer->removeTab(tabId);
    m_profilerState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// isProfilerOpen
// ---------------------------------------------------------------------------
bool UIManager::isProfilerOpen() const
{
    return m_profilerState.isOpen;
}

// ===========================================================================
// Audio Preview Tab
// ===========================================================================

// ---------------------------------------------------------------------------
// isAudioPreviewOpen
// ---------------------------------------------------------------------------
bool UIManager::isAudioPreviewOpen() const
{
    return m_audioPreviewState.isOpen;
}

// ---------------------------------------------------------------------------
// openAudioPreviewTab
// ---------------------------------------------------------------------------
void UIManager::openAudioPreviewTab(const std::string& assetPath)
{
    if (!m_renderer)
        return;

    const std::string tabId = "AudioPreview";

    // If already open with the same asset, just switch to it
    if (m_audioPreviewState.isOpen && m_audioPreviewState.assetPath == assetPath)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    // If open with a different asset, close first
    if (m_audioPreviewState.isOpen)
        closeAudioPreviewTab();

    // --- Resolve and load the asset ---
    auto& assetMgr = AssetManager::Instance();
    const std::string absPath = assetMgr.getAbsoluteContentPath(assetPath);
    if (absPath.empty())
    {
        showToastMessage("Failed to resolve audio asset.", 3.0f, NotificationLevel::Error);
        return;
    }

    auto existingAsset = assetMgr.getLoadedAssetByPath(assetPath);
    if (!existingAsset)
    {
        const int loadId = assetMgr.loadAsset(absPath, AssetType::Audio, AssetManager::Sync);
        if (loadId == 0)
        {
            showToastMessage("Failed to load audio asset.", 3.0f, NotificationLevel::Error);
            return;
        }
        existingAsset = assetMgr.getLoadedAssetByPath(assetPath);
    }

    if (!existingAsset)
    {
        showToastMessage("Audio asset not found in memory.", 3.0f, NotificationLevel::Error);
        return;
    }

    const auto& data = existingAsset->getData();

    // --- Create tab ---
    m_renderer->addTab(tabId, std::filesystem::path(assetPath).stem().string(), true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "AudioPreview.Main";
    unregisterWidget(widgetId);

    // --- Initialise state ---
    m_audioPreviewState = {};
    m_audioPreviewState.tabId       = tabId;
    m_audioPreviewState.widgetId    = widgetId;
    m_audioPreviewState.assetPath   = assetPath;
    m_audioPreviewState.isOpen      = true;
    m_audioPreviewState.volume      = 1.0f;
    m_audioPreviewState.displayName = std::filesystem::path(assetPath).stem().string();

    // Extract metadata
    if (data.contains("m_channels") && data["m_channels"].is_number())
        m_audioPreviewState.channels = data["m_channels"].get<int>();
    if (data.contains("m_sampleRate") && data["m_sampleRate"].is_number())
        m_audioPreviewState.sampleRate = data["m_sampleRate"].get<int>();
    if (data.contains("m_format") && data["m_format"].is_number())
        m_audioPreviewState.format = data["m_format"].get<int>();
    if (data.contains("m_data") && data["m_data"].is_array())
        m_audioPreviewState.dataBytes = data["m_data"].size();

    // Compute duration
    if (m_audioPreviewState.sampleRate > 0 && m_audioPreviewState.channels > 0)
    {
        int bytesPerSample = 2; // default: 16-bit
        // SDL_AUDIO_U8 = 0x0008
        if (m_audioPreviewState.format == 0x0008)
            bytesPerSample = 1;
        const size_t frameSize = static_cast<size_t>(m_audioPreviewState.channels) * bytesPerSample;
        if (frameSize > 0)
            m_audioPreviewState.durationSeconds = static_cast<float>(m_audioPreviewState.dataBytes) / static_cast<float>(frameSize * m_audioPreviewState.sampleRate);
    }

    // --- Build the widget ---
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "AudioPreview.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // Toolbar
        buildAudioPreviewToolbar(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Scrollable content area
        {
            WidgetElement content{};
            content.id          = "AudioPreview.Content";
            content.type        = WidgetElementType::StackPanel;
            content.fillX       = true;
            content.fillY       = true;
            content.scrollable  = true;
            content.orientation = StackOrientation::Vertical;
            content.padding     = EditorTheme::Scaled(Vec2{ 16.0f, 12.0f });
            content.style.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
            content.runtimeOnly = true;

            // Waveform
            buildAudioPreviewWaveform(content);

            // Metadata
            buildAudioPreviewMetadata(content);

            root.children.push_back(std::move(content));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // --- Tab / close click events ---
    registerClickEvent("TitleBar.Tab." + tabId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
    });

    registerClickEvent("TitleBar.TabClose." + tabId, [this]()
    {
        closeAudioPreviewTab();
    });

    // --- Playback button events ---
    registerClickEvent("AudioPreview.Play", [this]()
    {
        if (m_audioPreviewState.isPlaying)
            return;
        auto& audioMgr = AudioManager::Instance();
        auto& assetMgr = AssetManager::Instance();
        auto asset = assetMgr.getLoadedAssetByPath(m_audioPreviewState.assetPath);
        if (asset)
        {
            unsigned int handle = audioMgr.playAudioAsset(asset->getId(), false, m_audioPreviewState.volume);
            if (handle != 0)
            {
                m_audioPreviewState.playHandle = handle;
                m_audioPreviewState.isPlaying = true;
                refreshAudioPreview();
            }
        }
    });

    registerClickEvent("AudioPreview.Stop", [this]()
    {
        if (m_audioPreviewState.playHandle != 0)
        {
            AudioManager::Instance().stopSource(m_audioPreviewState.playHandle);
            m_audioPreviewState.playHandle = 0;
        }
        m_audioPreviewState.isPlaying = false;
        refreshAudioPreview();
    });

    refreshAudioPreview();
    markAllWidgetsDirty();

    Logger::Instance().log(Logger::Category::UI,
        "Audio Preview opened: " + assetPath, Logger::LogLevel::INFO);
}

// ---------------------------------------------------------------------------
// closeAudioPreviewTab
// ---------------------------------------------------------------------------
void UIManager::closeAudioPreviewTab()
{
    if (!m_audioPreviewState.isOpen || !m_renderer)
        return;

    // Stop any playing audio
    if (m_audioPreviewState.playHandle != 0)
    {
        AudioManager::Instance().stopSource(m_audioPreviewState.playHandle);
    }

    const std::string tabId = m_audioPreviewState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_audioPreviewState.widgetId);

    m_renderer->removeTab(tabId);
    m_audioPreviewState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// refreshAudioPreview  â€“ rebuilds content of the widget
// ---------------------------------------------------------------------------
void UIManager::refreshAudioPreview()
{
    if (!m_audioPreviewState.isOpen)
        return;

    auto* entry = findWidgetEntry(m_audioPreviewState.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    auto& root = elements[0];
    root.children.clear();

    const auto& theme = EditorTheme::Get();
    root.style.color = theme.panelBackground;

    // Toolbar
    buildAudioPreviewToolbar(root);

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        root.children.push_back(std::move(sep));
    }

    // Content area
    {
        WidgetElement content{};
        content.id          = "AudioPreview.Content";
        content.type        = WidgetElementType::StackPanel;
        content.fillX       = true;
        content.fillY       = true;
        content.scrollable  = true;
        content.orientation = StackOrientation::Vertical;
        content.padding     = EditorTheme::Scaled(Vec2{ 16.0f, 12.0f });
        content.style.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
        content.runtimeOnly = true;

        buildAudioPreviewWaveform(content);
        buildAudioPreviewMetadata(content);

        root.children.push_back(std::move(content));
    }

    entry->widget->markLayoutDirty();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildAudioPreviewToolbar
// ---------------------------------------------------------------------------
void UIManager::buildAudioPreviewToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "AudioPreview.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 36.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    auto makeToolBtn = [&](const std::string& id, const std::string& label, bool active) -> WidgetElement
    {
        WidgetElement btn{};
        btn.id              = id;
        btn.type            = WidgetElementType::Button;
        btn.text            = label;
        btn.font            = theme.fontDefault;
        btn.fontSize        = theme.fontSizeSmall;
        btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
        btn.style.color     = active ? theme.accent : theme.buttonDefault;
        btn.style.hoverColor = theme.buttonHover;
        btn.textAlignH      = TextAlignH::Center;
        btn.textAlignV      = TextAlignV::Center;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 60.0f, 26.0f });
        btn.padding         = EditorTheme::Scaled(Vec2{ 10.0f, 2.0f });
        btn.hitTestMode     = HitTestMode::Enabled;
        btn.runtimeOnly     = true;
        btn.clickEvent      = id;
        return btn;
    };

    // Play / Stop buttons
    toolbar.children.push_back(makeToolBtn("AudioPreview.Play", m_audioPreviewState.isPlaying ? "Playing..." : "Play", !m_audioPreviewState.isPlaying));
    toolbar.children.push_back(makeToolBtn("AudioPreview.Stop", "Stop", m_audioPreviewState.isPlaying));

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Volume label
    {
        WidgetElement volLabel{};
        volLabel.type          = WidgetElementType::Text;
        volLabel.text          = "Volume";
        volLabel.font          = theme.fontDefault;
        volLabel.fontSize      = theme.fontSizeSmall;
        volLabel.style.textColor = theme.textSecondary;
        volLabel.minSize       = EditorTheme::Scaled(Vec2{ 50.0f, 26.0f });
        volLabel.textAlignV    = TextAlignV::Center;
        volLabel.runtimeOnly   = true;
        toolbar.children.push_back(std::move(volLabel));
    }

    // Volume slider
    {
        WidgetElement slider = EditorUIBuilder::makeSliderRow(
            "AudioPreview.Volume", "",
            m_audioPreviewState.volume, 0.0f, 1.0f,
            [this](float v)
            {
                m_audioPreviewState.volume = v;
                if (m_audioPreviewState.playHandle != 0)
                    AudioManager::Instance().setHandleGain(m_audioPreviewState.playHandle, v);
            });
        slider.minSize = EditorTheme::Scaled(Vec2{ 140.0f, 26.0f });
        toolbar.children.push_back(std::move(slider));
    }

    // Asset name label (right side)
    {
        WidgetElement nameLabel{};
        nameLabel.type          = WidgetElementType::Text;
        nameLabel.text          = m_audioPreviewState.displayName;
        nameLabel.font          = theme.fontDefault;
        nameLabel.fontSize      = theme.fontSizeSmall;
        nameLabel.style.textColor = theme.textMuted;
        nameLabel.minSize       = EditorTheme::Scaled(Vec2{ 80.0f, 26.0f });
        nameLabel.textAlignV    = TextAlignV::Center;
        nameLabel.textAlignH    = TextAlignH::Right;
        nameLabel.runtimeOnly   = true;
        nameLabel.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 0.0f });
        toolbar.children.push_back(std::move(nameLabel));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// buildAudioPreviewWaveform  â€“ simple bar-chart visualisation of audio samples
// ---------------------------------------------------------------------------
void UIManager::buildAudioPreviewWaveform(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    // Section heading
    {
        WidgetElement heading = EditorUIBuilder::makeHeading("Waveform");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    // Waveform container
    WidgetElement waveContainer{};
    waveContainer.id          = "AudioPreview.Waveform";
    waveContainer.type        = WidgetElementType::StackPanel;
    waveContainer.fillX       = true;
    waveContainer.orientation = StackOrientation::Horizontal;
    waveContainer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 100.0f });
    waveContainer.style.color = Vec4{ 0.06f, 0.07f, 0.09f, 1.0f };
    waveContainer.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
    waveContainer.runtimeOnly = true;

    // Try to read raw sample data for waveform visualisation
    auto& assetMgr = AssetManager::Instance();
    auto asset = assetMgr.getLoadedAssetByPath(m_audioPreviewState.assetPath);

    const int numBars = 80;
    std::vector<float> barHeights(numBars, 0.0f);

    if (asset)
    {
        const auto& data = asset->getData();
        if (data.contains("m_data") && data["m_data"].is_array())
        {
            const auto& rawArr = data["m_data"];
            const size_t totalBytes = rawArr.size();

            const bool is8bit = (m_audioPreviewState.format == 0x0008);
            const int bytesPerSample = is8bit ? 1 : 2;
            const int frameSize = m_audioPreviewState.channels * bytesPerSample;
            if (frameSize > 0 && totalBytes > 0)
            {
                const size_t totalFrames = totalBytes / frameSize;
                const size_t framesPerBar = (totalFrames > 0) ? std::max<size_t>(1, totalFrames / numBars) : 1;

                for (int bar = 0; bar < numBars; ++bar)
                {
                    const size_t startFrame = bar * framesPerBar;
                    const size_t endFrame = std::min<size_t>(startFrame + framesPerBar, totalFrames);
                    float maxAmp = 0.0f;

                    // Sample a subset of frames to avoid reading millions of JSON elements
                    const size_t step = std::max<size_t>(1, (endFrame - startFrame) / 32);
                    for (size_t f = startFrame; f < endFrame; f += step)
                    {
                        const size_t byteOffset = f * frameSize;
                        if (is8bit)
                        {
                            if (byteOffset < totalBytes)
                            {
                                float sample = static_cast<float>(rawArr[byteOffset].get<int>()) / 255.0f;
                                sample = std::abs(sample - 0.5f) * 2.0f;
                                maxAmp = std::max(maxAmp, sample);
                            }
                        }
                        else
                        {
                            if (byteOffset + 1 < totalBytes)
                            {
                                int lo = rawArr[byteOffset].get<int>() & 0xFF;
                                int hi = rawArr[byteOffset + 1].get<int>() & 0xFF;
                                int16_t s16 = static_cast<int16_t>(lo | (hi << 8));
                                float sample = std::abs(static_cast<float>(s16)) / 32768.0f;
                                maxAmp = std::max(maxAmp, sample);
                            }
                        }
                    }
                    barHeights[bar] = maxAmp;
                }
            }
        }
    }

    // Build bars
    const float maxBarH = EditorTheme::Scaled(90.0f);
    const Vec4 barColor = Vec4{ 0.30f, 0.60f, 1.00f, 0.85f };
    const Vec4 barBg    = Vec4{ 0.12f, 0.13f, 0.16f, 1.0f };

    for (int i = 0; i < numBars; ++i)
    {
        WidgetElement barCol{};
        barCol.type        = WidgetElementType::StackPanel;
        barCol.orientation = StackOrientation::Vertical;
        barCol.fillX       = true;
        barCol.fillY       = true;
        barCol.runtimeOnly = true;

        float h = barHeights[i] * maxBarH;
        float emptyH = maxBarH - h;

        // Top empty space
        {
            WidgetElement empty{};
            empty.type        = WidgetElementType::Panel;
            empty.fillX       = true;
            empty.minSize     = Vec2{ 0.0f, std::max(emptyH, 1.0f) };
            empty.style.color = Vec4{ 0, 0, 0, 0 };
            empty.runtimeOnly = true;
            barCol.children.push_back(std::move(empty));
        }

        // Bar
        {
            WidgetElement bar{};
            bar.type        = WidgetElementType::Panel;
            bar.fillX       = true;
            bar.minSize     = Vec2{ 0.0f, std::max(h, 1.0f) };
            bar.style.color = barColor;
            bar.runtimeOnly = true;
            barCol.children.push_back(std::move(bar));
        }

        waveContainer.children.push_back(std::move(barCol));
    }

    root.children.push_back(std::move(waveContainer));
}

// ---------------------------------------------------------------------------
// buildAudioPreviewMetadata
// ---------------------------------------------------------------------------
void UIManager::buildAudioPreviewMetadata(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();
    const auto& st = m_audioPreviewState;

    // Spacer
    {
        WidgetElement sp{};
        sp.type        = WidgetElementType::Panel;
        sp.fillX       = true;
        sp.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 10.0f });
        sp.style.color = Vec4{ 0, 0, 0, 0 };
        sp.runtimeOnly = true;
        root.children.push_back(std::move(sp));
    }

    // Section heading
    {
        WidgetElement heading = EditorUIBuilder::makeHeading("Metadata");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    auto addInfoRow = [&](const std::string& label, const std::string& value)
    {
        WidgetElement row{};
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        row.runtimeOnly = true;

        WidgetElement lbl{};
        lbl.type          = WidgetElementType::Text;
        lbl.text          = label;
        lbl.font          = theme.fontDefault;
        lbl.fontSize      = theme.fontSizeBody;
        lbl.style.textColor = theme.textSecondary;
        lbl.minSize       = EditorTheme::Scaled(Vec2{ 120.0f, 20.0f });
        lbl.textAlignV    = TextAlignV::Center;
        lbl.runtimeOnly   = true;
        row.children.push_back(std::move(lbl));

        WidgetElement val{};
        val.type          = WidgetElementType::Text;
        val.text          = value;
        val.font          = theme.fontDefault;
        val.fontSize      = theme.fontSizeBody;
        val.style.textColor = theme.textPrimary;
        val.fillX         = true;
        val.minSize       = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        val.textAlignV    = TextAlignV::Center;
        val.runtimeOnly   = true;
        row.children.push_back(std::move(val));

        root.children.push_back(std::move(row));
    };

    addInfoRow("Path", st.assetPath);
    addInfoRow("Channels", std::to_string(st.channels) + (st.channels == 1 ? " (Mono)" : st.channels == 2 ? " (Stereo)" : ""));
    addInfoRow("Sample Rate", std::to_string(st.sampleRate) + " Hz");

    // Format string
    {
        std::string fmtStr = "Unknown";
        if (st.format == 0x0008) fmtStr = "8-bit Unsigned";
        else if (st.format != 0) fmtStr = "16-bit Signed";
        addInfoRow("Format", fmtStr);
    }

    // Duration
    {
        int totalSec = static_cast<int>(st.durationSeconds);
        int minutes = totalSec / 60;
        int seconds = totalSec % 60;
        int millis = static_cast<int>((st.durationSeconds - totalSec) * 1000);
        std::ostringstream oss;
        oss << minutes << ":" << std::setfill('0') << std::setw(2) << seconds
            << "." << std::setfill('0') << std::setw(3) << millis;
        addInfoRow("Duration", oss.str());
    }

    // Data size
    {
        std::string sizeStr;
        if (st.dataBytes >= 1024 * 1024)
            sizeStr = std::to_string(st.dataBytes / (1024 * 1024)) + " MB";
        else if (st.dataBytes >= 1024)
            sizeStr = std::to_string(st.dataBytes / 1024) + " KB";
        else
            sizeStr = std::to_string(st.dataBytes) + " B";
        addInfoRow("Data Size", sizeStr);
    }

    // File size on disk
    {
        const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
        if (!projPath.empty())
        {
            std::filesystem::path diskPath = std::filesystem::path(projPath) / "Content" / st.assetPath;
            if (std::filesystem::exists(diskPath))
            {
                std::error_code ec;
                size_t bytes = static_cast<size_t>(std::filesystem::file_size(diskPath, ec));
                std::string sizeStr;
                if (bytes >= 1024 * 1024)
                    sizeStr = std::to_string(bytes / (1024 * 1024)) + " MB";
                else if (bytes >= 1024)
                    sizeStr = std::to_string(bytes / 1024) + " KB";
                else
                    sizeStr = std::to_string(bytes) + " B";
                addInfoRow("File Size", sizeStr);
            }
        }
    }
}

// ===========================================================================
// Particle Editor Tab
// ===========================================================================

// ---------------------------------------------------------------------------
// isParticleEditorOpen
// ---------------------------------------------------------------------------
bool UIManager::isParticleEditorOpen() const
{
    return m_particleEditorState.isOpen;
}

// ---------------------------------------------------------------------------
// openParticleEditorTab
// ---------------------------------------------------------------------------
void UIManager::openParticleEditorTab(ECS::Entity entity)
{
    if (!m_renderer)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    if (!ecs.hasComponent<ECS::ParticleEmitterComponent>(entity))
        return;

    const std::string tabId = "ParticleEditor";

    // If already open for this entity, just switch to it
    if (m_particleEditorState.isOpen && m_particleEditorState.linkedEntity == entity)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    // If open for a different entity, close first
    if (m_particleEditorState.isOpen)
        closeParticleEditorTab();

    m_renderer->addTab(tabId, "Particle Editor", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "ParticleEditor.Main";
    unregisterWidget(widgetId);

    m_particleEditorState = {};
    m_particleEditorState.tabId        = tabId;
    m_particleEditorState.widgetId     = widgetId;
    m_particleEditorState.linkedEntity = entity;
    m_particleEditorState.isOpen       = true;
    m_particleEditorState.presetIndex  = -1;

    // Build the main widget
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "ParticleEditor.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // Toolbar
        buildParticleEditorToolbar(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Scrollable parameter area
        {
            WidgetElement paramsArea{};
            paramsArea.id          = "ParticleEditor.ParamsArea";
            paramsArea.type        = WidgetElementType::StackPanel;
            paramsArea.fillX       = true;
            paramsArea.fillY       = true;
            paramsArea.scrollable  = true;
            paramsArea.orientation = StackOrientation::Vertical;
            paramsArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            paramsArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            paramsArea.runtimeOnly = true;
            root.children.push_back(std::move(paramsArea));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // Tab / close click events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshParticleEditor();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeParticleEditorTab();
    });

    // Reset button â†’ restore default ParticleEmitterComponent values
    registerClickEvent("ParticleEditor.Reset", [this]()
    {
        auto& ecs = ECS::ECSManager::Instance();
        auto* comp = ecs.getComponent<ECS::ParticleEmitterComponent>(m_particleEditorState.linkedEntity);
        if (!comp)
            return;
        const ECS::Entity entity = m_particleEditorState.linkedEntity;
        const ECS::ParticleEmitterComponent saved = *comp;
        *comp = ECS::ParticleEmitterComponent{}; // reset to defaults
        m_particleEditorState.presetIndex = -1;
        if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
            level->setIsSaved(false);
        UndoRedoManager::Instance().pushCommand({
            "Reset Particle Emitter",
            [entity]() {
                auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
                if (c) *c = ECS::ParticleEmitterComponent{};
            },
            [entity, saved]() {
                auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
                if (c) *c = saved;
            }
        });
        refreshParticleEditor();
        showToastMessage("Particle emitter reset to defaults", 2.0f);
    });

    // Initial population
    refreshParticleEditor();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// closeParticleEditorTab
// ---------------------------------------------------------------------------
void UIManager::closeParticleEditorTab()
{
    if (!m_particleEditorState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_particleEditorState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_particleEditorState.widgetId);

    m_renderer->removeTab(tabId);
    m_particleEditorState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildParticleEditorToolbar
// ---------------------------------------------------------------------------
void UIManager::buildParticleEditorToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "ParticleEditor.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title
    {
        std::string titleText = "Particle Editor";
        auto& ecs = ECS::ECSManager::Instance();
        if (const auto* name = ecs.getComponent<ECS::NameComponent>(m_particleEditorState.linkedEntity))
        {
            if (!name->displayName.empty())
                titleText += "  -  " + name->displayName;
        }
        titleText += " (Entity " + std::to_string(m_particleEditorState.linkedEntity) + ")";

        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = titleText;
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, 24.0f });
        title.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Preset dropdown
    {
        static const std::vector<std::string> presetNames = {
            "Custom", "Fire", "Smoke", "Sparks", "Rain", "Snow", "Magic"
        };
        int idx = m_particleEditorState.presetIndex < 0 ? 0 : (m_particleEditorState.presetIndex + 1);
        WidgetElement dropdown = EditorUIBuilder::makeDropDown(
            "ParticleEditor.Preset", presetNames, idx,
            [this](int sel)
            {
                if (sel <= 0)
                    return; // "Custom" selected â€“ do nothing
                applyParticlePreset(sel - 1); // 0=Fire, 1=Smoke, ...
            });
        dropdown.minSize = EditorTheme::Scaled(Vec2{ 100.0f, 24.0f });
        dropdown.tooltipText = "Apply Preset";
        toolbar.children.push_back(std::move(dropdown));
    }

    // Reset button
    {
        WidgetElement resetBtn = EditorUIBuilder::makeButton(
            "ParticleEditor.Reset", "Reset", {}, EditorTheme::Scaled(Vec2{ 60.0f, 24.0f }));
        resetBtn.tooltipText = "Reset to defaults";
        toolbar.children.push_back(std::move(resetBtn));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// refreshParticleEditor  â€“ rebuilds the parameter area from the linked entity
// ---------------------------------------------------------------------------
void UIManager::refreshParticleEditor()
{
    if (!m_particleEditorState.isOpen)
        return;

    auto* entry = findWidgetEntry(m_particleEditorState.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find ParamsArea inside root
    WidgetElement* paramsArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "ParticleEditor.ParamsArea")
        {
            paramsArea = &child;
            break;
        }
    }
    if (!paramsArea)
        return;

    paramsArea->children.clear();

    auto& ecs = ECS::ECSManager::Instance();
    const auto* emitter = ecs.getComponent<ECS::ParticleEmitterComponent>(m_particleEditorState.linkedEntity);
    if (!emitter)
    {
        paramsArea->children.push_back(EditorUIBuilder::makeLabel("Entity no longer has a Particle Emitter component."));
        entry->widget->markLayoutDirty();
        m_renderDirty = true;
        return;
    }

    const ECS::Entity entity = m_particleEditorState.linkedEntity;

    // Helper: slider row that writes back to the ECS component with undo
    auto addSlider = [&](const std::string& id, const std::string& label,
        float value, float minVal, float maxVal,
        std::function<void(ECS::ParticleEmitterComponent&, float)> setter)
    {
        WidgetElement row = EditorUIBuilder::makeSliderRow(
            "ParticleEditor." + id, label, value, minVal, maxVal,
            [this, entity, setter = std::move(setter)](float v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(entity);
                if (comp)
                {
                    setter(*comp, v);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                }
                m_particleEditorState.presetIndex = -1;
            });
        paramsArea->children.push_back(std::move(row));
    };

    auto addCheckBox = [&](const std::string& id, const std::string& label, bool value,
        std::function<void(ECS::ParticleEmitterComponent&, bool)> setter)
    {
        WidgetElement row = EditorUIBuilder::makeCheckBox(
            "ParticleEditor." + id, label, value,
            [this, entity, setter = std::move(setter)](bool v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(entity);
                if (comp)
                {
                    setter(*comp, v);
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                }
            });
        paramsArea->children.push_back(std::move(row));
    };

    auto addIntSlider = [&](const std::string& id, const std::string& label,
        int value, int minVal, int maxVal,
        std::function<void(ECS::ParticleEmitterComponent&, int)> setter)
    {
        WidgetElement row = EditorUIBuilder::makeSliderRow(
            "ParticleEditor." + id, label,
            static_cast<float>(value), static_cast<float>(minVal), static_cast<float>(maxVal),
            [this, entity, setter = std::move(setter)](float v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::ParticleEmitterComponent>(entity);
                if (comp)
                {
                    setter(*comp, static_cast<int>(v));
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                }
                m_particleEditorState.presetIndex = -1;
            });
        paramsArea->children.push_back(std::move(row));
    };

    auto addHeading = [&](const std::string& text)
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        paramsArea->children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading(text);
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        paramsArea->children.push_back(std::move(heading));
    };

    // â”€â”€ General â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    addHeading("General");
    addCheckBox("Enabled", "Enabled", emitter->enabled,
        [](ECS::ParticleEmitterComponent& c, bool v) { c.enabled = v; });
    addCheckBox("Loop", "Loop", emitter->loop,
        [](ECS::ParticleEmitterComponent& c, bool v) { c.loop = v; });
    addIntSlider("MaxParticles", "Max Particles", emitter->maxParticles, 1, 10000,
        [](ECS::ParticleEmitterComponent& c, int v) { c.maxParticles = v; });

    // â”€â”€ Emission â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    addHeading("Emission");
    addSlider("EmissionRate", "Rate (p/s)", emitter->emissionRate, 0.1f, 500.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.emissionRate = v; });
    addSlider("Lifetime", "Lifetime (s)", emitter->lifetime, 0.1f, 30.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.lifetime = v; });
    addSlider("ConeAngle", "Cone Angle", emitter->coneAngle, 0.0f, 180.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.coneAngle = v; });

    // â”€â”€ Motion â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    addHeading("Motion");
    addSlider("Speed", "Speed (m/s)", emitter->speed, 0.0f, 50.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.speed = v; });
    addSlider("SpeedVariance", "Speed Variance", emitter->speedVariance, 0.0f, 20.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.speedVariance = v; });
    addSlider("Gravity", "Gravity", emitter->gravity, -30.0f, 30.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.gravity = v; });

    // â”€â”€ Size â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    addHeading("Size");
    addSlider("Size", "Start Size", emitter->size, 0.01f, 5.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.size = v; });
    addSlider("SizeEnd", "End Size", emitter->sizeEnd, 0.0f, 5.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.sizeEnd = v; });

    // â”€â”€ Start Color â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    addHeading("Start Color");
    addSlider("ColorR", "R", emitter->colorR, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorR = v; });
    addSlider("ColorG", "G", emitter->colorG, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorG = v; });
    addSlider("ColorB", "B", emitter->colorB, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorB = v; });
    addSlider("ColorA", "A", emitter->colorA, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorA = v; });

    // â”€â”€ End Color â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    addHeading("End Color");
    addSlider("ColorEndR", "R", emitter->colorEndR, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndR = v; });
    addSlider("ColorEndG", "G", emitter->colorEndG, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndG = v; });
    addSlider("ColorEndB", "B", emitter->colorEndB, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndB = v; });
    addSlider("ColorEndA", "A", emitter->colorEndA, 0.0f, 1.0f,
        [](ECS::ParticleEmitterComponent& c, float v) { c.colorEndA = v; });

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ---------------------------------------------------------------------------
// buildParticleEditorParams  â€“ called once during open (params rebuilt via refresh)
// ---------------------------------------------------------------------------
void UIManager::buildParticleEditorParams(WidgetElement& /*root*/)
{
    // Parameters are populated by refreshParticleEditor() into the ParamsArea.
}

// ---------------------------------------------------------------------------
// applyParticlePreset  â€“ applies a named preset to the linked entity
// ---------------------------------------------------------------------------
void UIManager::applyParticlePreset(int presetIndex)
{
    if (!m_particleEditorState.isOpen)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    auto* comp = ecs.getComponent<ECS::ParticleEmitterComponent>(m_particleEditorState.linkedEntity);
    if (!comp)
        return;

    // Save for undo
    const ECS::Entity entity = m_particleEditorState.linkedEntity;
    const ECS::ParticleEmitterComponent saved = *comp;

    // Preset definitions: Fire(0), Smoke(1), Sparks(2), Rain(3), Snow(4), Magic(5)
    switch (presetIndex)
    {
    case 0: // Fire
        comp->maxParticles   = 200;
        comp->emissionRate   = 60.0f;
        comp->lifetime       = 1.5f;
        comp->speed          = 3.0f;
        comp->speedVariance  = 1.0f;
        comp->size           = 0.3f;
        comp->sizeEnd        = 0.0f;
        comp->gravity        = 1.5f;
        comp->coneAngle      = 15.0f;
        comp->colorR = 1.0f; comp->colorG = 0.6f; comp->colorB = 0.1f; comp->colorA = 1.0f;
        comp->colorEndR = 1.0f; comp->colorEndG = 0.1f; comp->colorEndB = 0.0f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 1: // Smoke
        comp->maxParticles   = 150;
        comp->emissionRate   = 30.0f;
        comp->lifetime       = 4.0f;
        comp->speed          = 1.5f;
        comp->speedVariance  = 0.5f;
        comp->size           = 0.4f;
        comp->sizeEnd        = 1.2f;
        comp->gravity        = 0.8f;
        comp->coneAngle      = 20.0f;
        comp->colorR = 0.5f; comp->colorG = 0.5f; comp->colorB = 0.5f; comp->colorA = 0.6f;
        comp->colorEndR = 0.3f; comp->colorEndG = 0.3f; comp->colorEndB = 0.3f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 2: // Sparks
        comp->maxParticles   = 300;
        comp->emissionRate   = 100.0f;
        comp->lifetime       = 0.8f;
        comp->speed          = 8.0f;
        comp->speedVariance  = 4.0f;
        comp->size           = 0.05f;
        comp->sizeEnd        = 0.0f;
        comp->gravity        = -9.81f;
        comp->coneAngle      = 45.0f;
        comp->colorR = 1.0f; comp->colorG = 0.9f; comp->colorB = 0.4f; comp->colorA = 1.0f;
        comp->colorEndR = 1.0f; comp->colorEndG = 0.3f; comp->colorEndB = 0.0f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 3: // Rain
        comp->maxParticles   = 500;
        comp->emissionRate   = 200.0f;
        comp->lifetime       = 1.0f;
        comp->speed          = 15.0f;
        comp->speedVariance  = 2.0f;
        comp->size           = 0.02f;
        comp->sizeEnd        = 0.02f;
        comp->gravity        = -9.81f;
        comp->coneAngle      = 5.0f;
        comp->colorR = 0.7f; comp->colorG = 0.8f; comp->colorB = 1.0f; comp->colorA = 0.5f;
        comp->colorEndR = 0.5f; comp->colorEndG = 0.6f; comp->colorEndB = 0.9f; comp->colorEndA = 0.2f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 4: // Snow
        comp->maxParticles   = 300;
        comp->emissionRate   = 80.0f;
        comp->lifetime       = 5.0f;
        comp->speed          = 0.5f;
        comp->speedVariance  = 0.3f;
        comp->size           = 0.08f;
        comp->sizeEnd        = 0.06f;
        comp->gravity        = -1.0f;
        comp->coneAngle      = 60.0f;
        comp->colorR = 1.0f; comp->colorG = 1.0f; comp->colorB = 1.0f; comp->colorA = 0.9f;
        comp->colorEndR = 0.9f; comp->colorEndG = 0.9f; comp->colorEndB = 1.0f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    case 5: // Magic
        comp->maxParticles   = 200;
        comp->emissionRate   = 50.0f;
        comp->lifetime       = 2.0f;
        comp->speed          = 2.0f;
        comp->speedVariance  = 1.5f;
        comp->size           = 0.15f;
        comp->sizeEnd        = 0.0f;
        comp->gravity        = 0.5f;
        comp->coneAngle      = 90.0f;
        comp->colorR = 0.4f; comp->colorG = 0.2f; comp->colorB = 1.0f; comp->colorA = 1.0f;
        comp->colorEndR = 1.0f; comp->colorEndG = 0.4f; comp->colorEndB = 0.8f; comp->colorEndA = 0.0f;
        comp->enabled = true;
        comp->loop = true;
        break;

    default:
        return;
    }

    m_particleEditorState.presetIndex = presetIndex;

    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
        level->setIsSaved(false);

    // Push grouped undo
    static const char* presetNamesList[] = { "Fire", "Smoke", "Sparks", "Rain", "Snow", "Magic" };
    const std::string cmdName = std::string("Apply Preset: ") + presetNamesList[presetIndex];
    const ECS::ParticleEmitterComponent applied = *comp;
    UndoRedoManager::Instance().pushCommand({
        cmdName,
        [entity, applied]() {
            auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
            if (c) *c = applied;
        },
        [entity, saved]() {
            auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
            if (c) *c = saved;
        }
    });

    refreshParticleEditor();

    static const char* presetToasts[] = { "Fire", "Smoke", "Sparks", "Rain", "Snow", "Magic" };
    showToastMessage(std::string("Applied preset: ") + presetToasts[presetIndex], 2.0f);
}

// ===========================================================================
// Shader Viewer Tab
// ===========================================================================

// ---------------------------------------------------------------------------
// isShaderViewerOpen
// ---------------------------------------------------------------------------
bool UIManager::isShaderViewerOpen() const
{
    return m_shaderViewerState.isOpen;
}

// ---------------------------------------------------------------------------
// openShaderViewerTab
// ---------------------------------------------------------------------------
void UIManager::openShaderViewerTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "ShaderViewer";

    // If already open, just switch to it
    if (m_shaderViewerState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Shader Viewer", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "ShaderViewer.Main";

    // Clean up any stale registration
    unregisterWidget(widgetId);

    // Initialise state
    m_shaderViewerState = {};
    m_shaderViewerState.tabId    = tabId;
    m_shaderViewerState.widgetId = widgetId;
    m_shaderViewerState.isOpen   = true;

    // Scan the shaders directory for .glsl files
    {
        const auto shadersDir = std::filesystem::current_path() / "shaders";
        if (std::filesystem::exists(shadersDir) && std::filesystem::is_directory(shadersDir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(shadersDir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".glsl")
                    m_shaderViewerState.shaderFiles.push_back(entry.path().filename().string());
            }
            std::sort(m_shaderViewerState.shaderFiles.begin(), m_shaderViewerState.shaderFiles.end());
        }

        // Select the first file by default
        if (!m_shaderViewerState.shaderFiles.empty())
            m_shaderViewerState.selectedFile = m_shaderViewerState.shaderFiles[0];
    }

    // Build the main widget (fills entire tab area)
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "ShaderViewer.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // â”€â”€ Toolbar row â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        buildShaderViewerToolbar(root);

        // â”€â”€ Separator â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // â”€â”€ Content area (horizontal split: file list | code view) â”€â”€â”€
        {
            WidgetElement content{};
            content.id          = "ShaderViewer.Content";
            content.type        = WidgetElementType::StackPanel;
            content.fillX       = true;
            content.fillY       = true;
            content.orientation = StackOrientation::Horizontal;
            content.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            content.runtimeOnly = true;

            // Left panel: file list
            {
                WidgetElement fileListPanel{};
                fileListPanel.id          = "ShaderViewer.FileListPanel";
                fileListPanel.type        = WidgetElementType::StackPanel;
                fileListPanel.fillY       = true;
                fileListPanel.scrollable  = true;
                fileListPanel.orientation = StackOrientation::Vertical;
                fileListPanel.minSize     = EditorTheme::Scaled(Vec2{ 180.0f, 0.0f });
                fileListPanel.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
                fileListPanel.style.color = Vec4{ 0.06f, 0.06f, 0.08f, 1.0f };
                fileListPanel.runtimeOnly = true;
                content.children.push_back(std::move(fileListPanel));
            }

            // Vertical separator
            {
                WidgetElement sep{};
                sep.type        = WidgetElementType::Panel;
                sep.fillY       = true;
                sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 0.0f });
                sep.style.color = theme.panelBorder;
                sep.runtimeOnly = true;
                content.children.push_back(std::move(sep));
            }

            // Right panel: code view
            {
                WidgetElement codePanel{};
                codePanel.id          = "ShaderViewer.CodePanel";
                codePanel.type        = WidgetElementType::StackPanel;
                codePanel.fillX       = true;
                codePanel.fillY       = true;
                codePanel.scrollable  = true;
                codePanel.orientation = StackOrientation::Vertical;
                codePanel.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
                codePanel.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
                codePanel.runtimeOnly = true;
                content.children.push_back(std::move(codePanel));
            }

            root.children.push_back(std::move(content));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // â”€â”€ Tab / close click events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshShaderViewer();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeShaderViewerTab();
    });

    // â”€â”€ Toolbar button events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    registerClickEvent("ShaderViewer.Reload", [this]()
    {
        if (m_renderer)
        {
            m_renderer->requestShaderReload();
            showToastMessage("Shaders reloaded", 2.0f);
        }
    });

    // â”€â”€ File list click events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (const auto& filename : m_shaderViewerState.shaderFiles)
    {
        const std::string eventId = "ShaderViewer.File." + filename;
        registerClickEvent(eventId, [this, filename]()
        {
            m_shaderViewerState.selectedFile = filename;
            refreshShaderViewer();
        });
    }

    // Initial population
    refreshShaderViewer();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// closeShaderViewerTab
// ---------------------------------------------------------------------------
void UIManager::closeShaderViewerTab()
{
    if (!m_shaderViewerState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_shaderViewerState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_shaderViewerState.widgetId);

    m_renderer->removeTab(tabId);
    m_shaderViewerState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildShaderViewerToolbar
// ---------------------------------------------------------------------------
void UIManager::buildShaderViewerToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "ShaderViewer.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = "Shader Viewer";
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignH      = TextAlignH::Left;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 100.0f, 24.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Current file label
    {
        WidgetElement fileLabel{};
        fileLabel.id            = "ShaderViewer.CurrentFile";
        fileLabel.type          = WidgetElementType::Text;
        fileLabel.text          = m_shaderViewerState.selectedFile.empty()
                                  ? "(no file selected)"
                                  : m_shaderViewerState.selectedFile;
        fileLabel.font          = theme.fontDefault;
        fileLabel.fontSize      = theme.fontSizeSmall;
        fileLabel.style.textColor = theme.accent;
        fileLabel.textAlignH    = TextAlignH::Left;
        fileLabel.textAlignV    = TextAlignV::Center;
        fileLabel.minSize       = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
        fileLabel.runtimeOnly   = true;
        toolbar.children.push_back(std::move(fileLabel));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Reload button
    {
        WidgetElement btn{};
        btn.id              = "ShaderViewer.Reload";
        btn.type            = WidgetElementType::Button;
        btn.text            = "Reload Shaders";
        btn.font            = theme.fontDefault;
        btn.fontSize        = theme.fontSizeSmall;
        btn.style.textColor = theme.textPrimary;
        btn.style.color     = theme.accent;
        btn.style.hoverColor = theme.buttonHover;
        btn.textAlignH      = TextAlignH::Center;
        btn.textAlignV      = TextAlignV::Center;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
        btn.padding         = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        btn.hitTestMode     = HitTestMode::Enabled;
        btn.runtimeOnly     = true;
        btn.clickEvent      = "ShaderViewer.Reload";
        toolbar.children.push_back(std::move(btn));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// buildShaderFileList  â€“ populates the left panel with clickable file names
// ---------------------------------------------------------------------------
void UIManager::buildShaderFileList(WidgetElement& fileListPanel)
{
    const auto& theme = EditorTheme::Get();
    const float rowH = EditorTheme::Scaled(22.0f);

    // Section heading
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Shader Files";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSmall;
        heading.style.textColor = theme.textMuted;
        heading.textAlignH      = TextAlignH::Left;
        heading.textAlignV      = TextAlignV::Center;
        heading.fillX           = true;
        heading.minSize         = Vec2{ 0.0f, rowH };
        heading.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        heading.runtimeOnly     = true;
        fileListPanel.children.push_back(std::move(heading));
    }

    for (const auto& filename : m_shaderViewerState.shaderFiles)
    {
        const bool selected = (filename == m_shaderViewerState.selectedFile);

        WidgetElement row{};
        row.id              = "ShaderViewer.File." + filename;
        row.type            = WidgetElementType::Button;
        row.text            = filename;
        row.font            = theme.fontDefault;
        row.fontSize        = theme.fontSizeSmall;
        row.style.textColor = selected ? theme.textPrimary : theme.textSecondary;
        row.style.color     = selected ? theme.selectionHighlight : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.style.hoverColor = theme.treeRowHover;
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.fillX           = true;
        row.minSize         = Vec2{ 0.0f, rowH };
        row.padding         = EditorTheme::Scaled(Vec2{ 6.0f, 1.0f });
        row.hitTestMode     = HitTestMode::Enabled;
        row.runtimeOnly     = true;
        row.clickEvent      = "ShaderViewer.File." + filename;
        fileListPanel.children.push_back(std::move(row));
    }
}

// ---------------------------------------------------------------------------
// buildShaderCodeView  â€“ reads the selected shader file and displays it with
//                        basic GLSL syntax highlighting
// ---------------------------------------------------------------------------
void UIManager::buildShaderCodeView(WidgetElement& codePanel)
{
    const auto& theme = EditorTheme::Get();

    if (m_shaderViewerState.selectedFile.empty())
    {
        WidgetElement hint = EditorUIBuilder::makeSecondaryLabel("Select a shader file from the list.");
        codePanel.children.push_back(std::move(hint));
        return;
    }

    // Read the shader file
    const auto filePath = std::filesystem::current_path() / "shaders" / m_shaderViewerState.selectedFile;
    std::ifstream ifs(filePath);
    if (!ifs.is_open())
    {
        WidgetElement err{};
        err.type            = WidgetElementType::Text;
        err.text            = "Failed to open: " + m_shaderViewerState.selectedFile;
        err.font            = theme.fontDefault;
        err.fontSize        = theme.fontSizeSmall;
        err.style.textColor = theme.errorColor;
        err.textAlignH      = TextAlignH::Left;
        err.textAlignV      = TextAlignV::Center;
        err.fillX           = true;
        err.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        err.runtimeOnly     = true;
        codePanel.children.push_back(std::move(err));
        return;
    }

    // GLSL syntax-highlighting colour definitions
    const Vec4 colKeyword   { 0.40f, 0.60f, 1.00f, 1.0f };  // blue â€“ keywords
    const Vec4 colType      { 0.30f, 0.80f, 0.55f, 1.0f };  // green â€“ types
    const Vec4 colPreproc   { 0.70f, 0.45f, 0.85f, 1.0f };  // purple â€“ preprocessor
    const Vec4 colComment   { 0.45f, 0.50f, 0.55f, 1.0f };  // gray â€“ comments
    const Vec4 colNumber    { 0.85f, 0.65f, 0.30f, 1.0f };  // orange â€“ numeric literals
    const Vec4 colString    { 0.85f, 0.55f, 0.40f, 1.0f };  // coral â€“ string literals (rare in GLSL)
    const Vec4 colQualifier { 0.90f, 0.75f, 0.30f, 1.0f };  // gold â€“ uniform/in/out qualifiers
    const Vec4 colDefault   = theme.textSecondary;            // default text

    // Keyword/type sets for classification
    static const std::unordered_set<std::string> keywords = {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "break", "continue", "return", "discard", "struct", "const",
        "true", "false", "void", "main"
    };
    static const std::unordered_set<std::string> types = {
        "float", "int", "uint", "bool", "double",
        "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
        "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4",
        "dvec2", "dvec3", "dvec4",
        "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4",
        "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4",
        "sampler1D", "sampler2D", "sampler3D", "samplerCube",
        "sampler2DShadow", "sampler2DArray", "samplerCubeShadow"
    };
    static const std::unordered_set<std::string> qualifiers = {
        "uniform", "in", "out", "inout", "attribute", "varying",
        "layout", "flat", "smooth", "noperspective",
        "highp", "mediump", "lowp", "precision"
    };

    // Helper: classify a token and return its colour
    auto tokenColor = [&](const std::string& token) -> Vec4
    {
        if (keywords.count(token))   return colKeyword;
        if (types.count(token))      return colType;
        if (qualifiers.count(token)) return colQualifier;
        return colDefault;
    };

    const float lineH = EditorTheme::Scaled(16.0f);
    int lineNumber = 0;
    bool inBlockComment = false;

    std::string line;
    while (std::getline(ifs, line))
    {
        ++lineNumber;

        // Determine dominant colour for the line (simplified approach:
        // one colour per line based on first significant token).
        Vec4 lineColor = colDefault;

        std::string trimmed = line;
        // ltrim
        size_t firstNonSpace = trimmed.find_first_not_of(" \t\r\n");
        if (firstNonSpace != std::string::npos)
            trimmed = trimmed.substr(firstNonSpace);
        else
            trimmed.clear();

        if (inBlockComment)
        {
            lineColor = colComment;
            if (trimmed.find("*/") != std::string::npos)
                inBlockComment = false;
        }
        else if (trimmed.empty())
        {
            lineColor = colDefault;
        }
        else if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/')
        {
            lineColor = colComment;
        }
        else if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '*')
        {
            lineColor = colComment;
            if (trimmed.find("*/") == std::string::npos)
                inBlockComment = true;
        }
        else if (trimmed[0] == '#')
        {
            lineColor = colPreproc;
        }
        else
        {
            // Extract the first identifier token
            std::string firstToken;
            for (size_t i = 0; i < trimmed.size(); ++i)
            {
                char c = trimmed[i];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                    firstToken += c;
                else
                    break;
            }
            if (!firstToken.empty())
                lineColor = tokenColor(firstToken);
        }

        // Build line number prefix
        char lineNumBuf[12];
        std::snprintf(lineNumBuf, sizeof(lineNumBuf), "%4d  ", lineNumber);

        WidgetElement row{};
        row.id              = "ShaderViewer.Line." + std::to_string(lineNumber);
        row.type            = WidgetElementType::Text;
        row.text            = std::string(lineNumBuf) + line;
        row.font            = theme.fontDefault;
        row.fontSize        = theme.fontSizeMonospace;
        row.style.textColor = lineColor;
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.fillX           = true;
        row.minSize         = Vec2{ 0.0f, lineH };
        row.runtimeOnly     = true;

        codePanel.children.push_back(std::move(row));
    }

    // File info footer
    {
        WidgetElement footer{};
        footer.type            = WidgetElementType::Text;
        footer.text            = "â”€â”€ " + std::to_string(lineNumber) + " lines â”€â”€";
        footer.font            = theme.fontDefault;
        footer.fontSize        = theme.fontSizeCaption;
        footer.style.textColor = theme.textMuted;
        footer.textAlignH      = TextAlignH::Left;
        footer.textAlignV      = TextAlignV::Center;
        footer.fillX           = true;
        footer.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        footer.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
        footer.runtimeOnly     = true;
        codePanel.children.push_back(std::move(footer));
    }
}

// ---------------------------------------------------------------------------
// refreshShaderViewer  â€“ rebuilds file list + code view
// ---------------------------------------------------------------------------
void UIManager::refreshShaderViewer()
{
    if (!m_shaderViewerState.isOpen)
        return;

    auto* entry = findWidgetEntry(m_shaderViewerState.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find the content area (horizontal StackPanel)
    WidgetElement* content = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "ShaderViewer.Content")
        {
            content = &child;
            break;
        }
    }
    if (!content || content->children.size() < 3)
        return;

    // Also rebuild toolbar to update current file label
    {
        WidgetElement* toolbar = nullptr;
        for (auto& child : elements[0].children)
        {
            if (child.id == "ShaderViewer.Toolbar")
            {
                toolbar = &child;
                break;
            }
        }
        if (toolbar)
        {
            toolbar->children.clear();
            // Rebuild inline (same as buildShaderViewerToolbar but into existing element)
            const auto& theme = EditorTheme::Get();

            // Title label
            {
                WidgetElement title{};
                title.type            = WidgetElementType::Text;
                title.text            = "Shader Viewer";
                title.font            = theme.fontDefault;
                title.fontSize        = theme.fontSizeSubheading;
                title.style.textColor = theme.textPrimary;
                title.textAlignH      = TextAlignH::Left;
                title.textAlignV      = TextAlignV::Center;
                title.minSize         = EditorTheme::Scaled(Vec2{ 100.0f, 24.0f });
                title.runtimeOnly     = true;
                toolbar->children.push_back(std::move(title));
            }

            // Current file label
            {
                WidgetElement fileLabel{};
                fileLabel.id            = "ShaderViewer.CurrentFile";
                fileLabel.type          = WidgetElementType::Text;
                fileLabel.text          = m_shaderViewerState.selectedFile.empty()
                                          ? "(no file selected)"
                                          : m_shaderViewerState.selectedFile;
                fileLabel.font          = theme.fontDefault;
                fileLabel.fontSize      = theme.fontSizeSmall;
                fileLabel.style.textColor = theme.accent;
                fileLabel.textAlignH    = TextAlignH::Left;
                fileLabel.textAlignV    = TextAlignV::Center;
                fileLabel.minSize       = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
                fileLabel.runtimeOnly   = true;
                toolbar->children.push_back(std::move(fileLabel));
            }

            // Spacer
            {
                WidgetElement spacer{};
                spacer.type        = WidgetElementType::Panel;
                spacer.fillX       = true;
                spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.runtimeOnly = true;
                toolbar->children.push_back(std::move(spacer));
            }

            // Reload button
            {
                WidgetElement btn{};
                btn.id              = "ShaderViewer.Reload";
                btn.type            = WidgetElementType::Button;
                btn.text            = "Reload Shaders";
                btn.font            = theme.fontDefault;
                btn.fontSize        = theme.fontSizeSmall;
                btn.style.textColor = theme.textPrimary;
                btn.style.color     = theme.accent;
                btn.style.hoverColor = theme.buttonHover;
                btn.textAlignH      = TextAlignH::Center;
                btn.textAlignV      = TextAlignV::Center;
                btn.minSize         = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
                btn.padding         = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
                btn.hitTestMode     = HitTestMode::Enabled;
                btn.runtimeOnly     = true;
                btn.clickEvent      = "ShaderViewer.Reload";
                toolbar->children.push_back(std::move(btn));
            }
        }
    }

    // Rebuild file list (children[0])
    content->children[0].children.clear();
    buildShaderFileList(content->children[0]);

    // Rebuild code view (children[2], after the separator at children[1])
    content->children[2].children.clear();
    buildShaderCodeView(content->children[2]);

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ===========================================================================
// Render-Pass-Debugger Tab
// ===========================================================================

// ---------------------------------------------------------------------------
// isRenderDebuggerOpen
// ---------------------------------------------------------------------------
bool UIManager::isRenderDebuggerOpen() const
{
    return m_renderDebuggerState.isOpen;
}

// ---------------------------------------------------------------------------
// openRenderDebuggerTab
// ---------------------------------------------------------------------------
void UIManager::openRenderDebuggerTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "RenderDebugger";

    // If already open, just switch to it
    if (m_renderDebuggerState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Render Debugger", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "RenderDebugger.Main";

    // Clean up any stale registration
    unregisterWidget(widgetId);

    // Initialise state
    m_renderDebuggerState = {};
    m_renderDebuggerState.tabId    = tabId;
    m_renderDebuggerState.widgetId = widgetId;
    m_renderDebuggerState.isOpen   = true;

    // Build the main widget (fills entire tab area)
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "RenderDebugger.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // â”€â”€ Toolbar row â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        buildRenderDebuggerToolbar(root);

        // â”€â”€ Separator â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // â”€â”€ Scrollable pass list area â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        {
            WidgetElement passArea{};
            passArea.id          = "RenderDebugger.PassArea";
            passArea.type        = WidgetElementType::StackPanel;
            passArea.fillX       = true;
            passArea.fillY       = true;
            passArea.scrollable  = true;
            passArea.orientation = StackOrientation::Vertical;
            passArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            passArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            passArea.runtimeOnly = true;
            root.children.push_back(std::move(passArea));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // â”€â”€ Tab / close click events â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshRenderDebugger();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeRenderDebuggerTab();
    });

    // Initial population
    refreshRenderDebugger();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// closeRenderDebuggerTab
// ---------------------------------------------------------------------------
void UIManager::closeRenderDebuggerTab()
{
    if (!m_renderDebuggerState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_renderDebuggerState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_renderDebuggerState.widgetId);

    m_renderer->removeTab(tabId);
    m_renderDebuggerState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildRenderDebuggerToolbar
// ---------------------------------------------------------------------------
void UIManager::buildRenderDebuggerToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "RenderDebugger.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = "Render-Pass Debugger";
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignH      = TextAlignH::Left;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Pass count summary
    if (m_renderer)
    {
        const auto passes = m_renderer->getRenderPassInfo();
        int enabledCount = 0;
        for (const auto& p : passes)
            if (p.enabled) ++enabledCount;

        WidgetElement summary{};
        summary.type            = WidgetElementType::Text;
        summary.text            = std::to_string(enabledCount) + " / " + std::to_string(passes.size()) + " passes active";
        summary.font            = theme.fontDefault;
        summary.fontSize        = theme.fontSizeSmall;
        summary.style.textColor = theme.textMuted;
        summary.textAlignH      = TextAlignH::Right;
        summary.textAlignV      = TextAlignV::Center;
        summary.minSize         = EditorTheme::Scaled(Vec2{ 140.0f, 24.0f });
        summary.runtimeOnly     = true;
        toolbar.children.push_back(std::move(summary));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// refreshRenderDebugger  â€“ rebuilds the pass list with current pipeline state
// ---------------------------------------------------------------------------
void UIManager::refreshRenderDebugger()
{
    if (!m_renderDebuggerState.isOpen || !m_renderer)
        return;

    auto* entry = findWidgetEntry(m_renderDebuggerState.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find the pass-area container
    WidgetElement* passArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "RenderDebugger.PassArea")
        {
            passArea = &child;
            break;
        }
    }
    if (!passArea)
        return;

    // Rebuild toolbar
    {
        WidgetElement* toolbar = nullptr;
        for (auto& child : elements[0].children)
        {
            if (child.id == "RenderDebugger.Toolbar")
            {
                toolbar = &child;
                break;
            }
        }
        if (toolbar)
        {
            toolbar->children.clear();
            const auto& theme = EditorTheme::Get();

            // Title
            {
                WidgetElement title{};
                title.type            = WidgetElementType::Text;
                title.text            = "Render-Pass Debugger";
                title.font            = theme.fontDefault;
                title.fontSize        = theme.fontSizeSubheading;
                title.style.textColor = theme.textPrimary;
                title.textAlignH      = TextAlignH::Left;
                title.textAlignV      = TextAlignV::Center;
                title.minSize         = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
                title.runtimeOnly     = true;
                toolbar->children.push_back(std::move(title));
            }

            // Spacer
            {
                WidgetElement spacer{};
                spacer.type        = WidgetElementType::Panel;
                spacer.fillX       = true;
                spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.runtimeOnly = true;
                toolbar->children.push_back(std::move(spacer));
            }

            // Summary
            {
                const auto passes = m_renderer->getRenderPassInfo();
                int enabledCount = 0;
                for (const auto& p : passes)
                    if (p.enabled) ++enabledCount;

                WidgetElement summary{};
                summary.type            = WidgetElementType::Text;
                summary.text            = std::to_string(enabledCount) + " / " + std::to_string(passes.size()) + " passes active";
                summary.font            = theme.fontDefault;
                summary.fontSize        = theme.fontSizeSmall;
                summary.style.textColor = theme.textMuted;
                summary.textAlignH      = TextAlignH::Right;
                summary.textAlignV      = TextAlignV::Center;
                summary.minSize         = EditorTheme::Scaled(Vec2{ 140.0f, 24.0f });
                summary.runtimeOnly     = true;
                toolbar->children.push_back(std::move(summary));
            }
        }
    }

    passArea->children.clear();

    const auto& theme = EditorTheme::Get();
    const auto passes = m_renderer->getRenderPassInfo();

    // Timing summary from DiagnosticsManager
    const auto& metrics = DiagnosticsManager::Instance().getLatestMetrics();

    // â”€â”€ Frame timing header â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Frame: %.1f FPS | CPU World: %.2f ms | CPU UI: %.2f ms | GPU: %.2f ms",
            metrics.fps, metrics.cpuWorldMs, metrics.cpuUiMs, metrics.gpuFrameMs);

        WidgetElement header{};
        header.type            = WidgetElementType::Text;
        header.text            = buf;
        header.font            = theme.fontDefault;
        header.fontSize        = theme.fontSizeSmall;
        header.style.textColor = theme.accent;
        header.textAlignH      = TextAlignH::Left;
        header.textAlignV      = TextAlignV::Center;
        header.fillX           = true;
        header.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        header.runtimeOnly     = true;
        passArea->children.push_back(std::move(header));
    }

    // Object count row
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Objects: %u visible, %u culled, %u total",
            metrics.visibleCount, metrics.hiddenCount, metrics.totalCount);

        WidgetElement row{};
        row.type            = WidgetElementType::Text;
        row.text            = buf;
        row.font            = theme.fontDefault;
        row.fontSize        = theme.fontSizeSmall;
        row.style.textColor = theme.textSecondary;
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.fillX           = true;
        row.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        row.runtimeOnly     = true;
        passArea->children.push_back(std::move(row));
    }

    // â”€â”€ Divider â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement div{};
        div.type        = WidgetElementType::Panel;
        div.fillX       = true;
        div.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        div.style.color = theme.panelBorder;
        div.runtimeOnly = true;
        passArea->children.push_back(std::move(div));
    }

    // â”€â”€ Pipeline passes â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::string lastCategory;
    int passIndex = 0;

    // Category colour mapping
    auto categoryColor = [&](const std::string& cat) -> Vec4
    {
        if (cat == "Shadow")       return Vec4{ 0.60f, 0.45f, 0.80f, 1.0f };
        if (cat == "Geometry")     return Vec4{ 0.40f, 0.70f, 0.90f, 1.0f };
        if (cat == "Post-Process") return Vec4{ 0.85f, 0.65f, 0.30f, 1.0f };
        if (cat == "Overlay")      return Vec4{ 0.40f, 0.80f, 0.55f, 1.0f };
        if (cat == "Utility")      return Vec4{ 0.65f, 0.65f, 0.65f, 1.0f };
        if (cat == "UI")           return Vec4{ 0.90f, 0.55f, 0.55f, 1.0f };
        return theme.textSecondary;
    };

    const float rowH = EditorTheme::Scaled(20.0f);

    for (const auto& pass : passes)
    {
        // Category header when it changes
        if (pass.category != lastCategory)
        {
            lastCategory = pass.category;

            if (passIndex > 0)
            {
                WidgetElement space{};
                space.type        = WidgetElementType::Panel;
                space.fillX       = true;
                space.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
                space.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                space.runtimeOnly = true;
                passArea->children.push_back(std::move(space));
            }

            WidgetElement catLabel{};
            catLabel.type            = WidgetElementType::Text;
            catLabel.text            = "\xe2\x94\x80\xe2\x94\x80 " + pass.category + " \xe2\x94\x80\xe2\x94\x80";
            catLabel.font            = theme.fontDefault;
            catLabel.fontSize        = theme.fontSizeSmall;
            catLabel.style.textColor = categoryColor(pass.category);
            catLabel.textAlignH      = TextAlignH::Left;
            catLabel.textAlignV      = TextAlignV::Center;
            catLabel.fillX           = true;
            catLabel.minSize         = Vec2{ 0.0f, rowH };
            catLabel.padding         = EditorTheme::Scaled(Vec2{ 2.0f, 2.0f });
            catLabel.runtimeOnly     = true;
            passArea->children.push_back(std::move(catLabel));
        }

        // Pass row: [status] [name] [FBO info] [details]
        {
            WidgetElement passRow{};
            passRow.id          = "RenderDebugger.Pass." + std::to_string(passIndex);
            passRow.type        = WidgetElementType::StackPanel;
            passRow.fillX       = true;
            passRow.orientation = StackOrientation::Horizontal;
            passRow.minSize     = Vec2{ 0.0f, rowH };
            passRow.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 1.0f });
            passRow.style.color = (passIndex % 2 == 0)
                                  ? Vec4{ 0.07f, 0.07f, 0.09f, 1.0f }
                                  : Vec4{ 0.09f, 0.09f, 0.11f, 1.0f };
            passRow.runtimeOnly = true;

            // Status indicator
            {
                WidgetElement status{};
                status.type            = WidgetElementType::Text;
                status.text            = pass.enabled ? "\xe2\x97\x8f" : "\xe2\x97\x8b";
                status.font            = theme.fontDefault;
                status.fontSize        = theme.fontSizeSmall;
                status.style.textColor = pass.enabled ? theme.successColor : theme.textMuted;
                status.textAlignH      = TextAlignH::Center;
                status.textAlignV      = TextAlignV::Center;
                status.minSize         = EditorTheme::Scaled(Vec2{ 20.0f, rowH });
                status.runtimeOnly     = true;
                passRow.children.push_back(std::move(status));
            }

            // Pass name
            {
                WidgetElement name{};
                name.type            = WidgetElementType::Text;
                name.text            = pass.name;
                name.font            = theme.fontDefault;
                name.fontSize        = theme.fontSizeSmall;
                name.style.textColor = pass.enabled ? theme.textPrimary : theme.textMuted;
                name.textAlignH      = TextAlignH::Left;
                name.textAlignV      = TextAlignV::Center;
                name.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, rowH });
                name.runtimeOnly     = true;
                passRow.children.push_back(std::move(name));
            }

            // FBO format / resolution
            {
                std::string fboText = pass.fboFormat;
                if (pass.fboWidth > 0 && pass.fboHeight > 0)
                    fboText = std::to_string(pass.fboWidth) + "x" + std::to_string(pass.fboHeight) + " " + pass.fboFormat;

                WidgetElement fbo{};
                fbo.type            = WidgetElementType::Text;
                fbo.text            = fboText;
                fbo.font            = theme.fontDefault;
                fbo.fontSize        = theme.fontSizeCaption;
                fbo.style.textColor = categoryColor(pass.category);
                fbo.textAlignH      = TextAlignH::Left;
                fbo.textAlignV      = TextAlignV::Center;
                fbo.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, rowH });
                fbo.runtimeOnly     = true;
                passRow.children.push_back(std::move(fbo));
            }

            // Details
            {
                WidgetElement det{};
                det.type            = WidgetElementType::Text;
                det.text            = pass.details;
                det.font            = theme.fontDefault;
                det.fontSize        = theme.fontSizeCaption;
                det.style.textColor = theme.textSecondary;
                det.textAlignH      = TextAlignH::Left;
                det.textAlignV      = TextAlignV::Center;
                det.fillX           = true;
                det.minSize         = Vec2{ 0.0f, rowH };
                det.runtimeOnly     = true;
                passRow.children.push_back(std::move(det));
            }

            passArea->children.push_back(std::move(passRow));
        }

        ++passIndex;
    }

    // â”€â”€ Pipeline flow diagram â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    {
        WidgetElement space{};
        space.type        = WidgetElementType::Panel;
        space.fillX       = true;
        space.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 10.0f });
        space.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        space.runtimeOnly = true;
        passArea->children.push_back(std::move(space));
    }

    {
        WidgetElement flowTitle{};
        flowTitle.type            = WidgetElementType::Text;
        flowTitle.text            = "\xe2\x94\x80\xe2\x94\x80 Pipeline Flow \xe2\x94\x80\xe2\x94\x80";
        flowTitle.font            = theme.fontDefault;
        flowTitle.fontSize        = theme.fontSizeSmall;
        flowTitle.style.textColor = theme.textMuted;
        flowTitle.textAlignH      = TextAlignH::Left;
        flowTitle.textAlignV      = TextAlignV::Center;
        flowTitle.fillX           = true;
        flowTitle.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        flowTitle.runtimeOnly     = true;
        passArea->children.push_back(std::move(flowTitle));
    }

    // Build a simplified flow text
    {
        std::string flow;
        flow += "Shadows -> Skybox -> Geometry (Opaque)";
        flow += " -> Particles -> OIT -> HZB";
        flow += " -> Resolve (Bloom + SSAO + ToneMap + Gamma)";
        flow += " -> Grid -> Colliders -> Bones -> Outline -> Gizmo -> FXAA -> UI";

        WidgetElement flowLine{};
        flowLine.type            = WidgetElementType::Text;
        flowLine.text            = flow;
        flowLine.font            = theme.fontDefault;
        flowLine.fontSize        = theme.fontSizeCaption;
        flowLine.style.textColor = theme.accent;
        flowLine.textAlignH      = TextAlignH::Left;
        flowLine.textAlignV      = TextAlignV::Center;
        flowLine.fillX           = true;
        flowLine.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        flowLine.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
        flowLine.runtimeOnly     = true;
        passArea->children.push_back(std::move(flowLine));
    }

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ===========================================================================
// Cinematic Sequencer Tab (Phase 11.2)
// ===========================================================================

bool UIManager::isSequencerOpen() const
{
    return m_sequencerState.isOpen;
}

void UIManager::openSequencerTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "Sequencer";

    if (m_sequencerState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Sequencer", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "Sequencer.Main";
    unregisterWidget(widgetId);

    m_sequencerState = {};
    m_sequencerState.tabId    = tabId;
    m_sequencerState.widgetId = widgetId;
    m_sequencerState.isOpen   = true;

    // Seed from existing camera path if one is set
    {
        auto pts = m_renderer->getCameraPathPoints();
        if (!pts.empty())
        {
            m_sequencerState.pathDuration = m_renderer->getCameraPathDuration();
            m_sequencerState.loopPlayback = m_renderer->getCameraPathLoop();
        }
    }

    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "Sequencer.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // Toolbar
        buildSequencerToolbar(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Timeline area
        buildSequencerTimeline(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Keyframe list (scrollable)
        buildSequencerKeyframeList(root);

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // Tab / close click events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshSequencerTimeline();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeSequencerTab();
    });

    // Toolbar button events
    registerClickEvent("Sequencer.AddKeyframe", [this]()
    {
        if (!m_renderer) return;
        auto pts = m_renderer->getCameraPathPoints();
        CameraPathPoint pt;
        pt.position = m_renderer->getCameraPosition();
        auto rot = m_renderer->getCameraRotationDegrees();
        pt.yaw   = rot.x;
        pt.pitch = rot.y;
        pts.push_back(pt);
        m_renderer->setCameraPathPoints(pts);
        m_sequencerState.selectedKeyframe = static_cast<int>(pts.size()) - 1;
        refreshSequencerTimeline();
        markAllWidgetsDirty();
        showToastMessage("Keyframe " + std::to_string(pts.size()) + " added", 1.5f);
    });

    registerClickEvent("Sequencer.RemoveKeyframe", [this]()
    {
        if (!m_renderer) return;
        auto pts = m_renderer->getCameraPathPoints();
        int sel = m_sequencerState.selectedKeyframe;
        if (sel < 0 || sel >= static_cast<int>(pts.size())) return;
        pts.erase(pts.begin() + sel);
        m_renderer->setCameraPathPoints(pts);
        if (sel >= static_cast<int>(pts.size()))
            m_sequencerState.selectedKeyframe = static_cast<int>(pts.size()) - 1;
        refreshSequencerTimeline();
        markAllWidgetsDirty();
        showToastMessage("Keyframe removed", 1.5f);
    });

    registerClickEvent("Sequencer.Play", [this]()
    {
        if (!m_renderer) return;
        auto pts = m_renderer->getCameraPathPoints();
        if (pts.size() < 2)
        {
            showToastMessage("Need at least 2 keyframes", 2.0f);
            return;
        }
        if (m_renderer->isCameraPathPlaying())
        {
            m_renderer->pauseCameraPath();
            m_sequencerState.playing = false;
        }
        else if (m_sequencerState.playing)
        {
            m_renderer->resumeCameraPath();
            m_sequencerState.playing = true;
        }
        else
        {
            m_renderer->setCameraPathDuration(m_sequencerState.pathDuration);
            m_renderer->setCameraPathLoop(m_sequencerState.loopPlayback);
            m_renderer->startCameraPath(pts, m_sequencerState.pathDuration, m_sequencerState.loopPlayback);
            m_sequencerState.playing = true;
        }
        refreshSequencerTimeline();
        markAllWidgetsDirty();
    });

    registerClickEvent("Sequencer.Stop", [this]()
    {
        if (!m_renderer) return;
        m_renderer->stopCameraPath();
        m_sequencerState.playing = false;
        m_sequencerState.scrubberT = 0.0f;
        refreshSequencerTimeline();
        markAllWidgetsDirty();
    });

    registerClickEvent("Sequencer.Loop", [this]()
    {
        m_sequencerState.loopPlayback = !m_sequencerState.loopPlayback;
        if (m_renderer)
            m_renderer->setCameraPathLoop(m_sequencerState.loopPlayback);
        refreshSequencerTimeline();
        markAllWidgetsDirty();
    });

    registerClickEvent("Sequencer.ShowSpline", [this]()
    {
        m_sequencerState.showSplineInViewport = !m_sequencerState.showSplineInViewport;
        refreshSequencerTimeline();
        markAllWidgetsDirty();
    });

    // Keyframe click events (registered dynamically during refresh)

    refreshSequencerTimeline();
    markAllWidgetsDirty();
}

void UIManager::closeSequencerTab()
{
    if (!m_sequencerState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_sequencerState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_sequencerState.widgetId);
    m_renderer->removeTab(tabId);
    m_sequencerState = {};
    markAllWidgetsDirty();
}

void UIManager::buildSequencerToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "Sequencer.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.fillX       = true;
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
    toolbar.spacing     = EditorTheme::Scaled(6.0f);
    toolbar.style.color = theme.titleBarBackground;
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement lbl{};
        lbl.type             = WidgetElementType::Label;
        lbl.text             = "Sequencer";
        lbl.style.textColor  = theme.textPrimary;
        lbl.fontSize         = EditorTheme::Scaled(13.0f);
        lbl.runtimeOnly      = true;
        toolbar.children.push_back(std::move(lbl));
    }

    // Spacer
    {
        WidgetElement sp{};
        sp.type        = WidgetElementType::Panel;
        sp.fillX       = true;
        sp.minSize     = EditorTheme::Scaled(Vec2{ 4.0f, 1.0f });
        sp.style.color = Vec4{ 0,0,0,0 };
        sp.runtimeOnly = true;
        toolbar.children.push_back(std::move(sp));
    }

    auto makeBtn = [&](const std::string& id, const std::string& text, const std::string& tooltip, const Vec4& color) {
        WidgetElement btn{};
        btn.id              = id;
        btn.type            = WidgetElementType::Button;
        btn.clickEvent      = id;
        btn.text            = text;
        btn.tooltipText     = tooltip;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 26.0f, 24.0f });
        btn.style.color     = theme.inputBackground;
        btn.style.textColor = color;
        btn.fontSize        = EditorTheme::Scaled(13.0f);
        btn.runtimeOnly     = true;
        return btn;
    };

    toolbar.children.push_back(makeBtn("Sequencer.AddKeyframe", "+", "Add Keyframe at Camera", theme.accentGreen));
    toolbar.children.push_back(makeBtn("Sequencer.RemoveKeyframe", "-", "Remove Selected Keyframe", Vec4{1.0f, 0.4f, 0.4f, 1.0f}));

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 20.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        toolbar.children.push_back(std::move(sep));
    }

    toolbar.children.push_back(makeBtn("Sequencer.Play",
        m_sequencerState.playing ? "\xe2\x8f\xb8" : "\xe2\x96\xb6",
        m_sequencerState.playing ? "Pause" : "Play",
        theme.accent));
    toolbar.children.push_back(makeBtn("Sequencer.Stop", "\xe2\x96\xa0", "Stop", theme.textPrimary));
    toolbar.children.push_back(makeBtn("Sequencer.Loop",
        m_sequencerState.loopPlayback ? "\xe2\x86\xbb" : "\xe2\x86\xbb",
        m_sequencerState.loopPlayback ? "Loop: ON" : "Loop: OFF",
        m_sequencerState.loopPlayback ? theme.accent : Vec4{0.45f, 0.45f, 0.45f, 1.0f}));

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 20.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        toolbar.children.push_back(std::move(sep));
    }

    toolbar.children.push_back(makeBtn("Sequencer.ShowSpline",
        "\xe2\x97\x86",
        m_sequencerState.showSplineInViewport ? "Spline Visible" : "Spline Hidden",
        m_sequencerState.showSplineInViewport ? theme.accent : Vec4{0.45f, 0.45f, 0.45f, 1.0f}));

    // Duration label + value
    {
        WidgetElement durLbl{};
        durLbl.type             = WidgetElementType::Label;
        durLbl.text             = "Duration:";
        durLbl.style.textColor  = theme.textSecondary;
        durLbl.fontSize         = EditorTheme::Scaled(12.0f);
        durLbl.runtimeOnly      = true;
        toolbar.children.push_back(std::move(durLbl));
    }
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(m_sequencerState.pathDuration));
        WidgetElement durVal{};
        durVal.id              = "Sequencer.Duration";
        durVal.type            = WidgetElementType::Label;
        durVal.text            = buf;
        durVal.style.textColor = theme.textPrimary;
        durVal.fontSize        = EditorTheme::Scaled(12.0f);
        durVal.runtimeOnly     = true;
        toolbar.children.push_back(std::move(durVal));
    }

    root.children.push_back(std::move(toolbar));
}

void UIManager::buildSequencerTimeline(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement timelineArea{};
    timelineArea.id          = "Sequencer.TimelineArea";
    timelineArea.type        = WidgetElementType::Panel;
    timelineArea.fillX       = true;
    timelineArea.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 60.0f });
    timelineArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 6.0f });
    timelineArea.style.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
    timelineArea.runtimeOnly = true;

    // Track label
    {
        WidgetElement lbl{};
        lbl.type            = WidgetElementType::Label;
        lbl.text            = "Camera Path";
        lbl.style.textColor = theme.textSecondary;
        lbl.fontSize        = EditorTheme::Scaled(11.0f);
        lbl.from            = Vec2{ 0.0f, 0.0f };
        lbl.to              = Vec2{ 0.0f, 0.0f };
        lbl.runtimeOnly     = true;
        timelineArea.children.push_back(std::move(lbl));
    }

    // Timeline bar
    {
        WidgetElement bar{};
        bar.id          = "Sequencer.TimelineBar";
        bar.type        = WidgetElementType::Panel;
        bar.fillX       = true;
        bar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        bar.style.color = Vec4{ 0.15f, 0.16f, 0.19f, 1.0f };
        bar.runtimeOnly = true;
        bar.style.borderRadius = EditorTheme::Scaled(3.0f);

        // Draw keyframe markers on the bar
        if (m_renderer)
        {
            auto pts = m_renderer->getCameraPathPoints();
            const int n = static_cast<int>(pts.size());
            for (int i = 0; i < n; ++i)
            {
                float tNorm = (n <= 1) ? 0.5f : static_cast<float>(i) / static_cast<float>(n - 1);
                WidgetElement marker{};
                marker.id   = "Sequencer.KF." + std::to_string(i);
                marker.type = WidgetElementType::Panel;
                marker.minSize = EditorTheme::Scaled(Vec2{ 8.0f, 18.0f });
                marker.from = Vec2{ tNorm, 0.1f };
                marker.to   = Vec2{ tNorm, 0.9f };
                bool selected = (i == m_sequencerState.selectedKeyframe);
                marker.style.color = selected
                    ? theme.accent
                    : Vec4{ 0.7f, 0.7f, 0.7f, 1.0f };
                marker.style.borderRadius = EditorTheme::Scaled(2.0f);
                marker.runtimeOnly = true;
                marker.clickEvent  = "Sequencer.SelectKF." + std::to_string(i);
                bar.children.push_back(std::move(marker));
            }

            // Scrubber position indicator
            if (m_sequencerState.playing || m_renderer->isCameraPathPlaying())
            {
                float progress = m_renderer->getCameraPathProgress();
                WidgetElement scrubber{};
                scrubber.id   = "Sequencer.Scrubber";
                scrubber.type = WidgetElementType::Panel;
                scrubber.minSize = EditorTheme::Scaled(Vec2{ 3.0f, 24.0f });
                scrubber.from = Vec2{ progress, 0.0f };
                scrubber.to   = Vec2{ progress, 1.0f };
                scrubber.style.color = Vec4{ 1.0f, 0.3f, 0.3f, 1.0f };
                scrubber.runtimeOnly = true;
                bar.children.push_back(std::move(scrubber));
            }
        }

        timelineArea.children.push_back(std::move(bar));
    }

    root.children.push_back(std::move(timelineArea));
}

void UIManager::buildSequencerKeyframeList(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement listArea{};
    listArea.id          = "Sequencer.KeyframeList";
    listArea.type        = WidgetElementType::StackPanel;
    listArea.orientation = StackOrientation::Vertical;
    listArea.fillX       = true;
    listArea.fillY       = true;
    listArea.scrollable  = true;
    listArea.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
    listArea.spacing     = EditorTheme::Scaled(2.0f);
    listArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
    listArea.runtimeOnly = true;

    // Header
    {
        WidgetElement hdr{};
        hdr.type            = WidgetElementType::Label;
        hdr.text            = "Keyframes";
        hdr.style.textColor = theme.textSecondary;
        hdr.fontSize        = EditorTheme::Scaled(12.0f);
        hdr.runtimeOnly     = true;
        listArea.children.push_back(std::move(hdr));
    }

    if (m_renderer)
    {
        auto pts = m_renderer->getCameraPathPoints();
        for (int i = 0; i < static_cast<int>(pts.size()); ++i)
        {
            const auto& pt = pts[i];
            bool selected = (i == m_sequencerState.selectedKeyframe);

            WidgetElement row{};
            row.id          = "Sequencer.Row." + std::to_string(i);
            row.type        = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX       = true;
            row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
            row.spacing     = EditorTheme::Scaled(8.0f);
            row.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            row.style.color = selected
                ? Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.15f }
                : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.clickEvent  = "Sequencer.SelectKF." + std::to_string(i);
            row.runtimeOnly = true;

            // Index
            {
                WidgetElement idx{};
                idx.type            = WidgetElementType::Label;
                idx.text            = std::to_string(i + 1) + ".";
                idx.style.textColor = selected ? theme.accent : theme.textSecondary;
                idx.fontSize        = EditorTheme::Scaled(12.0f);
                idx.minSize         = EditorTheme::Scaled(Vec2{ 24.0f, 0.0f });
                idx.runtimeOnly     = true;
                row.children.push_back(std::move(idx));
            }

            // Position
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Pos (%.1f, %.1f, %.1f)",
                    static_cast<double>(pt.position.x),
                    static_cast<double>(pt.position.y),
                    static_cast<double>(pt.position.z));
                WidgetElement posLbl{};
                posLbl.type            = WidgetElementType::Label;
                posLbl.text            = buf;
                posLbl.style.textColor = theme.textPrimary;
                posLbl.fontSize        = EditorTheme::Scaled(11.0f);
                posLbl.runtimeOnly     = true;
                row.children.push_back(std::move(posLbl));
            }

            // Rotation
            {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "Yaw %.0f  Pitch %.0f",
                    static_cast<double>(pt.yaw), static_cast<double>(pt.pitch));
                WidgetElement rotLbl{};
                rotLbl.type            = WidgetElementType::Label;
                rotLbl.text            = buf;
                rotLbl.style.textColor = theme.textSecondary;
                rotLbl.fontSize        = EditorTheme::Scaled(11.0f);
                rotLbl.runtimeOnly     = true;
                row.children.push_back(std::move(rotLbl));
            }

            listArea.children.push_back(std::move(row));
        }

        if (pts.empty())
        {
            WidgetElement hint{};
            hint.type            = WidgetElementType::Label;
            hint.text            = "No keyframes. Click + to add one at the current camera position.";
            hint.style.textColor = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
            hint.fontSize        = EditorTheme::Scaled(11.0f);
            hint.runtimeOnly     = true;
            listArea.children.push_back(std::move(hint));
        }
    }

    root.children.push_back(std::move(listArea));
}

void UIManager::refreshSequencerTimeline()
{
    if (!m_sequencerState.isOpen || !m_renderer)
        return;

    auto* entry = findWidgetEntry(m_sequencerState.widgetId);
    if (!entry || !entry->widget) return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty()) return;

    auto& rootEl = elements[0];

    // Re-register dynamic keyframe click events
    if (m_renderer)
    {
        auto pts = m_renderer->getCameraPathPoints();
        for (int i = 0; i < static_cast<int>(pts.size()); ++i)
        {
            std::string evtId = "Sequencer.SelectKF." + std::to_string(i);
            registerClickEvent(evtId, [this, i]()
            {
                m_sequencerState.selectedKeyframe = i;
                // Move camera to selected keyframe position
                if (m_renderer)
                {
                    auto pts2 = m_renderer->getCameraPathPoints();
                    if (i >= 0 && i < static_cast<int>(pts2.size()))
                    {
                        const auto& kf = pts2[i];
                        m_renderer->startCameraTransition(kf.position, kf.yaw, kf.pitch, 0.3f);
                    }
                }
                refreshSequencerTimeline();
                markAllWidgetsDirty();
            });
        }
    }

    // Update playback state
    if (m_sequencerState.playing && m_renderer)
    {
        if (!m_renderer->isCameraPathPlaying())
        {
            m_sequencerState.playing = false;
            m_sequencerState.scrubberT = 0.0f;
        }
        else
        {
            m_sequencerState.scrubberT = m_renderer->getCameraPathProgress();
        }
    }

    // Rebuild toolbar + timeline + keyframe list
    rootEl.children.clear();
    buildSequencerToolbar(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildSequencerTimeline(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildSequencerKeyframeList(rootEl);

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// =============================================================================
// Level Composition Panel (Phase 11.4)
// =============================================================================

bool UIManager::isLevelCompositionOpen() const
{
    return m_levelCompositionState.isOpen;
}

void UIManager::openLevelCompositionTab()
{
    if (!m_renderer)
        return;

    const std::string tabId = "LevelComposition";

    if (m_levelCompositionState.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Level Composition", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "LevelComposition.Main";
    unregisterWidget(widgetId);

    m_levelCompositionState = {};
    m_levelCompositionState.tabId    = tabId;
    m_levelCompositionState.widgetId = widgetId;
    m_levelCompositionState.isOpen   = true;

    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "LC.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        buildLevelCompositionToolbar(root);

        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        buildLevelCompositionSubLevelList(root);

        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        buildLevelCompositionVolumeList(root);

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // Tab / close click events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshLevelCompositionPanel();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeLevelCompositionTab();
    });

    // Toolbar button events
    registerClickEvent("LC.AddSubLevel", [this]()
    {
        if (!m_renderer) return;
        const auto& subs = m_renderer->getSubLevels();
        std::string name = "SubLevel_" + std::to_string(subs.size());
        m_renderer->addSubLevel(name, "");
        refreshLevelCompositionPanel();
        markAllWidgetsDirty();
        showToastMessage("Sub-Level '" + name + "' added", 1.5f);
    });

    registerClickEvent("LC.RemoveSubLevel", [this]()
    {
        if (!m_renderer) return;
        int sel = m_levelCompositionState.selectedSubLevel;
        if (sel < 0) return;
        m_renderer->removeSubLevel(sel);
        m_levelCompositionState.selectedSubLevel = -1;
        refreshLevelCompositionPanel();
        markAllWidgetsDirty();
        showToastMessage("Sub-Level removed", 1.5f);
    });

    registerClickEvent("LC.AddVolume", [this]()
    {
        if (!m_renderer) return;
        int sel = m_levelCompositionState.selectedSubLevel;
        if (sel < 0)
        {
            showToastMessage("Select a Sub-Level first", 2.0f);
            return;
        }
        m_renderer->addStreamingVolume(Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 10.0f, 10.0f, 10.0f }, sel);
        refreshLevelCompositionPanel();
        markAllWidgetsDirty();
        showToastMessage("Streaming Volume added", 1.5f);
    });

    registerClickEvent("LC.ToggleVolumesVisible", [this]()
    {
        if (!m_renderer) return;
        m_renderer->m_streamingVolumesVisible = !m_renderer->m_streamingVolumesVisible;
        refreshLevelCompositionPanel();
        markAllWidgetsDirty();
    });

    refreshLevelCompositionPanel();
    markAllWidgetsDirty();
}

void UIManager::closeLevelCompositionTab()
{
    if (!m_levelCompositionState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_levelCompositionState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_levelCompositionState.widgetId);
    m_renderer->removeTab(tabId);
    m_levelCompositionState = {};
    markAllWidgetsDirty();
}

void UIManager::refreshLevelCompositionPanel()
{
    if (!m_levelCompositionState.isOpen || !m_renderer)
        return;

    WidgetEntry* entry = nullptr;
    for (auto& w : const_cast<std::vector<WidgetEntry>&>(getRegisteredWidgets()))
    {
        if (w.id == m_levelCompositionState.widgetId)
        {
            entry = &w;
            break;
        }
    }
    if (!entry || !entry->widget) return;

    auto* editorWidget = dynamic_cast<EditorWidget*>(entry->widget.get());
    if (!editorWidget) return;

    auto elems = editorWidget->getElements();
    if (elems.empty()) return;

    WidgetElement& rootEl = elems[0];
    rootEl.children.clear();

    buildLevelCompositionToolbar(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildLevelCompositionSubLevelList(rootEl);
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = EditorTheme::Get().panelBorder;
        sep.runtimeOnly = true;
        rootEl.children.push_back(std::move(sep));
    }
    buildLevelCompositionVolumeList(rootEl);

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

void UIManager::buildLevelCompositionToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "LC.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.fillX       = true;
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
    toolbar.spacing     = EditorTheme::Scaled(6.0f);
    toolbar.style.color = theme.titleBarBackground;
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement lbl{};
        lbl.type        = WidgetElementType::Text;
        lbl.text        = "Level Composition";
        lbl.style.color = theme.textPrimary;
        lbl.fontSize    = EditorTheme::Scaled(13.0f);
        lbl.runtimeOnly = true;
        toolbar.children.push_back(std::move(lbl));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type    = WidgetElementType::Panel;
        spacer.fillX   = true;
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Add Sub-Level button
    {
        WidgetElement btn{};
        btn.id          = "LC.AddSubLevel";
        btn.type        = WidgetElementType::Button;
        btn.text        = "+ Sub-Level";
        btn.style.color = theme.accent;
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    // Remove Sub-Level button
    {
        WidgetElement btn{};
        btn.id          = "LC.RemoveSubLevel";
        btn.type        = WidgetElementType::Button;
        btn.text        = "- Remove";
        btn.style.color = Vec4{ 0.6f, 0.2f, 0.2f, 1.0f };
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    // Add Streaming Volume button
    {
        WidgetElement btn{};
        btn.id          = "LC.AddVolume";
        btn.type        = WidgetElementType::Button;
        btn.text        = "+ Volume";
        btn.style.color = theme.accentGreen;
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    // Toggle volume visibility
    {
        bool vis = m_renderer ? m_renderer->m_streamingVolumesVisible : true;
        WidgetElement btn{};
        btn.id          = "LC.ToggleVolumesVisible";
        btn.type        = WidgetElementType::Button;
        btn.text        = vis ? "Volumes: ON" : "Volumes: OFF";
        btn.style.color = vis ? theme.accent : Vec4{ 0.4f, 0.4f, 0.4f, 1.0f };
        btn.style.borderRadius = EditorTheme::Scaled(4.0f);
        btn.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 3.0f });
        btn.fontSize    = EditorTheme::Scaled(12.0f);
        btn.runtimeOnly = true;
        toolbar.children.push_back(std::move(btn));
    }

    root.children.push_back(std::move(toolbar));
}

void UIManager::buildLevelCompositionSubLevelList(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement header{};
    header.type        = WidgetElementType::Text;
    header.text        = "Sub-Levels";
    header.style.color = theme.textSecondary;
    header.fontSize    = EditorTheme::Scaled(12.0f);
    header.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
    header.runtimeOnly = true;
    root.children.push_back(std::move(header));

    if (!m_renderer) return;

    const auto& subLevels = m_renderer->getSubLevels();
    if (subLevels.empty())
    {
        WidgetElement empty{};
        empty.type        = WidgetElementType::Text;
        empty.text        = "No sub-levels. Click '+ Sub-Level' to add one.";
        empty.style.color = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
        empty.fontSize    = EditorTheme::Scaled(11.0f);
        empty.padding     = EditorTheme::Scaled(Vec2{ 12.0f, 4.0f });
        empty.runtimeOnly = true;
        root.children.push_back(std::move(empty));
        return;
    }

    for (int i = 0; i < static_cast<int>(subLevels.size()); ++i)
    {
        const auto& sub = subLevels[i];
        const bool selected = (i == m_levelCompositionState.selectedSubLevel);

        WidgetElement row{};
        row.id          = "LC.SubLevel." + std::to_string(i);
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
        row.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        row.spacing     = EditorTheme::Scaled(6.0f);
        row.style.color = selected
            ? Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.25f }
            : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.runtimeOnly = true;

        // Color indicator
        {
            WidgetElement colorBox{};
            colorBox.type        = WidgetElementType::Panel;
            colorBox.minSize     = EditorTheme::Scaled(Vec2{ 14.0f, 14.0f });
            colorBox.style.color = sub.color;
            colorBox.style.borderRadius = EditorTheme::Scaled(3.0f);
            colorBox.runtimeOnly = true;
            row.children.push_back(std::move(colorBox));
        }

        // Name
        {
            WidgetElement name{};
            name.type        = WidgetElementType::Text;
            name.text        = sub.name;
            name.style.color = theme.textPrimary;
            name.fontSize    = EditorTheme::Scaled(12.0f);
            name.fillX       = true;
            name.runtimeOnly = true;
            row.children.push_back(std::move(name));
        }

        // Loaded indicator
        {
            WidgetElement loadedLbl{};
            loadedLbl.type        = WidgetElementType::Text;
            loadedLbl.text        = sub.loaded ? "[Loaded]" : "[Unloaded]";
            loadedLbl.style.color = sub.loaded
                ? Vec4{ 0.2f, 0.9f, 0.3f, 1.0f }
                : Vec4{ 0.6f, 0.6f, 0.6f, 1.0f };
            loadedLbl.fontSize    = EditorTheme::Scaled(11.0f);
            loadedLbl.runtimeOnly = true;
            row.children.push_back(std::move(loadedLbl));
        }

        // Loaded toggle button
        {
            WidgetElement toggleBtn{};
            toggleBtn.id          = "LC.ToggleLoaded." + std::to_string(i);
            toggleBtn.type        = WidgetElementType::Button;
            toggleBtn.text        = sub.loaded ? "Unload" : "Load";
            toggleBtn.style.color = theme.inputBackground;
            toggleBtn.style.borderRadius = EditorTheme::Scaled(3.0f);
            toggleBtn.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            toggleBtn.fontSize    = EditorTheme::Scaled(10.0f);
            toggleBtn.runtimeOnly = true;
            row.children.push_back(std::move(toggleBtn));

            const int idx = i;
            const bool loaded = sub.loaded;
            registerClickEvent("LC.ToggleLoaded." + std::to_string(i), [this, idx, loaded]()
            {
                if (!m_renderer) return;
                m_renderer->setSubLevelLoaded(idx, !loaded);
                refreshLevelCompositionPanel();
                markAllWidgetsDirty();
            });
        }

        // Visible toggle button
        {
            WidgetElement visBtn{};
            visBtn.id          = "LC.ToggleVisible." + std::to_string(i);
            visBtn.type        = WidgetElementType::Button;
            visBtn.text        = sub.visible ? "Vis" : "Hid";
            visBtn.style.color = sub.visible ? theme.accent : Vec4{ 0.4f, 0.4f, 0.4f, 1.0f };
            visBtn.style.borderRadius = EditorTheme::Scaled(3.0f);
            visBtn.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 2.0f });
            visBtn.fontSize    = EditorTheme::Scaled(10.0f);
            visBtn.runtimeOnly = true;
            row.children.push_back(std::move(visBtn));

            const int idx = i;
            const bool visible = sub.visible;
            registerClickEvent("LC.ToggleVisible." + std::to_string(i), [this, idx, visible]()
            {
                if (!m_renderer) return;
                m_renderer->setSubLevelVisible(idx, !visible);
                refreshLevelCompositionPanel();
                markAllWidgetsDirty();
            });
        }

        // Select this sub-level on row click
        {
            const int idx = i;
            registerClickEvent("LC.SubLevel." + std::to_string(i), [this, idx]()
            {
                m_levelCompositionState.selectedSubLevel = idx;
                refreshLevelCompositionPanel();
                markAllWidgetsDirty();
            });
        }

        root.children.push_back(std::move(row));
    }
}

void UIManager::buildLevelCompositionVolumeList(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement header{};
    header.type        = WidgetElementType::Text;
    header.text        = "Streaming Volumes";
    header.style.color = theme.textSecondary;
    header.fontSize    = EditorTheme::Scaled(12.0f);
    header.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
    header.runtimeOnly = true;
    root.children.push_back(std::move(header));

    if (!m_renderer) return;

    const auto& volumes = m_renderer->getStreamingVolumes();
    const auto& subLevels = m_renderer->getSubLevels();

    if (volumes.empty())
    {
        WidgetElement empty{};
        empty.type        = WidgetElementType::Text;
        empty.text        = "No streaming volumes. Select a sub-level and click '+ Volume'.";
        empty.style.color = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
        empty.fontSize    = EditorTheme::Scaled(11.0f);
        empty.padding     = EditorTheme::Scaled(Vec2{ 12.0f, 4.0f });
        empty.runtimeOnly = true;
        root.children.push_back(std::move(empty));
        return;
    }

    for (int i = 0; i < static_cast<int>(volumes.size()); ++i)
    {
        const auto& vol = volumes[i];

        WidgetElement row{};
        row.id          = "LC.Volume." + std::to_string(i);
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
        row.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        row.spacing     = EditorTheme::Scaled(6.0f);
        row.runtimeOnly = true;

        // Color indicator from linked sub-level
        {
            Vec4 volColor{ 0.5f, 0.5f, 0.5f, 1.0f };
            if (vol.subLevelIndex >= 0 && vol.subLevelIndex < static_cast<int>(subLevels.size()))
                volColor = subLevels[vol.subLevelIndex].color;

            WidgetElement colorBox{};
            colorBox.type        = WidgetElementType::Panel;
            colorBox.minSize     = EditorTheme::Scaled(Vec2{ 10.0f, 10.0f });
            colorBox.style.color = volColor;
            colorBox.style.borderRadius = EditorTheme::Scaled(2.0f);
            colorBox.runtimeOnly = true;
            row.children.push_back(std::move(colorBox));
        }

        // Label
        {
            std::string linkedName = "unlinked";
            if (vol.subLevelIndex >= 0 && vol.subLevelIndex < static_cast<int>(subLevels.size()))
                linkedName = subLevels[vol.subLevelIndex].name;

            char buf[128];
            snprintf(buf, sizeof(buf), "Vol %d -> %s  (%.0f,%.0f,%.0f  %.0f x %.0f x %.0f)",
                     i, linkedName.c_str(),
                     vol.center.x, vol.center.y, vol.center.z,
                     vol.halfExtents.x * 2.0f, vol.halfExtents.y * 2.0f, vol.halfExtents.z * 2.0f);

            WidgetElement lbl{};
            lbl.type        = WidgetElementType::Text;
            lbl.text        = buf;
            lbl.style.color = theme.textPrimary;
            lbl.fontSize    = EditorTheme::Scaled(11.0f);
            lbl.fillX       = true;
            lbl.runtimeOnly = true;
            row.children.push_back(std::move(lbl));
        }

        // Remove button
        {
            WidgetElement removeBtn{};
            removeBtn.id          = "LC.RemoveVolume." + std::to_string(i);
            removeBtn.type        = WidgetElementType::Button;
            removeBtn.text        = "X";
            removeBtn.style.color = Vec4{ 0.6f, 0.2f, 0.2f, 1.0f };
            removeBtn.style.borderRadius = EditorTheme::Scaled(3.0f);
            removeBtn.padding     = EditorTheme::Scaled(Vec2{ 5.0f, 1.0f });
            removeBtn.fontSize    = EditorTheme::Scaled(10.0f);
            removeBtn.runtimeOnly = true;
            row.children.push_back(std::move(removeBtn));

            const int idx = i;
            registerClickEvent("LC.RemoveVolume." + std::to_string(i), [this, idx]()
            {
                if (!m_renderer) return;
                m_renderer->removeStreamingVolume(idx);
                refreshLevelCompositionPanel();
                markAllWidgetsDirty();
                showToastMessage("Streaming Volume removed", 1.5f);
            });
        }

        root.children.push_back(std::move(row));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Build Game dialog (Phase 10)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
    struct FolderBrowseContext
    {
        PopupWindow* popup{ nullptr };
        std::string  entryId;
    };

    static void SDLCALL OnFolderBrowseCompleted(void* userdata, const char* const* filelist, int /*filter*/)
    {
        auto* ctx = static_cast<FolderBrowseContext*>(userdata);
        if (ctx && filelist && filelist[0])
        {
            if (auto* el = ctx->popup->uiManager().findElementById(ctx->entryId))
            {
                el->text = filelist[0];
            }
        }
        delete ctx;
    }
}

// ── Build Profiles (Phase 10.3) ─────────────────────────────────────────

void UIManager::loadBuildProfiles()
{
    m_buildProfiles.clear();

    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return;

    const auto profileDir = std::filesystem::path(projPath) / "Config" / "BuildProfiles";

    // Load existing profiles from JSON files
    if (std::filesystem::exists(profileDir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(profileDir))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;

            try
            {
                std::ifstream ifs(entry.path());
                if (!ifs.is_open()) continue;
                auto j = nlohmann::json::parse(ifs);

                BuildProfile p;
                if (j.contains("name"))            p.name            = j["name"].get<std::string>();
                if (j.contains("cmakeBuildType"))   p.cmakeBuildType  = j["cmakeBuildType"].get<std::string>();
                if (j.contains("logLevel"))         p.logLevel        = j["logLevel"].get<std::string>();
                if (j.contains("enableHotReload"))  p.enableHotReload = j["enableHotReload"].get<bool>();
                if (j.contains("enableValidation")) p.enableValidation= j["enableValidation"].get<bool>();
                if (j.contains("enableProfiler"))   p.enableProfiler  = j["enableProfiler"].get<bool>();
                if (j.contains("compressAssets"))    p.compressAssets  = j["compressAssets"].get<bool>();

                m_buildProfiles.push_back(std::move(p));
            }
            catch (...) { /* skip malformed files */ }
        }
    }

    // If no profiles exist, create 3 defaults
    if (m_buildProfiles.empty())
    {
        BuildProfile debug;
        debug.name            = "Debug";
        debug.cmakeBuildType  = "Debug";
        debug.logLevel        = "verbose";
        debug.enableHotReload = true;
        debug.enableValidation= true;
        debug.enableProfiler  = true;
        debug.compressAssets  = false;

        BuildProfile dev;
        dev.name              = "Development";
        dev.cmakeBuildType    = "RelWithDebInfo";
        dev.logLevel          = "info";
        dev.enableHotReload   = true;
        dev.enableValidation  = false;
        dev.enableProfiler    = true;
        dev.compressAssets    = false;

        BuildProfile ship;
        ship.name             = "Shipping";
        ship.cmakeBuildType   = "Release";
        ship.logLevel         = "error";
        ship.enableHotReload  = false;
        ship.enableValidation = false;
        ship.enableProfiler   = false;
        ship.compressAssets   = true;

        m_buildProfiles.push_back(debug);
        m_buildProfiles.push_back(dev);
        m_buildProfiles.push_back(ship);

        for (const auto& p : m_buildProfiles)
            saveBuildProfile(p);
    }
}

void UIManager::saveBuildProfile(const BuildProfile& profile)
{
    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return;

    const auto profileDir = std::filesystem::path(projPath) / "Config" / "BuildProfiles";
    std::error_code ec;
    std::filesystem::create_directories(profileDir, ec);

    nlohmann::json j;
    j["name"]            = profile.name;
    j["cmakeBuildType"]  = profile.cmakeBuildType;
    j["logLevel"]        = profile.logLevel;
    j["enableHotReload"] = profile.enableHotReload;
    j["enableValidation"]= profile.enableValidation;
    j["enableProfiler"]  = profile.enableProfiler;
    j["compressAssets"]  = profile.compressAssets;

    const auto filePath = profileDir / (profile.name + ".json");
    std::ofstream ofs(filePath);
    if (ofs.is_open())
        ofs << j.dump(4);
}

void UIManager::deleteBuildProfile(const std::string& name)
{
    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return;

    const auto filePath = std::filesystem::path(projPath) / "Config" / "BuildProfiles" / (name + ".json");
    std::error_code ec;
    std::filesystem::remove(filePath, ec);

    m_buildProfiles.erase(
        std::remove_if(m_buildProfiles.begin(), m_buildProfiles.end(),
            [&](const BuildProfile& p) { return p.name == name; }),
        m_buildProfiles.end());
}

void UIManager::openBuildGameDialog()
{
    if (!m_renderer) return;

    // Ensure profiles are loaded
    if (m_buildProfiles.empty())
        loadBuildProfiles();

    constexpr float kBaseW = 520.0f;
    constexpr float kBaseH = 520.0f;
    const int kPopupW = static_cast<int>(EditorTheme::Scaled(kBaseW));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(kBaseH));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "BuildGame", "Build Game", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    // Shared mutable state for the form
    struct FormState
    {
        std::string startLevel;
        std::string windowTitle = "Game";
        int profileIndex = 0;
        bool launchAfter = true;
        bool cleanBuild = false;
    };
    auto formState = std::make_shared<FormState>();

    // Collect all available levels for the dropdown
    std::vector<std::string> levelPaths;
    int preSelectedLevelIdx = -1;
    {
        auto& diag = DiagnosticsManager::Instance();
        auto& assetMgr = AssetManager::Instance();
        const auto& registry = assetMgr.getAssetRegistry();
        for (const auto& entry : registry)
        {
            if (entry.type == AssetType::Level)
                levelPaths.push_back(entry.path);
        }
        if (!levelPaths.empty())
        {
            preSelectedLevelIdx = 0;
            formState->startLevel = levelPaths[0];
        }

        // Pre-fill window title from project name
        formState->windowTitle = diag.getProjectInfo().projectName;
        if (formState->windowTitle.empty())
            formState->windowTitle = "Game";
    }

    // Standardized output and binary dirs
    std::string defaultOutputDir;
    std::string defaultBinaryDir;
    {
        auto& diag = DiagnosticsManager::Instance();
        const auto& projPath = diag.getProjectInfo().projectPath;
        if (!projPath.empty())
        {
            defaultOutputDir = (std::filesystem::path(projPath) / "Build").string();
            defaultBinaryDir = (std::filesystem::path(projPath) / "Binary").string();
        }
    }

    // Build profile names for dropdown
    std::vector<std::string> profileNames;
    int preSelectedProfileIdx = 0;
    for (size_t i = 0; i < m_buildProfiles.size(); ++i)
    {
        profileNames.push_back(m_buildProfiles[i].name);
        if (m_buildProfiles[i].name == "Development")
            preSelectedProfileIdx = static_cast<int>(i);
    }
    formState->profileIndex = preSelectedProfileIdx;

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type = WidgetElementType::Panel;
        bg.id = "BG.Bg";
        bg.from = Vec2{ 0.0f, 0.0f };
        bg.to = Vec2{ 1.0f, 1.0f };
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(std::move(bg));
    }

    // Title
    {
        WidgetElement title;
        title.type = WidgetElementType::Text;
        title.id = "BG.Title";
        title.from = Vec2{ nx(16.0f), ny(8.0f) };
        title.to = Vec2{ nx(W - 16.0f), ny(40.0f) };
        title.text = "Build Game";
        title.fontSize = EditorTheme::Get().fontSizeHeading;
        title.style.textColor = EditorTheme::Get().titleBarText;
        title.textAlignV = TextAlignV::Center;
        title.padding = Vec2{ 6.0f, 0.0f };
        elements.push_back(std::move(title));
    }

    // ── Form fields ──
    const float rowH = 24.0f;
    const float entryH = 22.0f;
    const float labelW = 120.0f;
    const float gap = 6.0f;
    const float leftPad = 20.0f;
    const float rightPad = 20.0f;
    float curY = 50.0f;

    auto addLabel = [&](const std::string& id, const std::string& text, float y)
    {
        WidgetElement lbl;
        lbl.type = WidgetElementType::Text;
        lbl.id = id;
        lbl.from = Vec2{ nx(leftPad), ny(y) };
        lbl.to = Vec2{ nx(leftPad + labelW), ny(y + rowH) };
        lbl.text = text;
        lbl.fontSize = EditorTheme::Get().fontSizeBody;
        lbl.style.textColor = EditorTheme::Get().textPrimary;
        lbl.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(lbl));
    };

    auto addEntry = [&](const std::string& id, const std::string& defaultVal, float y) -> std::string
    {
        WidgetElement entry;
        entry.type = WidgetElementType::EntryBar;
        entry.id = id;
        entry.from = Vec2{ nx(leftPad + labelW + gap), ny(y) };
        entry.to = Vec2{ nx(W - rightPad), ny(y + entryH) };
        entry.text = defaultVal;
        entry.fontSize = EditorTheme::Get().fontSizeBody;
        entry.style.color = EditorTheme::Get().inputBackground;
        entry.style.textColor = EditorTheme::Get().textPrimary;
        entry.padding = Vec2{ 6.0f, 4.0f };
        entry.minSize = Vec2{ 0.0f, entryH };
        entry.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(entry));
        return id;
    };

    // Build Profile (DropDown)
    addLabel("BG.Lbl.Profile", "Build Profile:", curY);
    {
        WidgetElement dd;
        dd.type = WidgetElementType::DropDown;
        dd.id = "BG.DD.Profile";
        dd.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        dd.to = Vec2{ nx(W - rightPad), ny(curY + entryH) };
        dd.items = profileNames;
        dd.selectedIndex = preSelectedProfileIdx;
        if (preSelectedProfileIdx >= 0 && preSelectedProfileIdx < static_cast<int>(profileNames.size()))
            dd.text = profileNames[static_cast<size_t>(preSelectedProfileIdx)];
        dd.fontSize = EditorTheme::Get().fontSizeBody;
        dd.style.color = EditorTheme::Get().inputBackground;
        dd.style.textColor = EditorTheme::Get().textPrimary;
        dd.padding = Vec2{ 6.0f, 4.0f };
        dd.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(dd));
    }
    curY += rowH + gap;

    // Profile info line (read-only, shows brief profile settings)
    {
        std::string profileInfo;
        if (!m_buildProfiles.empty())
        {
            const auto& p = m_buildProfiles[static_cast<size_t>(preSelectedProfileIdx)];
            profileInfo = "CMake: " + p.cmakeBuildType + "  |  Log: " + p.logLevel
                + "  |  HotReload: " + (p.enableHotReload ? "on" : "off")
                + "  |  Profiler: " + (p.enableProfiler ? "on" : "off");
        }
        WidgetElement info;
        info.type = WidgetElementType::Text;
        info.id = "BG.ProfileInfo";
        info.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        info.to = Vec2{ nx(W - rightPad), ny(curY + rowH) };
        info.text = profileInfo;
        info.fontSize = EditorTheme::Get().fontSizeSmall;
        info.style.textColor = EditorTheme::Get().textMuted;
        info.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(info));
    }
    curY += rowH + gap;

    // Start Level (DropDown)
    addLabel("BG.Lbl.StartLevel", "Start Level:", curY);
    {
        WidgetElement dd;
        dd.type = WidgetElementType::DropDown;
        dd.id = "BG.DD.StartLevel";
        dd.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        dd.to = Vec2{ nx(W - rightPad), ny(curY + entryH) };
        dd.items = levelPaths;
        dd.selectedIndex = preSelectedLevelIdx;
        if (preSelectedLevelIdx >= 0 && preSelectedLevelIdx < static_cast<int>(levelPaths.size()))
            dd.text = levelPaths[static_cast<size_t>(preSelectedLevelIdx)];
        dd.fontSize = EditorTheme::Get().fontSizeBody;
        dd.style.color = EditorTheme::Get().inputBackground;
        dd.style.textColor = EditorTheme::Get().textPrimary;
        dd.padding = Vec2{ 6.0f, 4.0f };
        dd.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(dd));
    }
    curY += rowH + gap;

    // Window Title
    addLabel("BG.Lbl.Title", "Window Title:", curY);
    addEntry("BG.Entry.Title", formState->windowTitle, curY);
    curY += rowH + gap;

    // Launch after build checkbox
    addLabel("BG.Lbl.Launch", "Launch after:", curY);
    {
        WidgetElement chk;
        chk.type = WidgetElementType::CheckBox;
        chk.id = "BG.Chk.Launch";
        chk.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        chk.to = Vec2{ nx(leftPad + labelW + gap + 20.0f), ny(curY + rowH) };
        chk.isChecked = true;
        chk.style.color = EditorTheme::Get().inputBackground;
        chk.style.fillColor = EditorTheme::Get().accent;
        chk.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(chk));
    }
    curY += rowH + gap;

    // Clean build checkbox
    addLabel("BG.Lbl.Clean", "Clean build:", curY);
    {
        WidgetElement chk;
        chk.type = WidgetElementType::CheckBox;
        chk.id = "BG.Chk.Clean";
        chk.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        chk.to = Vec2{ nx(leftPad + labelW + gap + 20.0f), ny(curY + rowH) };
        chk.isChecked = false;
        chk.style.color = EditorTheme::Get().inputBackground;
        chk.style.fillColor = EditorTheme::Get().accent;
        chk.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(chk));
    }
    curY += rowH + gap;

    // Output directory (read-only info)
    addLabel("BG.Lbl.Output", "Output Dir:", curY);
    {
        WidgetElement outputText;
        outputText.type = WidgetElementType::Text;
        outputText.id = "BG.Text.Output";
        outputText.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        outputText.to = Vec2{ nx(W - rightPad), ny(curY + rowH) };
        outputText.text = defaultOutputDir;
        outputText.fontSize = EditorTheme::Get().fontSizeSmall;
        outputText.style.textColor = EditorTheme::Get().textSecondary;
        outputText.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(outputText));
    }
    curY += rowH + gap;

    // Binary cache directory (read-only info)
    addLabel("BG.Lbl.Binary", "Binary Cache:", curY);
    {
        WidgetElement binaryText;
        binaryText.type = WidgetElementType::Text;
        binaryText.id = "BG.Text.Binary";
        binaryText.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        binaryText.to = Vec2{ nx(W - rightPad), ny(curY + rowH) };
        binaryText.text = defaultBinaryDir;
        binaryText.fontSize = EditorTheme::Get().fontSizeSmall;
        binaryText.style.textColor = EditorTheme::Get().textSecondary;
        binaryText.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(binaryText));
    }
    curY += rowH + gap;

    // ── Info text ──
    {
        WidgetElement info;
        info.type = WidgetElementType::Text;
        info.id = "BG.Info";
        info.from = Vec2{ nx(leftPad), ny(curY + gap) };
        info.to = Vec2{ nx(W - rightPad), ny(curY + gap + rowH) };
        info.text = "Output goes to <Project>/Build, binaries cached in <Project>/Binary.";
        info.fontSize = EditorTheme::Get().fontSizeSmall;
        info.style.textColor = EditorTheme::Get().textSecondary;
        info.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(info));
    }
    curY += rowH + gap * 2;

    // ── Buttons ──
    const float btnW = 100.0f;
    const float btnH = 30.0f;
    const float btnGap = 10.0f;
    const float btnY = H - 16.0f - btnH;

    // Build button
    {
        WidgetElement buildBtn;
        buildBtn.type = WidgetElementType::Button;
        buildBtn.id = "BG.Btn.Build";
        buildBtn.from = Vec2{ nx(W - rightPad - btnW * 2 - btnGap), ny(btnY) };
        buildBtn.to = Vec2{ nx(W - rightPad - btnW - btnGap), ny(btnY + btnH) };
        buildBtn.text = "Build";
        buildBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
        buildBtn.style.color = EditorTheme::Get().accent;
        buildBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        buildBtn.textAlignH = TextAlignH::Center;
        buildBtn.textAlignV = TextAlignV::Center;
        buildBtn.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(buildBtn));
    }

    // Cancel button
    {
        WidgetElement cancelBtn;
        cancelBtn.type = WidgetElementType::Button;
        cancelBtn.id = "BG.Btn.Cancel";
        cancelBtn.from = Vec2{ nx(W - rightPad - btnW), ny(btnY) };
        cancelBtn.to = Vec2{ nx(W - rightPad), ny(btnY + btnH) };
        cancelBtn.text = "Cancel";
        cancelBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
        cancelBtn.style.color = EditorTheme::Get().buttonDefault;
        cancelBtn.style.textColor = EditorTheme::Get().textPrimary;
        cancelBtn.textAlignH = TextAlignH::Center;
        cancelBtn.textAlignV = TextAlignV::Center;
        cancelBtn.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(cancelBtn));
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("BuildGameForm");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));

    popup->uiManager().registerWidget("BuildGameForm", widget);

    // Cancel button closes the popup
    popup->uiManager().registerClickEvent("BG.Btn.Cancel", [popup]()
    {
        popup->close();
    });

    // Build button: gather form values and invoke the build callback
    auto* parentUIMgr = this;
    auto profilesCopy = std::make_shared<std::vector<BuildProfile>>(m_buildProfiles);
    popup->uiManager().registerClickEvent("BG.Btn.Build",
        [popup, parentUIMgr, formState, profilesCopy, defaultOutputDir, defaultBinaryDir]()
    {
        auto& popupUI = popup->uiManager();

        BuildGameConfig config;

        // Read profile selection
        int profileIdx = 0;
        if (auto* el = popupUI.findElementById("BG.DD.Profile"))
            profileIdx = el->selectedIndex;
        if (profileIdx >= 0 && profileIdx < static_cast<int>(profilesCopy->size()))
            config.profile = (*profilesCopy)[static_cast<size_t>(profileIdx)];

        // Read other form values
        if (auto* el = popupUI.findElementById("BG.DD.StartLevel"))
            config.startLevel = el->text;
        if (auto* el = popupUI.findElementById("BG.Entry.Title"))
            config.windowTitle = el->text;
        if (auto* el = popupUI.findElementById("BG.Chk.Launch"))
            config.launchAfterBuild = el->isChecked;
        if (auto* el = popupUI.findElementById("BG.Chk.Clean"))
            config.cleanBuild = el->isChecked;

        // Standardized paths
        config.outputDir = defaultOutputDir;
        config.binaryDir = defaultBinaryDir;

        if (config.startLevel.empty())
        {
            parentUIMgr->showToastMessage("Start Level is required.", 3.0f, NotificationLevel::Warning);
            return;
        }
        if (config.outputDir.empty())
        {
            parentUIMgr->showToastMessage("Output directory is required.", 3.0f, NotificationLevel::Warning);
            return;
        }

        if (parentUIMgr->m_onBuildGame)
            parentUIMgr->m_onBuildGame(config);

        popup->close();
    });
}

// ── Build progress modal ────────────────────────────────────────────────

void UIManager::showBuildProgress()
{
    m_buildOutputLines.clear();
    m_buildCancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        m_buildPendingLines.clear();
        m_buildPendingStepDirty = false;
        m_buildPendingFinished = false;
    }

    // Open a separate OS window for build output
    if (m_renderer)
    {
        m_buildPopup = m_renderer->openPopupWindow("BuildOutput", "Build Output", 820, 520);
    }
    if (!m_buildPopup)
    {
        // Fallback: just log a warning and bail
        showToastMessage("Failed to open build output window.", 4.0f, NotificationLevel::Error);
        return;
    }

    m_buildProgressWidget = std::make_shared<EditorWidget>();
    m_buildProgressWidget->setName("BuildProgress");
    m_buildProgressWidget->setAnchor(WidgetAnchor::TopLeft);
    m_buildProgressWidget->setFillX(true);
    m_buildProgressWidget->setFillY(true);
    m_buildProgressWidget->setZOrder(100);

    // Full-window background panel as StackPanel
    WidgetElement panel{};
    panel.id = "BP.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.0f, 0.0f };
    panel.to = Vec2{ 1.0f, 1.0f };
    panel.padding = Vec2{ 18.0f, 12.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = EditorTheme::Get().windowBackground;
    panel.runtimeOnly = true;

    WidgetElement title{};
    title.id = "BP.Title";
    title.type = WidgetElementType::Text;
    title.text = "Building Game...";
    title.font = EditorTheme::Get().fontDefault;
    title.fontSize = EditorTheme::Get().fontSizeHeading;
    title.textAlignH = TextAlignH::Center;
    title.style.textColor = EditorTheme::Get().textPrimary;
    title.fillX = true;
    title.minSize = Vec2{ 0.0f, 28.0f };
    title.runtimeOnly = true;

    WidgetElement status{};
    status.id = "BP.Status";
    status.type = WidgetElementType::Text;
    status.text = "Preparing...";
    status.font = EditorTheme::Get().fontDefault;
    status.fontSize = EditorTheme::Get().fontSizeBody;
    status.textAlignH = TextAlignH::Center;
    status.style.textColor = EditorTheme::Get().textSecondary;
    status.fillX = true;
    status.minSize = Vec2{ 0.0f, 22.0f };
    status.runtimeOnly = true;

    WidgetElement counter{};
    counter.id = "BP.Counter";
    counter.type = WidgetElementType::Text;
    counter.text = "0 / 0";
    counter.font = EditorTheme::Get().fontDefault;
    counter.fontSize = EditorTheme::Get().fontSizeSubheading;
    counter.textAlignH = TextAlignH::Center;
    counter.style.textColor = EditorTheme::Get().textSecondary;
    counter.fillX = true;
    counter.minSize = Vec2{ 0.0f, 20.0f };
    counter.runtimeOnly = true;

    WidgetElement progress{};
    progress.id = "BP.Bar";
    progress.type = WidgetElementType::ProgressBar;
    progress.fillX = true;
    progress.minSize = Vec2{ 0.0f, 18.0f };
    progress.minValue = 0.0f;
    progress.maxValue = 1.0f;
    progress.valueFloat = 0.0f;
    progress.style.color = EditorTheme::Get().sliderTrack;
    progress.style.fillColor = Vec4{
        EditorTheme::Get().accent.x,
        EditorTheme::Get().accent.y,
        EditorTheme::Get().accent.z, 0.95f };
    progress.runtimeOnly = true;

    // Result text (hidden during build, shown after completion)
    WidgetElement resultText{};
    resultText.id = "BP.Result";
    resultText.type = WidgetElementType::Text;
    resultText.text = "";
    resultText.font = EditorTheme::Get().fontDefault;
    resultText.fontSize = EditorTheme::Get().fontSizeBody;
    resultText.textAlignH = TextAlignH::Center;
    resultText.style.textColor = EditorTheme::Get().textPrimary;
    resultText.fillX = true;
    resultText.minSize = Vec2{ 0.0f, 24.0f };
    resultText.runtimeOnly = true;
    resultText.isCollapsed = true;

    // Close button (hidden during build, shown after completion)
    auto closeBtn = EditorUIBuilder::makePrimaryButton("BP.CloseBtn", "Close", [this]() {
        dismissBuildProgress();
    });
    closeBtn.isCollapsed = true;

    panel.children.push_back(std::move(title));
    panel.children.push_back(std::move(status));
    panel.children.push_back(std::move(counter));
    panel.children.push_back(std::move(progress));

    // Scrollable build output log (children added dynamically per-line by pollBuildThread)
    WidgetElement outputPanel{};
    outputPanel.id = "BP.OutputScroll";
    outputPanel.type = WidgetElementType::StackPanel;
    outputPanel.orientation = StackOrientation::Vertical;
    outputPanel.fillX = true;
    outputPanel.fillY = true;
    outputPanel.scrollable = true;
    outputPanel.style.color = Vec4{ 0.04f, 0.04f, 0.04f, 0.95f };
    outputPanel.style.borderRadius = 4.0f;
    outputPanel.padding = Vec2{ 6.0f, 4.0f };
    outputPanel.runtimeOnly = true;

    panel.children.push_back(std::move(outputPanel));
    panel.children.push_back(std::move(resultText));

    // Abort Build button (visible during build, hidden after completion)
    auto abortBtn = EditorUIBuilder::makeButton("BP.AbortBtn", "Abort Build", [this]() {
        m_buildCancelRequested.store(true);
        appendBuildOutput("[INFO] Build cancellation requested...");
    });
    abortBtn.style.color = Vec4{ 0.7f, 0.15f, 0.15f, 1.0f };
    abortBtn.style.hoverColor = Vec4{ 0.85f, 0.2f, 0.2f, 1.0f };
    abortBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
    panel.children.push_back(std::move(abortBtn));

    panel.children.push_back(std::move(closeBtn));

    std::vector<WidgetElement> elems;
    elems.push_back(std::move(panel));
    m_buildProgressWidget->setElements(std::move(elems));
    m_buildProgressWidget->markLayoutDirty();

    m_buildPopup->uiManager().registerWidget("BuildProgress", m_buildProgressWidget);
}

void UIManager::updateBuildProgress(const std::string& status, int step, int totalSteps)
{
    if (!m_buildProgressWidget) return;

    auto& elems = m_buildProgressWidget->getElementsMutable();

    WidgetElement* statusEl = FindElementById(elems, "BP.Status");
    if (statusEl)
        statusEl->text = status;

    WidgetElement* counterEl = FindElementById(elems, "BP.Counter");
    if (counterEl)
        counterEl->text = std::to_string(step) + " / " + std::to_string(totalSteps);

    WidgetElement* barEl = FindElementById(elems, "BP.Bar");
    if (barEl)
    {
        barEl->maxValue = static_cast<float>(totalSteps);
        barEl->valueFloat = static_cast<float>(step);
    }

    m_buildProgressWidget->markLayoutDirty();
    m_renderDirty = true;
}

void UIManager::closeBuildProgress(bool success, const std::string& message)
{
    if (!m_buildProgressWidget) return;

    auto& elems = m_buildProgressWidget->getElementsMutable();

    // Update title to reflect completion
    WidgetElement* titleEl = FindElementById(elems, "BP.Title");
    if (titleEl)
        titleEl->text = success ? "Build Completed" : "Build Failed";

    // Hide the progress bar and counter
    WidgetElement* barEl = FindElementById(elems, "BP.Bar");
    if (barEl) barEl->isCollapsed = true;

    WidgetElement* counterEl = FindElementById(elems, "BP.Counter");
    if (counterEl) counterEl->isCollapsed = true;

    // Update status text
    WidgetElement* statusEl = FindElementById(elems, "BP.Status");
    if (statusEl)
        statusEl->text = success ? "The game was built successfully." : "The build encountered errors.";

    // Show result message
    WidgetElement* resultEl = FindElementById(elems, "BP.Result");
    if (resultEl)
    {
        resultEl->isCollapsed = false;
        if (!message.empty())
            resultEl->text = message;
        else
            resultEl->text = success ? "Build completed successfully!" : "Build failed.";
        resultEl->style.textColor = success
            ? Vec4{ 0.3f, 0.9f, 0.4f, 1.0f }
            : Vec4{ 0.95f, 0.3f, 0.3f, 1.0f };
    }

    // Show the close button, hide the abort button
    WidgetElement* closeBtn = FindElementById(elems, "BP.CloseBtn");
    if (closeBtn) closeBtn->isCollapsed = false;

    WidgetElement* abortBtn = FindElementById(elems, "BP.AbortBtn");
    if (abortBtn) abortBtn->isCollapsed = true;

    m_buildProgressWidget->markLayoutDirty();
    m_renderDirty = true;
}

void UIManager::dismissBuildProgress()
{
    if (m_buildPopup)
    {
        m_buildPopup->uiManager().unregisterWidget("BuildProgress");
        if (m_renderer)
            m_renderer->closePopupWindow("BuildOutput");
        m_buildPopup = nullptr;
    }
    m_buildProgressWidget.reset();
}

void UIManager::appendBuildOutput(const std::string& line)
{
    std::lock_guard<std::mutex> lock(m_buildMutex);
    m_buildPendingLines.push_back(line);
}

void UIManager::pollBuildThread()
{
    if (!m_buildProgressWidget)
        return;

    std::vector<std::string> newLines;
    bool stepDirty = false;
    std::string stepStatus;
    int stepNum = 0;
    int stepTotal = 0;
    bool finished = false;
    bool success = false;
    std::string errorMsg;

    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        if (!m_buildPendingLines.empty())
        {
            newLines.swap(m_buildPendingLines);
        }
        if (m_buildPendingStepDirty)
        {
            stepDirty = true;
            stepStatus = m_buildPendingStatus;
            stepNum = m_buildPendingStep;
            stepTotal = m_buildPendingTotalSteps;
            m_buildPendingStepDirty = false;
        }
        if (m_buildPendingFinished)
        {
            finished = true;
            success = m_buildPendingSuccess;
            errorMsg = m_buildPendingErrorMsg;
            m_buildPendingFinished = false;
        }
    }

    bool dirty = false;

    if (!newLines.empty())
    {
        for (auto& ln : newLines)
            m_buildOutputLines.push_back(std::move(ln));

        auto& elems = m_buildProgressWidget->getElementsMutable();
        WidgetElement* outputScroll = FindElementById(elems, "BP.OutputScroll");
        if (outputScroll)
        {
            const auto& theme = EditorTheme::Get();
            const float rowH = EditorTheme::Scaled(16.0f);

            // Rebuild all rows from m_buildOutputLines
            outputScroll->children.clear();
            for (size_t i = 0; i < m_buildOutputLines.size(); ++i)
            {
                WidgetElement row{};
                row.id        = "BP.Row." + std::to_string(i);
                row.type      = WidgetElementType::Text;
                row.text      = m_buildOutputLines[i];
                row.font      = theme.fontDefault;
                row.fontSize  = theme.fontSizeSmall;
                row.style.textColor = Vec4{ 0.75f, 0.80f, 0.75f, 1.0f };
                row.textAlignH = TextAlignH::Left;
                row.textAlignV = TextAlignV::Center;
                row.fillX     = true;
                row.minSize   = Vec2{ 0.0f, rowH };
                row.runtimeOnly = true;
                outputScroll->children.push_back(std::move(row));
            }

            // Auto-scroll to bottom
            outputScroll->scrollOffset = 999999.0f;
        }

        dirty = true;
    }

    if (stepDirty)
    {
        updateBuildProgress(stepStatus, stepNum, stepTotal);
        dirty = true;
    }

    if (finished)
    {
        if (m_buildThread.joinable())
            m_buildThread.join();
        m_buildRunning.store(false);
        closeBuildProgress(success, errorMsg);
        dirty = true;
    }

    if (dirty)
    {
        if (m_buildProgressWidget)
            m_buildProgressWidget->markLayoutDirty();
        if (m_buildPopup && m_buildPopup->isOpen())
            m_buildPopup->uiManager().m_renderDirty = true;
        m_renderDirty = true;
    }
}

// ---------------------------------------------------------------------------
// Silent process helpers (no console window) for CMake / Toolchain detection
// ---------------------------------------------------------------------------
#if defined(_WIN32)
namespace {

// Run a shell command silently (CREATE_NO_WINDOW) and return the exit code.
static int shellExecSilent(const std::string& shellCmd)
{
    std::string cmdLine = "cmd.exe /c " + shellCmd;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, 10000); // 10 s timeout
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

// Run a shell command silently and capture the first line of stdout.
static std::string shellReadSilent(const std::string& shellCmd)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return {};
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    std::string cmdLine = "cmd.exe /c " + shellCmd;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!ok)
    {
        CloseHandle(hRead);
        return {};
    }

    std::string result;
    char buf[1024];
    DWORD bytesRead = 0;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
    {
        buf[bytesRead] = '\0';
        result += buf;
        if (result.find('\n') != std::string::npos)
            break;
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto pos = result.find('\n');
    if (pos != std::string::npos)
        result = result.substr(0, pos);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}

} // anonymous namespace
#endif // _WIN32

// ---------------------------------------------------------------------------
// detectCMake – locate cmake executable
// ---------------------------------------------------------------------------
bool UIManager::detectCMake()
{
    m_cmakeAvailable = false;
    m_cmakePath.clear();

    // Helper: run a command silently and check exit code
    auto tryExec = [](const std::string& path) -> bool
    {
#if defined(_WIN32)
        const std::string cmd = "\"" + path + "\" --version >nul 2>&1";
        return shellExecSilent(cmd) == 0;
#else
        const std::string cmd = "\"" + path + "\" --version >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
#endif
    };

    // Helper: read the first line of output from a command
#if defined(_WIN32)
    auto readFirstLine = [](const std::string& cmd) -> std::string
    {
        return shellReadSilent(cmd);
    };
#endif

    // 1. Bundled location: <engine>/Tools/cmake/bin/cmake.exe
    {
        const char* bp = SDL_GetBasePath();
        if (bp)
        {
            auto bundled = std::filesystem::path(bp) / "Tools" / "cmake" / "bin" / "cmake.exe";
            if (std::filesystem::exists(bundled) && tryExec(bundled.string()))
            {
                m_cmakePath = bundled.string();
                m_cmakeAvailable = true;
                return true;
            }
        }
    }

    // 2. VS-bundled cmake via vswhere (Windows)
#if defined(_WIN32)
    {
        const std::string vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (std::filesystem::exists(vswhere))
        {
            std::string vsPath = readFirstLine(
                "\"" + vswhere + "\" -latest -property installationPath 2>nul");
            if (!vsPath.empty())
            {
                auto vsCmake = std::filesystem::path(vsPath)
                    / "Common7" / "IDE" / "CommonExtensions"
                    / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe";
                if (std::filesystem::exists(vsCmake) && tryExec(vsCmake.string()))
                {
                    m_cmakePath = vsCmake.string();
                    m_cmakeAvailable = true;
                    return true;
                }
            }
        }
    }
#endif

    // 3. System PATH – resolve to absolute path
    if (tryExec("cmake"))
    {
#if defined(_WIN32)
        std::string resolved = readFirstLine("where cmake 2>nul");
        if (!resolved.empty() && std::filesystem::exists(resolved))
            m_cmakePath = resolved;
        else
            m_cmakePath = "cmake";
#else
        m_cmakePath = "cmake";
#endif
        m_cmakeAvailable = true;
        return true;
    }

    // 4. Common install locations (Windows)
#if defined(_WIN32)
    {
        static const char* candidates[] = {
            "C:\\Program Files\\CMake\\bin\\cmake.exe",
            "C:\\Program Files (x86)\\CMake\\bin\\cmake.exe",
        };
        for (const auto* c : candidates)
        {
            if (std::filesystem::exists(c) && tryExec(c))
            {
                m_cmakePath = c;
                m_cmakeAvailable = true;
                return true;
            }
        }
    }
#endif

    return false;
}

// ---------------------------------------------------------------------------
// showCMakeInstallPrompt – modal popup if CMake is missing
// ---------------------------------------------------------------------------
void UIManager::showCMakeInstallPrompt()
{
    showConfirmDialog(
        "CMake wird zum Bauen des Spiels benoetigt, "
        "wurde aber nicht gefunden.\n\n"
        "Soll die CMake-Downloadseite geoeffnet werden?",
        [this]()
        {
            // Open the CMake download page in the default browser
#if defined(_WIN32)
            ShellExecuteA(nullptr, "open",
                "https://cmake.org/download/", nullptr, nullptr, SW_SHOWNORMAL);
#else
            std::system("xdg-open https://cmake.org/download/ &");
#endif
            showToastMessage("Bitte CMake installieren und den Editor neu starten.", 8.0f,
                NotificationLevel::Warning);
        },
        [this]()
        {
            showToastMessage("CMake nicht verfuegbar – Build Game deaktiviert.", 5.0f,
                NotificationLevel::Warning);
        }
    );
}

// ---------------------------------------------------------------------------
// detectBuildToolchain – check for MSVC / Clang / GCC
// ---------------------------------------------------------------------------
bool UIManager::detectBuildToolchain()
{
    m_toolchainAvailable = false;
    m_toolchainInfo = {};

#if defined(_WIN32)
    // Helper: read first line from a command (silent, no console window)
    auto readLine = [](const std::string& cmd) -> std::string
    {
        return shellReadSilent(cmd);
    };

    // 1. Try vswhere to find Visual Studio
    const std::string vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
    if (std::filesystem::exists(vswhere))
    {
        std::string vsPath = readLine(
            "\"" + vswhere + "\" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>nul");
        if (!vsPath.empty() && std::filesystem::exists(vsPath))
        {
            m_toolchainInfo.vsInstallPath = vsPath;
            m_toolchainInfo.name = "MSVC";

            // Get VS product display version (e.g. "18.4.1")
            std::string ver = readLine(
                "\"" + vswhere + "\" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog.productDisplayVersion 2>nul");
            if (!ver.empty())
                m_toolchainInfo.version = ver;

            // Try to find cl.exe
            auto vcToolsDir = std::filesystem::path(vsPath) / "VC" / "Tools" / "MSVC";
            if (std::filesystem::exists(vcToolsDir))
            {
                // Pick the latest version subdirectory
                std::string latest;
                for (auto& entry : std::filesystem::directory_iterator(vcToolsDir))
                {
                    if (entry.is_directory())
                    {
                        std::string name = entry.path().filename().string();
                        if (name > latest)
                            latest = name;
                    }
                }
                if (!latest.empty())
                {
                    auto cl = vcToolsDir / latest / "bin" / "Hostx64" / "x64" / "cl.exe";
                    if (std::filesystem::exists(cl))
                        m_toolchainInfo.compilerPath = cl.string();
                }
            }

            m_toolchainAvailable = true;
            return true;
        }
    }

    // 2. Check for cl.exe in PATH
    if (shellExecSilent("where cl.exe >nul 2>&1") == 0)
    {
        std::string clPath = shellReadSilent("where cl.exe 2>nul");
        m_toolchainInfo.name = "MSVC";
        m_toolchainInfo.compilerPath = clPath;
        m_toolchainAvailable = true;
        return true;
    }

    // 3. Check for clang-cl in PATH
    if (shellExecSilent("where clang-cl.exe >nul 2>&1") == 0)
    {
        std::string clangPath = shellReadSilent("where clang-cl.exe 2>nul");
        m_toolchainInfo.name = "Clang-CL";
        m_toolchainInfo.compilerPath = clangPath;
        m_toolchainAvailable = true;
        return true;
    }

#else  // Linux / macOS
    auto tryWhich = [](const char* tool) -> std::string
    {
        std::string cmd = std::string("which ") + tool + " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return {};
        char buf[512];
        std::string result;
        if (fgets(buf, sizeof(buf), pipe))
            result = buf;
        pclose(pipe);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    };

    std::string gpp = tryWhich("g++");
    if (!gpp.empty())
    {
        m_toolchainInfo.name = "GCC";
        m_toolchainInfo.compilerPath = gpp;
        m_toolchainAvailable = true;
        return true;
    }

    std::string clangpp = tryWhich("clang++");
    if (!clangpp.empty())
    {
        m_toolchainInfo.name = "Clang";
        m_toolchainInfo.compilerPath = clangpp;
        m_toolchainAvailable = true;
        return true;
    }
#endif

    return false;
}

// ---------------------------------------------------------------------------
// showToolchainInstallPrompt – modal popup if no C++ compiler found
// ---------------------------------------------------------------------------
void UIManager::showToolchainInstallPrompt()
{
    showConfirmDialog(
        "Keine C++ Build-Toolchain (MSVC / Clang) gefunden.\n\n"
        "Zum Bauen des Spiels wird ein C++ Compiler benoetigt.\n"
        "Bitte installiere Visual Studio mit der Workload\n"
        "\"Desktopentwicklung mit C++\".\n\n"
        "Soll die Visual Studio-Downloadseite geoeffnet werden?",
        [this]()
        {
#if defined(_WIN32)
            ShellExecuteA(nullptr, "open",
                "https://visualstudio.microsoft.com/downloads/", nullptr, nullptr, SW_SHOWNORMAL);
#else
            std::system("xdg-open https://visualstudio.microsoft.com/downloads/ &");
#endif
            showToastMessage("Bitte C++ Toolchain installieren und den Editor neu starten.", 8.0f,
                NotificationLevel::Warning);
        },
        [this]()
        {
            showToastMessage("Keine Build-Toolchain – Build Game deaktiviert.", 5.0f,
                NotificationLevel::Warning);
        }
    );
}

// ---------------------------------------------------------------------------
// startAsyncToolchainDetection – run CMake + toolchain detection on a
//                                 background thread (non-blocking)
// ---------------------------------------------------------------------------
void UIManager::startAsyncToolchainDetection()
{
    m_toolDetectDone.store(false);
    m_toolDetectPolled = false;

    std::thread([this]()
    {
        detectCMake();
        detectBuildToolchain();
        m_toolDetectDone.store(true);
    }).detach();
}

// ---------------------------------------------------------------------------
// pollToolchainDetection – call once per frame from the main thread.
//                          When detection finishes, logs results and shows
//                          install prompts if needed.
// ---------------------------------------------------------------------------
void UIManager::pollToolchainDetection()
{
    if (m_toolDetectPolled || !m_toolDetectDone.load())
        return;
    m_toolDetectPolled = true;

    auto& logger = Logger::Instance();

    if (!m_cmakeAvailable)
    {
        logger.log(Logger::Category::Engine,
            "CMake not found \xe2\x80\x93 Build Game will not be available.",
            Logger::LogLevel::WARNING);
        showCMakeInstallPrompt();
    }
    else
    {
        logger.log(Logger::Category::Engine,
            "CMake found: " + m_cmakePath,
            Logger::LogLevel::INFO);
    }

    if (!m_toolchainAvailable)
    {
        logger.log(Logger::Category::Engine,
            "C++ toolchain not found \xe2\x80\x93 Build Game will not be available.",
            Logger::LogLevel::WARNING);
        showToolchainInstallPrompt();
    }
    else
    {
        logger.log(Logger::Category::Engine,
            "Toolchain: " + m_toolchainInfo.name + " " + m_toolchainInfo.version,
            Logger::LogLevel::INFO);
    }
}

// ===========================================================================
// Animation Editor Tab (Phase 2.4)
// ===========================================================================

// ---------------------------------------------------------------------------
// isAnimationEditorOpen
// ---------------------------------------------------------------------------
bool UIManager::isAnimationEditorOpen() const
{
    return m_animationEditorState.isOpen;
}

// ---------------------------------------------------------------------------
// openAnimationEditorTab
// ---------------------------------------------------------------------------
void UIManager::openAnimationEditorTab(ECS::Entity entity)
{
    if (!m_renderer)
        return;

    auto& ecs = ECS::ECSManager::Instance();
    if (!ecs.hasComponent<ECS::AnimationComponent>(entity))
        return;

    if (!m_renderer->isEntitySkinned(entity))
        return;

    const std::string tabId = "AnimationEditor";

    // If already open for this entity, just switch to it
    if (m_animationEditorState.isOpen && m_animationEditorState.linkedEntity == entity)
    {
        m_renderer->setActiveTab(tabId);
        markAllWidgetsDirty();
        return;
    }

    // If open for a different entity, close first
    if (m_animationEditorState.isOpen)
        closeAnimationEditorTab();

    m_renderer->addTab(tabId, "Animation Editor", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "AnimationEditor.Main";
    unregisterWidget(widgetId);

    m_animationEditorState = {};
    m_animationEditorState.tabId        = tabId;
    m_animationEditorState.widgetId     = widgetId;
    m_animationEditorState.linkedEntity = entity;
    m_animationEditorState.isOpen       = true;
    m_animationEditorState.selectedClip = m_renderer->getEntityAnimatorCurrentClip(entity);

    // Build the main widget
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "AnimationEditor.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // Toolbar
        buildAnimationEditorToolbar(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Scrollable content area
        {
            WidgetElement contentArea{};
            contentArea.id          = "AnimationEditor.ContentArea";
            contentArea.type        = WidgetElementType::StackPanel;
            contentArea.fillX       = true;
            contentArea.fillY       = true;
            contentArea.scrollable  = true;
            contentArea.orientation = StackOrientation::Vertical;
            contentArea.padding     = EditorTheme::Scaled(Vec2{ 10.0f, 8.0f });
            contentArea.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            contentArea.runtimeOnly = true;
            root.children.push_back(std::move(contentArea));
        }

        widget->setElements({ std::move(root) });
        registerWidget(widgetId, widget, tabId);
    }

    // Tab / close click events
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refreshAnimationEditor();
        markAllWidgetsDirty();
    });

    registerClickEvent(closeBtnId, [this]()
    {
        closeAnimationEditorTab();
    });

    // Stop button
    registerClickEvent("AnimationEditor.Stop", [this]()
    {
        if (m_renderer)
        {
            m_renderer->stopEntityAnimation(m_animationEditorState.linkedEntity);
            refreshAnimationEditor();
        }
    });

    // Initial population
    refreshAnimationEditor();
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// closeAnimationEditorTab
// ---------------------------------------------------------------------------
void UIManager::closeAnimationEditorTab()
{
    if (!m_animationEditorState.isOpen || !m_renderer)
        return;

    const std::string tabId = m_animationEditorState.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    unregisterWidget(m_animationEditorState.widgetId);

    m_renderer->removeTab(tabId);
    m_animationEditorState = {};
    markAllWidgetsDirty();
}

// ---------------------------------------------------------------------------
// buildAnimationEditorToolbar
// ---------------------------------------------------------------------------
void UIManager::buildAnimationEditorToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "AnimationEditor.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title
    {
        std::string titleText = "Animation Editor";
        auto& ecs = ECS::ECSManager::Instance();
        if (const auto* name = ecs.getComponent<ECS::NameComponent>(m_animationEditorState.linkedEntity))
        {
            if (!name->displayName.empty())
                titleText += "  -  " + name->displayName;
        }
        titleText += " (Entity " + std::to_string(m_animationEditorState.linkedEntity) + ")";

        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = titleText;
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 200.0f, 24.0f });
        title.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Stop button
    {
        WidgetElement stopBtn = EditorUIBuilder::makeButton(
            "AnimationEditor.Stop", "Stop", {}, EditorTheme::Scaled(Vec2{ 60.0f, 24.0f }));
        stopBtn.tooltipText = "Stop playback";
        toolbar.children.push_back(std::move(stopBtn));
    }

    root.children.push_back(std::move(toolbar));
}

// ---------------------------------------------------------------------------
// refreshAnimationEditor – rebuilds the content area from the linked entity
// ---------------------------------------------------------------------------
void UIManager::refreshAnimationEditor()
{
    if (!m_animationEditorState.isOpen)
        return;

    auto* entry = findWidgetEntry(m_animationEditorState.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find ContentArea inside root
    WidgetElement* contentArea = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "AnimationEditor.ContentArea")
        {
            contentArea = &child;
            break;
        }
    }
    if (!contentArea)
        return;

    contentArea->children.clear();

    auto& ecs = ECS::ECSManager::Instance();
    const ECS::Entity entity = m_animationEditorState.linkedEntity;
    const auto* animComp = ecs.getComponent<ECS::AnimationComponent>(entity);
    if (!animComp || !m_renderer)
    {
        contentArea->children.push_back(EditorUIBuilder::makeLabel("Entity no longer has an Animation component."));
        entry->widget->markLayoutDirty();
        m_renderDirty = true;
        return;
    }

    // Build sections
    buildAnimationEditorClipList(*contentArea);
    buildAnimationEditorControls(*contentArea);
    buildAnimationEditorBoneTree(*contentArea);

    entry->widget->markLayoutDirty();
    m_renderDirty = true;
}

// ---------------------------------------------------------------------------
// buildAnimationEditorClipList
// ---------------------------------------------------------------------------
void UIManager::buildAnimationEditorClipList(WidgetElement& root)
{
    if (!m_renderer) return;
    const ECS::Entity entity = m_animationEditorState.linkedEntity;
    const int clipCount = m_renderer->getEntityAnimationClipCount(entity);
    const int currentClip = m_renderer->getEntityAnimatorCurrentClip(entity);

    // Heading
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        root.children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading("Animation Clips");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    if (clipCount == 0)
    {
        root.children.push_back(EditorUIBuilder::makeLabel("No animation clips found."));
        return;
    }

    auto& ecs = ECS::ECSManager::Instance();
    const auto* animComp = ecs.getComponent<ECS::AnimationComponent>(entity);

    for (int i = 0; i < clipCount; ++i)
    {
        const auto info = m_renderer->getEntityAnimationClipInfo(entity, i);
        const bool isCurrent = (i == currentClip);
        const float durationSec = info.ticksPerSecond > 0.0f ? info.duration / info.ticksPerSecond : 0.0f;

        std::string label = info.name.empty() ? ("Clip " + std::to_string(i)) : info.name;
        label += "  (" + std::to_string(info.channelCount) + " ch, "
              + std::to_string(static_cast<int>(durationSec * 10.0f) / 10.0f).substr(0, 4) + "s)";

        const auto& theme = EditorTheme::Get();
        Vec4 btnColor = isCurrent ? theme.accent : theme.buttonDefault;

        WidgetElement btn = EditorUIBuilder::makeButton(
            "AnimationEditor.Clip." + std::to_string(i), label,
            [this, i, entity]()
            {
                if (m_renderer)
                {
                    auto& ecs2 = ECS::ECSManager::Instance();
                    auto* comp = ecs2.getComponent<ECS::AnimationComponent>(entity);
                    bool loop = comp ? comp->loop : true;
                    m_renderer->playEntityAnimation(entity, i, loop);
                    if (comp)
                    {
                        comp->currentClipIndex = i;
                        comp->playing = true;
                    }
                    m_animationEditorState.selectedClip = i;
                    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                        level->setIsSaved(false);
                    refreshAnimationEditor();
                }
            },
            EditorTheme::Scaled(Vec2{ 0.0f, 24.0f }));
        btn.fillX = true;
        btn.style.color = btnColor;
        btn.runtimeOnly = true;
        root.children.push_back(std::move(btn));
    }
}

// ---------------------------------------------------------------------------
// buildAnimationEditorControls
// ---------------------------------------------------------------------------
void UIManager::buildAnimationEditorControls(WidgetElement& root)
{
    if (!m_renderer) return;
    const ECS::Entity entity = m_animationEditorState.linkedEntity;
    auto& ecs = ECS::ECSManager::Instance();
    const auto* animComp = ecs.getComponent<ECS::AnimationComponent>(entity);
    if (!animComp) return;

    // Heading
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        root.children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading("Playback Controls");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    // Status info
    {
        bool playing = m_renderer->isEntityAnimatorPlaying(entity);
        float currentTime = m_renderer->getEntityAnimatorCurrentTime(entity);
        int currentClip = m_renderer->getEntityAnimatorCurrentClip(entity);
        std::string status = playing ? "Playing" : "Stopped";
        if (currentClip >= 0)
        {
            auto clipInfo = m_renderer->getEntityAnimationClipInfo(entity, currentClip);
            float tps = clipInfo.ticksPerSecond > 0.0f ? clipInfo.ticksPerSecond : 25.0f;
            status += "  |  Clip: " + (clipInfo.name.empty() ? std::to_string(currentClip) : clipInfo.name);
            status += "  |  Time: " + std::to_string(static_cast<int>(currentTime / tps * 100.0f) / 100.0f).substr(0, 5) + "s";
        }
        root.children.push_back(EditorUIBuilder::makeLabel(status));
    }

    // Speed slider
    {
        WidgetElement speedRow = EditorUIBuilder::makeSliderRow(
            "AnimationEditor.Speed", "Speed", animComp->speed, 0.0f, 5.0f,
            [this, entity](float v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::AnimationComponent>(entity);
                if (comp)
                    comp->speed = v;
                if (m_renderer)
                    m_renderer->setEntityAnimationSpeed(entity, v);
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                    level->setIsSaved(false);
            });
        root.children.push_back(std::move(speedRow));
    }

    // Loop checkbox
    {
        WidgetElement loopRow = EditorUIBuilder::makeCheckBox(
            "AnimationEditor.Loop", "Loop", animComp->loop,
            [this, entity](bool v)
            {
                auto& ecs2 = ECS::ECSManager::Instance();
                auto* comp = ecs2.getComponent<ECS::AnimationComponent>(entity);
                if (comp)
                    comp->loop = v;
                if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
                    level->setIsSaved(false);
            });
        root.children.push_back(std::move(loopRow));
    }
}

// ---------------------------------------------------------------------------
// buildAnimationEditorBoneTree
// ---------------------------------------------------------------------------
void UIManager::buildAnimationEditorBoneTree(WidgetElement& root)
{
    if (!m_renderer) return;
    const ECS::Entity entity = m_animationEditorState.linkedEntity;
    const int boneCount = m_renderer->getEntityBoneCount(entity);

    // Heading
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 6.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        root.children.push_back(std::move(spacer));

        WidgetElement heading = EditorUIBuilder::makeHeading("Bone Hierarchy (" + std::to_string(boneCount) + " bones)");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    if (boneCount == 0)
    {
        root.children.push_back(EditorUIBuilder::makeLabel("No bones found."));
        return;
    }

    // Build indented bone list
    // First pass: determine depth for each bone
    std::vector<int> depth(boneCount, 0);
    for (int i = 0; i < boneCount; ++i)
    {
        int parent = m_renderer->getEntityBoneParent(entity, i);
        int d = 0;
        int curr = parent;
        while (curr >= 0 && d < 20)
        {
            ++d;
            curr = m_renderer->getEntityBoneParent(entity, curr);
        }
        depth[i] = d;
    }

    const auto& theme = EditorTheme::Get();
    for (int i = 0; i < boneCount; ++i)
    {
        std::string boneName = m_renderer->getEntityBoneName(entity, i);
        if (boneName.empty())
            boneName = "Bone " + std::to_string(i);

        std::string indent;
        for (int d = 0; d < depth[i]; ++d)
            indent += "  ";

        std::string prefix = (depth[i] > 0) ? "|- " : "";

        WidgetElement lbl{};
        lbl.type            = WidgetElementType::Text;
        lbl.text            = indent + prefix + boneName;
        lbl.font            = theme.fontDefault;
        lbl.fontSize        = theme.fontSizeSmall;
        lbl.style.textColor = theme.textSecondary;
        lbl.fillX           = true;
        lbl.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 18.0f });
        lbl.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 1.0f });
        lbl.runtimeOnly     = true;
        root.children.push_back(std::move(lbl));
    }
}
#endif // ENGINE_EDITOR
