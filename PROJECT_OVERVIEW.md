# Project Overview

## Actor System (ECS Abstraction)
The engine provides an Unreal-Engine-style **Actor system** that abstracts the low-level ECS for gameplay developers:

- **`Actor`** – Base class wrapping a single `ECS::Entity`. Provides lifecycle events (`beginPlay`, `tick`, `endPlay`, `onBeginOverlap`, `onEndOverlap`), transform convenience methods, parent/child actor hierarchy, and name/tag support.
- **`ActorComponent`** – Base class for modular actor functionality with `onRegister`/`onUnregister`/`tickComponent` lifecycle. Components are attached to actors and automatically manage the underlying ECS components.
- **Built-in Components**: `SceneComponent`, `StaticMeshComponent`, `ActorLightComponent`, `PhysicsBodyComponent`, `CameraActorComponent`, `AudioActorComponent`, `ParticleActorComponent`, `CharacterControllerActorComponent` – each wraps the corresponding ECS component with type-safe getters/setters.
- **Built-in Actors**: `StaticMeshActor`, `PointLightActor`, `DirectionalLightActor`, `SpotLightActor`, `CameraActor`, `PhysicsActor`, `CharacterActor`, `AudioActor`, `ParticleActor` – pre-configured actor subclasses for common game objects.
- **`World`** – Manages actor lifecycle: `spawnActor<T>()` factory, `spawnActorFromAsset()`, `destroyActor()`, per-frame tick dispatch, deferred destruction, actor queries by name/tag/class, and physics overlap event dispatch to actors. Provides a `setScriptAttacher()` callback so native scripts can be attached without a direct dependency on NativeScripting.
- **`ActorRegistry`** – Singleton with `REGISTER_ACTOR(ClassName)` macro for data-driven actor spawning via `World::spawnActorByClass("ClassName")`.
- The Actor system runs alongside the existing `INativeScript` / `NativeScriptManager` system – game developers can choose either approach. The World is ticked in the main loop after physics and overlap dispatch.

## Actor Asset System
The engine supports creating reusable **Actor Assets** (`.asset` files with `AssetType::ActorAsset`) as an alternative to entity-based assets:

- **`ActorAssetData`** (`src/Core/Actor/ActorAssetData.h/.cpp`) – Serializable struct defining an actor template: base actor class, root mesh/material paths, a nestable tree of child actors (`ChildActorEntry`), embedded script info (class name, header/source paths, enabled flag), name, and tag. Each `ChildActorEntry` holds an actor class, mesh/material paths, local position/rotation/scale, and a recursive `children` vector. Legacy component-based formats are auto-migrated on load. Provides `toJson()`/`fromJson()` for JSON serialization.
- **Auto-generated C++ Scripts** – When a new actor asset is created, `ActorAssetData::generateScriptFiles()` automatically creates a C++ header (INativeScript subclass with lifecycle methods) and source file (with `REGISTER_NATIVE_SCRIPT` macro) in the content directory. Existing scripts are never overwritten.
- **Auto-cleanup on Delete** – When an actor asset is deleted, `ActorAssetData::cleanupScriptFiles()` removes the auto-generated script files. This is triggered automatically from `AssetManager::deleteAsset()`.
- **`ActorEditorTab`** (`src/Renderer/EditorTabs/ActorEditorTab.h/.cpp`) – Dedicated editor tab with a 3D preview viewport (left) and a sidebar (right). The preview viewport renders the actor's mesh hierarchy using a dedicated `EngineLevel` with the same camera controls as the main viewport (right-click rotation, WASD movement, laptop mode, scroll wheel speed). The sidebar contains sections for Actor Class selection (with root mesh/material), Child Actors (add/remove nestable child actors of various types), and Script details. Transform is not displayed in the editor tab (only available when the actor is placed in a level).
- **Preview Viewport Level Swap** – The ActorEditorTab uses the same level-swap pattern as MeshViewer/MaterialEditor: `takeRuntimeLevel()`/`giveRuntimeLevel()` manage a preview `EngineLevel` that is swapped into the active level slot when the tab is activated, allowing the existing renderer and camera system to display the actor preview.
- **Content Browser Integration** – Actor assets can be created via the "New Actor" context menu item and opened by double-clicking in the content browser. Actors appear in the content browser grid with a distinctive icon and tint, support the type filter dropdown ("Actor" filter), and can be renamed inline via the Rename button (renaming also updates the embedded script file names and class name).
- **Spawning** – `World::spawnActorFromAsset()` instantiates an actor from an asset definition, recursively spawning child actors from the `childActors` hierarchy, setting mesh/material per actor, applying local transforms, attaching children to their parent, and attaching the embedded native script via a callback mechanism.

## Physics System
The engine provides a physics system layered around `IPhysicsBackend`, but the active implementation/build is currently Jolt-only:
- **Jolt Physics** (default) - primary backend, LinearCast CCD by default, enhanced internal edge removal for smooth heightfield interaction, tuned solver settings (Baumgarte 0.35, 12 velocity steps, 8 position steps, max penetration distance 1.0, 4 collision sub-steps, speculative contact distance 0.05) for robust collision handling and penetration recovery

### Constraint Authoring Groundwork
- Entities can now carry a `ConstraintComponent` that stores joint authoring data for hinge, ball-socket, fixed, slider, distance, spring, and cone constraints.
- Saved levels preserve connected entity ids, anchors, axis, limits, spring tuning, and breakability thresholds.
- The editor details panel supports adding/removing and editing this constraint data directly.

### Runtime Constraints
- The current runtime path supports live `Fixed`, `Hinge`, `Distance`, `BallSocket`, `Slider`, `Spring`, and `Cone` constraints through Jolt.
- `PhysicsWorld` creates/removes these constraints automatically from `ConstraintComponent` data after body sync.
- Fixed constraints now preserve the initial relative attachment more robustly instead of collapsing both bodies toward each other by default.
- Hinge constraints use local authoring anchors/axis data, optional angle limits, and Jolt spring settings for soft limits.
- Distance constraints keep two authored anchor points within a min/max range; leaving both limits at `0` now auto-preserves the current distance when the joint is created.
- Ball-socket and cone constraints use shared world anchors to avoid conflicting per-body target points.
- Slider constraints default to an unrestricted range when authored with `0/0` limits and use derived normal axes for a stable constraint frame.
- Spring constraints are implemented through Jolt distance constraints with active spring settings.
- Constraint teardown now follows Jolt's reference-counted ownership model correctly, avoiding PIE-exit crashes when active constraints are removed during physics shutdown.
- Constraint authoring in the editor now uses a scene-entity dropdown for the connected target instead of requiring raw entity-id entry.
- A single entity can now own multiple constraint entries, so one object can be attached to several others without spawning helper entities or overwriting previous constraint data.
- The runtime now internally substeps physics updates for better stability when constrained dynamic bodies collide while rotating, reducing visible penetration on impact.

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
