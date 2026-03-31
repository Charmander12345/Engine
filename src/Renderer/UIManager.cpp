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
#include "EditorTabs/EntityEditorTab.h"
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

    // Lightweight accumulators for child sizes – replaces std::vector<Vec2>
    // to avoid a heap allocation on every recursive call.
    struct ChildSizeStats
    {
        size_t count{ 0 };
        float  widthSum{ 0.f };
        float  heightSum{ 0.f };
        float  maxW{ 0.f };
        float  maxH{ 0.f };
        float  firstW{ 0.f };
        float  firstH{ 0.f };

        void add(const Vec2& s)
        {
            if (count == 0) { firstW = s.x; firstH = s.y; }
            widthSum += s.x; heightSum += s.y;
            maxW = std::max(maxW, s.x);
            maxH = std::max(maxH, s.y);
            ++count;
        }
    };

    Vec2 measureElementSize(WidgetElement& element, const std::function<Vec2(const std::string&, float)>& measureText)
    {
        Vec2 size{};
        ChildSizeStats childStats;
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
                for (auto& child : element.children)
                {
                    Vec2 cs = measureElementSize(child, measureText);
                    cs.x = std::max(cs.x, child.minSize.x);
                    cs.y = std::max(cs.y, child.minSize.y);
                    childStats.add({ cs.x + child.margin.x * 2.0f,
                                     cs.y + child.margin.y * 2.0f });
                }

                float widthSum = childStats.widthSum;
                float maxHeight = childStats.maxH;
                if (childStats.count > 0)
                {
                    widthSum += element.padding.x * 2.0f + element.padding.x * static_cast<float>(childStats.count - 1);
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
            for (auto& child : element.children)
            {
                Vec2 cs = measureElementSize(child, measureText);
                cs.x = std::max(cs.x, child.minSize.x);
                cs.y = std::max(cs.y, child.minSize.y);
                childStats.add({ cs.x + child.margin.x * 2.0f,
                                 cs.y + child.margin.y * 2.0f });
            }
            float heightSum = childStats.heightSum;
            float maxWidth = childStats.maxW;
            if (childStats.count > 0)
            {
                heightSum += element.padding.y * 2.0f + element.padding.y * static_cast<float>(childStats.count - 1);
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
            for (auto& child : element.children)
            {
                Vec2 cs = measureElementSize(child, measureText);
                cs.x = std::max(cs.x, child.minSize.x);
                cs.y = std::max(cs.y, child.minSize.y);
                childStats.add({ cs.x + child.margin.x * 2.0f,
                                 cs.y + child.margin.y * 2.0f });
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
                float widthSum = childStats.widthSum;
                float maxHeight = childStats.maxH;
                if (childStats.count > 0)
                {
                    widthSum += element.padding.x * 2.0f + element.padding.x * static_cast<float>(childStats.count - 1);
                    maxHeight += element.padding.y * 2.0f;
                }
                size = Vec2{ widthSum, maxHeight };
            }
            else
            {
                float heightSum = childStats.heightSum;
                float maxWidth = childStats.maxW;
                if (childStats.count > 0)
                {
                    heightSum += element.padding.y * 2.0f + element.padding.y * static_cast<float>(childStats.count - 1);
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
            computeGridDimensions(childStats.count, columns, rows);

            float maxWidth = childStats.maxW;
            float maxHeight = childStats.maxH;

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
            float spacing = element.spacing;
            float widthSum = childStats.widthSum;
            float maxHeight = childStats.maxH;
            if (childStats.count > 0)
            {
                widthSum += spacing * static_cast<float>(childStats.count - 1);
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
            const int childCount = static_cast<int>(childStats.count);
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
            float maxChildW = childStats.maxW;
            float maxChildH = childStats.maxH;
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
            float childW = childStats.firstW;
            float childH = childStats.firstH;
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
            if (childStats.count > 0)
            {
                size = Vec2{ childStats.firstW + element.padding.x * 2.0f,
                             childStats.firstH + element.padding.y * 2.0f };
            }
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // WidgetSwitcher: size of the largest child (only one visible at a time)
        if (element.type == WidgetElementType::WidgetSwitcher)
        {
            size = Vec2{ childStats.maxW + element.padding.x * 2.0f, childStats.maxH + element.padding.y * 2.0f };
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // Overlay: size of the largest child (all stacked)
        if (element.type == WidgetElementType::Overlay)
        {
            size = Vec2{ childStats.maxW + element.padding.x * 2.0f, childStats.maxH + element.padding.y * 2.0f };
            element.contentSizePixels = size;
            element.hasContentSize = true;
            return size;
        }

        // Border: single child with border insets
        if (element.type == WidgetElementType::Border)
        {
            float childW = childStats.firstW;
            float childH = childStats.firstH;
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
            size.x = std::max(childStats.maxW, element.minSize.x) + element.padding.x * 2.0f;
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


const std::vector<UIManager::WidgetEntry>& UIManager::getRegisteredWidgets() const
{
    return m_widgets;
}

std::vector<UIManager::WidgetEntry>& UIManager::getRegisteredWidgetsMutable()
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

    auto& orderedEntries = m_layoutOrderedScratch;  orderedEntries.clear();
    auto& topEntries     = m_layoutTopScratch;      topEntries.clear();
    auto& bottomEntries  = m_layoutBottomScratch;   bottomEntries.clear();
    auto& leftEntries    = m_layoutLeftScratch;      leftEntries.clear();
    auto& rightEntries   = m_layoutRightScratch;    rightEntries.clear();
    auto& otherEntries   = m_layoutOtherScratch;    otherEntries.clear();

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
    const std::string& eventId = element.clickEvent.empty() ? element.id : element.clickEvent;
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


