#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../AssetManager/json.hpp"
#include "../Core/EngineObject.h"
#include "../Core/MathTypes.h"

using json = nlohmann::json;

enum class WidgetElementType
{
    Unknown,
    Text,
    Button,
    Panel,
    StackPanel
};

struct WidgetElement
{
    WidgetElementType type{ WidgetElementType::Unknown };
    Vec2 from{};
    Vec2 to{ 1.0f, 1.0f };
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string text;
    std::string font;
};

class Widget : public EngineObject
{
public:
    Widget() = default;
    ~Widget() override = default;

    void setSizePixels(const Vec2& size);
    const Vec2& getSizePixels() const;

    void setElements(std::vector<WidgetElement> elements);
    const std::vector<WidgetElement>& getElements() const;

    void setZOrder(int zOrder);
    int getZOrder() const;

    bool loadFromJson(const json& data);
    json toJson() const;

private:
    Vec2 m_sizePixels{};
    std::vector<WidgetElement> m_elements;
    int m_zOrder{ 0 };
};
