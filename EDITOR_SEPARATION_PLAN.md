# Editor-Separation Plan – Saubere Editor-API Abstraktion

> Ziel: Der Editor wird fundamental von der Engine getrennt. Die Engine exponiert eine stabile API (`IEditorBridge`), über die der Editor auf alle benötigten Subsysteme zugreift. Der Editor wird ein eigenes CMake-Target (`HorizonEditor`-Library), das per Preprocessor-Definition (`ENGINE_EDITOR`) ein-/ausgeschaltet werden kann. Im Engine-Code selbst bleiben nur wenige, minimale `#if ENGINE_EDITOR`-Guards.

---

## 1. Ist-Zustand Analyse

### Probleme
1. **`main.cpp` ist ~2.800 Zeilen**, davon ~1.800 Zeilen reiner Editor-Code (Widget-Setup, Click-Events, Drag&Drop, Context-Menüs, PIE-Steuerung, Shortcuts, Build-Pipeline).
2. **`Renderer.h`** hat ~70 editor-only virtuelle Methoden hinter `#if ENGINE_EDITOR`-Guards direkt im Interface.
3. **`UIManager.h/cpp`** mischt Editor-Logik (Tabs, Dialoge, Build-System, Outliner, Content Browser) mit Runtime-UI-Logik (Toasts, Widgets, Layout).
4. **`Renderer`-Unterklassen** (z.B. `OpenGLRenderer`) implementieren Editor-Features inline (Gizmos, Picking, Tab-FBOs, Debug-Rendering).
5. **Zirkuläre Abhängigkeiten**: Editor-Code referenziert Engine-Internals direkt (ECS, Physics, AssetManager, Scripting) ohne definierte API-Grenzen.
6. **Kein klares Ownership**: Editor-Setup in `main.cpp` greift direkt auf ECS-Komponenten, Level-Objekte, PhysicsWorld etc. zu.

### Bereits vorhandene Trennung
- CMake hat bereits **zwei Targets**: `HorizonEngine` (ENGINE_EDITOR=1) und `HorizonEngineRuntime` (ENGINE_EDITOR=0)
- Renderer hat `Renderer` + `RendererRuntime`, AssetManager hat `AssetManager` + `AssetManagerRuntime`
- Editor-only Sources sind in `RENDERER_CORE_EDITOR_SOURCES` bereits separiert
- `#if ENGINE_EDITOR`-Guards existieren bereits an ~50+ Stellen

---

## 2. Architektur-Ziel

```
┌────────────────────────────────────────────────────────────┐
│                    HorizonEngine (exe)                      │
│  main.cpp (~300 Zeilen): Init, Game Loop, Shutdown          │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐  │
│  │              Engine Core (immer kompiliert)           │  │
│  │  Renderer, ECS, AssetManager, Physics, Audio,        │  │
│  │  Scripting, Logger, Diagnostics, UIManager(Runtime)   │  │
│  │                                                      │  │
│  │  Exponiert: IEditorBridge (abstrakte Schnittstelle)   │  │
│  └──────────────────────┬───────────────────────────────┘  │
│                         │ (implementiert von)               │
│  ┌──────────────────────▼───────────────────────────────┐  │
│  │         Editor Module (nur wenn ENGINE_EDITOR=1)      │  │
│  │  EditorApp: Setup, Event-Routing, PIE, Shortcuts      │  │
│  │  EditorUI:  Widgets, Panels, Tabs, Dialoge            │  │
│  │  EditorActions: Click-Events, D&D, Context-Menüs      │  │
│  │  BuildPipeline: Game-Build                            │  │
│  └──────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────┘
```

---

## 3. Neue Dateien & Struktur

### 3.1 `IEditorBridge` – Die Engine-zu-Editor API

**Datei:** `src/Core/IEditorBridge.h`

Abstrakte Schnittstelle, die alle Engine-Zugriffe bündelt, die der Editor braucht. Die Engine-Seite implementiert dies; der Editor kennt nur das Interface.

```cpp
#pragma once
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include "../Core/MathTypes.h"

class Renderer;
class UIManager;
class AssetData;
struct SDL_Window;
union SDL_Event;

/// Abstract bridge that the engine exposes to the editor.
/// The editor only depends on this interface, never on engine internals directly.
class IEditorBridge
{
public:
    virtual ~IEditorBridge() = default;

    // ── Renderer Access ─────────────────────────────────────────────
    virtual Renderer*       getRenderer() = 0;
    virtual UIManager&      getUIManager() = 0;
    virtual SDL_Window*     getWindow() = 0;

    // ── Camera ──────────────────────────────────────────────────────
    virtual Vec3  getCameraPosition() const = 0;
    virtual Vec2  getCameraRotation() const = 0;
    virtual void  setCameraPosition(const Vec3& pos) = 0;
    virtual void  setCameraRotation(float yaw, float pitch) = 0;

    // ── Entity Operations ───────────────────────────────────────────
    virtual unsigned int createEntity(const std::string& name) = 0;
    virtual void         removeEntity(unsigned int entity) = 0;
    virtual void         selectEntity(unsigned int entity) = 0;
    virtual unsigned int getSelectedEntity() const = 0;
    virtual void         invalidateEntity(unsigned int entity) = 0;

    // ── Asset Management ────────────────────────────────────────────
    virtual int  loadAsset(const std::string& path, int type) = 0;
    virtual bool saveAssets() = 0;
    virtual bool importAsset(const std::string& path) = 0;
    virtual bool deleteAsset(const std::string& path) = 0;
    virtual bool moveAsset(const std::string& from, const std::string& to) = 0;
    virtual std::string getAbsoluteContentPath(const std::string& rel) const = 0;
    virtual std::string getProjectPath() const = 0;
    virtual std::string getEditorWidgetPath(const std::string& name) const = 0;
    virtual unsigned int preloadUITexture(const std::string& path) = 0;

    // ── Level Management ────────────────────────────────────────────
    virtual bool loadLevel(const std::string& path) = 0;
    virtual bool saveActiveLevel() = 0;
    virtual std::string getActiveLevelName() const = 0;

    // ── PIE (Play in Editor) ────────────────────────────────────────
    virtual void startPIE() = 0;
    virtual void stopPIE() = 0;
    virtual bool isPIEActive() const = 0;

    // ── Physics (for editor tools like Drop-to-Surface) ─────────────
    struct RaycastResult { bool hit; float hitY; };
    virtual RaycastResult raycastDown(float ox, float oy, float oz, float maxDist) = 0;

    // ── Diagnostics / Config ────────────────────────────────────────
    virtual void        setState(const std::string& key, const std::string& value) = 0;
    virtual std::string getState(const std::string& key) const = 0;
    virtual bool        hasState(const std::string& key) const = 0;
    virtual void        requestShutdown() = 0;
    virtual bool        isShutdownRequested() const = 0;

    // ── Scripting ───────────────────────────────────────────────────
    virtual void reloadScripts() = 0;
    virtual void loadEditorPlugins(const std::string& projectPath) = 0;
};
```

### 3.2 `EditorBridgeImpl` – Engine-seitige Implementierung

**Datei:** `src/Core/EditorBridgeImpl.h` + `src/Core/EditorBridgeImpl.cpp`

Implementiert `IEditorBridge`, indem es an die realen Singletons/Instanzen delegiert (Renderer, AssetManager, DiagnosticsManager, ECS, Physics, Scripting). Lebt im Engine-Core, wird aber nur im Editor-Build instanziiert.

### 3.3 `EditorApp` – Der Editor als eigenständiges Modul

**Datei:** `src/Editor/EditorApp.h` + `src/Editor/EditorApp.cpp`

Enthält die gesamte Editor-Logik, die heute in `main.cpp` lebt:

```cpp
#pragma once
#include "Core/IEditorBridge.h"

class EditorApp
{
public:
    explicit EditorApp(IEditorBridge& bridge);
    ~EditorApp();

    /// Called once after engine subsystems are initialized.
    /// Sets up all editor widgets, registers click events, shortcuts, etc.
    bool initialize();

    /// Called once per frame before render.  Handles editor-specific
    /// event processing (PIE toggle, gizmo drag, rubber band, etc.)
    void processEvent(SDL_Event& event);

    /// Called once per frame for editor tick logic
    /// (tooltip timers, notification polling, build-thread poll, etc.)
    void tick(float deltaTime);

    /// Called during shutdown to save editor state.
    void shutdown();

    // ── PIE state (accessed by main loop for input routing) ─────────
    bool isPIEMouseCaptured() const;
    bool isPIEInputPaused() const;

private:
    IEditorBridge& m_bridge;

    // All the state that currently lives as locals in main():
    // stopPIE lambda, gridSnapEnabled, playTexId, stopTexId,
    // cameraSpeedMultiplier (editor portion), etc.
    // ...
};
```

### 3.4 `src/Editor/` Verzeichnis-Struktur

```
src/Editor/
├── CMakeLists.txt              # Eigenes Library-Target "Editor"
├── EditorApp.h                 # Haupt-Klasse
├── EditorApp.cpp               # Setup + Event-Routing + Tick
├── EditorActions.h             # Click-Event-Registrierungen
├── EditorActions.cpp           # Alle registerClickEvent()-Aufrufe
├── EditorShortcuts.h           # ShortcutManager-Registrierungen
├── EditorShortcuts.cpp         # Alle sm.registerAction()-Aufrufe
├── EditorContextMenus.h        # Rechtsklick-Kontextmenüs
├── EditorContextMenus.cpp      # Content Browser, Viewport, etc.
├── EditorDragDrop.h            # Drag & Drop Handler
├── EditorDragDrop.cpp          # setOnDropOnViewport/Folder/Entity
├── PIEController.h             # Play-In-Editor Steuerung
├── PIEController.cpp           # Start/Stop/Pause PIE
└── ProjectSelector.h/cpp       # Projekt-Auswahl-Screen
```

---

## 4. Migrations-Plan (Phasen)

### Phase 1: IEditorBridge Interface (Grundlage) ✅ DONE

1. `src/Core/IEditorBridge.h` erstellen – reine abstrakte Klasse
2. `src/Core/EditorBridgeImpl.h/cpp` erstellen – delegiert an reale Subsysteme
3. In `main.cpp`: `EditorBridgeImpl` instanziieren (nach Renderer-Init)
4. **Kein bestehender Code ändert sich** – nur neue Dateien

### Phase 2: EditorApp Grundgerüst ✅ DONE

1. `src/Editor/EditorApp.h/cpp` erstellen mit leerem `initialize()`/`tick()`/`shutdown()`
2. `src/Editor/CMakeLists.txt` erstellen als OBJECT-Library mit `ENGINE_EDITOR=1`
3. In `main.cpp`: `EditorApp` instanziieren und Lifecycle aufrufen
4. **Bestehender Code läuft weiterhin** – EditorApp ist zunächst ein No-Op

### Phase 3: Widget-Setup extrahieren (~800 Zeilen aus main.cpp) ✅ DONE

Schrittweise die Widget-Lade- und Registrierungs-Blöcke aus `main.cpp` nach `EditorApp::initialize()` verschieben:
1. TitleBar-Widget laden + registerWidget
2. ViewportOverlay-Widget laden + registerWidget
3. WorldSettings-Widget laden + registerWidget (inkl. ClearColor, Skybox)
4. WorldOutliner, EntityDetails, StatusBar, ContentBrowser
5. Jeder Schritt: In `main.cpp` entfernen, in `EditorApp` hinzufügen, Build testen

### Phase 4: Click-Events extrahieren (~400 Zeilen) ✅ DONE

Alle `registerClickEvent()`-Aufrufe nach `src/Editor/EditorActions.cpp` verschieben:
1. TitleBar-Buttons (Close, Minimize, Maximize)
2. Menu-Items (File, Edit, Window, Build, Help)
3. Viewport-Overlay-Buttons (Undo, Redo, Snap, Colliders, Bones, Layout, etc.)
4. PIE-Button
5. StatusBar-Buttons (Save, Undo, Redo, Notifications)

### Phase 5: Shortcuts extrahieren (~300 Zeilen) ✅ DONE

Alle `ShortcutManager::registerAction()`-Aufrufe für Editor-only Shortcuts wurden nach `EditorApp::registerShortcuts()` verschoben. In `main.cpp` verbleiben nur 3 shared Debug-Shortcuts (F10 ToggleMetrics, F9 ToggleOcclusionStats, F12 ToggleFPSCap) mit bidirektionaler Sync zu EditorApp. ~370 Zeilen aus main.cpp entfernt.

### Phase 6: Drag & Drop extrahieren (~300 Zeilen) ✅ DONE

`setOnDropOnViewport`, `setOnDropOnFolder`, `setOnDropOnEntity` und die zugehörigen Lambda-Bodies wurden nach `EditorApp::registerDragDropHandlers()` verschoben.

### Phase 7: Kontextmenüs extrahieren (~400 Zeilen) ✅ DONE

Das Content-Browser-Rechtsklick-Menü (New Folder, New Script, New Level, New Widget, New Material, Save as Prefab, Find References, Show Dependencies) wurde nach `EditorApp::handleContentBrowserContextMenu()` verschoben. ~700 Zeilen aus main.cpp entfernt, ersetzt durch einen einzigen Funktionsaufruf.

### Phase 8: PIE-Controller extrahieren ✅ DONE

PIE-Start/Stop/Pause-Logik wurde nach `EditorApp::startPIE()`/`stopPIE()` verschoben. `main.cpp` synchronisiert PIE-State von EditorApp am Frame-Start.

### Phase 9: Projekt-Selektion extrahieren ✅ DONE

Der temporäre Renderer + Projekt-Auswahl-Screen wurde nach `src/Editor/ProjectSelector.h/cpp` verschoben. Neue freie Funktion `showProjectSelection(RendererBackend)` kapselt den gesamten Flow: Default-Projekt-Check, temporärer Renderer mit modaler Event-Loop, SampleProject-Fallback. In `main.cpp` wurde der ~185-Zeilen `#if ENGINE_EDITOR`-Block durch einen einzigen Funktionsaufruf ersetzt. `startupSelectionCancelled`-Variable entfernt.

### Phase 10: Renderer.h bereinigen ✅ DONE

~65 editor-only virtuelle Methoden aus `Renderer.h` in ein separates `IEditorRenderer`-Interface (`src/Renderer/IEditorRenderer.h`) extrahiert. Zirkuläre Abhängigkeit durch `RendererEnums.h` gelöst: 7 Enum-Klassen (GizmoMode, GizmoAxis, AntiAliasingMode, DebugRenderMode, ViewportLayout, SubViewportPreset) + SubViewportCamera-Struct auf Namespace-Scope verschoben. `Renderer` erbt von `IEditorRenderer` nur wenn `ENGINE_EDITOR=1` (conditional inheritance). Backward-Kompatibilität via `using`-Aliases (`Renderer::GizmoMode` → `::GizmoMode` etc.). Alle editor-only Methoden in `Renderer.h` von `virtual` auf `override` umgestellt.

### Phase 11: UIManager aufteilen ✅ DONE

`UIManager.cpp` (~8490 Zeilen) in zwei Dateien aufgeteilt:
1. **`UIManager.cpp`** (Core, ~3350 Zeilen, immer kompiliert): Widgets, Layout, Input, Toasts, Drag&Drop, Hit-Testing, Hover/Scrollbar-Transitions, Click-Events, Fokus-Management. Core-Funktionen behalten kleine interne `#if ENGINE_EDITOR`-Guards für Konstruktor/Destruktor, `updateNotifications()`, Input-Handler.
2. **`UIManagerEditor.cpp`** (~4340 Zeilen, nur Editor): 10 eigenständige `#if ENGINE_EDITOR`-Blöcke verschoben – Outliner/Content Browser (~1240 Zeilen), Popup-Builder/Tabs/Build-System (~2990 Zeilen), StatusBar, Save/Load Progress, Widget Editor, Confirm Dialogs, Dropdown-Menüs, DPI-Rebuild, Theme-Update, Toast-Widget-Erstellung. In `RENDERER_CORE_EDITOR_SOURCES` eingetragen (nur Editor-Build kompiliert). Gemeinsame Hilfsfunktion `FindElementById` im anonymen Namespace dupliziert.
3. `UIManager.h` bleibt als einheitlicher Header (bestehende `#if ENGINE_EDITOR`-Guards trennen bereits die API).

---

## 5. Ergebnis: Neues `main.cpp` (~300 Zeilen)

```cpp
int main()
{
    // Phase 1: SDL + Logger Init
    // Phase 2: Renderer + Subsystem Init (identisch für Editor & Runtime)

#if ENGINE_EDITOR
    EditorBridgeImpl bridge(renderer, assetManager, diagnostics);
    EditorApp editor(bridge);
    editor.initialize();
#endif

    // Main Loop
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
#if ENGINE_EDITOR
            if (editor.processEvent(event)) continue;
#endif
            // Runtime event handling (minimal)
        }

#if ENGINE_EDITOR
        editor.tick(dt);
#endif

        renderer->render();
        renderer->present();
    }

#if ENGINE_EDITOR
    editor.shutdown();
#endif
    // Cleanup
}
```

**Nur 3 `#if ENGINE_EDITOR`-Stellen** in `main.cpp` statt der aktuellen ~30+.

---

## 6. CMake-Änderungen

```cmake
# Neues Editor-Module
if(ENGINE_EDITOR)
    add_subdirectory(src/Editor)
endif()

# Editor-Target linkt die Editor-Library
target_link_libraries(HorizonEngine PRIVATE Editor Renderer AssetManager ...)

# Runtime-Target linkt OHNE Editor
target_link_libraries(HorizonEngineRuntime PRIVATE RendererRuntime AssetManagerRuntime ...)
```

---

## 7. IEditorBridge Kategorien (Detail)

| Kategorie | Methoden | Genutzt von |
|-----------|----------|-------------|
| **Renderer** | getRenderer, getUIManager, getWindow, preloadUITexture | Widget-Setup, PIE, alle UI-Operationen |
| **Camera** | get/setCameraPosition, get/setCameraRotation | PIE, Level-Load, Shutdown-Save |
| **Entity** | create/remove/select/invalidateEntity | Drag&Drop, Context-Menü, Delete, Outliner |
| **Assets** | load/save/import/delete/moveAsset, getProjectPath | Content Browser, Import, Build |
| **Level** | load/saveLevel, getActiveLevelName | Level-Switch, PIE, Save |
| **PIE** | start/stop/isPIEActive | PIE-Button, Shortcuts |
| **Physics** | raycastDown | Drop-to-Surface |
| **Config** | get/set/hasState, requestShutdown | Settings, Snap/Grid, Themes |
| **Scripting** | reloadScripts, loadEditorPlugins | PIE-Stop, Plugin-System |

---

## 8. Regeln für die Implementierung

1. **Der Editor importiert nie direkt** `ECS.h`, `PhysicsWorld.h`, `AssetManager.h` etc. – nur `IEditorBridge.h`
2. **`IEditorBridge` ist stabil** – Änderungen am Engine-Internals brechen den Editor nicht
3. **Editor-Code kompiliert nur** wenn `ENGINE_EDITOR=1` (CMake-Guard + Preprocessor)
4. **`main.cpp` hat max. 5 `#if ENGINE_EDITOR`-Stellen**: Include, Bridge-Instanziierung, Init, Event-Routing, Shutdown
5. **Keine zirkulären Abhängigkeiten**: Engine → IEditorBridge (abstrakt) ← Editor (implementierung kennt Engine)
6. **Stack-Allokationen bevorzugen** (gemäß Copilot Instructions)
7. **Jeder Phase-Schritt einzeln buildbar** – kein Big-Bang-Refactoring

---

## 9. Risiken & Mitigationen

| Risiko | Mitigation |
|--------|------------|
| Große Lambda-Captures in Editor-Callbacks referenzieren main()-Locals | EditorApp besitzt den State als Member-Variablen |
| UIManager wird von Editor und Runtime genutzt | Phase 11 spaltet UIManager in Core+Editor |
| Renderer hat editor-only virtuelle Methoden | Phase 10 extrahiert IEditorRenderer |
| Build-Pipeline referenziert UIManager direkt | BuildPipeline bekommt IEditorBridge statt UIManager |
| Performance-Regression durch Indirektion | IEditorBridge-Calls sind selten (UI-Events, nicht Hot Path) |

---

## 10. Priorisierung

| Phase | Aufwand | Impact | Priorität |
|-------|---------|--------|-----------|
| 1 (IEditorBridge) | Klein | Grundlage | ⭐⭐⭐⭐⭐ |
| 2 (EditorApp Gerüst) | Klein | Grundlage | ⭐⭐⭐⭐⭐ |
| 3 (Widgets) | Mittel | -800 LOC aus main | ⭐⭐⭐⭐ |
| 4 (Click-Events) | Mittel | -400 LOC aus main | ⭐⭐⭐⭐ |
| 5 (Shortcuts) | Klein | -300 LOC aus main | ⭐⭐⭐ |
| 6 (Drag&Drop) | Mittel | -300 LOC aus main | ⭐⭐⭐ |
| 7 (Kontextmenüs) | Mittel | -400 LOC aus main | ⭐⭐⭐ |
| 8 (PIE) | Klein | Saubere PIE-Abstraktion | ⭐⭐⭐ |
| 9 (Projekt-Selektion) | Klein | -200 LOC aus main | ⭐⭐ |
| 10 (Renderer.h) | Groß | Sauberes Interface | ⭐⭐ |
| 11 (UIManager Split) | Sehr Groß | Vollständige Trennung | ⭐⭐ |

**Empfehlung:** Phase 1–4 zuerst → sofort spürbarer Effekt (main.cpp von ~2800 auf ~800 Zeilen). Phase 5–9 danach für Feinschliff. Phase 10–11 als langfristiges Ziel.
