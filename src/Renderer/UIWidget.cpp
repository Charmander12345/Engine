#include "UIWidget.h"

#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    float EaseOutBounce01(float t)
    {
        constexpr float n1 = 7.5625f;
        constexpr float d1 = 2.75f;

        if (t < 1.0f / d1)
        {
            return n1 * t * t;
        }
        if (t < 2.0f / d1)
        {
            t -= 1.5f / d1;
            return n1 * t * t + 0.75f;
        }
        if (t < 2.5f / d1)
        {
            t -= 2.25f / d1;
            return n1 * t * t + 0.9375f;
        }

        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }

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
        case WidgetElementType::WrapBox: return "WrapBox";
        case WidgetElementType::UniformGrid: return "UniformGrid";
        case WidgetElementType::SizeBox: return "SizeBox";
        case WidgetElementType::ScaleBox: return "ScaleBox";
        case WidgetElementType::WidgetSwitcher: return "WidgetSwitcher";
        case WidgetElementType::Overlay: return "Overlay";
        case WidgetElementType::Border: return "Border";
        case WidgetElementType::Spinner: return "Spinner";
        case WidgetElementType::RichText: return "RichText";
        case WidgetElementType::ListView: return "ListView";
        case WidgetElementType::TileView: return "TileView";
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
        if (value == "WrapBox") return WidgetElementType::WrapBox;
        if (value == "UniformGrid") return WidgetElementType::UniformGrid;
        if (value == "SizeBox") return WidgetElementType::SizeBox;
        if (value == "ScaleBox") return WidgetElementType::ScaleBox;
        if (value == "WidgetSwitcher") return WidgetElementType::WidgetSwitcher;
        if (value == "Overlay") return WidgetElementType::Overlay;
        if (value == "Border") return WidgetElementType::Border;
        if (value == "Spinner") return WidgetElementType::Spinner;
        if (value == "RichText") return WidgetElementType::RichText;
        if (value == "ListView") return WidgetElementType::ListView;
        if (value == "TileView") return WidgetElementType::TileView;
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
        case WidgetAnchor::Top: return "Top";
        case WidgetAnchor::Bottom: return "Bottom";
        case WidgetAnchor::Left: return "Left";
        case WidgetAnchor::Right: return "Right";
        case WidgetAnchor::Center: return "Center";
        case WidgetAnchor::Stretch: return "Stretch";
        default: return "TopLeft";
        }
    }

    WidgetAnchor anchorFromString(const std::string& value)
    {
        if (value == "TopRight") return WidgetAnchor::TopRight;
        if (value == "BottomLeft") return WidgetAnchor::BottomLeft;
        if (value == "BottomRight") return WidgetAnchor::BottomRight;
        if (value == "Top") return WidgetAnchor::Top;
        if (value == "Bottom") return WidgetAnchor::Bottom;
        if (value == "Left") return WidgetAnchor::Left;
        if (value == "Right") return WidgetAnchor::Right;
        if (value == "Center") return WidgetAnchor::Center;
        if (value == "Stretch") return WidgetAnchor::Stretch;
        return WidgetAnchor::TopLeft;
    }

    std::string toString(HitTestMode mode)
    {
        switch (mode)
        {
        case HitTestMode::Enabled: return "Enabled";
        case HitTestMode::DisabledAll: return "DisabledAll";
        default: return "DisabledSelf";
        }
    }

    HitTestMode hitTestModeFromString(const std::string& value)
    {
        if (value == "Enabled") return HitTestMode::Enabled;
        if (value == "DisabledAll") return HitTestMode::DisabledAll;
        return HitTestMode::DisabledSelf;
    }

    std::string toString(ScaleMode mode)
    {
        switch (mode)
        {
        case ScaleMode::Cover: return "Cover";
        case ScaleMode::Fill: return "Fill";
        case ScaleMode::ScaleDown: return "ScaleDown";
        case ScaleMode::UserSpecified: return "UserSpecified";
        default: return "Contain";
        }
    }

    ScaleMode scaleModeFromString(const std::string& value)
    {
        if (value == "Cover") return ScaleMode::Cover;
        if (value == "Fill") return ScaleMode::Fill;
        if (value == "ScaleDown") return ScaleMode::ScaleDown;
        if (value == "UserSpecified") return ScaleMode::UserSpecified;
        return ScaleMode::Contain;
    }

    // ── Phase 2: BrushType ──────────────────────────────────────────────────

    std::string toString(BrushType type)
    {
        switch (type)
        {
        case BrushType::None:           return "None";
        case BrushType::Image:          return "Image";
        case BrushType::NineSlice:      return "NineSlice";
        case BrushType::LinearGradient: return "LinearGradient";
        default:                        return "SolidColor";
        }
    }

    BrushType brushTypeFromString(const std::string& value)
    {
        if (value == "None")           return BrushType::None;
        if (value == "Image")          return BrushType::Image;
        if (value == "NineSlice")      return BrushType::NineSlice;
        if (value == "LinearGradient") return BrushType::LinearGradient;
        return BrushType::SolidColor;
    }

    // ── Phase 2: ClipMode ───────────────────────────────────────────────────

    std::string toString(ClipMode mode)
    {
        switch (mode)
        {
        case ClipMode::ClipToBounds:      return "ClipToBounds";
        case ClipMode::InheritFromParent: return "InheritFromParent";
        default:                          return "None";
        }
    }

    ClipMode clipModeFromString(const std::string& value)
    {
        if (value == "ClipToBounds")      return ClipMode::ClipToBounds;
        if (value == "InheritFromParent") return ClipMode::InheritFromParent;
        return ClipMode::None;
    }

    std::string toString(AnimatableProperty property)
    {
        switch (property)
        {
        case AnimatableProperty::RenderTranslationX: return "RenderTranslationX";
        case AnimatableProperty::RenderTranslationY: return "RenderTranslationY";
        case AnimatableProperty::RenderRotation: return "RenderRotation";
        case AnimatableProperty::RenderScaleX: return "RenderScaleX";
        case AnimatableProperty::RenderScaleY: return "RenderScaleY";
        case AnimatableProperty::RenderShearX: return "RenderShearX";
        case AnimatableProperty::RenderShearY: return "RenderShearY";
        case AnimatableProperty::ColorR: return "ColorR";
        case AnimatableProperty::ColorG: return "ColorG";
        case AnimatableProperty::ColorB: return "ColorB";
        case AnimatableProperty::ColorA: return "ColorA";
        case AnimatableProperty::PositionX: return "PositionX";
        case AnimatableProperty::PositionY: return "PositionY";
        case AnimatableProperty::SizeX: return "SizeX";
        case AnimatableProperty::SizeY: return "SizeY";
        case AnimatableProperty::FontSize: return "FontSize";
        default: return "Opacity";
        }
    }

    AnimatableProperty animatablePropertyFromString(const std::string& value)
    {
        if (value == "RenderTranslationX") return AnimatableProperty::RenderTranslationX;
        if (value == "RenderTranslationY") return AnimatableProperty::RenderTranslationY;
        if (value == "RenderRotation") return AnimatableProperty::RenderRotation;
        if (value == "RenderScaleX") return AnimatableProperty::RenderScaleX;
        if (value == "RenderScaleY") return AnimatableProperty::RenderScaleY;
        if (value == "RenderShearX") return AnimatableProperty::RenderShearX;
        if (value == "RenderShearY") return AnimatableProperty::RenderShearY;
        if (value == "ColorR") return AnimatableProperty::ColorR;
        if (value == "ColorG") return AnimatableProperty::ColorG;
        if (value == "ColorB") return AnimatableProperty::ColorB;
        if (value == "ColorA") return AnimatableProperty::ColorA;
        if (value == "PositionX") return AnimatableProperty::PositionX;
        if (value == "PositionY") return AnimatableProperty::PositionY;
        if (value == "SizeX") return AnimatableProperty::SizeX;
        if (value == "SizeY") return AnimatableProperty::SizeY;
        if (value == "FontSize") return AnimatableProperty::FontSize;
        return AnimatableProperty::Opacity;
    }

    std::string toString(EasingFunction easing)
    {
        switch (easing)
        {
        case EasingFunction::EaseInQuad: return "EaseInQuad";
        case EasingFunction::EaseOutQuad: return "EaseOutQuad";
        case EasingFunction::EaseInOutQuad: return "EaseInOutQuad";
        case EasingFunction::EaseInCubic: return "EaseInCubic";
        case EasingFunction::EaseOutCubic: return "EaseOutCubic";
        case EasingFunction::EaseInOutCubic: return "EaseInOutCubic";
        case EasingFunction::EaseInElastic: return "EaseInElastic";
        case EasingFunction::EaseOutElastic: return "EaseOutElastic";
        case EasingFunction::EaseInOutElastic: return "EaseInOutElastic";
        case EasingFunction::EaseInBounce: return "EaseInBounce";
        case EasingFunction::EaseOutBounce: return "EaseOutBounce";
        case EasingFunction::EaseInOutBounce: return "EaseInOutBounce";
        case EasingFunction::EaseInBack: return "EaseInBack";
        case EasingFunction::EaseOutBack: return "EaseOutBack";
        case EasingFunction::EaseInOutBack: return "EaseInOutBack";
        default: return "Linear";
        }
    }

    EasingFunction easingFunctionFromString(const std::string& value)
    {
        if (value == "EaseInQuad") return EasingFunction::EaseInQuad;
        if (value == "EaseOutQuad") return EasingFunction::EaseOutQuad;
        if (value == "EaseInOutQuad") return EasingFunction::EaseInOutQuad;
        if (value == "EaseInCubic") return EasingFunction::EaseInCubic;
        if (value == "EaseOutCubic") return EasingFunction::EaseOutCubic;
        if (value == "EaseInOutCubic") return EasingFunction::EaseInOutCubic;
        if (value == "EaseInElastic") return EasingFunction::EaseInElastic;
        if (value == "EaseOutElastic") return EasingFunction::EaseOutElastic;
        if (value == "EaseInOutElastic") return EasingFunction::EaseInOutElastic;
        if (value == "EaseInBounce") return EasingFunction::EaseInBounce;
        if (value == "EaseOutBounce") return EasingFunction::EaseOutBounce;
        if (value == "EaseInOutBounce") return EasingFunction::EaseInOutBounce;
        if (value == "EaseInBack") return EasingFunction::EaseInBack;
        if (value == "EaseOutBack") return EasingFunction::EaseOutBack;
        if (value == "EaseInOutBack") return EasingFunction::EaseInOutBack;
        return EasingFunction::Linear;
    }

    // ── Vec2/Vec4 JSON helpers ─────────────────────────────────────────────

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

    AnimationKeyframe readAnimationKeyframe(const json& value)
    {
        AnimationKeyframe keyframe{};
        if (!value.is_object())
        {
            return keyframe;
        }

        if (value.contains("time")) keyframe.time = value.at("time").get<float>();
        if (value.contains("value")) keyframe.value = readVec4(value.at("value"));
        if (value.contains("easing") && value.at("easing").is_string())
        {
            keyframe.easing = easingFunctionFromString(value.at("easing").get<std::string>());
        }
        return keyframe;
    }

    json writeAnimationKeyframe(const AnimationKeyframe& keyframe)
    {
        return json{
            { "time", keyframe.time },
            { "value", writeVec4(keyframe.value) },
            { "easing", toString(keyframe.easing) }
        };
    }

    AnimationTrack readAnimationTrack(const json& value)
    {
        AnimationTrack track{};
        if (!value.is_object())
        {
            return track;
        }

        if (value.contains("targetElementId") && value.at("targetElementId").is_string())
        {
            track.targetElementId = value.at("targetElementId").get<std::string>();
        }
        if (value.contains("property") && value.at("property").is_string())
        {
            track.property = animatablePropertyFromString(value.at("property").get<std::string>());
        }
        if (value.contains("keyframes") && value.at("keyframes").is_array())
        {
            for (const auto& keyframeJson : value.at("keyframes"))
            {
                track.keyframes.push_back(readAnimationKeyframe(keyframeJson));
            }
        }
        return track;
    }

    json writeAnimationTrack(const AnimationTrack& track)
    {
        json keyframes = json::array();
        for (const auto& keyframe : track.keyframes)
        {
            keyframes.push_back(writeAnimationKeyframe(keyframe));
        }

        return json{
            { "targetElementId", track.targetElementId },
            { "property", toString(track.property) },
            { "keyframes", keyframes }
        };
    }

    WidgetAnimation readWidgetAnimation(const json& value)
    {
        WidgetAnimation animation{};
        if (!value.is_object())
        {
            return animation;
        }

        if (value.contains("name") && value.at("name").is_string())
        {
            animation.name = value.at("name").get<std::string>();
        }
        if (value.contains("duration")) animation.duration = value.at("duration").get<float>();
        if (value.contains("isLooping")) animation.isLooping = value.at("isLooping").get<bool>();
        if (value.contains("playbackSpeed")) animation.playbackSpeed = value.at("playbackSpeed").get<float>();
        if (value.contains("tracks") && value.at("tracks").is_array())
        {
            for (const auto& trackJson : value.at("tracks"))
            {
                animation.tracks.push_back(readAnimationTrack(trackJson));
            }
        }

        return animation;
    }

    json writeWidgetAnimation(const WidgetAnimation& animation)
    {
        json tracks = json::array();
        for (const auto& track : animation.tracks)
        {
            tracks.push_back(writeAnimationTrack(track));
        }

        return json{
            { "name", animation.name },
            { "duration", animation.duration },
            { "isLooping", animation.isLooping },
            { "playbackSpeed", animation.playbackSpeed },
            { "tracks", tracks }
        };
    }

    // ── Phase 2: UIBrush JSON helpers ───────────────────────────────────────

    UIBrush readBrush(const json& value)
    {
        UIBrush brush;
        if (!value.is_object()) return brush;
        if (value.contains("type"))
            brush.type = brushTypeFromString(value.at("type").get<std::string>());
        if (value.contains("color"))
            brush.color = readVec4(value.at("color"));
        if (value.contains("colorEnd"))
            brush.colorEnd = readVec4(value.at("colorEnd"));
        if (value.contains("gradientAngle"))
            brush.gradientAngle = value.at("gradientAngle").get<float>();
        if (value.contains("imagePath"))
            brush.imagePath = value.at("imagePath").get<std::string>();
        if (value.contains("imageMargin"))
            brush.imageMargin = readVec4(value.at("imageMargin"));
        if (value.contains("imageTiling"))
            brush.imageTiling = readVec2(value.at("imageTiling"));
        return brush;
    }

    json writeBrush(const UIBrush& brush)
    {
        json obj = json::object();
        obj["type"] = toString(brush.type);
        if (brush.type == BrushType::SolidColor || brush.type == BrushType::LinearGradient)
        {
            obj["color"] = writeVec4(brush.color);
        }
        if (brush.type == BrushType::LinearGradient)
        {
            obj["colorEnd"] = writeVec4(brush.colorEnd);
            obj["gradientAngle"] = brush.gradientAngle;
        }
        if (brush.type == BrushType::Image || brush.type == BrushType::NineSlice)
        {
            if (!brush.imagePath.empty())
                obj["imagePath"] = brush.imagePath;
            if (brush.type == BrushType::NineSlice)
                obj["imageMargin"] = writeVec4(brush.imageMargin);
            if (brush.imageTiling.x != 1.0f || brush.imageTiling.y != 1.0f)
                obj["imageTiling"] = writeVec2(brush.imageTiling);
        }
        return obj;
    }

    // ── Phase 2: RenderTransform JSON helpers ───────────────────────────────

    RenderTransform readRenderTransform(const json& value)
    {
        RenderTransform rt;
        if (!value.is_object()) return rt;
        if (value.contains("translation"))
            rt.translation = readVec2(value.at("translation"));
        if (value.contains("rotation"))
            rt.rotation = value.at("rotation").get<float>();
        if (value.contains("scale"))
            rt.scale = readVec2(value.at("scale"));
        if (value.contains("shear"))
            rt.shear = readVec2(value.at("shear"));
        if (value.contains("pivot"))
            rt.pivot = readVec2(value.at("pivot"));
        return rt;
    }

    json writeRenderTransform(const RenderTransform& rt)
    {
        json obj = json::object();
        if (rt.translation.x != 0.0f || rt.translation.y != 0.0f)
            obj["translation"] = writeVec2(rt.translation);
        if (rt.rotation != 0.0f)
            obj["rotation"] = rt.rotation;
        if (rt.scale.x != 1.0f || rt.scale.y != 1.0f)
            obj["scale"] = writeVec2(rt.scale);
        if (rt.shear.x != 0.0f || rt.shear.y != 0.0f)
            obj["shear"] = writeVec2(rt.shear);
        if (rt.pivot.x != 0.5f || rt.pivot.y != 0.5f)
            obj["pivot"] = writeVec2(rt.pivot);
        return obj;
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
            element.style.color = readVec4(entry.at("color"));
        }
        else if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::Grid
            || element.type == WidgetElementType::ScrollView
            || element.type == WidgetElementType::WrapBox || element.type == WidgetElementType::UniformGrid
            || element.type == WidgetElementType::SizeBox || element.type == WidgetElementType::ScaleBox
            || element.type == WidgetElementType::WidgetSwitcher || element.type == WidgetElementType::Overlay
            || element.type == WidgetElementType::Border || element.type == WidgetElementType::ListView
            || element.type == WidgetElementType::TileView)
        {
            element.style.color = Vec4{ 0.1f, 0.1f, 0.12f, 0.65f };
        }
        else if (element.type == WidgetElementType::EntryBar)
        {
            element.style.color = Vec4{ 0.12f, 0.12f, 0.15f, 0.9f };
        }
        else if (element.type == WidgetElementType::ProgressBar || element.type == WidgetElementType::Slider)
        {
            element.style.color = Vec4{ 0.14f, 0.14f, 0.18f, 0.9f };
        }
        else if (element.type == WidgetElementType::ColorPicker)
        {
            element.style.color = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        }
        if (entry.contains("hoverColor"))
        {
            element.style.hoverColor = readVec4(entry.at("hoverColor"));
        }
        else
        {
            element.style.hoverColor = (element.type == WidgetElementType::Button) ? brightenColor(element.style.color) : element.style.color;
        }
        if (entry.contains("fillColor"))
        {
            element.style.fillColor = readVec4(entry.at("fillColor"));
        }
        else
        {
            element.style.fillColor = (element.type == WidgetElementType::ProgressBar || element.type == WidgetElementType::Slider)
                ? brightenColor(element.style.color)
                : element.style.color;
        }
        if (entry.contains("textColor"))
        {
            element.style.textColor = readVec4(entry.at("textColor"));
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
        if (entry.contains("hitTestMode"))
        {
            element.hitTestMode = hitTestModeFromString(entry.at("hitTestMode").get<std::string>());
        }
        else if (entry.contains("isHitTestable"))
        {
            element.hitTestMode = entry.at("isHitTestable").get<bool>() ? HitTestMode::Enabled : HitTestMode::DisabledSelf;
        }
        else
        {
            const bool interactive = (element.type == WidgetElementType::Button ||
                element.type == WidgetElementType::ToggleButton ||
                element.type == WidgetElementType::RadioButton ||
                element.type == WidgetElementType::ColorPicker ||
                element.type == WidgetElementType::EntryBar ||
                element.type == WidgetElementType::Slider ||
                element.type == WidgetElementType::CheckBox ||
                element.type == WidgetElementType::DropDown ||
                element.type == WidgetElementType::DropdownButton);
            element.hitTestMode = interactive ? HitTestMode::Enabled : HitTestMode::DisabledSelf;
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
            element.style.borderColor = readVec4(entry.at("borderColor"));
        }
        if (entry.contains("borderThickness"))
        {
            element.style.borderThickness = entry.at("borderThickness").get<float>();
        }
        if (entry.contains("borderRadius"))
        {
            element.style.borderRadius = entry.at("borderRadius").get<float>();
        }
        if (entry.contains("opacity"))
        {
            element.style.opacity = entry.at("opacity").get<float>();
        }
        if (entry.contains("isVisible"))
        {
            element.style.isVisible = entry.at("isVisible").get<bool>();
        }
        if (entry.contains("tooltipText"))
        {
            element.tooltipText = entry.at("tooltipText").get<std::string>();
        }
        if (entry.contains("isBold"))
        {
            element.style.isBold = entry.at("isBold").get<bool>();
        }
        if (entry.contains("isItalic"))
        {
            element.style.isItalic = entry.at("isItalic").get<bool>();
        }
        if (entry.contains("gradientColor"))
        {
            element.style.gradientColor = readVec4(entry.at("gradientColor"));
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
        if (entry.contains("anchor"))
        {
            element.anchor = anchorFromString(entry.at("anchor").get<std::string>());
        }
        if (entry.contains("anchorOffset"))
        {
            element.anchorOffset = readVec2(entry.at("anchorOffset"));
        }
        if (entry.contains("isCanvasRoot"))
        {
            element.isCanvasRoot = entry.at("isCanvasRoot").get<bool>();
        }
        if (entry.contains("columns"))
        {
            element.columns = entry.at("columns").get<int>();
        }
        if (entry.contains("rows"))
        {
            element.rows = entry.at("rows").get<int>();
        }
        if (entry.contains("widthOverride"))
        {
            element.widthOverride = entry.at("widthOverride").get<float>();
        }
        if (entry.contains("heightOverride"))
        {
            element.heightOverride = entry.at("heightOverride").get<float>();
        }
        if (entry.contains("scaleMode"))
        {
            element.scaleMode = scaleModeFromString(entry.at("scaleMode").get<std::string>());
        }
        if (entry.contains("userScale"))
        {
            element.userScale = entry.at("userScale").get<float>();
        }
        if (entry.contains("activeChildIndex"))
        {
            element.activeChildIndex = entry.at("activeChildIndex").get<int>();
        }
        // ── Phase 4: Border, Spinner, RichText, ListView, TileView ──────
        if (entry.contains("borderBrush"))
        {
            element.borderBrush = readBrush(entry.at("borderBrush"));
        }
        if (entry.contains("borderThicknessLeft"))
        {
            element.borderThicknessLeft = entry.at("borderThicknessLeft").get<float>();
        }
        if (entry.contains("borderThicknessTop"))
        {
            element.borderThicknessTop = entry.at("borderThicknessTop").get<float>();
        }
        if (entry.contains("borderThicknessRight"))
        {
            element.borderThicknessRight = entry.at("borderThicknessRight").get<float>();
        }
        if (entry.contains("borderThicknessBottom"))
        {
            element.borderThicknessBottom = entry.at("borderThicknessBottom").get<float>();
        }
        if (entry.contains("contentPadding"))
        {
            element.contentPadding = readVec2(entry.at("contentPadding"));
        }
        if (entry.contains("spinnerDotCount"))
        {
            element.spinnerDotCount = entry.at("spinnerDotCount").get<int>();
        }
        if (entry.contains("spinnerSpeed"))
        {
            element.spinnerSpeed = entry.at("spinnerSpeed").get<float>();
        }
        if (entry.contains("richText"))
        {
            element.richText = entry.at("richText").get<std::string>();
        }
        if (entry.contains("totalItemCount"))
        {
            element.totalItemCount = entry.at("totalItemCount").get<int>();
        }
        if (entry.contains("itemHeight"))
        {
            element.itemHeight = entry.at("itemHeight").get<float>();
        }
        if (entry.contains("itemWidth"))
        {
            element.itemWidth = entry.at("itemWidth").get<float>();
        }
        if (entry.contains("columnsPerRow"))
        {
            element.columnsPerRow = entry.at("columnsPerRow").get<int>();
        }
        // ── Phase 2: Brush, RenderTransform, ClipMode ────────────────────
        if (entry.contains("background"))
        {
            element.background = readBrush(entry.at("background"));
        }
        if (entry.contains("hoverBrush"))
        {
            element.hoverBrush = readBrush(entry.at("hoverBrush"));
        }
        if (entry.contains("fillBrush"))
        {
            element.fillBrush = readBrush(entry.at("fillBrush"));
        }
        if (entry.contains("renderTransform"))
        {
            element.renderTransform = readRenderTransform(entry.at("renderTransform"));
        }
        if (entry.contains("clipMode"))
        {
            element.clipMode = clipModeFromString(entry.at("clipMode").get<std::string>());
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
        entry["color"] = writeVec4(element.style.color);
        if (element.style.fillColor.x != element.style.color.x || element.style.fillColor.y != element.style.color.y ||
            element.style.fillColor.z != element.style.color.z || element.style.fillColor.w != element.style.color.w)
        {
            entry["fillColor"] = writeVec4(element.style.fillColor);
        }
        entry["textColor"] = writeVec4(element.style.textColor);
        if (element.style.hoverColor.x != element.style.color.x || element.style.hoverColor.y != element.style.color.y ||
            element.style.hoverColor.z != element.style.color.z || element.style.hoverColor.w != element.style.color.w)
        {
            entry["hoverColor"] = writeVec4(element.style.hoverColor);
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
        if (element.hitTestMode != HitTestMode::DisabledSelf)
        {
            entry["hitTestMode"] = toString(element.hitTestMode);
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
        else if (element.type == WidgetElementType::WrapBox)
        {
            entry["orientation"] = toString(element.orientation);
            if (element.spacing > 0.0f)
            {
                entry["spacing"] = element.spacing;
            }
        }
        else if (element.type == WidgetElementType::UniformGrid)
        {
            if (element.columns > 0)
            {
                entry["columns"] = element.columns;
            }
            if (element.rows > 0)
            {
                entry["rows"] = element.rows;
            }
            if (element.spacing > 0.0f)
            {
                entry["spacing"] = element.spacing;
            }
        }
        else if (element.type == WidgetElementType::SizeBox)
        {
            if (element.widthOverride > 0.0f)
            {
                entry["widthOverride"] = element.widthOverride;
            }
            if (element.heightOverride > 0.0f)
            {
                entry["heightOverride"] = element.heightOverride;
            }
        }
        else if (element.type == WidgetElementType::ScaleBox)
        {
            entry["scaleMode"] = toString(element.scaleMode);
            if (element.scaleMode == ScaleMode::UserSpecified)
            {
                entry["userScale"] = element.userScale;
            }
        }
        else if (element.type == WidgetElementType::WidgetSwitcher)
        {
            entry["activeChildIndex"] = element.activeChildIndex;
        }
        else if (element.type == WidgetElementType::Border)
        {
            if (element.borderBrush.isVisible())
            {
                entry["borderBrush"] = writeBrush(element.borderBrush);
            }
            if (element.borderThicknessLeft > 0.0f)
            {
                entry["borderThicknessLeft"] = element.borderThicknessLeft;
            }
            if (element.borderThicknessTop > 0.0f)
            {
                entry["borderThicknessTop"] = element.borderThicknessTop;
            }
            if (element.borderThicknessRight > 0.0f)
            {
                entry["borderThicknessRight"] = element.borderThicknessRight;
            }
            if (element.borderThicknessBottom > 0.0f)
            {
                entry["borderThicknessBottom"] = element.borderThicknessBottom;
            }
            if (element.contentPadding.x > 0.0f || element.contentPadding.y > 0.0f)
            {
                entry["contentPadding"] = writeVec2(element.contentPadding);
            }
        }
        else if (element.type == WidgetElementType::Spinner)
        {
            entry["spinnerDotCount"] = element.spinnerDotCount;
            entry["spinnerSpeed"] = element.spinnerSpeed;
        }
        else if (element.type == WidgetElementType::RichText)
        {
            if (!element.richText.empty())
            {
                entry["richText"] = element.richText;
            }
        }
        else if (element.type == WidgetElementType::ListView)
        {
            entry["totalItemCount"] = element.totalItemCount;
            entry["itemHeight"] = element.itemHeight;
            if (element.scrollable)
            {
                entry["scrollable"] = element.scrollable;
            }
        }
        else if (element.type == WidgetElementType::TileView)
        {
            entry["totalItemCount"] = element.totalItemCount;
            entry["itemHeight"] = element.itemHeight;
            entry["itemWidth"] = element.itemWidth;
            entry["columnsPerRow"] = element.columnsPerRow;
            if (element.scrollable)
            {
                entry["scrollable"] = element.scrollable;
            }
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
        if (element.style.borderThickness > 0.0f)
        {
            entry["borderThickness"] = element.style.borderThickness;
            entry["borderColor"] = writeVec4(element.style.borderColor);
        }
        if (element.style.borderRadius > 0.0f)
        {
            entry["borderRadius"] = element.style.borderRadius;
        }
        if (element.style.opacity < 1.0f)
        {
            entry["opacity"] = element.style.opacity;
        }
        if (!element.style.isVisible)
        {
            entry["isVisible"] = element.style.isVisible;
        }
        if (!element.tooltipText.empty())
        {
            entry["tooltipText"] = element.tooltipText;
        }
        if (element.style.isBold)
        {
            entry["isBold"] = element.style.isBold;
        }
        if (element.style.isItalic)
        {
            entry["isItalic"] = element.style.isItalic;
        }
        if (element.style.gradientColor.x != 0.0f ||
            element.style.gradientColor.y != 0.0f ||
            element.style.gradientColor.z != 0.0f ||
            element.style.gradientColor.w != 0.0f)
        {
            entry["gradientColor"] = writeVec4(element.style.gradientColor);
        }
        if (element.maxSize.x > 0.0f || element.maxSize.y > 0.0f)
        {
            entry["maxSize"] = writeVec2(element.maxSize);
        }
        if (element.anchor != WidgetAnchor::TopLeft)
        {
            entry["anchor"] = toString(element.anchor);
        }
        if (element.anchorOffset.x != 0.0f || element.anchorOffset.y != 0.0f)
        {
            entry["anchorOffset"] = writeVec2(element.anchorOffset);
        }
        if (element.isCanvasRoot)
        {
            entry["isCanvasRoot"] = true;
        }
        // ── Phase 2: Brush, RenderTransform, ClipMode ────────────────────
        if (element.background.type != BrushType::SolidColor
            || element.background.color.x != 1.0f || element.background.color.y != 1.0f
            || element.background.color.z != 1.0f || element.background.color.w != 1.0f)
        {
            entry["background"] = writeBrush(element.background);
        }
        if (element.hoverBrush.type != BrushType::SolidColor
            || element.hoverBrush.color.x != 1.0f || element.hoverBrush.color.y != 1.0f
            || element.hoverBrush.color.z != 1.0f || element.hoverBrush.color.w != 1.0f)
        {
            entry["hoverBrush"] = writeBrush(element.hoverBrush);
        }
        if (element.fillBrush.type != BrushType::SolidColor
            || element.fillBrush.color.x != 1.0f || element.fillBrush.color.y != 1.0f
            || element.fillBrush.color.z != 1.0f || element.fillBrush.color.w != 1.0f)
        {
            entry["fillBrush"] = writeBrush(element.fillBrush);
        }
        if (!element.renderTransform.isIdentity())
        {
            entry["renderTransform"] = writeRenderTransform(element.renderTransform);
        }
        if (element.clipMode != ClipMode::None)
        {
            entry["clipMode"] = toString(element.clipMode);
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

float EvaluateEasing(EasingFunction easing, float t)
{
    const float x = std::clamp(t, 0.0f, 1.0f);

    switch (easing)
    {
    case EasingFunction::EaseInQuad:
        return x * x;
    case EasingFunction::EaseOutQuad:
        return 1.0f - (1.0f - x) * (1.0f - x);
    case EasingFunction::EaseInOutQuad:
        return (x < 0.5f) ? (2.0f * x * x) : (1.0f - std::pow(-2.0f * x + 2.0f, 2.0f) * 0.5f);

    case EasingFunction::EaseInCubic:
        return x * x * x;
    case EasingFunction::EaseOutCubic:
        return 1.0f - std::pow(1.0f - x, 3.0f);
    case EasingFunction::EaseInOutCubic:
        return (x < 0.5f) ? (4.0f * x * x * x) : (1.0f - std::pow(-2.0f * x + 2.0f, 3.0f) * 0.5f);

    case EasingFunction::EaseInElastic:
    {
        if (x == 0.0f || x == 1.0f) return x;
        constexpr float c4 = (2.0f * kPi) / 3.0f;
        return -std::pow(2.0f, 10.0f * x - 10.0f) * std::sin((x * 10.0f - 10.75f) * c4);
    }
    case EasingFunction::EaseOutElastic:
    {
        if (x == 0.0f || x == 1.0f) return x;
        constexpr float c4 = (2.0f * kPi) / 3.0f;
        return std::pow(2.0f, -10.0f * x) * std::sin((x * 10.0f - 0.75f) * c4) + 1.0f;
    }
    case EasingFunction::EaseInOutElastic:
    {
        if (x == 0.0f || x == 1.0f) return x;
        constexpr float c5 = (2.0f * kPi) / 4.5f;
        if (x < 0.5f)
        {
            return -(std::pow(2.0f, 20.0f * x - 10.0f) * std::sin((20.0f * x - 11.125f) * c5)) * 0.5f;
        }
        return (std::pow(2.0f, -20.0f * x + 10.0f) * std::sin((20.0f * x - 11.125f) * c5)) * 0.5f + 1.0f;
    }

    case EasingFunction::EaseInBounce:
        return 1.0f - EaseOutBounce01(1.0f - x);
    case EasingFunction::EaseOutBounce:
        return EaseOutBounce01(x);
    case EasingFunction::EaseInOutBounce:
        return (x < 0.5f)
            ? (1.0f - EaseOutBounce01(1.0f - 2.0f * x)) * 0.5f
            : (1.0f + EaseOutBounce01(2.0f * x - 1.0f)) * 0.5f;

    case EasingFunction::EaseInBack:
    {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return c3 * x * x * x - c1 * x * x;
    }
    case EasingFunction::EaseOutBack:
    {
        constexpr float c1 = 1.70158f;
        constexpr float c3 = c1 + 1.0f;
        return 1.0f + c3 * std::pow(x - 1.0f, 3.0f) + c1 * std::pow(x - 1.0f, 2.0f);
    }
    case EasingFunction::EaseInOutBack:
    {
        constexpr float c1 = 1.70158f;
        constexpr float c2 = c1 * 1.525f;
        if (x < 0.5f)
        {
            const float t2 = 2.0f * x;
            return (t2 * t2 * ((c2 + 1.0f) * t2 - c2)) * 0.5f;
        }
        const float t2 = 2.0f * x - 2.0f;
        return (t2 * t2 * ((c2 + 1.0f) * t2 + c2) + 2.0f) * 0.5f;
    }

    case EasingFunction::Linear:
    default:
        return x;
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

void Widget::setAnimations(std::vector<WidgetAnimation> animations)
{
    m_animations = std::move(animations);
}

const std::vector<WidgetAnimation>& Widget::getAnimations() const
{
    return m_animations;
}

std::vector<WidgetAnimation>& Widget::getAnimationsMutable()
{
    return m_animations;
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
    m_animations.clear();
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

    if (data.contains("m_animations") && data.at("m_animations").is_array())
    {
        for (const auto& animationJson : data.at("m_animations"))
        {
            m_animations.push_back(readWidgetAnimation(animationJson));
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

    json animations = json::array();
    for (const auto& animation : m_animations)
    {
        if (animation.name.empty())
        {
            continue;
        }
        animations.push_back(writeWidgetAnimation(animation));
    }
    data["m_animations"] = animations;

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

// ── Widget constructor ──────────────────────────────────────────────────

Widget::Widget()
{
    m_animationPlayer.attachWidget(this);
}

// ── Widget animation helpers ────────────────────────────────────────────

WidgetAnimationPlayer& Widget::animationPlayer()
{
    return m_animationPlayer;
}

const WidgetAnimationPlayer& Widget::animationPlayer() const
{
    return m_animationPlayer;
}

const WidgetAnimation* Widget::findAnimationByName(const std::string& animationName) const
{
    for (const auto& anim : m_animations)
    {
        if (anim.name == animationName)
        {
            return &anim;
        }
    }
    return nullptr;
}

namespace
{
    WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
    {
        for (auto& el : elements)
        {
            if (el.id == id)
            {
                return &el;
            }
            if (!el.children.empty())
            {
                WidgetElement* found = FindElementById(el.children, id);
                if (found)
                {
                    return found;
                }
            }
        }
        return nullptr;
    }
}

void Widget::applyAnimationTrackValue(const AnimationTrack& track, const Vec4& value)
{
    WidgetElement* target = FindElementById(m_elements, track.targetElementId);
    if (!target)
    {
        return;
    }

    switch (track.property)
    {
    case AnimatableProperty::RenderTranslationX:
        target->renderTransform.translation.x = value.x;
        break;
    case AnimatableProperty::RenderTranslationY:
        target->renderTransform.translation.y = value.x;
        break;
    case AnimatableProperty::RenderRotation:
        target->renderTransform.rotation = value.x;
        break;
    case AnimatableProperty::RenderScaleX:
        target->renderTransform.scale.x = value.x;
        break;
    case AnimatableProperty::RenderScaleY:
        target->renderTransform.scale.y = value.x;
        break;
    case AnimatableProperty::RenderShearX:
        target->renderTransform.shear.x = value.x;
        break;
    case AnimatableProperty::RenderShearY:
        target->renderTransform.shear.y = value.x;
        break;
    case AnimatableProperty::Opacity:
        target->style.opacity = value.x;
        break;
    case AnimatableProperty::ColorR:
        target->style.color.x = value.x;
        break;
    case AnimatableProperty::ColorG:
        target->style.color.y = value.x;
        break;
    case AnimatableProperty::ColorB:
        target->style.color.z = value.x;
        break;
    case AnimatableProperty::ColorA:
        target->style.color.w = value.x;
        break;
    case AnimatableProperty::PositionX:
        target->from.x = value.x;
        break;
    case AnimatableProperty::PositionY:
        target->from.y = value.x;
        break;
    case AnimatableProperty::SizeX:
        target->to.x = value.x;
        break;
    case AnimatableProperty::SizeY:
        target->to.y = value.x;
        break;
    case AnimatableProperty::FontSize:
        target->fontSize = value.x;
        break;
    }
}

bool Widget::applyAnimationAtTime(const std::string& animationName, float timeSeconds)
{
    const WidgetAnimation* anim = findAnimationByName(animationName);
    if (!anim)
    {
        return false;
    }

    const float t = (anim->duration > 0.0f)
        ? std::clamp(timeSeconds, 0.0f, anim->duration)
        : 0.0f;

    for (const auto& track : anim->tracks)
    {
        if (track.keyframes.empty())
        {
            continue;
        }

        // Single keyframe — just use its value
        if (track.keyframes.size() == 1)
        {
            applyAnimationTrackValue(track, track.keyframes[0].value);
            continue;
        }

        // Find surrounding keyframes
        const AnimationKeyframe* prev = &track.keyframes.front();
        const AnimationKeyframe* next = &track.keyframes.back();

        for (size_t i = 0; i + 1 < track.keyframes.size(); ++i)
        {
            if (t >= track.keyframes[i].time && t <= track.keyframes[i + 1].time)
            {
                prev = &track.keyframes[i];
                next = &track.keyframes[i + 1];
                break;
            }
        }

        float alpha = 0.0f;
        const float span = next->time - prev->time;
        if (span > 0.0f)
        {
            alpha = (t - prev->time) / span;
        }

        alpha = EvaluateEasing(next->easing, alpha);

        Vec4 interpolated{};
        interpolated.x = prev->value.x + (next->value.x - prev->value.x) * alpha;
        interpolated.y = prev->value.y + (next->value.y - prev->value.y) * alpha;
        interpolated.z = prev->value.z + (next->value.z - prev->value.z) * alpha;
        interpolated.w = prev->value.w + (next->value.w - prev->value.w) * alpha;

        applyAnimationTrackValue(track, interpolated);
    }

    m_layoutDirty = true;
    return true;
}

// ── WidgetAnimationPlayer ───────────────────────────────────────────────

void WidgetAnimationPlayer::attachWidget(Widget* widget)
{
    m_widget = widget;
}

void WidgetAnimationPlayer::play(const std::string& animName, bool fromStart)
{
    m_currentAnimation = animName;
    m_playing = true;
    m_reverse = false;
    if (fromStart)
    {
        m_currentTime = 0.0f;
    }
}

void WidgetAnimationPlayer::playReverse(const std::string& animName)
{
    m_currentAnimation = animName;
    m_playing = true;
    m_reverse = true;

    if (m_widget)
    {
        const auto& animations = m_widget->getAnimations();
        for (const auto& anim : animations)
        {
            if (anim.name == animName)
            {
                m_currentTime = anim.duration;
                break;
            }
        }
    }
}

void WidgetAnimationPlayer::pause()
{
    m_playing = false;
}

void WidgetAnimationPlayer::stop()
{
    m_playing = false;
    m_currentTime = 0.0f;
    m_currentAnimation.clear();
}

void WidgetAnimationPlayer::setSpeed(float speed)
{
    m_speed = speed;
}

float WidgetAnimationPlayer::getCurrentTime() const
{
    return m_currentTime;
}

bool WidgetAnimationPlayer::isPlaying() const
{
    return m_playing;
}

const std::string& WidgetAnimationPlayer::getCurrentAnimation() const
{
    return m_currentAnimation;
}

void WidgetAnimationPlayer::tick(float deltaTime)
{
    if (!m_playing || !m_widget || m_currentAnimation.empty())
    {
        return;
    }

    const auto& animations = m_widget->getAnimations();
    const WidgetAnimation* anim = nullptr;
    for (const auto& a : animations)
    {
        if (a.name == m_currentAnimation)
        {
            anim = &a;
            break;
        }
    }

    if (!anim || anim->duration <= 0.0f)
    {
        m_playing = false;
        return;
    }

    const float dt = deltaTime * m_speed * anim->playbackSpeed;
    if (m_reverse)
    {
        m_currentTime -= dt;
    }
    else
    {
        m_currentTime += dt;
    }

    bool finished = false;
    if (!m_reverse && m_currentTime >= anim->duration)
    {
        if (anim->isLooping)
        {
            m_currentTime = std::fmod(m_currentTime, anim->duration);
        }
        else
        {
            m_currentTime = anim->duration;
            finished = true;
        }
    }
    else if (m_reverse && m_currentTime <= 0.0f)
    {
        if (anim->isLooping)
        {
            m_currentTime = anim->duration + std::fmod(m_currentTime, anim->duration);
        }
        else
        {
            m_currentTime = 0.0f;
            finished = true;
        }
    }

    m_widget->applyAnimationAtTime(m_currentAnimation, m_currentTime);

    if (finished)
    {
        m_playing = false;
    }
}
