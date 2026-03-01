"""Engine scripting API type stubs for IntelliSense."""

from typing import Callable, Optional, Tuple

# ---------------------------------------------------------------------------
# Top-level constants
# ---------------------------------------------------------------------------

Component_Transform: int
Component_Mesh: int
Component_Material: int
Component_Light: int
Component_Camera: int
Component_Physics: int
Component_Script: int
Component_Name: int
Component_Collision: int

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
    def attach_component(entity: int, kind: int) -> bool:
        """Attach a component by kind."""
        ...

    @staticmethod
    def detach_component(entity: int, kind: int) -> bool:
        """Detach a component by kind."""
        ...

    @staticmethod
    def get_entities(kinds: list[int]) -> list[int]:
        """Get entities matching component kinds."""
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
        """Register a key pressed callback for a key."""
        ...

    @staticmethod
    def register_key_released(key: int, callback: Callable[[int], None]) -> bool:
        """Register a key released callback for a key."""
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
    def spawn_widget(widget_id: str, asset_path: str, tab_id: str = "") -> bool:
        """Spawn or replace a UI widget from a widget asset path."""
        ...

    @staticmethod
    def remove_widget(widget_id: str) -> bool:
        """Remove a UI widget by id."""
        ...

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
