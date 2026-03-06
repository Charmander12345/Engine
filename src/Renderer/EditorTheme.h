#pragma once

#include "../Core/MathTypes.h"

/// Centralized editor theme — all editor UI colors, fonts, and spacing live here.
/// Change values in one place to restyle the entire editor.
///
/// **Viewport/gameplay UI** has its own theme (`ViewportUITheme`) so game
/// developers can customise the in-game look without touching the editor.
struct EditorTheme
{
    // ── Window / chrome ──────────────────────────────────────────────────
    Vec4 windowBackground     { 0.11f, 0.11f, 0.14f, 1.0f };   // main window bg
    Vec4 titleBarBackground   { 0.09f, 0.09f, 0.12f, 1.0f };   // title bar strip
    Vec4 titleBarText         { 0.82f, 0.84f, 0.90f, 1.0f };

    // ── Panels ───────────────────────────────────────────────────────────
    Vec4 panelBackground      { 0.13f, 0.14f, 0.17f, 1.0f };   // side-panels (outliner, details, content browser)
    Vec4 panelBackgroundAlt   { 0.11f, 0.12f, 0.15f, 1.0f };   // alternating row / secondary panel bg
    Vec4 panelHeader          { 0.16f, 0.17f, 0.21f, 1.0f };   // section header bg
    Vec4 panelBorder          { 0.20f, 0.21f, 0.26f, 0.6f };   // subtle border / separator

    // ── Buttons ──────────────────────────────────────────────────────────
    Vec4 buttonDefault        { 0.16f, 0.17f, 0.21f, 1.0f };
    Vec4 buttonHover          { 0.22f, 0.24f, 0.30f, 1.0f };
    Vec4 buttonPressed        { 0.12f, 0.13f, 0.17f, 1.0f };
    Vec4 buttonText           { 0.92f, 0.93f, 0.96f, 1.0f };

    // Primary action (save, confirm)
    Vec4 buttonPrimary        { 0.20f, 0.45f, 0.78f, 1.0f };
    Vec4 buttonPrimaryHover   { 0.26f, 0.52f, 0.88f, 1.0f };
    Vec4 buttonPrimaryText    { 0.96f, 0.97f, 1.00f, 1.0f };

    // Danger / destructive (delete, remove)
    Vec4 buttonDanger         { 0.62f, 0.18f, 0.18f, 1.0f };
    Vec4 buttonDangerHover    { 0.76f, 0.24f, 0.24f, 1.0f };
    Vec4 buttonDangerText     { 0.96f, 0.85f, 0.85f, 1.0f };

    // Subtle / transparent buttons (toolbar icons, palette entries)
    Vec4 buttonSubtle         { 0.0f,  0.0f,  0.0f,  0.0f };
    Vec4 buttonSubtleHover    { 0.22f, 0.24f, 0.30f, 0.75f };

    // ── Text ─────────────────────────────────────────────────────────────
    Vec4 textPrimary          { 0.92f, 0.93f, 0.96f, 1.0f };   // main text
    Vec4 textSecondary        { 0.62f, 0.64f, 0.70f, 1.0f };   // labels, hints
    Vec4 textMuted            { 0.45f, 0.47f, 0.52f, 1.0f };   // disabled / placeholder
    Vec4 textLink             { 0.40f, 0.65f, 0.95f, 1.0f };   // hyperlink / accent text

    // ── Input controls ───────────────────────────────────────────────────
    Vec4 inputBackground      { 0.09f, 0.10f, 0.13f, 0.90f };
    Vec4 inputBackgroundHover { 0.12f, 0.13f, 0.17f, 0.95f };
    Vec4 inputBorder          { 0.22f, 0.23f, 0.28f, 0.8f };
    Vec4 inputText            { 0.90f, 0.92f, 0.96f, 1.0f };
    Vec4 inputPlaceholder     { 0.42f, 0.44f, 0.50f, 1.0f };

    Vec4 checkboxDefault      { 0.16f, 0.17f, 0.21f, 1.0f };
    Vec4 checkboxHover        { 0.22f, 0.24f, 0.30f, 1.0f };
    Vec4 checkboxChecked      { 0.25f, 0.52f, 0.88f, 1.0f };   // accent blue
    Vec4 checkboxText         { 0.90f, 0.90f, 0.94f, 1.0f };

    Vec4 dropdownBackground   { 0.14f, 0.15f, 0.19f, 0.95f };
    Vec4 dropdownHover        { 0.22f, 0.26f, 0.34f, 1.0f };
    Vec4 dropdownText         { 0.90f, 0.92f, 0.96f, 1.0f };

    Vec4 sliderTrack          { 0.18f, 0.19f, 0.24f, 1.0f };
    Vec4 sliderFill           { 0.25f, 0.52f, 0.88f, 1.0f };
    Vec4 sliderThumb          { 0.90f, 0.92f, 0.96f, 1.0f };

    // ── Accent / selection ───────────────────────────────────────────────
    Vec4 accent               { 0.25f, 0.52f, 0.88f, 1.0f };   // main accent (blue)
    Vec4 accentHover          { 0.30f, 0.58f, 0.95f, 1.0f };
    Vec4 accentMuted          { 0.20f, 0.38f, 0.65f, 0.6f };
    Vec4 accentGreen          { 0.50f, 0.78f, 0.50f, 1.0f };   // green accent (add buttons)
    Vec4 selectionHighlight   { 0.25f, 0.52f, 0.88f, 0.25f };  // row selection bg
    Vec4 selectionHighlightHover { 0.30f, 0.58f, 0.92f, 0.35f }; // row selection hover

    Vec4 warningColor         { 0.92f, 0.72f, 0.20f, 1.0f };   // yellow warning
    Vec4 errorColor           { 0.88f, 0.25f, 0.25f, 1.0f };   // red error
    Vec4 successColor         { 0.28f, 0.72f, 0.40f, 1.0f };   // green success

    // ── Modal / overlay ──────────────────────────────────────────────────
    Vec4 modalOverlay         { 0.0f,  0.0f,  0.0f,  0.55f };
    Vec4 modalBackground      { 0.14f, 0.15f, 0.19f, 0.97f };
    Vec4 modalText            { 0.92f, 0.93f, 0.96f, 1.0f };

    // ── Toast notifications ──────────────────────────────────────────────
    Vec4 toastBackground      { 0.13f, 0.14f, 0.18f, 0.92f };
    Vec4 toastText            { 0.90f, 0.92f, 0.96f, 1.0f };

    // ── Scrollbar ────────────────────────────────────────────────────────
    Vec4 scrollbarTrack       { 0.10f, 0.11f, 0.14f, 0.5f };
    Vec4 scrollbarThumb       { 0.28f, 0.30f, 0.36f, 0.7f };
    Vec4 scrollbarThumbHover  { 0.36f, 0.38f, 0.44f, 0.9f };

    // ── Tree view ────────────────────────────────────────────────────────
    Vec4 treeRowEven          { 0.13f, 0.14f, 0.17f, 1.0f };
    Vec4 treeRowOdd           { 0.11f, 0.12f, 0.15f, 1.0f };
    Vec4 treeRowHover         { 0.18f, 0.20f, 0.26f, 0.8f };
    Vec4 treeRowSelected      { 0.20f, 0.38f, 0.65f, 0.5f };

    // ── Content browser ──────────────────────────────────────────────────
    Vec4 cbTileBackground     { 0.14f, 0.15f, 0.19f, 1.0f };
    Vec4 cbTileHover          { 0.20f, 0.22f, 0.28f, 1.0f };
    Vec4 cbTileSelected       { 0.20f, 0.38f, 0.65f, 0.6f };
    Vec4 cbFolderAccent       { 0.20f, 0.25f, 0.35f, 0.9f };

    // ── Timeline / animation editor ──────────────────────────────────────
    Vec4 timelineBackground   { 0.10f, 0.11f, 0.14f, 1.0f };
    Vec4 timelineRuler        { 0.06f, 0.07f, 0.10f, 1.0f };
    Vec4 timelineRulerText    { 0.50f, 0.50f, 0.55f, 1.0f };
    Vec4 timelineLaneEven     { 0.10f, 0.11f, 0.14f, 0.5f };
    Vec4 timelineLaneOdd      { 0.08f, 0.09f, 0.12f, 0.5f };
    Vec4 timelineKeyframe     { 0.95f, 0.75f, 0.15f, 1.0f };   // gold
    Vec4 timelineKeyframeHover{ 1.00f, 0.90f, 0.40f, 1.0f };
    Vec4 tlKeyframeDiamond    { 0.90f, 0.70f, 0.20f, 1.0f };    // diamond marker in left panel
    Vec4 timelineScrubber     { 1.00f, 0.60f, 0.10f, 0.9f };   // orange
    Vec4 timelineEndLine      { 0.90f, 0.20f, 0.20f, 0.8f };   // red

    // ── Status bar ───────────────────────────────────────────────────────
    Vec4 statusBarBackground  { 0.09f, 0.09f, 0.12f, 1.0f };
    Vec4 statusBarText        { 0.60f, 0.62f, 0.68f, 1.0f };

    // ── Transparent (helper) ─────────────────────────────────────────────
    Vec4 transparent          { 0.0f,  0.0f,  0.0f,  0.0f };

    // ── Fonts ────────────────────────────────────────────────────────────
    const char* fontDefault   = "default.ttf";

    float fontSizeHeading     = 16.0f;
    float fontSizeSubheading  = 14.0f;
    float fontSizeBody        = 13.0f;
    float fontSizeSmall       = 11.0f;
    float fontSizeCaption     = 10.0f;
    float fontSizeMonospace   = 12.0f;

    // ── Spacing / sizing ─────────────────────────────────────────────────
    float rowHeight           = 24.0f;
    float rowHeightSmall      = 20.0f;
    float rowHeightLarge      = 28.0f;
    float sectionHeaderHeight = 26.0f;
    float toolbarHeight       = 32.0f;

    Vec2 paddingSmall         { 4.0f,  2.0f };
    Vec2 paddingNormal        { 6.0f,  4.0f };
    Vec2 paddingLarge         { 10.0f, 6.0f };
    Vec2 paddingSection       { 8.0f,  4.0f };

    float indentSize          = 16.0f;   // tree-view indent per level
    float iconSize            = 16.0f;   // small icon (tree, list)
    float iconSizeLarge       = 24.0f;   // toolbar icon

    float borderRadius        = 3.0f;
    float separatorThickness  = 1.0f;

    // ── Singleton accessor ───────────────────────────────────────────────
    static EditorTheme& Get()
    {
        static EditorTheme s_theme;
        return s_theme;
    }
};
