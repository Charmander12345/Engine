# Engine Finalisierung – Gesamtplan

> Vollständiger Implementierungsplan aller fehlenden Features für eine spielfähige Engine.
> Priorisiert nach Impact auf Spieleentwicklung (Vergleich: Unity/Unreal/Godot).
> Branch: `C++-Scripting` | Sprache: C++20 | Build: CMake 3.12+

---

## Inhaltsverzeichnis

1. [Phase 1 – Kern-Architektur](#phase-1--kern-architektur)
2. [Phase 2 – Gameplay-Systeme](#phase-2--gameplay-systeme)
3. [Phase 3 – Rendering & Visuals](#phase-3--rendering--visuals)
4. [Phase 4 – Welt & Navigation](#phase-4--welt--navigation)
5. [Phase 5 – Polish & Erweiterte Features](#phase-5--polish--erweiterte-features)
6. [Zusammenfassung & Abhängigkeiten](#zusammenfassung--abhängigkeiten)

---

## Phase 1 – Kern-Architektur

### 1.1 Entity Parent-Child Hierarchie (Transform Parenting)

**Status:** ✅ Implementiert | **Aufwand:** Mittel | **Priorität:** 🔴 Kritisch

**Implementiert:** `TransformComponent` mit Parent/Children/Local-Transforms/Dirty-Flag. ECSManager: `setParent()`, `removeParent()`, `getParent()`, `getChildren()`, `getRoot()`, `isAncestorOf()`, `updateWorldTransforms()`, `markTransformDirty()`. Level-Serialisierung mit Deferred Parenting + Backward-Kompatibilität. GameplayAPI (13 Funktionen) + INativeScript (13 Convenience-Methoden) + Python-Bindings (14 Funktionen) + engine.pyi. PhysicsWorld ruft `updateWorldTransforms()` vor Physics-Sync auf. Editor-Integration (Outliner Baum, Gizmo im Parent-Space) steht noch aus.

#### Implementierungsschritte

1. **TransformComponent erweitern (Components.h)**
   - `uint32_t parent{ UINT32_MAX }` (UINT32_MAX = kein Parent)
   - `std::vector<uint32_t> children`
   - `float localPosition[3]`, `float localRotation[3]`, `float localScale[3]`
   - Bestehende `position/rotation/scale` werden zu **World-Space-Werten** (berechnet)

2. **Hierarchie-API im ECS (ECS.h/ECS.cpp)**
   - `setParent(entity, parentEntity)` – setzt Parent, fügt sich in `children` ein
   - `removeParent(entity)` – löst Parent-Bindung, konvertiert lokale→globale Transform
   - `getParent(entity) → uint32_t`
   - `getChildren(entity) → const std::vector<uint32_t>&`
   - `getRoot(entity) → uint32_t` – traversiert bis zum Root
   - `isAncestorOf(ancestor, descendant) → bool`
   - Beim Entfernen einer Entity: alle Kinder automatisch unparenten oder mitlöschen (konfigurierbar)

3. **Hierarchische Transform-Berechnung**
   - `updateWorldTransforms()` – Top-Down-Traversal: Parent-World × Local = Child-World
   - Dirty-Flag pro Entity um unnötige Neuberechnungen zu vermeiden
   - Reihenfolge: Scale → Rotation → Translation (SRT)
   - Aufruf einmal pro Frame **vor** Physics und Rendering

4. **Level-Serialisierung (EngineLevel.cpp)**
   - Parent-ID in JSON speichern/laden
   - Beim Laden: Deferred-Parenting nach allen Entity-Erstellungen

5. **Scripting-API (C++ & Python)**
   - `GameplayAPI::setParent(entity, parent)` / `removeParent(entity)`
   - `GameplayAPI::getChildren(entity)` / `getParent(entity)`
   - `engine.entity.set_parent(entity, parent)` / `get_children(entity)`
   - `INativeScript.h`: Convenience-Methoden `setParent(parent)`, `getChildren()`
   - `engine.pyi` aktualisieren

6. **Editor-Integration**
   - Outliner: Drag&Drop zum Umparenten, Baum-Darstellung statt flache Liste
   - Gizmo: Transformation im Parent-Space
   - Viewport: Auswahl eines Kindes selektiert nur das Kind, nicht den ganzen Baum

7. **Physics-Anpassung (PhysicsWorld.cpp)**
   - `syncBodiesToBackend()`: World-Position verwenden (nicht lokal)
   - `syncBodiesFromBackend()`: World→Local zurückrechnen wenn Parent existiert

---

### 1.2 3D Spatial Audio

**Status:** ✅ Implementiert | **Aufwand:** Gering | **Priorität:** 🔴 Kritisch

**Implementiert:** `AudioSourceComponent` (ECS), `AudioManager` erweitert (`setSourcePosition`, `setSourceSpatial`, `updateListenerTransform`), Listener-Sync von CameraComponent, Editor 2D/3D-Toggle in AudioPreviewTab mit Spatial-Settings, Level-Serialisierung, OutlinerPanel-Inspector, GameplayAPI + INativeScript + Python-Bindings + engine.pyi.

---

## Phase 2 – Gameplay-Systeme

### 2.1 Physics Constraints / Joints

**Status:** ⚠️ Teilweise | **Aufwand:** Mittel | **Priorität:** 🔴 Kritisch

**Problem:** Keine Verbindungen zwischen Rigidbodies. Türen, Fahrzeuge, Ragdolls, Ketten, Brücken sind unmöglich.

**Vergleich:** Unity (HingeJoint, ConfigurableJoint), Unreal (Physics Constraints). Jolt und PhysX unterstützen Constraints nativ.

**Aktuell umgesetzt:** `ConstraintComponent` als ECS-Datenbasis inklusive Level-Serialisierung, Editor-Inspector/Add-Component-Integration sowie Komponentenkind-Unterstützung in C++/Python (`Component_Constraint`, Attach/Detach/Query). Laufzeitseitig werden `Fixed`-, `Hinge`-, `Distance`-, `BallSocket`-, `Slider`-, `Spring`- und `Cone`-Constraints jetzt in `PhysicsWorld` / `JoltBackend` als echte Jolt-Constraints erzeugt und verwaltet. Ein Entity kann dabei mehrere Constraint-Einträge gleichzeitig besitzen. Der Inspector bietet außerdem eine Szenen-Entity-Auswahl für `connectedEntity` statt reiner ID-Eingabe.

#### Implementierungsschritte

1. **ConstraintComponent (Components.h)**
   ```
   ConstraintType: Hinge, BallSocket, Fixed, Slider, Distance, Spring, Cone
   uint32_t connectedEntity
   float anchor[3], connectedAnchor[3]
   float axis[3]
   float limits[2] (min/max angle oder distance)
   float springStiffness, springDamping
   bool breakable, float breakForce, float breakTorque
   ```

2. **IPhysicsBackend erweitern**
   - `ConstraintHandle`, `ConstraintDesc`, `createConstraint()`, `removeConstraint()`
   - `setConstraintLimits()`, `setConstraintMotor()`, `isConstraintBroken()`

3. **JoltBackend-Implementierung**
   - `JPH::HingeConstraint`, `JPH::PointConstraint` (Ball-Socket), `JPH::FixedConstraint`
   - `JPH::SliderConstraint`, `JPH::DistanceConstraint`, `JPH::ConeConstraint`
   - `JPH::SpringSettings` für Spring-Constraints
   - Motor-Support via `JPH::MotorSettings`

4. **PhysXBackend-Implementierung**
   - `PxD6Joint` als universelles Constraint (Lock/Free/Limited pro Achse)
   - Alternativ: `PxRevoluteJoint` (Hinge), `PxSphericalJoint` (Ball-Socket), `PxFixedJoint`

5. **PhysicsWorld-Integration**
   - `syncConstraintsToBackend()` – Constraints erstellen/entfernen wie Bodies
   - Constraint-Breaks erkennen und `ConstraintComponent` updaten

6. **Scripting-API & Editor-UI**
   - `GameplayAPI::createConstraint(entityA, entityB, type, params)`
   - Editor: ConstraintComponent-Properties im Outliner
   - Gizmo-Visualisierung für Constraint-Achsen/Limits

---

### 2.2 Animation Blending / State Machine

**Status:** ⚠️ Teilweise (Blending, Crossfade, Layers implementiert; State Machine & Blend Trees ausstehend) | **Aufwand:** Hoch | **Priorität:** 🔴 Kritisch

**Implementiert:**
- `AnimationBlending.h`: BlendEntry, CrossfadeState, BoneMask (bitset<128>), AnimBlendMode (Override/Additive), AnimationLayer, kMaxAnimLayers=4
- `SkeletalAnimator` rewritten: multi-layer blending, crossfade with smooth-step easing, per-bone NodePose decomposition, evaluateEntryPoses(), computeBlendedBoneMatrices()
- `AnimationComponent` extended: crossfade fields, LayerState sub-struct, layers[4], layerCount
- `Renderer.h`: 8 new virtual functions (crossfade, layers, findClipByName)
- `OpenGLRenderer`: All 8 overrides implemented
- `GameplayAPI`: 13 new C++ scripting functions (isEntitySkinned, play/stop/crossfade/layers)
- `engine.animation` Python module: 13 functions (play, stop, crossfade, play_on_layer, etc.)
- `engine.pyi` updated with full animation class

**Noch ausstehend:** Animation State Machine, Blend Trees, AnimationController Asset, Editor Graph

#### Implementierungsschritte

1. **Animation Blending**
   - `BlendState`: Gewichtung mehrerer aktiver Clips (z.B. 70% Walk + 30% Run)
   - Pro-Bone-Interpolation: `lerp(boneA, boneB, weight)` für Position, `slerp` für Rotation
   - `crossfade(fromClip, toClip, duration)` – sanfter Übergang über N Frames

2. **Animation Layers**
   - Mehrere unabhängige Layer (z.B. Layer 0: Lower Body, Layer 1: Upper Body)
   - Pro Layer: eigener aktiver Clip + Blend-Weight
   - Bone-Mask: Welche Bones von welchem Layer gesteuert werden
   - Layer-Blending: Override oder Additive

3. **Animation State Machine**
   - `AnimationState`: Name, Clip-Index, Speed, Loop
   - `AnimationTransition`: FromState → ToState, Condition, Blend-Duration
   - `AnimationStateMachine`: CurrentState, Update(dt) → evaluiert Transitions
   - Conditions: Parameter-basiert (Float/Int/Bool/Trigger)
   - `setAnimParam(name, value)` / `getAnimParam(name)` API

4. **AnimationComponent erweitern**
   - `std::vector<AnimationLayer> layers`
   - `AnimationStateMachine* stateMachine`
   - `std::unordered_map<std::string, AnimParam> parameters`

5. **Blend Tree (Fortgeschritten)**
   - 1D Blend: Interpolation basierend auf einem Parameter (z.B. Speed)
   - 2D Blend: Interpolation basierend auf zwei Parametern (z.B. SpeedX/SpeedY)
   - Baum-Struktur: Blend-Nodes können verschachtelt werden

6. **Scripting-API**
   - `GameplayAPI::playAnimation(entity, clipName, layer, blend)`
   - `GameplayAPI::crossfade(entity, clipName, duration)`
   - `GameplayAPI::setAnimParam(entity, name, value)`
   - Python: `engine.animation.play(entity, "Walk", layer=0, blend=0.2)`

7. **Editor: Animation State Machine Editor**
   - Visueller Graph-Editor (Nodes = States, Edges = Transitions)
   - Preview im Viewport
   - Asset-Typ: `AnimationController`

---

### 2.3 Physics Queries (Overlap / Sweep)

**Status:** ✅ Vollständig implementiert | **Aufwand:** Mittel | **Priorität:** 🟡 Wichtig

**Implementiert:** overlapSphere, overlapBox, sweepSphere, sweepBox in PhysicsWorld, JoltBackend, GameplayAPI (C++ + Python `engine.physics`).

#### Implementierungsschritte

1. **IPhysicsBackend erweitern**
   - `overlapSphere(cx, cy, cz, radius) → std::vector<uint32_t>` (Entity-IDs)
   - `overlapBox(cx, cy, cz, hx, hy, hz) → std::vector<uint32_t>`
   - `sweepSphere(ox, oy, oz, dx, dy, dz, radius, maxDist) → SweepResult`
   - `sweepBox(ox, oy, oz, dx, dy, dz, hx, hy, hz, maxDist) → SweepResult`

2. **JoltBackend**: `BroadPhaseQuery::CollideShape` / `CastShape`
3. **PhysXBackend**: `PxScene::overlap` / `PxScene::sweep`

4. **PhysicsWorld-API**
   - `overlapSphere()`, `overlapBox()`, `sweepSphere()`, `sweepBox()`

5. **Scripting-API**
   - `engine.physics.overlap_sphere(x, y, z, radius)` → Liste von Entity-IDs
   - `GameplayAPI::overlapSphere(x, y, z, radius, outEntities)`

---

### 2.4 Force an Position (AddForceAtPosition)

**Status:** ✅ Vollständig implementiert | **Aufwand:** Gering | **Priorität:** 🟡 Wichtig

**Implementiert:** addForceAtPosition, addImpulseAtPosition in PhysicsWorld, JoltBackend, GameplayAPI (C++ + Python `engine.physics`).

#### Implementierungsschritte

1. **IPhysicsBackend erweitern**
   - `addForceAtPosition(handle, fx, fy, fz, px, py, pz)`
   - `addImpulseAtPosition(handle, ix, iy, iz, px, py, pz)`

2. **JoltBackend**: `BodyInterface::AddForce(bodyId, force, point)`
3. **PhysXBackend**: `PxRigidBodyExt::addForceAtPos(body, force, point)`
4. **Scripting-API**: `engine.physics.add_force_at_position(entity, fx,fy,fz, px,py,pz)`

---

### 2.5 Mesh Collider (Convex / Concave)

**Status:** ✅ Vollständig implementiert | **Aufwand:** Mittel | **Priorität:** 🟡 Wichtig

**Implementiert:** Shape::Mesh in PhysicsTypes.h, ConvexHullShape/MeshShape in JoltBackend, Mesh-Daten aus AssetManager geladen.

#### Implementierungsschritte

1. **CollisionComponent erweitern**
   - `ColliderType::ConvexMesh = 6`, `ColliderType::TriangleMesh = 7`
   - `std::string meshAssetPath` – Referenz auf das Mesh-Asset für Collider-Generierung

2. **IPhysicsBackend::BodyDesc erweitern**
   - `Shape::ConvexMesh`, `Shape::TriangleMesh`
   - `const float* vertices`, `uint32_t vertexCount`
   - `const uint32_t* indices`, `uint32_t indexCount`

3. **JoltBackend**: `JPH::ConvexHullShapeSettings`, `JPH::MeshShapeSettings`
4. **PhysXBackend**: `PxConvexMesh` (via Cooking), `PxTriangleMesh`
5. **Mesh-Daten aus AssetManager lesen** – Vertices/Indices beim Body-Erstellen laden

---

## Phase 3 – Rendering & Visuals

### 3.1 Reflection Probes / Environment Mapping

**Status:** ❌ Fehlend | **Aufwand:** Hoch | **Priorität:** 🟡 Wichtig

#### Implementierungsschritte

1. **ReflectionProbeComponent (Components.h)**
   - Position, Radius/Box-Extents, Resolution (128/256/512)
   - `bool baked` vs Runtime-Update

2. **Cubemap-Capture**
   - 6-Faces rendern (±X, ±Y, ±Z) von der Probe-Position
   - In GL_TEXTURE_CUBE_MAP speichern
   - Mip-Chain für verschiedene Roughness-Levels (Prefiltered Environment Map)

3. **PBR-Shader-Integration**
   - `uniform samplerCube u_envMap`
   - Specular IBL: Prefiltered Environment Map × BRDF LUT
   - Diffuse IBL: Irradiance Map (niedriger Mip-Level oder separate Convolution)
   - Probe-Blending: Gewichtung basierend auf Distanz wenn mehrere Probes überlappen

4. **Screen-Space Reflections (SSR) – Optional**
   - Ray-Marching im Screen-Space gegen Depth-Buffer
   - Fallback auf Reflection Probe wenn SSR fehlschlägt

---

### 3.2 Decal System

**Status:** ❌ Fehlend | **Aufwand:** Mittel | **Priorität:** 🟡 Wichtig

#### Implementierungsschritte

1. **DecalComponent (Components.h)**
   - Material/Texture-Referenz, Size (projizierte Box-Größe)
   - Fade-Distance, Angle-Fade-Range

2. **Deferred Decal Rendering**
   - Decal als Box-Volume rendern
   - Im Fragment-Shader: Depth-Buffer lesen → Weltposition rekonstruieren → UV projizieren
   - GBuffer-Modifikation: Albedo, Normal, Roughness

3. **Scripting-API**
   - `GameplayAPI::spawnDecal(position, rotation, material, size, lifetime)`
   - Auto-Cleanup nach Lifetime

---

### 3.3 Frustum Culling Verbesserung

**Status:** ⚠️ Vorhanden, verbesserbar | **Aufwand:** Mittel | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. **CPU-seitiges Frustum Culling optimieren**
   - AABB-basiertes Frustum-Testing statt Sphere-basiert (genauere Ergebnisse)
   - SIMD-Optimierung für Plane-AABB-Tests (SSE/AVX)

2. **Hierarchisches Culling (BVH oder Octree)**
   - Spatial-Partitionierung für große Szenen
   - Nur Knoten traversieren, die den Frustum schneiden

3. **Occlusion Culling Verbesserung**
   - Hi-Z Occlusion Culling (Hierarchical-Z Buffer)
   - GPU-Driven Rendering Pipeline (Compute-Shader basiert)

---

## Phase 4 – Welt & Navigation

### 4.1 Runtime UI System (HUD / Menüs)

**Status:** ⚠️ Teilweise | **Aufwand:** Hoch | **Priorität:** 🟡 Wichtig

**Problem:** Widget-System existiert, ist aber als Editor-UI konzipiert. Kein strukturiertes In-Game-UI-Framework.

#### Implementierungsschritte

1. **Runtime UI Layer**
   - Separater Render-Pass für In-Game-UI (über der 3D-Szene)
   - Resolution-unabhängiges Layout (Anchor-System: TopLeft, Center, BottomRight, Stretch)
   - Canvas-Konzept: Entities mit UI-Elementen werden auf einen Canvas projiziert

2. **UI-Widgets für Gameplay**
   - ProgressBar (Health, Loading, Cooldown)
   - Label (Score, Timer, Objectives)
   - Image (Icons, Minimap-Elemente)
   - Button (Menü-Interaktion)
   - Panel/Container mit Layout (Horizontal/Vertical/Grid)
   - Scrollable List (Inventar, Chat)

3. **Data Binding**
   - UI-Elemente können an Script-Variablen gebunden werden
   - `bindValue("healthBar.progress", "player.health")`
   - Automatisches UI-Update wenn sich der Wert ändert

4. **Screen-Space vs World-Space UI**
   - Screen-Space: HUD-Overlay (feste Bildschirm-Position)
   - World-Space: Health Bars über NPCs, Interaktions-Prompts (Billboard)

5. **Scripting-API**
   - `engine.ui.create_panel(anchor, position, size)`
   - `engine.ui.create_progress_bar(parent, min, max, value)`
   - `engine.ui.set_text(element, "Score: 100")`

---

### 4.2 AI / Navigation System

**Status:** ❌ Fehlend | **Aufwand:** Hoch | **Priorität:** 🟡 Wichtig

**Empfehlung:** Recast/Detour als Third-Party-Bibliothek integrieren.

#### Implementierungsschritte

1. **Recast/Detour Integration (external/)**
   - Recast: NavMesh-Generierung aus Level-Geometrie
   - Detour: Runtime-Pathfinding (A* auf NavMesh)
   - CMake: `add_subdirectory(external/recastnavigation)`

2. **NavMeshComponent (Components.h)**
   - Auf eine Mesh/Landscape-Entity gesetzt
   - Agent-Radius, Agent-Height, Max-Slope, Step-Height
   - Bake-Trigger (Editor-Button)

3. **NavigationManager (Singleton)**
   - `bakeNavMesh()` – generiert NavMesh aus allen statischen Meshes der Szene
   - `findPath(startX, startY, startZ, endX, endY, endZ) → Path`
   - `isPointOnNavMesh(x, y, z) → bool`

4. **NavMeshAgentComponent**
   - `float speed`, `float stoppingDistance`, `float angularSpeed`
   - `setDestination(x, y, z)` – Agent bewegt sich automatisch entlang des Pfades
   - `isAtDestination() → bool`
   - Integration mit Character Controller (bevorzugt) oder Rigidbody

5. **Editor: NavMesh-Visualisierung**
   - NavMesh als Debug-Overlay rendern (halbtransparente Flächen)
   - Bake-Button im Editor-Toolbar
   - NavMesh als Asset speichern/laden

6. **AI Steering Behaviors**
   - Seek, Flee, Arrive, Wander, Obstacle Avoidance
   - Kombinierbar über Gewichtung

7. **Scripting-API**
   - `engine.ai.find_path(start, end)` → Pfad als Liste von Waypoints
   - `engine.ai.set_destination(entity, x, y, z)`

---

### 4.3 Multi-Level-Streaming / Additive Scenes

**Status:** ❌ Fehlend | **Aufwand:** Hoch | **Priorität:** 🟡 Wichtig

#### Implementierungsschritte

1. **Additives Level-Loading**
   - `loadLevelAdditive(path)` – Lädt ein Level zusätzlich zum aktuellen
   - Entity-IDs müssen eindeutig bleiben (ID-Remapping)
   - Level-Referenz pro Entity (zu welchem Sub-Level gehört sie)

2. **Level-Streaming basierend auf Distanz/Volumes**
   - `StreamingVolumeComponent`: Box-Trigger der ein Sub-Level lädt/entlädt
   - Asynchrones Laden auf Background-Thread
   - `LevelStreamingManager`: Verwaltet geladene/entladene Sub-Levels

3. **Persistent Level Konzept**
   - Ein "Persistent Level" ist immer geladen
   - Sub-Levels werden additiv geladen/entladen
   - Entities in verschiedenen Sub-Levels können interagieren

---

## Phase 5 – Polish & Erweiterte Features

### 5.1 Root Motion

**Status:** ❌ Fehlend | **Aufwand:** Mittel | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. Root-Bone-Delta pro Frame extrahieren (Translation + Rotation)
2. Delta auf Entity-Transform anwenden statt auf den Bone
3. `AnimationComponent.rootMotionEnabled` Toggle
4. Character Controller Integration: Root Motion als Desired Velocity

---

### 5.2 Physics Material System

**Status:** ❌ Fehlend | **Aufwand:** Gering | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. **PhysicsMaterial als Asset-Typ**
   - `AssetType::PhysicsMaterial`
   - Properties: Friction (static/dynamic), Restitution, Density
   - Vordefinierte Presets: Metal, Wood, Ice, Rubber, Concrete

2. **CollisionComponent.physicsMaterialAsset** – Referenz statt inline Friction/Restitution
3. **Editor: PhysicsMaterial-Editor-Tab**

---

### 5.3 Foliage / Vegetation Instancing

**Status:** ❌ Fehlend | **Aufwand:** Hoch | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. **FoliageComponent**: Mesh-Referenz, Density, Min/Max Scale, Random Rotation
2. **GPU Instancing**: Tausende Instanzen in einem Draw-Call
3. **Placement auf Landscape**: Brush-Tool im Editor (Paint Foliage)
4. **Wind-Animation**: Vertex-Shader-basierte Welle-Animation
5. **LOD**: Automatische LOD-Stufen basierend auf Distanz

---

### 5.4 Lightmapping / Baked Global Illumination

**Status:** ❌ Fehlend | **Aufwand:** Sehr Hoch | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. **UV2-Atlas-Generierung** (Lightmap UVs, non-overlapping)
2. **Lightmap-Baking**: Path-Tracing oder Radiosity auf CPU
3. **Lightmap-Speicherung**: Pro Mesh-Instanz eine Textur-Region im Atlas
4. **Shader-Integration**: `texture(u_lightmap, v_lightmapUV)` multipliziert mit Albedo
5. **Editor: Bake-Button + Preview**

---

### 5.5 Visual Scripting / Blueprint-System

**Status:** ❌ Fehlend | **Aufwand:** Sehr Hoch | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. **Node-Graph-Framework**: Nodes (Funktionsblöcke), Pins (Input/Output), Links
2. **Execution Flow**: Execution-Pins für kontrollierte Reihenfolge
3. **Standard-Nodes**: Event (Tick, BeginOverlap), Flow (Branch, ForLoop), Math, Transform, Physics
4. **Graph-Evaluation**: Interpreter oder Codegen nach C++/Python
5. **Editor: Graph-Editor-Tab** mit Drag&Drop-Node-Erstellung

---

### 5.6 Networking / Multiplayer

**Status:** ❌ Fehlend | **Aufwand:** Sehr Hoch | **Priorität:** 🟢 Nice-to-have

#### Implementierungsschritte

1. **Transport-Layer**: UDP-basiert (ENet oder GameNetworkingSockets)
2. **Client-Server-Architektur**: Authoritative Server, Client-Prediction
3. **Replication**: Automatische Synchronisation von Entity-Properties
4. **RPC (Remote Procedure Calls)**: Server→Client und Client→Server
5. **Lobby/Matchmaking**: Basis-Infrastruktur

---

## Zusammenfassung & Abhängigkeiten

### Abhängigkeitsgraph

```
Transform Parenting ─────────────────────────────────────┐
    │                                                     │
    ├→ Physics Joints (benötigt Parent für Compound Bodies)│
    ├→ Animation Blending (Bone-Hierarchie nutzt Parenting)│
    ├→ AI/Navigation (Agent-Entities als Children)         │
    └→ Runtime UI (Container-Hierarchie)                   │
                                                           │
3D Spatial Audio ─── unabhängig ──────────────────────────│
                                                           │
Physics Queries ─── unabhängig ───────────────────────────│
    └→ AI/Navigation (NavMesh benötigt Overlap für Agent) │
                                                           │
Character Controller ✅ ──────────────────────────────────│
    └→ AI/Navigation (NavMeshAgent nutzt Character Controller)
                                                           │
Reflection Probes ─── unabhängig ─────────────────────────│
Decals ─── unabhängig ────────────────────────────────────│
Level Streaming ─── benötigt stabile Serialisierung ──────┘
```

### Implementierungsreihenfolge (empfohlen)

| Reihenfolge | Feature | Abhängigkeiten | Status |
|-------------|---------|----------------|--------|
| 1 | Transform Parenting | Keine | ✅ Fertig |
| 2 | 3D Spatial Audio | Keine | ✅ Fertig |
| 3 | Physics Queries (Overlap/Sweep) | Keine | ✅ Fertig |
| 4 | Force at Position | Keine | ✅ Fertig |
| 5 | Physics Joints/Constraints | Transform Parenting | ⚠️ Teilweise |
| 6 | Mesh Collider | Keine | ✅ Fertig |
| 7 | Animation Blending | Transform Parenting | ⚠️ Blending/Layers fertig, State Machine ausstehend |
| 8 | Reflection Probes / IBL | Keine | ❌ Ausstehend |
| 9 | Decal System | Keine | ❌ Ausstehend |
| 10 | Runtime UI Framework | Transform Parenting | ❌ Ausstehend |
| 11 | Physics Material System | Keine | ❌ Ausstehend |
| 12 | AI/Navigation (Recast/Detour) | Physics Queries, Character Controller | ❌ Ausstehend |
| 13 | Root Motion | Animation Blending | ❌ Ausstehend |
| 14 | Multi-Level-Streaming | Keine | ❌ Ausstehend |
| 15 | Frustum Culling Verbesserung | Keine | ❌ Ausstehend |
| 16 | Foliage Instancing | Keine | ❌ Ausstehend |
| 17 | Lightmapping / Baked GI | Keine | ❌ Ausstehend |
| 18 | Visual Scripting | Keine | ❌ Ausstehend |
| 19 | Networking | Keine | ❌ Ausstehend |

**Gesamt geschätzt:** ~50-75 Wochen (für alle Features, inklusive Nice-to-have)
**Kern-Features (Prio 1+2):** ~20-30 Wochen

---

> **Hinweis:** Die Reihenfolge priorisiert Features die andere Features enablen (Transform Parenting) und Quick-Wins (3D Spatial Audio, Physics Queries, Force at Position).
> Features mit `Sehr Hoch`-Aufwand (Visual Scripting, Networking, Lightmapping) sollten erst nach Stabilisierung aller Kern-Features angegangen werden.
