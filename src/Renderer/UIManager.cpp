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
#include "EditorTabs/ConsoleTab.h"
#include "EditorTabs/ProfilerTab.h"
#include "EditorTabs/AudioPreviewTab.h"
#include "EditorTabs/ParticleEditorTab.h"
#include "EditorTabs/ShaderViewerTab.h"
#include "EditorTabs/RenderDebuggerTab.h"
#include "EditorTabs/SequencerTab.h"
#include "EditorTabs/LevelCompositionTab.h"
#include "EditorTabs/AnimationEditorTab.h"
#include "EditorTabs/UIDesignerTab.h"
#include "EditorTabs/WidgetEditorTab.h"
#include "EditorTabs/ContentBrowserPanel.h"
#include "EditorTabs/OutlinerPanel.h"
#include "EditorTabs/BuildSystemUI.h"
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ WrapBox: children flow and wrap ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ UniformGrid: all cells equal size ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ SizeBox: single child with size override ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ ScaleBox: single child, scaled to fit ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ WidgetSwitcher: only active child ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ Overlay: all children stacked, each aligned within the same area ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ Border: single child inset by border thickness + content padding ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ ListView: vertical stack of items ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ TileView: grid of tiles ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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
    m_buildSystemUI = std::make_unique<BuildSystemUI>(this, m_renderer);
    m_editorDialogs = std::make_unique<EditorDialogs>(this, m_renderer);
    m_outlinerPanel = std::make_unique<OutlinerPanel>(this, m_renderer);
    m_levelChangedCallbackToken = DiagnosticsManager::Instance().registerActiveLevelChangedCallback(
        [this](EngineLevel* level)
        {
            if (m_outlinerPanel) m_outlinerPanel->setLevel(level);
            refreshWorldOutliner();
        });
#endif // ENGINE_EDITOR
}

UIManager::~UIManager()
{
#if ENGINE_EDITOR
    m_buildSystemUI.reset();
    m_editorDialogs.reset();
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
    if (m_outlinerPanel) m_outlinerPanel->refresh();
}

unsigned int UIManager::getSelectedEntity() const
{
    return m_outlinerPanel ? m_outlinerPanel->getSelectedEntity() : 0;
}

void UIManager::invalidateHoveredElement()
{
    m_lastHoveredElement = nullptr;
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
            populateOutlinerDetails(m_outlinerPanel ? m_outlinerPanel->getSelectedEntity() : 0);
        }
        else if (id == "ContentBrowser")
        {
            if (!m_contentBrowserPanel)
                m_contentBrowserPanel = std::make_unique<ContentBrowserPanel>(this, m_renderer);
            m_contentBrowserPanel->populateWidget(entry->widget);
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
#if ENGINE_EDITOR
    m_editorDialogs->showModalMessage(message, std::move(onClosed));
#endif
}

void UIManager::closeModalMessage()
{
#if ENGINE_EDITOR
    m_editorDialogs->closeModalMessage();
#endif
}

#if ENGINE_EDITOR
void UIManager::showConfirmDialog(const std::string& message, std::function<void()> onConfirm, std::function<void()> onCancel)
{
    m_editorDialogs->showConfirmDialog(message, std::move(onConfirm), std::move(onCancel));
}

void UIManager::showConfirmDialogWithCheckbox(const std::string& message, const std::string& checkboxLabel, bool checkedByDefault,
    std::function<void(bool checked)> onConfirm, std::function<void()> onCancel)
{
    m_editorDialogs->showConfirmDialogWithCheckbox(message, checkboxLabel, checkedByDefault, std::move(onConfirm), std::move(onCancel));
}
#endif // ENGINE_EDITOR

void UIManager::showToastMessage(const std::string& message, float durationSeconds)
{
#if ENGINE_EDITOR
    showToastMessage(message, durationSeconds, NotificationLevel::Info);
#endif
}

void UIManager::showToastMessage(const std::string& message, float durationSeconds, NotificationLevel level)
{
#if ENGINE_EDITOR
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
#endif // ENGINE_EDITOR (notification history)
#endif // ENGINE_EDITOR (showToastMessage)
}

void UIManager::updateNotifications(float deltaSeconds)
{
#if ENGINE_EDITOR
    // Detect when the asset registry becomes ready and refresh the Content Browser
    {
        const bool registryReady = DiagnosticsManager::Instance().isAssetRegistryReady();
        if (registryReady && (!m_contentBrowserPanel || !m_contentBrowserPanel->registryWasReady()))
        {
            Logger::Instance().log(Logger::Category::UI, "Asset registry became ready, refreshing Content Browser.", Logger::LogLevel::INFO);
            refreshContentBrowser();
        }
        if (m_contentBrowserPanel) m_contentBrowserPanel->setRegistryWasReady(registryReady);
    }

    // Detect ECS component changes and refresh EntityDetails for the selected entity
    {
        const uint64_t currentVersion = ECS::ECSManager::Instance().getComponentVersion();
        if (m_outlinerPanel && currentVersion != m_outlinerPanel->getLastEcsComponentVersion())
        {
            m_outlinerPanel->setLastEcsComponentVersion(currentVersion);
            if (m_outlinerPanel->getSelectedEntity() != 0)
            {
                populateOutlinerDetails(m_outlinerPanel->getSelectedEntity());
            }
        }
    }

    // Detect asset registry changes (new assets created/imported) and refresh EntityDetails dropdowns
    {
        const uint64_t currentRegVer = AssetManager::Instance().getRegistryVersion();
        if (m_outlinerPanel && currentRegVer != m_outlinerPanel->getLastRegistryVersion())
        {
            m_outlinerPanel->setLastRegistryVersion(currentRegVer);
            if (m_outlinerPanel->getSelectedEntity() != 0)
            {
                populateOutlinerDetails(m_outlinerPanel->getSelectedEntity());
            }
        }
    }
#endif // ENGINE_EDITOR

#if ENGINE_EDITOR
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

    {
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
    }
#endif // ENGINE_EDITOR

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Hover transition interpolation (Phase 1.5) ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    updateHoverTransitions(deltaSeconds);

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Scrollbar auto-hide (Phase 1.6) ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    updateScrollbarVisibility(deltaSeconds);

#if ENGINE_EDITOR
    if (m_consoleTab)
        m_consoleTab->update(deltaSeconds);


    if (m_profilerTab)
        m_profilerTab->update(deltaSeconds);

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Particle editor refresh ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    if (m_particleEditorTab)
        m_particleEditorTab->update(deltaSeconds);

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Render debugger refresh ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    if (m_renderDebuggerTab)
        m_renderDebuggerTab->update(deltaSeconds);

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Sequencer refresh (while playing) ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    if (m_sequencerTab)
        m_sequencerTab->update(deltaSeconds);
#endif // ENGINE_EDITOR

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Tooltip timer ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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
        // Tooltip was hidden by updateHoverStates ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â remove the widget
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
    if (m_outlinerPanel) m_outlinerPanel->populateWidget(widget);
}

void UIManager::selectEntity(unsigned int entity)
{
    if (m_outlinerPanel) m_outlinerPanel->selectEntity(entity);
}

// ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Undo/Redo helper: capture old component state, apply mutation, push command ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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
    if (m_outlinerPanel) m_outlinerPanel->populateDetails(entity);
}

void UIManager::applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity)
{
    if (m_outlinerPanel)
        m_outlinerPanel->applyAssetToEntity(type, assetPath, entity);
}

void UIManager::refreshContentBrowser(const std::string& subfolder)
{
    if (!m_contentBrowserPanel)
        m_contentBrowserPanel = std::make_unique<ContentBrowserPanel>(this, m_renderer);
    m_contentBrowserPanel->refresh(subfolder);
}

void UIManager::focusContentBrowserSearch()
{
    if (m_contentBrowserPanel)
        m_contentBrowserPanel->focusSearch();
}

// ---------------------------------------------------------------------------
// Entity clipboard: Copy / Paste / Duplicate
// ---------------------------------------------------------------------------

void UIManager::copySelectedEntity()
{
    if (m_outlinerPanel)
        m_outlinerPanel->copySelectedEntity();
}

bool UIManager::pasteEntity()
{
    return m_outlinerPanel ? m_outlinerPanel->pasteEntity() : false;
}

bool UIManager::duplicateSelectedEntity()
{
    return m_outlinerPanel ? m_outlinerPanel->duplicateEntity() : false;
}

bool UIManager::hasEntityClipboard() const
{
    return m_outlinerPanel ? m_outlinerPanel->hasClipboard() : false;
}

// ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Prefab / Entity Templates ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬

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
        showToastMessage("No project loaded.", kToastMedium);
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
        showToastMessage("Failed to create prefab file.", kToastMedium);
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
    showToastMessage("Prefab saved: " + entry.name, kToastMedium);

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
        showToastMessage("No project loaded.", kToastMedium);
        return false;
    }

    // Load prefab JSON from disk
    const std::filesystem::path absPath =
        std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content" / prefabRelPath;

    if (!std::filesystem::exists(absPath))
    {
        showToastMessage("Prefab file not found.", kToastMedium);
        return false;
    }

    std::ifstream in(absPath);
    if (!in.is_open())
    {
        showToastMessage("Failed to open prefab file.", kToastMedium);
        return false;
    }

    json fileJson;
    try { in >> fileJson; } catch (...) { showToastMessage("Invalid prefab file.", kToastMedium); return false; }

    if (!fileJson.is_object() || !fileJson.contains("data"))
    {
        showToastMessage("Invalid prefab format.", kToastMedium);
        return false;
    }

    const auto& data = fileJson.at("data");
    if (!data.contains("entities") || !data.at("entities").is_array())
    {
        showToastMessage("Prefab has no entities.", kToastMedium);
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
    // else: "Empty Entity" ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ just Transform + Name, already added above

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

    // Nearly cubic ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ Sphere if all dimensions similar, else Box
    const float midHalf = halfX + halfY + halfZ - maxHalf - minHalf;
    const float sphereRatio = (minHalf > 0.001f) ? (maxHalf / minHalf) : 10.0f;

    if (sphereRatio < 1.4f)
    {
        // Approximately cube-like ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ Sphere
        collision.colliderType = ECS::CollisionComponent::ColliderType::Sphere;
        collision.colliderSize[0] = maxHalf; // radius
        collision.colliderSize[1] = 0.0f;
        collision.colliderSize[2] = 0.0f;
    }
    else if (aspectRatio > 2.5f && halfY > halfX && halfY > halfZ)
    {
        // Tall and thin ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ Capsule (vertical)
        collision.colliderType = ECS::CollisionComponent::ColliderType::Capsule;
        collision.colliderSize[0] = std::max(halfX, halfZ); // radius
        collision.colliderSize[1] = halfY;                   // half-height
        collision.colliderSize[2] = 0.0f;
    }
    else
    {
        // Default ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ Box
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
// computeEntityBottomOffset ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ distance from entity pivot to bottom of mesh
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
// dropSelectedEntitiesToSurface ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ raycast each selected entity downward and
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
    else if (getSelectedEntity() != 0)
    {
        entities.push_back(static_cast<ECS::Entity>(getSelectedEntity()));
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
    if (getSelectedEntity() != 0)
        populateOutlinerDetails(getSelectedEntity());
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

        // Skybox ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â try the first available skybox asset
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
    if (m_outlinerPanel) m_outlinerPanel->setSelectedEntity(0);
    refreshWorldOutliner();
    refreshContentBrowser();

    const char* templateNames[] = { "Empty", "Basic Outdoor", "Prototype" };
    showToastMessage("Created level '" + levelName + "' (" + templateNames[static_cast<int>(tmpl)] + ") ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ unsaved", kToastMedium);
    logger.log(Logger::Category::UI, "Created new level: " + levelName + " with template " + templateNames[static_cast<int>(tmpl)], Logger::LogLevel::INFO);
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

    // Icon child (left side, square ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â pixel-sized so it stays 1:1 regardless of button width)
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

#endif // ENGINE_EDITOR


const std::vector<UIManager::WidgetEntry>& UIManager::getRegisteredWidgets() const
{
    return m_widgets;
}

std::vector<UIManager::WidgetEntry>& UIManager::getRegisteredWidgetsMutable()
{
    return m_widgets;
}

#if ENGINE_EDITOR
std::string UIManager::getSelectedBrowserFolder() const
{
    return m_contentBrowserPanel ? m_contentBrowserPanel->getSelectedBrowserFolder() : std::string{};
}

std::string UIManager::getSelectedGridAsset() const
{
    return m_contentBrowserPanel ? m_contentBrowserPanel->getSelectedGridAsset() : std::string{};
}

void UIManager::clearSelectedGridAsset()
{
    if (m_contentBrowserPanel)
        m_contentBrowserPanel->clearSelectedGridAsset();
}

void UIManager::requestLevelLoad(const std::string& levelRelPath)
{
    if (m_onLevelLoadRequested)
        m_onLevelLoadRequested(levelRelPath);
}
#endif // ENGINE_EDITOR

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
    if (!anyDirty && m_widgetEditorTab)
    {
        for (const auto& [tabId, state] : m_widgetEditorTab->getStates())
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
    if (m_widgetEditorTab)
    {
    for (auto& [tabId, state] : m_widgetEditorTab->getStates())
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
            if (auto* weState = (m_widgetEditorTab ? m_widgetEditorTab->getActiveState() : nullptr))
            {
                if (m_widgetEditorTab && m_widgetEditorTab->isOverCanvas(screenPos))
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

    // Check if this element is draggable ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â set up pending drag
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
            if (m_outlinerPanel) m_outlinerPanel->setSelectedEntity(static_cast<unsigned int>(entityValue));
            populateOutlinerDetails(getSelectedEntity());
        }
    }
#endif // ENGINE_EDITOR
    const std::string separatorPrefix = "Separator.Toggle.";
    if (target->id.rfind(separatorPrefix, 0) == 0)
    {
        const std::string separatorId = target->id.substr(separatorPrefix.size());
        const std::string contentId = "Separator.Content." + separatorId;
        // UTF-8: ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ = \xe2\x96\xbe, ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¸ = \xe2\x96\xb8
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
    // Copy target data before callbacks ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â onClicked/onDoubleClicked may rebuild
    // the widget tree (e.g. refreshContentBrowser), invalidating the target pointer.
    const std::string targetId = target->id;
    const std::string targetClickEvent = target->clickEvent;
    const auto targetOnClicked = target->onClicked;
    const auto targetOnDoubleClicked = target->onDoubleClicked;
    const bool targetIsDraggable = target->isDraggable;

    // Suppress click for draggable elements ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â click will fire on mouse-up if no drag occurred
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

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Escape: close dropdowns ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ modals ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ unfocus entry ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ cancel rename ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    if (key == SDLK_ESCAPE)
    {
#if ENGINE_EDITOR
        if (m_dropdownVisible)
        {
            closeDropdownMenu();
            return true;
        }
#endif // ENGINE_EDITOR
#if ENGINE_EDITOR
        if (m_editorDialogs && m_editorDialogs->isModalVisible())
        {
            closeModalMessage();
            return true;
        }
#endif // ENGINE_EDITOR
        if (m_focusedEntry)
        {
            setFocusedEntry(nullptr);
#if ENGINE_EDITOR
            if (m_contentBrowserPanel && m_contentBrowserPanel->isRenamingGridAsset())
            {
                m_contentBrowserPanel->cancelRename();

                refreshContentBrowser();
            }
#endif // ENGINE_EDITOR
            return true;
        }
        return false;
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Tab / Shift+Tab: cycle focus through EntryBar elements ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
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
        if (m_contentBrowserPanel && !m_contentBrowserPanel->getSelectedGridAsset().empty() && !m_contentBrowserPanel->isRenamingGridAsset())
        {
            m_contentBrowserPanel->startRename();

            refreshContentBrowser();
            return true;
        }
        return false;
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Arrow keys: navigate Outliner entity list ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    if ((key == SDLK_UP || key == SDLK_DOWN) && !m_focusedEntry)
    {
        // Only navigate if Outliner is the relevant context (entity is selected or exists)
        if (getSelectedEntity() != 0 || (m_outlinerPanel && m_outlinerPanel->getLevel()))
        {
            navigateOutlinerByArrow(key == SDLK_UP ? -1 : 1);
            return true;
        }
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Arrow keys: navigate Content Browser grid ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    if (!m_focusedEntry && m_contentBrowserPanel && !m_contentBrowserPanel->getSelectedGridAsset().empty())
    {
        int dCol = 0, dRow = 0;
        if (key == SDLK_LEFT)  dCol = -1;
        if (key == SDLK_RIGHT) dCol =  1;
        if (key == SDLK_UP)    dRow = -1;
        if (key == SDLK_DOWN)  dRow =  1;
        if (dCol != 0 || dRow != 0)
        {
            m_contentBrowserPanel->navigateByArrow(dCol, dRow);
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
    if (auto* weState = (m_widgetEditorTab ? m_widgetEditorTab->getActiveState() : nullptr))
    {
        if (m_widgetEditorTab && m_widgetEditorTab->isOverCanvas(screenPos))
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
    if (auto* weState = (m_widgetEditorTab ? m_widgetEditorTab->getActiveState() : nullptr))
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

    // We are dropping ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â figure out where
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
        if (auto* weState = (m_widgetEditorTab ? m_widgetEditorTab->getActiveState() : nullptr))
        {
            if (m_widgetEditorTab && m_widgetEditorTab->isOverCanvas(screenPos))
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
        if (auto* weState = (m_widgetEditorTab ? m_widgetEditorTab->getActiveState() : nullptr))
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
        // Drop on Outliner entity row ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ apply asset to entity
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
            if (pipePos != std::string::npos && getSelectedEntity() != 0)
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
                    applyAssetToEntity(droppedType, assetPath, getSelectedEntity());
                }
                else
                {
                    showToastMessage("Wrong asset type for this slot.", kToastMedium);
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
    if (m_outlinerPanel)
        m_outlinerPanel->navigateByArrow(direction);
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
    populateOutlinerDetails(getSelectedEntity());
    if (auto* cb = findWidgetEntry("ContentBrowser"))
    {
        if (cb->widget && m_contentBrowserPanel) m_contentBrowserPanel->populateWidget(cb->widget);
    }
    refreshStatusBar();

    markAllWidgetsDirty();

    m_uiRenderingPaused = false;
}

void UIManager::applyThemeToAllEditorWidgets()
{
    // Close any open dropdown ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ its Panel/Button elements would receive
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
    applyToTransient(m_editorDialogs ? m_editorDialogs->getModalWidget() : nullptr);
    applyToTransient(m_editorDialogs ? m_editorDialogs->getSaveProgressWidget() : nullptr);
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

#if ENGINE_EDITOR

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
#endif // ENGINE_EDITOR

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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Tooltip tracking ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    if (m_lastHoveredElement && !m_lastHoveredElement->tooltipText.empty())
    {
        // Same element still hovered ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â accumulate time (caller must pass dt)
        if (!m_tooltipVisible && m_tooltipText == m_lastHoveredElement->tooltipText)
        {
            // Timer is advanced externally in updateTooltip(dt)
        }
        else if (m_tooltipText != m_lastHoveredElement->tooltipText)
        {
            // Switched to a new tooltip element ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â reset timer
            m_tooltipTimer = 0.0f;
            m_tooltipVisible = false;
            m_tooltipText = m_lastHoveredElement->tooltipText;
        }
        m_tooltipPosition = Vec2{ m_mousePosition.x + EditorTheme::Scaled(16.0f),
                                   m_mousePosition.y + EditorTheme::Scaled(16.0f) };
    }
    else
    {
        // No tooltip element hovered ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â hide immediately
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

// ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Scrollbar auto-hide (Phase 1.6) ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬

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
    markAllWidgetsDirty();
}


void UIManager::showSaveProgressModal(size_t total) { m_editorDialogs->showSaveProgressModal(total); }
void UIManager::updateSaveProgress(size_t saved, size_t total) { m_editorDialogs->updateSaveProgress(saved, total); }
void UIManager::closeSaveProgressModal(bool success) { m_editorDialogs->closeSaveProgressModal(success); }

void UIManager::showUnsavedChangesDialog(std::function<void()> onDone) { m_editorDialogs->showUnsavedChangesDialog(std::move(onDone)); }

void UIManager::showLevelLoadProgress(const std::string& levelName) { m_editorDialogs->showLevelLoadProgress(levelName); }
void UIManager::updateLevelLoadProgress(const std::string& statusText) { m_editorDialogs->updateLevelLoadProgress(statusText); }
void UIManager::closeLevelLoadProgress() { m_editorDialogs->closeLevelLoadProgress(); }


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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Clamp position so the menu stays on screen ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
    float posX = anchorPixels.x;
    float posY = anchorPixels.y;
    const float screenW = m_availableViewportSize.x;
    const float screenH = m_availableViewportSize.y;

    // Flip upward if the menu would overflow the bottom
    if (posY + menuH > screenH && posY - menuH >= 0.0f)
    {
        posY = posY - menuH;
    }
    // Still overflows bottom (and can't flip) ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ clamp to bottom edge
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
            // Placeholder item with no callback ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â just close the menu
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
    return m_contentBrowserPanel ? m_contentBrowserPanel->isOverGrid(screenPos) : false;
}



// ===========================================================================
// Widget Editor Tab  (delegated to EditorTabs/WidgetEditorTab)
// ===========================================================================

void UIManager::openWidgetEditorPopup(const std::string& relativeAssetPath)
{
 if (!m_widgetEditorTab)
 m_widgetEditorTab = std::make_unique<WidgetEditorTab>(this, m_renderer);
 m_widgetEditorTab->openTab(relativeAssetPath);
}

void UIManager::selectWidgetEditorElement(const std::string& tabId, const std::string& elementId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->selectElement(tabId, elementId);
}

void UIManager::applyWidgetEditorTransform(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->applyTransform(tabId);
}

void UIManager::saveWidgetEditorAsset(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->saveAsset(tabId);
}

void UIManager::markWidgetEditorDirty(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->markDirty(tabId);
}

void UIManager::refreshWidgetEditorToolbar(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->refreshToolbar(tabId);
}

void UIManager::deleteSelectedWidgetEditorElement(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->deleteSelectedElement(tabId);
}

bool UIManager::tryDeleteWidgetEditorElement()
{
 return m_widgetEditorTab ? m_widgetEditorTab->tryDeleteSelectedElement() : false;
}

bool UIManager::getWidgetEditorCanvasRect(Vec4& outRect) const
{
 return m_widgetEditorTab ? m_widgetEditorTab->getCanvasRect(outRect) : false;
}

bool UIManager::isWidgetEditorContentWidget(const std::string& widgetId) const
{
 return m_widgetEditorTab ? m_widgetEditorTab->isContentWidget(widgetId) : false;
}

bool UIManager::getWidgetEditorPreviewInfo(WidgetEditorPreviewInfo& out) const
{
 if (!m_widgetEditorTab) return false;
 WidgetEditorTab::PreviewInfo info;
 if (!m_widgetEditorTab->getPreviewInfo(info)) return false;
 out.editedWidget = info.editedWidget;
 out.selectedElementId = info.selectedElementId;
 out.hoveredElementId = info.hoveredElementId;
 out.zoom = info.zoom;
 out.panOffset = info.panOffset;
 out.dirty = info.dirty;
 out.tabId = info.tabId;
 return true;
}

void UIManager::clearWidgetEditorPreviewDirty()
{
 if (m_widgetEditorTab) m_widgetEditorTab->clearPreviewDirty();
}

bool UIManager::selectWidgetEditorElementAtPos(const Vec2& screenPos)
{
 return m_widgetEditorTab ? m_widgetEditorTab->selectElementAtPos(screenPos) : false;
}

void UIManager::updateWidgetEditorHover(const Vec2& screenPos)
{
 if (m_widgetEditorTab) m_widgetEditorTab->updateHover(screenPos);
}
#endif // ENGINE_EDITOR


// ---------------------------------------------------------------------------
// Widget Editor: right-mouse-down starts panning on the canvas
// ---------------------------------------------------------------------------
bool UIManager::handleRightMouseDown(const Vec2& screenPos)
{
#if ENGINE_EDITOR
 if (m_widgetEditorTab)
 {
 auto* state = m_widgetEditorTab->getActiveState();
 if (state && m_widgetEditorTab->isOverCanvas(screenPos))
 {
 state->isPanning = true;
 state->panStartMouse = screenPos;
 state->panStartOffset = state->panOffset;
 return true;
 }
 }
#else
 (void)screenPos;
#endif
 return false;
}

// ---------------------------------------------------------------------------
// Widget Editor: right-mouse-up ends panning
// ---------------------------------------------------------------------------
bool UIManager::handleRightMouseUp(const Vec2& screenPos)
{
#if ENGINE_EDITOR
 (void)screenPos;
 if (m_widgetEditorTab)
 {
 auto* state = m_widgetEditorTab->getActiveState();
 if (state && state->isPanning)
 {
 state->isPanning = false;
 return true;
 }
 }
#else
 (void)screenPos;
#endif
 return false;
}

// ---------------------------------------------------------------------------
// General mouse motion - handles slider drag and other continuous interactions
// ---------------------------------------------------------------------------
void UIManager::handleMouseMotion(const Vec2& screenPos)
{
#if ENGINE_EDITOR
 // Widget Editor: mouse motion updates pan offset
 if (m_widgetEditorTab)
 {
 auto* state = m_widgetEditorTab->getActiveState();
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
void UIManager::addElementToEditedWidget(const std::string& tabId, const std::string& elementType)
{
 if (m_widgetEditorTab) m_widgetEditorTab->addElement(tabId, elementType);
}

std::string UIManager::resolveHierarchyRowElementId(const std::string& tabId, const std::string& rowId) const
{
 return m_widgetEditorTab ? m_widgetEditorTab->resolveHierarchyRowElementId(tabId, rowId) : std::string{};
}

void UIManager::moveWidgetEditorElement(const std::string& tabId, const std::string& draggedId, const std::string& targetId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->moveElement(tabId, draggedId, targetId);
}

void UIManager::refreshWidgetEditorHierarchy(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->refreshHierarchy(tabId);
}

void UIManager::refreshWidgetEditorDetails(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->refreshDetails(tabId);
}
#endif // ENGINE_EDITOR

#if ENGINE_EDITOR
void UIManager::openLandscapeManagerPopup()
{
    if (!m_renderer)
        return;

    Logger::Instance().log(Logger::Category::Input, "WorldSettings: Tools -> Landscape Manager.", Logger::LogLevel::INFO);

    if (LandscapeManager::hasExistingLandscape())
    {
        showToastMessage("A landscape already exists in the scene.", kToastMedium);
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
                showToastMessage("Landscape created: " + p.name, kToastMedium, NotificationLevel::Success);

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
                showToastMessage("Failed to create landscape.", kToastMedium, NotificationLevel::Error);
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Collect material assets from the registry ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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
        showToastMessage("No material assets found.", kToastMedium);
        m_renderer->closePopupWindow("MaterialEditor");
        return;
    }

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Shared form state ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Helper: load values from the selected asset ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Build UI ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Content area (vertical StackPanel) ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ PBR Section ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Texture Slots ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

    // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Buttons: Save & Close ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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
                showToastMessage("Failed to load material for saving.", kToastMedium);
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

            // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ Backend selector ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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

// ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
// Editor Settings Popup (font size, theme tuning)
// ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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

        // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Section: UI Scale ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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
                if (idx == 0) // Auto ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â detect from primary monitor
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


        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ Section: Font Sizes ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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

        // ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ Section: Spacing ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚ÂÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬
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

        // ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ Section: Keyboard Shortcuts ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã‚ÂÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬
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

                // Keybind button ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã…â€œ click to start capture, next key press rebinds
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

                        // Check for Escape ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â cancel capture
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

    showToastMessage(label + "...", kToastShort, NotificationLevel::Info);

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

// Project Selection Screen
void UIManager::openProjectScreen(std::function<void(const std::string& projectPath, bool isNew, bool setAsDefault, bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI)> onProjectChosen)
{
    m_editorDialogs->openProjectScreen(std::move(onProjectChosen));
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
 if (m_widgetEditorTab) m_widgetEditorTab->refreshTimeline(tabId);
}

void UIManager::buildTimelineTrackRows(const std::string& tabId, WidgetElement& container)
{
 if (m_widgetEditorTab) m_widgetEditorTab->buildTimelineTrackRows(tabId, container);
}

void UIManager::buildTimelineRulerAndKeyframes(const std::string& tabId, WidgetElement& container)
{
 if (m_widgetEditorTab) m_widgetEditorTab->buildTimelineRulerAndKeyframes(tabId, container);
}

void UIManager::handleTimelineMouseDown(const std::string& tabId, const Vec2& localPos, float trackAreaWidth)
{
 if (m_widgetEditorTab) m_widgetEditorTab->handleTimelineMouseDown(tabId, localPos, trackAreaWidth);
}

void UIManager::handleTimelineMouseMove(const std::string& tabId, const Vec2& localPos, float trackAreaWidth)
{
 if (m_widgetEditorTab) m_widgetEditorTab->handleTimelineMouseMove(tabId, localPos, trackAreaWidth);
}

void UIManager::handleTimelineMouseUp(const std::string& tabId)
{
 if (m_widgetEditorTab) m_widgetEditorTab->handleTimelineMouseUp(tabId);
}

// ===========================================================================
// UI Designer Tab  (delegated to EditorTabs/UIDesignerTab)
// ===========================================================================

ViewportUIManager* UIManager::getViewportUIManager() const
{
    return m_renderer ? m_renderer->getViewportUIManagerPtr() : nullptr;
}

bool UIManager::isUIDesignerOpen() const
{
    return m_uiDesignerTab && m_uiDesignerTab->isOpen();
}

void UIManager::openUIDesignerTab()
{
    if (!m_uiDesignerTab)
        m_uiDesignerTab = std::make_unique<UIDesignerTab>(this, m_renderer);
    m_uiDesignerTab->open();
}

void UIManager::closeUIDesignerTab()
{
    if (m_uiDesignerTab)
        m_uiDesignerTab->close();
}

void UIManager::selectUIDesignerElement(const std::string& widgetName, const std::string& elementId)
{
    if (m_uiDesignerTab)
        m_uiDesignerTab->selectElement(widgetName, elementId);
}

void UIManager::addElementToViewportWidget(const std::string& elementType)
{
    if (m_uiDesignerTab)
        m_uiDesignerTab->addElementToViewportWidget(elementType);
}

void UIManager::deleteSelectedUIDesignerElement()
{
    if (m_uiDesignerTab)
        m_uiDesignerTab->deleteSelectedElement();
}

void UIManager::refreshUIDesignerHierarchy()
{
    if (m_uiDesignerTab)
        m_uiDesignerTab->refreshHierarchy();
}

void UIManager::refreshUIDesignerDetails()
{
    if (m_uiDesignerTab)
        m_uiDesignerTab->refreshDetails();
}

// ===========================================================================
//  Console / Log-Viewer Tab  (delegated to EditorTabs/ConsoleTab)
// ===========================================================================

bool UIManager::isConsoleOpen() const
{
    return m_consoleTab && m_consoleTab->isOpen();
}

void UIManager::openConsoleTab()
{
    if (!m_consoleTab)
        m_consoleTab = std::make_unique<ConsoleTab>(this, m_renderer);
    m_consoleTab->open();
}

void UIManager::closeConsoleTab()
{
    if (m_consoleTab)
        m_consoleTab->close();
}

// ===========================================================================
// Profiler / Performance-Monitor Tab  (delegated to ProfilerTab)
// ===========================================================================

bool UIManager::isProfilerOpen() const
{
    return m_profilerTab && m_profilerTab->isOpen();
}

void UIManager::openProfilerTab()
{
    if (!m_profilerTab)
        m_profilerTab = std::make_unique<ProfilerTab>(this, m_renderer);
    m_profilerTab->open();
}

void UIManager::closeProfilerTab()
{
    if (m_profilerTab)
        m_profilerTab->close();
}

// ===========================================================================
// Audio Preview Tab  (delegated to AudioPreviewTab)
// ===========================================================================

bool UIManager::isAudioPreviewOpen() const
{
    return m_audioPreviewTab && m_audioPreviewTab->isOpen();
}

void UIManager::openAudioPreviewTab(const std::string& assetPath)
{
    if (!m_audioPreviewTab)
        m_audioPreviewTab = std::make_unique<AudioPreviewTab>(this, m_renderer);
    m_audioPreviewTab->open(assetPath);
}

void UIManager::closeAudioPreviewTab()
{
    if (m_audioPreviewTab)
        m_audioPreviewTab->close();
}
// ===========================================================================
// Particle Editor Tab  (delegated to EditorTabs/ParticleEditorTab)
// ===========================================================================

bool UIManager::isParticleEditorOpen() const
{
    return m_particleEditorTab && m_particleEditorTab->isOpen();
}

void UIManager::openParticleEditorTab(ECS::Entity entity)
{
    if (!m_particleEditorTab)
        m_particleEditorTab = std::make_unique<ParticleEditorTab>(this, m_renderer);
    m_particleEditorTab->open(entity);
}

void UIManager::closeParticleEditorTab()
{
    if (m_particleEditorTab)
        m_particleEditorTab->close();
}

// ===========================================================================
// Shader Viewer Tab  (delegated to EditorTabs/ShaderViewerTab)
// ===========================================================================

bool UIManager::isShaderViewerOpen() const
{
    return m_shaderViewerTab && m_shaderViewerTab->isOpen();
}

void UIManager::openShaderViewerTab()
{
    if (!m_shaderViewerTab)
        m_shaderViewerTab = std::make_unique<ShaderViewerTab>(this, m_renderer);
    m_shaderViewerTab->open();
}

void UIManager::closeShaderViewerTab()
{
    if (m_shaderViewerTab)
        m_shaderViewerTab->close();
}

// ===========================================================================
// Render-Pass-Debugger Tab  (delegated to EditorTabs/RenderDebuggerTab)
// ===========================================================================

bool UIManager::isRenderDebuggerOpen() const
{
    return m_renderDebuggerTab && m_renderDebuggerTab->isOpen();
}

void UIManager::openRenderDebuggerTab()
{
    if (!m_renderDebuggerTab)
        m_renderDebuggerTab = std::make_unique<RenderDebuggerTab>(this, m_renderer);
    m_renderDebuggerTab->open();
}

void UIManager::closeRenderDebuggerTab()
{
    if (m_renderDebuggerTab)
        m_renderDebuggerTab->close();
}


// ===========================================================================
// Cinematic Sequencer Tab (extracted to EditorTabs/SequencerTab.h)
// ===========================================================================

bool UIManager::isSequencerOpen() const
{
    return m_sequencerTab && m_sequencerTab->isOpen();
}

void UIManager::openSequencerTab()
{
    if (!m_sequencerTab)
        m_sequencerTab = std::make_unique<SequencerTab>(this, m_renderer);
    m_sequencerTab->open();
}

void UIManager::closeSequencerTab()
{
    if (m_sequencerTab)
        m_sequencerTab->close();
}
// =============================================================================
// =============================================================================
// Level Composition Panel (extracted to EditorTabs/LevelCompositionTab.h)
// =============================================================================

bool UIManager::isLevelCompositionOpen() const
{
    return m_levelCompositionTab && m_levelCompositionTab->isOpen();
}

void UIManager::openLevelCompositionTab()
{
    if (!m_levelCompositionTab)
        m_levelCompositionTab = std::make_unique<LevelCompositionTab>(this, m_renderer);
    m_levelCompositionTab->open();
}

void UIManager::closeLevelCompositionTab()
{
    if (m_levelCompositionTab)
        m_levelCompositionTab->close();
}

// ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â
// Build Game dialog (Phase 10)
// ÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚ÂÃƒÂ¢Ã¢â‚¬Â¢Ã‚Â

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


// == Editor Dialogs (delegated to EditorTabs/EditorDialogs) ==================

EditorDialogs& UIManager::getEditorDialogs()
{
    return *m_editorDialogs;
}

// == Build System UI (delegated to EditorTabs/BuildSystemUI) =================

BuildSystemUI& UIManager::getBuildSystemUI()
{
    return *m_buildSystemUI;
}

void UIManager::loadBuildProfiles()               { m_buildSystemUI->loadBuildProfiles(); }
void UIManager::saveBuildProfile(const BuildProfile& p) { m_buildSystemUI->saveBuildProfile(p); }
void UIManager::deleteBuildProfile(const std::string& name) { m_buildSystemUI->deleteBuildProfile(name); }
const std::vector<UIManager::BuildProfile>& UIManager::getBuildProfiles() const { return m_buildSystemUI->getBuildProfiles(); }

void UIManager::openBuildGameDialog()              { m_buildSystemUI->openBuildGameDialog(); }
void UIManager::setOnBuildGame(BuildGameCallback cb) { m_buildSystemUI->setOnBuildGame(std::move(cb)); }

void UIManager::showBuildProgress()                { m_buildSystemUI->showBuildProgress(); }
void UIManager::updateBuildProgress(const std::string& status, int step, int totalSteps) { m_buildSystemUI->updateBuildProgress(status, step, totalSteps); }
void UIManager::closeBuildProgress(bool success, const std::string& message) { m_buildSystemUI->closeBuildProgress(success, message); }
void UIManager::dismissBuildProgress()             { m_buildSystemUI->dismissBuildProgress(); }

void UIManager::appendBuildOutput(const std::string& line) { m_buildSystemUI->appendBuildOutput(line); }
void UIManager::pollBuildThread()                  { m_buildSystemUI->pollBuildThread(); }
bool UIManager::isBuildRunning() const             { return m_buildSystemUI->isBuildRunning(); }

bool UIManager::detectCMake()                      { return m_buildSystemUI->detectCMake(); }
bool UIManager::isCMakeAvailable() const           { return m_buildSystemUI->isCMakeAvailable(); }
const std::string& UIManager::getCMakePath() const { return m_buildSystemUI->getCMakePath(); }
void UIManager::showCMakeInstallPrompt()           { m_buildSystemUI->showCMakeInstallPrompt(); }

bool UIManager::detectBuildToolchain()             { return m_buildSystemUI->detectBuildToolchain(); }
bool UIManager::isBuildToolchainAvailable() const  { return m_buildSystemUI->isBuildToolchainAvailable(); }
const UIManager::ToolchainInfo& UIManager::getBuildToolchain() const { return m_buildSystemUI->getBuildToolchain(); }
void UIManager::showToolchainInstallPrompt()       { m_buildSystemUI->showToolchainInstallPrompt(); }

void UIManager::startAsyncToolchainDetection()     { m_buildSystemUI->startAsyncToolchainDetection(); }
void UIManager::pollToolchainDetection()           { m_buildSystemUI->pollToolchainDetection(); }

// ===========================================================================
// ===========================================================================
// Animation Editor Tab (extracted to EditorTabs/AnimationEditorTab.h)
// ===========================================================================
bool UIManager::isAnimationEditorOpen() const
{
    return m_animationEditorTab && m_animationEditorTab->isOpen();
}

void UIManager::openAnimationEditorTab(ECS::Entity entity)
{
    if (!m_animationEditorTab)
        m_animationEditorTab = std::make_unique<AnimationEditorTab>(this, m_renderer);
    m_animationEditorTab->open(entity);
}

void UIManager::closeAnimationEditorTab()
{
    if (m_animationEditorTab)
        m_animationEditorTab->close();
}

#endif // ENGINE_EDITOR
