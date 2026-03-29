# Codebase Cleanup & Vereinfachungsplan

> Umfassende Analyse der Engine-Codebase mit konkreten Verbesserungsvorschlägen.
> Stand: Aktuelle Codebase auf `master`-Branch.

---

## Übersicht – Aktuelle Code-Statistiken

| Datei | Zeilen | Größe |
|-------|--------|-------|
| `UIManager.cpp` | **~13.100** | ~580 KB |
| `OpenGLRenderer.cpp` | **9.906** | ~385 KB |
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

#### 1.1 Editor-Tabs in eigene Klassen extrahieren ✅ ABGESCHLOSSEN

**Ergebnis:** 11 Editor-Tabs + 1 Interface in `src/Renderer/EditorTabs/` extrahiert. UIManager.cpp von ~22.100 auf ~14.450 Zeilen reduziert (~7.650 Zeilen, ~35% Reduktion).

| Datei | Zeilen entfernt | Beschreibung |
|---|---|---|
| `IEditorTab.h` | — | Interface (open/close/isOpen/update/getTabId) |
| `ConsoleTab.h/.cpp` | ~478 | Log-Viewer mit Filter/Suche |
| `ProfilerTab.h/.cpp` | ~548 | Performance-Monitor |
| `AudioPreviewTab.h/.cpp` | ~630 | Audio-Asset-Vorschau |
| `ParticleEditorTab.h/.cpp` | ~570 | Partikel-Parameter-Editor |
| `ShaderViewerTab.h/.cpp` | ~631 | Shader-Quellcode-Anzeige |
| `RenderDebuggerTab.h/.cpp` | ~527 | Render-Pipeline-Debugger |
| `SequencerTab.h/.cpp` | ~586 | Kamera-Sequenz-Editor |
| `LevelCompositionTab.h/.cpp` | ~538 | Level-Kompositions-Übersicht |
| `AnimationEditorTab.h/.cpp` | ~475 | Skelett-Animations-Editor |
| `UIDesignerTab.h/.cpp` | ~983 | Viewport-UI-Designer |
| `WidgetEditorTab.h/.cpp` | ~2843 | Widget-Editor (Multi-Instanz, State-Map, kein IEditorTab) |

Jeder Tab hält `UIManager*` + `Renderer*`. UIManager behält Thin-Wrapper-Delegationsmethoden.

#### 1.2 Content Browser in eigene Klasse ✅ ABGESCHLOSSEN

**Ergebnis:** Content Browser in `src/Renderer/EditorTabs/ContentBrowserPanel.h/.cpp` extrahiert. UIManager.cpp von ~14.450 auf ~13.100 Zeilen reduziert (~1.350 Zeilen).

| Aspekt | Details |
|---|---|
| `ContentBrowserPanel.h/.cpp` | ~1.400 Zeilen, eigene Klasse mit `UIManager*` + `Renderer*` |
| Extrahierte Methoden | `populateWidget`, `refresh`, `focusSearch`, `buildReferencedAssetSet`, `navigateByArrow`, `isOverGrid` |
| Extrahierte Member | 9 Variablen (`m_contentBrowserPath`, `m_selectedBrowserFolder`, `m_selectedGridAsset`, `m_expandedFolders`, `m_browserSearchText`, `m_browserTypeFilter`, `m_registryWasReady`, `m_renamingGridAsset`, `m_renameOriginalPath`) |
| Statische Helfer | `iconForAssetType`, `iconTintForAssetType`, `resolveTextureSourcePath`, `makeGridTile`, `makeTreeRow` (dupliziert, Shared mit Outliner) |
| Neue UIManager-APIs | `getRegisteredWidgetsMutable()`, `requestLevelLoad()`, `getSelectedBrowserFolder()`, `getSelectedGridAsset()`, `clearSelectedGridAsset()` |
| Cross-References | `handleKeyDown` (Escape/F2/Arrows), `updateNotifications` — delegieren an `m_contentBrowserPanel` |

UIManager behält Thin-Wrapper-Delegationsmethoden (`refreshContentBrowser`, `focusContentBrowserSearch`, `isOverContentBrowserGrid`).

#### 1.3 World Outliner + Entity Details in eigene Klasse ✅
- `OutlinerPanel.h/.cpp`
- `populateOutlinerWidget`, `populateOutlinerDetails`, `refreshWorldOutliner`, `navigateOutlinerByArrow`, `selectEntity`
- `m_outlinerLevel`, `m_outlinerSelectedEntity`, Entity-Clipboard

**Ergebnis:** Outliner + Entity Details in `src/Renderer/EditorTabs/OutlinerPanel.h/.cpp` extrahiert. UIManager.cpp von ~13.100 auf ~11.560 Zeilen reduziert (~1.545 Zeilen entfernt). OutlinerPanel.cpp: ~1.808 Zeilen.

#### 1.4 Build-System-UI extrahieren ✅
- `BuildSystemUI.h/.cpp`
- `BuildProfile`, `BuildGameConfig`, Build-Methoden (`openBuildGameDialog`, `showBuildProgress`, `updateBuildProgress`, etc.)
- CMake/Toolchain-Detection (`detectCMake`, `detectBuildToolchain`, `startAsyncToolchainDetection`, `pollToolchainDetection`)

**Ergebnis:** Build-System-UI in `src/Renderer/EditorTabs/BuildSystemUI.h/.cpp` extrahiert. UIManager.cpp von ~11.560 auf ~10.300 Zeilen reduziert (~1.260 Zeilen entfernt). BuildSystemUI.cpp: ~1.370 Zeilen. BuildPipeline.cpp aktualisiert (Zugriff auf Build-Thread-State über getBuildSystemUI()).

#### 1.5 Dialog-System extrahieren ✅
- `EditorDialogs.h/.cpp`
- Modale Dialoge, Confirm-Dialoge, Unsaved-Changes, Save-Progress, Level-Load-Progress, Projekt-Screen
- `showModalMessage`, `showConfirmDialog`, `showUnsavedChangesDialog`, `openProjectScreen`

**Ergebnis:** Dialog-System in `src/Renderer/EditorTabs/EditorDialogs.h/.cpp` extrahiert. 13 Methoden verschoben (showModalMessage, closeModalMessage, showConfirmDialog, showConfirmDialogWithCheckbox, ensureModalWidget, showSaveProgressModal, updateSaveProgress, closeSaveProgressModal, showUnsavedChangesDialog, showLevelLoadProgress, updateLevelLoadProgress, closeLevelLoadProgress, openProjectScreen). UIManager.cpp von ~10.300 auf ~8.550 Zeilen reduziert (~1.750 Zeilen entfernt). EditorDialogs.cpp: ~1.840 Zeilen. UIManager behält dünne Delegation-Wrapper.

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

**✅ Phase 2 abgeschlossen:** Alle verbleibenden 9 Module extrahiert. `PythonScripting.cpp` von ~3.690 auf 1.380 Zeilen reduziert (~63% Reduktion). Gesamt: 13 Dateien, ~4.670 Zeilen.

### Modul-Übersicht (Final)

| Datei | Modul | Zeilen | Status |
|---|---|---|---|
| `EntityModule.cpp` | `engine.entity` | 468 | ✅ |
| `AudioModule.cpp` | `engine.audio` | 354 | ✅ |
| `InputModule.cpp` | `engine.input` | 307 | ✅ |
| `CameraModule.cpp` | `engine.camera` | 213 | ✅ |
| `UIModule.cpp` | `engine.ui` | 489 | ✅ |
| `ParticleModule.cpp` | `engine.particle` | 98 | ✅ |
| `DiagnosticsModule.cpp` | `engine.diagnostics` | 126 | ✅ |
| `LoggingModule.cpp` | `engine.logging` | 53 | ✅ |
| `EditorModule.cpp` | `engine.editor` + Plugin-System | 393 | ✅ |
| `MathModule.cpp` | `engine.math` | 532 | ✅ (Phase 1) |
| `PhysicsModule.cpp` | `engine.physics` | 198 | ✅ (Phase 1) |
| `ScriptingInternal.h` | Shared State Header | 57 | ✅ |
| `PythonScripting.cpp` | Core (Init/Shutdown/Tick/HotReload/Assets) | 1.380 | ✅ |

`PythonScripting.cpp` behält: Initialisierung (`Initialize`, `Shutdown`, `SetRenderer`), Script-Laden/Tick-Dispatch, Level-Script-Management, Asset-Modul (`engine.assetmanagement`), Hot-Reload, `CreateEngineModule()` mit Factory-Calls, `PyLogWriter`.

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
| 🔴 Hoch | ~~1.1 – Editor-Tabs aus UIManager extrahieren~~ | ~~Mittel~~ | ✅ Erledigt (~7.650 Zeilen, 11 Tabs + IEditorTab Interface) |
| 🔴 Hoch | ~~2.1 – UI-Rendering vereinheitlichen~~ | ~~Mittel~~ | ✅ Erledigt (~1.224 Zeilen, renderWidgetElement unified method) |
| 🔴 Hoch | ~~2.2 – Debug-Rendering extrahieren~~ | ~~Gering~~ | ✅ Erledigt (-545 Zeilen → OpenGLRendererDebug.cpp) |
| 🔴 Hoch | ~~3.1 – Build-Pipeline aus main.cpp extrahieren~~ | ~~Gering~~ | ✅ Erledigt (-747 Zeilen) |
| 🟡 Mittel | ~~1.2 – Content Browser extrahieren~~ | ~~Mittel~~ | ✅ Erledigt (~1.212 Zeilen, ContentBrowserPanel) |
| 🟡 Mittel | ~~1.3 – Outliner/Details extrahieren~~ | ~~Mittel~~ | ✅ Erledigt (~1.545 Zeilen, OutlinerPanel) |
| 🟡 Mittel | ~~1.4 – Build-System-UI extrahieren~~ | ~~Mittel~~ | ✅ Erledigt (~1.260 Zeilen, BuildSystemUI) |
| 🟡 Mittel | ~~1.5 – Dialog-System extrahieren~~ | ~~Mittel~~ | ✅ Erledigt (~1.750 Zeilen, EditorDialogs) |
| 🟡 Mittel | ~~6 – PythonScripting Module aufteilen~~ | ~~Gering~~ | ✅ Erledigt (Phase 1+2: 11 Module extrahiert, PythonScripting.cpp -63%) |
| 🟡 Mittel | ~~4 – Dupliziertes ECS konsolidieren~~ | ~~Gering~~ | ✅ Erledigt (Legacy-ECS `src/ECS/` entfernt) |
| 🟡 Mittel | ~~5 – Leere .cpp-Dateien entfernen~~ | ~~Trivial~~ | ✅ Erledigt (12 UIWidget .cpp-Stubs entfernt) |
| 🟢 Niedrig | 8 – AssetManager aufteilen | Mittel | ~2.600 Zeilen weniger |
| 🟢 Niedrig | ~~9 – OpenGLRenderer Member-Structs~~ | ~~Gering~~ | ✅ Erledigt (12 Sub-Structs, ~108 Member gruppiert) |
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

### ✅ Abschnitt 2.1: UI-Rendering vereinheitlicht (`renderWidgetElement`)
Drei nahezu identische `renderElement`-Lambdas (je ~450–800 Zeilen) in `drawUIWidgetsToFramebuffer`, `renderViewportUI` und `renderUI` durch eine einzige `renderWidgetElement()`-Methode (~665 Zeilen) ersetzt.

**Neue Strukturen (OpenGLRenderer.h):**
- `UIRenderContext`-Struct: `projection` (glm::mat4), `screenHeight` (int), `debugEnabled` (bool), `scissorOffset` (Vec2, Default {0,0}), `DeferredDropDown`-Substruct + Vektor-Pointer
- `renderWidgetElement(const WidgetElement&, float parentX, float parentY, float parentW, float parentH, float parentOpacity, UIRenderContext& ctx)` — private Methode

**Geänderte Dateien:**
- `OpenGLRenderer.h`: `UIRenderContext`-Struct + `renderWidgetElement`-Deklaration
- `OpenGLRenderer.cpp`: Methode eingefügt (Zeile ~3077–3755), drei Lambdas durch jeweils 5–7-zeilige `UIRenderContext`-Setups + direkte Aufrufe ersetzt

**Besonderheiten:**
- `scissorOffset` in `UIRenderContext` löst den Viewport-UI-Offset (Scissor relativ zu Fenster, Widgets relativ zu Viewport-Rect)
- RAII-Structs `RtRestore`/`ScissorRestore` innerhalb der Methode für rekursive Aufrufe
- Widget-Editor-Preview-FBO in `renderUI` nutzt separaten `previewCtx`
- Deferred-Dropdown-Overlay-Pass bleibt in den Wrapper-Methoden

**Ergebnis:** `OpenGLRenderer.cpp` von ~12.787 auf ~11.563 Zeilen reduziert (-1.224 Zeilen).

### ✅ Abschnitt 2.2: Debug-Rendering extrahiert (`OpenGLRendererDebug.cpp`)
5 Debug-Rendering-Methoden aus `OpenGLRenderer.cpp` in eine eigene Übersetzungseinheit `OpenGLRendererDebug.cpp` verschoben (Split-Implementierungsdatei, gleiche Klasse).

**Verschobene Methoden:**
- `drawSelectionOutline()` (~28 Zeilen) – Outline-Shader über Pick-Textur
- `renderColliderDebug()` (~257 Zeilen) – Wireframe-Box/Sphere/Capsule/Cylinder für CollisionComponents
- `renderStreamingVolumeDebug()` (~61 Zeilen) – Wireframe-Boxen für Streaming Volumes
- `renderBoneDebug()` (~128 Zeilen) – Skelett-Knochen-Linien und Joint-Kreuz-Marker
- `drawRubberBand()` (~66 Zeilen) – Halbtransparentes Auswahlrechteck

**Neue Datei:**
- `src/Renderer/OpenGLRenderer/OpenGLRendererDebug.cpp` (583 Zeilen): Alle 5 Methoden + statischer `buildCircleVerts`-Helper (Duplikat, da auch von `renderGizmo` im Hauptfile genutzt)

**Geänderte Dateien:**
- `OpenGLRenderer.cpp`: 5 Methodenimplementierungen entfernt
- `OpenGLRenderer/CMakeLists.txt`: `OpenGLRendererDebug.cpp` zu `OPENGL_RENDERER_SOURCES` hinzugefügt

**Ergebnis:** `OpenGLRenderer.cpp` von ~11.563 auf ~11.018 Zeilen reduziert (-545 Zeilen).

### ✅ Abschnitt 2.3+2.4+2.5: Gizmo, Grid & Tab-System extrahiert
Gizmo-/Grid-Rendering und Editor-Tab-Verwaltung aus `OpenGLRenderer.cpp` in zwei neue Split-Implementierungsdateien verschoben.

**Verschobene Methoden (OpenGLRendererGizmo.cpp, 846 Zeilen):**
- `ensureGizmoResources()` (~49Z) – Gizmo-Shader + VAO/VBO
- `releaseGizmoResources()` (~6Z)
- `ensureGridResources()` (~101Z) – Grid-Shader (fwidth-basiert, Achsen-Highlights, Distance-Fade)
- `releaseGridResources()` (~5Z)
- `drawViewportGrid()` (~30Z)
- `getGizmoWorldAxis()` (~5Z)
- `renderGizmo()` (~120Z) – Translate/Rotate/Scale-Achsen
- `pickGizmoAxis()` (~122Z) – Screen-Space Hit-Testing
- `beginGizmoDrag()` (~53Z)
- `updateGizmoDrag()` (~218Z) – Translate/Rotate/Scale mit Snap + Multi-Entity
- `endGizmoDrag()` (~48Z) – UndoRedo-Command
- Statische Helper: `buildCircleVerts`, `screenToRay`, `closestTOnAxis`

**Verschobene Methoden (OpenGLRendererTabs.cpp, 294 Zeilen):**
- `addTab()` (~22Z)
- `removeTab()` (~19Z)
- `rebuildTitleBarTabs()` (~78Z) – Dynamische Tab-Buttons im TitleBar
- `setActiveTab()` (~141Z) – Level-Swap, Kamera-/Selektions-Persistenz
- `getActiveTabId()` (~4Z)
- `getTabs()` (~4Z)
- `releaseAllTabFbos()` (~7Z)

**Beibehaltene Methode:** `getEntityRotationMatrix()` bleibt im Hauptfile (wird von Debug + Gizmo genutzt).

**Geänderte Dateien:**
- `OpenGLRenderer.cpp`: Alle o.g. Methoden entfernt
- `OpenGLRenderer/CMakeLists.txt`: `OpenGLRendererGizmo.cpp` + `OpenGLRendererTabs.cpp` zu `OPENGL_RENDERER_SOURCES` hinzugefügt

**Ergebnis:** `OpenGLRenderer.cpp` von ~11.018 auf ~9.906 Zeilen reduziert (-1.112 Zeilen).
