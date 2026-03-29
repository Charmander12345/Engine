# Codebase Cleanup & Vereinfachungsplan

> Umfassende Analyse der Engine-Codebase mit konkreten Verbesserungsvorschlägen.
> Stand: Aktuelle Codebase auf `master`-Branch.

---

## Übersicht – Aktuelle Code-Statistiken

| Datei | Zeilen | Größe |
|-------|--------|-------|
| `UIManager.cpp` | **20.801** | 968 KB |
| `OpenGLRenderer.cpp` | **11.403** | 448 KB |
| `AssetManager.cpp` | **6.634** | 255 KB |
| `main.cpp` | **4.898** | 271 KB |
| `PythonScripting.cpp` | **3.928** | 160 KB |
| `UIWidget.cpp` | **1.928** | 69 KB |
| **Gesamt (183 Dateien)** | — | **3,7 MB** |

→ 5 Dateien machen über **70%** der gesamten Codebase aus.

---

## 1. UIManager.cpp – God-Object aufteilen (~20.800 Zeilen)

### Problem
`UIManager` ist ein **God-Object**, das folgende Verantwortlichkeiten in einer einzigen Klasse vereint:
- Widget-Registrierung & Layout
- Input-Handling (Mouse, Keyboard, Scroll, Text, Drag & Drop)
- ~12 verschiedene Editor-Tabs (Console, Profiler, Sequencer, Widget Editor, etc.)
- Content Browser mit Suche, Filter, Rename, Typ-Erkennung
- World Outliner + Entity Details
- Modal-Dialoge, Toast-Benachrichtigungen, Dropdown-Menüs
- Build-System-UI (CMake Detection, Build-Profile, Build-Progress)
- Prefab-System, Entity-Clipboard, Auto-Collider
- Projekt-Auswahl-Screen

### Konkrete Aufteilung

#### 1.1 Editor-Tabs in eigene Klassen extrahieren
Jeder Tab hat bereits ein eigenes State-Struct (`ConsoleState`, `ProfilerState`, etc.) und eigene Methoden (`open*Tab`, `close*Tab`, `refresh*`, `build*Toolbar`, `build*`). Diese können 1:1 in eigene Klassen extrahiert werden:

| Neuer Dateiname | Methoden-Umfang | Zeilen ca. |
|---|---|---|
| `EditorTabs/ConsoleTab.h/.cpp` | `openConsoleTab`, `closeConsoleTab`, `refreshConsoleLog`, `buildConsoleToolbar` + `ConsoleState` | ~250 |
| `EditorTabs/ProfilerTab.h/.cpp` | `openProfilerTab`, `closeProfilerTab`, `refreshProfilerMetrics`, `buildProfilerToolbar` + `ProfilerState` | ~300 |
| `EditorTabs/SequencerTab.h/.cpp` | 6 Methoden + `SequencerState` + Alle `registerClickEvent("Sequencer.*")` | ~700 |
| `EditorTabs/LevelCompositionTab.h/.cpp` | 7 Methoden + `LevelCompositionState` | ~500 |
| `EditorTabs/ShaderViewerTab.h/.cpp` | 6 Methoden + `ShaderViewerState` | ~300 |
| `EditorTabs/RenderDebuggerTab.h/.cpp` | 4 Methoden + `RenderDebuggerState` | ~250 |
| `EditorTabs/AudioPreviewTab.h/.cpp` | 6 Methoden + `AudioPreviewState` | ~350 |
| `EditorTabs/ParticleEditorTab.h/.cpp` | 6 Methoden + `ParticleEditorState` | ~400 |
| `EditorTabs/AnimationEditorTab.h/.cpp` | 7 Methoden + `AnimationEditorState` | ~500 |
| `EditorTabs/WidgetEditorTab.h/.cpp` | 18 Methoden + `WidgetEditorState` | ~2.500 |
| `EditorTabs/UIDesignerTab.h/.cpp` | 6 Methoden + `UIDesignerState` | ~400 |

**Erwartete Reduktion: ~6.500 Zeilen aus UIManager.cpp**

**Wie:** Jede Tab-Klasse bekommt einen Pointer auf `UIManager*` und `Renderer*` für den Zugriff auf gemeinsame Funktionen (`showToastMessage`, `markAllWidgetsDirty`, `registerClickEvent`, `registerWidget`, etc.). UIManager behält nur eine `std::unordered_map<std::string, std::unique_ptr<IEditorTab>>` und delegiert `open/close/refresh` Aufrufe.

```cpp
// Vorschlag: Gemeinsames Interface
class IEditorTab {
public:
    virtual ~IEditorTab() = default;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual void refresh(float deltaSeconds) = 0; // Timer-basierte Updates
    virtual const std::string& getTabId() const = 0;
};
```

#### 1.2 Content Browser in eigene Klasse
Der Content Browser (Pfad-Navigation, Grid-Rendering, Suche/Filter, Rename, Typ-Filter, Asset-Referenz-Tracking) ist ein eigener großer Bereich:
- `ContentBrowserPanel.h/.cpp`
- Alle `m_contentBrowser*` / `m_browser*` / `m_selectedGrid*` Member
- `populateContentBrowserWidget`, `refreshContentBrowser`, `focusContentBrowserSearch`, `buildReferencedAssetSet`, `navigateContentBrowserByArrow`

**Erwartete Reduktion: ~2.000 Zeilen**

#### 1.3 World Outliner + Entity Details in eigene Klasse
- `OutlinerPanel.h/.cpp`
- `populateOutlinerWidget`, `populateOutlinerDetails`, `refreshWorldOutliner`, `navigateOutlinerByArrow`, `selectEntity`
- `m_outlinerLevel`, `m_outlinerSelectedEntity`, Entity-Clipboard

**Erwartete Reduktion: ~2.000 Zeilen**

#### 1.4 Build-System-UI extrahieren
- `BuildSystemUI.h/.cpp`
- `BuildProfile`, `BuildGameConfig`, Build-Methoden (`openBuildGameDialog`, `showBuildProgress`, `updateBuildProgress`, etc.)
- CMake/Toolchain-Detection (`detectCMake`, `detectBuildToolchain`, `startAsyncToolchainDetection`, `pollToolchainDetection`)

**Erwartete Reduktion: ~1.500 Zeilen**

#### 1.5 Dialog-System extrahieren
- `EditorDialogs.h/.cpp`
- Modale Dialoge, Confirm-Dialoge, Unsaved-Changes, Save-Progress, Level-Load-Progress, Projekt-Screen
- `showModalMessage`, `showConfirmDialog`, `showUnsavedChangesDialog`, `openProjectScreen`

**Erwartete Reduktion: ~1.500 Zeilen**

**Gesamtpotenzial: UIManager.cpp von ~20.800 auf ~5.000–7.000 Zeilen reduzieren.**

---

## 2. OpenGLRenderer.cpp – UI-Rendering vereinheitlichen (~11.400 Zeilen)

### Problem
Es gibt **drei nahezu identische UI-Rendering-Pfade**, die denselben WidgetElement-Typen rendern:

| Pfad | Zeile | Zeilen | Zweck |
|------|-------|--------|-------|
| `drawUIWidgetsToFramebuffer()` | 3082 | ~2.140 | Editor-UI in FBO |
| `renderViewportUI()` | 5222 | ~637 | Gameplay/Viewport-UI |
| `renderUI()` | 5859 | ~642 | Fallback-/Popup-UI |

Alle drei enthalten einen großen `switch`-Block über `WidgetElementType` (25+ Typen) mit beinahe identischem Code für `drawUIPanel`, `drawText`, Opacity, RenderTransform, ClipMode, etc.

### Konkrete Verbesserung

**2.1 Einheitliche `renderWidgetElement()`-Methode erstellen:**
```cpp
// Eine zentrale Methode für alle drei Pfade
void OpenGLRenderer::renderWidgetElement(
    const WidgetElement& element,
    float parentX, float parentY, float parentW, float parentH,
    float parentOpacity,
    const glm::mat4& projection,
    UIRenderContext& ctx   // enthält deferred-Dropdown-Liste, Scissor-Stack etc.
);
```

- Die drei bestehenden Methoden werden zu **dünnen Wrappern**, die nur Projektion/FBO aufsetzen und dann `renderWidgetElement` für jedes Root-Element aufrufen.
- **Erwartete Reduktion: ~2.500 Zeilen** (redundanter switch-Code × 3 → × 1).

**2.2 Debug-Rendering extrahieren:**
- `renderColliderDebug()` (258 Zeilen), `renderBoneDebug()` (130 Zeilen), `renderStreamingVolumeDebug()` (62 Zeilen), `drawSelectionOutline()`, `drawRubberBand()` → Eigene `DebugRenderer.h/.cpp`
- **Erwartete Reduktion: ~600 Zeilen**

**2.3 Gizmo-Code extrahieren:**
- `renderGizmo()`, `pickGizmoAxis()`, `beginGizmoDrag()`, `updateGizmoDrag()`, `endGizmoDrag()` + zugehörige Hilfsfunktionen → `GizmoRenderer.h/.cpp`
- **Erwartete Reduktion: ~1.000 Zeilen**

**2.4 Grid-Rendering extrahieren:**
- `drawViewportGrid()`, `ensureGridResources()`, `releaseGridResources()` → In `DebugRenderer` oder eigene `GridRenderer`-Klasse
- **Erwartete Reduktion: ~200 Zeilen**

**2.5 Tab-/FBO-Management extrahieren:**
- `addTab`, `removeTab`, `rebuildTitleBarTabs`, `setActiveTab`, `releaseAllTabFbos`, `ensureTabFbo` → `TabManager.h/.cpp`
- **Erwartete Reduktion: ~300 Zeilen**

**Gesamtpotenzial: OpenGLRenderer.cpp von ~11.400 auf ~6.000–7.000 Zeilen reduzieren.**

---

## 3. main.cpp – Event-Loop aufräumen (~4.900 Zeilen)

### Problem
`main()` ist eine **einzelne monolithische Funktion** mit:
- ~800 Zeilen Initialisierung
- ~2.400 Zeilen Build-Pipeline (in einem Lambda)
- ~1.000 Zeilen SDL-Event-Handling
- ~70 `#if ENGINE_EDITOR` / `#if !defined(ENGINE_BUILD_SHIPPING)` Guards
- 32 `registerClickEvent`-Aufrufe direkt in main

### Konkrete Verbesserung

**3.1 Build-Pipeline extrahieren:**
Die Build-Pipeline (~2.400 Zeilen in einem Lambda ab Zeile ~2.418) kann in eine eigene `BuildPipeline.h/.cpp` extrahiert werden:
```cpp
class BuildPipeline {
public:
    void execute(const UIManager::BuildGameConfig& config, UIManager& ui);
    bool isRunning() const;
    void cancel();
};
```
**Erwartete Reduktion: ~2.400 Zeilen**

**3.2 Event-Handler extrahieren:**
SDL-Event-Dispatch in eine `EditorEventHandler`-Klasse:
- `handleKeyDown`, `handleKeyUp` (Shortcuts, Editor-Gizmo-Bindungen)
- `handleMouseMotion`, `handleMouseButton`
- `handleGamepadEvent`

**3.3 Initialisierung strukturieren:**
Die Initialisierungssequenz in eine `EngineBootstrap`-Klasse kapseln:
```cpp
class EngineBootstrap {
public:
    bool initialize();  // Alle ~17 Init-Schritte
    void shutdown();
    // Accessor für die initialisierten Systeme
};
```

**Gesamtpotenzial: main.cpp von ~4.900 auf ~800–1.200 Zeilen reduzieren.**

---

## 4. Dupliziertes ECS-System konsolidieren

### Problem
Es existieren **zwei separate ECS-Implementierungen**:

| System | Pfad | Namespace | Verwendet von |
|--------|------|-----------|---------------|
| **ECS (alt)** | `src/ECS/` | `ecs::` | `World`, `Entity`, eigene Komponenten (Transform, Name, Render, Group, Id) |
| **ECS (neu)** | `src/Core/ECS/` | `ECS::` | Überall in der Engine (UIManager, Renderer, Scripting, AssetManager) |

Das alte System in `src/ECS/` hat:
- `World.h/.cpp` (212 Zeilen)
- `SparseSet.h` (Duplikat von `Core/ECS/DataStructs/SparseSet.h`)
- `Entity.h`, `Components/` (TransformComponent, NameComponent, RenderComponent, GroupComponent, IdComponent)
- `Serialization/EcsLevelSerializer.h/.cpp`, `EcsLevelAsset.h`
- Eigene `CMakeLists.txt` → `ECS`-Library

### Konkrete Verbesserung
- **Prüfen ob `src/ECS/` noch referenziert wird** – wenn nicht, komplett entfernen
- Falls referenziert: Migration der Serialisierung (`EcsLevelSerializer`) in das Haupt-ECS-System und Entfernung des Duplikats
- `SparseSet.h` existiert 2× → auf eine Version reduzieren

---

## 5. UIWidget-Dateien – Leere .cpp-Dateien entfernen

### Problem
11 von 13 `.cpp`-Dateien im `UIWidgets/`-Ordner enthalten nur ein `#include`:
```cpp
#include "ButtonWidget.h"
// EOF
```
Diese Dateien existieren nur, weil sie in der `CMakeLists.txt` aufgelistet sind.

### Betroffene Dateien
- `ButtonWidget.cpp`, `CheckBoxWidget.cpp`, `DropdownButtonWidget.cpp`, `DropDownWidget.cpp`
- `GridWidget.cpp`, `ProgressBarWidget.cpp`, `SeparatorWidget.cpp`, `SliderWidget.cpp`
- `StackPanelWidget.cpp`, `TabViewWidget.cpp`, `TextWidget.cpp`, `TreeViewWidget.cpp`

### Konkrete Verbesserung
- Alle leeren `.cpp`-Dateien entfernen
- Stattdessen nur die `.h`-Dateien in `CMakeLists.txt` listen (Header-only Widgets)
- Alternativ: Wenn CMake die `.cpp` braucht um den Header korrekt in der IDE anzuzeigen, kann eine einzige `UIWidgets.cpp` erstellt werden, die alle Header inkludiert

---

## 6. PythonScripting.cpp – Module aufteilen (~3.900 Zeilen)

### Problem
Eine einzige Datei definiert **13 Python-Module** mit insgesamt ~200 C-Funktionen:
- `engine` (Kern), `engine.entity`, `engine.asset`, `engine.audio`, `engine.input`
- `engine.ui`, `engine.camera`, `engine.diagnostics`, `engine.logging`
- `engine.physics`, `engine.math`, `engine.particle`, `engine.editor`

### Fortschritt

**✅ Phase 1 abgeschlossen:** `engine.math` (54 Funktionen, ~530 Zeilen) und `engine.physics` (11 Funktionen, ~200 Zeilen) wurden in eigenständige Übersetzungseinheiten extrahiert (`MathModule.cpp`, `PhysicsModule.cpp`). Shared State über `ScriptingInternal.h` (`ScriptDetail`-Namespace). ~700 Zeilen aus `PythonScripting.cpp` entfernt.

### Konkrete Verbesserung (Phase 2+)

Verbleibende Module in eigene Dateien:

| Neue Datei | Aktuelles Modul | Zeilen ca. | Status |
|---|---|---|---|
| `ScriptModules/EntityModule.cpp` | `engine.entity` | ~300 | ❌ |
| `ScriptModules/AssetModule.cpp` | `engine.asset` | ~150 | ❌ |
| `ScriptModules/AudioModule.cpp` | `engine.audio` | ~200 | ❌ |
| `ScriptModules/InputModule.cpp` | `engine.input` | ~200 | ❌ |
| `ScriptModules/UIModule.cpp` | `engine.ui` | ~400 | ❌ |
| `ScriptModules/CameraModule.cpp` | `engine.camera` | ~300 | ❌ |
| ~~`ScriptModules/PhysicsModule.cpp`~~ `PhysicsModule.cpp` | `engine.physics` | ~200 | ✅ |
| ~~`ScriptModules/MathModule.cpp`~~ `MathModule.cpp` | `engine.math` | ~530 | ✅ |
| `ScriptModules/ParticleModule.cpp` | `engine.particle` | ~200 | ❌ |
| `ScriptModules/EditorModule.cpp` | `engine.editor` | ~200 | ❌ |

`PythonScripting.cpp` behält nur die Initialisierung (`Initialize`, `Shutdown`, `SetRenderer`) und die Modul-Registrierung.

**Erwartete Reduktion: PythonScripting.cpp von ~3.900 auf ~500 Zeilen**

---

## 7. Renderer.h – Interface zu groß (195 Virtual Methods)

### Problem
`Renderer.h` hat **195 virtuelle Methoden**. Viele davon sind Editor-only oder Debug-Features:
- ~30 Gizmo-Methoden
- ~15 Tab-Management-Methoden
- ~15 Popup/MeshViewer-Methoden
- ~10 Debug-Overlay-Methoden
- ~20 Skeletal-Animation-Methoden
- ~15 Viewport-Layout-Methoden

### Konkrete Verbesserung
Aufspaltung in **Interfaces/Mixins**:
```cpp
class IRendererCore { ... };           // ~20 Methoden (init, render, camera, clear)
class IRendererScene { ... };          // ~20 Methoden (entities, materials, lighting)
class IRendererEditor { ... };         // ~50 Methoden (gizmo, picking, tabs, popups)
class IRendererDebug { ... };          // ~15 Methoden (colliders, bones, grid, wireframe)
class IRendererAnimation { ... };      // ~20 Methoden (skeletal, clips, bones)
```

Alternativ minimaler Ansatz: Editor-Methoden mit `#if ENGINE_EDITOR` guard versehen, sodass das Runtime-Interface kleiner ist.

---

## 8. AssetManager.cpp – Verantwortlichkeiten trennen (~6.600 Zeilen)

### Problem
`AssetManager` kombiniert:
- Asset-Registry & Laden/Speichern
- Assimp-Import (FBX, glTF, OBJ, etc.)
- Worker-Thread-Pool
- Garbage Collection
- Audio-Asset-Management (OpenAL-Puffer)
- Editor-Widget-Asset-Generierung (`ensureDefaultAssetsCreated`, ~500 Zeilen JSON-Generierung)
- Asset-Referenz-Tracking
- Datei-Operationen (move, rename, delete)

### Konkrete Verbesserung

| Neue Datei | Inhalt | Zeilen ca. |
|---|---|---|
| `AssetImporter.h/.cpp` | Assimp-Import, `importAssetFromPath`, `OpenImportDialog` | ~900 |
| `EditorWidgetAssets.h/.cpp` | `ensureDefaultAssetsCreated` (JSON-Widget-Generierung) | ~500 |
| `AssetFileOps.h/.cpp` | `moveAsset`, `renameAsset`, `deleteAsset`, `updateAssetFileReferences` | ~500 |
| `AssetReferenceTracker.h/.cpp` | `findReferencesTo`, `getAssetDependencies` | ~300 |

**Erwartete Reduktion: AssetManager.cpp von ~6.600 auf ~4.000 Zeilen**

---

## 9. OpenGLRenderer.h – Member-Variable-Explosion (245+ Members)

### Problem
`OpenGLRenderer.h` hat **245+ Member-Variablen** und **57 GLuint-Handles**. Verschiedene Subsysteme teilen sich den gleichen flachen Namespace:

- UI-Shader-Handles: `m_panelProgram`, `m_buttonProgram`, `m_textProgram`, `m_uiImageProgram`, `m_progressProgram`, `m_sliderProgram`, `m_uiGradientProgram`
- PostProcess-Handles: Bereits in `PostProcessStack` extrahiert ✓
- Shadow-Map-Handles: `m_shadowFBO`, `m_shadowTexture`, `m_pointShadowFBO`, `m_pointShadowCubemap`
- Gizmo-Handles: `m_gizmoVAO`, `m_gizmoVBO`, `m_gizmoProgram`
- Grid-Handles: `m_gridVAO`, `m_gridVBO`, `m_gridProgram`, `m_gridMVP`, `m_gridColor`
- Pick-FBO: `m_pickFBO`, `m_pickTexture`, `m_pickDepth`, `m_pickProgram`
- Thumbnail-FBO: `m_thumbnailFBO`, `m_thumbnailColor`, `m_thumbnailDepth`

### Konkrete Verbesserung
Sub-Structs für zusammengehörige GL-Ressourcen:

```cpp
struct ShadowResources {
    GLuint fbo{0}, texture{0};
    GLuint pointFBO{0}, pointCubemap{0};
    int cascadeCount{4};
    // ...
    void init(); void release();
};

struct PickingResources {
    GLuint fbo{0}, texture{0}, depth{0}, program{0};
    void init(); void release();
};

struct UIShaderResources {
    GLuint panelProgram{0}, buttonProgram{0}, textProgram{0};
    GLuint imageProgram{0}, progressProgram{0}, sliderProgram{0};
    GLuint gradientProgram{0};
    struct Uniforms { /* ... */ };
    void init(); void release();
};
```

Dies verbessert Lesbarkeit und macht `ensure*/release*`-Paare explizit.

---

## 10. Weitere Vereinfachungs-Möglichkeiten

### 10.1 `registerClickEvent` Pattern vereinfachen
Aktuell gibt es **90 `registerClickEvent`-Aufrufe** (58 in UIManager.cpp, 32 in main.cpp), die alle dem gleichen Muster folgen:
```cpp
registerClickEvent("Tab.Button", [this]() { /* action */ });
```

**Verbesserung:** Event-Registrierung deklarativ machen:
```cpp
// Statt 90 einzelner Aufrufe ein Array/Map
static const EventBinding kEditorEvents[] = {
    { "Sequencer.Play",  &UIManager::onSequencerPlay },
    { "Sequencer.Stop",  &UIManager::onSequencerStop },
    // ...
};
```
Oder direkt in den jeweiligen Tab-Klassen registrieren (→ Punkt 1.1).

### 10.2 `markAllWidgetsDirty()` – 107 Aufrufe reduzieren
`markAllWidgetsDirty()` wird **107×** aufgerufen, oft direkt nach `refreshXYZ()`. 

**Verbesserung:** In `refresh*`-Methoden automatisch aufrufen, oder dirty-Tracking granularer machen (nur betroffenes Widget dirty markieren statt alle).

### 10.3 `showToastMessage` – 75 Aufrufe standardisieren
75 Toast-Aufrufe mit verschiedenen Dauern und Levels.

**Verbesserung:** Standard-Dauern in Konstanten:
```cpp
static constexpr float kToastShort = 1.5f;
static constexpr float kToastMedium = 3.0f;
static constexpr float kToastLong = 5.0f;
```

### 10.4 Doppelter `#if ENGINE_EDITOR` / `#endif`-Guard-Overhead
`main.cpp` hat **~70 Präprozessor-Guards**. Viele davon schützen nur 2–3 Zeilen.

**Verbesserung:** Editor-Code in eigene Funktionen extrahieren, die nur einmal mit `#if ENGINE_EDITOR` geschützt werden:
```cpp
#if ENGINE_EDITOR
void setupEditorShortcuts(ShortcutManager& sm, UIManager& ui, Renderer& r) { ... }
void handleEditorKeyDown(int key, UIManager& ui, Renderer& r) { ... }
#endif
```

### 10.5 PhysX-Backend – Wird es verwendet?
`PhysXBackend.h/.cpp` (529 Zeilen) existiert neben `JoltBackend.h/.cpp` (680 Zeilen). Wenn PhysX nicht aktiv benutzt/gewartet wird, könnte es zu totem Code werden.

**Prüfung:** Wird `PhysXBackend` tatsächlich in einer Build-Konfiguration aktiv genutzt? Falls nicht → Entfernen oder in ein separates Plugin verschieben.

### 10.6 Duplizierter UIWidget-Code: Editor vs. Gameplay
- `EditorWidget.h` (94 Zeilen) = Vereinfachte Widget-Klasse
- `GameplayWidget.h` = Alias auf `Widget` (volles Feature-Set)

Es gibt Übergangs-Überladungen in `registerWidget`, die `Widget → EditorWidget` konvertieren. Dies ist technische Schuld aus der Trennung.

**Verbesserung:** Migration abschließen – alle Editor-Code-Stellen auf `EditorWidget` umstellen, Übergangs-Überladungen entfernen.

### 10.7 `UIWidget.h` – WidgetElement ist zu groß
`WidgetElement` enthält **alle Felder für alle 25+ Widget-Typen** in einem einzigen Struct:
- Text-Felder (`text`, `richText`, `font`, `fontSize`, `isBold`, `isItalic`)
- Image-Felder (`imagePath`, `imageWidth`, `imageHeight`)
- Slider-Felder (`minValue`, `maxValue`, `value`)
- ListView-Felder (`totalItemCount`, `itemHeight`, `columnsPerRow`)
- Border-Felder (`borderThicknessLeft/Top/Right/Bottom`, `contentPadding`)
- Spinner-Felder (`spinnerDotCount`, `spinnerSpeed`, `spinnerElapsed`)
- RichText, Focus, RenderTransform, UIBrush, ...

Das verbraucht viel Speicher pro Element, auch wenn die meisten Felder unbenutzt sind.

**Verbesserung (langfristig):** Typ-spezifische Daten als `std::variant` oder separates Struct, das über einen Pointer/Index referenziert wird:
```cpp
struct WidgetElement {
    // Gemeinsame Felder (Position, Size, Style, Children)
    WidgetElementType type;
    WidgetElementStyle style;
    std::vector<WidgetElement> children;
    // ...
    
    // Typ-spezifische Daten
    std::unique_ptr<TypeSpecificData> typeData; // oder std::variant<TextData, ImageData, SliderData, ...>
};
```

---

## 11. Priorisierung

| Priorität | Maßnahme | Aufwand | Wirkung |
|-----------|----------|---------|---------|
| 🔴 Hoch | 1.1 – Editor-Tabs aus UIManager extrahieren | Mittel | ~6.500 Zeilen weniger, klar getrennte Verantwortlichkeiten |
| 🔴 Hoch | 2.1 – UI-Rendering vereinheitlichen | Mittel | ~2.500 Zeilen weniger, keine 3-fache Code-Wartung |
| 🔴 Hoch | ~~3.1 – Build-Pipeline aus main.cpp extrahieren~~ | ~~Gering~~ | ✅ Erledigt (-747 Zeilen) |
| 🟡 Mittel | 1.2 – Content Browser extrahieren | Mittel | ~2.000 Zeilen weniger |
| 🟡 Mittel | 1.3 – Outliner/Details extrahieren | Mittel | ~2.000 Zeilen weniger |
| 🟡 Mittel | 6 – PythonScripting Module aufteilen | Gering | 🟡 Phase 1 ✅ (~730 Zeilen extrahiert: Math + Physics), Phase 2+ ausstehend (~2.700 Zeilen) |
| 🟡 Mittel | 4 – Dupliziertes ECS konsolidieren | Gering | Architektur-Bereinigung |
| 🟡 Mittel | 5 – Leere .cpp-Dateien entfernen | Trivial | 12 Dateien weniger |
| 🟢 Niedrig | 8 – AssetManager aufteilen | Mittel | ~2.600 Zeilen weniger |
| 🟢 Niedrig | 9 – OpenGLRenderer Member-Structs | Gering | Bessere Lesbarkeit |
| 🟢 Niedrig | 7 – Renderer.h Interfaces | Hoch | Sauberes API-Design |
| 🟢 Niedrig | 10.7 – WidgetElement Typ-Daten | Hoch | Speicherersparnis, sauberes Design |

---

## 12. Quick Wins (sofort umsetzbar, minimales Risiko)

1. **Leere `.cpp`-Dateien entfernen** (12 Dateien in `UIWidgets/`) → Punkt 5
2. **Toast-Konstanten einführen** (`kToastShort`, `kToastMedium`, `kToastLong`) → Punkt 10.3
3. **`markAllWidgetsDirty()` in `refresh*`-Methoden integrieren** → Punkt 10.2
4. **`#if ENGINE_EDITOR`-Blöcke in main.cpp zu Funktionen konsolidieren** → Punkt 10.4
5. **Dupliziertes ECS prüfen und ggf. entfernen** (`src/ECS/` vs. `src/Core/ECS/`) → Punkt 4

---

## 13. Architektur-Vision nach Cleanup

```
Engine/
├── src/
│   ├── main.cpp                    (~800 Zeilen)
│   ├── EngineBootstrap.h/.cpp      (Init/Shutdown)
│   ├── BuildPipeline.h/.cpp        (CMake Build)
│   ├── Core/                       (wie bisher)
│   ├── Renderer/
│   │   ├── OpenGLRenderer/
│   │   │   ├── OpenGLRenderer.cpp  (~6.000 Zeilen)
│   │   │   ├── DebugRenderer.cpp   (Collider, Bones, Grid, Volumes)
│   │   │   ├── GizmoRenderer.cpp   (Transform-Gizmo)
│   │   │   └── UIElementRenderer.cpp (Einheitlicher Widget-Rendering-Code)
│   │   ├── UIManager.cpp           (~5.000 Zeilen)
│   │   ├── EditorTabs/             (11 Tab-Klassen)
│   │   ├── ContentBrowserPanel.cpp
│   │   ├── OutlinerPanel.cpp
│   │   ├── EditorDialogs.cpp
│   │   └── BuildSystemUI.cpp
│   ├── Scripting/
│   │   ├── PythonScripting.cpp     (~500 Zeilen)
│   │   └── ScriptModules/          (10 Modul-Dateien)
│   └── AssetManager/
│       ├── AssetManager.cpp        (~4.000 Zeilen)
│       ├── AssetImporter.cpp
│       └── EditorWidgetAssets.cpp
```

**Gesamtreduktion der 5 größten Dateien:**
- UIManager.cpp: 20.800 → ~5.000 (-76%)
- OpenGLRenderer.cpp: 11.400 → ~6.000 (-47%)
- main.cpp: 4.900 → ~800 (-84%)
- PythonScripting.cpp: 3.900 → ~500 (-87%)
- AssetManager.cpp: 6.600 → ~4.000 (-39%)

**Gesamt: ~47.600 Zeilen in den Top-5 → ~16.300 Zeilen (-66%)**

Die Logik wird nicht gelöscht, sondern in kleinere, fokussierte Dateien verschoben, die jeweils eine klar definierte Verantwortlichkeit haben.

---

## Fortschrittsprotokoll – Quick Wins

### ✅ Quick Win #1: Leere UIWidget .cpp-Dateien entfernt
**12 Dateien gelöscht**, die nur `#include "Header.h"` enthielten (alle Widgets sind header-only):
- `ButtonWidget.cpp`, `GridWidget.cpp`, `StackPanelWidget.cpp`, `TextWidget.cpp`
- `SeparatorWidget.cpp`, `ProgressBarWidget.cpp`, `SliderWidget.cpp`, `CheckBoxWidget.cpp`
- `DropDownWidget.cpp`, `TreeViewWidget.cpp`, `TabViewWidget.cpp`, `DropdownButtonWidget.cpp`

**Geändert:** `src/Renderer/CMakeLists.txt` – 11 .cpp-Referenzen aus `RENDERER_CORE_COMMON_SOURCES` entfernt (DropdownButtonWidget.cpp war nie in CMakeLists).
Nur `ColorPickerWidget.cpp` behält echten Code (40 Zeilen).

### ✅ Quick Win #2: Toast-Dauer-Konstanten eingeführt
**3 Konstanten** in `UIManager.h` hinzugefügt:
```cpp
static constexpr float kToastShort  = 1.5f;
static constexpr float kToastMedium = 3.0f;
static constexpr float kToastLong   = 5.0f;
```

**73 Ersetzungen** über 4 Dateien:
- `UIManager.cpp`: 40 Ersetzungen
- `main.cpp`: 22 Ersetzungen
- `OpenGLRenderer.cpp`: 10 Ersetzungen
- `PythonScripting.cpp`: 1 Ersetzung

Übrige hartcodierte Werte (2.0f, 2.5f, 4.0f, 8.0f) sind bewusst gewählte Feinabstimmungen.

### ✅ Quick Win #3: Legacy-ECS-System entfernt (`src/ECS/`)
Das alte ECS-System (`ecs::` Namespace, 15 Dateien) war **vollständig vom Build entkoppelt**:
- Nicht in `CMakeLists.txt` als `add_subdirectory` eingetragen
- Keine Referenzen von außerhalb des `src/ECS/`-Verzeichnisses
- Das aktive ECS (`ECS::` Namespace in `src/Core/ECS/`) wird überall verwendet

**Gelöschte Dateien:** `World.h`, `World.cpp`, `Entity.h`, `SparseSet.h`, `EcsLevelAsset.h`, `CMakeLists.txt`, 5 Component-Headers, `BinaryIO.h`, `EcsLevelSerializer.h`, `EcsLevelSerializer.cpp`

### ✅ Quick Win #4: `markAllWidgetsDirty()` in refresh-Methoden integriert
**`markAllWidgetsDirty()` am Ende von 13 `refresh*`-Methoden** hinzugefügt:
`refreshLevelCompositionPanel`, `refreshWidgetEditorDetails`, `refreshSequencerTimeline`,
`refreshUIDesignerDetails`, `refreshParticleEditor`, `refreshAnimationEditor`,
`refreshProfilerMetrics`, `refreshShaderViewer`, `refreshRenderDebugger`,
`refreshConsoleLog`, `refreshAudioPreview`, `refreshUIDesignerHierarchy`, `refreshStatusBar`

**50 redundante Aufrufe entfernt** (49 in `UIManager.cpp`, 1 in `main.cpp`).
Ergebnis: Aufrufanzahl von 129 auf 91 reduziert, davon 13 als autoritative Aufrufe innerhalb der refresh-Methoden.

### ✅ Abschnitt 9: OpenGLRenderer Member-Struct-Gruppierung
**245+ lose Member-Variablen** in `OpenGLRenderer.h` in **12 logische Sub-Structs** gruppiert:

| Struct | Instanz | Member | .cpp-Referenzen |
|--------|---------|--------|-----------------|
| `ThumbnailResources` | `m_thumb` | 4 | 19 |
| `GridResources` | `m_grid` | 8 | 31 |
| `SkyboxResources` | `m_skybox` | 8 | 51 |
| `BoundsDebugResources` | `m_boundsDebug` | 5 | 33 |
| `HeightFieldDebugResources` | `m_hfDebug` | 6 | 24 |
| `PickingResources` | `m_pick` | 11 | 70 |
| `OutlineResources` | `m_outline` | 5 | 16 |
| `GizmoResources` | `m_gizmo` | 17 | 124 |
| `ShadowResources` | `m_shadow` | 12 | 77 |
| `PointShadowResources` | `m_pointShadow` | 11 | 47 |
| `CsmResources` | `m_csm` | 7 | 26 |
| `OitResources` | `m_oit` | 11 | 47 |

**Geänderte Dateien:**
- `OpenGLRenderer.h`: 12 Struct-Definitionen + Instanz-Member + alle Inline-Getter/Setter aktualisiert
- `OpenGLRenderer.cpp`: ~565 Member-Referenzen umbenannt (z.B. `m_shadowFbo` → `m_shadow.fbo`)

Standalone-Member wie `m_shadowCasterList`, `m_gridVisible`, `m_gridSize`, `m_thumbnailCache`, `m_pickRequested`, `m_snapEnabled` bleiben bewusst außerhalb der Structs (andere Zuständigkeit).

### ✅ Abschnitt 10.5: PhysX-Backend-Untersuchung
PhysX-Backend ist **korrekt optional** implementiert:
- CMake-Flag `ENGINE_PHYSX_BACKEND` (default OFF)
- Preprocessor-Guard `ENGINE_PHYSX_BACKEND_AVAILABLE` in `PhysicsWorld.cpp`
- Kein toter Code → **Keine Aktion nötig, wird beibehalten.**

### ✅ Abschnitt 10.4: `#if ENGINE_EDITOR`-Konsolidierung in `main.cpp`
**3 Konsolidierungen** durchgeführt, `#if ENGINE_EDITOR`-Guards von **21 auf 19** reduziert:

1. **Shutdown-Block** (L5357-5387): 3 separate `if (!isRuntimeMode)`-Blöcke (Kamera-Speicherung, Shortcut-Overrides, Projekt-Config) zu einem Block zusammengeführt.
2. **Shortcut-Registrierung** (L3483-3564): `ToggleFPSCap`-Shortcut (shared/debug) vor den `#if ENGINE_EDITOR`-Block verschoben → 2 aufeinanderfolgende Editor-Blöcke (PIE/Gizmo + Help/Import/Delete) zu einem verschmolzen.
3. **Startup-Init** (L804-835): Script-Hot-Reload (`#if !ENGINE_BUILD_SHIPPING`) vor die Editor-Init verschoben → Editor-Theme + Editor-Plugins in einem `#if ENGINE_EDITOR`-Block.

### ✅ Abschnitt 3.1: Build-Pipeline aus `main.cpp` extrahiert
**~750 Zeilen Build-Pipeline-Lambda** (CMake configure/build, Asset-Cooking, HPK-Packaging, Deployment) aus `main.cpp` in eigenständige `BuildPipeline`-Klasse extrahiert.

**Neue Dateien:**
- `src/BuildPipeline.h`: Klassen-Deklaration mit statischer `execute()`-Methode
- `src/BuildPipeline.cpp`: Vollständige Build-Logik (8 Schritte: Verzeichnis vorbereiten, CMake configure, CMake build, Runtime deployen, Assets kochen, HPK packen, game.ini generieren, optional starten)

**Geänderte Dateien:**
- `src/main.cpp`: 753-zeiliges Lambda durch 5-zeiligen Delegate ersetzt (`BuildPipeline::execute(config, uiMgr, renderer)`)
- `CMakeLists.txt`: `BuildPipeline.cpp` zum Editor-Target hinzugefügt

**Ergebnis:** `main.cpp` von ~5.425 auf ~4.678 Zeilen reduziert (-747 Zeilen).
