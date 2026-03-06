#pragma once

#include "../UIWidget.h"

/// GameplayWidget is the full-featured widget class for viewport / gameplay UI.
///
/// It supports all customization options:
///   - JSON serialization (loadFromJson / toJson) for .widget asset files
///   - Animations and WidgetAnimationPlayer
///   - Anchor-based positioning (WidgetAnchor with 10 anchor points)
///   - EngineObject inheritance (managed as an engine asset)
///   - Full WidgetElementStyle with state colors, gradients, shadows
///   - UIBrush-based styling (SolidColor, Image, NineSlice, LinearGradient)
///   - RenderTransform (translation, rotation, scale, shear, pivot)
///   - ClipMode (None, ClipToBounds, InheritFromParent)
///   - Focus navigation and keyboard control (FocusConfig, tabIndex)
///   - Runtime Drag & Drop (DragDropOperation, acceptsDrop, onDrop)
///   - Tooltips, Rich Text, ListView / TileView virtualisation
///   - ScaleBox, SizeBox, WidgetSwitcher, Border, Spinner
///
/// Game developers can fully customise the look and behaviour of viewport
/// widgets through the Python scripting API and the Widget Editor.
///
/// The associated theme class is `ViewportUITheme` (not EditorTheme).
using GameplayWidget = Widget;
