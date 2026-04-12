# Actor-System Roadmap – Basierend auf Unreal Engine Breakdown

> Quelle: `docs/Unreal_actor_breakdown.md`
> Erstellt aus der Analyse des bestehenden Engine-Zustands gegenüber dem UE-Referenzdesign.

---

## Status-Legende

| Symbol | Bedeutung |
|--------|-----------|
| ✅ | Bereits vorhanden |
| 🔨 | Nächste Priorität (Quick Win / hoher Impact) |
| 📋 | Geplant (mittlerer Aufwand) |
| 📐 | Architektur-Design nötig (größerer Aufwand) |
| ⏳ | Langfristig / Optional |

---

## Bestandsaufnahme – Was existiert bereits

| UE-Konzept | Engine-Äquivalent | Status |
|---|---|---|
| `AActor` | `Actor` (wraps `ECS::Entity`, Lifecycle, Transform, Hierarchy) | ✅ |
| `UActorComponent` | `ActorComponent` (onRegister/onUnregister/beginPlay/tick/endPlay) | ✅ |
| Built-in Components | StaticMesh, Light, Physics, Camera, Audio, Particle, CharacterController | ✅ |
| Built-in Actors | StaticMeshActor, PointLight, DirectionalLight, SpotLight, Camera, Physics, Character, Audio, Particle | ✅ |
| `UWorld` | `World` (spawnActor, destroyActor, tick, queries, deferred destroy) | ✅ |
| Class Registry | `ActorRegistry` + `REGISTER_ACTOR()` Makro | ✅ |
| Actor Asset Templates | `ActorAssetData` + `ActorEditorTab` + JSON Serialisierung | ✅ |
| Overlap Events | `onBeginOverlap`/`onEndOverlap` via PhysicsWorld→World dispatch | ✅ |
| Parent/Child Actors | `attachToActor`/`detachFromParent` mit ECS-Parenting | ✅ |
| ECS | Sparse-Set basiert, 16 Component-Typen, Schema-Queries | ✅ |
| Physics | Jolt Backend, Constraints, CCD, Queries, Forces/Impulses | ✅ |
| Scripting | Python + Native C++ Scripts, Hot-Reload | ✅ |

---

## Phase 1: Quick Wins (Hoher Impact, geringer Aufwand) — ✅ Abgeschlossen

### 1.1 ✅ Tick-Gruppen (TickGroup Enum + phasenweises Ticking)

**Referenz**: Abschnitt 2.4 – UE hat 4 Tick-Gruppen relativ zur Physik.

**Aktueller Zustand**: Alle Actors ticken in Array-Reihenfolge *nach* der Physik. Keine Möglichkeit, vor oder während der Physik zu ticken.

**Hauptloop-Reihenfolge aktuell** (`main.cpp`):
```
NativeScriptManager::updateScripts(dt)   ← C++ Scripts
PhysicsWorld::step(dt)                    ← Jolt
Overlap-Dispatch                          ← Events
actorWorld.tick(dt)                       ← ALLE Actors uniform
Scripting::UpdateScripts(dt)              ← Python
```

**Ziel**: Actors können ihre Tick-Gruppe wählen:

```cpp
enum class ETickGroup { PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork };

struct TickSettings {
    bool        bCanEverTick{ true };
    ETickGroup  tickGroup{ ETickGroup::PostPhysics };
    float       tickInterval{ 0.0f };   // 0 = every frame
};
```

**Umsetzung**:
- `ETickGroup` Enum in `src/Core/Actor/Actor.h`
- `TickSettings` als Member von `Actor` (Stack-allokiert)
- `World::tick()` splitten: 4 Durchläufe (PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork)
- Main-Loop anpassen: `actorWorld.tickPrePhysics()` → `PhysicsWorld::step()` → `actorWorld.tickPostPhysics()` etc.
- Tick-Interval via akkumulierten Timer pro Actor

**Betroffene Dateien**:
- `src/Core/Actor/Actor.h` (modify) – TickSettings Member, Getter/Setter
- `src/Core/Actor/World.h/.cpp` (modify) – Phasen-basiertes Ticking
- `src/main.cpp` (modify) – Loop-Reihenfolge anpassen

**Aufwand**: ~2-3h | **Impact**: Hoch – Gameplay kann vor/nach Physik agieren

---

### 1.2 ✅ Actor Lifecycle verfeinern

**Referenz**: Abschnitt 2.1 – UE hat `PreInitializeComponents()` → `InitializeComponent()` → `PostInitializeComponents()` → `BeginPlay()`.

**Aktueller Zustand**: `internalInitialize()` → `internalBeginPlay()` (ruft `component->beginPlay()` dann `actor->beginPlay()`). Kein Hook zwischen Komponenteninitialisierung und BeginPlay.

**Ziel**: Reichere Lifecycle-Kette:
```cpp
// Actor.h – neue virtuelle Methoden:
virtual void preInitializeComponents() {}   // Vor Component-Init
virtual void postInitializeComponents() {}  // Nach Component-Init, vor BeginPlay
```

**Umsetzung**:
```
internalInitialize(entity, world)
  → TransformComponent sicherstellen
internalBeginPlay()
  → preInitializeComponents()
  → component->beginPlay() für alle Components
  → postInitializeComponents()
  → beginPlay()
```

**Betroffene Dateien**:
- `src/Core/Actor/Actor.h` (modify) – 2 neue virtuelle Methoden
- `src/Core/Actor/Actor.cpp` (modify) – `internalBeginPlay()` erweitern

**Aufwand**: ~30min | **Impact**: Mittel – saubere Initialisierungsreihenfolge

---

### 1.3 ✅ Deferred Spawning

**Referenz**: Abschnitt 2.5 – `SpawnActorDeferred<T>()` + `FinishSpawning()`.

**Aktueller Zustand**: `spawnActor<T>()` ruft sofort `internalBeginPlay()`. Keine Möglichkeit, Properties vor BeginPlay zu setzen.

**Ziel**: Actors können erzeugt, konfiguriert und dann finalisiert werden:
```cpp
// Nutzung:
auto* actor = world->spawnActorDeferred<MyActor>(0, 0, 0);
actor->setName("Boss");
actor->setTag("enemy");
actor->getComponent<StaticMeshComponent>()->setMesh("boss.obj");
actor->finishSpawning();  // → BeginPlay
```

**Umsetzung**:
- `m_deferredSpawn` Bool am Actor (Stack-allokiert, kein Heap)
- `World::spawnActorDeferred<T>()` – wie `spawnActor` aber ohne `internalBeginPlay()`
- `Actor::finishSpawning()` – ruft `internalBeginPlay()` und setzt Flag
- Sicherheitsprüfung: `internalTick()` überspringt nicht-finalisierte Actors

**Betroffene Dateien**:
- `src/Core/Actor/Actor.h/.cpp` (modify) – Flag + finishSpawning()
- `src/Core/Actor/World.h/.cpp` (modify) – spawnActorDeferred<T>() Template

**Aufwand**: ~1h | **Impact**: Hoch – ermöglicht konfigurierbare Spawns

---

### 1.4 ✅ Component Lifecycle: onRegister für Laufzeit-Komponenten

**Referenz**: Abschnitt 2.2 – `RegisterComponent()` aktiviert Render/Physik-State und ruft `InitializeComponent()` + `BeginPlay()` wenn Actor bereits initialisiert.

**Aktueller Zustand**: `addComponent<T>()` ruft `onRegister()`, aber NICHT `beginPlay()` wenn der Actor bereits läuft. Zur Laufzeit hinzugefügte Components verpassen ihren BeginPlay.

**Umsetzung**:
- In `Actor::addComponent<T>()`: nach `onRegister()` prüfen ob `m_beginPlayCalled == true`, und wenn ja, auch `comp->beginPlay()` aufrufen.

**Betroffene Dateien**:
- `src/Core/Actor/Actor.h` (modify) – Template-Implementierung anpassen

**Aufwand**: ~15min | **Impact**: Mittel – korrektes Verhalten für dynamische Components

---

## Phase 2: Kernarchitektur-Erweiterungen (Mittlerer Aufwand)

### 2.1 ✅ Name-System (Interned Strings)

**Referenz**: Abschnitt 1.5 – `FName` mit globalem Pool, O(1) Vergleiche.

**Aktueller Zustand**: Alles basiert auf `std::string`. Actor-Name, Tag, Asset-Pfade, Entity-Suche – alles String-Vergleiche.

**Ziel**: `NameID` für schnelle Identifikatoren:
```cpp
struct NameID {
    uint32_t index{ 0 };  // 0 = None/Invalid

    bool operator==(NameID other) const { return index == other.index; }
    bool operator!=(NameID other) const { return index != other.index; }
};

class NamePool {
public:
    static NamePool& Instance();
    NameID  find(const std::string& str);      // O(n) – Hash + Lookup
    NameID  findOrAdd(const std::string& str);  // O(n) – Hash + evtl. Insert
    const std::string& resolve(NameID id) const; // O(1)
private:
    std::unordered_map<std::string, uint32_t> m_lookup;
    std::vector<std::string>                  m_strings;  // Index → String
};
```

**Schrittweise Migration**:
1. `NamePool` + `NameID` in `src/Core/NameID.h` erstellen
2. `Actor::m_name` und `Actor::m_tag` auf `NameID` umstellen
3. `findActorByName()` / `findActorsByTag()` nutzen Index-Vergleiche
4. Langfristig: Asset-Pfade, Component-IDs etc. migrieren

**Betroffene Dateien**:
- `src/Core/NameID.h` (new) – NamePool + NameID
- `src/Core/Actor/Actor.h/.cpp` (modify) – NameID statt std::string
- `src/Core/Actor/World.cpp` (modify) – Queries anpassen

**Aufwand**: ~4-6h | **Impact**: Hoch bei vielen Actors – O(1) Lookups statt O(n) String-Vergleiche

---

### 2.2 ✅ Game Framework Basisklassen

**Referenz**: Abschnitt 2.6 – GameMode, GameState, PlayerController, Pawn.

**Aktueller Zustand**: Kein Game-Framework. Spielregeln, Spielerzustand und Input-Kopplung sind nicht abstrahiert.

**Ziel**: Rollen-Trennung für Gameplay-Architektur:

```cpp
// GameMode – Spielregeln, existiert einmal pro World
class GameMode : public Actor {
public:
    virtual void onPlayerJoined(Actor* player) {}
    virtual void onPlayerLeft(Actor* player) {}
    virtual std::string getDefaultPawnClass() const { return ""; }
    virtual std::string getPlayerControllerClass() const { return ""; }
};

// GameState – spielweiter Zustand, lesbar für alle
class GameState : public Actor {
public:
    // Spieler-Score, Match-Status etc. als Properties
};

// PlayerController – Input-Empfänger, besitzt einen Pawn
class PlayerController : public Actor {
public:
    void possess(Actor* pawn);
    void unpossess();
    Actor* getPawn() const { return m_possessedPawn; }
private:
    Actor* m_possessedPawn{ nullptr };
};

// Pawn – besitzbare physische Repräsentation
class Pawn : public Actor {
public:
    PlayerController* getController() const { return m_controller; }
private:
    friend class PlayerController;
    PlayerController* m_controller{ nullptr };
};
```

**Schrittweise Einführung**:
1. Basisklassen als Header in `src/Core/Actor/GameFramework/`
2. `World` bekommt `setGameMode<T>()` und `getGameMode()`
3. Optional: `World::beginPlay()` instanziiert GameMode automatisch

**Betroffene Dateien**:
- `src/Core/Actor/GameFramework/GameMode.h` (new)
- `src/Core/Actor/GameFramework/GameState.h` (new)
- `src/Core/Actor/GameFramework/PlayerController.h` (new)
- `src/Core/Actor/GameFramework/Pawn.h` (new)
- `src/Core/Actor/World.h/.cpp` (modify) – GameMode Integration
- `src/Core/CMakeLists.txt` (modify) – neue Dateien hinzufügen

**Aufwand**: ~6-8h | **Impact**: Hoch – fundamentale Gameplay-Architektur

---

### 2.3 ✅ Tick-Dependencies (DAG)

**Referenz**: Abschnitt 2.4 – `AddTickPrerequisiteActor()` für Ordnungs-Abhängigkeiten.

**Aktueller Zustand**: Keine Tick-Ordnung zwischen Actors. Alle ticken in Array-Reihenfolge.

**Ziel** (aufbauend auf Phase 1.1 Tick-Gruppen):
```cpp
// Actor.h:
void addTickPrerequisite(Actor* other);
void removeTickPrerequisite(Actor* other);
```

**Umsetzung**:
- `std::vector<Actor*> m_tickPrerequisites` am Actor
- `World::tick()` topologische Sortierung pro Tick-Gruppe (Kahn's Algorithm)
- Zyklen-Erkennung mit Warnung via Logger

**Aufwand**: ~4h (nach Phase 1.1) | **Impact**: Mittel – wichtig für komplexe Gameplay-Ketten

---

## Phase 3: Editor-Integration (Größerer Aufwand)

### 3.1 ✅ Reflektions-System (Makro-basiert)

**Referenz**: Abschnitt 1.1, 4.2 – UHT-ähnliche Reflektion für automatisches Details Panel.

**Status**: ✅ Implementiert.

**Implementierung**:
- `src/Core/Reflection.h` – Kern-Typen (`TypeID`, `PropertyFlags`, `EnumEntry`, `PropertyInfo`, `ClassInfo`), `TypeRegistry` Singleton, und `REFLECT_*` Makros.
- `src/Core/Reflection.cpp` – TypeRegistry Singleton-Implementierung mit Lookup per `type_index`, `const char*` Klassenname.
- `src/Core/ECS/ComponentReflection.h` – Alle 16+ ECS-Komponenten (inkl. verschachtelte Structs wie `MaterialOverrides`, `ConstraintEntry`, `LodLevel`) mit vollständiger Property-Metadaten, Enum-Tabellen, Kategorien und Flags registriert.
- `src/Renderer/EditorTabs/ReflectionWidgetFactory.h/.cpp` – Generiert Editor-Widgets aus ClassInfo: Float→FloatRow/Slider, Int→IntRow, Bool→CheckBox, String→StringRow, Vec3→Vec3Row, Color→ColorPicker, Enum→DropDown. Skips PF_Hidden.

**Design-Entscheidungen**:
- Nicht-intrusiver Ansatz gewählt (externe Registration via Makros) statt intrusive `REGISTER_CLASS()` in POD-Structs — minimaler Impact auf bestehende Components.h.
- `REFLECT_BEGIN_NESTED` / `REFLECT_END_NESTED` Variante für verschachtelte Typen (z.B. `LodComponent::LodLevel`) da `::` in Token-Paste ungültige Identifier erzeugt.
- `PropertyInfo::ptrIn<T>()` Template für typsicheren Runtime-Zugriff via byte-offset.

**Nächster Schritt**: Migration der bestehenden OutlinerPanel-Hardcoded-UI auf ReflectionWidgetFactory (schrittweise per Component).

---

### 3.2 ✅ Undo/Redo Transaction-System

**Referenz**: Abschnitt 4.4 – `FScopedTransaction` + Snapshot via Serialisierung.

**Implementiert** in `src/Core/TransactionManager.h/.cpp`:

- `TransactionManager` Singleton baut auf dem bestehenden `UndoRedoManager` auf. `beginTransaction(desc)` / `endTransaction()` klammern Property-Änderungen; bei `endTransaction()` wird der "After"-Zustand erfasst und ein einzelner `UndoRedoManager::Command` gepusht.
- Zwei Recording-Modi:
  - `recordSnapshot(void*, size_t)` – Raw-Memcpy für stabile Adressen.
  - `recordEntry(capture, restore)` – Lambda-basiert für relokierbare Daten (z.B. ECS-Komponenten per Entity-ID).
- `addPostRestoreCallback()` – Side-Effect-Callbacks nach jedem Undo/Redo (Physics-Invalidierung, UI-Refresh).
- `ScopedTransaction` RAII-Guard: Konstruktor → `beginTransaction`, Destruktor → `endTransaction`. Convenience: `snapshot()`, `entry()`, `onRestore()`.
- Integration: Committed Transactions als `UndoRedoManager::Command` – bestehende Ctrl+Z/Y, 100-Tiefe-Limit, `onChanged`-Callback funktionieren transparent.
- `std::shared_ptr` für gemeinsam genutzte Before/After-Puffer in Execute/Undo-Lambdas.

**Nutzung im Editor**:
```cpp
{
    ScopedTransaction txn("Actor verschieben");
    txn.snapshot(actor->getTransformPtr(), sizeof(TransformComponent));
    actor->setPosition(newX, newY, newZ);
}
// Ctrl+Z → Position zurücksetzen
```

**Nächster Schritt**: Schrittweise Migration bestehender `setCompFieldWithUndo<>` Stellen auf `ScopedTransaction` / `recordEntry`.

**Betroffene Dateien**:
- `src/Core/TransactionManager.h` (neu) – TransactionManager Singleton + ScopedTransaction RAII
- `src/Core/TransactionManager.cpp` (neu) – Implementierung
- `src/Core/CMakeLists.txt` (mod) – Neue Dateien hinzugefügt

---

### 3.3 ✅ Bidirektionales Archive-System

**Referenz**: Abschnitt 1.4 – `FArchive` mit `operator<<` für Load und Save.

**Implementiert** in `src/Core/Archive.h/.cpp` + `src/Core/ECS/ComponentSerialization.h`:

- `Archive` abstrakte Basisklasse: `isLoading()` / `isSaving()`, `operator<<` für alle fundamentalen C++-Typen, `std::string` (Length-Prefixed), `serializeFloatArray<N>()`, `serializeVector<T>()`, `serializeStringVector()`, `serializeVersion()` (Format-Versionierung), `hasError()`.
- `FileArchiveWriter` – Binäre Dateiausgabe (`std::ofstream`).
- `FileArchiveReader` – Binäre Dateieingabe (`std::ifstream`).
- `MemoryArchive` – In-Memory `std::vector<uint8_t>` Buffer. `takeBuffer()`, `resetRead()`, `resetWrite()`.
- Freie `serialize(Archive&, Component&)` Funktionen für alle 14+ ECS-Komponenten mit Version-Tags.
- `ChildActorEntry::serialize(Archive&)` und `ActorAssetData::serialize(Archive&)` – Bidirektional, rekursiv.
- JSON bleibt Editor-Format; Binär verfügbar für Runtime, Undo-Snapshots und Netzwerk.

**Nächste Schritte (inkrementelle Migration)**:
1. ~~`Archive` Basisklasse + File/Memory Implementierungen~~ ✅
2. ~~`ActorAssetData` optional binär serialisierbar~~ ✅
3. Level-Serialisierung auf Archive umstellen (zukünftig)
4. JSON bleibt als Editor-Format, Binär für Runtime/Builds

---

## Phase 4: Fortgeschrittene Systeme (Langfristig)

### 4.1 ✅ Class Default Objects (CDO)

**Referenz**: Abschnitt 1.1 – Eine Default-Instanz pro Klasse als Template für Spawning.

**Implementiert** in `src/Core/Actor/CDO.h` und `src/Core/Actor/CDO.cpp`:
- `ClassDefaultObject` speichert typisierte Component-Defaults (via `IComponentDefault`/`TComponentDefault<T>` Type-Erasure) pro Actor-Klasse.
- `CDORegistry` Singleton: `buildAll()` iteriert `ActorRegistry`, erzeugt temporär jede Actor-Klasse (Temp-ECS-Entity, voller Lifecycle, Capture aller 16 ECS-Component-Typen), räumt danach auf.
- CDO-Konstruktion per Sandbox: Temp-Entity ohne World, `beginPlay()` setzt Components auf, alle Component-Daten werden als Defaults gespeichert.
- `getDefault<T>()` für Zugriff, `isPropertyOverridden()` / `getOverriddenProperties()` für Delta-Erkennung, `getMutableDefault<T>()` für CDO-Editing.
- Reflection-Utilities: `reflectPropertyEquals()`, `reflectCopyProperties()`, `reflectDiffProperties()` in `Reflection.h/.cpp` für property-basiertes Vergleichen und Kopieren (TypeID-aware).
- CDOs werden automatisch in `World::beginPlay()` gebaut (einmalig). Ermöglicht Delta-Serialisierung, Editor-Override-Anzeige, Reset-to-Default.

**Aufwand**: ~8h (nach Reflection) | **Impact**: Mittel – Delta-Serialisierung, Override-Erkennung, Template-basierte Defaults

---

### 4.2 ✅ ObjectHandle-System (Chunked, Generationsgezählt)

**Referenz**: Abschnitt 1.2 – `GUObjectArray` mit Chunked-Architektur und Generationszähler.

**Implementiert** in `src/Core/ObjectHandle.h`:
- `ObjectHandle` (8 Byte: index + generation) für sichere Weak-Referenzen.
- Chunked `ObjectSlotArray` (4096 Slots/Chunk, Free-List-Recycling).
- `TObjectHandle<T>` typisierter Wrapper mit `get()`, `isValid()`, `operator->`.
- `ActorHandle` Typedef für Actor-spezifische Handles.
- `Actor::getHandle()` liefert einen `ActorHandle`; Handle wird in `internalInitialize()` allokiert, in `internalEndPlay()` freigegeben.
- `EObjectFlags` Enum für zukünftige GC-Flags (RootSet, PendingKill, Transactional).
- `World::resolveHandle()` für sichere Weak-Referenz-Auflösung.

---

### 4.3 ⏳ Garbage Collection (einfacher Mark-and-Sweep)

**Referenz**: Abschnitt 1.3 – Property-basiertes Referenz-Tracking + Mark-and-Sweep.

**Aktueller Zustand**: `unique_ptr`-Ownership in World. Deferred Destroy über Pending-Kill-Queue.

**Bewertung**: GC ist für diese Engine aktuell **nicht empfohlen**. Die bestehende Ownership-Semantik (`World` besitzt Actors, Actors besitzen Components) ist klar und deterministisch. GC wird erst relevant bei zirkulären Referenzen oder komplexen Besitzketten.

**Alternative**: Die bestehende Pending-Kill-Queue (deferred destroy) ist das pragmatische Äquivalent. Kann durch einen Frame-verzögerten Destroy-Pool ergänzt werden für sicherere Referenz-Invalidierung.

---

### 4.4 ✅ ECS Archetype-Storage / Mass-Entity-System

**Referenz**: Abschnitt 3 – Archetype-basiertes ECS für Massensimulation.

**Implementiert** in `src/Core/ECS/Archetype/`:
- **ArchetypeTypes.h**: FragmentTypeID, FragmentRegistry (compile-time unique IDs), MassEntity Handle (index+generation), MassEntitySlot, MassCommandBuffer (deferred structural changes), ArchetypeSignature (bitset<64>), MASS_FRAGMENT() Macro.
- **Chunk.h**: ~64KB SoA Chunk-Storage mit ColumnLayout, push/swap-remove/copy Operationen, `computeChunkLayout()` Utility. Cache-Line aligned Spalten.
- **Archetype.h**: Archetype-Klasse (Signatur → Chunk-Pool), allocateRow/removeRow, typisierter Fragment-Zugriff, Query-Matching (required/excluded Signaturen).
- **ArchetypeECS.h/cpp**: `MassEntitySubsystem` Singleton – Entity CRUD (create/batch-create/destroy), Archetype-Graph mit Signatur-Hash-Lookup, `moveEntity()` für strukturelle Änderungen (Fragment add/remove → Archetype-Migration), `forEachChunk()` Queries, Processor-Tick-Loop, Generation-counted Slot-Management.
- **MassProcessor.h**: `MassQuery` (require/exclude Fragments), `ChunkView` (typisierter Array-Zugriff), `MassProcessor` Basisklasse (configure/executeChunk Pattern).
- **MassBridge.h/cpp**: LOD-basierte Actor↔MassEntity Promotion/Demotion. `ERepresentationLOD` (High/Medium/Low/Off), `LODConfig` mit Hysterese, `MassAgentEntry` Tracking, `registerClass()` mit Custom Promotion/Demotion Callbacks, `spawnMassAgents()` Batch-Erstellung, distanzbasierte LOD-Berechnung. Built-in `MassPositionFragment`.

---

## Priorisierte Umsetzungsreihenfolge

| Prio | Item | Phase | Aufwand | Abhängigkeiten |
|------|------|-------|---------|----------------|
| 1 | Component-Lifecycle Fix (beginPlay für Runtime-Components) | 1.4 | 15min | Keine | ✅ |
| 2 | Actor Lifecycle Hooks (preInit/postInit) | 1.2 | 30min | Keine | ✅ |
| 3 | Deferred Spawning | 1.3 | 1h | Keine | ✅ |
| 4 | Tick-Gruppen | 1.1 | 2-3h | Keine | ✅ |
| 5 | Name-System | 2.1 | 4-6h | Keine | ✅ |
| 6 | Game Framework | 2.2 | 6-8h | Keine | ✅ |
| 7 | Tick-Dependencies DAG | 2.3 | 4h | Phase 1.1 | ✅ |
| 8 | Bidirektionales Archive | 3.3 | 8-12h | Keine | ✅ |
| 9 | Reflektions-System | 3.1 | 20-30h | Keine | ✅ |
| 10 | Undo/Redo | 3.2 | 12-16h | Besser nach 3.1/3.3 | ✅ |
| 11 | CDOs | 4.1 | 8h | Phase 3.1 | ✅ |
| 12 | ObjectHandle-System | 4.2 | 6-8h | Keine | ✅ |
| 13 | Archetype ECS | 4.4 | 30-40h | Keine (aber niedrige Prio) | ✅ |

**Empfohlener Start**: Phase 1 (komplett ✅), Phase 2 (komplett ✅), Phase 3 (komplett ✅: Archive, Reflektion, Undo/Redo) abgeschlossen. Phase 4.1 (CDOs ✅), Phase 4.2 (ObjectHandle ✅) und Phase 4.4 (Archetype ECS ✅) implementiert. **Alle priorisierten Roadmap-Items sind abgeschlossen.**

---

## Explizit NICHT übernommen

Folgende UE-Konzepte werden bewusst nicht nachgebaut, weil sie für den aktuellen Engine-Scope unverhältnismäßig wären:

| UE-Konzept | Begründung |
|---|---|
| Unreal Header Tool (UHT) | Makro-basierte Reflektion (Phase 3.1) ist ausreichend |
| UPackage / .uasset Format | Bestehende JSON + HPK-Archive reichen aus |
| GC mit Token-Stream | unique_ptr-Ownership ist klar und deterministisch |
| GC-Cluster | Nur relevant bei >100K Objekte |
| World Partition (OFPA) | Level-Streaming ist bereits implementiert |
| Netzwerk-Replikation | Kein Multiplayer-Scope aktuell |
| Blueprint-System | Python + Native C++ Scripts decken Scripting ab |
| FProperty-basiertes Bool-Packing | Micro-Optimierung, nicht kritisch |
