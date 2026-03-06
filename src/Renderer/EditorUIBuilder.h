#pragma once

#include <string>
#include <vector>
#include <functional>

#include "../Core/MathTypes.h"
#include "UIWidget.h"
#include "EditorTheme.h"

/// Static factory helpers that produce themed WidgetElement objects for the
/// editor UI.  Every element is marked `runtimeOnly = true` and uses colours /
/// fonts from `EditorTheme::Get()` so the whole editor can be restyled in one
/// place.
///
/// **Viewport / gameplay UI** should **not** use this class – use
/// ViewportUITheme instead.
class EditorUIBuilder
{
public:
    // ── Text / labels ────────────────────────────────────────────────────

    /// Single-line text (body font, primary text colour).
    static WidgetElement makeLabel(const std::string& text,
                                   float minWidth = 0.0f);

    /// Secondary / hint label (smaller font, muted colour).
    static WidgetElement makeSecondaryLabel(const std::string& text,
                                            float minWidth = 0.0f);

    /// Section heading text (sub-heading font, primary text colour).
    static WidgetElement makeHeading(const std::string& text);

    // ── Buttons ──────────────────────────────────────────────────────────

    /// Default editor button (neutral colours).
    static WidgetElement makeButton(const std::string& id,
                                    const std::string& text,
                                    std::function<void()> onClick = {},
                                    Vec2 minSize = {});

    /// Primary / accent button (blue).
    static WidgetElement makePrimaryButton(const std::string& id,
                                           const std::string& text,
                                           std::function<void()> onClick = {},
                                           Vec2 minSize = {});

    /// Danger / destructive button (red).
    static WidgetElement makeDangerButton(const std::string& id,
                                          const std::string& text,
                                          std::function<void()> onClick = {},
                                          Vec2 minSize = {});

    /// Subtle / transparent button (toolbar-style).
    static WidgetElement makeSubtleButton(const std::string& id,
                                          const std::string& text,
                                          std::function<void()> onClick = {},
                                          Vec2 minSize = {});

    // ── Input controls ───────────────────────────────────────────────────

    /// Themed EntryBar (single-line text input).
    static WidgetElement makeEntryBar(const std::string& id,
                                      const std::string& value,
                                      std::function<void(const std::string&)> onValueChanged = {},
                                      float minWidth = 0.0f);

    /// Themed CheckBox row (label + checkbox).
    static WidgetElement makeCheckBox(const std::string& id,
                                      const std::string& label,
                                      bool checked,
                                      std::function<void(bool)> onCheckedChanged = {});

    /// Themed DropDown (combo-box).
    static WidgetElement makeDropDown(const std::string& id,
                                      const std::vector<std::string>& items,
                                      int selectedIndex,
                                      std::function<void(int)> onSelectionChanged = {});

    // ── Property rows ────────────────────────────────────────────────────

    /// Label + single float entry bar in a horizontal row.
    static WidgetElement makeFloatRow(const std::string& id,
                                      const std::string& label,
                                      float value,
                                      std::function<void(float)> onChange);

    /// Label + 3 colour-tinted entry bars (X / Y / Z) in a horizontal row.
    static WidgetElement makeVec3Row(const std::string& idPrefix,
                                     const std::string& label,
                                     const float values[3],
                                     std::function<void(int axis, float value)> onChange);

    // ── Layout helpers ───────────────────────────────────────────────────

    /// Transparent horizontal StackPanel.
    static WidgetElement makeHorizontalRow(const std::string& id = {},
                                           Vec2 padding = {});

    /// Transparent vertical StackPanel.
    static WidgetElement makeVerticalStack(const std::string& id = {},
                                           Vec2 padding = {});

    /// Thin horizontal divider line.
    static WidgetElement makeDivider();

    // ── Collapsible section ──────────────────────────────────────────────

    /// Themed separator / collapsible section header + content area.
    static WidgetElement makeSection(const std::string& id,
                                     const std::string& title,
                                     const std::vector<WidgetElement>& children);

    // ── Utility ──────────────────────────────────────────────────────────

    /// Format a float to a short string (2 decimal places).
    static std::string fmtFloat(float v);

    /// Sanitise text into a safe element ID (alphanumeric + underscores).
    static std::string sanitizeId(const std::string& text);
};
