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

#### Schritt 1.3: `RenderResourceManager` auf abstrakte Typen umstellen
**Was:** Öffentliche API auf die neuen Interfaces umstellen:
- `getOrCreateObject2D()` → `IRenderObject2D*`
- `getOrCreateObject3D()` → `IRenderObject3D*`
- `prepareTextRenderer()` → `ITextRenderer*`

**Intern** können die Caches weiterhin die konkreten Typen speichern — nur die öffentliche Signatur ändert sich.

**Verifikation:**
- [ ] Alle Aufrufer von `getOrCreate*()` kompilieren ohne Fehler
- [ ] Kein Aufrufer castet zurück auf den konkreten OpenGL-Typ (falls doch: dort ebenfalls abstrahieren)
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

#### Schritt 2.3: `PopupWindow` abstrahieren
**Was:** `PopupWindow` sollte kein `SDL_GLContext` direkt halten, sondern ein abstraktes Render-Context-Konzept:

```cpp
// src/Renderer/IRenderContext.h
class IRenderContext
{
public:
    virtual ~IRenderContext() = default;
    virtual void makeCurrent() = 0;
    virtual void swapBuffers() = 0;
};
```

`PopupWindow` hält dann `std::unique_ptr<IRenderContext>` statt `SDL_GLContext`.

**Verifikation:**
- [ ] `PopupWindow.h` hat keinen OpenGL-Include mehr
- [ ] Popup-Fenster funktionieren wie vorher
- [ ] OpenGL-Kontext-Sharing funktioniert noch korrekt

---

#### Schritt 2.4: `SplashWindow` abstrahieren
**Was:** GL-Handles (`quadVao`, `quadVbo`, `quadProgram`) durch abstraktes Interface ersetzen oder die Splash-Implementierung Backend-spezifisch machen (z.B. `OpenGLSplashWindow`).

**Option A** (empfohlen): `SplashWindow` wird zur abstrakten Basisklasse, `OpenGLSplashWindow` erbt davon.
**Option B**: Splash verwendet nur SDL2-Surface-Rendering (kein GL nötig).

**Verifikation:**
- [ ] `SplashWindow.h` hat keinen OpenGL-Include
- [ ] Splash-Screen zeigt sich korrekt beim Start
- [ ] Fortschrittsanzeige funktioniert

---

### Phase 3: UIManager entkoppeln

#### Schritt 3.1: UIManager von `OpenGLRenderer*` auf `Renderer*` umstellen
**Was:**
- `m_renderer` Typ ändern: `OpenGLRenderer*` → `Renderer*`
- `setRenderer(OpenGLRenderer*)` → `setRenderer(Renderer*)`
- `getRenderer()` Rückgabe → `Renderer*`

**Betroffene Aufrufe in UIManager.cpp:**
1. `m_renderer->openPopupWindow(...)` → über das neue `Renderer`-Interface
2. `m_renderer->closePopupWindow(...)` → über das neue `Renderer`-Interface
3. `m_renderer->openMeshViewer(...)` → über das neue `Renderer`-Interface
4. Engine Settings: `renderer->isShadowsEnabled()`, `renderer->setShadowsEnabled()`, etc. → über das neue `Renderer`-Interface
5. `renderer->isVSyncEnabled()`, `renderer->setVSyncEnabled()`
6. `renderer->isWireframeEnabled()`, `renderer->setWireframeEnabled()`
7. `renderer->isOcclusionCullingEnabled()`, `renderer->setOcclusionCullingEnabled()`
8. `renderer->isUIDebugEnabled()`, `renderer->toggleUIDebug()`
9. `renderer->isBoundsDebugEnabled()`, `renderer->toggleBoundsDebug()`
10. `renderer->isHeightFieldDebugEnabled()`, `renderer->setHeightFieldDebugEnabled()`
11. `renderer->window()` (SDL_Window* — bereits im Interface)

**Verifikation:**
- [ ] `UIManager.h` und `UIManager.cpp` haben keinen `#include "OpenGLRenderer.h"` mehr
- [ ] UIManager.cpp inkludiert nur `Renderer.h`
- [ ] Alle UI-Funktionen arbeiten korrekt:
  - [ ] Modals öffnen/schließen
  - [ ] Toast-Nachrichten erscheinen
  - [ ] Content Browser navigieren
  - [ ] World Outliner Entities anklicken
  - [ ] Entity Details bearbeiten
  - [ ] Engine Settings Popup funktioniert
  - [ ] Landscape Manager Popup funktioniert
  - [ ] Project Screen funktioniert

---

#### Schritt 3.2: `MeshViewerWindow` abstrahieren
**Was:** `OpenGLObject3D` → `IRenderObject3D` verwenden.

**Verifikation:**
- [ ] `MeshViewerWindow.h` hat keinen OpenGL-Include
- [ ] Mesh Viewer zeigt Modelle korrekt an

---

### Phase 4: Build-System restrukturieren

#### Schritt 4.1: CMakeLists.txt aufteilen
**Was:** Das monolithische `Renderer`-Target in Schichten aufteilen:

```
src/Renderer/
├── CMakeLists.txt              ← Abstraktes Renderer-Target (Interface + UIManager + Widget)
├── Renderer.h
├── Camera.h
├── Shader.h
├── IRenderObject2D.h
├── IRenderObject3D.h
├── ITextRenderer.h
├── IShaderProgram.h
├── ITexture.h
├── IRenderContext.h
├── RendererCapabilities.h
├── UIManager.h / .cpp
├── UIWidget.h
├── RenderResourceManager.h
├── PopupWindow.h / .cpp
├── UIWidgets/
└── OpenGLRenderer/
    ├── CMakeLists.txt          ← OpenGL-Backend-Target (eigene shared/static lib)
    ├── OpenGLRenderer.h / .cpp
    ├── OpenGLCamera.h / .cpp
    ├── OpenGLShader.h / .cpp
    ├── OpenGLObject2D.h / .cpp
    ├── OpenGLObject3D.h / .cpp
    ├── OpenGLTextRenderer.h / .cpp
    ├── OpenGLTexture.h / .cpp
    ├── OpenGLMaterial.h / .cpp
    ├── OpenGLShaderProgram.h / .cpp
    ├── OpenGLSplashWindow.h / .cpp  ← (von SplashWindow abstrahiert)
    ├── glad/
    └── glm/                         ← Oder nach src/Math/ verschoben
```

**CMake-Targets:**
- `RendererCore` — Abstrakte Interfaces + UIManager + Widgets (KEIN OpenGL-Link)
- `RendererOpenGL` — OpenGL-Backend (linkt gegen `RendererCore`, `glad`, `freetype`)
- `Engine` (main) — linkt gegen `RendererOpenGL` (oder ein anderes Backend)

**Verifikation:**
- [ ] `RendererCore` kompiliert OHNE OpenGL-Header oder -Bibliotheken
- [ ] `RendererOpenGL` kompiliert und linkt korrekt
- [ ] Engine startet und funktioniert wie vorher
- [ ] `RendererCore` hat keine transitiven OpenGL-Abhängigkeiten

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

#### Schritt 5.1: `EditorTab` aus OpenGLRenderer herauslösen
**Was:** `EditorTab` enthält `GLuint fbo, colorTex, depthRbo, snapshotTex` — das sind OpenGL-Handles.

Lösung: Abstrakte `IRenderTarget`-Klasse:
```cpp
class IRenderTarget
{
public:
    virtual ~IRenderTarget() = default;
    virtual void bind() = 0;
    virtual void unbind() = 0;
    virtual void resize(int width, int height) = 0;
    virtual unsigned int getColorTextureId() const = 0;
};
```

`EditorTab` hält dann `std::unique_ptr<IRenderTarget>` statt roher GL-Handles.

**Verifikation:**
- [ ] Editor-Tabs rendern korrekt
- [ ] Tab-Snapshots werden korrekt angezeigt
- [ ] Kein GLuint mehr in der EditorTab-Struct (außer im OpenGL-Backend)

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
| 8 | 2.4 SplashWindow | Mittel | Gering | Isolierte Änderung |
| 9 | 3.2 MeshViewerWindow | Gering | Gering | Kleine Änderung |
| 10 | 5.1 EditorTab | Mittel | Mittel | Intern im Renderer |
| 11 | 4.1 CMake aufteilen | Hoch | Hoch | Build-System-Umbau |
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
