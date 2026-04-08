# Engine Status

## Editor Finalization: Notifications Tab (Implemented)
- Added a `Notifications` editor tab that exposes the stored notification history, unread count, relative age, and severity coloring in a persistent reviewable view.
- The dedicated `Build Pipeline` editor tab (separate dockable tab) has been removed. The `Build Pipeline` category inside `Engine Settings` remains, showing CMake/toolchain status, build profiles, and quick actions.
- The `Build Game` action is also accessible directly from the viewport settings dropdown.

## Editor Settings Menu Cleanup (Implemented)
- Moved editor tab/tool shortcuts into a separate quick-access `Workspace Tools` popup reachable from the viewport settings dropdown, keeping top-level menu clutter low.

### Follow-up Fix: `Build Game` dialog no longer opens blank
- **Problem**: The `Build Game` popup could reopen in a stale state and its `EntryBar` field values were written/read through the wrong member, which could make the dialog appear empty or lose its visible form state.
- **Fix**: The dialog now unregisters and rebuilds its `BuildGameForm` widget each time it opens, writes default text into `EntryBar::value`, and reads the selected start level / title back from the proper UI fields.
- **Result**: `Build Game` opens reliably with its full form UI again.

## PIE Native C++ Build Skip on Unchanged Sources (Implemented)
- The editor-side PIE path now computes a fingerprint for native C++ gameplay script inputs in `Content/Scripts/Native` using file name, size, and last-write time for `.cpp`, `.h`, and `.hpp` files.
- If that fingerprint matches the last successful PIE native-script build and `Engine/GameScripts.dll` is still present, the editor skips the pre-PIE C++ rebuild and immediately reuses the previous DLL.
- Successful native-script builds store the current fingerprint in `Binary/GameScripts/.pie_native_build_fingerprint`.
- `_AutoRegister.cpp`, the generated native-script `CMakeLists.txt`, and the VS Code IntelliSense config are now only rewritten when their content actually changes, preventing generated-file timestamp noise from forcing unnecessary rebuild work.

## Bug Fix 20: Editor-side physics value changes now survive dynamic body hot-rebuilds
- **Problem**: Most `CollisionComponent` / `PhysicsComponent` values already flowed into `PhysicsWorld` and into Jolt correctly, but one editor-live-edit gap remained for existing dynamic bodies. Editor changes marked the entity as physics-dirty and forced a rebuild, yet the dynamic-body rebuild path always restored transform and velocities from the old backend state. That meant editor-authored `PhysicsComponent::velocity` / `angularVelocity` changes — and dynamic transform edits during the same rebuild path — could be silently overwritten.
- **Fix**: `PhysicsWorld` now tracks which bodies were explicitly dirtied by editor-side physics edits during the current step. When such a dynamic body is rebuilt, the edited ECS transform/velocity values are kept and passed into the recreated `JoltBackend::BodyDesc` instead of being replaced unconditionally by the pre-rebuild backend state.
- **Result**: Collision/physics property edits still hot-rebuild as before, but explicit editor changes to dynamic-body transform and initial velocities now apply directly to the live Jolt body as expected.

### Follow-up Fix: Dynamic mass changes now enforced after Jolt body creation
- **Problem**: `PhysicsComponent::mass` already participated in the rebuild hash and body descriptor, but edited mass values could still appear ineffective on recreated dynamic bodies.
- **Fix**: After creating and adding a dynamic Jolt body, the backend now explicitly calls `MotionProperties::ScaleToMass(desc.mass)` through a write lock. This directly rescales mass/inertia on the live Jolt body to the requested editor value.
- **Result**: Changing mass in the editor now reliably affects the recreated dynamic rigidbody.

## Physics Queries - Overlap & Sweep (Implemented)
- Added `overlapSphere` and `overlapBox` to test a shape against the world and return all overlapping entity IDs.
- Added `sweepSphere` and `sweepBox` to sweep a shape along a direction and return the closest hit (same result format as raycast).
- Implemented across all layers: IPhysicsBackend, JoltBackend (NarrowPhaseQuery CollideShape/CastShape), PhysXBackend (PxScene::overlap/sweep), PhysicsWorld, GameplayAPI, INativeScript, PhysicsModule.cpp (Python), engine.pyi.

## Force / Impulse at Position (Implemented)
- Added `addForceAtPosition` and `addImpulseAtPosition` to apply forces/impulses at a specific world-space position, generating torque.
- Backend-level: Jolt BodyInterface::AddForce/AddImpulse with position, PhysX PxRigidBodyExt::addForceAtPos.
- Available in all layers: backend interface, both backends, PhysicsWorld, GameplayAPI, INativeScript, Python bindings, engine.pyi.

## Rigid Body Simulation Fixes (Implemented)
Three bugs fixed that caused unreliable rigid body simulations:

### Bug Fix 1: Force/Impulse/Velocity now routed through physics backend
- **Problem**: `addForce()`, `addImpulse()`, `setVelocity()`, `setAngularVelocity()` in GameplayAPI only modified ECS component data (`pc->velocity`). The backend readback (`syncBodiesFromBackend`) overwrote these changes every frame, making forces/impulses/velocity-sets effectively no-ops for existing bodies.
- **Fix**: All four functions now route through `PhysicsWorld` ? `IPhysicsBackend` to apply directly to the physics engine. Added new `addForce(handle, fx, fy, fz)` and `addImpulse(handle, ix, iy, iz)` pure virtual methods to the backend interface, implemented in both Jolt (`BodyInterface::AddForce/AddImpulse`) and PhysX (`PxRigidBody::addForce`). `setVelocity`/`setAngularVelocity` now also call backend `setLinearVelocity`/`setAngularVelocity`.

### Bug Fix 2: Correct position readback with collider offset (Jolt)
- **Problem**: `JoltBackend::getBodyState()` used `GetCenterOfMassPosition()` which includes the collider offset (from `OffsetCenterOfMassShape`), causing entities with non-zero `colliderOffset` to shift by that offset after the first simulation step.
- **Fix**: Changed to `GetPosition()` which returns the body's reference position without the COM offset.

### Bug Fix 3: HeightField scale and motion type
- **Problem**: HeightField shapes ignored the entity's transform scale (`tc->scale`), while all other shapes (Box, Sphere, Capsule, Cylinder) correctly incorporated it. This caused a mismatch between the visual terrain mesh and the physics heightfield when the entity was scaled. Additionally, HeightField bodies could be set to Dynamic/Kinematic motion types, causing terrain to fall or behave incorrectly.
- **Fix**: HeightField scales (`hfc->scaleX/Y/Z`) are now multiplied by `tc->scale[0/1/2]`. HeightField bodies are forced to Static motion type.

### Bug Fix 4: Rotated bodies getting stuck (Jolt penetration recovery)
- **Problem**: Bodies placed with steep initial rotation (e.g. a tilted box whose corners penetrate the ground) would bug out and get stuck instead of being properly pushed back. Default Jolt solver settings (Baumgarte = 0.2, 2 position steps, 1 collision step) resolved penetration too slowly for deep initial overlaps, so gravity and friction could trap the body. This was especially bad for heightfield terrain (one-sided triangle surfaces) where deep penetrations cause back-face contacts with inverted normals, trapping the solver entirely.
- **Fix (solver tuning)**: Tuned `PhysicsSettings` after `PhysicsSystem::Init()`: increased `mBaumgarte` to 0.35 (faster penetration recovery) and `mNumPositionSteps` to 4 (more position correction passes). Increased collision sub-steps in `Update()` from 1 to 2, giving the solver two full collision-detection-and-resolution passes per physics frame.
- **Fix (initial depenetration)**: After creating a dynamic body, `CollideShape` is used to detect overlaps with existing geometry. If the body starts inside another shape (e.g. heightfield), it is immediately moved along the inverse penetration axis to resolve the overlap before simulation begins. This prevents the iterative solver from having to deal with deep initial penetrations at all.

### Bug Fix 5: Runtime penetration through heightfield (LinearCast CCD)
- **Problem**: A dynamic body starting in the air could fall onto another body, bounce off at an angle, and hit the heightfield ground edge-first at high velocity. With `Discrete` motion quality, the body moves its full timestep without intermediate collision checks and can penetrate through the one-sided heightfield surface between frames. Once through, back-face contacts produce inverted normals — the solver oscillates (visible as glitching), then the body sleeps in a stuck state.
- **Fix (CCD default)**: Changed the default `PhysicsComponent::motionQuality` from `Discrete` to `LinearCast`. LinearCast performs a swept shape-cast from the old to the new position every simulation step, detecting the heightfield surface *before* the body passes through it. This is Jolt's recommended approach for bodies interacting with thin or one-sided surfaces.
- **Fix (speculative contacts)**: Increased `mSpeculativeContactDistance` from 0.02 (default) to 0.05 in PhysicsSettings. This creates a wider buffer zone around contact surfaces, allowing Jolt to generate stabilizing contact constraints before bodies actually touch edges, reducing jitter on angled landings.

### Bug Fix 6: Bodies getting stuck on heightfield edges (internal edge ghost contacts)
- **Problem**: A dynamic body rolling or sliding across a heightfield terrain would get stuck on internal triangle edges. Heightfields are tessellated into triangles, and the edges shared between adjacent triangles ("internal edges") are implementation artifacts that shouldn't block movement. Without enhanced edge removal, Jolt generates collision contacts against these internal edges ("ghost contacts"), creating artificial barriers. A cube bouncing off another body and landing on the heightfield at an angle would catch on these edges, glitch briefly, and then stop moving — even with sleeping disabled.
- **Fix**: Enabled `mEnhancedInternalEdgeRemoval = true` on all dynamic/kinematic `BodyCreationSettings` in `JoltBackend::createBody()`. This flag tells Jolt to perform additional analysis when generating contacts to identify and discard ghost contacts from internal mesh/heightfield edges. Since Jolt evaluates this flag as an OR between both bodies in a contact pair, setting it on the dynamic body ensures enhanced edge removal is active for all its collisions with heightfields and triangle meshes. Combined with the existing LinearCast CCD (Bug Fix 5), this provides smooth, artifact-free rolling and sliding on heightfield terrain.

### Bug Fix 7: Existing bodies ignored edited physics settings
- **Problem**: Several important Jolt settings in this project are creation-time settings (`mMotionQuality`, `mAllowSleeping`, `mEnhancedInternalEdgeRemoval`, shape/material configuration). The editor changed the ECS component values, but `PhysicsWorld::syncBodiesToBackend()` did not rebuild existing rigid bodies when those values changed. As a result, toggling `Allow Sleeping`, switching `Motion Quality`, or changing collider/physics settings in the details panel often had no effect on the actual live Jolt body.
- **Fix**: Added a body-configuration hash in `PhysicsWorld`. When collision or rigid-body settings change, the old backend body is removed and recreated with the new `BodyDesc`. For dynamic bodies, the current simulated position, rotation, and velocities are preserved across the rebuild so the body keeps moving naturally.
- **Fix (scene migration)**: Old saved `PhysicsComponent` data that still serialized `motionQuality = Discrete` is now upgraded to `LinearCast` during deserialization, so previously saved scenes no longer keep using the outdated collision mode.

### Bug Fix 8: Collision-only support geometry behaved like moving kinematic bodies
- **Problem**: Entities with `CollisionComponent` but without `PhysicsComponent` were created as `Kinematic` bodies instead of `Static`. That made ordinary support geometry (e.g. stacked cubes used as platforms) behave like teleported kinematic bodies rather than stable world geometry. On top of that, non-dynamic bodies were pushed through `setBodyPositionRotation()` every sync even when their transform had not changed, disturbing Jolt's contact caching and making edge contacts much less stable.
- **Fix**: Collision-only entities now default to `Static`. Only sensors without a `PhysicsComponent` are still promoted to `Kinematic` so overlap events continue to work.
- **Fix (transform sync)**: Added a transform hash so existing non-dynamic bodies are only updated in the backend when their transform actually changes. Static support bodies are no longer unnecessarily re-submitted every frame.

### Bug Fix 9: Dynamic bodies could still partially tunnel into static support cubes
- **Problem**: Even after enabling `LinearCast` and fixing support geometry motion types, fast box-vs-box impacts could still leave a dynamic cube partially embedded in a static cube. Jolt's own `PhysicsSettings` documentation shows two settings that matter directly here: `mMaxPenetrationDistance` limits how much overlap can be corrected in a single solver position iteration, and `mNumVelocitySteps` / `mNumPositionSteps` determine how much contact resolution work is done per simulation step. The previous tuning was still too conservative for these stacked-cube edge cases.
- **Fix (global solver)**: Increased Jolt solver settings to `mNumVelocitySteps = 12`, `mNumPositionSteps = 8`, and `mMaxPenetrationDistance = 1.0f`. This gives the solver more contact iterations and allows it to recover from deeper convex-convex penetration in one frame.
- **Fix (collision sub-steps)**: Increased `PhysicsSystem::Update()` collision steps from `2` to `4`, matching Jolt's recommendation that larger effective timesteps require additional collision steps for stability.
- **Fix (per-body override)**: Dynamic bodies now set `mNumVelocityStepsOverride = 12` and `mNumPositionStepsOverride = 8` in `BodyCreationSettings`, so colliding rigidbodies explicitly request stronger solving even if global defaults change later.

### Bug Fix 10: Runtime physics path is now Jolt-first
- **Problem**: The physics architecture was still runtime-switchable between backends. That kept the live execution path split between Jolt and PhysX even though stabilization work is currently focused entirely on Jolt. Persisted editor state could still request `PhysX`, making physics behavior depend on configuration instead of the hardened Jolt path.
- **Fix**: `PhysicsWorld` now falls back to `Jolt` for any non-Jolt backend request. PIE initialization also rewrites persisted backend state to `Jolt`, and the editor physics settings only expose `Jolt` as the selectable runtime backend. The abstraction layer remains in place, but the active runtime path is now consistently Jolt-focused.

### Bug Fix 11: Physics target is now built Jolt-only
- **Problem**: Even after the runtime path was pinned to Jolt, the `Physics` target still conditionally compiled and linked the PhysX backend. That kept unnecessary backend code, build dependencies, and compile-time feature switches in the active physics module.
- **Fix**: Removed conditional PhysX source inclusion and PhysX link dependencies from `src/Physics/CMakeLists.txt`. The physics module now builds against `Jolt` only. `PhysicsWorld` keeps the backend selector type only for API compatibility, but the exposed selector is now effectively `Jolt`-only.

### Bug Fix 12: `PhysicsWorld` active path no longer routes through `IPhysicsBackend`
- **Problem**: Even after moving runtime/build behavior to Jolt-only, `PhysicsWorld` still used `IPhysicsBackend` as its active member type and still built all body/character descriptors through `IPhysicsBackend::BodyDesc` / `CharacterDesc`. That left the main simulation path coupled to the old backend abstraction rather than directly to the actual runtime implementation.
- **Fix**: `PhysicsWorld` now owns a `std::unique_ptr<JoltBackend>` and uses `JoltBackend` descriptor, handle, and invalid-handle types directly throughout the active codepath. The old interface remains only as a compatibility layer under `JoltBackend`, but the world update/sync path itself is now explicitly Jolt-oriented.

### Bug Fix 13: Python physics API now routes through the Jolt runtime path
- **Problem**: The Python physics module still manipulated ECS `PhysicsComponent` values directly for operations like `set_velocity`, `set_angular_velocity`, `add_force`, and `add_impulse`. That bypassed the actual Jolt runtime path and effectively reintroduced the old split between exposed scripting APIs and the real physics backend.
- **Fix**: The exposed Python physics functions keep the same signatures, but now route through `PhysicsWorld` so they hit the active Jolt simulation path. For compatibility and immediate script-visible state, velocity fields in the ECS component are still updated where appropriate.
- **Fix (compatibility layer)**: `IPhysicsBackend` is now explicitly documented as a legacy compatibility interface rather than the active runtime abstraction.

### Bug Fix 14: `JoltBackend` no longer depends on active interface inheritance
- **Problem**: Even after `PhysicsWorld` moved to direct `JoltBackend` ownership, `JoltBackend` itself still inherited from `IPhysicsBackend`. That kept the active runtime class coupled to the old polymorphic backend design even though PhysX and runtime backend switching had already been removed.
- **Fix**: `JoltBackend` is now a direct class. It still exposes the same method shapes and reuses `IPhysicsBackend` data types as compatibility-facing aliases, but it no longer acts as the active implementation of the old backend inheritance chain.

### Bug Fix 15: Shared physics data types no longer originate in `IPhysicsBackend`
- **Problem**: Even after `JoltBackend` was detached from active interface inheritance, shared runtime data types like `BodyDesc`, `BodyState`, `RaycastResult`, and character descriptors/states still physically lived inside `IPhysicsBackend`. That meant the compatibility interface was still the type-origin for the active Jolt runtime path.
- **Fix**: Added `src/Physics/PhysicsTypes.h` as a neutral type home for shared physics handles and descriptors.
- **Fix (active path)**: `JoltBackend` now includes and uses `PhysicsTypes` directly, and `JoltBackend.cpp` implementations use those neutral types in their concrete definitions.
- **Fix (compatibility)**: `IPhysicsBackend` now re-exports those same neutral types via aliases, so externally exposed layers can keep the same function/data shapes without the active runtime path depending on the legacy interface as a type source.

### Bug Fix 16: Physics bodies now hot-reload after editor-side collider/physics changes
- **Problem**: Changing `CollisionComponent`, `PhysicsComponent`, transform scale, or heightfield-related data in the editor could leave the live PIE rigid body stale until a later implicit rebuild condition happened. This was especially confusing when changing collider type/shape in the details panel because the UI changed immediately, but the backend body was not always guaranteed to be rebuilt on the very next physics step.
- **Fix**: Editor-side component edits that affect rigid body creation now mark the affected entity in a dedicated `DiagnosticsManager` physics-dirty set via `invalidatePhysicsEntity(entity)`.
- **Fix (runtime rebuild trigger)**: `PhysicsWorld::step()` consumes that per-entity dirty list before syncing bodies and calls `requestBodyRebuild(entity)` only for the affected rigid bodies.
- **Result**: Collider/physics edits hot-reload in PIE on the next physics step, and only the changed bodies are rebuilt instead of forcing a global rebuild of all physics bodies.

### Build Fix: `x64-release` now produces real Release outputs
- **Problem**: With the Visual Studio multi-config generator, the `x64-release` configure preset still allowed multiple configurations. That made it possible for builds under the release preset to still land in `out/build/x64-release/DEBUG` or use non-Release configurations, which is unsuitable for redistribution.
- **Fix**: The `x64-release` configure preset now constrains `CMAKE_CONFIGURATION_TYPES` to `Release`, and a matching `buildPreset` explicitly builds `Release`. The `x64-debug` preset is likewise constrained to `Debug`.
- **Fix (internal/editor builds)**: The editor's internal game-script build path and `build.bat` default build path now use `Release` instead of `RelWithDebInfo` for non-debug builds.

### Bug Fix 17: Mesh collider data now flows into Jolt correctly
- **Problem**: `CollisionComponent::ColliderType::Mesh` still collapsed to a `Box` in `PhysicsWorld`, so mesh collision settings from ECS were not actually reaching Jolt as mesh geometry. That also meant changes to mesh-backed collider data could look correct in the editor while the live body still used unrelated fallback geometry.
- **Fix (shape data flow)**: Added `Mesh` support to the neutral physics body descriptor and passed mesh asset geometry into `JoltBackend` using explicit mesh vertex/index fields plus a mesh asset signature.
- **Fix (rebuild tracking)**: `hashBodyConfig()` now includes mesh asset identity/signature and mesh scale, so changes to mesh-backed collision input trigger body rebuilds reliably.
- **Fix (Jolt creation)**: `JoltBackend::createBody()` now creates a real Jolt `MeshShape` for static mesh colliders. For non-static mesh colliders it builds a `ConvexHullShape`, which is a safer Jolt-compatible approximation than the old unconditional box fallback.
- **Fix (dimension robustness)**: Primitive collider dimensions now use absolute transform scale when building shape descriptors so mirrored or negative ECS scale values do not feed invalid negative dimensions into Jolt.

### Bug Fix 18: Mesh-based rigid bodies now use safer fallback shapes and cleaner query results
- **Problem**: Even after avoiding triangle-mesh rigid bodies, box-like mesh colliders could still feel slightly soft or clip visually when represented only as convex hulls, and overlap queries against triangle-rich shapes could return the same entity multiple times because Jolt reports subshape hits.
- **Fix (mesh motion rules)**: Non-static mesh bodies are now always forced onto the convex path, including sensor/kinematic cases that could otherwise try to keep a true mesh shape even though Jolt requires mesh geometry to stay static.
- **Fix (fallback shape)**: If a mesh collider cannot become an exact box and later still fails mesh/convex creation, the backend now falls back to a bounds-derived box instead of a hardcoded `1x1x1` box.
- **Fix (query stability)**: `overlapSphere()` and `overlapBox()` now deduplicate entity IDs so mesh and heightfield subshapes do not produce repeated hits for the same body.

### Bug Fix 19: Physics readback now keeps ECS parenting/local transforms consistent
- **Problem**: Dynamic rigid bodies and character controllers wrote backend world-space transforms directly into `TransformComponent::position` / `rotation`, but did not recompute `localPosition` / `localRotation` for parented entities. In a hierarchy this could cause visual jitter, children lagging behind, or physics-updated world transforms being overwritten by stale local transforms on the next ECS world-transform update.
- **Fix**: Added a dedicated physics transform writeback helper in `PhysicsWorld.cpp` that converts backend world-space transforms back into ECS local-space data when an entity has a parent.
- **Fix (hierarchy propagation)**: After physics sync, descendants of physics-updated entities are marked dirty and `ECS::ECSManager::updateWorldTransforms()` is run once so child world transforms stay consistent with the newly written parent transforms.
