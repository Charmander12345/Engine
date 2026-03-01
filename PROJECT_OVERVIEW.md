# Engine – Projektübersicht

> Umfassende Dokumentation der gesamten Engine-Architektur, aller Komponenten und des Zusammenspiels.
> Branch: `Json_and_ecs` | Build-System: CMake 3.12+ | Sprache: C++20 | Plattform: Windows (x64)

---

## Aktuelle Änderung (Viewport)

- `OpenGLRenderer`: Das Compositing der Szenen-FBO in `render()` blitted jetzt gezielt nur den `ViewportContentRect` statt immer die komplette Fläche. Dadurch wird die 3D-Ansicht nur im verfügbaren Viewport-Bereich (abzüglich UI-Docks) dargestellt.
- `OpenGLRenderer`: In `renderWorld()` wird nach den Shadow-Pässen der Viewport jetzt wieder inklusive Content-Rect-Offset gesetzt (nicht mehr auf `(0,0)`), damit die Welt nicht in die linke untere Ecke rutscht.
- `ViewportUI`: Start der Umsetzung mit neuer Klasse `ViewportUIManager` (Dateien `src/Renderer/ViewportUIManager.h/.cpp`) und erster Integration in `OpenGLRenderer` (Übergabe des `ViewportContentRect` pro Frame).
- `ViewportUI`: Nächster Schritt umgesetzt: `renderViewportUI()` rendert jetzt das Root-Widget viewport-lokal in den aktiven Viewport-Tab-FBO (mit `glViewport`/`glScissor`-Clipping) vor dem finalen Blit.
- `ViewportUI`: Input-Routing im Main-Loop erweitert: Nach Editor-UI werden jetzt Viewport-UI-Ereignisse (`MouseDown/Up`, Scroll, Text, KeyDown) verarbeitet; `isOverUI` berücksichtigt Editor-UI **oder** Viewport-UI.
- `Scripting/UI`: `engine.ui` unterstützt jetzt dynamisches Spawnen/Ersetzen und Entfernen von Widgets zur Laufzeit über `spawn_widget(widget_id, asset_path, tab_id="")` und `remove_widget(widget_id)`.
- `Widget Editor`: Content-Browser-Integration erweitert – neuer Kontextmenüpunkt **New Widget** erzeugt ein Widget-Asset (Typ `AssetType::Widget`) im Projekt-Content und öffnet es direkt in einem eigenen Editor-Tab.
- `Widget Editor`: Doppelklick auf Widget-Assets im Content Browser öffnet jetzt einen dedizierten Widget-Editor-Tab und lädt das Widget zur Bearbeitung/Vorschau.
- `Widget Editor`: Tab-Workspace ausgebaut – linker Dock-Bereich zeigt verfügbare Steuerelemente + Element-Hierarchie, rechter Dock-Bereich zeigt Asset/Widget-Details, die Mitte enthält eine dedizierte Preview-Canvas mit Fill-Color-Rahmen.
- `Widget Editor`: Für Widget-Editor-Tabs wird im Renderer die 3D-Weltpass-Ausgabe unterdrückt; der tab-eigene Framebuffer wird als reine Widget-Workspace-Fläche genutzt.
- `Widget Editor`: TitleBar-Tabs werden jetzt beim Hinzufügen/Entfernen zentral neu aufgebaut, damit neue Widget-Editor-Tabs immer sichtbar sind wie beim Mesh Viewer.
- `Build/CMake`: Multi-Config-Ausgabeverzeichnisse werden nun pro Konfiguration getrennt (`${CMAKE_BINARY_DIR}/Debug|Release|...`) statt zusammengeführt, um Debug/Release-Lib-Kollisionen (MSVC `LNK2038`) zu vermeiden.

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
    - 11.3 [Script-Lebenszyklus (PIE-Modus)](#113-script-lebenszyklus-pie-modus)
    - 11.4 [engine.pyi (IntelliSense)](#114-enginepyi-intellisense)
12. [Main Loop im Detail](#12-main-loop-im-detail)
13. [Shutdown-Sequenz](#13-shutdown-sequenz)
14. [Architektur-Diagramm](#14-architektur-diagramm)
15. [Physik-System](#15-physik-system)
16. [Landscape-System](#16-landscape-system)

---

## 1. Projektstruktur

```
Engine/
├── CMakeLists.txt                  # Haupt-CMake (Projekt-Root)
├── external/                       # Drittanbieter-Bibliotheken
│   ├── SDL3/                       # Simple DirectMedia Layer 3
│   ├── freetype/                   # FreeType (Schriftart-Rendering)
│   ├── glm/                        # GLM Mathematics (Header-only, backend-agnostisch)
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
│   ├── Landscape/                  # Landscape-/Terrain-Verwaltung
│   │   ├── LandscapeManager.h/.cpp
│   │   └── CMakeLists.txt
│   ├── Logger/                     # Datei- und Konsolen-Logging
│   │   ├── Logger.h/.cpp
│   │   └── CMakeLists.txt
│   ├── Physics/                    # Physik-Simulation (Jolt Physics)
│   │   ├── PhysicsWorld.h/.cpp
│   │   └── CMakeLists.txt
│   ├── Renderer/                   # Rendering-Abstraktion + OpenGL-Impl.
│   │   ├── Renderer.h              # Abstrakte Renderer-Schnittstelle (~130 Zeilen, ~60 virtuelle Methoden)
│   │   ├── RendererCapabilities.h  # Backend-Fähigkeiten (Shadows, Occlusion, etc.)
│   │   ├── Camera.h                # Abstrakte Kamera-Schnittstelle
│   │   ├── Shader.h                # Abstrakte Shader-Schnittstelle
│   │   ├── ShaderProgram.h         # Abstrakte ShaderProgram-Schnittstelle
│   │   ├── IRenderObject2D.h       # Abstrakte 2D-Render-Objekt-Schnittstelle
│   │   ├── IRenderObject3D.h       # Abstrakte 3D-Render-Objekt-Schnittstelle
│   │   ├── ITextRenderer.h         # Abstrakte Text-Renderer-Schnittstelle
│   │   ├── IShaderProgram.h        # Abstrakte Shader-Programm-Schnittstelle
│   │   ├── ITexture.h              # Abstrakte GPU-Textur-Schnittstelle
│   │   ├── Material.h              # CPU-seitige Material-Basisklasse
│   │   ├── Texture.h/.cpp          # CPU-seitige Textur-Daten
│   │   ├── RenderResourceManager.h/.cpp  # Caching, Level-Vorbereitung
│   │   ├── UIManager.h/.cpp        # Kompletter UI-Manager (nutzt nur Renderer*, kein OpenGL)
│   │   ├── UIWidget.h/.cpp         # Widget + WidgetElement Datenmodell
│   │   ├── IRenderContext.h         # Abstrakte Render-Context-Schnittstelle
│   │   ├── IRenderTarget.h          # Abstrakte Render-Target-Schnittstelle (FBO-Abstraktion für Editor-Tabs)
│   │   ├── SplashWindow.h          # Abstrakte Splash-Fenster-Basisklasse (6 reine virtuelle Methoden)
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
│   │   │   ├── OpenGLRenderContext.h    # OpenGL-Implementierung von IRenderContext
│   │   │   ├── OpenGLRenderTarget.h/.cpp # OpenGL-FBO-Implementierung von IRenderTarget (Editor-Tab-FBOs)
│   │   │   ├── OpenGLSplashWindow.h/.cpp # OpenGL-Implementierung des Splash-Fensters (Shader, VAOs, FreeType-Atlas)
│   │   │   ├── glad/               # OpenGL-Loader (GLAD)
│   │   │   ├── shaders/            # GLSL-Shader-Dateien
│   │   │       ├── vertex.glsl / fragment.glsl       # 3D-Welt (Beleuchtung, Texturen)
│   │   │       ├── grid_fragment.glsl                # Prozedurales Grid-Material (Multi-Light, Schatten, Blinn-Phong)
│   │   │       ├── light_fragment.glsl               # Beleuchtung
│   │   │       ├── panel_vertex/fragment.glsl        # UI-Panels
│   │   │       ├── button_vertex/fragment.glsl       # UI-Buttons
│   │   │       ├── text_vertex/fragment.glsl         # Text
│   │   │       ├── ui_vertex/fragment.glsl           # UI-Bild/Textur
│   │   │       ├── progress_fragment.glsl            # Fortschrittsbalken
│   │   │       └── slider_fragment.glsl              # Schieberegler
│   │   │   └── CMakeLists.txt       # Renderer Target (OpenGL-Backend, wird zu Renderer.dll)
│   │   ├── UIWidgets/
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
│   │   ├── EditorWindows/           # Editor-Fenster (FBO-Override, 3D-Vorschau)
│   │   │   ├── MeshViewerWindow.h/.cpp  # Mesh-Viewer: Orbit-Kamera, dedizierter FBO
│   │   │   └── PopupWindow.h/.cpp       # Multi-Window Popup-System (backend-agnostisch, nutzt IRenderContext)
│   │   └── CMakeLists.txt          # RendererCore OBJECT-Lib (abstrakte Schicht, eingebettet in Renderer.dll)
├── RENDERER_ABSTRACTION_PLAN.md     # Detaillierter Plan zur Backend-Abstrahierung des Renderers
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
| `Renderer`      | SHARED  | RendererCore (OBJECT) + SDL3::SDL3, freetype (static), Logger, Core, AssetManager. Backend über `RendererFactory` + `config.ini` wählbar |
| `Scripting`     | SHARED  | SDL3::SDL3, Renderer, Logger, AssetManager, Diagnostics, Core, Physics, Python3 |
| `Physics`       | STATIC  | Core, Logger                                |
| `Engine` (exe)  | EXE     | SDL3, Renderer, Logger, AssetManager, Diagnostics, Core, Scripting, Physics, Python3 |

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
| **GLM**          | `external/glm/`         | Backend-agnostische Mathe-Bibliothek (glm::mat4 etc.) |
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
4.  DiagnosticsManager::loadConfig()  → Fenstergröße, Fenster-Zustand, bekannte Projekte
5.  Projekt-Auswahl:
    a) DefaultProject aus config.ini → direkt laden
    b) Kein Default → Projekt-Auswahl-Screen anzeigen (Recent/Open/New)
    c) Fallback → Downloads/SampleProject erstellen
6.  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)
7.  AudioManager::Instance().initialize()   → OpenAL-Kontext
8.  DiagnosticsManager::loadConfig()        → Fenstergröße, Fenster-Zustand
9.  RendererFactory::createRenderer(activeBackend) → Backend aus config.ini (RHI=OpenGL|Vulkan|DirectX12)
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

| Widget-Name       | Asset-Datei              | Tab-Scope  | Funktion                                    |
|-------------------|--------------------------|------------|---------------------------------------------|
| TitleBar          | `TitleBar.asset`         | Global     | 100px: HorizonEngine-Titel links + Projektname mittig + Min/Max/Close rechts, Tab-Leiste unten (volle Breite) |
| ViewportOverlay   | `ViewportOverlay.asset`  | Viewport   | Toolbar: Select/Move/Rotate/Scale + Play/Stop (PIE) zentriert + Settings rechts |
| WorldSettings     | `WorldSettings.asset`    | Viewport   | Clear-Color-Picker (RGB-Einträge)           |
| WorldOutliner     | `WorldOutliner.asset`    | Viewport   | Entitäten-Liste des aktiven Levels          |
| EntityDetails     | `EntityDetails.asset`    | Viewport   | Komponenten-Details der ausgewählten Entität mit editierbaren Steuerelementen: Vec3-Eingabefelder (X/Y/Z farbcodiert) für Transform, EntryBars für Floats, CheckBoxen für Booleans, DropDowns für Enums, ColorPicker für Farben, Drag-and-Drop + Dropdown-Auswahl für Mesh/Material/Script-Assets, "+ Add Component"-Dropdown, Remove-Button (X) pro Komponente mit Bestätigungsdialog |
| ContentBrowser    | `ContentBrowser.asset`   | Viewport   | TreeView (Ordner-Hierarchie + Shaders-Root, Highlight) + Grid (Kacheln mit Icons, Selektion + Delete + Rename + F2-Shortcut), Doppelklick-Navigation (Ordner öffnen, Model3D → Mesh-Viewer-Tab), farbcodierte PNGs, Rechtsklick-Kontextmenü (New Script/Level/Material) |
| StatusBar         | `StatusBar.asset`        | Global     | Undo/Redo + Dirty-Zähler + Save All        |

### 4.3 Editor-Tab-System
Die Engine unterstützt ein Tab-basiertes Editor-Layout:

- **Titelleiste** (obere Reihe der TitleBar, 50px): "HorizonEngine" links + Projektname mittig + Min/Max/Close rechts (Drag-Bereich)
- **Tab-Leiste** (untere Reihe der TitleBar, 50px): Dokument-/Level-Tabs horizontal von links nach rechts, volle Breite
- **Toolbar** (ViewportOverlay, 34px): PIE-Controls zentriert, Settings rechts (Select/Move/Rotate/Scale temporär entfernt)
- **Viewport-Tab**: Immer geöffnet (nicht schließbar), zeigt die 3D-Szene
- **Mesh-Viewer-Tabs**: Schließbare Tabs für 3D-Mesh-Vorschau mit Split-View (Viewport + Properties), geöffnet per Doppelklick auf Model3D im Content Browser. Jeder Tab besitzt ein eigenes Runtime-EngineLevel.
- **Per-Tab-Framebuffer**: Jeder Tab besitzt einen eigenen FBO (Color-Texture + Depth-RBO)
- **Tab-Umschaltung**: Click-Events auf TitleBar.Tab.* Buttons wechseln den aktiven Tab. `setActiveTab()` tauscht das aktive Level per `swapActiveLevel()` aus (Editor-Level ↔ Mesh-Viewer-Runtime-Level) und speichert/restauriert Kamera-State.
- **Tab-Scoped UI**: Widgets können einem Tab zugeordnet werden (`registerWidget(id, widget, tabId)`). Viewport-Widgets (ViewportOverlay, WorldSettings, WorldOutliner, EntityDetails, ContentBrowser) sind zum Tab "Viewport" zugeordnet, Mesh-Viewer-Properties-Panels zum jeweiligen Asset-Tab. Globale Widgets (TitleBar, StatusBar) bleiben immer sichtbar.
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
- **Aktives Level**: `setActiveLevel()` / `getActiveLevelSoft()` / `swapActiveLevel()` (atomarer Austausch via `unique_ptr`, gibt altes Level zurück, setzt Dirty-Callback, feuert `activeLevelChangedCallbacks`)
- **Level-Changed-Callbacks**: Token-basierte Registrierung via `registerActiveLevelChangedCallback()` → gibt `size_t`-Token zurück. `unregisterActiveLevelChangedCallback(token)` entfernt den Callback. Intern als `unordered_map<size_t, Callback>` gespeichert, damit kurzlebige Subscriber (z. B. temporärer UIManager) ihren Callback sicher abmelden können.
- **Action-Tracking**: Asynchrone Aktionen (Loading, Saving, Importing, Building)
- **Input-Dispatch**: Globale KeyDown/KeyUp-Handler
- **Benachrichtigungen**: Modal- und Toast-Notifications (Queue-basiert)
- **Shutdown-Request**: `requestShutdown()` → sauberes Beenden; `resetShutdownRequest()` → setzt Flag zurück (wird vor Main-Loop aufgerufen um verwaiste Flags aus der Startup-Phase zu entfernen)
- **Entity-Dirty-Queue**: `invalidateEntity(entityId)` → markiert einzelne Entitäten für Render-Refresh. `consumeDirtyEntities()` liefert und leert die Queue (thread-safe via `m_mutex`). `hasDirtyEntities()` prüft ob Dirty-Entitäten vorhanden sind. Wird von `renderWorld()` pro Frame konsumiert.

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
   → O(1)-Prüfung via m_loadedAssetsByPath-Index
   → readAssetFromDisk() (thread-safe, kein shared state)
   → finalizeAssetLoad() (Registration + GC)
   → Rückgabe: Asset-ID

2. loadBatchParallel(requests)
   → Dedupliziert gegen m_loadedAssetsByPath
   → std::async pro Asset: readAssetFromDisk() parallel
   → Sequentielle Finalisierung auf Caller-Thread
   → Rückgabe: map<path, assetId>

3. getLoadedAssetByID(id) → std::shared_ptr<AssetData>

4. saveAsset(asset, syncState) → Typ-spezifische Save-Funktion

5. unloadAsset(id) → Entfernt aus m_loadedAssets + m_loadedAssetsByPath

6. collectGarbage() → GC prüft weak_ptr-Tracking
```

### 7.5 Paralleles Laden (Architektur)
Die Asset-Pipeline ist in drei Phasen aufgeteilt:
1. **Phase 1+2: Disk I/O + CPU** (`readAssetFromDisk`, statisch, thread-safe): Datei lesen, JSON parsen, stbi_load für Texturen, WAV-Decode für Audio. Kein Zugriff auf shared state.
2. **Phase 3a: Finalisierung** (`finalizeAssetLoad`): AssetData erstellen, `registerLoadedAsset()` + GC-Registration unter Mutex.
3. **Phase 3b: GPU-Upload** (`OpenGLObject3D::prepare()`, `OpenGLMaterial::build()`): Shader-Kompilierung, VBO/VAO-Erstellung — muss auf GL-Thread.

**Thread-Pool:** `m_workerPool` wird beim `initialize()` mit `std::thread::hardware_concurrency()` Threads (min. 2) gestartet. Alle teilen sich eine globale `m_jobs`-Queue (Mutex + CV). `loadBatchParallel()` nutzt einen atomaren Batch-Counter (`m_batchPending`) + `m_batchCv` um auf Abschluss aller Jobs zu warten.

`buildRenderablesForSchema()` nutzt intern eine Collect-then-Batch-Strategie:
- Pass 1: Alle Mesh- + Material-Pfade aus ECS-Matches sammeln → `loadBatchParallel()`
- Pass 2: Textur-Pfade aus geladenen Materialien extrahieren → `loadBatchParallel()`
- Pass 3: Renderables bauen — alle `loadAsset()`-Aufrufe treffen den Cache (O(1), kein Disk-I/O)

### 7.6 Performance-Optimierungen
- **O(1)-Asset-Lookup**: `m_loadedAssetsByPath` (Hash-Map path→ID) eliminiert lineare Scans in `loadAsset()`, `isAssetLoaded()` und `loadAssetAsync()`.
- **Shader-Pfad-Cache**: `ResolveShaderPath()` in `OpenGLObject3D` cached aufgelöste Dateipfade statisch, vermeidet wiederholte `std::filesystem::exists()`-Aufrufe.
- **Material-Daten-Cache**: `RenderResourceManager::m_materialDataCache` cached geparste Material-Daten (Texturen, Shininess, Shader) nach Pfad.
- **Deduplizierte Model-Matrix-Berechnung**: Render- und Mesh-Entries nutzen eine gemeinsame Lambda-Funktion für die Matrixberechnung.
- **O(n²)-Registry-Schreibung eliminiert**: `m_suppressRegistrySave`-Flag unterdrückt wiederholtes `saveRegistry()` während `discoverAssetsAndBuildRegistry()`; Registry wird einmalig am Ende geschrieben.
- **engine.pyi statisch deployed**: Die statische `src/Scripting/engine.pyi` wird per CMake post-build nach `Content/Scripting/engine.pyi` im Deploy-Verzeichnis kopiert. Beim Laden/Erstellen eines Projekts wird die Datei von dort per `fs::copy_file` in das Projekt-Verzeichnis (`Content/Scripts/engine.pyi`) kopiert — keine Laufzeit-Generierung mehr.
- **Single-Open bei Asset-Discovery**: `readAssetHeader()` wird in `discoverAssetsAndBuildRegistry()` nur einmal pro Datei aufgerufen (statt doppeltem Open).

### 7.7 Asset-Registry
- Beim Projektladen wird ein Asset-Register aufgebaut (`discoverAssetsAndBuildRegistry`)
- Index nach Pfad und Name (`m_registryByPath`, `m_registryByName`)
- Erlaubt schnelle Existenzprüfungen (`doesAssetExist`)
- **Rename**: `renameAsset(relPath, newName)` benennt die Datei auf Disk um, aktualisiert die Registry (Name + Pfad + Index-Maps), alle geladenen AssetData-Objekte, ECS-Komponenten-Referenzen (Mesh/Material/Script-Pfade) und scannt Cross-Asset-Referenzen in .asset-Dateien. Die zugehörige Source-Datei (z.B. Textur-Original) wird ebenfalls umbenannt. Die Registry wird nach dem Rename persistiert.
- **Move**: `moveAsset(oldRelPath, newRelPath)` verschiebt Assets analog (Datei + Registry + ECS + .asset-Referenzen).

### 7.8 Garbage Collector
**Dateien:** `src/AssetManager/GarbageCollector.h/.cpp`
- Tracked `weak_ptr<EngineObject>` über `registerResource()`
- `collect()` entfernt abgelaufene Einträge
- Wird alle 60 Sekunden aus der Main-Loop aufgerufen

### 7.9 Asset-Integrität
- **`validateRegistry()`**: Prüft alle Registry-Einträge gegen das Dateisystem. Entfernt Einträge, deren Dateien nicht mehr auf Disk existieren (z.B. extern gelöscht). Rebuild der Index-Maps + Persist + Version-Bump. Wird automatisch nach `discoverAssetsAndBuildRegistryAsync()` aufgerufen.
- **`validateEntityReferences(showToast)`**: Prüft ECS-Entity-Referenzen (MeshComponent, MaterialComponent, ScriptComponent) gegen die Registry. Loggt Warnungen für fehlende Assets. Wird nach `prepareEcs()` in `RenderResourceManager::prepareActiveLevel()` aufgerufen, d.h. bei jedem Level-Laden.
- **`repairEntityReferences()`**: Repariert ungültige Asset-Referenzen vor dem Rendering:
  - **Fehlende Meshes**: Entfernt die `MeshComponent` von der Entity, damit der Renderer sie überspringt.
  - **Fehlende Materialien**: Ersetzt den Material-Pfad durch `Materials/WorldGrid.asset` (das WorldGrid-Fallback-Material).
  - Wird in `prepareActiveLevel()` vor `validateEntityReferences()` aufgerufen, sodass der RRM nur gültige Referenzen vorbereitet.
- Alle Methoden sind öffentlich und können jederzeit manuell aufgerufen werden (z.B. aus GC-Zyklus oder UI-Aktion).

### 7.10 Thread-Pool
- Thread-Pool mit `hardware_concurrency()` Threads (min. 2)
- Globale Job-Queue mit Mutex + Condition-Variable
- `enqueueJob(std::function<void()>)` → nächster freier Thread führt aus
- Batch-Wait via `m_batchPending` (atomic) + `m_batchCv`

### 7.10 Projekt-Verwaltung
```cpp
loadProject(projectPath, syncState, ensureDefaultContent=true)    // Lädt Projekt + Registry + Config; optional ohne Default-Content-Erzeugung
saveProject(projectPath)                                        // Speichert Projektdaten
createProject(parentDir, name, info, syncState, includeDefaultContent=true)  // Erstellt neues Projekt; optional nur Blank-Level
```

### 7.11 Pfad-Auflösung
- `getAbsoluteContentPath(relative)` → `<Projekt>/Content/<relative>`
- `getEditorWidgetPath(relative)` → `<Engine>/Editor/Widgets/<relative>`

### 7.12 Bild-Laden (stb_image)
- `loadRawImageData(path, w, h, channels)` → Pixel-Daten
- `freeRawImageData(data)` → Speicher freigeben

### 7.13 Asset-Import (`importAssetFromPath`)
- **Import-Dialog**: `OpenImportDialog()` öffnet SDL-Dateidialog, leitet an `importAssetFromPath()` weiter.
- **Textur-Import**: Kopiert Bilddatei nach `Content/`, erstellt `.asset` mit `m_sourcePath`.
- **Audio-Import**: Kopiert WAV nach `Content/`, erstellt `.asset` mit `m_sourcePath`.
- **3D-Modell-Import (Assimp)**:
  - Lädt OBJ, FBX, glTF, GLB, DAE, etc. via Assimp.
  - **Import-Logging**: Loggt beim Import die Scene-Zusammenfassung (Anzahl Meshes, Materials, eingebettete Texturen, Animationen), pro Mesh Details (Vertices, Faces, UV-Kanäle, Normals, Tangents, Material-Index) und pro Material die Textur-Anzahl nach Typ (Diffuse, Specular, Normal, Height, Ambient, Emissive).
  - Sammelt alle Meshes in einen Vertex/Index-Buffer (pos3 + uv2).
  - **Material-Extraktion**: Iteriert `scene->mMaterials` und erstellt für jedes Material ein `.asset` unter `Content/Materials/` mit:
    - `m_textureAssetPaths`: Pfade zu extrahierten Textur-Assets.
    - `m_diffuseColor`, `m_specularColor`, `m_shininess`: Material-Eigenschaften aus Assimp.
  - **Textur-Extraktion**: Für jedes Material werden Diffuse-, Specular- und Normal-Texturen extrahiert:
    - **Externe Texturen**: Datei wird nach `Content/Textures/` kopiert, `.asset` mit `m_sourcePath` erstellt.
    - **Eingebettete Texturen**: Komprimierte Daten (PNG/JPG) oder Raw-Pixel (TGA) werden nach `Content/Textures/` geschrieben.
  - **Benennungskonvention**: Materialien und Texturen werden nach dem Mesh-Asset benannt:
    - Material: `MeshName` (bei einem Material) oder `MeshName_Material_N` (bei mehreren).
    - Texturen: `MeshName_Diffuse`, `MeshName_Specular`, `MeshName_Normal`.
  - `m_materialAssetPaths` im Mesh-Asset speichert Referenzen auf erstellte Material-Assets.
  - **Auto-Material bei Entity-Spawn**: Beim Hinzufügen eines Meshes zur Szene (Drag-Drop auf Viewport, Outliner-Entity, EntityDetails-Dropdown) wird automatisch die erste referenzierte MaterialComponent hinzugefügt, falls das Mesh-Asset `m_materialAssetPaths` enthält.
  - **Entity-Spawn Undo/Redo**: Beim Drag-and-Drop eines Model3D-Assets auf den Viewport wird nach dem Spawn eine Undo/Redo-Action erstellt. Undo entfernt die gespawnte Entity aus Level und ECS.
- **Shader-Import**: Kopiert `.glsl`-Datei nach `Content/`.
- **Script-Import**: Kopiert `.py`-Datei nach `Content/`.

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
- **ECS-Reset**: `resetPreparedState()` – setzt `m_ecsPrepared = false`, erzwingt erneuten `prepareEcs()`-Aufruf beim nächsten Render (wird bei Level-Swap zwischen Editor und Mesh-Viewer-Tabs benötigt)
- **JSON-Serialisierung**: `setLevelData()` / `getLevelData()` / `serializeEcsEntities()`
- **Callback**: `registerEntityListChangedCallback()` – benachrichtigt UI bei Änderungen

#### PIE-Snapshot:
```cpp
struct EntitySnapshot {
    TransformComponent, MeshComponent, MaterialComponent,
    LightComponent, CameraComponent, PhysicsComponent,
    ScriptComponent, NameComponent, CollisionComponent, mask
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
| `Collision`   | `CollisionComponent` | ColliderType enum (Box/Sphere/Capsule/Cylinder/Mesh), colliderSize[3] (Half-Extents/Radius/HalfHeight), colliderOffset[3], restitution, friction, isSensor (Trigger-Volume) |
| `Physics`     | `PhysicsComponent`   | MotionType enum (Static/Kinematic/Dynamic), mass, gravityFactor (float, 1.0=normal), linearDamping, angularDamping, maxLinearVelocity (500), maxAngularVelocity (47.12), MotionQuality enum (Discrete/LinearCast CCD), allowSleeping, velocity[3], angularVelocity[3] |
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
uint64_t getComponentVersion(); // Globaler Zähler, inkrementiert bei jeder Komponentenänderung (add/set/remove)
```

#### 8.5.6 Dirty-Flagging bei Komponentenänderungen
- **Mechanismus**: `ECSManager::m_componentVersion` wird bei jedem `addComponent`, `setComponent` und `removeComponent` inkrementiert.
- **UIManager-Polling**: `UIManager::updateNotifications()` vergleicht den aktuellen Versionszähler mit dem zuletzt gesehenen Wert (`m_lastEcsComponentVersion`). Bei Änderung werden `populateOutlinerDetails()` und `refreshWorldOutliner()` automatisch aufgerufen.

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
**Datei:** `src/Renderer/Renderer.h`, `src/Renderer/RendererCapabilities.h`

Abstrakte Schnittstelle für jeden Rendering-Backend (~130 Zeilen, ~60 virtuelle Methoden):

**Pure-virtual (Core):** initialize, shutdown, clear, render, present, name, window, Camera-Steuerung, getUIManager, screenToWorldPos

**Virtual mit Default-Implementierung (optional):**
- Rendering-Toggles: Shadows, VSync, Wireframe, Occlusion Culling
- Debug-Visualisierungen: UI Debug, Bounds Debug, HeightField Debug
- Entity Picking: pickEntityAt, requestPick, getSelectedEntity, setSelectedEntity
- Gizmo: GizmoMode/GizmoAxis Enums, beginGizmoDrag, updateGizmoDrag, endGizmoDrag
- Editor Tabs: addTab, removeTab, setActiveTab, getActiveTabId
- Popup Windows: openPopupWindow, closePopupWindow, getPopupWindow, routeEventToPopup
- Mesh Viewer: openMeshViewer, closeMeshViewer, getMeshViewer
- Viewport/Visuals: getViewportSize, setClearColor, getClearColor, setSkyboxPath, getSkyboxPath, queueText, createWidgetFromAsset, preloadUITexture
- Scene Management: refreshEntity
- Capabilities: getCapabilities → RendererCapabilities
- Performance Metrics: GPU/CPU Timing, Occlusion-Statistiken

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
- **Entity-Löschen (DELETE-Taste)**: Erstellt einen vollständigen Snapshot aller 10 Komponentenarten (`std::make_optional`) vor der Löschung. Eine Undo/Redo-Action wird gepusht: Undo erstellt die Entity mit derselben ID (`ecs.createEntity(entity)`) und stellt alle gesicherten Komponenten wieder her.
- **Entity-Spawn Undo/Redo**: Beim Drag-and-Drop-Spawn eines Model3D-Assets auf den Viewport wird eine Undo/Redo-Action erzeugt. Undo entfernt die Entity via `level->onEntityRemoved()` + `ecs.removeEntity()`.

#### 9.2.6 Per-Entity Render Refresh
- `refreshEntity(entity)` → Sucht die Entität in `m_renderEntries` / `m_meshEntries`, baut GPU-Daten per `refreshEntityRenderable()` neu auf und tauscht In-Place aus
- Handhabt Listen-Migration: Mesh+Material ↔ Mesh-Only (wenn Material hinzugefügt/entfernt wird)
- Fügt neue Entitäten in die passende Liste ein, entfernt Entitäten die kein Mesh mehr haben
- `renderWorld()` konsumiert pro Frame die Dirty-Entity-Queue (`DiagnosticsManager::consumeDirtyEntities()`) und ruft `refreshEntity()` für jede auf
- Voller Scene-Rebuild (`setScenePrepared(false)`) wird nur noch bei Levelwechsel oder Asset-Verschiebung ausgelöst

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

#### 9.2.8 Renderer-Performance-Optimierungen
- **Cached Active Tab**: `m_cachedActiveTab` vermeidet wiederholte lineare Suche über die Tab-Liste pro Frame.
- **Projection Guard**: Projektionsmatrix wird nur bei tatsächlicher Größenänderung neu berechnet (`m_lastProjectionWidth`/`m_lastProjectionHeight`).
- **Toter Code entfernt**: `isRenderEntryRelevant()`-Lambda (immer `true`) aus der Render-Pipeline entfernt.

#### 9.2.9 Viewport-Content-Rect-basierte Projektion
- **Problem**: Bei Fenstergrößenänderung wurde das 3D-Bild verzerrt, da die Projektion die gesamte Fenstergröße nutzte, der sichtbare Bereich aber nur der Viewport-Content-Bereich (nach Abzug der Editor-Panels) war.
- **Lösung**: `UIManager::updateLayouts()` speichert das finale `available`-Rect (nach dem Dock-Layout der Editor-Panels) als `m_viewportContentRect`. Dieses Rect wird pro Frame vom Renderer gecacht (`m_cachedViewportContentRect`).
- **Rendering**: `renderWorld()` setzt `glViewport` auf den Viewport-Content-Bereich innerhalb des Tab-FBO (statt auf die volle Fenstergröße). Das volle FBO wird zunächst gecleart, dann wird der Viewport auf das Content-Rect gesetzt.
- **Projektion**: Die Projektionsmatrix nutzt das Seitenverhältnis des Viewport-Content-Rects. Fenstergrößenänderungen wirken wie Zoomen (mehr/weniger Szene sichtbar) statt Verzerrung.
- **Pick-Buffer**: Das Pick-FBO bleibt in voller Fenstergröße (Window-Koordinaten funktionieren direkt). `renderPickBuffer` und `renderPickBufferSingleEntity` setzen ihren `glViewport` ebenfalls auf das Content-Rect.
- **NDC-Mapping**: `screenToWorldPos()`, `pickGizmoAxis()`, `beginGizmoDrag()` und `updateGizmoDrag()` rechnen Screen-Koordinaten relativ zum Content-Rect in NDC um.

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
- **Default World-Grid-Material**: Objekte ohne Diffuse-Textur zeigen automatisch ein World-Space-Grid-Muster (`uHasDiffuseMap` Uniform, `worldGrid()` in `fragment.glsl`). Das Grid nutzt XZ-Weltkoordinaten mit Major-Linien (1.0 Einheit) und Minor-Linien (0.25 Einheit).
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

**Abstrakte Interfaces:** `IRenderObject2D` (`src/Renderer/IRenderObject2D.h`), `IRenderObject3D` (`src/Renderer/IRenderObject3D.h`)
- Definieren backend-agnostische Schnittstellen für Render-Objekte
- `IRenderObject3D`: `hasLocalBounds()`, `getLocalBoundsMin/Max()` (Vec3), `getVertexCount()`, `getIndexCount()`
- Andere Subsysteme (z.B. `MeshViewerWindow`, `RenderResourceManager`) verwenden ausschließlich die abstrakten Interfaces

#### OpenGLObject2D
- Erbt von `IRenderObject2D` und `EngineObject`
- Erstellt aus `AssetData` + Texturen
- `prepare()` → Material aufbauen
- `setMatrices()` + `render()`

#### OpenGLObject3D
- Erbt von `IRenderObject3D` und `EngineObject`
- Erstellt aus `AssetData` (Mesh-Daten) + Texturen
- `prepare()` → Material + VAO/VBO aufbauen
- Lokale Bounding Box (`getLocalBoundsMin/Max` via Interface, `localBoundsMinGLM/MaxGLM` für GL-Backend)
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
- Popup-Unterstützung: `ensurePopupVao()` / `swapVao()` erzeugen und wechseln ein kontext-lokales VAO für sekundäre GL-Kontexte (VAOs werden zwischen Kontexten nicht geteilt)

---

### 9.9 RenderResourceManager
**Datei:** `src/Renderer/RenderResourceManager.h/.cpp`

Verwaltet das Caching und die Erstellung von Render-Ressourcen:

- `prepareActiveLevel()` → Lädt alle Assets des aktiven Levels in GPU-Ressourcen
- `buildRenderablesForSchema(schema)` → Erstellt `RenderableAsset`-Liste für ein ECS-Schema
- `getOrCreateObject2D/3D()` → Cache-basierte Objekt-Erstellung (gibt abstrakte `shared_ptr<IRenderObject2D/3D>` zurück)
- `prepareTextRenderer()` → Lazy-Init des Text-Renderers (gibt abstrakten `shared_ptr<ITextRenderer>` zurück)
- `buildWidgetAsset(asset)` → Erstellt Widget aus Asset-Daten
- `refreshEntityRenderable(entity, defaultFragmentShader)` → Baut Render-Daten für eine einzelne Entität neu auf, nutzt bestehende GPU-Caches (Object3D, Material, Texturen); lädt nur fehlende Assets nach
- `resolveContentPath(rawPath)` → Löst relative Asset-Pfade in absolute Dateipfade auf (Content-Ordner und Engine-Content als Fallback). **Öffentlich** (`public`), damit andere Subsysteme (z.B. `openMeshViewer`) Registry-relative Pfade vor AssetManager-Lookups auflösen können.
- `clearCaches()` → Alle Caches leeren

**Abstraktion:**
- Öffentliche API verwendet ausschließlich abstrakte Interface-Typen (`IRenderObject2D`, `IRenderObject3D`, `ITextRenderer`)
- `RenderableAsset` Struct enthält `shared_ptr<IRenderObject3D>` und `shared_ptr<IRenderObject2D>`
- Intern werden weiterhin konkrete OpenGL-Objekte erstellt (impliziter Upcast bei Rückgabe)
- `OpenGLRenderer` castet bei Bedarf über `std::static_pointer_cast` auf konkrete Typen zurück

Caches:
- `m_object2DCache` → `weak_ptr<IRenderObject2D>` nach Asset-ID
- `m_object3DCache` → `weak_ptr<IRenderObject3D>` nach Pfad-String
- `m_materialDataCache` → Textur + Shininess nach Pfad
- `m_widgetCache` → `weak_ptr<Widget>` nach Asset-ID

---

## 10. UI-System

### 10.1 UIManager
**Datei:** `src/Renderer/UIManager.h/.cpp`

Zentrale Verwaltung aller UI-Widgets:

#### Registrierung:
```cpp
registerWidget("TitleBar", widgetPtr);                      // Global (alle Tabs)
registerWidget("ViewportOverlay", widgetPtr, "Viewport");    // Tab-scoped (nur sichtbar wenn Tab aktiv)
unregisterWidget("TitleBar");
```

#### Tab-Scoped Widgets:
- Jedes Widget hat einen optionalen `tabId` (leer = global, sichtbar in allen Tabs)
- Nicht-leerer `tabId` = Widget nur sichtbar/hit-testbar wenn `m_activeTabId == tabId`
- `setActiveTabId(id)` / `getActiveTabId()` steuern den aktiven Tab-Filter
- Viewport-spezifische Widgets (ViewportOverlay, WorldSettings, WorldOutliner, EntityDetails, ContentBrowser) sind zum Tab `"Viewport"` zugeordnet
- Globale Widgets (TitleBar, StatusBar) haben leeren `tabId` und bleiben immer sichtbar
- Mesh-Viewer-Properties-Panels werden beim Öffnen mit dem jeweiligen Tab-ID registriert

#### Z-Ordering:
- Widgets haben Z-Order (niedrig = hinten)
- `getWidgetsOrderedByZ()` → sortierte Liste (gecacht, dirty-flag, `std::stable_sort` für deterministische Reihenfolge bei gleichem Z-Wert)

#### Input-Handling:
```cpp
handleMouseDown(screenPos, button)  → Hit-Test → onClick/Focus
handleScroll(screenPos, delta)       → Scroll auf scrollable-Elementen
handleTextInput(text)                → Aktives Entry-Bar füllen
handleKeyDown(key)                   → Backspace/Enter für Entry-Bars, F2 für Asset-Rename im Content Browser
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
- **Größenberechnung:** `measureElementSize` erzwingt `minSize` für alle Kind-Elemente (StackPanel, Grid, TreeView, TabView, ColorPicker). Dadurch werden Elemente mit `minSize` korrekt in die Content-Höhe des Elternelements eingerechnet, was exakte Scroll-Bereiche und Spacing garantiert.
- **Scroll-Clipping:** `computeElementBounds` begrenzt die Bounds scrollbarer Container auf deren eigene Rect-Fläche. Kinder die aus dem sichtbaren Bereich gescrollt wurden erweitern die Hit-Test-Bounds nicht mehr, sodass verdeckte Elemente keine Klicks/Hover mehr abfangen.
- **EntityDetails Layout-Fix:** Das EntityDetails-Widget wird im ersten Layout-Durchlauf übersprungen und ausschließlich im zweiten Pass mit der korrekten Split-Größe (basierend auf WorldOutliner-Position × splitRatio) gelayoutet. Dadurch wird der ScrollOffset nicht mehr fälschlich am kleineren maxScroll des ersten Passes geklemmt, und das Panel lässt sich vollständig durchscrollen.
- **DropdownButton-Sizing:** `layoutElement` behandelt `DropdownButton` jetzt im selben Content-basierten Sizing-Pfad wie `Text` und `Button`, sodass die Elementgröße korrekt aus dem gemessenen Inhalt abgeleitet wird.
- **DropdownButton-Klick:** Dismiss-Logik erkennt DropdownButton-Elemente zusätzlich zum ID-Prefix `"Dropdown."`, damit der Klick nicht vorab geschluckt wird. Source-Tracking (`m_dropdownSourceId`) ermöglicht echtes Toggle-Verhalten (erneuter Klick auf denselben Button schließt das Menü). Leere Item-Listen zeigen einen deaktivierten Platzhalter „(No assets available)". `showDropdownMenu` akzeptiert einen `minWidth`-Parameter, sodass das Menü mindestens so breit wie der auslösende Button dargestellt wird.
- **DropdownButton-Hover:** Der Renderer nutzt nun den Button-Shader (`m_defaultButtonVertex`/`m_defaultButtonFragment`) statt des Panel-Shaders für DropdownButton-Elemente, sodass Hover-Farbwechsel korrekt angezeigt werden.
- **Hover-Stabilität:** `populateOutlinerDetails` invalidiert den gecachten `m_lastHoveredElement`-Zeiger beim Neuaufbau des Element-Baums, um Dangling-Pointer nach Elementzerstörung zu vermeiden.
- **Panel-Breite:** WorldOutliner und EntityDetails verwenden eine Breite von 280 px (statt 200 px), um editierbare Steuerelemente besser darzustellen. Validierungs-Checks in `ensureEditorWidgetsCreated` erzwingen eine Neugenerierung älterer `.asset`-Dateien.
- **DropDown-Z-Order:** Aufgeklappte DropDown-Listen werden in einem verzögerten zweiten Render-Durchgang gezeichnet, damit sie über allen anderen Steuerelementen liegen und nicht von Geschwister-Elementen verdeckt werden.
- **DropDown-Hit-Testing:** `hitTest` enthält einen Vor-Durchlauf, der aufgeklappte DropDown-Elemente prioritär prüft, bevor die normale Baumtraversierung startet. Dadurch erhalten die Dropdown-Items den Klick und nicht darunterliegende Geschwister-Elemente.
- **Registry-Refresh:** `AssetManager` besitzt einen atomaren Versionszähler (`m_registryVersion`), der bei jeder Registrierung, Umbenennung (`renameAsset`), Verschiebung (`moveAsset`) und Löschung (`deleteAsset`) hochgezählt wird. `UIManager::updateNotifications` vergleicht diesen Wert und baut das Details-Panel automatisch neu auf, damit Dropdowns (Mesh/Material/Script) sofort die aktuellen Asset-Namen und -Pfade anzeigen.
- **Rename-Tastatureingabe:** Beim Inline-Umbenennen im Content Browser wird die Rename-EntryBar automatisch per `setFocusedEntry` fokussiert. Engine-Shortcuts (W/E/R-Gizmo, Ctrl+Z/Y/S, F2/DELETE) werden blockiert, solange ein Eingabefeld aktiv ist.
- **Schriftgrößen:** Details-Panel-Hilfsfunktionen verwenden größere Schriftgrößen (Text 13 px, Eingabefelder/Checkboxen/Dropdowns 12 px) und breitere Labels (100 px) für bessere Lesbarkeit.

#### Click-Events:
```cpp
registerClickEvent("TitleBar.Close", []() { ... });
// Elemente mit clickEvent-Feld rufen registrierte Callbacks auf
```

#### Notifications:
- **Modal**: `showModalMessage(message, onClosed)` – blockierendes Popup
- **Toast**: `showToastMessage(message, duration)` – temporäre Meldung
- Toast-Stack-Layout: Automatisches Stapeln bei mehreren Toasts

#### Popup-Fenster:
- `openLandscapeManagerPopup()` — öffnet das Landscape-Manager-Popup mit Formular-UI (vormals in `main.cpp`).
- `openEngineSettingsPopup()` — öffnet das Engine-Settings-Popup mit Sidebar-Navigation (vormals in `main.cpp`).
- Beide Methoden nutzen den `m_renderer`-Back-Pointer (`setRenderer()`) um `OpenGLRenderer::openPopupWindow()` / `closePopupWindow()` aufzurufen.

#### Mesh Viewer (Editor-Fenster):
- **Klasse**: `MeshViewerWindow` (`src/Renderer/EditorWindows/MeshViewerWindow.h/.cpp`)
- **Zweck**: 3D-Vorschau einzelner Static Meshes (Model3D-Assets) mit normaler FPS-Kamera und Eigenschaften-Panel.
- **Architektur**: Nutzt ein **Runtime-EngineLevel** (`m_runtimeLevel`) mit einer Mesh-Entity, einem Directional Light und einer Ground-Plane. Das Level existiert **nur im Speicher** und wird nie auf Disk serialisiert (`saveLevelAsset` überspringt Levels mit dem Namen `__MeshViewer__`). Beim Öffnen des Viewers wird das aktive Level per `DiagnosticsManager::swapActiveLevel()` atomisch ausgetauscht. `renderWorld()` baut die Szene beim nächsten Frame automatisch aus dem JSON des Runtime-Levels auf.
- **Auto-Material**: `createRuntimeLevel()` liest `m_materialAssetPaths[0]` aus dem Mesh-Asset und setzt den Material-Pfad in der MaterialComponent. Meshes ohne referenziertes Material rendern mit dem Grid-Shader-Fallback.
- **Ground-Plane**: Eine skalierte `default_quad3d`-Entity (20×0.01×20 bei Y=-0.5) mit `Materials/WorldGrid.asset` als Material dient als Bodenebene mit Gitter-Darstellung.
- **Performance-Stats**: FPS, CPU/GPU-Metriken und Occlusion-Stats werden in Mesh-Viewer-Tabs ausgeblendet (nur im Viewport-Tab sichtbar).
- **Tab-basiertes System**: Jeder Mesh Viewer erstellt einen eigenen **EditorTab** mit eigenem FBO (`addTab(assetPath, name, closable)`). Beim Tab-Wechsel wird einfach der Framebuffer ausgetauscht — der Tab-FBO wird an der Position des Tab-Bereichs angezeigt. Dynamische Tab-Buttons werden in der TitleBar registriert.
- **UI-Tab-Filterung**: Das Properties-Widget (`MeshViewerDetails.{assetPath}`) wird mit `tabId = assetPath` registriert. Der UIManager rendert/layoutet nur Widgets, deren `tabId` zum aktiven Tab passt. Viewport-Widgets (WorldOutliner, EntityDetails etc.) sind dem Tab `"Viewport"` zugeordnet und erscheinen nur dort.
- **Kamera**: Normale FPS-Kamera (WASD + Rechtsklick-Mausbewegung). Beim Öffnen wird die Kamera automatisch aus der Mesh-AABB berechnet (Position: vorne-rechts-oben vom Zentrum, Blickrichtung zum Mesh). Die Kameraposition wird pro Tab im Level-EditorCamera gespeichert und beim Tab-Wechsel wiederhergestellt.
- **Per-Tab Entity-Selektion**: `m_tabSelectedEntity` Map speichert die ausgewählte Entity pro Tab-ID. Beim Verlassen eines Tabs wird der Selection-State gesichert, beim Betreten wiederhergestellt. Viewport-Selektion in `m_savedViewportSelectedEntity`.
- **Editierbare Asset-Properties**: Das Sidepanel (320px) zeigt bearbeitbare Felder für Scale X/Y/Z und Material-Pfad. Änderungen modifizieren die Asset-Daten direkt (`AssetData::getData()`) und markieren das Asset als unsaved (`setIsSaved(false)`), sodass es beim nächsten Speichern serialisiert wird.
- **Runtime-Level-JSON**: Enthält ein `"Entities"`-Array mit drei Einträgen: (1) Mesh-Entity mit Transform + Mesh + Material (aus .asset) + Name, (2) Directional Light Entity mit Transform + Light + Name, (3) Ground-Plane Entity mit Transform + Mesh (default_quad3d) + Material (WorldGrid) + Name. Zusätzlich ein `"EditorCamera"`-Block mit initialer Position und Rotation.
- **Level-Swap beim Tab-Wechsel**:
  - **Öffnen**: `openMeshViewer()` erstellt Tab, speichert Editor-Kamera in `m_savedCameraPos`/`m_savedCameraRot`, tauscht Runtime-Level als aktives Level ein, ruft `setActiveTab(assetPath)` auf.
  - **Schließen**: `closeMeshViewer()` wechselt zu `setActiveTab("Viewport")`, entfernt Tab-Buttons, deregistriert Properties-Widget, ruft `removeTab(assetPath)` auf.
- **Beleuchtung**: Ein Directional Light im Runtime-Level (Rotation 50°/30°, Intensität 0.8, natürliches Warmweiß 0.9/0.85/0.78) von oben-rechts-vorne.
- **Öffnung**: Doppelklick auf Model3D-Asset im Content Browser → `OpenGLRenderer::openMeshViewer(assetPath)`.
   - **Pfad-Auflösung**: Der Content-Browser übergibt Registry-relative Pfade (z.B. `default_quad3d.asset`). `openMeshViewer()` ruft `RenderResourceManager::resolveContentPath(assetPath)` auf, um den Pfad in einen absoluten Dateipfad aufzulösen.
   - Asset wird automatisch geladen falls noch nicht im Speicher (`AssetManager::loadAsset` mit `Sync`).
   - Toast-Benachrichtigung "Loading {name}..." wird angezeigt.
   - `createRuntimeLevel(assetPath)` erstellt das JSON-Level, Properties-Widget wird tab-scoped registriert.
   - **Diagnose-Logging**: Detailliertes Logging an jedem Fehlerpunkt in `openMeshViewer()` und `MeshViewerWindow::initialize()`.
- **Schließen**: `closeMeshViewer(assetPath)` — wechselt auf Viewport-Tab, stellt Editor-Level/Kamera wieder her, entfernt Tab und Widgets.
- **Input-Routing in `main.cpp`**: `getMeshViewer(getActiveTabId())` steuert Orbit-Kamera-Input (Scroll → Zoom, Rechtsklick-Drag → Orbit).

#### World-Outliner-Integration:
```cpp
refreshWorldOutliner()          → Aktualisiert Entitäten-Liste
selectEntity(entityId)          → Wählt Entität aus, zeigt Details
```
- **Optimiertes Refresh**: `refreshWorldOutliner()` wird nur bei Entity-Erstellung/-Löschung aufgerufen (über `EngineLevel::m_entityListChangedCallbacks`). Komponentenänderungen (Add/Remove/Edit) lösen keinen Outliner-Rebuild aus, sondern nur ein `populateOutlinerDetails()`-Update des Detail-Panels.

#### EntityDetails Drag-and-Drop & Asset-Auswahl:
- **Mesh/Material/Script-Sektionen** enthalten jeweils:
  - Aktuelle Asset-Pfad-Anzeige (Text)
  - **DropdownButton** mit allen Projekt-Assets des passenden Typs (Model3D, Material, Script) — Auswahl setzt die Komponente direkt. Der DropdownButton dient gleichzeitig als Drop-Target für Drag-and-Drop aus dem Content Browser (IDs: `Details.{Mesh|Material|Script}.Dropdown`).
- **Typ-Validierung**: Beim Drop wird der Asset-Typ aus dem Payload (`"typeInt|relPath"`) gegen den erwarteten Typ des DropdownButtons geprüft. Bei falschem Typ erscheint eine Toast-Meldung.
- **`applyAssetToEntity(type, path, entity)`**: Interne Hilfsmethode — setzt `MeshComponent`, `MaterialComponent` oder `ScriptComponent` via ECS, ruft `invalidateEntity(entity)` auf (damit nur diese Entität per `refreshEntity()` neu aufgebaut wird, ohne alle Render-Ressourcen zu rebuilden), markiert das Level als unsaved (`setIsSaved(false)`), zeigt Toast-Bestätigung und aktualisiert das Details-Panel.

#### EntityDetails Komponenten-Management:
- **Remove-Button ("X")**: Jede Komponenten-Sektion (Name, Transform, Mesh, Material, Light, Camera, Physics, Script) hat in der Separator-Kopfzeile einen roten "X"-Button. Klick öffnet `showConfirmDialog` mit Bestätigung, bei "Delete" wird `ecs.removeComponent<T>(entity)` aufgerufen. Danach: Für renderable Komponenten (Mesh, Material, Transform) `invalidateEntity(entity)`, für andere (Name, Light, Camera, Physics, Script) keine Render-Invalidierung nötig (werden direkt aus ECS gelesen). Immer: `setIsSaved(false)` (Level dirty) und `populateOutlinerDetails(entity)` (Panel neu aufbauen). Der World Outliner wird **nicht** manuell refreshed — er aktualisiert sich nur bei Entity-Erstellung/-Löschung über den `EngineLevel`-Callback.
- **"+ Add Component"-Dropdown**: Am Ende des Details-Panels listet ein `DropdownButton` alle Komponententypen auf, die die Entität noch **nicht** besitzt. Auswahl ruft `ecs.addComponent<T>(entity, T{})` mit Default-Werten auf, setzt `invalidateEntity(entity)` + `setIsSaved(false)`, ruft `populateOutlinerDetails(entity)` auf und zeigt eine Toast-Bestätigung. Der World Outliner wird nur bei Name-Komponentenzusatz refreshed.
- **Auto-Refresh**: Durch das ECS Dirty-Flagging (`m_componentVersion`) wird das Details-Panel nach Add/Remove automatisch aktualisiert.

#### EntityDetails Editierbare Komponentenwerte:
Alle Komponentenwerte sind über passende Steuerelemente direkt im Details-Panel editierbar:

| Komponente | Eigenschaft | Steuerelement |
|---|---|---|
| **Name** | `displayName` | EntryBar |
| **Transform** | `position`, `rotation`, `scale` | Vec3-Reihen (3 farbcodierte EntryBars: X=rot, Y=grün, Z=blau) |
| **Light** | `type` | DropDown (Point/Directional/Spot) |
| **Light** | `color` | ColorPicker (kompakt) |
| **Light** | `intensity`, `range`, `spotAngle` | Float-EntryBars |
| **Camera** | `fov`, `nearClip`, `farClip` | Float-EntryBars |
| **Camera** | `isActive` | CheckBox |
| **Physics** | `colliderType` | DropDown (Box/Sphere/Mesh) |
| **Physics** | `isStatic`, `isKinematic`, `useGravity` | CheckBoxen |
| **Physics** | `mass`, `restitution`, `friction` | Float-EntryBars |
| **Physics** | `colliderSize`, `velocity`, `angularVelocity` | Vec3-Reihen |
| **Mesh/Material/Script** | Asset-Pfad | DropdownButton (bestehend) |

- **Änderungsfluss**: Jedes Control ruft bei Commit `ecs.setComponent<T>(entity, updated)` auf, was `m_componentVersion` inkrementiert und das Panel beim nächsten Frame automatisch aktualisiert. **Alle Callbacks markieren das Level als unsaved** (`setIsSaved(false)`), damit Änderungen sofort im StatusBar-Dirty-Zähler reflektiert werden.
- **Sofortige visuelle Rückmeldung**: Transform-, Light- und Camera-Werte werden vom Renderer jeden Frame direkt aus dem ECS gelesen (`updateModelMatrices`-Lambda, Light-Schema-Query, Camera-Query) — Änderungen sind sofort im Viewport sichtbar ohne Render-Invalidierung. Mesh/Material-Pfadänderungen nutzen `invalidateEntity(entity)`, das die betroffene Entität in eine Dirty-Queue (`DiagnosticsManager::m_dirtyEntities`) einreiht. Im nächsten Frame ruft `renderWorld()` für jede Dirty-Entität `refreshEntity()` → `refreshEntityRenderable()` auf, das bestehende GPU-Caches nutzt und nur fehlende Assets nachlädt — statt den gesamten Scene-Graph neu aufzubauen.
- **Name-Änderungen**: Der Name-EntryBar aktualisiert zusätzlich das Entity-Header-Label (`Details.Entity.NameLabel`) und ruft `refreshWorldOutliner()` auf, damit Namensänderungen sofort im Outliner und im Details-Panel-Header reflektiert werden. Außerdem wird das Level als unsaved markiert.
- **Hilfslambdas**: `makeFloatEntry`, `makeVec3Row`, `makeCheckBoxRow` erzeugen wiederverwendbare UI-Zeilen mit Label + Control + onValueChanged-Callback. Alle drei Lambdas rufen nach dem eigentlichen Wert-Callback automatisch `setIsSaved(false)` auf, sodass jede Werteänderung das Level dirty markiert.
- **Inline-Callbacks** (Light-Typ-DropDown, Light-ColorPicker, Physics-Collider-DropDown): Diese Callbacks gehen nicht durch die Hilfslambdas, enthalten aber ebenfalls `setIsSaved(false)` für konsistente Dirty-Markierung.

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
| `DropdownButton`  | Button der ein Dropdown-Menü öffnet   |

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
| `DropdownButtonWidget` | `DropdownButtonWidget.h/.cpp` | Button der beim Klick ein Dropdown-Menü öffnet, `dropdownItems` oder `items`+`onSelectionChanged` |
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
| `get_light_color(e)`   | Gibt Lichtfarbe (r,g,b) zurück        |
| `set_light_color(e, r,g,b)` | Setzt Lichtfarbe                 |

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

---

## 15. Physik-System

**Dateien:** `src/Physics/PhysicsWorld.h/.cpp`, `src/Physics/IPhysicsBackend.h`, `src/Physics/JoltBackend.h/.cpp`
**Backend:** Jolt Physics v5.5.1 (`external/jolt/`) via austauschbares Backend-Interface

### 15.1 Übersicht
Backend-agnostische Rigid-Body-Simulation als Singleton (`PhysicsWorld::Instance()`). Wird nur während des PIE-Modus aktiv.

Die Physik-Logik ist in zwei Schichten aufgeteilt:
- **PhysicsWorld** (Backend-agnostisch): ECS-Synchronisation, Event-Dispatch, Overlap-Tracking, Fixed-Timestep-Akkumulator. Delegiert an `IPhysicsBackend`.
- **IPhysicsBackend** (abstraktes Interface): Definiert `BodyDesc`, `BodyState`, `CollisionEventData`, `RaycastResult` und ~15 virtuelle Methoden für Lifecycle, Body-Verwaltung, Simulation, Raycast und Sleep.
- **JoltBackend** (konkrete Implementierung): Kapselt alle Jolt-Physics-spezifischen Typen (`JPH::PhysicsSystem`, `JPH::BodyInterface`, Layer-Definitionen, `EngineContactListener`). Weitere Backends (z.B. PhysX) können durch Implementierung von `IPhysicsBackend` hinzugefügt werden.

### 15.2 Komponenten-Architektur
Die Physik nutzt zwei separate ECS-Komponenten:
- **CollisionComponent** (erforderlich): Definiert Form (Collider), Oberflächeneigenschaften und Trigger-Volumes.
- **PhysicsComponent** (optional): Definiert Rigid-Body-Dynamik. Fehlt diese Komponente, wird der Body als statisch behandelt.

Minimale Voraussetzung für einen Jolt-Body: `TransformComponent` + `CollisionComponent`.

#### CollisionComponent
```cpp
struct CollisionComponent {
    enum ColliderType { Box=0, Sphere=1, Capsule=2, Cylinder=3, Mesh=4 };
    int colliderType = 0;
    float colliderSize[3] = {0.5f, 0.5f, 0.5f}; // Half-Extents / Radius / HalfHeight
    float colliderOffset[3] = {0, 0, 0};          // Offset via OffsetCenterOfMassShape
    float restitution = 0.3f;
    float friction = 0.5f;
    bool isSensor = false;                         // Trigger-Volume (kein physischer Kontakt)
};
```

#### PhysicsComponent
```cpp
struct PhysicsComponent {
    enum MotionType { Static=0, Kinematic=1, Dynamic=2 };
    enum MotionQuality { Discrete=0, LinearCast=1 }; // CCD
    int motionType = 2;         // Default: Dynamic
    float mass = 1.0f;
    float gravityFactor = 1.0f; // Skaliert Gravitation pro Body
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    float maxLinearVelocity = 500.0f;
    float maxAngularVelocity = 47.12f; // ~15π rad/s
    int motionQuality = 0;      // 0=Discrete, 1=LinearCast (CCD)
    bool allowSleeping = true;
    float velocity[3] = {0,0,0};
    float angularVelocity[3] = {0,0,0};
};
```

### 15.3 Architektur
- **Backend-Abstraction**: `PhysicsWorld` hält ein `std::unique_ptr<IPhysicsBackend> m_backend` und delegiert alle Backend-spezifischen Operationen.
- **Fixed Timestep**: 1/60 s mit Akkumulator, delegiert an `IPhysicsBackend::step()`
- **Pipeline** (pro `step(dt)`):
  1. `syncBodiesToBackend()` – Erzeugt/aktualisiert Bodies via `IPhysicsBackend::createBody(BodyDesc)` / `updateBody()` aus ECS (`TransformComponent` + `CollisionComponent`, optional `PhysicsComponent`)
  2. `m_backend->step()` – Backend übernimmt Kollisionserkennung, Impulsauflösung, Constraint-Solving, Sleep-Management
  3. `syncBodiesFromBackend()` – Liest `BodyState` (Position, Rotation, Velocity) via `IPhysicsBackend::getBodyState()` zurück ins ECS (nur Dynamic)
  4. `updateOverlapTracking()` + `fireCollisionEvents()` – Overlap-Tracking und Collision-Callbacks
- **Entity-Tracking**: `m_trackedEntities` (std::set) verfolgt welche Entities einen Body im Backend haben.

### 15.4 Unterstützte Collider-Formen

| ColliderType | Jolt Shape         | colliderSize-Mapping                     |
|--------------|--------------------|-----------------------------------------|
| Box (0)      | `JPH::BoxShape`   | [halfX, halfY, halfZ]                    |
| Sphere (1)   | `JPH::SphereShape` | [radius, -, -]                          |
| Capsule (2)  | `JPH::CapsuleShape`| [radius, halfHeight, -]                 |
| Cylinder (3) | `JPH::CylinderShape`| [radius, halfHeight, -]                |
| Mesh (4)     | Fallback → BoxShape | Noch nicht implementiert                |

- **Collider-Offset**: `colliderOffset[3]` wird über `JPH::OffsetCenterOfMassShape` angewendet.
- **Sensor/Trigger**: `isSensor=true` → `BodyCreationSettings::mIsSensor = true` (keine physische Reaktion, nur Overlap-Events).

### 15.5 Jolt Body-Eigenschaften

| Eigenschaft          | Jolt-Mapping                              |
|----------------------|------------------------------------------|
| motionType           | `JPH::EMotionType` (Static/Kinematic/Dynamic) |
| mass                 | `JPH::MassProperties` via Shape          |
| gravityFactor        | `BodyCreationSettings::mGravityFactor`    |
| linearDamping        | `BodyCreationSettings::mLinearDamping`    |
| angularDamping       | `BodyCreationSettings::mAngularDamping`   |
| maxLinearVelocity    | `BodyCreationSettings::mMaxLinearVelocity`|
| maxAngularVelocity   | `BodyCreationSettings::mMaxAngularVelocity`|
| motionQuality        | `JPH::EMotionQuality` (Discrete/LinearCast CCD) |
| allowSleeping        | `BodyCreationSettings::mAllowSleeping`    |
| isSensor             | `BodyCreationSettings::mIsSensor`         |
| restitution          | `BodyCreationSettings::mRestitution`      |
| friction             | `BodyCreationSettings::mFriction`         |

### 15.6 Integration
- **PIE Start**: `PhysicsWorld::Instance().initialize()` (Gravitation auf 0, -9.81, 0)
- **PIE Frame**: `PhysicsWorld::Instance().step(dt)` (vor Scripting)
- **PIE Stop**: `PhysicsWorld::Instance().shutdown()`

### 15.7 Overlap-Tracking (Begin / End)
- `PhysicsWorld` vergleicht pro Frame die aktuelle Menge kollidierender Entity-Paare mit der des Vorframes.
- **Neue Paare** → `OverlapEvent` in `m_beginOverlapEvents`.
- **Entfallene Paare** → `OverlapEvent` in `m_endOverlapEvents`.
- `Scripting::UpdateScripts()` ruft für jede beteiligte Entity deren Script-Funktion auf:
  - `on_entity_begin_overlap(entity, other_entity)`
  - `on_entity_end_overlap(entity, other_entity)`

### 15.8 Serialisierung
- **Neue Formate**: `CollisionComponent`, `PhysicsComponent` und `HeightFieldComponent` werden separat als "Collision", "Physics" und "HeightField" JSON-Keys serialisiert. Die `HeightFieldComponent`-Serialisierung umfasst: `heights`-Vektor, `sampleCount`, `offsetX/Y/Z`, `scaleX/Y/Z`.
- **Backward Compatibility**: `deserializeLegacyPhysics()` erkennt alte Formate (mit "isStatic"-Feld) und splittet sie automatisch in beide Komponenten.

### 15.9 Editor-UI
- **Collision-Sektion**: ColliderType-Dropdown (Box/Sphere/Capsule/Cylinder/Mesh/HeightField), Size, Offset, Restitution, Friction, isSensor-Checkbox.
- **Physics-Sektion**: MotionType-Dropdown (Static/Kinematic/Dynamic), Mass, GravityFactor, LinearDamping, AngularDamping, MaxLinearVelocity, MaxAngularVelocity, MotionQuality-Dropdown (Discrete/LinearCast CCD), AllowSleeping, Velocity, AngularVelocity.

### 15.10 CMake
- Target: `Physics` (SHARED-Bibliothek)
- Quellen: `PhysicsWorld.cpp`, `IPhysicsBackend.h`, `JoltBackend.cpp`
- Abhängigkeiten: Core, Logger, Jolt

---

## 16. Landscape-System

**Dateien:** `src/Landscape/LandscapeManager.h/.cpp`

- `LandscapeManager::spawnLandscape(params)` – Generiert ein flaches Grid-Mesh (XZ-Ebene), speichert es als `.asset` in `Content/Landscape/`, erstellt ein ECS-Entity mit Transform + Mesh + Name + Material (WorldGrid) + CollisionComponent (HeightField) + HeightFieldComponent (Höhendaten, Offsets, Skalierung) + Physics (statisch). Jolt HeightFieldShape wird direkt aus den Höhendaten erzeugt.
- `LandscapeManager::hasExistingLandscape()` – Prüft ob bereits ein Landscape-Entity existiert (MeshComponent-Pfad beginnt mit `Landscape/`).
- **Nur ein Landscape pro Szene**: Das Landscape Manager Popup wird blockiert, wenn bereits ein Landscape existiert; stattdessen wird eine Toast-Nachricht angezeigt.
- `LandscapeParams`: name, width, depth, subdivisionsX, subdivisionsZ, heightData (optional).
- Popup-UI über `UIManager::openLandscapeManagerPopup()` mit Formular (Name, Width, Depth, SubdivX, SubdivZ, Create/Cancel). Die Widget-Erstellung wurde aus `main.cpp` in den UIManager verschoben. Landscape-Erstellung erzeugt eine Undo/Redo-Action (Undo entfernt das Entity).
- **Dropdown-Menü-System**: `UIManager::showDropdownMenu(anchor, items)` / `closeDropdownMenu()` — zeigt ein Overlay-Widget (z-Order 9000) mit klickbaren Menüeinträgen an einer Pixelposition. Unterstützt zusätzlich visuelle Separator-Einträge (`DropdownMenuItem::isSeparator`). Click-Outside schließt das Menü automatisch.
- **Content-Browser-Kontextmenü (Grid, Rechtsklick)**: enthält `New Folder`, anschließend Separator, dann `New Script`, `New Level`, `New Material`.
- **Engine Settings Popup** über `UIManager::openEngineSettingsPopup()` (aufgerufen aus `ViewportOverlay.Settings` → Dropdown-Menü → "Engine Settings"): Links Sidebar mit Kategorie-Buttons (General, Rendering, Debug, Physics), rechts scrollbarer Content-Bereich mit Checkboxen und Float-Eingabefeldern. General-Kategorie enthält: Splash Screen. Rendering-Kategorie enthält: Shadows, VSync, Wireframe Mode, Occlusion Culling. Debug-Kategorie enthält: UI Debug Outlines, Bounding Box Debug. Physics-Kategorie enthält: Backend-Dropdown (Jolt / PhysX, PhysX nur sichtbar wenn `ENGINE_PHYSX_BACKEND_AVAILABLE` definiert), Gravity X/Y/Z (Float-Eingabefelder, Default 0/-9.81/0), Fixed Timestep (Default 1/60 s), Sleep Threshold (Default 0.05). Die Backend-Auswahl wird als `PhysicsBackend`-Key in `DiagnosticsManager` persistiert und beim PIE-Start ausgelesen, um `PhysicsWorld::initialize(Backend)` mit dem gewählten Backend aufzurufen. Kategoriewechsel baut den Content-Bereich dynamisch um. Alle Änderungen werden in `config.ini` via `DiagnosticsManager::setState()` persistiert und beim PIE-Start auf `PhysicsWorld` angewendet (Backend, Gravity, Timestep, Sleep Threshold). Die Widget-Erstellung wurde aus `main.cpp` in den UIManager verschoben.
- Grid-Shader (`grid_fragment.glsl`) nutzt vollständige Lichtberechnung (Multi-Light, Schatten, Blinn-Phong) — Landscape wird von allen Lichtquellen der Szene beeinflusst.
- `EngineLevel::onEntityAdded()` / `onEntityRemoved()` setzen automatisch das Level-Dirty-Flag (`setIsSaved(false)`) und `setScenePrepared(false)` via Callback, sodass alle Aufrufer (Spawn, Delete, Landscape) einheitlich behandelt werden.

#### Skybox
- **Cubemap-Skybox**: Pro Level kann ein Skybox-Ordnerpfad oder ein `.asset`-Pfad gesetzt werden (`EngineLevel::setSkyboxPath()`). Der Ordner muss 6 Face-Bilder enthalten: `right`, `left`, `top`, `bottom`, `front`, `back` (als `.jpg`, `.png` oder `.bmp`).
- **Cubemap-Face-Zuordnung**: Die Faces werden gemäß der OpenGL-Cubemap-Konvention geladen: `right`→`+X`, `left`→`-X`, `top`→`+Y`, `bottom`→`-Y`, `front`→`-Z`, `back`→`+Z`. Die Standardkamera blickt entlang `-Z`, weshalb `front` auf `GL_TEXTURE_CUBE_MAP_NEGATIVE_Z` abgebildet wird.
- **Skybox Asset-Typ** (`AssetType::Skybox`): Eigener Asset-Typ mit JSON-Struktur `{ "faces": { "right": "...", "left": "...", ... }, "folderPath": "..." }`. Wird über `AssetManager::loadSkyboxAsset()` / `saveSkyboxAsset()` / `createSkyboxAsset()` verwaltet.
- **Level-JSON**: Der Pfad wird im Level-JSON als `"Skybox": "Skyboxes/MySkybox.asset"` (oder Ordnerpfad) gespeichert und beim Laden automatisch wiederhergestellt.
- **Rendering**: Die Skybox wird als erster 3D-Pass gerendert (nach glClear, vor Scene), mit `glDepthFunc(GL_LEQUAL)` und `glDepthMask(GL_FALSE)`. Die View-Matrix wird von der Translation befreit (`mat4(mat3(view))`), sodass die Skybox der Kamera folgt.
- **Scene-Prepare**: Beim Level-Prepare (`!isScenePrepared()`-Block) wird geprüft, ob das Level einen Skybox-Pfad hat aber die Cubemap noch nicht geladen ist (z.B. nach Fehlschlag oder Levelwechsel). In diesem Fall wird `setSkyboxPath` erneut aufgerufen, sodass die Skybox zuverlässig zusammen mit dem restlichen Level geladen wird.
- **Renderer-API**: `OpenGLRenderer::setSkyboxPath(path)` / `getSkyboxPath()` — akzeptiert sowohl Ordnerpfade als auch `.asset`-Pfade (Content-relativ). Bei `.asset`-Pfaden wird die Datei geparst und der `folderPath` oder die Face-Pfade aufgelöst. Direkte Ordnerpfade werden zuerst absolut versucht und dann als Content-relativer Pfad über `AssetManager::getAbsoluteContentPath()` aufgelöst (Fallback).
- **Shader**: `skybox_vertex.glsl` / `skybox_fragment.glsl` mit `gl_Position = pos.xyww` (depth=1.0 Trick).
- **WorldSettings UI**: Skybox-Pfad kann im WorldSettings-Panel eingegeben werden (EntryBar `WorldSettings.SkyboxPath`). Änderungen werden direkt auf den Renderer und das Level angewandt; die StatusBar wird sofort aktualisiert (`refreshStatusBar`), damit der Dirty-Status sichtbar ist.
- **Dirty-Tracking**: Jede Skybox-Änderung (Drag & Drop, UI-Eingabe, Clear) markiert das Level als `unsaved` und aktualisiert die StatusBar.
- **Diagnose-Logging**: `setSkyboxPath` loggt Warnungen bei fehlgeschlagener Pfadauflösung, nicht lesbaren `.asset`-Dateien und fehlenden Cubemap-Faces.
- **Content Browser**: Skybox-Assets erscheinen mit eigenem Icon (sky-blue Tint).
- **Default-Skyboxen**: Die Engine liefert zwei Beispiel-Skybox-Textursets unter `Content/Textures/SkyBoxes/Sunrise/` und `Content/Textures/SkyBoxes/Daytime/` (je 6 Faces: right/left/top/bottom/front/back). Beim Projektladen (`ensureDefaultAssetsCreated`) werden automatisch `.asset`-Dateien unter `Content/Skyboxes/Sunrise.asset` und `Content/Skyboxes/Daytime.asset` generiert, sofern die Face-Bilder im Engine-Content vorhanden sind. Die Bilder werden dabei ins Projekt-Content kopiert.

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
| `get_engine_time()`         | Sekunden seit Engine-Start (SDL)      |
| `get_state(key)`            | Engine-State abfragen                 |
| `set_state(key, value)`     | Engine-State setzen                   |

#### engine.physics
| Funktion                              | Beschreibung                          |
|---------------------------------------|---------------------------------------|
| `set_velocity(e, vx, vy, vz)`        | Geschwindigkeit setzen                |
| `get_velocity(e)`                     | Geschwindigkeit abfragen (Tupel)      |
| `add_force(e, fx, fy, fz)`           | Kraft anwenden (Impuls / Masse)       |
| `add_impulse(e, ix, iy, iz)`         | Impuls direkt anwenden                |
| `set_angular_velocity(e, x, y, z)`   | Winkelgeschwindigkeit setzen          |
| `get_angular_velocity(e)`             | Winkelgeschwindigkeit abfragen        |
| `set_gravity(gx, gy, gz)`            | Globale Gravitation setzen            |
| `get_gravity()`                       | Globale Gravitation abfragen          |
| `set_on_collision(callback)`          | Kollisions-Callback registrieren (entityA, entityB, normal, depth, point) |
| `raycast(ox,oy,oz,dx,dy,dz,max)`     | Raycast → `{entity, point, normal, distance}` oder `None` |
| `is_body_sleeping(entity)`            | Prüft ob Körper deaktiviert (schlafend) |

#### engine.logging
| Funktion                    | Beschreibung                          |
|-----------------------------|---------------------------------------|
| `log(message, level)`       | Log-Nachricht (0=Info, 1=Warn, 2=Error) |

#### engine.math (alle Berechnungen in C++)
| Funktion                    | Beschreibung                          |
|-----------------------------|---------------------------------------|
| `vec3(x,y,z)`              | Vec3-Tuple erzeugen                   |
| `vec3_add(a,b)`            | Komponentenweise Addition             |
| `vec3_sub(a,b)`            | Komponentenweise Subtraktion          |
| `vec3_mul(a,b)`            | Komponentenweise Multiplikation       |
| `vec3_div(a,b)`            | Komponentenweise Division             |
| `vec3_scale(v,s)`          | Vec3 mit Skalar multiplizieren        |
| `vec3_dot(a,b)`            | Skalarprodukt                         |
| `vec3_cross(a,b)`          | Kreuzprodukt                          |
| `vec3_length(v)`           | Länge                                 |
| `vec3_length_sq(v)`        | Quadrierte Länge                      |
| `vec3_normalize(v)`        | Normalisieren                         |
| `vec3_negate(v)`           | Negieren                              |
| `vec3_lerp(a,b,t)`         | Lineare Interpolation                 |
| `vec3_distance(a,b)`       | Abstand                               |
| `vec3_reflect(v,n)`        | Reflexion an Normale                  |
| `vec3_min(a,b)` / `vec3_max(a,b)` | Komponentenweises Min/Max      |
| `vec2(x,y)`                | Vec2-Tuple erzeugen                   |
| `vec2_add/sub/scale/dot/length/normalize/lerp/distance` | Wie Vec3, für 2D |
| `quat_from_euler(p,y,r)`   | Euler (Rad) → Quaternion (x,y,z,w)   |
| `quat_to_euler(q)`         | Quaternion → Euler (Rad)              |
| `quat_multiply(a,b)`       | Quaternion-Multiplikation             |
| `quat_normalize(q)`        | Quaternion normalisieren              |
| `quat_slerp(a,b,t)`        | Sphärische Interpolation              |
| `quat_inverse(q)`          | Quaternion-Inverse                    |
| `quat_rotate_vec3(q,v)`    | Vec3 mit Quaternion rotieren          |
| `clamp(v,lo,hi)`           | Wert begrenzen                        |
| `lerp(a,b,t)`              | Skalare Interpolation                 |
| `deg_to_rad(d)` / `rad_to_deg(r)` | Grad ↔ Radiant              |
| `sin(r)` / `cos(r)` / `tan(r)` | Trigonometrische Funktionen (Radiant) |
| `asin(v)` / `acos(v)` / `atan(v)` | Inverse trigonometrische Funktionen |
| `atan2(y,x)`               | Zwei-Argument Arkustangens            |
| `sqrt(v)`                  | Quadratwurzel                         |
| `abs(v)`                   | Betrag                                |
| `pow(base,exp)`            | Potenz                                |
| `floor(v)` / `ceil(v)` / `round(v)` | Runden (ab/auf/nächste Ganzzahl) |
| `sign(v)`                  | Vorzeichen (-1, 0 oder 1)            |
| `min(a,b)` / `max(a,b)`   | Minimum / Maximum zweier Werte        |
| `pi()`                     | Konstante π                           |

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
   → PhysicsWorld::Instance().initialize()
   → DiagnosticsManager.setPIEActive(true)

2. Pro Frame:
   → PhysicsWorld::Instance().step(dt)  (vor Scripting, Overlap-Events generieren)
   → Scripting::UpdateScripts(dt):
     → Level-Script laden + on_level_loaded() aufrufen (einmalig)
     → Für jede Script-Entity:
       → Script laden
       → onloaded(entity) aufrufen (einmalig pro Entity)
       → tick(entity, dt) aufrufen (jeden Frame)
     → Overlap-Events dispatchen:
       → on_entity_begin_overlap(entity, other_entity) (neue Kollisionen)
       → on_entity_end_overlap(entity, other_entity) (beendete Kollisionen)
     → Async-Asset-Load-Callbacks verarbeiten

3. PIE stoppen:
   → PhysicsWorld::Instance().shutdown()
   → DiagnosticsManager.setPIEActive(false)
   → AudioManager.stopAll()
   → Scripting::ReloadScripts() (Script-States zurücksetzen)
   → Level.restoreEcsSnapshot()
```

### 11.4 engine.pyi (IntelliSense)
**Datei:** `src/Scripting/engine.pyi`

Statische Python-Stub-Datei für IDE-Unterstützung (Autovervollständigung, Typ-Hinweise).

**Deployment-Ablauf:**
1. CMake post-build kopiert `src/Scripting/engine.pyi` → `Content/Scripting/engine.pyi` im Deploy-Verzeichnis
2. Beim Laden/Erstellen eines Projekts kopiert die Engine die Datei per `fs::copy_file` nach `<Projekt>/Content/Scripts/engine.pyi`
3. Bei API-Änderungen muss nur die statische Datei `src/Scripting/engine.pyi` aktualisiert werden — keine Laufzeit-Generierung mehr

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
    WASD → moveCamera (nur bei Rechtsklick gehalten, oder Laptop-Modus, oder PIE)
    Q/E  → moveCamera (hoch/runter, gleiche Bedingung)
    Maus-Position → UIManager.setMousePosition

    // ═══ Event-Verarbeitung ═══
    SDL_PollEvent:
    - QUIT → running = false
    - MOUSE_MOTION → UI-Hover + Kamera-Rotation (bei Rechtsklick oder PIE-Maus-Capture; UI-Updates deaktiviert während aktivem PIE-Capture)
    - MOUSE_BUTTON_DOWN (Links) → während PIE-Capture ignoriert; sonst PIE-Recapture (Position speichern + Grab) oder UI-Hit-Test oder Entity-Picking
    - MOUSE_BUTTON_DOWN (Rechts) → während PIE-Capture ignoriert; sonst Kamera-Steuerung aktivieren (nur außerhalb PIE), Mausposition speichern + Window-Grab
    - MOUSE_BUTTON_UP (Rechts) → Kamera-Steuerung deaktivieren, Maus zurück an gespeicherte Position warpen
    - MOUSE_WHEEL → während PIE-Capture ignoriert; sonst UI-Scroll oder Kamera-Geschwindigkeit ändern
    - TEXT_INPUT → UI-Texteingabe
    - KEY_UP:
        Shift+F1 → PIE-Maus freigeben / Input pausieren, Cursor an gespeicherte Position zurücksetzen
        F8  → Bounds-Debug toggle
        F9  → Occlusion-Stats toggle
        F10 → Metriken toggle
        F11 → UI-Debug toggle
        F12 → FPS-Cap toggle
        ESC → PIE stoppen
        W/E/R → Gizmo-Modus (nur ohne Rechtsklick, außerhalb PIE)
        DELETE → Selektierte Entity löschen (Snapshot aller Komponenten → Undo/Redo-Action)
        Sonst → DiagnosticsManager + Scripting
    - KEY_DOWN → UI-Keyboard + DiagnosticsManager + Scripting

    // ═══ Shutdown-Check ═══
    if (diagnostics.isShutdownRequested())
        LOG("Shutdown requested – exiting main loop.")
        running = false

    // ═══ Scripting ═══ (nur bei PIE aktiv)
    Scripting::UpdateScripts(dt)

    // ═══ Physik ═══ (nur bei PIE aktiv)
    PhysicsWorld::Instance().step(dt)

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

| Taste      | Funktion                              |
|------------|---------------------------------------|
| F8         | Bounding-Box-Debug toggle             |
| F9         | Occlusion-Statistiken toggle          |
| F10        | Performance-Metriken toggle           |
| F11        | UI-Debug (Bounds-Rahmen) toggle       |
| F12        | FPS-Cap (60 FPS) toggle               |
| ESC        | PIE stoppen (wenn aktiv)              |
| Shift+F1   | PIE-Maus freigeben / Input pausieren  |
| DELETE     | Selektierte Entity löschen (mit Undo/Redo) |

### Kamera-Steuerung

**Editor-Modus (Normal):**

| Eingabe           | Aktion                                |
|-------------------|---------------------------------------|
| RMB + W/A/S/D     | Kamera vorwärts/links/rückwärts/rechts |
| RMB + Q/E         | Kamera runter/hoch                    |
| Rechte Maustaste  | Kamera-Rotation aktivieren            |
| Mausrad (+ RMB)   | Kamera-Geschwindigkeit ändern (0.5x–5.0x) |
| W/E/R (ohne RMB)  | Gizmo-Modus: Translate/Rotate/Scale   |

**Editor-Modus (Laptop-Modus):**

| Eingabe           | Aktion                                |
|-------------------|---------------------------------------|
| W/A/S/D           | Kamera vorwärts/links/rückwärts/rechts (ohne RMB) |
| Q/E               | Kamera runter/hoch                    |
| Rechte Maustaste  | Kamera-Rotation aktivieren            |

**PIE-Modus:**

| Eingabe           | Aktion                                |
|-------------------|---------------------------------------|
| W/A/S/D           | Kamera-Bewegung (immer aktiv)         |
| Maus-Bewegung     | Kamera-Rotation (Maus versteckt & gefangen) |
| Shift+F1          | Maus freigeben, Input pausieren       |
| Klick auf Viewport | Maus erneut fangen, Input fortsetzen |
| ESC               | PIE beenden, vorherigen Zustand wiederherstellen |

### Projekt-Auswahl-Screen

Beim Start prüft die Engine, ob ein `DefaultProject`-Eintrag in `config.ini` existiert. Falls nicht (oder das Projekt nicht gefunden wird), läuft die Projektauswahl über einen temporären Renderer im normalen Hauptfenster.

| Kategorie        | Funktion                                                                                 |
|------------------|------------------------------------------------------------------------------------------|
| Recent Projects  | Liste bekannter Projekte (Pfade aus `KnownProjects` in config.ini), klickbar zum Laden   |
| Open Project     | "Browse"-Button öffnet SDL-Dateidialog zum Auswählen einer `.project`-Datei              |
| New Project      | Projektname + Speicherort (Browse-Dialog) eingeben, Checkbox **"Include default content"**, Live-Preview des Zielpfads, danach "Create Project" |

- Sidebar-Layout analog zu Engine Settings (Buttons links, Content rechts)
- **Isolierte Ressourcen**: Der Projekt-Auswahl-Screen nutzt einen temporären `OpenGLRenderer` (`tempRenderer`), der vor der Initialisierung der Haupt-Engine (AssetManager, Scripting, Audio) erstellt und danach wieder zerstört wird. Dadurch werden keine unnötigen Subsysteme geladen, bevor ein Projekt feststeht.
- Während der `tempRenderer`-Phase werden keine Runtime-Ressourcen beim `AssetManager` registriert (Guard auf `AssetManager::initialize()`), damit keine GL-Ressourcen über den temporären Kontext hinaus gehalten werden.
- Mini-Event-Loop im Startup: Render + Event-Pumping bis ein Projekt gewählt oder das Temp-Hauptfenster geschlossen wird
- Das Temp-Hauptfenster wird explizit angezeigt (`SDL_ShowWindow`/`SDL_RaiseWindow`) und nicht versteckt.
- Das Startup-Fenster läuft als **normales Fenster mit nativer Titlebar** (`SDL_SetWindowBordered(true)`, kein Fullscreen/Maximize) und deaktiviert Custom-HitTest (`SDL_SetWindowHitTest(..., nullptr, nullptr)`), damit Button-Hit-Testing stabil bleibt.
- Im Startup-Mini-Loop werden Maus-/Tastatur-/Text-Events explizit an `tempRenderer->getUIManager()` weitergereicht (`handleMouseDown/Up`, `handleScroll`, `handleTextInput`, `handleKeyDown`), damit die Projektauswahl-UI korrekt klick- und editierbar ist.
- Für das Startup-Fenster wird `SDL_StartTextInput(window)` explizit aktiviert, damit Texteingaben in `EntryBar`-Felder zuverlässig ankommen.
- `SDL_EVENT_QUIT` bzw. `SDL_EVENT_WINDOW_CLOSE_REQUESTED` für das Temp-Fenster beendet die Projektauswahl-Schleife sauber.
- Nach Schließen des Temp-Fensters und Zerstörung des `tempRenderer` wird das kleine Ladefenster (`SplashWindow`) angezeigt und die Haupt-Engine initialisiert; vor dem Schließen des Splash wird zuerst das Hauptfenster sichtbar gemacht, um SDL-`QUIT`-Fehlverhalten beim letzten sichtbaren Fenster zu vermeiden.
- Dateidialoge (Browse) nutzen das sichtbare Temp-Hauptfenster als Parent, damit Fokus und Stacking korrekt funktionieren.
- Im Bereich **Recent Projects** gibt es pro Eintrag einen quadratischen Lösch-Button (volle Zeilenhöhe). Existierende Projekte fragen vor dem Entfernen per Confirm-Dialog nach; der Dialog enthält zusätzlich die Checkbox **"Delete from filesystem"**. Fehlende Projekte werden direkt aus der Liste entfernt.
- **New Project** validiert den Projektnamen gegen ungültige Dateiname-Zeichen (`\\ / : * ? " < > |`) und zeigt eine **Warnung**, statt ungültige Eingaben automatisch zu korrigieren.
- Wird das Temp-Fenster ohne Projektauswahl geschlossen (z.B. Alt+F4/Schließen-Button), wird `DiagnosticsManager::requestShutdown()` gesetzt und die Engine beendet, statt auf `SampleProject` zu fallen.
- `DefaultProject` wird nur dann neu geschrieben, wenn im Projekt-Auswahlfenster die Checkbox **"Set as default project"** aktiv ist.
- Beim Laden eines Projekts mit aktivem Default-Content-Pfad ist `AssetManager::loadProject(...)` jetzt gegen Exceptions in `ensureDefaultAssetsCreated()` abgesichert (Error-Log + sauberer Abbruch statt Hard-Crash).
- Bei **"Include default content"** wird beim Projekt-Erstellen kein leeres `DefaultLevel.map` vorab gespeichert; stattdessen erzeugt `ensureDefaultAssetsCreated()` direkt ein befülltes Default-Level (Cubes + Point/Directional/Spot-Licht).
- **Nur wenn auch das Fallback-Projekt nicht geladen werden kann**, wird die Engine mit einem `FATAL`-Log heruntergefahren
- Alle Entscheidungspunkte (Temp-Fenster geschlossen ohne Auswahl, Fallback-Versuch, endgültiges Scheitern) werden geloggt
- **Alle Event-Pumps zwischen Startup-Auswahl und Main-Loop** (z.B. `showProgress`, "Engine ready"-Pump) ignorieren `SDL_EVENT_QUIT` weiterhin (`continue`), damit verspätete Quit-Events aus Fenster-Übergängen die Engine nicht vorzeitig beenden
- Vor dem Eintritt in die Main-Loop wird `resetShutdownRequest()` aufgerufen, um verwaiste Shutdown-Flags zu beseitigen
- Nur `TitleBar.Close` und `SDL_EVENT_QUIT` **innerhalb** der Main-Loop können die Engine ab diesem Punkt beenden
- Der Shutdown-Check in der Main-Loop (`isShutdownRequested()`) loggt jetzt den Grund bevor `running = false` gesetzt wird
- Gewähltes Projekt wird als `DefaultProject` in config.ini gespeichert
- Erfolgreich geladene/erstellte Projekte werden automatisch in die Known-Projects-Liste aufgenommen (max. 20 Einträge, zuletzt verwendete zuerst)
- Nicht mehr existierende Projekte werden in der Liste als "(not found)" angezeigt und sind nicht klickbar
- Bekannte Projekte werden mit abwechselnden Zeilenhintergründen, blauem Akzentstreifen links und vergrößerter Schrift dargestellt, um Einträge klar voneinander abzugrenzen

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
              └───────┬─────────────────┬──────────┘
                      │                 │
    ┌─────────────────▼───┐  ┌──────────▼─────────────┐
    │       Physics        │  │       Renderer          │
    │  PhysicsWorld (PIE)  │  │  ┌──────────────────┐   │
    │  Jolt Physics v5.5   │  │  │  OpenGLRenderer   │   │
    │  PhysX 5.6 (optional)│  │  │                   │   │
    │  Rigid Body, Raycast │  │  │  Kamera, Shader   │   │
    └──────────────────────┘  │  │  Material, Text   │   │
                              │  │  HZB, Picking     │   │
                              │  └──────────────────┘   │
                              │  ┌──────────────────┐   │
                              │  │    UI-System      │   │
                              │  │  UIManager, Elems │   │
                              │  │  FBO-Caching      │   │
                              │  └──────────────────┘   │
                              │  ┌──────────────────┐   │
                              │  │ RenderResource   │   │
                              │  │ Manager + Cache  │   │
                              │  └──────────────────┘   │
                              └─────────────────────────┘

Externe Bibliotheken:
  SDL3, FreeType, OpenAL Soft, GLAD, GLM, nlohmann/json, stb_image, Python 3, Jolt Physics, NVIDIA PhysX 5.6.1
```

---

## 15. Physik-System
**Dateien:** `src/Physics/PhysicsWorld.h/.cpp`, `src/Physics/IPhysicsBackend.h`, `src/Physics/JoltBackend.h/.cpp`, `src/Physics/PhysXBackend.h/.cpp`
**Backends:** Jolt Physics v5.5.1 (`external/jolt/`) + NVIDIA PhysX 5.6.1 (`external/PhysX/`, optional) via austauschbares `IPhysicsBackend`-Interface

- **Backend-Abstraktion**: `PhysicsWorld` ist backend-agnostisch und delegiert über `std::unique_ptr<IPhysicsBackend>`. `PhysicsWorld::Backend`-Enum (Jolt/PhysX) + `initialize(Backend)` für Backend-Auswahl.
- **JoltBackend** (`JoltBackend.h/.cpp`): Kapselt alle Jolt-spezifischen Typen. `EngineContactListener` für thread-safe Kollisionsereignisse. Broadphase-Layer (NON_MOVING/MOVING).
- **PhysXBackend** (`PhysXBackend.h/.cpp`): NVIDIA PhysX 5.6.1 Implementierung. `SimCallbackImpl` (`PxSimulationEventCallback`) für Kontakt-Callbacks. PxFoundation/PxPhysics/PxScene/PxPvd Lifecycle. Optional via `ENGINE_PHYSX_BACKEND` CMake-Option (`ENGINE_PHYSX_BACKEND_AVAILABLE` Define).
- Zwei ECS-Komponenten: `CollisionComponent` (Form/Oberfläche, erforderlich) + `PhysicsComponent` (Dynamik, optional → statisch wenn fehlend).
- Collider-Formen: Box, Sphere, Capsule, Cylinder, HeightField (Mesh fällt auf Box zurück). Collider-Offset via Backend.
- Fixed-Timestep-Akkumulator delegiert an `IPhysicsBackend::step()`.
- Backend übernimmt Kollisionserkennung, Impulsauflösung, Constraint-Solving und Sleep-Management.
- `syncBodiesToBackend()`: Erzeugt/aktualisiert Bodies via `IPhysicsBackend::createBody(BodyDesc)` aus ECS.
- `syncBodiesFromBackend()`: Liest `BodyState` (Position, Rotation, Velocity) via Backend zurück ins ECS (nur Dynamic).
- Body-Eigenschaften: gravityFactor, linearDamping, angularDamping, maxLinearVelocity, maxAngularVelocity, motionQuality (CCD), allowSleeping, isSensor.
- Entity-Tracking via `m_trackedEntities` (std::set) in PhysicsWorld.
- Raycast via `IPhysicsBackend::raycast()` → `RaycastResult`.
- Backward-Kompatibilität: `deserializeLegacyPhysics()` migriert alte Formate.
- Euler↔Quaternion-Konvertierung in beiden Backends mit Rotationsreihenfolge Y(Yaw)·X(Pitch)·Z(Roll).
- **PhysX CMake-Integration**: Statische Libs via `add_subdirectory(external/PhysX/physx/compiler/public)`. DLL-CRT-Override (`/MDd`/`/MD`) und `/WX-` für alle PhysX-Targets. Stub-freeglut für PUBLIC_RELEASE-Build. `CMAKE_CONFIGURATION_TYPES` Save/Restore um PhysX-Overrides zu isolieren.
- **HeightField-Kollision (Bugfixes)**:
  - **PhysX**: `BodyDesc.heightSampleCount` ist die Seitenlänge (nicht Gesamtanzahl) – direkter Einsatz statt `sqrtf()`. HeightField-Offset via `PxShape::setLocalPose()` angewandt (PhysX-HeightField beginnt bei (0,0,0)). Skalierungsreihenfolge korrigiert: `rowScale=Z`, `columnScale=X`. PxI16-Clamping für Höhenwerte.
  - **Jolt**: `HeightFieldShapeSettings` erfordert `sampleCount = 2^n + 1`. Bilineare Resampling-Logik für nicht-konforme Zählungen mit proportionaler Skalierungsanpassung. Fallback vermeidet fehlerhafte BoxShape-Ersetzung.

---

## 16. Landscape-System
**Dateien:** `src/Landscape/LandscapeManager.h/.cpp`

- Verwaltung von Terrain-Assets und Editor-Workflow (Popup, Import, Status).
- **HeightField Debug Wireframe**: Visualisiert das HeightField-Kollisionsgitter als grünes Wireframe-Overlay im Viewport. Toggle über Engine Settings → Debug → HeightField Debug. Automatischer Mesh-Rebuild bei ECS-Änderungen via `getComponentVersion()`. Nutzt den bestehenden `boundsDebugProgram`-Shader (GL_LINES, Identity-Model-Matrix, World-Space-Vertices). Persistenz über `config.ini` (`HeightFieldDebugEnabled`).
- **Grid-Größe**: `gridSize` wird auf die nächste Zweierpotenz aufgerundet, sodass `sampleCount = gridSize + 1` immer `2^n + 1` ergibt (Jolt-HeightField-Kompatibilität). Standard: gridSize=3 → aufgerundet auf 4 → sampleCount=5.

---

*Generiert aus dem Quellcode des Engine-Projekts. Stand: aktueller Branch `Json_and_ecs`.*
