#pragma once

#include "../Core/MathTypes.h"

/// Customisable theme for **viewport / gameplay UI** elements.
///
/// Game developers can change these values at runtime to skin their in-game UI
/// without affecting the editor chrome.  The defaults are intentionally neutral
/// (dark semi-transparent) so they look reasonable in any viewport.
///
/// The editor itself uses `EditorTheme` (separate file) – do **not** mix
/// the two.
struct ViewportUITheme
{
    // ── Backgrounds ──────────────────────────────────────────────────────
    Vec4 panelBackground      { 0.06f, 0.06f, 0.08f, 0.75f };  // semi-transparent dark
    Vec4 panelBackgroundAlt   { 0.08f, 0.09f, 0.12f, 0.70f };

    // ── Buttons ──────────────────────────────────────────────────────────
    Vec4 buttonDefault        { 0.14f, 0.15f, 0.19f, 0.85f };
    Vec4 buttonHover          { 0.22f, 0.24f, 0.30f, 0.90f };
    Vec4 buttonPressed        { 0.10f, 0.11f, 0.14f, 0.90f };
    Vec4 buttonText           { 0.95f, 0.95f, 0.98f, 1.0f };

    Vec4 buttonPrimary        { 0.20f, 0.45f, 0.78f, 0.90f };
    Vec4 buttonPrimaryHover   { 0.26f, 0.52f, 0.88f, 0.95f };
    Vec4 buttonPrimaryText    { 1.0f,  1.0f,  1.0f,  1.0f };

    Vec4 buttonDanger         { 0.62f, 0.18f, 0.18f, 0.90f };
    Vec4 buttonDangerHover    { 0.76f, 0.24f, 0.24f, 0.95f };
    Vec4 buttonDangerText     { 0.96f, 0.85f, 0.85f, 1.0f };

    // ── Text ─────────────────────────────────────────────────────────────
    Vec4 textPrimary          { 0.95f, 0.95f, 0.98f, 1.0f };
    Vec4 textSecondary        { 0.65f, 0.67f, 0.72f, 1.0f };
    Vec4 textMuted            { 0.45f, 0.47f, 0.52f, 1.0f };

    // ── Input controls ───────────────────────────────────────────────────
    Vec4 inputBackground      { 0.08f, 0.09f, 0.12f, 0.80f };
    Vec4 inputText            { 0.92f, 0.93f, 0.96f, 1.0f };

    // ── Accent / selection ───────────────────────────────────────────────
    Vec4 accent               { 0.25f, 0.52f, 0.88f, 1.0f };
    Vec4 selectionHighlight   { 0.25f, 0.52f, 0.88f, 0.30f };

    // ── Overlay / tooltip ────────────────────────────────────────────────
    Vec4 tooltipBackground    { 0.10f, 0.10f, 0.14f, 0.92f };
    Vec4 tooltipText          { 0.90f, 0.92f, 0.96f, 1.0f };

    // ── Progress / health bars ───────────────────────────────────────────
    Vec4 progressTrack        { 0.12f, 0.13f, 0.16f, 0.70f };
    Vec4 progressFill         { 0.25f, 0.72f, 0.40f, 0.90f };

    // ── Fonts ────────────────────────────────────────────────────────────
    const char* fontDefault   = "default.ttf";

    float fontSizeTitle       = 18.0f;
    float fontSizeBody        = 14.0f;
    float fontSizeSmall       = 12.0f;
    float fontSizeCaption     = 10.0f;

    // ── Spacing ──────────────────────────────────────────────────────────
    Vec2 paddingSmall         { 4.0f,  2.0f };
    Vec2 paddingNormal        { 6.0f,  4.0f };
    Vec2 paddingLarge         { 10.0f, 6.0f };

    // ── Transparent (helper) ─────────────────────────────────────────────
    Vec4 transparent          { 0.0f,  0.0f,  0.0f,  0.0f };

    // ── Singleton accessor ───────────────────────────────────────────────
    static ViewportUITheme& Get()
    {
        static ViewportUITheme s_theme;
        return s_theme;
    }
};
