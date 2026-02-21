# Engine – Projektübersicht

> Umfassende Dokumentation der gesamten Engine-Architektur, aller Komponenten und des Zusammenspiels.
> Branch: `Json_and_ecs` | Build-System: CMake 3.12+ | Sprache: C++20 | Plattform: Windows (x64)

---

## Inhaltsverzeichnis

1. [Projektstruktur](#1-projektstruktur)
2. [Build-System (CMake)](#2-build-system-cmake)
3. [Externe Abhängigkeiten](#3-externe-abhängigkeiten)
4. [Einstiegspunkt – main.cpp](#4-einstiegspunkt--maincpp)
5. [Logger](#5-logger)
6. [Diagnostics Manager](#6-diagnostics-manager)
7. [Asset Manager](#7-asset-manager)
8. [Core-Modul](#8-core-modul)
   - 8.1 [MathTypes](#81-mathtypes)
   - 8.2 [EngineObject](#82-engineobject)
   - 8.3 [Asset (AssetData)](#83-asset-assetdata)
   - 8.4 [EngineLevel](#84-enginelevel)
   - 8.5 [ECS (Entity Component System)](#85-ecs-entity-component-system)
   - 8.6 [AudioManager](#86-audiomanager)
9. [Renderer](#9-renderer)
   - 9.1 [Renderer (abstrakt)](#91-renderer-abstrakt)
   - 9.2 [OpenGLRenderer](#92-openglrenderer)
   - 9.3 [Kamera](#93-kamera)
   - 9.4 [Shader-System](#94-shader-system)
   - 9.5 [Material-System](#95-material-system)
   - 9.6 [Texturen](#96-texturen)
   - 9.7 [2D-/3D-Objekte](#97-2d3d-objekte)
   - 9.8 [Text-Rendering](#98-text-rendering)
   - 9.9 [RenderResourceManager](#99-renderresourcemanager)
10. [UI-System](#10-ui-system)
    - 10.1 [UIManager](#101-uimanager)
    - 10.2 [Widget & WidgetElement](#102-widget--widgetelement)
    - 10.3 [UIWidgets (Einzelne Controls)](#103-uiwidgets-einzelne-controls)
11. [Scripting (Python)](#11-scripting-python)
    - 11.1 [Initialisierung](#111-initialisierung)
    - 11.2 [Script-API (engine-Modul)](#112-script-api-engine-modul)
    - 11.3 [engine.pyi (IntelliSense)](#113-enginepyi-intellisense)
12. [Main Loop im Detail](#12-main-loop-im-detail)
13. [Shutdown-Sequenz](#13-shutdown-sequenz)
14. [Architektur-Diagramm](#14-architektur-diagramm)

---

## 1. Projektstruktur

```
Engine/
├── CMakeLists.txt                  # Haupt-CMake (Projekt-Root)
├── external/                       # Drittanbieter-Bibliotheken
│   ├── SDL3/                       # Simple DirectMedia Layer 3
│   ├── freetype/                   # FreeType (Schriftart-Rendering)
│   └── openal-soft/                # OpenAL Soft (3D-Audio)
├── src/
│   ├── main.cpp                    # Einstiegspunkt der Anwendung
│   ├── AssetManager/               # Asset-Verwaltung, GC, JSON, stb_image
│   │   ├── AssetManager.h/.cpp
│   │   ├── AssetTypes.h
│   │   ├── GarbageCollector.h/.cpp
│   │   ├── json.hpp                # nlohmann/json (Header-only)
│   │   ├── stb_image.h             # stb_image (Header-only)
│   │   └── CMakeLists.txt
│   ├── Core/                       # Kern-Datentypen, ECS, Audio, Level
│   │   ├── Core.h                  # Sammel-Include
│   │   ├── MathTypes.h             # Vec2, Vec3, Vec4, Mat3, Mat4, Transform
│   │   ├── EngineObject.h/.cpp     # Basisklasse aller Engine-Objekte
│   │   ├── Asset.h/.cpp            # AssetData (Laufzeit-Repräsentation)
│   │   ├── EngineLevel.h/.cpp      # Level-Verwaltung + ECS-Snapshot
│   │   ├── AudioManager.h/.cpp     # OpenAL-basierter Audio-Manager
│   │   ├── ECS/
│   │   │   ├── ECS.h/.cpp          # ECSManager, Schema, Entity-Verwaltung
│   │   │   ├── Components.h        # Transform-, Mesh-, Material-, Light- etc.
│   │   │   └── DataStructs/
│   │   │       └── SparseSet.h     # Generisches SparseSet<T, Max>
│   │   └── CMakeLists.txt
│   ├── Diagnostics/                # Zustandsverwaltung, Config, PIE, Fenster
│   │   ├── DiagnosticsManager.h/.cpp
│   │   └── CMakeLists.txt
│   ├── Logger/                     # Datei- und Konsolen-Logging
│   │   ├── Logger.h/.cpp
│   │   └── CMakeLists.txt
│   ├── Renderer/                   # Rendering-Abstraktion + OpenGL-Impl.
│   │   ├── Renderer.h              # Abstrakte Renderer-Schnittstelle
│   │   ├── Camera.h                # Abstrakte Kamera-Schnittstelle
│   │   ├── Shader.h                # Abstrakte Shader-Schnittstelle
│   │   ├── ShaderProgram.h         # Abstrakte ShaderProgram-Schnittstelle
│   │   ├── Material.h              # CPU-seitige Material-Basisklasse
│   │   ├── Texture.h/.cpp          # CPU-seitige Textur-Daten
│   │   ├── RenderResourceManager.h/.cpp  # Caching, Level-Vorbereitung
│   │   ├── UIManager.h/.cpp        # Kompletter UI-Manager
│   │   ├── UIWidget.h/.cpp         # Widget + WidgetElement Datenmodell
│   │   ├── OpenGLRenderer/         # OpenGL-spezifische Implementierung
│   │   │   ├── OpenGLRenderer.h/.cpp
│   │   │   ├── OpenGLCamera.h/.cpp
│   │   │   ├── OpenGLShader.h/.cpp
│   │   │   ├── OpenGLShaderProgram.h/.cpp
│   │   │   ├── OpenGLMaterial.h/.cpp
│   │   │   ├── OpenGLTexture.h/.cpp
│   │   │   ├── OpenGLObject2D.h/.cpp
│   │   │   ├── OpenGLObject3D.h/.cpp
│   │   │   ├── OpenGLTextRenderer.h/.cpp
│   │   │   ├── glad/               # OpenGL-Loader (GLAD)
│   │   │   ├── glm/                # OpenGL Mathematics (Header-only)
│   │   │   └── shaders/            # GLSL-Shader-Dateien
│   │   │       ├── vertex.glsl / fragment.glsl       # 3D-Welt
│   │   │       ├── light_fragment.glsl               # Beleuchtung
│   │   │       ├── panel_vertex/fragment.glsl        # UI-Panels
│   │   │       ├── button_vertex/fragment.glsl       # UI-Buttons
│   │   │       ├── text_vertex/fragment.glsl         # Text
│   │   │       ├── ui_vertex/fragment.glsl           # UI-Bild/Textur
│   │   │       ├── progress_fragment.glsl            # Fortschrittsbalken
│   │   │       └── slider_fragment.glsl              # Schieberegler
│   │   ├── UIWidgets/              # Einzelne UI-Control-Klassen
│   │   │   ├── ButtonWidget.h/.cpp
│   │   │   ├── TextWidget.h/.cpp
│   │   │   ├── StackPanelWidget.h/.cpp
│   │   │   ├── GridWidget.h/.cpp
│   │   │   ├── ColorPickerWidget.h/.cpp
│   │   │   ├── EntryBarWidget.h
│   │   │   ├── SeparatorWidget.h/.cpp
│   │   │   ├── ProgressBarWidget.h/.cpp
│   │   │   ├── SliderWidget.h/.cpp
│   │   │   ├── CheckBoxWidget.h/.cpp
│   │   │   ├── DropDownWidget.h/.cpp
│   │   │   ├── TreeViewWidget.h/.cpp
│   │   │   └── TabViewWidget.h/.cpp
│   │   └── CMakeLists.txt
│   └── Scripting/                  # Eingebettetes Python-Scripting
│       ├── PythonScripting.h/.cpp
│       └── engine.pyi              # Python-Stubs für IntelliSense
└── PROJECT_OVERVIEW.md             # Diese Datei
```

---

## 2. Build-System (CMake)

### 2.1 Voraussetzungen
- **CMake** ≥ 3.12
- **C++20**-fähiger Compiler (getestet mit Visual Studio 18 2026 / MSVC)
- **Python 3** (Interpreter + Development-Libs) im System-PATH

### 2.2 Haupt-CMakeLists.txt
```
cmake_minimum_required(VERSION 3.12)
project("Engine" LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
```

- MSVC-Runtime: `MultiThreaded$<$<CONFIG:Debug>:Debug>DLL` (konsistente CRT über alle Targets)
- Plattform: `x64` erzwungen
- Debug/RelWithDebInfo: `/PROFILE`-Linker-Flag aktiv

### 2.3 Bibliotheks-Targets (alle als SHARED/DLL gebaut)

| Target          | Typ     | Abhängigkeiten                              |
|-----------------|---------|---------------------------------------------|
| `Logger`        | SHARED  | *(keine)*                                   |
| `Core`          | SHARED  | Logger, OpenAL::OpenAL, SDL3::SDL3          |
| `Diagnostics`   | SHARED  | Core, Logger                                |
| `AssetManager`  | SHARED  | Diagnostics, Logger, Core, SDL3::SDL3, assimp (static) |
| `Renderer`      | SHARED  | SDL3::SDL3, freetype (static), Logger, Core, AssetManager |
| `Scripting`     | SHARED  | SDL3::SDL3, Renderer, Logger, AssetManager, Diagnostics, Core, Python3 |
| `Engine` (exe)  | EXE     | SDL3, Renderer, Logger, AssetManager, Diagnostics, Core, Scripting, Python3 |

### 2.4 Build-Schritte
1. CMake konfigurieren: `cmake -B build -G "Visual Studio 18 2026" -A x64`
2. Bauen: `cmake --build build --config RelWithDebInfo`
3. Ausgabe: Alle DLLs + `Engine.exe` landen in `CMAKE_RUNTIME_OUTPUT_DIRECTORY` (kein Debug-Postfix)

### 2.5 Python-Debug-Workaround
- Im Debug-Modus wird die Release-Python-Lib als `_d.lib` kopiert (vermeidet Debug-Python-Abhängigkeit)
- `_DEBUG`-Macro wird vor `#include <Python.h>` deaktiviert und danach wiederhergestellt

---

## 3. Externe Abhängigkeiten

| Bibliothek       | Version/Quelle          | Verwendungszweck                        |
|------------------|-------------------------|-----------------------------------------|
| **SDL3**         | `external/SDL3/`        | Fenster, Input, OpenGL-Kontext, Audio-Init |
| **FreeType**     | `external/freetype/`    | TrueType-Schriftart-Rasterung           |
| **OpenAL Soft**  | `external/openal-soft/` | 3D-Audiowiedergabe (OpenAL-API)         |
| **GLAD**         | In-Tree (`glad/`)       | OpenGL-Funktionen laden                 |
| **GLM**          | In-Tree (`glm/`)        | Mathe-Bibliothek für OpenGL (glm::mat4 etc.) |
| **nlohmann/json**| In-Tree (`json.hpp`)    | JSON-Parsing für Assets und Config      |
| **stb_image**    | In-Tree (`stb_image.h`) | Bildformate laden (PNG, TGA, JPG, etc.) |
| **Python 3**     | System-Installation     | Eingebetteter Skript-Interpreter        |

---

## 4. Einstiegspunkt – main.cpp

### 4.1 Initialisierungsreihenfolge

```
1.  Logger::Instance().initialize()
2.  Scripting::Initialize()           → Python-Interpreter starten
3.  AssetManager::Instance().initialize()
4.  Projektverzeichnis bestimmen (Downloads/SampleProject)
5.  AssetManager::loadProject() oder createProject()
6.  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)
7.  AudioManager::Instance().initialize()   → OpenAL-Kontext
8.  DiagnosticsManager::loadConfig()        → Fenstergröße, Fenster-Zustand
9.  OpenGLRenderer erstellen + initialize() → SDL-Fenster + GL-Kontext
10. Scripting::SetRenderer(renderer)
11. Fenstergröße/Zustand aus Config anwenden
12. SDL_GL_SetSwapInterval(0)               → V-Sync deaktiviert
13. Windows: FreeConsole() + Stdout-Logging unterdrücken
14. UI-Click-Events registrieren (TitleBar.Close, TitleBar.Minimize, TitleBar.Maximize, Menu-Buttons, ViewportOverlay.PIE, Toolbar-Tools, TitleBar.Tab.Viewport)
15. Viewport-Tab erstellen (addTab "Viewport", nicht schließbar)
16. Editor-Widgets laden (TitleBar, ViewportOverlay/Toolbar, WorldSettings, WorldOutliner, EntityDetails, ContentBrowser)
17. Hauptschleife starten
```

### 4.2 Editor-Widgets (UI-Panels)
Beim Start werden sechs Editor-Widgets geladen:

| Widget-Name       | Asset-Datei              | Funktion                                    |
|-------------------|--------------------------|---------------------------------------------|
| TitleBar          | `TitleBar.asset`         | 100px: HorizonEngine-Titel links + Projektname mittig + Min/Max/Close rechts, Tab-Leiste unten (volle Breite) |
| ViewportOverlay   | `ViewportOverlay.asset`  | Toolbar: Select/Move/Rotate/Scale + Play/Stop (PIE) zentriert + Settings rechts |
| WorldSettings     | `WorldSettings.asset`    | Clear-Color-Picker (RGB-Einträge)           |
| WorldOutliner     | `WorldOutliner.asset`    | Entitäten-Liste des aktiven Levels          |
| EntityDetails     | `EntityDetails.asset`    | Komponenten-Details der ausgewählten Entität|
| ContentBrowser    | `ContentBrowser.asset`   | TreeView (Ordner-Hierarchie, Highlight) + Grid (Kacheln mit Icons), Doppelklick-Navigation, farbcodierte PNGs |

### 4.3 Editor-Tab-System
Die Engine unterstützt ein Tab-basiertes Editor-Layout:

- **Titelleiste** (obere Reihe der TitleBar, 50px): "HorizonEngine" links + Projektname mittig + Min/Max/Close rechts (Drag-Bereich)
- **Tab-Leiste** (untere Reihe der TitleBar, 50px): Dokument-/Level-Tabs horizontal von links nach rechts, volle Breite
- **Toolbar** (ViewportOverlay, 34px): PIE-Controls zentriert, Settings rechts (Select/Move/Rotate/Scale temporär entfernt)
- **Viewport-Tab**: Immer geöffnet (nicht schließbar), zeigt die 3D-Szene
- **Per-Tab-Framebuffer**: Jeder Tab besitzt einen eigenen FBO (Color-Texture + Depth-RBO)
- **Tab-Umschaltung**: Click-Events auf TitleBar.Tab.* Buttons wechseln den aktiven Tab
- **FBO-Blit**: Der aktive Tab wird vor dem UI-Overlay auf den Bildschirm geblittet
- **HZB/Pick/Outline**: Diese Render-Passes arbeiten innerhalb des Viewport-Tab-FBO

---

## 5. Logger

**Dateien:** `src/Logger/Logger.h`, `src/Logger/Logger.cpp`

### 5.1 Architektur
- **Singleton**: `Logger::Instance()`
- Thread-sicher via `std::mutex`
- Schreibt gleichzeitig in eine Datei und auf `stdout` (abschaltbar)
- Log-Retention: behält nur die letzten 5 Log-Dateien im `Logs/`-Ordner

### 5.2 Log-Level
| Level     | Bedeutung                       |
|-----------|---------------------------------|
| `INFO`    | Informative Meldung             |
| `WARNING` | Warnung, Engine läuft weiter    |
| `ERROR`   | Fehler, Teilfunktion betroffen  |
| `FATAL`   | Schwerer Fehler, Engine stoppt  |

### 5.3 Kategorien
`General`, `Engine`, `Scripting`, `AssetManagement`, `Diagnostics`, `Rendering`, `Input`, `Project`, `IO`, `UI`

### 5.4 Verwendung
```cpp
Logger::Instance().log(Logger::Category::Engine, "Nachricht", Logger::LogLevel::INFO);
```

### 5.5 Fehler-Tracking
- `hasErrors()` / `hasFatal()` / `hasErrorsOrFatal()` werden beim Shutdown abgefragt
- Bei Fehlern wird die Log-Datei automatisch im System-Editor geöffnet

---

## 6. Diagnostics Manager

**Dateien:** `src/Diagnostics/DiagnosticsManager.h`, `src/Diagnostics/DiagnosticsManager.cpp`

### 6.1 Übersicht
Zentrale Zustandsverwaltung der Engine (Singleton). Verwaltet:

- **Key-Value-States**: Beliebige Engine-Zustände (`setState` / `getState`)
- **Projekt-States**: Pro-Projekt-Einstellungen aus `defaults.ini`
- **RHI-Auswahl**: `OpenGL`, `DirectX11`, `DirectX12` (derzeit nur OpenGL aktiv)
- **Fenster-Konfiguration**: Größe (`Vec2`), Zustand (Normal/Maximized/Fullscreen)
- **PIE-Modus** (Play In Editor): `setPIEActive(bool)` / `isPIEActive()`
- **Aktives Level**: `setActiveLevel()` / `getActiveLevelSoft()`
- **Action-Tracking**: Asynchrone Aktionen (Loading, Saving, Importing, Building)
- **Input-Dispatch**: Globale KeyDown/KeyUp-Handler
- **Benachrichtigungen**: Modal- und Toast-Notifications (Queue-basiert)
- **Shutdown-Request**: `requestShutdown()` → sauberes Beenden

### 6.2 Config-Persistierung
- **Engine-Config**: `config/config.ini` (Key=Value-Format)
- **Projekt-Config**: `<Projekt>/Config/defaults.ini`

### 6.3 Projekt-Info
```cpp
struct ProjectInfo {
    std::string projectName;
    std::string projectVersion;
    std::string engineVersion;
    std::string projectPath;
    RHIType selectedRHI;
};
```

---

## 7. Asset Manager

**Dateien:** `src/AssetManager/AssetManager.h`, `src/AssetManager/AssetManager.cpp`

### 7.1 Übersicht
Zentrales Asset-Management (Singleton). Verwaltet das Laden, Speichern, Erstellen, Importieren und Entladen aller Asset-Typen.

### 7.2 Unterstützte Asset-Typen

| AssetType    | Beschreibung              |
|--------------|---------------------------|
| `Texture`    | Bilder (PNG, TGA, JPG)    |
| `Material`   | Materialien + Shader-Refs |
| `Model2D`    | 2D-Sprite-Modelle         |
| `Model3D`    | 3D-Mesh-Modelle (OBJ etc.)|
| `PointLight`  | Punkt-Lichtquellen        |
| `Audio`      | Audiodateien (WAV etc.)   |
| `Script`     | Python-Skripte            |
| `Shader`     | GLSL-Shader               |
| `Level`      | Level-Definitionen        |
| `Widget`     | UI-Widget-Definitionen    |

### 7.3 Lade-Modi
- **Sync** (`AssetManager::Sync`): Blockierend, sofortiges Ergebnis
- **Async** (`AssetManager::Async`): Worker-Thread, Ergebnis via `tryConsumeAssetLoadResult()`

### 7.4 Asset-Lebenszyklus
```
1. loadAsset(path, type, syncState)
   → ReadAssetHeader() → Typ-spezifische Load-Funktion
   → registerLoadedAsset() → Rückgabe: Asset-ID

2. getLoadedAssetByID(id) → std::shared_ptr<AssetData>

3. saveAsset(asset, syncState) → Typ-spezifische Save-Funktion

4. unloadAsset(id) → Entfernt aus m_loadedAssets

5. collectGarbage() → GC prüft weak_ptr-Tracking
```

### 7.5 Asset-Registry
- Beim Projektladen wird ein Asset-Register aufgebaut (`discoverAssetsAndBuildRegistry`)
- Index nach Pfad und Name (`m_registryByPath`, `m_registryByName`)
- Erlaubt schnelle Existenzprüfungen (`doesAssetExist`)

### 7.6 Garbage Collector
**Dateien:** `src/AssetManager/GarbageCollector.h/.cpp`
- Tracked `weak_ptr<EngineObject>` über `registerResource()`
- `collect()` entfernt abgelaufene Einträge
- Wird alle 60 Sekunden aus der Main-Loop aufgerufen

### 7.7 Worker-Thread
- Einzelner Background-Thread für asynchrone Operationen
- Job-Queue mit Mutex + Condition-Variable
- `enqueueJob(std::function<void()>)` → Worker führt aus

### 7.8 Projekt-Verwaltung
```cpp
loadProject(projectPath)    // Lädt Projekt + Registry + Config
saveProject(projectPath)    // Speichert Projektdaten
createProject(parentDir, name, info)  // Erstellt neues Projekt mit Default-Assets
```

### 7.9 Pfad-Auflösung
- `getAbsoluteContentPath(relative)` → `<Projekt>/Content/<relative>`
- `getEditorWidgetPath(relative)` → `<Engine>/Editor/Widgets/<relative>`

### 7.10 Bild-Laden (stb_image)
- `loadRawImageData(path, w, h, channels)` → Pixel-Daten
- `freeRawImageData(data)` → Speicher freigeben

---

## 8. Core-Modul

### 8.1 MathTypes
**Datei:** `src/Core/MathTypes.h`

Eigene, leichtgewichtige Mathe-Typen (unabhängig von GLM):

| Typ          | Felder                        | Beschreibung                           |
|--------------|-------------------------------|----------------------------------------|
| `Vec2`       | `float x, y`                 | 2D-Vektor                             |
| `Vec3`       | `float x, y, z`             | 3D-Vektor                             |
| `Vec4`       | `float x, y, z, w`          | 4D-Vektor / Farbe (RGBA)              |
| `Mat3`       | `float m[9]`                 | 3×3-Matrix                             |
| `Mat4`       | `float m[16]`               | 4×4-Matrix mit `transpose()`           |
| `Transform`  | Position, Rotation, Scale    | TRS-Transformation mit Matrix-Export   |

**Transform** bietet:
- `setPosition / setRotation / setScale`
- `getRotationMat3()` – Euler-Winkel (XYZ-Ordnung, Rz·Ry·Rx)
- `getMatrix4ColumnMajor()` – Vollständige TRS-Matrix (column-major)
- `getMatrix4RowMajor()` – Transponierte Variante

Alle Typen nutzen `NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT` für JSON-Serialisierung.

---

### 8.2 EngineObject
**Datei:** `src/Core/EngineObject.h`

Basisklasse für alle serialisierbaren Engine-Objekte:

```cpp
class EngineObject {
    std::string m_path;          // Dateipfad
    std::string m_name;          // Anzeigename
    AssetType m_type;            // Asset-Typ
    bool isSaved;                // Gespeichert-Flag
    Transform m_transform;       // Welt-Transformation
    virtual void render() {}     // Optionale Render-Methode
};
```

Abgeleitet von: `AssetData`, `Texture`, `Material`, `Widget`, `OpenGLObject2D`, `OpenGLObject3D`, `OpenGLTextRenderer`, `EngineLevel`

---

### 8.3 Asset (AssetData)
**Datei:** `src/Core/Asset.h`

Laufzeit-Repräsentation eines geladenen Assets:

```cpp
class AssetData : public EngineObject {
    unsigned int m_id;    // Eindeutige Asset-ID
    json m_data;          // Asset-Daten als JSON
    AssetType m_type;     // Typ-Enum
};
```

---

### 8.4 EngineLevel
**Datei:** `src/Core/EngineLevel.h/.cpp`

Repräsentiert ein Level/eine Szene:

#### Hauptfunktionen:
- **Objekt-Gruppen**: `createGroup()`, `addObjectToGroup()`, Instancing-Unterstützung
- **Welt-Objekte**: `registerObject()`, `getWorldObjects()`
- **ECS-Integration**: `prepareEcs()` – baut Entitäten aus Level-Daten
- **Script-Entities**: `buildScriptEntityCache()` – cached alle Entitäten mit Script-Komponente
- **Snapshot/Restore**: `snapshotEcsState()` / `restoreEcsSnapshot()` – für PIE-Modus
- **JSON-Serialisierung**: `setLevelData()` / `getLevelData()` / `serializeEcsEntities()`
- **Callback**: `registerEntityListChangedCallback()` – benachrichtigt UI bei Änderungen

#### PIE-Snapshot:
```cpp
struct EntitySnapshot {
    TransformComponent, MeshComponent, MaterialComponent,
    LightComponent, CameraComponent, PhysicsComponent,
    ScriptComponent, NameComponent, mask
};
// snapshotEcsState() → m_componentSnapshot
// restoreEcsSnapshot() → stellt Zustand wieder her
```

---

### 8.5 ECS (Entity Component System)
**Dateien:** `src/Core/ECS/ECS.h/.cpp`, `Components.h`, `DataStructs/SparseSet.h`

#### 8.5.1 Entity
- Einfache `unsigned int`-ID
- Max. 10.000 Entitäten (konstant: `MaxEntities = 10000`)

#### 8.5.2 Komponenten

| ComponentKind | Struct               | Felder                                          |
|---------------|----------------------|-------------------------------------------------|
| `Transform`   | `TransformComponent` | position[3], rotation[3] (Euler°), scale[3]     |
| `Mesh`        | `MeshComponent`      | meshAssetPath, meshAssetId                       |
| `Material`    | `MaterialComponent`  | materialAssetPath, materialAssetId               |
| `Light`       | `LightComponent`     | type (Point/Dir/Spot), color, intensity, range, spotAngle |
| `Camera`      | `CameraComponent`    | fov, nearClip, farClip                           |
| `Physics`     | `PhysicsComponent`   | colliderType (Box/Sphere/Mesh), isStatic, mass   |
| `Script`      | `ScriptComponent`    | scriptPath, scriptAssetId                        |
| `Name`        | `NameComponent`      | displayName                                      |

#### 8.5.3 SparseSet
Template-basierte Datenstruktur für effiziente Komponentenspeicherung:
```cpp
template<typename T, size_t MaxEntities>
struct SparseSet {
    insert(entity, value) / erase(entity) / contains(entity)
    get(entity) → T& / dense() / data() / size()
};
```
- O(1) Zugriff über Sparse-Array-Indirektion
- Swap-Remove für schnelles Löschen

#### 8.5.4 Schema
Bitmasken-basierte Abfrage über Komponentenkombinationen:
```cpp
Schema schema;
schema.require<TransformComponent>().require<MeshComponent>();
auto entities = ecs.getEntitiesMatchingSchema(schema);
```

#### 8.5.5 ECSManager (Singleton)
```cpp
Entity createEntity();
bool removeEntity(Entity);
bool addComponent<T>(entity, component);
T* getComponent<T>(entity);
bool hasComponent<T>(entity);
bool setComponent<T>(entity, component);
std::vector<Entity> getEntitiesMatchingSchema(schema);
std::vector<SchemaAssetMatch> getAssetsMatchingSchema(schema);
```

---

### 8.6 AudioManager
**Datei:** `src/Core/AudioManager.h/.cpp`

#### 8.6.1 Architektur
- **Singleton**: `AudioManager::Instance()`
- **Backend**: OpenAL Soft
- **Async-Loading**: Dedizierter Load-Thread für Audio-Daten

#### 8.6.2 Audio-Pipeline
```
1. createAudioHandle(assetId, loop, gain) → handle
2. playHandle(handle) → true/false
   ODER
   playAudioAsset(assetId, loop, gain) → handle (erstellt + spielt)
   ODER
   playAudioAssetAsync(assetId, loop, gain) → handle (asynchron)
```

#### 8.6.3 Hauptfunktionen
- `initialize()` → OpenAL-Device + Context öffnen
- `update()` → Abgeschlossene Async-Requests verarbeiten, Quellen aufräumen
- `stopAll()` → Alle Quellen stoppen (z.B. bei PIE-Stop)
- `shutdown()` → Alle Ressourcen freigeben, OpenAL-Context schließen

#### 8.6.4 Asset-Resolver
- Callback-basierte Asset-Auflösung (`setAudioResolver`)
- Entkoppelt AudioManager von AssetManager (keine direkte Abhängigkeit)

#### 8.6.5 Handle-System
- Interne Handles beginnen bei `0xF0000000` (Pending), `0xE0000000` (Internal Assets)
- Source-ID ↔ Asset-ID Mapping in `m_sourceAssetIds`
- Buffer-Caching: Ein Buffer pro Asset-ID

---

## 9. Renderer

### 9.1 Renderer (abstrakt)
**Datei:** `src/Renderer/Renderer.h`

Abstrakte Schnittstelle für jeden Rendering-Backend:

```cpp
class Renderer {
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void clear() = 0;
    virtual void render() = 0;
    virtual void present() = 0;
    virtual const std::string& name() const = 0;
    virtual SDL_Window* window() const = 0;

    // Kamera-Steuerung
    virtual void moveCamera(forward, right, up) = 0;
    virtual void rotateCamera(yawDelta, pitchDelta) = 0;
    virtual Vec3 getCameraPosition() const = 0;
    virtual void setCameraPosition(position) = 0;
    virtual Vec2 getCameraRotationDegrees() const = 0;
    virtual void setCameraRotationDegrees(yaw, pitch) = 0;
};
```

---

### 9.2 OpenGLRenderer
**Datei:** `src/Renderer/OpenGLRenderer/OpenGLRenderer.h/.cpp`

Einzige implementierte Backend-Klasse (erbt von `Renderer`).

#### 9.2.1 Initialisierung
1. SDL-Fenster erstellen (borderless, resizable, hidden) + Custom Window Hit-Test (Drag/Resize, Button-Bereich rechts ausgenommen)
2. Konsole schließen (FreeConsole), dann Fenster anzeigen (SDL_ShowWindow)
2. OpenGL-Kontext anlegen (GL 4.1 Core)
3. GLAD laden
4. Text-Renderer vorbereiten (FreeType)
5. GPU-Timer-Queries initialisieren

#### 9.2.2 Render-Pipeline
```
render()
  → Fenstergröße cachen (1x SDL_GetWindowSizeInPixels pro Frame)
  → Aktiven Tab-FBO sicherstellen (ensureTabFbo)
  → renderWorld()  (in aktiven Tab-FBO)
     → Level-Entities abfragen (ECS-Schema: Transform + Mesh)
     → Renderables erstellen (RenderResourceManager)
     → Lichter sammeln (Member-Vektor, keine Heap-Alloc)
     → Hierarchical Z-Buffer (HZB) Occlusion Culling (liest Tiefe aus Tab-FBO)
     → Sortierung + Batch-Rendering
     → Pick-Buffer + Selection-Outline nur bei Bedarf (On-Demand)
  → Aktiven Tab-FBO per glBlitFramebuffer auf Bildschirm (Hardware-Blit, kein Shader)
  → renderUI()
     → UIManager-Layouts aktualisieren
     → UI in FBO cachen (nur bei Änderungen neu zeichnen)
     → Panels, Buttons, Text, Images, Slider, ProgressBars zeichnen
     → Modal/Toast-Notifications
  → Text-Queue rendern (Metriken etc., nur wenn Queue nicht leer)

present()
  → SDL_GL_SwapWindow
```

#### 9.2.3 Performance-Metriken
- GPU-Frame-Time via GL Timer Queries (Triple-Buffered)
- CPU-Metriken für: Welt-Render, UI-Render, UI-Layout, UI-Draw, ECS
- Anzeige: F10 togglet Metrik-Overlay

#### 9.2.4 Occlusion Culling (HZB)
- Hierarchical Z-Buffer aus Depth-Buffer aufgebaut
- CPU-seitiger AABB-Test gegen Mip-Pyramid
- PBO-basierter asynchroner Readback (Double-Buffered)
- Statistiken: Visible / Hidden / Total (F9 togglet Anzeige)

#### 9.2.5 Entity-Picking
- Separater Pick-FBO mit Entity-ID als Farbe
- Mausklick → `requestPick(x, y)` → `pickEntityAt` → `m_selectedEntity`
- Selection-Outline via Edge-Detection-Shader auf Pick-Buffer
- **Pick-Buffer wird nur bei Bedarf gerendert** (wenn Pick angefragt oder Entity selektiert)

#### 9.2.6 Editor-Gizmos
- 3D-Gizmo-Overlay für die selektierte Entity (Translate/Rotate/Scale)
- Eigener GLSL-Shader (`m_gizmoProgram`) + dynamisches Line-VBO (`m_gizmoVao`/`m_gizmoVbo`)
- Gizmo-Größe skaliert mit Kamera-Entfernung (konstante Bildschirmgröße)
- Achsen-Picking: Screen-Space-Projektion, Nearest-Distance < 12px
- Drag-Handling: Pixel-Delta wird auf Screen-Space-Achsenrichtung projiziert → Welt-Einheiten / Grad / Skalierung
- Tastatur: W=Translate, E=Rotate, R=Scale (nur im Editor, nicht PIE)
- Achsen-Farben: Rot=X, Grün=Y, Blau=Z; aktive Achse gelb hervorgehoben

#### 9.2.7 UI-FBO-Caching
- UI wird in eigenes FBO gerendert
- Nur bei `isRenderDirty()` wird das FBO neu gezeichnet
- Sonst wird der Cache per `blitUiCache()` ins Backbuffer geblittet
- Content-Area-Berechnung nur wenn Text-Queue nicht leer

#### 9.2.7 Editor-Tab-Framebuffer
- Jeder Editor-Tab besitzt einen eigenen FBO (`EditorTab`-Struct: colorTex + depthRbo + snapshotTex)
- Nur der **aktive Tab** rendert World + UI (inaktive Tabs werden übersprungen)
- `renderWorld()` bindet den aktiven Tab-FBO → 3D-Szene wird in den Tab gerendert
- `buildHzb()` liest Depth aus dem aktiven Tab-FBO statt Default-Framebuffer
- `renderPickBuffer()` und `drawSelectionOutline()` arbeiten innerhalb des Tab-FBO
- `render()` blittet den aktiven Tab per **glBlitFramebuffer** (Hardware-Blit, kein Shader-Quad)
- Tab-Snapshot: Beim Wechsel wird das letzte Bild gecacht (kein Schwarzbild)
- Tab-Wechsel während PIE blockiert
- Tab-Verwaltung: `addTab()`, `removeTab()`, `setActiveTab()` mit FBO-Lifecycle

---

### 9.3 Kamera
**Dateien:** `src/Renderer/Camera.h` (abstrakt), `OpenGLCamera.h/.cpp` (Implementierung)

#### Abstrakte Schnittstelle:
```cpp
virtual void move(delta) = 0;           // Welt-Raum
virtual void moveRelative(fwd, right, up) = 0;  // Kamera-relativ
virtual void rotate(yawDeg, pitchDeg) = 0;
virtual Mat4 getViewMatrixColumnMajor() const = 0;
```

#### OpenGLCamera:
- FPS-Style Kamera (Yaw + Pitch)
- Start: Position (0, 0, 3), Blickrichtung -Z
- Pitch-Clamp: ±89°
- `updateVectors()` berechnet Front/Up aus Euler-Winkeln

---

### 9.4 Shader-System

#### Abstrakt:
- `Shader` – Einzelner Shader (Vertex, Fragment, Geometry, Compute, Hull, Domain)
- `ShaderProgram` – Linkt mehrere Shader zusammen

#### OpenGL-Implementierung:
- `OpenGLShader` – Kompiliert GLSL, hält `GLuint`-Handle
- `OpenGLShaderProgram` – Linkt Shader, bietet Uniform-Setter (float, int, vec3, vec4, mat4)

#### Vorhandene GLSL-Shader:

| Datei                     | Zweck                          |
|---------------------------|--------------------------------|
| `vertex.glsl`             | 3D-Welt Vertex-Shader          |
| `fragment.glsl`           | 3D-Welt Fragment-Shader        |
| `light_fragment.glsl`     | Beleuchtungs-Fragment-Shader   |
| `panel_vertex/fragment`   | UI-Panel-Rendering             |
| `button_vertex/fragment`  | UI-Button-Rendering            |
| `text_vertex/fragment`    | Schrift-Rendering              |
| `ui_vertex/fragment`      | UI-Textur/Bild-Rendering       |
| `progress_fragment`       | ProgressBar-Spezial-Fragment   |
| `slider_fragment`         | Slider-Spezial-Fragment        |

---

### 9.5 Material-System
**Dateien:** `src/Renderer/Material.h` (Basis), `OpenGLMaterial.h/.cpp`

#### Material (CPU-seitig):
- Hält Texturen (`std::vector<shared_ptr<Texture>>`)
- Textur-Pfade für Serialisierung
- Shininess-Wert

#### OpenGLMaterial:
- Hält Shader-Liste, Vertex-Daten, Index-Daten, Layout
- `build()` → Erstellt VAO, VBO, EBO, linkt Shader-Programm
- `bind()` → Setzt Uniformen (Model/View/Projection, Lights, Shininess) und bindet Texturen
- **Beleuchtung**: Bis zu 8 Lichtquellen (`kMaxLights = 8`)
  - Typen: Point (0), Directional (1), Spot (2)
  - Uniforms: Position, Direction, Color, Intensity, Range, Cutoff
- `renderBatchContinuation()` → Wiederholtes Draw ohne Re-Bind (Batching)

---

### 9.6 Texturen
**Dateien:** `src/Renderer/Texture.h/.cpp` (CPU), `OpenGLTexture.h/.cpp` (GPU)

- `Texture` – CPU-seitige Pixel-Daten (width, height, channels, data[])
- `OpenGLTexture` – GPU-Upload, `bind(unit)` / `unbind()`, Handle: `GLuint`

---

### 9.7 2D-/3D-Objekte

#### OpenGLObject2D
- Erstellt aus `AssetData` + Texturen
- `prepare()` → Material aufbauen
- `setMatrices()` + `render()`

#### OpenGLObject3D
- Erstellt aus `AssetData` (Mesh-Daten) + Texturen
- `prepare()` → Material + VAO/VBO aufbauen
- Lokale Bounding Box (`getLocalBoundsMin/Max`)
- Batch-Rendering: `renderBatchContinuation()`
- Statischer Cache: `ClearCache()`

---

### 9.8 Text-Rendering
**Datei:** `src/Renderer/OpenGLRenderer/OpenGLTextRenderer.h/.cpp`

- FreeType-basierte Glyph-Atlas-Generierung
- `initialize(fontPath, vertexShader, fragmentShader)` → Baut Atlas + Shader-Programm
- `drawText(text, screenPos, scale, color)` → Rendert Text am Bildschirm
- `measureText(text, scale)` → Gibt Textgröße zurück (für Layout)
- `getLineHeight(scale)` → Zeilenhöhe
- Shader-Cache: `getProgramForShaders()` cacht verknüpfte Programme

---

### 9.9 RenderResourceManager
**Datei:** `src/Renderer/RenderResourceManager.h/.cpp`

Verwaltet das Caching und die Erstellung von Render-Ressourcen:

- `prepareActiveLevel()` → Lädt alle Assets des aktiven Levels in GPU-Ressourcen
- `buildRenderablesForSchema(schema)` → Erstellt `RenderableAsset`-Liste für ein ECS-Schema
- `getOrCreateObject2D/3D()` → Cache-basierte Objekt-Erstellung
- `prepareTextRenderer()` → Lazy-Init des Text-Renderers
- `buildWidgetAsset(asset)` → Erstellt Widget aus Asset-Daten
- `clearCaches()` → Alle Caches leeren

Caches:
- `m_object2DCache` → `weak_ptr<OpenGLObject2D>` nach Asset-ID
- `m_object3DCache` → `weak_ptr<OpenGLObject3D>` nach Pfad-String
- `m_materialDataCache` → Textur + Shininess nach Pfad
- `m_widgetCache` → `weak_ptr<Widget>` nach Asset-ID

---

## 10. UI-System

### 10.1 UIManager
**Datei:** `src/Renderer/UIManager.h/.cpp`

Zentrale Verwaltung aller UI-Widgets:

#### Registrierung:
```cpp
registerWidget("TitleBar", widgetPtr);
unregisterWidget("TitleBar");
```

#### Z-Ordering:
- Widgets haben Z-Order (niedrig = hinten)
- `getWidgetsOrderedByZ()` → sortierte Liste (gecacht, dirty-flag)

#### Input-Handling:
```cpp
handleMouseDown(screenPos, button)  → Hit-Test → onClick/Focus
handleScroll(screenPos, delta)       → Scroll auf scrollable-Elementen
handleTextInput(text)                → Aktives Entry-Bar füllen
handleKeyDown(key)                   → Backspace/Enter für Entry-Bars
setMousePosition(screenPos)          → Hover-States aktualisieren
isPointerOverUI(screenPos)           → Prüft ob Maus über UI
```

#### Layout:
```cpp
updateLayouts(measureTextFn)  → Berechnet Positionen/Größen aller Elemente
needsLayoutUpdate()           → Prüft dirty-Flag
```

- **Dock-Reihenfolge:** Top → Bottom → Left → Right → Other
- Side-Panels (Left/Right) werden in der Höhe auf den verbleibenden Platz (`available.h`) begrenzt, sodass sie nicht hinter ContentBrowser/Footer ragen
- Asset-Validierung stellt sicher, dass gecachte Widget-Dateien `m_fillY` enthalten; fehlende Eigenschaft löst Neugenerierung aus

#### Click-Events:
```cpp
registerClickEvent("TitleBar.Close", []() { ... });
// Elemente mit clickEvent-Feld rufen registrierte Callbacks auf
```

#### Notifications:
- **Modal**: `showModalMessage(message, onClosed)` – blockierendes Popup
- **Toast**: `showToastMessage(message, duration)` – temporäre Meldung
- Toast-Stack-Layout: Automatisches Stapeln bei mehreren Toasts

#### World-Outliner-Integration:
```cpp
refreshWorldOutliner()          → Aktualisiert Entitäten-Liste
selectEntity(entityId)          → Wählt Entität aus, zeigt Details
```

---

### 10.2 Widget & WidgetElement
**Datei:** `src/Renderer/UIWidget.h/.cpp`

#### Widget (erbt von EngineObject):
```cpp
class Widget : public EngineObject {
    Vec2 m_sizePixels, m_positionPixels;
    WidgetAnchor m_anchor;        // TopLeft, TopRight, BottomLeft, BottomRight
    bool m_fillX, m_fillY;       // Streckt sich über verfügbaren Platz
    int m_zOrder;
    std::vector<WidgetElement> m_elements;  // Element-Baum

    bool loadFromJson(const json& data);
    json toJson() const;
};
```

#### WidgetElement (Element-Baum-Knoten):
```cpp
struct WidgetElement {
    WidgetElementType type;       // Text, Button, Panel, StackPanel, Grid, etc.
    std::string id;               // Eindeutige Element-ID
    Vec2 from, to;                // Relative Position (0..1)
    Vec4 color, hoverColor;       // Farben
    std::string text, font;       // Text-Inhalt + Schriftart
    float fontSize;
    Vec2 minSize, padding, margin;
    bool fillX, fillY, sizeToContent;
    StackOrientation orientation;
    std::string imagePath, clickEvent;
    unsigned int textureId;
    std::vector<WidgetElement> children;  // Kind-Elemente

    // Berechnete Layout-Werte:
    Vec2 computedSizePixels, computedPositionPixels;
    Vec2 boundsMinPixels, boundsMaxPixels;

    // Interaktions-States:
    bool isHovered, isPressed, isFocused;
    bool scrollable;
    float scrollOffset;

    // Callbacks:
    std::function<void()> onClicked;
    std::function<void(const Vec4&)> onColorChanged;
    std::function<void(const std::string&)> onValueChanged;
};
```

#### Element-Typen:

| WidgetElementType | Beschreibung                          |
|-------------------|---------------------------------------|
| `Text`            | Statischer Text                       |
| `Button`          | Klickbarer Button mit Hover-State     |
| `Panel`           | Farbiger Hintergrund-Bereich          |
| `StackPanel`      | Automatisches Layout (H/V-Stapelung)  |
| `Grid`            | Raster-Layout                         |
| `ColorPicker`     | Farbauswahl-Widget                    |
| `EntryBar`        | Text-Eingabefeld                      |
| `ProgressBar`     | Fortschrittsanzeige                   |
| `Slider`          | Schieberegler mit Min/Max             |
| `Image`           | Bild/Textur-Anzeige                   |

---

### 10.3 UIWidgets (Einzelne Controls)
**Verzeichnis:** `src/Renderer/UIWidgets/`

Jedes Widget ist als eigene Klasse implementiert (gemäß Projekt-Richtlinien):

| Klasse               | Datei                    | Beschreibung                               |
|----------------------|--------------------------|---------------------------------------------|
| `ButtonWidget`       | `ButtonWidget.h/.cpp`    | State-Machine (Normal/Hovered/Pressed/Disabled), Child-TextWidget |
| `TextWidget`         | `TextWidget.h/.cpp`      | Text, Schriftart, Farbe, Schriftgröße      |
| `StackPanelWidget`   | `StackPanelWidget.h/.cpp`| Horizontale/Vertikale Kind-Anordnung       |
| `GridWidget`         | `GridWidget.h/.cpp`      | Raster-Layout mit Padding                  |
| `ColorPickerWidget`  | `ColorPickerWidget.h/.cpp`| Farbauswahl, `onColorChanged`-Callback    |
| `EntryBarWidget`     | `EntryBarWidget.h`       | Text-Eingabe, Passwort-Modus, Validierung  |
| `SeparatorWidget`    | `SeparatorWidget.h/.cpp` | Aufklappbarer Abschnitt mit flachem Sektions-Header (▾/▸ Chevron, Trennlinie, indentierter Inhalt) |
| `ProgressBarWidget`  | `ProgressBarWidget.h/.cpp`| Wertebalken mit Min/Max und Farben        |
| `SliderWidget`       | `SliderWidget.h/.cpp`    | Schieberegler, `onValueChanged`-Callback   |
| `CheckBoxWidget`     | `CheckBoxWidget.h/.cpp`  | Boolean-Toggle mit Label, `onCheckedChanged`-Callback |
| `DropDownWidget`     | `DropDownWidget.h/.cpp`  | Auswahlliste mit Expand/Collapse, `onSelectionChanged`-Callback |
| `TreeViewWidget`     | `TreeViewWidget.h/.cpp`  | Hierarchische Baumansicht mit aufklappbaren Knoten |
| `TabViewWidget`      | `TabViewWidget.h/.cpp`   | Tab-Leiste mit umschaltbaren Inhaltsbereichen, `onTabChanged`-Callback |

---

## 11. Scripting (Python)

### 11.1 Initialisierung
**Dateien:** `src/Scripting/PythonScripting.h/.cpp`

```cpp
Scripting::Initialize()     // Python-Interpreter starten, engine-Modul registrieren
Scripting::SetRenderer(r)   // Renderer-Pointer für Kamera-API setzen
Scripting::Shutdown()       // Python-Interpreter herunterfahren
```

- Nutzt CPython-API direkt (kein pybind11)
- `_DEBUG` wird vor `Python.h`-Include deaktiviert (vermeidet Debug-Python-Lib)
- Engine-Modul (`engine`) wird als eingebettetes C-Modul registriert

### 11.2 Script-API (engine-Modul)

Das `engine`-Modul wird Python-Skripten automatisch zur Verfügung gestellt und bietet:

#### engine.entity
| Funktion                | Beschreibung                          |
|-------------------------|---------------------------------------|
| `create_entity()`       | Erstellt neue ECS-Entität             |
| `attach_component(e, k)` | Fügt Komponente nach Kind hinzu     |
| `detach_component(e, k)` | Entfernt Komponente                 |
| `get_entities(kinds)`   | Findet Entitäten mit bestimmten Komponenten |
| `get_transform(e)`      | Gibt (pos, rot, scale) als Tupel zurück |
| `set_position(e, x,y,z)` | Setzt Position                     |
| `translate(e, dx,dy,dz)` | Bewegt relativ                     |
| `set_rotation(e, p,y,r)` | Setzt Rotation                     |
| `rotate(e, dp,dy,dr)`   | Rotiert relativ                      |
| `set_scale(e, sx,sy,sz)` | Setzt Skalierung                   |
| `set_mesh(e, path)`     | Setzt Mesh-Asset                      |
| `get_mesh(e)`           | Gibt Mesh-Pfad zurück                 |

#### engine.assetmanagement
| Funktion                    | Beschreibung                          |
|-----------------------------|---------------------------------------|
| `is_asset_loaded(path)`     | Prüft ob Asset geladen                |
| `load_asset(path, type)`    | Lädt synchron                         |
| `load_asset_async(path, type, cb)` | Lädt asynchron mit Callback    |
| `save_asset(id, type)`      | Speichert Asset                       |
| `unload_asset(id)`          | Entlädt Asset                         |

#### engine.audio
| Funktion                        | Beschreibung                        |
|---------------------------------|-------------------------------------|
| `create_audio(path, loop, gain)` | Audio-Handle aus Content-Pfad     |
| `play_audio(path, loop, gain)`   | Erstellt und spielt sofort        |
| `play_audio_handle(handle)`      | Spielt vorhandenes Handle         |
| `set_audio_volume(handle, gain)` | Lautstärke setzen                 |
| `pause_audio(handle)`            | Pausiert                          |
| `stop_audio(handle)`             | Stoppt                            |
| `is_audio_playing(handle)`       | Prüft Wiedergabe                  |
| `invalidate_audio_handle(handle)`| Handle ungültig machen            |

#### engine.input
| Funktion                          | Beschreibung                      |
|-----------------------------------|-----------------------------------|
| `set_on_key_pressed(callback)`    | Globaler KeyDown-Callback         |
| `set_on_key_released(callback)`   | Globaler KeyUp-Callback           |
| `register_key_pressed(key, cb)`   | KeyDown für bestimmte Taste       |
| `register_key_released(key, cb)`  | KeyUp für bestimmte Taste         |
| `is_shift_pressed()`              | Shift-Status                      |
| `is_ctrl_pressed()`               | Ctrl-Status                       |
| `is_alt_pressed()`                | Alt-Status                        |
| `get_key(name)`                   | Key-Code aus Name auflösen        |

#### engine.ui
| Funktion                           | Beschreibung                    |
|------------------------------------|---------------------------------|
| `show_modal_message(msg, cb)`      | Modales Popup anzeigen          |
| `close_modal_message()`            | Modal schließen                 |
| `show_toast_message(msg, dur)`     | Toast-Nachricht anzeigen        |

#### engine.camera
| Funktion                       | Beschreibung                       |
|--------------------------------|------------------------------------|
| `get_camera_position()`        | Kamera-Position (x,y,z)           |
| `set_camera_position(x,y,z)`  | Kamera-Position setzen             |
| `get_camera_rotation()`       | Kamera-Rotation (yaw, pitch)       |
| `set_camera_rotation(yaw, p)` | Kamera-Rotation setzen             |

#### engine.diagnostics
| Funktion                    | Beschreibung                          |
|-----------------------------|---------------------------------------|
| `get_delta_time()`          | Letzte Frame-Deltazeit                |
| `get_state(key)`            | Engine-State abfragen                 |
| `set_state(key, value)`     | Engine-State setzen                   |

#### engine.logging
| Funktion                    | Beschreibung                          |
|-----------------------------|---------------------------------------|
| `log(message, level)`       | Log-Nachricht (0=Info, 1=Warn, 2=Error) |

#### Konstanten
```python
Component_Transform, Component_Mesh, Component_Material, ...
Asset_Texture, Asset_Material, Asset_Model3D, ...
Log_Info, Log_Warning, Log_Error
input.Keys  # dict mit allen SDL-Key-Constants
```

### 11.3 Script-Lebenszyklus (PIE-Modus)

```
1. PIE starten:
   → Level.snapshotEcsState()
   → DiagnosticsManager.setPIEActive(true)

2. Pro Frame (Scripting::UpdateScripts):
   → Level-Script laden + on_level_loaded() aufrufen (einmalig)
   → Für jede Script-Entity:
     → Script laden
     → onloaded(entity) aufrufen (einmalig pro Entity)
     → tick(entity, dt) aufrufen (jeden Frame)
   → Async-Asset-Load-Callbacks verarbeiten

3. PIE stoppen:
   → DiagnosticsManager.setPIEActive(false)
   → AudioManager.stopAll()
   → Scripting::ReloadScripts() (Script-States zurücksetzen)
   → Level.restoreEcsSnapshot()
```

### 11.4 engine.pyi (IntelliSense)
**Datei:** `src/Scripting/engine.pyi`

Python-Stub-Datei für IDE-Unterstützung. Muss bei API-Änderungen synchron gehalten werden (siehe Projektrichtlinien).

---

## 12. Main Loop im Detail

```
while (running) {
    // ═══ Timing ═══
    Berechne dt (Delta-Zeit) aus Performance-Counter
    Aktualisiere FPS-Zähler (1-Sekunden-Intervall)
    Prüfe Metriken-Update-Intervall (0.25s)

    // ═══ Audio ═══
    audioManager.update()    // Verarbeite fertige Audio-Loads

    // ═══ Garbage Collection ═══ (alle 60 Sekunden)
    assetManager.collectGarbage()

    // ═══ Input (Kamera) ═══
    WASD → moveCamera (relativ, kamera-vorwärts)
    Q/E  → moveCamera (hoch/runter)
    Maus-Position → UIManager.setMousePosition

    // ═══ Event-Verarbeitung ═══
    SDL_PollEvent:
    - QUIT → running = false
    - MOUSE_MOTION → UI-Hover + Kamera-Rotation (bei Rechtsklick)
    - MOUSE_BUTTON_DOWN (Links) → UI-Hit-Test oder Entity-Picking
    - MOUSE_BUTTON_DOWN (Rechts) → Kamera-Steuerung aktivieren
    - MOUSE_BUTTON_UP (Rechts) → Kamera-Steuerung deaktivieren
    - MOUSE_WHEEL → UI-Scroll oder Kamera-Geschwindigkeit ändern
    - TEXT_INPUT → UI-Texteingabe
    - KEY_UP:
        F8  → Bounds-Debug toggle
        F9  → Occlusion-Stats toggle
        F10 → Metriken toggle
        F11 → UI-Debug toggle
        F12 → FPS-Cap toggle
        ESC → PIE stoppen
        Sonst → DiagnosticsManager + Scripting
    - KEY_DOWN → UI-Keyboard + DiagnosticsManager + Scripting

    // ═══ Shutdown-Check ═══
    if (diagnostics.isShutdownRequested()) running = false

    // ═══ Scripting ═══ (nur bei PIE aktiv)
    Scripting::UpdateScripts(dt)

    // ═══ UI-Updates ═══
    uiManager.updateNotifications(dt)

    // ═══ Metriken ═══ (bei Bedarf aktualisiert)
    FPS-Text, Speed-Text, CPU/GPU/UI/Input/Render/GC/ECS-Metriken

    // ═══ Rendering ═══
    renderer->clear()
    renderer->render()   // Welt + UI
    renderer->present()  // SwapBuffers

    // ═══ FPS-Cap ═══ (wenn aktiviert: ~60 FPS via SDL_Delay)
}
```

### Debug-Tasten

| Taste | Funktion                              |
|-------|---------------------------------------|
| F8    | Bounding-Box-Debug toggle             |
| F9    | Occlusion-Statistiken toggle          |
| F10   | Performance-Metriken toggle           |
| F11   | UI-Debug (Bounds-Rahmen) toggle       |
| F12   | FPS-Cap (60 FPS) toggle               |
| ESC   | PIE stoppen (wenn aktiv)              |

### Kamera-Steuerung

| Eingabe           | Aktion                                |
|-------------------|---------------------------------------|
| W/A/S/D           | Kamera vorwärts/links/rückwärts/rechts |
| Q/E               | Kamera runter/hoch                    |
| Rechte Maustaste  | Kamera-Rotation aktivieren            |
| Mausrad (+ RMB)   | Kamera-Geschwindigkeit ändern (0.5x–5.0x) |

---

## 13. Shutdown-Sequenz

```
1. Main-Loop verlassen (running = false)
2. Warten auf laufende Aktionen (diagnostics.isActionInProgress)
3. Fenster-Größe/Zustand in Config speichern
4. diagnostics.saveProjectConfig() + saveConfig()
5. audioManager.shutdown()          → OpenAL aufräumen
6. renderer->shutdown()             → OpenGL-Ressourcen freigeben
7. delete renderer
8. SDL_Quit()
9. Falls Fehler: Log-Datei im Editor öffnen
10. Scripting::Shutdown()           → Python-Interpreter beenden
```

---

## 14. Architektur-Diagramm

```
┌──────────────────────────────────────────────────────┐
│                      main.cpp                        │
│              (Initialisierung + Main Loop)            │
└────────┬──────────┬──────────┬───────────┬───────────┘
         │          │          │           │
    ┌────▼───┐  ┌───▼────┐ ┌──▼──────┐ ┌──▼──────────┐
    │ Logger │  │Scripting│ │  Audio  │ │ Diagnostics  │
    │        │  │(Python) │ │ Manager │ │   Manager    │
    └────────┘  └───┬────┘ └──┬──────┘ └──┬───────────┘
                    │         │            │
              ┌─────▼─────────▼────────────▼──────┐
              │          Asset Manager             │
              │  (Laden, Speichern, GC, Registry)  │
              └─────────────────┬──────────────────┘
                                │
              ┌─────────────────▼──────────────────┐
              │              Core                   │
              │  ┌──────────┐  ┌───────────────┐   │
              │  │MathTypes │  │ EngineObject   │   │
              │  │Vec/Mat/  │  │  EngineLevel   │   │
              │  │Transform │  │  AssetData     │   │
              │  └──────────┘  └───────────────┘   │
              │  ┌──────────────────────────────┐   │
              │  │           ECS                │   │
              │  │  SparseSet, Schema, Manager  │   │
              │  │  Components (8 Typen)        │   │
              │  └──────────────────────────────┘   │
              └─────────────────┬──────────────────┘
                                │
              ┌─────────────────▼──────────────────┐
              │           Renderer                  │
              │  ┌────────────────────────────┐    │
              │  │      OpenGLRenderer        │    │
              │  │  Kamera, Shader, Material  │    │
              │  │  2D/3D-Objekte, Text       │    │
              │  │  HZB Occlusion, Picking    │    │
              │  │  GPU Timer Queries         │    │
              │  └────────────────────────────┘    │
              │  ┌────────────────────────────┐    │
              │  │       UI-System            │    │
              │  │  UIManager, Widget, Elems  │    │
              │  │  9 Widget-Typen            │    │
              │  │  FBO-Caching, Notifications│    │
              │  └────────────────────────────┘    │
              │  ┌────────────────────────────┐    │
              │  │  RenderResourceManager     │    │
              │  │  Caching + Level-Prep      │    │
              │  └────────────────────────────┘    │
              └────────────────────────────────────┘

Externe Bibliotheken:
  SDL3, FreeType, OpenAL Soft, GLAD, GLM, nlohmann/json, stb_image, Python 3
```

---

*Generiert aus dem Quellcode des Engine-Projekts. Stand: aktueller Branch `Json_and_ecs`.*
