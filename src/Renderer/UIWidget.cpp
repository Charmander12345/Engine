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
}

void Widget::setSizePixels(const Vec2& size)
{
    m_sizePixels = size;
}

const Vec2& Widget::getSizePixels() const
{
    return m_sizePixels;
}

void Widget::setElements(std::vector<WidgetElement> elements)
{
    m_elements = std::move(elements);
}

const std::vector<WidgetElement>& Widget::getElements() const
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
            if (entry.contains("text"))
            {
                element.text = entry.at("text").get<std::string>();
            }
            if (entry.contains("font"))
            {
                element.font = entry.at("font").get<std::string>();
            }
            m_elements.push_back(element);
        }
    }

    return true;
}

json Widget::toJson() const
{
    json data = json::object();
    data["m_sizePixels"] = writeVec2(m_sizePixels);
    data["m_zOrder"] = m_zOrder;

    json elements = json::array();
    for (const auto& element : m_elements)
    {
        json entry = json::object();
        entry["type"] = toString(element.type);
        entry["from"] = writeVec2(element.from);
        entry["to"] = writeVec2(element.to);
        entry["color"] = writeVec4(element.color);
        if (!element.text.empty())
        {
            entry["text"] = element.text;
        }
        if (!element.font.empty())
        {
            entry["font"] = element.font;
        }
        elements.push_back(entry);
    }

    data["m_elements"] = elements;
    return data;
}
