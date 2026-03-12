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
    Overlay,
    Border,
    Spinner,
    RichText,
    ListView,
    TileView
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

// ── Phase 5: Focus & Keyboard Navigation ─────────────────────────────────

struct FocusConfig
{
    bool isFocusable{ false };
    int tabIndex{ -1 };                  // -1 = automatic (document order)
    std::string focusUp;                 // element ID for Up-navigation
    std::string focusDown;               // element ID for Down-navigation
    std::string focusLeft;               // element ID for Left-navigation
    std::string focusRight;              // element ID for Right-navigation
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

class Widget;

class WidgetAnimationPlayer
{
public:
    void attachWidget(Widget* widget);
    void play(const std::string& animName, bool fromStart = true);
    void playReverse(const std::string& animName);
    void pause();
    void stop();
    void setSpeed(float speed);
    float getCurrentTime() const;
    bool isPlaying() const;
    const std::string& getCurrentAnimation() const;
    void tick(float deltaTime);

private:
    Widget* m_widget{ nullptr };
    std::string m_currentAnimation;
    float m_currentTime{ 0.0f };
    float m_speed{ 1.0f };
    bool m_playing{ false };
    bool m_reverse{ false };
};

// ── Phase 4: Rich Text segment ───────────────────────────────────────────

struct RichTextSegment
{
    std::string text;
    bool bold{ false };
    bool italic{ false };
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };   // per-segment override (default = white)
    bool hasColor{ false };                    // true if <color> was specified
    std::string imagePath;                     // non-empty for <img> segments
    float imageW{ 16.0f };
    float imageH{ 16.0f };

    bool isImage() const { return !imagePath.empty(); }
};

// Parse simplified rich-text markup into a flat list of segments.
// Supported tags: <b>...</b>  <i>...</i>  <color=#RRGGBB>...</color>
//                 <img src="path" w=W h=H/>
std::vector<RichTextSegment> ParseRichTextMarkup(const std::string& markup, const Vec4& defaultColor);

// ── Phase 5: Runtime Drag & Drop ─────────────────────────────────────────

struct DragDropOperation
{
    std::string sourceElementId;
    std::string payload;           // Free-form string (e.g. "item:sword")
    Vec2        dragPosition{};    // Current cursor position during drag
};

// ── Consolidated Style Properties ────────────────────────────────────────

struct WidgetElementStyle
{
    // Core state colors
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };            // normal/default background color
    Vec4 hoverColor{ 1.0f, 1.0f, 1.0f, 1.0f };        // background on hover
    Vec4 pressedColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // background on press (alpha 0 = use hoverColor)
    Vec4 disabledColor{ 0.4f, 0.4f, 0.4f, 1.0f };     // background when disabled

    // Text colors
    Vec4 textColor{ 1.0f, 1.0f, 1.0f, 1.0f };         // normal text color
    Vec4 textHoverColor{ 0.0f, 0.0f, 0.0f, 0.0f };    // text on hover (alpha 0 = use textColor)
    Vec4 textPressedColor{ 0.0f, 0.0f, 0.0f, 0.0f };  // text on press (alpha 0 = use textColor)

    // Fill colors (ProgressBar, Slider, etc.)
    Vec4 fillColor{ 1.0f, 1.0f, 1.0f, 1.0f };         // fill area color

    // Outline / Border
    Vec4 outlineColor{ 0.0f, 0.0f, 0.0f, 0.0f };      // general outline/stroke color
    Vec4 borderColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // border color
    float borderThickness{ 0.0f };                      // border thickness
    float borderRadius{ 0.0f };                         // border corner radius

    // Gradient
    Vec4 gradientColor{ 0.0f, 0.0f, 0.0f, 0.0f };     // secondary gradient color (top to bottom)

    // Shadow
    Vec4 shadowColor{ 0.0f, 0.0f, 0.0f, 0.0f };       // drop shadow color (alpha 0 = no shadow)
    Vec2 shadowOffset{ 0.0f, 2.0f };                   // drop shadow offset
    float shadowBlurRadius{ 6.0f };                    // soft shadow blur spread (px)

    /// Apply an elevation level (0–5) using the given base shadow color/offset.
    /// 0 = no shadow, 1 = subtle, 2 = medium (dropdowns), 3 = strong (modals), 4–5 = extra.
    void applyElevation(int level, const Vec4& baseShadowColor, const Vec2& baseShadowOffset)
    {
        if (level <= 0) { shadowColor = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }; return; }
        const float t = static_cast<float>(std::min(level, 5));
        const float alphaScale = 0.4f + t * 0.2f;   // 0.6, 0.8, 1.0, 1.2, 1.4
        const float offsetScale = 0.5f + t * 0.25f;  // 0.75, 1.0, 1.25, 1.5, 1.75
        shadowColor  = Vec4{ baseShadowColor.x, baseShadowColor.y, baseShadowColor.z, baseShadowColor.w * alphaScale };
        shadowOffset = Vec2{ baseShadowOffset.x * offsetScale, baseShadowOffset.y * offsetScale };
        shadowBlurRadius = 2.0f + t * 2.0f;          // 4, 6, 8, 10, 12
    }

    // Opacity / Visibility
    float opacity{ 1.0f };                              // 0=transparent, 1=fully opaque
    bool isVisible{ true };                             // hide without collapsing layout

    // Font styling
    bool isBold{ false };
    bool isItalic{ false };
    float letterSpacing{ 0.0f };                        // extra spacing between characters
    float lineSpacing{ 0.0f };                          // extra spacing between lines
};

struct WidgetElement
{
    WidgetElementType type{ WidgetElementType::Unknown };
    std::string id;
    Vec2 from{};
    Vec2 to{ 1.0f, 1.0f };
    WidgetElementStyle style;
    int elevation{ 0 };              // 0–5: shadow depth level (0 = none)
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
    bool isMultiline{ false };
    int maxLines{ 0 };               // 0 = unlimited
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

    // Tooltip
    std::string tooltipText;

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

    // ── Phase 4: Extended widget properties ────────────────────────────────
    // Border – dedicated container with separate border brush and per-side thickness
    UIBrush borderBrush;                     // brush for the border itself
    float borderThicknessLeft{ 0.0f };
    float borderThicknessTop{ 0.0f };
    float borderThicknessRight{ 0.0f };
    float borderThicknessBottom{ 0.0f };
    Vec2 contentPadding{ 0.0f, 0.0f };       // inner padding for the child

    // Spinner – animated loading indicator
    int spinnerDotCount{ 8 };                // number of dots on the circle
    float spinnerSpeed{ 1.0f };              // rotations per second
    float spinnerElapsed{ 0.0f };            // runtime: accumulated time (not serialised)

    // RichText – inline-formatted text block
    std::string richText;                    // markup source: <b>, <i>, <color=#RRGGBB>, <img src="" w= h=/>

    // ListView / TileView – virtualised list rendering
    int totalItemCount{ 0 };                 // total number of items in the data source
    float itemHeight{ 32.0f };               // fixed height per item (ListView & TileView)
    float itemWidth{ 100.0f };               // fixed width per tile (TileView only)
    int columnsPerRow{ 4 };                  // tiles per row (TileView only)
    std::function<void(int index, WidgetElement& itemTemplate)> onGenerateItem;

    // ── Phase 5: Focus & Keyboard Navigation ──────────────────────────────
    FocusConfig focusConfig;                 // per-element focus/navigation settings
    UIBrush focusBrush;                      // visual outline when element has focus

    // ── Phase 5: Runtime Drag & Drop ──────────────────────────────────────
    bool acceptsDrop{ false };               // element can receive drops
    std::function<bool(const DragDropOperation&)> onDragOver;   // return true to accept
    std::function<void(const DragDropOperation&)> onDrop;       // called on successful drop
    std::function<void()> onDragStart;       // called when drag begins on this element
};

class Widget : public EngineObject
{
public:
    Widget();
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
    WidgetAnimationPlayer& animationPlayer();
    const WidgetAnimationPlayer& animationPlayer() const;
    bool applyAnimationAtTime(const std::string& animationName, float timeSeconds);

    void setZOrder(int zOrder);
    int getZOrder() const;

    const WidgetAnimation* findAnimationByName(const std::string& animationName) const;
    WidgetAnimation* findAnimationByNameMutable(const std::string& animationName);

    bool loadFromJson(const json& data);
    json toJson() const;

private:
    void applyAnimationTrackValue(const AnimationTrack& track, const Vec4& value);

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
    WidgetAnimationPlayer m_animationPlayer;
    int m_zOrder{ 0 };
};
