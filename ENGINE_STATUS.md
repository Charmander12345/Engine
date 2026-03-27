# Engine – Status & Roadmap

> Übersicht über den aktuellen Implementierungsstand und offene Punkte – pro Modul gegliedert.
> Branch: `Json_and_ecs` | Stand: aktuell

---

## Letzte Änderung (PBR Specular Fix + Material Editor Redesign)

- ✅ `Specular Map eliminiert Reflexionen vollständig bei Wert 0`: Die vorherige Fix (`F0 *= specColor`) war unzureichend — Fresnel-Schlick `F = F0 + (1-F0)*pow(1-cosTheta,5)` erzeugte weiterhin nicht-null Specular bei Glancing Angles selbst mit F0=0. **Neuer Fix (fragment.glsl):** Statt F0 zu modulieren wird jetzt der finale `specBRDF` mit `specColor * uSpecularMultiplier` multipliziert. Dies garantiert `specBRDF * vec3(0) = vec3(0)` unabhängig von Fresnel/NDF/Geometrie. Blinn-Phong-Pfad ebenfalls mit `uSpecularMultiplier` versehen.
- ✅ `Specular Multiplier als neues Material-Property`: Neues `uSpecularMultiplier`-Uniform (Standard: 1.0) durch die gesamte Pipeline: `Material.h` → `Components.h` (MaterialOverrides) → `OpenGLMaterial` → `RenderResourceManager` → `OpenGLObject3D` → `OpenGLRenderer` Overrides → `EngineLevel` Serialisierung. Erlaubt Künstlern die globale Specular-Intensität pro Material/Entity zu steuern.
- ✅ `Material Editor Redesign (Entity Details)`: Material-Sektion im Entity Details Panel zeigt jetzt editierbare Float-Felder für Metallic, Roughness und Specular Multiplier mit Undo/Redo-Support und Entity-Invalidierung.
- ✅ `New Material Dialog Redesign`: Shininess-Feld ersetzt durch Metallic, Roughness und Specular Multiplier. Neue Materialien werden mit PBR-enabled erstellt.

## Letzte Änderung (PBR Specular Map Fix — Initial)

- ✅ `PBR-Pfad nutzt jetzt Specular Map`: Der Cook-Torrance-PBR-Pfad im Fragment-Shader ignorierte die Specular Map komplett — `specColor` wurde zwar korrekt gesampelt und an `calcLight()` übergeben, aber nur im Blinn-Phong-Fallback verwendet. Im PBR-Pfad wurde `F0 = mix(vec3(0.04), diffColor, metallic)` ohne Berücksichtigung der Specular Map berechnet. **Auswirkung:** Bereiche mit Specular-Map-Wert 0 (z.B. Holz beim Container-Material) zeigten trotzdem Reflexionen (dielectric F0 = 0.04). **Fix (fragment.glsl):** `F0 *= specColor;` nach der F0-Berechnung eingefügt. Wurde in Phase 2 durch vollständigere specBRDF-Multiplikation ersetzt.

## Letzte Änderung (Toast/Modal Messages Editor-Only)

- ✅ `Toast- und Modal-Nachrichten aus Runtime-Builds entfernt`: Toast-Notifications und modale Dialoge waren in allen Runtime-Builds (Debug/Development/Shipping) sichtbar. **Root Cause:** `showToastMessage()`, `showModalMessage()`, `closeModalMessage()`, Notification-Polling und Toast-Timer/Fade in `UIManager.cpp` sowie `enqueueToastNotification()`/`enqueueModalNotification()` in `DiagnosticsManager.cpp` waren komplett ungeschützt — keine `#if ENGINE_EDITOR`-Guards. **Fix:**
  - **UIManager.cpp:** `showModalMessage()`, `closeModalMessage()`, `showToastMessage()` (beide Überladungen), `ensureModalWidget()`, `createToastWidget()`, `updateToastStackLayout()` — alle Methodenrümpfe in `#if ENGINE_EDITOR` gewrappt (leere No-Op-Funktionen in Runtime).
  - **UIManager.cpp `updateNotifications()`:** Notification-Polling aus `DiagnosticsManager` (Modal + Toast Consume) und Toast-Timer/Fade/Removal-Logik in eigenen `#if ENGINE_EDITOR`-Block verschoben. Shared-Logik (Hover-Transitions, Scrollbar-Visibility) bleibt unberührt.
  - **DiagnosticsManager.cpp:** `enqueueModalNotification()` und `enqueueToastNotification()` — Bodies in `#if ENGINE_EDITOR` (No-Op in Runtime, kein Memory-Accumulation). `consumeModalNotifications()` und `consumeToastNotifications()` — geben in Runtime leere Vektoren zurück.
  - **Deklarationen bleiben sichtbar:** Alle Methoden-Deklarationen in den Headers sind NICHT hinter Guards (71+ Aufrufstellen in Shared-Code). Die leeren Methodenrümpfe verhindern Compile-Fehler.
  - **Ergebnis:** Kein Toast, kein Modal, kein Notification-Polling, kein Toast-Timer in Runtime-Builds. Python `engine.ui.show_toast_message()` / `show_modal_message()` sind funktionslose No-Ops. Alle Build-Profile (Debug/Development/Shipping) kompilieren fehlerfrei.

## Letzte Änderung (PIE-Entfernung aus Runtime/Packaged Builds)

- ✅ `PIE-Konzept aus Runtime-Builds entfernt`: Runtime/Packaged Builds nutzten `diagnostics.setPIEActive(true)` als Hack, damit Physik und Scripting liefen — PIE (Play In Editor) ist aber ein reines Editor-Konzept (Snapshot/Restore, Play/Stop-Toggle). **Root Cause des Build-Fehlers:** PIE.Stop- und PIE.ToggleInput-Shortcuts referenzierten `stopPIE` (editor-only Variable, hinter `#if ENGINE_EDITOR`), waren aber im `#if !defined(ENGINE_BUILD_SHIPPING)`-Block registriert. **Fix (main.cpp):**
  - **PIE-Shortcuts hinter ENGINE_EDITOR:** `PIE.Stop` und `PIE.ToggleInput` in inneren `#if ENGINE_EDITOR`-Block verschoben (existieren nur noch im Editor).
  - **setPIEActive(true) entfernt:** Künstliche PIE-Aktivierung aus der Runtime-Initialisierung entfernt — Runtime braucht kein PIE mehr.
  - **Physik/Scripting direkt:** Main-Loop-Gate von `if (isPIEActive())` zu `if (isRuntimeMode || isPIEActive())` geändert. Physik und Scripting laufen in Runtime-Builds bedingungslos.
  - **Kamerabewegung direkt:** Kamera-Bewegung (`canMove`) nutzt `isRuntimeMode` als eigene Bedingung neben dem PIE-Pfad. Kein PIE nötig für WASD-Steuerung.
  - **Input-Gates aktualisiert:** Alle Mouse-Motion/Click/Scroll-PIE-Guards um `isRuntimeMode` erweitert: Mausbewegung (UI-Skip bei Capture), Linksklick (Ignore bei Capture), Rechtsklick (Ignore bei Capture), Mouse-Up (Ignore bei Capture), Mausrad (Skip bei Capture), Kamerarotation (isRuntimeMode-Trigger), Recapture nach Input-Pause.
  - **Script-Key-Events:** `HandleKeyUp`/`HandleKeyDown` feuern in Runtime-Builds direkt (über `isRuntimeMode || isPIEActive()`).
  - **Ergebnis:** Runtime-Builds haben einen sauberen Game-Loop ohne PIE-Abstraktion. Physik, Scripting, Kamera und Input funktionieren direkt. Development/Debug-Runtime-Builds kompilieren wieder fehlerfrei.

## Letzte Änderung (ShortcutManager in Debug/Development Runtime-Builds)

- ✅ `ShortcutManager in Debug/Development Runtime-Builds`: Tastenkombinationen (F10/F9/F8/F11/F12/ESC/Shift+F1) funktionierten nur im Editor, nicht in gebauten Debug/Development-Spielen. **Root Cause:** `ShortcutManager.h`-Include und alle Shortcut-Registrierungen + Event-Dispatch waren vollständig hinter `#if ENGINE_EDITOR` geschützt — d.h. in ALLEN Runtime-Builds (auch Debug/Development) nicht verfügbar. Ein Fallback-Block handelte F10/F9/F8/F11 direkt, war aber unvollständig (F9/F8/F11 nur in ENGINE_BUILD_DEBUG, kein F12/ESC/Shift+F1). **Fix (main.cpp):** Dreistufiges Guard-Modell implementiert:
  - **Include:** `#include "Core/ShortcutManager.h"` von `#if ENGINE_EDITOR` zu `#if !defined(ENGINE_BUILD_SHIPPING)` verschoben.
  - **Registrierung:** Block in `#if !defined(ENGINE_BUILD_SHIPPING)` (äußerer Guard) mit verschachtelten `#if ENGINE_EDITOR`-Blöcken für Editor-only Shortcuts (Undo/Redo/Save/Search/Copy/Paste/Duplicate/Gizmo/Focus/DropToSurface/Help/Import/Delete). Debug/Runtime-Shortcuts (F11 UIDebug, F8 BoundsDebug, F10 Metrics, F9 OcclusionStats, ESC PIE.Stop, Shift+F1 PIE.ToggleInput, F12 FPSCap) bleiben im äußeren `!SHIPPING`-Scope.
  - **Dispatch:** KEY_UP/KEY_DOWN `ShortcutManager::handleKey()`-Guards von `#if ENGINE_EDITOR` zu `#if !defined(ENGINE_BUILD_SHIPPING)` geändert.
  - **Fallback entfernt:** Redundanter direkter Key-Handling-Block (`#if !ENGINE_EDITOR && !defined(ENGINE_BUILD_SHIPPING)`) vollständig entfernt — ShortcutManager übernimmt alles.
  - **Ergebnis:** Debug- und Development-Builds haben vollen ShortcutManager-Support (F10/F9/F8/F11/F12/ESC/Shift+F1). Shipping-Builds enthalten keinen ShortcutManager — bare minimum zum Laufen.

## Letzte Änderung (Build-Flow Audit: Shipping-Optimierungen & Profil-Korrekturen)

- ✅ `Landscape.dll in Editor-Only-DLL-Ausschlussliste`: Die Build-Deploy-Step kopierte `Landscape.dll` (Editor-Variante) in den Build-Output, obwohl die Runtime `LandscapeRuntime.dll` verwendet. **Fix (main.cpp, Deploy Step 4):** `Landscape.dll` zur `editorOnlyDlls`-Ausschlussliste hinzugefügt (3→4 Einträge). Shipping-Builds enthalten keine unnötige Editor-DLL mehr.

- ✅ `ScriptHotReload in Shipping deaktiviert`: `Scripting::InitScriptHotReload()` und `Scripting::PollScriptHotReload()` liefen auch in Shipping-Builds — unnötiger Filesystem-Scan nach .py-Änderungen alle 500ms. **Fix (main.cpp):** Beide Aufrufe hinter `#if !defined(ENGINE_BUILD_SHIPPING)` Guard. Kein Hot-Reload-Overhead im Shipping-Build.

- ✅ `Metrics-Textformatierung in Shipping eliminiert`: Die FPS/CPU/GPU-Metriken-Strings (snprintf + std::string-Allokationen) wurden pro Frame formatiert, auch wenn `showMetrics=false` in Shipping. **Fix (main.cpp):** Beide Textformatierungs- und queueText-Blöcke hinter `#if !defined(ENGINE_BUILD_SHIPPING)` Guard. Eliminiert ~15 snprintf-Aufrufe und String-Allokationen pro Frame im Shipping-Build.

- ✅ `Editor-Only Includes hinter ENGINE_EDITOR Guard`: `PopupWindow.h`, `TextureViewerWindow.h`, `EditorTheme.h`, `AssetCooker.h` wurden in main.cpp außerhalb von `#if ENGINE_EDITOR` inkludiert, obwohl sie nur im Editor-Code verwendet werden. **Fix (main.cpp):** Alle vier Includes hinter `#if ENGINE_EDITOR` verschoben. Explizites `#include "UIManager.h"` hinzugefügt (war zuvor transitiv über PopupWindow.h eingebunden). Runtime-Build kompiliert weniger Header.

- ✅ `Build-Profil korrekt bei Baked-In Config`: Bei kompiliertem `GAME_START_LEVEL` war `rtBuildProfile` fest auf "Shipping" gesetzt, unabhängig vom tatsächlichen `ENGINE_BUILD_PROFILE`. `rtEnableHotReload` und `rtEnableProfiler` stimmten nicht mit dem Profil überein. **Fix (main.cpp):** `rtBuildProfile`, `rtEnableHotReload` und `rtEnableProfiler` werden jetzt über `#if defined(ENGINE_BUILD_DEBUG)` / `ENGINE_BUILD_DEVELOPMENT` / else korrekt gesetzt. Debug-Profil aktiviert HotReload+Profiler, Shipping deaktiviert beides.

## Letzte Änderung (Build-Profile Compile-Defines & PDB-Separation)

- ✅ `Build-Profile Compile-Time Definitions`: Drei separate Compile-Defines für die Runtime-Build-Profile: `ENGINE_BUILD_DEBUG`, `ENGINE_BUILD_DEVELOPMENT`, `ENGINE_BUILD_SHIPPING`. Jedes Profil kompiliert unterschiedlich viele Dev-Features in die Runtime-Binary. **CMakeLists.txt:** Neuer CMake-Parameter `ENGINE_BUILD_PROFILE` (Debug/Development/Shipping, Default: Shipping) setzt das entsprechende Define auf `HorizonEngineRuntime`. **main.cpp (Build-Pipeline):** `-DENGINE_BUILD_PROFILE=<name>` wird an CMake configure übergeben. **Feature-Gating (main.cpp):**
  - **Shipping** (`ENGINE_BUILD_SHIPPING`): Kein FPS-Overlay, keine Metrics, keine Debug-Shortcuts, kein Profiler, stdout unterdrückt. Reines Spiel ohne Dev-Features.
  - **Development** (`ENGINE_BUILD_DEVELOPMENT`): F10 Metrics-Toggle, FPS/Performance-Overlay. Basis-Diagnostik.
  - **Debug** (`ENGINE_BUILD_DEBUG`): Alle Dev-Tools – F10 Metrics, F9 Occlusion-Stats, F8 Bounds-Debug, F11 UI-Debug. Validierung, ausführliches Logging.

- ✅ `PDB-Dateien in Symbols/-Unterverzeichnis`: PDB-Dateien (Runtime-Exe + DLLs) werden bei non-Shipping-Builds jetzt in ein `Symbols/`-Unterverzeichnis im Build-Output kopiert statt neben die Binaries. Schlanker Build-Output, PDBs für Crash-Analyse separat verfügbar.

- ✅ `FPS-Overlay an showMetrics gekoppelt`: Das FPS/Speed-Text-Overlay im Viewport war bisher immer sichtbar (auch in Shipping). Jetzt nur noch angezeigt wenn `showMetrics=true` (Editor + Debug/Development via F10-Toggle). In Shipping komplett unsichtbar.

## Letzte Änderung (Async CMake/Toolchain-Erkennung)

- ✅ `CMake/Toolchain-Erkennung async & ohne Konsolen-Popup`: Die CMake- und C++-Toolchain-Erkennung beim Editor-Start war synchron (blockierte Startup) und verwendete `std::system()` / `_popen()`, die sichtbare Konsolenfenster öffneten. **Fix (UIManager.cpp):** Neue Hilfsfunktionen `shellExecSilent()` und `shellReadSilent()` verwenden `CreateProcessA` mit `CREATE_NO_WINDOW` – kein Konsolenfenster mehr sichtbar. **Fix (UIManager.h/cpp + main.cpp):** Neue Methoden `startAsyncToolchainDetection()` (startet Erkennung auf Hintergrund-Thread) und `pollToolchainDetection()` (Main-Thread: loggt Ergebnis und zeigt Install-Prompts wenn nötig). `m_cmakeAvailable` / `m_toolchainAvailable` sind jetzt `std::atomic<bool>` für Thread-Safety. Doppelte Erkennung im First-Frame-Block entfernt. Die Engine startet sofort — Erkennung läuft parallel im Hintergrund.

## Letzte Änderung (Runtime-Skybox-Fix)

- ✅ `Skybox im Runtime-Build`: Im gebauten Spiel fehlte die Skybox, obwohl sie im Editor korrekt angezeigt wurde. **Root Cause:** Im Editor-Level-Ladepfad wird nach `loadLevelAsset()` der Skybox-Pfad aus dem EngineLevel auf den Renderer angewendet (`renderer->setSkyboxPath(newLevel->getSkyboxPath())`). Im Runtime-Level-Ladepfad (main.cpp) fehlte dieser Aufruf — die Skybox wurde aus der Level-JSON korrekt geparst, aber nie dem Renderer mitgeteilt. **Fix (main.cpp, Runtime-Level-Load):** Nach `diagnostics.setPIEActive(true)` wird jetzt `renderer->setSkyboxPath(level->getSkyboxPath())` aufgerufen, analog zum Editor-Ladepfad.

## Letzte Änderung (CrashHandler PID-einzigartige Pipe-Namen & Deploy-Fixes)

- ✅ `CrashHandler PID-einzigartige Pipe-Namen`:

- ✅ `CrashHandler.exe im Build-Output`: CrashHandler.exe fehlte im Build-Game-Output → "CrashHandler.exe not found". **Fix (main.cpp, Deploy Step 4):** `Tools/CrashHandler.exe` (+ PDB für nicht-Shipping-Profile) wird aus dem Editor-Verzeichnis in den Build-Output kopiert.

- ✅ `Runtime-DLLs im Build-Output (Binary-Cache-Deploy)`: Runtime-DLLs (`RendererRuntime.dll`, `AssetManagerRuntime.dll`, `ScriptingRuntime.dll`, `LandscapeRuntime.dll`) fehlten im Build-Output. **Root Cause:** Der Deploy-Step kopierte DLLs nur aus dem Editor-Verzeichnis. Die Runtime-DLLs werden aber vom CMake-Build im Binary-Cache erzeugt (gleicher Ordner wie `HorizonEngineRuntime.exe`), nicht im Editor-Verzeichnis, da der Editor sie nicht benötigt. **Fix (main.cpp, Deploy Step 4):** DLLs (+ PDBs für non-Shipping) werden jetzt zuerst aus dem Binary-Cache-Output kopiert. Danach werden verbleibende DLLs (z.B. Python, SDL3) aus dem Editor-Verzeichnis nachgezogen, wobei bereits vorhandene DLLs übersprungen und Editor-Only-DLLs (`AssetManager.dll`, `Scripting.dll`, `Renderer.dll`) weiterhin ausgeschlossen werden.

## Letzte Änderung (CMake-Build-Pipeline wiederhergestellt, VSync-Tearing-Fix & Build-Profil-Overlay)

- ✅ `Build-Pipeline: CMake-basierter Build mit inkrementellem Binary-Cache`: Die Build-Game-Pipeline wurde zunächst auf Pre-Built-Binary-Deployment umgestellt (kein CMake nötig), dann aber wieder auf CMake-basierte Kompilierung zurückgesetzt, da (1) Build-Profile als Compile-Defines korrekt eingebacken werden müssen (`GAME_START_LEVEL`, `GAME_WINDOW_TITLE`), (2) zukünftig C++ Gameplay-Logik in den Build kompiliert werden soll, und (3) ohne CMake die Konfigurationsoptionen nicht korrekt angewendet werden. **Implementierung (main.cpp):** (1) CMake/Toolchain-Detection beim Editor-Start wiederhergestellt (`detectCMake()` + `detectBuildToolchain()`) mit Warnungen/Prompts bei Fehlen. (2) Build-Pipeline hat 8 Schritte: Step 1 Output-Dir, Step 2 CMake configure (`-DGAME_START_LEVEL`, `-DGAME_WINDOW_TITLE`, `-DENGINE_BUILD_RUNTIME=ON`, Generator via `CMAKE_GENERATOR`-Define), Step 3 CMake build (`--target HorizonEngineRuntime --config <profile>`), Step 4 Deploy (gebaute exe aus Binary-Cache + DLLs/PDBs/Python aus Editor-Dir), Step 5 Asset-Cooking, Step 6 HPK-Packaging, Step 7 game.ini, Step 8 Done. (3) `runCmdWithOutput()` nutzt `CreateProcess(CREATE_NO_WINDOW)` mit Pipe-Redirection (kein sichtbares Konsolenfenster). (4) Inkrementelle Builds über `config.binaryDir` (`<Projekt>/Binary`) — nur geänderte Dateien werden rekompiliert. (5) `config.cleanBuild` löscht den Binary-Cache vor dem Build. (6) Built-exe wird im Binary-Cache unter `<config>/HorizonEngineRuntime.exe` (Multi-Config) oder direkt (Single-Config) gefunden. (7) Guard: Build Game ist nur verfügbar wenn CMake + C++-Toolchain erkannt wurden.

- ✅ `VSync Runtime-Default (Tearing-Fix)`: Beim Drehen der Kamera im Runtime-Build war das Bild stark zerrissen (Screen-Tearing). **Root Cause:** Der Editor setzt VSync beim Start explizit auf `false` (Zeile 509). Der Pre-Build-Sync speicherte `VSyncEnabled=0` in `game.ini`. Im Runtime-Defaults-Block fehlte ein VSync-Fallback → Renderer-Default (OFF) blieb aktiv. **Fix (main.cpp, Runtime-Defaults-Block):** `if (!diag.getState("VSyncEnabled")) renderer->setVSyncEnabled(true);` — Runtime-Builds haben jetzt VSync standardmäßig AN. Editor behält VSync=OFF für bessere Entwicklungs-Performance.

- ✅ `showMetrics an Build-Profil gekoppelt (Performance-Overlay im Dev/Debug-Build)`: Das On-Screen-Performance-Overlay (FPS, Frame-Time) funktionierte in keinem Runtime-Build, obwohl `EnableProfiler` korrekt in `game.ini` stand. **Root Cause:** `showMetrics = !isRuntimeMode` setzte das Overlay in ALLEN Runtime-Builds auf `false`. Die Variable `rtEnableProfiler` wurde aus `game.ini` geparst, aber nirgends verwendet. **Fix (main.cpp):** `showMetrics = !isRuntimeMode || rtEnableProfiler` — Development- und Debug-Profile haben `EnableProfiler=true` → Overlay ist standardmäßig AN. Shipping-Profile haben `EnableProfiler=false` → Overlay ist OFF. F10-Toggle funktioniert weiterhin in allen Builds.

## Letzte Änderung (Runtime-Helligkeit Fix & Performance-Overlay im Dev-Build)

- ✅ `loadConfig() Merge-Modus (Hauptursache "zu hell")`:

- ✅ `config.ini Windows-NTFS-Case-Insensitivity Fix`: Im Build-Schritt 5 wurde `config/config.ini` **vor** dem HPK-Packaging-Schritt kopiert. Auf Windows NTFS (case-insensitive) sind `Config/` und `config/` dasselbe Verzeichnis. Schritt 6 packt `Config/` (AssetRegistry.bin, defaults.ini) ins HPK und löscht danach das Verzeichnis via `remove_all("Config")` — dabei wurde auch `config/config.ini` gelöscht. Im Runtime-Build fehlte daher `config.ini` auf Disk. **Fix (main.cpp, Build Step 6):** Die `config/config.ini`-Kopie wurde **nach** dem `remove_all("Config")`-Aufruf verschoben, sodass die Datei als lose Datei im Build-Output erhalten bleibt.

- ✅ `Performance-Overlay F10-Shortcut im Runtime-Build`: Der F10-Shortcut zum Ein-/Ausblenden des On-Screen-Performance-Overlays war nur im Editor verfügbar (via `ShortcutManager`, der hinter `#if ENGINE_EDITOR` kompiliert wird). **Fix (main.cpp):** Direktes F10-Key-Handling im `SDL_EVENT_KEY_DOWN`-Abschnitt der Main-Loop unter `#if !ENGINE_EDITOR`. Im Editor nutzt weiterhin der `ShortcutManager` den F10-Shortcut. Im Runtime-Build ist `showMetrics` standardmäßig `false` — Benutzer können das Overlay per F10 einschalten.

- ✅ `Post-Process-Shader Fehler-Logging`: `ensurePostProcessResources()` gab bei Fehlschlag ohne Logging `false` zurück, was dazu führte, dass Post-Processing (Gamma-Korrektur, Tone-Mapping) stillschweigend deaktiviert wurde — Szene erschien zu hell. **Fix (OpenGLRenderer.cpp):** Bei fehlgeschlagenem `PostProcessStack::init()` wird jetzt eine Error-Log-Meldung mit den versuchten Shader-Pfaden ausgegeben.

## Letzte Änderung (Vollständige Renderer-Config-Persistenz im Build)

- ✅ `Vollständige Renderer-Config-Sync vor Build`: 5 fehlende Renderer-Einstellungen wurden nicht vor dem Build in den `DiagnosticsManager` synchronisiert: TextureCompressionEnabled, TextureStreamingEnabled, DisplacementMappingEnabled, DisplacementScale, TessellationLevel. Diese existierten zwar in der Engine-Settings-UI und wurden bei User-Toggle gespeichert, aber der Pre-Build-Sync-Block in `main.cpp` hat sie übersprungen. **Fix (main.cpp, Pre-Build-Sync):** Alle 5 fehlenden Settings werden jetzt vor Build-Start über `diag.setState()` synchronisiert. Zusätzlich wird `diag.saveConfig()` nach dem Sync aufgerufen, damit `config/config.ini` auf Disk aktuell ist, bevor der Build-Thread die Datei kopiert.

- ✅ `Runtime-Fallback-Defaults erweitert`: Der Runtime-Fallback-Block (für Builds ohne persistierte Config) wurde um die 5 fehlenden Settings erweitert: TextureCompression (default: aus), TextureStreaming (default: aus), DisplacementMapping (default: aus), DisplacementScale (default: 0.5), TessellationLevel (default: 16.0). Damit sind alle Renderer-Settings in jedem Build-Szenario korrekt initialisiert.

## Letzte Änderung (Out-of-Process CrashHandler – Pipe-basiert)

- ✅ `CrashHandler (Out-of-Process, Named-Pipe IPC)`:

  **CrashHandler/CrashProtocol.h (NEU):** Shared IPC-Protokoll zwischen Engine und CrashHandler. Length-Prefixed Messages (4-Byte LE uint32 + UTF-8 Payload). Payload-Format: `TAG|data`. 13 Tags: `HB` (Heartbeat), `LOG` (Log-Eintrag), `STATE` (Engine-State), `HW` (Hardware-Info), `PROJ` (Projekt-Info), `FRAME` (Frame-Metriken), `CRASH` (Crash-Beschreibung+StackTrace), `QUIT` (Graceful Shutdown), `MODS` (Module), `UP` (Uptime), `CMD` (Commandline), `CWD` (Working Directory), `VER` (Engine-Version). `buildMessage(tag, data)` und `parsePayload(payload, outTag, outData)` Helfer. Pipe-Name: `\\.\pipe\HorizonEngineCrashPipe` (Win) / `/tmp/HorizonEngineCrashPipe` (Linux).

  **CrashHandler/CrashMain.cpp (REWRITE):** Pipe-basierter Crash-Reporter. `CrashReport`-Struct akkumuliert alle empfangenen Daten (engineVersion, commandLine, workingDir, PID, uptime, hardwareInfo, projectInfo, engineState, frameInfo, modules, logEntries als Ringpuffer, crashDescription, stackTrace, gracefulQuit-Flag). `processMessage()` dispatcht nach Tag. `buildReportText()` generiert formatierten Crash-Bericht. `saveReportFile()` speichert in `CrashReports/crash_YYYY-M-D_H-M-S.txt`. Windows: `CreateNamedPipeA`-Server → `ConnectNamedPipe` → Read-Loop → `MessageBoxA`-Dialog bei Crash. Linux: `AF_UNIX`-Socket-Server → `accept` → Read-Loop → `stderr`-Ausgabe bei Crash.

  **CrashHandler/CmakeLists.txt:** Deploy nach `${ENGINE_DEPLOY_DIR}/Tools`, C++20, WIN32_EXECUTABLE, inkludiert `CrashProtocol.h`.

  **Logger.h:** Neue Public-API: `startCrashHandler()` (Prozess starten + Pipe verbinden), `sendToCrashHandler(tag, data)` (Daten senden), `ensureCrashHandlerAlive()` (Prozess-Check + Restart), `stopCrashHandler()` (QUIT senden + Cleanup). Private: `launchCrashHandlerProcess()`, `connectPipe()`, `writePipe()`. Plattformabstraktion: Windows `void*` für HANDLE-Members (kein `<Windows.h>` im Header um ERROR-Makro-Konflikt mit `LogLevel::ERROR` zu vermeiden), Linux `int` für pid/fd.

  **Logger.cpp (REWRITE):** `<Windows.h>` nur im .cpp mit `#undef ERROR`/`#undef WARNING` nach Include. `log()` leitet jeden Eintrag per Pipe an den CrashHandler weiter (non-blocking best-effort). Destruktor ruft `stopCrashHandler()` auf. `captureStackTrace()` als separate Funktion (returns string). `sendCrashToPipe()` sendet CRASH-Tag mit Beschreibung + Stack-Trace. **Windows:** `launchCrashHandlerProcess()` findet `Tools/CrashHandler.exe` via `GetModuleFileNameA`, startet mit `CreateProcessA(DETACHED_PROCESS)`, übergibt Engine-PID. `connectPipe()` verbindet mit 50 Retries (100ms). `writePipe()` mit Broken-Pipe-Erkennung. `ensureCrashHandlerAlive()` via `GetExitCodeProcess`, Restart bei Prozess-Tod. `stopCrashHandler()` sendet QUIT, flusht, wartet 2s. **Linux:** `fork()`+`execl()`, `AF_UNIX`-Socket-Connect, `waitpid(WNOHANG)`.

  **main.cpp:** `logger.startCrashHandler()` nach `installCrashHandler()`. Heartbeat+State-Sync alle 2s im Main-Loop (`crashHandlerTimer`): `ensureCrashHandlerAlive()`, Heartbeat mit Timestamp, Engine-State (alle DiagnosticsManager-States), Projekt-Info (Name/Version/Pfad/Level), Frame-Metriken (FPS/CPU/GPU/Entity-Counts), Uptime. Hardware-Info (CPU/GPU/VRAM/RAM/Monitore) + Engine-Version nach Renderer-Init. `logger.stopCrashHandler()` vor `return 0`.

  **CMakeLists.txt (Root):** `${CMAKE_SOURCE_DIR}/CrashHandler` in `ENGINE_COMMON_INCLUDES`. **Logger/CMakeLists.txt:** `${CMAKE_SOURCE_DIR}/CrashHandler` als PRIVATE Include.

## Letzte Änderung (Level-Script HPK-Fallback & Renderer-State-Sync im Build)

- ✅ `Level-Script HPK-Fallback`: `LoadLevelScriptModule()` (PythonScripting.cpp) konnte Level-Skripte im Packaged Build nicht laden, da nur `std::ifstream` genutzt wurde – Dateien liegen aber nur im HPK. **Fix:** HPK-Fallback-Pattern aus `LoadScriptModule()` übernommen: Bei fehlgeschlagenem `ifstream` wird `HPKReader::makeVirtualPath()` → `readFile()` versucht. Level-Skripte funktionieren jetzt im Runtime-Build.

- ✅ `Renderer-State-Sync vor Build`: Renderer-Einstellungen (PostProcessing, Gamma, ToneMapping, Shadows, etc.) wurden nur in `DiagnosticsManager::m_states` gespeichert, wenn der User sie explizit in der UI umgeschaltet hat. Beim Build fehlten sie daher oft in `game.ini`, was zu falschen Rendering-Defaults im Runtime-Build führen konnte (zu hell). **Fix (main.cpp):** Vor dem Start des Build-Threads werden alle aktuellen Renderer-States (`isPostProcessingEnabled`, `isGammaCorrectionEnabled`, `isToneMappingEnabled`, etc.) explizit über `DiagnosticsManager::setState()` synchronisiert. `game.ini` enthält damit immer die korrekten Werte.

## Letzte Änderung (Config-Persistenz & HPK-Fallback im Packaged Build)

- ✅ `Engine-Settings via game.ini`: `loadConfig()` lief vor der `current_path()`-Korrektur im Runtime-Modus → suchte `config/config.ini` im falschen Verzeichnis → alle Renderer-Einstellungen verloren. **Fix (main.cpp, Build Step 7):** Alle Engine-States (PostProcessing, Gamma, ToneMapping, Shadows, AntiAliasing, etc.) werden jetzt zusätzlich in `game.ini` geschrieben. `game.ini` liegt immer neben der EXE und wird früh im Startup geparst. Neue `getStates()`-Methode in `DiagnosticsManager` liefert alle gespeicherten Engine-States. Unbekannte Keys in `game.ini` werden via `setState()` direkt in den DiagnosticsManager geladen → Renderer-Settings-Restoration-Block (Zeile 494+) findet sie per `getState()`. Zusätzlich: `loadConfig()` wird nach `current_path()`-Korrektur erneut aufgerufen (nur wenn `config/config.ini` tatsächlich existiert).

- ✅ `WorldGrid-Material im Build`: Das Landscape-Material `Materials/WorldGrid.asset` wurde im Build nicht mitkopiert, da es im Editor-Deploy-Verzeichnis (`current_path()/Content/`) erstellt wird, die Build-Pipeline aber nur aus `ENGINE_SOURCE_DIR/Content/` kopierte. **Fix (main.cpp, Build Step 5):** Zusätzliche Kopie von `current_path()/Content/` → Build-Output mit `skip_existing` (überschreibt keine gecookten Projekt-Assets).

- ✅ `Build-Pipeline: Config-Dateien kopieren`: Im Packaged Build fehlten sowohl die Engine-Config (`config/config.ini`) als auch die Projekt-Config (`Config/defaults.ini`). **Fix (main.cpp, Build Step 5):** `Config/defaults.ini` aus dem Projekt-Verzeichnis (in HPK gepackt) und `config/config.ini` aus dem Engine-Verzeichnis als lose Datei.

- ✅ `Runtime Renderer-Defaults erweitert`: Von 5 auf 12 Einstellungen erweitert. Neue Defaults: `FogEnabled=false`, `BloomEnabled=false`, `SsaoEnabled=false`, `OcclusionCullingEnabled=true`, `AntiAliasingMode=FXAA`, `WireframeEnabled=false`, `HeightFieldDebugEnabled=false`.

- ✅ `Projekt-Config HPK-Fallback`: Neue Methode `DiagnosticsManager::loadProjectConfigFromString()` für In-Memory-INI-Parsing. `AssetManager.cpp` liest `defaults.ini` aus HPK bei Fehlschlag. Vermeidet zirkuläre Abhängigkeit Diagnostics↔AssetManager.

## Letzte Änderung (Runtime-Initialisierung – Helligkeit, Kamera, Physik, Input)

- ✅ `Runtime Renderer-Defaults`: Im Packaged-Build-Modus existiert kein gespeicherter DiagnosticsManager-Config → Renderer-Einstellungen wie GammaCorrection, ToneMapping, PostProcessing blieben auf Default (oft OFF) → Szene viel zu hell/ausgewaschen. **Fix (main.cpp):** Nach dem Block zur Wiederherstellung gespeicherter Einstellungen wird jetzt im Runtime-Modus geprüft, ob Einstellungen vorhanden sind. Fehlen sie (kein gespeicherter State), werden sinnvolle Defaults gesetzt: `PostProcessingEnabled=true`, `GammaCorrectionEnabled=true`, `ToneMappingEnabled=true`, `ShadowsEnabled=true`, `CsmEnabled=true`.

- ✅ `Runtime Physik-Initialisierung`: `PhysicsWorld::Instance().initialize()` wurde im Runtime-Modus nie aufgerufen. Die Hauptschleife ruft `PhysicsWorld::Instance().step()` wenn PIE aktiv ist, aber ohne vorheriges `initialize()` passiert nichts. **Fix (main.cpp):** Nach erfolgreichem Level-Laden im Runtime-Modus wird jetzt `PhysicsWorld::Instance().initialize(PhysicsWorld::Backend::Jolt)` mit Standard-Gravity (-9.81), fixedTimestep (1/60) und sleepThreshold (0.05) aufgerufen.

- ✅ `Runtime Kamera-Übergabe`: Im Runtime-Modus wurde `renderer->setActiveCameraEntity()` nie aufgerufen → der Renderer nutzte die Editor-Kamera statt der Entity-Kamera mit CameraComponent. **Fix (main.cpp):** Nach Level-Laden im Runtime-Modus wird jetzt per ECS-Schema-Query (CameraComponent + TransformComponent) die erste aktive Kamera-Entity gesucht und via `renderer->setActiveCameraEntity()` aktiviert – identisch zum Editor-PIE-Flow.

- ✅ `Runtime Mouse-Capture & Input`: `pieMouseCaptured` war im Runtime-Modus `false` → die Kamera-Bewegungs-Gate-Bedingung `(inPIE && pieMouseCaptured && !pieInputPaused)` schlug fehl → kein WASD-Movement, keine Mausdrehung. **Fix (main.cpp):** Im Runtime-Startup wird jetzt `pieMouseCaptured=true`, `pieInputPaused=false` gesetzt und `SDL_SetWindowRelativeMouseMode`/`SDL_SetWindowMouseGrab`/`SDL_HideCursor` aufgerufen – spiegelt den Editor-PIE-Button-Flow.

## Letzte Änderung (Mesh- & Script-Loading HPK-Fallbacks)

- ✅ `Mesh-Loading HPK-Fallback`:

- ✅ `Script-Loading HPK-Fallback`: Python-Skripte können jetzt zur Laufzeit aus dem HPK-Archiv geladen werden. **Problem:** `LoadScriptModule()` nutzte ausschließlich `std::ifstream` zum Laden von `.py`-Dateien. Im Runtime-Modus liegen Skripte nur im HPK → `ifstream` scheitert. Da Skripte pro Entity pro Frame geladen werden, entstand endloser Retry-Spam mit Error-Logging. **Fix (PythonScripting.cpp):** `LoadScriptModule()` versucht jetzt bei fehlgeschlagenem `ifstream` den HPK-Fallback: `makeVirtualPath` → `readFile` → Buffer als Code-String für `PyRun_StringFlags`. Neues `loadFailed`-Flag in `ScriptState` verhindert Per-Frame-Retry-Spam: nach erstem Fehlschlag wird das Skript nicht erneut geladen.

## Letzte Änderung (Cooked-Asset-Binärformat – MessagePack statt JSON)

- ✅ `Cooked-Asset-Binärformat (MessagePack)`:

## Letzte Änderung (Runtime Asset-Registry & Level-Loading – HPK Fix)

- ✅ `Runtime Asset-Registry (kein Filesystem-Scan)`: Im Runtime-/Packaged-Build-Modus wird die Asset-Discovery (`discoverAssetsAndBuildRegistryAsync`) jetzt übersprungen, da das `Content/`-Verzeichnis nicht auf Disk existiert (alles in `content.hpk` gepackt). **Problem:** `loadProject()` rief immer `discoverAssetsAndBuildRegistryAsync(projectPath)` auf, das rekursiv `Content/` scannt – im Runtime-Modus existiert dieses Verzeichnis nicht, der Scan war sinnlos und produzierte leere Ergebnisse. **Fix (AssetManager.cpp):** `loadProject()` unterscheidet jetzt: Packaged Build (`isPackagedBuild == true`) → ruft `loadAssetRegistry(projectPath)` auf (liest die vorgefertigte `AssetRegistry.bin` aus HPK im ARRG-Binärformat) und setzt `setAssetRegistryReady(true)`. Editor-/Dev-Modus → weiterhin `discoverAssetsAndBuildRegistryAsync()` für Live-Discovery vom Dateisystem.

- ✅ `Level-Loading HPK-Fallback`: `loadLevelAsset()` kann Level-Dateien jetzt direkt aus dem HPK-Archiv laden. **Problem:** `loadLevelAsset()` nutzte ausschließlich `std::ifstream` zum Öffnen der Level-Datei. Im Runtime-Modus liegt die Datei aber nur im HPK-Archiv → `ifstream` schlägt fehl mit "Failed to open level asset file", ohne dass HPK-Info in die Konsole geloggt wird. Alle anderen `load*Asset`-Funktionen (Texture, Audio, Material, Object3D, Object2D, Skybox) gehen über `readAssetFromDisk()`, das bereits einen HPK-Fallback hat – nur `loadLevelAsset()` fehlte. **Fix (AssetManager.cpp):** Wenn `ifstream` fehlschlägt, prüft `loadLevelAsset()` jetzt ob ein HPK-Archiv gemountet ist. Falls ja: `makeVirtualPath()` → `readFile()` → `readAssetHeaderFromMemory()` + `readAssetJsonFromMemory()` für In-Memory-Parsing des Level-Assets. Detailliertes Logging: vpath, Dateigröße, oder Warnung wenn HPK nicht gemountet.

## Letzte Änderung (Runtime ohne .project-Datei – Packaged Build Support)

- ✅ `Runtime ohne .project-Datei`: Die gebaute Runtime benötigt keine `.project`-Datei mehr. **Problem:** `AssetManager::loadProject()` suchte zwingend nach einer `.project`-Datei im Verzeichnis und gab `false` zurück, wenn keine gefunden wurde. Die Build-Pipeline kopiert keine `.project`-Datei in das Output-Verzeichnis, da die Runtime alle benötigten Infos bereits aus `game.ini` (StartLevel, WindowTitle, BuildProfile) bzw. compile-time Defines erhält. **Fix (AssetManager.cpp):** `loadProject()` erkennt jetzt "Packaged Builds": Wenn keine `.project`-Datei im Verzeichnis gefunden wird, aber eine `content.hpk` existiert, wird dies als gepackter Runtime-Build behandelt. Ein minimales `ProjectInfo` wird automatisch erstellt (Name = Verzeichnisname, Path = Root-Verzeichnis, RHI = Unknown). Die Funktion fährt normal fort mit HPK-Mounting, Registry-Laden und Config-Laden. Die `.project`-Datei-Parsing-Logik bleibt für den Editor-Modus vollständig erhalten.

## Letzte Änderung (HPK Early-Mount & Runtime-Logging – Fix)

- ✅ `HPK Early-Mount Fix`: Behebt das Kernproblem, dass Shader im Runtime-Modus nicht geladen werden konnten. **Root Cause:** Der Renderer wird vor dem AssetManager initialisiert. `renderer->initialize()` lädt ParticleSystem-, PostProcess- und Text-Shader. Aber `AssetManager::loadProject()` (das HPK mounted) wird erst danach aufgerufen → `HPKReader::GetMounted()` gibt `nullptr` zurück → alle HPK-Fallbacks in den Shader-Ladern scheitern. **main.cpp:** Im Runtime-Modus wird das HPK-Archiv jetzt **direkt vor** `renderer->initialize()` gemountet (`earlyHpkReader`). Nach `loadProject()` wird der Early-Reader freigegeben, da der AssetManager seinen eigenen Reader erstellt und als globalen Singleton setzt. **HPK Runtime-Logging:** Alle HPK-Fallback-Stellen loggen jetzt detailliert: `OpenGLShader::loadFromFile/loadFromFileWithDefines` (Pfad, vpath, baseDir, Dateigröße), `ParticleSystem::readFile` (Pfad, vpath), `ResolveShaderPath` in OpenGLObject3D/2D (Dateiname, Archiv-Lookup-Ergebnis), `OpenGLTextRenderer::buildGlyphAtlas` (Font-Pfad, vpath, Dateigröße). Bei nicht gemountem HPK wird explizit gewarnt ("HPK not mounted when loading ...").

## Letzte Änderung (HPK Runtime-Pfadauflösung – Fix)

- ✅ `HPK Runtime-Pfadauflösung Fix`: Behebt das Problem, dass Shader und andere Dateien zur Laufzeit nicht aus dem HPK-Archiv geladen werden konnten. **Root Cause:** `std::filesystem::current_path()` war im Runtime-Modus nicht auf das Exe-Verzeichnis gesetzt. Alle Shader-/Content-Pfade im Engine-Code werden über `current_path() / "shaders" / ...` konstruiert. Das HPK-Archiv nutzt `m_baseDir` (= Exe-Verzeichnis via `SDL_GetBasePath()`) für `makeVirtualPath()` (`lexically_relative`). Wenn `current_path()` ≠ Exe-Verzeichnis, schlägt die Pfadauflösung fehl und HPK-Fallbacks können keine virtuellen Pfade ableiten. **main.cpp:** Im Runtime-Modus wird `std::filesystem::current_path(chosenPath)` direkt nach dem Setzen von `chosenPath = SDL_GetBasePath()` aufgerufen, sodass `current_path()` mit dem Exe-Verzeichnis und dem HPK-`m_baseDir` synchron ist. **HPKArchive.cpp:** `makeVirtualPath()` erhält einen Defense-in-Depth-Fallback: Wenn `lexically_relative` fehlschlägt (z.B. verschiedene Laufwerksbuchstaben), wird der Pfad nach bekannten Verzeichnispräfixen (`shaders/`, `Content/`, `Config/`) durchsucht und gegen das HPK-TOC geprüft. Damit funktioniert die Auflösung auch bei unerwarteten Pfad-Konfigurationen.

## Letzte Änderung (HPK-Containerformat – Phase 10.2 Erweiterung)

- ✅ `HPK-Containerformat (Phase 10.2 Erweiterung)`: Binäres Archivformat (.hpk – Horizon Package) zum Verpacken aller Game-Assets in eine einzige Datei, analog zu Unreal's .pak. **HPKArchive.h:** Format-Konstanten (shared Editor+Runtime): `HPK_MAGIC` (0x48504B31, 'HPK1'), `HPK_VERSION` (1). `HPKHeader` (32 Bytes: magic, version, flags, reserved[5]). `HPKFooter` (16 Bytes: tocOffset, tocCount, magic). `HPKReader`-Klasse (shared): `mount(hpkPath)`/`unmount()`, `readFile(virtualPath)` (thread-safe mit Mutex), `contains(virtualPath)`, `getFileInfo()`/`getFileList()`/`getFileCount()`, `makeVirtualPath(absolutePath)` (leitet virtuellen Pfad via `lexically_relative` ab), statischer Singleton `GetMounted()`/`SetMounted()` für cross-modul Zugriff. `HPKWriter`-Klasse (editor-only): `begin(outputPath)`, `addFile(virtualPath, data, size)` (16-Byte-Alignment), `addFileFromDisk(virtualPath, diskPath)`, `finalize()` (schreibt TOC + Footer). **HPKArchive.cpp:** Vollständige Reader-Implementierung: `mount()` liest Footer→Header→TOC, `readTOC()` parst TOC-Einträge (uint16 pathLen + String + uint64 offset + uint64 size + uint32 flags). Writer: sequenzielles Schreiben mit 16-Byte-Alignment, TOC am Ende, Footer als letztes. **AssetManager.h:** `mountContentArchive(hpkPath)`/`unmountContentArchive()`/`isContentArchiveMounted()` API. `m_hpkReader` unique_ptr Member. **AssetManager.cpp:** (1) `loadProject()` mounted automatisch `<projektRoot>/content.hpk` wenn vorhanden. (2) `loadAssetRegistry()` HPK-Fallback: wenn AssetRegistry.bin nicht auf Disk existiert, liest aus HPK und parst ARRG-Binärformat aus Memory-Buffer. (3) `readAssetFromDisk()` HPK-Fallback: WAV-Dateien via `SDL_LoadWAV_IO`+`SDL_IOFromMem`, Asset-Dateien via `readAssetHeaderFromMemory`+`readAssetJsonFromMemory`, Textur-Quellbilder via `stbi_load_from_memory`, Audio-Quellen via `SDL_LoadWAV_IO`. **OpenGLShader.cpp:** `loadFromFile()`/`loadFromFileWithDefines()` HPK-Fallback: wenn ifstream fehlschlägt, liest Shader-Source aus HPK-Archiv. **OpenGLObject3D.cpp:** `ResolveShaderPath()` HPK-Fallback: prüft `HPKReader::contains("shaders/"+filename)` wenn Datei nicht auf Disk. `FindCookedMeshPath()` HPK-Fallback: prüft HPK via `contains()` wenn .cooked nicht auf Disk. `LoadCookedMesh()` HPK-Fallback: liest CMSH-Header + Vertex-Daten + JSON-Blob aus HPK-Buffer via `memcpy`. **OpenGLObject2D.cpp:** `ResolveShaderPath()` mit gleichem HPK-Fallback. **ParticleSystem.cpp:** `readFile()` HPK-Fallback: wenn ifstream fehlschlägt, liest Shader-Source via `HPKReader::makeVirtualPath`+`readFile`. **OpenGLTextRenderer.cpp:** `buildGlyphAtlas()` HPK-Fallback: wenn `FT_New_Face` fehlschlägt, lädt Font-Datei aus HPK in Memory-Buffer und nutzt `FT_New_Memory_Face`. **main.cpp (Build-Pipeline):** `kTotalSteps` von 7 auf 8 erhöht. Neuer Step 6 „Packaging content into HPK": `HPKWriter` packt Content/, shaders/, Config/ Verzeichnisse aus dem Build-Output rekursiv in `content.hpk`, entfernt danach die losen Verzeichnisse. Step 7: game.ini. Step 8: Done. **CMakeLists.txt:** HPKArchive.h/cpp zu ASSET_MANAGER_SOURCES hinzugefügt.

## Letzte Änderung (Build-Konfigurationsprofile – Phase 10.3)

- ✅ `Build-Konfigurationsprofile (Phase 10.3)`: Vollständiges Build-Profil-System für die Game-Build-Pipeline. **UIManager.h:** Neues `BuildProfile`-Struct (name, cmakeBuildType, logLevel, enableHotReload, enableValidation, enableProfiler, compressAssets). 3 Methoden: `loadBuildProfiles()`, `saveBuildProfile()`, `deleteBuildProfile()`. `BuildGameConfig` erweitert um `binaryDir` (Binary-Cache-Pfad) und `profile` (aktives BuildProfile). `m_buildProfiles`-Member. **UIManager.cpp:** `loadBuildProfiles()` lädt JSON-Profile aus `<Projekt>/Config/BuildProfiles/*.json`. Erstellt automatisch 3 Standard-Profile (Debug: CMake Debug/verbose/HotReload+Validation+Profiler, Development: RelWithDebInfo/info/HotReload+Profiler, Shipping: Release/error/kein HotReload/kein Profiler/Asset-Compression). `saveBuildProfile()` serialisiert als JSON mit `nlohmann::json`. `deleteBuildProfile()` entfernt Datei und Vector-Eintrag. `openBuildGameDialog()` überarbeitet: Neues Profil-Dropdown (zeigt alle Profile), Profil-Info-Zeile (CMake-Typ/LogLevel/HotReload/Profiler), Output-Dir und Binary-Cache als read-only Info-Felder (`<Projekt>/Build` und `<Projekt>/Binary` standardisiert), Browse-Button entfernt. **main.cpp:** Build-Thread loggt Profil-Name und CMake-BuildType. `kTotalSteps` von 6 auf 7 erhöht. `buildDir` nutzt `config.binaryDir` (`<Projekt>/Binary`) statt `_build` im Output-Dir – Binary-Dateien werden persistent gecacht für inkrementelle Builds. CMake `--config` nutzt `config.profile.cmakeBuildType` statt hardcoded `Release`. Deploy-Schritt nutzt profilabhängigen Build-Typ-Ordner. Neuer Step 6: `game.ini`-Generierung mit Profil-Einstellungen (StartLevel, WindowTitle, BuildProfile, LogLevel, EnableHotReload, EnableValidation, EnableProfiler). Step 7: Done.

## Letzte Änderung (Build-Output-Scroll-Fix & Build-Abbruch-Button)

- ✅ `Build-Output-Scroll-Fix`: Das Build-Output scrollt jetzt korrekt wie der Console-Tab. **UIManager.cpp:** `showBuildProgress()` erstellt das scrollbare Output-Panel (`BP.OutputScroll`) ohne vordefinierten Text-Child – Zeilen werden dynamisch als individuelle `WidgetElement`-Rows hinzugefügt (gleiche Architektur wie `Console.LogArea`). `pollBuildThread()` baut bei neuen Zeilen alle `BP.Row.*`-Children im ScrollPanel neu auf, jede Zeile als eigenständiges Text-Element mit fester Höhe (`fontSizeSmall`, 16px Zeilenhöhe). Auto-Scroll via `scrollOffset = 999999.0f` am Ende jedes Updates.

- ✅ `Build-Abbruch-Button`: Neuer „Abort Build"-Button im Build-Popup ermöglicht den Abbruch eines laufenden Builds. **UIManager.h:** Neues `std::atomic<bool> m_buildCancelRequested` im Build-Thread-State. **UIManager.cpp:** `showBuildProgress()` erstellt einen roten „Abort Build"-Button (`BP.AbortBtn`) zwischen Output-Panel und Close-Button. `closeBuildProgress()` blendet den Abort-Button aus und den Close-Button ein. **main.cpp:** `checkCancelled()`-Lambda prüft `m_buildCancelRequested` und setzt `ok=false` + Fehlermeldung. Wird zwischen allen Build-Steps aufgerufen. `runCmdWithOutput()` prüft das Flag in der Lese-Schleife und terminiert den Prozess via `TerminateProcess()` bei Abbruch.

## Letzte Änderung (Build-System-Verbesserungen: Eigenes OS-Fenster, Konsole versteckt, Python-Deploy, Runtime-Splash-Skip)

- ✅ `Build-Output als eigenes OS-Fenster`: Das Build-Output wird jetzt in einem separaten OS-Fenster (PopupWindow) angezeigt statt als Overlay im Hauptfenster. **UIManager.cpp:** `showBuildProgress()` erstellt via `m_renderer->openPopupWindow("BuildOutput", ...)` ein echtes SDL-Fenster mit eigenem UIManager und Render-Context. Widget wird auf `m_buildPopup->uiManager()` registriert statt auf dem Haupt-UIManager. `dismissBuildProgress()` schließt das Popup-Fenster via `m_renderer->closePopupWindow("BuildOutput")`, setzt `m_buildPopup = nullptr` und gibt das Widget frei. `pollBuildThread()` markiert das Popup-UIManager als dirty für korrekte Neuzeichnung. Output-Panel nutzt `fillY = true` statt fester `maxSize` – scrollt jetzt korrekt mit dem gesamten verfügbaren Platz.

- ✅ `Konsolen-Fenster versteckt`: Während des Build-Vorgangs wird kein sichtbares Konsolenfenster mehr geöffnet. **main.cpp:** `runCmdWithOutput()` nutzt auf Windows jetzt `CreateProcess()` mit `CREATE_NO_WINDOW`-Flag + `STARTF_USESTDHANDLES` + Pipe-Redirection statt `_popen()`. stdout+stderr werden über anonyme Pipes zeilenweise gelesen. Auf Linux/Mac bleibt `popen()` erhalten.

- ✅ `Python-Runtime im Build`: Die Python-DLL und -ZIP-Datei werden automatisch in das Build-Ausgabeverzeichnis kopiert. **main.cpp:** Nach dem DLL-Deploy (Step 4) wird in mehreren Verzeichnissen (Build-Dir, SDL_GetBasePath(), Engine-Source-Dir) nach `python*.dll` und `python*.zip` gesucht und ins Output-Verzeichnis kopiert.

- ✅ `Runtime-Splash-Skip`: Das gebaute Spiel überspringt den Splash-Screen und lädt direkt das Start-Level im Play-Modus. **main.cpp:** `useSplash` wird nach der Runtime-Mode-Erkennung auf `false` gesetzt wenn `isRuntimeMode == true`. Die Runtime nutzt weiterhin `setPIEActive(true)` für vollständige Script- und Physics-Ausführung. Beendigung via Alt+F4 (Standard-SDL-Fenster-Close).

## Letzte Änderung (Build-Fenster als persistentes Popup)

- ✅ `Build-Fenster Popup`: Das Build-Fortschritts-Fenster ist jetzt ein persistentes Popup, das nach dem Build-Abschluss nicht automatisch verschwindet. Nach Abschluss (Erfolg oder Fehler) werden Titel, Status und Ergebnis-Text aktualisiert, Fortschrittsbalken und Zähler ausgeblendet, und ein „Close"-Button eingeblendet, über den das Popup manuell geschlossen werden kann. **UIManager.h:** Neue Methode `dismissBuildProgress()` zum manuellen Schließen des Build-Popups. **UIManager.cpp:** `closeBuildProgress()` entfernt das Widget nicht mehr, sondern aktualisiert die UI: Titel wird zu „Build Completed"/„Build Failed", Ergebnis-Text (`BP.Result`) wird farbig angezeigt (grün/rot), Close-Button (`BP.CloseBtn`) wird sichtbar. `dismissBuildProgress()` ruft `unregisterWidget("BuildProgress")` auf. `showBuildProgress()` erstellt zusätzlich einen zunächst versteckten Ergebnis-Text und Close-Button.

## Letzte Änderung (Asynchroner Build-Thread & CMake-Output-Log)

- ✅ `Build-Thread & Output-Log`: Build Game läuft jetzt in einem eigenen `std::thread` – die Editor-UI bleibt während des gesamten Build-Vorgangs vollständig responsiv. CMake-Output (stdout+stderr) wird zeilenweise über `_popen()`/`popen()` erfasst und in Echtzeit in einem scrollbaren Log-Panel im Build-Progress-Dialog angezeigt. **UIManager.h:** Neue Methoden `appendBuildOutput()` (thread-safe, schreibt via Mutex in `m_buildPendingLines`), `pollBuildThread()` (main-thread, verarbeitet ausstehende Zeilen/Steps/Finish), `isBuildRunning()`. Neuer öffentlicher Build-Thread-State: `m_buildThread`, `m_buildRunning` (atomic), `m_buildMutex`, `m_buildPendingLines/Status/Step/Finished/Success/ErrorMsg`. **UIManager.cpp:** `showBuildProgress()` erstellt jetzt ein scrollbares Output-Panel (`BP.OutputScroll` + `BP.OutputText`), Build-Progress-Dialog vergrößert. `pollBuildThread()` überträgt Zeilen in `m_buildOutputLines`, aktualisiert den Text, auto-scrollt zum Ende, verarbeitet Step-Updates und Build-Abschluss. Destruktor joined den Build-Thread. **main.cpp:** Build-Pipeline komplett in `std::thread`-Lambda verschoben. `std::system()` durch `_popen()`/`pclose()` + `2>&1`-Redirect ersetzt – jede Zeile wird via `appendBuildOutput()` an den main thread gepusht. `advanceStep()` setzt mutex-geschützte pending-Felder statt direkt UI zu manipulieren. `pollBuildThread()` wird einmal pro Frame am Anfang der Main-Loop aufgerufen. Kein `renderer->render()/present()` mehr aus dem Build-Callback. **CMakeLists.txt:** `CMAKE_GENERATOR` als Compile-Define an HorizonEngine übergeben, damit der Build den gleichen Generator nutzt. Überflüssiges `-DENGINE_EDITOR=OFF` entfernt.

## Letzte Änderung (Phase 8.3 – Keyboard-Navigation)

- ✅ `Keyboard-Navigation (Phase 8.3)`: Grundlegende Keyboard-Navigation im Editor implementiert. **UIManager.cpp `handleKeyDown()`:** (1) **Escape-Kaskade:** Schließt zuerst Dropdown (`closeDropdownMenu()`), dann Modal (`closeModalMessage()`), dann Entry-Fokus (`setFocusedEntry(nullptr)`) + Rename-Abbruch. (2) **Tab/Shift+Tab:** Zykelt durch alle sichtbaren EntryBar-Elemente (`cycleFocusedEntry()`). Sammelt per `collectFocusableEntries()` alle EntryBars in aktiven Widgets, findet den aktuellen Fokus-Index und springt zum nächsten/vorherigen. (3) **Pfeiltasten Outliner:** `navigateOutlinerByArrow()` – Holt alle Entities via ECS Schema, findet aktuelle Selektion, springt um +1/-1, ruft `populateOutlinerDetails()` auf. Wrap-around am Anfang/Ende. (4) **Pfeiltasten Content Browser:** `navigateContentBrowserByArrow()` – Sammelt GridAsset-Tile-IDs, berechnet Grid-Spalten aus `ContentBrowser.Grid` Panel-Breite, navigiert per dCol+dRow*cols. **UIManager.h:** Neue Methoden `collectFocusableEntries()`, `cycleFocusedEntry()`, `navigateOutlinerByArrow()`, `navigateContentBrowserByArrow()`. **OpenGLRenderer.cpp:** Fokussierte EntryBars erhalten einen blauen Outline-Highlight (`drawUIOutline` mit `{0.25, 0.55, 0.95, 0.8}`) in beiden Rendering-Pfaden (Legacy + New).

## Letzte Änderung (Build Game – CMake-basierte Kompilierung & CMake-Detection)

- ✅ `Build Game CMake-Rework`: Build Game kompiliert jetzt die Engine neu mit eingebackenen Projektdaten statt nur Dateien zu kopieren. **CMakeLists.txt:** `ENGINE_SOURCE_DIR`-Define für den Editor (zeigt auf das Engine-Source-Verzeichnis). `GAME_START_LEVEL`/`GAME_WINDOW_TITLE`-Optionen – wenn gesetzt, werden sie als Compile-Defines in `HorizonEngineRuntime` eingebacken. Runtime-Exe wird nach `Tools/` verschoben statt ins Root-Deploy-Verzeichnis. **main.cpp:** Runtime-Modus nutzt jetzt zuerst compile-time `GAME_START_LEVEL` (via `#if defined(GAME_START_LEVEL)`), fällt auf `game.ini` zurück. Build-Pipeline: (1) Output-Dir erstellen, (2) CMake configure mit `-DGAME_START_LEVEL` und `-DGAME_WINDOW_TITLE`, (3) CMake build `--target HorizonEngineRuntime --config Release`, (4) Deploy (exe umbenannt zu `<WindowTitle>.exe` + DLLs), (5) Content/Shaders/Registry kopieren, (6) Build-Intermediates aufräumen. Kein `game.ini` mehr nötig. CMake-Detection beim Editor-Start: `detectCMake()` prüft bundled `Tools/cmake/bin/`, System-PATH, und Standard-Installationsorte. Popup `showCMakeInstallPrompt()` bei fehlendem CMake mit Link zur Download-Seite. **UIManager.h:** `detectCMake()`/`isCMakeAvailable()`/`getCMakePath()`/`showCMakeInstallPrompt()` API. `m_cmakeAvailable`/`m_cmakePath` State. **UIManager.cpp:** `detectCMake()` Implementierung (3-stufige Suche), `showCMakeInstallPrompt()` via `showConfirmDialog()` mit Browser-Öffnung.

## Letzte Änderung (Build-Game-Crash-Fix – Popup-Lifetime-Schutz)

- ✅ `Build-Game-Crash-Fix`: Access Violation (0xC0000005) behoben, der nach dem Build-Vorgang auftrat. **Ursache:** Im Build-Button-Callback wurde `popup->close()` **vor** dem Build-Pipeline-Aufruf (`m_onBuildGame`) ausgeführt. Die Build-Pipeline ruft `renderer->render()` innerhalb von `advanceStep()` auf, und `renderPopupWindows()` zerstört Popups mit `isOpen()==false` sofort (destroy + erase). Da der Code noch im `handleMouseDown()` des Popup-UIManagers lief, war `this` nach der Zerstörung ungültig → Crash bei `m_lastClickedElementId = targetId`. **Fix:** `popup->close()` wird jetzt **nach** dem `m_onBuildGame`-Callback aufgerufen, sodass das Popup während der verschachtelten Render-Aufrufe nicht zerstört wird. Betroffene Datei: `UIManager.cpp` (Build-Button-Event in `openBuildGameDialog`).

## Letzte Änderung (Crash-Safe Logging & Build-Pipeline-Absicherung)

- ✅ `Crash-Safe Logging`: Logger flusht nach jedem Schreibvorgang (`logFile.flush()` + `std::endl`). Neue `flush()` Public-Methode. `installCrashHandler()`: Windows SEH (`SetUnhandledExceptionFilter`) mit Stack-Trace via `DbgHelp` (StackWalk64, SymFromAddr, SymGetLineFromAddr64, bis 32 Frames), Linux `std::signal`-Handler (SIGSEGV/SIGABRT/SIGFPE/SIGILL), plus `std::set_terminate`. Crash-Handler loggt Exception-Code, Adresse, Typ-Beschreibung und Stack-Trace. Build-Pipeline in `try/catch` gewrappt – Exceptions werden abgefangen und als Fehlermeldung angezeigt. `advanceStep()` loggt jeden Build-Schritt und fängt Render-Exceptions separat ab. Betroffene Dateien: `Logger.h`, `Logger.cpp`, `main.cpp`.

## Letzte Änderung (Runtime Auto-Resolution & Fullscreen)

- ✅ `Runtime Auto-Resolution & Fullscreen`: Das Runtime-Fenster erkennt beim Start automatisch die native Display-Auflösung via `SDL_GetCurrentDisplayMode()` und startet immer im Fullscreen-Modus. Manuelle Resolution/Fullscreen-Konfiguration entfernt aus: `BuildGameConfig`-Struct (`windowWidth`/`windowHeight`/`fullscreen` entfernt), Build-Dialog (Resolution-Eingabefelder und Fullscreen-Checkbox entfernt), `game.ini`-Generierung (`WindowWidth`/`WindowHeight`/`Fullscreen`-Einträge entfernt), Runtime-Parsing (`WindowWidth`/`WindowHeight`/`Fullscreen`-Keys werden nicht mehr gelesen). Betroffene Dateien: `main.cpp`, `UIManager.h`, `UIManager.cpp`.

## Letzte Änderung (Standalone Game Build – Phase 10.1 Neuimplementierung)

- ✅ `Standalone Game Build (Phase 10.1)`: Neuimplementierung auf Basis von Phase 12.1 (Editor-Separation). **UIManager.h:** `BuildGameConfig`-Struct (startLevel, windowTitle, outputDir, launchAfterBuild). `BuildGameCallback`-Typedef. `openBuildGameDialog()`/`showBuildProgress()`/`updateBuildProgress()`/`closeBuildProgress()` API. `setOnBuildGame()`-Callback-Setter. Private Members: `m_onBuildGame`, `m_buildProgressWidget`. **UIManager.cpp:** `openBuildGameDialog()` – PopupWindow mit Formular: Start Level (DropDown aus AssetRegistry), Window Title (Projektname), Launch After Build-Checkbox, Output Dir (Projekt/Build default) mit Browse-Button (`SDL_ShowOpenFolderDialog`). Build- und Cancel-Buttons. Liest Formular-Werte und ruft `m_onBuildGame`-Callback auf. `showBuildProgress()` – Modal-Overlay mit StackPanel: Titel, Status-Text, Schritt-Zähler, ProgressBar (Accent-Farbe). `updateBuildProgress(status, step, totalSteps)` – Aktualisiert Status/Counter/Bar. `closeBuildProgress(success, message)` – Entfernt Widget, zeigt Toast (Success/Error). **main.cpp:** "Build Game..." im Settings-Dropdown (innerhalb `#if ENGINE_EDITOR`). `setOnBuildGame()`-Callback mit 6-Schritt-Pipeline: (1) Output-Verzeichnis erstellen via `std::filesystem::create_directories`, (2) HorizonEngineRuntime.exe + alle .dll aus SDL_GetBasePath() kopieren, (3) Content-Ordner rekursiv kopieren, (4) AssetRegistry.bin in Config/ kopieren, (5) game.ini generieren (StartLevel, WindowTitle, BuildProfile=Development, LogLevel=info, EnableHotReload/Validation/Profiler=false), (6) Optionaler Launch via `ShellExecuteA` (Win32) oder `std::system` (Posix). Runtime erkennt native Display-Auflösung automatisch via `SDL_GetCurrentDisplayMode()` und startet immer Fullscreen. `advanceStep()`-Helfer mit `renderer->render()`/`present()` für Echtzeit-Fortschrittsanzeige. Fehlerbehandlung mit Abbruch bei kritischen Fehlern, Warn-Logs bei nicht-kritischen.

## Letzte Änderung (Runtime-Minimierung – Phase 12.1 Erweiterung)

- ✅ `Runtime-Minimierung (Phase 12.1 Erweiterung)`: Assimp und alle Editor-Abstraktionen komplett aus dem Runtime-Game-Build entfernt. **Dual-Target-Pattern auf alle Kernbibliotheken erweitert:** `AssetManager` → `AssetManagerRuntime` (SHARED, ENGINE_EDITOR=0, **kein assimp**, ~3.3 MB statt ~21.7 MB). `Scripting` → `ScriptingRuntime` (SHARED, ENGINE_EDITOR=0, linkt RendererRuntime + AssetManagerRuntime). `Landscape` → `LandscapeRuntime` (STATIC, ENGINE_EDITOR=0, linkt AssetManagerRuntime). **RendererCoreRuntime** linkt jetzt AssetManagerRuntime + LandscapeRuntime statt Editor-Varianten. **Root CMakeLists.txt:** `ENGINE_COMMON_LIBS` aufgespalten in `ENGINE_EDITOR_LIBS` (AssetManager, Scripting, Landscape) und `ENGINE_RUNTIME_LIBS` (AssetManagerRuntime, ScriptingRuntime, LandscapeRuntime). **Quellcode-Guards (ENGINE_EDITOR):** AssetManager.h/cpp – 10+ Guard-Blöcke: Editor-Includes (SDL_dialog, assimp, ECS, EditorTheme), Import-System, Save/Create/Delete/Rename/Move-APIs, Entity-Referenz-Validierung, Editor-Widgets, Registry-Speicherung. PythonScripting.h/cpp – Editor-Plugin-System komplett guardiert (LoadEditorPlugins, PollPluginHotReload, EditorModule, EditorMethods, engine.editor Submodul, py_save_asset). RenderResourceManager.cpp – repairEntityReferences/validateEntityReferences guardiert. main.cpp – createProject, importAssetFromPath (OS-File-Drop), PollPluginHotReload guardiert. **Ergebnis:** Runtime-Build enthält kein assimp, keine Import/Export-Funktionalität, keine Editor-UI-Abstraktionen, keine Plugin-System-Funktionen. Beide Targets kompilieren und linken fehlerfrei.

## Letzte Änderung (Engine-Editor-API-Layer – Phase 12.1)

- ✅ `Engine-Editor-API-Layer (Phase 12.1)`: Vollständige Editor/Engine-Separation über `#if ENGINE_EDITOR`-Präprozessor-Guards in allen Kernquellen. **CMakeLists.txt (3 Dateien):** Duales OBJECT-Library-System: `RendererCore` (ENGINE_EDITOR=1, alle Quellen) und `RendererCoreRuntime` (ENGINE_EDITOR=0, nur Common-Quellen) als CMake OBJECT-Libs. `Renderer` (SHARED, linkt RendererCore) und `RendererRuntime` (SHARED, linkt RendererCoreRuntime) als finale DLLs. Getrennte `ENGINE_COMMON_SOURCES`-Liste in `src/Renderer/CMakeLists.txt` und `src/Renderer/OpenGLRenderer/CMakeLists.txt` – Editor-only Dateien (EditorUIBuilder, WidgetDetailSchema, EditorWindows) nur in RendererCore. **UIManager.h:** Ehemals monolithischer `#if ENGINE_EDITOR`-Block (Zeilen 319–808) in ~6 gezielte Guard-Blöcke aufgespalten. Runtime-benötigte Members aus dem Guard herausgelöst: `bindClickEventsForWidget/Element`, Double-Click/Hover/Tooltip-State, Drag-Public-API (`isDragging`/`getDragPayload`/`getDragSourceId`/`cancelDrag` mit eigenem `public:`-Specifier), Drag-Member-State + `m_sliderDragElementId`, Drop-Callback-Members (vollständige `std::function`-Typen statt Editor-only Type-Aliases). `handleMouseMotionForPan`-Deklaration entfernt (in `handleMouseMotion` inlined). **UIManager.cpp:** `#endif` vor `handleRightMouseDown` (~Zeile 10240) eingefügt, `#if ENGINE_EDITOR` vor `addElementToEditedWidget` wiedereröffnet. `handleRightMouseDown`/`handleRightMouseUp` mit internen `#if ENGINE_EDITOR`/`#else`-Stubs (geben `false` zurück im Runtime). `handleMouseMotion`: Pan-Logik aus entfernter `handleMouseMotionForPan` inline mit `#if ENGINE_EDITOR`-Guard, Slider-Drag-Logik bleibt unguarded (Runtime). Inline-Guards für: Notification-History in `showToastMessage`, Dropdown-Dismissal in `handleMouseDown`, Outliner-Entity-Selektion in `handleMouseDown`. `bindClickEventsForWidget/Element` aus ENGINE_EDITOR-Block herausbewegt. `EditorUIBuilder::makeButton` in Modal-Dialog durch Inline-Button-Konstruktion ersetzt (nicht verfügbar im Runtime). **OpenGLRenderer.cpp:** Zusätzliche Guards für: `getSelectedEntity()`-Sync, `selectEntity()` nach Pick-Resolution, `isSequencerOpen()`-Spline-Rendering, `isUIRenderingPaused()` (2 Stellen), Widget-Editor-Canvas-Clip (`getWidgetEditorCanvasRect`/`isWidgetEditorContentWidget`), Widget-Editor-FBO-Preview-Block. **main.cpp:** Guards für: `pollBuildThread()`, `routeEventToPopup()`, `getTextureViewer()` (4 Stellen: Laptop-Panning, Right-Click-Panning, Zoom, Pan-Drag), Content-Browser-Rechtsklick-Kontextmenü (~700 Zeilen). **Ergebnis:** Beide Targets (`HorizonEngine` + `HorizonEngineRuntime`) kompilieren und linken fehlerfrei. Inkrementelle Builds bleiben möglich (Build-Intermediates werden nicht gelöscht).

## Letzte Änderung (Level-Streaming-UI – Phase 11.4)

- ✅ `Level-Streaming-UI (Phase 11.4)`: Sub-Level-System mit Streaming Volumes und Level-Composition-Panel. **Renderer.h:** `SubLevelEntry`-Struct (name, levelPath, loaded, visible, color als Vec4) und `StreamingVolume`-Struct (center Vec3, halfExtents Vec3, subLevelIndex). Management-API: `addSubLevel(name, path)` mit automatischer Farbpalette (6 Farben zyklisch), `removeSubLevel(index)` mit kaskadirender Streaming-Volume-Entfernung, `setSubLevelLoaded/setSubLevelVisible`, `addStreamingVolume/removeStreamingVolume`, `updateLevelStreaming(cameraPos)` für kamerabasiertes Auto-Load/Unload (AABB-Test). `m_streamingVolumesVisible`-Toggle. Protected: `m_subLevels`/`m_streamingVolumes`-Vektoren. **UIManager.h:** `LevelCompositionState`-Struct (tabId, widgetId, isOpen, refreshTimer, selectedSubLevel). `openLevelCompositionTab()`/`closeLevelCompositionTab()`/`isLevelCompositionOpen()`/`refreshLevelCompositionPanel()`. Builder-Methoden: `buildLevelCompositionToolbar`, `buildLevelCompositionSubLevelList`, `buildLevelCompositionVolumeList`. **UIManager.cpp:** Toolbar mit Titel, + Sub-Level, - Remove, + Volume, Volumes ON/OFF-Toggle. Sub-Level-Liste: Farb-Indikator, Name, [Loaded]/[Unloaded]-Status (grün/grau), Load/Unload-Button, Vis/Hid-Toggle, Zeilen-Selektion mit Accent-Highlight. Streaming-Volume-Liste: Farb-Indikator (von verlinktem Sub-Level), Label mit Index/Name/Position/Größe, X-Remove-Button. Alle mit dynamischen Click-Events und Auto-Refresh. **OpenGLRenderer.h/cpp:** `renderStreamingVolumeDebug(view, projection)` – Wireframe-Box (12 Kanten, 24 Vertices, GL_LINES) pro StreamingVolume, Farbe aus verlinktem SubLevelEntry, via Gizmo-Shader, Depth-Test deaktiviert. Aufgerufen in `renderWorld()` wenn `m_streamingVolumesVisible` und nicht in PIE. **main.cpp:** "Level Composition"-Eintrag im Settings-Dropdown, `updateLevelStreaming(getCameraPosition())` im Main-Loop für Frame-basiertes Auto-Streaming.

## Letzte Änderung (Editor-Plugin-System – Phase 11.3)

- ✅ `Editor-Plugin-System (Phase 11.3)`: Python-basiertes Plugin-System mit `engine.editor`-Submodul für Editor-Interaktion. **PythonScripting.cpp:** `EditorMethods[]` PyMethodDef-Array mit 8 Funktionen: `show_toast(message, level)` (Info/Success/Warning/Error via DiagnosticsManager::NotificationLevel), `get_selected_entities()` (Entity-IDs aus `Renderer::getSelectedEntities()`), `get_asset_list(type_filter)` (Asset-Registry als Dict-Liste), `create_entity(name)` (Entity mit NameComponent + TransformComponent), `select_entity(entity_id)` (UIManager::selectEntity), `add_menu_item(menu, name, callback)` (registriert Python-Callback), `register_tab(name, on_build_ui)` (registriert Tab-Builder-Callback), `get_menu_items()` (alle registrierten Items). `EditorModule` PyModuleDef + `AddSubmodule(module, editorModule, "editor")`. Toast-Konstanten: `TOAST_INFO=0`/`TOAST_SUCCESS=1`/`TOAST_WARNING=2`/`TOAST_ERROR=3`. **Plugin-Discovery:** `LoadEditorPlugins(projectRoot)` scannt `Editor/Plugins/*.py`, führt jede Datei via `PyRun_String()` in eigenem globals-Dict mit `__file__`/`__builtins__` aus. Fehler werden via `PyErr_Print()` + Logger gemeldet. Toast bei erfolgreichem Laden. **Hot-Reload:** Eigene `ScriptHotReload`-Instanz (`s_pluginHotReload`) überwacht Plugin-Verzeichnis (500ms Polling). Bei Änderung werden alle Plugin-Callbacks freigegeben und alle Plugins neu ausgeführt. **PythonScripting.h:** Neue öffentliche API: `LoadEditorPlugins()`, `PollPluginHotReload()`, `GetPluginMenuItems()`, `GetPluginTabs()`, `InvokePluginMenuCallback(index)`. Header-Structs `PluginMenuItem`/`PluginTab` für externe Konsumenten. **main.cpp:** `LoadEditorPlugins()` nach Script-Hot-Reload-Init (Phase 2d), `PollPluginHotReload()` im Main-Loop neben `PollScriptHotReload()`. Plugin-Menüeinträge mit `[Plugin]`-Prefix im Settings-Dropdown. **engine.pyi:** Vollständige `editor`-Klasse mit allen Methoden, Konstanten und Docstrings.

## Letzte Änderung (Build-Dialog entfernt – Phase 10 pausiert) → Neu implementiert (siehe oben)

- ✅ `Build-Dialog & Build-Pipeline neu implementiert (Phase 10.1)`: Ursprünglich entfernt wegen Rendering-Fehlern. Nach Abschluss von Phase 12.1 (Editor-Separation mit zwei CMake-Targets) vollständig neu implementiert. Siehe "Standalone Game Build – Phase 10.1 Neuimplementierung" oben.

## Letzte Änderung (Cinematic Sequencer UI – Phase 11.2)

- ✅ `Cinematic Sequencer UI (Phase 11.2)`: Dedizierter Sequencer-Tab für visuelles Kamera-Pfad-Editing. **Renderer.h:** 6 neue virtuelle Accessoren (`getCameraPathPoints`/`setCameraPathPoints`, `getCameraPathDuration`/`setCameraPathDuration`, `getCameraPathLoop`/`setCameraPathLoop`). **OpenGLRenderer:** Override-Implementierungen für alle 6 Accessoren auf `m_cameraPath`. **UIManager.h:** `SequencerState`-Struct (playing, scrubberT, selectedKeyframe, showSplineInViewport, loopPlayback, pathDuration). `openSequencerTab`/`closeSequencerTab`/`isSequencerOpen` API. **UIManager.cpp:** Sequencer-Tab mit Toolbar (Add/Remove Keyframe, Play/Pause/Stop/Loop, Spline-Toggle, Duration), Timeline-Bar (Keyframe-Marker mit Selected-Highlight, Scrubber bei Playback), scrollbare Keyframe-Liste (Index, Position, Rotation). Add Keyframe fügt aktuelle Kamera-Position ein. Klick auf Keyframe → Smooth-Camera-Transition. 0.1s Auto-Refresh während Playback. **OpenGLRenderer.cpp renderWorld():** 3D-Spline-Visualisierung (100-Segment Catmull-Rom Polyline orange GL_LINES + gelbe GL_POINTS für Kontrollpunkte via m_gizmoProgram). **main.cpp:** "Sequencer"-Eintrag im Settings-Dropdown.

## Letzte Änderung (Multi-Viewport-Layout – Phase 11.1)

- ✅ `Multi-Viewport-Layout (Phase 11.1)`: Viewport kann in Single/Two Horizontal/Two Vertical/Quad aufgeteilt werden. **`ViewportLayout`-Enum** und **`SubViewportPreset`-Enum** (Perspective/Top/Front/Right) in `Renderer.h`. **`SubViewportCamera`-Struct** mit Position, Yaw, Pitch und Preset. Virtuelle API: `setViewportLayout`, `getViewportLayout`, `getActiveSubViewport`, `setActiveSubViewport`, `getSubViewportCount`, `get/setSubViewportCamera`, `subViewportHitTest`. **OpenGLRenderer:** `kMaxSubViewports=4`, `m_subViewportCameras[4]` mit Preset-Initialisierung (Perspective synced von Editor-Kamera, Top -Y, Front -Z, Right -X). `computeSubViewportRects()` berechnet Pixel-Rects mit 2px Gap. **render():** Multi-Viewport-Schleife ruft `renderWorld()` pro Sub-Viewport auf. **renderWorld():** glViewport/glScissor pro Sub-Viewport, FBO-Bindung nur beim ersten, Kamera-View-Matrix aus Sub-Viewport-Kamera für Index > 0, Gizmo/Selektion/Rubber-Band nur im aktiven Sub-Viewport, blauer Rahmen-Highlight + Preset-Label (drawText). **moveCamera/rotateCamera:** Routing an aktiven Sub-Viewport-Kamera wenn Index > 0. **AssetManager.cpp:** Layout-Button (▣) in ViewportOverlay-Toolbar. **main.cpp:** Layout-Dropdown mit 4 Optionen, Sub-Viewport-Auswahl per Linksklick.

## Letzte Änderung (Build-Konfigurationsprofile – Phase 10.3 & Build-Dialog-Verbesserungen) ⏸️ ENTFERNT

- ⏸️ `Build-Konfigurationsprofile (Phase 10.3)`: ~~Drei Build-Profile (Debug/Development/Shipping) über Dropdown im Build-Dialog auswählbar.~~ **ENTFERNT** – wird nach Editor-Separation-Rework (Phase 12) neu implementiert.

- ⏸️ `Build-Dialog-Verbesserungen`: ~~Hintergrund-Fix, Progress-Bar, Python-Runtime-Kopie.~~ **ENTFERNT** – Build-Dialog hatte Rendering-Fehler (weißer Hintergrund, nicht-klickbare Controls).

## Letzte Änderung (Asset-Cooking-Pipeline – Phase 10.2) ✅ Neu implementiert

- ✅ `Asset-Cooking-Pipeline (Phase 10.2)`: Vollständige Neuimplementierung nach Editor-Separation-Rework. **AssetCooker.h:** CMSH-Binärformat-Definitionen (shared zwischen Editor und Runtime): `CMSH_MAGIC` (0x434D5348), `CMSH_VERSION` (1), `CmshFlags` (HAS_BONES, HAS_NORMALS), `CookedMeshHeader` (80 Bytes: magic, version, vertexCount, indexCount, vertexStride, flags, boundsMin[3], boundsMax[3], boneCount, animCount, reserved[2]). Editor-only (`#if ENGINE_EDITOR`): `CookManifestEntry` (originalPath, cookedPath, type, sourceHash, cookedSize), `CookManifest`-Klasse (addEntry, saveToFile/loadFromFile als JSON, findByOriginalPath), `AssetCooker`-Klasse mit `CookConfig` (projectRoot, engineRoot, outputDir, compressAssets, buildType, maxThreads) und `CookResult` (success, totalAssets, cookedAssets, skippedAssets, failedAssets, errors, elapsedSeconds). **AssetCooker.cpp:** `cookAll()` – iteriert AssetManager-Registry, inkrementelles Hashing (FNV-1a `hashFile()`), Manifest-Vergleich (skip wenn Hash unverändert), Typ-Dispatch: Model3D→`cookMesh()`, Material/Level/Widget/Skybox/Prefab→`cookStrippedJson()`, Audio→`cookAudio()`, Script/Shader/Texture→`copyAsset()`. `cookMesh()` – liest JSON-Asset, baut GPU-ready Vertex-Daten mit `BuildVerticesWithFlatNormals()` (standalone, kein GLM), schreibt CMSH-Binary (Header + Vertex-Stream + JSON-Metadaten-Blob für Skeleton/Animationen/Shader-Overrides). `cookStrippedJson()` – entfernt Editor-only-Felder, kompakt serialisiert. `cookAudio()` – kopiert .asset + referenzierte WAV-Datei. `copyAsset()` – 1:1-Kopie-Fallback. Manifest wird am Ende als `manifest.json` gespeichert. **OpenGLObject3D.cpp (Runtime-Loader):** Neue anonyme Namespace-Helfer: `FindCookedMeshPath()` (sucht .cooked-Datei), `IsCookedMesh()` (prüft CMSH-Magic), `CookedMeshData`-Struct, `LoadCookedMesh()` (liest Header + Vertex-Daten + JSON-Blob). `prepare()` erweitert um CMSH-Fast-Path vor dem JSON-Fallback: lädt Vertex-Daten direkt (kein BuildVerticesWithFlatNormals nötig), setzt Bounds aus Header, lädt Skeleton/Animationen aus Metadaten-Blob, baut OpenGLMaterial. **main.cpp (Build-Pipeline):** Step 5 „Cook assets" ersetzt bisheriges „Copy Content": erstellt `AssetCooker::CookConfig` aus Build-Konfiguration, ruft `cookAll()` mit Progress-Callback (zeilenweise Build-Output), Cancel-Flag-Integration (`m_buildCancelRequested`), loggt Ergebnis-Statistiken. Shader-Kopie, Engine-Content-Kopie und AssetRegistry.bin-Kopie bleiben als separate Operationen erhalten. **CMakeLists.txt:** AssetCooker.h/cpp zu ASSET_MANAGER_SOURCES hinzugefügt. **Editor-DLL-Filter:** Step 4 (Deploy) filtert Editor-only DLLs (AssetManager.dll, Scripting.dll, Renderer.dll) per Blocklist mit case-insensitive Vergleich aus dem Build-Output.

## Letzte Änderung (Standalone Game Build – Phase 10.1) → Neu implementiert

- ✅ `Standalone Game Build (Phase 10.1)`: Ursprünglich entfernt. Vollständig neu implementiert – siehe "Standalone Game Build – Phase 10.1 Neuimplementierung" oben.

## Letzte Änderung (Asset-Referenz-Tracking – Phase 4.4)

- ✅ `Asset-Referenz-Tracking (Phase 4.4)`: Vollständige UI-Integration für Asset-Referenz-Analyse im Content Browser. **Refs-Button:** PathBar-Button „Refs" zeigt alle Entities und Assets an, die das ausgewählte Asset referenzieren (nutzt `AssetManager::findReferencesTo()` – scannt `.asset`-Dateien und ECS-Entities). **Deps-Button:** PathBar-Button „Deps" zeigt alle Abhängigkeiten des ausgewählten Assets (nutzt `AssetManager::getAssetDependencies()` – parst JSON-Referenzen). **Unreferenzierte-Asset-Indikator:** `buildReferencedAssetSet()` in `UIManager` scannt alle ECS-Entities (MeshComponent, MaterialComponent, ScriptComponent) und baut ein `unordered_set<string>` referenzierter Pfade auf. Grid-Tiles für nicht-referenzierte Assets erhalten ein dezentes Punkt-Symbol (●) in gedämpfter Farbe (`textMuted`) oben rechts – sowohl im Such-Modus als auch in der normalen Ordneransicht. **Lösch-Warnung:** War bereits in `main.cpp` implementiert (Zeilen 2838–2843) – zeigt Referenzanzahl vor dem Löschen an.

## Letzte Änderung (Animation Editor Tab – Phase 2.4)

- ✅ `Animation Editor Tab (Phase 2.4)`: Dedizierter Editor-Tab zum Betrachten und Steuern von Skeletal-Animationen. `openAnimationEditorTab()`/`closeAnimationEditorTab()`/`refreshAnimationEditor()` in `UIManager.cpp` – folgt dem bewährten Tab-Pattern (Particle Editor als Vorlage). **Layout:** Toolbar (Titel mit Entity-Name + Stop-Button) → scrollbarer Content-Bereich mit drei Sektionen: **Clip-Liste** (alle Animation-Clips des Skeletts als klickbare Buttons, aktiver Clip mit Accent-Farbe hervorgehoben, zeigt Kanal-Anzahl und Dauer), **Playback Controls** (Status-Anzeige mit aktuellem Clip/Zeit, Speed-Slider 0–5x, Loop-Checkbox), **Bone-Hierarchie** (eingerückte Baum-Darstellung aller Bones mit Parent-Beziehungen und Indentation). **Renderer-API:** 12 neue virtuelle Methoden in `Renderer.h` (`isEntitySkinned`, `getEntityAnimationClipCount`, `getEntityAnimationClipInfo`, `getEntityAnimatorCurrentClip`, `getEntityAnimatorCurrentTime`, `isEntityAnimatorPlaying`, `playEntityAnimation`, `stopEntityAnimation`, `setEntityAnimationSpeed`, `getEntityBoneCount`, `getEntityBoneName`, `getEntityBoneParent`) + `AnimationClipInfo`-Struct. Override in `OpenGLRenderer.cpp` – greift auf `m_entityAnimators`-Map und `Skeleton`-Daten zu. **Details-Panel:** Neue AnimationComponent-Sektion mit Speed/Loop/Playing/ClipIndex-Feldern, "Edit Animation"-Button (nur bei skinned Meshes), Remove-Separator mit Undo/Redo. Animation im "Add Component"-Dropdown verfügbar. `AnimationEditorState`-Struct in `UIManager.h` (tabId, widgetId, linkedEntity, isOpen, refreshTimer, selectedClip).

## Letzte Änderung (Render-Pass-Debugger – Phase 9.4)

- 🔄 `Render-Pass-Debugger (Phase 9.4)`: Dedizierter Editor-Tab zur Echtzeit-Inspektion der gesamten Render-Pipeline. `openRenderDebuggerTab()`/`closeRenderDebuggerTab()`/`refreshRenderDebugger()` in `UIManager.cpp` – folgt dem bewährten Tab-Pattern. **RenderPassInfo-Struct** in `Renderer.h` (name, category, enabled, fboWidth, fboHeight, fboFormat, details). `getRenderPassInfo()` virtuelle Methode, Override in `OpenGLRenderer` liefert **19 Passes**: Shadow CSM, Point Shadow, Skybox, Geometry Opaque, Particles, OIT Composite, HeightField Debug, HZB Build, Pick Buffer, Post-Process Resolve, Bloom, SSAO, Grid, Collider Debug, Bone Debug, Selection Outline, Gizmo, FXAA Deferred, UI Rendering. **Layout:** Toolbar (Titel + Refresh-Button) → scrollbarer Content-Bereich mit Frame-Timing-Header (FPS, CPU World/UI, GPU), Object-Counts (Visible/Hidden/Total/Cull-Rate), Pipeline-Passes gruppiert nach Kategorie (Shadow=Lila, Geometry=Blau, Post-Process=Orange, Overlay=Grün, Utility=Grau, UI=Rot) mit Status-Dots (●/○), Pass-Name, FBO-Format/Auflösung, Details. Pipeline-Flow-Diagramm am Ende. **Auto-Refresh** alle 0.5s via Timer in `updateNotifications()`. `RenderDebuggerState`-Struct in `UIManager.h` (tabId, widgetId, isOpen, refreshTimer). Eintrag im Settings-Dropdown (`main.cpp`).

## Letzte Änderung (Shader Viewer Tab – Phase 9.1)

- 🔄 `Shader Viewer Tab (Phase 9.1)`: Dedizierter Editor-Tab zum Betrachten von GLSL-Shadern mit Syntax-Highlighting. `openShaderViewerTab()`/`closeShaderViewerTab()`/`refreshShaderViewer()` in `UIManager.cpp` implementiert – folgt dem bewährten Tab-Pattern (Console, Profiler). **Layout:** Toolbar (Titel + aktuelle Datei + Reload-Button) → horizontaler Split: linkes Panel mit scrollbarer Shader-Dateiliste (30 `.glsl`-Dateien aus `shaders/`-Verzeichnis), rechtes Panel mit zeilennummeriertem Code-Viewer. **Syntax-Highlighting:** Zeilenbasierte Farbgebung – Blau (Keywords: `if`, `else`, `for`, `return`, etc.), Grün (Typen: `vec2/3/4`, `mat4`, `sampler2D`, etc.), Gold (Qualifiers: `uniform`, `in`, `out`, `layout`, etc.), Lila (Preprocessor: `#version`, `#define`, `#ifdef`, etc.), Grau (Kommentare: `//` und `/* */` mit Block-Tracking). **Reload-Button:** Löst `requestShaderReload()` aus – neue virtuelle Methode in `Renderer.h`, in `OpenGLRenderer` implementiert: invalidiert Material-Cache, löscht UI-Shader-Programme, reloaded PostProcessStack, rebuildet alle Render-Entries. `ShaderViewerState`-Struct in `UIManager.h` (tabId, widgetId, isOpen, selectedFile, shaderFiles). Eintrag im Settings-Dropdown (`main.cpp`). Monospace-Schriftgröße (`fontSizeMonospace`).

## Letzte Änderung (Bone Debug-Overlay – Phase 9.3)

- ✅ `Bone / Skeleton Debug-Overlay (Phase 9.3)`: Wireframe-Overlay für Bone-Hierarchien selektierter Skinned-Mesh-Entities. `renderBoneDebug()` in `OpenGLRenderer.cpp` zeichnet für jede selektierte Entity mit `SkeletalAnimator` die Bone-Linien und Joint-Marker. Bone-Positionen werden aus `finalBoneMatrices * inverse(offsetMatrix)` extrahiert (Mesh-Space) und mit der Entity-Model-Matrix (Position/Rotation/Scale) in Weltkoordinaten transformiert. **Linien:** Cyan von jedem Bone zu seinem Parent-Bone. **Joint-Marker:** 3D-Kreuz an jeder Bone-Position (Cyan für normale Bones, Gelb+größer für Root-Bones). Nutzt bestehenden Gizmo-Shader (`m_gizmoProgram`, GL_LINES). Toggle-Button "Bone" in der ViewportOverlay-Toolbar. `Renderer.h`: `setBonesVisible()`/`isBonesVisible()` + `m_bonesVisible`-Member. Aktualisiert sich jeden Frame bei laufender Animation. Nur im Editor-Modus (nicht in PIE), nur für selektierte Entities (Performance).

## Letzte Änderung (Collider Visualisierung – Phase 9.2)

- ✅ `Collider & Physics Visualisierung (Phase 9.2)`: Wireframe-Overlays für alle Collider-Typen direkt im Viewport. `renderColliderDebug()` in `OpenGLRenderer.cpp` implementiert – iteriert alle Entities mit `CollisionComponent` via `Schema`/`getEntitiesMatchingSchema()` und zeichnet Wireframes mit dem bestehenden Gizmo-Shader (`m_gizmoProgram`, GL_LINES). **Box:** 12 Kanten (Wireframe-Quader). **Sphere:** 3 Kreise (XY, XZ, YZ) mit 32 Segmenten. **Capsule:** 2 Kreise + 4 Hemisphären-Bögen + 4 vertikale Linien. **Cylinder:** 2 Kreise + 4 vertikale Linien. Farb-Codierung nach `PhysicsComponent::MotionType`: Grün (Static), Orange (Kinematic), Blau (Dynamic), Rot (isSensor/Trigger). Entity-Transform (Position/Rotation) und `colliderOffset` werden korrekt berücksichtigt. Toggle-Button "Col" in der ViewportOverlay-Toolbar (`AssetManager.cpp`) mit Click-Event in `main.cpp` (toggelt `m_collidersVisible`, Text-Farb-Feedback, Toast-Nachricht, DiagnosticsManager-State). `Renderer.h`: `setCollidersVisible()`/`isCollidersVisible()` + `m_collidersVisible`-Member. Nur im Editor-Modus sichtbar (nicht in PIE).

## Letzte Änderung (Editor Roadmap)

- ✅ `EDITOR_ROADMAP.md Erweiterung`: Roadmap um 4 neue Phasen erweitert (Phase 9–12). **Phase 9 – Erweiterte Visualisierung & Scene-Debugging**: Shader Editor/Viewer Tab (GLSL Syntax-Highlighting, Uniform-Inspector, Varianten-Switching), Collider & Physics Visualisierung (Wireframe-Overlays für Box/Sphere/Capsule im Viewport), Bone/Skeleton Debug-Overlay (Bone-Linien + Positionen aus finalBoneMatrices), Render-Pass-Debugger (Pipeline-Flow, Buffer-Inspector, Per-Pass-Statistiken). **Phase 10 – Build & Packaging** ⏸️: Pausiert – Build-Dialog und Build-Pipeline entfernt, wird nach Editor-Separation-Rework neu implementiert. **Phase 11 – Erweiterte Editor-Workflows**: Multi-Viewport-Layout (2–4 Sub-Viewports mit unabhängigen Kameras), Cinematic Sequencer UI (visuelles Kamera-Pfad-Editing mit 3D-Spline-Visualisierung und Timeline), Editor-Plugin-System (`engine.editor` Python-Modul für Menüeinträge, Tabs, Entity-Zugriff), Level-Streaming-UI (Multi-Level mit Streaming-Volumes, Level-Composition-Panel). **Phase 12 – Editor-Separation-Rework**: Grundlegende Trennung Editor/Engine-Kern via API-Layer und `#if ENGINE_EDITOR`-Präprozessor-Guards, zwei CMake-Targets (Engine, EngineEditor). Alle bestehenden Fortschrittsprüfungen aktualisiert: Particle Editor (2.5) und Asset Thumbnails (4.1) auf ✅ gesetzt. Sprint-Empfehlungen auf 14 Sprints erweitert.

## Letzte Änderung (Content Browser / ECS)

- ✅ `Entity Templates / Prefabs (Phase 3.2)`: Neuer Asset-Typ `AssetType::Prefab` für serialisierbare Entity-Vorlagen. Content Browser → Rechtsklick → „Save as Prefab" speichert die selektierte Entity mit allen 13 Komponententypen als JSON-Asset (Magic 0x41535453, Version 2, Type 12). Prefab-Assets im Content Browser mit eigenem Icon (`entity.png`, Teal-Tint) und Typ-Filter-Button. Drag & Drop auf den Viewport spawnt Entity an der Cursor-Position (`screenToWorldPos`-Fallback auf Kamera-Richtung). Doppelklick spawnt am Ursprung. „+ Entity"-Dropdown-Button in der Content-Browser-PathBar mit 7 Built-in-Templates: Empty Entity, Point Light, Directional Light, Camera, Static Mesh, Physics Object, Particle Emitter. Alle Spawn-Operationen mit Undo/Redo-Integration. Neue Methoden in `UIManager`: `savePrefabFromEntity()`, `spawnPrefabAtPosition()`, `spawnBuiltinTemplate()`. Interne Helfer: `prefabSerializeEntity()`/`prefabDeserializeEntity()` serialisieren/deserialisieren alle ECS-Komponenten.

## Letzte Änderung (Toast Notification Levels – Phase 6.3)

- ✅ `Toast NotificationLevel (Phase 6.3)`: Einheitliches `NotificationLevel`-Enum (`Info`, `Success`, `Warning`, `Error`) in `DiagnosticsManager.h`, per `using`-Alias in `UIManager` übernommen. `ToastNotification`-Struct um `level`-Feld erweitert. `enqueueToastNotification()` akzeptiert optionalen Level-Parameter (default `Info`). `showToastMessage()` erhöht Mindestdauer für Warning (≥4s) und Error (≥5s). `createToastWidget()` rendert farbigen 4px-Akzentbalken links am Toast (Theme-Farben: `accentColor`/`successColor`/`warningColor`/`errorColor`). Notification History speichert Level pro Eintrag. Alle Aufrufer aktualisiert: `AssetManager.cpp` (7 Stellen), `PythonScripting.cpp` (3 Stellen) mit passenden Error/Success-Levels.

## Letzte Änderung (Viewport)

- ✅ `Rubber-Band-Selection (Phase 5.2)`: Marquee-Selektion im Viewport implementiert. Linksklick+Drag im Viewport zieht ein halbtransparentes blaues Auswahlrechteck auf. Bei Mouse-Up werden alle Entities im Rechteck über den Pick-Buffer (glReadPixels-Block) selektiert. Ctrl+Drag für additive Selektion. Bei kleinem Rechteck (<4px) Fallback auf Einzel-Pick. `Renderer.h`: 6 virtuelle Methoden (`beginRubberBand`, `updateRubberBand`, `endRubberBand`, `cancelRubberBand`, `isRubberBandActive`, `getRubberBandStart`). `OpenGLRenderer`: State-Members (`m_rubberBandActive`, `m_rubberBandStart`, `m_rubberBandEnd`), `resolveRubberBandSelection()` liest Pick-FBO blockweise, `drawRubberBand()` rendert Overlay mit Gizmo-Shader (Fill + Border). `main.cpp`: Mouse-Down startet Rubber-Band (statt sofortigem Pick), Motion aktualisiert, Mouse-Up resolved oder fällt auf Pick zurück.

- ✅ `Material Editor Tab (Phase 2.2)`: Vollwertiger Editor-Tab für Material-Assets statt Popup. Doppelklick auf Material im Content Browser öffnet dedizierten Tab mit 3D-Preview (Cube + Directional Light + Ground Plane via eigenem Runtime-Level) und rechtem Properties-Panel. Properties-Panel enthält: PBR-Section (PBR-Checkbox, Metallic/Roughness/Shininess-Slider), Textures-Section (5 Textur-Slot-Einträge: Diffuse, Specular, Normal, Emissive, MetallicRoughness), Save-Button. Neue `MaterialEditorWindow`-Klasse (`src/Renderer/EditorWindows/`) folgt MeshViewerWindow-Architektur: `initialize()`, `createRuntimeLevel()` (JSON-Entities), `take/giveRuntimeLevel()`. `OpenGLRenderer`: `m_materialEditors`-Map, `openMaterialEditorTab()`/`closeMaterialEditorTab()`/`getMaterialEditor()`. `setActiveTab()` erweitert für Material-Editor-Level-Swap (leaving/entering/returning in allen 3 Branches). `Renderer.h` um 3 neue virtuelle Methoden erweitert. Properties-Panel nutzt `EditorUIBuilder`-Factories (makeSliderRow, makeCheckBox, makeStringRow, makeSection, makePrimaryButton).

- ✅ `Verbessertes Scrollbar-Design (Phase 1.6)`: macOS-inspirierte Overlay-Scrollbars für alle scrollbaren Panels. Auto-Hide nach 1.5s Inaktivität mit 0.3s Fade-Out. Scrollbar-Breite: 6px default, 10px bei Hover. Abgerundete Thumb-Enden via `scrollbarBorderRadius` (3.0px). `EditorTheme` um 5 Felder erweitert: `scrollbarAutoHide`, `scrollbarWidth`, `scrollbarWidthHover`, `scrollbarAutoHideDelay`, `scrollbarBorderRadius` – alle DPI-skaliert + JSON-serialisiert. `WidgetElement` Runtime-State: `scrollbarOpacity`, `scrollbarActivityTimer`, `scrollbarHovered`. `UIManager::updateScrollbarVisibilityRecursive()` steuert Fade und Hover-Erkennung. Rendering in allen 3 Pfaden (Editor UI, Viewport UI, Widget Editor FBO) via `drawUIPanel` mit Theme-Farben × Opacity.

- ✅ `Animierte Übergänge / Micro-Interactions (Phase 1.5)`:

- ✅ `Modernisierte Icon-Sprache (Phase 1.3)`:

- ✅ `Surface-Snap / Drop to Surface (Phase 5.4 + 3.6)`:

- ✅ `Python print() → Console Tab (Phase 2.1 Ergänzung)`: `PyLogWriter` C++-Typ (anonymer Namespace in `PythonScripting.cpp`) mit `write()`/`flush()`-Methoden. Zeilengepuffert: akkumuliert Text bis `\n`, dann Flush kompletter Zeilen an Logger. `sys.stdout` → `Logger::LogLevel::INFO` mit `Category::Scripting`. `sys.stderr` → `Logger::LogLevel::ERROR` mit `Category::Scripting`. `InstallPythonLogRedirect()` wird nach `Py_Initialize()` in `Scripting::Initialize()` aufgerufen. Python-`print()`-Ausgaben erscheinen nun direkt im Console-Tab des Editors.

- ✅ `Profiler / Performance-Monitor Tab (Phase 2.7)`:

- ✅ `One-Click Scene Setup (Phase 3.3)`: `createNewLevelWithTemplate()` in `UIManager` mit `SceneTemplate`-Enum (Empty/BasicOutdoor/Prototype). „+ Level"-`DropdownButtonWidget` in Content-Browser-PathBar neben Import. ECS via `initialize({})` zurückgesetzt, Entities per Lambda (Name, Transform, Mesh, Material, Light) aufgebaut, `level->onEntityAdded()` registriert. BasicOutdoor sucht ersten Skybox-Asset in Registry. Level sofort gespeichert, Outliner/ContentBrowser refresht. Eindeutige Level-Namen (NewLevel, NewLevel1, ...).

- ✅ `Auto-Collider-Generierung (Phase 3.5)`: `autoFitColliderForEntity()` in `UIManager` berechnet Mesh-AABB aus Vertex-Daten (stride 5), skaliert mit Entity-Scale, wählt ColliderType per Heuristik (Sphere bei Aspekt <1.4, Capsule bei vertikal >2.5, sonst Box) mit Center-Offset. „Add Component → Physics" fügt automatisch `CollisionComponent` mit gefitteten Dimensionen hinzu. „Auto-Fit Collider"-Button in der Collision-Sektion des Details-Panels über `EditorUIBuilder::makeButton`. `<limits>` Include in UIManager.cpp.

- ✅ `Auto-Material bei Model-Import (Phase 3.1)`: Erweiterte Toast-Benachrichtigung für 3D-Model-Import. `createdTextureCount`-Zähler in der Import-Pipeline. Model3D-Importe zeigen jetzt „Imported {name} with {n} material(s) and {m} texture(s)" statt generischem „Imported: {name}". Generischer Toast wird für Model3D übersprungen.

- ✅ `Entity Copy/Paste & Duplicate (Phase 5.3)`: Ctrl+C/Ctrl+V/Ctrl+D für Entities. `EntityClipboard`-Struct in `UIManager` speichert Snapshots aller 13 Komponententypen (Transform, Mesh, Material, Light, Camera, Physics, Script, Name, Collision, HeightField, Lod, Animation, ParticleEmitter). `copySelectedEntity()` erstellt Snapshot der selektierten Entity. `pasteEntity()` erzeugt neue Entity mit Positions-Offset (+1,0,0), Name-Suffix „(Copy)", registriert bei Level, selektiert neue Entity, UndoRedo-Command. `duplicateSelectedEntity()` (Ctrl+D) erstellt frischen Snapshot und pastet, ohne den Clipboard zu überschreiben. Shortcuts in main.cpp im Ctrl-Block (neben Ctrl+Z/Y/S/F).

- ✅ `Content Browser Suche & Filter (Phase 4.2)`: Echtzeit-Suchfeld und Typ-Filter im Content Browser. `m_browserSearchText` (string) + `m_browserTypeFilter` (uint16_t Bitmask) in `UIManager`. PathBar erweitert: 7 Typ-Filter-Toggle-Buttons (Mesh/Mat/Tex/Script/Audio/Level/Widget) als Accent-farbige Toggles + EntryBar-Suchfeld rechts. Suchmodus: Bei nicht-leerem Suchtext werden ALLE Assets über alle Ordner als flache Liste angezeigt (case-insensitive Substring-Suche + Typ-Filter). Normalmodus: Typ-Filter filtert Assets im aktuellen Ordner. Doppelklick auf Such-Ergebnis: navigiert zum Asset-Ordner, klappt Tree auf, öffnet Asset-Editor (Model3D→MeshViewer, Widget→WidgetEditor, Material→MaterialEditor, Level→LevelLoad), leert Suchtext. `focusContentBrowserSearch()` Methode + Ctrl+F Shortcut in main.cpp fokussiert das Suchfeld.

- ✅ `Console / Log-Viewer Tab (Phase 2.1)`: Vollständiges Console-Tab im Editor. `Logger::ConsoleEntry`-Ring-Buffer (max 2000 Einträge) mit `sequenceId`, `timestamp`, `level`, `category`, `message`. `UIManager::openConsoleTab()` erzeugt Tab mit Toolbar + scrollbarem Log-Bereich. Toolbar: Filter-Buttons (All/Info/Warning/Error) als Toggle mit Bitmask (`levelFilter`), Such-EntryBar (case-insensitive Substring), Clear-Button (leert Buffer), Auto-Scroll-Toggle (ON/OFF). `refreshConsoleLog()` baut Log-Zeilen aus Ring-Buffer auf — Level-Tag + Timestamp + Category + Message, farblich nach Level (Info=textSecondary, Warning=warningColor, Error=errorColor, Fatal=rot). `closeConsoleTab()` entfernt Tab + Widget. 0.5s-Poll-Timer in `updateNotifications()`. Zugang über Settings-Dropdown → „Console" in main.cpp.

- ✅ `Editor Shadows & Elevation (Phase 1.2)`: Elevation-System für visuelle Tiefenhierarchie. Neues `elevation`-Feld (int, 0–5) in `WidgetElement` – bestimmt Shadow-Intensität. `WidgetElementStyle::applyElevation()` Helper mappt Stufen auf `shadowColor`/`shadowOffset`/`shadowBlurRadius`. `EditorTheme` um `shadowColor` (Vec4, Default `{0,0,0,0.3}`) und `shadowOffset` (Vec2, Default `{2,3}`) erweitert – DPI-skaliert, JSON-serialisiert (toJson/fromJson), Light-Theme-Variante mit reduzierter Opacity (0.18). `drawUIShadow()` um `blurRadius`-Parameter erweitert – Spread-Layer skalieren mit blurRadius statt fester 1/3/6px. Alle 3 Render-Pfade (`drawUIWidgetsToFramebuffer`, `renderViewportUI`, `renderUI`) übergeben `shadowBlurRadius`. Modals/Confirm-Dialoge: `elevation=3` (starker Schatten). Toasts: `elevation=3` (schwebende Karten). Dropdown-Menüs: `elevation=2` (mittlerer Schatten). Deferred DropDown-Rendering: Shadow vor Background-Panel eingefügt. `WidgetDetailSchema`: Shadow-Section um Elevation-Slider (0–5), Blur-Radius-Feld erweitert; Elevation-Änderung ruft automatisch `applyElevation()` auf. Widget-JSON-Serialisierung: `shadowColor`, `shadowOffset`, `shadowBlurRadius`, `elevation` in `writeElement()`/`readElement()`.

- ✅ `Tooltip-System (Phase 8.1)`: Vollständiges Tooltip-System. UIManager: `m_tooltipTimer`/`m_tooltipText`/`m_tooltipVisible` + `kTooltipDelay=0.45s`. Hover-Tracking in `updateHoverStates()`, Timer in `updateNotifications(dt)`. Tooltip als EditorWidget z=10000, `toastBackground`/`toastText`, Bildschirm-Clamping. Tooltips auf 17+ Buttons (Toolbar, TitleBar, StatusBar, Remove Component, Add Component). `kEditorWidgetUIVersion=7`.

- ✅ `Toolbar Redesign (ViewportOverlay)`: Komplette Neugestaltung als ein horizontaler StackPanel. Layout: RenderMode | Undo(↶) Redo(↷) | PIE(zentriert, volle Höhe) | Snap(#) CamSpeed(1.0x) Stats | Settings(Icon). Settings.png statt Text. RenderMode-Dropdown-Bug behoben (war von CenterStack überdeckt). Undo/Redo funktional, Snap/CamSpeed/Stats als Dummy mit Toast.

- ✅ `Editor Spacing & Typography (Phase 1.4)`: `EditorTheme` um `sectionSpacing` (10px), `groupSpacing` (6px), `gridTileSpacing` (8px) erweitert. Alle in `applyDpiScale`/`toJson`/`fromJson` integriert. `SeparatorWidget::toElement()` nutzt `theme.sectionSpacing` als Margin, `theme.separatorThickness` + `theme.panelBorder` für Trennlinien, `theme.sectionHeaderHeight` für Header. Content-Browser Grid-Tiles mit `gridTileSpacing`-Margin. `makeLabel()` → `textSecondary`, `makeSecondaryLabel()` → `textMuted` für konsistente Font-Gewichtung.

- ✅ `DPI/Theme Startup Fix (v2)`: `rebuildAllEditorUI()` durch `rebuildEditorUIForDpi(currentDpi)` in `main.cpp` ersetzt. Startup durchläuft jetzt exakt denselben vollständigen Pfad wie ein DPI-Wechsel zur Laufzeit: JSON-Assets regenerieren → Elementbäume nachladen → Theme anwenden → dynamische Widgets neu aufbauen. Behebt: Skalierung wurde beim Start nicht korrekt auf Buttons/Controls angewendet.

- ✅ `DPI Scaling Phase 2 – Dynamic UI Elements`: Alle dynamisch erzeugten UI-Elemente verwenden `EditorTheme::Scaled()` für Pixelwerte. Betrifft: `makeTreeRow`, `makeGridTile`, `populateOutlinerWidget`, `populateOutlinerDetails`, `populateContentBrowserWidget`, `showDropdownMenu`, UIDesigner. `makeLabel()`/`makeSecondaryLabel()` skalieren `minWidth` intern. Deferred-Dropdown-Rendering: min ItemHeight skaliert + Background-Panel.

- ✅ `DPI Scaling Rebuild Architecture`: DPI-Erkennung in `main.cpp` VOR `AssetManager::initialize()`. `rebuildEditorUIForDpi(float)`: pausiert Rendering → regeneriert Widget-Assets → lädt komplette Widgets aus JSON → wendet Theme an → baut dynamische Widgets neu → setzt fort.

- ✅ `Dropdown Background Fix`: `dropdownBackground` Farbe in `EditorTheme` von `{0.07, 0.07, 0.07, 0.98}` (identisch zu `panelBackground`) auf `{0.13, 0.13, 0.16, 1.0}` geändert. Dropdowns sind nun visuell klar vom Panel-Hintergrund unterscheidbar. `dropdownHover` ebenfalls angepasst auf `{0.18, 0.18, 0.22, 1.0}`.

- ✅ `Editor Widget DPI Scaling (Phase 1.3)`: Alle 7 Editor-Widget-Assets vollständig DPI-skaliert. `kEditorWidgetUIVersion=5`. Neuer `S(px)`-Lambda in `ensureEditorWidgetsCreated` und `createWorldSettingsWidgetAsset` skaliert alle Pixel-Werte mit `EditorTheme::Get().dpiScale`. Font-Größen verwenden Theme-Felder (`t.fontSizeHeading/Subheading/Body/Small`) statt Literale. Widget-Abmessungen (`m_sizePixels`), Button-`minSize`, `padding`, `borderRadius` werden alle mit `S()` skaliert. `makeToolBtn`-Lambda fängt `S`/`t` per Referenz. `checkUIVersion` prüft zusätzlich `_dpiScale` im gespeicherten JSON (Toleranz 0.01) – bei DPI-Wechsel werden alle Assets automatisch neu generiert. `_dpiScale` wird in jedem Widget-JSON gespeichert.

- ✅ `Editor Widget UI Refresh (Phase 1.2)`: Alle Editor-Widget-Assets (TitleBar, ViewportOverlay, WorldSettings, WorldOutliner, EntityDetails, ContentBrowser, StatusBar) auf kompakteres, modernes Design umgestellt. `kEditorWidgetUIVersion` auf 4 angehoben. Zentrale Helper-Lambdas `checkUIVersion` und `writeWidgetAsset`. Panel-Höhen reduziert: TitleBar 100px, ViewportOverlay 34px, StatusBar 28px, ContentBrowser 190px, WorldOutliner/EntityDetails Breite 240px. Alle Buttons mit `borderRadius`. `ApplyThemeToElement` erweitert. Titel-Labels auf 13px vereinheitlicht. StatusBar-Button-Höhen 22px.

- ✅ `SDF Rounded Rect UI Rendering (Phase 1.1)`: Abgerundete Ecken für alle Editor-Widgets implementiert. GLSL-Shader `panel_fragment.glsl` und `button_fragment.glsl` von einfachem Rect-Rendering auf SDF-basierte Rounded-Rect-Berechnung umgestellt (`roundedRectSDF()` mit Smoothstep-Anti-Aliasing). Neues `uniform float uBorderRadius` in beiden Shadern. `drawUIPanel()` um `borderRadius`-Parameter erweitert (Default 0.0 für Rückwärtskompatibilität). `UIPanelUniforms`-Struct um `borderRadiusLoc` ergänzt. `drawUIBrush()` leitet `borderRadius` an `drawUIPanel()` weiter (SolidColor-Brush). Alle drei Render-Pfade (`drawUIWidgetsToFramebuffer`, `renderViewportUI`, `renderUI`) für ~30 Element-Typen aktualisiert: Panel, Button, ToggleButton, RadioButton, EntryBar, DropDown, DropdownButton, CheckBox, ScrollView, StackPanel, Grid, TreeView, TabView, Border, ProgressBar, Slider, WrapBox, UniformGrid, SizeBox etc. übergeben `element.style.borderRadius` an den Shader. `EditorUIBuilder`: Factory-Methoden (`makeButtonBase`, `makeEntryBar`, `makeDropDown`) setzen `borderRadius` aus `EditorTheme::borderRadius` (Default 3.0px). Discard bei alpha < 0.001 verhindert Overdraw außerhalb der Rundung.

- ✅ `EDITOR_ROADMAP.md`: Ausführliche Editor-Roadmap erstellt (`EDITOR_ROADMAP.md`). 8 Phasen, 35+ Features, 10 Sprint-Empfehlungen. Phase 1: Visuelles Widget-Redesign (Rounded Panels, Elevation/Shadows, Icon-System, Spacing, Micro-Animations, Scrollbar-Design). Phase 2: Fehlende Tabs (Console/Log-Viewer, Material Editor Tab, Texture Viewer, Animation Editor, Particle Editor, Audio Preview, Profiler). Phase 3: Automatisierung (Auto-Material bei Import, Prefabs/Templates, Scene Setup, Auto-LOD, Auto-Collider, Snap & Grid). Phase 4: Content-Browser (Thumbnails, Suche/Filter, Drag & Drop, Referenz-Tracking). Phase 5: Viewport-UX (Viewport-Einstellungen, Multi-Select, Copy/Paste, Surface-Snap). Phase 6: Editor-Framework (Docking, Shortcut-System, Benachrichtigungen). Phase 7: Scripting (Script-Editor, Debug-Breakpoints, Hot-Reload). Phase 8: Polish (Tooltips, Onboarding, Keyboard-Navigation, Undo-Erweiterungen). Fortschrittstabelle mit Priorität/Aufwand pro Feature.
- ✅ `Displacement Mapping (Tessellation)`:
- ✅ `Cinematic-Kamera / Pfad-Follow`: Catmull-Rom-Spline-basierte Kamera-Pfade implementiert. Neues `CameraPath.h` mit `CameraPathPoint`-Struct (Position + Yaw/Pitch) und `CameraPath`-Struct mit `evaluate(t)`-Methode für stückweise kubische Catmull-Rom-Interpolation über beliebig viele Kontrollpunkte. Loop-Modus mit nahtlosem Index-Wraparound. `Renderer.h` um 6 virtuelle Methoden erweitert: `startCameraPath(points, duration, loop)`, `isCameraPathPlaying()`, `pauseCameraPath()`, `resumeCameraPath()`, `stopCameraPath()`, `getCameraPathProgress()`. `OpenGLRenderer` implementiert alle Methoden: Pfad-Tick im Render-Loop parallel zum bestehenden `CameraTransition`-Tick, SDL-PerformanceCounter-Delta-Timing, automatisches Stoppen bei Pfadende (non-loop) oder kontinuierliches Wraparound (loop). `moveCamera()`/`rotateCamera()` blockiert während Pfad-Playback. `startCameraPath()` bricht aktive Transitions ab. Python-API: `engine.camera.start_path(points, duration, loop)`, `is_path_playing()`, `pause_path()`, `resume_path()`, `stop_path()`, `get_path_progress()`. `engine.pyi` aktualisiert. CMakeLists.txt um `CameraPath.h` erweitert.
- ✅ `Particle-System`: CPU-simuliertes, GPU-instanced Partikelsystem. `ParticleEmitterComponent` in ECS (13. ComponentKind, `MaxComponentTypes` auf 13 erhöht) mit 20 Parametern (emissionRate, lifetime, speed, speedVariance, size, sizeEnd, gravity, coneAngle, colorStart/End RGBA, maxParticles, enabled, loop). `ParticleSystem` (`ParticleSystem.h/.cpp`): Per-Emitter-Partikelpool, LCG-Random Cone-Emission, Gravity, Lifetime-Decay, Color/Size-Interpolation. GPU-Rendering via Point-Sprites: Single VBO (pos3+rgba4+size1 = 8 floats/Partikel), back-to-front Sort, `GL_PROGRAM_POINT_SIZE`. Shaders: `particle_vertex.glsl` (perspektivische Billboard-Skalierung via `gl_PointSize`), `particle_fragment.glsl` (prozeduraler Soft-Circle mit `gl_PointCoord` + smoothstep). Render-Pass nach Opaque, vor OIT in `renderWorld()`, nur während PIE. Frame-Dt via SDL Performance Counter. JSON-Serialisierung (save/load) in EngineLevel. Python-API: `engine.particle.set_emitter(entity, key, value)`, `set_enabled(entity, bool)`, `set_color(entity, r,g,b,a)`, `set_end_color(entity, r,g,b,a)`. `Component_ParticleEmitter` Konstante. `engine.pyi` aktualisiert.
- ✅ `Shader-Variants / Permutationen`: Präprozessor-basierte Shader-Varianten implementiert. `ShaderVariantKey.h` definiert 8 Feature-Flags (`SVF_HAS_DIFFUSE_MAP`, `SVF_HAS_SPECULAR_MAP`, `SVF_HAS_NORMAL_MAP`, `SVF_HAS_EMISSIVE_MAP`, `SVF_HAS_METALLIC_ROUGHNESS`, `SVF_PBR_ENABLED`, `SVF_FOG_ENABLED`, `SVF_OIT_ENABLED`) als Bitmask. `buildVariantDefines()` generiert `#define`-Block. `OpenGLShader::loadFromFileWithDefines()` injiziert Defines nach `#version`-Zeile. Fragment-Shader nutzt `#ifdef`/`#else`-Guards für Diffuse/Specular/Normal/Emissive-Sampling, Fog und OIT — eliminiert tote Branches bei gesetztem Define, Uniform-Fallback bleibt für unbekannte Varianten. `OpenGLMaterial::setVariantKey()` rekompiliert Fragment-Shader on-the-fly und relinkt Programm. `cacheUniformLocations()` als eigenständige Methode extrahiert. `OpenGLObject3D::setTextures()` berechnet automatisch den Variant-Key aus aktiven Textur-Slots. `setPbrData()` setzt `SVF_PBR_ENABLED`-Flag.
- ✅ `Kamera-Überblendung`: Smooth-Interpolation zwischen Kamera-Positionen/-Orientierungen. `CameraTransition`-Struct in `Renderer.h`. `OpenGLRenderer` implementiert `startCameraTransition()`, `isCameraTransitioning()`, `cancelCameraTransition()` mit Smooth-Step-Easing (3t²−2t³). Während Transition sind `moveCamera()`/`rotateCamera()` blockiert. Python-API: `engine.camera.transition_to()`, `is_transitioning()`, `cancel_transition()`. `engine.pyi` aktualisiert.
- ✅ `Material-Instancing / Overrides`: Per-Entity-Material-Overrides implementiert. Neue `MaterialOverrides`-Struct in `MaterialComponent` mit optionalen Overrides für `colorTint` (RGB), `metallic`, `roughness`, `shininess`, `emissiveColor` – jeweils mit `has*`-Flags. Fragment-Shader erweitert um `uniform vec3 uColorTint` (multiplikativer Farb-Tint auf diffuse Textur). `OpenGLMaterial` um `setColorTint()`, `setOverrideMetallic()`, `setOverrideRoughness()`, `setOverrideShininess()` erweitert, Uniform-Location `uColorTint` in `build()` gecacht und in `bind()` hochgeladen. Renderer wendet Overrides vor jedem individuellen Draw-Call an und stellt danach Defaults wieder her. Entities mit Overrides brechen GPU-Instancing-Batches auf (eigener Draw-Call wie bei Skinned/Emission-Entities). JSON-Serialisierung in EngineLevel (unter `MaterialComponent.overrides`). Scripting-API: `set_material_override_color_tint`, `get_material_override_color_tint`, `set_material_override_metallic`, `set_material_override_roughness`, `set_material_override_shininess`, `clear_material_overrides`. `engine.pyi` aktualisiert.
- ✅ `Skeletal Animation`: Vollständiges Skeletal-Animation-System implementiert. Neue Datenstrukturen (`SkeletalData.h`): `Skeleton` (Bone-Hierarchie, Node-Tree, AnimationClips), `SkeletalAnimator` (Runtime-Playback mit Keyframe-Interpolation: linear für Position/Scale, Slerp für Rotation), `Mat4x4`/`Quat` Helfer-Typen. Per-Vertex Bone-Daten (4 IDs + 4 Weights) werden beim Assimp-Import extrahiert (`aiProcess_LimitBoneWeights`), Node-Hierarchie und Animations-Keyframes im Asset-JSON gespeichert (`m_hasBones`, `m_bones`, `m_boneIds`, `m_boneWeights`, `m_nodes`, `m_animations`).
- ✅ `Skeletal Animation – Vertex Layout`: Skinned Meshes verwenden erweiterten Vertex-Layout (22 Floats/Vertex statt 14): pos3+norm3+uv2+tan3+bitan3+boneIds4+boneWeights4. Attribute-Locations 5 (boneIds als float, im Shader zu int gecastet) und 6 (boneWeights). Nicht-Skinned-Meshes bleiben bei 14 Floats.
- ✅ `Skeletal Animation – Shader`: Neuer `skinned_vertex.glsl` erweitert den Standard-Vertex-Shader um `uniform bool uSkinned` und `uniform mat4 uBoneMatrices[128]`. Bei aktivem Skinning wird die gewichtete Summe der Bone-Matrizen auf Position/Normal/Tangent/Bitangent angewendet, bevor die Model-Matrix multipliziert wird.
- ✅ `Skeletal Animation – Material`: `OpenGLMaterial` um `setSkinned(bool)` und `setBoneMatrices(float*, int)` erweitert. Uniform-Locations `uSkinned`/`uBoneMatrices[0]` werden in `build()` gecacht, in `bind()` hochgeladen (row-major, `GL_TRUE`).
- ✅ `Skeletal Animation – Object3D`: `OpenGLObject3D::prepare()` erkennt `m_hasBones` im Asset-JSON, wählt automatisch `skinned_vertex.glsl`, baut erweiterten Vertex-Buffer mit Bone-Daten, lädt Skeleton (Bones, Node-Hierarchie, Animations) aus JSON. `isSkinned()`/`getSkeleton()` API exponiert.
- ✅ `Skeletal Animation – Renderer-Integration`: Pro Skinned-Entity wird automatisch ein `SkeletalAnimator` erstellt und die erste Animation im Loop gestartet. Animatoren werden pro Frame per SDL-PerformanceCounter-Delta getickt. Bone-Matrizen werden vor jedem Draw-Call hochgeladen. Skinned Meshes werden einzeln gerendert (kein GPU-Instancing, da jede Entity eigene Bone-Pose hat).
- ✅ `Skeletal Animation – Shadow Mapping`: Shadow-Vertex-Shader (`ensureShadowResources`) um Skinning erweitert (gleiche `uSkinned`/`uBoneMatrices`-Uniforms). Skinned Meshes werden in allen 3 Shadow-Passes (Regular, CSM, Point) einzeln mit Bone-Matrizen gerendert. Korrekte animierte Schatten.
- ✅ `Skeletal Animation – ECS`: Neue `AnimationComponent` (currentClipIndex, currentTime, speed, playing, loop). `ComponentKind::Animation`, `MaxComponentTypes` auf 12 erhöht. JSON-Serialisierung in `EngineLevel.cpp`. `EntitySnapshot` um `animation`-Feld erweitert.
- ✅ `Skeletal Animation – IRenderObject3D`: Interface um `isSkinned()` und `getSkeleton()` (mit Default-Implementierung) erweitert.
- ✅ `Scripting`: `engine.pyi` mit vollständiger Skeletal-Animation-Dokumentation aktualisiert (Import-Pipeline, Vertex-Layout, Shader, Runtime-Playback, ECS-Integration).
- ✅ `OpenGLTextRenderer`: Bugfix – Horizontale Text-Spiegelung im Viewport behoben. `renderViewportUI()` rendert jetzt im Full-FBO-Viewport (`glViewport(0,0,windowW,windowH)`) statt mit Offset-Viewport, um driverabhängige Quirks mit Offset-Viewport + Text-Rendering zu vermeiden. Die Ortho-Projektion wird mit Viewport-Offset verschoben, Scissor-Test clippt auf den Viewport-Bereich.
- ✅ `UIWidget`: Neue `WidgetElement`-Properties: `borderColor`, `borderThickness`, `borderRadius`, `opacity`, `isVisible`, `tooltipText`, `isBold`, `isItalic`, `gradientColor`, `maxSize`, `spacing`, `radioGroup`. JSON-Serialisierung vollständig.
- ✅ `UIWidget`: Neue Widget-Typen: `Label`, `Separator`, `ScrollView`, `ToggleButton`, `RadioButton`. Rendering in `renderUI()` und `renderViewportUI()` vollständig.
- ✅ `UIWidgets`: Neue Helper-Header: `LabelWidget.h`, `ToggleButtonWidget.h`, `ScrollViewWidget.h`, `RadioButtonWidget.h`.
- ✅ `UIManager`: Layout-Berechnung/Anordnung für neue Widget-Typen und `spacing`-Property erweitert.
- ✅ `UIManager`: Neue Controls (Label, ToggleButton, RadioButton, ScrollView) in der Widget-Editor-Palette (linkes Panel) hinzugefügt, inkl. Drag&Drop-Defaults.
- ✅ `UIManager`: Details-Panel erweitert – H/V-Alignment (Left/Center/Right/Fill), Size-to-Content, Max Width/Height, Spacing, Opacity, Visibility, Border-Width/-Radius, Tooltip, Bold/Italic, RadioGroup. FillX/FillY-Checkboxen durch intuitivere Alignment-Steuerung ersetzt.
- ✅ `Scripting`: Neues Python-Modul `engine.viewport_ui` mit 17 Methoden für Viewport-UI aus Scripts. `engine.pyi` aktualisiert.
- ✅ `OpenGLRenderer`: Nach Shadow-Rendering wird der Content-Rect-Viewport (inkl. Offset) wiederhergestellt. Dadurch bleibt die Welt an der korrekten Position im Viewport-Bereich.
- ✅ `ViewportUI`: Grundgerüst `ViewportUIManager` erstellt und an `OpenGLRenderer` angebunden (Viewport-Rect-Übergabe, Layout-Dirty-Tracking, Selektion, JSON-Serialisierung).
- ✅ `ViewportUI`: Renderpfad `renderViewportUI()` im `OpenGLRenderer` voll funktionsfähig – Full-FBO-Viewport mit Ortho-Offset und Scissor-Clipping, nur für Viewport-Tab aktiv.
- ✅ `ViewportUI`: Input-Routing im Haupt-Eventloop integriert (Editor-UI → Viewport-UI → 3D). HitTest, Klick-Callbacks, Pressed-State und `isOverUI`-Berücksichtigung beider UI-Systeme aktiv.
- ✅ `Gameplay UI`: Vollständig implementiert – Multi-Widget-System mit Z-Order, WidgetAnchor (10 Positionen), implizites Canvas Panel pro Widget, Anchor-basiertes Layout (`computeAnchorPivot` + `ResolveAnchorsRecursive`), `engine.viewport_ui` Python-Modul (28 Methoden), Gameplay-Cursor-Steuerung mit automatischer Kamera-Blockade, Auto-Cleanup bei PIE-Stop. Rein dynamisch per Script, kein Asset-Typ, kein Level-Bezug. Siehe `GAMEPLAY_UI_PLAN.md` Phase A.
- ✅ `UI Designer Tab`: Editor-Tab (wie MeshViewer) für visuelles Viewport-UI-Design – Controls-Palette (7 Typen: Panel/Text/Label/Button/Image/ProgressBar/Slider), Widget-Hierarchie-TreeView, Properties-Panel (Identity/Anchor/Size/Appearance/Text/Image/Value), bidirektionale Sync via `setOnSelectionChanged`, Selektions-Highlight (orangefarbener 2px-Rahmen im Viewport). Öffnung über Settings-Dropdown. Siehe `GAMEPLAY_UI_PLAN.md` Phase B.
- ✅ `Scripting/UI`: Runtime-Widget-Steuerung erweitert – `engine.ui.spawn_widget(content_path)` lädt ein Widget-Asset per Content-relativem Pfad und gibt eine Widget-ID zurück; `engine.ui.remove_widget(widget_id)` entfernt das Widget. Widgets werden ausschließlich im Viewport-Bereich gerendert (via `ViewportUIManager::registerScriptWidget`/`unregisterScriptWidget`) und beim Beenden von PIE automatisch zerstört (`clearAllScriptWidgets`); `engine.pyi` wurde synchronisiert.
- ✅ `Widget Editor`: Widget-Assets können jetzt im Content Browser über **New Widget** erzeugt werden (`AssetType::Widget`) und erscheinen danach direkt in Tree/Grid.
- ✅ `Widget Editor`: Doppelklick auf ein Widget-Asset öffnet nun einen eigenen Widget-Editor-Tab; das Asset wird geladen und tab-scoped dargestellt.
- ✅ `Widget Editor`: Tab-Layout jetzt im Editor-Stil (links Controls+Hierarchie, rechts Details, Mitte Preview-Center mit Fill-Color-Hintergrund).
- ✅ `Widget Editor`: Widget-Editor-Tabs nutzen den tab-spezifischen Framebuffer als reine Workspace-Fläche (kein 3D-Welt-Renderpass in diesen Tabs).
- ✅ `Widget Editor`: TitleBar-Tab-Leiste wird beim Hinzufügen/Entfernen automatisch neu aufgebaut, sodass neue Widget-Editor-Tabs sofort sichtbar sind (analog Mesh Viewer).
- ✅ `Widget Editor`: Klickbare Hierarchie im linken Panel – jedes Element ist als Button dargestellt mit Typ-Label und ID; Klick wählt das Element aus und aktualisiert das Details-Panel.
- ✅ `Widget Editor`: Preview-Elemente im Center-Bereich sind hit-testable – Klick auf ein Element im Widget-Preview selektiert es direkt.
- ✅ `Widget Editor`: Rechtes Details-Panel zeigt editierbare Properties des selektierten Elements: Layout (From/To, MinSize, Padding, FillX/Y), Appearance (Color RGBA), Text (Text, Font, FontSize, TextColor), Image (ImagePath), Slider/ProgressBar (Min/Max/Value).
- ✅ `Widget Editor`: `WidgetEditorState`-Tracking pro offenem Editor-Tab (tabId, assetPath, editedWidget, selectedElementId) in `UIManager`.
- ✅ `Widget Editor`: Bereits offene Widget-Editor-Tabs werden bei erneutem Doppelklick nur aktiviert (kein Doppel-Öffnen).
- ✅ `Build-System`: Debug/Release-Artefakt-Kollisionen bei Multi-Config-Builds behoben (konfigurationsgetrennte Output-Verzeichnisse), dadurch `LNK2038` Runtime-/Iterator-Mismatch beseitigt.
- ✅ `OpenGLRenderer`: Default-Framebuffer wird jetzt vor dem Tab-FBO-Blit explizit mit `m_clearColor` gecleart. Verhindert undefinierte Back-Buffer-Inhalte bei Nicht-Viewport-Tabs (z. B. Widget Editor).
- ✅ `OpenGLTextRenderer`: Blend-State wird jetzt in `drawTextWithProgram()` per `glGetIntegerv`/`glBlendFuncSeparate` gesichert und nach dem Text-Rendering wiederhergestellt. Behebt das Überschreiben der separaten Alpha-Blend-Funktion des UI-FBO durch `glBlendFunc`.
- ✅ `Widget Editor`: Preview-Zoom per Mausrad auf dem Canvas (0.1×–5.0×), zentriert auf Widget-Mitte.
- ✅ `Widget Editor`: Preview-Pan per Rechtsklick+Ziehen auf dem Canvas (im Laptop-Modus per Linksklick+Ziehen).
- ✅ `Widget Editor`: Steuerelemente in der linken Palette sind per Drag-&-Drop auf das Preview hinzufügbar. Unterstützte Typen: Panel, Text, Button, Image, EntryBar, StackPanel, Grid, Slider, CheckBox, DropDown, ColorPicker, ProgressBar, Separator.
- ✅ `Widget Editor`: Schriftgrößen in allen Panels vergrößert (Titel 16px, Steuerelemente 14px, Hints 13px) für bessere Lesbarkeit.
- ✅ `Widget Editor`: Bugfix – Erneutes Öffnen eines Widget-Assets funktioniert jetzt zuverlässig. `loadWidgetAsset` löst Content-relative Pfade gegen das Projekt-Content-Verzeichnis auf. Verwaiste Tabs bei Ladefehler werden automatisch entfernt.
- ✅ `Widget Editor`: Toolbar am oberen Rand mit Save-Button und Dirty-Indikator („* Unsaved changes"). `saveWidgetEditorAsset()` synchronisiert das Widget-JSON zurück in die AssetData und speichert via `AssetManager::saveAsset()`. `markWidgetEditorDirty()` setzt das `isDirty`-Flag und aktualisiert die Toolbar.
- ✅ `Widget Editor`: Z-Order-Fix – Preview-Widget rendert jetzt auf z=1 (hinter den UI-Panels z=2), Canvas-Hintergrund auf z=0, Toolbar auf z=3. Beim Zoomen/Panning überdeckt die Preview nicht mehr die Seitenpanels.
- ✅ `OpenGLRenderer`: Panel-Elemente rendern jetzt Kind-Elemente rekursiv (sowohl in `renderUI()` als auch `renderViewportUI()`). Behebt das Problem, dass Widget-Previews nur eine konstante Hintergrundfarbe ohne Inhalt anzeigten.
- ✅ `Widget Editor`: Preview-Clipping – Das Preview-Widget wird per `glScissor` auf den Canvas-Bereich beschränkt und ragt beim Zoomen/Panning nicht mehr über die Tab-Content-Area hinaus. `getWidgetEditorCanvasRect()` und `isWidgetEditorContentWidget()` liefern die Clip-Bounds für den Renderer.
- ✅ `Widget Editor`: Tab-Level-Selektion – Die Delete-Taste löscht im Widget-Editor-Tab das selektierte Element (`deleteSelectedWidgetEditorElement`) statt das Asset im Content Browser. `tryDeleteWidgetEditorElement()` prüft ob ein Widget-Editor aktiv ist und leitet den Delete dorthin um.
- ✅ `Widget Editor`: Undo/Redo – Hinzufügen und Löschen von Elementen werden als `UndoRedoManager::Command` registriert. Ctrl+Z macht die Aktion rückgängig (Element wird wiederhergestellt bzw. entfernt), Ctrl+Y wiederholt sie.
- ✅ `Widget Editor`: FBO-basierte Preview – Das editierte Widget wird in einen eigenen OpenGLRenderTarget-FBO gerendert (bei (0,0) mit Design-Größe layoutet, nicht im UI-System registriert). Die FBO-Textur wird per `drawUIImage` als Quad im Canvas-Bereich angezeigt mit Zoom/Pan und Scissor-Clipping. Selektierte Elemente erhalten eine orangefarbene Outline (`drawUIOutline`). Linksklick im Canvas transformiert Screen→Widget-Koordinaten und selektiert das oberste Element per Bounds-Hit-Test. `previewDirty`-Flag steuert Neu-Rendering. FBO-Cleanup beim Tab-Schließen via `cleanupWidgetEditorPreview()`.
- ✅ `Widget Editor`: Details-Panel-Werte werden sofort auf die FBO-Preview angewendet – alle onChange-Callbacks nutzen einen `applyChange`-Helper, der `markWidgetEditorDirty()` (setzt `previewDirty`) und `editedWidget->markLayoutDirty()` aufruft, sodass das FBO bei jeder Eigenschaftsänderung neu gerendert wird.
- ✅ `Widget Editor`: Drag-&-Drop auf leere Widgets – Wenn ein Widget noch keine Elemente hat, wird das per Drag-&-Drop hinzugefügte Element als Root-Element eingefügt (statt früher stillschweigend ignoriert zu werden).
- ✅ `Widget Editor`: Hierarchie-Drag-&-Drop – Elemente im linken Hierarchie-Panel können per Drag-&-Drop umsortiert werden. `moveWidgetEditorElement()` entfernt das Element aus seiner aktuellen Position und fügt es als Sibling nach dem Ziel-Element ein (mit Zyklus-Schutz gegen Drop auf eigene Kinder).
- ✅ `Widget Editor`: Outline-Fix – `drawUIOutline` rendert Outlines jetzt als 4 dünne Kantenrechtecke statt per `glPolygonMode(GL_LINE)`, wodurch keine Dreiecks-Diagonalen mehr sichtbar sind.
- ✅ `Widget Editor`: Preview-Klick-Fix – Hit-Test in `selectWidgetEditorElementAtPos` verwendet nun `computedPositionPixels/computedSizePixels` (eigenes visuelles Rect) statt `boundsMinPixels/boundsMaxPixels` (expandiert mit Kindern). Elemente ohne ID erhalten beim Laden automatisch generierte IDs.
- ✅ `Widget Editor`: Alignment-Dropdowns – Horizontale und vertikale Ausrichtung im Details-Panel werden jetzt per DropDown-Widget (Left/Center/Right/Fill bzw. Top/Center/Bottom/Fill) statt per Texteingabe gesteuert.
- ✅ `Widget Editor`: Details-Reorganisation – Properties sind nun in logische Sektionen gegliedert: Identity (Typ, editierbare ID) → Transform (From/To) → Layout (Alignment, Min/Max, Padding) → Appearance → typspezifische Sektionen.
- ✅ `Widget Editor`: UX-Plan erstellt – `WIDGET_EDITOR_UX_PLAN.md` beschreibt 5 Phasen zur Verbesserung: Grundlegende Bedienbarkeit, WYSIWYG-Editing, Produktivität, fortgeschrittene Features und Polish.
- ✅ `Widget Editor`: Hit-Test-Fix – `measureAllElements` stellt sicher, dass alle Elemente (auch Kinder von Panel-Elementen) korrekte `hasContentSize`-Werte erhalten. Die Hit-Test-Traversierung in `selectWidgetEditorElementAtPos` verwendet nun `std::function` statt rekursiver Auto-Lambdas für zuverlässigere Tiefensuche.
- ✅ `Widget Editor`: Hover-Preview – Beim Überfahren eines Elements im Canvas-Preview wird dessen Bounding-Box als hellblaue Outline angezeigt (`updateWidgetEditorHover()`). Die Selection-Outline (orange) und Hover-Outline (blau) verwenden nun `computedPositionPixels/computedSizePixels` statt `boundsMinPixels/boundsMaxPixels`.
- ✅ `UIWidget`: Phase 1 Layout-Fundament – 6 neue `WidgetElementType`-Werte: `WrapBox` (Flow-Container mit automatischem Umbruch), `UniformGrid` (Grid mit gleichgroßen Zellen, `columns`/`rows`), `SizeBox` (Single-Child-Container mit `widthOverride`/`heightOverride`), `ScaleBox` (skaliert Kind per `ScaleMode`: Contain/Cover/Fill/ScaleDown/UserSpecified), `WidgetSwitcher` (zeigt nur Kind `activeChildIndex`), `Overlay` (stapelt Kinder übereinander mit Alignment). Neue Felder: `columns`, `rows`, `widthOverride`, `heightOverride`, `scaleMode`, `userScale`, `activeChildIndex`. `ScaleMode`-Enum hinzugefügt.
- ✅ `UIWidget`: JSON-Serialisierung für alle 6 neuen Layout-Typen (readElement/writeElement) mit typspezifischen Feldern.
- ✅ `UIManager`: Layout-Berechnung (measureElementSize + layoutElement) für alle 6 neuen Typen implementiert – WrapBox Flow+Wrap, UniformGrid gleichmäßige Zellen, SizeBox Override-Dimensionen, ScaleBox Skalierungsfaktor pro Modus, WidgetSwitcher nur aktives Kind, Overlay gestapelt mit H/V-Alignment.
- ✅ `OpenGLRenderer`: Rendering-Support für alle 6 neuen Layout-Typen in `renderViewportUI()` und `drawUIWidgetsToFramebuffer()` – Container-Hintergrund (falls alpha > 0) + rekursives Kind-Rendering.
- ✅ `UIManager`: Widget-Editor-Palette um 6 neue Typen erweitert (WrapBox, UniformGrid, SizeBox, ScaleBox, WidgetSwitcher, Overlay). Drag-&-Drop-Defaults und Viewport-Designer-Palette ebenfalls aktualisiert.
- ✅ `UIManager`: Details-Panel-Properties für neue Typen: Spacing (WrapBox/UniformGrid), Columns/Rows (UniformGrid), Width/Height Override (SizeBox), ScaleMode-Dropdown + UserScale (ScaleBox), Active Index (WidgetSwitcher).
- ✅ `UIWidgets`: 6 neue Helper-Header erstellt: `WrapBoxWidget.h`, `UniformGridWidget.h`, `SizeBoxWidget.h`, `ScaleBoxWidget.h`, `WidgetSwitcherWidget.h`, `OverlayWidget.h`.
- ✅ `Scripting`: `engine.pyi` mit Dokumentation der 6 neuen Widget-Typen aktualisiert.
- ✅ `UIWidget`: Phase 2 Styling & Visual Polish – Neue Datentypen: `BrushType`-Enum (None, SolidColor, Image, NineSlice, LinearGradient), `UIBrush`-Struct (Typ, Farbe, Gradient-Endfarbe, Winkel, Bild-Pfad, 9-Slice-Margins, Tiling), `RenderTransform`-Struct (Translation, Rotation, Scale, Shear, Pivot), `ClipMode`-Enum (None, ClipToBounds, InheritFromParent).
- ✅ `UIWidget`: Phase 2 WidgetElement-Felder: `background` (UIBrush), `hoverBrush` (UIBrush), `fillBrush` (UIBrush), `renderTransform` (RenderTransform), `clipMode` (ClipMode), `effectiveOpacity` (float, berechneter Wert zur Renderzeit).
- ✅ `UIWidget`: Phase 2 JSON-Serialisierung: `readBrush`/`writeBrush`, `readRenderTransform`/`writeRenderTransform` Hilfsfunktionen. Rückwärtskompatibilität mit Legacy-`color`-Feldern gewährleistet.
- ✅ `OpenGLRenderer`: `drawUIBrush()` – Neue Renderer-Funktion, die nach `BrushType` dispatcht: SolidColor → `drawUIPanel`, Image → `drawUIImage`, NineSlice → Bild-Fallback, LinearGradient → dedizierter Gradient-Shader (inline GLSL 330 mit `uColorStart`/`uColorEnd`/`uAngle`).
- ✅ `OpenGLRenderer`: Gradient-Shader-Programm (`m_uiGradientProgram`) mit Lazy-Init und gecachten Uniform-Locations (`UIGradientUniforms`).
- ✅ `OpenGLRenderer`: Opacity-Vererbung in beiden Rendering-Pfaden (`renderViewportUI` + `drawUIWidgetsToFramebuffer`). `parentOpacity`-Parameter in `renderElement`-Lambda, `effectiveOpacity = element.opacity * parentOpacity`, Alpha-Multiplikation auf alle `drawUIPanel`/`drawUIImage`/`drawUIBrush`-Aufrufe.
- ✅ `OpenGLRenderer`: Brush-basiertes Background-Rendering – Wenn `element.background.isVisible()`, wird `drawUIBrush()` vor dem typspezifischen Rendering aufgerufen. Legacy-`color`-Felder werden nur gezeichnet, wenn kein Brush gesetzt ist.
- ✅ `UIManager`: Phase 2 Details-Panel-Properties im Widget-Editor: Brush-Typ-Dropdown (Background), Brush-Farbfelder (RGBA + Gradient-End-Farbe + Winkel + Bild-Pfad), ClipMode-Dropdown, RenderTransform-Felder (Translation, Rotation, Scale, Shear, Pivot).
- ✅ `Scripting`: `engine.pyi` mit Phase 2 Typ-Dokumentation aktualisiert (BrushType, UIBrush, RenderTransform, ClipMode, Opacity-Vererbung).
- ✅ `UIManager`: Bugfix – `handleScroll()` prüft jetzt scrollbare Widgets (z. B. Details-Panel) *vor* dem Canvas-Zoom. Zuvor konsumierte `isOverWidgetEditorCanvas()` den Scroll-Event über dem gesamten Fenster (da das Canvas-Widget `fillX/fillY` hat), sodass Scrolling im rechten Details-Panel als Zoom interpretiert wurde. Zusätzlich Tab-Filterung hinzugefügt (analog `getWidgetsOrderedByZ`), damit Widgets inaktiver Tabs keine Scroll-Events abfangen.
- ✅ `OpenGLRenderer`: RenderTransform-Rendering – `ComputeRenderTransformMatrix()` Helper (T(pivot)·Translate·Rotate·Scale·Shear·T(-pivot)) im anonymen Namespace. Alle drei Render-Pfade (`renderViewportUI`, `drawUIWidgetsToFramebuffer`, `renderUI`) multiplizieren die Transform-Matrix auf die uiProjection. RAII-Structs (`RtRestore`/`RtRestore2`/`RtRestore3`) stellen die Projektion bei jedem Exit-Pfad automatisch wieder her.
- ✅ `ViewportUIManager`: RenderTransform-Hit-Testing – `InverseTransformPoint()` Helper im anonymen Namespace berechnet die inverse 2D-Transformation (Undo Translation → Rotation → Scale → Shear). `HitTestRecursive()` und `HitTestRecursiveConst()` transformieren den Mauszeiger in den lokalen (untransformierten) Koordinatenraum, bevor der Bounds-Check erfolgt. Kinder erben den transformierten Punkt.
- ✅ `OpenGLRenderer`: ClipMode-Scissor-Stack – Alle drei Render-Pfade unterstützen `ClipMode::ClipToBounds`. Bei aktivem Clip wird der aktuelle GL-Scissor gespeichert, mit den Element-Bounds geschnitten (Achsen-ausgerichtete Intersection) und per RAII-Structs (`ScissorRestore`/`ScissorRestore2`/`ScissorRestore3`) beim Verlassen wiederhergestellt. Verschachtelte Clips schneiden korrekt ineinander.
- ✅ `Scripting`: `engine.pyi` mit RenderTransform-Rendering/Hit-Testing und ClipMode-Scissor-Verhaltensdokumentation aktualisiert.
- ✅ `UIManager`: Widget-Editor/UI-Designer Sidepanel-Rendering korrigiert – mehrere `StackPanel`-Container setzen jetzt explizit transparente `color`-Werte statt ungewollt den weißen `WidgetElement`-Default zu verwenden (behebt partielle weiße Flächen im linken Panel und in Details-Zeilen).
- ✅ `UIManager`: Widget-Editor-Control-Palette verbessert – die einzelnen Steuerelement-Einträge nutzen jetzt einen echten Hover-State (`Button` mit transparenter Basis + Hover-Farbe), damit der aktuell überfahrene Eintrag klar sichtbar ist.
- ✅ `OpenGLRenderer`: Viewport-UI-Control-Rendering korrigiert – `Text`/`Label` sowie `Button`/`ToggleButton`/`RadioButton` verwenden jetzt korrekte H/V-Ausrichtung, Wrap-Text und Auto-Fit der Schrift statt fixer Top-Left-Textausgabe; behebt fehlerhafte Darstellung bei verfügbaren Controls (u. a. Label/Layout).
- ✅ `UIWidget`: Phase-3-Easing-Grundlage implementiert – neue zentrale Runtime-Funktion `EvaluateEasing(EasingFunction, t)` deckt alle Standardkurven ab (`Linear`, `EaseIn/Out/InOut` für Quad, Cubic, Elastic, Bounce, Back) und normalisiert Eingaben über Clamping auf `[0..1]`.
- ✅ `UIWidget`: Phase-3-Animation-Playback ergänzt – `WidgetAnimationPlayer` mit `play/playReverse/pause/stop/tick`, in `Widget` angebunden (`animationPlayer()`), inklusive Track-Interpolation über Keyframes + Easing und Property-Application auf animierbare `WidgetElement`-Felder (`RenderTransform`, `Opacity`, `Color`, `Position`, `Size`, `FontSize`).
- ✅ `ViewportUIManager` / `OpenGLRenderer`: Phase-3-Tick-Integration umgesetzt – `ViewportUIManager::tickAnimations(float)` tickt alle Widget-Animationen und markiert Layout/Render als dirty; `OpenGLRenderer::render()` speist den Tick pro Frame mit SDL-PerformanceCounter-Delta.
- ✅ `Scripting`: Phase-3-Python-API ergänzt – `engine.ui.play_animation`, `engine.ui.stop_animation` und `engine.ui.set_animation_speed` steuern Widget-Animationen auf gespawnten Viewport-Widgets; `engine.pyi` wurde dazu synchronisiert.
- ✅ `UIManager`: Widget-Editor-Animations-Timeline – Redesigned Unreal-Style Bottom-Panel (260px) mit zwei Hauptbereichen: **Links (150px)** Animations-Liste (Animations-Header mit +/x-Buttons, klickbare Animationseinträge mit Selektions-Highlight), **Rechts** Timeline-Content mit Toolbar (+Track-Dropdown, Play ▶/Stop ■, Duration-Eingabe, Loop-Checkbox). Track-Bereich als horizontaler Split: Links (200px) Tree-View mit aufklappbaren Element-Headern (▾/▸ Chevrons, `expandedTimelineElements`-State) und eingerückten Property-Zeilen (Property-Dropdown, ◆ Keyframe-Hinzufügen, x Track-Entfernen); Rechts Timeline mit Ruler, **Scrubber-Linie** (2px orange, per Klick/Drag verschiebbar mit Echtzeit-FBO-Preview via `applyAnimationAtTime`), kleinere Keyframe-Diamanten (7px/7pt statt 14px/16px), **End-of-Animation-Linie** (2px rot, verschiebbar zur Änderung der Animationsdauer). Drag-Interaktionen direkt in `handleMouseDown`/`handleMouseMotionForPan`/`handleMouseUp` integriert (statt separate Timeline-Handler). Drei Drag-Modi: `isDraggingScrubber`, `isDraggingEndLine`, `draggingKeyframeTrack/Index` – Keyframes folgen dem Cursor horizontal in Echtzeit (begrenzt auf [0, duration]). Alternating Row Colors für bessere Track-Unterscheidung. Ruler-Indikator-Leiste (4px) zeigt Scrubber- und End-Line-Position. Element-Header-Rows betten orangefarbene Scrubber- und rote End-Linie als 1px-Panels ein. Ausgewählte Keyframe-Details (Time, Value, Easing-Dropdown, Delete) in unterer Leiste. Toggle via "Timeline"/"Hide Timeline"-Button in der Editor-Toolbar.
- ✅ `UIWidget`: Phase-3-Grundlage implementiert – neue Animationsdatenstrukturen (`AnimatableProperty`, `EasingFunction`, `AnimationKeyframe`, `AnimationTrack`, `WidgetAnimation`) plus Widget-Persistenz (`m_animations`) mit JSON-Serialisierung (`m_animations` im Widget-Asset).
- ✅ `UIWidget`: Phase 4 Border Widget – neuer `WidgetElementType::Border` (Single-Child-Container). Neue Felder: `borderBrush` (UIBrush), `borderThicknessLeft/Top/Right/Bottom` (float, per-Seite Dicke), `contentPadding` (Vec2). JSON-Serialisierung vollständig.
- ✅ `UIWidget`: Phase 4 Spinner Widget – neuer `WidgetElementType::Spinner` (animiertes Lade-Symbol). Neue Felder: `spinnerDotCount` (int, default 8), `spinnerSpeed` (float, Umdrehungen/Sek, default 1.0), `spinnerElapsed` (float, Runtime-Zähler). JSON-Serialisierung (ohne Runtime-Feld).
- ✅ `UIManager`: Phase 4 Layout – Border: Kind wird um borderThickness + contentPadding eingerückt. Spinner: feste Größe (minSize oder 32×32 Default). Border als Container-Typ in Switch-Case registriert.
- ✅ `OpenGLRenderer`: Phase 4 Rendering – Border: Universal-Background-Brush + 4 Kanten-Rects via `drawUIBrush` + Kind-Rekursion. Spinner: N Punkte im Kreis mit Opacity-Falloff, animiert über `spinnerElapsed * spinnerSpeed`. Alle 3 Render-Pfade (Viewport-UI, Editor-UI, Widget-Editor-Preview) aktualisiert.
- ✅ `UIManager`: Phase 4 Editor-Integration – Border/Spinner in Palette-Controls, Drag-&-Drop-Defaults (addElementToEditedWidget). Details-Panel: Border (Dicke L/T/R/B, ContentPadding X/Y, BorderBrush RGBA), Spinner (DotCount, Speed).
- ✅ `ViewportUIManager`: Phase 4 Spinner-Tick – `tickSpinnersRecursive` im `tickAnimations()` Tick-Loop, inkl. `m_renderDirty`-Markierung.
- ✅ `UIManager`: Phase 4 Spinner-Tick – `TickSpinnersRecursive` in `updateNotifications()` für Editor-Widgets.
- ✅ `Build/CMake`: Cross-Platform-Vorbereitung – CMake-Konfiguration für Linux und macOS erweitert. MSVC/WIN32-Guards für plattformspezifische Optionen, `ENGINE_PYTHON_LIB` für portables Python-Linking, `find_package(OpenGL/Threads)`, `OpenGL::GL`/`CMAKE_DL_LIBS` im Renderer, `CMAKE_POSITION_INDEPENDENT_CODE ON`, plattformabhängige Deploy-Pfade, PhysX-Plattformerkennung (windows/linux/mac), GCC/Clang-Warnflags.
- ✅ `UIWidgets`: 2 neue Helper-Header: `BorderWidget.h`, `SpinnerWidget.h`.
- ✅ `Scripting`: `engine.pyi` mit Border- und Spinner-Typ-Dokumentation (Felder, Layout, Rendering) aktualisiert.
- ✅ `UIWidget`: Phase 4 Multiline EntryBar – neue Felder `isMultiline` (bool, default false) und `maxLines` (int, 0 = unbegrenzt). JSON-Serialisierung vollständig.
- ✅ `UIManager`: Phase 4 Multiline-Input – Enter-Taste fügt `\n` ein wenn `isMultiline` aktiv (mit `maxLines`-Prüfung). Details-Panel: Multiline-Checkbox und Max-Lines-Property für EntryBar.
- ✅ `OpenGLRenderer`: Phase 4 Multiline-Rendering – EntryBar rendert mehrzeiligen Text zeilenweise (Split an `\n`, Y-Offset pro Zeile). Caret wird auf der letzten Zeile positioniert. Beide Render-Pfade (Viewport-UI, Editor-UI) aktualisiert.
- ✅ `UIWidgets`: `EntryBarWidget.h` um `setMultiline()`/`setMaxLines()` erweitert.
- ✅ `Scripting`: `engine.pyi` mit Multiline-EntryBar-Dokumentation (isMultiline, maxLines, Rendering-Verhalten) aktualisiert.
- ✅ `UIWidget`: Phase 4 Rich Text Block – neuer `WidgetElementType::RichText`. Neues Feld `richText` (string, Markup-Quelle). Neues Struct `RichTextSegment` (text, bold, italic, color, hasColor, imagePath, imageW, imageH). `ParseRichTextMarkup()` Parser für `<b>`, `<i>`, `<color=#RRGGBB>`, `<img>` Tags. JSON-Serialisierung vollständig.
- ✅ `OpenGLRenderer`: Phase 4 RichText-Rendering – Markup → Segment-Parse → Word-Liste mit Per-Word-Style → Greedy Word-Wrap → Zeilen-Rendering mit `drawText` pro Wort. Alle 3 Render-Pfade (Viewport-UI, Editor-UI, Widget-Editor-Preview) aktualisiert.
- ✅ `UIManager`: Phase 4 RichText-Integration – Layout (minSize oder 200×40 Default), Palette-Eintrag „RichText", addElementToEditedWidget-Defaults, Details-Panel „Rich Text"-Markup-Feld.
- ✅ `UIWidgets`: Neuer Helper-Header `RichTextWidget.h` (Builder für RichText-Elemente).
- ✅ `Scripting`: `engine.pyi` mit Rich-Text-Block-Dokumentation (richText-Feld, Markup-Tags, Layout, Helper-Klasse) aktualisiert.
- ✅ `UIWidget`: Phase 4 ListView/TileView – neue `WidgetElementType::ListView` und `WidgetElementType::TileView`. Neue Felder `totalItemCount` (int), `itemHeight` (float, default 32), `itemWidth` (float, default 100), `columnsPerRow` (int, default 4), `onGenerateItem` (Callback). JSON-Serialisierung vollständig.
- ✅ `OpenGLRenderer`: Phase 4 ListView/TileView-Rendering – virtualisierte Darstellung mit Scissor-Clipping, alternierenden Zeilen-/Tile-Farben, Scroll-Offset-Unterstützung. Alle 3 Render-Pfade (Viewport-UI, Editor-UI, Widget-Editor-Preview) aktualisiert.
- ✅ `UIManager`: Phase 4 ListView/TileView-Integration – Layout (ListView 200×200, TileView 300×200 Default), Palette-Einträge „ListView"/„TileView", addElementToEditedWidget-Defaults, Details-Panel (Item Count, Item Height, Item Width, Columns).
- ✅ `UIWidgets`: 2 neue Helper-Header: `ListViewWidget.h`, `TileViewWidget.h`.
- ✅ `Scripting`: `engine.pyi` mit ListView- und TileView-Dokumentation (Felder, Virtualisierung, Helper-Klassen) aktualisiert.
- ✅ `UIWidget`: Phase 5 Focus System – neues Struct `FocusConfig` (isFocusable, tabIndex, focusUp/Down/Left/Right). Neue Felder `focusConfig` (FocusConfig) und `focusBrush` (UIBrush) auf WidgetElement. JSON-Serialisierung vollständig.
- ✅ `ViewportUIManager`: Phase 5 Focus-Manager – `setFocus()`, `clearFocus()`, `getFocusedElementId()`, `setFocusable()` API. Tab/Shift+Tab-Navigation (tabToNext/tabToPrevious mit tabIndex-Sortierung), Pfeiltasten Spatial-Navigation (Dot-Product + Nearest-Neighbor), Enter/Space-Aktivierung (CheckBox/ToggleButton-Toggle, onClicked für andere), Escape zum Fokus-Löschen. Focus-on-Click in handleMouseDown.
- ✅ `OpenGLRenderer`: Phase 5 Fokus-Highlight – Post-Render-Pass in `renderViewportUI()` zeichnet 2px-Outline um fokussiertes Element mit `focusBrush.color` (Default blau {0.2, 0.6, 1.0, 0.9}).
- ✅ `UIManager`: Phase 5 Editor-Integration – Neuer „Focus"-Abschnitt im Widget-Editor Details-Panel: Focusable-Checkbox, Tab Index, Focus Up/Down/Left/Right ID-Felder, Focus Brush RGBA.
- ✅ `Scripting`: Phase 5 Python API – `engine.ui.set_focus(element_id)`, `engine.ui.clear_focus()`, `engine.ui.get_focused_element()`, `engine.ui.set_focusable(element_id, focusable)`.
- ✅ `Scripting`: `engine.pyi` mit Phase-5-Dokumentation (FocusConfig, Keyboard-Navigation, Python API) aktualisiert.
- ✅ `ViewportUIManager`: Phase 5 Gamepad-Input-Adapter – `handleGamepadButton(int, bool)` und `handleGamepadAxis(int, float)`. D-Pad → Spatial-Navigation, A/South → Aktivierung, B/East → Fokus löschen, LB/RB → Tab-Navigation. Left-Stick mit Deadzone (0.25), Repeat-Delay (0.35s) und Repeat-Interval (0.12s) in `tickAnimations()`.
- ✅ `main.cpp`: SDL3-Gamepad-Integration – `SDL_INIT_GAMEPAD` aktiviert, `SDL_Gamepad*` Tracking, Event-Routing (GAMEPAD_ADDED/REMOVED/BUTTON_DOWN/UP/AXIS_MOTION), Cleanup vor `SDL_Quit()`.
- ✅ `Scripting`: `engine.pyi` mit Gamepad-Navigation-Dokumentation (Button-Mapping, Stick-Repeat, SDL3-Integration) aktualisiert.
- ✅ `UIWidget`: Phase 5 Drag & Drop – neues Struct `DragDropOperation` (sourceElementId, payload, dragPosition). Neue Felder `acceptsDrop` (bool), `onDragOver`, `onDrop`, `onDragStart` Callbacks auf WidgetElement. JSON-Serialisierung für `isDraggable`, `dragPayload`, `acceptsDrop`.
- ✅ `ViewportUIManager`: Phase 5 Drag & Drop – `handleMouseMove()`, `isDragging()`, `getCurrentDragOperation()`, `getDragOverElementId()`, `cancelDrag()`. Threshold-basierter Drag-Start (5px), Drop-Target-Erkennung via Hit-Test, Drop-Completion mit onDragOver/onDrop Callbacks.
- ✅ `OpenGLRenderer`: Phase 5 Drag-Visual – Grüne 2px-Outline um Drop-Target-Element während aktivem Drag.
- ✅ `Scripting`: Phase 5 Python API – `engine.ui.set_draggable(element_id, enabled, payload)`, `engine.ui.set_drop_target(element_id, enabled)`.
- ✅ `Scripting`: `engine.pyi` mit Phase-5-Drag-&-Drop-Dokumentation (DragDropOperation, Drag-Flow, JSON, Python API) aktualisiert.
- ✅ `UIWidget`: WidgetElementStyle-Refactoring – 14 visuelle Felder (`color`, `hoverColor`, `pressedColor`, `disabledColor`, `textColor`, `textHoverColor`, `textPressedColor`, `fillColor`, `opacity`, `borderThickness`, `borderRadius`, `isVisible`, `isBold`, `isItalic`) aus `WidgetElement` in neues Sub-Struct `WidgetElementStyle style` konsolidiert. Zugriff einheitlich über `element.style.*`. JSON-Serialisierung rückwärtskompatibel.
- ✅ `OpenGLRenderer` / `UIManager` / `ViewportUIManager` / `PythonScripting` / `main.cpp`: Alle Render-Pfade, Layout-Berechnungen, Details-Panel-Bindings, Hit-Test-Logik, Scripting-Bridges und Color-Picker-Zugriffe auf `element.style.*`-Zugriffsmuster migriert.
- ✅ `UIWidget`: Fehlende Implementierungen für `Widget::Widget()`, `Widget::animationPlayer()`, `Widget::findAnimationByName()`, `Widget::applyAnimationTrackValue()`, `Widget::applyAnimationAtTime()` und alle 10 `WidgetAnimationPlayer`-Methoden (play, playReverse, pause, stop, setSpeed, getCurrentTime, isPlaying, getCurrentAnimation, tick, attachWidget) in `UIWidget.cpp` nachgetragen.
- ✅ `Scripting`: `engine.pyi` mit `WidgetElementStyle`-Struct-Dokumentation (14 Felder, Zugriffsmuster `element.style.*`) aktualisiert.
- ✅ `UIWidget` / `UIManager` / `OpenGLRenderer`: Phase 4 Integration-Fix – Fehlende Switch-Cases für Border, Spinner, RichText, ListView, TileView in `toString`/`fromString`, `measureElementSize`, `layoutElement`, Auto-ID-Zuweisung, Hierarchy-Type-Labels und Renderer-Container-Checks nachgetragen. Viewport-Designer-Palette und Creation-Defaults für alle 5 neuen Typen ergänzt.
- ✅ `UIManager` / `UIWidget`: Widget-Editor-Animations-Timeline-Restore – Alle 6 deklarierten aber fehlenden Timeline-Methoden (`refreshWidgetEditorTimeline`, `buildTimelineTrackRows`, `buildTimelineRulerAndKeyframes`, `handleTimelineMouseDown`, `handleTimelineMouseMove`, `handleTimelineMouseUp`) in `UIManager.cpp` implementiert. "Timeline"/"Hide Timeline"-Toggle-Button in Editor-Toolbar wiederhergestellt. `bottomWidgetId`-Initialisierung in `openWidgetEditorPopup` ergänzt. Neues `Widget::findAnimationByNameMutable()` in `UIWidget.h`/`UIWidget.cpp` hinzugefügt (öffentliche mutable Überladung von `findAnimationByName`).
- ✅ `UIManager`: Timeline-Keyframe-Anzeige-Fix – `buildTimelineRulerAndKeyframes` überarbeitet: Keyframe-Diamanten (◆) werden jetzt auf der Timeline-Ruler-Seite (rechts) als positionierte Elemente innerhalb von Track-Lanes angezeigt (from/to-Positionierung bei `time/duration`-Fraktion). Jeder Track erhält eine eigene Keyframe-Lane (Panel, 20px) die mit den Track-Tree-Rows (links) aligniert ist. Spacer-Lanes für expandierte Keyframe-Detail-Rows und "+Keyframe"-Rows halten die Ausrichtung konsistent. Scrubber/End-Linie als Overlay mit `HitTestMode::DisabledAll`.
- ✅ `UIManager`: Editierbare Keyframes – In `buildTimelineTrackRows` sind die expandierten Keyframe-Zeilen jetzt interaktiv: Time- und Value-Felder verwenden `EntryBar`-Elemente (statt read-only Text), sodass Werte direkt inline bearbeitet werden können. `onValueChanged`-Callbacks aktualisieren `AnimationKeyframe::time` bzw. `AnimationKeyframe::value.x`, sortieren Keyframes nach Zeit und refreshen die Timeline. Zusätzlich pro Keyframe-Zeile ein ×-Delete-Button (`onClicked` entfernt den Keyframe aus dem Track).
- ✅ `Editor Theme System`: Zentralisiertes Theme-System für einheitliches Editor-Design eingeführt. Neues `EditorTheme.h` (Singleton) definiert alle Editor-UI-Farben (Window/Chrome, Panels, Buttons, Text, Input, Accent/Selection, Modal, Toast, Scrollbar, TreeView, ContentBrowser, Timeline, StatusBar), Fonts (`fontDefault`, 6 Font-Sizes von Caption 10px bis Heading 16px), und Spacing (Row-Heights, Paddings, Indent/Icon-Sizes). `EditorUIBuilder.h/.cpp` bietet 17+ statische Factory-Methoden (`makeLabel`, `makeButton`, `makePrimaryButton`, `makeDangerButton`, `makeSubtleButton`, `makeEntryBar`, `makeCheckBox`, `makeDropDown`, `makeFloatRow`, `makeVec3Row`, `makeSection`, etc.) die konsistent gestylte `WidgetElement`-Objekte erzeugen. Separates `ViewportUITheme.h` für anpassbares Gameplay-/Viewport-UI-Styling (Runtime-Theme, unabhängig vom Editor-Look). `ViewportUIManager` exponiert per-Instanz `getTheme()`/`const getTheme()`. Systematischer Umbau in `UIManager.cpp` (~13.000 Zeilen): Alle Editor-UI-Lambdas (`makeTextLine`, `sanitizeId`, `addSeparator`, `fmtF`, `makeFloatEntry`, `makeVec3Row`, `makeCheckBoxRow`), alle 3 Modal-Dialoge, Toast-Benachrichtigungen, Content-Browser (TreeRows, GridTiles, PathBar, Breadcrumbs), Outliner-Buttons, alle DropDown-/DropdownButton-Widgets, Project-Screen, Widget-Editor (Toolbar, Controls, Hierarchy, Details, Timeline mit Track-Headers/Keyframe-Rows/Ruler), UI-Designer (Toolbar, Controls, Hierarchy, Properties, Delete-Button) – jetzt durchgehend EditorTheme-Referenzen statt hardcoded Vec4/font/fontSize-Literale. `OpenGLRenderer.cpp`: Mesh-Viewer-Details-Panel (Title, Path, Stats, Transform/Material-Sections, Float-Rows, Entry-Bars) und TitleBar-Tab-Leiste (Tab-Buttons, Close-Buttons) auf EditorTheme umgestellt.
- ✅ `EditorWidget / GameplayWidget Trennung`: Architektonische Aufspaltung des UI-Widget-Systems in zwei separate Basisklassen. Neue `EditorWidget`-Klasse (`src/Renderer/EditorUI/EditorWidget.h`) – einfache, schlanke Basisklasse für alle Editor-UI-Widgets ohne `EngineObject`-Vererbung, ohne JSON-Serialisierung, ohne Animationssystem. Felder: name, sizePixels, positionPixels, anchor (WidgetAnchor), fillX/fillY, absolutePosition, computedSize/Position, layoutDirty, elements (vector<WidgetElement>), zOrder. Statische Factory `EditorWidget::fromWidget(shared_ptr<Widget>)` für Übergangskonvertierung. Neuer `GameplayWidget`-Alias (`src/Renderer/GameplayUI/GameplayWidget.h`) – `using GameplayWidget = Widget;` – behält alle Features (EngineObject, JSON, Animationen, Focus, DragDrop). `UIManager.h/.cpp`: `WidgetEntry` nutzt jetzt `shared_ptr<EditorWidget>`, duale `registerWidget`-Überladungen (EditorWidget primär + Widget-Transition via `fromWidget()`). Alle 17 `make_shared<Widget>()`-Aufrufe in UIManager.cpp durch `make_shared<EditorWidget>()` ersetzt. `ViewportUIManager.h/.cpp`: `WidgetEntry` nutzt `shared_ptr<GameplayWidget>` für volles Feature-Set im Gameplay-UI. `WidgetEditorState.editedWidget` bleibt `Widget` (bearbeitet Gameplay-Widgets). CMakeLists.txt um beide neuen Header erweitert.
- ✅ `Darker Modern Editor Theme`: Komplette Überarbeitung der EditorTheme-Farbpalette für dunkleres, moderneres Erscheinungsbild mit weißer Schrift. Alle ~60 Farbwerte in `EditorTheme.h` angepasst: Window/Chrome-Hintergründe auf 0.06–0.08 abgesenkt, Panel-Hintergründe auf 0.08–0.10, alle Textfarben auf 0.95 (nahezu reines Weiß) angehoben. Blaustich aus neutralen Hintergründen entfernt (rein neutrales Grau). Buttons, Inputs, Dropdowns, TreeView, ContentBrowser, Timeline, StatusBar proportional abgedunkelt. Akzentfarben (Selection, Hover) dezent angepasst für besseren Kontrast auf dunklem Hintergrund.
- ✅ `Editor Settings Popup`: Neues Editor-Settings-Popup erreichbar über Settings-Dropdown im ViewportOverlay (zwischen "Engine Settings" und "UI Designer"). `openEditorSettingsPopup()` in UIManager.h deklariert und in UIManager.cpp implementiert (~200 Zeilen). PopupWindow (480×380) mit dunklem Theme-Styling aus EditorTheme::Get(). Zwei Sektionen: **Font Sizes** (6 Einträge: Heading/Subheading/Body/Small/Caption/Monospace, Bereich 6–48px) und **Spacing** (5 Einträge: Row Height/Small/Large, Toolbar Height, Border Radius mit feldspezifischen Min/Max-Werten). Jeder Eintrag schreibt direkt in den EditorTheme-Singleton via Float-Pointer + ruft `markAllWidgetsDirty()` für sofortiges visuelles Feedback. Wertvalidierung mit try/catch auf std::stof.
- ✅ `UIManager`: Vollständige EditorTheme-Migration – Alle verbliebenen hardcoded `Vec4`-Farbliterale in `UIManager.cpp` durch `EditorTheme::Get().*`-Referenzen ersetzt. Betrifft: Engine-Settings-Popup (Checkbox-Hover/Fill, EntryBar-Text, Dropdown-Farben, Kategorie-Buttons), Projekt-Auswahl-Screen (Background, Titlebar, Sidebar, Footer, Projekt-Zeilen, Akzentbalken, Buttons, Checkboxen, RHI-Dropdown, Create-Button, Preview-Pfad), Content-Browser-Selektionsfarben (`treeRowSelected`, `cbTileSelected`). Insgesamt ~53 verschiedene Theme-Konstanten referenziert. Verbleibende hardcoded `Vec4{0,0,0,0}` sind transparente Strukturcontainer (funktional korrekt), Gameplay-Widget-Defaults in `addElementToEditedWidget` (bewusst eigenständig) und Timeline-Akzentfarben.
- ✅ `Editor Theme Serialization & Selection`: Vollständige Theme-Persistierung und Auswahl. `EditorTheme` um JSON-Serialisierung erweitert (`toJson()`/`fromJson()`, `saveToFile()`/`loadFromFile()`, `discoverThemes()`). Neue Methoden: `GetThemesDirectory()` (Editor/Themes/), `EnsureDefaultThemes()` (erstellt Dark.json + Light.json mit vollständigen Farbpaletten), `loadThemeByName()`, `saveActiveTheme()`. Default-Themes werden automatisch über `AssetManager::ensureDefaultAssetsCreated()` beim Projektladen erzeugt. Gespeichertes Theme wird beim Start aus `DiagnosticsManager` geladen (`EditorTheme` Key). Editor Settings Popup um Theme-Dropdown erweitert (Sektion "Theme" mit "Active Theme"-DropDown, zeigt alle .json-Dateien aus Editor/Themes/). Theme-Wechsel lädt neues Theme, persistiert Auswahl, schließt und öffnet Popup neu für sofortiges visuelles Feedback. Font-Size- und Spacing-Änderungen werden automatisch ins aktive Theme zurückgespeichert (`saveActiveTheme()`).
- ✅ `Full UI Rebuild on Theme Change`: Neue Methode `rebuildAllEditorUI()` in UIManager mit deferred Update-Mechanismus. Beim Theme-Wechsel wird ein `m_themeDirty`-Flag gesetzt; die eigentliche Aktualisierung erfolgt verzögert im nächsten Frame via `applyPendingThemeUpdate()` in `updateLayouts()`. Private Methode `applyPendingThemeUpdate()` ruft `applyThemeToAllEditorWidgets()` auf (rekursiver Farb-Walk über alle registrierten Editor-Widgets via `EditorTheme::ApplyThemeToElement`) und markiert alle Widgets dirty. Deferred-Ansatz verhindert Freeze/Crash bei synchroner UI-Rekonstruktion innerhalb von Dropdown-Callbacks.
- ✅ `Theme-Driven Editor Widget Styling`: Neue statische Methode `EditorTheme::ApplyThemeToElement(WidgetElement&, const EditorTheme&)` in `EditorTheme.h` – mappt jeden `WidgetElementType` auf die passenden Theme-Farben (color, hoverColor, pressedColor, textColor, borderColor, fillColor, font, fontSize). Spezialbehandlung: ColorPicker wird übersprungen (Benutzer-Daten), Image-Elemente behalten ihren Tint, intentional transparente Spacer (`alpha < 0.01`) bleiben transparent. ID-basierte Overrides: Close-Buttons → `buttonDanger`-Hover, Save-Buttons → `buttonPrimary`. Abdeckt alle ~25 Element-Typen (Panel, StackPanel, Grid, Button, ToggleButton, DropdownButton, Text, Label, EntryBar, CheckBox, RadioButton, DropDown, Slider, ProgressBar, Separator, ScrollView, TreeView, TabView, ListView, TileView, Spinner, Border, RichText, WrapBox, UniformGrid, SizeBox, ScaleBox, Overlay, WidgetSwitcher). Rekursive Anwendung auf Kind-Elemente. Neue Methode `UIManager::applyThemeToAllEditorWidgets()` iteriert über alle registrierten Editor-Widgets und ruft `ApplyThemeToElement` auf jedes Element auf. Integration in `applyPendingThemeUpdate()` – beim Theme-Wechsel werden alle Widgets (asset-basierte und dynamische) korrekt umgefärbt. Fallback in `loadThemeByName()`: Falls die Theme-Datei nicht existiert, wird auf den Dark-Theme-Default zurückgesetzt.
- ✅ `Theme Switch Crash Fix`: Vereinfachung des Theme-Wechsel-Flows zur Vermeidung von Crashes. `applyPendingThemeUpdate()` ruft keine `populate*`-Funktionen mehr auf (Outliner, Details, ContentBrowser, StatusBar wurden vorher mid-frame neu aufgebaut, was zu ungültigem Zustand führte). Stattdessen werden nur noch die Farben bestehender Elemente via `ApplyThemeToElement` aktualisiert. Theme-Dropdown-Callback im Editor-Settings-Popup schließt und öffnet das Popup nicht mehr neu (verursachte Crash innerhalb des eigenen Callbacks). Neuer Flow: `loadThemeByName()` → `rebuildAllEditorUI()` (setzt `m_themeDirty`) → nächster Frame: `applyThemeToAllEditorWidgets()` + `markAllWidgetsDirty()`.
- ✅ `Theme Update Bugfixes`: `applyThemeToAllEditorWidgets()` erfasst jetzt auch Dropdown-Menü-, Modal-Dialog- und Save-Progress-Widgets, die zuvor beim Theme-Wechsel unberücksichtigt blieben. Popup-Fenster (`renderPopupWindows()`) verwenden `EditorTheme::Get().windowBackground` für `glClearColor` statt hardcoded Farben. Mesh-Viewer-Details-Panel-Root nutzt `EditorTheme::Get().panelBackground`. `applyPendingThemeUpdate()` wird pro Frame auf Popup-UIManagern aufgerufen. `UIManager::applyPendingThemeUpdate()` von `private` auf `public` verschoben (benötigt vom Renderer für Popup-Kontext).
- ✅ `Dropdown Flip-Above Positionierung`: `showDropdownMenu()` prüft verfügbaren Platz unterhalb des Auslöser-Elements; reicht der Platz nicht, wird das Menü oberhalb positioniert (Flip-Above-Logik). Verhindert abgeschnittene Dropdown-Listen am unteren Fensterrand.
- ✅ `WidgetDetailSchema`: Schema-basierter Property-Editor (`WidgetDetailSchema.h/.cpp`) ersetzt ~1500 Zeilen manuellen Detail-Panel-Code in `UIManager.cpp`. Zentraler Einstiegspunkt `buildDetailPanel(prefix, selected, applyChange, rootPanel, options)` baut komplettes Detail-Panel für beliebiges `WidgetElement`. 9 Shared Sections (Identity, Transform, Anchor, Hit Test, Layout, Style/Colors, Brush, Render Transform, Shadow) + 12 per-type Sections (Text, Image, Value, EntryBar, Container, Border, Spinner, RichText, ListView, TileView, Focus, Drag & Drop) + optionaler Delete-Button. `Options`-Struct konfiguriert kontextspezifisches Verhalten (editierbare IDs, onIdRenamed, showDeleteButton, onDelete, onRefreshHierarchy). `refreshWidgetEditorDetails()` (~1060→75 Zeilen) und `refreshUIDesignerDetails()` (~420→99 Zeilen) nutzen jetzt ausschließlich `WidgetDetailSchema::buildDetailPanel()`.
- ✅ `DPI-Aware UI Scaling`: Neues `dpiScale`-Feld in `EditorTheme` mit `applyDpiScale(float)` Methode — skaliert alle Font-Größen, Row-Heights, Padding, Icon-Sizes, Border-Radius und Separator-Thickness relativ zum aktuellen DPI-Faktor. Beim Startup wird die DPI-Skalierung automatisch vom primären Monitor erkannt (`MonitorInfo::dpiScale` aus `HardwareInfo`). Gespeicherter Override (`UIScale` Key in `config.ini`) hat Vorrang. Theme-JSON-Dateien speichern immer DPI-unabhängige Basiswerte; `toJson()` dividiert durch `dpiScale`, `fromJson()` multipliziert beim Laden. `loadThemeByName()` und `loadFromFile()` bewahren den aktiven `dpiScale` über Theme-Wechsel hinweg. Editor Settings Popup um "UI Scale" Sektion erweitert mit Dropdown: Auto/100%/125%/150%/175%/200%/250%/300%. Änderungen werden sofort angewendet (`applyDpiScale` + `rebuildAllEditorUI`) und in `config.ini` persistiert.
- ✅ `DPI Scaling – Vollständige UI-Abdeckung`: Neue statische Hilfsmethoden `EditorTheme::Scaled(float)` und `EditorTheme::Scaled(Vec2)` für beliebige Pixelwert-Skalierung. Systematischer Umbau aller hardcoded Pixelwerte im gesamten Editor-UI: **UIManager.cpp** – alle 37 `fontSize`-Literale durch Theme-Felder ersetzt (`fontSizeHeading`/`Subheading`/`Body`/`Small`/`Caption`/`Monospace`); Engine-Settings-Popup (620×480), Editor-Settings-Popup (480×380), Projekt-Auswahl-Screen (720×540) und Landscape-Manager-Popup (420×340) – alle Popup-Dimensionen, Layout-Konstanten (Row-Heights, Label-Widths, Sidebar, Title-Heights, Paddings) via `Scaled()` skaliert; `measureElementSize()` – Slider-Defaults (140×18), Image-Defaults (24), Checkbox-Box/Gap (16/6), Dropdown-Arrow (16), DropdownButton-Arrow (12) und alle Fallback-FontSizes via `Scaled()` oder Theme-Werte. **main.cpp** – New-Material-Popup (460×400): Popup-Dimensionen, fontSize-Werte (15→Heading, 13→Body, 14→Subheading), minSize-Werte (20, 24) und Paddings skaliert. **OpenGLRenderer.cpp** – 15 hardcoded `minSize`-Werte in Mesh-/Material-Editor-Popups und Tab-Buttons skaliert. **UIWidgets** – `SeparatorWidget.h` (22px Header), `TabViewWidget.h` (26px Tab), `TreeViewWidget.h` (22px Row) via `Scaled()` skaliert. Normalisierte Popup-Layouts (nx/ny) nutzen Basis-Pixelwerte für korrekte proportionale Skalierung bei vergrößerten Popup-Fenstern.
- ✅ `OpenGLRenderer` / `OpenGLTexture`: Mipmaps systematisch aktiviert – `glGenerateMipmap` wird jetzt konsequent bei jedem GPU-Textur-Upload aufgerufen. Betrifft: `OpenGLTexture::initialize()` (bereits vorhanden), Skybox-Cubemap (`GL_LINEAR_MIPMAP_LINEAR` + `glGenerateMipmap(GL_TEXTURE_CUBE_MAP)`), UI-Textur-Cache (`GL_LINEAR_MIPMAP_LINEAR` + `glGenerateMipmap(GL_TEXTURE_2D)`). Framebuffer-/Shadow-/Depth-/Pick-Texturen bleiben ohne Mipmaps (korrekt, da 1:1 gesampelt). Reduziert Moiré/Flimmern bei entfernten Objekten und Skybox-Übergängen.
- ✅ `OpenGLRenderer`: GPU Instanced Rendering – Draw-Liste wird nach (Material-Pointer, Obj-Pointer) sortiert und in Batches gruppiert. Nur Objekte mit gleichem Mesh UND gleichem Material werden per `glDrawElementsInstanced`/`glDrawArraysInstanced` in einem Draw-Call gerendert. Model-Matrizen über SSBO (`layout(std430, binding=0)`, `GL_DYNAMIC_DRAW`) an Shader übergeben, per `gl_InstanceID` indiziert. `uniform bool uInstanced` schaltet zwischen SSBO- und Uniform-Pfad. Betrifft: Haupt-Render-Pass (`renderWorld`), reguläre Shadow Maps (`renderShadowMap`), Cascaded Shadow Maps (`renderCsmShadowMaps`). Emission-Objekte weiterhin einzeln (per-Entity Light Override). Einzelobjekt-Batches nutzen klassischen Non-Instanced-Pfad. `uploadInstanceData()` verwaltet SSBO mit automatischem Grow und erzwingt Buffer-Orphaning (`glBufferData(nullptr)` vor `glBufferSubData`) zur Vermeidung von GPU-Read/Write-Hazards. Nach jedem Instanced-Draw wird SSBO explizit entbunden (`glBindBufferBase(0,0)`) und `uInstanced` auf `false` zurückgesetzt — verhindert stale SSBO-State bei nachfolgenden Non-Instanced-Draws. Vertex-Shader nutzt `if/else` statt Ternary für Model-Matrix-Auswahl (verhindert spekulative SSBO-Zugriffe auf SIMD-GPUs). `releaseInstanceResources()` in `shutdown()` für Cleanup.
- ✅ `Texture Compression (S3TC/BC)`: DDS-Dateiformat-Unterstützung implementiert. Neuer `DDSLoader` (`DDSLoader.h/.cpp`) parst DDS-Header (Standard + DX10-Extended) und lädt Block-Compressed Mip-Chains. Unterstützte Formate: BC1 (DXT1), BC2 (DXT3), BC3 (DXT5), BC4 (ATI1/RGTC1), BC5 (ATI2/RGTC2), BC7 (BPTC). `Texture`-Klasse um `CompressedFormat`-Enum, `CompressedMipLevel`-Struct und `compressedBlockSize()`-Helper erweitert. `OpenGLTexture::initialize()` nutzt `glCompressedTexImage2D` für komprimierte Texturen (S3TC-Extension-Konstanten als Fallback). `.dds` als Import-Format registriert. `readAssetFromDisk` speichert `m_ddsPath` statt stbi_load. `RenderResourceManager` delegiert an `loadDDS()`. `RendererCapabilities` um `supportsTextureCompression` erweitert.
- ✅ `Runtime Texture Compression`: Unkomprimierte Texturen (PNG/JPG/TGA/BMP) können jetzt zur Laufzeit vom OpenGL-Treiber in S3TC/RGTC-Blockformate komprimiert werden. `Texture`-Klasse um `m_requestCompression`-Flag erweitert. `OpenGLTexture::initialize()` nutzt bei gesetztem Flag komprimierte `internalFormat`s (`GL_COMPRESSED_RGB_S3TC_DXT1_EXT` / `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` / `GL_COMPRESSED_RED_RGTC1` / `GL_COMPRESSED_RG_RGTC2`) mit normalem `glTexImage2D`-Aufruf — der Treiber übernimmt die Kompression beim Upload. Neuer Toggle: `Renderer::isTextureCompressionEnabled()` / `setTextureCompressionEnabled()` (virtual in `Renderer.h`, Override in `OpenGLRenderer`). `RenderResourceManager` liest `DiagnosticsManager::getState("TextureCompressionEnabled")` und setzt das Flag auf Texturen. Engine-Settings-Popup → Rendering → Performance: Checkbox „Texture Compression (S3TC)" hinzugefügt. Config-Persistenz über `config.ini` (`TextureCompressionEnabled`). Wirksam ab nächstem Level-Load (Texturen werden beim GPU-Upload komprimiert, nicht beim Import).
- ✅ `Level Loading via Content Browser`: Doppelklick auf ein Level-Asset (`.map`) im Content Browser löst einen vollständigen Level-Wechsel aus. Ablauf: (1) Unsaved-Changes-Dialog mit Checkbox-Liste aller ungespeicherten Assets (alle standardmäßig ausgewählt, einzeln abwählbar) — erscheint auch beim normalen Speichern (Ctrl+S, StatusBar.Save). (2) Rendering wird eingefroren (`Renderer::setRenderFrozen`) — letzter Frame bleibt sichtbar, UI bleibt interaktiv. (3) Modaler Lade-Fortschritt (`showLevelLoadProgress`). (4) `AssetManager::loadLevelAsset()` (jetzt public) lädt neues Level, `DiagnosticsManager::setActiveLevel()` setzt es aktiv, `setScenePrepared(false)` erzwingt Neuaufbau. (5) Editor-Kamera wird aus dem neuen Level wiederhergestellt, Skybox wird gesetzt. (6) Rendering wird fortgesetzt — `renderWorld()` erkennt das neue Level, ruft `prepareActiveLevel()` + `buildRenderablesForSchema()` auf. Neue APIs: `AssetManager::getUnsavedAssetList()` (liefert Name/Pfad/Typ/ID jedes ungespeicherten Assets), `AssetManager::saveSelectedAssetsAsync()` (selektives Speichern), `UIManager::showUnsavedChangesDialog()`, `UIManager::showLevelLoadProgress()`/`updateLevelLoadProgress()`/`closeLevelLoadProgress()`, `UIManager::setOnLevelLoadRequested()`, `Renderer::setRenderFrozen()`/`isRenderFrozen()`.
- ✅ `Texture Streaming`: Asynchrones Texture-Streaming implementiert. `TextureStreamingManager` (`TextureStreamingManager.h/.cpp`) im `OpenGLRenderer`-Verzeichnis: Background-Loader-Thread + GPU-Upload-Queue. Texturen werden sofort als 1×1 Magenta-Placeholder zurückgegeben; `processUploads()` lädt pro Frame bis zu 4 Texturen auf die GPU hoch. De-Duplikation über `m_streamCache`. `OpenGLMaterial::bindTextures()` nutzt den Streaming-Manager wenn verfügbar. Toggle: `Renderer::isTextureStreamingEnabled()`/`setTextureStreamingEnabled()`. Engine Settings → Rendering → Performance: Checkbox „Texture Streaming". Config-Persistenz über `config.ini` (`TextureStreamingEnabled`).
- ✅ `Console / Log-Viewer Tab`: Dedizierter Editor-Tab für Live-Log-Output (Editor Roadmap 2.1). `openConsoleTab()`/`closeConsoleTab()`/`isConsoleOpen()` in `UIManager`. `ConsoleState`-Struct (tabId, widgetId, levelFilter-Bitmask, searchText, autoScroll, refreshTimer). Toolbar mit Filter-Buttons (All/Info/Warning/Error als Toggle-Bitmask), Suchfeld (Echtzeit-Textfilter, case-insensitive), Clear-Button, Auto-Scroll-Toggle. Scrollbare Log-Area zeigt farbcodierte Einträge aus dem Logger-Ringbuffer (INFO=textSecondary, WARNING=warningColor, ERROR=errorColor, FATAL=rot). `refreshConsoleLog()` baut Log-Zeilen mit Timestamp, Level-Tag, Category und Message. Periodischer Refresh alle 0.5s in `updateNotifications()` (nur bei neuen Einträgen via sequenceId-Vergleich). Erreichbar über Settings-Dropdown → „Console".
- ✅ `Viewport-Einstellungen Panel (Editor Roadmap 5.1)`: Toolbar-Buttons funktional gemacht. **CamSpeed-Dropdown**: Klick auf CamSpeed-Button öffnet Dropdown mit 7 Geschwindigkeitsvoreinstellungen (0.25x–5.0x), aktuelle Geschwindigkeit mit „>" markiert, Button-Label wird bei Auswahl und Mausrad-Scroll aktualisiert. **Stats-Toggle**: Klick schaltet Performance-Metriken-Overlay ein/aus, aktiver Zustand mit weißem Text, inaktiv mit gedimmtem Grau (0.45). **Grid-Snap-Toggle**: Klick schaltet Grid-Snap ein/aus mit visueller Rückmeldung (weiß/gedimmt) und Toast-Bestätigung. **Settings-Dropdown**: Einträge Engine Settings, Editor Settings, Console. **Focus on Selection (F-Taste)**: Neue `Renderer::focusOnSelectedEntity()` Methode (virtual mit Impl in OpenGLRenderer) – berechnet AABB-Center der selektierten Entity, positioniert Kamera per Smooth-Transition (0.3s) in 2.5× Radius-Entfernung aus aktueller Blickrichtung. F-Taste im Gizmo-Shortcut-Block (W/E/R/F, nur Editor-Modus, nicht bei RMB/PIE/Texteingabe).
- ✅ `Intelligent Snap & Grid (Editor Roadmap 3.6)`: Vollständiges Snap-to-Grid-System für Gizmo-Operationen. **Grid-Overlay**: SDF-basierter Infinite-Grid-Shader auf der XZ-Ebene mit `fwidth()`-Antialiasing, Achsen-Highlighting (rot=X, blau=Z), 10er-Verstärkungslinien, Distance-Fade (200 Einheiten). Grid-Quad 500×500, gerendert nach Post-Processing, vor Gizmo/Outline. **Snap-to-Grid**: Translate rastet alle Positionskomponenten auf `gridSize`-Vielfache, Rotate rastet Euler-Winkel auf 15°-Schritte, Scale rastet auf 0.1-Schritte. **Toolbar-Integration**: `ViewportOverlay.Snap`-Toggle schaltet Snap + Grid gemeinsam ein/aus. Neuer `ViewportOverlay.GridSize`-Dropdown mit 6 Presets (0.25/0.5/1/2/5/10 Einheiten). **Persistenz**: Snap-Zustand und Grid-Größe über `DiagnosticsManager::setState/getState` in config.ini gespeichert, beim Start wiederhergestellt. Snap-State-Felder (`m_snapEnabled`, `m_gridVisible`, `m_gridSize`, `m_rotationSnapDeg`, `m_scaleSnapStep`) in `Renderer`-Basisklasse mit virtuellen Gettern/Settern. Grid-Rendering-Ressourcen (`ensureGridResources`/`releaseGridResources`/`drawViewportGrid`) in OpenGLRenderer.

- ✅ `Asset Thumbnails (Editor Roadmap 4.1)`: FBO-gerenderte Thumbnails für Model3D- und Material-Assets im Content Browser. `Renderer::generateAssetThumbnail(assetPath, assetType)` virtuelles Interface, `OpenGLRenderer`-Implementierung rendert 128×128 FBO mit Auto-Kamera (AABB-Framing), Directional Light, Material/PBR-Support. Material-Preview nutzt prozedural erzeugte UV-Sphere (16×12). Thumbnail-Cache (`m_thumbnailCache`) verhindert redundantes Rendering. `ensureThumbnailFbo()`/`releaseThumbnailFbo()` verwalten GL-Ressourcen. Content-Browser-Grid (`populateContentBrowserWidget`) zeigt Thumbnails für Model3D, Material und Texture-Assets — Texturen via `preloadUITexture()`, 3D-Modelle und Materialien via `generateAssetThumbnail()`.
- ✅ `Build Pipeline – Clean Output Directory`: Vor dem Deploy-Schritt wird das Output-Verzeichnis vollständig geleert (`std::filesystem::remove_all()` + `create_directories()`), sodass keine veralteten Dateien aus vorherigen Builds verbleiben. Fehlerbehandlung bei fehlgeschlagener Bereinigung mit Abbruch der Build-Pipeline. Log-Ausgabe „Cleaned output directory." bei Erfolg.

## Legende

| Symbol | Bedeutung                          |
|--------|------------------------------------|
| ✅     | Vollständig implementiert          |
| 🟡     | Teilweise implementiert / Lücken   |
| ❌     | Noch nicht implementiert / geplant |

---

## Inhaltsverzeichnis

1. [Logger](#1-logger)
2. [Diagnostics Manager](#2-diagnostics-manager)
3. [Asset Manager](#3-asset-manager)
4. [Core – MathTypes](#4-core--mathtypes)
5. [Core – EngineObject / AssetData](#5-core--engineobject--assetdata)
6. [Core – EngineLevel](#6-core--enginelevel)
7. [Core – ECS](#7-core--ecs)
8. [Core – AudioManager](#8-core--audiomanager)
9. [Renderer – OpenGL](#9-renderer--opengl)
10. [Renderer – Kamera](#10-renderer--kamera)
11. [Renderer – Shader-System](#11-renderer--shader-system)
12. [Renderer – Material-System](#12-renderer--material-system)
13. [Renderer – Texturen](#13-renderer--texturen)
14. [Renderer – 2D-/3D-Objekte](#14-renderer--2d3d-objekte)
15. [Renderer – Text-Rendering](#15-renderer--text-rendering)
16. [Renderer – RenderResourceManager](#16-renderer--renderresourcemanager)
17. [UI-System](#17-ui-system)
18. [Scripting (Python)](#18-scripting-python)
19. [Build-System](#19-build-system)
20. [Gesamtübersicht fehlender Systeme](#20-gesamtübersicht-fehlender-systeme)
21. [Multi-Window / Popup-System](#21-multi-window--popup-system)
22. [Landscape-System](#22-landscape-system)
23. [Skybox-System](#23-skybox-system)
24. [Physik-System](#24-physik-system)
25. [Editor-Fenster / Mesh Viewer](#25-editor-fenster--mesh-viewer)
26. [Viewport UI System](#26-viewport-ui-system)

---

## 1. Logger

| Feature                          | Status |
|----------------------------------|--------|
| Singleton-Architektur            | ✅     |
| Datei-Logging (Logs-Verzeichnis) | ✅     |
| Konsolen-Logging (stdout)        | ✅     |
| Log-Level (INFO/WARNING/ERROR/FATAL) | ✅ |
| 10 Kategorien (General, Engine, …) | ✅   |
| Thread-Sicherheit (Mutex)        | ✅     |
| Fehler-Tracking (hasErrors etc.) | ✅     |
| Log-Datei bei Fehler automatisch öffnen | ✅ |
| Zeitstempel-basierte Dateinamen  | ✅     |
| Log-Retention (max. 5 Log-Dateien) | ✅  |
| Crash-Safe Flush (sofortiges Flushen) | ✅ |
| Crash-Handler (SEH + Stack-Trace + std::terminate) | ✅ |
| Out-of-Process CrashHandler (CrashHandler.exe)    | ✅ |
| Remote-/Netzwerk-Logging         | ❌     |

**Offene Punkte:**
- Kein Netzwerk-/Remote-Logging

---

## 2. Diagnostics Manager

| Feature                              | Status |
|--------------------------------------|--------|
| Singleton + Key-Value-States         | ✅     |
| Config-Persistierung (config.ini)    | ✅     |
| Projekt-Config (defaults.ini)        | ✅     |
| RHI-Auswahl (Enum: OpenGL/DX11/DX12)| 🟡     |
| Fenster-Konfiguration (Größe, Zustand)| ✅    |
| PIE-Modus (Play In Editor)           | ✅     |
| PIE Maus-Capture + Shift+F1 Pause + Cursor-Restore + Window-Grab + UI-Blocking | ✅     |
| Aktives Level verwalten (`setActiveLevel` / `getActiveLevelSoft` / `swapActiveLevel`) | ✅ |
| Token-basierte Level-Changed-Callbacks (register/unregister) | ✅ |
| Action-Tracking (Loading, Saving…)   | ✅     |
| Input-Dispatch (KeyDown/KeyUp)       | ✅     |
| Benachrichtigungen (Modal + Toast)   | ✅     |
| Shutdown-Request                     | ✅     |
| Engine Settings: Laptop-Modus        | ✅     |
| Known Projects Liste (max. 20, config.ini) | ✅ |
| Default-Startup-Projekt (config.ini) | ✅     |
| Projekt-Auswahl-Screen (Recent/Open/New) | ✅ |
| Hardware-Diagnostics (CPU/GPU/RAM/VRAM/Monitor) | ✅ |
| DPI-Aware UI Scaling (Auto-Detect + Manual Override) | ✅ |

**Offene Punkte:**
- RHI-Auswahl existiert als Enum, aber nur OpenGL ist tatsächlich implementiert (DirectX 11/12 nicht vorhanden)

---

## 3. Asset Manager

| Feature                                   | Status |
|-------------------------------------------|--------|
| Singleton-Architektur                     | ✅     |
| Sync- und Async-Laden                    | ✅     |
| Thread-Pool (hardware_concurrency Threads, globale Job-Queue) | ✅ |
| Asset-Registry (binär, schnelle Suche)   | ✅     |
| Discovery (Content-Verzeichnis scannen)  | ✅     |
| Discovery: Script-Dateien (.py)          | ✅     |
| Discovery: Audio-Dateien (.wav/.mp3/.ogg/.flac) | ✅     |
| Laden: Textur (PNG, TGA, JPG, BMP)      | ✅     |
| Laden: Audio (WAV)                       | ✅     |
| Laden: Material                          | ✅     |
| Laden: Level                             | ✅     |
| Laden: Widget                            | ✅     |
| Laden: Script                            | ✅     |
| Laden: Shader                            | ✅     |
| Laden: Model2D                           | ✅     |
| Laden: Model3D                           | 🟡     |
| Import-Dialog (SDL_ShowOpenFileDialog)   | ✅     |
| Import: Texturen                         | ✅     |
| Import: Audio (WAV)                      | ✅     |
| Import: 3D-Modelle (Assimp: OBJ, FBX, glTF, DAE, etc.) | ✅ |
| Import: 3D-Modell Material-Extraktion (Diffuse/Specular/Normal) | ✅ |
| Import: 3D-Modell Textur-Extraktion (extern + eingebettet) | ✅ |
| Import: Mesh-basierte Benennung (MeshName_Diffuse, MeshName_Material) | ✅ |
| Import: Detailliertes Scene-Logging (Meshes, Materials, Texturen pro Typ) | ✅ |
| Auto-Material bei Mesh-Hinzufügung (Viewport/Outliner/Details) | ✅ |
| Viewport-Sofortupdate bei Mesh/Material-Änderung (setComponent + invalidateEntity) | ✅ |
| Referenz-Reparatur vor RRM-Prepare (fehlende Meshes entfernen, fehlende Materialien → WorldGrid) | ✅ |
| Assimp-Integration (static in AssetManager, nur Editor-Build) | ✅ |
| Runtime-Minimierung: AssetManagerRuntime/ScriptingRuntime/LandscapeRuntime ohne assimp | ✅ |
| Import: Shader-Dateien (.glsl)             | ✅     |
| Import: Scripts (.py)                      | ✅     |
| Speichern (Typ-spezifisch)              | ✅     |
| Asset-Header (binär v2 + JSON-Fallback) | ✅     |
| Garbage Collector (weak_ptr Tracking)    | ✅     |
| Projekt-Verwaltung (load/save/create)    | ✅     |
| Editor-Widgets automatisch erzeugen     | ✅     |
| stb_image Integration                    | ✅     |
| Pfad-Auflösung (Content + Editor)       | ✅     |
| O(1)-Asset-Lookup (m_loadedAssetsByPath Hash-Index) | ✅ |
| Paralleles Batch-Laden (readAssetFromDisk + std::async) | ✅ |
| Disk-I/O / CPU-Processing von Shared-State getrennt | ✅ |
| Level-Preload (preloadLevelAssets: Mesh+Material+Textur parallel) | ✅ |
| Registry-Save-Suppression (m_suppressRegistrySave bei Discovery) | ✅ |
| engine.pyi statisch deployed (CMake post-build + fs::copy_file) | ✅ |
| Single-Open Asset-Discovery (readAssetHeader 1× pro Datei) | ✅ |
| Asset-Thumbnails / Vorschaubilder       | ❌     |
| Asset-Versionierung                      | ❌     |
| Hot-Reload (Dateiänderung erkennen)     | ❌     |

**Offene Punkte:**
- Keine Thumbnail-Generierung für Asset-Browser
- Kein Hot-Reload bei externer Dateiänderung

---

## 4. Core – MathTypes

| Feature                              | Status |
|--------------------------------------|--------|
| Vec2, Vec3, Vec4                     | ✅     |
| Mat3, Mat4 (mit transpose)           | ✅     |
| Transform (TRS)                      | ✅     |
| Euler-Rotation (XYZ-Ordnung)         | ✅     |
| Column-Major / Row-Major Export      | ✅     |
| JSON-Serialisierung (nlohmann)       | ✅     |
| Quaternion-Unterstützung (via engine.math Python-API) | ✅ |
| Mathe-Operatoren (via engine.math Python-API: +, -, *, /) | ✅ |
| Interpolation (Lerp, Slerp via engine.math Python-API) | ✅ |

**Offene Punkte:**
- C++-Structs selbst haben keine Operatoren (GLM wird intern genutzt)
- Quaternion, Operatoren und Interpolation sind über `engine.math` Python-API verfügbar (Berechnung in C++)

---

## 5. Core – EngineObject / AssetData

| Feature                       | Status |
|-------------------------------|--------|
| EngineObject Basisklasse      | ✅     |
| Pfad, Name, Typ, Transform   | ✅     |
| isSaved-Flag                  | ✅     |
| Virtuelle render()-Methode    | ✅     |
| AssetData (ID + JSON-Daten)   | ✅     |

**Keine offenen Punkte** – vollständig für den aktuellen Anwendungsfall.

---

## 6. Core – EngineLevel

| Feature                                  | Status |
|------------------------------------------|--------|
| Level-Daten (JSON-basiert)               | ✅     |
| ECS-Vorbereitung (prepareEcs)           | ✅     |
| Entity-Serialisierung (JSON ↔ ECS)      | ✅     |
| Alle 10 Komponentenarten serialisierbar (inkl. HeightFieldComponent) | ✅ |
| LodComponent (LOD-Stufen pro Entity, Distance-Thresholds)           | ✅ |
| Script-Entity-Cache                      | ✅     |
| Objekt-Registrierung + Gruppen          | ✅     |
| Instancing (enable/disable)             | ✅     |
| Snapshot/Restore (PIE-Modus)            | ✅     |
| `resetPreparedState()` (ECS-Reset für Level-Swap) | ✅ |
| Entity-Liste Callbacks                   | ✅     |
| Level-Script-Pfad                       | ✅     |
| Multi-Level-Verwaltung (Level wechseln) | 🟡     |
| Level-Streaming                          | ❌     |

**Offene Punkte:**
- Grundlegendes Level-Wechseln funktioniert (aktives Level setzen), aber kein nahtloses Streaming
- Kein Level-Streaming (Teilbereiche laden/entladen)

---

## 7. Core – ECS

| Feature                                 | Status |
|-----------------------------------------|--------|
| Entity-Erzeugung / -Löschung           | ✅     |
| 12 Komponentenarten                    | ✅     |
| SparseSet-Speicherung (O(1)-Zugriff)   | ✅     |
| Schema-basierte Abfragen               | ✅     |
| Bitmasken-System                        | ✅     |
| Max. 10.000 Entitäten                  | ✅     |
| TransformComponent                      | ✅     |
| MeshComponent                           | ✅     |
| MaterialComponent                       | ✅     |
| LightComponent (Point/Dir/Spot)        | ✅     |
| CameraComponent                         | ✅     |
| PhysicsComponent (vollständig: Collider, Mass, Restitution, Friction, Velocity, AngularVelocity, ColliderSize) | ✅     |
| ScriptComponent                         | ✅     |
| NameComponent                           | ✅     |
| CollisionComponent (Box/Sphere/Capsule/Cylinder/HeightField) | ✅ |
| HeightFieldComponent (Höhendaten, Skalierung, Offsets) | ✅ |
| LodComponent (LOD-Stufen pro Entity)   | ✅     |
| AnimationComponent (Skeletal Animation State) | ✅ |
| Dirty-Flagging (m_componentVersion)     | ✅     |
| Physik-Simulation (Kollision, Dynamik) | ✅     |
| Hierarchie (Parent-Child-Entities)     | ❌     |
| Entity-Recycling / Freelist            | ❌     |
| Parallele Iteration                     | ❌     |

**Offene Punkte:**
- **CameraComponent**: FOV, Near/Far-Clip und `isActive`-Flag. Wird als aktive View-Kamera genutzt wenn eine Entity-Kamera im Renderer gesetzt ist (`setActiveCameraEntity`). View- und Projection-Matrix werden aus TransformComponent + CameraComponent berechnet.
- Keine Parent-Child-Entity-Hierarchie (alle Entities sind flach)
- Kein Entity-Recycling (gelöschte IDs werden nicht wiederverwendet)
- Keine parallele/multi-threaded ECS-Iteration

---

## 8. Core – AudioManager

| Feature                                 | Status |
|-----------------------------------------|--------|
| Singleton + OpenAL-Backend              | ✅     |
| Device/Context-Verwaltung               | ✅     |
| Sync-Erstellung (createAudioHandle)    | ✅     |
| Async-Laden (Background-Thread)        | ✅     |
| Play / Pause / Stop / Gain             | ✅     |
| Buffer-Caching (pro Asset-ID)          | ✅     |
| Source-Cleanup (fertige Sources)        | ✅     |
| Callback-basierte Asset-Auflösung      | ✅     |
| WAV-Format                              | ✅     |
| OGG/MP3/FLAC-Format                    | ❌     |
| 3D-Audio (Positional Audio)            | ❌     |
| Audio-Effekte (Reverb, Echo)           | ❌     |
| Audio-Mixer / Kanäle                   | ❌     |
| Streaming (große Dateien)              | ❌     |

**Offene Punkte:**
- Nur WAV-Format unterstützt – kein OGG, MP3, FLAC
- OpenAL ist eingerichtet, aber 3D-Positionierung (Listener/Source-Position) wird nicht aktiv genutzt
- Keine Audio-Effekte
- Kein Audio-Mixer / Kanal-System
- Kein Streaming für große Audiodateien (alles im Speicher)

---

## 9. Renderer – OpenGL

| Feature                                    | Status |
|--------------------------------------------|--------|
| SDL3-Fenster (borderless, resizable)       | ✅     |
| OpenGL 4.6 Core-Kontext                    | ✅     |
| GLAD-Loader                                | ✅     |
| Render-Pipeline (render → present, kein redundantes Clear) | ✅     |
| Default-Framebuffer-Clear vor Tab-FBO-Blit                 | ✅     |
| Welt-Rendering (3D-Objekte)               | ✅     |
| UI-Rendering (FBO-cached, Dirty-Flag)     | ✅     |
| Tab-FBO Hardware-Blit (glBlitFramebuffer) | ✅     |
| Pick-Buffer nur bei Bedarf (On-Demand)    | ✅     |
| Fenster-Größe gecacht (1x SDL-Call/Frame) | ✅     |
| sceneLights als Member (keine Heap-Alloc) | ✅     |
| Frustum Culling (AABB + Sphere)           | ✅     |
| HZB Occlusion Culling (Mip-Pyramid)      | ✅     |
| PBO-basierter Async-Readback              | ✅     |
| Entity-Picking (Pick-FBO + Farbcodierung) | ✅     |
| Entity-Löschen (Entf-Taste + Undo/Redo)          | ✅     |
| **Multi-Select** (Ctrl+Klick Toggle, `m_selectedEntities` unordered_set) | ✅ |
| **Gruppen-Gizmo** (Translate/Rotate/Scale aller selektierten Entities gleichzeitig) | ✅ |
| **Gruppen-Löschen** (DELETE löscht alle selektierten Entities, Gruppen-Undo/Redo) | ✅ |
| **Gruppen-Undo/Redo** (Einzelner Undo-Command für Multi-Entity-Transform/Delete) | ✅ |
| **Rubber-Band-Selection** (Marquee-Rechteck im Viewport aufziehen → alle Entities im Bereich selektieren, Ctrl+Drag für additive Selektion, Fallback auf Einzel-Pick bei kleinem Rect) | ✅ |
| **Prefab / Entity Templates** (AssetType::Prefab, Save as Prefab, Drag & Drop Spawn, Doppelklick Spawn, „+ Entity"-Dropdown mit 7 Built-in-Templates, Undo/Redo) | ✅ |
| Screen-to-World (Depth-Buffer Unproject)  | ✅     |
| Selection-Outline (Edge-Detection, Multi-Entity) | ✅     |
| GPU Timer Queries (Triple-Buffered)       | ✅     |
| CPU-Metriken (Welt/UI/Layout/Draw/ECS)   | ✅     |
| Metriken-Overlay (F10)                    | ✅     |
| Occlusion-Stats (F9)                      | ✅     |
| Bounds-Debug (F8)                         | ✅     |
| HeightField Debug Wireframe (Engine Settings) | ✅     |
| UI-Debug-Rahmen (F11)                     | ✅     |
| FPS-Cap (F12)                             | ✅     |
| Custom Window Hit-Test (Resize/Drag, konfigurierbarer Button-Bereich links/rechts) | ✅     |
| Fenster erst nach Konsolen-Schließung sichtbar (Hidden → FreeConsole → ShowWindow) | ✅     |
| Beleuchtung (bis 8 Lichtquellen)          | ✅     |
| Sortierung + Batch-Rendering              | ✅     |
| Shader-Pfad-Cache (statisch, kein FS-Check pro prepare) | ✅ |
| Model-Matrix-Berechnung dedupliziert (shared Lambda) | ✅ |
| Cached Active Tab (m_cachedActiveTab, kein linearer Scan) | ✅ |
| Projection Guard (Rebuild nur bei Größenänderung) | ✅ |
| Viewport-Content-Rect-basierte Projektion (keine Verzerrung) | ✅ |
| Toter Code entfernt (isRenderEntryRelevant) | ✅ |
| Shadow Mapping (Multi-Light, Directional/Spot) | ✅     |
| Shadow Mapping (Point Light Cube Maps)      | ✅     |
| Post-Processing (Bloom, SSAO, HDR)       | ✅     |
| Anti-Aliasing (FXAA, MSAA 2x/4x)        | ✅     |
| Transparenz / OIT (Weighted Blended)      | ✅     |
| Instanced Rendering (GPU)                | ✅     |
| LOD-System (Level of Detail)             | ✅     |
| Debug Render Modes (Lit/Unlit/Wireframe/ShadowMap/Cascades/InstanceGroups/Normals/Depth/Overdraw) | ✅ |
| Skeletal Animation Rendering              | ✅     |
| Particle-Rendering (Point-Sprite, CPU-Sim) | ✅    |
| DirectX 11 Backend                        | ❌     |
| DirectX 12 Backend                        | ❌     |
| Vulkan Backend                            | ❌     |
| **Renderer-Abstrahierung (Multi-Backend-Vorbereitung)** | 🟡 |

**Offene Punkte:**
- Post-Processing Pipeline vollständig: HDR FBO, Gamma Correction, ACES Tone Mapping, FXAA 3.11 Quality (9-Sample, Edge Walking, Subpixel Correction), MSAA 2x/4x, Bloom (5-Mip Downsample + Gaussian Blur), SSAO (32-Sample Hemisphere Kernel, Half-Res, Bilateral Depth-Aware 5×5 Blur).
- Weighted Blended OIT (McGuire & Bavoil 2013): Auto-Detect transparenter Objekte (RGBA 4-Kanal Diffuse-Textur), separater OIT-Pass mit RGBA16F Accumulation + R8 Revealage FBO, Per-Attachment-Blending (`glBlendFunci`), Depth-Blit vom HDR-FBO, Fullscreen-Composite. Toggle über `setOitEnabled()`
- Instancing existiert auf CPU-/Level-Seite und GPU-Seite (SSBO-basiertes Instanced Rendering für Haupt-Render, Shadow Maps, CSM). Batching nur bei gleichem Mesh UND gleichem Material (Obj-Pointer-Check verhindert falsche Gruppierung unterschiedlicher Meshes). Buffer-Orphaning, SSBO-Cleanup nach Draws und `if/else`-Shader-Guard gegen Flicker implementiert
- Debug Render Modes: 9 Modi (Lit, Unlit, Wireframe, Shadow Map, Shadow Cascades, Instance Groups, Normals, Depth, Overdraw) über Viewport-Toolbar-Dropdown umschaltbar. Uniform-basiertes Shader-Branching in `fragment.glsl` und `grid_fragment.glsl` (`uDebugMode`), pro Modus spezifische Render-Konfiguration (Shadow-Pass-Skip, Wireframe-Polygon-Mode, Overdraw-Additiv-Blending, HSL-Batch-Einfärbung für Instance Groups). Depth-Visualisierung mit logarithmischem Mapping (`log2`) für gleichmäßige Verteilung. Shadow Cascades färbt alle Objekte inkl. Landscape nach Kaskaden-Zugehörigkeit ein.
- Keine Alternative zu OpenGL (DirectX / Vulkan nicht implementiert, nur als Enum-Placeholder)
CMake-Targets konsolidiert: `RendererCore` (OBJECT-Lib, abstrakte Schicht) eingebettet in `Renderer` (SHARED, Renderer.dll). Noch zu entkoppeln: `main.cpp` (direkte Instanziierung).
- **Schritt 1.1 erledigt:** GLM von `src/Renderer/OpenGLRenderer/glm/` nach `external/glm/` verschoben. Include-Pfad `${CMAKE_SOURCE_DIR}/external` als PUBLIC in `src/Renderer/CMakeLists.txt` hinzugefügt. Build verifiziert ✅.
- **Schritt 1.2 erledigt:** 5 abstrakte Render-Ressourcen-Interfaces erstellt: `IRenderObject2D`, `IRenderObject3D`, `ITextRenderer`, `IShaderProgram`, `ITexture`. OpenGL-Klassen erben jeweils davon. Build verifiziert ✅.
- **Schritt 2.1 erledigt:**
- **Schritt 2.2 erledigt:** `Renderer.h` von ~36 auf ~130 Zeilen erweitert mit ~60 virtuellen Methoden. GizmoMode/GizmoAxis Enums in Renderer definiert. OpenGLRenderer: ~45 Methoden mit `override` markiert, `getCapabilities()` implementiert (alle Caps = true). Build verifiziert ✅.
- **Schritt 3.1 erledigt:** `UIManager` vollständig von `OpenGLRenderer*` auf `Renderer*` umgestellt. Kein `#include "OpenGLRenderer.h"` mehr in UIManager.h/.cpp — nur noch `Renderer.h`. Alle Aufrufe nutzen das abstrakte Interface. Build verifiziert ✅.
- **Schritt 1.3 erledigt:** `RenderResourceManager` öffentliche API auf abstrakte Typen umgestellt: `getOrCreateObject2D/3D()` → `shared_ptr<IRenderObject2D/3D>`, `prepareTextRenderer()` → `shared_ptr<ITextRenderer>`, `RenderableAsset` Struct mit abstrakten Interfaces, Caches auf abstrakte `weak_ptr`. `OpenGLRenderer.cpp` nutzt `std::static_pointer_cast` für GL-spezifische Methoden. Build verifiziert ✅.
- **Schritt 3.2 erledigt:** `MeshViewerWindow` von `OpenGLObject3D` auf `IRenderObject3D` umgestellt. Kein OpenGL-Include mehr in MeshViewerWindow.h/.cpp. Alle verwendeten Methoden (`hasLocalBounds`, `getLocalBoundsMin/Max`, `getVertexCount`, `getIndexCount`) sind im abstrakten Interface definiert — kein Cast nötig. Build verifiziert ✅.
- **Schritt 2.3 erledigt:** `PopupWindow` abstrahiert: `SDL_GLContext` → `IRenderContext` Interface. `IRenderContext.h` (abstract) und `OpenGLRenderContext.h` (OpenGL-Impl.) erstellt. `PopupWindow::create()` nimmt `SDL_WindowFlags` + `unique_ptr<IRenderContext>`. Kein GL-Code mehr in PopupWindow.h/.cpp. Build verifiziert ✅.
- **Schritt 2.4 erledigt:** `SplashWindow` abstrahiert: Konvertiert zur abstrakten Basisklasse mit 6 reinen virtuellen Methoden. `OpenGLSplashWindow.h/.cpp` erstellt mit kompletter GL-Implementierung (~390 Zeilen, Inline-GLSL-Shader, FreeType-Glyph-Atlas, VAOs/VBOs). Alte `SplashWindow.cpp` gelöscht. `main.cpp` nutzt `OpenGLSplashWindow` direkt. Build verifiziert ✅.
- **Schritt 5.1 erledigt:** `EditorTab` FBO-Abstraktion: `IRenderTarget.h` (11 reine virtuelle Methoden: resize, bind, unbind, destroy, isValid, getWidth/Height, getColorTextureId, takeSnapshot, hasSnapshot, getSnapshotTextureId) und `OpenGLRenderTarget.h/.cpp` (~100 Zeilen GL-FBO-Implementierung) erstellt. `EditorTab`-Struct von 7 GL-spezifischen Feldern auf `unique_ptr<IRenderTarget> renderTarget` reduziert. 12+ Zugriffsstellen in `OpenGLRenderer.cpp` aktualisiert. `ensureTabFbo`/`releaseTabFbo`/`snapshotTabBeforeSwitch` (~100 Zeilen) entfernt. Build verifiziert ✅.
- **Schritt 4.1 erledigt → konsolidiert:** CMake-Targets `RendererCore` (OBJECT, abstrakte Schicht + UIManager + Widgets + RenderResourceManager + PopupWindow + EditorWindows) und `Renderer` (SHARED → Renderer.dll, alle GL-Dateien + glad + RendererCore-Objekte, links PUBLIC gegen freetype). Engine links gegen `Renderer`. Ergebnis: Eine einzige Renderer.dll statt zwei getrennter DLLs. Build verifiziert ✅.

---

## 10. Renderer – Kamera

| Feature                              | Status |
|--------------------------------------|--------|
| FPS-Kamera (Yaw + Pitch)            | ✅     |
| Abstrakte Kamera-Schnittstelle       | ✅     |
| Maus-basierte Rotation               | ✅     |
| WASD + Q/E Bewegung                 | ✅     |
| Geschwindigkeits-Steuerung (Mausrad) | ✅     |
| Pitch-Clamp (±89°)                  | ✅     |
| Orbit-Kamera (Mesh Viewer)          | ✅     |
| Cinematic-Kamera / Pfad-Follow      | ❌     |
| Entity-Kamera (CameraComponent)     | ✅     |
| Kamera-Überblendung (Smooth-Step)   | ✅     |
| Editor: WASD nur bei Rechtsklick    | ✅     |
| Editor: Laptop-Modus (WASD frei)    | ✅     |
| Editor: W/E/R Gizmo nur ohne RMB   | ✅     |
| PIE: Maus-Capture + WASD immer      | ✅     |
| PIE: Shift+F1 Maus freigeben       | ✅     |
| PIE: Viewport-Klick recapture      | ✅     |
| PIE: ESC → vorherigen Zustand      | ✅     |

**Offene Punkte:**
- Orbit-Kamera ist im Mesh-Viewer implementiert (`MeshViewerWindow`): Orbit-Parameter werden vor `renderWorld()` per `setPosition()`/`setRotationDegrees()` auf die Renderer-Kamera übertragen
- Entity-Kamera via `setActiveCameraEntity()` / `clearActiveCameraEntity()` – überschreibt View + Projection aus CameraComponent + TransformComponent
- Keine Kamera-Überblendung / Cinematic-Pfade

---

## 11. Renderer – Shader-System

| Feature                              | Status |
|--------------------------------------|--------|
| Abstrakte Shader-Schnittstelle       | ✅     |
| GLSL-Kompilierung                    | ✅     |
| Shader-Programm-Linking             | ✅     |
| Uniform-Setter (float, int, vec, mat)| ✅     |
| Vertex-Shader                        | ✅     |
| Fragment-Shader                      | ✅     |
| Geometry-Shader (Enum vorhanden)    | 🟡     |
| Compute-Shader (Enum vorhanden)     | 🟡     |
| Hull-/Domain-Shader (Enum vorhanden)| 🟡     |
| Shader Hot-Reload                   | ✅     |
| Shader-Variants / Permutationen     | ✅     |
| Shader-Reflection                   | ❌     |

**Offene Punkte:**
- Geometry-, Compute-, Hull-, Domain-Shader sind im Enum definiert, werden aber nirgendwo aktiv genutzt
- Shader Hot-Reload implementiert: `ShaderHotReload`-Klasse pollt `shaders/` alle 500 ms per `last_write_time`, invalidiert Material-Cache, UI-Quad-Programme und PostProcessStack-Programme, rebuildet Render-Entries automatisch
- Keine Shader-Variants oder Permutations-System

---

## 12. Renderer – Material-System

| Feature                              | Status |
|--------------------------------------|--------|
| CPU-Material (Texturen + Shininess)  | ✅     |
| OpenGLMaterial (VAO/VBO/EBO)        | ✅     |
| Multi-Textur-Unterstützung          | ✅     |
| Beleuchtung (8 Lichtquellen, 3 Typen)| ✅    |
| Batch-Rendering (renderBatchCont.)  | ✅     |
| Default World-Grid-Material (eigener Shader + .asset) | ✅ |
| Material-Shader-Override (m_shaderFragment in .asset) | ✅ |
| PBR-Material (Metallic/Roughness)   | ✅     |
| Normal Mapping                      | ✅     |
| Emissive Maps                       | ✅     |
| Material-Editor (UI)                | ✅     |
| Material-Instancing / Overrides     | ✅     |

**Offene Punkte:**
- Kein Displacement Mapping
- Normal Mapping implementiert (TBN-Matrix im Vertex-Shader, Tangent-Space Normal Maps im Fragment-Shader, Slot 2)
- Emissive Maps implementiert (material.emissiveMap Slot 3, additive Emission vor Fog/Tone Mapping)
- Material-Editor: Popup-basierter Editor (480×560) mit Material-Auswahl per Dropdown, PBR-Parameter-Editing (Metallic, Roughness, Shininess als Slider, PBR-Enabled-Checkbox), Textur-Slot-Bearbeitung (5 Slots: Diffuse, Specular, Normal, Emissive, MetallicRoughness) und Save/Close-Buttons. Erreichbar über Content-Browser-Doppelklick und World-Settings-Tools-Bereich.
- Default-Grid-Material (`Content/Materials/WorldGrid.asset`) liegt im Engine-Verzeichnis (neben der Executable, wie Editor-Widgets) und nutzt eigenen Shader (`grid_fragment.glsl`) mit World-Space XZ-Koordinaten, Major/Minor-Grid (1.0 / 0.25 Einheiten)
- Grid-Shader unterstützt vollständige Lichtberechnung (Multi-Light, Schatten, Blinn-Phong) wie `fragment.glsl` — Landscape wird von allen Lichtquellen beeinflusst
- Landscape-Entities erhalten automatisch das WorldGrid-Material via MaterialComponent
- Material-Pfad-Auflösung: Projekt-Content → Engine-Content (Fallback für Built-in-Materialien)

---

## 13. Renderer – Texturen

| Feature                              | Status |
|--------------------------------------|--------|
| CPU-Texturdaten (stb_image)          | ✅     |
| GPU-Upload (OpenGLTexture)           | ✅     |
| Format: PNG, TGA, JPG, BMP          | ✅     |
| Format: DDS (BC1–BC7 komprimiert)   | ✅     |
| Bind/Unbind (Texture Units)         | ✅     |
| Mipmaps                              | ✅     |
| Texture-Compression (S3TC/BC)       | ✅     |
| Texture-Streaming                   | ❌     |
| Cubemap / Skybox                    | ✅     |

**Offene Punkte:**
- Kein Texture-Streaming für große Texturen

---

## 14. Renderer – 2D-/3D-Objekte

| Feature                              | Status |
|--------------------------------------|--------|
| OpenGLObject2D (Sprites)            | ✅     |
| OpenGLObject3D (Meshes)             | ✅     |
| **Abstrakte Interfaces (IRenderObject2D/3D)** | ✅ |
| Material-Verknüpfung                | ✅     |
| Lokale Bounding Box (AABB)          | ✅     |
| Batch-Rendering                      | ✅     |
| Statischer Cache                    | ✅     |
| OBJ-Laden (Basis-Meshes)           | ✅     |
| FBX-Import (via Assimp)             | ✅     |
| glTF-Import (via Assimp)            | ✅     |
| LOD-System (Level of Detail)        | ✅     |
| Skeletal Meshes / Animation         | ✅     |

**Abstraktion:** `IRenderObject2D` und `IRenderObject3D` definieren backend-agnostische Interfaces. OpenGL-Klassen erben davon. `MeshViewerWindow` und `RenderResourceManager` nutzen ausschließlich die abstrakten Interfaces.

---

## 15. Renderer – Text-Rendering

| Feature                              | Status |
|--------------------------------------|--------|
| FreeType-Glyph-Atlas                | ✅     |
| drawText + measureText              | ✅     |
| Zeilenhöhe-Berechnung               | ✅     |
| Shader-Cache                        | ✅     |
| Blend-State Save/Restore            | ✅     |
| Multi-Font-Unterstützung            | 🟡     |
| Rich-Text (Farbe, Bold, Italic)    | ❌     |
| Text-Wrapping / Layout              | ❌     |
| Unicode-Vollunterstützung           | 🟡     |

**Offene Punkte:**
- Multi-Font funktioniert prinzipiell, wird aber hauptsächlich mit einer Default-Schrift genutzt
- Kein Rich-Text (inline-Formatierung)
- Kein automatisches Text-Wrapping
- Unicode-Unterstützung abhängig von FreeType-Glyph-Abdeckung der jeweiligen Font

---

## 16. Renderer – RenderResourceManager

| Feature                                   | Status |
|-------------------------------------------|--------|
| Level-Vorbereitung (prepareActiveLevel)  | ✅     |
| Renderable-Erstellung (buildRenderables) | ✅     |
| Object2D/3D-Cache (weak_ptr-basiert)     | ✅     |
| Material-Daten-Cache                     | ✅     |
| Widget-Cache                             | ✅     |
| Text-Renderer Lazy-Init                  | ✅     |
| Cache-Invalidierung                      | ✅     |
| Per-Entity Render Refresh (refreshEntityRenderable) | ✅ |
| Content-Pfad-Auflösung (resolveContentPath, **public**) | ✅ |
| **Abstrakte Interface-Typen in Public API** | ✅ |

**Abstraktion:** Öffentliche API (`getOrCreateObject2D/3D`, `prepareTextRenderer`, `RenderableAsset`) verwendet ausschließlich abstrakte Interface-Typen (`IRenderObject2D`, `IRenderObject3D`, `ITextRenderer`). Caches intern auf abstrakte `weak_ptr` umgestellt. `OpenGLRenderer` castet bei Bedarf über `std::static_pointer_cast` auf konkrete Typen zurück.

---

## 17. UI-System

### 17.1 UIManager

| Feature                              | Status |
|--------------------------------------|--------|
| Widget-Registrierung / Z-Ordering   | ✅     |
| **Tab-Scoped Widgets** (tabId-Filter in Rendering + Hit-Testing) | ✅ |
| Hit-Test + Focus                     | ✅     |
| Maus-Interaktion (Click, Hover)     | ✅     |
| Scroll-Unterstützung                | ✅     |
| Text-Eingabe (Entry-Bars)           | ✅     |
| Tastatur-Handling (Backspace/Enter/F2) | ✅     |
| Layout-Berechnung                   | ✅     |
| Click-Events (registrierbar)        | ✅     |
| Modal-Nachrichten                   | ✅     |
| Toast-Nachrichten (Stapel-Layout)   | ✅     |
| World-Outliner Integration          | ✅     |
| World-Outliner: Optimiertes Refresh (nur bei Entity-Erstellung/-Löschung) | ✅ |
| Entity-Auswahl + Details            | ✅     |
| EntityDetails: Asset-Dropdown (Mesh/Material/Script) | ✅ |
| EntityDetails: Drop-Zones mit Typ-Validierung | ✅ |
| EntityDetails: \"+ Add Component\"-Dropdown | ✅ |
| EntityDetails: Remove-Button (X) pro Komponente mit Bestätigungsdialog | ✅ |
| EntityDetails: Editierbare Komponentenwerte (EntryBar, Vec3, CheckBox, DropDown, ColorPicker) | ✅ |
| EntityDetails: Sofortige visuelle Rückmeldung (Transform/Light/Camera per-Frame, Mesh/Material via Per-Entity Refresh) | ✅ |
| EntityDetails: Alle Wertänderungen markieren Level als unsaved (`setIsSaved(false)`) | ✅ |
| EntityDetails: Add/Remove Component mit `invalidateEntity()` + UI-Refresh | ✅ |
| EntityDetails: Namensänderung reflektiert sofort in Outliner + Details-Header | ✅ |
| Panel-Breite WorldOutliner/EntityDetails (280 px) | ✅ |
| DropDown-Z-Order (verzögerter Render-Pass) | ✅ |
| Verbesserte Schriftgrößen/Lesbarkeit im Details-Panel | ✅ |
| Drag & Drop (CB → Viewport/Folder/Entity) | ✅ |
| Popup-Builder: Landscape Manager (`openLandscapeManagerPopup`) | ✅ |
| Popup-Builder: Engine Settings (`openEngineSettingsPopup`) | ✅ |
| Docking-System (Panels verschieben) | ❌     |
| Theming / Style-System             | ❌     |
| Multi-Monitor-Unterstützung        | ❌     |

### 17.2 Widget-Elemente

| Element-Typ    | Status |
|----------------|--------|
| Text           | ✅     |
| Button         | ✅     |
| Panel          | ✅     |
| StackPanel     | ✅     |
| Grid           | ✅     |
| ColorPicker    | ✅     |
| EntryBar       | ✅     |
| ProgressBar    | ✅     |
| Slider         | ✅     |
| Image          | ✅     |
| Separator      | ✅     |
| DropDown / ComboBox | ✅ |
| DropdownButton      | ✅ |
| CheckBox       | ✅     |
| TreeView       | ✅     |
| TabView        | ✅     |
| ScrollBar (eigenständig) | ❌ |

### 17.3 Editor-Panels

| Panel           | Status |
|-----------------|--------|
| TitleBar (100px: HorizonEngine-Titel + Projektname + Min/Max/Close rechts, Tab-Leiste unten) | ✅ |
| Toolbar / ViewportOverlay (Select/Move/Rotate/Scale + PIE + Settings) | ✅ |
| Settings-Button → Dropdown-Menü → "Engine Settings" | ✅ |
| Engine Settings Popup (Sidebar + Content, Kategorien: General, Rendering, Debug, Physics) | ✅ |
| Projekt-Auswahl-Screen (Sidebar: Recent Projects, Open Project, New Project) | ✅ |
| New-Project: Checkbox "Include default content" (unchecked => Blank DefaultLevel ohne Default-Assets) | ✅ |
| New-Project mit "Include default content": DefaultLevel wird befüllt (Cubes + Lichter) statt als leere Map angelegt | ✅ |
| New-Project: Zielpfad-Preview wird bei Name/Location live aktualisiert | ✅ |
| Content-Browser-Rechtsklickmenü: "New Folder" + Separator vor weiteren Create-Optionen | ✅ |
| Projekt-Liste: Akzentstreifen, alternierende Zeilen, größere Schrift | ✅ |
| Recent-Projects: pro Eintrag quadratischer Lösch-Button in voller Zeilenhöhe | ✅ |
| Existing-Project-Remove-Dialog enthält Checkbox "Delete from filesystem" | ✅ |
| Recent-Projects: existierend => Confirm, fehlend => direkt entfernen | ✅ |
| New-Project: Dateinameingabe zeigt Warnung bei ungültigen Zeichen (keine Auto-Korrektur) | ✅ |
| Dropdown-Menü-System (`showDropdownMenu` / `closeDropdownMenu`) | ✅ |
| WorldSettings   | ✅     |
| WorldOutliner   | ✅     |
| EntityDetails   | ✅     |
| ContentBrowser  | ✅     |
| StatusBar (Undo/Redo + Dirty-Zähler + Save All + Progress-Modal) | ✅ |
| Material-Editor | ❌     |
| Shader-Editor   | ❌     |
| Console / Log-Viewer | ✅ |
| Asset-Import-Dialog (erweitert) | ✅ |
| Viewport-Einstellungen | ❌ |

### 17.4 Editor-Tab-System

| Feature                              | Status |
|--------------------------------------|--------|
| Tab-Infrastruktur (EditorTab-Struct) | ✅     |
| Per-Tab-Framebuffer (FBO + Color + Depth) | ✅ |
| Viewport-Tab (nicht schließbar)      | ✅     |
| Tab-Leiste in TitleBar               | ✅     |
| Tab-Umschaltung (Click-Event)        | ✅     |
| Tab-Close-Button (schließbare Tabs)  | ✅     |
| HZB-Occlusion aus Tab-FBO-Tiefe     | ✅     |
| Pick-/Outline-Pass in Tab-FBO       | ✅     |
| Nur aktiver Tab rendert World/UI     | ✅     |
| Tab-Snapshot-Cache (kein Schwarzbild beim Wechsel) | ✅ |
| Tab-Wechsel während PIE blockiert    | ✅     |
| Mesh-Viewer-Tabs (Doppelklick auf Model3D) | ✅ |
| Widget-Editor-Tabs (Doppelklick auf Widget, FBO-Preview + Zoom/Pan + Outline-Selektion + Hierarchie + Details) | ✅ |
| **Tab-Scoped UI** (Viewport-Widgets + ContentBrowser nur bei Viewport-Tab, Mesh-Viewer-Props nur bei deren Tab) | ✅ |
| **Level-Swap bei Tab-Wechsel** (`swapActiveLevel` + Camera Save/Restore) | ✅ |
| Weitere Tabs (z.B. Material-Editor) | ❌     |

**Offene Punkte:**
- Kein Docking-System (Panels sind fest positioniert)
- Kein Theming / Style-System
- Fehlende Widget-Typen: ScrollBar (eigenständig)
- Fehlende Editor-Panels: Material-Editor, Shader-Editor, erweiterte Viewport-Einstellungen
- Content Browser: Ordnernavigation + Asset-Icons per Registry implementiert (Asset-Editor noch Dummy)
- Content Browser: Ausführliches Diagnose-Logging (Prefixed `[ContentBrowser]` / `[Registry]`) über die gesamte Pipeline
- Content Browser: Crash beim Klick auf Ordner behoben (dangling `this`-Capture in Builder-Lambdas → `self` direkt captured)
- Content Browser: Icons werden als PNG vom User bereitgestellt in `Editor/Textures/` (stb_image-Laden)
- Content Browser: Icons werden per Tint-Color eingefärbt (Ordner gelb, Scripte grün, Texturen blau, Materials orange, Audio rot, Shader lila, etc.)
- Content Browser: TreeView-Inhalte per `glScissor` auf den Zeichenbereich begrenzt (kein Überlauf beim Scrollen)
- Content Browser: Grid-View zeigt Ordner + Assets des ausgewählten Ordners als quadratische Kacheln (80×80px, Icon + Name)
- Content Browser: Textur-Asset-Thumbnails: `resolveTextureSourcePath()` löst Quellbild aus Asset-JSON auf, `makeGridTile()` mit `thumbnailTextureId`-Parameter zeigt das Bild direkt als Tile-Icon (größerer Bereich, weiße Tint). Funktioniert in Normal- und Suchmodus. Caching via Renderer `m_uiTextureCache`.
- Content Browser: Doppelklick auf Grid-Ordner navigiert hinein, Doppelklick auf Model3D-Asset öffnet Mesh-Viewer-Tab, Doppelklick auf andere Assets zeigt Toast
- Content Browser: Ausgewählter Ordner im TreeView visuell hervorgehoben
- Content Browser: Einfachklick auf TreeView-Ordner wählt ihn aus und aktualisiert Grid
- Content Browser: Zweiter Klick auf bereits ausgewählten Ordner klappt ihn wieder zu
- Content Browser: "Content" Root-Knoten im TreeView, klickbar zum Zurücknavigieren zur Wurzel
- Content Browser: Pfadleiste (Breadcrumbs) über der Grid: Zurück-Button + klickbare Pfadsegmente (Content > Ordner > UnterOrdner)
- Content Browser: Crash beim Ordnerwechsel nach Grid-Interaktion behoben (Use-After-Free: Target-Daten vor Callback-Aufruf kopiert)
- Content Browser: Rechtsklick-Kontextmenü auf Grid zum Erstellen neuer Assets (Script, Level, Material)
- Content Browser: Shaders-Ordner des Projekts wird als eigener Root-Knoten im TreeView angezeigt (lila Icon, separate Ansicht)
- Content Browser: "New Script" erstellt `.py`-Datei mit `import engine` und `onloaded`/`tick`-Boilerplate
- Content Browser: "New Level" öffnet Popup mit Namenseingabe, erstellt Level via `createNewLevelWithTemplate` als ungespeicherte Änderung (kein sofortiges Speichern)
- Content Browser: "New Material" öffnet Popup mit Eingabefeldern für Name, Vertex/Fragment-Shader, Texturen, Shininess
- Content Browser: Einfachklick auf Grid-Asset selektiert es (blaue Hervorhebung), Doppelklick öffnet wie zuvor
- Content Browser: Entf-Taste auf selektiertem Grid-Asset zeigt Bestätigungsdialog ("Delete" / "Cancel")
- Content Browser: Bestätigungsdialog (`showConfirmDialog`) mit Yes/No-Buttons als wiederverwendbare UIManager-API
- Content Browser: `AssetManager::deleteAsset()` entfernt Asset aus Registry + löscht Datei von Disk
- Content Browser: Drag & Drop von Skybox-Asset auf Viewport setzt die Level-Skybox direkt (keine Entity-Prüfung)
- Content Browser: Selektion wird bei Ordnernavigation automatisch zurückgesetzt
- Widget-System: `readElement` parst jetzt das `id`-Feld aus JSON (fehlte zuvor, wodurch alle Element-IDs nach Laden leer waren)
- Widget-System: `layoutElement` hat jetzt Default-Pfad für Kinder aller Element-Typen (Button-Kinder werden korrekt gelayoutet)
- Widget-System: Grid-Layout berechnet Spalten aus verfügbarer Breite / Kachelgröße für quadratische Zellen
- Widget-System: `onDoubleClicked`-Callback auf `WidgetElement` mit Doppelklick-Erkennung (400ms, SDL_GetTicks)
- EntityDetails-Panel endet über dem ContentBrowser (Layout berücksichtigt die Oberkante des ContentBrowsers als Unterlimit)
- Side-Panels (WorldOutliner, WorldSettings) werden jetzt korrekt auf die verfügbare Höhe begrenzt – kein Überzeichnen hinter ContentBrowser/StatusBar mehr (Fallback-Höhe aus Content-Messung auf `available.h` geclampt; Asset-Validierung prüft `m_fillY`)
- Scrollbare StackPanels/Grids werden per `glScissor` auf ihren Zeichenbereich begrenzt (kein Überlauf beim Scrollen)
- EntityDetails: Asset-Validierung prüft jetzt `scrollable`-Flag auf Details.Content – veraltete Cache-Dateien ohne Scrolling werden automatisch neu generiert
- EntityDetails: Mesh/Material/Script-Sektionen enthalten DropdownButtons mit allen Assets des passenden Typs; die DropdownButtons dienen gleichzeitig als Drop-Targets für Drag-and-Drop aus dem Content Browser (Typ-Validierung mit Toast bei falschem Typ). Separate Drop-Zone-Panels entfernt, da sie den Hit-Test der DropdownButtons blockieren konnten.
- Scrollbare Container: `computeElementBounds` begrenzt Bounds auf die eigene sichtbare Fläche – herausgerollte Elemente erweitern die Hit-Test-Bounds nicht mehr (behebt falsches Hit-Testing im Content-Browser TreeView und Details-Panel nach dem Scrollen)
- EntityDetails: Doppelter Layout-Pass behoben – das Widget wird im ersten Layout-Durchlauf übersprungen und nur im zweiten Pass mit korrekter Split-Größe gelayoutet. Vorher klemmte der ScrollOffset am kleineren maxScroll des ersten Passes, sodass nicht bis zum Ende gescrollt werden konnte und die DropdownButtons in unteren Sektionen unerreichbar waren.
- `layoutElement`: DropdownButton nutzt jetzt den Content-basierten Sizing-Pfad (wie Text/Button), sodass die Höhe korrekt aus dem gemessenen Inhalt statt nur aus minSize kommt
- DropdownButton: Klick-Handling komplett überarbeitet – Dismiss-Logik erkennt jetzt DropdownButton-Elemente (nicht nur ID-Prefix), Toggle-Verhalten per Source-Tracking (erneuter Klick schließt das Menü statt Close+Reopen), leere Items zeigen „(No assets available)" Platzhalter, Menü-Breite passt sich an Button-Breite an
- DropdownButton: Renderer nutzt jetzt den Button-Shader (`m_defaultButtonVertex`/`m_defaultButtonFragment`) statt Panel-Shader, sodass Hover-Feedback korrekt angezeigt wird
- Z-Ordering: `getWidgetsOrderedByZ` nutzt jetzt `std::stable_sort` statt `std::sort` für deterministische Reihenfolge bei gleichem Z-Wert (verhindert nicht-deterministisches Hit-Testing zwischen EntityDetails und ContentBrowser)
- **DropdownButton Rendering**: `WidgetElementType::DropdownButton` hat jetzt einen eigenen Render-Case in beiden `renderElement`-Lambdas (`renderUI` und `drawUIWidgetsToFramebuffer`). Zeichnet Hintergrund-Panel mit Hover, Text mit Alignment + Padding und einen kleinen Pfeil-Indikator rechts. Nutzt Button-Shader (`m_defaultButtonVertex`/`m_defaultButtonFragment`). Behebt unsichtbare DropdownButtons im EntityDetails-Panel (Mesh/Material/Script-Auswahl und "+Add Component").
- **F2-Tastenkürzel (Rename)**: `handleKeyDown` reagiert jetzt auf F2 – startet Inline-Rename im Content-Browser-Grid, wenn ein Asset selektiert ist (`m_selectedGridAsset` nicht leer, `m_renamingGridAsset` noch nicht aktiv). Check wird vor dem `m_focusedEntry`-Guard ausgeführt, damit F2 auch ohne fokussierte EntryBar funktioniert.
- **Editierbare Komponentenwerte**: Alle ECS-Komponentenfelder sind im EntityDetails-Panel über passende Steuerelemente editierbar: Vec3-Reihen mit farbkodierten X/Y/Z-EntryBars (rot/grün/blau) für Transform-Position/Rotation/Scale und Physics-Vektoren, Float-EntryBars für Kamera-FOV/Clip-Planes und Light-Intensity/Range, CheckBoxen für Physics-isStatic/isKinematic/useGravity und Camera-isActive, DropDowns für LightType (Point/Directional/Spot) und ColliderType (Box/Sphere/Mesh), ColorPicker (kompakt) für Light-Color, EntryBar für NameComponent-displayName. Hilfslambdas `makeFloatEntry`, `makeVec3Row`, `makeCheckBoxRow` erzeugen die UI-Zeilen. Jede Änderung ruft `ecs.setComponent<T>()` auf, was `m_componentVersion` inkrementiert und Auto-Refresh auslöst.
- **Sofortige visuelle Rückmeldung bei Komponentenänderungen**: Transform-, Light- und Camera-Werte werden vom Renderer jeden Frame direkt aus dem ECS gelesen (per-Frame-Queries in `renderWorld`) — Änderungen sind sofort im Viewport sichtbar. Mesh/Material-Pfadänderungen lösen `invalidateEntity(entity)` aus, was die Entität in die Dirty-Queue einreiht. Im nächsten Frame konsumiert `renderWorld()` die Queue und ruft `refreshEntity()` → `refreshEntityRenderable()` auf — bestehende GPU-Caches werden wiederverwendet, nur fehlende Assets werden nachgeladen (kein vollständiger Scene-Rebuild mehr). Alle Wert-Callbacks (`makeFloatEntry`, `makeVec3Row`, `makeCheckBoxRow` sowie Inline-Callbacks für Light-Typ, Light-Color, Physics-Collider) markieren das Level als unsaved (`setIsSaved(false)`). Add/Remove-Component-Callbacks rufen `invalidateEntity(entity)`, `populateOutlinerDetails(entity)` und `refreshWorldOutliner()` auf. Nicht-renderable Komponenten (Name, Light, Camera, Physics, Script) lösen keine Render-Invalidierung aus.
- **Panel-Breite 280 px**: WorldOutliner und EntityDetails verwenden jetzt 280 px statt 200 px Breite. `ensureEditorWidgetsCreated` prüft die Breite im `.asset`-Cache und generiert die Datei bei abweichendem Wert automatisch neu.
- **DropDown-Z-Order Fix**: Aufgeklappte DropDown-Listen (WidgetElementType::DropDown) werden nicht mehr inline im renderElement-Lambda gezeichnet, sondern in einem verzögerten zweiten Render-Durchgang nach allen Widgets. Dadurch liegen sie immer über allen Geschwister-Elementen. Betrifft beide Render-Pfade (renderUI und drawUIWidgetsToFramebuffer).
- **DropDown-Hit-Testing Fix**: `hitTest` enthält einen Vor-Durchlauf, der aufgeklappte DropDown-Elemente mit Priorität prüft, bevor die reguläre Baumtraversierung beginnt. Geschwister-Elemente unterhalb eines aufgeklappten DropDowns fangen damit keine Klicks mehr ab.
- **Registry-Version für Details-Panel-Refresh**: `AssetManager::m_registryVersion` (atomarer Zähler) wird bei `registerAssetInRegistry()`, `renameAsset()`, `moveAsset()` und `deleteAsset()` inkrementiert. `UIManager::updateNotifications` vergleicht den Wert mit `m_lastRegistryVersion` und baut das EntityDetails-Panel automatisch neu auf, sobald Assets erstellt, importiert, umbenannt, verschoben oder gelöscht werden. Dropdowns (Mesh/Material/Script/Add Component) zeigen die aktuellen Asset-Namen sofort an.
- **Asset-Integritäts-Validierung**: Zwei neue Methoden in `AssetManager`: `validateRegistry()` prüft alle Registry-Einträge gegen das Dateisystem und entfernt Einträge für nicht mehr vorhandene Dateien (Rebuild Index-Maps + Persist + Version-Bump). `validateEntityReferences(showToast)` prüft ECS-Entity-Referenzen (MeshComponent, MaterialComponent, ScriptComponent) gegen die Registry und loggt Warnungen für fehlende Assets. `validateRegistry()` wird automatisch nach `discoverAssetsAndBuildRegistryAsync()` aufgerufen, `validateEntityReferences()` nach `prepareEcs()` in `RenderResourceManager::prepareActiveLevel()`.
- **Rename-Tastatureingabe Fix**: Beim Starten eines Inline-Renames im Content Browser wird die EntryBar automatisch per `setFocusedEntry` fokussiert. Engine-Shortcuts (W/E/R Gizmo-Modi, Ctrl+Z/Y/S, F2/DELETE-Handlers via `diagnostics.dispatchKeyUp`) werden blockiert, solange `hasEntryFocused()` true ist. `onValueChanged`-Callback ruft `setFocusedEntry(nullptr)` vor dem Tree-Rebuild auf, um Dangling-Pointer zu vermeiden.
- **Verbesserte Schriftgrößen**: Details-Panel Hilfslambdas nutzen größere Fonts (makeTextLine 13 px, Eingabefelder/Checkboxen/Dropdowns 12 px) und breitere Labels (100 px statt 90 px) für bessere Lesbarkeit.
- Hover-Stabilität:
- SeparatorWidget (Collapsible Sections): Redesign als flache Sektions-Header mit ▾/▸ Chevrons, dünner Trennlinie, subtilen Farben und 14px Content-Einrückung (statt prominenter Buttons mit v/>)
- **Performance-Optimierungen:**
  - `updateHoverStates`: O(1) Tracked-Pointer statt O(N) Full-Tree-Walk pro Mausbewegung
  - `hitTest`: Keine temporäre Vektor-Allokation mehr, iteriert gecachte Liste direkt rückwärts
  - `drawUIPanel`/`drawUIImage`: Uniform-Locations pro Shader-Programm gecacht (eliminiert ~13 `glGetUniformLocation`-Aufrufe pro Draw)
  - Verbose INFO-Logging aus allen Per-Frame-Hotpaths entfernt (Hover, HitTest, ContentBrowser-Builder, RegisterWidget)
  - Per-Click-Position-Logs entfernt (MouseDown/Click-Miss-Koordinaten waren diagnostischer Noise)
- **Editor-Gizmos (Translate/Rotate/Scale):**
  - 3D-Gizmo-Rendering im Viewport für die ausgewählte Entity (immer im Vordergrund, keine Tiefenverdeckung)
  - Translate-Modus: 3 Achsenpfeile (Rot=X, Grün=Y, Blau=Z) mit Pfeilspitzen
  - Rotate-Modus: 3 Achsenkreise (Rot=X, Grün=Y, Blau=Z)
  - Scale-Modus: 3 Achsenlinien mit Würfel-Enden
  - Gizmo skaliert mit Kamera-Entfernung (konstante Bildschirmgröße)
  - Achsen-Highlighting: aktive/gehoverte Achse wird gelb hervorgehoben
  - Achsen-Picking: Screen-Space Projektion der Achsenlinien, nächste Achse innerhalb 12px Schwellenwert
  - Maus-Drag: Pixel-Bewegung wird auf die Screen-Space-Achsenrichtung projiziert, dann in Welt-Einheiten / Grad / Skalierung umgerechnet
  - Tastatur-Shortcuts: W=Translate, E=Rotate, R=Scale (nur im Editor-Modus, nicht während PIE)
  - Gizmo-Drag hat Vorrang vor Entity-Picking (Klick auf Achse startet Drag, nicht neuen Pick)
  - Eigener GLSL-Shader (Vertex + Fragment) mit dynamischem VBO für Linien-Geometrie
- Mesh-Viewer-Tabs für 3D-Modell-Vorschau implementiert (Doppelklick auf Model3D im Content Browser)
- **Audio Preview Tab (Phase 2.6):** Doppelklick auf Audio-Asset im Content Browser öffnet einen eigenen Tab mit Play/Stop-Buttons, Lautstärke-Slider, Waveform-Balkengrafik (80 Balken aus Sample-Daten) und Metadaten-Anzeige (Pfad, Channels, Sample Rate, Format, Duration, Dateigröße). Folgt dem Console/Profiler-Tab-Muster (rein UIManager-basiert, kein FBO). `AudioPreviewState`-Struct in `UIManager` verwaltet Tab-Lifecycle.
- **Particle Editor Tab (Phase 2.5):** Schließbarer Tab mit Toolbar (Titel inkl. Entity-Name, Preset-Dropdown mit 6 Presets, Reset-Button), scrollbarem Parameterbereich mit Slider-Controls für alle 20 `ParticleEmitterComponent`-Parameter, gruppiert in Sektionen (General, Emission, Motion, Size, Start Color, End Color). Öffnung über „Edit Particles"-Button in den Entity-Details. Alle Änderungen werden live auf die verlinkte Entity im Viewport angewendet. 6 eingebaute Presets (Fire, Smoke, Sparks, Rain, Snow, Magic). Reset-Button stellt Standardwerte wieder her. Undo/Redo-Integration für Presets und Reset. 0.3s-Validierungs-Timer prüft Entity-Existenz. `ParticleEditorState`-Struct in `UIManager` verwaltet Tab-Lifecycle.
- **StatusBar (Fußleiste):**
  - Horizontales Widget am unteren Fensterrand (32px, z-order=3, BottomLeft, fillX)
  - Undo-Button + Redo-Button links, Dirty-Asset-Zähler Mitte, Save-All-Button rechts
  - Dirty-Label zeigt Anzahl ungespeicherter Assets (gelb wenn >0, grau wenn 0)
  - Undo/Redo-Buttons zeigen Beschreibung der letzten Aktion, ausgegraut wenn nicht verfügbar
  - Save-All-Button startet asynchrones Speichern über `AssetManager::saveAllAssetsAsync()`
  - Save-Progress-Modal (z-order=10001): Overlay mit Titel, Zähler, ProgressBar – wird per Callback aktualisiert
  - Nach Abschluss: Toast-Nachricht ("All assets saved successfully." / "Some assets failed to save.")
- **Undo/Redo-System:**
  - `UndoRedoManager`-Singleton (`src/Core/UndoRedoManager.h/.cpp`)
  - Command-Pattern: Jeder Command hat `execute()`, `undo()`, `description`
  - `pushCommand()` für bereits angewendete Aktionen (old/new State in Lambdas), `executeCommand()` als Legacy-Helper
  - Separater Undo-Stack und Redo-Stack (max. 100 Einträge)
  - `onChanged`-Callback: Feuert nach jedem push/undo/redo → markiert aktives Level als dirty, refresht StatusBar
  - `clear()` feuert NICHT `onChanged` (nach Speichern soll Level nicht erneut dirty werden)
  - Gizmo-Integration: `beginGizmoDrag` snapshoted die alte TransformComponent, `endGizmoDrag` pusht Command mit old/new Transform
  - Entity-Löschen (DELETE): Vollständiger Snapshot aller 10 Komponentenarten (`std::make_optional`) vor Löschung. Undo erstellt Entity mit derselben ID (`ecs.createEntity(entity)`) und stellt alle Komponenten wieder her.
  - Entity-Spawn (Drag-and-Drop Model3D auf Viewport): Undo entfernt die gespawnte Entity aus Level und ECS (`level->onEntityRemoved()` + `ecs.removeEntity()`).
  - Landscape-Erstellung: Undo entfernt die Landscape-Entity aus Level und ECS.
  - Tastenkürzel: Ctrl+Z (Undo), Ctrl+Y (Redo), Ctrl+S (Save All)
  - StatusBar-Buttons rufen `undo()` / `redo()` auf
  - Undo-History wird beim Speichern gecleared (`UndoRedoManager::clear()`)
  - Level-Save: `saveLevelAsset()` hat Raw-Pointer-Überladung (`EngineLevel*`), speichert das echte Level-Objekt (keine Kopie)
  - Level-Load: `ensureDefaultAssetsCreated` lädt bei vorhandener Level-Datei die gespeicherten Daten von Disk (via `loadLevelAsset`), statt immer die hartkodierten Defaults zu verwenden
  - `loadLevelAsset` speichert Content-relativen Pfad, damit `saveLevelAsset` den korrekten absoluten Pfad rekonstruieren kann
  - Editor-Kamera wird pro Level gespeichert (Position + Rotation in `EditorCamera` JSON-Block), beim Laden wiederhergestellt und bei jedem Save/Shutdown automatisch geschrieben
- **Shadow Mapping (Multi-Light):**
  - Bis zu 4 gleichzeitige Shadow-Maps für Directional und Spot Lights (GL_TEXTURE_2D_ARRAY, eine Schicht pro Licht)
  - 4096×4096 Depth-Texture pro Schicht
  - Shadow-Depth-Pass rendert die gesamte Szene pro Shadow-Licht aus dessen Perspektive
  - Directional Lights: orthographische Projektion (±15 Einheiten, kamerazentriert)
  - Spot Lights: perspektivische Projektion (FOV aus Outer-Cutoff-Winkel, Range als Far-Plane)
  - 5×5 PCF (Percentage Closer Filtering) im Fragment-Shader für weiche Schatten
  - Light-Space-Position wird im Fragment-Shader berechnet (kein Varying-Limit-Problem bei Multi-Light)
  - Front-Face Culling während des Shadow-Pass zur Reduzierung von Shadow Acne
  - Slope-basierter Depth Bias im Shader (`max(0.005 * (1 - NdotL), 0.001)`)
  - Shadow Maps auf Texture Unit 4 gebunden (sampler2DArray), Clamp-to-Border mit weißem Rand
  - Shadow-Zuordnung: `findShadowLightIndices()` sammelt bis zu 4 Directional/Spot Lights
  - Separate `m_shadowCasterList` ohne Kamera-Frustum-Culling: Objekte werfen Schatten auch wenn sie außerhalb des Kamerasichtfelds liegen
  - Lichtquellen-Geometry (Entities mit `LightComponent`, `AssetType::PointLight`) wird automatisch vom Shadow-Casting ausgeschlossen
  - Shader-Dateien: `shadow_vertex.glsl`, `shadow_fragment.glsl` (Inline im Renderer kompiliert)
  - `OpenGLMaterial`: Uniforms `uShadowMaps` (sampler2DArray), `uShadowCount`, `uLightSpaceMatrices[4]`, `uShadowLightIndices[4]`
  - `OpenGLObject3D::setShadowData()` delegiert Shadow-Parameter an das Material
  - `OpenGLRenderer`: `ensureShadowResources()`, `releaseShadowResources()`, `renderShadowMap()`, `computeLightSpaceMatrix()`, `findShadowLightIndices()`
  - **Point Light Shadow Mapping (Cube Maps):**
    - Omnidirektionale Schatten für Point Lights über GL_TEXTURE_CUBE_MAP_ARRAY
    - Bis zu 4 Point Lights gleichzeitig mit Shadow Mapping
    - Geometry-Shader-basierter Single-Pass Cube-Rendering (6 Faces pro Draw Call)
    - Lineare Tiefenwerte (Distanz / Far Plane) für korrekte omnidirektionale Schattenberechnung
    - Fragment-Shader sampelt `samplerCubeArray` mit Richtungsvektor Fragment→Licht
    - `findPointShadowLightIndices()` sammelt Point Lights, `renderPointShadowMaps()` rendert Cube Maps
    - `ensurePointShadowResources()` / `releasePointShadowResources()` verwalten GPU-Ressourcen
    - `OpenGLMaterial`: Uniforms `uPointShadowMaps`, `uPointShadowCount`, `uPointShadowPositions[4]`, `uPointShadowFarPlanes[4]`, `uPointShadowLightIndices[4]`
  - **Cascaded Shadow Maps (CSM) für Directional Lights:**
    - 4 Kaskaden mit Practical Split Scheme (λ=0.75, Blend aus logarithmisch und uniform)
    - 2048×2048 Depth-Texture pro Kaskade (GL_TEXTURE_2D_ARRAY, 4 Schichten)
    - Frustum-Corners-basierte tight orthographische Projektion pro Kaskade
    - View-Space Depth für Kaskaden-Auswahl im Fragment-Shader
    - 5×5 PCF pro Kaskade mit kaskadenabhängigem Bias (nähere Kaskaden = kleinerer Bias)
    - Erster Directional Light wird automatisch für CSM verwendet, übersprungen in regulärer Shadow-Map-Liste
    - CSM Maps auf Texture Unit 6 gebunden (separates sampler2DArray)
    - Shader-Uniforms: `uCsmMaps`, `uCsmEnabled`, `uCsmLightIndex`, `uCsmMatrices[4]`, `uCsmSplits[4]`, `uViewMatrix`
    - `OpenGLRenderer`: `ensureCsmResources()`, `releaseCsmResources()`, `computeCsmMatrices()`, `renderCsmShadowMaps()`
    - `OpenGLMaterial::setCsmData()` + `OpenGLObject3D::setCsmData()` delegieren CSM-Parameter
    - Beide Fragment-Shader (`fragment.glsl`, `grid_fragment.glsl`) unterstützen CSM via `calcCsmShadow()`
    - Point Shadow Maps auf Texture Unit 5 gebunden

---

## 21. Multi-Window / Popup-System

| Feature                                          | Status |
|--------------------------------------------------|--------|
| `PopupWindow`-Klasse (`src/Renderer/EditorWindows/PopupWindow.h/.cpp`) | ✅ |
| **Abstraktion: `IRenderContext` statt `SDL_GLContext`** | ✅ |
| Shared OpenGL-Context (SDL3 SHARE_WITH_CURRENT_CONTEXT) | ✅ |
| Eigener `UIManager` pro Popup                    | ✅ |
| `OpenGLRenderer::openPopupWindow(id, title, w, h)` | ✅ |
| `OpenGLRenderer::closePopupWindow(id)`           | ✅ |
| `OpenGLRenderer::getPopupWindow(id)`             | ✅ |
| `OpenGLRenderer::routeEventToPopup(SDL_Event&)`  | ✅ |
| `renderPopupWindows()` im Render-Loop            | ✅ |
| `drawUIWidgetsToFramebuffer(UIManager&, w, h)`   | ✅ |
| `ensurePopupUIVao()` – kontext-lokaler VAO mit gesharetem VBO | ✅ |
| `ensurePopupVao()` für TextRenderer – kontext-lokaler VAO | ✅ |
| SDL-Event-Routing (Mouse, Key, KeyUp, Text, Close) | ✅ |
| `SDL_StartTextInput` für Popup-Fenster           | ✅ |
| Popup schließen per `SDL_EVENT_WINDOW_CLOSE_REQUESTED` | ✅ |
| Deferred Popup-Destruction (sichere Lebenszeit)  | ✅ |
| Popup fokussieren wenn bereits offen             | ✅ |
| SDL_EVENT_QUIT-Drain nach Popup-Schließung (verhindert Engine-Abort) | ✅ |
| Projekt-Mini-Event-Loop beendet bei `SDL_EVENT_QUIT`/`SDL_EVENT_WINDOW_CLOSE_REQUESTED` des Temp-Hauptfensters (kein globales Ignore mehr) | ✅ |
| Startup-Projektfenster nutzt native Titlebar (nicht fullscreen, nicht maximized, kein Custom-HitTest) | ✅ |
| Alt+F4/Schließen im Startup-Projektfenster setzt `DiagnosticsManager::requestShutdown()` und beendet die Engine (kein SampleProject-Fallback) | ✅ |
| Startup-Projektauswahl leitet Input-Events direkt an `UIManager` weiter (Mouse/Scroll/Text/KeyDown) für funktionierendes UI-HitTesting | ✅ |
| Startup-Projektfenster aktiviert `SDL_StartTextInput(window)` für zuverlässige Texteingabe in EntryBars | ✅ |
| `AssetManager::createProject(..., includeDefaultContent)` / `loadProject(..., ensureDefaultContent)` unterstützen optionales Starten ohne Default-Content | ✅ |
| `DefaultProject` wird nur bei gesetzter "Set as default project"-Checkbox aus dem Projekt-Auswahlfenster aktualisiert | ✅ |
| Projekt-Load mit Default-Content ist gegen Exceptions in `ensureDefaultAssetsCreated()` abgesichert (kein Hard-Crash, sauberer Fehlerpfad) | ✅ |
| Default-Lights im Projekt-Content enthalten `PointLight.asset`, `DirectionalLight.asset`, `SpotLight.asset` | ✅ |
| Projekt-Auswahl nutzt isolierten `tempRenderer` vor Initialisierung der Haupt-Engine | ✅ |
| `AssetManager::registerRuntimeResource()` ist vor `initialize()` deaktiviert (kein GL-Resource-Leak aus Startup-Renderer) | ✅ |
| Hauptfenster wird nach Startup-Auswahl immer bedingungslos wieder sichtbar | ✅ |
| Engine beendet sich nur wenn kein Projekt (inkl. Fallback) geladen werden kann | ✅ |
| Entscheidungs-Logging an allen Stellen des Projekt-Auswahl-Flows | ✅ |
| Alle Zwischen-Event-Pumps (showProgress, "Engine ready") ignorieren SDL_EVENT_QUIT | ✅ |
| `resetShutdownRequest()` vor Main-Loop-Eintritt (verhindert verwaiste Shutdown-Flags) | ✅ |
| Shutdown-Check in der Main-Loop loggt den Exit-Grund | ✅ |
| Hauptfenster wird vor `SplashWindow::close()` sichtbar gemacht (stabiler Window-Übergang) | ✅ |
| Dateidialoge in der Startup-Projektauswahl nutzen das sichtbare Temp-Hauptfenster als Parent | ✅ |
| Fenstergröße dynamisch (refreshSize)             | ✅ |
| Docking / Snapping                               | ❌ |
| Mehrere Popups gleichzeitig                      | ✅ |
| Engine Settings Popup (Sidebar-Layout, Kategorien: General, Rendering, Debug, Physics, Info) | ✅ |
| Engine Settings Info-Tab: CPU, GPU, VRAM, RAM, Monitor Hardware-Infos (read-only) | ✅ |
| DPI-Aware UI Scaling (Auto-Detect + manuelle Auswahl in Editor Settings) | ✅ |
| Dropdown-Menü als Overlay-Widget (z-Order 9000, Click-Outside-Dismiss) | ✅ |
| Engine Settings Persistenz via `config.ini` (Shadows, Occlusion, Debug, VSync, Wireframe, Physics, HeightField Debug, Post Processing, Gamma, Tone Mapping, AA, Bloom, SSAO, CSM) — sofortige Speicherung bei jeder Änderung (`saveConfig()` in allen Callbacks) | ✅ |
| Physics-Kategorie (Gravity X/Y/Z, Fixed Timestep, Sleep Threshold) | ✅ |
| VSync Toggle (Engine Settings → Rendering → Display) | ✅ |
| Wireframe Mode (Engine Settings → Rendering → Display) | ✅ |
| Post Processing Toggle (Engine Settings → Rendering → Post-Processing) | ✅ |
| Gamma Correction Toggle (Engine Settings → Rendering → Post-Processing) | ✅ |
| Tone Mapping Toggle (Engine Settings → Rendering → Post-Processing) | ✅ |
| Anti-Aliasing Dropdown (Engine Settings → Rendering → Post-Processing: None/FXAA/MSAA 2x/MSAA 4x) | ✅ |
| Bloom Toggle (Engine Settings → Rendering → Post-Processing) | ✅ |
| SSAO Toggle (Engine Settings → Rendering → Post-Processing) | ✅ |
| CSM Toggle (Engine Settings → Rendering → Lighting) | ✅ |
| Script Hot-Reload Toggle (Engine Settings → General → Scripting, config.ini-Persistenz) | ✅ |
| Absolute Widget-Positionierung (`setAbsolutePosition`) | ✅ |

**Offene Punkte:**
- Kein Docking/Snapping zwischen Fenstern
- Popup-VAO wird erst beim ersten Render-Frame erstellt (einmaliger Overhead)

---

## 25. Editor-Fenster / Mesh Viewer

| Feature                                          | Status |
|--------------------------------------------------|--------|
| `MeshViewerWindow`-Klasse (`src/Renderer/EditorWindows/MeshViewerWindow.h/.cpp`) | ✅ |
| **Abstraktion: nutzt `IRenderObject3D` statt `OpenGLObject3D`** | ✅ |
| **Tab-basiertes System** (eigener EditorTab pro Mesh Viewer mit eigenem FBO) | ✅ |
| **Runtime-EngineLevel** pro Mesh-Viewer (isolierte Szene) | ✅ |
| **Per-Tab-FBO**: Jeder Tab rendert in eigenen Framebuffer, Tab-Wechsel tauscht FBO | ✅ |
| **UI-Tab-Filterung**: Properties-Widget mit `tabId` registriert, UIManager filtert nach aktivem Tab | ✅ |
| **Dynamische Tab-Buttons** in TitleBar beim Öffnen/Schließen | ✅ |
| **Level-Swap** beim Tab-Wechsel (`swapActiveLevel` + `setActiveTab`) | ✅ |
| **Normale FPS-Kamera** (WASD+Maus, keine Orbit-Kamera, initiale Ausrichtung auf Mesh-AABB) | ✅ |
| **Tab-scoped Properties-Widget** (`MeshViewerDetails.{path}`, tabId = assetPath) | ✅ |
| **Default-Material-Komponente** im Runtime-Level (Mesh+Material für Render-Schema) | ✅ |
| **Rendering über normale renderWorld-Pipeline** (kein eigener Render-Pfad, nutzt RRM + buildRenderablesForSchema) | ✅ |
| **Auto-Material aus .asset** (liest `m_materialAssetPaths[0]` beim Level-Aufbau) | ✅ |
| **Performance-Stats ausgeblendet** in Mesh-Viewer-Tabs (FPS, Metriken, Occlusion nur im Viewport) | ✅ |
| **Rein-Runtime-Level** (kein Serialisieren auf Disk, `saveLevelAsset` überspringt `__MeshViewer__`) | ✅ |
| **Ground-Plane** im Preview-Level (default_quad3d + WorldGrid-Material, 20×20 Einheiten) | ✅ |
| Initiale Kameraposition aus Mesh-AABB berechnet  | ✅ |
| Automatische Ausrichtung der Kamera auf Mesh-Zentrum | ✅ |
| Standard-Beleuchtung (Directional Light, Rotation 50°/30°, natürliches Warmweiß, Intensität 0.8) | ✅ |
| Kamera-State Save/Restore pro Tab (EditorCamera in Level) | ✅ |
| **Per-Tab Entity-Selektion** (Multi-Selection-State wird beim Tab-Wechsel gespeichert/wiederhergestellt via `m_tabSelectedEntities`) | ✅ |
| **Editierbare Asset-Properties** im Sidepanel (Scale X/Y/Z, Material-Pfad, markiert Asset als unsaved) | ✅ |
| Doppelklick auf Model3D im Content Browser öffnet Viewer | ✅ |
| Automatisches Laden von noch nicht geladenen Assets | ✅ |
| Toast-Benachrichtigung "Loading..." während Laden | ✅ |
| Pfad-Auflösung: Registry-relative → absolute Pfade via `resolveContentPath` | ✅ |
| Detailliertes Diagnose-Logging in `initialize()` + `openMeshViewer()` | ✅ |
| Input-Routing: `getMeshViewer(getActiveTabId())` in `main.cpp` | ✅ |
| **Editor-Kamera State Save/Restore** beim Tab-Wechsel | ✅ |
| Material-Vorschau im Mesh Viewer                 | ✅ |
| Mesh-Editing (Vertices, Normals)                 | ❌ |
| Animations-Vorschau                              | ❌ |
| Info-Overlay (Vertex/Triangle-Count, Dateiname)  | ❌ |

**Offene Punkte:**
- Kein Mesh-Editing (nur Betrachtung)
- Keine Animations-Unterstützung
- Kein Info-Overlay (Vertex/Triangle-Count)

---

## 25b. Editor-Fenster / Texture Viewer

| Feature                                          | Status |
|--------------------------------------------------|--------|
| `TextureViewerWindow`-Klasse (`src/Renderer/EditorWindows/TextureViewerWindow.h/.cpp`) | ✅ |
| **Tab-basiertes System** (eigener EditorTab pro Texture Viewer mit eigenem FBO) | ✅ |
| Doppelklick auf Texture-Asset im Content Browser öffnet Viewer | ✅ |
| **Texture-Anzeige** mit Zoom/Pan und Fit-to-Window (Zoom 1.0 = Fit, Scroll-Zoom stufenlos) | ✅ |
| **Channel-Isolation** (R/G/B/A einzeln togglebar, Grayscale bei Single-Channel, ausgegraut wenn deaktiviert) | ✅ |
| **Checkerboard-Hintergrund** für Alpha-Transparenz-Visualisierung (Toggle mit Graying) | ✅ |
| **GLSL Channel-Isolation Shader** (`uChannelMask`, `uCheckerboard`) | ✅ |
| **Metadaten-Anzeige** im Sidepanel (Auflösung, Kanäle, Format, Dateigröße) | ✅ |
| **Format-Erkennung** (PNG/JPEG/TGA/BMP/HDR/DDS, komprimiert/unkomprimiert) | ✅ |
| **Tab-scoped Properties-Widget** (`TextureViewerDetails.{path}`, tabId = assetPath) | ✅ |
| Kein Level-Swap nötig (reine 2D-Vorschau, kein 3D-Szenen-Rendering) | ✅ |
| Automatisches Laden von noch nicht geladenen Texture-Assets | ✅ |
| Toast-Benachrichtigung beim Laden | ✅ |
| **Scroll-Zoom** (1.15x Faktor, Bereich 0.05–50.0, relativ zu Fit-Scale) | ✅ |
| **Rechtsklick-Pan** (Drag zum Verschieben der Textur-Ansicht) | ✅ |
| **Laptop-Modus-Pan** (Linksklick-Drag bei aktivem Laptop-Modus) | ✅ |
| **Channel-Button-Graying** (deaktivierte Kanäle visuell ausgegraut) | ✅ |
| Mipmap-Level-Slider                             | ❌ |

---

## 26. Gameplay UI System (Viewport UI)

### 26.1 ViewportUIManager – Grundgerüst & Multi-Widget (Phase A ✅)

| Feature                                          | Status |
|--------------------------------------------------|--------|
| `ViewportUIManager`-Klasse (`src/Renderer/ViewportUIManager.h/.cpp`) | ✅ |
| Multi-Widget-System (`vector<WidgetEntry>`, Z-Order-Sortierung) | ✅ |
| `createWidget` / `removeWidget` / `getWidget` / `clearAllWidgets` | ✅ |
| Implizites Canvas Panel pro Widget (Root-Element) | ✅ |
| `WidgetAnchor`-Enum (10 Werte: TopLeft/TopRight/BottomLeft/BottomRight/Top/Bottom/Left/Right/Center/Stretch) | ✅ |
| Anchor-basiertes Layout (`computeAnchorPivot` + `ResolveAnchorsRecursive`) | ✅ |
| Viewport-Rect-Verwaltung (setViewportRect, getViewportRect, getViewportSize) | ✅ |
| Element-Zugriff (findElementById, getRootElement) | ✅ |
| Layout-Update mit Dirty-Tracking | ✅ |
| Input-Handling (MouseDown/Up, Scroll, TextInput, KeyDown) | ✅ |
| HitTest (rekursiv, Z-Order-basiert, Multi-Widget) | ✅ |
| Koordinaten-Transformation (Window→Viewport) | ✅ |
| Selektion + SelectionChanged-Callback | ✅ |
| Sichtbarkeitssteuerung (setVisible/isVisible) | ✅ |
| Render-Dirty-Tracking | ✅ |
| Gameplay-Cursor-Steuerung (`setGameplayCursorVisible`) | ✅ |
| Automatische Kamera-Input-Blockade bei sichtbarem Cursor | ✅ |
| Integration in `OpenGLRenderer` (`m_viewportUIManager`) | ✅ |
| `renderViewportUI()` (Full-FBO-Viewport, Ortho-Offset, Scissor, Multi-Widget) | ✅ |
| Input-Routing in `main.cpp` (Editor-UI → Viewport-UI → 3D) | ✅ |
| Auto-Cleanup aller Widgets bei PIE-Stop | ✅ |

### 26.2 Asset-Integration (Phase 2 – ✅ Vereinfacht)

Gameplay-UI wird ausschließlich über Widget-Assets gesteuert. Widgets werden im Widget Editor gestaltet und per `engine.ui.spawn_widget` zur Laufzeit angezeigt. Die dynamische Widget-Erstellung per Script (`engine.viewport_ui`) wurde entfernt.

| Feature                                          | Status |
|--------------------------------------------------|--------|
| Canvas-Panel als Root jedes neuen Widgets (`isCanvasRoot`) | ✅ |
| Canvas-Root-Löschschutz im Widget Editor         | ✅ |
| `isCanvasRoot`/`anchor`/`anchorOffset` Serialisierung | ✅ |
| Normalisierte from/to-Werte (0..1) im Viewport korrekt skaliert | ✅ |
| 3-Case-Layout in `ResolveAnchorsRecursive` (Normalized/Stretch/Anchor-basiert) | ✅ |

### 26.3 UI Designer Tab (Phase B ✅)

| Feature                                          | Status |
|--------------------------------------------------|--------|
| `UIDesignerState`-Struct in `UIManager.h`        | ✅ |
| Editor-Tab (wie MeshViewer, kein Popup)          | ✅ |
| Toolbar-Widget (New Widget, Delete Widget, Status-Label) | ✅ |
| Linkes Panel (250px): Controls-Palette + Widget-Hierarchie-TreeView | ✅ |
| Controls-Palette (7 Typen: Panel/Text/Label/Button/Image/ProgressBar/Slider) | ✅ |
| Widget-Hierarchie mit Selektion + Highlighting   | ✅ |
| Rechtes Panel (280px): Properties-Panel (dynamisch, typ-basiert) | ✅ |
| Properties: Identity, Transform, Anchor (Dropdown + Offset X/Y), Hit Test (Mode-Dropdown: Enabled/DisabledSelf/DisabledAll), Layout, Appearance, Text, Image, Value | ✅ |
| Bidirektionale Sync (Designer ↔ Viewport via `setOnSelectionChanged`) | ✅ |
| `HitTestMode`-Enum (Enabled/DisabledSelf/DisabledAll) pro WidgetElement mit Parent-Override | ✅ |
| "UI Designer" im Settings-Dropdown               | ✅ |
| Selektions-Highlight im Viewport (orangefarbener 2px-Rahmen) | ✅ |
| `addElementToViewportWidget()` (7 Element-Typen mit Defaults) | ✅ |
| `deleteSelectedUIDesignerElement()` (rekursive Entfernung, Canvas-Root geschützt) | ✅ |

### 26.4 Scripting (Phase A – Vereinfacht ✅)

| Feature                                          | Status |
|--------------------------------------------------|--------|
| `engine.ui.spawn_widget(path)` (Auto `.asset`-Endung) | ✅ |
| `engine.ui.remove_widget(name)` | ✅ |
| `engine.ui.show_cursor(bool)` + Kamera-Blockade | ✅ |
| `engine.ui.clear_all_widgets()` | ✅ |
| `engine.pyi` IntelliSense-Stubs (aktualisiert) | ✅ |
| Auto-Cleanup Script-Widgets bei PIE-Stop         | ✅ |
| ~~`engine.viewport_ui` Python-Modul (28 Methoden)~~ | ❌ Entfernt |

**Offene Punkte (Phase D – Zukunft):**
- Undo/Redo für Designer-Aktionen
- Drag & Drop aus Palette in Viewport
- Copy/Paste von Elementen
- Responsive-Preview (Fenstergrößen-Simulation)
- Detaillierter Plan mit Fortschritts-Tracking in `GAMEPLAY_UI_PLAN.md`

---

## 22. Landscape-System

| Feature                                           | Status |
|---------------------------------------------------|--------|
| `LandscapeManager` (`src/Landscape/LandscapeManager.h/.cpp`) | ✅ |
| `LandscapeParams` (name, width, depth, subdX, subdZ) | ✅ |
| Flaches Grid-Mesh (N×M Kacheln, XZ-Ebene)        | ✅ |
| Vertex-Format: x, y, z, u, v (5 Floats)          | ✅ |
| Mesh als `.asset`-JSON in `Content/Landscape/` speichern | ✅ |
| Asset über `AssetManager::loadAsset()` registrieren | ✅ |
| ECS-Entity mit Transform + Mesh + Name + Material (WorldGrid) + CollisionComponent (HeightField) + HeightFieldComponent + PhysicsComponent (Static) | ✅ |
| Level-Dirty-Flag + Outliner-Refresh nach Spawn   | ✅ |
| Landscape-Erstellung Undo/Redo-Action            | ✅ |
| Grid-Shader mit vollem Lighting (Multi-Light, Schatten) | ✅ |
| Landscape Manager Popup (via `TitleBar.Menu.Tools`) | ✅ |
| Popup-UI: Name, Width, Depth, Subdiv X, Subdiv Z, Create/Cancel | ✅ |
| Nur ein Landscape pro Szene (Popup blockiert bei existierendem) | ✅ |
| HeightField Debug Wireframe (grünes Gitter-Overlay im Viewport) | ✅ |
| Höhenkarte (Heightmap)                            | ❌ |
| Landscape-Material / Textur-Blending             | ❌ |
| LOD-System für Landscape                         | ❌ |
| HeightField-Collider für Landscape (Jolt HeightFieldShape aus Höhendaten) | ✅ |
| Terrain-Sculpting im Editor                      | ❌ |

**Offene Punkte:**
- Aktuell nur flache Ebene – keine Höhenkarte (HeightField-Collider ist vorbereitet, Höhendaten standardmäßig 0)
- HeightField Debug Wireframe: Rendert das HeightField-Kollisionsgitter als grünes Wireframe-Overlay im Viewport (Engine Settings → Debug → HeightField Debug). Automatischer Rebuild bei ECS-Änderungen via `getComponentVersion()`. Nutzt den bestehenden `boundsDebugProgram`-Shader.
- Für große Terrains empfiehlt sich später LOD + Streaming

---

## 23. Skybox-System

| Feature                                           | Status |
|---------------------------------------------------|--------|
| Cubemap-Skybox Rendering (6 Faces: right/left/top/bottom/front/back) | ✅ |
| Skybox-Shader (eigener Vertex+Fragment, depth=1.0 trick) | ✅ |
| Skybox-Pfad pro Level (JSON-Feld `"Skybox"`)     | ✅ |
| Level-Serialisierung / Deserialisierung           | ✅ |
| Automatisches Laden beim Levelwechsel             | ✅ |
| Skybox im Runtime-Build (setSkyboxPath nach Level-Load) | ✅ |
| Skybox-(Re-)Load bei Scene-Prepare                | ✅ |
| `setSkyboxPath()` / `getSkyboxPath()` API         | ✅ |
| Face-Formate: JPG, PNG, BMP                       | ✅ |
| **Skybox Asset-Typ (`AssetType::Skybox`)**        | ✅ |
| Skybox `.asset`-Datei (JSON mit 6 Face-Pfaden + folderPath) | ✅ |
| Skybox Load / Save / Create im AssetManager       | ✅ |
| Content Browser Icon + Tint (sky blue)            | ✅ |
| WorldSettings UI: Skybox-Pfad-Eingabe             | ✅ |
| Dirty-Tracking bei Skybox-Änderung + StatusBar-Refresh | ✅ |
| Diagnose-Logging bei Skybox-Ladefehlern           | ✅ |
| Auflösung von `.asset`-Pfaden im Renderer         | ✅ |
| Content-relativer Ordnerpfad-Fallback (`getAbsoluteContentPath`) | ✅ |
| Korrekte OpenGL-Cubemap-Face-Zuordnung (front→-Z, back→+Z) | ✅ |
| Default-Skybox-Assets (Sunrise, Daytime) Auto-Generierung | ✅ |
| Engine Content-Ordner `Content/Textures/SkyBoxes/` | ✅ |
| HDR / Equirectangular Skybox                      | ❌ |
| Skybox-Rotation                                   | ❌ |
| Skybox-Tinting / Blending                         | ❌ |

**Offene Punkte:**
- Cubemap-Faces aus einem Ordner (right/left/top/bottom/front/back.jpg/.png/.bmp)
- Kein HDR/equirectangular Support – nur LDR-Cubemaps
- Im WorldSettings-Panel kann der Skybox-Pfad als Content-relativer `.asset`-Pfad oder Ordnerpfad eingegeben werden
- Default-Skyboxen (Sunrise, Daytime) werden automatisch beim Projektladen erstellt, wenn die Face-Bilder im Engine-Content unter `Content/Textures/SkyBoxes/` vorhanden sind
- Face-Zuordnung: `right`→`+X`, `left`→`-X`, `top`→`+Y`, `bottom`→`-Y`, `front`→`-Z` (Blickrichtung), `back`→`+Z`

---

## 18. Scripting (Python)

### 18.1 Infrastruktur

| Feature                              | Status |
|--------------------------------------|--------|
| CPython eingebettet                  | ✅     |
| engine-Modul registriert            | ✅     |
| Script-Lebenszyklus (PIE)           | ✅     |
| on_level_loaded() Callback          | ✅     |
| onloaded(entity) Callback           | ✅     |
| tick(entity, dt) pro Frame          | ✅     |
| engine.pyi statisch deployed (CMake + copy) | ✅     |
| Async-Asset-Load Callbacks          | ✅     |
| Mehrere Scripts pro Level           | ✅     |
| Script-Fehlerbehandlung             | 🟡     |
| Script-Debugger                     | ❌     |
| Script Hot-Reload                   | ✅     |

### 18.2 Script-API Module

| Submodul                  | Status |
|---------------------------|--------|
| engine.entity (CRUD, Transform, Mesh, Light) | ✅ |
| engine.assetmanagement    | ✅     |
| engine.audio              | ✅     |
| engine.input              | ✅     |
| engine.ui                 | ✅     |
| engine.camera             | ✅     |
| engine.diagnostics (delta_time, engine_time, state, cpu_info, gpu_info, ram_info, monitor_info) | ✅     |
| engine.logging            | ✅     |
| engine.physics            | ✅     |
| engine.math (Vec2, Vec3, Quat, Scalar, Trig — C++-Berechnung) | ✅ |
| engine.renderer (Shader-Parameter etc.) | ❌ |

**Offene Punkte:**
- Script-Fehler werden geloggt, aber kein detailliertes Error-Recovery (Script crasht → Fehlermeldung, aber kein Retry)
- Kein Script-Debugger (Breakpoints etc.)
- Kein Zugriff auf Renderer-Parameter (z.B. Material-Uniforms) aus Python
- `engine.math` bietet 54 Funktionen: Vec3 (17), Vec2 (9), Quaternion (7), Scalar (4), Trigonometrie (7: sin, cos, tan, asin, acos, atan, atan2), Common Math (10: sqrt, abs, pow, floor, ceil, round, sign, min, max, pi) — alle Berechnungen laufen in C++

---

## 19. Build-System

| Feature                              | Status |
|--------------------------------------|--------|
| CMake ≥ 3.12                        | ✅     |
| C++20-Standard                       | ✅     |
| MSVC-Unterstützung (VS 18 2026)    | ✅     |
| x64-Plattform                       | ✅     |
| SHARED/DLL-Bibliotheken             | ✅     |
| Debug-Postfix entfernt (kein "d")  | ✅     |
| Debug-Python-Workaround             | ✅     |
| Profiling-Flag (/PROFILE)           | ✅     |
| **Renderer als Renderer.dll** (RendererCore OBJECT + OpenGL SHARED) | ✅ |
| **Factory-Pattern** (Backend über `config.ini` wählbar) | ✅ |
| GCC/Clang-Unterstützung             | ❌     |
| Linux/macOS-Build                   | ❌     |
| CI/CD-Pipeline                      | ❌     |
| Automatische Tests                  | ❌     |
| Package-Manager (vcpkg/conan)       | ❌     |

**Offene Punkte:**
- Nur MSVC / Windows – kein GCC, Clang, Linux, macOS
- Keine CI/CD-Pipeline
- Keine Unit-Tests oder Integrationstests
- Externe Bibliotheken werden manuell verwaltet (kein vcpkg/conan)

---

## 20. Gesamtübersicht fehlender Systeme

Große Feature-Blöcke, die noch nicht existieren:

| System                            | Priorität | Beschreibung                                                                   |
|-----------------------------------|-----------|--------------------------------------------------------------------------------|
| **Physik-Engine (Jolt)**         | ✅     | Jolt Physics v5.5.1 Backend: Fixed Timestep, Box/Sphere-Kollision, Constraint-Solving, Sleep, Raycast. `PhysicsWorld`-Singleton, `engine.physics` Python-API |
| **Physik-Engine (PhysX)**        | ✅     | NVIDIA PhysX 5.6.1 Backend (optional, `ENGINE_PHYSX_BACKEND`): Box/Sphere/Capsule/Cylinder/HeightField-Collider, Kontakt-Callbacks, Raycast, Sleep. Statische Libs, DLL-CRT, `/WX-` Override. |
| **3D-Modell-Import (Assimp)**    | ✅     | Import von OBJ, FBX, glTF, GLB, DAE, 3DS, STL, PLY, X3D via Assimp inkl. automatischer Material- und Textur-Extraktion (Diffuse, Specular, Normal, Emissive; extern + eingebettet) |
| **Entity-Hierarchie**            | Mittel    | Parent-Child-Beziehungen für Entities (kein ParentComponent im ECS)           |
| **Entity-Kamera (Runtime)**      | ✅     | Entity-Kamera via `setActiveCameraEntity()` mit FOV/NearClip/FarClip aus CameraComponent |
| **PBR-Material**                 | ✅     | Cook-Torrance BRDF (GGX NDF + Smith-Schlick Geometry + Fresnel-Schlick), Metallic/Roughness Workflow, glTF-kompatible metallicRoughness Map (G=Roughness, B=Metallic, Slot 4), Scalar-Fallback (uMetallic/uRoughness), auto-PBR-Erkennung beim Assimp-Import, abwärtskompatibel mit Blinn-Phong |
| **Normal Mapping**               | ✅     | TBN-Matrix (Gram-Schmidt), Tangent/Bitangent pro Vertex, material.normalMap (Slot 2), uHasNormalMap Uniform |
| **Post-Processing**              | ✅     | HDR FBO, Gamma Correction, ACES Tone Mapping, FXAA 3.11 Quality (deferred, nach Gizmo/Outline, Content-Rect Viewport, 9-Sample Neighbourhood, Edge Walking 12 Steps, Subpixel Correction), MSAA 2x/4x, Bloom (5-Mip Gaussian), SSAO (32-Sample, Half-Res, Bilateral Depth-Aware 5×5 Blur). |
| **Cascaded Shadow Maps**         | ✅     | 4-Kaskaden CSM für Directional Lights: Practical Split (λ=0.75), 2048² pro Kaskade, tight Ortho-Projektion, View-Space Cascade Selection, 5×5 PCF, Toggle in Engine Settings (Lighting) |
| **Skeletal Animation**           | Mittel    | Bone-System, Skinning, Animation-Blending                                    |
| **Cubemap / Skybox**            | ✅     | 6-Face Cubemap Rendering, Skybox-Shader, Skybox-Pfad pro Level (JSON), WorldSettings UI, Drag&Drop |
| **Drag & Drop (Editor)**        | ✅     | Model3D→Spawn (Depth-Raycast + Undo/Redo), Material/Script→Apply (pickEntityAtImmediate), Asset-Move mit tiefem Referenz-Scan aller .asset-Dateien, Entf zum Löschen (mit Undo/Redo), EntityDetails Drop-Zones mit Typ-Validierung, OS-Datei-Drop (`SDL_EVENT_DROP_FILE`) → Auto-Import via `AssetManager::importAssetFromPath()` mit automatischer Typ-Erkennung (Texture/Model3D/Audio/Shader/Script) |
| **Asset Rename (Editor)**       | ✅     | Rename-Button in Content-Browser PathBar (aktiv bei selektiertem Asset) + F2-Tastenkürzel. Inline-EntryBar im Grid-Tile zum Eingeben des neuen Namens. `AssetManager::renameAsset()` benennt Datei + Source-File um, aktualisiert Registry (Name/Pfad/Index), geladene AssetData, ECS-Komponenten (Mesh/Material/Script) und scannt Cross-Asset-Referenzen in .asset-Dateien. Escape bricht ab. |
| **Audio-Formate (OGG/MP3)**     | Niedrig   | Weitere Audio-Formate unterstützen (aktuell nur WAV)                         |
| **3D-Audio (Positional)**       | Niedrig   | OpenAL-Listener-/Source-Positionierung nutzen                                |
| **Particle-System**             | Niedrig   | GPU-/CPU-Partikel für Effekte                                                |
| **Netzwerk / Multiplayer**      | Niedrig   | Netzwerk-Synchronisation, Server/Client                                      |
| **Renderer-Abstrahierung**      | ✅     | Multi-Backend-Architektur: Abstrakte Interfaces (Renderer, Camera, Shader, IRenderObject2D/3D, ITexture, IShaderProgram, ITextRenderer, IRenderTarget, IRenderContext), UIManager entkoppelt, RendererCore OBJECT + Renderer.dll, Factory-Pattern mit Config-basierter Backend-Auswahl → siehe `RENDERER_ABSTRACTION_PLAN.md`. Offen: Integrationstest, Mock-Backend-Tests, Doku-Update |
| **DirectX 11/12 Backend**       | Niedrig   | Alternative Rendering-Backends (aktuell nur OpenGL 4.6)                      |
| **Cross-Platform (Linux/macOS)**| Niedrig   | GCC/Clang-Support, Plattform-Abstraktion                                    |
| **CI/CD + Tests**               | Niedrig   | Automatisierte Builds, Unit-Tests, Integrationstests                         |
| **Script-Debugger**             | Niedrig   | Python-Breakpoints, Step-Through im Editor                                   |
| **Hot-Reload (Assets/Scripts)** | Niedrig   | Dateiänderungen erkennen und automatisch neu laden                           |

### Bereits abgeschlossene Systeme (aus früheren Iterationen)

| System                            | Status | Beschreibung                                                                   |
|-----------------------------------|--------|--------------------------------------------------------------------------------|
| **Undo/Redo**                    | ✅     | Command-Pattern für Editor-Aktionen (UndoRedoManager-Singleton, Ctrl+Z/Y, StatusBar-Buttons). Entity-Löschen (DELETE) mit vollständigem Komponenten-Snapshot (Einzel- und Gruppen-Delete), Entity-Spawn (Drag-and-Drop) und Landscape-Erstellung erzeugen Undo/Redo-Actions. Multi-Entity-Transform-Undo via `m_gizmoDragOldTransforms`-Map. **Erweitert (Phase 8.4):** Alle Details-Panel-Wertänderungen (Transform, Light, Camera, Collision, Physics, ParticleEmitter, Name), Komponenten-Hinzufügen/Entfernen und Asset-Zuweisungen (Mesh/Material/Script) sind jetzt vollständig undoable via `setCompFieldWithUndo<>`-Template-Helper. |
| **Editor-Gizmos**               | ✅     | Translate/Rotate/Scale-Gizmos für Entity-Manipulation (W/E/R Shortcuts). Multi-Entity-Gruppen-Gizmo: Transformiert alle selektierten Entities gleichzeitig mit einem einzigen Undo-Command. |
| **Shadow Mapping (Dir/Spot)**    | ✅     | Multi-Light Shadow Maps für bis zu 4 Directional/Spot Lights, 5×5 PCF       |
| **Shadow Mapping (Point Lights)**| ✅     | Omnidirektionale Cube-Map Shadows für bis zu 4 Point Lights via Geometry-Shader |
| **Popup-UI Refactoring**         | ✅     | Landscape-Manager- und Engine-Settings-Popup-Erstellung aus `main.cpp` in `UIManager` verschoben (`openLandscapeManagerPopup`, `openEngineSettingsPopup`). UIManager hält jetzt einen Back-Pointer auf `OpenGLRenderer`. |
| **Performance-Optimierungen**    | ✅     | O(1)-Asset-Lookup via `m_loadedAssetsByPath`-Index (statt O(n)-Scan), Shader-Pfad-Cache in `OpenGLObject3D`, deduplizierte Model-Matrix-Berechnung in `renderWorld()`. |
| **Paralleles Asset-Laden**       | ✅     | Dreiphasen-Architektur: `readAssetFromDisk()` (thread-safe Disk-I/O + CPU), `finalizeAssetLoad()` (Registration), GPU-Upload. Thread-Pool mit `hardware_concurrency()` Threads + globaler Job-Queue. `loadBatchParallel()` dispatched in den Pool mit Batch-Wait (atomic counter + CV). `preloadLevelAssets()` warmed den Cache beim Scene-Prepare mit allen Mesh-, Material- und Textur-Assets. |
| **Physik-System (Jolt)**         | ✅     | `PhysicsWorld`-Singleton mit Backend-Abstraktion (`IPhysicsBackend`). `JoltBackend` (Jolt Physics v5.5.1). Zwei ECS-Komponenten: `CollisionComponent` + `PhysicsComponent`. BodyDesc/BodyState für backend-agnostische Body-Verwaltung. ECS↔Backend-Sync in PhysicsWorld, alle Jolt-spezifischen Typen in JoltBackend isoliert. |
| **Physik-System (PhysX)**        | ✅     | `PhysXBackend` (NVIDIA PhysX 5.6.1, `external/PhysX/`). Optional via `ENGINE_PHYSX_BACKEND` CMake-Option. Kontakt-Callbacks (`SimCallbackImpl`), Euler↔Quat-Konvertierung, PVD-Support. `PhysicsWorld::Backend`-Enum (Jolt/PhysX) für Backend-Auswahl bei `initialize()`. |
| **Keyboard-Shortcut-System**    | ✅     | `ShortcutManager` Singleton (`src/Core/ShortcutManager.h/.cpp`). 20 Shortcuts registriert (Editor/Gizmo/Debug/PIE). Konfigurations-UI in Editor Settings (Rebind-Buttons, KeyCapture, Konflikt-Erkennung, Reset All). F1 Shortcut-Hilfe Popup. Persistenz via `shortcuts.cfg` im Projektverzeichnis. |
| **Keyboard-Navigation**         | ✅     | Phase 8.3: Tab/Shift+Tab zykelt durch EntryBar-Felder, Escape schließt Dropdowns→Modals→Entry-Fokus (Kaskade), Pfeiltasten navigieren Outliner-Entity-Liste und Content-Browser-Grid. Fokus-Highlight: Blauer Outline auf fokussierte EntryBars. Hilfsfunktionen: `cycleFocusedEntry()`, `navigateOutlinerByArrow()`, `navigateContentBrowserByArrow()`. |

---

## 24. Physik-System (Jolt Physics / PhysX)

| Feature                                               | Status |
|-------------------------------------------------------|--------|
| **Backend-Abstraktion (`IPhysicsBackend`-Interface)** | ✅     |
| **Backend: Jolt Physics v5.5.1** (`JoltBackend`, `external/jolt/`) | ✅ |
| **Backend: NVIDIA PhysX 5.6.1** (`PhysXBackend`, `external/PhysX/`) | ✅ |
| `PhysicsWorld`-Singleton (backend-agnostisch, `src/Physics/PhysicsWorld.h/.cpp`) | ✅ |
| `PhysicsWorld::Backend`-Enum (Jolt/PhysX) + `initialize(Backend)` | ✅ |
| `IPhysicsBackend`-Interface (`src/Physics/IPhysicsBackend.h`) | ✅ |
| `JoltBackend`-Implementierung (`src/Physics/JoltBackend.h/.cpp`) | ✅ |
| `PhysXBackend`-Implementierung (`src/Physics/PhysXBackend.h/.cpp`) | ✅ |
| `BodyDesc`-Struct (backend-agnostische Body-Erstellung) | ✅ |
| `BodyState`-Struct (backend-agnostischer Body-Readback) | ✅ |
| Fixed Timestep (1/60 s, Akkumulator)                 | ✅     |
| Gravitation (konfigurierbar, Default 0/-9.81/0)      | ✅     |
| **Komponenten-Split: `CollisionComponent` + `PhysicsComponent`** | ✅ |
| `CollisionComponent`: Form, Oberfläche, Sensor       | ✅     |
| `PhysicsComponent`: Dynamik (optional, Default=Static)| ✅     |
| Collider: Box, Sphere, Capsule, Cylinder, HeightField | ✅    |
| Collider-Offset                                       | ✅     |
| Sensor/Trigger-Volumes (`isSensor`)                   | ✅     |
| MotionType (Static/Kinematic/Dynamic)                 | ✅     |
| GravityFactor (pro Body)                              | ✅     |
| LinearDamping / AngularDamping                        | ✅     |
| MaxLinearVelocity / MaxAngularVelocity                | ✅     |
| MotionQuality: Discrete / LinearCast (CCD)            | ✅     |
| AllowSleeping (pro Body)                              | ✅     |
| ECS↔Backend Synchronisation (`syncBodiesToBackend`/`syncBodiesFromBackend`) | ✅ |
| Dynamische Body-Erzeugung/-Löschung pro Frame        | ✅     |
| Kollisions-Callbacks (`setCollisionCallback`, `CollisionEvent`) | ✅ |
| Raycast (delegiert an Backend)                        | ✅     |
| Sleep/Deactivation                                    | ✅     |
| Overlap-Tracking (Begin/End pro Frame)               | ✅     |
| Per-Entity Overlap-Script-Callbacks (`on_entity_begin_overlap` / `on_entity_end_overlap`) | ✅ |
| ECS-Serialisierung (Collision + Physics + HeightField separat) | ✅ |
| Backward-Kompatibilität (`deserializeLegacyPhysics`) | ✅     |
| Editor-UI: Collision-Sektion (Dropdown inkl. HeightField, Size, Offset, Sensor) | ✅ |
| Editor-UI: Physics-Sektion (MotionType, Damping, CCD, etc.) | ✅ |
| Engine Settings: Physics-Backend-Dropdown (Jolt / PhysX)   | ✅ |
| PIE-Integration (init/step/shutdown)                 | ✅     |
| `engine.physics` Python-API (11 Funktionen)          | ✅     |
| `engine.pyi` Stubs + `Component_Collision` Konstante | ✅     |
| CMake: `Physics` SHARED-Bibliothek (linkt Jolt + optional PhysX) | ✅ |
| CMake: `ENGINE_PHYSX_BACKEND` Option + `ENGINE_PHYSX_BACKEND_AVAILABLE` Define | ✅ |
| PhysX: Statische Libs, DLL-CRT-Override (`/MDd`/`/MD`), `/WX-` Override | ✅ |
| PhysX: Stub-freeglut für PUBLIC_RELEASE-Build                 | ✅     |
| PhysX: PxFoundation/PxPhysics/PxScene/PxPvd Lifecycle         | ✅     |
| PhysX: Kontakt-Callbacks (`SimCallbackImpl`, `PxSimulationEventCallback`) | ✅ |
| Euler↔Quaternion (Y·X·Z Rotationsreihenfolge)        | ✅     |
| Jolt JobSystemThreadPool (multi-threaded Solver)     | ✅     |
| Mesh-Collider (Fallback → Box)                       | ⚠️     |
| PhysX-Backend                                         | ✅     |
| Jolt Constraints / Joints                             | ❌     |
| Mesh-Shape (Triangle-Mesh via Jolt `MeshShape`)      | ❌     |
| Convex-Hull-Collider                                  | ❌     |

**Offene Punkte:**
- Backend-Abstraktion abgeschlossen: `IPhysicsBackend`-Interface mit `BodyDesc`/`BodyState`/`CollisionEventData`/`RaycastResult`-Structs. `PhysicsWorld` delegiert an `m_backend`. Zwei Backends: `JoltBackend` (Jolt 5.5.1) und `PhysXBackend` (PhysX 5.6.1). Backend-Auswahl über `PhysicsWorld::initialize(Backend)`.
- Engine Settings enthält ein Physics-Backend-Dropdown (Jolt / PhysX) unter der Physics-Kategorie. Die Auswahl wird in `DiagnosticsManager` persistiert (`PhysicsBackend`-Key) und beim PIE-Start ausgelesen, um das gewählte Backend zu initialisieren. PhysX-Option erscheint nur wenn `ENGINE_PHYSX_BACKEND_AVAILABLE` gesetzt ist.
- PhysX-Backend ist optional (`ENGINE_PHYSX_BACKEND` CMake-Option). Wenn `external/PhysX` nicht vorhanden ist, wird nur Jolt gebaut. Conditional compile via `ENGINE_PHYSX_BACKEND_AVAILABLE` Define.
- PhysX-Integration erfordert CRT-Override (DLL-Runtime `/MD(d)`) und `/WX-` für alle PhysX-Targets, da PhysX `CMAKE_CXX_FLAGS` wholesale ersetzt und `/WX` (Warnings-as-Errors) verwendet.
- Mesh-Collider (Typ 4) fällt aktuell auf Box zurück – Jolt `MeshShape`/`ConvexHullShape` noch nicht integriert
- Keine Jolt-Constraints/Joints genutzt (Gelenke, Federn, etc.)
- **Bugfix: PhysX HeightField Fall-Through** – `PhysXBackend::createBody()` behandelte `heightSampleCount` fälschlich als Gesamtzahl (√N), obwohl es die Per-Side-Anzahl ist. Zusätzlich fehlte die Anwendung des HeightField-Offsets als Shape-Local-Pose und Row/Column-Scales waren vertauscht. Behoben: Direktverwendung von `heightSampleCount`, Offset als `setLocalPose`, korrektes Scale-Mapping (Row=Z, Column=X).
- **Bugfix: Jolt HeightField Stuck** – Jolt erfordert `sampleCount = 2^n + 1` (z.B. 3, 5, 9, 17). Der LandscapeManager erzeugte `sampleCount = gridSize + 1 = 4`, was Jolts `HeightFieldShapeSettings::Create()` zum Fehler veranlasste und ein winziges BoxShape-Fallback einsetzte. Behoben: (1) `JoltBackend` resampled per bilinearer Interpolation auf den nächsten gültigen Count, (2) `LandscapeManager` rundet gridSize auf die nächste Zweierpotenz auf.
- **Bugfix: Crash bei Projekterstellung (Use-After-Free)** – Der temporäre `UIManager` (Projekt-Auswahl-Screen) registrierte einen `ActiveLevelChangedCallback` beim `DiagnosticsManager` mit `this`-Capture, wurde aber zerstört ohne den Callback abzumelden. Beim anschließenden `createProject()` → `setActiveLevel()` wurde der dangling Callback aufgerufen → Crash. Behoben: Callback-System auf Token-basierte `unordered_map` umgestellt (`registerActiveLevelChangedCallback` gibt `size_t`-Token zurück, `unregisterActiveLevelChangedCallback(token)` entfernt ihn). `UIManager::~UIManager()` meldet den Callback sauber ab.
- ✅ `Keyboard-Shortcut-System (Phase 6.2)`: Zentrales, konfigurierbares Shortcut-System implementiert. **ShortcutManager** Singleton (`src/Core/ShortcutManager.h/.cpp`) mit Registry aller Aktionen (id, displayName, category, defaultCombo, currentCombo, phase, callback). 20 Shortcuts registriert in `main.cpp` (Editor: Undo/Redo/Save/Copy/Paste/Duplicate/Delete/SearchCB/FocusSelected/DropToSurface/ImportDialog/ToggleFPSCap, Gizmo: W/E/R, Debug: F8/F9/F10/F11, PIE: Escape/Shift+F1). **Konfigurations-UI** in Editor Settings Popup: Shortcut-Liste nach Kategorien gruppiert, klickbare Keybind-Buttons zum Umbelegen (KeyCaptureCallback-Mechanismus in UIManager), Konflikt-Erkennung mit Logger-Warnung, „Reset All to Defaults" Button. **F1 Shortcut-Hilfe Popup**: `openShortcutHelpPopup()` zeigt alle registrierten Shortcuts als scrollbare Liste nach Kategorien sortiert. **Persistenz**: `shortcuts.cfg` im Projektverzeichnis (Text-Format: id key mods), geladen nach Shortcut-Registrierung, gespeichert beim Shutdown.

---

*Generiert aus Analyse des Quellcodes. Stand: aktueller Branch `Json_and_ecs`.*
