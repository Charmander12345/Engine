#include "WidgetDetailSchema.h"

#include <cstdio>
#include <algorithm>

#include "EditorUIBuilder.h"
#include "EditorTheme.h"
#include "UIWidgets/EntryBarWidget.h"
#include "UIWidgets/CheckBoxWidget.h"
#include "UIWidgets/DropDownWidget.h"
#include "UIWidgets/ColorPickerWidget.h"
#include "UIWidgets/SliderWidget.h"

// ── Utilities ────────────────────────────────────────────────────────────

std::string WidgetDetailSchema::getTypeName(WidgetElementType type)
{
    switch (type)
    {
    case WidgetElementType::Panel:          return "Panel";
    case WidgetElementType::Text:           return "Text";
    case WidgetElementType::Label:          return "Label";
    case WidgetElementType::Button:         return "Button";
    case WidgetElementType::StackPanel:     return "StackPanel";
    case WidgetElementType::Grid:           return "Grid";
    case WidgetElementType::Image:          return "Image";
    case WidgetElementType::EntryBar:       return "EntryBar";
    case WidgetElementType::ProgressBar:    return "ProgressBar";
    case WidgetElementType::Slider:         return "Slider";
    case WidgetElementType::CheckBox:       return "CheckBox";
    case WidgetElementType::DropDown:       return "DropDown";
    case WidgetElementType::ColorPicker:    return "ColorPicker";
    case WidgetElementType::DropdownButton: return "DropdownButton";
    case WidgetElementType::TreeView:       return "TreeView";
    case WidgetElementType::TabView:        return "TabView";
    case WidgetElementType::Separator:      return "Separator";
    case WidgetElementType::ScrollView:     return "ScrollView";
    case WidgetElementType::ToggleButton:   return "ToggleButton";
    case WidgetElementType::RadioButton:    return "RadioButton";
    case WidgetElementType::WrapBox:        return "WrapBox";
    case WidgetElementType::UniformGrid:    return "UniformGrid";
    case WidgetElementType::SizeBox:        return "SizeBox";
    case WidgetElementType::ScaleBox:       return "ScaleBox";
    case WidgetElementType::WidgetSwitcher: return "WidgetSwitcher";
    case WidgetElementType::Overlay:        return "Overlay";
    case WidgetElementType::Border:         return "Border";
    case WidgetElementType::Spinner:        return "Spinner";
    case WidgetElementType::RichText:       return "RichText";
    case WidgetElementType::ListView:       return "ListView";
    case WidgetElementType::TileView:       return "TileView";
    default:                                return "Unknown";
    }
}

bool WidgetDetailSchema::hasTextProperties(WidgetElementType type)
{
    return type == WidgetElementType::Text
        || type == WidgetElementType::Label
        || type == WidgetElementType::Button
        || type == WidgetElementType::ToggleButton
        || type == WidgetElementType::DropdownButton
        || type == WidgetElementType::RadioButton;
}

// ── Shared sections ─────────────────────────────────────────────────────

void WidgetDetailSchema::addIdentitySection(const std::string& prefix,
                                            WidgetElement* sel,
                                            std::function<void()> applyChange,
                                            WidgetElement* root,
                                            const Options& options)
{
    std::vector<WidgetElement> children;

    // Type (read-only)
    children.push_back(EditorUIBuilder::makeSecondaryLabel("Type: " + getTypeName(sel->type)));

    // Editable ID
    if (options.showEditableId)
    {
        children.push_back(EditorUIBuilder::makeStringRow(prefix + ".Id", "ID",
            sel->id,
            [sel, applyChange, onIdRenamed = options.onIdRenamed,
             onRefresh = options.onRefreshHierarchy](const std::string& v) {
                sel->id = v;
                if (onIdRenamed) onIdRenamed(v);
                applyChange();
                if (onRefresh) onRefresh();
            }));
    }
    else
    {
        children.push_back(EditorUIBuilder::makeSecondaryLabel("ID: " + sel->id));
    }

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Identity", "Identity", children));
}

void WidgetDetailSchema::addTransformSection(const std::string& prefix,
                                             WidgetElement* sel,
                                             std::function<void()> applyChange,
                                             WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".From", "From",
        sel->from,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->from.x = v; else sel->from.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".To", "To",
        sel->to,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->to.x = v; else sel->to.y = v;
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Transform", "Transform", children));
}

void WidgetDetailSchema::addAnchorSection(const std::string& prefix,
                                          WidgetElement* sel,
                                          std::function<void()> applyChange,
                                          WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".Anchor", "Anchor",
        { "TopLeft", "TopRight", "BottomLeft", "BottomRight",
          "Top", "Bottom", "Left", "Right", "Center", "Stretch" },
        static_cast<int>(sel->anchor),
        [sel, applyChange](int idx) {
            sel->anchor = static_cast<WidgetAnchor>(idx);
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".AnchorOff", "Offset",
        sel->anchorOffset,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->anchorOffset.x = v; else sel->anchorOffset.y = v;
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Anchor", "Anchor", children));
}

void WidgetDetailSchema::addHitTestSection(const std::string& prefix,
                                           WidgetElement* sel,
                                           std::function<void()> applyChange,
                                           WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".HitTest", "Mode",
        { "Enabled", "Disabled (Self)", "Disabled (Self + Children)" },
        static_cast<int>(sel->hitTestMode),
        [sel, applyChange](int idx) {
            sel->hitTestMode = static_cast<HitTestMode>(idx);
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".HitTest", "Hit Test", children));
}

void WidgetDetailSchema::addLayoutSection(const std::string& prefix,
                                          WidgetElement* sel,
                                          std::function<void()> applyChange,
                                          WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".MinSize", "Min Size",
        sel->minSize,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->minSize.x = v; else sel->minSize.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".MaxSize", "Max Size",
        sel->maxSize,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->maxSize.x = v; else sel->maxSize.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".Padding", "Padding",
        sel->padding,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->padding.x = v; else sel->padding.y = v;
            applyChange();
        }));

    // H Align
    {
        int hAlignIndex = 0;
        if (sel->fillX) hAlignIndex = 3;
        else if (sel->textAlignH == TextAlignH::Center) hAlignIndex = 1;
        else if (sel->textAlignH == TextAlignH::Right) hAlignIndex = 2;

        children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".HAlign", "H Align",
            { "Left", "Center", "Right", "Fill" }, hAlignIndex,
            [sel, applyChange](int idx) {
                if (idx == 3) { sel->fillX = true; }
                else {
                    sel->fillX = false;
                    if (idx == 1)      sel->textAlignH = TextAlignH::Center;
                    else if (idx == 2) sel->textAlignH = TextAlignH::Right;
                    else               sel->textAlignH = TextAlignH::Left;
                }
                applyChange();
            }));
    }

    // V Align
    {
        int vAlignIndex = 0;
        if (sel->fillY) vAlignIndex = 3;
        else if (sel->textAlignV == TextAlignV::Center) vAlignIndex = 1;
        else if (sel->textAlignV == TextAlignV::Bottom) vAlignIndex = 2;

        children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".VAlign", "V Align",
            { "Top", "Center", "Bottom", "Fill" }, vAlignIndex,
            [sel, applyChange](int idx) {
                if (idx == 3) { sel->fillY = true; }
                else {
                    sel->fillY = false;
                    if (idx == 1)      sel->textAlignV = TextAlignV::Center;
                    else if (idx == 2) sel->textAlignV = TextAlignV::Bottom;
                    else               sel->textAlignV = TextAlignV::Top;
                }
                applyChange();
            }));
    }

    children.push_back(EditorUIBuilder::makeCheckBox(prefix + ".SizeToContent", "Size to Content",
        sel->sizeToContent,
        [sel, applyChange](bool c) { sel->sizeToContent = c; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Layout", "Layout", children));
}

void WidgetDetailSchema::addStyleColorsSection(const std::string& prefix,
                                               WidgetElement* sel,
                                               std::function<void()> applyChange,
                                               WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".Color", "Color",
        sel->style.color,
        [sel, applyChange](const Vec4& c) { sel->style.color = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".HoverCol", "Hover",
        sel->style.hoverColor,
        [sel, applyChange](const Vec4& c) { sel->style.hoverColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".PressedCol", "Pressed",
        sel->style.pressedColor,
        [sel, applyChange](const Vec4& c) { sel->style.pressedColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".DisabledCol", "Disabled",
        sel->style.disabledColor,
        [sel, applyChange](const Vec4& c) { sel->style.disabledColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".TextCol", "Text Color",
        sel->style.textColor,
        [sel, applyChange](const Vec4& c) { sel->style.textColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".TextHoverCol", "Text Hover",
        sel->style.textHoverColor,
        [sel, applyChange](const Vec4& c) { sel->style.textHoverColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeSliderRow(prefix + ".Opacity", "Opacity",
        sel->style.opacity, 0.0f, 1.0f,
        [sel, applyChange](float v) { sel->style.opacity = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeCheckBox(prefix + ".Visible", "Visible",
        sel->style.isVisible,
        [sel, applyChange](bool c) { sel->style.isVisible = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BorderW", "Border Width",
        sel->style.borderThickness,
        [sel, applyChange](float v) { sel->style.borderThickness = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BorderR", "Border Radius",
        sel->style.borderRadius,
        [sel, applyChange](float v) { sel->style.borderRadius = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".BorderCol", "Border Color",
        sel->style.borderColor,
        [sel, applyChange](const Vec4& c) { sel->style.borderColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".OutlineCol", "Outline",
        sel->style.outlineColor,
        [sel, applyChange](const Vec4& c) { sel->style.outlineColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".GradCol", "Gradient",
        sel->style.gradientColor,
        [sel, applyChange](const Vec4& c) { sel->style.gradientColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".Tooltip", "Tooltip",
        sel->tooltipText,
        [sel, applyChange](const std::string& v) { sel->tooltipText = v; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".StyleColors", "Style / Colors", children));
}

void WidgetDetailSchema::addBrushSection(const std::string& prefix,
                                         WidgetElement* sel,
                                         std::function<void()> applyChange,
                                         WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".BgBrush", "Bg Brush",
        { "None", "SolidColor", "Image", "NineSlice", "LinearGradient" },
        static_cast<int>(sel->background.type),
        [sel, applyChange](int idx) {
            sel->background.type = static_cast<BrushType>(idx);
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".BgCol", "Bg Color",
        sel->background.color,
        [sel, applyChange](const Vec4& c) { sel->background.color = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".BgEnd", "Bg End Color",
        sel->background.colorEnd,
        [sel, applyChange](const Vec4& c) { sel->background.colorEnd = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BgAngle", "Gradient Angle",
        sel->background.gradientAngle,
        [sel, applyChange](float v) { sel->background.gradientAngle = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".BgImage", "Bg Image",
        sel->background.imagePath,
        [sel, applyChange](const std::string& v) {
            sel->background.imagePath = v;
            sel->background.textureId = 0;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".ClipMode", "Clip Mode",
        { "None", "ClipToBounds", "InheritFromParent" },
        static_cast<int>(sel->clipMode),
        [sel, applyChange](int idx) {
            sel->clipMode = static_cast<ClipMode>(idx);
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Brush", "Brush", children));
}

void WidgetDetailSchema::addRenderTransformSection(const std::string& prefix,
                                                   WidgetElement* sel,
                                                   std::function<void()> applyChange,
                                                   WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".RT.Translate", "Translate",
        sel->renderTransform.translation,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->renderTransform.translation.x = v;
            else           sel->renderTransform.translation.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".RT.Rotation", "Rotation",
        sel->renderTransform.rotation,
        [sel, applyChange](float v) { sel->renderTransform.rotation = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".RT.Scale", "Scale",
        sel->renderTransform.scale,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->renderTransform.scale.x = v;
            else           sel->renderTransform.scale.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".RT.Shear", "Shear",
        sel->renderTransform.shear,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->renderTransform.shear.x = v;
            else           sel->renderTransform.shear.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".RT.Pivot", "Pivot",
        sel->renderTransform.pivot,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->renderTransform.pivot.x = v;
            else           sel->renderTransform.pivot.y = v;
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".RenderTransform", "Render Transform", children));
}

void WidgetDetailSchema::addShadowSection(const std::string& prefix,
                                          WidgetElement* sel,
                                          std::function<void()> applyChange,
                                          WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeIntRow(prefix + ".Elev", "Elevation (0-5)",
        sel->elevation,
        [sel, applyChange](int v) {
            sel->elevation = std::max(0, std::min(5, v));
            if (sel->elevation > 0)
            {
                const auto& theme = EditorTheme::Get();
                sel->style.applyElevation(sel->elevation, theme.shadowColor, theme.shadowOffset);
            }
            else
            {
                sel->style.shadowColor = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            }
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".ShadCol", "Shadow Color",
        sel->style.shadowColor,
        [sel, applyChange](const Vec4& c) { sel->style.shadowColor = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".ShadOff", "Shadow Offset",
        sel->style.shadowOffset,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->style.shadowOffset.x = v;
            else           sel->style.shadowOffset.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".ShadBlur", "Blur Radius",
        sel->style.shadowBlurRadius,
        [sel, applyChange](float v) { sel->style.shadowBlurRadius = std::max(0.0f, v); applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Shadow", "Shadow & Elevation", children));
}

// ── Per-type sections ───────────────────────────────────────────────────

void WidgetDetailSchema::addTextSection(const std::string& prefix,
                                        WidgetElement* sel,
                                        std::function<void()> applyChange,
                                        WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".Text", "Text",
        sel->text,
        [sel, applyChange](const std::string& v) { sel->text = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".Font", "Font",
        sel->font,
        [sel, applyChange](const std::string& v) { sel->font = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".FontSize", "Font Size",
        sel->fontSize,
        [sel, applyChange](float v) { sel->fontSize = std::max(1.0f, v); applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Text", "Text", children));
}

void WidgetDetailSchema::addImageSection(const std::string& prefix,
                                         WidgetElement* sel,
                                         std::function<void()> applyChange,
                                         WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".ImgSrc", "Image Source",
        sel->imagePath,
        [sel, applyChange](const std::string& v) {
            sel->imagePath = v;
            sel->textureId = 0;
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Image", "Image", children));
}

void WidgetDetailSchema::addValueSection(const std::string& prefix,
                                         WidgetElement* sel,
                                         std::function<void()> applyChange,
                                         WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeSliderRow(prefix + ".Value", "Value",
        sel->valueFloat, sel->minValue, sel->maxValue,
        [sel, applyChange](float v) { sel->valueFloat = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".MinVal", "Min Value",
        sel->minValue,
        [sel, applyChange](float v) { sel->minValue = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".MaxVal", "Max Value",
        sel->maxValue,
        [sel, applyChange](float v) { sel->maxValue = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".FillCol", "Fill Color",
        sel->style.fillColor,
        [sel, applyChange](const Vec4& c) { sel->style.fillColor = c; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Value", "Value", children));
}

void WidgetDetailSchema::addEntryBarSection(const std::string& prefix,
                                            WidgetElement* sel,
                                            std::function<void()> applyChange,
                                            WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".Value", "Value",
        sel->value,
        [sel, applyChange](const std::string& v) { sel->value = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeCheckBox(prefix + ".Password", "Password",
        sel->isPassword,
        [sel, applyChange](bool c) { sel->isPassword = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeCheckBox(prefix + ".Multiline", "Multiline",
        sel->isMultiline,
        [sel, applyChange](bool c) { sel->isMultiline = c; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".EntryBar", "Entry Bar", children));
}

void WidgetDetailSchema::addContainerSection(const std::string& prefix,
                                             WidgetElement* sel,
                                             std::function<void()> applyChange,
                                             WidgetElement* root)
{
    std::vector<WidgetElement> children;

    // Orientation (for StackPanel)
    if (sel->type == WidgetElementType::StackPanel)
    {
        children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".Orientation", "Orientation",
            { "Horizontal", "Vertical" },
            static_cast<int>(sel->orientation),
            [sel, applyChange](int idx) {
                sel->orientation = static_cast<StackOrientation>(idx);
                applyChange();
            }));
    }

    // Spacing
    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".Spacing", "Spacing",
        sel->spacing,
        [sel, applyChange](float v) { sel->spacing = v; applyChange(); }));

    // UniformGrid: columns / rows
    if (sel->type == WidgetElementType::UniformGrid)
    {
        children.push_back(EditorUIBuilder::makeIntRow(prefix + ".Columns", "Columns",
            sel->columns,
            [sel, applyChange](int v) { sel->columns = std::max(0, v); applyChange(); }));

        children.push_back(EditorUIBuilder::makeIntRow(prefix + ".Rows", "Rows",
            sel->rows,
            [sel, applyChange](int v) { sel->rows = std::max(0, v); applyChange(); }));
    }

    // SizeBox: overrides
    if (sel->type == WidgetElementType::SizeBox)
    {
        children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".WidthOvr", "Width Override",
            sel->widthOverride,
            [sel, applyChange](float v) { sel->widthOverride = std::max(0.0f, v); applyChange(); }));

        children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".HeightOvr", "Height Override",
            sel->heightOverride,
            [sel, applyChange](float v) { sel->heightOverride = std::max(0.0f, v); applyChange(); }));
    }

    // ScaleBox: scaleMode / userScale
    if (sel->type == WidgetElementType::ScaleBox)
    {
        children.push_back(EditorUIBuilder::makeDropDownRow(prefix + ".ScaleMode", "Scale Mode",
            { "Contain", "Cover", "Fill", "ScaleDown", "UserSpecified" },
            static_cast<int>(sel->scaleMode),
            [sel, applyChange](int idx) {
                sel->scaleMode = static_cast<ScaleMode>(idx);
                applyChange();
            }));

        children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".UserScale", "User Scale",
            sel->userScale,
            [sel, applyChange](float v) { sel->userScale = std::max(0.01f, v); applyChange(); }));
    }

    // WidgetSwitcher: active index
    if (sel->type == WidgetElementType::WidgetSwitcher)
    {
        children.push_back(EditorUIBuilder::makeIntRow(prefix + ".ActiveIdx", "Active Index",
            sel->activeChildIndex,
            [sel, applyChange](int v) { sel->activeChildIndex = std::max(0, v); applyChange(); }));
    }

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Container", "Container", children));
}

void WidgetDetailSchema::addBorderWidgetSection(const std::string& prefix,
                                                WidgetElement* sel,
                                                std::function<void()> applyChange,
                                                WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BdrL", "Border Left",
        sel->borderThicknessLeft,
        [sel, applyChange](float v) { sel->borderThicknessLeft = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BdrT", "Border Top",
        sel->borderThicknessTop,
        [sel, applyChange](float v) { sel->borderThicknessTop = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BdrR", "Border Right",
        sel->borderThicknessRight,
        [sel, applyChange](float v) { sel->borderThicknessRight = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".BdrB", "Border Bottom",
        sel->borderThicknessBottom,
        [sel, applyChange](float v) { sel->borderThicknessBottom = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeVec2Row(prefix + ".ContentPad", "Content Padding",
        sel->contentPadding,
        [sel, applyChange](int axis, float v) {
            if (axis == 0) sel->contentPadding.x = v; else sel->contentPadding.y = v;
            applyChange();
        }));

    children.push_back(EditorUIBuilder::makeColorPickerRow(prefix + ".BdrBrush", "Border Brush",
        sel->borderBrush.color,
        [sel, applyChange](const Vec4& c) { sel->borderBrush.color = c; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".BorderWidget", "Border Widget", children));
}

void WidgetDetailSchema::addSpinnerSection(const std::string& prefix,
                                           WidgetElement* sel,
                                           std::function<void()> applyChange,
                                           WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeIntRow(prefix + ".DotCount", "Dot Count",
        sel->spinnerDotCount,
        [sel, applyChange](int v) { sel->spinnerDotCount = std::max(1, v); applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".Speed", "Speed",
        sel->spinnerSpeed,
        [sel, applyChange](float v) { sel->spinnerSpeed = v; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Spinner", "Spinner", children));
}

void WidgetDetailSchema::addRichTextSection(const std::string& prefix,
                                            WidgetElement* sel,
                                            std::function<void()> applyChange,
                                            WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".RichTxt", "Rich Text",
        sel->richText,
        [sel, applyChange](const std::string& v) { sel->richText = v; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".RichText", "Rich Text", children));
}

void WidgetDetailSchema::addListViewSection(const std::string& prefix,
                                            WidgetElement* sel,
                                            std::function<void()> applyChange,
                                            WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeIntRow(prefix + ".ItemCount", "Item Count",
        sel->totalItemCount,
        [sel, applyChange](int v) { sel->totalItemCount = std::max(0, v); applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".ItemH", "Item Height",
        sel->itemHeight,
        [sel, applyChange](float v) { sel->itemHeight = v; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".ListView", "List View", children));
}

void WidgetDetailSchema::addTileViewSection(const std::string& prefix,
                                            WidgetElement* sel,
                                            std::function<void()> applyChange,
                                            WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeIntRow(prefix + ".ItemCount", "Item Count",
        sel->totalItemCount,
        [sel, applyChange](int v) { sel->totalItemCount = std::max(0, v); applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".ItemH", "Item Height",
        sel->itemHeight,
        [sel, applyChange](float v) { sel->itemHeight = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeFloatRow(prefix + ".ItemW", "Item Width",
        sel->itemWidth,
        [sel, applyChange](float v) { sel->itemWidth = v; applyChange(); }));

    children.push_back(EditorUIBuilder::makeIntRow(prefix + ".Cols", "Columns",
        sel->columnsPerRow,
        [sel, applyChange](int v) { sel->columnsPerRow = std::max(1, v); applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".TileView", "Tile View", children));
}

void WidgetDetailSchema::addFocusSection(const std::string& prefix,
                                         WidgetElement* sel,
                                         std::function<void()> applyChange,
                                         WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeCheckBox(prefix + ".Focusable", "Focusable",
        sel->hitTestMode == HitTestMode::Enabled,
        [sel, applyChange](bool c) {
            sel->hitTestMode = c ? HitTestMode::Enabled : HitTestMode::DisabledSelf;
            applyChange();
        }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".Focus", "Focus", children));
}

void WidgetDetailSchema::addDragDropSection(const std::string& prefix,
                                            WidgetElement* sel,
                                            std::function<void()> applyChange,
                                            WidgetElement* root)
{
    std::vector<WidgetElement> children;

    children.push_back(EditorUIBuilder::makeCheckBox(prefix + ".Draggable", "Draggable",
        sel->isDraggable,
        [sel, applyChange](bool c) { sel->isDraggable = c; applyChange(); }));

    children.push_back(EditorUIBuilder::makeStringRow(prefix + ".DragPayload", "Drag Payload",
        sel->dragPayload,
        [sel, applyChange](const std::string& v) { sel->dragPayload = v; applyChange(); }));

    root->children.push_back(EditorUIBuilder::makeSection(prefix + ".DragDrop", "Drag & Drop", children));
}

// ── Top-level builder ───────────────────────────────────────────────────

void WidgetDetailSchema::buildDetailPanel(const std::string& prefix,
                                          WidgetElement* selected,
                                          std::function<void()> applyChange,
                                          WidgetElement* rootPanel,
                                          const Options& options)
{
    if (!selected || !rootPanel)
        return;

    // ── Shared sections (all element types) ──────────────────────────────
    addIdentitySection(prefix, selected, applyChange, rootPanel, options);
    addTransformSection(prefix, selected, applyChange, rootPanel);
    addAnchorSection(prefix, selected, applyChange, rootPanel);
    addHitTestSection(prefix, selected, applyChange, rootPanel);
    addLayoutSection(prefix, selected, applyChange, rootPanel);
    addStyleColorsSection(prefix, selected, applyChange, rootPanel);
    addBrushSection(prefix, selected, applyChange, rootPanel);
    addRenderTransformSection(prefix, selected, applyChange, rootPanel);
    addShadowSection(prefix, selected, applyChange, rootPanel);

    // ── Per-type sections ────────────────────────────────────────────────
    const auto type = selected->type;

    if (hasTextProperties(type))
        addTextSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::Image)
        addImageSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::Slider || type == WidgetElementType::ProgressBar)
        addValueSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::EntryBar)
        addEntryBarSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::StackPanel || type == WidgetElementType::ScrollView
        || type == WidgetElementType::WrapBox || type == WidgetElementType::UniformGrid
        || type == WidgetElementType::SizeBox || type == WidgetElementType::ScaleBox
        || type == WidgetElementType::WidgetSwitcher)
        addContainerSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::Border)
        addBorderWidgetSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::Spinner)
        addSpinnerSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::RichText)
        addRichTextSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::ListView)
        addListViewSection(prefix, selected, applyChange, rootPanel);

    if (type == WidgetElementType::TileView)
        addTileViewSection(prefix, selected, applyChange, rootPanel);

    // Drag & Drop for all types
    addDragDropSection(prefix, selected, applyChange, rootPanel);

    // ── Optional: Delete button ──────────────────────────────────────────
    if (options.showDeleteButton && options.onDelete)
    {
        rootPanel->children.push_back(EditorUIBuilder::makeDivider());
        rootPanel->children.push_back(EditorUIBuilder::makeDangerButton(
            prefix + ".Delete", "Delete Element", options.onDelete));
    }
}
