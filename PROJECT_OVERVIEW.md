# Engine – Projektübersicht

> Umfassende Dokumentation der gesamten Engine-Architektur, aller Komponenten und des Zusammenspiels.
> Branch: `C++-Scripting` | Build-System: CMake 3.12+ | Sprache: C++20 | Plattform: Windows (x64), Linux, macOS

---

## Aktuelle Änderung (Input Action / Input Mapping System)

- `AssetTypes.h (Neue Asset-Typen)`: `AssetType::InputAction = 15` und `AssetType::InputMapping = 16` zum Enum hinzugefügt. InputAction definiert einen benannten Input-Event mit optionalen Modifier-Anforderungen (Shift/Ctrl/Alt). InputMapping ordnet InputActions konkreten Tasten zu.
- `InputActionManager.h/.cpp (Neuer Core-Singleton)`: Runtime-Manager für das Input-Action-System. Verwaltet `ActionDef` (Name + requiredMods-Bitmask), `KeyBinding` (actionName + SDL_Keycode) und C++ Callbacks. Modifier-Matching: KeyDown prüft ob alle geforderten Modifier gedrückt sind, KeyUp überspringt Modifier-Check (Modifier könnten bereits losgelassen sein). `DispatchHook`-Mechanismus entkoppelt Core von Python — die Scripting-Schicht setzt einen Hook der Python-Callbacks auslöst. Convenience-Overloads: `addAction(name, mods)`, `addBinding(actionName, keycode)`.
- `InputActionEditorTab.h/.cpp (Neuer Editor-Tab)`: Editor-Tab zum Bearbeiten von InputAction-Assets. Zeigt Modifier-Checkboxen (Shift/Ctrl/Alt) und speichert als JSON-Asset. Beim Speichern wird der InputActionManager automatisch aktualisiert.
- `InputMappingEditorTab.h/.cpp (Neuer Editor-Tab)`: Editor-Tab zum Bearbeiten von InputMapping-Assets. Zeigt Bindings-Liste mit Action-Name-Cycling (wählt aus vorhandenen InputAction-Assets), Key-Dropdown (A-Z, Space, Return, Pfeiltasten, F1-F8, 0-9), Hinzufügen/Entfernen von Bindings. Beim Speichern werden alle Bindings im InputActionManager aktualisiert.
- `UIManager.h/.cpp (Tab-Integration)`: Forward-Declarations, `unique_ptr`-Member und `openInputActionEditorTab`/`closeInputActionEditorTab`/`isInputActionEditorOpen` + Äquivalent für InputMapping. Includes in UIManager.cpp für Complete-Type bei unique_ptr-Destruktor.
- `UIManagerEditor.cpp (Tab-Methoden)`: Implementierung der 6 Tab-Methoden (open/close/isOpen für beide Tabs). Lazy-Creation der Tab-Instanzen beim ersten Öffnen.
- `ContentBrowserPanel.cpp (Browser-Integration)`: Icons (`entity.png` für beide), Tints (gelb für InputAction, türkis für InputMapping), Filter-Buttons ("Action"/"Mapping"), Doppelklick-Handler öffnen entsprechenden Editor-Tab.
- `AssetManager.h/.cpp (Speichern + Laden)`: `saveGenericJsonAsset()` als generische JSON-Save-Methode für einfache Asset-Typen (Magic/Version/Type/Name/Data-Pattern). InputAction/InputMapping-Cases im `saveAsset`-Switch. `populateInputActionsFromRegistry()` lädt beim Projektstart alle InputAction/InputMapping-Assets aus dem Registry und befüllt den InputActionManager — sowohl nach Async-Discovery (Editor) als auch nach Registry-Load (Packaged Build).
- `EditorApp.cpp (Kontextmenü)`: "New Input Action" und "New Input Mapping" Einträge im Content-Browser-Kontextmenü. Erstellt .asset-Datei, registriert im AssetManager, öffnet Editor-Tab.
- `InputModule.cpp (Python-API)`: `register_action_pressed(action_name, callback)` und `register_action_released(action_name, callback)` zum Registrieren von Python-Callbacks für Input-Actions. Callbacks werden in `ScriptDetail::s_actionPressedCallbacks`/`s_actionReleasedCallbacks`-Maps gespeichert und bei `ClearKeyCallbacks` bereinigt.
- `ScriptingInternal.h (Shared State)`: Extern-Deklarationen für `s_actionPressedCallbacks` und `s_actionReleasedCallbacks`.
- `PythonScripting.cpp (Dispatch Hook)`: `SetRenderer()` richtet den InputActionManager-DispatchHook ein — Lambda mit PyGILState das Python-Action-Callbacks aufruft.
- `GameplayAPI.h/.cpp (C++ API)`: `registerInputActionCallback(actionName, callback)` → gibt Callback-ID zurück. `unregisterInputActionCallback(callbackId)` → entfernt Callback.
- `engine.pyi (Python IntelliSense)`: `register_action_pressed` und `register_action_released` Stubs in der `input`-Klasse.
- `main.cpp (Event-Loop)`: `InputActionManager::Instance().handleKeyDown/handleKeyUp` im SDL-Event-Loop nach `Scripting::HandleKeyDown/Up` (nur im Runtime/PIE-Modus).
- `CMakeLists.txt (Build-Integration)`: `InputActionManager.cpp/.h` zu Core-Library, `InputActionEditorTab.h/.cpp` und `InputMappingEditorTab.h/.cpp` zu `RENDERER_CORE_EDITOR_SOURCES`.
- **Architektur**: Asset-System → InputActionManager (Core-Singleton) → C++ Callbacks + DispatchHook → Python Callbacks. Entkopplung von Core und Scripting über Hook-Pattern. Modifier-Check nur bei KeyDown, KeyUp feuert immer (Modifier-State kann sich zwischen Down/Up ändern).

## Aktuelle Änderung (GameplayAPI Python-Parität – alle Scripting-APIs für C++)

- `GameplayAPI.h (Vollständige API)`: Massiv erweitert von 18 auf ~90 exportierte Funktionen. Deckt jetzt **alle** Python-Scripting-Module ab (außer `math`):
  - **Transform** (erweitert): `getTransform()`, `translate()`, `rotate()` — zusätzlich zu bestehenden get/set Position/Rotation/Scale.
  - **Entity** (erweitert): `attachComponent()`, `detachComponent()`, `getEntities()` — Komponentenverwaltung über `ComponentKind`-Enum.
  - **Mesh**: `setMesh()`, `getMesh()` — Mesh-Asset-Zuweisung per Content-Pfad.
  - **Light**: `getLightColor()`, `setLightColor()` — Entity-Light-Farbe lesen/setzen.
  - **Material Overrides**: `setMaterialOverrideColorTint()`, `getMaterialOverrideColorTint()`, `setMaterialOverrideMetallic()`, `setMaterialOverrideRoughness()`, `setMaterialOverrideShininess()`, `clearMaterialOverrides()` — Per-Entity Material-Parameter-Overrides.
  - **Physics** (erweitert): `setAngularVelocity()`, `getAngularVelocity()`, `setGravity()`, `getGravity()`, `isBodySleeping()`, `raycast()` (mit `RaycastResult`-Struct) — Rotation, Welt-Gravitation, Ray-Queries.
  - **Camera**: `getCameraPosition()`, `setCameraPosition()`, `getCameraRotation()`, `setCameraRotation()`, `cameraTransitionTo()`, `isCameraTransitioning()`, `cancelCameraTransition()`, `startCameraPath()`, `isCameraPathPlaying()`, `pauseCameraPath()`, `resumeCameraPath()`, `stopCameraPath()`, `getCameraPathProgress()` — Volle Kamera-Steuerung mit Transition und Path-System.
  - **Audio**: `createAudio()`, `createAudioFromAsset()`, `playAudio()`, `playAudioHandle()`, `setAudioVolume()`, `getAudioVolume()`, `pauseAudio()`, `isAudioPlaying()`, `isAudioPlayingPath()`, `stopAudio()`, `invalidateAudioHandle()` — Vollständiges Audio-Management.
  - **Input**: `isKeyPressed()`, `isShiftPressed()`, `isCtrlPressed()`, `isAltPressed()`, `getKey()` — Tastatur-Input-Polling (Callback-basierte Varianten aus Python entfallen, da C++ in `tick()` pollt).
  - **UI**: `showToastMessage()`, `showModalMessage()`, `closeModalMessage()`, `showCursor()`, `clearAllWidgets()`, `spawnWidget()`, `removeWidget()`, `playAnimation()`, `stopAnimation()`, `setAnimationSpeed()`, `setFocus()`, `clearFocus()`, `setFocusable()`, `setDraggable()`, `setDropTarget()` — Viewport-Widget-System, Cursor-Kontrolle, Animations-Steuerung, Focus/Drag-Drop.
  - **Particle**: `setEmitterProperty()`, `setEmitterEnabled()`, `setEmitterColor()`, `setEmitterEndColor()` — Partikel-Emitter-Konfiguration.
  - **Diagnostics**: `setState()`, `getState()` — Engine-State-Strings lesen/schreiben.
  - **Global State**: `setGlobalNumber()`, `setGlobalString()`, `setGlobalBool()`, `getGlobalNumber()`, `getGlobalString()`, `getGlobalBool()`, `removeGlobal()`, `clearGlobals()` — Typisiertes globales State-System (Number/String/Bool).
  - **Logging** (erweitert): `log()` — Level-basiertes Logging (0=Info, 1=Warning, 2=Error) zusätzlich zu bestehenden `logInfo/logWarning/logError`.
- `GameplayAPI.cpp (Implementierungen)`: Alle neuen Funktionen implementiert mit Zugriff auf ECS, PhysicsWorld, AudioManager, AssetManager, Renderer, UIManager, ViewportUIManager, DiagnosticsManager, ScriptingGlobalState, SDL3. Internes `s_renderer`-Pointer wird beim Engine-Start gesetzt.
- `GameplayAPIInternal.h (Neu)`: Engine-interner Header mit `setRendererInternal()` — ermöglicht Camera/UI-Funktionen ohne den Renderer im öffentlichen GameplayAPI.h zu exponieren.
- `INativeScript.h (Convenience-Methoden erweitert)`: Neue Entity-gebundene Convenience-Methoden: `getTransform()`, `translate()`, `rotate()`, `setAngularVelocity()`, `getAngularVelocity()`, `getLightColor()`, `setLightColor()`, `setEmitterProperty()`, `setEmitterEnabled()`, `setEmitterColor()`, `setEmitterEndColor()`.
- `main.cpp (Renderer-Init)`: `GameplayAPI::setRendererInternal(renderer)` nach `Scripting::SetRenderer(renderer)` — C++ GameplayAPI hat Zugriff auf Renderer für Camera/UI-Funktionen.
- `CMakeLists.txt (Scripting)`: `GameplayAPIInternal.h` zur SCRIPTING_SOURCES-Liste hinzugefügt.

## Aktuelle Änderung (NativeScript Engine-API-Zugriff + Content Browser C++-Klassen-Anzeige)

- `INativeScript.h (Convenience API)`: Basisklasse erweitert um Convenience-Methoden die `GameplayAPI`-Funktionen für die eigene Entity forwarden. Scripts können Engine-Systeme direkt über Member-Methoden nutzen (`getPosition()`, `setPosition()`, `setVelocity()`, `addForce()`, `addImpulse()`, etc.) ohne `GameplayAPI::`-Namespace-Prefix und ohne `getEntity()` manuell zu übergeben. Statische Methoden für Entity-Management (`findEntityByName`, `createEntity`, `removeEntity`), Logging (`logInfo`, `logWarning`, `logError`) und Time (`getDeltaTime`, `getTotalTime`). Include von `GameplayAPIExport.h` für `GAMEPLAY_API`-Export-Macro.
- `GameplayAPI.cpp (Convenience-Implementierungen)`: Implementierung aller `INativeScript`-Convenience-Methoden als Delegation an die gleichnamigen `GameplayAPI`-Namespace-Funktionen mit `m_entity` als implizitem Entity-Parameter.
- `AssetTypes.h (NativeScript-Typ)`: Neuer `AssetType::NativeScript` Enum-Wert für C++-Gameplay-Klassen. Separiert Python-Scripts (`Script`) von C++-Scripts (`NativeScript`) im Asset-System.
- `AssetManager.cpp (Asset-Discovery für .h/.cpp)`: Synchrone und asynchrone Asset-Registry-Discovery erkennt jetzt C++-Dateien: `.h`/`.hpp`-Dateien mit zugehöriger `.cpp`-Datei werden als einzelner `NativeScript`-Eintrag registriert (Header+Source als eine Klasse zusammengefasst). Standalone `.cpp`-Dateien ohne Header werden ebenfalls als `NativeScript` registriert. `.cpp`-Dateien mit zugehörigem Header werden übersprungen (Duplikat-Vermeidung). Save-Switch behandelt `NativeScript` als nicht-speicherbar (wie `Script`).
- `AssetManagerImport.cpp (Extension-Detection)`: `DetectAssetTypeFromPath()` erkennt `.h`, `.hpp`, `.cpp` als `NativeScript`.
- `ContentBrowserPanel.cpp (C++-Klassen-Anzeige)`: Icon `script.png` mit blauem Tint (`Vec4{0.30, 0.70, 1.00, 1.0}`) für `NativeScript` — visuell unterscheidbar von grünem Python-Script-Tint. Neuer `"C++"` Type-Filter-Button neben dem `"Script"`-Button. Unreferenced-Asset-Indicator für `NativeScript` deaktiviert (C++-Scripts werden über Klassennamen referenziert, nicht über Asset-Pfade).
- `PythonScripting.cpp (Python-Binding)`: `Asset_NativeScript`-Konstante auf Modul- und Submodul-Ebene registriert.
- `engine.pyi (IntelliSense)`: `Asset_NativeScript: int` in `assetmanagement`-Klasse ergänzt.

## Aktuelle Änderung (Engine-Lib-Deployment + Leichtgewichtiger Game Build)

- `CMakeLists.txt (POST_BUILD .lib-Deployment)`: Alle Engine-Import-Libraries (.lib) werden nach `${ENGINE_DEPLOY_DIR}/Tools/lib/` kopiert. Umfasst: Shared-Lib-Import-Libs (Scripting, Renderer, AssetManager, Core, Logger, Diagnostics, Physics, SDL3) und Runtime-Varianten (ScriptingRuntime, RendererRuntime, AssetManagerRuntime) sowie statische Libs (Landscape, LandscapeRuntime). Ermöglicht GameScripts-PIE-Build und Game-Build ohne Zugriff auf den cmake-Build-Ordner.
- `CMakeLists.txt (Runtime-DLL-Deployment)`: Runtime-Varianten-DLLs (ScriptingRuntime.dll, RendererRuntime.dll, AssetManagerRuntime.dll) werden ebenfalls nach `Tools/lib/` deployed. `add_dependencies(HorizonEngine ScriptingRuntime RendererRuntime AssetManagerRuntime LandscapeRuntime)` stellt sicher, dass Runtime-Targets automatisch beim Editor-Build mitgebaut werden.
- `CMakeLists.txt (NativeScripting-Header-Deployment)`: Header-Dateien aus `src/NativeScripting/` werden nach `Tools/include/NativeScripting/` deployed (GameplayAPIExport.h, INativeScript.h, NativeScriptRegistry.h, GameplayAPI.h, NativeScriptManager.h). GameScripts-Projekte kompilieren gegen deployed Headers statt Engine-Source-Tree.
- `CMakeLists.txt (SDL3-Header-Deployment)`: SDL3-Header (`external/SDL3/include/SDL3/`) werden nach `Tools/include/SDL3/` kopiert für den leichtgewichtigen Game Build.
- `CMakeLists.txt (EngineConfig.cmake)`: Generierte Config-Datei in `Tools/EngineConfig.cmake` mit ENGINE_SOURCE_DIR, ENGINE_DEPLOY_DIR, Python3_INCLUDE_DIRS, ENGINE_PYTHON_LIB. Wird vom leichtgewichtigen Game Build verwendet.
- `EditorApp.cpp (GameScripts Link-Pfad Fix)`: `target_link_directories` im generierten GameScripts-CMakeLists zeigt jetzt auf `Tools/lib/` (statt direkt auf Editor-Binary-Verzeichnis). `target_include_directories` nutzt `Tools/include/NativeScripting/` (deployed Headers statt Engine-Source-Pfad).
- `BuildPipeline.cpp (Leichtgewichtiger Game Build)`: Erkennt automatisch ob vorgebaute Engine-Libraries in `Tools/lib/` vorhanden sind (prüft ScriptingRuntime.lib, RendererRuntime.lib, AssetManagerRuntime.lib, Core.lib, SDL3.lib). **Wenn vorhanden**: Generiert minimales CMakeLists.txt das nur `main.cpp` kompiliert und gegen vorgebaute .lib-Dateien linkt — Engine wird NICHT neu kompiliert. **Wenn nicht vorhanden**: Fallback auf vollständigen Engine-Build. DLL-Deploy im leichtgewichtigen Modus kopiert (1) Shared-DLLs aus Editor-Root und (2) Runtime-DLLs aus `Tools/lib/`.
- **Architektur**: Editor-Build → deployed .lib/.dll/headers → Game Build linkt dagegen. Kein cmake add_subdirectory für SDL/assimp/jolt/PhysX/freetype/OpenAL nötig.

## Aktuelle Änderung (Default-Content ScriptingMode-Awareness + DefaultCubeScript entfernt)

- `DefaultCubeScript.py entfernt (AssetManagerEditorWidgets.cpp)`: Die einzelne `DefaultCubeScript.py`-Datei wird beim Erstellen des Beispielprojekts nicht mehr generiert — sie war keiner Entity zugewiesen und somit überflüssig.
- `DefaultCube1-5 ScriptingMode-aware (AssetManagerEditorWidgets.cpp)`: Die 5 Standard-Cube-Skripte werden jetzt basierend auf der Projekt-ScriptingMode generiert. **PythonOnly/Both**: Python `.py`-Dateien mit `on_loaded`, `tick`, `on_begin_overlap`, `on_end_overlap`. **CppOnly/Both**: C++ `.h`/`.cpp`-Dateien in `Scripts/Native/` als INativeScript-Subklassen mit allen Lifecycle-Methoden (`onLoaded`, `tick`, `onBeginOverlap`, `onEndOverlap`, `onDestroy`).
- `Default-Level JSON nutzt "Logic"-Key`: Entity-Komponenten im generierten Default-Level verwenden den neuen `"Logic"`-JSON-Key mit `scriptPath` und/oder `nativeClassName` abhängig vom ScriptingMode (statt altem `"Script"`-Key).

## Aktuelle Änderung (Unified LogicComponent: Script + NativeScript zusammengeführt)

- `LogicComponent ersetzt ScriptComponent + NativeScriptComponent (Components.h)`: Die zwei separaten Komponenten `ScriptComponent` (Python-Skripting) und `NativeScriptComponent` (C++-Skripting) wurden zu einer einzigen `LogicComponent` zusammengeführt. Felder: `scriptPath` (Python-Skript-Pfad), `nativeClassName` (C++-Klassenname), `scriptAssetId` (Asset-ID). Ermöglicht Entities gleichzeitig Python- und C++-Logik zu haben.
- `ComponentKind::Logic (ECS.h)`: `ComponentKind::Script` und `ComponentKind::NativeScript` durch `ComponentKind::Logic` ersetzt. `ComponentTraits<LogicComponent>` Spezialisierung. `m_logicComponents` Storage. Beide `getStorage()`-Überladungen (const/non-const) und `setComponentAsset<>` aktualisiert. `MaxComponentTypes = 14` bleibt.
- `ECS.cpp (initialize + removeEntity)`: Initialisierung und Entity-Entfernung nutzen `LogicComponent` statt der zwei getrennten Typen.
- `EngineLevel.cpp/h (Serialisierung + Backward-Compat)`: `serializeLogicComponent`/`deserializeLogicComponent` ersetzen die alten Funktionen. **Backward-Kompatibilität**: Level-Deserialisierung erkennt alte `"Script"` und `"NativeScript"` JSON-Keys und migriert sie automatisch in die neue `LogicComponent`-Struktur. `EntitySnapshot` nutzt `LogicComponent`.
- `OutlinerPanel.cpp/h (Logic-UI + Auto-File-Generation)`: Entity-Details zeigen eine "Logic"-Sektion mit Python-Script-Pfad und C++-Klassen-Anzeige. **Auto-File-Erzeugung beim Hinzufügen**: Je nach `DiagnosticsManager::ScriptingMode` werden automatisch Python-`.py`-Dateien (mit `on_loaded`, `tick`, `on_begin_overlap`, `on_end_overlap`) und/oder C++-`.h`/`.cpp`-Dateien (INativeScript-Subklasse mit allen Lifecycle-Methoden) erstellt. Entity-Name wird zu Dateinamen sanitisiert (Nicht-Alphanumerische → Underscore). `EntityClipboard` nutzt `LogicComponent`.
- `EntityModule.cpp (Python-API)`: Alle drei Switch-Statements (`AddComponentByKind`, `RemoveComponentByKind`, `RequireComponentByKind`) verwenden `ComponentKind::Logic` mit `LogicComponent`.
- `PythonScripting.cpp (Script-Loop + Konstanten)`: Entity-Script-Loop und Overlap-Dispatch nutzen `LogicComponent`. Python-Konstante `Component_Logic` ersetzt `Component_Script` und `Component_NativeScript`.
- `NativeScriptManager.cpp`: Schema-Query und Komponentenzugriff nutzen `LogicComponent.nativeClassName`.
- `UIManagerEditor.cpp (Prefab-Serialisierung)`: Prefab-Serialisierung/Deserialisierung nutzt `LogicComponent`. Backward-Kompatibilität für alte `"Script"`-JSON-Keys. Icon/Tint-Helper aktualisiert.
- `AssetManagerFileOps.cpp`: Integritätsvalidierung, `moveAsset`, `renameAsset`, `findAssetReferences` nutzen `LogicComponent.scriptPath`.
- `EditorApp.cpp`: Viewport-Drop, Spawn-Snapshot, Drop-on-Entity, Delete-Entity-Snapshots, Undo/Redo nutzen `LogicComponent`.
- `ContentBrowserPanel.cpp`: Asset-Referenz-Sammlung nutzt `LogicComponent`.
- `engine.pyi`: `Component_Logic` ersetzt `Component_Script` und `Component_NativeScript` für Python-IntelliSense.

## Aktuelle Änderung (ScriptingMode-Persistenz in Projektdatei)

- `DiagnosticsManager.h/cpp (String-Konvertierung)`: Neue Hilfsfunktionen `scriptingModeToString()` und `scriptingModeFromString()` für die Konvertierung von `ScriptingMode`-Enum zu/von String (`"PythonOnly"`, `"CppOnly"`, `"Both"`).
- `AssetManager.cpp (Projekt-Datei Save/Load)`: `ScriptingMode=`-Zeile wird in die `.project`-Datei geschrieben (nach `RHI=`). Beim Laden wird das Feld geparst und in `DiagnosticsManager::ProjectInfo::scriptingMode` gesetzt.

## Aktuelle Änderung (Editor-UI-Verbesserungen: Hover-Effekte, IDE-Empfehlung, Dialog-Buttons)

- `Popup-Hover-Effekte (EditorDialogs.cpp, UIManagerEditor.cpp)`: `style.transitionDuration = 0.15f` an 4 Stellen hinzugefügt, damit Hover-Effekte in Popup-Dialogen korrekt funktionieren (Projekt-Erstellungs-Dialog, IDE-Empfehlungs-Dialog, Bestätigungs-Dialog, Checkbox-Bestätigungs-Dialog).
- `VS IDE-Empfehlungs-Popup (EditorDialogs.cpp)`: Wenn C++/Both-Scripting gewählt wird, erscheint ein `showConfirmDialogWithCheckbox`-Dialog mit Visual Studio Download-Empfehlung. "Download"-Button öffnet `https://visualstudio.microsoft.com/` via `SDL_OpenURL`. "Skip"-Button überspringt. Checkbox "Do not show again" mit `DiagnosticsManager::setState("HideVSRecommendation", "1")`.
- `Optionale Button-Labels (EditorDialogs.h/cpp)`: `showConfirmDialog` und `showConfirmDialogWithCheckbox` akzeptieren jetzt optionale `confirmLabel`/`cancelLabel`-Parameter. Primär-Button wird als Primary-Style gerendert (Accent-Farbe), Cancel als Danger-Style.
- `Cancel-Button-Fix (EditorDialogs.cpp)`: Cancel-Button im IDE-Empfehlungs-Dialog erstellt das Projekt nicht mehr (leere Lambda statt `doCreateProject()`-Aufruf).

## Aktuelle Änderung (C++ Native Scripting System: DLL-basiertes Gameplay-Scripting)

- `NativeScripting-Modul (src/NativeScripting/)`: Neues Engine-Modul für C++-basiertes Gameplay-Scripting als DLL-Plugin-System. Spieleentwickler schreiben C++-Klassen die von `INativeScript` erben, kompilieren sie als `GameScripts.dll`, und die Engine lädt sie zur Laufzeit via `LoadLibrary`/`dlopen`. Automatische Klassenregistrierung über statische Initialisierer (`REGISTER_NATIVE_SCRIPT`-Makro).
- `INativeScript.h (Basisklasse)`: Abstrakte Basisklasse mit 5 Lifecycle-Events: `onLoaded()` (nach Zuweisung), `tick(float dt)` (jeden Frame), `onBeginOverlap(Entity)` / `onEndOverlap(Entity)` (Physik-Overlap), `onDestroy()` (vor Entfernung). Entity-ID über `getEntity()` zugänglich.
- `NativeScriptRegistry.h/cpp (Automatische Registrierung)`: Singleton-Registry mit Factory-Pattern. `REGISTER_NATIVE_SCRIPT(ClassName)`-Makro erzeugt statisches Objekt das bei DLL-Load die Klasse registriert. API: `registerClass()`, `createInstance()`, `hasClass()`, `getRegisteredClassNames()`, `unregisterAll()`.
- `NativeScriptManager.h/cpp (Kern-Singleton)`: DLL-Management (`loadGameplayDLL`/`unloadGameplayDLL`), Script-Lifecycle (`initializeScripts`/`updateScripts`/`shutdownScripts`), Instanz-Verwaltung (`createInstance`/`destroyInstance`), Overlap-Dispatch, Hot-Reload (`ENGINE_EDITOR`-only). Speichert `m_lastDeltaTime` für `GameplayAPI::getDeltaTime()`.
- `GameplayAPI.h/cpp (Öffentliche Spieler-API)`: Zentral-Include für Spieler-Code. Wrapper-Funktionen für Engine-Subsysteme: Transform (get/setPosition/Rotation/Scale), Entity (findEntityByName), Physics (set/getVelocity, addForce/Impulse), Logging (logInfo/Warning/Error), Time (getDeltaTime/getTotalTime).
- `GameplayAPIExport.h (DLL-Export-Macros)`: `GAMEPLAY_API`-Macro für `__declspec(dllexport/dllimport)` (Windows) bzw. `__attribute__((visibility("default")))` (Unix).
- `ECS-Integration`: Neuer `NativeScriptComponent` (className + instance-Pointer) in `Components.h`. `ComponentKind::NativeScript = 13`, `MaxComponentTypes = 14` in `ECS.h`. Serialisierung in `EngineLevel.cpp`.
- `CMakeLists.txt (Zusammengeführt in Scripting-DLL)`: C++ NativeScripting-Quellen sind in `Scripting.dll` / `ScriptingRuntime.dll` integriert (kein separates NativeScripting-Target mehr). `GAMEPLAY_DLL_EXPORT=1` + `WINDOWS_EXPORT_ALL_SYMBOLS=ON`. Quelldateien liegen weiterhin in `src/NativeScripting/`, werden aber vom Scripting-CMakeLists kompiliert.
- `main.cpp Integration (Phase 3)`: (1) Runtime-Modus: `GameScripts.dll` aus `Engine/`-Verzeichnis laden + `initializeScripts()` nach Level-Load. (2) Game-Loop: `NativeScriptManager::updateScripts(dt)` vor Physics und Python-Scripts (C++ → Physics → Python Lifecycle-Reihenfolge). (3) Shutdown: `shutdownScripts()` + `unloadGameplayDLL()` vor `Scripting::Shutdown()`.
- `Python-Integration`: `engine.pyi` erweitert um `Component_NativeScript`-Konstante. `EntityModule.cpp` mit Switch-Cases für NativeScript-Komponentenzugriff. `PythonScripting.cpp` mit `Component_NativeScript`-Integer-Konstante.
- **Lifecycle-Reihenfolge pro Frame**: C++ Scripts (`tick()`) → Physics (`step()`) → Python Scripts (`tick()`) → Overlap-Dispatch.
- **Architektur**: Engine exportiert `GameplayAPI`-Funktionen aus `Scripting.dll` (Python + C++ vereint). Spieler-DLL (`GameScripts.dll`) linkt dagegen. Statische Initialisierer in der Spieler-DLL registrieren Klassen beim `LoadLibrary()`-Aufruf automatisch.

## Aktuelle Änderung (Global State Module: Daten zwischen Python-Entities austauschen)

- `ScriptingGlobalState.h (Singleton Fix + Erweiterung)`: `instance()` → `static Instance()` (Singleton war nicht aufrufbar ohne Instanz). Neue Methoden: `setVariable()` (überschreibt existierende), `removeVariable()`, `getAllVariables()`, `clear()`. Unterstützt Typen: `Number`, `String`, `Boolean`, `None`, `Object`, `Function`.
- `GlobalStateModule.cpp (Neues Python-Submodul)`: Neues `engine.globalstate`-Submodul mit 5 Funktionen: `set_global(name, value)` (setzt globale Variable — float/int/str/bool/None), `get_global(name)` (liest Variable, None wenn nicht vorhanden), `remove_global(name)` (löscht Variable), `get_all()` (alle Variablen als Python-Dict), `clear()` (alle löschen). String-Werte werden als Heap-Kopie gespeichert und bei Überschreiben/Löschen korrekt freigegeben.
- `ScriptingInternal.h`: `CreateGlobalStateModule()` Factory-Deklaration hinzugefügt.
- `PythonScripting.cpp (CreateEngineModule)`: `globalStateModule` erstellt, Null-Check integriert, `AddSubmodule(module, globalStateModule, "globalstate")` registriert.
- `engine.pyi`: `globalstate`-Klasse mit Stubs für `set_global`, `get_global`, `remove_global`, `get_all`, `clear`.
- `CMakeLists.txt`: `Python/GlobalStateModule.cpp` zu `SCRIPTING_SOURCES` hinzugefügt.
- **Verwendung in Python-Skripten**: Entity A setzt `engine.globalstate.set_global("health", 100)`, Entity B liest `engine.globalstate.get_global("health")`. Ermöglicht Datenaustausch zwischen beliebigen Entity-Skripten ohne direkte Referenzen.

## Aktuelle Änderung (VC++ Redistributable Auto-Bundling im Game Build)

- `BuildPipeline.cpp – Neuer Step 5 (VC++ Redistributable bundeln)`: Die Build-Pipeline sucht `vc_redist.x64.exe` in 4 Verzeichnissen (`<Editor>/Tools/`, `<Editor>/tools/`, `<EngineSource>/tools/`, `<EngineSource>/Tools/`). Falls nicht gefunden, wird der Installer automatisch via PowerShell von `https://aka.ms/vs/17/release/vc_redist.x64.exe` (~24 MB) heruntergeladen und in `<Editor>/Tools/` gespeichert. Die Datei wird ins Game-Build-Root kopiert. Zusätzlich wird ein Launcher-Batch-Script (`<GameName>.bat`) generiert, das beim Start prüft ob `vcruntime140.dll` vorhanden ist (via `where` + `%SystemRoot%\System32`-Check). Falls nicht, wird `vc_redist.x64.exe` silent installiert (`/install /quiet /norestart`). Bei Fehler oder fehlender Redist-Datei wird eine Fehlermeldung mit Download-URL angezeigt. Danach startet das Script die Game-Exe.
- `Build-Pipeline Schrittfolge aktualisiert`: 8 → 9 Schritte. Neue Reihenfolge: (1) Prepare, (2) CMake Configure, (3) CMake Build, (4) Deploy Runtime, (5) Bundle VC++ Redist, (6) Cook Assets, (7) Package HPK, (8) Generate game.ini, (9) Done/Launch.
- `bootstrap.ps1 – VC++ Redistributable Download`: Neuer Abschnitt 5 im Bootstrap-Script lädt `vc_redist.x64.exe` in `Tools/` herunter (überspringt wenn vorhanden, `-Force` erzwingt Re-Download). Nachfolgende Abschnitte renummeriert (5→6 Cleanup, 6→7 Env-Scripts, 7→8 Summary).
- `Game-Build Output-Struktur erweitert`:
  ```
  GameBuild/
    <GameName>.exe          (Game-Binary)
    <GameName>.bat          (Launcher mit VC++ Runtime-Check)
    vc_redist.x64.exe       (Microsoft VC++ Redistributable Installer)
    game.ini
    config/
    Engine/
    Content/
  ```

## Aktuelle Änderung (Entity Asset Drag & Drop: Spawn in Szene)

- `Entity-Asset Drag & Drop auf Viewport (EditorApp.cpp)`: Entity-Assets (`AssetType::Entity`) werden beim Drag & Drop auf den Viewport nicht mehr als Komponente auf bestehende Entities angewendet, sondern als neue Entity in die Szene gespawnt. Verhalten: `screenToWorldPos()` sucht die nächste Oberfläche unter dem Cursor (Depth-Buffer-Unprojection); falls keine Geometrie getroffen wird (depth==1.0), wird die Entity 5 Einheiten vor der Kamera in Blickrichtung platziert. Neue Methode `UIManager::spawnEntityAssetAtPosition()` in `UIManagerEditor.cpp`: Lädt die Entity-Asset-JSON-Datei, nutzt `prefabDeserializeEntity()` für vollständige Komponenten-Deserialisierung mit Position-Override, Undo/Redo-fähig. Analoges Muster wie `spawnPrefabAtPosition()` für Prefab-Assets.

## Aktuelle Änderung (Entity Editor Rework: Split-Layout + Asset-Dropdowns)

- `EntityEditorTab Rework (EditorTabs/EntityEditorTab.h/cpp)`: Komplette Überarbeitung des Entity-Editor-Tabs. Neues Split-Layout: Komponentenliste links (scrollbar, klickbare Einträge mit Selektion-Highlighting), Detail-Panel rechts (zeigt Properties der ausgewählten Komponente). Neuer State `selectedComponent` in `State`-Struct. „+ Add Component"-Button ersetzt durch kompaktes `DropdownButtonWidget` am Ende der Komponentenliste. Entfernen von Komponenten jetzt über „Remove"-Button im Detail-Header statt „X" im Section-Header. Asset-referenzierende Properties (Mesh, Material, Script) nutzen jetzt `DropdownButtonWidget` mit aus dem `AssetRegistry` befüllten Einträgen (Model3D, Material, Script) statt manueller String-Eingabe — inklusive „(None)"-Option. Neue Hilfsmethode `getAssetPathsByType()` liefert sortierte Asset-Pfade nach Typ. Per-Component Detail-Builder umbenannt zu `build*Details()`. Statische `findElementById()`-Hilfsfunktion für Element-Suche im Widget-Tree.

## Aktuelle Änderung (Entity Asset Type: Neuer Asset-Typ mit dediziertem Editor-Tab)

- `Neuer Asset-Typ AssetType::Entity (AssetTypes.h)`: Neuer Enum-Wert `Entity` nach `Prefab` — repräsentiert eine wiederverwendbare Entity-Vorlage als .asset-Datei mit JSON-serialisierten Komponenten.
- `EntityEditorTab (EditorTabs/EntityEditorTab.h/cpp)`: Neuer Editor-Tab (~600 Zeilen) zum Bearbeiten von Entity-Assets. Funktionen: (1) Öffnen/Laden einer Entity-Asset-Datei (.asset mit type=Entity), (2) Anzeige aller Komponenten mit editierbaren Properties (Name, Transform, Mesh, Material, Light, Camera, Physics, Collision, Script, Animation, ParticleEmitter), (3) Hinzufügen neuer Komponenten via „+ Component"-Buttons mit sinnvollen Defaults, (4) Entfernen von Komponenten via „X"-Button im Section-Header, (5) Speichern als JSON-Asset. Nutzt `EditorUIBuilder`-Helper für konsistentes Look&Feel (makeStringRow, makeFloatRow, makeVec3Row, makeDropDownRow, makeCheckBox, makeIntRow, makeDivider, makePrimaryButton, makeDangerButton). State als `nlohmann::json`-Objekt — keine ECS-Entity nötig.
- `UIManager-Integration (UIManager.h/UIManager.cpp/UIManagerEditor.cpp)`: Forward-Declaration `class EntityEditorTab;`, `std::unique_ptr<EntityEditorTab> m_entityEditorTab` Member. Public API: `openEntityEditorTab(const std::string& assetPath)`, `closeEntityEditorTab()`, `isEntityEditorOpen()`. Lazy-Erstellung des Tabs bei erstem Aufruf.
- `Content Browser: New Entity (EditorApp.cpp)`: Neuer Menüpunkt „New Entity" im Rechtsklick-Kontextmenü. Erzeugt eine .asset-Datei mit `AssetType::Entity`, Name+Transform als Standard-Komponenten, registriert im AssetRegistry und öffnet sofort den EntityEditorTab.
- `Content Browser: Doppelklick (ContentBrowserPanel.cpp)`: Doppelklick auf Entity-Assets öffnet den EntityEditorTab. Icon: `entity.png`, Tint: Lila `{0.85, 0.55, 1.0}`.
- `CMakeLists.txt`: `EntityEditorTab.h` und `EntityEditorTab.cpp` zu `RENDERER_CORE_EDITOR_SOURCES` hinzugefügt.

## Aktuelle Änderung (Tools-Deploy: Bootstrap-Skripte neben Editor-Exe)

- `CMakeLists.txt POST_BUILD Copy`: Bootstrap-Skripte (`tools/bootstrap.ps1`, `tools/bootstrap.sh`, `tools/README.md`) werden beim Build automatisch aus dem Source-Tree nach `ENGINE_DEPLOY_DIR/tools/` kopiert (via `copy_if_different`). Damit liegen die Skripte zur Laufzeit neben der Editor-Exe und sind nicht mehr vom Source-Verzeichnis abhängig.
- `BuildSystemUI::runBootstrapInstall() refactored`: `ENGINE_SOURCE_DIR`-Abhängigkeit komplett entfernt. Skript-Lokalisierung ist jetzt rein exe-relativ: (1) `<exe>/tools/bootstrap.ps1|.sh` (Primary, deployed by CMake), (2) `<exe>/../tools/...` (Fallback für Multi-Config-Generatoren). Plattformunabhängige Skriptname-Auswahl (`bootstrap.ps1` auf Windows, `bootstrap.sh` auf Linux/macOS) erfolgt einmalig am Anfang der Funktion. Linux-Pfad-Lookup vereinfacht (wiederverwendet `scriptPath` statt separater `shScript`-Variable). `std::filesystem::canonical()` für saubere Log-Ausgabe.

## Aktuelle Änderung (In-Engine Auto-Install: Build-Tools beim Start prüfen und installieren)

- `BuildSystemUI Auto-Install Integration (BuildSystemUI.h/cpp)`: Der Editor prüft beim Start automatisch ob CMake und ein C++ Compiler (MSVC/Clang) vorhanden sind. Falls Build-Tools fehlen, wird dem User ein einzelner kombinierter Dialog angezeigt, der alle fehlenden Tools auflistet (CMake, C++ Compiler). Bei Zustimmung wird `tools/bootstrap.ps1` (Windows) bzw. `tools/bootstrap.sh` (Linux/macOS) auf einem Hintergrund-Thread ausgeführt, der die Tools silent installiert. Fortschritt wird im Engine-Log protokolliert. Nach Abschluss wird eine Erfolgs- oder Fehler-Toast-Nachricht angezeigt und die Tool-Erkennung automatisch wiederholt.
- `Neue Methoden in BuildSystemUI`: `promptAndInstallTools()` (zeigt kombinierten Bestätigungsdialog mit fehlenden Tools), `runBootstrapInstall()` (führt Bootstrap-Skript auf Background-Thread aus mit Pipe-basierter Log-Ausgabe), `pollToolInstall()` (prüft Install-Status pro Frame, zeigt Result-Toast). `isToolInstallRunning()` für externen Status-Check.
- `Prozess-Ausführung`: Windows: `CreateProcessA` mit `CREATE_NO_WINDOW` + anonyme Pipe für stdout/stderr-Capture. Linux/macOS: `popen()` mit zeilenweiser Log-Ausgabe. Bootstrap-Skript wird relativ zur Exe lokalisiert (`SDL_GetBasePath()/tools/`).
- `showCMakeInstallPrompt() / showToolchainInstallPrompt() refactored`: Beide Methoden delegieren jetzt an `promptAndInstallTools()` statt Browser-Seiten zu öffnen. Alte ShellExecute/xdg-open-Aufrufe entfernt.
- `pollToolchainDetection() erweitert`: Ruft jetzt `pollToolInstall()` am Anfang jedes Frames auf. Zeigt nach Erkennung einen einzelnen kombinierten Auto-Install-Dialog wenn CMake ODER Toolchain fehlen (statt zwei separate Dialoge). Log-Ausgabe für erkannte/fehlende Tools bleibt.
- `Workflow`: EditorApp::initialize() → startAsyncToolchainDetection() (Background-Thread: detectCMake + detectBuildToolchain) → EditorApp::tick() → pollToolchainDetection() (Main-Thread: Ergebnis auswerten, bei Bedarf Dialog zeigen) → User bestätigt → runBootstrapInstall() (Background-Thread: bootstrap.ps1 ausführen) → pollToolInstall() (Main-Thread: Ergebnis prüfen, Toast zeigen, Tools re-detecten).

## Aktuelle Änderung (Build-Bootstrapping: Automatische Tool-Installation)

- `tools/bootstrap.ps1 (Windows PowerShell)`: Bootstrap-Skript das fehlende Build-Tools automatisch erkennt und installiert. Unterstützt 3 Compiler-Modi (`-Compiler auto|clang|msvc`): (1) Auto: MSVC wenn Visual Studio vorhanden, sonst LLVM/Clang silent install. (2) Clang: LLVM/Clang NSIS-Installer silent nach `Tools/llvm/` (`/S /D=<path>`). (3) MSVC: VS Build Tools silent install (`vs_BuildTools.exe --quiet --wait --add VCTools`). Installiert außerdem CMake (portables ZIP nach `Tools/cmake/`), Ninja (portables ZIP nach `Tools/ninja/`) und Python mit Dev-Headers (Installer nach `Tools/python/`). Erkennt vorhandene Tools in `Tools/`-Verzeichnis, Visual Studio (via vswhere), und System-PATH. Generiert `Tools/env.ps1` und `Tools/env.bat` für Umgebungsvariablen-Setup. Parameter `-SkipPython` und `-Force` für Anpassung.
- `Windows SDK Auto-Installation (bootstrap.ps1)`: Wenn Clang als Compiler gewählt wird und kein Windows SDK vorhanden ist, wird es automatisch installiert: Methode 1 via `winget install Microsoft.WindowsSDK.10.0.26100 --silent`, Methode 2 via `winsdksetup.exe /quiet /features OptionId.DesktopCPPx64` (Standalone-Installer von go.microsoft.com). Bei MSVC-Wahl (`-Compiler msvc`) wird das SDK über die VS Build Tools mitinstalliert (`Microsoft.VisualStudio.Component.Windows11SDK.22621`).
- `tools/bootstrap.sh (Linux/macOS)`: Äquivalentes Bootstrap-Skript für Unix. Installiert CMake und Ninja als portable Downloads in `Tools/`. Compiler (Clang/GCC) müssen über System-Paketmanager installiert sein — Skript prüft Verfügbarkeit und zeigt Installationsanweisungen. Prüft Python3 Dev-Headers (`Python.h`), OpenGL-Headers und X11-Headers (Linux). Generiert `Tools/env.sh`.
- `build.bat (Windows)`: Zentrales Build-Entry-Point. Auto-Erkennung von CMake (Tools/ → VS-bundled → PATH), Compiler (MSVC → Clang+Ninja → Ninja), und Generator-Wahl. Unterstützt `release|debug|runtime|configure|clean|bootstrap` als Argumente. Kein manuelles Umgebungs-Setup nötig.
- `build.sh (Linux/macOS)`: Äquivalentes Build-Entry-Point für Unix. Gleiche Argument-Syntax wie build.bat. Bevorzugt Clang über GCC, Ninja als Generator wenn verfügbar.
- `tools/README.md`: Vollständige Dokumentation des Bootstrap-Systems: Schnellstart, Parameter-Referenz, Verzeichnisstruktur nach Bootstrap, Abhängigkeitstabelle, Download-Größen, 3 Setup-Szenarien (VS vorhanden / Clang / MSVC neu), Compiler-Empfehlungen pro Szenario.
- `Tools/-Verzeichniskonvention`: Portable Tools werden in `Tools/cmake/`, `Tools/ninja/`, `Tools/llvm/`, `Tools/python/` installiert. `BuildSystemUI::detectCMake()` sucht bereits nach `Tools/cmake/bin/cmake.exe` als erste Priorität.

## Aktuelle Änderung (Game Build Output Reorganization: Saubere Verzeichnisstruktur)

- `Neue Output-Struktur für Game Build`:
  ```
  GameBuild/
    <GameName>.exe          (Root – umbenannte Runtime-Exe)
    game.ini                (Root – Start-Level, Window-Title, Build-Profil)
    config/
      config.ini            (Root – Renderer-/Engine-Einstellungen)
    Engine/                 (Engine-Runtime)
      *.dll                 (Alle Engine-DLLs: Logger, Core, Diagnostics, Physics, SDL3, AssetManagerRuntime, ScriptingRuntime, RendererRuntime, Python)
      Tools/
        CrashHandler.exe    (+ PDB)
      Logs/                 (Wird zur Laufzeit vom Logger erstellt)
    Content/
      content.hpk           (Gepacktes Spielinhalt-Archiv)
  ```
- `MSVC /DELAYLOAD für DLL-Subdirectory-Loading (CMakeLists.txt)`: Alle 8 Engine-SHARED-DLLs + Python-DLL werden mit `/DELAYLOAD:<dll>` gelinkt (`delayimp.lib`). Delay-loaded DLLs werden erst beim ersten Funktionsaufruf geladen, nicht beim Prozessstart — dadurch kann `SetDllDirectory()` in `main()` den Suchpfad auf `Engine/` umleiten, bevor irgendeine DLL benötigt wird. `LandscapeRuntime` ist STATIC und benötigt kein Delay-Loading.
- `SetDllDirectoryW in main.cpp`: Neuer `#if defined(_WIN32) && !ENGINE_EDITOR`-Block am Anfang von `main()`. Berechnet absoluten Pfad zu `Engine/` via `GetModuleFileNameW()` (Exe-Verzeichnis) und ruft `SetDllDirectoryW()` auf. Windows-DLL-Loader durchsucht dann auch `Engine/` bei jedem DLL-Load.
- `Logger: Konfigurierbares Log- und Tools-Verzeichnis (Logger.h/cpp)`: Neue Methoden `setLogDirectory(const std::string&)` und `setToolsDirectory(const std::string&)` mit zugehörigen Members `m_customLogDir`/`m_customToolsDir`. `initialize()` nutzt `m_customLogDir` falls gesetzt, sonst Fallback auf `current_path()/Logs`. `launchCrashHandlerProcess()` nutzt `m_customToolsDir` für CrashHandler-Pfad (Windows + Linux). Logger ist SHARED-Library und kann kein compile-time `ENGINE_EDITOR` nutzen — daher Runtime-Konfiguration.
- `main.cpp Logger-Konfiguration`: Im Runtime-Build (`!ENGINE_EDITOR`) werden `setLogDirectory("Engine/Logs")` und `setToolsDirectory("Engine/Tools")` vor `Logger::initialize()` aufgerufen.
- `BuildPipeline.cpp Output-Reorganisation`: (1) `engineDir = dstDir / "Engine"` wird früh erstellt. (2) Isoliertes Deploy-Verzeichnis: CMake-Configure erhält `-DENGINE_DEPLOY_DIR=<binaryDir>/deploy`, sodass nur Runtime-Artefakte (HorizonEngineRuntime.exe + benötigte DLLs) dort landen — keine Editor-DLLs (Renderer.dll, Scripting.dll, AssetManager.dll) oder Debug-Artefakte (OpenAL32d.dll). Deploy-Verzeichnis wird vor jedem Build bereinigt um Altlasten aus vorherigen Konfigurationen zu vermeiden. (3) DLLs aus dem Deploy-Dir nach `engineDir` kopiert. (4) Python-DLLs/Zips separat aus Editor-Verzeichnis (externe Abhängigkeit, nicht von CMake gebaut). (5) CrashHandler.exe + PDB nach `engineDir / "Tools"`. (6) HPK wird temporär als `dstDir/content.hpk` gepackt, dann nach `dstDir/Content/content.hpk` verschoben. (7) **Editor-DLL-Filter:** `isEditorOnlyDll()`-Lambda filtert `AssetManager.dll`, `Scripting.dll` und `Renderer.dll` (case-insensitive) beim DLL-Deploy heraus — die Runtime linkt gegen die `*Runtime`-Varianten (`AssetManagerRuntime.dll`, `ScriptingRuntime.dll`, `RendererRuntime.dll`). Übersprungene DLLs werden im Build-Log protokolliert.
- `AssetManager.cpp HPK-Mount-Fallback`: `loadProject()` versucht zuerst `Content/content.hpk`, fällt auf `content.hpk` im Root zurück (Backward-Kompatibilität mit älteren Builds).
- `AssetManager.cpp Packaged-Build-Erkennung`: `loadProject()` prüft bei fehlendem `.project`-File sowohl `Content/content.hpk` als auch `content.hpk` (Legacy) um Packaged Builds korrekt zu erkennen.
- `main.cpp HPK-Early-Mount-Fallback`: HPK-Pfad auf `Content/content.hpk` aktualisiert mit Legacy-Fallback auf `content.hpk`.
- `HPKReader::mount() baseDir Auto-Detection (HPKArchive.cpp)`: Nach dem Lesen der TOC wird geprüft, ob der Elternordner-Name des HPK-Archivs als Pfad-Präfix in TOC-Einträgen vorkommt (z.B. HPK in `Content/content.hpk` mit TOC-Einträgen `Content/Fonts/...`). Falls ja, wird `m_baseDir` auf den Großeltern-Ordner korrigiert (Exe-Verzeichnis statt `Content/`), damit `makeVirtualPath()` korrekte virtuelle Pfade berechnet.

## Aktuelle Änderung (Shipping Build Hardening: Editor-Code aus Runtime entfernen)

- `OpenGLRendererGizmo.cpp`: Gesamte Datei (Gizmo + Grid Rendering) in `#if ENGINE_EDITOR` gewrappt. Eliminiert Grid-Rendering, Gizmo-Achsen, Drag-Handling aus dem Runtime-Build.
- `OpenGLRendererDebug.cpp`: Gesamte Datei (Selection Outline, Collider/Streaming-Volume/Bone Debug, Rubber Band) in `#if ENGINE_EDITOR` gewrappt.
- `OpenGLRenderer.cpp renderWorld()`: 3 Editor-Visualisierungsblöcke mit `#if ENGINE_EDITOR` geschützt: (1) Pick-Buffer für selektierte Entities + Gizmo-Hover-Update, (2) Viewport-Grid + Collider/Streaming-Volume/Bone-Debug, (3) Selection-Outline + Gizmo + Rubber-Band + Sub-Viewport-Border.
- `OpenGLRenderer.cpp shutdown()`: `releasePickFbo()` + `releaseOutlineResources()` Aufrufe mit `#if ENGINE_EDITOR` geschützt.
- `OpenGLRenderer.cpp Funktionsdefinitionen`: `ensurePickFbo`, `releasePickFbo`, `renderPickBuffer`, `renderPickBufferSelectedEntities`, `pickEntityAt`, `pickEntityAtImmediate` in einem `#if ENGINE_EDITOR`-Block zusammengefasst. `ensureOutlineResources`, `releaseOutlineResources`, `getEntityRotationMatrix` in separatem `#if ENGINE_EDITOR`-Block. `computeSubViewportRects` mit `#if ENGINE_EDITOR` geschützt.
- `OpenGLRenderer.cpp Diagnostics`: Editor-only Render-Pass-Info (Pick Buffer, Grid Overlay, Collider/Bone Debug, Selection Outline, Gizmo) mit `#if ENGINE_EDITOR` geschützt. Core-Passes (Post-Process, Bloom, SSAO) bleiben ungeschützt.
- `OpenGLRenderer.cpp m_pick.dirty`: Referenz nach Entity-Refresh mit `#if ENGINE_EDITOR` geschützt.
- `Renderer.h`: Protected Editor-Members (`m_snapEnabled`, `m_gridVisible`, `m_gridSize`, `m_rotationSnapDeg`, `m_scaleSnapStep`, `m_collidersVisible`, `m_bonesVisible`, `m_viewportLayout`, `m_activeSubViewport`) mit `#if ENGINE_EDITOR` geschützt.
- `OpenGLRenderer.h`: Private Editor-Members (PickingResources, OutlineResources, GizmoResources, GridResources, m_selectedEntities, Rubber-Band-State, Pick-State) mit `#if ENGINE_EDITOR` geschützt. `computeSubViewportRects` Deklaration mit `#if ENGINE_EDITOR` geschützt.
- `main.cpp Shipping Hardening`: Metrics-Text-Variablen + Timer mit `#if !defined(ENGINE_BUILD_SHIPPING)` geschützt. Log-Datei-Öffnung bei Fehlern mit `#if !defined(ENGINE_BUILD_SHIPPING)` geschützt.

## Aktuelle Änderung (Editor-Separation Phase 11: UIManager aufteilen)

- `Phase 11 – UIManager.cpp Split`: `UIManager.cpp` (~8490 Zeilen) in `UIManager.cpp` (Core, ~3350 Zeilen) + `UIManagerEditor.cpp` (Editor, ~4340 Zeilen) aufgeteilt. 10 eigenständige `#if ENGINE_EDITOR`-Blöcke (~4845 Zeilen) nach `UIManagerEditor.cpp` verschoben: World Outliner, Content Browser, Entity-Operationen (Copy/Paste/Duplicate, Prefabs, Templates, Auto-Collider, Surface-Snap), Popup-Builder (Landscape Manager, Engine/Editor Settings, Shortcut Help, Notification History, Asset References), Tab-Management (Console, Profiler, Audio Preview, Particle Editor, Shader Viewer, Render Debugger, Sequencer, Level Composition, Animation Editor, UI Designer, Widget Editor), Build-System-Delegates, Progress Bars, StatusBar-Refresh, Save/Level-Load Progress, DPI-Rebuild, Theme-Update, Toast-Widget-Erstellung. `UIManagerEditor.cpp` in `RENDERER_CORE_EDITOR_SOURCES` (nur Editor-Build). Core-Funktionen (Layout, Input, Hit-Testing, Hover, Drag&Drop) verbleiben in `UIManager.cpp` mit kleinen internen `#if ENGINE_EDITOR`-Guards.

## Aktuelle Änderung (Editor-Separation Phase 10: Renderer.h bereinigen – IEditorRenderer Interface)

- `Phase 10 – IEditorRenderer Interface extrahiert`:

## Aktuelle Änderung (Editor-Separation Phase 9: Projekt-Selektion extrahiert)

- `Phase 9 – Projekt-Selektion nach ProjectSelector`:

## Aktuelle Änderung (Editor-Separation Phase 5+7: Shortcuts & Context Menus extrahiert)

- `Phase 5 – Shortcuts nach EditorApp`: Alle 19 Editor-only `ShortcutManager::registerAction()`-Aufrufe aus main.cpp nach `EditorApp::registerShortcuts()` verschoben (~370 Zeilen entfernt). Beinhaltet: Ctrl+Z/Y (Undo/Redo), Ctrl+S (Save), Ctrl+F (Search), Ctrl+C/V/D (Copy/Paste/Duplicate), F11/F8 (UI/Bounds Debug), Escape/Shift+F1 (PIE Stop/Pause), W/E/R (Gizmo), F (Focus), End (Drop-to-Surface), F1 (Help), F2 (Import), Delete (Asset/Entity Delete mit Undo/Redo). In main.cpp verbleiben nur 3 shared Debug-Shortcuts (F10 ToggleMetrics, F9 ToggleOcclusionStats, F12 ToggleFPSCap) mit bidirektionaler Sync zu EditorApp.
- `Phase 7 – Kontextmenüs nach EditorApp`: Content-Browser-Rechtsklick-Menü (~700 Zeilen) aus main.cpp nach `EditorApp::handleContentBrowserContextMenu()` verschoben. Alle Items: New Folder, New Script, New Level (PopupWindow), New Widget, New Material (PopupWindow mit Formular), Save as Prefab, Find References, Show Dependencies. In main.cpp ersetzt durch einen einzigen `editorApp->handleContentBrowserContextMenu(mousePos)` Aufruf.
- `State-Sync zwischen main.cpp und EditorApp`: Bidirektionale Synchronisation für PIE-State (pieMouseCaptured, pieInputPaused, preCaptureMousePos), rightMouseDown, cameraSpeedMultiplier, showMetrics, showOcclusionStats. EditorApp ist Source-of-Truth; main.cpp synchronisiert am Frame-Start und aktualisiert EditorApp bei direkten Änderungen (Scroll-Wheel, Right-Click).
- `Unused Includes entfernt`: `AssetCooker.h`, `PopupWindow.h`, `UndoRedoManager.h`, `BuildPipeline.h` aus main.cpp entfernt (Code in EditorApp verschoben).
- `EditorApp.h aktualisiert`: `handleContentBrowserContextMenu(const Vec2&)` public, `handleDelete()` als `bool` return, `setRightMouseDown()`, `MathTypes.h` Include hinzugefügt.

## Aktuelle Änderung (Editor-Separation Phase 3: EditorApp Lifecycle verdrahtet)

- `EditorApp in main.cpp instanziiert`: `EditorBridgeImpl` + `EditorApp` werden nach Renderer-Init via `std::make_unique` erzeugt (innerhalb `#if ENGINE_EDITOR`, nur wenn `!isRuntimeMode`). 4 Lifecycle-Calls verdrahtet:
  - `editorApp->initialize()`: Ersetzt DPI-Rebuild, markAllWidgetsDirty(), "Engine ready!"-Toast und Build-Pipeline-Registration (startAsyncToolchainDetection + setOnBuildGame) — diese Duplikate wurden aus main.cpp Phase 3 entfernt.
  - `editorApp->tick(dt)`: Ersetzt pollBuildThread() + pollToolchainDetection() im Main Loop.
  - `editorApp->processEvent(event)`: Ersetzt direkten routeEventToPopup()-Aufruf im Event-Loop.
  - `editorApp->shutdown()`: Ersetzt Editor-Camera-Capture + saveActiveLevel bei Shutdown. Shortcut-Saves und DiagnosticsManager-Config-Saves bleiben vorerst in main.cpp.
- `CMake-Fix: ENGINE_EDITOR=1 für Editor-Target`: `target_compile_definitions(Editor PRIVATE ENGINE_EDITOR=1)` zu `src/Editor/CMakeLists.txt` hinzugefügt. Ohne dieses Define wurden alle `#if ENGINE_EDITOR`-guardierte Dateien (EditorApp.cpp, EditorBridgeImpl.cpp) zu leeren Translation Units kompiliert.
- `EditorBridgeImpl von Core nach Editor verschoben`: `EditorBridgeImpl.h/cpp` aus `src/Core/CMakeLists.txt` entfernt und zu `src/Editor/CMakeLists.txt` hinzugefügt (physisch weiterhin in `src/Core/`). Core enthält nur noch das Interface `IEditorBridge.h`. Das Editor-OBJECT-Library kompiliert die Implementierung mit ENGINE_EDITOR=1.

## Aktuelle Änderung (Editor-Separation Phase 2: IEditorBridge API & EditorApp Skeleton)

- `IEditorBridge – Abstrakte Editor-API (src/Core/IEditorBridge.h)`: Neues Interface (~234 Zeilen) als saubere API-Grenze zwischen Engine und Editor. ~50 rein virtuelle Methoden in 10 Kategorien: Renderer/Window (getRenderer, getUIManager, getWindow, preloadUITexture, createWidgetFromAsset), Camera (get/set Position/Rotation, move/rotate), Entity (create/remove/select/invalidate), Assets (load/save/import/delete/move, getProjectPath, findReferencesTo, getAssetDependencies), Level (loadLevel, saveActiveLevel, captureEditorCameraToLevel, restoreEditorCameraFromLevel), PIE (start/stop, initializePhysicsForPIE, snapshotEcsState, findActiveCameraEntity), Physics (raycastDown), Diagnostics (get/setState, requestShutdown), Scripting (reloadScripts, loadEditorPlugins), Undo/Redo (push/undo/redo/clear), Audio (stopAllAudio). Nested Structs: RaycastResult, AssetReference, UndoCommand. Nur Forward-Declarations + MathTypes.h als Dependency.
- `EditorBridgeImpl – Konkrete Implementierung (src/Core/EditorBridgeImpl.h/cpp)`: Konkrete Implementierung (~450 Zeilen) delegiert alle IEditorBridge-Methoden an Engine-Singletons (ECS::ECSManager, AssetManager, DiagnosticsManager, PhysicsWorld, Scripting, UndoRedoManager, AudioManager). Vollständig `#if ENGINE_EDITOR`-guardiert. Konstruktor nimmt `Renderer*`. In `src/Core/CMakeLists.txt` integriert.
- `EditorApp – Editor-Lifecycle-Klasse (src/Editor/EditorApp.h/cpp)`: Neues Editor-Modul (~170 Zeilen) mit Lifecycle: `initialize()`, `processEvent()`, `tick()`, `shutdown()`, `stopPIE()`. Empfängt `IEditorBridge&` und kapselt Editor-spezifische Logik. Registriert Build-Pipeline-Callback, preloaded Editor-Textures, DPI-Rebuild. Placeholder-Methoden für Widget-/ClickEvent-/DragDrop-Migration (Phase 3+).
- `src/Editor/CMakeLists.txt`: Neues OBJECT-Library-Target `Editor` mit ENGINE_EDITOR=1. Verlinkt mit SDL3, Logger, Core.
- `CMakeLists.txt (Root)`: `add_subdirectory(src/Editor)` hinzugefügt. `Editor` OBJECT-Library mit `HorizonEngine` verlinkt. Editor-Include-Directory zu HorizonEngine-Target hinzugefügt.
- `src/Core/CMakeLists.txt`: `IEditorBridge.h`, `EditorBridgeImpl.h`, `EditorBridgeImpl.cpp` zu Core-Library hinzugefügt. Include-Directories erweitert um Diagnostics, Physics, Scripting, Python.
- `main.cpp`: Includes für `IEditorBridge.h`, `EditorBridgeImpl.h`, `EditorApp.h` hinzugefügt (innerhalb `#if ENGINE_EDITOR`-Block).
- `EDITOR_SEPARATION_PLAN.md`: Umfassender 11-Phasen-Migrationsplan erstellt. Ziel: main.cpp von ~2.800 auf ~300 Zeilen mit nur 3 `#if ENGINE_EDITOR`-Guards.

## Aktuelle Änderung (Memory-Management & Performance-Optimierung: Rendering Hot Path)

- `UIManager Layout Scratch-Vektoren`: 5 temporäre `std::vector`-Allokationen pro `updateLayouts()`-Aufruf (Top/Bottom/Left/Right/Other + orderedEntries) durch 6 Member-Scratch-Vektoren (`m_layoutOrderedScratch`, `m_layoutTopScratch` etc.) mit `clear()`+Reuse-Pattern ersetzt. Eliminiert Heap-Churn bei jedem Layout-Pass. **UIManager.h:** 6 neue Member-Vektoren. **UIManager.cpp:** `updateLayouts()` nutzt `clear()` statt lokaler Deklarationen.
- `GarbageCollector O(1) Duplikat-Check`: `registerResource()` scannte linear alle `m_trackedResources` mit `weak_ptr::lock()` auf jedes Element — O(n) pro Registrierung. **Fix:** `std::unordered_set<const EngineObject*> m_registeredPtrs` als Hilfsindex. Insert prüft O(1) ob Pointer bereits registriert. `collect()` baut Set nach Pruning neu auf. **GarbageCollector.h:** `#include <unordered_set>` + neuer Member. **GarbageCollector.cpp:** `registerResource()`, `collect()`, `clear()` aktualisiert.
- `DrawCmd MaterialOverrides Pointer statt Kopie`: `DrawCmd::overrides` war `ECS::MaterialOverrides` by-value (~60+ Bytes inline). Bei jedem Sort/Move wurde das gesamte Struct kopiert — Cache-Pollution. **Fix:** Zu `const ECS::MaterialOverrides* overrides{nullptr}` geändert (8 Bytes). Zeigt auf ECS-owned Daten. **OpenGLRenderer.h:** DrawCmd-Struct geändert. **OpenGLRenderer.cpp:** Zuweisung (`&matComp->overrides`) + 3 Verbrauchsstellen auf Pointer-Dereferenzierung mit Null-Check umgestellt.
- `shared_ptr Refcount-Vermeidung in renderWorld()`: `std::static_pointer_cast<OpenGLObject3D/2D>(...)` in World-Objects- und Groups-Schleife erzeugte pro Objekt temporäre `shared_ptr`-Kopien (atomarer Refcount-Increment/Decrement). **Fix:** `getOrCreateObject3D/2D()`-Rückgabewert per `const auto&` gebunden + `static_cast<OpenGLObject3D*>(ptr.get())` für Raw-Pointer-Zugriff. `static_pointer_cast<AssetData>` bleibt (unterschiedliche shared_ptr-Typen erfordern echten Cast). **OpenGLRenderer.cpp:** Beide Schleifen (World Objects + Groups) optimiert.
- `measureElementSize Heap-Allokation eliminiert`: `std::vector<Vec2> childSizes` wurde bei jedem rekursiven Aufruf neu alloziert — massiver Heap-Churn im gesamten Widget-Baum. **Fix:** `ChildSizeStats`-Struct mit 7 Skalar-Akkumulatoren (count, widthSum, heightSum, maxW, maxH, firstW, firstH) + `add()`-Methode. Ersetzt Vektor + 12 Post-Loop-Iterationen durch direkte Feld-Zugriffe. 3 Kind-Mess-Schleifen (ColorPicker, TreeView/TabView, StackPanel/Grid/…) akkumulieren inline. **UIManager.cpp:** `ChildSizeStats`-Struct in anonymem Namespace, alle `childSizes`-Referenzen ersetzt.

## Aktuelle Änderung (Codebase-Cleanup Quick Win: Toast-Konstanten standardisiert)

- `Toast-Konstanten standardisiert (Quick Win 10.3)`:

## Aktuelle Änderung (Codebase-Cleanup Punkt 7: Renderer.h Interface-Guards)

- `Renderer.h Interface-Guards (Punkt 7)`: ~70 editor-only virtuelle Methoden in `Renderer.h` mit `#if ENGINE_EDITOR`-Präprozessor-Guards versehen. Minimaler Ansatz statt Interface-Aufspaltung – Runtime-Interface deutlich kleiner. **Renderer.h:** 2 Guard-Blöcke (Debug-Rendering/Picking/Gizmo/Snap/Grid/Tabs + Multi-Viewport). **OpenGLRenderer.h:** ~6 Guard-Blöcke für Override-Deklarationen. **OpenGLRenderer.cpp:** Implementierungen + 4 interne Call-Sites guardiert. **OpenGLRendererGizmo.cpp:** Gizmo-Drag-Methoden guardiert. **main.cpp:** 5 Editor-only Input-Handling-Regionen guardiert (Debug-Viz, Gizmo-Drag, Rubber-Band, Picking, Multi-Viewport).

## Aktuelle Änderung (Codebase-Cleanup 2.3+2.4+2.5: Gizmo, Grid & Tab-System extrahiert)

- `OpenGLRendererGizmo.cpp – Gizmo+Grid Split`: 14 Gizmo-/Grid-Methoden aus `OpenGLRenderer.cpp` in `OpenGLRendererGizmo.cpp` (846 Zeilen) verschoben: `ensureGizmoResources`, `releaseGizmoResources`, `ensureGridResources`, `releaseGridResources`, `drawViewportGrid`, `getGizmoWorldAxis`, `renderGizmo`, `pickGizmoAxis`, `beginGizmoDrag`, `updateGizmoDrag`, `endGizmoDrag` + statische Helper `buildCircleVerts`, `screenToRay`, `closestTOnAxis`.
- `OpenGLRendererTabs.cpp – Tab-System Split`: 7 Editor-Tab-Methoden in `OpenGLRendererTabs.cpp` (294 Zeilen) verschoben: `addTab`, `removeTab`, `rebuildTitleBarTabs`, `setActiveTab`, `getActiveTabId`, `getTabs`, `releaseAllTabFbos`. `getEntityRotationMatrix` bleibt im Hauptfile (shared von Debug + Gizmo). **CMakeLists.txt:** Beide Dateien zu `OPENGL_RENDERER_SOURCES` hinzugefügt. **Ergebnis:** `OpenGLRenderer.cpp` von ~11.018 auf ~9.906 Zeilen (-1.112 Zeilen).

## Aktuelle Änderung (Codebase-Cleanup 2.2: Debug-Rendering extrahiert)

- `OpenGLRendererDebug.cpp – Debug-Rendering Split`: 5 Debug-Rendering-Methoden (`drawSelectionOutline`, `renderColliderDebug`, `renderStreamingVolumeDebug`, `renderBoneDebug`, `drawRubberBand`) aus `OpenGLRenderer.cpp` in eigene Übersetzungseinheit `OpenGLRendererDebug.cpp` verschoben. Statischer `buildCircleVerts`-Helper als Kopie. **Ergebnis:** `OpenGLRenderer.cpp` von ~11.563 auf ~11.018 Zeilen (-545 Zeilen).

## Aktuelle Änderung (Codebase-Cleanup 2.1: UI-Rendering vereinheitlicht)

- `renderWidgetElement – Unified UI Rendering`: Drei nahezu identische `renderElement`-Lambdas (~450–800 Zeilen je) in `drawUIWidgetsToFramebuffer`, `renderViewportUI` und `renderUI` (OpenGLRenderer.cpp) durch eine einzige `renderWidgetElement()`-Methode (~665 Zeilen) ersetzt. `UIRenderContext`-Struct (OpenGLRenderer.h) mit `projection`, `screenHeight`, `debugEnabled`, `scissorOffset` (Vec2) und `DeferredDropDown`-Vektor-Pointer. Die drei Rendering-Methoden sind jetzt dünne Wrapper (UIRenderContext-Setup + Widget-Iteration). `scissorOffset` löst den Viewport-UI-Offset. Widget-Editor-Preview-FBO nutzt separaten `previewCtx`. **Ergebnis:** `OpenGLRenderer.cpp` von ~12.787 auf ~11.563 Zeilen (-1.224 Zeilen).

## Aktuelle Änderung (Toast/Modal Messages Editor-Only)

- `Toast- und Modal-Nachrichten aus Runtime-Builds entfernt`: Toast-Notifications und modale Dialoge waren in allen Runtime-Builds sichtbar. **Root Cause:** `showToastMessage()`, `showModalMessage()`, `closeModalMessage()`, Notification-Polling und Toast-Timer in `UIManager.cpp` sowie `enqueueToastNotification()`/`enqueueModalNotification()` in `DiagnosticsManager.cpp` hatten keine `#if ENGINE_EDITOR`-Guards. **Fix:** (1) `UIManager.cpp`: Alle Toast/Modal-Methodenrümpfe (`showModalMessage`, `closeModalMessage`, `showToastMessage` ×2, `ensureModalWidget`, `createToastWidget`, `updateToastStackLayout`) in `#if ENGINE_EDITOR` gewrappt — leere No-Ops in Runtime. (2) `updateNotifications()`: Notification-Polling aus DiagnosticsManager und Toast-Timer/Fade-Logik in eigenen `#if ENGINE_EDITOR`-Block. Shared-Logik (Hover-Transitions, Scrollbar-Visibility) bleibt. (3) `DiagnosticsManager.cpp`: `enqueueModalNotification`/`enqueueToastNotification` Bodies hinter `#if ENGINE_EDITOR` (No-Op in Runtime). `consumeModalNotifications`/`consumeToastNotifications` geben in Runtime leere Vektoren zurück. (4) Deklarationen bleiben sichtbar (71+ Aufrufstellen in Shared-Code). Ergebnis: Kein Toast, kein Modal in Runtime-Builds.

## Aktuelle Änderung (PIE-Entfernung aus Runtime/Packaged Builds)

- `PIE-Konzept aus Runtime-Builds entfernt`: Runtime/Packaged Builds benutzten `diagnostics.setPIEActive(true)` als Hack, damit Physik und Scripting liefen. PIE (Play In Editor) ist ein reines Editor-Konzept. **Build-Fehler-Root-Cause:** PIE.Stop/PIE.ToggleInput-Shortcuts referenzierten `stopPIE` (editor-only, `#if ENGINE_EDITOR`), waren aber im `#if !defined(ENGINE_BUILD_SHIPPING)`-Scope. **Fix (main.cpp):** (1) PIE.Stop und PIE.ToggleInput in `#if ENGINE_EDITOR`-Block verschoben. (2) `diagnostics.setPIEActive(true)` aus Runtime-Init entfernt. (3) Physik/Scripting-Gate zu `if (isRuntimeMode || isPIEActive())` geändert — laufen in Runtime bedingungslos. (4) Kamerabewegung, Mouse-Motion/Click/Scroll-Gates, Kamerarotation, Recapture und Script-Key-Events um `isRuntimeMode` erweitert. Ergebnis: Sauberer Game-Loop ohne PIE-Abstraktion, Development/Debug-Runtime-Builds kompilieren wieder.

## Aktuelle Änderung (ShortcutManager in Debug/Development Runtime-Builds)

- `ShortcutManager in Debug/Development Runtime-Builds`: Tastenkombinationen (F10/F9/F8/F11/F12/ESC/Shift+F1) funktionierten nur im Editor, nicht in gebauten Debug/Development-Spielen. **Root Cause:** `ShortcutManager.h`-Include, alle Shortcut-Registrierungen und der Event-Dispatch waren vollständig hinter `#if ENGINE_EDITOR` — in ALLEN Runtime-Builds nicht verfügbar. **Fix (main.cpp):** Dreistufiges Guard-Modell: (1) `#include "Core/ShortcutManager.h"` zu `#if !defined(ENGINE_BUILD_SHIPPING)` verschoben. (2) Registrierungsblock aufgespalten: äußerer `#if !defined(ENGINE_BUILD_SHIPPING)` mit inneren `#if ENGINE_EDITOR`-Blöcken für Editor-only Shortcuts (Undo/Redo/Save/Search/Copy/Paste/Duplicate/Gizmo/Focus/DropToSurface/Help/Import/Delete). Debug/Runtime-Shortcuts (F11/F8/F10/F9/ESC/Shift+F1/F12) im äußeren Scope. (3) KEY_UP/KEY_DOWN Dispatch von `#if ENGINE_EDITOR` zu `#if !defined(ENGINE_BUILD_SHIPPING)` geändert. (4) Redundanter Fallback-Key-Handling-Block entfernt. Debug- und Development-Builds haben vollen ShortcutManager-Support, Shipping ist auf bare minimum gestripped.

## Aktuelle Änderung (Build-Flow Audit: Shipping-Optimierungen & Profil-Korrekturen)

- `Landscape.dll aus Shipping-Deploy entfernt`: `Landscape.dll` fehlte in der `editorOnlyDlls`-Ausschlussliste und wurde in Standalone-Builds mitkopiert. **Fix (main.cpp):** `editorOnlyDlls`-Array um `Landscape.dll` erweitert (3→4 Einträge).
- `ScriptHotReload im Shipping deaktiviert`: `InitScriptHotReload()` und `PollScriptHotReload()`/`PollPluginHotReload()` liefen auch in Shipping-Builds und scannten unnötig das Dateisystem. **Fix (main.cpp):** Init und Poll hinter `#if !defined(ENGINE_BUILD_SHIPPING)` geschützt.
- `Metrics-Formatierung im Shipping eliminiert`: ~15 `snprintf`-Aufrufe + String-Allokationen pro Frame für Metriken, die in Shipping nie angezeigt werden. **Fix (main.cpp):** Beide Metrics-Text-Blöcke (System-Metrics + GPU-Metrics) hinter `#if !defined(ENGINE_BUILD_SHIPPING)`.
- `Editor-Only Includes geschützt`: `PopupWindow.h`, `TextureViewerWindow.h`, `EditorTheme.h`, `AssetCooker.h` wurden in Runtime-Builds eingebunden. **Fix (main.cpp):** Includes hinter `#if ENGINE_EDITOR` verschoben, explizites `#include "Renderer/UIManager.h"` hinzugefügt (war vorher transitiv über PopupWindow.h).
- `Baked Build-Profil korrigiert`: `rtBuildProfile` war bei gebackenen Builds immer `"Shipping"` unabhängig vom tatsächlichen Profil. **Fix (main.cpp):** `rtBuildProfile`, `rtEnableHotReload`, `rtEnableProfiler` werden jetzt über `#if defined(ENGINE_BUILD_DEBUG)` / `ENGINE_BUILD_DEVELOPMENT` / else korrekt zur Compile-Zeit gesetzt.

## Aktuelle Änderung (Build-Profile Compile-Defines, PDB-Separation & Async Toolchain-Detection)

- `Build-Profile Compile-Time Definitions`: Drei Compile-Defines für Runtime-Build-Profile: `ENGINE_BUILD_DEBUG`, `ENGINE_BUILD_DEVELOPMENT`, `ENGINE_BUILD_SHIPPING`. Neuer CMake-Parameter `ENGINE_BUILD_PROFILE` (Default: Shipping). Build-Pipeline übergibt `-DENGINE_BUILD_PROFILE=<name>` an CMake configure. **Shipping:** Kein Overlay, keine Metrics, keine Debug-Shortcuts, stdout unterdrückt – reines Spiel. **Development:** F10-Metrics, FPS-Overlay. **Debug:** F10 Metrics + F9 Occlusion-Stats + F8 Bounds-Debug + F11 UI-Debug.
- `PDB-Dateien in Symbols/-Unterverzeichnis`: PDBs (Exe + DLLs) landen bei non-Shipping-Builds in `<OutputDir>/Symbols/` statt neben den Binaries.
- `FPS-Overlay an showMetrics gekoppelt`: FPS/Speed-Overlay nur noch sichtbar wenn showMetrics=true (Editor, Debug/Development). In Shipping komplett unsichtbar.
- `CMake/Toolchain-Erkennung async & ohne Konsolen-Popup`: `shellExecSilent()`/`shellReadSilent()` mit `CreateProcessA(CREATE_NO_WINDOW)`. `startAsyncToolchainDetection()` auf Hintergrund-Thread, `pollToolchainDetection()` im Main-Loop. Kein Startup-Blocking, keine Konsolenfenster.
- `Runtime-Skybox-Fix`: `renderer->setSkyboxPath(level->getSkyboxPath())` nach `loadLevelAsset()` im Runtime-Pfad hinzugefügt (fehlte, Editor hatte es bereits).

## Aktuelle Änderung (CMake-Build-Pipeline wiederhergestellt, VSync-Tearing-Fix & Build-Profil-Overlay)

- `Build-Pipeline: CMake-basierter Build mit inkrementellem Binary-Cache`: CMake configure + build wiederhergestellt (8 Schritte). Build-Profile werden als Compile-Defines eingebacken (`GAME_START_LEVEL`, `GAME_WINDOW_TITLE`), CMake/Toolchain-Detection mit Warnungen, `runCmdWithOutput()` mit `CREATE_NO_WINDOW`, Binary-Cache in `<Projekt>/Binary` für inkrementelle Builds. Built-exe aus Cache deployed + DLLs/PDBs/Python aus Editor-Dir. Vorbereitet für zukünftige C++ Gameplay-Logik-Kompilierung.
- `VSync Runtime-Default (Tearing-Fix)`: Screen-Tearing beim Kamera-Drehen im Runtime-Build. Editor setzt VSync=OFF, Pre-Build-Sync schrieb `VSyncEnabled=0` in game.ini, Runtime-Defaults-Block hatte keinen VSync-Fallback. **Fix (main.cpp):** `VSyncEnabled=true` als Runtime-Default hinzugefügt.
- `showMetrics an Build-Profil gekoppelt`: Performance-Overlay war in allen Runtime-Builds deaktiviert (`showMetrics = !isRuntimeMode`), obwohl `rtEnableProfiler` aus game.ini gelesen wurde. **Fix (main.cpp):** `showMetrics = !isRuntimeMode || rtEnableProfiler` — Dev/Debug-Profile (EnableProfiler=true) zeigen Overlay, Shipping (EnableProfiler=false) nicht. F10-Toggle weiterhin funktional.

## Aktuelle Änderung (Runtime-Helligkeit Fix & Performance-Overlay im Dev-Build)

- `loadConfig() Merge-Modus`:
- `config.ini NTFS-Case-Insensitivity Fix`: Auf Windows NTFS sind `Config/` und `config/` dasselbe Verzeichnis. Die Build-Pipeline kopierte `config/config.ini` **vor** dem HPK-Schritt, der `Config/` packt und anschließend löscht — `config.ini` wurde mitgelöscht. **Fix (main.cpp, Build Step 6):** `config/config.ini`-Kopie nach `remove_all("Config")` verschoben.
- `F10 Performance-Overlay im Runtime-Build`: F10-Shortcut war nur im Editor verfügbar (ShortcutManager ist `#if ENGINE_EDITOR`). **Fix (main.cpp):** Direktes F10-Key-Handling unter `#if !ENGINE_EDITOR` in der SDL-Event-Loop. `showMetrics` ist im Runtime-Modus standardmäßig `false`.
- `Post-Process-Shader Fehler-Logging`: `ensurePostProcessResources()` gab bei Fehlschlag ohne Logging `false` zurück — Post-Processing (Gamma/ToneMapping) wurde stumm deaktiviert. **Fix (OpenGLRenderer.cpp):** Error-Log mit Shader-Pfaden bei `init()`-Fehlschlag.

## Aktuelle Änderung (Vollständige Renderer-Config-Persistenz im Build)

- `Vollständige Renderer-Config-Sync`: 5 fehlende Renderer-Settings (TextureCompressionEnabled, TextureStreamingEnabled, DisplacementMappingEnabled, DisplacementScale, TessellationLevel) wurden nicht vor dem Build in den DiagnosticsManager synchronisiert. **Fix (main.cpp):** Alle 5 Settings zum Pre-Build-Sync-Block hinzugefügt. `diag.saveConfig()` wird nach dem Sync aufgerufen, damit `config/config.ini` auf Disk aktuell ist bevor der Build-Thread die Datei kopiert. Runtime-Fallback-Defaults um dieselben 5 Settings erweitert.

## Aktuelle Änderung (PythonScripting Module Split – Phase 2: Alle Module extrahiert)

- `PythonScripting Module Split (Phase 2)`: Alle 9 verbleibenden Python-Submodule aus `PythonScripting.cpp` (~3.690 Zeilen) in eigenständige Dateien extrahiert. **Ergebnis:** 13 Scripting-Dateien, ~4.670 Zeilen gesamt, `PythonScripting.cpp` auf 1.380 Zeilen reduziert (~63%). **Neue Dateien:** `EntityModule.cpp` (468Z, entity CRUD + Transform/Mesh/Light), `AudioModule.cpp` (354Z, Audio-Handle-Lifecycle + Playback), `InputModule.cpp` (307Z, Key-Callbacks + Modifier-Queries + Shared State), `CameraModule.cpp` (213Z, Position/Rotation + Transitions + Spline-Pfade), `UIModule.cpp` (489Z, Modal/Toast/Widget-Spawn + Animationen + Focus/Drag&Drop), `ParticleModule.cpp` (98Z, Emitter-Properties), `DiagnosticsModule.cpp` (126Z, Timing + Hardware-Info + State), `LoggingModule.cpp` (53Z, Log mit Level), `EditorModule.cpp` (393Z, Editor-Funktionen + Plugin-Discovery/Hot-Reload). **Architektur:** `ScriptingInternal.h` als Shared Header mit `ScriptDetail`-Namespace (Shared State + Utilities). Jedes Modul exportiert eine `Create*Module()`-Factory. `PythonScripting.cpp` behält Init/Shutdown, Script-Tick-Dispatch, Level-Script-Management, Asset-Modul, Hot-Reload und `CreateEngineModule()` mit Factory-Calls. Input-Callback-State (`s_onKeyPressed` etc.) und Invocation-Funktionen in `InputModule.cpp` definiert, über `ScriptDetail::` von `PythonScripting.cpp` referenziert. Editor-Plugin-Management (LoadEditorPlugins etc.) nach `EditorModule.cpp` verschoben.

## Aktuelle Änderung (PythonScripting Module Split – Phase 1: Math + Physics)

- `PythonScripting Module Split (Phase 1)`:

## Aktuelle Änderung (Out-of-Process CrashHandler – Pipe-basiert)

- `CrashHandler (Out-of-Process, Named-Pipe IPC)`: Vollständig überarbeiteter Out-of-Process-CrashHandler mit Named-Pipe-basierter Echtzeit-Kommunikation. Die Engine startet den CrashHandler beim Boot, sendet kontinuierlich State-Daten über eine Named Pipe, und der CrashHandler erstellt bei unerwartetem Pipe-Abbruch (Crash) einen ausführlichen Crash-Bericht mit Hardware-Info, Engine-State, Frame-Metriken, Projekt-Info, Uptime und Log-Einträgen. **CrashProtocol.h:** Shared IPC-Protokoll (Length-Prefixed Messages, 13 Tags: HB/LOG/STATE/HW/PROJ/FRAME/CRASH/QUIT/MODS/UP/CMD/CWD/VER). **CrashMain.cpp:** Pipe-Server mit `CrashReport`-Akkumulator, Report-Generierung + Dialog bei Crash, Datei-Export in `CrashReports/`. **Logger.h:** `startCrashHandler()`/`sendToCrashHandler()`/`ensureCrashHandlerAlive()`/`stopCrashHandler()` API. `void*` statt `HANDLE` im Header (kein `<Windows.h>` wegen ERROR-Makro-Konflikt). **Logger.cpp:** `log()` leitet an Pipe weiter. Windows: `CreateProcessA`→`CreateFileA`-Pipe→`WriteFile`. Linux: `fork()`→`AF_UNIX`-Socket. Prozess-Monitoring mit Restart. **main.cpp:** Startup (`startCrashHandler()`), 2s-Heartbeat im Main-Loop (State/Project/Frame/Uptime), Hardware-Info nach Renderer-Init, Shutdown (`stopCrashHandler()`). **CMakeLists.txt:** CrashHandler deployed nach `Tools/`, Include in `ENGINE_COMMON_INCLUDES`.

## Aktuelle Änderung (Level-Script HPK-Fallback & Renderer-State-Sync im Build)

- `Level-Script HPK-Fallback`: `LoadLevelScriptModule()` (PythonScripting.cpp) nutzte nur `ifstream` — Level-Skripte im HPK wurden nicht gefunden. **Fix:** HPK-Fallback-Pattern aus `LoadScriptModule()` übernommen (`makeVirtualPath` → `readFile`). Level-Skripte laden jetzt auch im Packaged Build.
- `Renderer-State-Sync vor Build`: Renderer-Settings (PostProcessing, Gamma, ToneMapping, Shadows, etc.) fehlten in `game.ini`, wenn der User sie nie in der UI toggled hatte — DiagnosticsManager kannte sie nicht. **Fix (main.cpp):** Vor Build-Thread-Start werden alle Renderer-States explizit via `setState()` in den DiagnosticsManager synchronisiert. `game.ini` enthält damit immer die korrekten Werte.

## Aktuelle Änderung (Config-Persistenz & HPK-Fallback im Packaged Build)

- `Engine-Settings via game.ini`: `loadConfig()` lief vor `current_path()`-Korrektur → Config unauffindbar. **main.cpp:** Build Step 7 schreibt alle DiagnosticsManager-States in `game.ini`. Runtime-Parser speichert unbekannte Keys als Engine-States via `setState()`. Neue `getStates()`-Methode in DiagnosticsManager. `loadConfig()` wird nach `current_path()`-Korrektur erneut versucht (nur wenn Datei existiert).
- `WorldGrid-Material im Build`: Material wurde im Editor-Deploy-Dir erstellt, Build kopierte nur aus Source-Dir. **main.cpp:** Zusätzliche Kopie von `current_path()/Content/` → Build-Output mit `skip_existing`.
- `Build-Pipeline Config-Kopie`: `Config/defaults.ini` (Projekt) und `config/config.ini` (Engine) werden ins Build-Verzeichnis kopiert.
- `Runtime Renderer-Defaults erweitert`: 12 statt 5 Fallback-Defaults.
- `Projekt-Config HPK-Fallback`: `DiagnosticsManager::loadProjectConfigFromString()` für In-Memory-INI-Parsing. AssetManager liest `defaults.ini` aus HPK bei Fehlschlag.

## Aktuelle Änderung (Runtime-Initialisierung – Helligkeit, Kamera, Physik, Input)

- `Runtime Renderer-Defaults`: Packaged Builds haben keinen gespeicherten DiagnosticsManager-Config → PostProcessing/GammaCorrection/ToneMapping blieben aus → zu helle Darstellung. **main.cpp:** Nach Wiederherstellung gespeicherter Settings werden im Runtime-Modus fehlende Einstellungen auf sinnvolle Defaults gesetzt (PostProcessing, GammaCorrection, ToneMapping, Shadows, CSM = true).
- `Runtime Physik-Initialisierung`: `PhysicsWorld::initialize()` wurde im Runtime-Modus nie aufgerufen → `step()` ohne Effekt. **main.cpp:** Nach Level-Laden wird `PhysicsWorld::Instance().initialize(Jolt)` mit Gravity (-9.81), fixedTimestep (1/60), sleepThreshold (0.05) aufgerufen.
- `CrashHandler PID-einzigartige Pipe-Namen`: Pipe-Name enthält jetzt Engine-PID (`CrashProtocol::pipeName(pid)`), sodass Editor und gebautes Spiel gleichzeitig ihren eigenen CrashHandler nutzen können. CrashHandler.exe wird im Build-Output unter `Tools/` deployed.
- `Runtime-DLLs Binary-Cache-Deploy`: Deploy-Step kopiert DLLs jetzt zuerst aus dem Binary-Cache (RendererRuntime.dll, AssetManagerRuntime.dll etc.), dann fehlende aus dem Editor-Verzeichnis. Editor-Only-DLLs werden weiterhin ausgeschlossen.
- `Runtime Kamera-Übergabe`: `renderer->setActiveCameraEntity()` fehlte → Editor-Kamera statt Entity-Kamera. **main.cpp:** ECS-Schema-Query (CameraComponent + TransformComponent) findet aktive Kamera-Entity und aktiviert sie – identisch zum Editor-PIE-Flow.
- `Runtime Mouse-Capture & Input`: `pieMouseCaptured` war false → WASD/Mausdrehung blockiert. **main.cpp:** `pieMouseCaptured=true`, `pieInputPaused=false`, SDL relative mouse mode + grab + hide cursor – spiegelt Editor-PIE-Button-Flow.

## Aktuelle Änderung (Mesh- & Script-Loading HPK-Fallbacks)

- `Mesh-Loading HPK-Fallback`: Model3D-Assets werden im Cooker als `.cooked` (CMSH-Binär) geschrieben, nicht als `.asset`. **AssetManager.cpp:** `loadObject3DAsset()` erstellt ein Stub-`RawAssetData` wenn `.cooked` im HPK vorhanden ist — `prepare()` nutzt dann `FindCookedMeshPath`/`LoadCookedMesh`. **OpenGLObject3D.cpp:** `IsCookedMesh()` hat HPK-Fallback: liest erste 4 Bytes aus HPK und prüft `CMSH_MAGIC`.
- `Script-Loading HPK-Fallback`: **PythonScripting.cpp:** `LoadScriptModule()` liest `.py`-Dateien aus HPK wenn `ifstream` scheitert. `ScriptState::loadFailed`-Flag verhindert Per-Frame-Retry-Spam.

## Aktuelle Änderung (Build-Konfigurationsprofile – Phase 10.3)

- `Build-Konfigurationsprofile (Phase 10.3)`: Build-Profil-System mit 3 Standard-Profilen (Debug/Development/Shipping). **UIManager.h:** `BuildProfile`-Struct (name, cmakeBuildType, logLevel, enableHotReload, enableValidation, enableProfiler, compressAssets). `loadBuildProfiles()`/`saveBuildProfile()`/`deleteBuildProfile()`. `BuildGameConfig` erweitert: `binaryDir` + `profile`. **UIManager.cpp:** Profile als JSON in `<Projekt>/Config/BuildProfiles/`. Build-Dialog: Profil-Dropdown, Profil-Info-Zeile, standardisierte Pfade (`<Projekt>/Build`, `<Projekt>/Binary`). **main.cpp:** 7-Schritt-Pipeline: Binary-Cache in `<Projekt>/Binary` (persistent), profilabhängiger CMake-BuildType, game.ini-Generierung mit Profil-Settings. **Compile-Time Defines (CMakeLists.txt):** `-DENGINE_BUILD_PROFILE=<name>` setzt `ENGINE_BUILD_SHIPPING` (kein Overlay/Metrics/Debug), `ENGINE_BUILD_DEVELOPMENT` (F10 Metrics, FPS-Overlay) oder `ENGINE_BUILD_DEBUG` (alle Dev-Tools: F10/F9/F8/F11). **PDBs:** Non-Shipping-Builds kopieren PDB-Dateien in `<OutputDir>/Symbols/` statt neben die Binaries.

## Aktuelle Änderung (Editor/Engine-Separation – Phase 12.1 Komplett)

- `Editor/Engine-Separation (Phase 12.1 Komplett)`: Vollständige Trennung von Editor- und Runtime-Code über `#if ENGINE_EDITOR`-Präprozessor-Guards in allen Kern-Quelldateien. **CMakeLists.txt (3 Dateien):** Duales OBJECT-Library-System – `RendererCore` (ENGINE_EDITOR=1, alle Quellen) + `RendererCoreRuntime` (ENGINE_EDITOR=0, nur Common-Quellen) → `Renderer` (SHARED) + `RendererRuntime` (SHARED). Getrennte `ENGINE_COMMON_SOURCES`-Listen in Renderer- und OpenGLRenderer-CMakeLists. **UIManager.h:** Monolithischer Guard-Block (319–808) in ~6 gezielte Blöcke aufgespalten. Runtime-benötigte Members herausgelöst: bindClickEvents, Double-Click/Hover/Tooltip-State, Drag-API, Drop-Callbacks (std::function-Typen). **UIManager.cpp:** Inline-Guards für Notification-History, Dropdown, Outliner. handleRightMouseDown/Up mit Editor/Runtime-Stubs. handleMouseMotionForPan in handleMouseMotion inlined. **OpenGLRenderer.cpp:** Guards für Selektion-Sync, Sequencer-Spline, UI-Rendering-Pause, Widget-Editor-Canvas/FBO. **main.cpp:** Guards für Build-Thread, Popup-Routing, Texture-Viewer (4×), Content-Browser-Kontextmenü (~700 Zeilen). Beide Targets kompilieren fehlerfrei. Inkrementelle Builds bleiben möglich.

## Aktuelle Änderung (Build-Output-Scroll-Fix & Build-Abbruch-Button)

- `Build-Output-Scroll-Fix`:
- `Build-Abbruch-Button`: Roter „Abort Build"-Button (`BP.AbortBtn`) im Build-Popup. **UIManager.h:** `m_buildCancelRequested` (atomic). **main.cpp:** `checkCancelled()`-Lambda zwischen Steps, `TerminateProcess()` bei laufendem Prozess. Button wird nach Build-Abschluss ausgeblendet.

## Aktuelle Änderung (Build-System-Verbesserungen: OS-Fenster, Konsole versteckt, Python-Deploy, Runtime-Splash-Skip)

- `Build-Output als eigenes OS-Fenster`: Build-Output wird in einem separaten OS-Fenster (PopupWindow mit eigenem SDL-Window + UIManager + Render-Context) angezeigt. **UIManager.cpp:** `showBuildProgress()` nutzt `m_renderer->openPopupWindow("BuildOutput", ...)`. Widget auf Popup-UIManager registriert. `dismissBuildProgress()` schließt das Popup via `closePopupWindow()`. Output-Panel nutzt `fillY = true` für korrektes Scrollen.
- `Konsolen-Fenster versteckt`: Build-Prozesse erzeugen kein sichtbares Konsolenfenster mehr. **main.cpp:** `runCmdWithOutput()` nutzt `CreateProcess()` + `CREATE_NO_WINDOW` + Pipe-Redirection statt `_popen()` auf Windows.
- `Python-Runtime im Build`: Python-DLL und -ZIP werden automatisch ins Build-Verzeichnis kopiert. **main.cpp:** Deploy-Step sucht in Build-Dir, SDL_GetBasePath() und Engine-Source-Dir nach `python*.dll`/`python*.zip`.
- `Runtime-Splash-Skip`: Gebautes Spiel überspringt Splash-Screen, lädt Start-Level direkt im Play-Modus (PIE). **main.cpp:** `useSplash = false` bei `isRuntimeMode`. Beendigung via Alt+F4.

## Aktuelle Änderung (Build-Fenster als persistentes Popup)

- `Build-Fenster Popup`: Das Build-Fortschritts-Fenster verschwindet nicht mehr automatisch nach Build-Abschluss. Stattdessen wird die UI aktualisiert (Titel → „Build Completed"/„Build Failed", farbiger Ergebnis-Text, Close-Button sichtbar) und der Benutzer kann das Popup manuell schließen. **UIManager.h:** Neue Methode `dismissBuildProgress()`. **UIManager.cpp:** `closeBuildProgress()` blendet Fortschrittsbalken/Zähler aus und zeigt Ergebnis + Close-Button. `dismissBuildProgress()` entfernt das Widget. `showBuildProgress()` erstellt versteckten Ergebnis-Text (`BP.Result`) und Close-Button (`BP.CloseBtn`).

## Aktuelle Änderung (Asynchroner Build-Thread & CMake-Output-Log)

- `Build-Thread & Output-Log`: Build Game läuft in eigenem `std::thread` – Editor-UI bleibt responsiv. CMake-Output wird zeilenweise via `_popen()`/`popen()` + `2>&1` erfasst und in scrollbarem Log-Panel im Build-Dialog angezeigt. **UIManager.h:** `appendBuildOutput()` (thread-safe), `pollBuildThread()` (main-thread polling), `isBuildRunning()`. Build-Thread-State: `m_buildThread`, `m_buildRunning` (atomic), `m_buildMutex`, pending-Felder. **UIManager.cpp:** Scrollbares Output-Panel in `showBuildProgress()`, `pollBuildThread()` überträgt Output-Zeilen + Step-Updates + Finish vom Worker-Thread in die UI. **main.cpp:** Komplette Build-Pipeline in Thread-Lambda, `std::system()` durch `_popen()` ersetzt, `pollBuildThread()` pro Frame aufgerufen. **CMakeLists.txt:** `CMAKE_GENERATOR` als Compile-Define.

## Aktuelle Änderung (Phase 8.3 – Keyboard-Navigation)

- `Keyboard-Navigation (Phase 8.3)`: Grundlegende Keyboard-Navigation im Editor. **UIManager.cpp `handleKeyDown()`:** Escape-Kaskade (Dropdown→Modal→Entry-Fokus schließen), Tab/Shift+Tab zykelt durch alle sichtbaren EntryBar-Elemente via `cycleFocusedEntry()`, Pfeiltasten navigieren Outliner-Entities (`navigateOutlinerByArrow()`) und Content-Browser-Grid (`navigateContentBrowserByArrow()`). **UIManager.h:** Neue Methoden `collectFocusableEntries()`, `cycleFocusedEntry()`, `navigateOutlinerByArrow(int)`, `navigateContentBrowserByArrow(int, int)`. **OpenGLRenderer.cpp:** Fokussierte EntryBars erhalten blauen Outline-Highlight (`drawUIOutline` mit Accent-Blau) in beiden Rendering-Pfaden.

## Aktuelle Änderung (Build Game – CMake-basierte Kompilierung)

- `Build Game CMake-Rework`: Build Game kompiliert jetzt die Engine mit eingebackenen Projektdaten statt nur Dateien zu kopieren. **CMakeLists.txt:** `ENGINE_SOURCE_DIR`-Define für den Editor. `GAME_START_LEVEL`/`GAME_WINDOW_TITLE`-Optionen als Compile-Defines in `HorizonEngineRuntime`. Runtime-Exe nach `Tools/` verschoben. **main.cpp:** Runtime-Modus nutzt zuerst compile-time `GAME_START_LEVEL`, fällt auf `game.ini` zurück. Build-Pipeline: CMake configure (`-DGAME_START_LEVEL`, `-DGAME_WINDOW_TITLE`), CMake build (`--target HorizonEngineRuntime --config Release`), Deploy (exe umbenannt zu `<WindowTitle>.exe` + DLLs), Content/Shaders/Registry kopieren, Build-Intermediates aufräumen. CMake-Detection beim Editor-Start (`detectCMake()`): prüft bundled `Tools/cmake/bin/`, System-PATH, Standard-Installationsorte. Popup bei fehlendem CMake mit Link zur Download-Seite. **UIManager.h/cpp:** `detectCMake()`/`isCMakeAvailable()`/`getCMakePath()`/`showCMakeInstallPrompt()`.

## Aktuelle Änderung (Crash-Safe Logging & Build-Pipeline-Absicherung)

- `Crash-Safe Logging`: Logger flusht jetzt nach jedem Schreibvorgang sofort auf die Festplatte (`logFile.flush()` + `std::endl` für stdout), sodass bei einem Crash alle Log-Einträge erhalten bleiben. Neue `flush()` Public-Methode. Neue `installCrashHandler()` Methode: Auf Windows `SetUnhandledExceptionFilter` mit Stack-Trace via `DbgHelp` (`StackWalk64`/`SymFromAddr`/`SymGetLineFromAddr64`), auf anderen Plattformen `std::signal`-Handler (SIGSEGV/SIGABRT/SIGFPE/SIGILL). Zusätzlich `std::set_terminate` für unhandled C++ Exceptions. Crash-Handler loggt Exception-Code, Adresse, Beschreibung und vollständigen Stack-Trace (bis 32 Frames) vor dem Beenden. Build-Pipeline (`setOnBuildGame`-Callback) ist jetzt in `try/catch` gewrappt – Exceptions in `std::filesystem::copy` oder Render-Aufrufen während des Builds werden abgefangen und als Fehlermeldung angezeigt statt die Engine zu crashen. `advanceStep()` loggt jeden Build-Schritt und fängt Render-Exceptions separat ab.

## Aktuelle Änderung (Runtime Auto-Resolution & Fullscreen)

- `Runtime Auto-Resolution & Fullscreen`: Das Runtime-Fenster erkennt beim Start automatisch die native Display-Auflösung via `SDL_GetCurrentDisplayMode()` und startet immer im Fullscreen-Modus. Manuelle Angabe von Resolution und Fullscreen in `game.ini` entfällt. `BuildGameConfig`-Struct: `windowWidth`/`windowHeight`/`fullscreen`-Felder entfernt. Build-Dialog: Resolution-Eingabefelder (Width/Height) und Fullscreen-Checkbox entfernt. `game.ini`-Generierung: `WindowWidth`/`WindowHeight`/`Fullscreen`-Einträge entfernt. Runtime-Parsing: `WindowWidth`/`WindowHeight`/`Fullscreen`-Keys werden nicht mehr gelesen. Betroffene Dateien: `main.cpp`, `UIManager.h`, `UIManager.cpp`.

## Aktuelle Änderung (Standalone Game Build – Phase 10.1)

- `Standalone Game Build` (Editor Roadmap 10.1): Neuimplementierung auf Basis von Phase 12.1 (Editor-Separation). Build-Dialog als PopupWindow mit Formular: Start Level (DropDown aus AssetRegistry), Window Title (Projektname), Launch After Build-Checkbox, Output Dir (Projekt/Build) mit Browse-Button. `BuildGameConfig`-Struct in `UIManager.h`. 6-Schritt Build-Pipeline in `main.cpp`: (1) Output-Verzeichnis bereinigen (`remove_all`) und neu erstellen, (2) HorizonEngineRuntime.exe + alle DLLs kopieren, (3) Content-Ordner rekursiv kopieren, (4) AssetRegistry.bin → Config/, (5) game.ini generieren (StartLevel/WindowTitle/BuildProfile=Development), (6) Optionaler Launch via `ShellExecuteA`. Das Output-Verzeichnis wird vor jedem Build vollständig geleert, sodass keine veralteten Artefakte aus vorherigen Builds verbleiben.

## Aktuelle Änderung (Engine-Editor-API-Layer – Phase 12.1)

- `Engine-Editor-API-Layer` (Editor Roadmap 12.1): Editor-Separation-Rework – Duales CMake-Library-System und umfassende ENGINE_EDITOR-Präprozessor-Guards. **CMakeLists.txt (3 Dateien):** `RendererCore` (OBJECT, ENGINE_EDITOR=1, alle Quellen) + `RendererCoreRuntime` (OBJECT, ENGINE_EDITOR=0, Common-Quellen) → `Renderer` (SHARED) + `RendererRuntime` (SHARED). Getrennte `ENGINE_COMMON_SOURCES`-Listen (Editor-only: EditorUIBuilder, WidgetDetailSchema, EditorWindows). `HorizonEngine` (Editor) + `HorizonEngineRuntime` als separate Executables. **main.cpp:** Guards für Editor-only Includes, Projektauswahl-Screen, DPI/Theme-Setup, Plugin-Loading, Phase-3 UI-Setup (~1500 Zeilen), Keyboard-Shortcuts (~380 Zeilen), Build-Thread-Polling, Popup-Routing, Texture-Viewer (4×), Content-Browser-Kontextmenü (~700 Zeilen). Shared Variables außerhalb Guards für Game-Loop-Zugriff. **UIManager.h:** Monolithischer Guard (319–808) in ~6 gezielte Blöcke aufgespalten. Runtime-benötigte Members herausgelöst: bindClickEvents, Double-Click/Hover/Tooltip-State, Drag-API (eigener `public:`-Specifier), Drop-Callbacks (std::function-Typen). **UIManager.cpp:** Inline-Guards für Notification-History, Dropdown, Outliner. handleRightMouseDown/Up mit Editor/Runtime-Stubs. handleMouseMotionForPan in handleMouseMotion inlined. **OpenGLRenderer.cpp:** Guards für Selektion-Sync, Sequencer-Spline, UI-Rendering-Pause, Widget-Editor-Canvas/FBO. **Ergebnis:** Beide Targets kompilieren und linken fehlerfrei. Inkrementelle Builds erhalten.

## Aktuelle Änderung (Level-Streaming-UI – Phase 11.4)

- `Level-Streaming-UI` (Editor Roadmap 11.4): Sub-Level-System mit Streaming Volumes und Level-Composition-Panel. **Renderer.h:** `SubLevelEntry`-Struct (name, levelPath, loaded, visible, color Vec4) und `StreamingVolume`-Struct (center Vec3, halfExtents Vec3, subLevelIndex). Management-API: `addSubLevel(name, path)` mit automatischer 6-Farben-Palette, `removeSubLevel(index)` mit kaskadirender Volume-Entfernung, `setSubLevelLoaded/setSubLevelVisible`, `addStreamingVolume/removeStreamingVolume`, `updateLevelStreaming(cameraPos)` AABB-basiert, `m_streamingVolumesVisible`-Toggle. **UIManager.h:** `LevelCompositionState`-Struct (tabId, widgetId, isOpen, selectedSubLevel). **UIManager.cpp:** Level-Composition-Tab mit Toolbar (+ Sub-Level, - Remove, + Volume, Volumes ON/OFF), Sub-Level-Liste (Farb-Indikator, Name, Loaded/Unloaded-Status, Load/Unload-Button, Vis/Hid-Toggle, Zeilen-Selektion), Streaming-Volume-Liste (Farbe, Label mit Index/SubLevel/Position/Größe, X-Remove). **OpenGLRenderer.h/cpp:** `renderStreamingVolumeDebug()` Wireframe-Boxen (12 Kanten GL_LINES, Gizmo-Shader, farbig nach Sub-Level). **main.cpp:** "Level Composition" im Settings-Dropdown, `updateLevelStreaming()` im Main-Loop.

## Aktuelle Änderung (Asset-Referenz-Tracking – Phase 4.4)

- `Asset-Referenz-Tracking` (Editor Roadmap 4.4): UI-Integration für Asset-Referenz-Analyse im Content Browser. **PathBar-Buttons:** „Refs"-Button ruft `AssetManager::findReferencesTo()` auf und zeigt alle referenzierenden Entities/Assets als Modal-Liste (`.asset`-Datei-Scan + ECS-Entity-Scan). „Deps"-Button ruft `AssetManager::getAssetDependencies()` auf und zeigt alle Abhängigkeiten (JSON-Referenz-Parsing). Beide Buttons nur aktiv bei selektiertem Asset. **Unreferenzierte-Asset-Indikator:** `buildReferencedAssetSet()` in `UIManager` scannt alle ECS-Entities (MeshComponent→modelPath, MaterialComponent→materialPath, ScriptComponent→scriptPath) und baut `unordered_set<string>` referenzierter Asset-Pfade. Grid-Tiles für nicht-referenzierte Assets (alle Typen außer Level/Shader/Unknown) erhalten ein dezentes Punkt-Symbol (●) in `textMuted`-Farbe oben rechts – sowohl im Such-Modus als auch in der normalen Ordneransicht. **Lösch-Warnung:** Bereits implementiert in `main.cpp` – zeigt Referenzanzahl vor dem Löschen.

## Aktuelle Änderung (Cinematic Sequencer UI – Phase 11.2)

- `Cinematic Sequencer UI` (Editor Roadmap 11.2): Dedizierter Sequencer-Tab für visuelles Kamera-Pfad-Editing. **Renderer.h:** 6 neue virtuelle CameraPath-Accessoren (`get/setCameraPathPoints`, `get/setCameraPathDuration`, `get/setCameraPathLoop`), Overrides in `OpenGLRenderer`. **UIManager.h:** `SequencerState`-Struct (playing, scrubberT, selectedKeyframe, showSplineInViewport, loopPlayback, pathDuration). `openSequencerTab`/`closeSequencerTab`/`isSequencerOpen`. **UIManager.cpp:** Tab mit Toolbar (Add/Remove Keyframe +/−, Play ▶/Pause ⏸/Stop ■/Loop ↻, Spline-Toggle ◆, Duration-Label), Timeline-Bar (Keyframe-Marker accent/grau mit Selected-Highlight, roter Scrubber bei Playback), scrollbare Keyframe-Liste (Index, Position xyz, Yaw/Pitch). Add Keyframe = aktuelle Kamera-Pos/Rot. Klick auf Keyframe → `startCameraTransition(0.3s)`. 0.1s Auto-Refresh während Playback in `updateNotifications()`. **OpenGLRenderer.cpp renderWorld():** 3D-Spline-Visualisierung (100-Segment Catmull-Rom Polyline, orange GL_LINES via `m_gizmoProgram`, gelbe GL_POINTS für Kontrollpunkte, 8px Pointsize). Nur sichtbar wenn Sequencer-Tab offen + mindestens 2 Kontrollpunkte. **main.cpp:** "Sequencer" im Settings-Dropdown.

## Aktuelle Änderung (Editor-Plugin-System – Phase 11.3)

- `Editor-Plugin-System` (Editor Roadmap 11.3): Python-basiertes Plugin-System mit `engine.editor`-Submodul. **PythonScripting.cpp:** 8 Funktionen: `show_toast(message, level)` (Info/Success/Warning/Error via UIManager::showToastMessage), `get_selected_entities()` (Entity-IDs aus Renderer::getSelectedEntities), `get_asset_list(type_filter)` (AssetRegistry Dict-Liste mit name/path/type), `create_entity(name)` (Entity + NameComponent + TransformComponent), `select_entity(entity_id)` (UIManager::selectEntity), `add_menu_item(menu, name, callback)` (registriert Python-Callback für Settings-Dropdown), `register_tab(name, on_build_ui)` (Tab-Builder-Callback), `get_menu_items()` (alle Plugin-Items). `EditorModule` PyModuleDef + Toast-Konstanten (TOAST_INFO/SUCCESS/WARNING/ERROR). **Plugin-Discovery:** `LoadEditorPlugins()` scannt `Editor/Plugins/*.py`, `PyRun_String()` mit eigenem globals-Dict. **Hot-Reload:** Eigene `ScriptHotReload`-Instanz (500ms), bei Änderung werden Callbacks freigegeben und Plugins neu geladen. **PythonScripting.h:** `LoadEditorPlugins()`, `PollPluginHotReload()`, `GetPluginMenuItems()`, `GetPluginTabs()`, `InvokePluginMenuCallback()`. **main.cpp:** Phase 2d Plugin-Loading, Main-Loop Polling, `[Plugin]`-Prefix-Items im Settings-Dropdown. **engine.pyi:** `editor`-Klasse mit allen Stubs.

## Aktuelle Änderung (Multi-Viewport-Layout – Phase 11.1)

- `Multi-Viewport-Layout` (Editor Roadmap 11.1): Viewport in 1–4 Teilbereiche aufteilbar. **`ViewportLayout`-Enum** (Single/TwoHorizontal/TwoVertical/Quad) und **`SubViewportPreset`-Enum** (Perspective/Top/Front/Right) in `Renderer.h`. **`SubViewportCamera`-Struct** (Position, yawDeg, pitchDeg, preset). Virtuelle API: `set/getViewportLayout`, `set/getActiveSubViewport`, `getSubViewportCount`, `get/setSubViewportCamera`, `subViewportHitTest`. **OpenGLRenderer:** `kMaxSubViewports=4`, `m_subViewportCameras[4]` mit `ensureSubViewportCameras()` (Perspective synced von Editor-Kamera, Top -Y, Front -Z, Right -X). `computeSubViewportRects()` berechnet Pixel-Rects mit 2px Gap. `m_currentSubViewportIndex`/`m_currentSubViewportRect` steuern Render-Loop. **render():** Multi-Viewport-Schleife ruft `renderWorld()` pro Sub-Viewport. **renderWorld():** glViewport/glScissor pro Sub-Viewport, FBO-Bindung nur beim ersten, Kamera-View-Matrix aus Sub-Viewport-Kamera für Index > 0, Gizmo/Selektion/Rubber-Band nur im aktiven Sub-Viewport, blauer Rahmen-Highlight, Preset-Label via `drawText`. **moveCamera/rotateCamera:** Routing an aktiven Sub-Viewport-Kamera (Front/Right/Up-Berechnung aus Yaw/Pitch, Pitch-Clamping ±89°). **AssetManager.cpp:** Layout-Button (▣) in ViewportOverlay-Toolbar. **main.cpp:** Layout-Dropdown mit 4 Optionen (aktuelle markiert mit ">"), Sub-Viewport-Auswahl per Linksklick via `subViewportHitTest`.

## Aktuelle Änderung (Editor Roadmap Erweiterung)

- `EDITOR_ROADMAP.md Erweiterung`: Roadmap von 8 auf 11 Phasen erweitert (48+ Features). Phase 9: Erweiterte Visualisierung & Scene-Debugging (Shader Editor/Viewer, Collider-Wireframes, Bone-Debug-Overlay, Render-Pass-Debugger). Phase 10: Build & Packaging (Standalone Game Build mit Runtime-Executable, Asset-Cooking-Pipeline mit Textur→DDS/Mesh→Binär, Build-Konfigurationsprofile Debug/Development/Shipping). Phase 11: Erweiterte Editor-Workflows (Multi-Viewport-Layout 2–4 Split, Cinematic Sequencer mit 3D-Spline-Visualisierung, Python-basiertes Editor-Plugin-System `engine.editor`, Level-Streaming-UI mit Streaming-Volumes). Bestehende Statuses aktualisiert: Particle Editor (2.5) ✅, Asset Thumbnails (4.1) ✅ (FBO-Render für Model3D + Material). Sprint-Plan auf 13 Sprints erweitert. Fortschrittsübersicht-Tabelle um 13 neue Einträge ergänzt.

## Aktuelle Änderung (Content Browser / ECS)

- `Entity Templates / Prefabs (Phase 3.2)`: Neuer Asset-Typ `AssetType::Prefab` (Enum-Wert 12) für serialisierbare Entity-Vorlagen. Rechtsklick → „Save as Prefab" im Content Browser speichert die selektierte Entity mit allen 13 Komponententypen als JSON-Asset. Content Browser: eigenes Icon (`entity.png`, Teal-Tint `{0.30, 0.90, 0.70}`), Typ-Filter-Button „Prefab". Drag & Drop auf Viewport spawnt Entity an Cursor-Position. Doppelklick spawnt am Ursprung. „+ Entity"-Dropdown-Button in PathBar mit 7 Built-in-Templates (Empty Entity, Point Light, Directional Light, Camera, Static Mesh, Physics Object, Particle Emitter). Alle Operationen Undo/Redo-fähig. `UIManager`: `savePrefabFromEntity()`, `spawnPrefabAtPosition()`, `spawnBuiltinTemplate()`. Interne Helfer: `prefabSerializeEntity()`/`prefabDeserializeEntity()` für vollständige Komponenten-Serialisierung.

## Aktuelle Änderung (Toast Notification Levels – Phase 6.3)

- `Toast NotificationLevel (Phase 6.3)`: Einheitliches `NotificationLevel`-Enum (`Info`, `Success`, `Warning`, `Error`) in `DiagnosticsManager.h` definiert, von `UIManager` per `using`-Alias übernommen. `ToastNotification`-Struct um `level`-Feld erweitert. `enqueueToastNotification()` akzeptiert optionalen Level-Parameter (default `Info`). `showToastMessage()` passt Mindestdauer an (Warning ≥ 4s, Error ≥ 5s). `createToastWidget()` rendert farbigen 4px-Akzentbalken links (Theme-basiert: `accentColor`/`successColor`/`warningColor`/`errorColor`). Notification History speichert Level. Alle Aufrufer aktualisiert: `AssetManager.cpp` (7 Stellen: Import-Fehler → Error, Import-Erfolg → Success), `PythonScripting.cpp` (3 Stellen: Hot-Reload-Fehler → Error, Erfolg → Success).

## Aktuelle Änderung (Viewport)

- `Rubber-Band-Selection (Phase 5.2)`: Marquee-Selektion im Viewport. Linksklick+Drag zieht halbtransparentes blaues Auswahlrechteck auf; Mouse-Up selektiert alle Entities im Bereich über Pick-FBO-Block-Read. Ctrl+Drag für additive Selektion, Fallback auf Einzel-Pick bei kleinem Rect (<4px). `Renderer.h`: 6 virtuelle Methoden (begin/update/end/cancel/isActive/getStart). `OpenGLRenderer`: State-Members, `resolveRubberBandSelection()` (glReadPixels-Block), `drawRubberBand()` (Gizmo-Shader, Fill+Border). `main.cpp`: Mouse-Down startet Rubber-Band statt sofortigem Pick.
- `Verbessertes Scrollbar-Design (Phase 1.6)`: macOS-inspirierte Overlay-Scrollbars für alle scrollbaren Panels. Auto-Hide nach 1.5s Inaktivität mit 0.3s Fade-Out. Scrollbar-Breite: 6px default, 10px bei Hover. Abgerundete Thumb-Enden via `scrollbarBorderRadius` (3.0px). `EditorTheme` um 5 neue Felder erweitert: `scrollbarAutoHide` (bool), `scrollbarWidth` (6.0f), `scrollbarWidthHover` (10.0f), `scrollbarAutoHideDelay` (1.5s), `scrollbarBorderRadius` (3.0f) – alle DPI-skaliert, JSON-serialisiert. `WidgetElement` um Runtime-State: `scrollbarOpacity`, `scrollbarActivityTimer`, `scrollbarHovered`. `UIManager::updateScrollbarVisibilityRecursive()` steuert Fade-Logik und Hover-Erkennung (Mausposition gegen rechten Rand). Scrollbar-Rendering in allen drei Render-Pfaden (Editor UI, Viewport UI, Widget Editor FBO) über `drawUIPanel` mit Theme-Farben (`scrollbarTrack`, `scrollbarThumb`, `scrollbarThumbHover`) × `scrollbarOpacity`.
- `Animierte Übergänge / Micro-Interactions (Phase 1.5)`:
- `Modernisierte Icon-Sprache (Phase 1.3)`:
- `Tooltip-System (Phase 8.1)`:
- `Toolbar Redesign (ViewportOverlay)`: Komplette Neugestaltung der Editor-Toolbar als ein einzelner horizontaler StackPanel. Layout (links→rechts): RenderMode-Dropdown | Sep | Undo (↶) | Redo (↷) | Sep | Spacer | PIE (Play/Stop) | Spacer | Sep | Grid-Snap (#) | CamSpeed (1.0x) | Stats | Sep | Settings (Settings.png Icon). Alle Buttons `fillY=true` für volle Toolbar-Höhe. Settings-Button verwendet `Settings.png`-Icon statt Text. PIE-Button füllt die komplette verfügbare Höhe. RenderMode-Dropdown-Bug behoben: war vorher von CenterStack (from=0,0 to=1,1) überdeckt und nicht klickbar. Undo/Redo-Buttons rufen `UndoRedoManager::undo()/redo()` auf. Grid-Snap/CamSpeed/Stats sind funktionale Toggle-/Dropdown-Buttons (seit 5.1).
- `Editor Spacing & Typography (Phase 1.4)`: `EditorTheme` um `sectionSpacing` (10px), `groupSpacing` (6px) und `gridTileSpacing` (8px) erweitert – alle drei in `applyDpiScale`, `toJson`, `fromJson` integriert. `SeparatorWidget::toElement()` nutzt `theme.sectionSpacing` als oberen Margin, `theme.separatorThickness` für Trennlinien, `theme.panelBorder`-Farbe und `theme.sectionHeaderHeight` für Header-Höhe. `makeSection()` Content-Padding DPI-skaliert. Content-Browser Grid-Tiles erhalten `gridTileSpacing`-Margin für gleichmäßige Abstände. Labels (`makeLabel`) verwenden `textSecondary`, Hint-Labels (`makeSecondaryLabel`) verwenden `textMuted` – konsistente Font-Gewichtung.
- `DPI/Theme Startup Fix (v2)`: `rebuildAllEditorUI()` durch `rebuildEditorUIForDpi(currentDpi)` in `main.cpp` vor dem ersten Render ersetzt. Damit durchläuft der Startup exakt denselben vollständigen Pfad wie ein DPI-Wechsel zur Laufzeit: (1) Widget-JSON-Assets regenerieren falls nötig, (2) komplette Elementbäume aus JSON nachladen (minSize/padding/fontSize), (3) Theme-Farben anwenden, (4) dynamische Widgets neu aufbauen (Outliner, Details, ContentBrowser, StatusBar). Behebt: Skalierung wurde beim Start nicht korrekt angewendet – Buttons und Controls behielten alte/unscaled Größen.
- `DPI Scaling Phase 2 – Dynamic UI Elements`: Alle dynamisch erzeugten UI-Elemente (Outliner, EntityDetails, ContentBrowser, Dropdown-Menüs, Deferred-Dropdown-Rendering, EditorUIBuilder-Label-Breiten) auf DPI-skalierte Pixelwerte umgestellt. `EditorTheme::Scaled()` für `makeTreeRow` Label-Padding, `makeGridTile` MinSize/Label-Padding, `populateOutlinerWidget` List-Padding, `populateOutlinerDetails` Remove-Button-Size/Row-Padding/Label-MinSize/ColorPicker-MinSize, `populateContentBrowserWidget` Loading-Placeholder/PathBar-Buttons/Breadcrumb-Separator, `showDropdownMenu` ItemHeight/PadY/MenuWidth/ItemPadding, UIDesigner-Separatoren/Spacer. `makeLabel()`/`makeSecondaryLabel()` skalieren `minWidth` intern via `EditorTheme::Scaled()`. Deferred-Dropdown-Rendering in OpenGLRenderer: min ItemHeight skaliert + Background-Panel hinter Dropdown-Items.
- `DPI Scaling Rebuild Architecture`: DPI-Erkennung in `main.cpp` vor `AssetManager::initialize()` verschoben. Neue `UIManager::rebuildEditorUIForDpi(float)` Methode: pausiert Rendering → regeneriert Widget-Assets → lädt komplett nach → wendet Theme an → baut dynamische Widgets neu → setzt fort. `m_uiRenderingPaused` Flag verhindert Rendering während Rebuild. Editor-Settings DPI-Dropdown nutzt `rebuildEditorUIForDpi()`.
- `Dropdown Background Fix`: `dropdownBackground` Farbe von `{0.07, 0.07, 0.07}` auf `{0.13, 0.13, 0.16}` geändert – visuell klar unterscheidbar vom Panel-Hintergrund. `dropdownHover` angepasst auf `{0.18, 0.18, 0.22}`.
- `Editor Widget DPI Scaling (Phase 1.3)`: Alle 7 Editor-Widget-Assets vollständig DPI-skaliert (`kEditorWidgetUIVersion=5`). `S(px)`-Lambda multipliziert alle Pixel-Werte mit `EditorTheme::Get().dpiScale`. Font-Größen aus Theme-Feldern (`fontSizeHeading/Body/Small`). `checkUIVersion` validiert `_dpiScale` – automatische Neugenerierung bei DPI-Änderung.
- `Editor Widget UI Refresh (Phase 1.2)`: Alle 7 Editor-Widget-Assets auf kompakteres Design umgestellt. `kEditorWidgetUIVersion=4`. Zentrale Helper-Lambdas `checkUIVersion`/`writeWidgetAsset`. Höhen/Breiten reduziert. Alle Buttons mit `borderRadius`. `ApplyThemeToElement` setzt `borderRadius`. Titel-Labels auf 13px.
- `SDF Rounded Rect UI Rendering (Phase 1.1)`: Abgerundete Ecken für alle Editor-Widgets. GLSL-Shader `panel_fragment.glsl` und `button_fragment.glsl` auf SDF-basierte Rounded-Rect-Berechnung umgestellt (`roundedRectSDF()`, Smoothstep-AA, `uniform float uBorderRadius`). `drawUIPanel()` um `borderRadius`-Parameter erweitert. Alle drei Render-Pfade (`drawUIWidgetsToFramebuffer`, `renderViewportUI`, `renderUI`) für ~30 Element-Typen aktualisiert (Panel, Button, EntryBar, DropDown, CheckBox, ProgressBar, Slider etc.). `EditorUIBuilder`-Factories setzen `borderRadius` automatisch aus Theme. Discard bei alpha < 0.001 verhindert Overdraw.
- `EDITOR_ROADMAP.md`: Ausführliche Editor-Roadmap mit 8 Phasen, 35+ Features und Sprint-Empfehlungen erstellt. Deckt ab: Visuelles Widget-Redesign (Rounded Panels, Elevation, Icon-System, Spacing, Micro-Animations), fehlende Editor-Tabs (Console, Material Editor Tab, Texture Viewer, Animation Editor, Particle Editor, Audio Preview, Profiler), Automatisierung (Auto-Material-Import, Prefabs/Templates, Scene Setup, Auto-LOD, Auto-Collider, Snap & Grid), Content-Browser-Erweiterungen (Thumbnails, Suche/Filter, Drag & Drop, Referenz-Tracking), Viewport-UX (Viewport-Einstellungen, Multi-Select, Copy/Paste, Surface-Snap), Editor-Framework (Docking, Shortcut-System, Benachrichtigungen), Scripting-Workflow (Script-Editor, Debug-Breakpoints, Hot-Reload) und Polish (Tooltips, Onboarding, Keyboard-Navigation, Undo-Erweiterungen).
- `Displacement Mapping (Tessellation)`:
- `Cinematic-Kamera / Pfad-Follow`: Catmull-Rom-Spline-basierte Kamera-Pfade. `CameraPath.h` mit `CameraPathPoint` (Position+Yaw/Pitch) und `CameraPath` mit `evaluate(t)` für stückweise kubische Interpolation und Loop-Modus. 6 neue Renderer-Methoden (`startCameraPath`, `isCameraPathPlaying`, `pauseCameraPath`, `resumeCameraPath`, `stopCameraPath`, `getCameraPathProgress`). Delta-gesteuerter Tick im Render-Loop, Kamera-Blockade während Playback. Python-API: `engine.camera.start_path(points, duration, loop)`, `is_path_playing()`, `pause_path()`, `resume_path()`, `stop_path()`, `get_path_progress()`.
- `Skeletal Animation`: Vollständiges Skeletal-Animation-System. Import von Bones, Weights und Animations aus FBX/glTF/DAE via Assimp (`aiProcess_LimitBoneWeights`). Daten im Asset-JSON (`m_hasBones`, `m_bones`, `m_boneIds`, `m_boneWeights`, `m_nodes`, `m_animations`). Erweiterter Vertex-Layout für Skinned Meshes (22 Floats: +boneIds4+boneWeights4). Neuer `skinned_vertex.glsl` mit `uSkinned`/`uBoneMatrices[128]` Uniforms. `SkeletalAnimator` (SkeletalData.h) für Runtime-Keyframe-Interpolation (Slerp/Lerp). Pro Skinned-Entity automatischer Animator + Auto-Play. Bone-Matrizen-Upload vor jedem Draw (einzeln, kein Instancing). Shadow-Shader mit Skinning-Support. `AnimationComponent` als 12. ECS-Komponente mit JSON-Serialisierung.
- `Particle-System`: CPU-simuliertes, GPU-instanced Partikelsystem. `ParticleEmitterComponent` als 13. ECS-Komponente (20 Parameter: emissionRate, lifetime, speed, speedVariance, size, sizeEnd, gravity, coneAngle, colorStart/End RGBA, maxParticles, enabled, loop). `ParticleSystem` (ParticleSystem.h/.cpp): Per-Emitter-Partikelpool, LCG-Cone-Emission, Gravity, Color/Size-Interpolation. GPU Point-Sprite Rendering (VBO pos3+rgba4+size1), back-to-front Sort, Soft-Circle Billboard. Render-Pass nach Opaque/vor OIT. JSON-Serialisierung. Python-API: `engine.particle` Submodul.
- `OpenGLTextRenderer`: Bugfix – Horizontale Text-Spiegelung im Viewport behoben. `renderViewportUI()` rendert jetzt im Full-FBO-Viewport (`glViewport(0,0,wW,wH)`) mit offset-verschobener Ortho-Projektion und Scissor-Clip, statt mit Offset-glViewport. Dadurch stimmt das Rendering-Setup exakt mit dem Editor-UI-Pfad (`drawUIWidgetsToFramebuffer`) überein.
- `UIWidget`: Neue `WidgetElement`-Properties ergänzt: `borderColor`, `borderThickness`, `borderRadius`, `opacity`, `isVisible`, `tooltipText`, `isBold`, `isItalic`, `gradientColor`, `maxSize`, `spacing`, `radioGroup`. Alle Properties werden serialisiert/deserialisiert (JSON).
- `UIWidget`: Neue Widget-Typen hinzugefügt: `Label` (leichtgewichtiges Text-Element), `Separator` (visuelle Trennlinie), `ScrollView` (dedizierter scrollbarer Container), `ToggleButton` (Button mit An/Aus-Zustand), `RadioButton` (Radio-Button mit Gruppen-ID). Vollständige Rendering-Unterstützung in `renderUI()` und `renderViewportUI()`.
- `UIWidgets`: Neue Helper-Klassen: `LabelWidget.h`, `ToggleButtonWidget.h`, `ScrollViewWidget.h`, `RadioButtonWidget.h`.
- `UIManager`: Layout-Berechnung und -Anordnung erweitert für alle neuen Widget-Typen und das neue `spacing`-Property in StackPanel/ScrollView.
- `UIManager`: Neue Controls (Label, ToggleButton, RadioButton, ScrollView) in der Widget-Editor-Palette (linkes Panel) und Drag&Drop-Defaults.
- `UIManager`: Details-Panel erweitert – H/V-Alignment (Left/Center/Right/Fill), Size-to-Content, Max Width/Height, Spacing, Opacity, Visibility, Border-Width/-Radius, Tooltip, Bold/Italic, RadioGroup.
- `Scripting`: Neues Python-Submodul `engine.viewport_ui` mit vollständiger API zum Erstellen und Anpassen von Viewport-UI-Widgets aus Python-Scripten (`create_widget`, `add_text`, `add_label`, `add_button`, `add_panel`, `set_text`, `set_color`, `set_text_color`, `set_opacity`, `set_element_visible`, `set_border`, `set_border_radius`, `set_tooltip`, `set_font_size`, `set_font_bold`, `set_font_italic`).
- `engine.pyi`: IntelliSense-Typen für `viewport_ui`-Modul und alle neuen Properties/Widget-Typen aktualisiert.
- `OpenGLRenderer`: Rendering-Unterstützung für neue Widget-Typen (`Label`, `Separator`, `ScrollView`, `ToggleButton`, `RadioButton`) in `renderUI()` und `renderViewportUI()`.
- `OpenGLRenderer`: In `renderWorld()` wird nach den Shadow-Pässen der Viewport jetzt wieder inklusive Content-Rect-Offset gesetzt (nicht mehr auf `(0,0)`), damit die Welt nicht in die linke untere Ecke rutscht.
- `Gameplay UI`: Vollständig implementiert – Multi-Widget-System mit Z-Order, `WidgetAnchor` (10 Positionen), implizites Canvas Panel pro Widget, Anchor-basiertes Layout (`computeAnchorPivot` + `ResolveAnchorsRecursive`), Gameplay-Cursor-Steuerung mit automatischer Kamera-Blockade, Auto-Cleanup bei PIE-Stop. Rein dynamisch per Script, kein Asset-Typ, kein Level-Bezug. Siehe `GAMEPLAY_UI_PLAN.md` Phase A.
- `UI Designer Tab`: Editor-Tab (wie MeshViewer) für visuelles Viewport-UI-Design – Controls-Palette (7 Typen), Widget-Hierarchie-TreeView, Properties-Panel (Identity/Transform/Anchor/Hit Test/Layout/Appearance/Text/Image/Value), bidirektionale Sync via `setOnSelectionChanged`, Selektions-Highlight (orangefarbener 2px-Rahmen). Öffnung über Settings-Dropdown. Siehe `GAMEPLAY_UI_PLAN.md` Phase B.
- `Material Editor Tab` (Editor Roadmap 2.2): Vollwertiger Editor-Tab für Material-Assets. Doppelklick im Content Browser öffnet Tab mit 3D-Preview (Cube + Licht + Ground Plane via Runtime-Level), rechtem Properties-Panel mit PBR-Slidern (Metallic/Roughness/Shininess), PBR-Checkbox, 5 Textur-Slot-Einträgen und Save-Button. Folgt der MeshViewerWindow-Architektur (eigenes EngineLevel, Tab-scoped Widget, Level-Swap in `setActiveTab`).
- `Scripting/UI`: `engine.viewport_ui` Python-Modul mit 28 Methoden für Gameplay-UI – Widget-Erstellung/Entfernung, 7 Element-Typen (Panel/Text/Label/Button/Image/ProgressBar/Slider), Eigenschafts-Zugriff, Anchor-Steuerung, Cursor Show/Hide. Zusätzlich `engine.ui.spawn_widget`/`remove_widget` für Widget-Asset-Spawning.
- `Widget Editor`: Content-Browser-Integration erweitert – neuer Kontextmenüpunkt **New Widget** erzeugt ein Widget-Asset (Typ `AssetType::Widget`) im Projekt-Content und öffnet es direkt in einem eigenen Editor-Tab.
- `Widget Editor`: Doppelklick auf Widget-Assets im Content Browser öffnet jetzt einen dedizierten Widget-Editor-Tab und lädt das Widget zur Bearbeitung/Vorschau.
- `Widget Editor`: Tab-Workspace ausgebaut – linker Dock-Bereich zeigt verfügbare Steuerelemente + Element-Hierarchie, rechter Dock-Bereich zeigt Asset/Widget-Details, die Mitte enthält eine dedizierte Preview-Canvas mit Fill-Color-Rahmen.
- `Widget Editor`: Steuerelement-Einträge in der linken Palette haben jetzt einen sichtbaren Hover-State (Button-Basis mit transparenter Grundfarbe + Hover-Farbe), sodass einzelne Controls beim Überfahren visuell hervorgehoben werden.
- `OpenGLRenderer`: Viewport-UI-Rendering für `Text`/`Label` sowie `Button`/`ToggleButton`/`RadioButton` an den Editor-Renderpfad angeglichen – korrekte H/V-Ausrichtung, Wrap-Text-Unterstützung und bessere Text-Fit-Berechnung statt fixer Top-Left-Ausgabe.
- `UIWidget` (Phase 3 – Animation): Zentrale Easing-Auswertung ergänzt: `EvaluateEasing(EasingFunction, t)` unterstützt jetzt alle Standardkurven (`Linear`, `Quad`, `Cubic`, `Elastic`, `Bounce`, `Back`; jeweils In/Out/InOut) mit auf `[0..1]` geklemmtem Eingangswert.
- `UIWidget` (Phase 3 – Animation): `WidgetAnimationPlayer` implementiert (`play`, `playReverse`, `pause`, `stop`, `setSpeed`, `tick`) und in `Widget` integriert. Track-Interpolation nutzt `EvaluateEasing`; Track-Werte werden auf animierbare Properties angewendet (`RenderTransform`, `Opacity`, `Color`, `Position`, `Size`, `FontSize`).
- `ViewportUIManager` / `OpenGLRenderer`: Phase-3-Tick-Integration ergänzt – `ViewportUIManager::tickAnimations(deltaTime)` tickt alle Widget-Animationen pro Frame; `OpenGLRenderer::render()` berechnet das Delta via SDL-Performance-Counter und ruft den Tick zentral auf.
- `Scripting` (Phase 3 – Animation): `engine.ui` um Animationssteuerung erweitert: `play_animation(widget_id, animation_name, from_start=True)`, `stop_animation(widget_id, animation_name)` und `set_animation_speed(widget_id, animation_name, speed)` für Laufzeit-Widgets; `engine.pyi` entsprechend synchronisiert.
- `UIManager` (Widget Editor): Rechtses Panel kann jetzt per Toggle-Button in der oberen Leiste zwischen **Details** und **Animations** umgeschaltet werden. Der Animationsmodus zeigt eine Liste vorhandener Widget-Animationen inkl. Play/Stop-Bedienung als Basis-Animations-Panel.
- `UIManager` (Widget Editor): Animations-Timeline – Redesigned Unreal-Style Bottom-Panel (260px) mit zwei Hauptbereichen: **Links (150px)** Animations-Liste mit klickbaren Einträgen und Selektions-Highlight. **Rechts** Timeline-Content: Toolbar (+Track, Play/Stop, Duration, Loop), horizontaler Split mit Tree-View-Track-Namen (aufklappbare Element-Header ▾/▸ + eingerückte Property-Tracks) und Timeline-Bereich (Ruler, Scrubber-Linie draggbar mit Echtzeit-FBO-Preview, kleinere Keyframe-Diamanten 9px, draggbare End-of-Animation-Linie zur Dauer-Änderung, Echtzeit-Keyframe-Dragging begrenzt auf [0, duration]).
- `Widget Editor`: Für Widget-Editor-Tabs wird im Renderer die 3D-Weltpass-Ausgabe unterdrückt; der tab-eigene Framebuffer wird als reine Widget-Workspace-Fläche genutzt.
- `Widget Editor`: TitleBar-Tabs werden jetzt beim Hinzufügen/Entfernen zentral neu aufgebaut, damit neue Widget-Editor-Tabs immer sichtbar sind wie beim Mesh Viewer.
- `Widget Editor`: Bugfix – Erneutes Öffnen eines Widget-Assets schlug fehl, weil `loadWidgetAsset` Content-relative Pfade nicht gegen das Projekt-Content-Verzeichnis auflöste (Disk-Load scheiterte nach Neustart oder Cache-Miss). Außerdem wird bei Ladefehler der bereits hinzugefügte Tab entfernt, um verwaiste Tabs zu vermeiden.
- `Widget Editor`: Outline-Rendering ohne Dreiecks-Diagonalen – `drawUIOutline` zeichnet nun 4 Kantenrechtecke statt GL_LINE-Wireframe.
- `Widget Editor`: Preview-Klick-Selektion korrigiert – Hit-Test nutzt `computedPositionPixels/computedSizePixels` statt expandierter Bounding-Box. Auto-ID-Zuweisung für ID-lose Elemente beim Laden.
- `Widget Editor`: H/V-Alignment im Details-Panel als DropDown-Widgets (Left/Center/Right/Fill, Top/Center/Bottom/Fill).
- `Widget Editor`: Details-Panel neu organisiert – Sektionen: Identity → Transform → Layout → Appearance → typspezifisch. ID ist jetzt editierbar.
- `Widget Editor`: Umfassender UX-Plan in `WIDGET_EDITOR_UX_PLAN.md` (5 Phasen, Priorisierungstabelle).
- `Widget Editor`: Hit-Test-Fix – Rekursive Messung aller Elemente (`measureAllElements`) und robustere Tiefensuche. Hover-Preview mit hellblauer Outline beim Überfahren von Elementen im Canvas.
- `Build/CMake`: Multi-Config-Ausgabeverzeichnisse werden nun pro Konfiguration getrennt (`${CMAKE_BINARY_DIR}/Debug|Release|...`) statt zusammengeführt, um Debug/Release-Lib-Kollisionen (MSVC `LNK2038`) zu vermeiden.
- `Gameplay UI/Designer`: Gesamtplan und Fortschritts-Tracking in `GAMEPLAY_UI_PLAN.md`. Phase A (Runtime-System) und Phase B (UI Designer Tab) vollständig implementiert. Scripting vereinfacht: `engine.viewport_ui` entfernt, nur noch `engine.ui.spawn_widget/remove_widget/show_cursor/clear_all_widgets`. Canvas-Root mit Löschschutz, Anchor-Dropdown im Details-Panel, normalisierte from/to-Werte korrekt skaliert. Phase D (UX-Verbesserungen) steht als Zukunft aus.
- `UIWidget` (Phase 4 – Border): Neuer `WidgetElementType::Border` – Single-Child-Container mit separater `borderBrush` (UIBrush) für die vier Kanten, per-Seite Dicke (`borderThicknessLeft/Top/Right/Bottom`), `contentPadding` (Vec2). JSON-Serialisierung vollständig. Helper-Klasse `BorderWidget.h`.
- `UIWidget` (Phase 4 – Spinner): Neuer `WidgetElementType::Spinner` – animiertes Lade-Symbol mit `spinnerDotCount` (default 8), `spinnerSpeed` (Umdrehungen/Sek, default 1.0), `spinnerElapsed` (Runtime-Zähler). JSON-Serialisierung (ohne Runtime-Feld). Helper-Klasse `SpinnerWidget.h`.
- `UIManager` (Phase 4): Layout/Measure für Border (Kind + Insets) und Spinner (feste Größe). Border-Arrange: Kind um borderThickness + contentPadding eingerückt. Editor-Palette, addElementToEditedWidget-Defaults und Details-Panel (Border: Dicke L/T/R/B, ContentPadding X/Y, BorderBrush RGBA; Spinner: DotCount, Speed).
- `OpenGLRenderer` (Phase 4): Rendering für Border (4 Kanten-Rects via `drawUIBrush` + Kind-Rekursion) und Spinner (N Punkte im Kreis mit Opacity-Falloff) in allen 3 Render-Pfaden (Viewport-UI, Editor-UI, Widget-Editor-Preview).
- `ViewportUIManager` (Phase 4): Spinner-Tick via `tickSpinnersRecursive` in `tickAnimations()` – inkrementiert `spinnerElapsed` für alle Spinner-Elemente pro Frame.
- `UIManager` (Phase 4): Spinner-Tick via `TickSpinnersRecursive` in `updateNotifications()` für Editor-Widgets.
- `Build/CMake`: Cross-Platform-Vorbereitung – CMake-Konfiguration für Linux- und macOS-Builds erweitert. MSVC-spezifische Optionen (CRT-Flags, PDB-Pfade, `/FS`, `/WX-`) mit `if(MSVC)` bzw. `if(WIN32)` geschützt. `WINDOWS_EXPORT_ALL_SYMBOLS` plattformbedingt gesetzt. Neues `ENGINE_PYTHON_LIB` für plattformunabhängiges Python-Linking. `find_package(OpenGL REQUIRED)` und `find_package(Threads REQUIRED)` hinzugefügt. `OpenGL::GL` im Renderer verlinkt, `${CMAKE_DL_LIBS}` auf Linux für GLAD. `CMAKE_POSITION_INDEPENDENT_CODE ON` global gesetzt. Deploy-Pfade (`ENGINE_DEPLOY_DIR`, `ENGINE_TESTS_DEPLOY_DIR`) plattformabhängig konfigurierbar. Post-Build-DLL-Kopierschritte (`$<TARGET_RUNTIME_DLLS>`, Python-Runtime) mit `if(WIN32)` geschützt. PhysX-Backend: `TARGET_BUILD_PLATFORM` erkennt automatisch windows/linux/mac, freeglut-Stubs und CRT-Overrides nur auf Windows. GCC/Clang: `-Wall -Wextra` Warnflags.
- `engine.pyi` (Phase 4): IntelliSense-Dokumentation für Border- und Spinner-Widgettypen mit allen Feldern, Layout- und Rendering-Beschreibung.
- `UIWidget` (Phase 4 – Multiline EntryBar): Neue Felder `isMultiline` (bool, default false) und `maxLines` (int, 0 = unbegrenzt) für mehrzeilige Texteingabe. JSON-Serialisierung vollständig.
- `UIManager` (Phase 4 – Multiline EntryBar): Enter-Taste fügt `\n` ein bei `isMultiline`-Modus (mit `maxLines`-Prüfung). Details-Panel: Multiline-Checkbox und Max-Lines-Property für EntryBar-Elemente.
- `OpenGLRenderer` (Phase 4 – Multiline EntryBar): Mehrzeiliges Rendering – Text wird an `\n` gesplittet und zeilenweise gezeichnet (Y-Offset = lineHeight pro Zeile). Caret auf letzter Zeile. Beide Render-Pfade (Viewport-UI, Editor-UI) aktualisiert.
- `EntryBarWidget.h` (Phase 4): `setMultiline(bool)` und `setMaxLines(int)` Builder-Methoden hinzugefügt.
- `engine.pyi` (Phase 4): IntelliSense-Dokumentation für Multiline-EntryBar (isMultiline, maxLines, Rendering-Verhalten) ergänzt.
- `UIWidget` (Phase 4 – Rich Text Block): Neuer `WidgetElementType::RichText` mit `richText`-Feld (Markup-String). Neues `RichTextSegment` Struct (text, bold, italic, color, hasColor, imagePath, imageW, imageH). `ParseRichTextMarkup()` Parser für `<b>`, `<i>`, `<color=#RRGGBB>`, `<img>` Tags mit Style-Stack für Verschachtelung. JSON-Serialisierung vollständig.
- `OpenGLRenderer` (Phase 4 – Rich Text Block): Markup-Parse → Word-Liste mit Per-Word-Style → Greedy Word-Wrap → Zeilen-Rendering mit `drawText` pro Wort und segmentspezifischer Farbe. Alle 3 Render-Pfade (Viewport-UI, Editor-UI, Widget-Editor-Preview) aktualisiert.
- `UIManager` (Phase 4 – Rich Text Block): Layout (minSize oder 200×40 Default), Palette-Eintrag „RichText", addElementToEditedWidget-Defaults, Details-Panel „Rich Text"-Markup-Feld.
- `RichTextWidget.h` (Phase 4): Neuer Builder-Header in UIWidgets – setRichText, setFontSize, setTextColor, setBackgroundColor, setPadding, setMinSize, toElement().
- `engine.pyi` (Phase 4): IntelliSense-Dokumentation für Rich-Text-Block (richText-Feld, Markup-Tags, Layout, Helper-Klasse) ergänzt.
- `UIWidget` (Phase 4 – ListView/TileView): Neue `WidgetElementType::ListView` und `WidgetElementType::TileView`. Neue Felder `totalItemCount` (int), `itemHeight` (float, 32), `itemWidth` (float, 100), `columnsPerRow` (int, 4), `onGenerateItem` (Callback). JSON-Serialisierung vollständig.
- `OpenGLRenderer` (Phase 4 – ListView/TileView): Virtualisiertes Rendering mit Scissor-Clipping, alternierenden Zeilen-/Tile-Farben, Scroll-Offset. Alle 3 Render-Pfade aktualisiert.
- `UIManager` (Phase 4 – ListView/TileView): Layout (ListView 200×200, TileView 300×200), Palette-Einträge, addElementToEditedWidget-Defaults, Details-Panel (Item Count, Item Height, Item Width, Columns).
- `ListViewWidget.h` (Phase 4): Builder-Header – setTotalItemCount, setItemHeight, setOnGenerateItem, toElement().
- `TileViewWidget.h` (Phase 4): Builder-Header – setItemWidth, setColumnsPerRow + alle ListView-Methoden, toElement().
- `engine.pyi` (Phase 4): IntelliSense-Dokumentation für ListView/TileView (Felder, Virtualisierung, Helper-Klassen) ergänzt.
- `UIWidget` / `UIManager` / `OpenGLRenderer` (Phase 4 – Integration-Fix): Fehlende Switch-Cases für Border, Spinner, RichText, ListView, TileView in `toString`/`fromString`, `measureElementSize`, `layoutElement` (Border: Kind-Inset, ListView: vertikaler Stack, TileView: Grid), Auto-ID-Zuweisung, Hierarchy-Type-Labels und Renderer-Container-Checks nachgetragen. Viewport-Designer-Palette und Creation-Defaults für alle 5 neuen Typen ergänzt. Details-Panel-Properties: Border (Dicke L/T/R/B, ContentPadding, BorderBrush RGBA), Spinner (DotCount, Speed), RichText (Markup), ListView (Item Count, Item Height), TileView (Item Count, Item Height, Item Width, Columns).
- `UIWidget` (Phase 2 – Styling):
- `UIWidget` (Phase 2 – Styling): `RenderTransform` Struct (Translation, Rotation, Scale, Shear, Pivot) für rein visuelle Per-Element-Transformation ohne Layout-Einfluss. `ClipMode` Enum (None/ClipToBounds/InheritFromParent) für Clipping-Steuerung.
- `UIWidget` (Phase 2 – Serialisierung): `readBrush`/`writeBrush` und `readRenderTransform`/`writeRenderTransform` JSON-Helfer. Rückwärtskompatibilität mit bestehenden `color`-Feldern.
- `OpenGLRenderer` (Phase 2): Neue `drawUIBrush()` Funktion für Brush-basiertes Rendering (SolidColor, Image, NineSlice-Fallback, LinearGradient mit eigenem GLSL-Shader). `m_uiGradientProgram` + `UIGradientUniforms` für Gradient-Rendering.
- `OpenGLRenderer` (Phase 2): Opacity-Vererbung in `renderViewportUI()` und `drawUIWidgetsToFramebuffer()` – `parentOpacity`-Parameter, `effectiveOpacity = element.opacity * parentOpacity`, `applyAlpha`-Helfer für alle Render-Aufrufe.
- `OpenGLRenderer` (Phase 2): Brush-basiertes Hintergrund-Rendering – `element.background` wird vor typ-spezifischem Rendering gezeichnet (wenn sichtbar).
- `Widget Editor` (Phase 2): Neue „Brush / Transform"-Sektion im Details-Panel – BrushType-Dropdown, Brush-Farbe (RGBA), Gradient-Endfarbe, Gradient-Winkel, Bild-Pfad, ClipMode-Dropdown, RenderTransform-Felder (Translate/Rotation/Scale/Shear/Pivot).
- `engine.pyi` (Phase 2): IntelliSense-Dokumentation für BrushType, UIBrush, RenderTransform, ClipMode und Opacity-Vererbung ergänzt.
- `OpenGLRenderer` (Phase 2): RenderTransform-Rendering – `ComputeRenderTransformMatrix()` berechnet T(pivot)·Translate·Rotate·Scale·Shear·T(-pivot) und multipliziert die Matrix auf die uiProjection in allen drei Pfaden (`renderViewportUI`, `drawUIWidgetsToFramebuffer`, `renderUI`). RAII-Restore-Structs stellen die Projektion automatisch wieder her.
- `ViewportUIManager` (Phase 2): RenderTransform-Hit-Testing – `InverseTransformPoint()` transformiert den Mauszeiger in den lokalen (untransformierten) Koordinatenraum. `HitTestRecursive`/`HitTestRecursiveConst` nutzen den invertierten Punkt für Bounds-Check und Child-Rekursion.
- `OpenGLRenderer` (Phase 2): ClipMode-Scissor-Stack – `ClipMode::ClipToBounds` erzeugt per RAII einen verschachtelten GL-Scissor-Bereich (Achsen-ausgerichtete Intersection mit dem aktuellen Scissor). Alle drei Render-Pfade unterstützt.
- `UIWidget` (Phase 5 – Focus System): Neues Struct `FocusConfig` (isFocusable, tabIndex, focusUp/Down/Left/Right). Neue Felder `focusConfig` (FocusConfig) und `focusBrush` (UIBrush) auf WidgetElement. JSON-Serialisierung (readFocusConfig/writeFocusConfig).
- `ViewportUIManager` (Phase 5): Focus-Manager – `setFocus()`, `clearFocus()`, `getFocusedElementId()`, `setFocusable()`. Tab/Shift+Tab-Navigation (tabIndex-sortiert), Pfeiltasten Spatial-Navigation, Enter/Space-Aktivierung, Escape zum Fokus-Löschen. Fokus-on-Click in `handleMouseDown`. `handleKeyDown(key, modifiers)` Signatur.
- `OpenGLRenderer` (Phase 5): Fokus-Highlight – Post-Render-Pass in `renderViewportUI()` zeichnet 2px-Outline mit `focusBrush.color` (Default blau).
- `UIManager` (Phase 5): Widget-Editor Details-Panel „Focus"-Sektion – Focusable-Checkbox, Tab Index, Focus Up/Down/Left/Right, Focus Brush RGBA.
- `Scripting` (Phase 5): Python API – `engine.ui.set_focus()`, `clear_focus()`, `get_focused_element()`, `set_focusable()`. `engine.pyi` mit Phase-5-Dokumentation aktualisiert.
- `UIWidget` (WidgetElementStyle-Refactoring): 14 visuelle Felder (`color`, `hoverColor`, `pressedColor`, `disabledColor`, `textColor`, `textHoverColor`, `textPressedColor`, `fillColor`, `opacity`, `borderThickness`, `borderRadius`, `isVisible`, `isBold`, `isItalic`) aus `WidgetElement` in neues Sub-Struct `WidgetElementStyle style` konsolidiert. Zugriff einheitlich über `element.style.*`. Alle Renderer-, Layout-, Editor- und Scripting-Pfade migriert. JSON-Serialisierung rückwärtskompatibel. Zusätzlich: `shadowColor` (Vec4), `shadowOffset` (Vec2), `shadowBlurRadius` (float) für Drop-Shadows; `applyElevation(level, baseColor, baseOffset)` Helper-Methode berechnet Shadow-Parameter aus Elevation-Stufe (0–5).
- `UIWidget` (Bugfix): Fehlende Implementierungen für `Widget`-Konstruktor, `animationPlayer()`, `findAnimationByName()`, `applyAnimationTrackValue()`, `applyAnimationAtTime()` und alle `WidgetAnimationPlayer`-Methoden in `UIWidget.cpp` ergänzt.
- `engine.pyi`: `WidgetElementStyle`-Struct-Dokumentation mit allen 14 Feldern und `element.style.*`-Zugriffsmuster hinzugefügt.
- `UIManager` (Widget Editor): Timeline-Keyframe-Visualisierung überarbeitet – `buildTimelineRulerAndKeyframes` nutzt jetzt flache Panel-basierte Struktur (alle Elemente als direkte Kinder des Containers mit from/to-Positionierung) statt verschachtelter StackPanel→Panel→Text-Hierarchie. Keyframe-Marker als gold-farbene Panel-Blöcke auf den Track-Lanes, Ruler-Ticks als Text, Scrubber/End-Linie als Overlays.
- `UIManager` (Widget Editor): Editierbare Keyframes in `buildTimelineTrackRows` – expandierte Keyframe-Zeilen verwenden EntryBar-Elemente für Time- und Value-Felder (inline editierbar, `onValueChanged`-Callbacks aktualisieren `AnimationKeyframe::time`/`value.x`). Zusätzlich ×-Delete-Button pro Keyframe-Zeile.
- `Editor Theme System`: Zentralisiertes Theme-System für einheitliches Editor-Design. `EditorTheme.h` (Singleton, ~60 Vec4-Farben, 6 Schriftgrößen, 7 Spacing-Werte für alle Editor-UI-Bereiche). `EditorUIBuilder.h/.cpp` (17+ statische Factory-Methoden: makeLabel, makeButton, makePrimaryButton, makeDangerButton, makeEntryBar, makeCheckBox, makeDropDown, makeFloatRow, makeVec3Row etc.). `ViewportUITheme.h` (separate, anpassbare Runtime-Theme-Klasse für Viewport/Gameplay-UI mit halbtransparenten Defaults). Systematischer Ersatz aller hardcoded Farben/Fonts in `UIManager.cpp` (~13.000 Zeilen): Outliner, Details-Panel, Modals, Toasts, Content Browser, Dropdowns, Projekt-Screen, Widget Editor (Controls, Hierarchy, Details, Toolbar, Timeline inkl. Tracks/Keyframes/Ruler), UIDesigner (Toolbar, Controls, Hierarchy, Properties). `OpenGLRenderer.cpp`: Mesh-Viewer (Titel, Pfad, Stats, Transform/Material-Header, Float-Rows, Material-Pfad) und Tab-Bar auf EditorTheme umgestellt. `ViewportUIManager.h` erweitert um `ViewportUITheme m_theme` Member mit `getTheme()`-Accessors.
- `EditorWidget / GameplayWidget Trennung`: Architektonische Aufspaltung des UI-Systems in Editor-Widgets (`EditorWidget`, `src/Renderer/EditorUI/EditorWidget.h`) und Gameplay-Widgets (`GameplayWidget = Widget`, `src/Renderer/GameplayUI/GameplayWidget.h`). Editor-Widgets sind schlank (kein EngineObject, kein JSON, keine Animationen), Gameplay-Widgets behalten volles Feature-Set. `UIManager` nutzt `EditorWidget`, `ViewportUIManager` nutzt `GameplayWidget`. Übergangsüberladungen in `registerWidget` konvertieren bestehende Widget-Instanzen via `EditorWidget::fromWidget()`.
- `Darker Modern Editor Theme`: Komplette Überarbeitung der EditorTheme-Farbpalette für dunkleres, moderneres Erscheinungsbild mit weißer Schrift. Alle ~60 Farbwerte in `EditorTheme.h` angepasst (Window/Chrome 0.06–0.08, Panels 0.08–0.10, Text 0.95 weiß). Blaustich entfernt, rein neutrales Grau. Alle UI-Bereiche proportional abgedunkelt.
- `Editor Settings Popup`: Neues Editor-Settings-Popup über Settings-Dropdown (zwischen "Engine Settings" und "UI Designer"). `openEditorSettingsPopup()` in UIManager (PopupWindow 480×380). Zwei Sektionen: Font Sizes (6 Einträge, 6–48px) und Spacing (5 Einträge mit feldspezifischen Bereichen). Direkte Modifikation des EditorTheme-Singletons + `markAllWidgetsDirty()` für Live-Vorschau.
- `UIManager` (EditorTheme-Migration): Vollständige Ersetzung aller verbliebenen hardcoded `Vec4`-Farbliterale in `UIManager.cpp` durch `EditorTheme::Get().*`-Referenzen. Betrifft: Engine-Settings-Popup (Checkbox-Farben, EntryBar-Text, Dropdown-Styling, Kategorie-Buttons), Projekt-Auswahl-Screen (Background, Titlebar, Sidebar, Footer, Projekt-Zeilen mit Akzentbalken, Buttons, Checkboxen, RHI-Dropdown, Create-Button, Pfad-Vorschau), Content-Browser-Selektionsfarben. ~53 verschiedene Theme-Konstanten im Einsatz.
- `Editor Theme Serialization & Selection`: Vollständige Theme-Persistierung und Auswahl. `EditorTheme` um JSON-Serialisierung erweitert (`toJson()`/`fromJson()`, `saveToFile()`/`loadFromFile()`, `discoverThemes()`). Statische Methoden: `GetThemesDirectory()` (Editor/Themes/), `EnsureDefaultThemes()` (Dark.json + Light.json), `loadThemeByName()`, `saveActiveTheme()`. Default-Themes werden automatisch über `AssetManager::ensureDefaultAssetsCreated()` erzeugt. Gespeichertes Theme wird beim Start aus DiagnosticsManager geladen. Editor Settings Popup um Theme-Dropdown erweitert (Sektion "Theme" mit Live-Wechsel, Persistierung, Auto-Save bei Font/Spacing-Änderungen).
- `Full UI Rebuild on Theme Change (Deferred)`: `rebuildAllEditorUI()` setzt nur `m_themeDirty = true` + `markAllWidgetsDirty()`. Die eigentliche Aktualisierung erfolgt verzögert im nächsten Frame über `applyPendingThemeUpdate()` (aufgerufen am Anfang von `updateLayouts()`). Private Methode populiert dynamische Panels gezielt neu (Outliner, Details, ContentBrowser, StatusBar) – die `populate*`-Funktionen erzeugen frische Elemente über `EditorUIBuilder` mit aktuellen `EditorTheme`-Farben – und markiert alles dirty. Asset-basierte Widget-Hintergründe (TitleBar, ViewportOverlay etc.) behalten ihre bewusst gesetzten JSON-Farben. Ein früherer generischer `applyThemeColors`-Walk wurde entfernt (überschrieb Sonderfarben wie rote Close-Button-Hover und deckte nicht alle Widget-Typen ab). Deferred-Ansatz verhindert Freeze/Crash bei Theme-Wechsel innerhalb von Dropdown-Callbacks. Neue `rebuildEditorUIForDpi(float)` Methode für DPI-Wechsel: pausiert Rendering, regeneriert Widget-Assets, lädt Größen nach, wendet Theme an, setzt Rendering fort.
- `WidgetDetailSchema`: Schema-basierter Property-Editor (`WidgetDetailSchema.h/.cpp`) ersetzt ~1500 Zeilen manuellen Detail-Panel-Code in `UIManager.cpp`. Zentraler Einstiegspunkt `buildDetailPanel()` baut automatisch alle Sections (Identity, Transform, Anchor, Hit Test, Layout, Style/Colors, Brush, Render Transform, Shadow + per-type Sections für Text, Image, Value, EntryBar, Container, Border, Spinner, RichText, ListView, TileView, Drag & Drop). `Options`-Struct ermöglicht kontextspezifische Konfiguration (editierbare IDs, Delete-Button, Callbacks). Verwendet von `refreshWidgetEditorDetails()` und `refreshUIDesignerDetails()`.
- `Theme-Driven Editor Widget Styling`: Neue statische Methode `EditorTheme::ApplyThemeToElement(WidgetElement&, const EditorTheme&)` mappt jeden `WidgetElementType` auf Theme-Farben (color, hoverColor, pressedColor, textColor, borderColor, fillColor, font, fontSize). Spezialbehandlung für ColorPicker (übersprungen), Image (Tint beibehalten), transparente Spacer (bleiben transparent). ID-basierte Overrides: Close → `buttonDanger`-Hover, Save → `buttonPrimary`. Alle ~25 Element-Typen abgedeckt, rekursive Kind-Anwendung. `UIManager::applyThemeToAllEditorWidgets()` iteriert über alle Editor-Widgets und wendet Theme per `ApplyThemeToElement` an. Integration in `applyPendingThemeUpdate()` – asset-basierte Widgets (TitleBar, ViewportOverlay, WorldOutliner, EntityDetails, ContentBrowser, StatusBar, WorldSettings) werden beim Theme-Wechsel jetzt korrekt umgefärbt. `loadThemeByName()`-Fallback auf Dark-Theme-Defaults bei fehlender Theme-Datei.
- `Theme Update Bugfixes`: `applyThemeToAllEditorWidgets()` erfasst jetzt auch Dropdown-Menü-, Modal-Dialog- und Save-Progress-Widgets, die zuvor beim Theme-Wechsel nicht aktualisiert wurden. Popup-Fenster (`renderPopupWindows()`) nutzen jetzt `EditorTheme::Get().windowBackground` für `glClearColor` statt hardcoded Farben. Mesh-Viewer-Details-Panel-Root verwendet `EditorTheme::Get().panelBackground` statt eines festen RGBA-Literales. `applyPendingThemeUpdate()` wird in Popup-UIManagern per Frame aufgerufen, damit Theme-Änderungen auch in Popup-Fenstern wirksam werden. `UIManager::applyPendingThemeUpdate()` von `private` auf `public` verschoben, damit der Renderer sie auf Popup-UIManagern aufrufen kann.
- `Dropdown Flip-Above Positionierung`: `showDropdownMenu()` prüft jetzt den verfügbaren Platz unterhalb des Auslöser-Elements. Reicht der Platz nicht für die Dropdown-Items, wird das Menü oberhalb des Auslösers positioniert (Flip-Above-Logik). Verhindert abgeschnittene Dropdown-Listen am unteren Fensterrand.
- `Texture Compression (S3TC/BC)`: DDS-Dateiformat-Unterstützung implementiert. Neuer `DDSLoader` (`src/Renderer/DDSLoader.h/.cpp`) parst DDS-Header (Standard + DX10-Extended-Header) und lädt Block-Compressed Mip-Chains. Unterstützte Formate: BC1 (DXT1, RGB 4:1), BC2 (DXT3, RGBA), BC3 (DXT5, RGBA interpolated alpha), BC4 (ATI1/RGTC1, single-channel), BC5 (ATI2/RGTC2, two-channel), BC7 (BPTC, high-quality RGBA). `Texture`-Klasse (`Texture.h`) um `CompressedFormat`-Enum (8 Werte), `CompressedMipLevel`-Struct (width, height, data) und `compressedBlockSize()`-Helper erweitert. `OpenGLTexture::initialize()` erkennt `texture.isCompressed()` und nutzt `glCompressedTexImage2D` für alle Mip-Levels mit passendem GL-Internal-Format (S3TC-Extension-Konstanten als `#ifndef`-Fallback). `.dds` als Import-Format in `AssetManager::detectAssetType` registriert; Import-Handler kopiert DDS-Dateien und setzt `m_compressed`-Flag im Asset-JSON. `readAssetFromDisk` speichert den aufgelösten Absolutpfad als `m_ddsPath`. `RenderResourceManager` erkennt komprimierte Assets und delegiert an `loadDDS()`. `RendererCapabilities` um `supportsTextureCompression` erweitert (OpenGL-Backend: true).
- `Runtime Texture Compression`: Unkomprimierte Texturen können vom GL-Treiber beim Upload in S3TC/RGTC-Formate komprimiert werden. `Texture::m_requestCompression`-Flag steuert den Modus. `OpenGLTexture::initialize()` wählt bei gesetztem Flag komprimierte `internalFormat`s (BC1/BC3/BC4/BC5 je nach Kanalzahl) und nutzt normales `glTexImage2D` – der Treiber komprimiert on-the-fly. Toggle über `Renderer::isTextureCompressionEnabled()` / `setTextureCompressionEnabled()`. Engine Settings → Rendering → Performance: Checkbox „Texture Compression (S3TC)". Persistiert in `config.ini` (`TextureCompressionEnabled`). Wirksam ab nächstem Level-Load.
- `Level Loading via Content Browser`: Doppelklick auf ein Level-Asset im Content Browser löst Level-Wechsel aus. Unsaved-Changes-Dialog zeigt Checkbox-Liste ungespeicherter Assets (alle standardmäßig ausgewählt). Rendering friert ein (`Renderer::setRenderFrozen`), modaler Progress wird angezeigt. `AssetManager::loadLevelAsset()` (public) lädt neues Level, `setScenePrepared(false)` erzwingt Neuaufbau. Editor-Kamera/Skybox werden aus dem neuen Level wiederhergestellt. Der Unsaved-Changes-Dialog ersetzt auch den bisherigen Speicher-Flow (Ctrl+S, StatusBar.Save). Neue APIs: `getUnsavedAssetList()`, `saveSelectedAssetsAsync()`, `showUnsavedChangesDialog()`, `showLevelLoadProgress()`.
- `Texture Streaming`: Asynchrones Texture-Streaming via `TextureStreamingManager` (Background-Thread + GPU-Upload-Queue). Texturen werden als 1×1 Placeholder zurückgegeben, pro Frame werden bis zu 4 Texturen hochgeladen. `OpenGLMaterial::bindTextures()` nutzt den Streaming-Manager wenn aktiv. Toggle über `Renderer::isTextureStreamingEnabled()`/`setTextureStreamingEnabled()`. Engine Settings → Rendering → Performance: Checkbox „Texture Streaming". Persistiert in `config.ini` (`TextureStreamingEnabled`).
- `Console / Log-Viewer Tab` (Editor Roadmap 2.1): Dedizierter Editor-Tab für Live-Log-Output. `openConsoleTab()`/`closeConsoleTab()`/`isConsoleOpen()` in `UIManager`. `ConsoleState`-Struct (tabId, widgetId, levelFilter-Bitmask, searchText, autoScroll, refreshTimer). Toolbar mit Filter-Buttons (All/Info/Warning/Error als Toggle-Bitmask), Suchfeld (Echtzeit-Textfilter, case-insensitive), Clear-Button, Auto-Scroll-Toggle. Scrollbare Log-Area zeigt farbcodierte Einträge aus dem Logger-Ringbuffer (2000 Einträge max): INFO=textSecondary, WARNING=warningColor, ERROR=errorColor, FATAL=rot. `refreshConsoleLog()` baut Log-Zeilen mit Timestamp, Level-Tag, Category und Message. Periodischer Refresh alle 0.5s in `updateNotifications()` (nur bei neuen Einträgen via sequenceId-Vergleich). Erreichbar über Settings-Dropdown → „Console".
- `Profiler / Performance-Monitor Tab` (Editor Roadmap 2.7): Dedizierter Editor-Tab für Echtzeit-Performance-Analyse. `FrameMetrics`-Struct in `DiagnosticsManager` mit Ring-Buffer (300 Frames, `pushFrameMetrics()`/`getLatestMetrics()`/`getFrameHistory()`). `openProfilerTab()`/`closeProfilerTab()`/`isProfilerOpen()` in `UIManager`. `ProfilerState`-Struct (tabId, widgetId, frozen, refreshTimer). Toolbar mit Freeze/Resume-Button. Scrollbare Metriken-Area: Frame-History-Balkengrafik (letzte 150 Frames, farbcodiert grün<8.3ms/gelb<16.6ms/rot≥16.6ms), Current-Frame-Übersicht (FPS, CPU/GPU Frame-Time), CPU-Breakdown (10 Kategorien: World/UI/Layout/Draw/ECS/Input/Events/Render/GC/Other mit Proportions-Balken), Occlusion-Culling-Statistik (Visible/Hidden/Total/Cull-Rate%). 0.25s-Auto-Refresh-Timer in `updateNotifications()`. Freeze-Modus pausiert Datenanzeige. Erreichbar über Settings-Dropdown → „Profiler".
- `Content Browser Suche & Filter` (Editor Roadmap 4.2): Echtzeit-Suchfeld + 7 Typ-Filter-Buttons in der Content-Browser-PathBar. `m_browserSearchText` (Textfilter) + `m_browserTypeFilter` (uint16_t Bitmask, 1 Bit pro AssetType). Filter-Buttons: Mesh, Mat, Tex, Script, Audio, Level, Widget — Accent-Toggle (aktiv=halbtransparent blau, inaktiv=transparent). Suchmodus: alle Assets über alle Ordner als flache Liste, case-insensitive Substring + Typ-Filter. Doppelklick navigiert zum Asset-Ordner, öffnet Asset-Editor, leert Suchtext. Normalmodus: nur Typ-Filter auf aktuellen Ordner. `focusContentBrowserSearch()` + Ctrl+F Shortcut.
- `Entity Copy/Paste & Duplicate` (Editor Roadmap 5.3): Ctrl+C/Ctrl+V/Ctrl+D für Entities. `EntityClipboard`-Struct in `UIManager` mit Snapshots aller 13 ECS-Komponententypen. `copySelectedEntity()` speichert vollständigen Entity-Snapshot. `pasteEntity()` erzeugt neue Entity mit +1x Offset und „(Copy)"-Namenssuffix, Undo/Redo-Integration. `duplicateSelectedEntity()` (Ctrl+D) erstellt frischen Snapshot ohne den Clipboard zu überschreiben.
- `Auto-Collider-Generierung` (Editor Roadmap 3.5): `autoFitColliderForEntity()` berechnet Mesh-AABB, skaliert mit Entity-Transform, wählt ColliderType per Heuristik (Sphere/Capsule/Box). „Add Component → Physics" fügt automatisch CollisionComponent mit gefitteten Dimensionen hinzu. „Auto-Fit Collider"-Button in Collision-Details.
- `Collider & Physics Visualisierung` (Editor Roadmap 9.2): Wireframe-Debug-Overlays für alle Collider-Typen im Viewport. `renderColliderDebug()` in `OpenGLRenderer` zeichnet Box (12 Kanten), Sphere (3 Kreise×32 Segmente), Capsule (2 Kreise + 4 Hemisphären-Bögen + 4 Linien), Cylinder (2 Kreise + 4 Linien) mittels Gizmo-Shader (GL_LINES). Farb-Codierung: Grün=Static, Orange=Kinematic, Blau=Dynamic, Rot=Sensor/Trigger. Entity-Transform + Collider-Offset berücksichtigt. „Col"-Toggle-Button in ViewportOverlay-Toolbar. `m_collidersVisible` in `Renderer`-Basisklasse. Nur im Editor-Modus (nicht in PIE).
- `Bone / Skeleton Debug-Overlay` (Editor Roadmap 9.3): Wireframe-Overlay für Bone-Hierarchien selektierter Skinned-Mesh-Entities. `renderBoneDebug()` in `OpenGLRenderer` extrahiert Bone-Positionen aus `finalBoneMatrices * inverse(offsetMatrix)`, transformiert mit Entity-Model-Matrix in Weltkoordinaten. Linien (Cyan) von jedem Bone zu seinem Parent. 3D-Kreuz-Marker an Joint-Positionen: Cyan für normale Bones, Gelb+größer für Root-Bones. „Bone"-Toggle-Button in ViewportOverlay-Toolbar. `m_bonesVisible` in `Renderer`-Basisklasse. Aktualisiert sich per-Frame bei laufender Animation. Nur für selektierte Entities, nur im Editor-Modus.
- `Render-Pass-Debugger` (Editor Roadmap 9.4): Dedizierter Editor-Tab zur Echtzeit-Inspektion der Render-Pipeline. `RenderPassInfo`-Struct in `Renderer.h` (name, category, enabled, fboWidth/Height, fboFormat, details). `getRenderPassInfo()` virtuell, Override in `OpenGLRenderer` liefert 19 Passes (Shadow CSM, Point Shadow, Skybox, Geometry Opaque, Particles, OIT, HeightField, HZB, Pick, PostProcess Resolve, Bloom, SSAO, Grid, Collider Debug, Bone Debug, Selection Outline, Gizmo, FXAA, UI). Tab zeigt Frame-Timing (FPS, CPU/GPU), Object-Counts, kategorie-gruppierte Pass-Liste mit Status-Dots (●/○), FBO-Info, Pipeline-Flow-Diagramm. Farbcodierte Kategorien: Shadow=Lila, Geometry=Blau, Post-Process=Orange, Overlay=Grün, Utility=Grau, UI=Rot. Auto-Refresh (0.5s). `RenderDebuggerState` in `UIManager.h`. Settings-Dropdown-Eintrag.
- ✅ `Standalone Game Build` (Editor Roadmap 10.1): Neu implementiert – Build-Dialog mit PopupWindow (Profil-DropDown, Start Level-DropDown, Window Title), 8-Schritt-Pipeline (OutputDir/CMake-Configure/CMake-Build/Deploy-EXE+DLLs/Cook-Assets/HPK-Pack/game.ini/Done), modale Fortschrittsanzeige. Binary-Cache in `<Projekt>/Binary` für inkrementelle Builds. Profil-abhängiger CMake-BuildType. Runtime erkennt automatisch native Display-Auflösung und startet immer im Fullscreen. Basiert auf Phase 12.1 Editor-Separation (HorizonEngineRuntime.exe). Isoliertes Deploy-Verzeichnis (`-DENGINE_DEPLOY_DIR=<binaryDir>/deploy`) stellt sicher, dass nur Runtime-DLLs deployed werden — keine Editor-only DLLs oder Debug-Artefakte.
- ✅ `Asset-Cooking-Pipeline` (Editor Roadmap 10.2): Neu implementiert. CMSH-Binärformat (magic 0x434D5348, 80-Byte Header + GPU-ready Vertex-Stream + JSON-Metadaten-Blob). `AssetCooker`-Klasse mit inkrementellem Hashing (FNV-1a + manifest.json). Typ-basiertes Cooking: Model3D→CMSH (pre-computed normals/tangents, Skeleton/Animationen als Blob), Material/Level/Widget/Skybox/Prefab→**Binärformat Version 3** (binärer Header + MessagePack-Body via `json::to_msgpack()`), Audio→Binärformat V3 + WAV-Kopie, Script/Shader/Texture→1:1 Kopie. Runtime-Loader in OpenGLObject3D::prepare() (CMSH Fast-Path vor JSON-Fallback). Build-Pipeline Step 5 nutzt cookAll() mit Progress-Callback und Cancel-Support.
**Cooked-Asset-Binärformat:** Gecooked-te Assets (Level, Material, Widget, Skybox, Prefab, Audio) nutzen binären Header (Magic ASTS, **Version 3**, Type, NameLen, Name) + **MessagePack**-Body (`json::to_msgpack()`). Version 3 vs Version 2 (JSON-Text-Body) wird automatisch erkannt. `readAssetHeaderFromMemory()` gibt `outIsMsgPack`-Flag aus, `readAssetJsonFromMemory()` nutzt `json::from_msgpack()` für Version 3. Editor-Assets (JSON-Wrapper) bleiben abwärtskompatibel.
- ✅ `Build-Konfigurationsprofile` (Editor Roadmap 10.3): 3 Standard-Profile (Debug: CMake Debug/verbose/Validation, Development: RelWithDebInfo/info/HotReload, Shipping: Release/error/Compression). Profile als JSON in `<Projekt>/Config/BuildProfiles/`. Profil-Dropdown im Build-Dialog. game.ini enthält Profil-Settings (LogLevel, HotReload, Validation, Profiler). Output standardisiert auf `<Projekt>/Build`, Binary-Cache auf `<Projekt>/Binary`.
- ⏸️ `Asset-Cooking-Pipeline` (Editor Roadmap 10.2): **Ausstehend** – cookAssetsForBuild(), generateAssetManifest() noch nicht neu implementiert.
- ✅ `Editor-Separation-Rework` (Editor Roadmap Phase 12.1 – abgeschlossen): Vollständige Trennung von Editor und Engine-Kern über `#if ENGINE_EDITOR`-Präprozessor-Guards in allen Kern-Quelldateien (UIManager.h/.cpp, OpenGLRenderer.cpp, main.cpp). Duales CMake-Library-System: `RendererCore`/`RendererCoreRuntime` (OBJECT) → `Renderer`/`RendererRuntime` (SHARED) mit getrennten Source-Listen (ENGINE_COMMON_SOURCES). Zwei Executable-Targets: `HorizonEngine` (ENGINE_EDITOR=1) und `HorizonEngineRuntime` (ENGINE_EDITOR=0). UIManager.h Guard-Refactoring: monolithischer Block in ~6 gezielte Blöcke aufgespalten, Runtime-benötigte Members (Drag-API, Drop-Callbacks, bindClickEvents, Tooltip/Hover-State) herausgelöst. Inline-Guards in UIManager.cpp (Notification-History, Dropdown, Outliner, handleRightMouseDown/Up-Stubs). OpenGLRenderer.cpp Guards (Selektion-Sync, Sequencer-Spline, Widget-Editor-Canvas/FBO). main.cpp Guards (Build-Thread, Popup-Routing, Texture-Viewer, Content-Browser-Kontextmenü). Inkrementelle Builds erhalten. Voraussetzung für Phase 10.1 Build & Packaging.
- ✅ `Runtime-Minimierung` (Phase 12.1 Erweiterung): Duales Target-System auf alle Kernbibliotheken erweitert – Assimp und Editor-Abstraktionen komplett aus Runtime-Build entfernt. **Neue Dual-Targets:** `AssetManager`/`AssetManagerRuntime` (SHARED, Runtime ohne assimp: ~3.3 MB statt ~21.7 MB), `Scripting`/`ScriptingRuntime` (SHARED, Runtime linkt RendererRuntime + AssetManagerRuntime), `Landscape`/`LandscapeRuntime` (STATIC, Runtime linkt AssetManagerRuntime). `RendererCoreRuntime` linkt jetzt AssetManagerRuntime + LandscapeRuntime. Root-CMake: `ENGINE_COMMON_LIBS` aufgespalten in `ENGINE_EDITOR_LIBS` + `ENGINE_RUNTIME_LIBS`. **Source-Guards:** AssetManager.h/cpp (Import/Save/Create/Delete/Rename/Move, Editor-Widgets, Validierung, assimp-Includes), PythonScripting.h/cpp (Plugin-System, EditorModule, py_save_asset), RenderResourceManager.cpp (Entity-Referenz-Reparatur), main.cpp (createProject, File-Drop-Import, PollPluginHotReload).
- `Shader Viewer Tab` (Editor Roadmap 9.1): Dedizierter Editor-Tab zum Betrachten von GLSL-Shadern mit Syntax-Highlighting.
- `One-Click Scene Setup` (Editor Roadmap 3.3): `createNewLevelWithTemplate()` mit SceneTemplate-Enum (Empty/BasicOutdoor/Prototype) und optionalem `relFolder`-Parameter. „+ Level"-Dropdown in Content-Browser-PathBar. Rechtsklick-Kontextmenü „New Level" öffnet Popup mit Namenseingabe, erstellt Level als ungespeicherte Änderung (dirty), registriert im Asset-Registry. Entities direkt per ECS aufgebaut, Editor-Kamera template-spezifisch positioniert.
- `Viewport-Einstellungen Panel (Editor Roadmap 5.1)`: Toolbar-Buttons funktional gemacht. **CamSpeed-Dropdown**: Klick öffnet Dropdown mit 7 Geschwindigkeitsvoreinstellungen (0.25x–5.0x), aktuelle Geschwindigkeit markiert, Button-Label aktualisiert bei Auswahl und Mausrad-Scroll. **Stats-Toggle**: Schaltet Performance-Metriken-Overlay ein/aus, visueller Zustand (weiß=aktiv, grau=inaktiv). **Grid-Snap-Toggle**: Schaltet Grid-Snap ein/aus mit visueller Rückmeldung und Toast. **Settings-Dropdown**: Einträge Engine Settings, Editor Settings, Console. **Focus on Selection (F-Taste)**: Neue `Renderer::focusOnSelectedEntity()` – berechnet AABB-Center, Smooth-Transition (0.3s) in 2.5× Radius-Entfernung. F-Taste im Gizmo-Shortcut-Block (W/E/R/F).
- `Intelligent Snap & Grid (Editor Roadmap 3.6)`: Vollständiges Snap-to-Grid-System. **Grid-Overlay**: Infinite-Grid-Shader (XZ-Ebene, SDF-basiert, fwidth-AA, Achsen-Highlighting rot/blau, 10er-Verstärkungslinien, Distance-Fade). **Gizmo-Snap**: Translate rastet auf gridSize-Vielfache, Rotate auf 15°-Schritte, Scale auf 0.1-Schritte. **Toolbar**: Snap-Toggle + GridSize-Dropdown (0.25/0.5/1/2/5/10). **Persistenz**: config.ini via DiagnosticsManager. Snap-State in Renderer-Basisklasse (m_snapEnabled, m_gridVisible, m_gridSize, m_rotationSnapDeg, m_scaleSnapStep).
- `Surface-Snap / Drop to Surface (Editor Roadmap 5.4 + 3.6)`: `dropSelectedEntitiesToSurface()` in `UIManager` setzt selektierte Entities per Raycast nach unten (-Y) auf die nächste Oberfläche. `computeEntityBottomOffset()` berechnet Pivot-zu-AABB-Unterseite-Abstand. Callback-Pattern (`RaycastDownFn`) überbrückt DLL-Grenze Renderer↔Physics. End-Taste als Shortcut + Settings-Dropdown „Drop to Surface (End)". Undo/Redo + Multi-Select-fähig.
- `Python print() → Console Tab (Editor Roadmap 2.1 Ergänzung)`: `PyLogWriter` C++-Typ in `PythonScripting.cpp` mit `write()`/`flush()`. Zeilengepuffert, `sys.stdout` → INFO, `sys.stderr` → ERROR (Category::Scripting). `InstallPythonLogRedirect()` nach `Py_Initialize()`. Python-`print()`-Ausgaben erscheinen im Console-Tab.
- `Asset Thumbnails` (Editor Roadmap 4.1): FBO-gerenderte Thumbnails für Model3D- und Material-Assets im Content Browser. `Renderer::generateAssetThumbnail(assetPath, assetType)` virtuelles Interface, `OpenGLRenderer`-Implementierung rendert 128×128 FBO mit Auto-Kamera (AABB-Framing), Directional Light, Material/PBR-Support. Material-Preview nutzt prozedural erzeugte UV-Sphere (16×12). Thumbnail-Cache (`m_thumbnailCache`) verhindert redundantes Rendering. `ensureThumbnailFbo()`/`releaseThumbnailFbo()` verwalten GL-Ressourcen. Content-Browser-Grid (`populateContentBrowserWidget`) zeigt Thumbnails für Model3D, Material und Texture-Assets — Texturen via `preloadUITexture()`, 3D-Modelle und Materialien via `generateAssetThumbnail()`.
- `Auto-Build GameScripts.dll vor PIE`: Automatische Kompilierung von C++-Game-Scripts vor jedem Play-In-Editor-Start. **EditorApp.h:** Neue private Methode `buildGameScriptsForPIE()`. **EditorApp.cpp:** `startPIE()` prüft `DiagnosticsManager::ScriptingMode` — bei `CppOnly` oder `Both` wird `buildGameScriptsForPIE()` synchron aufgerufen. Bei Fehlschlag wird PIE mit Toast-Fehlermeldung abgebrochen. `buildGameScriptsForPIE()` (~180 Zeilen): (1) Prüft CMake- und Toolchain-Verfügbarkeit (nicht-fatal wenn fehlend — PIE läuft ohne C++ Scripts). (2) Prüft ob `.cpp`-Dateien in `Content/Scripts/Native/` existieren. (3) Generiert `CMakeLists.txt` im `Content/Scripts/Native/`-Verzeichnis (C++20, `file(GLOB *.cpp/*.h)`, SHARED-Library `GameScripts`, linkt gegen `Scripting`-Import-Lib aus Editor-Bin-Verzeichnis, `POST_BUILD` kopiert DLL nach `<projectPath>/Engine/GameScripts.dll`). CMakeLists.txt wird bei jedem Aufruf überschrieben um Engine-Pfade aktuell zu halten. (4) CMake configure ohne `-G` — cmake erkennt den passenden Generator automatisch anhand der installierten Toolchain (`-S ... -B <projectPath>/Binary/GameScripts/`). (5) CMake build (`--target GameScripts --config RelWithDebInfo`). (6) Lädt/reloaded DLL via `NativeScriptManager::loadGameplayDLL()` + `initializeScripts()`. Prozesse laufen über `CreateProcessA`/`CREATE_NO_WINDOW` (Windows) bzw. `popen` (Linux) mit Pipe-basierter Output-Erfassung ins Logger-System. Build-Cache in `<projectPath>/Binary/GameScripts/` für inkrementelle Builds. Die Engine selbst wird nicht neu gebaut — nur der Spieler-Code wird kompiliert.

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
    - 10.4 [Editor Theme System](#104-editor-theme-system)
    - 10.5 [EditorWidget / GameplayWidget Trennung](#105-editorwidget--gameplaywidget-trennung)
    - 10.6 [Darker Modern Editor Theme](#106-darker-modern-editor-theme)
    - 10.7 [Editor Settings Popup](#107-editor-settings-popup)
    - 10.8 [Editor Theme Serialization & Selection](#108-editor-theme-serialization--selection)
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
17. [Gameplay UI System (Viewport UI)](#17-gameplay-ui-system-viewport-ui)

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
│   │   ├── AssetManager.h/.cpp     # Kern (Registry, Laden/Speichern, Worker-Pool)
│   │   ├── AssetManagerImport.cpp  # Import-Dialog & Assimp-Pipeline (Editor)
│   │   ├── AssetManagerFileOps.cpp # Delete, Move, Rename, Referenz-Tracking (Editor)
│   │   ├── AssetManagerEditorWidgets.cpp # Editor-Widget-Asset-Generierung (Editor)
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
│   │   ├── ShortcutManager.h/.cpp  # Zentrales Keyboard-Shortcut-System
│   │   ├── ECS/
│   │   │   ├── ECS.h/.cpp          # ECSManager, Schema, Entity-Verwaltung
│   │   │   ├── Components.h        # Transform-, Mesh-, Material-, Light- etc.
│   │   │   └── DataStructs/
│   │   │       └── SparseSet.h     # Generisches SparseSet<T, Max>
│   │   └── CMakeLists.txt
│   ├── Diagnostics/                # Zustandsverwaltung, Config, PIE, Fenster, Hardware-Info
│   │   ├── DiagnosticsManager.h/.cpp
│   │   ├── HardwareInfo.h/.cpp        # Hardware-Abfrage (CPU, RAM, Monitor via Win32; GPU via Renderer)
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
│   │   ├── ViewportUIManager.h/.cpp # Viewport-UI-Manager (Runtime-UI, unabhängig vom Editor-UI)
│   │   ├── EditorTheme.h           # Zentrales Editor-Theme (Singleton, Farben/Fonts/Spacing)
│   │   ├── EditorUIBuilder.h/.cpp  # Statische Factory-Methoden für theme-basierte UI-Elemente
│   │   ├── WidgetDetailSchema.h/.cpp # Schema-basierter Property-Editor für Widget Editor & UI Designer
│   │   ├── ViewportUITheme.h       # Anpassbare Runtime-Theme-Klasse für Viewport/Gameplay-UI
│   │   ├── EditorUI/
│   │   │   └── EditorWidget.h      # Einfache Editor-Widget-Basisklasse (kein EngineObject/JSON/Animations)
│   │   ├── GameplayUI/
│   │   │   └── GameplayWidget.h    # Gameplay-Widget-Alias (= Widget, volles Feature-Set)
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
│   │   │   ├── PostProcessStack.h/.cpp   # Post-Processing-Pipeline (HDR FBO, MSAA 2x/4x, Fullscreen-Resolve, Bloom 5-Mip Gaussian, SSAO 32-Sample Half-Res Bilateral Blur, Deferred FXAA 3.11 Quality nach Gizmo/Outline)
│   │   │   ├── ShaderHotReload.h/.cpp    # Shader-Hot-Reload: Überwacht shaders/ per last_write_time (500 ms Poll), invalidiert Material-/UI-/PostProcess-Caches und rebuildet Render-Entries bei Dateiänderung
│   │   │   ├── ShaderVariantKey.h        # Shader-Varianten-Bitmask (8 Feature-Flags: Diffuse/Specular/Normal/Emissive/MetallicRoughness/PBR/Fog/OIT), buildVariantDefines() generiert #define-Block
│   │   │   ├── ParticleSystem.h/.cpp    # CPU-simuliertes Partikelsystem: Per-Emitter-Partikelpool, Cone-Emission, Gravity, Color/Size-Interpolation, GPU Point-Sprite Rendering mit back-to-front Sort
│   │   │   ├── OpenGLSplashWindow.h/.cpp # OpenGL-Implementierung des Splash-Fensters (Shader, VAOs, FreeType-Atlas)
│   │   │   ├── glad/               # OpenGL-Loader (GLAD)
│   │   │   ├── shaders/            # GLSL-Shader-Dateien
│   │   │       ├── vertex.glsl / fragment.glsl       # 3D-Welt (Beleuchtung, Texturen, Debug Render Modes)
│   │   │       ├── particle_vertex.glsl / particle_fragment.glsl  # Partikel-Billboard-Shader (Point-Sprites, Soft-Circle via gl_PointCoord)
│   │   │       ├── grid_fragment.glsl                # Prozedurales Grid-Material (Multi-Light, Schatten, Blinn-Phong, Debug Render Modes)
│   │   │       ├── light_fragment.glsl               # Beleuchtung
│   │   │       ├── panel_vertex/fragment.glsl        # UI-Panels
│   │   │       ├── button_vertex/fragment.glsl       # UI-Buttons
│   │   │       ├── text_vertex/fragment.glsl         # Text
│   │   │       ├── ui_vertex/fragment.glsl           # UI-Bild/Textur
│   │   │       ├── progress_fragment.glsl            # Fortschrittsbalken
│   │   │       ├── slider_fragment.glsl              # Schieberegler
│   │   │       ├── resolve_vertex.glsl / resolve_fragment.glsl  # Post-Processing Fullscreen-Resolve (Gamma, Tone Mapping, FXAA 3.11 Quality/MSAA, Bloom, SSAO)
│   │   │       ├── bloom_downsample.glsl            # Bloom Bright-Pass + Progressive Downsample
│   │   │       ├── bloom_blur.glsl                  # Bloom 9-Tap Separable Gaussian Blur
│   │   │       ├── ssao_fragment.glsl               # SSAO 32-Sample Hemisphere (Depth-Only, Half-Res R8)
│   │   │       ├── ssao_blur.glsl                   # SSAO Bilateral Depth-Aware 5×5 Blur (verhindert AO-Bleeding an Tiefenkanten)
│   │   │       ├── oit_composite_fragment.glsl      # Weighted Blended OIT Composite Pass (Accumulation + Revealage → opake Szene)
│   │   │   └── CMakeLists.txt
│   │   ├── UIWidgets/
│   │   │   ├── ButtonWidget.h              # header-only (kein .cpp)
│   │   │   ├── TextWidget.h                # header-only
│   │   │   ├── StackPanelWidget.h           # header-only
│   │   │   ├── GridWidget.h                 # header-only
│   │   │   ├── ColorPickerWidget.h/.cpp
│   │   │   ├── EntryBarWidget.h
│   │   │   ├── SeparatorWidget.h            # header-only
│   │   │   ├── ProgressBarWidget.h          # header-only
│   │   │   ├── SliderWidget.h               # header-only
│   │   │   ├── CheckBoxWidget.h             # header-only
│   │   │   ├── DropDownWidget.h             # header-only
│   │   │   ├── TreeViewWidget.h             # header-only
│   │   │   ├── TabViewWidget.h              # header-only
│   │   │   ├── LabelWidget.h           # NEU – leichtgewichtiges Text-Element
│   │   │   ├── ToggleButtonWidget.h    # NEU – Button mit An/Aus-Zustand
│   │   │   ├── ScrollViewWidget.h      # NEU – scrollbarer Container
│   │   │   ├── RadioButtonWidget.h     # NEU – Radio-Button (Gruppen-ID)
│   │   │   ├── ListViewWidget.h        # NEU – Virtualisierte Liste (Phase 4)
│   │   │   └── TileViewWidget.h        # NEU – Grid-Tile-Ansicht (Phase 4)
│   │   ├── EditorWindows/           # Editor-Fenster (FBO-Override, 3D-Vorschau)
│   │   │   ├── MeshViewerWindow.h/.cpp  # Mesh-Viewer: Orbit-Kamera, dedizierter FBO
│   │   │   ├── MaterialEditorWindow.h/.cpp # Material-Editor-Tab: Preview-Cube + Licht + Boden, PBR-Slider, Textur-Slots
│   │   │   ├── TextureViewerWindow.h/.cpp # Texture-Viewer: Channel-Isolation, Checkerboard, Metadaten
│   │   │   └── PopupWindow.h/.cpp       # Multi-Window Popup-System (backend-agnostisch, nutzt IRenderContext)
│   │   └── CMakeLists.txt          # RendererCore OBJECT-Lib (abstrakte Schicht, eingebettet in Renderer.dll)
├── RENDERER_ABSTRACTION_PLAN.md     # Detaillierter Plan zur Backend-Abstrahierung des Renderers
│   └── Scripting/                  # Eingebettetes Python-Scripting
│       ├── PythonScripting.h/.cpp  # Kern-Initialisierung, Modul-Registrierung, Entity/Asset/Audio/Input/UI/Camera/Diagnostics/Logging/Particle/Editor-Module
│       ├── ScriptingInternal.h     # Shared Header für extrahierte Module (Python.h-Workaround, ScriptDetail-Namespace)
│       ├── MathModule.cpp          # Extrahiertes engine.math-Modul (Vec2/Vec3/Quat/Scalar/Trig-Operationen)
│       ├── PhysicsModule.cpp       # Extrahiertes engine.physics-Modul (Velocity/Force/Gravity/Collision/Raycast)
│       ├── ScriptHotReload.h/.cpp  # Script-Hot-Reload: Überwacht Content/ rekursiv per last_write_time (500ms Poll) auf .py-Änderungen, löst Modul-Neuladen im Python-Interpreter aus
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
- `HorizonEngineRuntime`: `/DELAYLOAD` für alle 8 Engine-SHARED-DLLs + Python-DLL (`delayimp.lib`). Ermöglicht `SetDllDirectory("Engine")` in `main()` vor erstem DLL-Zugriff

### 2.3 Bibliotheks-Targets (alle als SHARED/DLL gebaut)

| Target          | Typ     | Abhängigkeiten                              |
|-----------------|---------|---------------------------------------------|
| `Logger`        | SHARED  | *(keine)*                                   |
| `Core`          | SHARED  | Logger, OpenAL::OpenAL, SDL3::SDL3          |
| `Diagnostics`   | SHARED  | Core, Logger                                |
| `AssetManager`  | SHARED  | Diagnostics, Logger, Core, SDL3::SDL3, assimp (static) |
| `RendererCore`  | OBJECT  | Alle Renderer-Quellen (ENGINE_EDITOR=1). Wird in `Renderer` gelinkt |
| `RendererCoreRuntime` | OBJECT | Nur Common-Quellen (ENGINE_EDITOR=0). Wird in `RendererRuntime` gelinkt |
| `Renderer`      | SHARED  | RendererCore (OBJECT) + SDL3::SDL3, freetype (static), Logger, Core, AssetManager. Backend über `RendererFactory` + `config.ini` wählbar |
| `RendererRuntime`| SHARED | RendererCoreRuntime (OBJECT) + gleiche Dependencies wie Renderer, ohne Editor-Code |
| `Scripting`     | SHARED  | SDL3::SDL3, Renderer, Logger, AssetManager, Diagnostics, Core, Physics, Python3 |
| `Physics`       | STATIC  | Core, Logger                                |
| `HorizonEngine` (exe) | EXE | SDL3, Renderer, Logger, AssetManager, Diagnostics, Core, Scripting, Physics, Python3 (ENGINE_EDITOR=1) |
| `HorizonEngineRuntime` (exe) | EXE | SDL3, RendererRuntime, Logger, AssetManager, Diagnostics, Core, Scripting, Physics, Python3 (ENGINE_EDITOR=0) |

### 2.4 Build-Schritte
1. CMake konfigurieren: `cmake -B build -G "Visual Studio 18 2026" -A x64`
2. Bauen: `cmake --build build --config RelWithDebInfo`
3. Ausgabe: Alle DLLs + `Engine.exe` landen in `CMAKE_RUNTIME_OUTPUT_DIRECTORY` (kein Debug-Postfix)
4. Game Build Output: Exe in Root, `Engine/` (DLLs + Tools/ + Logs/), `Content/` (content.hpk), `config/` (config.ini) — via `BuildPipeline.cpp`, DLLs per `/DELAYLOAD` + `SetDllDirectory("Engine")` aufgelöst. Isoliertes Deploy-Verzeichnis (`-DENGINE_DEPLOY_DIR`) verhindert Editor-DLL-Kontamination.

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
1b. Logger::installCrashHandler()     → SEH + std::terminate Handler
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
| ViewportOverlay   | `ViewportOverlay.asset`  | Viewport   | Toolbar: Render-Mode-Dropdown links (Lit/Unlit/Wireframe/Shadow Map/Shadow Cascades/Instance Groups/Normals/Depth/Overdraw) + Play/Stop (PIE) zentriert + Settings rechts |
| WorldSettings     | `WorldSettings.asset`    | Viewport   | Clear-Color-Picker (RGB-Einträge)           |
| WorldOutliner     | `WorldOutliner.asset`    | Viewport   | Entitäten-Liste des aktiven Levels          |
| EntityDetails     | `EntityDetails.asset`    | Viewport   | Komponenten-Details der ausgewählten Entität mit editierbaren Steuerelementen: Vec3-Eingabefelder (X/Y/Z farbcodiert) für Transform, EntryBars für Floats, CheckBoxen für Booleans, DropDowns für Enums, ColorPicker für Farben, Drag-and-Drop + Dropdown-Auswahl für Mesh/Material/Script-Assets, "+ Add Component"-Dropdown, Remove-Button (X) pro Komponente mit Bestätigungsdialog |
| ContentBrowser    | `ContentBrowser.asset`   | Viewport   | TreeView (Ordner-Hierarchie + Shaders-Root, Highlight) + Grid (Kacheln mit Icons, Selektion + Delete + Rename + F2-Shortcut), Doppelklick-Navigation (Ordner öffnen, Model3D → Mesh-Viewer-Tab), farbcodierte PNGs, Rechtsklick-Kontextmenü (New Script/Level/Material), Textur-Asset-Thumbnails (Quellbild als Grid-Icon via `preloadUITexture`, gecacht im Renderer) |
| StatusBar         | `StatusBar.asset`        | Global     | Undo/Redo + Dirty-Zähler + Save All        |

### 4.3 Editor-Tab-System
Die Engine unterstützt ein Tab-basiertes Editor-Layout:

- **Titelleiste** (obere Reihe der TitleBar, 50px): "HorizonEngine" links + Projektname mittig + Min/Max/Close rechts (Drag-Bereich)
- **Tab-Leiste** (untere Reihe der TitleBar, 50px): Dokument-/Level-Tabs horizontal von links nach rechts, volle Breite
- **Toolbar** (ViewportOverlay, 34px): Debug-Render-Mode-Dropdown links (9 Modi), PIE-Controls zentriert, Settings rechts (Select/Move/Rotate/Scale temporär entfernt)
- **Viewport-Tab**: Immer geöffnet (nicht schließbar), zeigt die 3D-Szene
- **Mesh-Viewer-Tabs**: Schließbare Tabs für 3D-Mesh-Vorschau mit Split-View (Viewport + Properties), geöffnet per Doppelklick auf Model3D im Content Browser. Jeder Tab besitzt ein eigenes Runtime-EngineLevel.
- **Widget-Editor-Tabs**: Schließbare Tabs für Widget-Bearbeitung, geöffnet per Doppelklick auf Widget-Asset im Content Browser. Vier-Panel-Layout: Oben schmale Toolbar (Save-Button + Dirty-Indikator „* Unsaved changes", z=3), Links Steuerelement-Liste (Drag-&-Drop-fähig) + klickbare Hierarchie mit Drag-&-Drop-Reordering (z=2), Mitte FBO-basierte Widget-Vorschau (z=1), Rechts editierbares Details-Panel (z=2). Canvas-Hintergrund z=0. **FBO-Preview**: Das editierte Widget wird in einen eigenen OpenGLRenderTarget-FBO gerendert (bei (0,0) mit Design-Größe layoutet, nicht im UI-System registriert). Die FBO-Textur wird per `drawUIImage` als Quad im Canvas-Bereich angezeigt, mit Zoom (Scroll) und Pan (Rechtsklick-Drag). Scissor-Clipping begrenzt die Anzeige auf den Canvas. Selektierte Elemente werden per `drawUIOutline` mit orangefarbener Outline hervorgehoben. Linksklick im Canvas transformiert Screen-Koordinaten → Widget-lokale Koordinaten (via Zoom/Pan/Canvas-Rect) und selektiert das oberste Element per Bounds-Hit-Test. **Details-Panel**: Eigenschaftsänderungen (From/To, MinSize, Padding, FillX/Y, Farbe, Text, Font, Image-Path, Slider-Werte) werden sofort auf die FBO-Vorschau angewendet via `applyChange`-Helper (setzt `previewDirty` + `markLayoutDirty` + `markAllWidgetsDirty`). **Drag-&-Drop**: Steuerelemente aus der Toolbox können per Drag-&-Drop auf den Canvas gezogen werden — auch wenn das Widget noch keine Elemente hat (Root-Element wird automatisch erstellt). Hierarchie-Einträge im linken Panel sind per Drag-&-Drop verschiebbar (`moveWidgetEditorElement` entfernt das Element und fügt es als Sibling nach dem Ziel ein, mit Zyklus-Schutz). `WidgetEditorState` pro Tab in `UIManager` verwaltet (inkl. Zoom/Pan, `isDirty`-Flag, `previewDirty`-Flag, `assetId`, `toolbarWidgetId`). Speichern: `saveWidgetEditorAsset()` synchronisiert Widget-JSON zurück in AssetData und ruft `AssetManager::saveAsset()` auf. Dirty-Tracking: `markWidgetEditorDirty()` setzt `isDirty`- und `previewDirty`-Flags und aktualisiert Toolbar-Label. Tab-Level-Selektion: Delete-Taste löscht im Widget-Editor das selektierte Element statt das Asset im Content Browser. Undo/Redo: Hinzufügen und Löschen von Elementen werden als `UndoRedoManager::Command` registriert und sind per Ctrl+Z/Ctrl+Y rückgängig/wiederholbar. FBO-Cleanup: `cleanupWidgetEditorPreview(tabId)` wird beim Schließen des Tabs aufgerufen.
- **Console-Tab**: Schließbarer Tab für Live-Log-Viewer (Editor Roadmap 2.1). Toolbar mit Filter-Buttons (All/Info/Warning/Error), Suchfeld, Clear und Auto-Scroll-Toggle. Scrollbare Log-Area mit farbcodierten Einträgen (Timestamp + Level + Category + Message). Periodischer Refresh (0.5s) über `updateNotifications()`. `ConsoleState` in `UIManager` verwaltet (levelFilter-Bitmask, searchText, autoScroll). Öffnung über Settings-Dropdown → „Console".
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
- **Crash-sicher**: Jeder Log-Eintrag wird sofort geflusht (`logFile.flush()` + `std::endl`)
- **Crash-Handler**: `installCrashHandler()` installiert OS-Level Exception-Handler (Windows: `SetUnhandledExceptionFilter` mit Stack-Trace via DbgHelp, Linux: `std::signal`) und `std::set_terminate` für C++-Exceptions
- **Out-of-Process CrashHandler**: Bei einem fatalen Crash startet `LaunchCrashReporter()` das separate `CrashHandler.exe` (`CrashHandler/CrashMain.cpp`) per `CreateProcessA` (Win) / `std::system` (Linux). Empfängt Log-Pfad und Fehlerbeschreibung als Kommandozeile und zeigt einen nativen Crash-Dialog mit den letzten 60 Log-Zeilen.
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

**Dateien:** `src/Diagnostics/DiagnosticsManager.h`, `src/Diagnostics/DiagnosticsManager.cpp`, `src/Diagnostics/HardwareInfo.h`, `src/Diagnostics/HardwareInfo.cpp`

### 6.1 Übersicht
Zentrale Zustandsverwaltung der Engine (Singleton). Verwaltet:

- **Key-Value-States**: Beliebige Engine-Zustände (`setState` / `getState`)
- **Projekt-States**: Pro-Projekt-Einstellungen aus `defaults.ini`
- **RHI-Auswahl**: `OpenGL`, `DirectX11`, `DirectX12` (derzeit nur OpenGL aktiv)
- **Fenster-Konfiguration**: Größe (`Vec2`), Zustand (Normal/Maximized/Fullscreen). Runtime erkennt native Auflösung automatisch via `SDL_GetCurrentDisplayMode()` und startet immer Fullscreen.
- **PIE-Modus** (Play In Editor): `setPIEActive(bool)` / `isPIEActive()`
- **Aktives Level**: `setActiveLevel()` / `getActiveLevelSoft()` / `swapActiveLevel()` (atomarer Austausch via `unique_ptr`, gibt altes Level zurück, setzt Dirty-Callback, feuert `activeLevelChangedCallbacks`)
- **Level-Changed-Callbacks**: Token-basierte Registrierung via `registerActiveLevelChangedCallback()` → gibt `size_t`-Token zurück. `unregisterActiveLevelChangedCallback(token)` entfernt den Callback. Intern als `unordered_map<size_t, Callback>` gespeichert, damit kurzlebige Subscriber (z. B. temporärer UIManager) ihren Callback sicher abmelden können.
- **Action-Tracking**: Asynchrone Aktionen (Loading, Saving, Importing, Building)
- **Input-Dispatch**: Globale KeyDown/KeyUp-Handler
- **Benachrichtigungen**: Modal- und Toast-Notifications (Queue-basiert)
- **Shutdown-Request**: `requestShutdown()` → sauberes Beenden; `resetShutdownRequest()` → setzt Flag zurück (wird vor Main-Loop aufgerufen um verwaiste Flags aus der Startup-Phase zu entfernen)
- **Entity-Dirty-Queue**: `invalidateEntity(entityId)` → markiert einzelne Entitäten für Render-Refresh. `consumeDirtyEntities()` liefert und leert die Queue (thread-safe via `m_mutex`). `hasDirtyEntities()` prüft ob Dirty-Entitäten vorhanden sind. Wird von `renderWorld()` pro Frame konsumiert.
- **Hardware-Diagnostics**: `getHardwareInfo()` liefert gecachte `HardwareInfo` (CPU Brand/Cores, GPU Renderer/Vendor/VRAM, RAM Total/Available, Monitor Name/Resolution/RefreshRate/DPI). CPU/RAM/Monitor via Win32-APIs (CPUID, GlobalMemoryStatusEx, EnumDisplayDevices), GPU/VRAM via OpenGL (glGetString + NVX/ATI Extensions), gesetzt durch `setGpuInfo()` im Renderer nach GL-Kontext-Erstellung.

### 6.2 Config-Persistierung
- **Engine-Config**: `config/config.ini` (Key=Value-Format) — sofortige Speicherung bei jeder Setting-Änderung (`saveConfig()` in allen Engine-Settings-Callbacks)
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

**Dateien:** `src/AssetManager/AssetManager.h`, `src/AssetManager/AssetManager.cpp` (Kern), `AssetManagerImport.cpp` (Import-Pipeline), `AssetManagerFileOps.cpp` (Dateioperationen & Referenz-Tracking), `AssetManagerEditorWidgets.cpp` (Editor-Widget-Generierung)

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
- **Runtime/Packaged Build**: Im gepackten Runtime-Modus (`isPackagedBuild`) wird **kein** Filesystem-Scan durchgeführt (`discoverAssetsAndBuildRegistryAsync` wird übersprungen). Stattdessen lädt `loadAssetRegistry()` die vorgefertigte `AssetRegistry.bin` direkt aus dem HPK-Archiv (ARRG-Binärformat). Danach wird `setAssetRegistryReady(true)` gesetzt.
- **Editor/Dev-Modus**: Weiterhin asynchrone Discovery via `discoverAssetsAndBuildRegistryAsync()` vom Dateisystem.
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
- **OS-Datei-Drop**: Dateien können direkt aus dem OS-Dateiexplorer in das Engine-Fenster gezogen werden. `SDL_EVENT_DROP_FILE` wird im Event-Loop verarbeitet und ruft `AssetManager::importAssetFromPath()` mit `AssetType::Unknown` auf — der Dateityp wird automatisch anhand der Endung erkannt (Textur/Model3D/Audio/Shader/Script). Toast-Benachrichtigung zeigt den Import-Fortschritt an.

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

### 8.7 ShortcutManager
**Datei:** `src/Core/ShortcutManager.h/.cpp`

Zentrales, konfigurierbares Keyboard-Shortcut-System (Singleton). Verfügbar in Editor und Debug/Development-Runtime-Builds (`#if !defined(ENGINE_BUILD_SHIPPING)`). In Shipping-Builds vollständig entfernt.

**Architektur:**
- **Action-Registry:** Jede Aktion hat id, displayName, category, defaultCombo, currentCombo, phase (KeyDown/KeyUp) und callback
- **KeyCombo:** SDL_Keycode + Modifier-Bitmask (Ctrl/Shift/Alt), `toString()` für lesbare Labels (z.B. "Ctrl+Z")
- **handleKey():** Dispatcht SDL-Key-Events an registrierte Aktionen (O(n) Scan, ~20 Einträge)
- **Rebinding:** `rebind(id, newCombo)`, `resetToDefault(id)`, `resetAllToDefaults()`
- **Konflikt-Erkennung:** `findConflict(combo, phase, excludeId)` → id des kollidierenden Shortcuts
- **Persistenz:** `saveToFile()`/`loadFromFile()` → Text-Format (`shortcuts.cfg` im Projektverzeichnis)

**Dreistufiges Guard-Modell (main.cpp):**
- **`#if !defined(ENGINE_BUILD_SHIPPING)`** (äußerer Guard): ShortcutManager-Include, Instanz-Setup, Debug/Runtime-Shortcuts, Config-Load, Event-Dispatch
- **`#if ENGINE_EDITOR`** (innerer Guard, verschachtelt): Editor-only Shortcuts (Undo/Redo/Save/Search/Copy/Paste/Duplicate/Gizmo/Focus/DropToSurface/Help/Import/Delete)
- **Shipping**: Kein ShortcutManager, kein Dispatch — bare minimum zum Laufen

**Registrierte Shortcuts (20):**
| Kategorie | Verfügbarkeit | Shortcuts |
|-----------|--------------|-----------|
| Editor    | Nur Editor (`#if ENGINE_EDITOR`) | Ctrl+Z Undo, Ctrl+Y Redo, Ctrl+S Save, Ctrl+F Search, Ctrl+C/V/D Copy/Paste/Duplicate, F1 Help, F2 Import, DELETE Delete, END Drop-to-Surface |
| Gizmo     | Nur Editor (`#if ENGINE_EDITOR`) | W Translate, E Rotate, R Scale, F Focus |
| Debug     | Editor + Debug/Dev Runtime (`!ENGINE_BUILD_SHIPPING`) | F8 Bounds, F9 Occlusion Stats, F10 Metrics, F11 UI Debug, F12 FPS Cap |
| PIE       | Editor + Debug/Dev Runtime (`!ENGINE_BUILD_SHIPPING`) | Escape Stop, Shift+F1 Toggle Input |

**Editor-Integration:**
- Konfigurations-UI in Editor Settings Popup (Rebind-Buttons mit KeyCaptureCallback, Konflikt-Warnung, Reset All)
- F1 Shortcut-Hilfe Popup (`UIManager::openShortcutHelpPopup()`) – scrollbare Liste aller Shortcuts nach Kategorien

### 8.8 Keyboard-Navigation (Phase 8.3)
**Datei:** `src/Renderer/UIManager.h/.cpp`, `src/Renderer/OpenGLRenderer/OpenGLRenderer.cpp`

Grundlegende Keyboard-Navigation im Editor-UI.

**Funktionen:**
- **Escape-Kaskade:** `handleKeyDown(SDLK_ESCAPE)` schließt erst Dropdown → dann Modal → dann Entry-Fokus → Rename-Abbruch
- **Tab/Shift+Tab-Cycling:** `cycleFocusedEntry(bool reverse)` sammelt alle sichtbaren EntryBar-Elemente via `collectFocusableEntries()` und springt zum nächsten/vorherigen
- **Outliner-Navigation:** `navigateOutlinerByArrow(int direction)` – Pfeiltasten Up/Down navigieren durch die Entity-Liste mit Wrap-around
- **Content-Browser-Navigation:** `navigateContentBrowserByArrow(int dCol, int dRow)` – Pfeiltasten Links/Rechts/Oben/Unten navigieren durch das Asset-Grid, Spaltenanzahl wird aus Grid-Panel-Breite berechnet
- **Fokus-Highlight:** Fokussierte EntryBars erhalten einen blauen Outline (`drawUIOutline` mit `{0.25, 0.55, 0.95, 0.8}`) in beiden Rendering-Pfaden

---

## 9. Renderer

### 9.1 Renderer (abstrakt)
**Datei:** `src/Renderer/Renderer.h`, `src/Renderer/RendererCapabilities.h`

Abstrakte Schnittstelle für jeden Rendering-Backend (~130 Zeilen, ~60 virtuelle Methoden):

**Pure-virtual (Core):** initialize, shutdown, clear, render, present, name, window, Camera-Steuerung, getUIManager, screenToWorldPos

**Virtual mit Default-Implementierung (optional):**
- Rendering-Toggles: Shadows, VSync, Wireframe, Occlusion Culling
- Debug-Visualisierungen: UI Debug, Bounds Debug, HeightField Debug
- Debug Render Modes: DebugRenderMode Enum (Lit, Unlit, Wireframe, ShadowMap, ShadowCascades, InstanceGroups, Normals, Depth, Overdraw), getDebugRenderMode, setDebugRenderMode
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
      → Shadow-Pässe (Regular, Point Cube Maps, CSM — CSM abschaltbar via Settings)
     → Hierarchical Z-Buffer (HZB) Occlusion Culling (liest Tiefe aus Tab-FBO)
     → Sortierung nach (Material, Mesh) + GPU Instanced Batching (SSBO, glDrawElementsInstanced)
     → Post-Processing Resolve (Gamma, Tone Mapping, Bloom, SSAO, MSAA — FXAA hier übersprungen)
     → Pick-Buffer + Selection-Outline nur bei Bedarf (On-Demand)
     → Editor-Gizmos (Translate/Rotate/Scale Overlay)
     → Deferred FXAA 3.11 Quality Pass (nach Gizmo/Outline, Content-Rect Viewport, damit AA auf gesamtes Bild wirkt)
  → Default-Framebuffer mit m_clearColor leeren (verhindert undefinierte Inhalte bei Nicht-Viewport-Tabs)
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
- **Details-Panel Undo/Redo (Phase 8.4)**: Alle Wertänderungen im Details-Panel (Transform, Light, Camera, Collision, Physics, ParticleEmitter, Name) sind undoable via `setCompFieldWithUndo<>`-Template-Helper. Komponenten-Hinzufügen/Entfernen und Asset-Zuweisungen (Mesh/Material/Script über Dropdown oder Drag & Drop) erzeugen ebenfalls Undo/Redo-Commands mit vollständigem Komponenten-Snapshot.

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
| `vertex.glsl`             | 3D-Welt Vertex-Shader (TBN-Matrix für Normal Mapping) |
| `fragment.glsl`           | 3D-Welt Fragment-Shader (Blinn-Phong + PBR Cook-Torrance mit Specular-Map × SpecularMultiplier auf specBRDF, Normal Mapping, Emissive Maps, CSM, Fog) |
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
- Hält Texturen (`std::vector<shared_ptr<Texture>>`) — Slot 0: Diffuse, Slot 1: Specular, Slot 2: Normal Map, Slot 3: Emissive Map, Slot 4: MetallicRoughness (PBR)
- Textur-Pfade für Serialisierung
- Shininess-Wert, Metallic/Roughness-Werte, Specular-Multiplier (Standard: 1.0), PBR-Enabled Flag

#### OpenGLMaterial:
- Hält Shader-Liste, Vertex-Daten, Index-Daten, Layout
- `build()` → Erstellt VAO, VBO, EBO, linkt Shader-Programm
- `bind()` → Setzt Uniformen (Model/View/Projection, Lights, Shininess, PBR-Parameter, uSpecularMultiplier, uHasNormalMap, uHasEmissiveMap, uHasMetallicRoughnessMap) und bindet Texturen (Diffuse/Specular/Normal Map/Emissive Map/MetallicRoughness)
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
- **Mipmaps:** Systematisch aktiv für alle Bild-Texturen (`glGenerateMipmap` bei jedem GPU-Upload). Min-Filter `GL_LINEAR_MIPMAP_LINEAR` für trilineare Filterung. Gilt für Material-Texturen (`OpenGLTexture`), Skybox-Cubemaps und UI-Textur-Cache. Framebuffer-/Shadow-/Depth-Texturen sind bewusst ausgenommen.

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
- GPU Instanced Rendering: `renderInstanced(instanceCount)` via Material
- Statischer Cache: `ClearCache()`
- Skeletal Animation: `isSkinned()` erkennt Meshes mit Bones, `getSkeleton()` gibt die geladene Bone-Hierarchie zurück. `setSkinned(bool)`/`setBoneMatrices(float*, int)` leiten Bone-Daten an `OpenGLMaterial` weiter. Bei `prepare()` wird automatisch `skinned_vertex.glsl` gewählt und der Vertex-Buffer mit 22 Floats/Vertex (14 Basis + 4 boneIds + 4 boneWeights) aufgebaut.

#### GPU Instanced Rendering
- Draw-Liste wird nach (`OpenGLMaterial*`, `OpenGLObject3D*`) sortiert — nur Objekte mit gleichem Mesh UND Material werden gruppiert
- Aufeinanderfolgende DrawCmds mit gleichem Mesh und Material werden zu einem Batch zusammengefasst
- Model-Matrizen über SSBO (`layout(std430, binding=0)`, `GL_DYNAMIC_DRAW`) an Shader übergeben
- Shader nutzt `gl_InstanceID` zum Indizieren; `uniform bool uInstanced` schaltet zwischen SSBO und `uModel`
- Vertex-Shader verwendet `if/else` statt Ternary für Model-Matrix-Auswahl (verhindert spekulative SSBO-Zugriffe auf SIMD-GPUs)
- `uploadInstanceData()` verwaltet SSBO mit automatischem Grow (Kapazität verdoppelt sich bei Bedarf)
- Buffer-Orphaning: `glBufferData(nullptr, GL_DYNAMIC_DRAW)` vor `glBufferSubData` verhindert GPU-Read/Write-Hazards
- SSBO-Cleanup: Nach jedem Instanced-Draw wird SSBO explizit entbunden (`glBindBufferBase(0,0)`) und `uInstanced=false` gesetzt
- Implementiert für: Haupt-Render-Pass, reguläre Shadow Maps, Cascaded Shadow Maps
- Emission-Objekte und Einzelobjekt-Batches nutzen klassischen Non-Instanced-Pfad
- Cleanup über `releaseInstanceResources()` in `shutdown()`

---

### 9.8 Text-Rendering
**Datei:** `src/Renderer/OpenGLRenderer/OpenGLTextRenderer.h/.cpp`

- FreeType-basierte Glyph-Atlas-Generierung
- `initialize(fontPath, vertexShader, fragmentShader)` → Baut Atlas + Shader-Programm
- `drawText(text, screenPos, scale, color)` → Rendert Text am Bildschirm
- `measureText(text, scale)` → Gibt Textgröße zurück (für Layout)
- `getLineHeight(scale)` → Zeilenhöhe
- Shader-Cache: `getProgramForShaders()` cacht verknüpfte Programme
- Blend-State Save/Restore: `drawTextWithProgram()` sichert den aktuellen `glBlendFuncSeparate`-Zustand vor dem Rendering und stellt ihn danach wieder her, damit der UI-FBO-Alpha-Kanal nicht korrumpiert wird
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

Zentrale Verwaltung aller UI-Widgets. Editor-Tabs sind in eigenständige Klassen unter `src/Renderer/EditorTabs/` extrahiert:

| Klasse | Datei | Beschreibung |
|--------|-------|-------------|
| `IEditorTab` | `IEditorTab.h` | Interface (open/close/isOpen/update/getTabId) |
| `ConsoleTab` | `ConsoleTab.h/.cpp` | Log-Viewer mit Filter/Suche |
| `ProfilerTab` | `ProfilerTab.h/.cpp` | Performance-Monitor |
| `AudioPreviewTab` | `AudioPreviewTab.h/.cpp` | Audio-Asset-Vorschau |
| `ParticleEditorTab` | `ParticleEditorTab.h/.cpp` | Partikel-Parameter-Editor |
| `ShaderViewerTab` | `ShaderViewerTab.h/.cpp` | Shader-Quellcode-Anzeige |
| `RenderDebuggerTab` | `RenderDebuggerTab.h/.cpp` | Render-Pipeline-Debugger |
| `SequencerTab` | `SequencerTab.h/.cpp` | Kamera-Sequenz-Editor |
| `LevelCompositionTab` | `LevelCompositionTab.h/.cpp` | Level-Kompositions-Übersicht |
| `AnimationEditorTab` | `AnimationEditorTab.h/.cpp` | Skelett-Animations-Editor |
| `UIDesignerTab` | `UIDesignerTab.h/.cpp` | Viewport-UI-Designer |
| `WidgetEditorTab` | `WidgetEditorTab.h/.cpp` | Widget-Editor (Multi-Instanz, State-Map) |
| `ContentBrowserPanel` | `ContentBrowserPanel.h/.cpp` | Content Browser (Pfad-Navigation, Grid, Suche/Filter, Rename, Asset-Referenz-Tracking) |
| `OutlinerPanel` | `OutlinerPanel.h/.cpp` | World Outliner (Entity-Baumansicht) + Entity Details (Komponenten-Property-Editor, Undo/Redo) |
| `BuildSystemUI` | `BuildSystemUI.h/.cpp` | Build-System-UI (Build-Profile, Build-Game-Dialog, Progress-Popup, CMake/Toolchain-Detection) |
| `EditorDialogs` | `EditorDialogs.h/.cpp` | Dialog-System (Modale Dialoge, Confirm-Dialoge, Save-Progress, Level-Load-Progress, Projekt-Screen) |

Jeder Tab hält `UIManager*` + `Renderer*` Pointer. UIManager delegiert via Thin Wrappers. `ContentBrowserPanel` verwaltet 9 eigene Member-Variablen (Pfad, Selektion, Filter, Rename-State) und wird lazy-initialisiert. `OutlinerPanel` verwaltet Entity-Selektion, Entity-Clipboard, ECS-Versions-Tracking und wird im UIManager-Konstruktor initialisiert. `BuildSystemUI` verwaltet Build-Profile, Build-Thread-State, CMake/Toolchain-Detection und wird im UIManager-Konstruktor initialisiert. `EditorDialogs` verwaltet modale Dialoge, Confirm-Dialoge, Save/Level-Load-Progress und den Projekt-Auswahl-Screen (13 Methoden, ~1.840 Zeilen). Type-Aliases in `UIManager.h` (`UIManager::BuildGameConfig` etc.) erhalten API-Kompatibilität.

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
handleScroll(screenPos, delta)       → Scroll auf scrollable-Elementen (Priorität vor Canvas-Zoom)
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
- **Scroll-Priorität:** `handleScroll` prüft zuerst scrollbare Widgets (absteigende Z-Reihenfolge, tab-gefiltert) und fällt nur auf Canvas-Zoom zurück, wenn kein scrollbares Widget den Event konsumiert.
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
- **Toast**: `showToastMessage(message, duration[, level])` – temporäre Meldung mit optionalem Priority-Level
- **NotificationLevel**: `DiagnosticsManager::NotificationLevel` Enum (`Info`, `Success`, `Warning`, `Error`) – gemeinsam genutzt von `DiagnosticsManager` und `UIManager` (via `using`-Alias). Steuert farbigen Akzentbalken (links, 4px) am Toast-Widget: Info = `accentColor`, Success = `successColor`, Warning = `warningColor`, Error = `errorColor` (aus `EditorTheme`). Warning/Error-Toasts erhalten verlängerte Anzeigedauer (min 4s bzw. 5s).
- **Notification History**: `NotificationHistoryEntry` mit Level + Zeitstempel. `getNotificationHistory()`, `getUnreadNotificationCount()`, `clearUnreadNotifications()`, `openNotificationHistoryPopup()`, `refreshNotificationBadge()`.
- Toast-Stack-Layout: Automatisches Stapeln bei mehreren Toasts
- **enqueueToastNotification**: Akzeptiert `NotificationLevel` (default `Info`). Aufrufer: `AssetManager` (Import-Erfolg/Fehler), `PythonScripting` (Hot-Reload-Erfolg/Fehler), `UIManager` (Level-Load, Validierung).

#### Popup-Fenster:
- `openLandscapeManagerPopup()` — öffnet das Landscape-Manager-Popup mit Formular-UI (vormals in `main.cpp`).
- `openEngineSettingsPopup()` — öffnet das Engine-Settings-Popup mit Sidebar-Navigation (vormals in `main.cpp`).
- `openMaterialEditorPopup(materialAssetPath)` — öffnet den Material-Editor als Popup (480×560). Material-Auswahl per Dropdown aus der Asset-Registry, PBR-Parameter (Metallic, Roughness, Shininess als Slider, PBR-Enabled-Checkbox), Textur-Slot-Bearbeitung (Diffuse, Specular, Normal, Emissive, MetallicRoughness als String-Rows) und Save/Close-Buttons. Erreichbar über Content-Browser-Doppelklick auf Material-Assets und über den Tools-Bereich in World Settings.
- Alle Popup-Methoden nutzen den `m_renderer`-Back-Pointer (`setRenderer()`) um `OpenGLRenderer::openPopupWindow()` / `closePopupWindow()` aufzurufen.

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

#### Texture Viewer (Editor-Fenster):
- **Klasse**: `TextureViewerWindow` (`src/Renderer/EditorWindows/TextureViewerWindow.h/.cpp`)
- **Zweck**: 2D-Vorschau von Textur-Assets mit Channel-Isolation (R/G/B/A), Checkerboard-Hintergrund und Metadaten-Anzeige.
- **Architektur**: Kein Runtime-EngineLevel nötig — die Textur wird direkt per GLSL-Shader in den Tab-FBO gerendert. Kein Level-Swap beim Tab-Wechsel.
- **Channel-Isolation**: Ein eigener GLSL-Shader (`m_texViewerChannelProgram`) mit `uniform ivec4 uChannelMask` und `uniform int uCheckerboard`. Bei Einzel-Kanal-Isolation wird der Wert als Grayscale dargestellt.
- **Checkerboard-Hintergrund**: Prozedural im Fragment-Shader generiert (32×32 Kacheln). Zeigt Transparenz-Bereiche deutlich an.
- **Metadaten-Panel** (rechts, 320px): Auflösung, Kanäle, Format (PNG/JPEG/TGA/BMP/HDR/DDS), Dateigröße, Kompressionsformat. Tab-scoped Widget (`TextureViewerDetails.{path}`).
- **Channel-Toggle-Buttons**: R/G/B/A-Buttons in der Sidebar mit farblicher Kodierung (Rot/Grün/Blau/Grau). Klick togglet den jeweiligen Kanal. Deaktivierte Kanäle werden visuell ausgegraut (gedämpfte Hintergrund- und Textfarbe), aktive Kanäle zeigen ihre lebendige Originalfarbe.
- **Zoom/Pan**: Zoom 1.0 = Fit-to-Window (ohne Upscaling). Scroll-Rad zoomt stufenlos (Faktor 1.15x pro Tick, Bereich 0.05–50.0, relativ zu Fit-Scale). Rechtsklick-Drag verschiebt die Ansicht (Pan). Im Laptop-Modus kann alternativ Linksklick-Drag zum Pan verwendet werden. Zoom- und Pan-State im `TextureViewerWindow`-Objekt gespeichert.
- **Öffnung**: Doppelklick auf Texture-Asset im Content Browser → `OpenGLRenderer::openTextureViewer(assetPath)`.
  - Pfad-Auflösung via `resolveContentPath()`, automatisches Laden via `AssetManager::loadAsset(Sync)`.
  - Textur-Upload per `getOrLoadUITexture()` mit Fallback auf `m_sourcePath`.
- **Schließen**: `closeTextureViewer(assetPath)` — wechselt auf Viewport-Tab, entfernt Tab und Widgets.
- **Rendering**: Im `render()`-Loop wird für Texture-Viewer-Tabs `renderWorld()` übersprungen und stattdessen die Textur direkt in den Tab-FBO gerendert.

#### Audio Preview Tab (Editor-Tab):
- **Architektur**: Folgt dem Console/Profiler-Tab-Muster (rein UIManager-basiert, kein FBO/Renderer-Level nötig).
- **`AudioPreviewState`** in `UIManager.h`: tabId, widgetId, assetPath, isPlaying, playHandle, volume, channels, sampleRate, format, dataBytes, durationSeconds, displayName.
- **Öffnung**: Doppelklick auf Audio-Asset im Content Browser → `UIManager::openAudioPreviewTab(assetPath)`.
  - Lädt Audio-Asset via `AssetManager::loadAsset(Sync)`, extrahiert Metadaten (Channels, Sample Rate, Format, Duration, Dateigröße) aus dem Asset-JSON.
  - Erstellt Tab via `Renderer::addTab()` und registriert ein Widget mit Toolbar, Waveform und Metadaten.
- **Toolbar**: Play/Stop-Buttons (über `AudioManager::playAudioAsset()`/`stopSource()`), Lautstärke-Slider (`setHandleGain()`), Asset-Name-Anzeige.
- **Waveform**: 80-Balken-Diagramm aus den rohen Sample-Daten (liest JSON-Byte-Array, berechnet Amplituden pro Balken). Farbcodiert nach EditorTheme.
- **Metadaten-Panel**: Pfad, Channels, Sample Rate, Format (8-bit/16-bit), Duration, Datengröße, Dateigröße.
- **Schließen**: `closeAudioPreviewTab()` — stoppt Wiedergabe, deregistriert Widget, entfernt Tab.
- **Refresh**: `refreshAudioPreview()` baut Widget-Inhalt komplett neu auf (Toolbar + Waveform + Metadaten).

#### Particle Editor Tab (Editor-Tab):
- **Architektur**: Folgt dem Console/Profiler/Audio-Tab-Muster (rein UIManager-basiert, kein FBO). Verlinkt eine ECS-Entity und editiert deren `ParticleEmitterComponent` live.
- **`ParticleEditorState`** in `UIManager.h`: tabId, widgetId, linkedEntity, isOpen, refreshTimer, presetIndex.
- **Öffnung**: „Edit Particles"-Button in den Entity-Details bei Entities mit `ParticleEmitterComponent` → `UIManager::openParticleEditorTab(entity)`.
  - Erstellt Tab via `Renderer::addTab()` und registriert ein Widget mit Toolbar und scrollbarem Parameterbereich.
- **Toolbar**: Titel mit Entity-Name, Preset-Dropdown (Custom/Fire/Smoke/Sparks/Rain/Snow/Magic), Reset-Button.
- **Parameter-Area**: Alle 20 `ParticleEmitterComponent`-Parameter als Slider-Controls, gruppiert in Sektionen (General: Enabled/Loop/MaxParticles, Emission: Rate/Lifetime/ConeAngle, Motion: Speed/SpeedVariance/Gravity, Size: Start/End, Start Color: RGBA, End Color: RGBA).
- **Presets**: 6 eingebaute Presets (Fire, Smoke, Sparks, Rain, Snow, Magic) mit vollständiger Parameterbelegung. Undo/Redo-Integration über `UndoRedoManager::pushCommand()`.
- **Reset**: Stellt alle Parameter auf `ParticleEmitterComponent{}`-Defaults zurück, mit Undo-Unterstützung.
- **Validierung**: 0.3s-Timer prüft ob die verlinkte Entity noch eine `ParticleEmitterComponent` hat; schließt den Tab automatisch falls nicht.
- **Schließen**: `closeParticleEditorTab()` — deregistriert Widget, entfernt Tab.
- **Refresh**: `refreshParticleEditor()` baut den Parameterbereich komplett neu auf.

#### Animation Editor Tab (Editor-Tab):
- **Architektur**: Folgt dem Particle-Editor-Tab-Muster (rein UIManager-basiert, kein FBO). Verlinkt eine ECS-Entity und steuert deren `AnimationComponent` und den zugehörigen `SkeletalAnimator` live.
- **`AnimationEditorState`** in `UIManager.h`: tabId, widgetId, linkedEntity, isOpen, refreshTimer, selectedClip.
- **Öffnung**: „Edit Animation"-Button in den Entity-Details bei Entities mit `AnimationComponent` auf skinned Meshes → `UIManager::openAnimationEditorTab(entity)`.
  - Erstellt Tab via `Renderer::addTab()` und registriert ein Widget mit Toolbar und scrollbarem Content-Bereich.
- **Toolbar**: Titel mit Entity-Name, Stop-Button.
- **Clip-Liste**: Alle Animation-Clips des Skeletts als klickbare Buttons. Aktiver Clip mit Accent-Farbe hervorgehoben. Zeigt Kanal-Anzahl und Dauer.
- **Playback Controls**: Status-Anzeige (Playing/Stopped, aktueller Clip, aktuelle Zeit), Speed-Slider (0–5x), Loop-Checkbox.
- **Bone-Hierarchie**: Eingerückte Baum-Darstellung aller Bones mit Parent-Beziehungen und korrekter Indentation.
- **Renderer-API**: 12 neue virtuelle Methoden in `Renderer.h` + `AnimationClipInfo`-Struct. Override in `OpenGLRenderer.cpp` greift auf `m_entityAnimators`-Map und `Skeleton`-Daten zu.
- **Details-Panel**: AnimationComponent-Sektion mit Speed/Loop/Playing/ClipIndex-Feldern, Remove-Separator mit Undo/Redo. Animation im "Add Component"-Dropdown verfügbar.
- **Schließen**: `closeAnimationEditorTab()` — deregistriert Widget, entfernt Tab.
- **Refresh**: `refreshAnimationEditor()` baut den Content-Bereich komplett neu auf (Clip-Liste + Controls + Bone-Tree).

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
| **Material** | `metallic`, `roughness`, `specularMultiplier` | Float-EntryBars (MaterialOverrides mit Undo/Redo + invalidateEntity) |

- **Änderungsfluss**: Jedes Control ruft bei Commit `ecs.setComponent<T>(entity, updated)` auf, was `m_componentVersion` inkrementiert und das Panel beim nächsten Frame automatisch aktualisiert. **Alle Callbacks markieren das Level als unsaved** (`setIsSaved(false)`), damit Änderungen sofort im StatusBar-Dirty-Zähler reflektiert werden.
- **Sofortige visuelle Rückmeldung**: Transform-, Light- und Camera-Werte werden vom Renderer jeden Frame direkt aus dem ECS gelesen (`updateModelMatrices`-Lambda, Light-Schema-Query, Camera-Query) — Änderungen sind sofort im Viewport sichtbar ohne Render-Invalidierung. Mesh/Material-Pfadänderungen nutzen `invalidateEntity(entity)`, das die betroffene Entität in eine Dirty-Queue (`DiagnosticsManager::m_dirtyEntities`) einreiht. Im nächsten Frame ruft `renderWorld()` für jede Dirty-Entität `refreshEntity()` → `refreshEntityRenderable()` auf, das bestehende GPU-Caches nutzt und nur fehlende Assets nachlädt — statt den gesamten Scene-Graph neu aufzubauen.
- **Name-Änderungen**: Der Name-EntryBar aktualisiert zusätzlich das Entity-Header-Label (`Details.Entity.NameLabel`) und ruft `refreshWorldOutliner()` auf, damit Namensänderungen sofort im Outliner und im Details-Panel-Header reflektiert werden. Außerdem wird das Level als unsaved markiert.
- **Hilfslambdas**: `makeFloatEntry`, `makeVec3Row`, `makeCheckBoxRow` erzeugen wiederverwendbare UI-Zeilen mit Label + Control + onValueChanged-Callback. Alle drei Lambdas rufen nach dem eigentlichen Wert-Callback automatisch `setIsSaved(false)` auf, sodass jede Werteänderung das Level dirty markiert.
- **Inline-Callbacks** (Light-Typ-DropDown, Light-ColorPicker, Physics-Collider-DropDown): Diese Callbacks gehen nicht durch die Hilfslambdas, enthalten aber ebenfalls `setIsSaved(false)` für konsistente Dirty-Markierung.

---

### 10.2 Widget & WidgetElement
**Dateien:** `src/Renderer/UIWidget.h/.cpp`, `src/Renderer/EditorUI/EditorWidget.h`, `src/Renderer/GameplayUI/GameplayWidget.h`

Das Widget-System ist in zwei Basisklassen aufgeteilt:

#### EditorWidget (einfache Editor-UI):
**Datei:** `src/Renderer/EditorUI/EditorWidget.h`

Schlanke Basisklasse für alle Editor-UI-Widgets. Kein `EngineObject`, keine JSON-Serialisierung, kein Animationssystem.
```cpp
class EditorWidget {
    std::string m_name;
    Vec2 m_sizePixels, m_positionPixels;
    WidgetAnchor m_anchor;
    bool m_fillX, m_fillY;
    int m_zOrder;
    std::vector<WidgetElement> m_elements;

    // Statische Factory für Übergangskonvertierung:
    static std::shared_ptr<EditorWidget> fromWidget(std::shared_ptr<Widget> w);
};
```
- Wird vom `UIManager` für alle Editor-Panels (Outliner, Details, Content Browser, Widget Editor, Modals, Toasts, etc.) verwendet.
- `WidgetEntry` im `UIManager` hält `shared_ptr<EditorWidget>`.

#### GameplayWidget (= Widget, volles Feature-Set):
**Datei:** `src/Renderer/GameplayUI/GameplayWidget.h`

```cpp
using GameplayWidget = Widget;  // Alias für Widget mit allen Features
```
- Erbt von `EngineObject`, unterstützt JSON-Serialisierung, Animationen, Focus, Drag & Drop.
- Wird vom `ViewportUIManager` für Gameplay-/Viewport-UI verwendet.
- `WidgetEditorState.editedWidget` bleibt `Widget` (bearbeitet Gameplay-Widgets im Editor).

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

    // Layout-Panel-Felder (Phase 1):
    int columns, rows;                    // UniformGrid
    float widthOverride, heightOverride;  // SizeBox
    ScaleMode scaleMode;                  // ScaleBox
    float userScale;                      // ScaleBox (UserSpecified)
    int activeChildIndex;                 // WidgetSwitcher

    // Border-Widget-Felder (Phase 4):
    UIBrush borderBrush;                   // Brush für die 4 Kanten
    float borderThicknessLeft, borderThicknessTop, borderThicknessRight, borderThicknessBottom;
    Vec2 contentPadding;                   // Zusätzlicher Innen-Abstand

    // Spinner-Widget-Felder (Phase 4):
    int spinnerDotCount;                   // Anzahl Punkte (default 8)
    float spinnerSpeed;                    // Umdrehungen/Sek (default 1.0)
    float spinnerElapsed;                  // Runtime-Zähler (nicht serialisiert)

    // Multiline-EntryBar-Felder (Phase 4):
    bool isMultiline;                      // Mehrzeilige Eingabe (default false)
    int maxLines;                          // Max Zeilen, 0 = unbegrenzt

    // Rich-Text-Block-Felder (Phase 4):
    std::string richText;                  // Markup-String (<b>, <i>, <color>, <img>)

    // ListView/TileView-Felder (Phase 4):
    int totalItemCount;                    // Anzahl Items (default 0)
    float itemHeight;                      // Zeilenhöhe in px (default 32)
    float itemWidth;                       // Tile-Breite in px (default 100, nur TileView)
    int columnsPerRow;                     // Spalten pro Zeile (default 4, nur TileView)
    std::function<void(int, WidgetElement&)> onGenerateItem; // Item-Template-Callback

    // Styling & Visual Polish (Phase 2):
    UIBrush background;                   // Brush-basierter Hintergrund (None/SolidColor/Image/NineSlice/LinearGradient)
    UIBrush hoverBrush;                   // Brush für Hover-State
    UIBrush fillBrush;                    // Brush für Füllbereich (ProgressBar, Slider, etc.)
    RenderTransform renderTransform;      // Rein visuelle Transformation (Translate/Rotate/Scale/Shear/Pivot)
    ClipMode clipMode;                    // Clipping-Modus (None/ClipToBounds/InheritFromParent)
    float effectiveOpacity;               // Berechnete Opacity (element.opacity * parent.effectiveOpacity)
    int elevation;                        // 0–5: Shadow-Tiefenstufe (0 = kein Shadow, 3 = Modals/Toasts, 5 = Drag-Ghost)

    // Focus (Phase 5):
    FocusConfig focusConfig;              // isFocusable, tabIndex, focusUp/Down/Left/Right
    UIBrush focusBrush;                   // Farbe für Fokus-Highlight-Outline

    // Drag & Drop (Phase 5):
    bool isDraggable;                     // Element kann per Drag bewegt werden
    std::string dragPayload;              // Beliebiger Payload-String für Drag
    bool acceptsDrop;                     // Element akzeptiert Drops
    std::function<void()> onDragStart;    // Callback bei Drag-Start
    std::function<bool(const DragDropOperation&)> onDragOver; // Hover-Validierung
    std::function<void(const DragDropOperation&)> onDrop;     // Drop-Callback

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
| `Panel`           | Farbiger Hintergrund-Bereich (rendert Kind-Elemente) |
| `StackPanel`      | Automatisches Layout (H/V-Stapelung)  |
| `Grid`            | Raster-Layout                         |
| `ColorPicker`     | Farbauswahl-Widget                    |
| `EntryBar`        | Text-Eingabefeld                      |
| `ProgressBar`     | Fortschrittsanzeige                   |
| `Slider`          | Schieberegler mit Min/Max             |
| `Image`           | Bild/Textur-Anzeige                   |
| `DropdownButton`  | Button der ein Dropdown-Menü öffnet   |
| `WrapBox`         | Container mit automatischem Zeilenumbruch (Flow-Layout) |
| `UniformGrid`     | Raster mit gleichgroßen Zellen (Columns/Rows) |
| `SizeBox`         | Erzwingt Breite/Höhe-Constraints auf ein Kind |
| `ScaleBox`        | Skaliert Kind auf verfügbare Fläche (Contain/Cover/Fill/ScaleDown/UserSpecified) |
| `WidgetSwitcher`  | Zeigt nur ein Kind gleichzeitig (Index-basiert) |
| `Overlay`         | Stapelt alle Kinder übereinander mit Alignment |
| `Border`          | Single-Child-Container mit konfigurierbarem Rahmen (separate borderBrush, per-Seite Dicke, contentPadding) |
| `Spinner`         | Animiertes Lade-Symbol (N Punkte im Kreis mit Opacity-Falloff) |

#### Brush-System (Phase 2 – Styling):

| Typ | Beschreibung |
|-----|-------------|
| `BrushType::None` | Keine Füllung |
| `BrushType::SolidColor` | Einfarbige Fläche (RGBA) |
| `BrushType::Image` | Textur-Füllung |
| `BrushType::NineSlice` | 9-Slice-Textur (Ecken fix, Kanten/Mitte gestreckt) |
| `BrushType::LinearGradient` | Linearer Farbverlauf (Start-/End-Farbe + Winkel) |

**UIBrush Struct:** `type`, `color`, `colorEnd`, `gradientAngle`, `imagePath`, `textureId`, `imageMargin` (L/T/R/B), `imageTiling`.

**RenderTransform Struct:** `translation` (Vec2), `rotation` (Grad), `scale` (Vec2), `shear` (Vec2), `pivot` (normalisiert, 0.5/0.5 = Mitte). Wird in allen drei Render-Pfaden als Matrix T(pivot)·Translate·Rotate·Scale·Shear·T(-pivot) auf die Ortho-Projektion multipliziert. Hit-Testing im `ViewportUIManager` wendet die Inverse an (`InverseTransformPoint`), sodass Klicks auf transformierte Elemente korrekt erkannt werden.

**ClipMode Enum:** `None` (kein Clipping), `ClipToBounds` (Scissor auf Element-Bounds, verschachtelte Clips schneiden per Intersection), `InheritFromParent` (Eltern-Scissor übernehmen). RAII-basierter GL-Scissor-Stack in allen drei Render-Pfaden.

**Opacity-Vererbung:** `effectiveOpacity = element.opacity × parent.effectiveOpacity` – rekursiv berechnet, als Alpha-Multiplikator an alle Render-Aufrufe übergeben.

#### Animation-Basis (Phase 3 – Datenmodell):

- `AnimatableProperty`-Enum ergänzt (Transform-, Appearance-, Layout- und Content-Properties wie `RenderTranslationX`, `Opacity`, `ColorR`, `SizeX`, `FontSize`).
- `EasingFunction`-Enum ergänzt (Linear, Quad/Cubic, Elastic, Bounce, Back Varianten).
- Neue Widget-Animationsstrukturen: `AnimationKeyframe` (`time`, `value` als `Vec4`, `easing`), `AnimationTrack` (`targetElementId`, `property`, `keyframes`), `WidgetAnimation` (`name`, `duration`, `isLooping`, `playbackSpeed`, `tracks`).
- `Widget` speichert Animationen in `m_animations` inkl. JSON-Laden/Speichern über `m_animations` im Widget-Asset.

#### Animations-Timeline (Widget-Editor Bottom-Panel):

- Unreal-Style Bottom-Dock-Panel (250px Höhe, per Toggle-Button ein-/ausblendbar)
- Horizontales Split-Layout: Links (220px) Track-Liste (Element-Label + Property-Dropdown + ◆-Add-Keyframe + Remove-Track), rechts scrollbare Timeline mit Ruler/Zeitachse + Keyframe-Diamanten als Drag-&-Drop-Buttons
- Toolbar: Animations-Dropdown, +New/Delete, Play ▶ / Stop ■, Duration-Eingabe, Loop-Checkbox
- Tracks per Dropdown über alle Widget-Elemente hinzufügbar (Element-ID + animierbare Property)
- Keyframe-Details-Leiste: Time, Value, Easing-Dropdown, Delete-Button
- Scrubber: Klick auf Ruler setzt Position, orangefarbener 2px-Indikator; Echtzeit-Drag über Ruler via `handleMouseDown`/`handleMouseMotionForPan`
- End-of-Animation-Linie: 2px roter Indikator, per Drag verschiebbar zur Änderung der Dauer
- Drag-Interaktionen in bestehende Event-Handler integriert (`handleMouseDown` startet Drag via Element-Bounds-Hit-Test, `handleMouseMotionForPan` aktualisiert Position in Echtzeit, `handleMouseUp` beendet Drag und sortiert Keyframes)
- Alternating Row Colors (gerade/ungerade Zeilen) für bessere Track-Sichtbarkeit; Element-Header-Rows betten 1px Scrubber- (orange) und End-Linie (rot) ein
- Ruler-Indikator-Leiste (4px): zeigt Scrubber- und End-Line-Position als farbige Marker
- Keyframe-Diamanten: 7px/7pt (kleine ◆-Symbole) mit Hit-Test für Click-Selektion und Drag-Start
- Implementierung: `UIManager::refreshWidgetEditorTimeline()` in `UIManager.cpp`, Drag-Logik in `handleMouseDown`/`handleMouseMotionForPan`/`handleMouseUp`, State-Felder (`timelineScrubTime`, `timelineZoom`, `selectedTrackIndex`, `isDraggingScrubber`, `isDraggingEndLine`, `draggingKeyframeTrack/Index`, `expandedTimelineElements`) in `WidgetEditorState`

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
| `EntryBarWidget`     | `EntryBarWidget.h`       | Text-Eingabe, Passwort-Modus, Multiline-Modus (isMultiline, maxLines) |
| `SeparatorWidget`    | `SeparatorWidget.h/.cpp` | Aufklappbarer Abschnitt mit flachem Sektions-Header (▾/▸ Chevron, Trennlinie, indentierter Inhalt) |
| `ProgressBarWidget`  | `ProgressBarWidget.h/.cpp`| Wertebalken mit Min/Max und Farben        |
| `SliderWidget`       | `SliderWidget.h/.cpp`    | Schieberegler, `onValueChanged`-Callback   |
| `CheckBoxWidget`     | `CheckBoxWidget.h/.cpp`  | Boolean-Toggle mit Label, `onCheckedChanged`-Callback |
| `DropDownWidget`     | `DropDownWidget.h/.cpp`  | Auswahlliste mit Expand/Collapse, `onSelectionChanged`-Callback |
| `DropdownButtonWidget` | `DropdownButtonWidget.h/.cpp` | Button der beim Klick ein Dropdown-Menü öffnet, `dropdownItems` oder `items`+`onSelectionChanged` |
| `TreeViewWidget`     | `TreeViewWidget.h/.cpp`  | Hierarchische Baumansicht mit aufklappbaren Knoten |
| `TabViewWidget`      | `TabViewWidget.h/.cpp`   | Tab-Leiste mit umschaltbaren Inhaltsbereichen, `onTabChanged`-Callback |
| `WrapBoxWidget`      | `WrapBoxWidget.h`         | Flow-Container mit automatischem Zeilenumbruch |
| `UniformGridWidget`  | `UniformGridWidget.h`     | Gleichmäßiges Raster-Layout (Columns/Rows) |
| `SizeBoxWidget`      | `SizeBoxWidget.h`         | Container mit Breite/Höhe-Override |
| `ScaleBoxWidget`     | `ScaleBoxWidget.h`        | Skaliert Kind (Contain/Cover/Fill/ScaleDown/UserSpecified) |
| `WidgetSwitcherWidget` | `WidgetSwitcherWidget.h` | Zeigt ein Kind per Index |
| `OverlayWidget`      | `OverlayWidget.h`         | Stapelt Kinder übereinander |
| `BorderWidget`       | `BorderWidget.h`           | Single-Child-Container mit konfigurierbarem Rahmen |
| `SpinnerWidget`      | `SpinnerWidget.h`          | Animiertes Lade-Symbol (rotierende Punkte) |
| `RichTextWidget`     | `RichTextWidget.h`          | Formatierter Textblock mit Inline-Markup (Bold, Italic, Color) |
| `ListViewWidget`     | `ListViewWidget.h`          | Virtualisierte scrollbare Liste mit Item-Template-Callback |
| `TileViewWidget`     | `TileViewWidget.h`          | Grid-basierte Tile-Ansicht mit konfigurierbaren Spalten/Größen |

---

### 10.4 Editor Theme System
**Dateien:** `src/Renderer/EditorTheme.h`, `src/Renderer/EditorUIBuilder.h/.cpp`, `src/Renderer/WidgetDetailSchema.h/.cpp`, `src/Renderer/ViewportUITheme.h`

Zentralisiertes Theme-System für einheitliches Editor-Design und anpassbare Viewport-UI:

#### EditorTheme (Singleton)
- **Zugriff:** `EditorTheme::Get()` — liefert statische Referenz
- **Inhalt:** ~60 `Vec4`-Farbkonstanten (Window/Panel/Button/Text/Input/Accent/Selection/Modal/Toast/Scrollbar/TreeView/ContentBrowser/Timeline/StatusBar), 6 Schriftgrößen (`fontSizeHeading` 16px bis `fontSizeCaption` 10px), 7 Spacing-Werte (`rowHeight`, `paddingSmall/Normal/Large` etc.), Font-Name (`fontFamily = "default.ttf"`), DPI-Scaling (`dpiScale`, `applyDpiScale()`, `Scaled(float)`/`Scaled(Vec2)` Hilfsmethoden)
- **Verwendung:** Alle Editor-UI-Elemente in `UIManager.cpp` und `OpenGLRenderer.cpp` referenzieren ausschließlich Theme-Konstanten statt hardcoded Werte

#### EditorUIBuilder (Statische Factory)
- 17+ Methoden: `makeLabel`, `makeSecondaryLabel`, `makeHeading`, `makeButton`, `makePrimaryButton`, `makeDangerButton`, `makeSubtleButton`, `makeEntryBar`, `makeCheckBox`, `makeDropDown`, `makeFloatRow`, `makeVec3Row`, `makeHorizontalRow`, `makeVerticalStack`, `makeDivider`, `makeSection`, `fmtFloat`, `sanitizeId`
- Erzeugt fertig konfigurierte `WidgetElement`-Objekte mit Theme-Farben, Fonts und Spacing
- Reduziert Boilerplate bei der Editor-UI-Erstellung erheblich

#### WidgetDetailSchema (Schema-basierter Property-Editor)
- **Datei:** `WidgetDetailSchema.h/.cpp` — statische Klasse, ersetzt ~1500 Zeilen manuellen Property-Panel-Code
- **Einstiegspunkt:** `buildDetailPanel(prefix, selected, applyChange, rootPanel, options)` — baut komplettes Detail-Panel für beliebiges `WidgetElement`
- **Shared Sections (alle Typen):** Identity, Transform, Anchor, Hit Test, Layout, Style/Colors, Brush, Render Transform, Shadow & Elevation, Drag & Drop
- **Per-Type Sections:** Text (Text/Label/Button/ToggleButton/DropdownButton/RadioButton), Image, Value (Slider/ProgressBar), EntryBar, Container (StackPanel/ScrollView/WrapBox/UniformGrid/SizeBox/ScaleBox/WidgetSwitcher), Border, Spinner, RichText, ListView, TileView
- **Options-Struct:** `showEditableId`, `onIdRenamed`, `showDeleteButton`, `onDelete`, `onRefreshHierarchy` — konfiguriert Verhalten pro Kontext (Widget Editor vs UI Designer)
- **Verwendet von:** `UIManager::refreshWidgetEditorDetails()` und `UIManager::refreshUIDesignerDetails()`

#### ViewportUITheme (Runtime-Theme)
- **Klasse:** `ViewportUITheme` — instanziierbar, nicht Singleton
- **Integration:** `ViewportUIManager` hält `m_theme`-Member mit `getTheme()`-Accessors
- **Defaults:** Halbtransparente Farben für In-Game-Overlay-Look (z.B. `panelBg {0.05, 0.05, 0.05, 0.75}`)
- **Anpassbar:** Gameplay-Code kann Viewport-UI-Theme zur Laufzeit ändern

---

### 10.5 EditorWidget / GameplayWidget Trennung
**Dateien:** `src/Renderer/EditorUI/EditorWidget.h`, `src/Renderer/GameplayUI/GameplayWidget.h`

Architektonische Aufspaltung des UI-Widget-Systems in zwei separate Basisklassen für Editor- und Gameplay-UI:

#### Designziel
- **Editor-Widgets** sollen so einfach wie möglich sein: fest definiert, vom UIManager statisch platziert, einheitliches Theme, keine JSON-Serialisierung, keine Animationen.
- **Gameplay-Widgets** behalten das volle Feature-Set: EngineObject-Vererbung, JSON-Persistenz, Animationssystem, Focus, Drag & Drop.

#### EditorWidget (`src/Renderer/EditorUI/EditorWidget.h`)
- Einfache C++-Klasse ohne `EngineObject`-Vererbung
- Felder: `name`, `sizePixels`, `positionPixels`, `anchor` (WidgetAnchor), `fillX`/`fillY`, `absolutePosition`, `computedSize`/`computedPosition`, `layoutDirty`, `elements` (vector\<WidgetElement\>), `zOrder`
- Statische Factory: `EditorWidget::fromWidget(shared_ptr<Widget>)` — konvertiert ein bestehendes `Widget` für Übergangskompatibilität
- Verwendet im `UIManager` (`WidgetEntry` hält `shared_ptr<EditorWidget>`)

#### GameplayWidget (`src/Renderer/GameplayUI/GameplayWidget.h`)
- Type-Alias: `using GameplayWidget = Widget;`
- Behält alle Features: EngineObject, JSON load/save, Animationen (`WidgetAnimationPlayer`), Focus (`FocusConfig`), Drag & Drop
- Verwendet im `ViewportUIManager` (`WidgetEntry` hält `shared_ptr<GameplayWidget>`)

#### Übergangskompatibilität
- `UIManager::registerWidget` bietet duale Überladungen: `shared_ptr<EditorWidget>` (primär) und `shared_ptr<Widget>` (Transition, ruft `EditorWidget::fromWidget()` intern auf)
- `main.cpp` und bestehender Code, der `Widget`-Instanzen aus JSON lädt, funktioniert weiterhin über die Transition-Überladung
- `WidgetEditorState.editedWidget` bleibt `shared_ptr<Widget>`, da der Widget-Editor Gameplay-Widgets bearbeitet
- Renderer (`OpenGLRenderer`) arbeitet weiterhin mit `vector<WidgetElement>&`, das beide Widget-Typen über `getElements()`/`getElementsMutable()` bereitstellen

---

### 10.6 Darker Modern Editor Theme
**Datei:** `src/Renderer/EditorTheme.h`

Komplette Überarbeitung der EditorTheme-Farbpalette für ein dunkleres, moderneres Erscheinungsbild mit weißer Schrift:

- **Window/Chrome**: Hintergründe auf 0.06–0.08 abgesenkt (vorher 0.09–0.11)
- **Panel-Hintergründe**: Auf 0.08–0.10 abgesenkt (vorher 0.11–0.14)
- **Text**: Alle Textfarben auf 0.95 angehoben (nahezu reines Weiß, vorher 0.82–0.92)
- **Neutral**: Blaustich aus Hintergründen entfernt, rein neutrales Grau
- **Proportional**: Buttons, Inputs, Dropdowns, TreeView, ContentBrowser, Timeline, StatusBar proportional abgedunkelt
- **Akzentfarben**: Selection/Hover dezent angepasst für besseren Kontrast

### 10.7 Editor Settings Popup
**Dateien:** `src/Renderer/UIManager.h`, `src/Renderer/UIManager.cpp`, `src/main.cpp`

Editor-Settings-Popup erreichbar über Settings-Dropdown im ViewportOverlay (zwischen "Engine Settings" und "Console").

**Implementierung:**
- `openEditorSettingsPopup()` in UIManager.h deklariert, in UIManager.cpp implementiert (~200 Zeilen)
- PopupWindow (480×380) mit dunklem Theme-Styling aus `EditorTheme::Get()`

**Sektionen:**
1. **Theme** – Active Theme DropDown (zeigt alle .json-Dateien aus Editor/Themes/)
2. **UI Scale** – DPI Scale DropDown (Auto/100%/125%/150%/175%/200%/250%/300%). "Auto" erkennt den DPI-Wert des primären Monitors automatisch. Änderungen werden live über `applyDpiScale()` + `rebuildAllEditorUI()` angewendet und in `config.ini` persistiert (`UIScale` Key).
3. **Font Sizes** – 6 EntryBar-Einträge: Heading, Subheading, Body, Small, Caption, Monospace (Bereich 6–48px)
4. **Spacing** – 5 EntryBar-Einträge: Row Height (16–48), Row Height Small (14–40), Row Height Large (20–56), Toolbar Height (24–64), Border Radius (0–12)

**Mechanik:**
- Jeder Eintrag schreibt direkt in den `EditorTheme`-Singleton via `float*`-Pointer
- Nach Änderung: `markAllWidgetsDirty()` für sofortiges visuelles Feedback (Live-Vorschau)
- Wertvalidierung mit `try/catch` auf `std::stof` und feldspezifischen Min/Max-Bereichen
- Font-Size- und Spacing-Änderungen werden automatisch ins aktive Theme zurückgespeichert (`saveActiveTheme()`)

**Theme-Auswahl:**
- Neue Sektion "Theme" am Anfang des Popups mit "Active Theme"-DropDown
- DropDown zeigt alle `.json`-Dateien aus `Editor/Themes/` (via `EditorTheme::discoverThemes()`)
- Theme-Wechsel: lädt neues Theme (`loadThemeByName`), persistiert Auswahl in DiagnosticsManager, löst deferred UI-Rebuild aus (`rebuildAllEditorUI`) – Farben werden im nächsten Frame über `ApplyThemeToElement` aktualisiert

### 10.8 Editor Theme Serialization & Selection
**Dateien:** `src/Renderer/EditorTheme.h`, `src/AssetManager/AssetManager.cpp`, `src/main.cpp`

Vollständige Theme-Persistierung mit JSON-Serialisierung und automatischer Default-Theme-Erstellung.

**Serialisierung:**
- `toJson()` / `fromJson()`: Konvertiert alle ~60 Vec4-Farben, Fonts, Spacing-Werte zu/von `nlohmann::json`. Font-/Spacing-Werte werden DPI-unabhängig gespeichert (`toJson()` dividiert durch `dpiScale`, `fromJson()` multipliziert beim Laden)
- `saveToFile(path)` / `loadFromFile(path)`: Schreibt/liest Theme-JSON-Dateien. `loadFromFile()` bewahrt den aktiven `dpiScale` über Theme-Wechsel hinweg
- `discoverThemes()`: Scannt `Editor/Themes/`-Verzeichnis nach `.json`-Dateien, gibt Namensliste zurück

**DPI-Aware Scaling:**
- `float dpiScale`: Aktueller DPI-Skalierungsfaktor (1.0 = 96 DPI / 100%). Wird beim Startup aus dem primären Monitor oder gespeichertem Override (`UIScale` Key in `config.ini`) ermittelt
- `applyDpiScale(float newScale)`: Skaliert alle Font-Größen, Row-Heights, Padding, Icon-Sizes, Border-Radius und Separator-Thickness vom aktuellen zum neuen Skalierungsfaktor
- `static float Scaled(float px)` / `static Vec2 Scaled(Vec2 v)`: Hilfsmethoden für beliebige Pixelwert-Skalierung (`px * dpiScale`). Verwendet für alle Layout-Konstanten, die nicht über Theme-Felder abgebildet werden (Popup-Dimensionen, Row-Heights, Label-Widths, Widget-Fallback-Größen)
- `loadThemeByName()` / `loadFromFile()` bewahren `dpiScale` automatisch — neue Themes werden sofort mit dem aktiven Skalierungsfaktor geladen

**Vollständige UI-Abdeckung:**
- **UIManager.cpp**: Alle 37 hardcoded `fontSize`-Literale → Theme-Felder; Engine-Settings/Editor-Settings/Projekt-Auswahl/Landscape-Manager-Popup-Dimensionen und Layout-Konstanten via `Scaled()`; `measureElementSize()` Fallback-Größen (Slider, Image, Checkbox, Dropdown-Arrow) via `Scaled()` oder Theme-Werte
- **main.cpp**: New-Material-Popup Dimensionen, FontSizes, MinSizes und Paddings skaliert
- **OpenGLRenderer.cpp**: 15 `minSize`-Werte in Mesh-/Material-Editor-Popups und Tab-Buttons skaliert
- **UIWidgets**: SeparatorWidget (22px Header), TabViewWidget (26px Tab), TreeViewWidget (22px Row) via `Scaled()` skaliert
- **Popup-Layout-Strategie**: Popup-Fenster werden mit `Scaled(baseW/H)` vergrößert; interne Positionen nutzen normalisierte Koordinaten (`from/to` 0-1) berechnet aus Basis-Pixelwerten, sodass Layouts proportional mitskalieren

**Default-Themes:**
- `EnsureDefaultThemes()`: Erstellt `Dark.json` (Standard-Defaults) und `Light.json` (helle Farbpalette mit ~50 Overrides) falls nicht vorhanden
- Wird automatisch von `AssetManager::ensureDefaultAssetsCreated()` aufgerufen (kein separater Aufruf in `main.cpp` nötig)
- Theme-Verzeichnis: `Editor/Themes/` (relativ zum Arbeitsverzeichnis)

**Startup-Flow:**
1. `AssetManager::ensureDefaultAssetsCreated()` → `EditorTheme::EnsureDefaultThemes()` (erstellt Dark.json + Light.json)
2. `main.cpp` Phase 2b: Erkennt DPI-Skalierung vom primären Monitor (oder liest gespeicherten `UIScale`-Override aus `config.ini`), wendet `applyDpiScale()` an, und lädt gespeichertes Theme via `loadThemeByName()`

**Hilfsmethoden:**
- `GetThemesDirectory()`: Gibt `Editor/Themes/`-Pfad zurück
- `loadThemeByName(name)`: Lädt Theme aus `Editor/Themes/<name>.json`
- `saveActiveTheme()`: Speichert aktuelles Theme zurück in seine Datei

**Deferred UI Rebuild (`rebuildAllEditorUI()` + `applyPendingThemeUpdate()`):**
- `rebuildAllEditorUI()` setzt nur `m_themeDirty = true` und ruft `markAllWidgetsDirty()` auf – keine schwere Arbeit im Callback-Kontext
- `applyPendingThemeUpdate()` (private) wird am Anfang von `updateLayouts()` aufgerufen und prüft `m_themeDirty`
- Ruft `applyThemeToAllEditorWidgets()` auf: rekursiver Farb-Walk über alle registrierten Editor-Widgets via `EditorTheme::ApplyThemeToElement` – aktualisiert Farben, Fonts und Spacing bestehender Elemente in-place
- Abschließend `markAllWidgetsDirty()` für Layout-Neuberechnung
- Deferred-Ansatz verhindert Editor-Freeze/Crash bei synchroner UI-Rekonstruktion innerhalb von Dropdown-Callbacks

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
- **HPK-Fallback**: Sowohl `LoadScriptModule()` (Entity-Skripte) als auch `LoadLevelScriptModule()` (Level-Skripte) laden `.py`-Dateien zuerst per `ifstream` und fallen bei Fehler auf HPK zurück (`HPKReader::makeVirtualPath()` → `readFile()`). Damit funktionieren Skripte im Packaged Build, wo Dateien nur im HPK-Archiv liegen.

### 11.1b Script Hot-Reload
**Dateien:** `src/Scripting/ScriptHotReload.h/.cpp`, `src/Scripting/PythonScripting.cpp`

```cpp
Scripting::InitScriptHotReload(contentDir)  // File-Watcher auf Content/ initialisieren
Scripting::PollScriptHotReload()            // Prüft auf .py-Änderungen, lädt Module neu
Scripting::IsScriptHotReloadEnabled()       // Abfrage ob Hot-Reload aktiv
Scripting::SetScriptHotReloadEnabled(b)     // Toggle (wird auch aus config.ini gelesen)
```

- `ScriptHotReload`-Klasse: Überwacht Content/-Verzeichnis rekursiv (`recursive_directory_iterator`) auf `.py`-Dateiänderungen via `std::filesystem::last_write_time`
- Poll-Intervall: 500ms (selbstthrottled)
- Bei Änderung: Alte `ScriptState` freigeben, Modul neu laden, `startedEntities` beibehalten (kein erneutes `onloaded`)
- Level-Skript wird ebenfalls erkannt und neu geladen (mit `on_level_loaded` Re-Aufruf)
- Toast-Benachrichtigung bei Erfolg/Fehler via `DiagnosticsManager::enqueueToastNotification()`
- Toggle in Engine Settings → General → Scripting mit config.ini-Persistenz (`ScriptHotReloadEnabled`)

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
| `spawn_widget(content_path) -> str`| Widget per Content-Pfad laden, gibt Widget-ID zurück. Wird nur im Viewport gerendert, bei PIE-Stop automatisch zerstört. |
| `remove_widget(widget_id) -> bool` | Viewport-Widget per ID entfernen |
| `play_animation(widget_id, name, from_start)` | Widget-Animation abspielen |
| `stop_animation(widget_id, name)`  | Widget-Animation stoppen        |
| `set_animation_speed(widget_id, name, speed)` | Animationsgeschwindigkeit setzen |
| `show_cursor(visible) -> bool`     | Gameplay-Cursor ein-/ausblenden (+ Kamera-Blockade) |
| `clear_all_widgets() -> bool`      | Alle Viewport-Widgets entfernen |
| `set_focus(element_id) -> bool`    | Fokus auf ein UI-Element setzen |
| `clear_focus() -> bool`            | Fokus vom aktuellen Element entfernen |
| `get_focused_element() -> str/None`| ID des fokussierten Elements    |
| `set_focusable(element_id, focusable) -> bool` | Element als fokussierbar markieren |

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
- **LodComponent**: Serialisiert als "Lod" JSON-Key mit `levels`-Array (je `meshAssetPath` + `maxDistance`). Ermöglicht distanzbasierte Mesh-LOD-Auswahl pro Entity im Render-Loop.
- **ParticleEmitterComponent**: Serialisiert als "ParticleEmitter" JSON-Key mit 19 Feldern (maxParticles, emissionRate, lifetime, speed, speedVariance, size, sizeEnd, gravity, colorR/G/B/A, colorEndR/G/B/A, coneAngle, enabled, loop).
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
- **Content-Browser-Kontextmenü (Grid, Rechtsklick)**: enthält `New Folder`, anschließend Separator, dann `New Script`, `New Level` (Popup mit Namenseingabe, Level bleibt ungespeichert/dirty), `New Material`.
- **Engine Settings Popup** über `UIManager::openEngineSettingsPopup()` (aufgerufen aus `ViewportOverlay.Settings` → Dropdown-Menü → "Engine Settings"): Links Sidebar mit Kategorie-Buttons (General, Rendering, Debug, Physics, Info), rechts scrollbarer Content-Bereich mit Checkboxen und Float-Eingabefeldern. General-Kategorie enthält: Splash Screen. Rendering-Kategorie enthält: Display (Shadows, VSync, Wireframe Mode, Occlusion Culling), Post-Processing (Post Processing Toggle, Gamma Correction, Tone Mapping, Anti-Aliasing Dropdown, Bloom, SSAO), Lighting (CSM Toggle). Debug-Kategorie enthält: UI Debug Outlines, Bounding Box Debug, HeightField Debug. Physics-Kategorie enthält: Backend-Dropdown (Jolt / PhysX, PhysX nur sichtbar wenn `ENGINE_PHYSX_BACKEND_AVAILABLE` definiert), Gravity X/Y/Z (Float-Eingabefelder, Default 0/-9.81/0), Fixed Timestep (Default 1/60 s), Sleep Threshold (Default 0.05). Info-Kategorie zeigt Hardware-Informationen (read-only): CPU (Brand, Physical/Logical Cores), GPU (Renderer, Vendor, Driver Version, VRAM Total/Free), RAM (Total/Available), Monitors (Name, Resolution, Refresh Rate, DPI Scale). Die Backend-Auswahl wird als `PhysicsBackend`-Key in `DiagnosticsManager` persistiert und beim PIE-Start ausgelesen, um `PhysicsWorld::initialize(Backend)` mit dem gewählten Backend aufzurufen. Kategoriewechsel baut den Content-Bereich dynamisch um. Alle Änderungen werden **sofort** in `config.ini` via `DiagnosticsManager::setState()` + `saveConfig()` persistiert (nicht erst beim Shutdown). Die Widget-Erstellung wurde aus `main.cpp` in den UIManager verschoben.
- Grid-Shader (`grid_fragment.glsl`) nutzt vollständige Lichtberechnung (Multi-Light, Schatten, Blinn-Phong) — Landscape wird von allen Lichtquellen der Szene beeinflusst.
- `EngineLevel::onEntityAdded()` / `onEntityRemoved()` setzen automatisch das Level-Dirty-Flag (`setIsSaved(false)`) und `setScenePrepared(false)` via Callback, sodass alle Aufrufer (Spawn, Delete, Landscape) einheitlich behandelt werden.

#### Skybox
- **Cubemap-Skybox**: Pro Level kann ein Skybox-Ordnerpfad oder ein `.asset`-Pfad gesetzt werden (`EngineLevel::setSkyboxPath()`). Der Ordner muss 6 Face-Bilder enthalten: `right`, `left`, `top`, `bottom`, `front`, `back` (als `.jpg`, `.png` oder `.bmp`).
- **Cubemap-Face-Zuordnung**: Die Faces werden gemäß der OpenGL-Cubemap-Konvention geladen: `right`→`+X`, `left`→`-X`, `top`→`+Y`, `bottom`→`-Y`, `front`→`-Z`, `back`→`+Z`. Die Standardkamera blickt entlang `-Z`, weshalb `front` auf `GL_TEXTURE_CUBE_MAP_NEGATIVE_Z` abgebildet wird.
- **Skybox Asset-Typ** (`AssetType::Skybox`): Eigener Asset-Typ mit JSON-Struktur `{ "faces": { "right": "...", "left": "...", ... }, "folderPath": "..." }`. Wird über `AssetManager::loadSkyboxAsset()` / `saveSkyboxAsset()` / `createSkyboxAsset()` verwaltet.
- **Level-JSON**: Der Pfad wird im Level-JSON als `"Skybox": "Skyboxes/MySkybox.asset"` (oder Ordnerpfad) gespeichert und beim Laden automatisch wiederhergestellt.
- **Rendering**: Die Skybox wird als erster 3D-Pass gerendert (nach glClear, vor Scene), mit `glDepthFunc(GL_LEQUAL)` und `glDepthMask(GL_FALSE)`. Die View-Matrix wird von der Translation befreit (`mat4(mat3(view))`), sodass die Skybox der Kamera folgt.
- **Scene-Prepare**: Beim Level-Prepare (`!isScenePrepared()`-Block) wird geprüft, ob das Level einen Skybox-Pfad hat aber die Cubemap noch nicht geladen ist (z.B. nach Fehlschlag oder Levelwechsel). In diesem Fall wird `setSkyboxPath` erneut aufgerufen, sodass die Skybox zuverlässig zusammen mit dem restlichen Level geladen wird.
- **Runtime-Build**: Im Runtime-Level-Ladepfad (main.cpp) wird nach `loadLevelAsset()` der Skybox-Pfad aus dem geladenen EngineLevel auf den Renderer angewendet (`renderer->setSkyboxPath(level->getSkyboxPath())`), analog zum Editor-Ladepfad.
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
| `get_cpu_info()`            | CPU-Info Dict (brand, physical_cores, logical_cores) |
| `get_gpu_info()`            | GPU-Info Dict (renderer, vendor, driver_version, vram_total_mb, vram_free_mb) |
| `get_ram_info()`            | RAM-Info Dict (total_mb, available_mb) |
| `get_monitor_info()`        | Liste von Monitor-Dicts (name, width, height, refresh_rate, dpi_scale, primary) |

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
    - MOUSE_BUTTON_DOWN (Links) → während PIE-Capture ignoriert; sonst PIE-Recapture (Position speichern + Grab) oder UI-Hit-Test oder Entity-Picking (Ctrl+Klick für Multi-Select Toggle)
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
        F → Fokus auf selektierte Entity (Kamera-Transition zum AABB-Center)
        DELETE → Selektierte Entity(s) löschen (Gruppen-Delete aller selektierten Entities, EntitySnapshot pro Entity → Gruppen-Undo/Redo-Action)
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
| DELETE     | Selektierte Entity(s) löschen (Gruppen-Delete mit Undo/Redo) |
| Ctrl+Klick | Multi-Select Toggle (Entity zur Selektion hinzufügen/entfernen) |

### Kamera-Steuerung

**Editor-Modus (Normal):**

| Eingabe           | Aktion                                |
|-------------------|---------------------------------------|
| RMB + W/A/S/D     | Kamera vorwärts/links/rückwärts/rechts |
| RMB + Q/E         | Kamera runter/hoch                    |
| Rechte Maustaste  | Kamera-Rotation aktivieren            |
| Mausrad (+ RMB)   | Kamera-Geschwindigkeit ändern (0.5x–5.0x) |
| W/E/R (ohne RMB)  | Gizmo-Modus: Translate/Rotate/Scale (Multi-Entity-Gruppen-Gizmo) |

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

## 17. Gameplay UI System (Viewport UI)

**Dateien:** `src/Renderer/ViewportUIManager.h/.cpp`, `src/Renderer/UIManager.h/.cpp` (Designer-Tab), `src/Scripting/PythonScripting.cpp`
**Plan:** `GAMEPLAY_UI_PLAN.md` (Phase A–D mit Fortschritts-Tracking)

### 17.1 Übersicht

Ein **Runtime-UI-System**, das unabhängig vom Editor-UI (`UIManager`) operiert und Widgets ausschließlich innerhalb des Viewport-Bereichs rendert. Für Spieler-HUD, Menüs und In-Game-UI im PIE-Modus. Widgets werden im **Widget Editor** gestaltet und per `engine.ui.spawn_widget(path)` zur Laufzeit angezeigt. Die frühere dynamische Erstellungs-API (`engine.viewport_ui`) wurde entfernt.

### 17.2 ViewportUIManager (Multi-Widget)

Eigenständiger Manager mit dem Viewport-Content-Rect als Basis:

- **Multi-Widget-System**: `vector<WidgetEntry>` mit per-Widget Z-Order-Sortierung; `createWidget(name, z_order)`, `removeWidget(name)`, `getWidget(name)`, `clearAllWidgets()`
- **Canvas Panel Root**: Jedes neue Widget erhält ein Root-Canvas-Panel (`isCanvasRoot=true`), das im Widget Editor nicht gelöscht werden kann
- **WidgetAnchor**: 10 Werte – TopLeft, TopRight, BottomLeft, BottomRight, Top, Bottom, Left, Right, Center, Stretch – pro Element setzbar (im Details-Panel per Dropdown + Offset X/Y)
- **Anchor-Layout**: `computeAnchorPivot()` + `ResolveAnchorsRecursive()` mit 3-Case-Logik:
  - **Normalized**: from/to-Werte ≤1.0 → werden mit Parent-Dimensionen multipliziert
  - **Stretch**: Füllt den Parent mit Offset-Margins
  - **Anchor-basiert**: Pixel-basierte Positionierung mit Anchor-Pivot + Offset
- **Serialisierung**: `anchor`, `anchorOffset`, `isCanvasRoot` werden in Widget-Asset-JSON persistiert
- **Viewport-Rect-Verwaltung**: `setViewportRect(x,y,w,h)` – Pixel-Koordinaten des Viewport-Bereichs
- **Element-Zugriff**: `findElementById(id)` – rekursive Suche über alle Widgets
- **Layout**: Dirty-Tracking, `updateLayout()` mit Text-Measure-Callback
- **Input**: `handleMouseDown/Up`, `handleScroll`, `handleTextInput`, `handleKeyDown` – Koordinaten intern von Fenster- in Viewport-lokale Pixel umgerechnet; Z-Order-basiertes Multi-Widget-Hit-Testing
- **Gamepad**: `handleGamepadButton(button, pressed)` und `handleGamepadAxis(axis, value)` – D-Pad/Left-Stick → Spatial-Navigation, A → Aktivierung, B → Fokus löschen, LB/RB → Tab-Navigation. Left-Stick mit Deadzone (0.25), Repeat-Delay (0.35s) und Repeat-Interval (0.12s). SDL3-Gamepad-Events werden in `main.cpp` geroutet (`SDL_INIT_GAMEPAD`, Auto-Open erster Controller).
- **Selektion**: `setSelectedElementId()` mit `setOnSelectionChanged`-Callback (bidirektionale Sync mit UI Designer)
- **Cursor-Steuerung**: `setGameplayCursorVisible(bool)` steuert SDL-Cursor + unterdrückt Kamera-Rotation im PIE-Modus
- **Auto-Cleanup**: Alle Widgets werden beim PIE-Stop automatisch zerstört

### 17.3 Integration in OpenGLRenderer

- **Member**: `m_viewportUIManager` in `OpenGLRenderer`
- **Viewport-Rect**: `m_viewportContentRect` in `UIManager` wird nach jedem Layout-Update an `ViewportUIManager` übergeben
- **Rendering**: `renderViewportUI()` rendert nach `renderWorld()`, vor dem Blit auf den Default-FBO:
  - Full-FBO-Viewport (`glViewport(0,0,wW,wH)`) mit offset-verschobener Ortho-Projektion
  - Scissor-Test clippt auf den Viewport-Bereich
  - Multi-Widget-Rendering in Z-Order-Reihenfolge (alle Elemente inkl. ProgressBar/Slider)
  - **Selektions-Highlight**: Orangefarbener 2px-Rahmen um das selektierte Element (4 Kanten-Panels)
  - Nur für den Viewport-Tab aktiv
- **Input-Routing**: Im Main-Loop (`main.cpp`): Editor-UI → Viewport-UI → 3D-Interaktion

### 17.4 UI Designer Tab

Editor-Tab (wie MeshViewer, kein Popup) für visuelles Viewport-UI-Design:

- **UIDesignerState** in `UIManager.h`: tabId, leftWidgetId, rightWidgetId, toolbarWidgetId, selectedWidgetName, selectedElementId, isOpen
- **Toolbar** (z=3): New Widget, Delete Widget, Status-Label ("N Widgets, M Elements")
- **Linkes Panel** (z=2, 250px): Controls-Palette (7 Typen: Panel, Text, Label, Button, Image, ProgressBar, Slider) + Widget-Hierarchie-TreeView mit Selektion-Highlighting
- **Rechtes Panel** (z=2, 280px): Properties-Panel (dynamisch, typ-basiert) mit Sektionen:
  - Identity (Type, ID editierbar)
  - Transform (From X/Y, To X/Y)
  - Anchor (WidgetAnchor-Dropdown mit 10 Werten, Offset X/Y)
  - Hit Test (HitTestMode-Dropdown: Enabled / Disabled Self / Disabled Self + Children – DisabledAll überschreibt Kinder)
  - Layout (Min/Max Size, Padding, H/V Align, SizeToContent, Spacing)
  - Appearance (Color RGBA, Opacity, Visible, Border Width/Radius)
  - Text (Text, Font Size, Text Color RGBA, Bold/Italic) – nur für Text/Button/Label
  - Image (Image Path) – nur für Image
  - Value (Min/Max/Value) – nur für Slider/ProgressBar
  - Delete Element Button
- **Bidirektionale Sync**: `setOnSelectionChanged`-Callback verbindet Viewport-Klick mit Designer-Selektion und umgekehrt
- **Öffnung**: Über Settings-Dropdown im ViewportOverlay

### 17.5 Python-API

- `engine.ui.spawn_widget(content_path)` – Widget-Asset laden und im Viewport anzeigen (`.asset`-Endung wird automatisch ergänzt), gibt Widget-ID zurück
- `engine.ui.remove_widget(widget_id)` – Widget aus dem Viewport entfernen
- `engine.ui.show_cursor(visible)` – Gameplay-Cursor ein-/ausblenden (+ Kamera-Blockade)
- `engine.ui.clear_all_widgets()` – Alle Viewport-Widgets entfernen
- `engine.ui.set_focus(element_id)` – Fokus auf ein Viewport-UI-Element setzen
- `engine.ui.clear_focus()` – Fokus vom aktuell fokussierten Element entfernen
- `engine.ui.get_focused_element()` – ID des fokussierten Elements (oder None)
- `engine.ui.set_focusable(element_id, focusable)` – Element als fokussierbar markieren
- `engine.pyi` IntelliSense-Stubs synchronisiert
- Automatisches Cleanup aller Script-Widgets bei PIE-Stop
- ~~`engine.viewport_ui` (28 Methoden)~~ – Entfernt zugunsten des Asset-basierten Ansatzes

### 17.6 Aktueller Status

| Phase | Beschreibung | Status |
|---|---|---|
| Phase A | Runtime-System (Multi-Widget, Anchor, Layout, Rendering, Input) | ✅ Abgeschlossen |
| Phase 2 | Asset-Integration (Canvas-Root, isCanvasRoot, Serialisierung, normalisierte from/to) | ✅ Abgeschlossen |
| Phase B | UI Designer Tab (Controls, Hierarchie, Properties inkl. Anchor-Dropdown, Sync, Highlight) | ✅ Abgeschlossen |
| Scripting | Vereinfacht: spawn_widget, remove_widget, show_cursor, clear_all_widgets + Focus API | ✅ Abgeschlossen |
| Phase 5 | Focus System & Keyboard Navigation (FocusConfig, Tab/Arrow/Enter/Escape, Focus-Highlight, Python API) + Gamepad-Input-Adapter (D-Pad/Stick/A/B/LB/RB → FocusManager) | ✅ Abgeschlossen |
| Phase D | UX-Verbesserungen (Undo/Redo, Drag & Drop, Copy/Paste, Responsive-Preview) | ❌ Zukunft |

### 17.7 Nächste Schritte (Phase D)

1. Undo/Redo für Designer-Aktionen
2. Drag & Drop aus Palette in Viewport
3. Copy/Paste von Elementen
4. Responsive-Preview (Fenstergrößen-Simulation)
5. Detaillierter Plan in `GAMEPLAY_UI_PLAN.md`

---

*Generiert aus dem Quellcode des Engine-Projekts. Stand: aktueller Branch `master`.*

### Cleanup-Änderungen
- **12 leere UIWidget .cpp-Stubs entfernt** (alle Widgets außer ColorPickerWidget sind header-only)
- **Legacy-ECS-System entfernt** (`src/ECS/` Verzeichnis, `ecs::` Namespace – war nicht im Build)
- **Toast-Dauer-Konstanten** (`kToastShort`/`kToastMedium`/`kToastLong`) in `UIManager.h` eingeführt
- **`markAllWidgetsDirty()` in 13 `refresh*`-Methoden integriert** (50 redundante Aufrufe entfernt)
- **OpenGLRenderer Member-Struct-Gruppierung**: 12 Sub-Structs (`ThumbnailResources`, `GridResources`, `SkyboxResources`, `BoundsDebugResources`, `HeightFieldDebugResources`, `PickingResources`, `OutlineResources`, `GizmoResources`, `ShadowResources`, `PointShadowResources`, `CsmResources`, `OitResources`) fassen ~108 Member-Variablen zusammen (~565 Referenzen in `.cpp` aktualisiert)
- **`#if ENGINE_EDITOR`-Konsolidierung** in `main.cpp`: Guards von 21 auf 19 reduziert (Shutdown, Shortcuts, Startup-Init zusammengeführt)
- **Build-Pipeline extrahiert**: ~750-Zeilen Build-Lambda aus `main.cpp` in `BuildPipeline.h/.cpp` ausgelagert (CMake configure/build, Asset-Cooking, HPK-Packaging, Deployment)
