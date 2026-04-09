# Project Overview

## Physics System
The engine provides a physics system layered around `IPhysicsBackend`, but the active implementation/build is currently Jolt-only:
- **Jolt Physics** (default) - primary backend, LinearCast CCD by default, enhanced internal edge removal for smooth heightfield interaction, tuned solver settings (Baumgarte 0.35, 12 velocity steps, 8 position steps, max penetration distance 1.0, 4 collision sub-steps, speculative contact distance 0.05) for robust collision handling and penetration recovery

## Build Configuration
- The Visual Studio `x64-release` preset is constrained to a true `Release` configuration and outputs to `out/build/x64-release/RELEASE`.
- The Visual Studio `x64-debug` preset is constrained to `Debug` and outputs to `out/build/x64-debug/DEBUG`.
- Non-debug editor-internal script builds and the default `build.bat` path also use `Release` so distributable builds are not accidentally produced as `RelWithDebInfo`.

## Editor Tabs
- The editor exposes a `Notifications` tab that mirrors the in-editor notification history with unread counts and recent toast entries, making editor feedback easier to review after longer workflows.
- The dedicated `Build Pipeline` editor tab (separate dockable tab) has been removed. The `Build Pipeline` category inside `Engine Settings` remains, showing project/environment info, CMake/toolchain status, build profiles, and quick actions.
- The `Build Game` action is accessible directly from the viewport settings dropdown.
- Workspace tools are exposed through a dedicated quick-access `Workspace Tools` popup from the viewport settings dropdown.
- The `Build Game` dialog now rebuilds its widget tree every time it is opened and reads `EntryBar` values correctly, preventing the dialog from appearing blank or losing form content.

## PIE Native C++ Script Build
- Before PIE, native C++ gameplay scripts under `Content/Scripts/Native` are fingerprinted by filename, size, and write time.
- The editor now reuses the last successful `GameScripts.dll` build when those native C++ source/header files have not changed since the previous PIE build.
- Successful PIE builds persist their fingerprint under `Binary/GameScripts/.pie_native_build_fingerprint`.
- Generated helper files such as `_AutoRegister.cpp`, native-script `CMakeLists.txt`, and VS Code IntelliSense config are only rewritten when their content actually changes, avoiding unnecessary timestamp churn.

## Live Physics Editing
- Editor-side changes to `CollisionComponent`, `PhysicsComponent`, `TransformComponent`, and `HeightFieldComponent` continue to mark bodies as physics-dirty for hot-rebuild in PIE.
- Dynamic rigidbodies now keep editor-authored transform and velocity state when such a hot-rebuild was triggered by an explicit editor change, instead of always restoring the previous backend state over the edited ECS values.
- This ensures values like `PhysicsComponent::velocity` and `PhysicsComponent::angularVelocity` are applied correctly to the recreated Jolt body when changed from the editor.
- Dynamic-body mass changes are now also enforced explicitly after Jolt body creation via `MotionProperties::ScaleToMass(...)`, so edited `PhysicsComponent::mass` values reliably affect recreated bodies.

The live runtime/build path is currently **Jolt-only**: `PhysicsWorld` and PIE always initialize Jolt, the editor backend selector is pinned to Jolt, and the `Physics` target links only against `Jolt`.

### Physics Queries
- **Raycast**: Cast a ray and get the closest hit entity, point, normal, distance.
- **Overlap Sphere/Box**: Test a shape at a position and get all overlapping entity IDs.
- **Sweep Sphere/Box**: Sweep a shape along a direction and get the closest hit.

### Forces & Impulses
- `addForce` / `addImpulse`: Apply at center-of-mass (routed through backend for correct physics simulation).
- `addForceAtPosition` / `addImpulseAtPosition`: Apply at a world-space point (backend-level, generates torque).
- `setVelocity` / `setAngularVelocity`: Set directly on the backend body so changes survive the backend readback cycle.
- Python `engine.physics` functions keep the same external signatures, but now route through `PhysicsWorld`/`JoltBackend` instead of applying fake physics directly to ECS state.

### HeightField Collision
- HeightField shapes combine `HeightFieldComponent` scale values with entity transform scale.
- HeightField bodies are always forced to Static motion type.
- Existing rigid bodies are recreated automatically when collision or rigid-body configuration changes, so create-time Jolt settings like motion quality, sleeping, and internal edge removal are applied immediately.
- Older saved scenes upgrade serialized `Discrete` motion quality to `LinearCast` on load.
- Collision-only scene geometry without a `PhysicsComponent` defaults to `Static` rather than `Kinematic`, while sensor-only colliders still use `Kinematic` to produce overlap events.
- Non-dynamic bodies are only re-synced to the backend when their transform changes, preserving stable contact caches for support geometry.
- Editor-side collider/physics/scale changes mark the affected entities in a dedicated physics-dirty list, and `PhysicsWorld` rebuilds only those bodies on the next physics step so PIE reflects the updated body setup immediately.
- `ColliderType::Mesh` now flows real mesh vertex/index data into Jolt. Static mesh colliders use a true Jolt `MeshShape`; non-static mesh colliders use a `ConvexHullShape` approximation instead of collapsing directly to a box.
- Mesh colliders that are not truly static are always routed away from `MeshShape` and onto convex/primitive fallbacks, which avoids illegal moving triangle-mesh setups in Jolt.
- Mesh/heightfield overlap queries now return unique entity IDs instead of repeated entries from subshape-level hits.
- Physics writeback now keeps `TransformComponent` world and local transform data aligned for parented entities, and child transforms are re-propagated after backend sync so hierarchy rendering stays consistent with Jolt motion.

### API Layers
Active runtime path: `JoltBackend` -> `PhysicsWorld` (singleton) -> `GameplayAPI` (C++) -> `INativeScript` (convenience) -> `PhysicsModule.cpp` (Python) -> `engine.pyi` (stubs).

Compatibility layer: `IPhysicsBackend` still exists under `JoltBackend`, but `PhysicsWorld` no longer routes the live simulation/sync path through that abstraction.

`JoltBackend` itself is now also a direct class rather than an active `IPhysicsBackend` subclass; compatibility types from `IPhysicsBackend` are retained only to keep external API/data shapes stable during migration.

Shared runtime physics types now live in `src/Physics/PhysicsTypes.h`. `JoltBackend` uses these types directly, while `IPhysicsBackend` only re-exports them for compatibility-facing layers.
