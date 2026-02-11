#include "ColorPickerWidget.h"

#include <algorithm>
#include <cmath>

WidgetElement ColorPickerWidget::toElement() const
{
    WidgetElement element{};
    element.type = WidgetElementType::ColorPicker;
    element.color = m_color;
    element.minSize = m_minSize;
    element.isHitTestable = m_hitTestable;
    element.isCompact = m_compact;
    element.onColorChanged = m_onColorChanged;

    if (!m_compact)
    {
        return element;
    }

    const int rValue = static_cast<int>(std::round(std::clamp(m_color.x, 0.0f, 1.0f) * 255.0f));
    const int gValue = static_cast<int>(std::round(std::clamp(m_color.y, 0.0f, 1.0f) * 255.0f));
    const int bValue = static_cast<int>(std::round(std::clamp(m_color.z, 0.0f, 1.0f) * 255.0f));

    WidgetElement stack{};
    stack.type = WidgetElementType::StackPanel;
    stack.orientation = StackOrientation::Horizontal;
    stack.padding = Vec2{ 6.0f, 2.0f };
    stack.fillX = true;
    stack.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };

    EntryBarWidget entryR;
    entryR.setMinSize(Vec2{ 50.0f, 24.0f });
    entryR.setValue(std::to_string(rValue));
    stack.children.push_back(entryR.toElement());

    EntryBarWidget entryG;
    entryG.setMinSize(Vec2{ 50.0f, 24.0f });
    entryG.setValue(std::to_string(gValue));
    stack.children.push_back(entryG.toElement());

    EntryBarWidget entryB;
    entryB.setMinSize(Vec2{ 50.0f, 24.0f });
    entryB.setValue(std::to_string(bValue));
    stack.children.push_back(entryB.toElement());

    element.children.push_back(std::move(stack));
    return element;
}
