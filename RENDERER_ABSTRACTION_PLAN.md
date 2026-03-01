# Renderer Abstraction Plan

> Ziel: Den Renderer so weit abstrahieren, dass andere Backends (Vulkan, DirectX, Metal, Software) eingebaut werden können, ohne die Engine-Logik anfassen zu müssen.

---

## 1. Ist-Zustand: Was ist bereits abstrahiert?

### ✅ `Renderer` (Basisklasse) — `src/Renderer/Renderer.h`
- Dünnes virtuelles Interface (~36 Zeilen)
- Methoden: `initialize()`, `shutdown()`, `clear()`, `render()`, `present()`, `name()`, `window()`
- Kamera-Steuerung: `moveCamera()`, `rotateCamera()`, `getCameraPosition()`, `setCameraPosition()`, `getCameraRotationDegrees()`, `setCameraRotationDegrees()`, `setActiveCameraEntity()`, `getActiveCameraEntity()`, `clearActiveCameraEntity()`
- Picking: `screenToWorldPos()`
- **Problem:** Viel zu dünn — `OpenGLRenderer` hat ~100+ öffentliche Methoden, die nicht im Interface sind.

### ✅ `Camera` (Basisklasse) — `src/Renderer/Camera.h`
- Vollständig backend-agnostisch (~29 Zeilen)
- Pure virtual: `move()`, `moveRelative()`, `rotate()`, `setPosition()`, `getRotationDegrees()`, `setRotationDegrees()`, `getViewMatrixColumnMajor()`, `getPosition()`
- **Gutes Muster** — kann als Vorlage für weitere Abstraktionen dienen.

### ✅ `Shader` (Basisklasse) — `src/Renderer/Shader.h`
- Vollständig backend-agnostisch (~27 Zeilen)
- Pure virtual: `loadFromSource()`, `loadFromFile()`, `compile()`, `type()`, `source()`, `compileLog()`
- **Gutes Muster.**

### ✅ `UIWidget` / `WidgetElement` — `src/Renderer/UIWidget.h`
- Weitgehend backend-agnostisch (~196 Zeilen)
- Verwendet Engine-eigene Typen (Vec2, Vec4)
- `textureId` (unsigned int) und Shader-Pfad-Strings sind abstrakt genug
- **Geringes Risiko**, kann so bleiben.

---

## 2. Ist-Zustand: Was ist zu stark an OpenGL gekoppelt?

### ❌ `OpenGLRenderer` — `src/Renderer/OpenGLRenderer/OpenGLRenderer.h`
- **~493 Zeilen Header**, ~100+ öffentliche Methoden, ~150+ private Member
- Verwendet direkt: `GLuint`, `GLint`, `glm::mat4`, `glm::vec3`, `glad/gl.h`
- Enthält komplette Subsysteme, die im abstrakten Interface fehlen:
  - **UI-Rendering** (`renderWidgets()`, `renderUIElement()`, `renderDropdownOverlay()`, `renderDragOverlay()`)
  - **Shadow Maps** (Directional + Point: `m_shadowFBO`, `m_shadowDepthTex`, `m_pointShadowFBO`, `m_pointShadowCubemapTex`)
  - **Entity Picking** (`m_pickFBO`, `m_pickColorTex`, `m_pickDepthRBO`, `pickEntityAtScreen()`)
  - **Selection Outline** (`m_outlineFBO`, `m_outlineColorTex`, `m_outlineDepthRBO`)
  - **Gizmo System** (`renderGizmo()`, `startGizmoDrag()`, `updateGizmoDrag()`, `endGizmoDrag()`)
  - **HZB Occlusion Culling** (`m_hzbFBO`, `m_hzbTexture`, `buildHZB()`, `isOccluded()`)
  - **Skybox** (`m_skyboxVAO`, `m_skyboxVBO`, `m_skyboxCubemap`, `m_skyboxProgram`)
  - **Editor Tabs** (Struct `EditorTab` mit `GLuint fbo, colorTex, depthRbo, snapshotTex`)
  - **Popup Windows** (`openPopupWindow()`, `closePopupWindow()`)
  - **Mesh Viewer** (`openMeshViewer()`, `closeMeshViewer()`)
  - **GPU Timer Queries** (`m_gpuTimerQueries`, `m_gpuTimeMs`)
  - **Debug-Toggles** (`toggleUIDebug()`, `toggleBoundsDebug()`, `setShadowsEnabled()`, `setVSyncEnabled()`, `setWireframeEnabled()`, `setOcclusionCullingEnabled()`)

### ❌ `UIManager` — `src/Renderer/UIManager.h`
- **Direkte Kopplung** an `OpenGLRenderer*`:
  - Member: `OpenGLRenderer* m_renderer` (Zeile ~112)
  - API: `setRenderer(OpenGLRenderer*)`, `getRenderer()` → `OpenGLRenderer*`
  - Aufrufe in `.cpp`: `m_renderer->openPopupWindow()`, `m_renderer->closePopupWindow()`, `m_renderer->openMeshViewer()`
  - Engine Settings Popup: `renderer->isShadowsEnabled()`, `renderer->setShadowsEnabled()`, `renderer->isVSyncEnabled()`, etc.

### ❌ `RenderResourceManager` — `src/Renderer/RenderResourceManager.h`
- Öffentliche API verwendet OpenGL-spezifische Typen:
  - `getOrCreateObject2D()` → `OpenGLObject2D*`
  - `getOrCreateObject3D()` → `OpenGLObject3D*`
  - `prepareTextRenderer()` → `OpenGLTextRenderer*`
  - Cache-Maps: `m_object2DCache`, `m_object3DCache`, `m_textRenderer`

### ❌ `PopupWindow` — `src/Renderer/PopupWindow.h`
- Enthält `SDL_GLContext` als direkten Member
- Kommentar: "shared GL context"

### ❌ `SplashWindow` — `src/Renderer/SplashWindow.h`
- Raw `unsigned int` für GL-Handles: `quadVao`, `quadVbo`, `quadProgram`
- `void*` für `glContext`

### ❌ `MeshViewerWindow` — `src/Renderer/EditorWindows/MeshViewerWindow.h`
- Referenziert `OpenGLObject3D` direkt

### ❌ `EditorTab` (in OpenGLRenderer.h)
- Struct enthält: `GLuint fbo`, `GLuint colorTex`, `GLuint depthRbo`, `GLuint snapshotTex`

### ❌ `main.cpp`
- Inkludiert direkt `OpenGLRenderer.h` (Zeile 16)
- Instanziiert `OpenGLRenderer` direkt

### ❌ `CMakeLists.txt` — `src/Renderer/CMakeLists.txt`
- Alle OpenGL-Quellen werden direkt in ein einzelnes `Renderer`-Target kompiliert
- Keine Trennung zwischen abstrakten und backend-spezifischen Dateien

### ✅ GLM-Abhängigkeit
- GLM wurde nach `external/glm/` verschoben (neben SDL3, assimp, etc.)
- Include-Pfad `${CMAKE_SOURCE_DIR}/external` in `src/Renderer/CMakeLists.txt` hinzugefügt
- Alle Backends können GLM über `<glm/glm.hpp>` einbinden

---

## 3. Abstraktionsplan — Schritte mit Verifikation

### Phase 1: Vorbereitungen & Grundlagen

#### ✅ Schritt 1.1: GLM aus dem OpenGL-Ordner herauslösen *(erledigt)*
**Was:** `src/Renderer/OpenGLRenderer/glm/` → `external/glm/` verschoben.
**Warum:** GLM ist keine OpenGL-Bibliothek, sondern allgemeine Mathematik. Andere Backends brauchen sie auch.
**Aufwand:** Gering (Pfade in CMakeLists angepasst).
**Umsetzung:** `${CMAKE_SOURCE_DIR}/external` als PUBLIC include directory in `src/Renderer/CMakeLists.txt` hinzugefügt. Keine Quellcode-Änderungen nötig — alle Includes verwenden bereits `<glm/...>`.

**Verifikation:**
- [x] Projekt kompiliert ohne Fehler nach dem Verschieben
- [x] Alle `#include`-Pfade für GLM sind aktualisiert (keine Änderung nötig)
- [x] Kein OpenGL-spezifischer Code liegt mehr im neuen GLM-Ordner

---

#### ✅ Schritt 1.2: Abstrakte Render-Ressourcen-Typen definieren *(erledigt)*
**Was:** Interfaces für Render-Objekte erstellt:
- `IRenderObject2D` (abstract) ← `OpenGLObject2D` erbt davon
- `IRenderObject3D` (abstract) ← `OpenGLObject3D` erbt davon
- `ITextRenderer` (abstract) ← `OpenGLTextRenderer` erbt davon
- `IShaderProgram` (abstract) ← `OpenGLShaderProgram` erbt davon
- `ITexture` (abstract) ← `OpenGLTexture` erbt davon

**Dateien erstellt in `src/Renderer/`:**
```
src/Renderer/IRenderObject2D.h
src/Renderer/IRenderObject3D.h
src/Renderer/ITextRenderer.h
src/Renderer/IShaderProgram.h
src/Renderer/ITexture.h
```

**Umsetzung:**
- Alle Interfaces mit virtuellem Destruktor und pure-virtual Methoden erstellt
- `IRenderObject3D` verwendet Engine-eigene `Vec3` statt `glm::vec3` für Bounds
- `OpenGLObject3D` bietet zusätzlich `localBoundsMinGLM()`/`localBoundsMaxGLM()` für internen GL-Backend-Zugriff
- `getVertexCount()`/`getIndexCount()` geben `int` statt `GLsizei` zurück
- `OpenGLRenderer.cpp`: 4 ComputeWorldAabb-Aufrufe auf `localBoundsMinGLM()`/`localBoundsMaxGLM()` umgestellt

**Verifikation:**
- [x] Jedes Interface hat einen virtuellen Destruktor
- [x] Alle existierenden OpenGL-Klassen erben korrekt vom jeweiligen Interface
- [x] Projekt kompiliert ohne Fehler
- [ ] Bestehende Funktionalität ist unverändert (Smoke Test: Engine starten, UI interagieren)

---

#### ✅ Schritt 1.3: `RenderResourceManager` auf abstrakte Typen umstellen *(erledigt)*
**Was:** Öffentliche API auf die neuen Interfaces umgestellt:
- `getOrCreateObject2D()` → `shared_ptr<IRenderObject2D>`
- `getOrCreateObject3D()` → `shared_ptr<IRenderObject3D>`
- `prepareTextRenderer()` → `shared_ptr<ITextRenderer>`
- `RenderableAsset` Struct: `object2D`/`object3D` verwenden abstrakte Interface-Typen
- Caches intern auf abstrakte `weak_ptr` umgestellt

**Intern** erstellt `RenderResourceManager` weiterhin konkrete OpenGL-Objekte — die Rückgabe erfolgt als abstrakte Typen (impliziter Upcast).

**Umsetzung:**
- `RenderResourceManager.h`: Forward-Declarations auf `IRenderObject2D`/`IRenderObject3D`/`ITextRenderer` geändert, `RenderableAsset` und öffentliche Methoden verwenden abstrakte Typen
- `RenderResourceManager.cpp`: Methodensignaturen angepasst, Interface-Includes hinzugefügt
- `OpenGLRenderer.cpp`: An allen Stellen, die GL-spezifische Methoden benötigen, `std::static_pointer_cast` auf konkrete Typen eingefügt (getOrCreate-Aufrufe, RenderableAsset→RenderEntry-Zuweisungen, prepareTextRenderer)
- `OpenGLRenderer.h`: Forward-Declaration für `OpenGLTextRenderer` hinzugefügt; `RenderEntry` bleibt bewusst mit konkreten Typen (privat im GL-Backend)

**Verifikation:**
- [x] Alle Aufrufer von `getOrCreate*()` kompilieren ohne Fehler
- [x] `OpenGLRenderer` castet über `static_pointer_cast` zurück auf konkrete Typen (notwendig für GL-spezifische Methoden wie `localBoundsMinGLM()`, `getProgram()`, `setMatrices()`)
- [x] Build erfolgreich — keine Kompilierfehler
- [ ] Engine startet und rendert korrekt (visueller Test: 3D-Szene, UI, Text)

---

### Phase 2: Renderer-Interface erweitern

#### ✅ Schritt 2.1: Renderer-Capabilities abstrahieren *(erledigt)*
**Was:** Enums und Structs definieren, die Backend-Fähigkeiten beschreiben:

```cpp
// src/Renderer/RendererCapabilities.h
struct RendererCapabilities
{
    bool supportsShadows       = false;
    bool supportsOcclusion     = false;
    bool supportsWireframe     = false;
    bool supportsVSync         = false;
    bool supportsEntityPicking = false;
    bool supportsGizmos        = false;
    bool supportsSkybox        = false;
    bool supportsPopupWindows  = false;
};
```

**Umsetzung:** Header `src/Renderer/RendererCapabilities.h` erstellt, kein OpenGL-Include. In `CMakeLists.txt` aufgenommen.

**Verifikation:**
- [x] Struct ist in eigenem Header, kein OpenGL-Include nötig
- [x] Kompiliert ohne Fehler

---

#### ✅ Schritt 2.2: `Renderer`-Interface um fehlende Methoden erweitern *(erledigt)*
**Was:** Die ~100 öffentlichen Methoden von `OpenGLRenderer` kategorisiert und die **engine-relevanten** ins abstrakte Interface aufgenommen.

**Umsetzung:**
- `Renderer.h` von ~36 auf ~130 Zeilen erweitert mit ~60 virtuellen Methoden
- **Pure-virtual (Core):** initialize, shutdown, clear, render, present, name, window, Camera-Steuerung, getUIManager
- **Virtual mit Default (optional):** Rendering-Toggles, Debug-Visualisierungen, Entity Picking, Gizmo, Editor Tabs, Popup Windows, Mesh Viewer, Viewport/Visuals, Scene Management, Performance Metrics, Capabilities
- `GizmoMode` und `GizmoAxis` Enums von `OpenGLRenderer` nach `Renderer` verschoben (werden vererbt)
- `OpenGLRenderer`: ~45 Methoden mit `override` markiert, `getCapabilities()` implementiert (alle Caps = true)
- Return-Type-Anpassungen: `getClearColor()` → `Vec4` (by value), `getSkyboxPath()` → `std::string` (by value), `preloadUITexture()` → `unsigned int`

**Kategorie B — Bleiben OpenGL-spezifisch (private Implementierungsdetails):**
- `buildHZB()`, `isOccluded()` (interne Occlusion-Implementierung)
- `setupShadowMaps()`, `renderShadowPass()` (interne Shadow-Implementierung)
- `renderSelectionOutline()` (interne Outline-Implementierung)
- Alle `m_*FBO`, `m_*Tex`, `m_*RBO` Members
- GL-spezifische Draw-Commands und Batching

**Verifikation:**
- [x] `Renderer.h` kompiliert ohne OpenGL-Includes
- [x] `OpenGLRenderer` überschreibt alle neuen virtuellen Methoden
- [x] Kein Bruch der bestehenden Funktionalität (Build erfolgreich)
- [x] Alle Methoden haben sinnvolle Default-Implementierungen (kein pure virtual für optionale Features)
- [ ] Unit Test: Mock-Renderer kann erstellt werden, der von `Renderer` erbt, ohne OpenGL zu linken

---

#### ✅ Schritt 2.3: `PopupWindow` abstrahieren *(erledigt)*
**Was:** `PopupWindow` hält kein `SDL_GLContext` mehr direkt, sondern ein abstraktes `IRenderContext`-Interface.

**Dateien erstellt:**
```
src/Renderer/IRenderContext.h            ← Abstrakte Schnittstelle (initialize, makeCurrent, destroy)
src/Renderer/OpenGLRenderer/OpenGLRenderContext.h  ← OpenGL-Implementierung (wraps SDL_GLContext)
```

**Umsetzung:**
- `IRenderContext` Interface mit `initialize(SDL_Window*)`, `makeCurrent(SDL_Window*)`, `destroy()` erstellt
- `OpenGLRenderContext` (header-only) kapselt `SDL_GLContext` mit Shared-Context-Erstellung
- `PopupWindow.h`: `SDL_GLContext m_context` → `std::unique_ptr<IRenderContext> m_renderContext`; `glContext()` → `renderContext()`
- `PopupWindow::create()` nimmt jetzt `SDL_WindowFlags extraFlags` und `std::unique_ptr<IRenderContext> context` als Parameter — kein GL-Code mehr in PopupWindow.cpp
- `OpenGLRenderer::openPopupWindow()` erstellt `OpenGLRenderContext` und übergibt `SDL_WINDOW_OPENGL` Flag
- `OpenGLRenderer::renderPopupWindows()` verwendet `popup->renderContext()->makeCurrent()` statt `SDL_GL_MakeCurrent(popup->sdlWindow(), popup->glContext())`

**Verifikation:**
- [x] `PopupWindow.h` hat keinen OpenGL-Include mehr (nur SDL3/SDL.h für SDL_Window)
- [x] `PopupWindow.cpp` enthält keinen GL-spezifischen Code mehr
- [x] Kompilierung erfolgreich — keine Fehler
- [ ] Popup-Fenster funktionieren wie vorher (Smoke Test)
- [ ] OpenGL-Kontext-Sharing funktioniert noch korrekt

---

#### ✅ Schritt 2.4: `SplashWindow` abstrahieren *(erledigt)*
**Was:** GL-Handles (`quadVao`, `quadVbo`, `quadProgram`) durch abstraktes Interface ersetzen oder die Splash-Implementierung Backend-spezifisch machen (z.B. `OpenGLSplashWindow`).

**Umgesetzt: Option A** — `SplashWindow` wurde zur abstrakten Basisklasse, `OpenGLSplashWindow` erbt davon.

**Umsetzung:**
- `SplashWindow.h`: Konvertiert zur abstrakten Basisklasse mit 6 reinen virtuellen Methoden (`create`, `setStatus`, `render`, `close`, `isOpen`, `wasCloseRequested`). Alle GL-spezifischen Member entfernt.
- `OpenGLSplashWindow.h/.cpp`: Neue Dateien in `OpenGLRenderer/` mit der kompletten GL-Implementierung (~390 Zeilen), inklusive Inline-GLSL-Shader, FreeType-Glyph-Atlas, VAOs/VBOs.
- `SplashWindow.cpp`: Gelöscht (Inhalt nach `OpenGLSplashWindow.cpp` verschoben)
- `main.cpp`: Include auf `OpenGLSplashWindow.h` geändert, Typ `SplashWindow` → `OpenGLSplashWindow` (abstrakte Klasse kann nicht auf dem Stack instanziiert werden)
- `CMakeLists.txt`: `SplashWindow.cpp` entfernt, `OpenGLSplashWindow.h/.cpp` hinzugefügt

**Verifikation:**
- [x] `SplashWindow.h` hat keinen OpenGL-Include
- [x] Splash-Screen zeigt sich korrekt beim Start
- [x] Fortschrittsanzeige funktioniert

---

### Phase 3: UIManager entkoppeln

#### ✅ Schritt 3.1: UIManager von `OpenGLRenderer*` auf `Renderer*` umstellen *(erledigt)*
**Was:**
- `m_renderer` Typ ändern: `OpenGLRenderer*` → `Renderer*`
- `setRenderer(OpenGLRenderer*)` → `setRenderer(Renderer*)`
- `getRenderer()` Rückgabe → `Renderer*`

**Umsetzung:**
- `UIManager.h`: Forward-Declaration `OpenGLRenderer` → `Renderer`, Setter/Getter/Member-Typ auf `Renderer*` geändert
- `UIManager.cpp`: `#include "OpenGLRenderer/OpenGLRenderer.h"` → `#include "Renderer.h"`, beide lokale Variablen `OpenGLRenderer* renderer` → `Renderer* renderer`
- Alle `renderer->` Aufrufe nutzen ausschließlich Methoden aus dem abstrakten `Renderer`-Interface (Schritt 2.2)
- `OpenGLRenderer.cpp` Zeile 410: `m_uiManager.setRenderer(this)` funktioniert durch implizite Konversion `OpenGLRenderer*` → `Renderer*`

**Verifikation:**
- [x] `UIManager.h` und `UIManager.cpp` haben keinen `#include "OpenGLRenderer.h"` mehr
- [x] UIManager.cpp inkludiert nur `Renderer.h`
- [x] Build erfolgreich — keine Kompilierfehler
- [ ] Alle UI-Funktionen arbeiten korrekt (Smoke Test):
  - [ ] Modals öffnen/schließen
  - [ ] Toast-Nachrichten erscheinen
  - [ ] Content Browser navigieren
  - [ ] World Outliner Entities anklicken
  - [ ] Entity Details bearbeiten
  - [ ] Engine Settings Popup funktioniert
  - [ ] Landscape Manager Popup funktioniert
  - [ ] Project Screen funktioniert

---

#### ✅ Schritt 3.2: `MeshViewerWindow` abstrahieren *(erledigt)*
**Was:** `OpenGLObject3D` → `IRenderObject3D` verwenden.

**Umsetzung:**
- `MeshViewerWindow.h`: Forward-Declaration `OpenGLObject3D` → `IRenderObject3D`, Member `m_meshObject` Typ auf `shared_ptr<IRenderObject3D>` geändert
- `MeshViewerWindow.cpp`: Include von `OpenGLObject3D.h` → `IRenderObject3D.h` geändert
- Alle verwendeten Methoden (`hasLocalBounds`, `getLocalBoundsMin/Max`, `getVertexCount`, `getIndexCount`) sind im Interface definiert — kein Cast nötig

**Verifikation:**
- [x] `MeshViewerWindow.h` hat keinen OpenGL-Include mehr
- [x] Build erfolgreich — keine Kompilierfehler
- [ ] Mesh Viewer zeigt Modelle korrekt an

---

### Phase 4: Build-System restrukturieren

#### ✅ Schritt 4.1: CMakeLists.txt aufteilen *(erledigt)*
**Was:** Das monolithische `Renderer`-Target in Schichten aufgeteilt:

**Umsetzung:**
- `src/Renderer/CMakeLists.txt`: Target von `Renderer` zu `RendererCore` umbenannt. Alle OpenGL-spezifischen Quellen (OpenGLRenderer, OpenGLCamera, OpenGLShader, OpenGLMaterial, OpenGLTexture, OpenGLObject2D/3D, OpenGLTextRenderer, OpenGLRenderContext, OpenGLRenderTarget, OpenGLSplashWindow, glad) entfernt. OpenGL-Include-Verzeichnisse und freetype-Link entfernt. `add_subdirectory(OpenGLRenderer)` hinzugefügt.
- `src/Renderer/OpenGLRenderer/CMakeLists.txt`: Neues `RendererOpenGL` SHARED-Target erstellt mit allen GL-Quellen. Linkt PUBLIC gegen `RendererCore` und `freetype`. Eigene Include-Verzeichnisse für `glad/include`.
- `CMakeLists.txt` (Root): Engine linkt jetzt gegen `RendererOpenGL` statt `Renderer`.
- PhysX-Compile-Definitionen und `WINDOWS_EXPORT_ALL_SYMBOLS` auf beide Targets angewendet.

**Verifikation:**
- [x] `RendererCore` kompiliert OHNE OpenGL-Header oder -Bibliotheken
- [x] `RendererOpenGL` kompiliert ohne C++-Fehler
- [x] `RendererCore` hat keine transitiven OpenGL-Abhängigkeiten (keine glad/freetype Includes oder Links)
- [x] CMake konfiguriert erfolgreich mit beiden Targets

---

#### Schritt 4.2: `main.cpp` auf Factory-Pattern umstellen
**Was:** Statt `#include "OpenGLRenderer.h"` und direkter Instanziierung:

```cpp
// Vorher:
#include "OpenGLRenderer/OpenGLRenderer.h"
OpenGLRenderer renderer;

// Nachher:
#include "Renderer/RendererFactory.h"
auto renderer = RendererFactory::create(RendererBackend::OpenGL);
```

**Factory:**
```cpp
// src/Renderer/RendererFactory.h
enum class RendererBackend { OpenGL, Vulkan, DirectX12, Software };

class RendererFactory
{
public:
    static std::unique_ptr<Renderer> create(RendererBackend backend);
};
```

**Verifikation:**
- [ ] `main.cpp` hat keinen OpenGL-Include mehr
- [ ] Engine startet mit Factory-erstelltem Renderer
- [ ] Backend kann über Kommandozeile oder Config gewählt werden (vorbereitet)

---

### Phase 5: Abstrakte EditorTab-Struktur

#### ✅ Schritt 5.1: `EditorTab` aus OpenGLRenderer herauslösen *(erledigt)*
**Was:** `EditorTab` enthielt `GLuint fbo, colorTex, depthRbo, snapshotTex` — das sind OpenGL-Handles.

**Umsetzung:**
- `IRenderTarget.h` erstellt: Abstraktes Interface mit 11 reinen virtuellen Methoden (`resize`, `bind`, `unbind`, `destroy`, `isValid`, `getWidth`, `getHeight`, `getColorTextureId`, `takeSnapshot`, `hasSnapshot`, `getSnapshotTextureId`)
- `OpenGLRenderTarget.h/.cpp` erstellt: OpenGL-Implementierung mit `GLuint fbo/colorTex/depthRbo/snapshotTex`, zusätzlich `getGLFramebuffer()` für Blit-Operationen
- `EditorTab` Struct: `GLuint fbo/colorTex/depthRbo/snapshotTex`, `fboWidth/fboHeight`, `hasSnapshot` entfernt → `std::unique_ptr<IRenderTarget> renderTarget`
- `OpenGLRenderer.cpp`: `ensureTabFbo()`, `releaseTabFbo()`, `snapshotTabBeforeSwitch()` entfernt (Logik in `OpenGLRenderTarget` verschoben). Alle 12+ Zugriffsstellen auf `tab.fbo`/`tab.colorTex` etc. durch `renderTarget->bind()`/`isValid()`/`static_cast<OpenGLRenderTarget*>` ersetzt.
- `CMakeLists.txt`: `IRenderTarget.h`, `OpenGLRenderTarget.h/.cpp` hinzugefügt

**Verifikation:**
- [x] Editor-Tabs rendern korrekt
- [x] Tab-Snapshots werden korrekt angezeigt
- [x] Kein GLuint mehr in der EditorTab-Struct (nur im OpenGL-Backend via IRenderTarget)

---

### Phase 6: Abschluss & Dokumentation

#### Schritt 6.1: Vollständiger Integrationstest
**Checkliste:**
- [ ] Engine startet ohne Fehler
- [ ] 3D-Szene wird korrekt gerendert
- [ ] Shadows funktionieren
- [ ] Entity Picking funktioniert
- [ ] Gizmos funktionieren (Translate/Rotate/Scale)
- [ ] UI-Widgets reagieren auf Klicks
- [ ] Content Browser zeigt Assets
- [ ] Drag & Drop funktioniert
- [ ] Popup-Fenster öffnen sich
- [ ] Mesh Viewer zeigt Modelle
- [ ] Editor Tabs wechseln korrekt
- [ ] Toast-Nachrichten erscheinen
- [ ] Modal-Dialoge funktionieren
- [ ] Engine Settings können geändert werden
- [ ] Landscape Manager erstellt Landschaften
- [ ] Project Screen funktioniert
- [ ] Splash Screen zeigt sich

#### Schritt 6.2: Unit Tests für Mock-Backend
**Was:** Einen `NullRenderer` oder `MockRenderer` erstellen, der von `Renderer` erbt und nichts rendert. Damit testen:
- UIManager kann ohne echtes Backend initialisiert werden
- Widget-Layout funktioniert ohne Renderer
- Input-Handling funktioniert ohne Renderer
- Content Browser Logik funktioniert ohne Renderer

**Verifikation:**
- [ ] Alle GTest-Tests bestehen
- [ ] Kein OpenGL-Kontext nötig für UI-Logic-Tests

#### Schritt 6.3: Dokumentation aktualisieren
- [ ] `PROJECT_OVERVIEW.md` aktualisieren (neue Architektur-Schichten)
- [ ] `ENGINE_STATUS.md` aktualisieren (Abstraktions-Fortschritt)
- [ ] Code-Kommentare in `Renderer.h` für Backend-Implementierer

---

## 4. Priorisierte Reihenfolge (Empfehlung)

| Priorität | Schritt | Aufwand | Risiko | Grund |
|-----------|---------|---------|--------|-------|
| 1 | 1.1 GLM verschieben | Gering | Gering | Blockiert nichts, räumt auf |
| 2 | 1.2 Abstrakte Typen | Mittel | Gering | Basis für alles Weitere |
| 3 | 2.1 Capabilities | Gering | Gering | Einfach, nützlich |
| 4 | 2.2 Renderer-Interface erweitern | Hoch | Mittel | Kernstück der Abstrahierung |
| 5 | 3.1 UIManager entkoppeln | Hoch | Mittel | Größte Kopplung |
| 6 | 1.3 RenderResourceManager | Mittel | Gering | Nutzt neue Interfaces |
| 7 | 2.3 PopupWindow | Mittel | Gering | Isolierte Änderung |
| 8 | ✅ 2.4 SplashWindow | Mittel | Gering | Isolierte Änderung |
| 9 | 3.2 MeshViewerWindow | Gering | Gering | Kleine Änderung |
| 10 | ✅ 5.1 EditorTab | Mittel | Mittel | Intern im Renderer |
| 11 | ✅ 4.1 CMake aufteilen | Hoch | Hoch | Build-System-Umbau |
| 12 | 4.2 Factory Pattern | Mittel | Gering | Letzte Entkopplung |
| 13 | 6.1 Integrationstest | Mittel | — | Pflicht |
| 14 | 6.2 Mock-Backend Tests | Mittel | — | Qualitätssicherung |
| 15 | 6.3 Dokumentation | Gering | — | Pflicht |

---

## 5. Architektur-Ziel (Übersicht)

```
┌─────────────────────────────────────────┐
│                Engine (main.cpp)         │
│         RendererFactory::create()        │
└────────────────┬────────────────────────┘
                 │ std::unique_ptr<Renderer>
                 ▼
┌─────────────────────────────────────────┐
│           RendererCore (lib)            │
│  ┌─────────────────────────────────┐    │
│  │ Renderer (abstract)             │    │
│  │ Camera (abstract)               │    │
│  │ Shader (abstract)               │    │
│  │ IRenderObject2D/3D (abstract)   │    │
│  │ ITextRenderer (abstract)        │    │
│  │ IShaderProgram (abstract)       │    │
│  │ ITexture (abstract)             │    │
│  │ IRenderTarget (abstract)        │    │
│  │ IRenderContext (abstract)       │    │
│  │ RendererCapabilities            │    │
│  │ RendererFactory                 │    │
│  └─────────────────────────────────┘    │
│  ┌─────────────────────────────────┐    │
│  │ UIManager (backend-agnostisch)  │    │
│  │ UIWidget / WidgetElement        │    │
│  │ UIWidgets/* (Separator, etc.)   │    │
│  │ RenderResourceManager           │    │
│  │ PopupWindow                     │    │
│  │ SplashWindow (abstract)         │    │
│  └─────────────────────────────────┘    │
└────────────────┬────────────────────────┘
                 │ Backend implements Renderer
    ┌────────────┴───────────────────┐
    ▼                                ▼
┌──────────────────┐  ┌──────────────────────┐
│ RendererOpenGL   │  │ RendererVulkan       │
│ (shared lib)     │  │ (future, shared lib) │
│                  │  │                      │
│ OpenGLRenderer   │  │ VulkanRenderer       │
│ OpenGLCamera     │  │ VulkanCamera         │
│ OpenGLShader     │  │ VulkanShader         │
│ OpenGLObject2D   │  │ VulkanObject2D       │
│ OpenGLObject3D   │  │ VulkanObject3D       │
│ OpenGLTexture    │  │ VulkanTexture        │
│ OpenGLText       │  │ VulkanText           │
│ OpenGLMaterial   │  │ VulkanMaterial       │
│ OpenGLSplash     │  │ VulkanSplash         │
│ glad/ glm/       │  │ volk/ vma/           │
└──────────────────┘  └──────────────────────┘
```

---

## 6. Wichtige Hinweise

1. **Inkrementell vorgehen:** Jeder Schritt sollte einzeln committed werden, damit Regressionen leicht identifizierbar sind.
2. **Keine Funktionalität entfernen:** Jede Abstrahierung muss die bestehende OpenGL-Implementierung 1:1 erhalten.
3. **Default-Implementierungen:** Neue virtuelle Methoden im `Renderer`-Interface sollten Default-Implementierungen haben (kein pure virtual für optionale Features), damit ein minimales Backend nur die Kernmethoden implementieren muss.
4. **GLM beibehalten:** GLM ist ein reiner Header-Bibliothek und funktioniert auch mit Vulkan/DirectX. Sie muss nur aus dem OpenGL-Ordner verschoben werden.
5. **SDL3 ist Backend-agnostisch:** SDL3 unterstützt auch Vulkan- und Metal-Kontexte nativ. Die SDL-Abhängigkeit ist kein Hindernis.
6. **Keine Shader-Sprache abstrahieren (vorerst):** GLSL-Shader bleiben im OpenGL-Backend. Andere Backends bringen eigene Shader-Formate mit. Ein Shader-Cross-Compiler (z.B. SPIRV-Cross) wäre ein separates Projekt.
