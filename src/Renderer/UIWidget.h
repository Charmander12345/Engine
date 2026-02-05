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

enum class TextAlignH
{
    Left,
    Center,
    Right
};

enum class TextAlignV
{
    Top,
    Center,
    Bottom
};

enum class StackOrientation
{
    Horizontal,
    Vertical
};

struct WidgetElement
{
    WidgetElementType type{ WidgetElementType::Unknown };
    Vec2 from{};
    Vec2 to{ 1.0f, 1.0f };
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string text;
    std::string font;
    float fontSize{ 0.0f };
    Vec2 minSize{ 0.0f, 0.0f };
    TextAlignH textAlignH{ TextAlignH::Left };
    TextAlignV textAlignV{ TextAlignV::Top };
    Vec2 padding{ 0.0f, 0.0f };
    Vec2 margin{ 0.0f, 0.0f };
    bool fillX{ false };
    bool fillY{ false };
    bool sizeToContent{ false };
    StackOrientation orientation{ StackOrientation::Vertical };
    std::string shaderVertex;
    std::string shaderFragment;
    std::vector<WidgetElement> children;
    Vec2 computedSizePixels{};
    bool hasComputedSize{ false };
    Vec2 contentSizePixels{};
    bool hasContentSize{ false };
    Vec2 computedPositionPixels{};
    bool hasComputedPosition{ false };
};

class Widget : public EngineObject
{
public:
    Widget() = default;
    ~Widget() override = default;

    void setSizePixels(const Vec2& size);
    const Vec2& getSizePixels() const;
    const Vec2& getComputedSizePixels() const;
    bool hasComputedSize() const;
    void setComputedSizePixels(const Vec2& size, bool hasComputed);

    void markLayoutDirty();
    bool isLayoutDirty() const;
    void setLayoutDirty(bool dirty);

    void setElements(std::vector<WidgetElement> elements);
    const std::vector<WidgetElement>& getElements() const;
    std::vector<WidgetElement>& getElementsMutable();

    void setZOrder(int zOrder);
    int getZOrder() const;

    bool loadFromJson(const json& data);
    json toJson() const;

private:
    Vec2 m_sizePixels{};
    Vec2 m_computedSizePixels{};
    bool m_hasComputedSize{ false };
    bool m_layoutDirty{ true };
    std::vector<WidgetElement> m_elements;
    int m_zOrder{ 0 };
};
