# Engine – Status & Roadmap

> Übersicht über den aktuellen Implementierungsstand und offene Punkte – pro Modul gegliedert.
> Branch: `Json_and_ecs` | Stand: aktuell

---

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
| Aktives Level verwalten              | ✅     |
| Action-Tracking (Loading, Saving…)   | ✅     |
| Input-Dispatch (KeyDown/KeyUp)       | ✅     |
| Benachrichtigungen (Modal + Toast)   | ✅     |
| Shutdown-Request                     | ✅     |

**Offene Punkte:**
- RHI-Auswahl existiert als Enum, aber nur OpenGL ist tatsächlich implementiert (DirectX 11/12 nicht vorhanden)

---

## 3. Asset Manager

| Feature                                   | Status |
|-------------------------------------------|--------|
| Singleton-Architektur                     | ✅     |
| Sync- und Async-Laden                    | ✅     |
| Worker-Thread (Job-Queue)                | ✅     |
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
| Assimp-Integration (static in AssetManager) | ✅ |
| Import: Shader-Dateien (.glsl)             | ✅     |
| Import: Scripts (.py)                      | ✅     |
| Speichern (Typ-spezifisch)              | ✅     |
| Asset-Header (binär v2 + JSON-Fallback) | ✅     |
| Garbage Collector (weak_ptr Tracking)    | ✅     |
| Projekt-Verwaltung (load/save/create)    | ✅     |
| Editor-Widgets automatisch erzeugen     | ✅     |
| stb_image Integration                    | ✅     |
| Pfad-Auflösung (Content + Editor)       | ✅     |
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
| Quaternion-Unterstützung             | ❌     |
| Mathe-Operatoren (+, -, *, /)       | ❌     |
| Interpolation (Lerp, Slerp)         | ❌     |

**Offene Punkte:**
- Keine Quaternion-Rotation (nur Euler) → Gimbal Lock möglich
- Keine arithmetischen Operatoren auf eigenen Typen (GLM wird intern für Berechnungen genutzt)
- Keine Interpolations-Funktionen (Lerp, Slerp etc.)

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
| Alle 8 Komponentenarten serialisierbar  | ✅     |
| Script-Entity-Cache                      | ✅     |
| Objekt-Registrierung + Gruppen          | ✅     |
| Instancing (enable/disable)             | ✅     |
| Snapshot/Restore (PIE-Modus)            | ✅     |
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
| 8 Komponentenarten                     | ✅     |
| SparseSet-Speicherung (O(1)-Zugriff)   | ✅     |
| Schema-basierte Abfragen               | ✅     |
| Bitmasken-System                        | ✅     |
| Max. 10.000 Entitäten                  | ✅     |
| TransformComponent                      | ✅     |
| MeshComponent                           | ✅     |
| MaterialComponent                       | ✅     |
| LightComponent (Point/Dir/Spot)        | ✅     |
| CameraComponent                         | ✅     |
| PhysicsComponent (Datenstruktur)        | 🟡     |
| ScriptComponent                         | ✅     |
| NameComponent                           | ✅     |
| Physik-Simulation (Kollision, Dynamik) | ❌     |
| Hierarchie (Parent-Child-Entities)     | ❌     |
| Entity-Recycling / Freelist            | ❌     |
| Parallele Iteration                     | ❌     |

**Offene Punkte:**
- **PhysicsComponent**: Datenstruktur (ColliderType, isStatic, mass) existiert und wird serialisiert, aber es gibt **keine Physik-Engine** – keine Kollisionserkennung, keine Rigid-Body-Dynamik, keine Gravity
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
| Entity-Löschen (Entf-Taste)               | ✅     |
| Screen-to-World (Depth-Buffer Unproject)  | ✅     |
| Selection-Outline (Edge-Detection)        | ✅     |
| GPU Timer Queries (Triple-Buffered)       | ✅     |
| CPU-Metriken (Welt/UI/Layout/Draw/ECS)   | ✅     |
| Metriken-Overlay (F10)                    | ✅     |
| Occlusion-Stats (F9)                      | ✅     |
| Bounds-Debug (F8)                         | ✅     |
| UI-Debug-Rahmen (F11)                     | ✅     |
| FPS-Cap (F12)                             | ✅     |
| Custom Window Hit-Test (Resize/Drag, konfigurierbarer Button-Bereich links/rechts) | ✅     |
| Fenster erst nach Konsolen-Schließung sichtbar (Hidden → FreeConsole → ShowWindow) | ✅     |
| Beleuchtung (bis 8 Lichtquellen)          | ✅     |
| Sortierung + Batch-Rendering              | ✅     |
| Shadow Mapping (Multi-Light, Directional/Spot) | ✅     |
| Shadow Mapping (Point Light Cube Maps)      | ✅     |
| Post-Processing (Bloom, SSAO, HDR)       | ❌     |
| Anti-Aliasing (MSAA, FXAA)               | ❌     |
| Transparenz / Alpha-Sorting               | 🟡     |
| Instanced Rendering (GPU)                | ❌     |
| Skeletal Animation Rendering              | ❌     |
| Particle-Rendering                        | ❌     |
| DirectX 11 Backend                        | ❌     |
| DirectX 12 Backend                        | ❌     |
| Vulkan Backend                            | ❌     |

**Offene Punkte:**
- Kein Post-Processing (Bloom, SSAO, HDR, Tonemapping)
- Kein Anti-Aliasing
- Transparenz nur eingeschränkt (kein korrektes Order-Independent-Transparency)
- Instancing existiert auf CPU-/Level-Seite, aber kein GPU-Instanced-Rendering
- Keine Alternative zu OpenGL (DirectX / Vulkan nicht implementiert, nur als Enum-Placeholder)

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
| Orbit-Kamera                        | ❌     |
| Cinematic-Kamera / Pfad-Follow      | ❌     |
| Entity-Kamera (CameraComponent)     | ✅     |
| Kamera-Überblendung                 | ❌     |

**Offene Punkte:**
- Nur Editor-FPS-Kamera – keine Orbit-Kamera für Objekt-Inspektion
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
| Shader Hot-Reload                   | ❌     |
| Shader-Variants / Permutationen     | ❌     |
| Shader-Reflection                   | ❌     |

**Offene Punkte:**
- Geometry-, Compute-, Hull-, Domain-Shader sind im Enum definiert, werden aber nirgendwo aktiv genutzt
- Kein Hot-Reload bei Shader-Änderungen
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
| PBR-Material (Metallic/Roughness)   | ❌     |
| Normal Mapping                      | ❌     |
| Material-Editor (UI)                | ❌     |
| Material-Instancing / Overrides     | ❌     |

**Offene Punkte:**
- Nur Blinn-Phong-ähnliche Beleuchtung – kein PBR (Physically Based Rendering)
- Kein Normal Mapping / Displacement
- Kein Material-Editor in der Editor-UI

---

## 13. Renderer – Texturen

| Feature                              | Status |
|--------------------------------------|--------|
| CPU-Texturdaten (stb_image)          | ✅     |
| GPU-Upload (OpenGLTexture)           | ✅     |
| Format: PNG, TGA, JPG, BMP          | ✅     |
| Bind/Unbind (Texture Units)         | ✅     |
| Mipmaps                              | 🟡     |
| Texture-Compression (S3TC/BC)       | ❌     |
| Texture-Streaming                   | ❌     |
| Cubemap / Skybox                    | ❌     |

**Offene Punkte:**
- Mipmaps: Grundlegende Unterstützung möglich, aber nicht systematisch aktiv
- Keine Textur-Komprimierung
- Keine Cubemaps / Skybox
- Kein Texture-Streaming für große Texturen

---

## 14. Renderer – 2D-/3D-Objekte

| Feature                              | Status |
|--------------------------------------|--------|
| OpenGLObject2D (Sprites)            | ✅     |
| OpenGLObject3D (Meshes)             | ✅     |
| Material-Verknüpfung                | ✅     |
| Lokale Bounding Box (AABB)          | ✅     |
| Batch-Rendering                      | ✅     |
| Statischer Cache                    | ✅     |
| OBJ-Laden (Basis-Meshes)           | ✅     |
| FBX-Import (via Assimp)             | ✅     |
| glTF-Import (via Assimp)            | ✅     |
| LOD-System (Level of Detail)        | ❌     |
| Skeletal Meshes / Animation         | ❌     |

**Offene Punkte:**
- Kein LOD-System
- Keine Skeletal-Animation

---

## 15. Renderer – Text-Rendering

| Feature                              | Status |
|--------------------------------------|--------|
| FreeType-Glyph-Atlas                | ✅     |
| drawText + measureText              | ✅     |
| Zeilenhöhe-Berechnung               | ✅     |
| Shader-Cache                        | ✅     |
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

**Keine offenen Punkte** – vollständig für den aktuellen Anwendungsfall.

---

## 17. UI-System

### 17.1 UIManager

| Feature                              | Status |
|--------------------------------------|--------|
| Widget-Registrierung / Z-Ordering   | ✅     |
| Hit-Test + Focus                     | ✅     |
| Maus-Interaktion (Click, Hover)     | ✅     |
| Scroll-Unterstützung                | ✅     |
| Text-Eingabe (Entry-Bars)           | ✅     |
| Tastatur-Handling (Backspace/Enter) | ✅     |
| Layout-Berechnung                   | ✅     |
| Click-Events (registrierbar)        | ✅     |
| Modal-Nachrichten                   | ✅     |
| Toast-Nachrichten (Stapel-Layout)   | ✅     |
| World-Outliner Integration          | ✅     |
| Entity-Auswahl + Details            | ✅     |
| Drag & Drop (CB → Viewport/Folder/Entity) | ✅ |
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
| CheckBox       | ✅     |
| TreeView       | ✅     |
| TabView        | ✅     |
| ScrollBar (eigenständig) | ❌ |

### 17.3 Editor-Panels

| Panel           | Status |
|-----------------|--------|
| TitleBar (100px: HorizonEngine-Titel + Projektname + Min/Max/Close rechts, Tab-Leiste unten) | ✅ |
| Toolbar / ViewportOverlay (Select/Move/Rotate/Scale + PIE + Settings) | ✅ |
| WorldSettings   | ✅     |
| WorldOutliner   | ✅     |
| EntityDetails   | ✅     |
| ContentBrowser  | ✅     |
| StatusBar (Undo/Redo + Dirty-Zähler + Save All + Progress-Modal) | ✅ |
| Material-Editor | ❌     |
| Shader-Editor   | ❌     |
| Console / Log-Viewer | ❌ |
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
| Weitere Tabs (z.B. Material-Editor) | ❌     |

**Offene Punkte:**
- Kein Docking-System (Panels sind fest positioniert)
- Kein Theming / Style-System
- Fehlende Widget-Typen: ScrollBar (eigenständig)
- Fehlende Editor-Panels: Material-Editor, Shader-Editor, Log-Viewer, erweiterte Viewport-Einstellungen
- Content Browser: Ordnernavigation + Asset-Icons per Registry implementiert (Asset-Editor noch Dummy)
- Content Browser: Ausführliches Diagnose-Logging (Prefixed `[ContentBrowser]` / `[Registry]`) über die gesamte Pipeline
- Content Browser: Crash beim Klick auf Ordner behoben (dangling `this`-Capture in Builder-Lambdas → `self` direkt captured)
- Content Browser: Icons werden als PNG vom User bereitgestellt in `Editor/Textures/` (stb_image-Laden)
- Content Browser: Icons werden per Tint-Color eingefärbt (Ordner gelb, Scripte grün, Texturen blau, Materials orange, Audio rot, Shader lila, etc.)
- Content Browser: TreeView-Inhalte per `glScissor` auf den Zeichenbereich begrenzt (kein Überlauf beim Scrollen)
- Content Browser: Grid-View zeigt Ordner + Assets des ausgewählten Ordners als quadratische Kacheln (80×80px, Icon + Name)
- Content Browser: Doppelklick auf Grid-Ordner navigiert hinein, Doppelklick auf Asset öffnet es
- Content Browser: Ausgewählter Ordner im TreeView visuell hervorgehoben
- Content Browser: Einfachklick auf TreeView-Ordner wählt ihn aus und aktualisiert Grid
- Content Browser: Zweiter Klick auf bereits ausgewählten Ordner klappt ihn wieder zu
- Content Browser: "Content" Root-Knoten im TreeView, klickbar zum Zurücknavigieren zur Wurzel
- Content Browser: Pfadleiste (Breadcrumbs) über der Grid: Zurück-Button + klickbare Pfadsegmente (Content > Ordner > UnterOrdner)
- Content Browser: Crash beim Ordnerwechsel nach Grid-Interaktion behoben (Use-After-Free: Target-Daten vor Callback-Aufruf kopiert)
- Widget-System: `readElement` parst jetzt das `id`-Feld aus JSON (fehlte zuvor, wodurch alle Element-IDs nach Laden leer waren)
- Widget-System: `layoutElement` hat jetzt Default-Pfad für Kinder aller Element-Typen (Button-Kinder werden korrekt gelayoutet)
- Widget-System: Grid-Layout berechnet Spalten aus verfügbarer Breite / Kachelgröße für quadratische Zellen
- Widget-System: `onDoubleClicked`-Callback auf `WidgetElement` mit Doppelklick-Erkennung (400ms, SDL_GetTicks)
- EntityDetails-Panel endet über dem ContentBrowser (Layout berücksichtigt die Oberkante des ContentBrowsers als Unterlimit)
- Scrollbare StackPanels/Grids werden per `glScissor` auf ihren Zeichenbereich begrenzt (kein Überlauf beim Scrollen)
- **Performance-Optimierungen:**
  - `updateHoverStates`: O(1) Tracked-Pointer statt O(N) Full-Tree-Walk pro Mausbewegung
  - `hitTest`: Keine temporäre Vektor-Allokation mehr, iteriert gecachte Liste direkt rückwärts
  - `drawUIPanel`/`drawUIImage`: Uniform-Locations pro Shader-Programm gecacht (eliminiert ~13 `glGetUniformLocation`-Aufrufe pro Draw)
  - Verbose INFO-Logging aus allen Per-Frame-Hotpaths entfernt (Hover, HitTest, ContentBrowser-Builder, RegisterWidget)
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
- Weitere Editor-Tabs für zusätzliche Editoren noch nicht implementiert
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
    - Point Shadow Maps auf Texture Unit 5 gebunden

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
| engine.pyi IntelliSense-Stubs       | ✅     |
| Async-Asset-Load Callbacks          | ✅     |
| Mehrere Scripts pro Level           | ✅     |
| Script-Fehlerbehandlung             | 🟡     |
| Script-Debugger                     | ❌     |
| Script Hot-Reload                   | ❌     |

### 18.2 Script-API Module

| Submodul                  | Status |
|---------------------------|--------|
| engine.entity (CRUD, Transform, Mesh) | ✅ |
| engine.assetmanagement    | ✅     |
| engine.audio              | ✅     |
| engine.input              | ✅     |
| engine.ui                 | ✅     |
| engine.camera             | ✅     |
| engine.diagnostics        | ✅     |
| engine.logging            | ✅     |
| engine.physics            | ❌     |
| engine.renderer (Shader-Parameter etc.) | ❌ |

**Offene Punkte:**
- Script-Fehler werden geloggt, aber kein detailliertes Error-Recovery (Script crasht → Fehlermeldung, aber kein Retry)
- Kein Script-Debugger (Breakpoints etc.)
- Kein Hot-Reload bei Script-Änderung (nur bei PIE-Neustart)
- Kein `engine.physics`-Modul (da Physik-Simulation fehlt)
- Kein Zugriff auf Renderer-Parameter (z.B. Material-Uniforms) aus Python

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
| **Physik-Engine**                | Hoch      | Kollisionserkennung, Rigid Body, Gravity – PhysicsComponent-Daten existieren bereits, aber keine Simulation |
| **3D-Modell-Import (Assimp)**    | ✅     | Import von OBJ, FBX, glTF, GLB, DAE, 3DS, STL, PLY, X3D via Assimp            |
| **Entity-Hierarchie**            | Mittel    | Parent-Child-Beziehungen für Entities (kein ParentComponent im ECS)           |
| **Entity-Kamera (Runtime)**      | ✅     | Entity-Kamera via `setActiveCameraEntity()` mit FOV/NearClip/FarClip aus CameraComponent |
| **PBR-Material + Normal Mapping**| Mittel    | Physically Based Rendering, Normal/Roughness/Metallic-Maps (aktuell nur Blinn-Phong) |
| **Post-Processing**              | Mittel    | Bloom, SSAO, HDR, Tonemapping, Anti-Aliasing                                |
| **Cascaded Shadow Maps**         | Mittel    | CSM für Directional Lights (aktuell feste ortho-Projektion, kein Cascading)  |
| **Skeletal Animation**           | Mittel    | Bone-System, Skinning, Animation-Blending                                    |
| **Cubemap / Skybox**            | Mittel    | Umgebungstexturen für Himmel                                                  |
| **Drag & Drop (Editor)**        | ✅     | Model3D→Spawn (Depth-Raycast), Material/Script→Apply (pickEntityAtImmediate), Asset-Move mit tiefem Referenz-Scan aller .asset-Dateien, Entf zum Löschen |
| **Audio-Formate (OGG/MP3)**     | Niedrig   | Weitere Audio-Formate unterstützen (aktuell nur WAV)                         |
| **3D-Audio (Positional)**       | Niedrig   | OpenAL-Listener-/Source-Positionierung nutzen                                |
| **Particle-System**             | Niedrig   | GPU-/CPU-Partikel für Effekte                                                |
| **Netzwerk / Multiplayer**      | Niedrig   | Netzwerk-Synchronisation, Server/Client                                      |
| **DirectX 11/12 Backend**       | Niedrig   | Alternative Rendering-Backends (aktuell nur OpenGL 4.6)                      |
| **Cross-Platform (Linux/macOS)**| Niedrig   | GCC/Clang-Support, Plattform-Abstraktion                                    |
| **CI/CD + Tests**               | Niedrig   | Automatisierte Builds, Unit-Tests, Integrationstests                         |
| **Script-Debugger**             | Niedrig   | Python-Breakpoints, Step-Through im Editor                                   |
| **Hot-Reload (Assets/Scripts)** | Niedrig   | Dateiänderungen erkennen und automatisch neu laden                           |

### Bereits abgeschlossene Systeme (aus früheren Iterationen)

| System                            | Status | Beschreibung                                                                   |
|-----------------------------------|--------|--------------------------------------------------------------------------------|
| **Undo/Redo**                    | ✅     | Command-Pattern für Editor-Aktionen (UndoRedoManager-Singleton, Ctrl+Z/Y, StatusBar-Buttons) |
| **Editor-Gizmos**               | ✅     | Translate/Rotate/Scale-Gizmos für Entity-Manipulation (W/E/R Shortcuts)      |
| **Shadow Mapping (Dir/Spot)**    | ✅     | Multi-Light Shadow Maps für bis zu 4 Directional/Spot Lights, 5×5 PCF       |
| **Shadow Mapping (Point Lights)**| ✅     | Omnidirektionale Cube-Map Shadows für bis zu 4 Point Lights via Geometry-Shader |

---

*Generiert aus Analyse des Quellcodes. Stand: aktueller Branch `AssetManager_Json`.*
