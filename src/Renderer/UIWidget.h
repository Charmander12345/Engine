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
    Label,
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
    DropdownButton,
    TreeView,
    TabView,
    Separator,
    ScrollView,
    ToggleButton,
    RadioButton,
    WrapBox,
    UniformGrid,
    SizeBox,
    ScaleBox,
    WidgetSwitcher,
    Overlay
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
    BottomRight,
    Top,
    Bottom,
    Left,
    Right,
    Center,
    Stretch
};

enum class HitTestMode
{
    Enabled,        // Element participates in hit testing
    DisabledSelf,   // Element itself excluded, children follow their own setting
    DisabledAll     // Element and all children excluded (parent overrides children)
};

enum class ScaleMode
{
    Contain,        // Scale uniformly to fit inside available area
    Cover,          // Scale uniformly to cover entire available area
    Fill,           // Stretch non-uniformly to fill available area
    ScaleDown,      // Like Contain but never scales up (max scale = 1)
    UserSpecified   // Use explicit userScale value
};

// ── Phase 2: Brush-based styling ─────────────────────────────────────────

enum class BrushType
{
    None,
    SolidColor,
    Image,
    NineSlice,
    LinearGradient
};

struct UIBrush
{
    BrushType type{ BrushType::None };
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec4 colorEnd{ 0.0f, 0.0f, 0.0f, 0.0f };       // for LinearGradient
    float gradientAngle{ 0.0f };                      // 0=vertical, 90=horizontal
    std::string imagePath;
    unsigned int textureId{ 0 };
    Vec4 imageMargin{ 0.0f, 0.0f, 0.0f, 0.0f };     // 9-Slice margins (L,T,R,B)
    Vec2 imageTiling{ 1.0f, 1.0f };

    // Convenience: check if brush should render anything
    bool isVisible() const
    {
        if (type == BrushType::None) return false;
        if (type == BrushType::SolidColor) return color.w > 0.0f;
        return true;
    }

    // Convenience: create a solid-color brush
    static UIBrush solid(const Vec4& c) { UIBrush b; b.type = BrushType::SolidColor; b.color = c; return b; }
    static UIBrush none() { UIBrush b; b.type = BrushType::None; return b; }
};

// ── Phase 2: Render Transforms ───────────────────────────────────────────

struct RenderTransform
{
    Vec2 translation{ 0.0f, 0.0f };
    float rotation{ 0.0f };          // degrees
    Vec2 scale{ 1.0f, 1.0f };
    Vec2 shear{ 0.0f, 0.0f };
    Vec2 pivot{ 0.5f, 0.5f };       // normalised (0,0)=top-left, (0.5,0.5)=centre

    bool isIdentity() const
    {
        return translation.x == 0.0f && translation.y == 0.0f
            && rotation == 0.0f
            && scale.x == 1.0f && scale.y == 1.0f
            && shear.x == 0.0f && shear.y == 0.0f;
    }
};

// ── Phase 2: Clipping modes ──────────────────────────────────────────────

enum class ClipMode
{
    None,              // no additional clipping
    ClipToBounds,      // clip children to this element's bounds
    InheritFromParent  // explicitly inherit parent scissor
};

enum class AnimatableProperty
{
    RenderTranslationX,
    RenderTranslationY,
    RenderRotation,
    RenderScaleX,
    RenderScaleY,
    RenderShearX,
    RenderShearY,
    Opacity,
    ColorR,
    ColorG,
    ColorB,
    ColorA,
    PositionX,
    PositionY,
    SizeX,
    SizeY,
    FontSize
};

enum class EasingFunction
{
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInElastic,
    EaseOutElastic,
    EaseInOutElastic,
    EaseInBounce,
    EaseOutBounce,
    EaseInOutBounce,
    EaseInBack,
    EaseOutBack,
    EaseInOutBack
};

struct AnimationKeyframe
{
    float time{ 0.0f };
    Vec4 value{ 0.0f, 0.0f, 0.0f, 0.0f };
    EasingFunction easing{ EasingFunction::Linear };
};

struct AnimationTrack
{
    std::string targetElementId;
    AnimatableProperty property{ AnimatableProperty::Opacity };
    std::vector<AnimationKeyframe> keyframes;
};

struct WidgetAnimation
{
    std::string name;
    float duration{ 0.0f };
    bool isLooping{ false };
    float playbackSpeed{ 1.0f };
    std::vector<AnimationTrack> tracks;
};

float EvaluateEasing(EasingFunction easing, float t);

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
    HitTestMode hitTestMode{ HitTestMode::DisabledSelf };
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

    // DropdownButton items (label + callback pairs, used with showDropdownMenu)
    struct DropdownItem
    {
        std::string label;
        std::function<void()> onClick;
    };
    std::vector<DropdownItem> dropdownItems;

    // Drag & Drop
    bool isDraggable{ false };
    std::string dragPayload;    // e.g. "Texture|MyTexture.asset" or "Model3D|Mesh.asset"

    // ── Extended styling properties ──────────────────────────────────────
    // Border
    Vec4 borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    float borderThickness{ 0.0f };
    float borderRadius{ 0.0f };

    // Visibility / Opacity
    float opacity{ 1.0f };       // 0=transparent, 1=fully opaque
    bool isVisible{ true };      // hide without collapsing layout

    // Tooltip
    std::string tooltipText;

    // Font styling
    bool isBold{ false };
    bool isItalic{ false };

    // Background gradient (secondary color blended from top to bottom)
    Vec4 gradientColor{ 0.0f, 0.0f, 0.0f, 0.0f };

    // Maximum size constraints (0 = unconstrained, analogous to minSize)
    Vec2 maxSize{ 0.0f, 0.0f };

    // Spacing between children in StackPanel / ScrollView
    float spacing{ 0.0f };

    // RadioButton group identifier
    std::string radioGroup;

    // Anchor-based positioning (used by Gameplay UI / Viewport UI)
    WidgetAnchor anchor{ WidgetAnchor::TopLeft };
    Vec2 anchorOffset{ 0.0f, 0.0f };

    // Canvas root flag – the root canvas panel of a widget asset (non-deletable)
    bool isCanvasRoot{ false };

    // ── Layout panel properties (Phase 1) ─────────────────────────────────
    // UniformGrid
    int columns{ 0 };            // 0 = auto-calculate from children count
    int rows{ 0 };               // 0 = auto-calculate from children count

    // SizeBox
    float widthOverride{ 0.0f };     // 0 = no override
    float heightOverride{ 0.0f };    // 0 = no override

    // ScaleBox
    ScaleMode scaleMode{ ScaleMode::Contain };
    float userScale{ 1.0f };         // only used with ScaleMode::UserSpecified

    // WidgetSwitcher
    int activeChildIndex{ 0 };

    // ── Styling & Visual (Phase 2) ────────────────────────────────────────
    // Brush-based styling (coexists with legacy color fields for backwards compat)
    UIBrush background;              // replaces color for new assets
    UIBrush hoverBrush;              // replaces hoverColor for new assets
    UIBrush fillBrush;               // replaces fillColor for new assets

    // Render transform (visual only – does not affect layout)
    RenderTransform renderTransform;

    // Clipping mode
    ClipMode clipMode{ ClipMode::None };

    // Effective (inherited) opacity – computed at render time, not serialised
    float effectiveOpacity{ 1.0f };
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
    void setAbsolutePosition(bool absolute);
    bool isAbsolutePositioned() const;
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

    void setAnimations(std::vector<WidgetAnimation> animations);
    const std::vector<WidgetAnimation>& getAnimations() const;
    std::vector<WidgetAnimation>& getAnimationsMutable();

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
    bool m_absolutePosition{ false };
    Vec2 m_computedSizePixels{};
    Vec2 m_computedPositionPixels{};
    bool m_hasComputedSize{ false };
    bool m_hasComputedPosition{ false };
    bool m_layoutDirty{ true };
    std::vector<WidgetElement> m_elements;
    std::vector<WidgetAnimation> m_animations;
    int m_zOrder{ 0 };
};
