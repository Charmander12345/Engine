#include "UIWidget.h"

namespace
{
    std::string toString(WidgetElementType type)
    {
        switch (type)
        {
        case WidgetElementType::Text: return "Text";
        case WidgetElementType::Button: return "Button";
        case WidgetElementType::Panel: return "Panel";
        case WidgetElementType::StackPanel: return "StackPanel";
        default: return "Unknown";
        }
    }

    WidgetElementType fromString(const std::string& value)
    {
        if (value == "Text") return WidgetElementType::Text;
        if (value == "Button") return WidgetElementType::Button;
        if (value == "Panel") return WidgetElementType::Panel;
        if (value == "StackPanel") return WidgetElementType::StackPanel;
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
        if (entry.contains("color"))
        {
            element.color = readVec4(entry.at("color"));
        }
        if (entry.contains("hoverColor"))
        {
            element.hoverColor = readVec4(entry.at("hoverColor"));
        }
        else
        {
            element.hoverColor = (element.type == WidgetElementType::Button) ? brightenColor(element.color) : element.color;
        }
        if (entry.contains("textColor"))
        {
            element.textColor = readVec4(entry.at("textColor"));
        }
        if (entry.contains("text"))
        {
            element.text = entry.at("text").get<std::string>();
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
        if (entry.contains("textAlignH"))
        {
            element.textAlignH = alignHFromString(entry.at("textAlignH").get<std::string>());
        }
        if (entry.contains("textAlignV"))
        {
            element.textAlignV = alignVFromString(entry.at("textAlignV").get<std::string>());
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
            element.isHitTestable = (element.type == WidgetElementType::Button);
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
        if (entry.contains("clickEvent"))
        {
            element.clickEvent = entry.at("clickEvent").get<std::string>();
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
        if (element.type == WidgetElementType::StackPanel)
        {
            entry["orientation"] = toString(element.orientation);
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
