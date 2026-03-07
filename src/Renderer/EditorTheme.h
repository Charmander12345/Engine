#pragma once

#include "../Core/MathTypes.h"
#include "../Renderer/UIWidget.h"
#include <string>
#include <fstream>
#include <filesystem>
#include "../AssetManager/json.hpp"
#include <cmath>

/// Centralized editor theme
/// Change values in one place to restyle the entire editor.
///
/// **Viewport/gameplay UI** has its own theme (`ViewportUITheme`) so game
/// developers can customise the in-game look without touching the editor.
struct EditorTheme
{
    // ── Window / chrome ──────────────────────────────────────────────────
    Vec4 windowBackground     { 0.05f, 0.05f, 0.05f, 1.0f };   // main window bg
    Vec4 titleBarBackground   { 0.04f, 0.04f, 0.04f, 1.0f };   // title bar strip
    Vec4 titleBarText         { 0.95f, 0.95f, 0.95f, 1.0f };

    // ── Panels ───────────────────────────────────────────────────────────
    Vec4 panelBackground      { 0.07f, 0.07f, 0.07f, 1.0f };   // side-panels (outliner, details, content browser)
    Vec4 panelBackgroundAlt   { 0.06f, 0.06f, 0.06f, 1.0f };   // alternating row / secondary panel bg
    Vec4 panelHeader          { 0.09f, 0.09f, 0.09f, 1.0f };   // section header bg
    Vec4 panelBorder          { 0.14f, 0.14f, 0.14f, 0.6f };   // subtle border / separator

    // ── Buttons ──────────────────────────────────────────────────────────
    Vec4 buttonDefault        { 0.11f, 0.11f, 0.11f, 1.0f };
    Vec4 buttonHover          { 0.16f, 0.16f, 0.16f, 1.0f };
    Vec4 buttonPressed        { 0.08f, 0.08f, 0.08f, 1.0f };
    Vec4 buttonText           { 0.95f, 0.95f, 0.95f, 1.0f };

    // Primary action (save, confirm)
    Vec4 buttonPrimary        { 0.16f, 0.38f, 0.72f, 1.0f };
    Vec4 buttonPrimaryHover   { 0.22f, 0.46f, 0.82f, 1.0f };
    Vec4 buttonPrimaryText    { 1.00f, 1.00f, 1.00f, 1.0f };

    // Danger / destructive (delete, remove)
    Vec4 buttonDanger         { 0.55f, 0.14f, 0.14f, 1.0f };
    Vec4 buttonDangerHover    { 0.70f, 0.20f, 0.20f, 1.0f };
    Vec4 buttonDangerText     { 1.00f, 1.00f, 1.00f, 1.0f };

    // Subtle / transparent buttons (toolbar icons, palette entries)
    Vec4 buttonSubtle         { 0.0f,  0.0f,  0.0f,  0.0f };
    Vec4 buttonSubtleHover    { 0.14f, 0.14f, 0.14f, 0.75f };

    // ── Text ─────────────────────────────────────────────────────────────
    Vec4 textPrimary          { 0.95f, 0.95f, 0.95f, 1.0f };   // main text (white)
    Vec4 textSecondary        { 0.70f, 0.70f, 0.70f, 1.0f };   // labels, hints
    Vec4 textMuted            { 0.45f, 0.45f, 0.45f, 1.0f };   // disabled / placeholder
    Vec4 textLink             { 0.45f, 0.68f, 0.98f, 1.0f };   // hyperlink / accent text

    // ── Input controls ───────────────────────────────────────────────────
    Vec4 inputBackground      { 0.04f, 0.04f, 0.04f, 0.95f };
    Vec4 inputBackgroundHover { 0.07f, 0.07f, 0.07f, 0.98f };
    Vec4 inputBorder          { 0.16f, 0.16f, 0.16f, 0.8f };
    Vec4 inputText            { 0.95f, 0.95f, 0.95f, 1.0f };
    Vec4 inputPlaceholder     { 0.40f, 0.40f, 0.40f, 1.0f };

    Vec4 checkboxDefault      { 0.11f, 0.11f, 0.11f, 1.0f };
    Vec4 checkboxHover        { 0.16f, 0.16f, 0.16f, 1.0f };
    Vec4 checkboxChecked      { 0.20f, 0.48f, 0.85f, 1.0f };   // accent blue
    Vec4 checkboxText         { 0.95f, 0.95f, 0.95f, 1.0f };

    Vec4 dropdownBackground   { 0.07f, 0.07f, 0.07f, 0.98f };
    Vec4 dropdownHover        { 0.14f, 0.14f, 0.16f, 1.0f };
    Vec4 dropdownText         { 0.95f, 0.95f, 0.95f, 1.0f };

    Vec4 sliderTrack          { 0.10f, 0.10f, 0.10f, 1.0f };
    Vec4 sliderFill           { 0.20f, 0.48f, 0.85f, 1.0f };
    Vec4 sliderThumb          { 0.95f, 0.95f, 0.95f, 1.0f };

    // ── Accent / selection ───────────────────────────────────────────────
    Vec4 accent               { 0.20f, 0.48f, 0.85f, 1.0f };   // main accent (blue)
    Vec4 accentHover          { 0.26f, 0.54f, 0.92f, 1.0f };
    Vec4 accentMuted          { 0.16f, 0.32f, 0.58f, 0.6f };
    Vec4 accentGreen          { 0.35f, 0.68f, 0.38f, 1.0f };   // green accent (add buttons)
    Vec4 selectionHighlight   { 0.20f, 0.48f, 0.85f, 0.20f };  // row selection bg
    Vec4 selectionHighlightHover { 0.26f, 0.54f, 0.90f, 0.30f }; // row selection hover

    Vec4 warningColor         { 0.92f, 0.72f, 0.20f, 1.0f };   // yellow warning
    Vec4 errorColor           { 0.88f, 0.22f, 0.22f, 1.0f };   // red error
    Vec4 successColor         { 0.26f, 0.70f, 0.38f, 1.0f };   // green success

    // ── Modal / overlay ──────────────────────────────────────────────────
    Vec4 modalOverlay         { 0.0f,  0.0f,  0.0f,  0.65f };
    Vec4 modalBackground      { 0.07f, 0.07f, 0.07f, 0.98f };
    Vec4 modalText            { 0.95f, 0.95f, 0.95f, 1.0f };

    // ── Toast notifications ──────────────────────────────────────────────
    Vec4 toastBackground      { 0.07f, 0.07f, 0.07f, 0.95f };
    Vec4 toastText            { 0.95f, 0.95f, 0.95f, 1.0f };

    // ── Scrollbar ────────────────────────────────────────────────────────
    Vec4 scrollbarTrack       { 0.04f, 0.04f, 0.04f, 0.5f };
    Vec4 scrollbarThumb       { 0.20f, 0.20f, 0.20f, 0.7f };
    Vec4 scrollbarThumbHover  { 0.28f, 0.28f, 0.28f, 0.9f };

    // ── Tree view ────────────────────────────────────────────────────────
    Vec4 treeRowEven          { 0.07f, 0.07f, 0.07f, 1.0f };
    Vec4 treeRowOdd           { 0.05f, 0.05f, 0.05f, 1.0f };
    Vec4 treeRowHover         { 0.12f, 0.12f, 0.14f, 0.8f };
    Vec4 treeRowSelected      { 0.16f, 0.32f, 0.58f, 0.5f };

    // ── Content browser ──────────────────────────────────────────────────
    Vec4 cbTileBackground     { 0.07f, 0.07f, 0.07f, 1.0f };
    Vec4 cbTileHover          { 0.12f, 0.12f, 0.14f, 1.0f };
    Vec4 cbTileSelected       { 0.16f, 0.32f, 0.58f, 0.6f };
    Vec4 cbFolderAccent       { 0.12f, 0.16f, 0.22f, 0.9f };

    // ── Timeline / animation editor ──────────────────────────────────────
    Vec4 timelineBackground   { 0.04f, 0.04f, 0.04f, 1.0f };
    Vec4 timelineRuler        { 0.03f, 0.03f, 0.03f, 1.0f };
    Vec4 timelineRulerText    { 0.50f, 0.50f, 0.50f, 1.0f };
    Vec4 timelineLaneEven     { 0.06f, 0.06f, 0.06f, 0.5f };
    Vec4 timelineLaneOdd      { 0.04f, 0.04f, 0.04f, 0.5f };
    Vec4 timelineKeyframe     { 0.95f, 0.75f, 0.15f, 1.0f };   // gold
    Vec4 timelineKeyframeHover{ 1.00f, 0.90f, 0.40f, 1.0f };
    Vec4 tlKeyframeDiamond    { 0.90f, 0.70f, 0.20f, 1.0f };    // diamond marker in left panel
    Vec4 timelineScrubber     { 1.00f, 0.60f, 0.10f, 0.9f };   // orange
    Vec4 timelineEndLine      { 0.90f, 0.20f, 0.20f, 0.8f };   // red

    // ── Status bar ───────────────────────────────────────────────────────
    Vec4 statusBarBackground  { 0.04f, 0.04f, 0.04f, 1.0f };
    Vec4 statusBarText        { 0.70f, 0.70f, 0.70f, 1.0f };

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

    // ── DPI scaling ──────────────────────────────────────────────────────
    /// Current DPI scale factor applied to all sizing values.
    /// 1.0 = 96 DPI (100%), 1.25 = 120 DPI (125%), 1.5 = 144 DPI (150%) etc.
    float dpiScale            = 1.0f;

    /// Change the DPI scale: rescales every font-size, spacing, padding,
    /// and icon-size field from the current scale to @p newScale.
    void applyDpiScale(float newScale)
    {
        if (newScale <= 0.0f) newScale = 1.0f;
        if (std::abs(newScale - dpiScale) < 0.001f) return;

        const float ratio = newScale / dpiScale;
        dpiScale = newScale;

        // Fonts
        fontSizeHeading    *= ratio;
        fontSizeSubheading *= ratio;
        fontSizeBody       *= ratio;
        fontSizeSmall      *= ratio;
        fontSizeCaption    *= ratio;
        fontSizeMonospace  *= ratio;

        // Row / toolbar heights
        rowHeight          *= ratio;
        rowHeightSmall     *= ratio;
        rowHeightLarge     *= ratio;
        sectionHeaderHeight *= ratio;
        toolbarHeight      *= ratio;

        // Padding
        paddingSmall   = { paddingSmall.x   * ratio, paddingSmall.y   * ratio };
        paddingNormal  = { paddingNormal.x  * ratio, paddingNormal.y  * ratio };
        paddingLarge   = { paddingLarge.x   * ratio, paddingLarge.y   * ratio };
        paddingSection = { paddingSection.x * ratio, paddingSection.y * ratio };

        // Icons / indent
        indentSize     *= ratio;
        iconSize       *= ratio;
        iconSizeLarge  *= ratio;

        // Geometry
        borderRadius       *= ratio;
        separatorThickness *= ratio;
    }

    /// Scale an arbitrary pixel value by the current DPI factor.
    /// Use for hardcoded sizes that don't have a dedicated theme field.
    static float Scaled(float px) { return px * Get().dpiScale; }
    static Vec2  Scaled(Vec2 v)   { return { v.x * Get().dpiScale, v.y * Get().dpiScale }; }

    // ── Theme identity ───────────────────────────────────────────────────
    std::string themeName = "Dark";

    // ── JSON serialization ───────────────────────────────────────────────
    nlohmann::json toJson() const
    {
        using json = nlohmann::json;
        auto v4 = [](const Vec4& c) { return json::array({ c.x, c.y, c.z, c.w }); };
        auto v2 = [](const Vec2& c) { return json::array({ c.x, c.y }); };

        json j;
        j["themeName"] = themeName;

        // Window / chrome
        j["windowBackground"]     = v4(windowBackground);
        j["titleBarBackground"]   = v4(titleBarBackground);
        j["titleBarText"]         = v4(titleBarText);
        // Panels
        j["panelBackground"]      = v4(panelBackground);
        j["panelBackgroundAlt"]   = v4(panelBackgroundAlt);
        j["panelHeader"]          = v4(panelHeader);
        j["panelBorder"]          = v4(panelBorder);
        // Buttons
        j["buttonDefault"]        = v4(buttonDefault);
        j["buttonHover"]          = v4(buttonHover);
        j["buttonPressed"]        = v4(buttonPressed);
        j["buttonText"]           = v4(buttonText);
        j["buttonPrimary"]        = v4(buttonPrimary);
        j["buttonPrimaryHover"]   = v4(buttonPrimaryHover);
        j["buttonPrimaryText"]    = v4(buttonPrimaryText);
        j["buttonDanger"]         = v4(buttonDanger);
        j["buttonDangerHover"]    = v4(buttonDangerHover);
        j["buttonDangerText"]     = v4(buttonDangerText);
        j["buttonSubtle"]         = v4(buttonSubtle);
        j["buttonSubtleHover"]    = v4(buttonSubtleHover);
        // Text
        j["textPrimary"]          = v4(textPrimary);
        j["textSecondary"]        = v4(textSecondary);
        j["textMuted"]            = v4(textMuted);
        j["textLink"]             = v4(textLink);
        // Input
        j["inputBackground"]      = v4(inputBackground);
        j["inputBackgroundHover"] = v4(inputBackgroundHover);
        j["inputBorder"]          = v4(inputBorder);
        j["inputText"]            = v4(inputText);
        j["inputPlaceholder"]     = v4(inputPlaceholder);
        j["checkboxDefault"]      = v4(checkboxDefault);
        j["checkboxHover"]        = v4(checkboxHover);
        j["checkboxChecked"]      = v4(checkboxChecked);
        j["checkboxText"]         = v4(checkboxText);
        j["dropdownBackground"]   = v4(dropdownBackground);
        j["dropdownHover"]        = v4(dropdownHover);
        j["dropdownText"]         = v4(dropdownText);
        j["sliderTrack"]          = v4(sliderTrack);
        j["sliderFill"]           = v4(sliderFill);
        j["sliderThumb"]          = v4(sliderThumb);
        // Accent / selection
        j["accent"]               = v4(accent);
        j["accentHover"]          = v4(accentHover);
        j["accentMuted"]          = v4(accentMuted);
        j["accentGreen"]          = v4(accentGreen);
        j["selectionHighlight"]   = v4(selectionHighlight);
        j["selectionHighlightHover"] = v4(selectionHighlightHover);
        j["warningColor"]         = v4(warningColor);
        j["errorColor"]           = v4(errorColor);
        j["successColor"]         = v4(successColor);
        // Modal / overlay
        j["modalOverlay"]         = v4(modalOverlay);
        j["modalBackground"]      = v4(modalBackground);
        j["modalText"]            = v4(modalText);
        // Toast
        j["toastBackground"]      = v4(toastBackground);
        j["toastText"]            = v4(toastText);
        // Scrollbar
        j["scrollbarTrack"]       = v4(scrollbarTrack);
        j["scrollbarThumb"]       = v4(scrollbarThumb);
        j["scrollbarThumbHover"]  = v4(scrollbarThumbHover);
        // Tree view
        j["treeRowEven"]          = v4(treeRowEven);
        j["treeRowOdd"]           = v4(treeRowOdd);
        j["treeRowHover"]         = v4(treeRowHover);
        j["treeRowSelected"]      = v4(treeRowSelected);
        // Content browser
        j["cbTileBackground"]     = v4(cbTileBackground);
        j["cbTileHover"]          = v4(cbTileHover);
        j["cbTileSelected"]       = v4(cbTileSelected);
        j["cbFolderAccent"]       = v4(cbFolderAccent);
        // Timeline
        j["timelineBackground"]   = v4(timelineBackground);
        j["timelineRuler"]        = v4(timelineRuler);
        j["timelineRulerText"]    = v4(timelineRulerText);
        j["timelineLaneEven"]     = v4(timelineLaneEven);
        j["timelineLaneOdd"]      = v4(timelineLaneOdd);
        j["timelineKeyframe"]     = v4(timelineKeyframe);
        j["timelineKeyframeHover"]= v4(timelineKeyframeHover);
        j["tlKeyframeDiamond"]    = v4(tlKeyframeDiamond);
        j["timelineScrubber"]     = v4(timelineScrubber);
        j["timelineEndLine"]      = v4(timelineEndLine);
        // Status bar
        j["statusBarBackground"]  = v4(statusBarBackground);
        j["statusBarText"]        = v4(statusBarText);
        // Transparent
        j["transparent"]          = v4(transparent);
        // Fonts — save base (unscaled) values so theme files stay DPI-independent
        const float inv = (dpiScale > 0.0f) ? (1.0f / dpiScale) : 1.0f;
        j["fontDefault"]          = std::string(fontDefault);
        j["fontSizeHeading"]      = fontSizeHeading    * inv;
        j["fontSizeSubheading"]   = fontSizeSubheading * inv;
        j["fontSizeBody"]         = fontSizeBody       * inv;
        j["fontSizeSmall"]        = fontSizeSmall      * inv;
        j["fontSizeCaption"]      = fontSizeCaption    * inv;
        j["fontSizeMonospace"]    = fontSizeMonospace  * inv;
        // Spacing / sizing
        j["rowHeight"]            = rowHeight          * inv;
        j["rowHeightSmall"]       = rowHeightSmall     * inv;
        j["rowHeightLarge"]       = rowHeightLarge     * inv;
        j["sectionHeaderHeight"]  = sectionHeaderHeight * inv;
        j["toolbarHeight"]        = toolbarHeight      * inv;
        j["paddingSmall"]         = v2({ paddingSmall.x   * inv, paddingSmall.y   * inv });
        j["paddingNormal"]        = v2({ paddingNormal.x  * inv, paddingNormal.y  * inv });
        j["paddingLarge"]         = v2({ paddingLarge.x   * inv, paddingLarge.y   * inv });
        j["paddingSection"]       = v2({ paddingSection.x * inv, paddingSection.y * inv });
        j["indentSize"]           = indentSize         * inv;
        j["iconSize"]             = iconSize           * inv;
        j["iconSizeLarge"]        = iconSizeLarge      * inv;
        j["borderRadius"]         = borderRadius       * inv;
        j["separatorThickness"]   = separatorThickness * inv;

        return j;
    }

    void fromJson(const nlohmann::json& j)
    {
        auto rv4 = [&](const char* key, Vec4& dst)
        {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 4)
            {
                dst.x = j[key][0].get<float>();
                dst.y = j[key][1].get<float>();
                dst.z = j[key][2].get<float>();
                dst.w = j[key][3].get<float>();
            }
        };
        auto rv2 = [&](const char* key, Vec2& dst)
        {
            if (j.contains(key) && j[key].is_array() && j[key].size() >= 2)
            {
                dst.x = j[key][0].get<float>();
                dst.y = j[key][1].get<float>();
            }
        };
        auto rf = [&](const char* key, float& dst)
        {
            if (j.contains(key) && j[key].is_number()) dst = j[key].get<float>();
        };

        if (j.contains("themeName") && j["themeName"].is_string())
            themeName = j["themeName"].get<std::string>();

        rv4("windowBackground",     windowBackground);
        rv4("titleBarBackground",   titleBarBackground);
        rv4("titleBarText",         titleBarText);
        rv4("panelBackground",      panelBackground);
        rv4("panelBackgroundAlt",   panelBackgroundAlt);
        rv4("panelHeader",          panelHeader);
        rv4("panelBorder",          panelBorder);
        rv4("buttonDefault",        buttonDefault);
        rv4("buttonHover",          buttonHover);
        rv4("buttonPressed",        buttonPressed);
        rv4("buttonText",           buttonText);
        rv4("buttonPrimary",        buttonPrimary);
        rv4("buttonPrimaryHover",   buttonPrimaryHover);
        rv4("buttonPrimaryText",    buttonPrimaryText);
        rv4("buttonDanger",         buttonDanger);
        rv4("buttonDangerHover",    buttonDangerHover);
        rv4("buttonDangerText",     buttonDangerText);
        rv4("buttonSubtle",         buttonSubtle);
        rv4("buttonSubtleHover",    buttonSubtleHover);
        rv4("textPrimary",          textPrimary);
        rv4("textSecondary",        textSecondary);
        rv4("textMuted",            textMuted);
        rv4("textLink",             textLink);
        rv4("inputBackground",      inputBackground);
        rv4("inputBackgroundHover", inputBackgroundHover);
        rv4("inputBorder",          inputBorder);
        rv4("inputText",            inputText);
        rv4("inputPlaceholder",     inputPlaceholder);
        rv4("checkboxDefault",      checkboxDefault);
        rv4("checkboxHover",        checkboxHover);
        rv4("checkboxChecked",      checkboxChecked);
        rv4("checkboxText",         checkboxText);
        rv4("dropdownBackground",   dropdownBackground);
        rv4("dropdownHover",        dropdownHover);
        rv4("dropdownText",         dropdownText);
        rv4("sliderTrack",          sliderTrack);
        rv4("sliderFill",           sliderFill);
        rv4("sliderThumb",          sliderThumb);
        rv4("accent",               accent);
        rv4("accentHover",          accentHover);
        rv4("accentMuted",          accentMuted);
        rv4("accentGreen",          accentGreen);
        rv4("selectionHighlight",   selectionHighlight);
        rv4("selectionHighlightHover", selectionHighlightHover);
        rv4("warningColor",         warningColor);
        rv4("errorColor",           errorColor);
        rv4("successColor",         successColor);
        rv4("modalOverlay",         modalOverlay);
        rv4("modalBackground",      modalBackground);
        rv4("modalText",            modalText);
        rv4("toastBackground",      toastBackground);
        rv4("toastText",            toastText);
        rv4("scrollbarTrack",       scrollbarTrack);
        rv4("scrollbarThumb",       scrollbarThumb);
        rv4("scrollbarThumbHover",  scrollbarThumbHover);
        rv4("treeRowEven",          treeRowEven);
        rv4("treeRowOdd",           treeRowOdd);
        rv4("treeRowHover",         treeRowHover);
        rv4("treeRowSelected",      treeRowSelected);
        rv4("cbTileBackground",     cbTileBackground);
        rv4("cbTileHover",          cbTileHover);
        rv4("cbTileSelected",       cbTileSelected);
        rv4("cbFolderAccent",       cbFolderAccent);
        rv4("timelineBackground",   timelineBackground);
        rv4("timelineRuler",        timelineRuler);
        rv4("timelineRulerText",    timelineRulerText);
        rv4("timelineLaneEven",     timelineLaneEven);
        rv4("timelineLaneOdd",      timelineLaneOdd);
        rv4("timelineKeyframe",     timelineKeyframe);
        rv4("timelineKeyframeHover",timelineKeyframeHover);
        rv4("tlKeyframeDiamond",    tlKeyframeDiamond);
        rv4("timelineScrubber",     timelineScrubber);
        rv4("timelineEndLine",      timelineEndLine);
        rv4("statusBarBackground",  statusBarBackground);
        rv4("statusBarText",        statusBarText);
        rv4("transparent",          transparent);

        rf("fontSizeHeading",       fontSizeHeading);
        rf("fontSizeSubheading",    fontSizeSubheading);
        rf("fontSizeBody",          fontSizeBody);
        rf("fontSizeSmall",         fontSizeSmall);
        rf("fontSizeCaption",       fontSizeCaption);
        rf("fontSizeMonospace",     fontSizeMonospace);

        rf("rowHeight",             rowHeight);
        rf("rowHeightSmall",        rowHeightSmall);
        rf("rowHeightLarge",        rowHeightLarge);
        rf("sectionHeaderHeight",   sectionHeaderHeight);
        rf("toolbarHeight",         toolbarHeight);
        rv2("paddingSmall",         paddingSmall);
        rv2("paddingNormal",        paddingNormal);
        rv2("paddingLarge",         paddingLarge);
        rv2("paddingSection",       paddingSection);
        rf("indentSize",            indentSize);
        rf("iconSize",              iconSize);
        rf("iconSizeLarge",         iconSizeLarge);
        rf("borderRadius",          borderRadius);
        rf("separatorThickness",    separatorThickness);

        // JSON stores base (unscaled) values — re-apply current DPI scale
        if (dpiScale > 0.0f && std::abs(dpiScale - 1.0f) > 0.001f)
        {
            const float s = dpiScale;
            fontSizeHeading    *= s;  fontSizeSubheading *= s;
            fontSizeBody       *= s;  fontSizeSmall      *= s;
            fontSizeCaption    *= s;  fontSizeMonospace  *= s;
            rowHeight          *= s;  rowHeightSmall     *= s;
            rowHeightLarge     *= s;  sectionHeaderHeight *= s;
            toolbarHeight      *= s;
            paddingSmall   = { paddingSmall.x   * s, paddingSmall.y   * s };
            paddingNormal  = { paddingNormal.x  * s, paddingNormal.y  * s };
            paddingLarge   = { paddingLarge.x   * s, paddingLarge.y   * s };
            paddingSection = { paddingSection.x * s, paddingSection.y * s };
            indentSize     *= s;  iconSize       *= s;  iconSizeLarge  *= s;
            borderRadius   *= s;  separatorThickness *= s;
        }
    }

    bool saveToFile(const std::string& path) const
    {
        try
        {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            std::ofstream ofs(path);
            if (!ofs.is_open()) return false;
            ofs << toJson().dump(4);
            return true;
        }
        catch (...) { return false; }
    }

    bool loadFromFile(const std::string& path)
    {
        try
        {
            const float savedScale = dpiScale;
            std::ifstream ifs(path);
            if (!ifs.is_open()) return false;
            nlohmann::json j = nlohmann::json::parse(ifs);
            dpiScale = savedScale;   // preserve scale before fromJson applies it
            fromJson(j);
            return true;
        }
        catch (...) { return false; }
    }

    static std::vector<std::string> discoverThemes(const std::string& themesDir)
    {
        std::vector<std::string> names;
        std::error_code ec;
        if (!std::filesystem::exists(themesDir, ec)) return names;
        for (const auto& entry : std::filesystem::directory_iterator(themesDir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                names.push_back(entry.path().stem().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    // ── Themes directory ─────────────────────────────────────────────────
    static std::string GetThemesDirectory()
    {
        return (std::filesystem::current_path() / "Editor" / "Themes").string();
    }

    /// Write built-in default themes (Dark + Light) if they don't exist yet.
    static void EnsureDefaultThemes()
    {
        const std::string dir = GetThemesDirectory();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        // Dark theme = current defaults
        {
            const std::string darkPath = (std::filesystem::path(dir) / "Dark.json").string();
            if (!std::filesystem::exists(darkPath, ec))
            {
                EditorTheme dark;
                dark.themeName = "Dark";
                dark.saveToFile(darkPath);
            }
        }

        // Light theme variant
        {
            const std::string lightPath = (std::filesystem::path(dir) / "Light.json").string();
            if (!std::filesystem::exists(lightPath, ec))
            {
                EditorTheme light;
                light.themeName = "Light";

                // Window / chrome
                light.windowBackground     = Vec4{ 0.92f, 0.92f, 0.92f, 1.0f };
                light.titleBarBackground   = Vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
                light.titleBarText         = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };

                // Panels
                light.panelBackground      = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
                light.panelBackgroundAlt   = Vec4{ 0.90f, 0.90f, 0.90f, 1.0f };
                light.panelHeader          = Vec4{ 0.88f, 0.88f, 0.88f, 1.0f };
                light.panelBorder          = Vec4{ 0.78f, 0.78f, 0.78f, 0.6f };

                // Buttons
                light.buttonDefault        = Vec4{ 0.82f, 0.82f, 0.82f, 1.0f };
                light.buttonHover          = Vec4{ 0.76f, 0.76f, 0.76f, 1.0f };
                light.buttonPressed        = Vec4{ 0.70f, 0.70f, 0.70f, 1.0f };
                light.buttonText           = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };
                light.buttonPrimary        = Vec4{ 0.20f, 0.45f, 0.80f, 1.0f };
                light.buttonPrimaryHover   = Vec4{ 0.28f, 0.52f, 0.88f, 1.0f };
                light.buttonPrimaryText    = Vec4{ 1.00f, 1.00f, 1.00f, 1.0f };
                light.buttonDanger         = Vec4{ 0.80f, 0.20f, 0.20f, 1.0f };
                light.buttonDangerHover    = Vec4{ 0.90f, 0.28f, 0.28f, 1.0f };
                light.buttonDangerText     = Vec4{ 1.00f, 1.00f, 1.00f, 1.0f };
                light.buttonSubtle         = Vec4{ 0.0f,  0.0f,  0.0f,  0.0f };
                light.buttonSubtleHover    = Vec4{ 0.80f, 0.80f, 0.80f, 0.5f };

                // Text
                light.textPrimary          = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };
                light.textSecondary        = Vec4{ 0.35f, 0.35f, 0.35f, 1.0f };
                light.textMuted            = Vec4{ 0.55f, 0.55f, 0.55f, 1.0f };
                light.textLink             = Vec4{ 0.18f, 0.42f, 0.78f, 1.0f };

                // Input
                light.inputBackground      = Vec4{ 1.00f, 1.00f, 1.00f, 1.0f };
                light.inputBackgroundHover = Vec4{ 0.96f, 0.96f, 0.96f, 1.0f };
                light.inputBorder          = Vec4{ 0.75f, 0.75f, 0.75f, 0.8f };
                light.inputText            = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };
                light.inputPlaceholder     = Vec4{ 0.60f, 0.60f, 0.60f, 1.0f };

                light.checkboxDefault      = Vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
                light.checkboxHover        = Vec4{ 0.78f, 0.78f, 0.78f, 1.0f };
                light.checkboxChecked      = Vec4{ 0.20f, 0.48f, 0.85f, 1.0f };
                light.checkboxText         = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };

                light.dropdownBackground   = Vec4{ 0.96f, 0.96f, 0.96f, 0.98f };
                light.dropdownHover        = Vec4{ 0.88f, 0.88f, 0.90f, 1.0f };
                light.dropdownText         = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };

                light.sliderTrack          = Vec4{ 0.82f, 0.82f, 0.82f, 1.0f };
                light.sliderFill           = Vec4{ 0.20f, 0.48f, 0.85f, 1.0f };
                light.sliderThumb          = Vec4{ 0.30f, 0.30f, 0.30f, 1.0f };

                // Accent / selection
                light.accent               = Vec4{ 0.20f, 0.48f, 0.85f, 1.0f };
                light.accentHover          = Vec4{ 0.28f, 0.55f, 0.92f, 1.0f };
                light.accentMuted          = Vec4{ 0.60f, 0.75f, 0.92f, 0.4f };
                light.accentGreen          = Vec4{ 0.22f, 0.62f, 0.32f, 1.0f };
                light.selectionHighlight   = Vec4{ 0.20f, 0.48f, 0.85f, 0.15f };
                light.selectionHighlightHover = Vec4{ 0.28f, 0.55f, 0.92f, 0.25f };

                light.warningColor         = Vec4{ 0.85f, 0.65f, 0.10f, 1.0f };
                light.errorColor           = Vec4{ 0.85f, 0.18f, 0.18f, 1.0f };
                light.successColor         = Vec4{ 0.20f, 0.65f, 0.32f, 1.0f };

                // Modal / overlay
                light.modalOverlay         = Vec4{ 0.0f,  0.0f,  0.0f,  0.35f };
                light.modalBackground      = Vec4{ 0.96f, 0.96f, 0.96f, 0.98f };
                light.modalText            = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };

                // Toast
                light.toastBackground      = Vec4{ 0.96f, 0.96f, 0.96f, 0.95f };
                light.toastText            = Vec4{ 0.10f, 0.10f, 0.10f, 1.0f };

                // Scrollbar
                light.scrollbarTrack       = Vec4{ 0.88f, 0.88f, 0.88f, 0.5f };
                light.scrollbarThumb       = Vec4{ 0.70f, 0.70f, 0.70f, 0.7f };
                light.scrollbarThumbHover  = Vec4{ 0.60f, 0.60f, 0.60f, 0.9f };

                // Tree view
                light.treeRowEven          = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
                light.treeRowOdd           = Vec4{ 0.92f, 0.92f, 0.92f, 1.0f };
                light.treeRowHover         = Vec4{ 0.85f, 0.85f, 0.88f, 0.8f };
                light.treeRowSelected      = Vec4{ 0.70f, 0.82f, 0.95f, 0.5f };

                // Content browser
                light.cbTileBackground     = Vec4{ 0.94f, 0.94f, 0.94f, 1.0f };
                light.cbTileHover          = Vec4{ 0.86f, 0.86f, 0.90f, 1.0f };
                light.cbTileSelected       = Vec4{ 0.70f, 0.82f, 0.95f, 0.6f };
                light.cbFolderAccent       = Vec4{ 0.82f, 0.86f, 0.92f, 0.9f };

                // Timeline
                light.timelineBackground   = Vec4{ 0.90f, 0.90f, 0.90f, 1.0f };
                light.timelineRuler        = Vec4{ 0.85f, 0.85f, 0.85f, 1.0f };
                light.timelineRulerText    = Vec4{ 0.40f, 0.40f, 0.40f, 1.0f };
                light.timelineLaneEven     = Vec4{ 0.92f, 0.92f, 0.92f, 0.5f };
                light.timelineLaneOdd      = Vec4{ 0.88f, 0.88f, 0.88f, 0.5f };

                // Status bar
                light.statusBarBackground  = Vec4{ 0.88f, 0.88f, 0.88f, 1.0f };
                light.statusBarText        = Vec4{ 0.30f, 0.30f, 0.30f, 1.0f };

                light.saveToFile(lightPath);
            }
        }
    }

    /// Load a theme by name from the themes directory.
    /// Falls back to default dark theme if loading fails.
    /// Preserves the current DPI scale so the new theme is
    /// automatically scaled to the active display density.
    bool loadThemeByName(const std::string& name)
    {
        const float savedScale = dpiScale;
        const std::string path = (std::filesystem::path(GetThemesDirectory()) / (name + ".json")).string();
        if (loadFromFile(path))
            return true;
        // Fallback: reset to default dark theme
        *this = EditorTheme{};
        themeName = "Dark";
        dpiScale = 1.0f;
        applyDpiScale(savedScale);
        return false;
    }

    /// Save the current active theme back to its file.
    bool saveActiveTheme()
    {
        const std::string path = (std::filesystem::path(GetThemesDirectory()) / (themeName + ".json")).string();
        return saveToFile(path);
    }

    // ── Apply theme colors to a single widget element (recursive) ────────
    /// Maps WidgetElementType + element ID to theme colors.
    /// Skips: ColorPicker (user data), fully transparent spacers,
    /// elements with images that should keep their look.
    /// Special cases: Close button → danger, Save → primary.
    static void ApplyThemeToElement(WidgetElement& el, const EditorTheme& t)
    {
        const auto& id = el.id;

        // Skip ColorPicker elements — they hold user-chosen colors
        if (el.type == WidgetElementType::ColorPicker)
        {
            for (auto& child : el.children) ApplyThemeToElement(child, t);
            return;
        }

        // Skip Image-only elements (icons keep their tint, but set text if present)
        if (el.type == WidgetElementType::Image)
        {
            el.style.textColor = t.textPrimary;
            for (auto& child : el.children) ApplyThemeToElement(child, t);
            return;
        }

        // Determine if element is intentionally transparent (spacer, overlay gap)
        const bool wasTransparent = (el.style.color.w < 0.01f);

        // ── Special ID-based overrides ───────────────────────────────────
        // Close button (title bar close → danger colors)
        if (id.find("Close") != std::string::npos && el.type == WidgetElementType::Button)
        {
            el.style.color        = t.buttonSubtle;
            el.style.hoverColor   = t.buttonDanger;
            el.style.pressedColor = t.buttonPressed;
            el.style.textColor    = t.buttonText;
            el.font = t.fontDefault;
            el.fontSize = t.fontSizeBody;
            for (auto& child : el.children) ApplyThemeToElement(child, t);
            return;
        }

        // Save button (primary accent)
        if (id.find("Save") != std::string::npos && el.type == WidgetElementType::Button)
        {
            el.style.color        = t.buttonPrimary;
            el.style.hoverColor   = t.buttonPrimaryHover;
            el.style.pressedColor = t.buttonPressed;
            el.style.textColor    = t.buttonPrimaryText;
            el.font = t.fontDefault;
            el.fontSize = t.fontSizeBody;
            for (auto& child : el.children) ApplyThemeToElement(child, t);
            return;
        }

        // ── Type-based theme mapping ─────────────────────────────────────
        switch (el.type)
        {
        case WidgetElementType::Panel:
        case WidgetElementType::StackPanel:
        case WidgetElementType::Grid:
        case WidgetElementType::WrapBox:
        case WidgetElementType::UniformGrid:
        case WidgetElementType::SizeBox:
        case WidgetElementType::ScaleBox:
        case WidgetElementType::Overlay:
            if (!wasTransparent)
            {
                el.style.color = t.panelBackground;
                el.style.hoverColor = t.panelBackground;
            }
            el.style.textColor = t.textPrimary;
            el.style.borderColor = t.panelBorder;
            break;

        case WidgetElementType::Border:
            if (!wasTransparent)
                el.style.color = t.panelBackground;
            el.style.borderColor = t.panelBorder;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::Button:
            el.style.color        = t.buttonDefault;
            el.style.hoverColor   = t.buttonHover;
            el.style.pressedColor = t.buttonPressed;
            el.style.textColor    = t.buttonText;
            el.style.textHoverColor = t.buttonText;
            el.style.borderColor  = t.panelBorder;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::ToggleButton:
            el.style.color        = t.buttonDefault;
            el.style.hoverColor   = t.buttonHover;
            el.style.pressedColor = t.accent;
            el.style.textColor    = t.buttonText;
            el.style.borderColor  = t.panelBorder;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::DropdownButton:
            el.style.color        = t.buttonDefault;
            el.style.hoverColor   = t.buttonHover;
            el.style.pressedColor = t.buttonPressed;
            el.style.textColor    = t.buttonText;
            el.style.borderColor  = t.panelBorder;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::Text:
        case WidgetElementType::RichText:
            el.style.textColor = t.textPrimary;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::Label:
            el.style.textColor = t.textSecondary;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::EntryBar:
            el.style.color           = t.inputBackground;
            el.style.hoverColor      = t.inputBackgroundHover;
            el.style.textColor       = t.inputText;
            el.style.borderColor     = t.inputBorder;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::CheckBox:
            el.style.color        = t.checkboxDefault;
            el.style.hoverColor   = t.checkboxHover;
            el.style.fillColor    = t.checkboxChecked;
            el.style.textColor    = t.checkboxText;
            el.style.borderColor  = t.inputBorder;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::RadioButton:
            el.style.color        = t.checkboxDefault;
            el.style.hoverColor   = t.checkboxHover;
            el.style.fillColor    = t.accent;
            el.style.textColor    = t.checkboxText;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::DropDown:
            el.style.color        = t.dropdownBackground;
            el.style.hoverColor   = t.dropdownHover;
            el.style.textColor    = t.dropdownText;
            el.style.borderColor  = t.inputBorder;
            el.font = t.fontDefault;
            if (el.fontSize <= 0.0f) el.fontSize = t.fontSizeBody;
            break;

        case WidgetElementType::Slider:
            el.style.color     = t.sliderTrack;
            el.style.fillColor = t.sliderFill;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::ProgressBar:
            el.style.color     = t.sliderTrack;
            el.style.fillColor = t.accent;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::Separator:
            el.style.color = t.panelBorder;
            break;

        case WidgetElementType::ScrollView:
            if (!wasTransparent)
                el.style.color = t.panelBackground;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::TreeView:
            el.style.color     = t.panelBackground;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::TabView:
            el.style.color      = t.panelHeader;
            el.style.hoverColor = t.buttonHover;
            el.style.textColor  = t.textPrimary;
            break;

        case WidgetElementType::ListView:
        case WidgetElementType::TileView:
            el.style.color     = t.panelBackground;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::Spinner:
            el.style.color     = t.accent;
            el.style.textColor = t.textPrimary;
            break;

        case WidgetElementType::WidgetSwitcher:
            // Container only, keep transparent
            break;

        default:
            break;
        }

        // Recurse into children
        for (auto& child : el.children)
            ApplyThemeToElement(child, t);
    }

    // ── Singleton accessor ───────────────────────────────────────────────
    static EditorTheme& Get()
    {
        static EditorTheme s_theme;
        return s_theme;
    }
};
