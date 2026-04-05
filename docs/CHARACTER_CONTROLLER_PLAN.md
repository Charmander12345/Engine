# Character Controller – Implementierungsplan

> Backend-agnostischer Character Controller für die HorizonEngine.
> Unterstützt Jolt Physics (`CharacterVirtual`) und PhysX (`PxController`).
> Branch: `C++-Scripting` | Build-System: CMake 3.12+ | Sprache: C++20

---

## Inhaltsverzeichnis

1. [Übersicht & Designziele](#1-übersicht--designziele)
2. [Architektur](#2-architektur)
3. [ECS-Komponente](#3-ecs-komponente)
4. [IPhysicsBackend-Erweiterung](#4-iphysicsbackend-erweiterung)
5. [JoltBackend-Implementierung](#5-joltbackend-implementierung)
6. [PhysXBackend-Implementierung](#6-physxbackend-implementierung)
7. [PhysicsWorld-Integration](#7-physicsworld-integration)
8. [Scripting-API](#8-scripting-api)
9. [Editor-UI](#9-editor-ui)
10. [Implementierungsreihenfolge](#10-implementierungsreihenfolge)

---

## 1. Übersicht & Designziele

### Problem
Aktuell gibt es nur Raw-Rigidbodies für Spielercharaktere. Das führt zu:
- Rutschen auf Schrägen
- Hängenbleiben an Kanten
- Keine Stufen-Erkennung (Step-Up)
- Keine Ground-Detection
- Instabiles Verhalten bei hohen Geschwindigkeiten

### Designziele
- **Backend-agnostisch**: Gleiche API für Jolt und PhysX — `IPhysicsBackend` wird erweitert
- **Kinematisch, nicht dynamisch**: Character Controller ist kein Rigidbody, sondern ein kinematisches Objekt das die Physik-Welt abfragt (Sweep/Overlap) statt von ihr simuliert zu werden
- **Industriestandard-Features**: Step-Up, Slope Limit, Ground Detection, Push-back, Walking/Falling States
- **ECS-integriert**: Neue `CharacterControllerComponent` — kann neben `TransformComponent` und `CollisionComponent` verwendet werden
- **Stack-Allokation bevorzugt**: Kein Heap-Alloc im Hot-Path (per-frame Update)

---

## 2. Architektur

```
┌──────────────────────────────────────────────┐
│              PhysicsWorld                      │
│  syncCharactersToBackend()                     │
│  syncCharactersFromBackend()                   │
│  ↕                                             │
│  IPhysicsBackend (abstrakt)                    │
│  ├─ CharacterDesc (Erstellungsparameter)       │
│  ├─ CharacterHandle (opaker Handle)            │
│  ├─ createCharacter() / removeCharacter()      │
│  ├─ updateCharacter(handle, dt, desiredVel)    │
│  ├─ getCharacterPosition()                     │
│  ├─ isCharacterGrounded()                      │
│  └─ getCharacterGroundNormal()                 │
│         ↕                        ↕             │
│  ┌──────────────┐  ┌──────────────────────┐    │
│  │ JoltBackend   │  │   PhysXBackend       │    │
│  │ CharacterVir- │  │   PxCapsuleControl-  │    │
│  │ tual          │  │   ler                │    │
│  └──────────────┘  └──────────────────────┘    │
└──────────────────────────────────────────────┘
```

### Separation of Concerns
- **`CharacterControllerComponent`** (ECS): Konfigurationsparameter (Höhe, Radius, Step-Up, Slope Limit, etc.) + Runtime-State (isGrounded, groundNormal)
- **`IPhysicsBackend`**: Abstrakte Character-API (create/remove/update/query)
- **`PhysicsWorld`**: ECS ↔ Backend Synchronisation (wie bei Rigidbodies)
- **`JoltBackend`**: Jolt `CharacterVirtual` mit `ExtendedUpdateSettings`
- **`PhysXBackend`**: PhysX `PxCapsuleController` mit `PxControllerManager`

---

## 3. ECS-Komponente

### CharacterControllerComponent

```cpp
struct CharacterControllerComponent
{
    // ── Shape (Kapsel) ──────────────────────────────
    float radius{ 0.3f };        // Kapsel-Radius
    float height{ 1.8f };        // Gesamthöhe (inkl. Halbkugeln)

    // ── Movement Parameters ─────────────────────────
    float maxSlopeAngle{ 45.0f };   // Max begehbare Neigung (Grad)
    float stepUpHeight{ 0.3f };     // Max Stufenhöhe die überwunden wird
    float skinWidth{ 0.02f };       // Abstand zur Kollisionsgeometrie

    // ── Gravity & Jumping ───────────────────────────
    float gravityFactor{ 1.0f };    // Skaliert Welt-Gravitation
    float maxFallSpeed{ 50.0f };    // Terminal Velocity (m/s)

    // ── Runtime State (nicht serialisiert) ───────────
    bool  isGrounded{ false };
    float groundNormal[3]{ 0.0f, 1.0f, 0.0f };
    float groundAngle{ 0.0f };     // Neigung in Grad
    float velocity[3]{ 0.0f, 0.0f, 0.0f }; // Aktuelle Geschwindigkeit
};
```

### ECS-Integration
- **ComponentKind**: `CharacterController = 13` (nach `ParticleEmitter = 12`, Index innerhalb bestehender Grenzen)
- **MaxComponentTypes**: Von 14 auf 15 erhöhen
- **SparseSet**: `m_characterControllerComponents`
- **Exklusivität**: `CharacterControllerComponent` und `PhysicsComponent` sind **exklusiv** — eine Entity hat entweder einen Character Controller ODER einen Rigidbody, nicht beides

---

## 4. IPhysicsBackend-Erweiterung

### Neue Typen

```cpp
using CharacterHandle = uint64_t;
static constexpr CharacterHandle InvalidCharacter = 0;

struct CharacterDesc
{
    // Shape (Kapsel)
    float radius{ 0.3f };
    float height{ 1.8f };          // Gesamthöhe

    // Movement
    float maxSlopeAngle{ 45.0f };  // Grad
    float stepUpHeight{ 0.3f };    // Meter
    float skinWidth{ 0.02f };

    // Initial transform
    float position[3]{ 0.0f, 0.0f, 0.0f };
    float rotationYDeg{ 0.0f };

    // Entity ID
    uint32_t entityId{ 0 };
};

struct CharacterState
{
    float position[3]{};
    bool  isGrounded{ false };
    float groundNormal[3]{ 0.0f, 1.0f, 0.0f };
    float groundAngle{ 0.0f };
    float velocity[3]{};
};
```

### Neue virtuelle Methoden in IPhysicsBackend

```cpp
// ── Character Controller ────────────────────────────────────
virtual CharacterHandle createCharacter(const CharacterDesc& desc) = 0;
virtual void            removeCharacter(CharacterHandle handle) = 0;
virtual void            removeAllCharacters() = 0;

/// Move the character by desired velocity for one fixed step.
/// The backend handles collision detection, step-up, slope limiting, gravity.
virtual void updateCharacter(CharacterHandle handle,
                             float dt,
                             float desiredVelX, float desiredVelY, float desiredVelZ,
                             float gravityX, float gravityY, float gravityZ) = 0;

/// Read back the character's state after the update.
virtual CharacterState getCharacterState(CharacterHandle handle) const = 0;

/// Teleport the character to a specific position (no collision).
virtual void setCharacterPosition(CharacterHandle handle,
                                  float x, float y, float z) = 0;

/// Get entity ID from character handle.
virtual CharacterHandle getCharacterForEntity(uint32_t entity) const = 0;
```

---

## 5. JoltBackend-Implementierung

### Jolt-API: `JPH::CharacterVirtual`

Jolt bietet `CharacterVirtual` — ein nicht-physik-simuliertes Objekt das Sweep-Queries gegen die Physik-Welt durchführt.

**Verwendete Jolt-Klassen:**
- `JPH::CharacterVirtualSettings` — Konfiguration (Shape, MaxSlopeAngle, MaxStrength, etc.)
- `JPH::CharacterVirtual` — Laufzeit-Instanz
- `JPH::ExtendedUpdateSettings` — Erweiterte Update-Optionen (StickToFloor, WalkStairs)

**Mapping:**

| CharacterDesc       | Jolt                                                |
|---------------------|-----------------------------------------------------|
| radius, height      | `CapsuleShape(halfHeight, radius)`                 |
| maxSlopeAngle       | `CharacterVirtualSettings::mMaxSlopeAngle`          |
| stepUpHeight        | `ExtendedUpdateSettings::mWalkStairsStepUp`         |
| skinWidth           | `CharacterVirtualSettings::mCharacterPadding`        |

**Update-Flow (pro Frame):**
1. `character->SetLinearVelocity(desiredVel + gravity * dt)`
2. `character->ExtendedUpdate(dt, gravity, updateSettings, broadPhaseFilter, ...)` — internes Sweep, Step-Up, Slope-Limiting
3. `character->GetPosition()` → zurück ins ECS
4. `character->GetGroundState()` → `isGrounded` (OnGround/OnSteepGround/InAir)
5. `character->GetGroundNormal()` → `groundNormal`

**Implementierungsdetails:**
- Pro Entity ein `JPH::CharacterVirtual*` in `m_entityToCharacter` Map
- `createCharacter()`: CapsuleShape erstellen, CharacterVirtualSettings konfigurieren, `new CharacterVirtual(settings, position, rotation, physicsSystem)`
- `removeCharacter()`: `delete` + Map-Eintrag entfernen
- `updateCharacter()`: ExtendedUpdate mit WalkStairs + StickToFloor
- Broadphase/Object-Layer-Filter aus bestehendem Body-System wiederverwenden

### Neue Members in JoltBackend

```cpp
std::map<uint32_t, JPH::CharacterVirtual*> m_entityToCharacter;
std::map<JPH::CharacterVirtual*, uint32_t> m_characterToEntity;
uint64_t m_nextCharacterHandle{ 1 };
std::map<uint64_t, JPH::CharacterVirtual*> m_handleToCharacter;
std::map<JPH::CharacterVirtual*, uint64_t> m_characterToHandle;
```

---

## 6. PhysXBackend-Implementierung

### PhysX-API: `PxController`

PhysX bietet `PxCapsuleController` über den `PxControllerManager`.

**Verwendete PhysX-Klassen:**
- `PxControllerManager` — Factory und Verwaltung aller Controller
- `PxCapsuleControllerDesc` — Erstellungsparameter
- `PxCapsuleController` — Laufzeit-Instanz
- `PxControllerFilters` — Kollisionsfilter für `move()`

**Mapping:**

| CharacterDesc       | PhysX                                               |
|---------------------|-----------------------------------------------------|
| radius              | `PxCapsuleControllerDesc::radius`                   |
| height              | `PxCapsuleControllerDesc::height` (Zylinder-Höhe)   |
| maxSlopeAngle       | `PxCapsuleControllerDesc::slopeLimit` (cos(angle))   |
| stepUpHeight        | `PxCapsuleControllerDesc::stepOffset`                |
| skinWidth           | `PxCapsuleControllerDesc::contactOffset`             |

**Update-Flow (pro Frame):**
1. `displacement = desiredVel * dt + gravity * dt * dt * 0.5`
2. `controller->move(displacement, minDist, dt, filters)` → `PxControllerCollisionFlags`
3. `controller->getPosition()` → zurück ins ECS
4. `flags & PxControllerCollisionFlag::eCOLLISION_DOWN` → `isGrounded`

**Implementierungsdetails:**
- `PxControllerManager` wird bei `initialize()` erstellt
- Pro Entity ein `PxCapsuleController*` in `m_entityToController` Map
- Ground-Normal via `PxControllerState::touchedShape` + Kontaktpunkt-Berechnung
- `removeCharacter()`: `controller->release()` + Map-Eintrag entfernen

### Neue Members in PhysXBackend

```cpp
physx::PxControllerManager* m_controllerManager{ nullptr };
std::map<uint32_t, physx::PxController*> m_entityToController;
std::map<physx::PxController*, uint32_t> m_controllerToEntity;
uint64_t m_nextCharacterHandle{ 1 };
std::map<uint64_t, physx::PxController*> m_handleToController;
std::map<physx::PxController*, uint64_t> m_controllerToHandle;
```

---

## 7. PhysicsWorld-Integration

### Sync-Methoden

```cpp
void syncCharactersToBackend();   // ECS → Backend (erstellt/entfernt Characters)
void syncCharactersFromBackend(); // Backend → ECS (liest Position/State zurück)
```

### step() Erweiterung

```
step(dt):
  1. syncBodiesToBackend()          // Rigidbodies
  2. syncCharactersToBackend()      // Character Controllers (NEU)
  3. m_backend->update(fixedDt)     // Physik-Simulation
  4. updateCharacters(fixedDt)      // Character-Update (NEU)
  5. syncBodiesFromBackend()        // Rigidbody → ECS
  6. syncCharactersFromBackend()    // Character → ECS (NEU)
  7. fireCollisionEvents()
  8. updateOverlapTracking()
```

### updateCharacters(float dt)

```cpp
void PhysicsWorld::updateCharacters(float dt)
{
    auto& ecs = ECS::ECSManager::Instance();
    ECS::Schema schema;
    schema.require<ECS::TransformComponent>()
          .require<ECS::CharacterControllerComponent>();

    for (auto entity : ecs.getEntitiesMatchingSchema(schema))
    {
        auto* cc = ecs.getComponent<ECS::CharacterControllerComponent>(entity);
        auto handle = m_backend->getCharacterForEntity(entity);
        if (handle == IPhysicsBackend::InvalidCharacter) continue;

        float gx, gy, gz;
        getGravity(gx, gy, gz);
        gx *= cc->gravityFactor;
        gy *= cc->gravityFactor;
        gz *= cc->gravityFactor;

        m_backend->updateCharacter(handle, dt,
            cc->velocity[0], cc->velocity[1], cc->velocity[2],
            gx, gy, gz);
    }
}
```

### Neue State-Members

```cpp
std::set<uint32_t> m_trackedCharacters;  // Entities mit Character im Backend
```

---

## 8. Scripting-API

### C++ (GameplayAPI / INativeScript)

```cpp
// GameplayAPI-Namespace
bool  isCharacterGrounded(ECS::Entity entity);
float getCharacterGroundAngle(ECS::Entity entity);
void  setCharacterVelocity(ECS::Entity entity, float vx, float vy, float vz);
void  getCharacterVelocity(ECS::Entity entity, float& vx, float& vy, float& vz);

// INativeScript Convenience
bool  isGrounded();
void  setMovementVelocity(float vx, float vy, float vz);
```

### Python (engine.physics)

```python
engine.physics.is_character_grounded(entity) -> bool
engine.physics.get_character_ground_angle(entity) -> float
engine.physics.set_character_velocity(entity, vx, vy, vz)
engine.physics.get_character_velocity(entity) -> (vx, vy, vz)
```

---

## 9. Editor-UI

### OutlinerPanel – CharacterController-Sektion

| Property      | Control    | Beschreibung                     |
|---------------|-----------|----------------------------------|
| Radius        | Float     | Kapsel-Radius                    |
| Height        | Float     | Gesamthöhe                       |
| Max Slope     | Float     | Max begehbare Neigung (Grad)     |
| Step Up       | Float     | Max Stufenhöhe                   |
| Skin Width    | Float     | Collision Padding                |
| Gravity Factor| Float     | Gravitations-Skalierung          |
| Max Fall Speed| Float     | Terminal Velocity                |
| Is Grounded   | Label     | Runtime-Anzeige (read-only)      |

### Add Component Dropdown

- Neuer Eintrag: "Character Controller"
- **Exklusivität**: Wird ausgegraut wenn `PhysicsComponent` vorhanden (und umgekehrt)

---

## 10. Implementierungsreihenfolge

```
Schritt 1: CharacterControllerComponent in Components.h
Schritt 2: ECS-Registrierung (ComponentKind, Traits, Storage, MaxComponentTypes)
Schritt 3: IPhysicsBackend erweitern (CharacterDesc, virtuelle Methoden)
Schritt 4: JoltBackend implementieren (CharacterVirtual)
Schritt 5: PhysXBackend implementieren (PxController)
Schritt 6: PhysicsWorld Integration (sync, updateCharacters, lifecycle)
Schritt 7: Serialisierung (EngineLevel — serialize/deserialize)
Schritt 8: Editor-UI (OutlinerPanel — CharacterController-Sektion)
Schritt 9: Scripting-API (GameplayAPI, INativeScript, Python Bindings)
Schritt 10: engine.pyi aktualisieren
```

**Geschätzte Aufwände:**
| Schritt | Aufwand |
|---------|---------|
| ECS-Komponente + Registrierung | Gering |
| IPhysicsBackend-Erweiterung | Gering |
| JoltBackend (CharacterVirtual) | Mittel |
| PhysXBackend (PxController) | Mittel |
| PhysicsWorld Integration | Mittel |
| Serialisierung | Gering |
| Editor-UI | Mittel |
| Scripting-API | Gering |

---

*Erstellt für das HorizonEngine-Projekt. Basierend auf der bestehenden IPhysicsBackend-Architektur.*
