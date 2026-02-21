#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>

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
    StackPanel,
    Grid,
    ColorPicker,
    EntryBar,
    ProgressBar,
    Slider,
    Image,
    CheckBox,
    DropDown,
    TreeView,
    TabView
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

enum class WidgetAnchor
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

struct WidgetElement
{
    WidgetElementType type{ WidgetElementType::Unknown };
    std::string id;
    Vec2 from{};
    Vec2 to{ 1.0f, 1.0f };
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec4 hoverColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec4 fillColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    std::string text;
    std::string font;
    float fontSize{ 0.0f };
    Vec2 minSize{ 0.0f, 0.0f };
    std::string value;
    float valueFloat{ 0.0f };
    float minValue{ 0.0f };
    float maxValue{ 1.0f };
    bool isPassword{ false };
    bool isCompact{ false };
    TextAlignH textAlignH{ TextAlignH::Left };
    TextAlignV textAlignV{ TextAlignV::Top };
    bool wrapText{ false };
    Vec2 padding{ 0.0f, 0.0f };
    Vec2 margin{ 0.0f, 0.0f };
    bool isHitTestable{ false };
    bool fillX{ false };
    bool fillY{ false };
    bool sizeToContent{ false };
    StackOrientation orientation{ StackOrientation::Vertical };
    std::string imagePath;
    unsigned int textureId{ 0 };
    std::string shaderVertex;
    std::string shaderFragment;
    std::string clickEvent;
    std::vector<WidgetElement> children;
    std::vector<WidgetElement> cachedChildren;
    bool isCollapsed{ false };
    Vec2 computedSizePixels{};
    bool hasComputedSize{ false };
    Vec2 contentSizePixels{};
    bool hasContentSize{ false };
    Vec2 computedPositionPixels{};
    bool hasComputedPosition{ false };
    Vec2 boundsMinPixels{};
    Vec2 boundsMaxPixels{};
    bool hasBounds{ false };
    bool isHovered{ false };
    bool isPressed{ false };
    bool isFocused{ false };
    bool runtimeOnly{ false };
    bool scrollable{ false };
    float scrollOffset{ 0.0f };
    std::function<void()> onClicked;
    std::function<void()> onDoubleClicked;
    std::function<void(const Vec4&)> onColorChanged;
    std::function<void(const std::string&)> onValueChanged;
    std::function<void()> onHovered;
    std::function<void()> onUnhovered;
    bool isChecked{ false };
    std::vector<std::string> items;
    int selectedIndex{ -1 };
    bool isExpanded{ false };
    int activeTab{ 0 };
    std::function<void(bool)> onCheckedChanged;
    std::function<void(int)> onSelectionChanged;
    std::function<void(int)> onTabChanged;

    // Drag & Drop
    bool isDraggable{ false };
    std::string dragPayload;    // e.g. "Texture|MyTexture.asset" or "Model3D|Mesh.asset"
};

class Widget : public EngineObject
{
public:
    Widget() = default;
    ~Widget() override = default;

    void setSizePixels(const Vec2& size);
    const Vec2& getSizePixels() const;
    void setPositionPixels(const Vec2& position);
    const Vec2& getPositionPixels() const;
    void setAnchor(WidgetAnchor anchor);
    WidgetAnchor getAnchor() const;
    void setFillX(bool fill);
    bool getFillX() const;
    void setFillY(bool fill);
    bool getFillY() const;
    const Vec2& getComputedSizePixels() const;
    bool hasComputedSize() const;
    void setComputedSizePixels(const Vec2& size, bool hasComputed);
    const Vec2& getComputedPositionPixels() const;
    bool hasComputedPosition() const;
    void setComputedPositionPixels(const Vec2& position, bool hasComputed);

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
    Vec2 m_positionPixels{};
    WidgetAnchor m_anchor{ WidgetAnchor::TopLeft };
    bool m_fillX{ false };
    bool m_fillY{ false };
    Vec2 m_computedSizePixels{};
    Vec2 m_computedPositionPixels{};
    bool m_hasComputedSize{ false };
    bool m_hasComputedPosition{ false };
    bool m_layoutDirty{ true };
    std::vector<WidgetElement> m_elements;
    int m_zOrder{ 0 };
};
