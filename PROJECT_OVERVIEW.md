# Project Overview

## Actor System (ECS Abstraction)
The engine provides an Unreal-Engine-style **Actor system** that abstracts the low-level ECS for gameplay developers:

- **`Actor`** – Base class wrapping a single `ECS::Entity`. Provides lifecycle events (`preInitializeComponents`, `postInitializeComponents`, `beginPlay`, `tick`, `endPlay(EEndPlayReason)`, `onBeginOverlap`, `onEndOverlap`), transform convenience methods, parent/child actor hierarchy, `NameID`-backed name/tag (O(1) comparisons via interned strings), per-actor `TickSettings` (tick group, interval, enable/disable), deferred spawning via `finishSpawning()`, tick dependency DAG via `addTickPrerequisite()`, `ActorHandle` weak reference (generation-safe), and `onDestroyed` delegate. Components added at runtime automatically receive their `beginPlay()` call.
- **`ActorComponent`** – Base class for modular actor functionality with `onRegister`/`onUnregister`/`tickComponent` lifecycle. Components are attached to actors and automatically manage the underlying ECS components.
- **Built-in Components**: `SceneComponent`, `StaticMeshComponent`, `ActorLightComponent`, `PhysicsBodyComponent`, `CameraActorComponent`, `AudioActorComponent`, `ParticleActorComponent`, `CharacterControllerActorComponent` – each wraps the corresponding ECS component with type-safe getters/setters.
- **Built-in Actors**: `StaticMeshActor`, `PointLightActor`, `DirectionalLightActor`, `SpotLightActor`, `CameraActor`, `PhysicsActor`, `CharacterActor`, `AudioActor`, `ParticleActor` – pre-configured actor subclasses for common game objects.
- **`World`** – Manages actor lifecycle: `spawnActor<T>()` factory, `spawnActorDeferred<T>()` for deferred spawning, `spawnActorFromAsset()`, `destroyActor()`, per-frame tick dispatch with `tickGroup(ETickGroup)` for phased ticking (PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork) using topological sort (Kahn's algorithm) for tick dependency ordering, deferred destruction via `flushDestroys()`, `beginPlay()` for world initialization, `NameID`-based queries (`findActorByNameID`, `findActorsByTagID` for O(1) comparisons), `resolveHandle()` for safe weak references, Game Framework integration (`setGameMode<T>()`, `getGameMode()`, `getGameState()`), and physics overlap event dispatch to actors. Provides a `setScriptAttacher()` callback so native scripts can be attached without a direct dependency on NativeScripting.
- **`ActorRegistry`** – Singleton with `REGISTER_ACTOR(ClassName)` macro for data-driven actor spawning via `World::spawnActorByClass("ClassName")`.
- **Tick Groups** – Actors can choose when they tick relative to physics via `ETickGroup` (PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork). The main loop dispatches: PrePhysics → Physics step → DuringPhysics → Overlap events → PostPhysics → PostUpdateWork → Destroy flush → Python scripts. Per-actor tick interval throttling is supported via `TickSettings::tickInterval`.
- **Tick Dependencies** – Actors can declare ordering constraints via `addTickPrerequisite(other)` so they tick after their prerequisites within the same tick group. World uses topological sort (Kahn's algorithm) per tick group; cycles are detected and actors in cycles still tick (in array order) with a fallback.
- **NameID System** (`src/Core/NameID.h`) – Interned string identifiers (4 bytes) with a global `NamePool` singleton. O(1) comparisons via integer index instead of string comparisons. Used for Actor names and tags; available engine-wide for asset paths, component IDs, etc.
- **ObjectHandle System** (`src/Core/ObjectHandle.h`) – Generation-counted weak handles (8 bytes: index + generation) with a chunked `ObjectSlotArray` (4096 slots/chunk, free-list recycling). Detects stale references without dangling pointers. `TObjectHandle<T>` provides typed wrapper with `get()`, `isValid()`, `operator->`. `ActorHandle` is the Actor-specific typedef.
- **Game Framework** (`src/Core/Actor/GameFramework/`) – UE-inspired role separation for gameplay architecture:
  - `GameMode` – Defines game rules, exists once per World. Override to define pawn/controller classes, player join/leave handling, match flow.
  - `GameState` – Holds game-wide state (match time, scores). Visible to all systems. Created automatically with the GameMode.
  - `PlayerController` – Represents the human player's will. Survives Pawn death/respawn. Manages possession via `possess(Pawn*)` / `unpossess()`.
  - `Pawn` – Possessable physical representation. Receives `onPossessedBy` / `onUnpossessed` callbacks.
- **EEndPlayReason** – Enum passed to `endPlay()` indicating why the actor is shutting down: `Destroyed`, `LevelUnloaded`, `Quit`, or `RemovedFromWorld`.
- The Actor system runs alongside the existing `INativeScript` / `NativeScriptManager` system – game developers can choose either approach.

## Actor Asset System
The engine supports creating reusable **Actor Assets** (`.asset` files with `AssetType::ActorAsset`) as an alternative to entity-based assets:

- **`ActorAssetData`** (`src/Core/Actor/ActorAssetData.h/.cpp`) – Serializable struct defining an actor template: base actor class, root mesh/material paths, root transform (position/rotation/scale), tick settings (canEverTick, tickGroup, tickInterval), a nestable tree of child actors (`ChildActorEntry`), embedded script info (class name, header/source paths, enabled flag), name, and tag. Each `ChildActorEntry` holds an actor class, mesh/material paths, local position/rotation/scale, and a recursive `children` vector. Legacy component-based formats are auto-migrated on load. Provides `toJson()`/`fromJson()` for JSON serialization and bidirectional binary serialization via `Archive` (version 2).
- **Auto-generated C++ Scripts** – When a new actor asset is created, `ActorAssetData::generateScriptFiles()` automatically creates a C++ header (INativeScript subclass with lifecycle methods) and source file (with `REGISTER_NATIVE_SCRIPT` macro) in the content directory. Existing scripts are never overwritten.
- **Auto-cleanup on Delete** – When an actor asset is deleted, `ActorAssetData::cleanupScriptFiles()` removes the auto-generated script files. This is triggered automatically from `AssetManager::deleteAsset()`.
and **Embedded Script** (class/paths/enable toggle). All sections are visible simultaneously with dividers (no section switching). The preview viewport uses a dark gray background (distinct from the main viewport's black), renders colored origin axis lines (X=red, Y=green, Z=blue), and always shows the ground grid (independent of the main viewport's snap/grid setting) for spatial orientation. Supports drag-drop of meshes and materials into the viewport, with gizmo transforms syncing back to ActorAssetData.
- **Preview Viewport Level Swap** – The ActorEditorTab uses the same level-swap pattern as MeshViewer/MaterialEditor: `takeRuntimeLevel()`/`giveRuntimeLevel()` manage a preview `EngineLevel` that is swapped into the active level slot when the tab is activated, allowing the existing renderer and camera system to display the actor preview.
- **Content Browser Integration** – Actor assets can be created via the "New Actor" context menu item and opened by double-clicking in the content browser. Actors appear in the content browser grid with a distinctive icon and tint, support the type filter dropdown ("Actor" filter), and can be renamed inline via the Rename button (renaming also updates the embedded script file names and class name).
- **Spawning** – `World::spawnActorFromAsset()` instantiates an actor from an asset definition, recursively spawning child actors from the `childActors` hierarchy, setting mesh/material per actor, applying local transforms, attaching children to their parent, and attaching the embedded native script via a callback mechanism.

## Archive System (Bidirectional Serialization)
The engine provides a bidirectional **Archive** system (`src/Core/Archive.h/.cpp`) where the same `serialize()` call works for both saving and loading – the direction is determined by `ar.isLoading()` / `ar.isSaving()`:

- **`Archive`** – Abstract base class with `operator<<` for all fundamental C++ types (bool, int, float, double, unsigned variants, etc.), length-prefixed `std::string`, `serializeFloatArray<N>()` for fixed-size C arrays, `serializeVector<T>()` for trivially-copyable vectors, `serializeStringVector()`, and `serializeVersion()` for format versioning. Error state is tracked via `hasError()`.
- **`FileArchiveWriter`** – Writes binary data to a file (`std::ofstream`). Used for binary asset export and runtime builds.
- **`FileArchiveReader`** – Reads binary data from a file (`std::ifstream`). Used for binary asset import.
- **`MemoryArchive`** – Reads/writes to an in-memory `std::vector<uint8_t>` buffer. Ideal for undo/redo snapshots and network packets. Supports `takeBuffer()` for zero-copy extraction and `resetRead()` / `resetWrite()` for reuse.
- **ECS Component Serialization** (`src/Core/ECS/ComponentSerialization.h`) – Free `serialize(Archive&, Component&)` functions for all 14 ECS component types (Transform, Mesh, Material, Light, Camera, Collision, Physics, Constraint, Logic, Name, HeightField, Animation, Lod, CharacterController, AudioSource, ParticleEmitter). Each includes a version tag for forward-compatible deserialization. Runtime-only state (e.g. `CharacterControllerComponent::isGrounded`, `ParticleEmitterComponent::emissionAccumulator`) is excluded.
- **ActorAssetData Serialization** – `ActorAssetData::serialize(Archive&)` and `ChildActorEntry::serialize(Archive&)` provide bidirectional binary serialization alongside the existing JSON `toJson()`/`fromJson()` methods. JSON remains the editor format; binary is available for runtime/builds.
- **Design**: JSON remains the primary editor format for human readability; the Archive system provides a parallel binary path for runtime performance, undo snapshots, and future network serialization.

## Reflection System
The engine includes a macro-based runtime reflection system (`src/Core/Reflection.h`) for automatic property enumeration, editor UI generation, and future serialization:
- **TypeID** enum – Describes property value kinds: `Float`, `Int`, `Bool`, `String`, `Vec3`, `Vec2`, `Color3`, `Color4`, `Enum`, `AssetPath`, `EntityRef`, `Custom`.
- **PropertyFlags** – Bitwise flags controlling editor/serialization behaviour: `PF_EditAnywhere`, `PF_VisibleOnly`, `PF_Transient`, `PF_Hidden`.
- **PropertyInfo** struct – Stores name, TypeID, byte offset, size, flags, category, optional clamp range, and enum entries. Provides typed `ptrIn<T>()` to access members at runtime.
- **ClassInfo** struct – Groups all `PropertyInfo` entries for a reflected class, with an optional `superClass` pointer for inheritance chains.
- **TypeRegistry** singleton – Maps `std::type_index` to `ClassInfo*`. Supports lookup by type, type_index, or class name string. All registrations occur via static initialization.
- **REFLECT macros** – `REFLECT_BEGIN(ClassName, DisplayName)` / `REFLECT_END(ClassName)` with per-type macros (`REFLECT_FLOAT`, `REFLECT_VEC3`, `REFLECT_ENUM`, etc.). `REFLECT_BEGIN_NESTED` / `REFLECT_END_NESTED` handle `::` in nested type names.
- **ECS Component Registration** (`src/Core/ECS/ComponentReflection.h`) – All 16+ ECS component types (plus nested structs like `MaterialOverrides`, `ConstraintEntry`, `LodLevel`) are reflected with full property metadata, enum tables, categories, and appropriate flags.
- **ReflectionWidgetFactory** (`src/Renderer/EditorTabs/ReflectionWidgetFactory.h/.cpp`) – Generates themed editor `WidgetElement` objects from `ClassInfo` metadata: Float→FloatRow/Slider, Int→IntRow, Bool→CheckBox, String→StringRow, Vec3→Vec3Row, Color3/4→ColorPicker, Enum→DropDown. Skips `PF_Hidden` properties automatically.

## Transaction System (Snapshot-Based Undo/Redo)
The engine provides a snapshot-based **Transaction** system (`src/Core/TransactionManager.h/.cpp`) that layers on top of the existing `UndoRedoManager` to offer before/after byte-snapshot undo/redo:

- **`TransactionManager`** – Singleton that manages the lifecycle of a transaction. `beginTransaction(desc)` / `endTransaction()` bracket property changes; on end the "after" state is captured and a single `UndoRedoManager::Command` is pushed. No nested transactions.
- **Two recording modes**:
  - `recordSnapshot(void* target, size_t size)` – Raw memcpy for stable addresses (globals, editor settings). Captures "before" immediately; "after" is captured at `endTransaction()`.
  - `recordEntry(capture, restore)` – Lambda-based for relocatable data (e.g. ECS components looked up by entity ID). `capture()` is called at record time (before) and at end (after); `restore(data)` is called on undo/redo.
- **`addPostRestoreCallback()`** – Registers side-effect callbacks (physics invalidation, UI refresh) that fire after every undo or redo of the transaction.
- **`ScopedTransaction`** – RAII guard: constructor calls `beginTransaction`, destructor calls `endTransaction`. Provides `snapshot()`, `entry()`, and `onRestore()` convenience methods.
- **Integration** – Committed transactions are stored as `UndoRedoManager::Command` entries, so the existing undo/redo stack, Ctrl+Z/Y shortcuts, depth limit (100), and `onChanged` callback all work transparently.
- **Design** – Uses `std::shared_ptr` for committed snapshot data so both redo (execute) and undo lambdas share the same before/after buffers without duplication.

## CDO System (Class Default Objects)
The engine provides a **Class Default Object (CDO)** system (`src/Core/Actor/CDO.h/.cpp`) that stores per-Actor-class default ECS component values as templates for delta serialization and editor override detection:

- **`ClassDefaultObject`** – Stores typed component defaults for one registered Actor class via type-erased `IComponentDefault`/`TComponentDefault<T>` wrappers (handles non-trivial types like `std::string` correctly). Provides `getDefault<T>()` for read access, `getMutableDefault<T>()` for CDO editing, `isPropertyOverridden(prop, instance)` for single-property delta detection, and `getOverriddenProperties(instance)` to find all properties differing from CDO. Also captures default `TickSettings`.
- **`CDORegistry`** – Singleton that manages CDOs for all registered Actor classes. `buildAll()` iterates `ActorRegistry::getRegisteredClassNames()`, temporarily constructs each Actor type (creates temp ECS entity, initializes, runs `beginPlay()` to set up default components, captures all 16 ECS component types), then cleans up (releases ObjectHandle, removes entity, deletes actor). `getCDO(className)` for lookup. Built automatically in `World::beginPlay()` (once).
- **CDO Construction** – Each Actor class is temporarily spawned in a sandbox (temp ECS entity, `World*` = nullptr) so `beginPlay()` runs and sets up components with their class-specific defaults (e.g. `PointLightActor` sets light type to Point). All ECS component state is then captured as typed snapshots.
- **Reflection Utilities** (`src/Core/Reflection.h/.cpp`) – `reflectPropertyEquals(prop, a, b)` compares a single reflected property between two instances, `reflectCopyProperties(info, src, dst)` copies all reflected properties, `reflectDiffProperties(info, a, b)` finds all differing properties. TypeID-aware: handles Float, Int, Bool, String, Vec3, Vec2, Color3/4, Enum, AssetPath, EntityRef, Custom.
- **Use Cases** – Delta serialization (only save values that differ from CDO defaults), editor override display (highlight non-default properties), reset-to-default (restore a property to its CDO value), CDO editing (modify class-level defaults for all future instances).

## Archetype ECS – Mass-Entity-System
The engine provides a standalone **Archetype-based ECS** subsystem (`src/Core/ECS/Archetype/`) for mass simulation of >10K homogeneous entities alongside the existing sparse-set ECS:

- **Fragment System** – `FragmentTypeID` compile-time unique IDs via `getFragmentTypeID<T>()`, `FragmentRegistry` singleton for runtime type info (size, alignment, name), `MASS_FRAGMENT()` macro for registration.
- **Chunk-based SoA Storage** (`Chunk.h`) – ~64KB chunks with cache-line aligned columns per fragment type. `computeChunkLayout()` computes optimal entity capacity. Supports push, swap-remove, and cross-chunk row copy.
- **Archetype** (`Archetype.h`) – Groups all entities with the same fragment composition. Manages a pool of chunks, provides typed fragment access, and supports query matching with required/excluded signatures.
- **`MassEntitySubsystem`** (`ArchetypeECS.h/.cpp`) – Singleton: entity CRUD (create/batch-create/destroy), archetype graph with signature-hash lookup, structural change migration (`addFragment`/`removeFragment` moves entity between archetypes copying shared data), `forEachChunk()` queries, processor tick loop, `MassCommandBuffer` for deferred changes.
- **`MassEntity`** handle – 8 bytes (index + generation), `MassEntitySlot` with free-list recycling. Generation-counted for stale reference detection.
- **Processors** (`MassProcessor.h`) – `MassQuery` (require/exclude fragments), `ChunkView` (typed array access), `MassProcessor` base class with `configure()`/`executeChunk()` pattern for stateless batch processing.
- **LOD Bridge** (`MassBridge.h/.cpp`) – Distance-based promotion/demotion between pure MassEntity data and full Actors. `ERepresentationLOD` (High/Medium/Low/Off), `LODConfig` with hysteresis to prevent flickering, `registerClass()` with custom promotion/demotion callbacks, `spawnMassAgents()` for batch creation.

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

## Animation Blending System
The engine supports multi-layer skeletal animation blending with crossfade transitions and per-bone masking:

- **`AnimationBlending.h`** (`src/Core/AnimationBlending.h`) – Core data structures: `BlendEntry` (clipIndex/time/speed/weight/loop/playing), `CrossfadeState` (active/duration/elapsed), `BoneMask` (std::bitset<128>), `AnimBlendMode` (Override/Additive), `AnimationLayer` (current + previous blend entries, crossfade, bone mask, blend mode, layer weight, name), `kMaxAnimLayers = 4`.
- **`SkeletalAnimator`** (`src/Core/SkeletalData.h`) – Multi-layer animation evaluator. Per-bone `NodePose` (pos/rot/scl) decomposition, `evaluateEntryPoses()` from keyframes, `computeBlendedBoneMatrices()` with smooth-step crossfade easing, bone mask filtering, Override/Additive blend mode support. `decomposeNodeTransform()` uses Shepperd's quaternion extraction. `blendPose()` uses lerp for position/scale and slerp for rotation. Backward-compatible: `playAnimation()`/`stop()` still work on layer 0.
- **`AnimationComponent`** – Extended with crossfade fields (crossfadeTargetClip, crossfadeDuration, crossfadeRequested), `LayerState` sub-struct (clipIndex/speed/weight/loop/active), `layers[4]`, `layerCount`.
- **Renderer**: 8 new virtual functions in `Renderer.h` (crossfadeEntityAnimation, isEntityCrossfading, playEntityAnimationOnLayer, stopEntityAnimationLayer, setEntityLayerWeight, get/setEntityAnimationLayerCount, findEntityAnimationClipByName). All implemented in `OpenGLRenderer`.
- **GameplayAPI**: 13 new C++ scripting functions (isEntitySkinned, getAnimationClipCount, findAnimationClipByName, playSkeletalAnimation, stopSkeletalAnimation, setSkeletalAnimationSpeed, crossfadeAnimation, isCrossfading, playAnimationOnLayer, stopAnimationLayer, setAnimationLayerWeight, get/setAnimationLayerCount).
- **Python**: `engine.animation` module with 13 functions (is_skinned, get_clip_count, find_clip, play, stop, set_speed, crossfade, is_crossfading, play_on_layer, stop_layer, set_layer_weight, get_layer_count, set_layer_count). `engine.pyi` includes full IntelliSense stubs.
- **Planned**: Animation State Machine, Blend Trees, AnimationController asset type, and visual editor graph.

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
