# C++ Scripting System – Implementierungsplan

> Vollständiger Technischer Plan für die Integration eines C++-Gameplay-Scripting-Systems in die HorizonEngine.
> Branch: `C++-Scripting` | Build-System: CMake 3.12+ | Sprache: C++20 | Plattform: Windows (x64), Linux, macOS

---

## Inhaltsverzeichnis

1. [Übersicht & Architektur-Entscheidungen](#1-übersicht--architektur-entscheidungen)
2. [Was wird benötigt](#2-was-wird-benötigt)
3. [Ordnerstruktur](#3-ordnerstruktur)
4. [Klassenstruktur & Interfaces](#4-klassenstruktur--interfaces)
5. [ECS-Integration: C++ und Python Scripts an Entities hängen](#5-ecs-integration-c-und-python-scripts-an-entities-hängen)
6. [DLL-basiertes Plugin-System (Linken von Spieler-C++-Code)](#6-dll-basiertes-plugin-system-linken-von-spieler-c-code)
7. [Runtime-Funktionsfindung (Reflection / Registry)](#7-runtime-funktionsfindung-reflection--registry)
8. [Python ↔ C++ Kommunikation](#8-python--c-kommunikation)
9. [Hot-Reloading im Editor](#9-hot-reloading-im-editor)
10. [Build-Pipeline-Erweiterung](#10-build-pipeline-erweiterung)
11. [Implementierungs-Reihenfolge (Phasen)](#11-implementierungs-reihenfolge-phasen)

---

## 1. Übersicht & Architektur-Entscheidungen

### Kernkonzept

Der Spieleentwickler schreibt Gameplay-Logik als **C++-Klassen** (genannt `NativeScript`), die in eine **Shared Library (DLL/SO)** kompiliert werden. Die Engine lädt diese DLL zur Laufzeit über `LoadLibrary`/`dlopen` und findet die registrierten Script-Klassen über ein **Registry-Pattern** mit automatischer Registrierung über ein Makro.

### Warum DLL-basiert (statt statisches Linken)?

| Ansatz | Pro | Contra |
|--------|-----|--------|
| **DLL (gewählt)** | Hot-Reload möglich, kein Engine-Recompile, schnelle Iteration | Etwas komplexer, Symbol-Export nötig |
| Statisches Linken | Einfacher, kein Export nötig | Kein Hot-Reload, Engine muss bei jeder Änderung neu gebaut werden |
| Interpretiert (Lua/etc.) | Triviales Hot-Reload | Neue Abhängigkeit, Performance-Overhead |

### Beziehung zu Python

Python-Scripting bleibt bestehen. Der Entwickler kann pro Entity **entweder** ein Python-Script **oder** ein C++-NativeScript **oder beides** zuweisen. Beide Systeme nutzen dieselben Lifecycle-Events (`onLoaded`, `tick`, `onBeginOverlap`, `onEndOverlap`).

---

## 2. Was wird benötigt

### Engine-seitig (neue Komponenten)

| Komponente | Beschreibung |
|-----------|--------------|
| `NativeScriptComponent` | Neue ECS-Komponente: speichert den Klassennamen des C++-Scripts |
| `NativeScriptManager` | Singleton: lädt/entlädt die Gameplay-DLL, verwaltet Script-Instanzen |
| `INativeScript` | Interface/Basisklasse für alle C++-Scripts |
| `NativeScriptRegistry` | Globale Registry: Klassenname → Factory-Funktion |
| `GameplayAPI.h` | Public Engine-API-Header für Spieler-Code (ECS-Zugriff, Input, Physics, Audio) |
| `NativeScriptHotReload` | Watcher + Reload-Logik für DLL-Änderungen im Editor |

### Spieler-seitig (Projekt-Template)

| Datei | Beschreibung |
|-------|--------------|
| `GameScripts/CMakeLists.txt` | CMake-Projekt das gegen die Engine-API linkt und eine DLL erzeugt |
| `GameScripts/MyScript.h/cpp` | Beispiel-Script mit `REGISTER_NATIVE_SCRIPT(MyScript)` Makro |
| `GameScripts/GameplayAPI/` | Kopierte/verlinkte Engine-API-Header (oder als CMake-Interface-Library) |

### Tools & Abhängigkeiten

- **CMake** (bereits vorhanden – Bootstrap-System erkennt es)
- **C++ Compiler** (bereits vorhanden – Bootstrap-System installiert MSVC/Clang)
- **Kein neues externes Library nötig** – alles mit Standard-C++20 + OS-APIs realisierbar

---

## 3. Ordnerstruktur

### Engine-seitige Dateien (im Repository)

```
src/
├── NativeScripting/                     # Neues Engine-Modul
│   ├── CMakeLists.txt                   # SHARED Library "NativeScripting"
│   ├── INativeScript.h                  # Basisklasse / Interface
│   ├── NativeScriptComponent.h          # ECS-Komponente (Klassenname + Instanz-Pointer)
│   ├── NativeScriptManager.h            # Singleton: DLL laden, Instanzen verwalten
│   ├── NativeScriptManager.cpp
│   ├── NativeScriptRegistry.h           # Klassenname → Factory (macro-basiert)
│   ├── NativeScriptRegistry.cpp
│   ├── NativeScriptHotReload.h          # DLL File-Watcher + Reload-Logik
│   ├── NativeScriptHotReload.cpp
│   ├── GameplayAPI.h                    # Zentral-Include für Spieler-Code
│   └── GameplayAPIExport.h              # DLL-Export-Macros (GAMEPLAY_API)
```

### Spieler-Projekt (generiert vom Editor)

```
<ProjektRoot>/
├── Content/
│   ├── Scripts/                         # Python-Scripts (bestehend)
│   └── NativeScripts/                   # C++-Quellcode des Spielers
│       ├── CMakeLists.txt               # Baut GameScripts.dll
│       ├── PlayerController.h
│       ├── PlayerController.cpp
│       ├── EnemyAI.h
│       ├── EnemyAI.cpp
│       └── ...
├── Binary/
│   └── GameScripts/                     # CMake-Build-Output
│       └── GameScripts.dll              # Die geladene Gameplay-DLL
└── Config/
    └── NativeScripts.json               # Mapping: Script-Klasse → Quelldatei
```

---

## 4. Klassenstruktur & Interfaces

### 4.1 `INativeScript` – Basisklasse für alle C++-Scripts

```cpp
// src/NativeScripting/INativeScript.h
#pragma once
#include <string>

namespace ECS { using Entity = unsigned int; }

class INativeScript
{
public:
    virtual ~INativeScript() = default;

    // ── Lifecycle-Events (vom Spieler überschrieben) ──────────────
    virtual void onLoaded()  {}          // Einmalig nach Zuweisung / Level-Load
    virtual void tick(float deltaTime) {} // Jeden Frame
    virtual void onBeginOverlap(ECS::Entity other) {} // Physik-Overlap Start
    virtual void onEndOverlap(ECS::Entity other)   {} // Physik-Overlap Ende
    virtual void onDestroy() {}          // Bevor Entity/Script entfernt wird

    // ── Engine setzt diese automatisch ────────────────────────────
    ECS::Entity getEntity() const { return m_entity; }

private:
    friend class NativeScriptManager;
    ECS::Entity m_entity{ 0 };
};
```

### 4.2 `NativeScriptComponent` – ECS-Komponente

```cpp
// src/NativeScripting/NativeScriptComponent.h
#pragma once
#include <string>

class INativeScript;

namespace ECS
{
    struct NativeScriptComponent
    {
        std::string className;              // Registrierter Klassenname ("PlayerController")
        INativeScript* instance{ nullptr };  // Lebende Instanz (von NativeScriptManager verwaltet)
    };
}
```

**ECS-Integration:**
- Neuer Eintrag in `ComponentKind`: `NativeScript = 13`
- `MaxComponentTypes` von 13 auf 14 erhöhen
- `ComponentTraits<NativeScriptComponent>` Spezialisierung hinzufügen

### 4.3 `NativeScriptRegistry` – Automatische Klassenregistrierung

```cpp
// src/NativeScripting/NativeScriptRegistry.h
#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include "INativeScript.h"
#include "GameplayAPIExport.h"

class NativeScriptRegistry
{
public:
    using FactoryFunc = std::function<INativeScript*()>;

    static NativeScriptRegistry& Instance();

    void registerClass(const std::string& name, FactoryFunc factory);
    void unregisterAll();

    INativeScript* createInstance(const std::string& className) const;
    bool hasClass(const std::string& className) const;
    std::vector<std::string> getRegisteredClassNames() const;

private:
    std::unordered_map<std::string, FactoryFunc> m_factories;
};

// ── Registrierungsmakro (vom Spieler in jeder Script-Datei verwendet) ──
// Erzeugt ein statisches Objekt das sich beim DLL-Load automatisch registriert.
#define REGISTER_NATIVE_SCRIPT(ClassName)                                    \
    static struct ClassName##_AutoRegister {                                 \
        ClassName##_AutoRegister() {                                         \
            NativeScriptRegistry::Instance().registerClass(                  \
                #ClassName,                                                  \
                []() -> INativeScript* { return new ClassName(); });         \
        }                                                                    \
    } g_##ClassName##_autoReg;
```

### 4.4 `NativeScriptManager` – Singleton (Kern des Systems)

```cpp
// src/NativeScripting/NativeScriptManager.h
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "INativeScript.h"

namespace ECS { using Entity = unsigned int; }

class NativeScriptManager
{
public:
    static NativeScriptManager& Instance();

    // ── DLL-Management ────────────────────────────────────────────
    bool loadGameplayDLL(const std::string& dllPath);
    void unloadGameplayDLL();
    bool isDLLLoaded() const;
    const std::string& getDLLPath() const;

    // ── Script-Lifecycle ──────────────────────────────────────────
    /// Erstellt Instanzen für alle Entities mit NativeScriptComponent
    void initializeScripts();
    /// Ruft tick() auf allen aktiven Instanzen auf
    void updateScripts(float deltaTime);
    /// Räumt alle Instanzen auf (Level-Wechsel / Shutdown)
    void shutdownScripts();

    /// Erstellt eine Instanz für eine spezifische Entity
    void createInstance(ECS::Entity entity, const std::string& className);
    /// Zerstört die Instanz einer Entity
    void destroyInstance(ECS::Entity entity);

    // ── Overlap-Dispatch ──────────────────────────────────────────
    void dispatchBeginOverlap(ECS::Entity self, ECS::Entity other);
    void dispatchEndOverlap(ECS::Entity self, ECS::Entity other);

    // ── Hot-Reload (Editor) ───────────────────────────────────────
    #if ENGINE_EDITOR
    void hotReload();
    #endif

private:
    NativeScriptManager() = default;
    ~NativeScriptManager();

    void* m_dllHandle{ nullptr };                       // HMODULE / void*
    std::string m_dllPath;
    std::unordered_map<ECS::Entity, INativeScript*> m_instances;
};
```

### 4.5 `GameplayAPI.h` – Public API für Spieler-Code

```cpp
// src/NativeScripting/GameplayAPI.h
// Zentral-Include den der Spieler in seinen Scripts einbindet.
#pragma once

#include "INativeScript.h"
#include "NativeScriptRegistry.h"
#include "GameplayAPIExport.h"

// Zugriff auf Engine-Subsysteme (vereinfachte Wrapper)
namespace GameplayAPI
{
    // ── ECS ───────────────────────────────────────────────────────
    // Transform lesen/schreiben
    bool getPosition(ECS::Entity entity, float outPos[3]);
    bool setPosition(ECS::Entity entity, const float pos[3]);
    bool getRotation(ECS::Entity entity, float outRot[3]);
    bool setRotation(ECS::Entity entity, const float rot[3]);
    bool getScale(ECS::Entity entity, float outScale[3]);
    bool setScale(ECS::Entity entity, const float scale[3]);

    // Entity erstellen/entfernen
    ECS::Entity createEntity();
    bool removeEntity(ECS::Entity entity);
    ECS::Entity findEntityByName(const char* name);

    // ── Input ─────────────────────────────────────────────────────
    bool isKeyDown(int sdlKey);
    bool isKeyPressed(int sdlKey);   // Nur im ersten Frame
    bool isMouseButtonDown(int button);
    void getMouseDelta(float& dx, float& dy);

    // ── Physics ───────────────────────────────────────────────────
    void setVelocity(ECS::Entity entity, const float vel[3]);
    void addForce(ECS::Entity entity, const float force[3]);
    void addImpulse(ECS::Entity entity, const float impulse[3]);

    // ── Audio ─────────────────────────────────────────────────────
    int playSound(const char* assetPath, float volume = 1.0f);
    void stopSound(int handle);

    // ── Logging ───────────────────────────────────────────────────
    void logInfo(const char* msg);
    void logWarning(const char* msg);
    void logError(const char* msg);

    // ── Time ──────────────────────────────────────────────────────
    float getDeltaTime();
    float getTotalTime();
}
```

### 4.6 `GameplayAPIExport.h` – DLL-Export-Macros

```cpp
// src/NativeScripting/GameplayAPIExport.h
#pragma once

#if defined(_WIN32)
    #if defined(GAMEPLAY_DLL_EXPORT)
        #define GAMEPLAY_API __declspec(dllexport)
    #else
        #define GAMEPLAY_API __declspec(dllimport)
    #endif
#else
    #define GAMEPLAY_API __attribute__((visibility("default")))
#endif
```

---

## 5. ECS-Integration: C++ und Python Scripts an Entities hängen

### 5.1 Aktuelle Situation

Aktuell hat jede Entity einen optionalen `ScriptComponent` (Python):
```cpp
struct ScriptComponent {
    std::string scriptPath;        // "Content/Scripts/MyScript.py"
    unsigned int scriptAssetId{0};
};
```

### 5.2 Neues duales System

Es gibt **zwei getrennte Komponenten** – eine Entity kann beide gleichzeitig haben:

| Komponente | Typ | Inhalt |
|-----------|-----|--------|
| `ScriptComponent` (bestehend) | Python | `scriptPath` → `.py`-Datei |
| `NativeScriptComponent` (neu) | C++ | `className` → Registrierter Klassenname |

**Warum zwei getrennte Komponenten statt einer kombinierten?**
- Clean Separation of Concerns
- Man kann unabhängig Python oder C++ Scripts zuweisen
- Kein Breaking Change im bestehenden System
- `ComponentKind::NativeScript` ermöglicht effiziente ECS-Queries

### 5.3 Component-Kind-Erweiterung

```cpp
// ECS.h – Erweitert
enum class ComponentKind : size_t
{
    Transform = 0,
    Mesh,
    Material,
    Light,
    Camera,
    Physics,
    Script,          // Python (bestehend)
    Name,
    Collision,
    HeightField,
    Lod,
    Animation,
    ParticleEmitter,
    NativeScript     // C++ (NEU, Index 13)
};

static constexpr size_t MaxComponentTypes = 14; // war 13
```

### 5.4 Editor-UI: Script-Zuweisung

Im Properties-Panel (Inspector) wird bei selektierter Entity ein neuer Abschnitt angezeigt:

```
┌─ Native Script (C++) ─────────────────────┐
│  Class: [PlayerController     ▾]          │
│  Status: ● Active                         │
│  [Detach]                                 │
└───────────────────────────────────────────┘
┌─ Script (Python) ─────────────────────────┐
│  Script: Content/Scripts/Movement.py      │
│  [Detach]                                 │
└───────────────────────────────────────────┘
```

Das Dropdown zeigt alle Klassen aus `NativeScriptRegistry::getRegisteredClassNames()`.

### 5.5 Lifecycle-Reihenfolge pro Frame

```
1. NativeScriptManager::updateScripts(dt)  → Alle C++-Script tick()
2. Scripting::UpdateScripts(dt)             → Alle Python-Script tick()
3. PhysicsWorld::step(dt)                   → Physik-Simulation
4. Overlap-Dispatch an beide Systeme
```

C++ Scripts laufen **vor** Python Scripts, damit Python-Scripts auf Ergebnisse von C++ reagieren können (z.B. Position die C++ gesetzt hat).

---

## 6. DLL-basiertes Plugin-System (Linken von Spieler-C++-Code)

### 6.1 Wie die DLL aufgebaut ist

Die Gameplay-DLL exportiert **eine einzige C-Funktion** als Entry-Point:

```cpp
// Im Spieler-Projekt (generiert)
extern "C" GAMEPLAY_API void RegisterGameScripts()
{
    // Wird automatisch durch die REGISTER_NATIVE_SCRIPT Makros
    // bei DLL-Load aufgerufen (statische Initialisierer)
    // Diese Funktion dient als expliziter Fallback / Validierung
}
```

**Tatsächlich** werden die Scripts über **statische Initialisierer** registriert: Jedes `REGISTER_NATIVE_SCRIPT(ClassName)` Makro erzeugt ein statisches Objekt, dessen Konstruktor die Klasse in der Registry anmeldet. Das passiert automatisch beim `LoadLibrary()`/`dlopen()`-Aufruf.

### 6.2 DLL-Lade-Sequenz

```
┌─────────────────────────────────────────────────┐
│  NativeScriptManager::loadGameplayDLL(path)     │
│                                                  │
│  1. Falls vorherige DLL geladen:                │
│     a. shutdownScripts() → onDestroy() + delete │
│     b. Registry.unregisterAll()                 │
│     c. FreeLibrary() / dlclose()                │
│                                                  │
│  2. LoadLibrary(path) / dlopen(path)            │
│     → Statische Initialisierer laufen           │
│     → REGISTER_NATIVE_SCRIPT Makros registrieren│
│       Klassen in NativeScriptRegistry            │
│                                                  │
│  3. Validierung: mindestens 1 Klasse registriert│
│                                                  │
│  4. initializeScripts()                         │
│     → Für jede Entity mit NativeScriptComponent:│
│       a. Registry.createInstance(className)      │
│       b. instance->m_entity = entity            │
│       c. instance->onLoaded()                   │
└─────────────────────────────────────────────────┘
```

### 6.3 Linken der Spieler-DLL gegen die Engine

Die Gameplay-DLL muss Engine-Funktionen aufrufen können (`GameplayAPI::setPosition()` etc.). Dafür gibt es zwei Ansätze:

**Gewählter Ansatz: Engine-API als Shared Library**

Die Engine exportiert die `GameplayAPI`-Funktionen aus dem `NativeScripting`-Modul (das bereits als SHARED Library kompiliert wird). Die Spieler-DLL linkt gegen diese Library:

```cmake
# Content/NativeScripts/CMakeLists.txt (Spieler-Projekt)
cmake_minimum_required(VERSION 3.12)
project(GameScripts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

# Engine-API-Header (werden von der Engine bereitgestellt)
set(ENGINE_API_DIR "${ENGINE_SOURCE_DIR}/src/NativeScripting")

# Alle .cpp Dateien im Verzeichnis sammeln
file(GLOB GAME_SCRIPT_SOURCES "*.cpp")
file(GLOB GAME_SCRIPT_HEADERS "*.h")

add_library(GameScripts SHARED ${GAME_SCRIPT_SOURCES} ${GAME_SCRIPT_HEADERS})

target_include_directories(GameScripts PRIVATE ${ENGINE_API_DIR})

# Gegen Engine NativeScripting Library linken
target_link_libraries(GameScripts PRIVATE NativeScripting)

target_compile_definitions(GameScripts PRIVATE GAMEPLAY_DLL_EXPORT=1)

if(WIN32)
    set_target_properties(GameScripts PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
```

**Alternative (nicht gewählt aber dokumentiert): Function-Pointer-Table**
Statt direkt zu linken, übergibt die Engine beim DLL-Load einen Struct mit Funktionszeigern. Vorteil: Keine Link-Time-Abhängigkeit. Nachteil: Umständlichere API, jede Funktion muss durch Pointer aufgerufen werden.

### 6.4 CMake-Integration (Engine-Seite)

```cmake
# src/NativeScripting/CMakeLists.txt
set(NATIVE_SCRIPTING_SOURCES
    NativeScriptManager.cpp
    NativeScriptRegistry.cpp
    NativeScriptHotReload.cpp
    GameplayAPI.cpp           # Implementiert die API-Funktionen
)

set(NATIVE_SCRIPTING_HEADERS
    INativeScript.h
    NativeScriptComponent.h
    NativeScriptManager.h
    NativeScriptRegistry.h
    NativeScriptHotReload.h
    GameplayAPI.h
    GameplayAPIExport.h
)

add_library(NativeScripting SHARED
    ${NATIVE_SCRIPTING_SOURCES}
    ${NATIVE_SCRIPTING_HEADERS})

set_target_properties(NativeScripting PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)

target_include_directories(NativeScripting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(NativeScripting PRIVATE
    Core
    Physics
    Logger
    Diagnostics
    SDL3::SDL3)

target_compile_definitions(NativeScripting PRIVATE GAMEPLAY_DLL_EXPORT=1)

if(WIN32)
    set_target_properties(NativeScripting PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
endif()
```

---

## 7. Runtime-Funktionsfindung (Reflection / Registry)

### 7.1 Das Problem

C++ hat keine eingebaute Reflection wie C# oder Java. Die Engine muss zur Laufzeit:
1. Wissen, welche Script-Klassen existieren
2. Instanzen dieser Klassen erstellen können
3. Die richtigen Lifecycle-Methoden aufrufen

### 7.2 Die Lösung: Automatische Registrierung über statische Initialisierer

```
Spieler schreibt:                   Engine-seitig:
                                    
 PlayerController.cpp               NativeScriptRegistry
 ┌────────────────────┐             ┌──────────────────────────────┐
 │ #include "Gameplay │             │ factories = {                │
 │  API.h"            │  DLL-Load   │   "PlayerController" → λ,   │
 │                    │ ──────────> │   "EnemyAI"          → λ,   │
 │ class PlayerCtrl : │  statische  │   "Projectile"       → λ    │
 │   public INativeS. │  Init.      │ }                            │
 │ { ... };           │             │                              │
 │                    │             │ createInstance("PlayerCtrl") │
 │ REGISTER_NATIVE_   │             │  → new PlayerController()   │
 │  SCRIPT(PlayerCtrl)│             └──────────────────────────────┘
 └────────────────────┘
```

**Ablauf im Detail:**

1. `LoadLibrary("GameScripts.dll")` wird aufgerufen
2. Das OS lädt die DLL in den Prozess-Speicher
3. Die C++-Runtime führt alle **statischen Initialisierer** aus
4. Jedes `REGISTER_NATIVE_SCRIPT` Makro hat ein statisches Objekt erzeugt
5. Dessen Konstruktor ruft `NativeScriptRegistry::Instance().registerClass(name, factory)` auf
6. Die Factory ist ein Lambda: `[]() { return new ClassName(); }`
7. Nach dem Load kann die Engine `Registry.getRegisteredClassNames()` abfragen
8. Um eine Instanz zu erstellen: `Registry.createInstance("PlayerController")` → ruft Lambda auf → gibt `INativeScript*` zurück

### 7.3 Warum dieses Pattern funktioniert

- **Automatisch**: Der Spieler muss nirgends eine Liste pflegen – das Makro erledigt alles
- **Typsicher**: Die Factory erzeugt den konkreten Typ, gibt aber `INativeScript*` zurück
- **DLL-kompatibel**: Statische Initialisierer laufen garantiert bei `LoadLibrary()`/`dlopen()`
- **Kein externes Tool nötig**: Kein Code-Generator, kein Preprocessing-Schritt

### 7.4 Klassenname → Entity-Zuordnung

```
Entity 42:  NativeScriptComponent { className = "PlayerController" }
            ↓
NativeScriptManager:
  m_instances[42] = Registry.createInstance("PlayerController")
  m_instances[42]->m_entity = 42
  m_instances[42]->onLoaded()
```

Die Zuordnung ist **deklarativ**: Der Entwickler (oder das Editor-UI) setzt nur den `className` String in der Komponente. Der `NativeScriptManager` erstellt die Instanz automatisch.

---

## 8. Python ↔ C++ Kommunikation

### 8.1 Kommunikationsrichtungen

```
┌───────────────────┐          ┌───────────────────┐
│   Python Script   │          │   C++ Script      │
│   (Movement.py)   │          │ (PlayerCtrl.cpp)  │
└───────┬───────────┘          └───────┬───────────┘
        │                              │
        │  Beide nutzen dieselbe       │
        │  Engine-API:                 │
        ▼                              ▼
┌─────────────────────────────────────────────────┐
│              Engine-Subsysteme                   │
│  (ECS, Physics, Audio, Input, Renderer)          │
└─────────────────────────────────────────────────┘
```

### 8.2 Indirekte Kommunikation (empfohlen)

Python und C++ Scripts kommunizieren **nicht direkt** miteinander, sondern über **gemeinsame Engine-Daten**:

| Mechanismus | Beispiel |
|------------|---------|
| **ECS-Komponenten** | C++ setzt Position → Python liest Position |
| **Event-System** (neu) | C++ feuert "PlayerDied" → Python reagiert |
| **Shared State** (Tags/Properties) | C++ setzt Tag "isAlerted" auf Entity → Python prüft Tag |
| **Name-basierte Entity-Suche** | Python: `engine.entity.find_by_name("Player")` → bekommt Entity-ID → kann Komponenten lesen/schreiben |

### 8.3 Direkter Aufruf (optional, später)

Für fortgeschrittene Szenarien kann ein explizites Message-System implementiert werden:

```cpp
// C++ Seite
void PlayerController::tick(float dt) {
    // Sendet eine Nachricht die Python-Scripts empfangen können
    GameplayAPI::sendMessage(getEntity(), "health_changed", healthValue);
}
```

```python
# Python Seite
def tick(entity, dt):
    msgs = engine.entity.get_messages(entity, "health_changed")
    for msg in msgs:
        update_health_bar(msg.value)
```

### 8.4 Python-Modul-Erweiterung für NativeScript-Info

Das bestehende `engine.entity`-Modul wird erweitert:

```python
# Neues Python-API:
engine.entity.get_native_script_class(entity)  # → "PlayerController" oder ""
engine.entity.has_native_script(entity)         # → True/False
```

---

## 9. Hot-Reloading im Editor

### 9.1 Übersicht

Hot-Reloading erlaubt dem Spieleentwickler, C++-Scripts im Editor zu ändern, zu kompilieren und die Änderungen **ohne Engine-Neustart** zu sehen.

### 9.2 Ablauf

```
┌─────────────────────────────────────────────────────────┐
│  1. Entwickler ändert PlayerController.cpp und speichert│
│                                                          │
│  2. FileWatcher erkennt Änderung in Content/NativeScripts│
│     (nutzt ReadDirectoryChangesW / inotify)              │
│                                                          │
│  3. NativeScriptHotReload::triggerRecompile()           │
│     a. CMake configure (falls nötig)                    │
│     b. CMake --build GameScripts                        │
│     c. → Erzeugt neue GameScripts.dll                   │
│                                                          │
│  4. NativeScriptManager::hotReload()                    │
│     a. Für alle aktiven Instanzen:                      │
│        - Serialisiere Runtime-State (optional)           │
│        - Rufe onDestroy() auf                           │
│        - Lösche Instanz                                 │
│     b. Registry.unregisterAll()                         │
│     c. FreeLibrary(alte DLL)                            │
│     d. Kopiere neue DLL → GameScripts_hot.dll (*)       │
│     e. LoadLibrary(GameScripts_hot.dll)                 │
│        → Statische Initialisierer registrieren Klassen  │
│     f. Für alle Entities mit NativeScriptComponent:     │
│        - Erstelle neue Instanz                          │
│        - Deserialisiere State (optional)                │
│        - Rufe onLoaded() auf                            │
│                                                          │
│  5. Toast: "C++ Scripts reloaded (3 classes, 0.8s)"     │
└─────────────────────────────────────────────────────────┘

(*) Die DLL wird kopiert, weil Windows die Original-Datei sperrt
    solange sie geladen ist. Die Kopie hat einen Zeitstempel-Suffix
    oder alternierenden Namen (GameScripts_0.dll / GameScripts_1.dll).
```

### 9.3 DLL-Kopier-Strategie (Windows)

Windows sperrt geladene DLLs – man kann sie nicht überschreiben. Lösung:

```
GameScripts.dll          ← CMake Build-Output (gesperrt während geladen)
GameScripts_live_0.dll   ← Kopie die geladen ist (wird bei Reload freigegeben)
GameScripts_live_1.dll   ← Nächste Kopie wird geladen
                           (alterniert: 0 → 1 → 0 → 1 ...)
```

### 9.4 State-Serialisierung (optional, Phase 2)

Für nahtloses Hot-Reload ohne State-Verlust:

```cpp
class INativeScript
{
public:
    // Optional überschreiben für Hot-Reload State-Persistence
    virtual void serialize(std::vector<uint8_t>& outData) {}
    virtual void deserialize(const std::vector<uint8_t>& inData) {}
};
```

Der `NativeScriptManager` ruft vor dem Unload `serialize()` und nach dem Reload `deserialize()` auf. So bleiben z.B. Gesundheitswerte, Timer, etc. erhalten.

### 9.5 Kompilierung im Editor

Die Kompilierung nutzt das bestehende Build-System:

```cpp
// NativeScriptHotReload.cpp
bool NativeScriptHotReload::recompile()
{
    // CMake und Toolchain sind bereits über BuildSystemUI erkannt
    const std::string cmakePath = m_buildSystemUI->getCMakePath();
    
    // Configure (nur beim ersten Mal oder nach CMakeLists.txt-Änderung)
    std::string configureCmd = cmakePath + " -S \"" + m_scriptsSourceDir + "\""
                             + " -B \"" + m_scriptsBuildDir + "\""
                             + " -DENGINE_SOURCE_DIR=\"" + m_engineSourceDir + "\"";
    
    // Build
    std::string buildCmd = cmakePath + " --build \"" + m_scriptsBuildDir + "\""
                         + " --config RelWithDebInfo";
    
    // ... Ausführung auf Background-Thread (wie BuildPipeline)
}
```

### 9.6 Integration mit bestehendem ScriptHotReload

```
┌────────────────────────────────────────────────────────────┐
│  EditorApp::tick(dt)                                       │
│    ├── Scripting::PollScriptHotReload()    [Python .py]    │
│    ├── NativeScriptHotReload::poll()       [C++ .cpp/.h]   │
│    └── Scripting::PollPluginHotReload()    [Python Plugins]│
└────────────────────────────────────────────────────────────┘
```

---

## 10. Build-Pipeline-Erweiterung

### 10.1 Änderungen an `BuildPipeline.cpp`

Die bestehende Build-Pipeline muss erweitert werden um die Gameplay-DLL zu kompilieren und ins Game-Build-Verzeichnis zu kopieren:

```
Bestehende Pipeline:
  Step 1: Prepare output directory
  Step 2: CMake configure (Engine Runtime)
  Step 3: CMake build (Engine Runtime)
  Step 4: Deploy runtime exe + DLLs
  Step 5: Cook assets
  Step 6: Copy config
  Step 7: Write game.ini
  Step 8: Launch (optional)

Erweiterte Pipeline:
  Step 1: Prepare output directory
  Step 2: CMake configure (Engine Runtime)
  Step 3: CMake build (Engine Runtime)
  Step 4: Deploy runtime exe + DLLs
  Step 5: Build GameScripts.dll (NEU)       ← CMake configure + build der Spieler-DLL
  Step 6: Deploy GameScripts.dll (NEU)      ← Kopie nach Engine/ oder Root
  Step 7: Cook assets
  Step 8: Copy config
  Step 9: Write game.ini
  Step 10: Launch (optional)
```

### 10.2 GameScripts im packaged Build

Im fertigen Spiel wird die `GameScripts.dll` neben der Exe (oder in `Engine/`) deployed:

```
GameBuild/
  MyGame.exe
  Engine/
    *.dll (Engine-DLLs)
    GameScripts.dll          ← Gameplay-Code des Spielers
    NativeScripting.dll      ← Engine NativeScripting Library
  Content/
    content.hpk
```

### 10.3 Runtime-Laden (kein Editor)

```cpp
// main.cpp – Runtime-Modus
if (isRuntimeMode)
{
    // GameScripts.dll liegt neben der Exe oder in Engine/
    std::string dllPath = (engineDir / "GameScripts.dll").string();
    if (std::filesystem::exists(dllPath))
    {
        NativeScriptManager::Instance().loadGameplayDLL(dllPath);
    }
    NativeScriptManager::Instance().initializeScripts();
}
```

---

## 11. Implementierungs-Reihenfolge (Phasen)

### Phase 1: Grundgerüst (NativeScripting-Modul)
**Aufwand: ~2-3 Tage**

1. `src/NativeScripting/` Ordner erstellen
2. `INativeScript.h` – Basisklasse
3. `NativeScriptComponent.h` – ECS-Komponente
4. `NativeScriptRegistry.h/cpp` – Registry mit Makro
5. `GameplayAPIExport.h` – Export-Macros
6. `CMakeLists.txt` für NativeScripting-Modul
7. Root `CMakeLists.txt` erweitern: `add_subdirectory(src/NativeScripting)`

### Phase 2: ECS-Integration
**Aufwand: ~1 Tag**

1. `Components.h`: `NativeScriptComponent` hinzufügen
2. `ECS.h`: `ComponentKind::NativeScript`, `MaxComponentTypes = 14`, `ComponentTraits`
3. `ECS.cpp`: SparseSet für `NativeScriptComponent`
4. Serialisierung: Level-Save/Load für `NativeScriptComponent`
5. Editor Properties Panel: NativeScript-Abschnitt im Inspector

### Phase 3: NativeScriptManager (DLL-Loading)
**Aufwand: ~2 Tage**

1. `NativeScriptManager.h/cpp` – Singleton
2. `loadGameplayDLL()` mit `LoadLibrary`/`dlopen`
3. `unloadGameplayDLL()` mit `FreeLibrary`/`dlclose`
4. `initializeScripts()` – Instanzen erstellen
5. `updateScripts()` – Tick-Dispatch
6. `shutdownScripts()` – Cleanup
7. `main.cpp` Integration: Init/Update/Shutdown Aufrufe

### Phase 4: GameplayAPI Implementation
**Aufwand: ~2 Tage**

1. `GameplayAPI.h` – Vollständige API-Deklaration
2. `GameplayAPI.cpp` – Implementation (delegiert an ECS/Physics/Audio/Input)
3. Funktions-Export sicherstellen (GAMEPLAY_API Macros)
4. Test: Minimales Script das Position setzt

### Phase 5: Spieler-Projekt-Template
**Aufwand: ~1 Tag**

1. Template `CMakeLists.txt` für GameScripts-Projekt
2. Template `ExampleScript.h/cpp` mit `REGISTER_NATIVE_SCRIPT`
3. Editor: "New C++ Script" im Content Browser Kontextmenü
4. Editor: "Build Scripts" Button / automatischer Build

### Phase 6: Hot-Reload im Editor
**Aufwand: ~3 Tage**

1. `NativeScriptHotReload.h/cpp` – FileWatcher für `.cpp/.h`
2. DLL-Kopier-Strategie (alternierende Namen)
3. Recompile-Trigger → Background-Thread CMake build
4. `NativeScriptManager::hotReload()` – Unload/Reload/Re-Init
5. Toast-Benachrichtigung nach Reload
6. Fehler-Handling: Compile-Fehler → Build-Output anzeigen

### Phase 7: Python ↔ C++ Brücke
**Aufwand: ~1 Tag**

1. `engine.entity.has_native_script()` / `get_native_script_class()` in Python
2. Gemeinsamer Event/Message-Bus (optional)
3. Lifecycle-Reihenfolge sicherstellen (C++ vor Python)

### Phase 8: Build-Pipeline-Erweiterung
**Aufwand: ~1-2 Tage**

1. `BuildPipeline.cpp`: Neue Steps für GameScripts-Build + Deploy
2. `NativeScripting.dll` ins `Engine/`-Verzeichnis kopieren
3. `GameScripts.dll` ins `Engine/`-Verzeichnis kopieren
4. Runtime-Loading in `main.cpp`

### Phase 9: Dokumentation & Polish
**Aufwand: ~1 Tag**

1. `engine.pyi` erweitern (Python IntelliSense)
2. `PROJECT_OVERVIEW.md` + `ENGINE_STATUS.md` aktualisieren
3. Spieler-seitiges README / Dokumentation
4. Beispiel-Projekt mit C++ + Python Scripts

---

## Zusammenfassung der Architektur

```
┌──────────────────────────────────────────────────────────────────────┐
│                           EDITOR                                      │
│  ┌─────────────┐  ┌──────────────┐  ┌────────────────────────────┐  │
│  │ Properties  │  │Content       │  │ NativeScriptHotReload      │  │
│  │ Panel:      │  │Browser:      │  │ (FileWatcher + Recompile   │  │
│  │ [ClassName] │  │ New C++ Script│ │  + DLL-Swap)               │  │
│  └──────┬──────┘  └──────────────┘  └─────────────┬──────────────┘  │
│         │                                          │                  │
├─────────┼──────────────────────────────────────────┼──────────────────┤
│         ▼              ENGINE CORE                  ▼                  │
│  ┌──────────────────┐  ┌─────────────────────────────────────────┐   │
│  │ ECS              │  │ NativeScriptManager (Singleton)         │   │
│  │ NativeScript     │  │  ├── loadGameplayDLL()                  │   │
│  │ Component        │──│  ├── initializeScripts()                │   │
│  │ {className,      │  │  ├── updateScripts(dt)                  │   │
│  │  instance*}      │  │  ├── shutdownScripts()                  │   │
│  └──────────────────┘  │  └── hotReload()                        │   │
│                        └───────────┬─────────────────────────────┘   │
│                                    │                                  │
│  ┌─────────────────────────────────┼──────────────────────────────┐  │
│  │ NativeScriptRegistry            │                              │  │
│  │  factories: { "PlayerCtrl" → λ, "EnemyAI" → λ, ... }         │  │
│  └─────────────────────────────────┼──────────────────────────────┘  │
│                                    │                                  │
├────────────────────────────────────┼──────────────────────────────────┤
│                                    ▼       GAMEPLAY DLL               │
│  ┌───────────────────────────────────────────────────────────────┐   │
│  │  GameScripts.dll (vom Spieleentwickler)                       │   │
│  │                                                               │   │
│  │  class PlayerController : public INativeScript {              │   │
│  │      void onLoaded() override { ... }                         │   │
│  │      void tick(float dt) override {                           │   │
│  │          float pos[3];                                        │   │
│  │          GameplayAPI::getPosition(getEntity(), pos);          │   │
│  │          if (GameplayAPI::isKeyDown(SDLK_W))                  │   │
│  │              pos[2] -= speed * dt;                             │   │
│  │          GameplayAPI::setPosition(getEntity(), pos);          │   │
│  │      }                                                        │   │
│  │  };                                                           │   │
│  │  REGISTER_NATIVE_SCRIPT(PlayerController)                     │   │
│  └───────────────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────────────┘
```

---

## Offene Fragen / Spätere Erweiterungen

| Thema | Status | Beschreibung |
|-------|--------|-------------|
| Serialisierbare Properties | Später | Script-Variablen im Editor anzeigen/editieren (z.B. `float speed = 5.0f`) |
| State-Persistence bei Hot-Reload | Phase 2 | `serialize()`/`deserialize()` für nahtloses Reload |
| Debugging | Später | Breakpoints in Gameplay-DLL (funktioniert automatisch wenn PDB vorhanden) |
| Multi-DLL | Später | Mehrere Gameplay-DLLs (Plugins von Drittanbietern) |
| Linux/macOS .so/.dylib | Phase 3+ | Gleiche Architektur, nur andere OS-API-Calls |
| C++ Script Templates | Phase 5 | Editor generiert Boilerplate-Code für neue Scripts |
| IntelliSense für GameplayAPI | Phase 5 | Automatische Header-Verteilung ins Spieler-Projekt |
