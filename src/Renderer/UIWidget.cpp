#include "UIWidget.h"

namespace
{
    std::string toString(WidgetElementType type)
    {
        switch (type)
        {
        case WidgetElementType::Text: return "Text";
        case WidgetElementType::Label: return "Label";
        case WidgetElementType::Button: return "Button";
        case WidgetElementType::Panel: return "Panel";
        case WidgetElementType::StackPanel: return "StackPanel";
        case WidgetElementType::Grid: return "Grid";
        case WidgetElementType::ColorPicker: return "ColorPicker";
        case WidgetElementType::EntryBar: return "EntryBar";
        case WidgetElementType::ProgressBar: return "ProgressBar";
        case WidgetElementType::Slider: return "Slider";
        case WidgetElementType::Image: return "Image";
        case WidgetElementType::CheckBox: return "CheckBox";
        case WidgetElementType::DropDown: return "DropDown";
        case WidgetElementType::DropdownButton: return "DropdownButton";
        case WidgetElementType::TreeView: return "TreeView";
        case WidgetElementType::TabView: return "TabView";
        case WidgetElementType::Separator: return "Separator";
        case WidgetElementType::ScrollView: return "ScrollView";
        case WidgetElementType::ToggleButton: return "ToggleButton";
        case WidgetElementType::RadioButton: return "RadioButton";
        default: return "Unknown";
        }
    }

    WidgetElementType fromString(const std::string& value)
    {
        if (value == "Text") return WidgetElementType::Text;
        if (value == "Label") return WidgetElementType::Label;
        if (value == "Button") return WidgetElementType::Button;
        if (value == "Panel") return WidgetElementType::Panel;
        if (value == "StackPanel") return WidgetElementType::StackPanel;
        if (value == "Grid") return WidgetElementType::Grid;
        if (value == "ColorPicker") return WidgetElementType::ColorPicker;
        if (value == "EntryBar") return WidgetElementType::EntryBar;
        if (value == "ProgressBar") return WidgetElementType::ProgressBar;
        if (value == "Slider") return WidgetElementType::Slider;
        if (value == "Image") return WidgetElementType::Image;
        if (value == "CheckBox") return WidgetElementType::CheckBox;
        if (value == "DropDown") return WidgetElementType::DropDown;
        if (value == "DropdownButton") return WidgetElementType::DropdownButton;
        if (value == "TreeView") return WidgetElementType::TreeView;
        if (value == "TabView") return WidgetElementType::TabView;
        if (value == "Separator") return WidgetElementType::Separator;
        if (value == "ScrollView") return WidgetElementType::ScrollView;
        if (value == "ToggleButton") return WidgetElementType::ToggleButton;
        if (value == "RadioButton") return WidgetElementType::RadioButton;
        return WidgetElementType::Unknown;
    }

    std::string toString(TextAlignH align)
    {
        switch (align)
        {
        case TextAlignH::Center: return "Center";
        case TextAlignH::Right: return "Right";
        default: return "Left";
        }
    }

    std::string toString(TextAlignV align)
    {
        switch (align)
        {
        case TextAlignV::Center: return "Center";
        case TextAlignV::Bottom: return "Bottom";
        default: return "Top";
        }
    }

    TextAlignH alignHFromString(const std::string& value)
    {
        if (value == "Center") return TextAlignH::Center;
        if (value == "Right") return TextAlignH::Right;
        return TextAlignH::Left;
    }

    TextAlignV alignVFromString(const std::string& value)
    {
        if (value == "Center") return TextAlignV::Center;
        if (value == "Bottom") return TextAlignV::Bottom;
        return TextAlignV::Top;
    }

    std::string toString(StackOrientation orientation)
    {
        switch (orientation)
        {
        case StackOrientation::Horizontal: return "Horizontal";
        default: return "Vertical";
        }
    }

    StackOrientation orientationFromString(const std::string& value)
    {
        if (value == "Horizontal") return StackOrientation::Horizontal;
        return StackOrientation::Vertical;
    }

    std::string toString(WidgetAnchor anchor)
    {
        switch (anchor)
        {
        case WidgetAnchor::TopRight: return "TopRight";
        case WidgetAnchor::BottomLeft: return "BottomLeft";
        case WidgetAnchor::BottomRight: return "BottomRight";
        default: return "TopLeft";
        }
    }

    WidgetAnchor anchorFromString(const std::string& value)
    {
        if (value == "TopRight") return WidgetAnchor::TopRight;
        if (value == "BottomLeft") return WidgetAnchor::BottomLeft;
        if (value == "BottomRight") return WidgetAnchor::BottomRight;
        return WidgetAnchor::TopLeft;
    }

    Vec2 readVec2(const json& value)
    {
        Vec2 out{};
        if (value.is_object())
        {
            if (value.contains("x")) out.x = value.at("x").get<float>();
            if (value.contains("y")) out.y = value.at("y").get<float>();
        }
        return out;
    }

    json writeVec2(const Vec2& value)
    {
        return json{ {"x", value.x}, {"y", value.y} };
    }

    Vec4 readVec4(const json& value)
    {
        Vec4 out{};
        if (value.is_object())
        {
            if (value.contains("x")) out.x = value.at("x").get<float>();
            if (value.contains("y")) out.y = value.at("y").get<float>();
            if (value.contains("z")) out.z = value.at("z").get<float>();
            if (value.contains("w")) out.w = value.at("w").get<float>();
        }
        return out;
    }

    json writeVec4(const Vec4& value)
    {
        return json{ {"x", value.x}, {"y", value.y}, {"z", value.z}, {"w", value.w} };
    }

    Vec4 brightenColor(const Vec4& color)
    {
        return Vec4{
            std::min(1.0f, color.x + 0.15f),
            std::min(1.0f, color.y + 0.15f),
            std::min(1.0f, color.z + 0.15f),
            color.w
        };
    }

    WidgetElement readElement(const json& entry)
    {
        WidgetElement element{};
        if (entry.contains("id"))
        {
            element.id = entry.at("id").get<std::string>();
        }
        if (entry.contains("type"))
        {
            element.type = fromString(entry.at("type").get<std::string>());
        }
        if (entry.contains("from"))
        {
            element.from = readVec2(entry.at("from"));
        }
        if (entry.contains("to"))
        {
            element.to = readVec2(entry.at("to"));
        }
        const bool hasColor = entry.contains("color");
        if (hasColor)
        {
            element.color = readVec4(entry.at("color"));
        }
        else if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::Grid
            || element.type == WidgetElementType::ScrollView)
        {
            element.color = Vec4{ 0.1f, 0.1f, 0.12f, 0.65f };
        }
        else if (element.type == WidgetElementType::EntryBar)
        {
            element.color = Vec4{ 0.12f, 0.12f, 0.15f, 0.9f };
        }
        else if (element.type == WidgetElementType::ProgressBar || element.type == WidgetElementType::Slider)
        {
            element.color = Vec4{ 0.14f, 0.14f, 0.18f, 0.9f };
        }
        else if (element.type == WidgetElementType::ColorPicker)
        {
            element.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
        if (entry.contains("hoverColor"))
        {
            element.hoverColor = readVec4(entry.at("hoverColor"));
        }
        else
        {
            element.hoverColor = (element.type == WidgetElementType::Button) ? brightenColor(element.color) : element.color;
        }
        if (entry.contains("fillColor"))
        {
            element.fillColor = readVec4(entry.at("fillColor"));
        }
        else
        {
            element.fillColor = (element.type == WidgetElementType::ProgressBar || element.type == WidgetElementType::Slider)
                ? brightenColor(element.color)
                : element.color;
        }
        if (entry.contains("textColor"))
        {
            element.textColor = readVec4(entry.at("textColor"));
        }
        if (entry.contains("text"))
        {
            element.text = entry.at("text").get<std::string>();
        }
        if (entry.contains("value"))
        {
            const auto& valueEntry = entry.at("value");
            if ((element.type == WidgetElementType::ProgressBar || element.type == WidgetElementType::Slider) && valueEntry.is_number())
            {
                element.valueFloat = valueEntry.get<float>();
            }
            else if (valueEntry.is_string())
            {
                element.value = valueEntry.get<std::string>();
            }
        }
        if (entry.contains("minValue"))
        {
            element.minValue = entry.at("minValue").get<float>();
        }
        if (entry.contains("maxValue"))
        {
            element.maxValue = entry.at("maxValue").get<float>();
        }
        if (entry.contains("font"))
        {
            element.font = entry.at("font").get<std::string>();
        }
        if (entry.contains("fontSize"))
        {
            element.fontSize = entry.at("fontSize").get<float>();
        }
        if (entry.contains("minSize"))
        {
            element.minSize = readVec2(entry.at("minSize"));
        }
        if (entry.contains("isPassword"))
        {
            element.isPassword = entry.at("isPassword").get<bool>();
        }
        if (entry.contains("compact"))
        {
            element.isCompact = entry.at("compact").get<bool>();
        }
        if (entry.contains("textAlignH"))
        {
            element.textAlignH = alignHFromString(entry.at("textAlignH").get<std::string>());
        }
        if (entry.contains("textAlignV"))
        {
            element.textAlignV = alignVFromString(entry.at("textAlignV").get<std::string>());
        }
        if (entry.contains("wrapText"))
        {
            element.wrapText = entry.at("wrapText").get<bool>();
        }
        if (entry.contains("padding"))
        {
            element.padding = readVec2(entry.at("padding"));
        }
        if (entry.contains("margin"))
        {
            element.margin = readVec2(entry.at("margin"));
        }
        if (entry.contains("isHitTestable"))
        {
            element.isHitTestable = entry.at("isHitTestable").get<bool>();
        }
        else
        {
            element.isHitTestable = (element.type == WidgetElementType::Button ||
                element.type == WidgetElementType::ToggleButton ||
                element.type == WidgetElementType::RadioButton ||
                element.type == WidgetElementType::ColorPicker ||
                element.type == WidgetElementType::EntryBar ||
                element.type == WidgetElementType::Slider ||
                element.type == WidgetElementType::CheckBox ||
                element.type == WidgetElementType::DropDown ||
                element.type == WidgetElementType::DropdownButton);
        }
        if (entry.contains("fillX"))
        {
            element.fillX = entry.at("fillX").get<bool>();
        }
        if (entry.contains("fillY"))
        {
            element.fillY = entry.at("fillY").get<bool>();
        }
        if (entry.contains("sizeToContent"))
        {
            element.sizeToContent = entry.at("sizeToContent").get<bool>();
        }
        if (entry.contains("scrollable"))
        {
            element.scrollable = entry.at("scrollable").get<bool>();
        }
        if (entry.contains("orientation"))
        {
            element.orientation = orientationFromString(entry.at("orientation").get<std::string>());
        }
        if (entry.contains("shaderVertex"))
        {
            element.shaderVertex = entry.at("shaderVertex").get<std::string>();
        }
        if (entry.contains("shaderFragment"))
        {
            element.shaderFragment = entry.at("shaderFragment").get<std::string>();
        }
        if (element.shaderFragment.empty())
        {
            if (element.type == WidgetElementType::ProgressBar)
            {
                element.shaderFragment = "progress_fragment.glsl";
            }
            else if (element.type == WidgetElementType::Slider)
            {
                element.shaderFragment = "slider_fragment.glsl";
            }
        }
        if (entry.contains("clickEvent"))
        {
            element.clickEvent = entry.at("clickEvent").get<std::string>();
        }
        if (entry.contains("isChecked"))
        {
            element.isChecked = entry.at("isChecked").get<bool>();
        }
        if (entry.contains("items") && entry.at("items").is_array())
        {
            for (const auto& item : entry.at("items"))
            {
                if (item.is_string())
                {
                    element.items.push_back(item.get<std::string>());
                }
            }
        }
        if (entry.contains("selectedIndex"))
        {
            element.selectedIndex = entry.at("selectedIndex").get<int>();
        }
        if (entry.contains("activeTab"))
        {
            element.activeTab = entry.at("activeTab").get<int>();
        }
        if (entry.contains("imagePath"))
        {
            element.imagePath = entry.at("imagePath").get<std::string>();
        }
        if (entry.contains("children") && entry.at("children").is_array())
        {
            for (const auto& child : entry.at("children"))
            {
                if (child.is_object())
                {
                    element.children.push_back(readElement(child));
                }
            }
        }
        if (entry.contains("borderColor"))
        {
            element.borderColor = readVec4(entry.at("borderColor"));
        }
        if (entry.contains("borderThickness"))
        {
            element.borderThickness = entry.at("borderThickness").get<float>();
        }
        if (entry.contains("borderRadius"))
        {
            element.borderRadius = entry.at("borderRadius").get<float>();
        }
        if (entry.contains("opacity"))
        {
            element.opacity = entry.at("opacity").get<float>();
        }
        if (entry.contains("isVisible"))
        {
            element.isVisible = entry.at("isVisible").get<bool>();
        }
        if (entry.contains("tooltipText"))
        {
            element.tooltipText = entry.at("tooltipText").get<std::string>();
        }
        if (entry.contains("isBold"))
        {
            element.isBold = entry.at("isBold").get<bool>();
        }
        if (entry.contains("isItalic"))
        {
            element.isItalic = entry.at("isItalic").get<bool>();
        }
        if (entry.contains("gradientColor"))
        {
            element.gradientColor = readVec4(entry.at("gradientColor"));
        }
        if (entry.contains("maxSize"))
        {
            element.maxSize = readVec2(entry.at("maxSize"));
        }
        if (entry.contains("spacing"))
        {
            element.spacing = entry.at("spacing").get<float>();
        }
        if (entry.contains("radioGroup"))
        {
            element.radioGroup = entry.at("radioGroup").get<std::string>();
        }
        return element;
    }

    json writeElement(const WidgetElement& element)
    {
        if (element.runtimeOnly)
        {
            return json();
        }
        json entry = json::object();
        entry["type"] = toString(element.type);
        if (!element.id.empty())
        {
            entry["id"] = element.id;
        }
        entry["from"] = writeVec2(element.from);
        entry["to"] = writeVec2(element.to);
        entry["color"] = writeVec4(element.color);
        if (element.fillColor.x != element.color.x || element.fillColor.y != element.color.y ||
            element.fillColor.z != element.color.z || element.fillColor.w != element.color.w)
        {
            entry["fillColor"] = writeVec4(element.fillColor);
        }
        entry["textColor"] = writeVec4(element.textColor);
        if (element.hoverColor.x != element.color.x || element.hoverColor.y != element.color.y ||
            element.hoverColor.z != element.color.z || element.hoverColor.w != element.color.w)
        {
            entry["hoverColor"] = writeVec4(element.hoverColor);
        }
        if (!element.text.empty())
        {
            entry["text"] = element.text;
        }
        if (!element.font.empty())
        {
            entry["font"] = element.font;
        }
        if (element.fontSize > 0.0f)
        {
            entry["fontSize"] = element.fontSize;
        }
        if (element.minSize.x > 0.0f || element.minSize.y > 0.0f)
        {
            entry["minSize"] = writeVec2(element.minSize);
        }
        entry["textAlignH"] = toString(element.textAlignH);
        entry["textAlignV"] = toString(element.textAlignV);
        if (element.wrapText)
        {
            entry["wrapText"] = element.wrapText;
        }
        if (element.padding.x > 0.0f || element.padding.y > 0.0f)
        {
            entry["padding"] = writeVec2(element.padding);
        }
        if (element.margin.x > 0.0f || element.margin.y > 0.0f)
        {
            entry["margin"] = writeVec2(element.margin);
        }
        if (element.isHitTestable)
        {
            entry["isHitTestable"] = element.isHitTestable;
        }
        if (element.fillX)
        {
            entry["fillX"] = element.fillX;
        }
        if (element.fillY)
        {
            entry["fillY"] = element.fillY;
        }
        if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::ScrollView)
        {
            entry["orientation"] = toString(element.orientation);
            entry["sizeToContent"] = element.sizeToContent;
            if (element.scrollable || element.type == WidgetElementType::ScrollView)
            {
                entry["scrollable"] = true;
            }
            if (element.spacing > 0.0f)
            {
                entry["spacing"] = element.spacing;
            }
        }
        else if (element.type == WidgetElementType::ToggleButton || element.type == WidgetElementType::RadioButton)
        {
            entry["isChecked"] = element.isChecked;
            if (!element.radioGroup.empty())
            {
                entry["radioGroup"] = element.radioGroup;
            }
        }
        else if (element.type == WidgetElementType::Grid)
        {
            entry["sizeToContent"] = element.sizeToContent;
            if (element.scrollable)
            {
                entry["scrollable"] = element.scrollable;
            }
        }
        else if (element.type == WidgetElementType::EntryBar)
        {
            if (!element.value.empty())
            {
                entry["value"] = element.value;
            }
            if (element.isPassword)
            {
                entry["isPassword"] = element.isPassword;
            }
        }
        else if (element.type == WidgetElementType::ProgressBar || element.type == WidgetElementType::Slider)
        {
            entry["value"] = element.valueFloat;
            entry["minValue"] = element.minValue;
            entry["maxValue"] = element.maxValue;
        }
        else if (element.type == WidgetElementType::ColorPicker)
        {
            if (element.isCompact)
            {
                entry["compact"] = element.isCompact;
            }
        }
        else if (element.type == WidgetElementType::CheckBox)
        {
            entry["isChecked"] = element.isChecked;
        }
        else if (element.type == WidgetElementType::DropDown)
        {
            if (!element.items.empty())
            {
                entry["items"] = element.items;
            }
            entry["selectedIndex"] = element.selectedIndex;
        }
        else if (element.type == WidgetElementType::DropdownButton)
        {
            if (!element.items.empty())
            {
                entry["items"] = element.items;
            }
        }
        else if (element.type == WidgetElementType::TreeView)
        {
            entry["sizeToContent"] = element.sizeToContent;
            if (element.scrollable)
            {
                entry["scrollable"] = element.scrollable;
            }
        }
        else if (element.type == WidgetElementType::TabView)
        {
            entry["activeTab"] = element.activeTab;
            entry["sizeToContent"] = element.sizeToContent;
        }
        if (!element.shaderVertex.empty())
        {
            entry["shaderVertex"] = element.shaderVertex;
        }
        if (!element.shaderFragment.empty())
        {
            entry["shaderFragment"] = element.shaderFragment;
        }
        if (!element.clickEvent.empty())
        {
            entry["clickEvent"] = element.clickEvent;
        }
        if (!element.imagePath.empty())
        {
            entry["imagePath"] = element.imagePath;
        }
        // Extended styling properties
        if (element.borderThickness > 0.0f)
        {
            entry["borderThickness"] = element.borderThickness;
            entry["borderColor"] = writeVec4(element.borderColor);
        }
        if (element.borderRadius > 0.0f)
        {
            entry["borderRadius"] = element.borderRadius;
        }
        if (element.opacity < 1.0f)
        {
            entry["opacity"] = element.opacity;
        }
        if (!element.isVisible)
        {
            entry["isVisible"] = element.isVisible;
        }
        if (!element.tooltipText.empty())
        {
            entry["tooltipText"] = element.tooltipText;
        }
        if (element.isBold)
        {
            entry["isBold"] = element.isBold;
        }
        if (element.isItalic)
        {
            entry["isItalic"] = element.isItalic;
        }
        if (element.gradientColor.w > 0.0f)
        {
            entry["gradientColor"] = writeVec4(element.gradientColor);
        }
        if (element.maxSize.x > 0.0f || element.maxSize.y > 0.0f)
        {
            entry["maxSize"] = writeVec2(element.maxSize);
        }
        if (!element.children.empty())
        {
            json children = json::array();
            for (const auto& child : element.children)
            {
                if (child.runtimeOnly)
                {
                    continue;
                }
                json childJson = writeElement(child);
                if (!childJson.is_null())
                {
                    children.push_back(std::move(childJson));
                }
            }
            entry["children"] = children;
        }
        return entry;
    }
}

void Widget::setSizePixels(const Vec2& size)
{
    m_sizePixels = size;
    m_layoutDirty = true;
}

const Vec2& Widget::getSizePixels() const
{
    return m_sizePixels;
}

const Vec2& Widget::getComputedSizePixels() const
{
    return m_computedSizePixels;
}

bool Widget::hasComputedSize() const
{
    return m_hasComputedSize;
}

void Widget::setComputedSizePixels(const Vec2& size, bool hasComputed)
{
    m_computedSizePixels = size;
    m_hasComputedSize = hasComputed;
}

const Vec2& Widget::getComputedPositionPixels() const
{
    return m_computedPositionPixels;
}

bool Widget::hasComputedPosition() const
{
    return m_hasComputedPosition;
}

void Widget::setComputedPositionPixels(const Vec2& position, bool hasComputed)
{
    m_computedPositionPixels = position;
    m_hasComputedPosition = hasComputed;
}

void Widget::setElements(std::vector<WidgetElement> elements)
{
    m_elements = std::move(elements);
    m_layoutDirty = true;
}

const std::vector<WidgetElement>& Widget::getElements() const
{
    return m_elements;
}

std::vector<WidgetElement>& Widget::getElementsMutable()
{
    return m_elements;
}

void Widget::setZOrder(int zOrder)
{
    m_zOrder = zOrder;
}

int Widget::getZOrder() const
{
    return m_zOrder;
}

bool Widget::loadFromJson(const json& data)
{
    m_sizePixels = {};
    m_elements.clear();
    m_layoutDirty = true;

    if (data.contains("m_positionPixels"))
    {
        m_positionPixels = readVec2(data.at("m_positionPixels"));
    }
    if (data.contains("m_anchor"))
    {
        m_anchor = anchorFromString(data.at("m_anchor").get<std::string>());
    }
    if (data.contains("m_fillX"))
    {
        m_fillX = data.at("m_fillX").get<bool>();
    }
    if (data.contains("m_fillY"))
    {
        m_fillY = data.at("m_fillY").get<bool>();
    }

    if (!data.is_object())
    {
        return false;
    }

    if (data.contains("m_sizePixels"))
    {
        m_sizePixels = readVec2(data.at("m_sizePixels"));
    }

    if (data.contains("m_zOrder"))
    {
        m_zOrder = data.at("m_zOrder").get<int>();
    }

    if (data.contains("m_elements") && data.at("m_elements").is_array())
    {
        for (const auto& entry : data.at("m_elements"))
        {
            if (!entry.is_object())
            {
                continue;
            }
            m_elements.push_back(readElement(entry));
        }
    }

    return true;
}

json Widget::toJson() const
{
    json data = json::object();
    data["m_sizePixels"] = writeVec2(m_sizePixels);
    data["m_positionPixels"] = writeVec2(m_positionPixels);
    data["m_anchor"] = toString(m_anchor);
    data["m_fillX"] = m_fillX;
    data["m_fillY"] = m_fillY;
    data["m_zOrder"] = m_zOrder;

    json elements = json::array();
    for (const auto& element : m_elements)
    {
        if (element.runtimeOnly)
        {
            continue;
        }
        json elementJson = writeElement(element);
        if (!elementJson.is_null())
        {
            elements.push_back(std::move(elementJson));
        }
    }
    data["m_elements"] = elements;
    return data;
}

void Widget::setPositionPixels(const Vec2& position)
{
    m_positionPixels = position;
    m_layoutDirty = true;
}

const Vec2& Widget::getPositionPixels() const
{
    return m_positionPixels;
}

void Widget::setAnchor(WidgetAnchor anchor)
{
    m_anchor = anchor;
    m_layoutDirty = true;
}

WidgetAnchor Widget::getAnchor() const
{
    return m_anchor;
}

void Widget::setFillX(bool fill)
{
    m_fillX = fill;
    m_layoutDirty = true;
}

bool Widget::getFillX() const
{
    return m_fillX;
}

void Widget::setFillY(bool fill)
{
    m_fillY = fill;
    m_layoutDirty = true;
}

bool Widget::getFillY() const
{
    return m_fillY;
}

void Widget::setAbsolutePosition(bool absolute)
{
    m_absolutePosition = absolute;
    m_layoutDirty = true;
}

bool Widget::isAbsolutePositioned() const
{
    return m_absolutePosition;
}

void Widget::markLayoutDirty()
{
    m_layoutDirty = true;
}

bool Widget::isLayoutDirty() const
{
    return m_layoutDirty;
}

void Widget::setLayoutDirty(bool dirty)
{
    m_layoutDirty = dirty;
}
