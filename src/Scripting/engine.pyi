"""Engine scripting API type stubs for IntelliSense."""

from typing import Callable, Optional

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
