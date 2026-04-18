# Plan: Sauberere Trennung von Editor und Engine-Schnittstellen

Dieser Plan beschreibt die Architektur- und Implementierungsschritte, um die Core-Engine und den Editor strikt voneinander zu entkoppeln. Aktuell ist der Editor-Code (z.B. Editor-Tabs, Popups) tief in den Engine-Systemen wie dem `UIManager` und im `Renderer` verankert, und `EditorApp` greift über direkte `#include`-Anweisungen auf Engine-Kernelemente (wie `ECS`, `PhysicsWorld`, `AssetManager`) zu.

Das langfristige Design-Ziel (wie auch in `PROJECT_OVERVIEW.md` und `CODEBASE_CLEANUP_PLAN.md` gefordert) ist:
**Der Editor kommuniziert als ein externes Modul ausschließlich über dedizierte APIs mit der Engine.** Er darf die Backend-Logik fernsteuern, ohne dessen internen Datensilos direkt anzufassen.

**Wichtigste Regel:** Die Engine darf absolut **keine** direkten Abhängigkeiten zum Editor haben. Der Code des Editors muss rein optional sein und sich via CMake-Variable (z.B. `ENGINE_EDITOR=ON/OFF`) vollständig abdocken lassen, um schlanke und saubere Game-Builds zu ermöglichen. Ohne aktivierten Editor dürfen keinerlei Editor-Includes, -Abhängigkeiten oder -Logiken im finalen Build verbleiben.

---

## 1. Problem-Analyse der aktuellen Codebase

- **Fehlende Kapselung im UIManager:** Die Datei `UIManager.cpp` ist mit über 13.000 Zeilen massiv überladen und enthält harte Verweise auf Editor-spezifische Fenster (z.B. `openConsoleTab()`, `openMaterialEditorPopup()`). 
- **Verletzung der Modulgrenzen:** Editor-Tabs (`ActorEditorTab.cpp`) liegen im Verzeichnis `src/Renderer/EditorTabs/`, obwohl sie eigentlich ins `src/Editor/`-Modul gehören.
- **Direkte ECS-Manipulation:** Der Editor modifiziert Entity-Daten und Game-States direkt im Speicherkreislauf der Engine, was eine saubere Undo-/Redo-Einbindung sowie Game/Play-State-Trennung enorm erschwert.
- **IEditorBridge unvollständig:** Die angedachte `IEditorBridge` existiert zwar im Ansatz, wird aber vielfach durch einbinden direkter Subsystem-Header in `EditorApp.cpp` umgangen.

---

## 2. Ziel-Architektur

Die Entkopplung wird durch strikte API-Grenzen realisiert. Wir etablieren drei dedizierte Kommunikations-Säulen:

1. **`IEditorWorldAPI` (Teil der EditorBridge)**
   Verarbeitet alle Editor-Befehle, die Auswirkungen auf die Entitäten der Szene haben. 
   - *Zugriffe:* `CreateEntity()`, `DuplicateEntity()`, `UpdateComponentData()`, `MoveEntity(Transform)`.
   - *Sicherheit:* Mutationen können direkt über das interne Command-Pattern geleitet werden, um sauberes Undo/Redo durchzuführen.

2. **`IEditorAssetAPI` (Teil der EditorBridge)**
   Verwaltet das Einlesen, Neuladen und Abspeichern von Level-Daten und Assets.
   - *Nutzung:* Der Editor bittet die API "Lade Asset XY in den Arbeitsspeicher" oder "Schreibe die Level-Daten nun ins JSON/HPK". Kein direkter Aufruf auf den `AssetManager`.

3. **`EditorWindowManager` (Neues lokales System im Editor)**
   Der `UIManager` der Engine baut **nur** die graphischen Primitive (Buttons, Panels, Text) und rendert diese.
   Das Aufrufen, Schließen und Platzieren spezifischer Editor-Fenster (Audio Preview, Actor Inspector, Console) wird in einen reinen `EditorWindowManager` im `src/Editor/`-Namespace ausgelagert.

---

## 3. Schritt-für-Schritt Umsetzungsplan

### **Phase 1: UI-Entkopplung (UIManager ausmisten)**
1. **Erstellen eines `EditorWindowManager` (oder `EditorLayoutManager`)** im Verzeichnis `src/Editor/`. Dieser übernimmt das Lifecycle-Management der Editorpanels.
   - ✅ **Erledigt:** `src/Editor/UI/EditorWindowManager.{h,cpp}` angelegt (Namespace `Editor`).
     Aktuell ist die Klasse eine Fassade, die über `IEditorBridge` an die bestehenden
     `UIManager::openXYZTab()` / `isXYZOpen()` delegiert. Sie wird in `EditorApp::initialize()`
     instanziiert und ist via `EditorApp::getWindowManager()` verfügbar. Dadurch existiert ab
     sofort eine zentrale Editor-seitige Anlaufstelle, gegen die neue/bestehende Aufrufstellen
     migriert werden können, ohne dass der Build bricht.
2. Entfernen aller Editor-spezifischen `openXYZTab()` und `isXYZOpen()` Methoden aus `UIManager.h/cpp`.
   - ✅ **Teilerledigt (EditorApp):** Alle 11 `openXYZTab()`/`openXYZPopup()`-Aufrufe in
     `EditorApp.cpp` wurden auf `m_windowManager->openXYZ()` umgestellt. Betroffen:
     Console, Profiler, ShortcutHelp, LandscapeManager, MaterialEditor, EngineSettings,
     EditorSettings, WorkspaceTools, WidgetEditor, InputActionEditor, InputMappingEditor,
     EntityEditor, ActorEditor.
   - 🔜 *Nächster Schritt:* Die Lifecycle-Logik
     (Member-Variablen, `openXYZTab`-Bodies) aus `UIManager` in den `EditorWindowManager`
     verschieben und die Methoden aus `UIManager.h` entfernen.
   - ✅ **Erledigt (ContentBrowserPanel + OutlinerPanel):** Alle `openXYZ()`-Aufrufe in
     `ContentBrowserPanel.cpp` (8 Stück) und `OutlinerPanel.cpp` (2 Stück) wurden über das
     neue `ITabOpener`-Interface entkoppelt. `EditorWindowManager` implementiert `ITabOpener`
     und wird über `UIManager::setTabOpener()` an die Panels durchgereicht. Die Panels
     rufen nun `m_tabOpener->openXYZ()` statt `m_uiManager->openXYZ()` auf.
   - ✅ **Erledigt (UIManagerEditor – Workspace Tools):** Die 7 Buttons im Workspace-Tools-Popup
     (`openConsoleTab`, `openNotificationsTab`, `openProfilerTab`, `openShaderViewerTab`,
     `openRenderDebuggerTab`, `openSequencerTab`, `openLevelCompositionTab`) nutzen nun
     `m_tabOpener->openXYZ()` mit Fallback auf die alten Methoden.
        `ITabOpener` wurde um 7 parameterlose Methoden erweitert (Console, Profiler,
        Notifications, ShaderViewer, RenderDebugger, Sequencer, LevelComposition).
     - ✅ **Erledigt (Phase 1.4 – Simple Tab Ownership Transfer):** Die 10 „einfachen" Editor-Tabs
       (Console, Profiler, Notifications, AudioPreview, ParticleEditor, ShaderViewer,
       RenderDebugger, Sequencer, LevelComposition, AnimationEditor) werden nun vollständig
       vom `EditorWindowManager` besessen und verwaltet. Konkret:
       - Die `unique_ptr`-Member für diese 10 Tabs wurden aus `UIManager.h` entfernt.
       - Die `ITabOpener`-Schnittstelle wurde um close/isOpen/updateTabs-Methoden erweitert.
       - `EditorWindowManager` konstruiert, öffnet, schließt und aktualisiert die Tabs selbst.
       - Die `openXYZTab()`/`closeXYZTab()`/`isXYZOpen()`-Methoden in `UIManagerEditor.cpp`
         delegieren nun vollständig via `m_tabOpener->`.
       - Die Tab-Update-Aufrufe in `UIManager::update()` wurden durch einen einzelnen
         `m_tabOpener->updateTabs(deltaSeconds)`-Aufruf ersetzt.
       - Stale Includes für die 10 Tab-Header wurden aus `UIManager.cpp` und
         `UIManagerEditor.cpp` entfernt.
     - 🔜 *Nächster Schritt:* Die verbleibenden 5 Tabs (UIDesigner, WidgetEditor, EntityEditor,
       ActorEditor, InputAction/InputMapping) sowie die delegierenden Methoden-Hüllen aus
       `UIManager.h` entfernen.
     - ✅ **Erledigt (Phase 1.5 – Complex Tab Ownership Transfer):** Die verbleibenden 5 komplexen
       Editor-Tabs (EntityEditor, ActorEditor, InputActionEditor, InputMappingEditor, UIDesigner)
       wurden vom `UIManager` in den `EditorWindowManager` verschoben. Konkret:
       - `ITabOpener` um open/close/isOpen für alle 5 Tabs sowie `getActorEditorTab()` erweitert.
       - `EditorWindowManager` konstruiert und besitzt die Tabs direkt.
       - `UIManager` delegiert alle verbleibenden Tab-Methoden via `m_tabOpener->`.
       - Die `unique_ptr`-Member und zugehörige Includes für diese 5 Tabs aus `UIManager.h/.cpp` entfernt.
       - Tote UIDesigner-Hilfsmethoden (selectElement, addElement, etc.) aus UIManager entfernt.
       - `OpenGLRenderer.h` verwendet nun Forward-Declarations statt Editor-Window-Includes;
         die vollständigen Includes sind in den `.cpp`-Dateien hinter `#if ENGINE_EDITOR`.
     - 🔜 *Nächster Schritt:* WidgetEditorTab, ContentBrowserPanel, OutlinerPanel und
       BuildSystemUI/EditorDialogs aus UIManager herauslösen (tief integriert, größerer Umbau).
3. Anpassen des Window-Routings: Der `EditorWindowManager` nutzt die Basis-Funktionen des `UIManager` lediglich, um seine Widgets zu zeichnen, behält aber die Kontrolle über die Logik der spezifischen Tabs.

### **Phase 2: Datei- und Modul-Reorganisation**
1. Verschieben aller Editor-Tabs und Editor-Widgets aus `src/Renderer/EditorTabs/` und `src/Renderer/EditorUI/` in das `src/Editor/`-Modul (z. B. `src/Editor/Tabs/` und `src/Editor/UI/`).
   - ✅ **Erledigt:** Alle Editor-Tabs von `src/Renderer/EditorTabs/` nach `src/Editor/Tabs/` verschoben.
     Alle Editor-Windows von `src/Renderer/EditorWindows/` nach `src/Editor/Windows/` verschoben.
     Sämtliche Include-Pfade in den verschobenen Dateien sowie in allen referenzierenden Dateien
     (`UIManager.h/cpp`, `UIManagerEditor.cpp`, `UIManagerContentBrowserOverlay.cpp`,
     `OpenGLRenderer.h`, `OpenGLRendererTabs.cpp`, `EditorApp.cpp`, `main.cpp`, `BuildPipeline.cpp`)
     wurden aktualisiert.
2. Anpassen der CMakeLists, sodass diese Dateien nur inkludiert werden, wenn `ENGINE_EDITOR` gesetzt ist.
   - ✅ **Erledigt:** Die Dateien werden weiterhin über `RENDERER_CORE_EDITOR_SOURCES` in
     `RendererCore` (Editor-Variante, `ENGINE_EDITOR=1`) kompiliert, da der `UIManager` und
     `OpenGLRenderer` sie noch direkt referenzieren. Die physische Verzeichnisstruktur ist aber
     bereits korrekt im `src/Editor/`-Modul. Die Dateien sind **nicht** in der Runtime-Variante
     (`RendererCoreRuntime`, `ENGINE_EDITOR=0`) enthalten.

### **Phase 3: Ausbau der `IEditorBridge` und dedizierter APIs**
1. Aufspalten und Erweitern der `IEditorBridge` in saubere Data-Access-Interfaces (`IEditorWorldAPI`, `IEditorAssetAPI`).
2. Die Klasse `EditorBridgeImpl` (welche tief in der Engine Implementiert ist) implementiert diese Methoden sicher und baut die Brücke zu `AssetManager`, `ECS` und `EngineLevel`.
3. In `src/Editor/EditorApp.cpp` und in sämtlichen Tab-Klassen werden alle Header-Includes wie `#include "Core/ECS/ECS.h"` oder `#include "Physics/PhysicsWorld.h"` GELÖSCHT. Sie operieren fortan **nur noch** gegen die puren API-Interfaces.

### **Phase 4: Trennung der Daten (Command Pattern zur Mutation)**
1. Umsetzungen von Level/UI-Änderungen über ein API-Proxy: Ändert der Nutzer per Slider einen Scale-Wert oder benennt eine UI-Textbox um, schreibt er den Wert nicht hart ins ECS. Er sendet statt dessen eine Nachricht über die API `worldAPI->ExecuteCommand(ChangePropertyCommand(...))`.
2. Dies sorgt dafür, dass die Engine den State nach eigenen Vorstellungen threadsafe validieren und sichern kann (Undo/Redo profitiert massiv davon).

---

## Zusammenfassung
Ist dieses Vorhaben umgesetzt, kann die Editor-Applikation vollkommen abgekapselt getestet oder weiterentwickelt werden. Der Engine-Kern läuft blind ohne Wissen über die Existenz des Tools – er reagiert nur noch auf deterministische API-Aufrufe, welche die Game-Logik, die Welt oder Leveldaten steuern und umgestalten.