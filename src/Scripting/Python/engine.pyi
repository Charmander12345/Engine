"""Engine scripting API type stubs for IntelliSense."""

from typing import Callable, Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Top-level constants
# ---------------------------------------------------------------------------

Component_Transform: int
Component_Mesh: int
Component_Material: int
Component_Light: int
Component_Camera: int
Component_Physics: int
Component_Logic: int
Component_Name: int
Component_Collision: int
Component_Animation: int
Component_ParticleEmitter: int

Asset_Texture: int
Asset_Material: int
Asset_Model2D: int
Asset_Model3D: int
Asset_PointLight: int
Asset_Audio: int
Asset_Script: int
Asset_Shader: int
Asset_Level: int
Asset_Widget: int

Log_Info: int
Log_Warning: int
Log_Error: int

# ---------------------------------------------------------------------------
# engine.entity
# ---------------------------------------------------------------------------

class entity:
    @staticmethod
    def create_entity() -> int:
        """Create an entity."""
        ...

    @staticmethod
    def remove_entity(entity: int) -> bool:
        """Remove an entity from the scene."""
        ...

    @staticmethod
    def attach_component(entity: int, kind: int) -> bool:
        """Attach a component by kind."""
        ...

    @staticmethod
    def detach_component(entity: int, kind: int) -> bool:
        """Detach a component by kind."""
        ...

    @staticmethod
    def has_component(entity: int, kind: int) -> bool:
        """Check if an entity has a component of the given kind."""
        ...

    @staticmethod
    def get_entities(kinds: list[int]) -> list[int]:
        """Get entities matching component kinds."""
        ...

    @staticmethod
    def get_all_entities() -> list[int]:
        """Get all entities currently in the scene."""
        ...

    @staticmethod
    def get_entity_count() -> int:
        """Get the total number of entities in the scene."""
        ...

    @staticmethod
    def find_entity_by_name(name: str) -> int:
        """Find an entity by its display name. Returns 0 if not found."""
        ...

    @staticmethod
    def get_entity_name(entity: int) -> str:
        """Get the display name of an entity. Returns empty string if no name."""
        ...

    @staticmethod
    def set_entity_name(entity: int, name: str) -> bool:
        """Set the display name of an entity. Adds NameComponent if missing."""
        ...

    @staticmethod
    def is_entity_valid(entity: int) -> bool:
        """Check if an entity id refers to a valid, existing entity."""
        ...

    @staticmethod
    def distance_between(entity_a: int, entity_b: int) -> float:
        """Get the world-space distance between two entities.
        Both entities must have a TransformComponent."""
        ...

    @staticmethod
    def get_transform(entity: int) -> Optional[tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]]:
        """Get transform (pos, rot, scale)."""
        ...

    @staticmethod
    def set_position(entity: int, x: float, y: float, z: float) -> bool:
        """Set entity position."""
        ...

    @staticmethod
    def translate(entity: int, dx: float, dy: float, dz: float) -> bool:
        """Translate entity position."""
        ...

    @staticmethod
    def set_rotation(entity: int, pitch: float, yaw: float, roll: float) -> bool:
        """Set entity rotation."""
        ...

    @staticmethod
    def rotate(entity: int, dp: float, dy: float, dr: float) -> bool:
        """Rotate entity."""
        ...

    @staticmethod
    def set_scale(entity: int, sx: float, sy: float, sz: float) -> bool:
        """Set entity scale."""
        ...

    @staticmethod
    def set_mesh(entity: int, path: str) -> bool:
        """Set mesh asset for an entity."""
        ...

    @staticmethod
    def get_mesh(entity: int) -> Optional[str]:
        """Get mesh asset path for an entity."""
        ...

    @staticmethod
    def get_light_color(entity: int) -> Optional[tuple[float, float, float]]:
        """Get light color (r, g, b) for an entity with LightComponent."""
        ...

    @staticmethod
    def set_light_color(entity: int, r: float, g: float, b: float) -> bool:
        """Set light color (r, g, b) for an entity with LightComponent."""
        ...

    @staticmethod
    def call_native(entity: int, func_name: str, *args) -> object:
        """Call a named function on the entity's C++ native script.
        Returns the result from the C++ onScriptCall handler, or None if no native script is attached."""
        ...

    @staticmethod
    def call_function(entity: int, func_name: str, *args) -> object:
        """Call a named function on any entity's script (C++ or Python).
        Automatically routes to C++ (onScriptCall) first, then Python.
        Callers never need to know which language implements the function."""
        ...

    @staticmethod
    def get_velocity(entity: int) -> tuple[float, float, float]:
        """Get linear velocity of an entity -> (x, y, z). Returns (0,0,0) if no PhysicsComponent."""
        ...

    @staticmethod
    def set_velocity(entity: int, x: float, y: float, z: float) -> bool:
        """Set linear velocity of an entity with PhysicsComponent."""
        ...

    @staticmethod
    def add_force(entity: int, fx: float, fy: float, fz: float) -> bool:
        """Apply a force (divided by mass) to an entity with PhysicsComponent."""
        ...

    @staticmethod
    def add_impulse(entity: int, ix: float, iy: float, iz: float) -> bool:
        """Apply a direct velocity impulse to an entity with PhysicsComponent."""
        ...

    @staticmethod
    def set_material_override_color_tint(entity: int, r: float, g: float, b: float) -> bool:
        """Set per-entity color tint override (multiplicative RGB, 1,1,1 = no tint).
        Requires MaterialComponent on the entity."""
        ...

    @staticmethod
    def get_material_override_color_tint(entity: int) -> Optional[tuple[float, float, float]]:
        """Get per-entity color tint override. Returns None if no override is set."""
        ...

    @staticmethod
    def set_material_override_metallic(entity: int, metallic: float) -> bool:
        """Set per-entity metallic override (0.0 - 1.0)."""
        ...

    @staticmethod
    def set_material_override_roughnessbbb(entity: int, roughness: float) -> bool:
        """Set per-entity roughness override (0.0 - 1.0)."""
        ...

    @staticmethod
    def set_material_override_shininess(entity: int, shininess: float) -> bool:
        """Set per-entity shininess override."""
        ...

    @staticmethod
    def clear_material_overrides(entity: int) -> bool:
        """Clear all per-entity material overrides, reverting to base material."""
        ...

# ---------------------------------------------------------------------------
# engine.assetmanagement
# ---------------------------------------------------------------------------

class assetmanagement:
    Asset_Texture: int
    Asset_Material: int
    Asset_Model2D: int
    Asset_Model3D: int
    Asset_PointLight: int
    Asset_Audio: int
    Asset_Script: int
    Asset_Shader: int
    Asset_Level: int
    Asset_Widget: int
    Asset_NativeScript: int

    @staticmethod
    def is_asset_loaded(path: str) -> bool:
        """Check if an asset is loaded by path."""
        ...

    @staticmethod
    def load_asset(path: str, type: int, allow_gc: bool = False) -> int:
        """Load an asset by path and type (sync)."""
        ...

    @staticmethod
    def load_asset_async(path: str, type: int, callback: Optional[Callable[[int], None]] = None, allow_gc: bool = False) -> int:
        """Load an asset by path and type (async)."""
        ...

    @staticmethod
    def save_asset(id: int, type: int, sync: bool = True) -> bool:
        """Save an asset by id and type."""
        ...

    @staticmethod
    def unload_asset(id: int) -> bool:
        """Unload an asset by id."""
        ...

# ---------------------------------------------------------------------------
# engine.audio
# ---------------------------------------------------------------------------

class audio:
    @staticmethod
    def create_audio(path: str, loop: int = 0, gain: float = 1.0, keep_loaded: bool = False) -> int:
        """Create an audio handle from a Content-relative path."""
        ...

    @staticmethod
    def create_audio_from_asset(asset_id: int, loop: int = 0, gain: float = 1.0) -> int:
        """Create an audio handle from an asset id."""
        ...

    @staticmethod
    def create_audio_from_asset_async(asset_id: int, callback: Callable[[int], None], loop: int = 0, gain: float = 1.0) -> int:
        """Create an audio handle asynchronously from an asset id with callback."""
        ...

    @staticmethod
    def play_audio(path: str, loop: int = 0, gain: float = 1.0, keep_loaded: bool = False) -> int:
        """Play an audio asset by Content-relative path."""
        ...

    @staticmethod
    def play_audio_handle(handle: int) -> bool:
        """Play an audio handle."""
        ...

    @staticmethod
    def set_audio_volume(handle: int, gain: float) -> bool:
        """Set audio handle volume."""
        ...

    @staticmethod
    def get_audio_volume(handle: int) -> float:
        """Get audio handle volume."""
        ...

    @staticmethod
    def pause_audio(handle: int) -> bool:
        """Pause a playing audio handle."""
        ...

    @staticmethod
    def pause_audio_handle(handle: int) -> bool:
        """Pause a playing audio handle."""
        ...

    @staticmethod
    def is_audio_playing(handle: int) -> bool:
        """Check if an audio handle is playing."""
        ...

    @staticmethod
    def is_audio_playing_path(path: str) -> bool:
        """Check if an audio path is playing."""
        ...

    @staticmethod
    def stop_audio(handle: int) -> bool:
        """Stop a playing audio handle."""
        ...

    @staticmethod
    def stop_audio_handle(handle: int) -> bool:
        """Stop a playing audio handle."""
        ...

    @staticmethod
    def invalidate_audio_handle(handle: int) -> bool:
        """Invalidate an audio handle."""
        ...

# ---------------------------------------------------------------------------
# engine.input
# ---------------------------------------------------------------------------

class input:
    Keys: dict[str, int]

    Key_A: int
    Key_B: int
    Key_C: int
    Key_D: int
    Key_E: int
    Key_F: int
    Key_G: int
    Key_H: int
    Key_I: int
    Key_J: int
    Key_K: int
    Key_L: int
    Key_M: int
    Key_N: int
    Key_O: int
    Key_P: int
    Key_Q: int
    Key_R: int
    Key_S: int
    Key_T: int
    Key_U: int
    Key_V: int
    Key_W: int
    Key_X: int
    Key_Y: int
    Key_Z: int
    Key_1: int
    Key_2: int
    Key_3: int
    Key_4: int
    Key_5: int
    Key_6: int
    Key_7: int
    Key_8: int
    Key_9: int
    Key_0: int
    Key_RETURN: int
    Key_ESCAPE: int
    Key_BACKSPACE: int
    Key_TAB: int
    Key_SPACE: int
    Key_MINUS: int
    Key_EQUALS: int
    Key_LEFTBRACKET: int
    Key_RIGHTBRACKET: int
    Key_BACKSLASH: int
    Key_SEMICOLON: int
    Key_APOSTROPHE: int
    Key_GRAVE: int
    Key_COMMA: int
    Key_PERIOD: int
    Key_SLASH: int
    Key_CAPSLOCK: int
    Key_F1: int
    Key_F2: int
    Key_F3: int
    Key_F4: int
    Key_F5: int
    Key_F6: int
    Key_F7: int
    Key_F8: int
    Key_F9: int
    Key_F10: int
    Key_F11: int
    Key_F12: int
    Key_PRINTSCREEN: int
    Key_SCROLLLOCK: int
    Key_PAUSE: int
    Key_INSERT: int
    Key_HOME: int
    Key_PAGEUP: int
    Key_DELETE: int
    Key_END: int
    Key_PAGEDOWN: int
    Key_RIGHT: int
    Key_LEFT: int
    Key_DOWN: int
    Key_UP: int
    Key_NUMLOCKCLEAR: int
    Key_KP_DIVIDE: int
    Key_KP_MULTIPLY: int
    Key_KP_MINUS: int
    Key_KP_PLUS: int
    Key_KP_ENTER: int
    Key_KP_1: int
    Key_KP_2: int
    Key_KP_3: int
    Key_KP_4: int
    Key_KP_5: int
    Key_KP_6: int
    Key_KP_7: int
    Key_KP_8: int
    Key_KP_9: int
    Key_KP_0: int
    Key_KP_PERIOD: int
    Key_LCTRL: int
    Key_LSHIFT: int
    Key_LALT: int
    Key_LGUI: int
    Key_RCTRL: int
    Key_RSHIFT: int
    Key_RALT: int
    Key_RGUI: int

    @staticmethod
    def set_on_key_pressed(callback: Callable[[int], None]) -> bool:
        """Set a global key pressed callback."""
        ...

    @staticmethod
    def set_on_key_released(callback: Callable[[int], None]) -> bool:
        """Set a global key released callback."""
        ...

    @staticmethod
    def register_key_pressed(key: int, callback: Callable[[int], None]) -> bool:
        """Register a callback for when a specific key is pressed.
        The callback receives the entity that registered it (not the key code)."""
        ...

    @staticmethod
    def register_key_released(key: int, callback: Callable[[int], None]) -> bool:
        """Register a callback for when a specific key is released.
        The callback receives the entity that registered it (not the key code)."""
        ...

    @staticmethod
    def is_shift_pressed() -> bool:
        """Check if shift is pressed."""
        ...

    @staticmethod
    def is_ctrl_pressed() -> bool:
        """Check if ctrl is pressed."""
        ...

    @staticmethod
    def is_alt_pressed() -> bool:
        """Check if alt is pressed."""
        ...

    @staticmethod
    def get_key(name: str) -> int:
        """Resolve a keycode from a key name."""
        ...

    @staticmethod
    def register_action_pressed(action_name: str, callback: Callable[[], None]) -> bool:
        """Register a callback for when an input action is pressed."""
        ...

    @staticmethod
    def register_action_released(action_name: str, callback: Callable[[], None]) -> bool:
        """Register a callback for when an input action is released."""
        ...

# ---------------------------------------------------------------------------
# engine.ui
# ---------------------------------------------------------------------------

class ui:
    @staticmethod
    def show_modal_message(message: str, callback: Optional[Callable[[], None]] = None) -> bool:
        """Show a blocking modal message."""
        ...

    @staticmethod
    def close_modal_message() -> bool:
        """Close the active modal message."""
        ...

    @staticmethod
    def show_toast_message(message: str, duration: float = 2.5) -> bool:
        """Show a toast message."""
        ...

    @staticmethod
    def spawn_widget(content_path: str) -> Optional[str]:
        """Spawn a viewport widget from a content-relative path.

        The ``.asset`` extension is appended automatically if omitted,
        so ``spawn_widget("HUD")`` resolves to ``HUD.asset``.

        The widget is rendered only within the viewport area and is
        automatically destroyed when PIE stops.

        Returns a widget id string on success, or None on failure.
        """
        ...

    @staticmethod
    def remove_widget(widget_id: str) -> bool:
        """Remove a viewport widget by the id returned from spawn_widget."""
        ...

    @staticmethod
    def play_animation(widget_id: str, animation_name: str, from_start: bool = True) -> bool:
        """Play a named animation on a spawned viewport widget."""
        ...

    @staticmethod
    def stop_animation(widget_id: str, animation_name: str) -> bool:
        """Stop a named animation on a spawned viewport widget."""
        ...

    @staticmethod
    def set_animation_speed(widget_id: str, animation_name: str, speed: float) -> bool:
        """Set playback speed of a named animation on a spawned viewport widget."""
        ...

    @staticmethod
    def show_cursor(visible: bool) -> bool:
        """Show or hide the gameplay cursor.

        When visible the OS cursor is shown, relative mouse mode is
        disabled, and camera rotation is blocked automatically.
        """
        ...

    @staticmethod
    def clear_all_widgets() -> bool:
        """Remove all spawned viewport widgets."""
        ...

    @staticmethod
    def set_focus(element_id: str) -> bool:
        """Set focus to a viewport UI element by id.

        The element does not need to be marked as focusable; this forces
        focus directly.  A focus highlight is drawn around the element.
        """
        ...

    @staticmethod
    def clear_focus() -> bool:
        """Clear focus from the currently focused viewport UI element."""
        ...

    @staticmethod
    def get_focused_element() -> Optional[str]:
        """Get the id of the currently focused viewport UI element.

        Returns None if no element is focused.
        """
        ...

    @staticmethod
    def set_focusable(element_id: str, focusable: bool = True) -> bool:
        """Set whether a viewport UI element is focusable.

        Focusable elements can be reached via Tab / Shift+Tab navigation
        and arrow-key spatial navigation.
        """
        ...

    @staticmethod
    def set_draggable(element_id: str, enabled: bool = True, payload: str = '') -> bool:
        """Set a viewport UI element as draggable.

        When enabled, the element can be picked up by clicking and
        dragging beyond a 5-pixel threshold.  The optional payload
        string is delivered to drop targets via the DragDropOperation.
        """
        ...

    @staticmethod
    def set_drop_target(element_id: str, enabled: bool = True) -> bool:
        """Set a viewport UI element as a drop target.

        Drop targets receive dragged items.  During a drag, a green
        highlight outline is drawn around the hovered drop target.
        Callbacks (onDragOver, onDrop) can be set from C++ code.
        """
        ...

    # Widget Element Types available in Widget Assets:
    # Panel, Text, Label, Button, ToggleButton, RadioButton, Image, EntryBar,
    # StackPanel, ScrollView, Grid, Slider, CheckBox, DropDown, ColorPicker,
    # ProgressBar, Separator, TreeView, TabView, DropdownButton,
    # WrapBox        — flow container that wraps children to the next line/column
    # UniformGrid    — grid with equal-sized cells (columns, rows)
    # SizeBox        — single-child container with explicit width/height override
    # ScaleBox       — single-child container that scales to fit (Contain/Cover/Fill/ScaleDown/UserSpecified)
    # WidgetSwitcher — shows only one child at a time (activeChildIndex)
    # Overlay        — stacks all children on top of each other with alignment
    # Border         — single-child container with configurable border edges
    # Spinner        — animated loading indicator (circular dot pattern)
    # RichText       — formatted text block with inline markup (bold, italic, color)
    # ListView       — virtualised scrollable list (renders only visible items)
    # TileView       — virtualised tile grid (renders only visible tiles)
    #
    # ── Phase 4: Border Widget ────────────────────────────────────────────
    #
    # WidgetElementType::Border — a single-child container that draws a
    # configurable border around its content.
    #
    # Border-specific fields on WidgetElement:
    #   borderBrush            — UIBrush (color/style for the four border edges)
    #   borderThicknessLeft    — float (left edge thickness in pixels, default 1)
    #   borderThicknessTop     — float (top edge thickness, default 1)
    #   borderThicknessRight   — float (right edge thickness, default 1)
    #   borderThicknessBottom  — float (bottom edge thickness, default 1)
    #   contentPadding         — Vec2 (extra inner padding X/Y between border and child)
    #
    # Layout: child is inset by border thickness + contentPadding on each side.
    # Rendering: background brush fills the full rect, then four border edge
    # rects are drawn using borderBrush.  The child is rendered inside the
    # inset area.
    #
    # ── Phase 4: Spinner Widget ───────────────────────────────────────────
    #
    # WidgetElementType::Spinner — an animated circular loading indicator.
    #
    # Spinner-specific fields on WidgetElement:
    #   spinnerDotCount  — int (number of dots arranged in a circle, default 8)
    #   spinnerSpeed     — float (rotations per second, default 1.0)
    #   spinnerElapsed   — float (runtime accumulated time, not serialised)
    #
    # Rendering: N dots are placed evenly around a circle.  Each dot's opacity
    # fades based on its index offset from the "active" dot (determined by
    # spinnerElapsed * spinnerSpeed).  The element's color/background brush
    # controls the dot colour.  Spinner is a leaf node (no children).
    #
    # ── Phase 4: Multiline EntryBar ────────────────────────────────────────
    #
    # The existing EntryBar widget now supports multiline text input.
    #
    # New fields on WidgetElement (EntryBar):
    #   isMultiline — bool (default false, enables newline insertion on Enter)
    #   maxLines    — int  (0 = unlimited, caps the number of lines)
    #
    # When isMultiline is true:
    #   - Pressing Enter inserts a '\n' character instead of committing the value.
    #   - Text is rendered line-by-line (split on '\n'), each line at an
    #     incremented Y offset equal to the line height.
    #   - The caret is positioned at the end of the last line.
    #   - maxLines limits the number of '\n' characters that can be inserted
    #     (0 means no limit).
    # When isMultiline is false (default), EntryBar behaves as a single-line
    # text input (unchanged from previous behaviour).
    #
    # ── Phase 4: Rich Text Block ────────────────────────────────────────────
    #
    # WidgetElementType::RichText — displays formatted text with inline markup
    # for bold, italic, per-word colour, and inline images.
    #
    # RichText-specific fields on WidgetElement:
    #   richText — str (markup source string, HTML-like tags)
    #
    # Supported markup tags:
    #   <b>...</b>             — bold text (flag stored; same font used)
    #   <i>...</i>             — italic text (flag stored; same font used)
    #   <color=#RRGGBB>...</color> — per-segment text colour override
    #   <img src="path" w=N h=N/> — inline image placeholder (parsed, not rendered yet)
    #
    # Tags can be nested.  Text between tags is split into segments via
    # ParseRichTextMarkup().  The renderer performs greedy word-wrap within
    # the element's padded content area and draws each word with its
    # segment-specific colour.  The element's textColor is used as the
    # default colour for segments that do not override it.
    #
    # Layout: leaf node, uses minSize or defaults to 200x40 pixels.
    # Helper class: RichTextWidget (src/Renderer/UIWidgets/RichTextWidget.h)
    #
    # ── Phase 4: ListView ───────────────────────────────────────────────────
    #
    # WidgetElementType::ListView — a virtualised scrollable list that renders
    # only the visible range of items for efficient display of large datasets.
    #
    # ListView-specific fields on WidgetElement:
    #   totalItemCount — int (total number of items, default 0)
    #   itemHeight     — float (fixed height per row in pixels, default 32)
    #   scrollOffset   — float (current vertical scroll position)
    #   onGenerateItem — callback(int index, WidgetElement& template)
    #
    # Rendering: computes firstVisible = scrollOffset / itemHeight, renders
    # only the visible rows with alternating background colours and an index
    # label.  Scissor clipping constrains output to the element bounds.
    #
    # Layout: leaf node, uses minSize or defaults to 200x200 pixels.
    # Helper class: ListViewWidget (src/Renderer/UIWidgets/ListViewWidget.h)
    #
    # ── Phase 4: TileView ───────────────────────────────────────────────────
    #
    # WidgetElementType::TileView — a virtualised tile grid that renders
    # only the visible range of tiles arranged in a fixed column layout.
    #
    # TileView-specific fields on WidgetElement:
    #   totalItemCount — int (total number of tiles, default 0)
    #   itemHeight     — float (fixed height per tile in pixels, default 80)
    #   itemWidth      — float (fixed width per tile in pixels, default 100)
    #   columnsPerRow  — int (number of columns per row, default 4)
    #   scrollOffset   — float (current vertical scroll position)
    #   onGenerateItem — callback(int index, WidgetElement& template)
    #
    # Rendering: computes visible row range from scrollOffset / itemHeight,
    # draws tiles in a grid with alternating colours and index labels.
    # Scissor clipping constrains output to the element bounds.
    #
    # Layout: leaf node, uses minSize or defaults to 300x200 pixels.
    # Helper class: TileViewWidget (src/Renderer/UIWidgets/TileViewWidget.h)
    #
    # ── Phase 5: Focus System & Keyboard Navigation ───────────────────────
    #
    # FocusConfig struct (per WidgetElement):
    #   isFocusable — bool (default false; marks element as reachable by Tab/arrows)
    #   tabIndex    — int  (-1 = auto document order; ≥0 = explicit order)
    #   focusUp     — str  (element id override for Up arrow navigation)
    #   focusDown   — str  (element id override for Down arrow navigation)
    #   focusLeft   — str  (element id override for Left arrow navigation)
    #   focusRight  — str  (element id override for Right arrow navigation)
    #
    # focusBrush — UIBrush (colour used for the focus highlight outline;
    #   default blue {0.2, 0.6, 1.0, 0.9} when colour alpha is zero)
    #
    # Keyboard handling (ViewportUIManager.handleKeyDown):
    #   Tab / Shift+Tab — cycle focus through focusable elements (by tabIndex)
    #   Arrow keys      — spatial navigation to nearest focusable in direction
    #   Enter / Space   — activate focused element (click / toggle)
    #   Escape          — clear focus
    #
    # Python API:
    #   engine.ui.set_focus(element_id)       — set focus to an element
    #   engine.ui.clear_focus()               — clear current focus
    #   engine.ui.get_focused_element()       — get focused element id or None
    #   engine.ui.set_focusable(id, enabled)  — mark element as focusable
    #
    # Gamepad navigation (ViewportUIManager.handleGamepadButton / handleGamepadAxis):
    #   D-Pad Up/Down/Left/Right — spatial navigation (moveFocusInDirection)
    #   A / Cross (South)        — activate focused element
    #   B / Circle (East)        — clear focus
    #   LB / RB (Shoulders)      — tab previous / tab next
    #   Left Stick               — spatial navigation with deadzone (0.25),
    #                              repeat delay 0.35s then interval 0.12s
    #
    # SDL3 integration (main.cpp):
    #   SDL_INIT_GAMEPAD enabled, first connected gamepad auto-opened.
    #   Button/axis events routed to ViewportUIManager.
    #
    # ── Phase 5: Runtime Drag & Drop ──────────────────────────────────────
    #
    # DragDropOperation struct:
    #   sourceElementId — str (id of the element being dragged)
    #   payload         — str (user-defined payload string)
    #   dragPosition    — Vec2 (current pointer position in viewport coords)
    #
    # WidgetElement drag fields:
    #   isDraggable  — bool (default false; element can be picked up)
    #   dragPayload  — str  (arbitrary payload string attached to drag)
    #   acceptsDrop  — bool (default false; element accepts dropped items)
    #   onDragStart  — callback()           (called when drag begins)
    #   onDragOver   — callback(op) -> bool (called while hovering; return false to reject)
    #   onDrop       — callback(op)         (called when item is dropped)
    #
    # Drag flow:
    #   mouseDown on isDraggable element → drag pending
    #   mouseMove exceeds 5px threshold  → drag starts (onDragStart called)
    #   mouseMove updates dragPosition, tracks drop-target under cursor
    #   mouseUp on acceptsDrop element   → onDragOver check → onDrop
    #   mouseUp elsewhere / Escape       → cancelDrag
    #
    # Visual feedback (OpenGLRenderer):
    #   Drop-target highlight: green 2px outline around hovered acceptsDrop element
    #
    # JSON serialization: isDraggable, dragPayload, acceptsDrop (in widget asset files)
    #
    # Python API:
    #   engine.ui.set_draggable(element_id, enabled=True, payload='')
    #   engine.ui.set_drop_target(element_id, enabled=True)

    # ── WidgetElementStyle Struct ─────────────────────────────────────────
    #
    # All visual/style properties on a WidgetElement are now consolidated
    # inside a single ``style`` member (WidgetElementStyle struct).
    #
    # WidgetElementStyle fields:
    #   color           — Vec4 RGBA (normal/default background color)
    #   hoverColor      — Vec4 RGBA (background on hover)
    #   pressedColor    — Vec4 RGBA (background on press; alpha 0 = use hoverColor)
    #   disabledColor   — Vec4 RGBA (background when disabled)
    #   textColor       — Vec4 RGBA (normal text color)
    #   textHoverColor  — Vec4 RGBA (text on hover; alpha 0 = use textColor)
    #   textPressedColor — Vec4 RGBA (text on press; alpha 0 = use textColor)
    #   fillColor       — Vec4 RGBA (fill/accent, e.g. slider fill, progress bar)
    #   opacity         — float (0..1, default 1)
    #   borderThickness — float (border outline in pixels, default 0)
    #   borderRadius    — float (corner radius in pixels, default 0)
    #   isVisible       — bool  (whether the element is rendered/interactive)
    #   isBold          — bool  (bold text style)
    #   isItalic        — bool  (italic text style)
    #   shadowColor     — Vec4 RGBA (drop shadow color; alpha 0 = no shadow)
    #   shadowOffset    — Vec2 (shadow offset in pixels, default (0, 2))
    #   shadowBlurRadius — float (soft blur spread in pixels, default 6.0)
    #   applyElevation(level, baseShadowColor, baseShadowOffset) — sets shadow
    #       from elevation level (0=none, 1=subtle, 2=medium, 3=strong, 4-5=extra)
    #
    # WidgetElement also has:
    #   elevation — int (0–5, shadow depth level; 0 = none)
    #
    # Access pattern: element.style.color, element.style.opacity, etc.

    # ── Phase 2: Brush-based Styling ──────────────────────────────────────
    #
    # BrushType enum: None, SolidColor, Image, NineSlice, LinearGradient
    #
    # UIBrush struct fields:
    #   type          — BrushType (default SolidColor)
    #   color         — Vec4 RGBA (start color for gradients)
    #   colorEnd      — Vec4 RGBA (end color for LinearGradient)
    #   gradientAngle — float (0=vertical, 90=horizontal)
    #   imagePath     — str (asset-relative texture path)
    #   textureId     — int (preloaded GL texture id, 0 if not loaded)
    #   imageMargin   — Vec4 (L,T,R,B margins for NineSlice)
    #   imageTiling   — Vec2 (tile repeat count)
    #
    # Every WidgetElement now has:
    #   background      — UIBrush (replaces color for new assets)
    #   hoverBrush      — UIBrush (replaces hoverColor for new assets)
    #   fillBrush       — UIBrush (replaces fillColor for new assets)
    #   renderTransform — RenderTransform (visual-only, does not affect layout)
    #   clipMode        — ClipMode (None, ClipToBounds, InheritFromParent)
    #   effectiveOpacity — float (computed: element.opacity * parent.effectiveOpacity)
    #
    # RenderTransform struct fields:
    #   translation — Vec2 (pixels)
    #   rotation    — float (degrees)
    #   scale       — Vec2 (1,1 = identity)
    #   shear       — Vec2
    #   pivot       — Vec2 (normalised, 0.5,0.5 = centre)
    #
    # RenderTransform is applied visually in all three render paths
    # (viewport UI, widget preview FBO, editor UI).  The transform matrix is
    # T(pivot) * Translate * Rotate * Scale * Shear * T(-pivot) and is
    # multiplied onto the orthographic projection.  Hit-testing applies the
    # inverse transform so clicks on rotated/scaled widgets are detected
    # correctly.
    #
    # ClipMode behaviour:
    #   None             — no additional clipping
    #   ClipToBounds     — children are scissor-clipped to this element's
    #                      axis-aligned bounding box (nested clips intersect)
    #   InheritFromParent — keeps the parent's current scissor rect
    #
    # Opacity is inherited: children multiply their opacity with the parent's
    # effective opacity at render time.

# ---------------------------------------------------------------------------
# engine.physics
# ---------------------------------------------------------------------------

class physics:
    @staticmethod
    def set_velocity(entity: int, x: float, y: float, z: float) -> None:
        """Set linear velocity of an entity with PhysicsComponent."""
        ...

    @staticmethod
    def get_velocity(entity: int) -> Tuple[float, float, float]:
        """Get linear velocity of an entity -> (x, y, z)."""
        ...

    @staticmethod
    def add_force(entity: int, fx: float, fy: float, fz: float) -> None:
        """Apply a force (divided by mass) to an entity."""
        ...

    @staticmethod
    def add_impulse(entity: int, ix: float, iy: float, iz: float) -> None:
        """Apply a direct velocity impulse to an entity."""
        ...

    @staticmethod
    def set_angular_velocity(entity: int, x: float, y: float, z: float) -> None:
        """Set angular velocity of an entity (degrees/sec)."""
        ...

    @staticmethod
    def get_angular_velocity(entity: int) -> Tuple[float, float, float]:
        """Get angular velocity of an entity -> (x, y, z)."""
        ...

    @staticmethod
    def set_gravity(x: float, y: float, z: float) -> None:
        """Set world gravity vector."""
        ...

    @staticmethod
    def get_gravity() -> Tuple[float, float, float]:
        """Get current world gravity -> (x, y, z)."""
        ...

    @staticmethod
    def set_on_collision(callback: Optional[Callable]) -> None:
        """Register a collision callback(entityA, entityB, normal, depth, point) or None to clear."""
        ...

    @staticmethod
    def raycast(ox: float, oy: float, oz: float, dx: float, dy: float, dz: float, max_dist: float = 1000.0) -> Optional[dict]:
        """Cast a ray and return hit dict {entity, point, normal, distance} or None."""
        ...

    @staticmethod
    def is_body_sleeping(entity: int) -> bool:
        """Check if an entity's physics body is sleeping."""
        ...

# ---------------------------------------------------------------------------
# engine.camera
# ---------------------------------------------------------------------------

class camera:
    @staticmethod
    def get_camera_position() -> tuple[float, float, float]:
        """Get the camera position."""
        ...

    @staticmethod
    def set_camera_position(x: float, y: float, z: float) -> bool:
        """Set the camera position."""
        ...

    @staticmethod
    def get_camera_rotation() -> tuple[float, float]:
        """Get the camera rotation (yaw, pitch)."""
        ...

    @staticmethod
    def set_camera_rotation(yaw: float, pitch: float) -> bool:
        """Set the camera rotation (yaw, pitch)."""
        ...

    @staticmethod
    def transition_to(x: float, y: float, z: float, yaw: float, pitch: float, duration: float = 1.0) -> bool:
        """Start a smooth camera transition to the given position and rotation.

        The camera interpolates from its current state to the target over
        ``duration`` seconds using smooth-step (hermite) easing.  During
        the transition, manual camera movement and rotation are blocked.

        Args:
            x, y, z: Target world-space position.
            yaw: Target yaw in degrees.
            pitch: Target pitch in degrees.
            duration: Transition time in seconds (default 1.0).
                      If ≤ 0, the camera snaps instantly.
        """
        ...

    @staticmethod
    def is_transitioning() -> bool:
        """Returns True while a camera transition is in progress."""
        ...

    @staticmethod
    def cancel_transition() -> bool:
        """Cancel the active camera transition, keeping the current interpolated position."""
        ...

    @staticmethod
    def start_path(points: list[tuple[float, float, float, float, float]], duration: float, loop: bool = False) -> bool:
        """Start a spline-based (Catmull-Rom) camera path through control points.

        Each point is a tuple ``(x, y, z, yaw, pitch)``.  The camera
        smoothly interpolates through all points over ``duration`` seconds.
        At least 2 points are required.

        Args:
            points: List of ``(x, y, z, yaw, pitch)`` tuples.
            duration: Total playback time in seconds.
            loop: If True, the path loops seamlessly.
        """
        ...

    @staticmethod
    def is_path_playing() -> bool:
        """Returns True while a camera path is actively playing (not paused)."""
        ...

    @staticmethod
    def pause_path() -> bool:
        """Pause the active camera path playback."""
        ...

    @staticmethod
    def resume_path() -> bool:
        """Resume a paused camera path."""
        ...

    @staticmethod
    def stop_path() -> bool:
        """Stop the camera path, keeping the current interpolated position."""
        ...

    @staticmethod
    def get_path_progress() -> float:
        """Get normalised progress [0.0 – 1.0] of the current camera path."""
        ...

# ---------------------------------------------------------------------------
# engine.particle
# ---------------------------------------------------------------------------

class particle:
    @staticmethod
    def set_emitter(entity: int, key: str, value: float) -> bool:
        """Set a particle emitter property by key.

        Valid keys: emissionRate, lifetime, speed, speedVariance, size, sizeEnd,
        gravity, coneAngle, maxParticles, colorR, colorG, colorB, colorA,
        colorEndR, colorEndG, colorEndB, colorEndA.
        """
        ...

    @staticmethod
    def set_enabled(entity: int, enabled: bool = True) -> bool:
        """Enable or disable a particle emitter on the given entity."""
        ...

    @staticmethod
    def set_color(entity: int, r: float, g: float, b: float, a: float) -> bool:
        """Set the start color (RGBA) for a particle emitter."""
        ...

    @staticmethod
    def set_end_color(entity: int, r: float, g: float, b: float, a: float) -> bool:
        """Set the end-of-life color (RGBA) for a particle emitter."""
        ...

# ---------------------------------------------------------------------------
# engine.diagnostics
# ---------------------------------------------------------------------------

class diagnostics:
    @staticmethod
    def get_delta_time() -> float:
        """Get last frame delta time."""
        ...

    @staticmethod
    def get_engine_time() -> float:
        """Get seconds elapsed since engine start."""
        ...

    @staticmethod
    def get_state(key: str) -> Optional[str]:
        """Get engine state string."""
        ...

    @staticmethod
    def set_state(key: str, value: str) -> bool:
        """Set engine state string."""
        ...

    @staticmethod
    def get_cpu_info() -> dict:
        """Get CPU info: {'brand': str, 'physical_cores': int, 'logical_cores': int}."""
        ...

    @staticmethod
    def get_gpu_info() -> dict:
        """Get GPU info: {'renderer': str, 'vendor': str, 'driver_version': str, 'vram_total_mb': int, 'vram_free_mb': int}."""
        ...

    @staticmethod
    def get_ram_info() -> dict:
        """Get RAM info: {'total_mb': int, 'available_mb': int}."""
        ...

    @staticmethod
    def get_monitor_info() -> list:
        """Get list of monitor dicts: [{'name': str, 'width': int, 'height': int, 'refresh_rate': int, 'dpi_scale': float, 'primary': bool}, ...]."""
        ...

# ---------------------------------------------------------------------------
# engine.logging
# ---------------------------------------------------------------------------

class logging:
    @staticmethod
    def log(message: str, level: int = 0) -> bool:
        """Log a message (level: 0=info, 1=warn, 2=error)."""
        ...

# ---------------------------------------------------------------------------
# engine.math  – all computations run in C++
# ---------------------------------------------------------------------------

class math:
    # --- Vec3 (tuple[float, float, float]) ---
    @staticmethod
    def vec3(x: float = 0.0, y: float = 0.0, z: float = 0.0) -> Tuple[float, float, float]:
        """Create a Vec3 tuple."""
        ...
    @staticmethod
    def vec3_add(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Component-wise add two Vec3."""
        ...
    @staticmethod
    def vec3_sub(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Component-wise subtract two Vec3."""
        ...
    @staticmethod
    def vec3_mul(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Component-wise multiply two Vec3."""
        ...
    @staticmethod
    def vec3_div(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Component-wise divide two Vec3."""
        ...
    @staticmethod
    def vec3_scale(v: Tuple[float, float, float], s: float) -> Tuple[float, float, float]:
        """Scale Vec3 by a scalar."""
        ...
    @staticmethod
    def vec3_dot(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
        """Dot product of two Vec3."""
        ...
    @staticmethod
    def vec3_cross(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Cross product of two Vec3."""
        ...
    @staticmethod
    def vec3_length(v: Tuple[float, float, float]) -> float:
        """Length of a Vec3."""
        ...
    @staticmethod
    def vec3_length_sq(v: Tuple[float, float, float]) -> float:
        """Squared length of a Vec3."""
        ...
    @staticmethod
    def vec3_normalize(v: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Normalize a Vec3."""
        ...
    @staticmethod
    def vec3_negate(v: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Negate a Vec3."""
        ...
    @staticmethod
    def vec3_lerp(a: Tuple[float, float, float], b: Tuple[float, float, float], t: float) -> Tuple[float, float, float]:
        """Linearly interpolate between two Vec3."""
        ...
    @staticmethod
    def vec3_distance(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
        """Distance between two Vec3."""
        ...
    @staticmethod
    def vec3_reflect(v: Tuple[float, float, float], n: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Reflect Vec3 over a normal."""
        ...
    @staticmethod
    def vec3_min(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Component-wise min of two Vec3."""
        ...
    @staticmethod
    def vec3_max(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Component-wise max of two Vec3."""
        ...

    # --- Vec2 (tuple[float, float]) ---
    @staticmethod
    def vec2(x: float = 0.0, y: float = 0.0) -> Tuple[float, float]:
        """Create a Vec2 tuple."""
        ...
    @staticmethod
    def vec2_add(a: Tuple[float, float], b: Tuple[float, float]) -> Tuple[float, float]:
        """Component-wise add two Vec2."""
        ...
    @staticmethod
    def vec2_sub(a: Tuple[float, float], b: Tuple[float, float]) -> Tuple[float, float]:
        """Component-wise subtract two Vec2."""
        ...
    @staticmethod
    def vec2_scale(v: Tuple[float, float], s: float) -> Tuple[float, float]:
        """Scale Vec2 by a scalar."""
        ...
    @staticmethod
    def vec2_dot(a: Tuple[float, float], b: Tuple[float, float]) -> float:
        """Dot product of two Vec2."""
        ...
    @staticmethod
    def vec2_length(v: Tuple[float, float]) -> float:
        """Length of a Vec2."""
        ...
    @staticmethod
    def vec2_normalize(v: Tuple[float, float]) -> Tuple[float, float]:
        """Normalize a Vec2."""
        ...
    @staticmethod
    def vec2_lerp(a: Tuple[float, float], b: Tuple[float, float], t: float) -> Tuple[float, float]:
        """Linearly interpolate between two Vec2."""
        ...
    @staticmethod
    def vec2_distance(a: Tuple[float, float], b: Tuple[float, float]) -> float:
        """Distance between two Vec2."""
        ...

    # --- Quaternion (tuple[float, float, float, float] = (x, y, z, w)) ---
    @staticmethod
    def quat_from_euler(pitch: float, yaw: float, roll: float) -> Tuple[float, float, float, float]:
        """Euler (pitch,yaw,roll) radians -> quaternion (x,y,z,w)."""
        ...
    @staticmethod
    def quat_to_euler(q: Tuple[float, float, float, float]) -> Tuple[float, float, float]:
        """Quaternion -> Euler (pitch,yaw,roll) radians."""
        ...
    @staticmethod
    def quat_multiply(a: Tuple[float, float, float, float], b: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
        """Multiply two quaternions."""
        ...
    @staticmethod
    def quat_normalize(q: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
        """Normalize a quaternion."""
        ...
    @staticmethod
    def quat_slerp(a: Tuple[float, float, float, float], b: Tuple[float, float, float, float], t: float) -> Tuple[float, float, float, float]:
        """Slerp between two quaternions."""
        ...
    @staticmethod
    def quat_inverse(q: Tuple[float, float, float, float]) -> Tuple[float, float, float, float]:
        """Inverse of a quaternion."""
        ...
    @staticmethod
    def quat_rotate_vec3(q: Tuple[float, float, float, float], v: Tuple[float, float, float]) -> Tuple[float, float, float]:
        """Rotate a Vec3 by a quaternion."""
        ...

    # --- Scalar utilities ---
    @staticmethod
    def clamp(value: float, min_val: float, max_val: float) -> float:
        """Clamp value between min and max."""
        ...

# ---------------------------------------------------------------------------
# engine.globalstate – Shared global state for data exchange between entities
# ---------------------------------------------------------------------------

class globalstate:
    @staticmethod
    def set_global(name: str, value: object) -> bool:
        """Set a global variable (number, string, bool, or None).

        This value is shared across all Python scripts and can be used
        to exchange data between different entities.

        Args:
            name: Variable name (string key).
            value: Value to store (float, int, str, bool, or None).
        """
        ...

    @staticmethod
    def get_global(name: str) -> object:
        """Get a global variable by name. Returns None if not found."""
        ...

    @staticmethod
    def remove_global(name: str) -> bool:
        """Remove a global variable by name. Returns True if removed."""
        ...

    @staticmethod
    def get_all() -> Dict[str, object]:
        """Get all global variables as a dict."""
        ...

    @staticmethod
    def clear() -> bool:
        """Clear all global variables."""
        ...

# ---------------------------------------------------------------------------
# engine.editor – Editor Plugin API (Phase 11.3)
# ---------------------------------------------------------------------------

class editor:
    """Editor plugin scripting API. Access via ``import engine.editor``."""

    TOAST_INFO: int
    TOAST_SUCCESS: int
    TOAST_WARNING: int
    TOAST_ERROR: int

    @staticmethod
    def show_toast(message: str, level: int = 0) -> None:
        """Show a toast notification. Levels: 0=Info, 1=Success, 2=Warning, 3=Error."""
        ...

    @staticmethod
    def get_selected_entities() -> List[int]:
        """Get list of currently selected entity IDs."""
        ...

    @staticmethod
    def get_asset_list(type_filter: int = -1) -> List[Dict[str, object]]:
        """Get asset registry entries. Each dict has 'name', 'path', 'type' keys.
        Optional type_filter (int) to filter by asset type, -1 for all."""
        ...

    @staticmethod
    def create_entity(name: str = "New Entity") -> int:
        """Create a named entity with Transform component. Returns entity ID."""
        ...

    @staticmethod
    def select_entity(entity_id: int) -> None:
        """Select an entity by ID in the editor."""
        ...

    @staticmethod
    def add_menu_item(menu: str, name: str, callback: Callable[[], None]) -> None:
        """Add a custom menu item to the Settings dropdown.
        The callback is invoked when the user clicks the item."""
        ...

    @staticmethod
    def register_tab(name: str, on_build_ui: Callable[[], None]) -> None:
        """Register a custom editor tab with a build-UI callback."""
        ...

    @staticmethod
    def get_menu_items() -> List[Dict[str, str]]:
        """Get all registered plugin menu items as list of dicts with 'menu' and 'name'."""
        ...
    @staticmethod
    def lerp(a: float, b: float, t: float) -> float:
        """Linearly interpolate between two scalars."""
        ...
    @staticmethod
    def deg_to_rad(degrees: float) -> float:
        """Convert degrees to radians."""
        ...
    @staticmethod
    def rad_to_deg(radians: float) -> float:
        """Convert radians to degrees."""
        ...

    # --- Trigonometric ---
    @staticmethod
    def sin(radians: float) -> float:
        """Sine of angle in radians."""
        ...
    @staticmethod
    def cos(radians: float) -> float:
        """Cosine of angle in radians."""
        ...
    @staticmethod
    def tan(radians: float) -> float:
        """Tangent of angle in radians."""
        ...
    @staticmethod
    def asin(value: float) -> float:
        """Arc sine, returns radians."""
        ...
    @staticmethod
    def acos(value: float) -> float:
        """Arc cosine, returns radians."""
        ...
    @staticmethod
    def atan(value: float) -> float:
        """Arc tangent, returns radians."""
        ...
    @staticmethod
    def atan2(y: float, x: float) -> float:
        """Two-argument arc tangent (y, x), returns radians."""
        ...

    # --- Common math ---
    @staticmethod
    def sqrt(value: float) -> float:
        """Square root."""
        ...
    @staticmethod
    def abs(value: float) -> float:
        """Absolute value."""
        ...
    @staticmethod
    def pow(base: float, exponent: float) -> float:
        """Raise base to exponent."""
        ...
    @staticmethod
    def floor(value: float) -> float:
        """Round down to nearest integer."""
        ...
    @staticmethod
    def ceil(value: float) -> float:
        """Round up to nearest integer."""
        ...
    @staticmethod
    def round(value: float) -> float:
        """Round to nearest integer."""
        ...
    @staticmethod
    def sign(value: float) -> float:
        """Sign of value (-1, 0, or 1)."""
        ...
    @staticmethod
    def min(a: float, b: float) -> float:
        """Minimum of two values."""
        ...
    @staticmethod
    def max(a: float, b: float) -> float:
        """Maximum of two values."""
        ...
    @staticmethod
    def pi() -> float:
        """Return the constant pi."""
        ...

# ---------------------------------------------------------------------------
# Skeletal Animation System
# ---------------------------------------------------------------------------
#
# **Overview**
# When a 3D model asset (.fbx, .glTF, .dae, etc.) contains bones and
# animations, the engine automatically extracts:
#   - A Skeleton (bone hierarchy with offset/inverse-bind-pose matrices)
#   - Per-vertex bone IDs (4) and bone weights (4)
#   - AnimationClips with position/rotation/scaling keyframes per bone
#
# **Import**
# During ``AssetManager::importAsset(AssetType::Model3D, path)`` the Assimp
# importer now uses ``aiProcess_LimitBoneWeights`` in addition to the
# existing flags.  Bone data is stored in the asset JSON under:
#   - ``m_hasBones`` (bool)
#   - ``m_bones``    (array of { name, offsetMatrix[16] })
#   - ``m_boneIds``  (flat float array, 4 per original vertex)
#   - ``m_boneWeights`` (flat float array, 4 per original vertex)
#   - ``m_nodes``    (scene node hierarchy for animation traversal)
#   - ``m_animations`` (array of clips with channels + keyframes)
#
# **Vertex Layout (skinned meshes)**
# Non-skinned: 14 floats/vertex (pos3 + norm3 + uv2 + tan3 + bitan3)
# Skinned:     22 floats/vertex (above + boneIds4 + boneWeights4)
# Attribute locations: 0=pos, 1=norm, 2=uv, 3=tan, 4=bitan, 5=boneIds, 6=boneWeights
#
# **Shader**
# ``skinned_vertex.glsl`` extends the standard vertex shader with:
#   uniform bool uSkinned;
#   uniform mat4 uBoneMatrices[128];
# When uSkinned is true, the vertex position/normal/tangent/bitangent are
# transformed by the weighted sum of up to 4 bone matrices before the
# model matrix is applied.
#
# **Runtime Animation Playback**
# ``SkeletalAnimator`` (SkeletalData.h) performs per-frame keyframe
# interpolation (linear for position/scale, slerp for rotation) and
# computes the final bone matrices (globalTransform * offsetMatrix).
# The renderer automatically creates an animator per skinned entity and
# auto-plays the first animation clip in a loop.
#
# **ECS Integration**
# ``AnimationComponent`` stores runtime animation state:
#   - currentClipIndex (int, -1 = none)
#   - currentTime (float)
#   - speed (float, default 1.0)
#   - playing (bool)
#   - loop (bool, default true)
# Serialized in level JSON under "Animation" key.
#
# **Rendering**
# Skinned meshes are drawn individually (not instanced) since each entity
# has a unique bone pose.  Bone matrices are uploaded via
# ``glUniformMatrix4fv(uBoneMatrices, count, GL_TRUE, data)`` before each
# draw call.  Shadow passes use the standard shadow shader (no skinning)
# which is acceptable for most use cases.
#
