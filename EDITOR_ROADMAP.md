# Editor Roadmap – Modernes, intuitives Engine-Editor-Design

> Ausführliche Roadmap für die Weiterentwicklung des Engine-Editors hin zu einem modernen, benutzerfreundlichen und hochautomatisierten Entwicklungswerkzeug.
> Ziel: **Einfache Bedienung**, **intuitives Design**, **maximale Automatisierung** – der Entwickler soll sich auf kreative Arbeit konzentrieren können, nicht auf Kleinkram.
> Stand: aktuell · Branch: `master`

---

## Inhaltsverzeichnis

1. [Phase 1 – Visuelles Redesign der Editor-Widgets](#phase-1--visuelles-redesign-der-editor-widgets)
2. [Phase 2 – Fehlende Editor-Tabs](#phase-2--fehlende-editor-tabs)
3. [Phase 3 – Automatisierung & Smart-Features](#phase-3--automatisierung--smart-features)
4. [Phase 4 – Content-Browser-Erweiterungen](#phase-4--content-browser-erweiterungen)
5. [Phase 5 – Viewport-UX & Workflow](#phase-5--viewport-ux--workflow)
6. [Phase 6 – Erweitertes Editor-Framework](#phase-6--erweitertes-editor-framework)
7. [Phase 7 – Scripting & Debugging](#phase-7--scripting--debugging)
8. [Phase 8 – Polish & Barrierefreiheit](#phase-8--polish--barrierefreiheit)
9. [Phase 9 – Erweiterte Visualisierung & Scene-Debugging](#phase-9--erweiterte-visualisierung--scene-debugging)
10. [Phase 10 – Build & Packaging](#phase-10--build--packaging)
11. [Phase 11 – Erweiterte Editor-Workflows](#phase-11--erweiterte-editor-workflows)
12. [Fortschrittsübersicht](#fortschrittsübersicht)

---

## Bestandsaufnahme – Was existiert bereits

### Vorhandene Editor-Tabs
| Tab | Status |
|-----|--------|
| Viewport (3D-Szene, Gizmos, PIE) | ✅ |
| Mesh Viewer (Doppelklick auf Model3D) | ✅ |
| Widget Editor (Doppelklick auf Widget-Asset) | ✅ |
| UI Designer (Viewport-UI-Inspektion) | ✅ |

### Vorhandene Editor-Panels
| Panel | Status |
|-------|--------|
| TitleBar mit Tab-Leiste | ✅ |
| World Outliner (Entity-Liste) | ✅ |
| Entity Details (Komponenten-Editor) | ✅ |
| Content Browser (Tree + Grid + Breadcrumbs) | ✅ |
| World Settings | ✅ |
| StatusBar (Undo/Redo, Dirty-Count, Save) | ✅ |
| ViewportOverlay (Settings-Dropdown) | ✅ |

### Vorhandene Popups
| Popup | Status |
|-------|--------|
| Engine Settings (Sidebar-Kategorien) | ✅ |
| Editor Settings (Theme, Font, Spacing, DPI) | ✅ |
| Material Editor | ✅ |
| Landscape Manager | ✅ |
| Projekt-Auswahl-Screen | ✅ |

### Fehlende Editor-Tabs / Panels
| Feature | Status |
|---------|--------|
| Material Editor Tab (voller Tab, nicht nur Popup) | ✅ |
| Shader Editor / Viewer | ❌ |
| Console / Log-Viewer | ✅ |
| Animation Editor (Skeletal) | ✅ |
| Particle Editor | ✅ |
| Texture Viewer | ✅ |
| Audio Preview | ✅ |
| Blueprint / Visual Scripting | ❌ |
| Profiler / Performance-Monitor | ✅ |
| Viewport-Einstellungen Panel | ✅ |

---

## Phase 1 – Visuelles Redesign der Editor-Widgets

> **Ziel:** Moderneres, einheitlicheres und cleanes Erscheinungsbild aller Editor-Widgets. Die vorhandene EditorTheme-Infrastruktur wird genutzt und erweitert.

### 1.1 Abgerundete Panels & Weiche Übergänge
**Aktueller Stand:** Alle Panels sind scharfkantige Rechtecke. `borderRadius` existiert als Property in `WidgetElement`, wird aber im Renderer nicht für Panel-Hintergründe genutzt.

**Ziel:** Subtile abgerundete Ecken (4–6px Radius) für Panels, Buttons, Inputs und Cards.

**Herangehensweise:**
- `drawUIPanel()` in `OpenGLRenderer.cpp` um einen SDF-basierten (Signed Distance Field) Rounded-Rect-Shader erweitern
- Neuer GLSL-Fragment-Shader `ui_rounded_rect.glsl` mit `uniform float uBorderRadius`
- `EditorTheme` um `defaultBorderRadius` (float, default 4.0) erweitern
- Alle `EditorUIBuilder`-Factory-Methoden setzen automatisch `borderRadius` aus dem Theme
- Vorhandene `borderRadius`-Werte aus `WidgetElementStyle` werden an den Shader übergeben

**Fortschrittsprüfung:**
- [x] Buttons haben sichtbar abgerundete Ecken
- [x] Panels (Outliner, Details, Content Browser) haben subtile Rundungen
- [x] Inputs/EntryBars haben abgerundete Ränder
- [x] Kein visueller Bruch zwischen Elementen mit und ohne Radius

### 1.2 Schatten & Tiefe (Elevation System)
**Aktueller Stand:** Elevation-System vollständig implementiert. `elevation`-Feld (int, 0–5) in `WidgetElement`, `applyElevation()` Helper in `WidgetElementStyle`. `EditorTheme` mit `shadowColor`/`shadowOffset` (DPI-skaliert, JSON-serialisiert). `drawUIShadow()` mit variablem `blurRadius`. Modals/Toasts elevation=3, Dropdowns elevation=2, Deferred-Dropdowns mit Theme-Shadow. Widget-Editor: Shadow & Elevation-Sektion mit Elevation-Slider + Blur-Radius.

**Ziel:** Leichter Drop-Shadow auf schwebenden Elementen (Dropdowns, Modals, Toasts, Kontextmenüs) für visuelle Hierarchie.

**Herangehensweise:**
- Neues `elevation`-Feld (int, 0–5) in `WidgetElement` – bestimmt die Shadow-Intensität
- Shadow-Rendering als leicht versetzte, verschwommene Hintergrund-Rect unter dem Element (2-Pass: erst Shadow, dann Content)
- `EditorTheme` um `shadowColor` (Vec4, z.B. `{0, 0, 0, 0.3}`) und `shadowOffset` (Vec2, z.B. `{2, 3}`) erweitern
- Elevation-Stufen: 0 = kein Shadow (normale Panels), 1 = leicht (Sektions-Header), 2 = mittel (Dropdowns), 3 = stark (Modals), 4–5 = extra (Drag-Ghost)
- Modals, Toasts und Dropdown-Menüs bekommen automatisch `elevation = 3`

**Fortschrittsprüfung:**
- [x] Dropdown-Menüs haben sichtbaren Schatten
- [x] Modal-Dialoge heben sich visuell vom Hintergrund ab
- [x] Toasts wirken wie schwebende Karten
- [x] Elevation 0 Panels haben keinen Schatten (kein Overhead)

### 1.3 Modernisierte Icon-Sprache
**Aktueller Stand:** 24 Editor-Icons (22 PNG + 2 TGA) in `Editor/Textures/`. Neue Icons: `light.png`, `camera.png`, `entity.png`, `level.png`, `widget.png`, `skybox.png`, `save.png`, `add.png`, `search.png`, `eye.png` (24×24, weiße Silhouette auf transparent). `iconForAssetType()` gibt für jeden Asset-Typ ein passendes Icon zurück (Widget→widget.png, Skybox→skybox.png, Level→level.png). `iconForEntity()` / `iconTintForEntity()` bestimmen Icon+Farbe nach prominentester ECS-Komponente (Light→gelb, Camera→grün, Mesh→cyan, Script→grün, HeightField→lime, Physics→lila, default→grau). Outliner-Einträge nutzen `makeTreeRow()` mit Typ-Icons. Content Browser nutzt verbesserte Icons. Toolbar-Buttons verwenden Icons (PIE→Play.tga/Stop.tga, Settings→Settings.png, Undo→undo.png, Redo→redo.png).

**Ziel:** Konsistente, skalierbare Icon-Sprache für den gesamten Editor.

**Herangehensweise:**
- **Option A (empfohlen):** Icon-Atlas – Eine einzelne PNG-Sprite-Sheet mit allen Editor-Icons (32×32 pro Icon, z.B. 16×16 Grid = 256 Icons). UV-Lookup statt vieler Einzeltexturen. `EditorTheme` bekommt eine `iconAtlasPath`-Eigenschaft.
- **Option B:** Icon-Font (z.B. Material Icons oder FontAwesome als TTF einbetten). Der vorhandene FreeType-basierte `OpenGLTextRenderer` kann TTF-Glyphen rendern. Neues `drawIcon(iconId, pos, size, color)` API.
- Standard-Icon-Set definieren: Ordner, Datei, Mesh, Material, Texture, Audio, Script, Shader, Light, Camera, Entity, Play, Pause, Stop, Save, Undo, Redo, Delete, Add, Search, Settings, Filter, Collapse, Expand, Eye, EyeOff, Lock, Unlock, Pin, Link, Unlink
- Alle hardcoded Text-Symbole (▾, ▸, ◆, ×, +, ■, ▶) durch Icon-Referenzen ersetzen

**Fortschrittsprüfung:**
- [x] Mindestens 20 Editor-Icons als Atlas oder Icon-Font verfügbar
- [x] Content Browser nutzt neue Icons statt farbiger Quadrate
- [x] Outliner Entity-Einträge haben Typ-Icons (Mesh, Light, Camera, etc.)
- [x] Toolbar-Buttons nutzen Icons statt Text

### 1.4 Überarbeitete Spacing & Typography
**Aktueller Stand:** `EditorTheme` um `sectionSpacing` (10px), `groupSpacing` (6px) und `gridTileSpacing` (8px) erweitert. Alle drei Felder werden in `applyDpiScale`, `toJson` und `fromJson` korrekt skaliert/persistiert. `SeparatorWidget::toElement()` nutzt `theme.sectionSpacing` als oberen Margin, `theme.separatorThickness` für die Trennlinie, `theme.panelBorder`-Farbe, und `theme.sectionHeaderHeight` für die Header-Höhe. `makeSection()` Content-Padding DPI-skaliert. Content-Browser Grid-Tiles erhalten `gridTileSpacing`-Margin. Labels (`makeLabel`) verwenden `textSecondary`, Hint-Labels (`makeSecondaryLabel`) verwenden `textMuted`. Werte in EntryBars/DropDowns bleiben `textPrimary`/`inputText`.

**Ziel:** Mehr Luft, klarere visuelle Trennung von Sektionen, bessere Lesbarkeit.

**Herangehensweise:**
- `EditorTheme` um `sectionSpacing` (float, 10px), `groupSpacing` (float, 6px) und `gridTileSpacing` (float, 8px) erweitern
- Alle `EditorUIBuilder::makeSection()` Aufrufe nutzen `sectionSpacing` als Abstand vor der Sektion
- Details-Panel: Gruppen-Trennlinien (1px, `panelBorder`-Farbe) zwischen Komponentensektionen
- Content Browser Grid: Kachelabstand über `gridTileSpacing` (8px, DPI-skaliert)
- Font-Gewichtung: Labels in `textSecondary`, Werte in `textPrimary` – konsistent durchziehen

**Fortschrittsprüfung:**
- [x] Details-Panel hat sichtbaren Abstand zwischen Komponentensektionen
- [x] Outliner-Einträge fühlen sich nicht „eng zusammengequetscht" an
- [x] Content Browser Grid hat gleichmäßige Abstände
- [x] Gesamteindruck: „luftiger" als vorher

### 1.5 Animierte Übergänge (Micro-Interactions)
**Aktueller Stand:** UI-Elemente wechseln Zustände sofort (Hover → instant Farbwechsel). Kein Fade, kein Slide, keine Transition.

**Ziel:** Dezente, performante Micro-Animationen für ein poliertes Gefühl.

**Herangehensweise:**
- Neues `transitionDuration`-Feld (float, Sekunden, Default 0.0 = instant) in `WidgetElementStyle`
- `currentVisualColor` als interpolierter Wert zwischen `color` und `hoverColor`/`pressedColor`
- Lineare Interpolation pro Frame: `currentColor = lerp(currentColor, targetColor, dt / transitionDuration)`
- `EditorTheme` um `hoverTransitionSpeed` (float, default 0.12s) erweitern
- Nur für: Hover-Farbe (Buttons, Tree-Einträge, Grid-Tiles), Opacity (Toasts ein-/ausblenden)
- **NICHT** für: Layout-Änderungen, Panel-Größen, Scrolling (zu komplex, zu wenig Nutzen)

**Fortschrittsprüfung:**
- [x] Button-Hover hat sichtbaren Farbübergang (~120ms)
- [x] Toast-Notifications faden ein/aus statt zu poppen
- [x] Performance-Impact: < 0.1ms pro Frame Overhead

### 1.6 Verbessertes Scrollbar-Design
**Aktueller Stand:** Scrollbalken existieren als dünne Overlay-Tracks (`scrollbarTrack`/`scrollbarThumb` im Theme). Design ist funktional, aber nicht besonders modern.

**Ziel:** Overlapping Scrollbars, die nur bei Hover/Scroll erscheinen (macOS-Stil).

**Herangehensweise:**
- Scrollbar-Sichtbarkeit standardmäßig auf `auto-hide` (erscheint bei Scroll-Aktivität, blendet nach 1.5s aus)
- Scrollbar-Breite: 6px (statt 8px), bei Hover auf 10px verbreitern
- Runde Scroll-Thumb-Enden (via `borderRadius` auf dem Thumb-Panel)
- `EditorTheme` um `scrollbarAutoHide` (bool, default true) und `scrollbarWidth` (float, 6.0) erweitern

**Fortschrittsprüfung:**
- [x] Scrollbars verschwinden wenn nicht gescrollt wird
- [x] Scrollbars verbreitern sich beim Hover
- [x] Scrollbar-Design ist konsistent in allen scrollbaren Panels

---

## Phase 2 – Fehlende Editor-Tabs

> **Ziel:** Alle wichtigen Asset-Typen und Workflows in dedizierten Editor-Tabs abbilden.

### 2.1 Console / Log-Viewer Tab
**Priorität:** Hoch (essentiell für Debugging und Scripting)

**Aktueller Stand:** Vollständig implementiert. Schließbarer Tab mit Toolbar (Filter-Buttons, Suchfeld, Clear, Auto-Scroll), scrollbarer Log-Area, farbcodierte Einträge aus Logger-Ringbuffer (2000 max). 0.5s-Refresh-Timer. Zugang über Settings-Dropdown. Python `sys.stdout`/`sys.stderr` werden über einen C++-basierten `PyLogWriter`-Typ auf den Logger umgeleitet – `print()` erscheint als INFO/Scripting, Fehlerausgaben als ERROR/Scripting im Console-Tab.

**Herangehensweise:**
- Neuer `ConsoleTab` der den bestehenden `Logger`-Singleton anzapft
- Ring-Buffer für die letzten 1000 Log-Einträge (im Speicher, nicht auf Disk)
- Farbcodierte Zeilen: INFO = weiß, WARNING = gelb, ERROR = rot, FATAL = rot+fett
- Filter-Buttons oben: All / Info / Warning / Error (Toggle-Buttons, mehrere gleichzeitig aktiv)
- Suchfeld (EntryBar) mit Echtzeit-Textfilter
- „Clear"-Button zum Leeren des Buffers
- Auto-Scroll zum neuesten Eintrag (deaktivierbar per Toggle)
- Doppelklick auf eine Zeile mit Dateireferenz → öffnet die Datei (falls Script)
- Python `print()`-Output wird über den Logger umgeleitet und erscheint als INFO
- Konfigurierbar: Als Tab ODER als festes Panel am unteren Rand (zwischen Content Browser und StatusBar)

**Implementation:**
1. `ConsoleLogSink` Klasse die sich beim `Logger` als Listener registriert
2. Ring-Buffer `std::deque<ConsoleEntry>` mit `{timestamp, level, category, message}`
3. `ConsolePanel` Widget (oder EditorTab) mit ScrollView + StackPanel
4. Lazy-Rendering: Nur sichtbare Zeilen als `WidgetElement` erzeugen (virtualisiert)
5. Filter-State als Bitmaske (wie `ShaderVariantKey`)

**Fortschrittsprüfung:**
- [x] Tab zeigt Live-Log-Einträge in Echtzeit
- [x] Filter reduzieren die sichtbaren Zeilen korrekt
- [x] Suche findet Teilstrings in Nachrichten
- [x] Python `print()` erscheint im Console-Tab (sys.stdout Umleitung ausstehend)
- [ ] Performance: 10.000+ Einträge ohne Stutter (virtualisierte Zeilen ausstehend)

### 2.2 Material Editor Tab (Vollwertiger Tab statt Popup)
**Priorität:** Hoch (Material-Editing ist einer der häufigsten Workflows)

**Beschreibung:** Der bestehende Material-Editor-Popup wird zu einem vollwertigen Tab erweitert, analog zum Widget Editor. Mit Live-Preview-Sphäre/Mesh, Node-basiertem Shader-Graph (langfristig) und sofortiger Viewport-Rückmeldung.

**Herangehensweise:**
- Basis: Der `MeshViewerWindow`-Architektur folgen (Runtime-Level, eigener FBO, Tab-scoped Properties)
- **Links:** Material-Properties (PBR-Parameter, Textur-Slots, Shader-Auswahl)
- **Mitte:** Preview-FBO mit einer Sphäre (oder wählbarem Mesh: Sphere/Cube/Plane/Custom)
- **Rechts:** Shader-Varianten-Info, Texture-Preview-Thumbnails
- **Unten:** Shader-Code-Vorschau (read-only, Fragment-Shader des Materials)
- Änderungen werden sofort auf die Preview-Sphäre angewendet
- Save-Button speichert das Material-Asset
- Preview-Mesh-Auswahl: Dropdown mit Sphere, Cube, Plane, Cylinder, Torus, Custom Mesh
- Textur-Slots: Drag & Drop von Texturen aus dem Content Browser
- PBR-Werte: Slider mit sofortigem Live-Update

**Fortschrittsprüfung:**
- [x] Doppelklick auf Material im Content Browser öffnet Material-Tab
- [x] Preview-Sphäre zeigt das Material korrekt an
- [x] PBR-Slider-Änderungen sind sofort in der Preview sichtbar
- [ ] Textur-Drag & Drop funktioniert
- [x] Material-Änderungen werden korrekt gespeichert

### 2.3 Texture Viewer Tab
**Priorität:** Mittel

**Beschreibung:** Einfacher Tab zum Betrachten von Texturen mit Zoom/Pan, Kanal-Isolation (R/G/B/A einzeln), Mipmap-Level-Auswahl und Metadaten-Anzeige.

**Herangehensweise:**
- Doppelklick auf Textur-Asset im Content Browser → öffnet Texture Viewer Tab
- **Mitte:** Textur-Anzeige mit Zoom/Pan (gleiche Mechanik wie Widget Editor Canvas)
- **Rechts:** Info-Panel: Auflösung, Format, Dateigröße, Mipmap-Count, Kompressionsformat
- **Oben:** Toolbar mit Channel-Toggles (R/G/B/A, Checkered-Background für Alpha), Zoom-Level, Fit-to-Window
- Channel-Isolation: GLSL-Shader der einzelne Kanäle isoliert (`uniform int uChannelMask`)
- Checkerboard-Hintergrund für Transparenz-Visualisierung
- Mipmap-Level-Slider (wenn Texture Mipmaps hat)

**Fortschrittsprüfung:**
- [x] Textur wird korrekt dargestellt
- [x] Zoom/Pan funktioniert
- [x] Einzelne RGBA-Kanäle können isoliert werden
- [x] Metadaten werden angezeigt
- [x] DDS-komprimierte Texturen werden korrekt dargestellt

### 2.4 Animation Editor Tab
**Priorität:** Mittel–Hoch (Skeletal Animation ist bereits implementiert, aber es gibt keine Editing-UI)

**Aktueller Stand:** ✅ Implementiert. Schließbarer Tab mit Toolbar (Titel mit Entity-Name, Stop-Button), scrollbarem Content-Bereich mit drei Sektionen: Clip-Liste (alle Clips des Skeletts, klickbar zum Umschalten, aktiver Clip hervorgehoben), Playback Controls (Status-Anzeige, Speed-Slider 0–5x, Loop-Checkbox) und Bone-Hierarchie (eingerückte Tree-Darstellung aller Bones mit Parent-Beziehungen). Öffnung über "Edit Animation"-Button in den Entity-Details bei Entities mit `AnimationComponent` auf skinned Meshes. AnimationComponent-Sektion im Details-Panel mit Speed/Loop/Playing/ClipIndex-Feldern und Remove-Option. Animation auch im "Add Component"-Dropdown verfügbar. Renderer-API mit 12 neuen virtuellen Methoden für Skeleton/Animator-Zugriff (`isEntitySkinned`, `getEntityAnimationClipCount`, `playEntityAnimation`, etc.). Alle Änderungen werden live auf die verlinkte Entity im Viewport angewendet.

**Beschreibung:** Tab zum Betrachten und Steuern von Skeletal-Animationen. Kein Keyframe-Editing (zu komplex), aber Playback-Controls, Clip-Auswahl, Speed, Loop-Einstellungen und Bone-Visualisierung.

**Herangehensweise:**
- Öffnung: Über Entity-Details bei Entities mit `AnimationComponent` → „Open in Animation Editor"
- **Mitte:** Preview-FBO mit dem animierten Mesh (wie Mesh Viewer, aber mit laufender Animation)
- **Links:** Clip-Liste (alle Animationen des Skeletts), klickbar zum Umschalten
- **Unten:** Timeline mit Playback-Controls (Play/Pause/Stop, Speed-Slider, Scrubber, Frame-Anzeige)
- **Rechts:** Bone-Hierarchie als TreeView, ausgewählter Bone wird im Preview hervorgehoben
- Bone-Visualisierung: Wireframe-Linien zwischen Bone-Positionen (Debug-Overlay im FBO)
- Playback-Geschwindigkeit: 0.1x – 5.0x Slider
- Loop-Toggle
- Frame-by-Frame-Navigation (← →)

**Fortschrittsprüfung:**
- [ ] Animiertes Mesh wird in der Preview korrekt dargestellt
- [x] Clip-Auswahl wechselt die laufende Animation
- [ ] Scrubber steuert die Animation-Zeit
- [x] Bone-Hierarchie wird als Tree angezeigt
- [x] Speed-Slider beeinflusst die Abspielgeschwindigkeit

### 2.5 Particle Editor Tab
**Priorität:** Mittel (Particle-System existiert, aber Parameter nur über Python API oder ECS editierbar)

**Aktueller Stand:** Implementiert. Schließbarer Tab mit Toolbar (Titel mit Entity-Name, Preset-Dropdown, Reset-Button), scrollbarem Parameterbereich mit Slider-Controls für alle 20 Parameter, gruppiert in Sektionen (General, Emission, Motion, Size, Start Color, End Color). Öffnung über "Edit Particles"-Button in den Entity-Details bei Entities mit `ParticleEmitterComponent`. 6 eingebaute Presets (Fire, Smoke, Sparks, Rain, Snow, Magic) als Dropdown. Alle Änderungen werden live auf die verlinkte Entity im Viewport angewendet. Reset-Button stellt Standardwerte wieder her. Undo/Redo-Integration für Preset-Anwendung und Reset. 0.3s-Validierungs-Timer prüft ob die verlinkte Entity noch existiert.

**Beschreibung:** Visueller Editor für Particle-Emitter-Konfigurationen mit Live-Preview.

**Herangehensweise:**
- Öffnung: Über Entity-Details bei Entities mit `ParticleEmitterComponent` → „Edit Particles"
- **Mitte:** Preview-FBO mit dem isolierten Emitter auf schwarzem Hintergrund
- **Rechts:** Alle 20 ParticleEmitter-Parameter als passende Controls:
  - Rate/Lifetime/Speed/SpeedVariance/Gravity: Float-Slider
  - Size/SizeEnd: Float-Slider mit Preview-Kurve
  - ConeAngle: Winkel-Slider (0–360°) mit Cone-Visualisierung
  - ColorStart/ColorEnd: ColorPicker
  - MaxParticles: Integer-Eingabe
  - Enabled/Loop: CheckBoxen
- Alle Änderungen live in der Preview sichtbar
- Preset-System: Eingebaute Presets (Fire, Smoke, Sparks, Rain, Snow, Magic) als Dropdown
- Preset-Export/Import (JSON)

**Fortschrittsprüfung:**
- [x] Preview zeigt Partikel in Echtzeit (live im Viewport auf der verlinkten Entity)
- [x] Alle 20 Parameter sind editierbar
- [x] Änderungen sind sofort sichtbar
- [x] Mindestens 3 Presets verfügbar (6: Fire, Smoke, Sparks, Rain, Snow, Magic)
- [x] Parameter werden korrekt in ECS zurückgeschrieben

### 2.6 Audio Preview Panel
**Priorität:** Niedrig–Mittel

**Aktueller Stand:** Implementiert. Doppelklick auf Audio-Asset im Content Browser öffnet einen eigenen Tab. Play/Stop-Buttons, Lautstärke-Slider, Waveform-Balkengrafik (80 Balken aus Sample-Daten), Metadaten-Anzeige (Pfad, Channels, Sample Rate, Format, Duration, Dateigröße). Folgt dem Console/Profiler-Tab-Muster (rein UIManager-basiert, kein FBO). AudioPreviewState-Struct in UIManager verwaltet Tab-Lifecycle.

**Beschreibung:** Einfaches Panel/Tab für Audio-Asset-Vorschau mit Waveform-Anzeige und Play/Stop.

**Herangehensweise:**
- Doppelklick auf Audio-Asset im Content Browser → öffnet Audio Preview
- Kann als kleiner Panel-Bereich im Content Browser (inline) oder als eigener Tab implementiert werden
- Play/Pause/Stop-Buttons
- Seek-Bar (Scrubber über die Waveform)
- Lautstärke-Slider
- Waveform-Rendering: Sample-Daten als Balkendiagramm (wie in DAWs)
- Metadaten: Format, Sample Rate, Channels, Duration, Dateigröße

**Fortschrittsprüfung:**
- [x] Audio kann abgespielt werden
- [ ] Scrubber zeigt die aktuelle Position
- [x] Metadaten werden angezeigt
- [x] Waveform wird visuell dargestellt

### 2.7 Profiler / Performance-Monitor Tab
**Priorität:** Mittel (F10 Overlay existiert, aber kein dedizierter Tab mit Verlaufshistorie)

**Aktueller Stand:** Implementiert. Schließbarer Tab mit Toolbar (Freeze/Resume-Button), scrollbarer Metrik-Bereich. `FrameMetrics`-Struct in `DiagnosticsManager` mit Ring-Buffer (300 Frames). Anzeige: Frame-History-Balkengrafik (letzte 150 Frames, farbcodiert grün/gelb/rot nach Warnschwellen), Current-Frame-Übersicht (FPS, CPU/GPU Frame-Time), CPU-Breakdown (World/UI/Layout/Draw/ECS/Input/Events/Render/GC/Other mit Proportions-Balken), Occlusion-Culling-Statistik (Visible/Hidden/Total/Cull-Rate). 0.25s-Auto-Refresh-Timer. Freeze-Modus hält die aktuelle Anzeige an. Warnschwellen: grün < 8.3ms, gelb < 16.6ms, rot ≥ 16.6ms. Zugang über Settings-Dropdown → „Profiler".

**Herangehensweise:**
- Die existierenden GPU Timer Queries und CPU-Metriken nutzen
- **Oben:** FPS-Graph (letzte 300 Frames als Liniendiagramm)
- **Mitte:** Aufschlüsselungs-Balken pro Frame: World-Rendering, UI-Rendering, Layout, ECS, Physics
- **Unten:** Tabellarische Detailansicht (Draw Calls, Triangles, Texture Memory, etc.)
- Frame-Capture: Button zum „Einfrieren" des aktuellen Frames für Detail-Analyse
- CSV-Export für externe Analyse-Tools
- Warnschwellen: Zeilen werden rot wenn > 16.6ms (60fps), gelb wenn > 8.3ms (120fps)

**Fortschrittsprüfung:**
- [x] FPS-Graph zeigt Live-Daten
- [x] Aufschlüsselung nach Rendering-Phase sichtbar
- [x] Frame-Einfrieren funktioniert
- [ ] Draw-Call-Count wird korrekt angezeigt

---

## Phase 3 – Automatisierung & Smart-Features

> **Ziel:** Dem Entwickler möglichst viel Arbeit abnehmen. Intelligente Defaults, Auto-Konfiguration, One-Click-Workflows.

### 3.1 Auto-Material-Erstellung bei Model-Import
**Aktueller Stand:** Vollständig implementiert. Assimp-Import erzeugt automatisch Material- und Textur-Assets. PBR-Daten (Metallic, Roughness) werden übernommen, Fallback-Defaults gesetzt. Textur-Slots (Diffuse, Specular, Normal, Emissive, MetallicRoughness) automatisch verknüpft. Detaillierte Toast-Benachrichtigung mit Material-/Textur-Zähler.

**Ziel:** Beim Import eines 3D-Modells werden automatisch:
1. Alle Texturen extrahiert und als Textur-Assets registriert
2. Ein Material-Asset pro Mesh-Material erstellt (mit PBR-Defaults)
3. Das Material automatisch dem Mesh zugewiesen
4. Der Entwickler sieht sofort das korrekt texturierte Modell im Viewport

**Herangehensweise:**
- `AssetManager::importModel3D()` erweitern: Nach Assimp-Import automatisch `createMaterialAsset()` für jedes Mesh-Material aufrufen
- Material-Asset erhält automatisch die korrekten Textur-Pfade (Diffuse, Normal, Specular, etc.)
- PBR-Werte aus Assimp übernehmen (Metallic, Roughness falls vorhanden)
- Fallback: Wenn Assimp keine PBR-Daten hat → sinnvolle Defaults (Metallic=0.0, Roughness=0.5)
- Toast-Benachrichtigung: „Imported {model} with {n} materials and {m} textures"

**Fortschrittsprüfung:**
- [x] FBX-Import erzeugt automatisch Material-Assets
- [x] Texturen werden automatisch extrahiert und referenziert
- [x] Mesh im Viewport zeigt sofort die korrekten Texturen
- [x] Toast-Nachricht informiert über den Import-Umfang

### 3.2 Smart Entity Templates / Prefabs
**Aktueller Stand:** Implementiert. `AssetType::Prefab` hinzugefügt. Content Browser → Rechtsklick → „Save as Prefab" speichert die selektierte Entity als Prefab-Asset (JSON mit allen Komponenten). Prefab-Assets im Content Browser mit eigenem Icon (entity.png, Teal-Tint) und Typ-Filter sichtbar. Drag & Drop eines Prefab-Assets auf den Viewport spawnt die Entity an der Cursor-Position. Doppelklick spawnt am Ursprung. „+ Entity"-Dropdown-Button in der Content-Browser-PathBar mit 7 Built-in-Templates (Empty Entity, Point Light, Directional Light, Camera, Static Mesh, Physics Object, Particle Emitter). Alle Operationen mit Undo/Redo.

**Ziel:** Vorgefertigte Entity-Templates für häufige Anwendungsfälle, die per Drag & Drop instanziiert werden können.

**Herangehensweise:**
- Neuer Asset-Typ `AssetType::Prefab` (JSON: Liste von Entities mit Komponenten)
- Content Browser → Rechtsklick → „Save as Prefab" erstellt ein Prefab aus der aktuell selektierten Entity (mit allen Komponenten)
- Drag & Drop eines Prefab-Assets auf den Viewport erzeugt alle Entities mit ihren Komponenten
- Eingebaute Templates:
  - **Empty Entity** (nur Transform)
  - **Point Light** (Transform + Light + Name)
  - **Directional Light** (Transform + Light)
  - **Camera** (Transform + Camera)
  - **Static Mesh** (Transform + Mesh + Material)
  - **Physics Object** (Transform + Mesh + Material + Physics)
  - **Particle Emitter** (Transform + ParticleEmitter)
- „+ Entity"-Button in der Content-Browser-PathBar als Dropdown mit allen Built-in-Templates

**Fortschrittsprüfung:**
- [x] „Create Prefab" aus selektierter Entity funktioniert
- [x] Prefab-Asset im Content Browser sichtbar
- [x] Drag & Drop erzeugt korrekte Entity mit allen Komponenten
- [x] Mindestens 5 Built-in-Templates verfügbar

### 3.3 One-Click Scene Setup
**Aktueller Stand:** Implementiert. `createNewLevelWithTemplate()` in `UIManager` mit 3 Templates. „+ Level"-Dropdown-Button in der Content-Browser-PathBar (neben Import). ECS wird mit `initialize({})` zurückgesetzt, Entities per `onEntityAdded` registriert, Level sofort gespeichert. Editor-Kamera wird template-spezifisch positioniert.

**Ziel:** Beim Erstellen eines neuen Levels automatisch eine sinnvolle Basisszene aufbauen.

**Herangehensweise:**
- Neuer Dialog bei „New Level" im Content Browser: Scene-Template-Auswahl
  - **Empty** (nur Default-Licht)
  - **Basic Indoor** (3 Point Lights, Boden-Plane, 4 Wände)
  - **Basic Outdoor** (Directional Light, Skybox, Landscape)
  - **Prototype** (Grid-Boden, Directional Light, einige Basis-Primitives als Orientierung)
- Jedes Template wird als JSON-Definition gespeichert in `Editor/Templates/`
- Template-Entities werden als normale ECS-Entities in das Level eingefügt
- Default-Skybox wird automatisch gesetzt
- Editor-Kamera wird auf eine sinnvolle Position gesetzt

**Fortschrittsprüfung:**
- [x] „New Level"-Dropdown zeigt Template-Auswahl (Empty / Basic Outdoor / Prototype)
- [x] „Basic Outdoor" erzeugt Level mit Licht, Skybox-Lookup und Boden
- [x] „Prototype" erzeugt Level mit Grid und Basis-Geometrie
- [x] Editor-Kamera blickt auf die generierte Szene

### 3.4 Auto-LOD-Generierung
**Aktueller Stand:** LOD-System existiert, aber LOD-Meshes müssen manuell erstellt und zugewiesen werden.

**Ziel:** Automatische LOD-Generierung beim Model-Import oder per Button.

**Herangehensweise:**
- Mesh-Simplification-Algorithmus (Edge-Collapse, z.B. Garland-Heckbert QEM)
- 3 LOD-Stufen: 100% (Original), 50% (Medium), 25% (Low)
- Integration in `AssetManager::importModel3D()` als optionaler Schritt
- Checkbox im Import-Dialog: „Generate LOD levels"
- Pro LOD-Stufe: Eigene .asset-Datei (ModelName_LOD0/LOD1/LOD2)
- `LodComponent` wird automatisch auf die Entity gesetzt
- Alternativ: „Generate LODs"-Button im Mesh Viewer Tab

**Fortschrittsprüfung:**
- [ ] Import-Dialog hat LOD-Checkbox
- [ ] 3 LOD-Stufen werden automatisch generiert
- [ ] LodComponent wird korrekt befüllt
- [ ] LOD-Wechsel im Viewport sichtbar (per Debug-Modus)

### 3.5 Auto-Collider-Generierung
**Aktueller Stand:** Implementiert. `autoFitColliderForEntity()` in `UIManager` berechnet AABB aus Mesh-Vertices (stride 5), skaliert mit Entity-Transform, wählt Collider-Typ per Heuristik (Sphere bei Aspekt <1.4, Capsule bei vertikal >2.5, sonst Box). „Add Component → Physics" fügt automatisch `CollisionComponent` mit gefitteten Dimensionen hinzu. „Auto-Fit Collider"-Button in der Collision-Sektion des Details-Panels.

**Ziel:** Bei Hinzufügen einer `PhysicsComponent` automatisch einen passenden Collider vorschlagen.

**Herangehensweise:**
- Mesh-AABB auslesen → Box-Collider als Default
- Heuristik: Wenn das Mesh annähernd kugelförmig ist (AABB-Aspektverhältnis ≈ 1:1:1) → Sphere
- Heuristik: Wenn das Mesh langgestreckt ist → Capsule
- „Auto-Fit Collider"-Button in den Physics-Details
- Collider-Visualisierung als transparente Wireframe-Overlay im Viewport

**Fortschrittsprüfung:**
- [x] „Add Component → Physics" erzeugt automatisch passenden Collider
- [x] „Auto-Fit"-Button berechnet Collider-Dimensionen aus dem Mesh
- [ ] Collider-Wireframe ist im Viewport sichtbar

### 3.6 Intelligent Snap & Grid
Surface-Snap implementiert: `dropSelectedEntitiesToSurface()` in `UIManager` mit Callback-Pattern (`RaycastDownFn`) für DLL-Grenzüberschreitung. `computeEntityBottomOffset()` berechnet den Abstand vom Entity-Pivot zur Unterseite der Mesh-AABB. End-Taste als Shortcut + Settings-Dropdown-Eintrag „Drop to Surface (End)". Undo/Redo-Integration über `UndoRedoManager::pushCommand()`.

**Ziel:** Konfigurierbares Snap-to-Grid und Surface-Snapping für präzise Platzierung.

**Herangehensweise:**
- Grid-Overlay im Viewport (XZ-Ebene, konfigurierbare Größe: 0.25, 0.5, 1, 2, 5, 10 Einheiten)
- Snap-to-Grid: Modifier (Ctrl gedrückt) rastert Gizmo-Bewegungen auf Grid-Einheiten
- Surface-Snap: Modifier (Shift gedrückt) setzt Entity auf die nächste Oberfläche (Raycast nach unten)
- Rotation-Snap: 15°-Schritte wenn Ctrl gedrückt
- Scale-Snap: 0.1-Schritte wenn Ctrl gedrückt
- Anzeige in der Toolbar: Grid-Size-Dropdown, Snap-Toggle-Button
- Snap-Settings in Engine Settings persistiert

**Fortschrittsprüfung:**
- [x] Grid-Overlay im Viewport sichtbar (toggelbar)
- [x] Snap aktiviert rastet Translate auf Grid
- [x] Surface-Snap platziert Entity auf Oberfläche
- [x] Rotation-Snap funktioniert in 15°-Schritten

---

## Phase 4 – Content-Browser-Erweiterungen

> **Ziel:** Content Browser zu einem vollwertigen Asset-Management-Tool ausbauen.

### 4.1 Asset-Thumbnails
**Aktueller Stand:** Vollständig implementiert. FBO-gerenderte Thumbnails für Model3D- und Material-Assets im Content Browser. `Renderer::generateAssetThumbnail(assetPath, assetType)` virtuelles Interface, `OpenGLRenderer`-Implementierung rendert 128×128 FBO mit Auto-Kamera (AABB-Framing), Directional Light, Material/PBR-Support. Material-Preview nutzt prozedural erzeugte UV-Sphere (16×12). Thumbnail-Cache (`m_thumbnailCache`) verhindert redundantes Rendering. `ensureThumbnailFbo()`/`releaseThumbnailFbo()` verwalten GL-Ressourcen. Content-Browser-Grid (`populateContentBrowserWidget`) zeigt Thumbnails für Model3D, Material und Texture-Assets — Texturen via `preloadUITexture()`, 3D-Modelle und Materialien via `generateAssetThumbnail()`.

**Ziel:** Echte Thumbnails für Texturen, Meshes und Materialien.

**Herangehensweise:**
- **Texturen:** Thumbnail = verkleinerte Version des Bildes (64×64). Bei Discovery generieren und in `Editor/Thumbnails/` cachen.
- **Meshes:** Offline-Render eines Thumbnails per FBO (analoge Technik wie Mesh Viewer, aber in 64×64 rendern). Cache in `Editor/Thumbnails/`.
- **Materialien:** Render einer Kugel mit dem Material (wie Material-Preview, 64×64 FBO).
- **Scripts:** Typ-Icon (kein inhaltliches Thumbnail nötig)
- Lazy-Generation: Thumbnail wird beim ersten Anzeigen im Grid generiert, dann gecacht
- Background-Thread für Thumbnail-Generierung (nicht den Main-Thread blockieren)
- Cache-Invalidierung: Wenn sich das Asset ändert (isSaved=false), Thumbnail neu generieren

**Fortschrittsprüfung:**
- [x] Texturen zeigen ihr Bild als Thumbnail
- [x] 3D-Modelle zeigen einen kleinen Render (128×128 FBO, Auto-Kamera mit AABB-Framing)
- [x] Materialien zeigen eine Kugel mit dem Material (UV-Sphere 16×12 mit PBR)
- [x] Thumbnails werden gecacht und schnell geladen

### 4.2 Erweiterte Such- und Filterfunktion
**Aktueller Stand:** Vollständig implementiert. Echtzeit-Suchfeld + 7 Typ-Filter-Toggle-Buttons in der PathBar. Suchmodus durchsucht alle Ordner als flache Liste. Typ-Filter per Bitmask. Ctrl+F fokussiert Suchfeld. Doppelklick auf Suchergebnis navigiert zum Ordner + öffnet Asset-Editor.

**Ziel:** Echtzeit-Suche über alle Assets, Filter nach Typ.

**Herangehensweise:**
- Such-EntryBar oben im Content Browser (über Breadcrumbs)
- Echtzeit-Filter: Während der Eingabe werden nur matchende Assets angezeigt
- Suchmodus: Durchsucht ALLE Ordner (nicht nur den aktuellen), zeigt Ergebnisse als flache Liste
- Filter-Toggles: Buttons für jeden Asset-Typ (Mesh, Material, Texture, Script, Audio, Shader, Level, Widget)
- Kombinierbar: Suche + Typ-Filter gleichzeitig
- Keyboard-Shortcut: Ctrl+F fokussiert das Suchfeld

**Fortschrittsprüfung:**
- [x] Suchfeld ist sichtbar im Content Browser
- [x] Echtzeit-Filtern funktioniert
- [x] Typ-Filter-Buttons reduzieren die Ergebnisse
- [x] Ctrl+F fokussiert das Suchfeld

### 4.3 Drag & Drop Verbesserungen
**Aktueller Stand:** Drag & Drop von Assets auf den Viewport zum Spawnen existiert. Textur-Drag auf Entity-Details existiert. Material-Drop und Script-Drop auf Viewport-Entities zuweisen funktioniert. OS-Datei-Drop (`SDL_EVENT_DROP_FILE`) importiert Dateien automatisch basierend auf Dateiendung via `AssetManager::importAssetFromPath()`.

**Ziel:** Umfassenderes Drag & Drop:
- Material auf eine Entity im Viewport droppen → Material zuweisen
- Textur auf Material-Slot im Details-Panel droppen → Textur zuweisen
- Script auf eine Entity droppen → ScriptComponent hinzufügen
- Prefab auf Viewport droppen → Prefab instanziieren
- Asset aus Explorer (OS) in den Content Browser droppen → Importieren

**Herangehensweise:**
- Viewport-Drop: Pick-Buffer nutzen um die Ziel-Entity zu bestimmen
- Typ-Erkennung: Aus dem Asset-Typ die richtige Aktion ableiten
- Visueller Feedback: Ghost-Preview des zu droppenden Assets am Mauszeiger
- OS-Drag: `SDL_EVENT_DROP_FILE` verarbeiten → Auto-Import basierend auf Dateiendung

**Fortschrittsprüfung:**
- [x] Material-Drop auf Entity im Viewport weist das Material zu
- [x] Script-Drop auf Entity fügt ScriptComponent hinzu
- [x] Dateien aus dem OS-Explorer können in den Content Browser gedroppt werden
- [ ] Visueller Ghost-Preview bei Drag

### 4.4 Asset-Referenz-Tracking
**Aktueller Stand:** ✅ Vollständig implementiert. „Refs"-Button zeigt alle Entities/Assets die ein Asset nutzen. „Deps"-Button zeigt alle Abhängigkeiten. Lösch-Warnung mit Referenzanzahl. Unreferenzierte Assets mit dezenter Punkt-Markierung im Grid-Tile.

**Ziel:** „Find References" und „Used By"-Ansichten für Assets.

**Herangehensweise:**
- Referenz-Graph: Beim Laden eines Levels alle Mesh→Material→Texture-Beziehungen aufbauen
- Content Browser PathBar → „Refs"-Button zeigt eine Liste aller Entities/Assets die dieses Asset nutzen
- Content Browser PathBar → „Deps"-Button zeigt alle Assets die dieses Asset referenziert
- Warnung beim Löschen: „This asset is used by {n} entities. Delete anyway?"
- Warnung als Icon im Grid-Tile: Unreferenzierte Assets werden mit gedämpftem Punkt-Symbol markiert

**Fortschrittsprüfung:**
- [x] „Find References" zeigt korrekte Ergebnis-Liste
- [x] Lösch-Warnung erscheint bei referenzierten Assets
- [x] Unreferenzierte Assets sind visuell erkennbar

---

## Phase 5 – Viewport-UX & Workflow

> **Ziel:** Der Viewport als zentrales Arbeitswerkzeug optimieren.

### 5.1 Viewport-Einstellungen Panel
**Aktueller Stand:** ✅ Toolbar-Buttons (CamSpeed, Stats, Snap) funktional. Settings-Dropdown mit Quick-Toggles. Focus on Selection (F-Taste) implementiert.

**Ziel:** Viewport-Toolbar mit Visualisierungs-Optionen.

**Herangehensweise:**
- Dropdown/Toolbar am oberen Rand des Viewports (innerhalb des ViewportOverlay)
- Render-Mode-Dropdown: Lit / Unlit / Wireframe / Normals / Depth / Overdraw / Shadow Map / Cascades / Instance Groups
- Grid-Toggle: World Grid ein/aus
- Stats-Toggle: FPS / Draw Calls / Triangle Count
- Post-Processing Toggles: Bloom, SSAO, FXAA (schneller Zugriff statt Engine Settings)
- Snap-Einstellungen: Grid Size, Snap aktiv/inaktiv
- Kamera-Speed-Slider
- „Fokus auf Selektion"-Button (Kamera zentriert auf die ausgewählte Entity)

**Fortschrittsprüfung:**
- [x] Render-Mode-Dropdown wechselt die Darstellung
- [x] Grid-Toggle funktioniert
- [x] Kamera-Speed ist einstellbar
- [x] Fokus-Button zentriert die Kamera

### 5.2 Multi-Select & Group Operations
**Aktueller Stand:** ✅ Grundlegende Multi-Select implementiert. `std::unordered_set<unsigned int>` Selektionsmodell. Ctrl+Click im Viewport fügt/entfernt Entities. Gizmo (Translate/Rotate/Scale) bewegt alle selektierten Entities gleichzeitig mit Group-Undo/Redo. Delete entfernt alle selektierten Entities mit Group-Undo. Selection-Outline für alle selektierten Entities. Tab-Wechsel speichert/stellt Selektion pro Tab wieder her.

**Ziel:** Mehrfachauswahl und Gruppenoperationen.

**Herangehensweise:**
- Ctrl+Klick: Entity zur Selektion hinzufügen
- Shift+Klick: Bereichsauswahl im Outliner
- Gizmo-Operations auf alle selektierten Entities gleichzeitig
- Rechtsklick-Kontextmenü für Gruppen: Delete All, Duplicate All, Group (Parent-Child)
- Rubber-Band-Selection: Rechteck im Viewport aufziehen um mehrere Entities zu selektieren
- Selektion in `std::unordered_set<uint32_t>` statt `uint32_t`

**Fortschrittsprüfung:**
- [x] Ctrl+Klick fügt Entities zur Selektion hinzu
- [x] Gizmo bewegt alle selektierten Entities gleichzeitig
- [x] Rubber-Band-Selection funktioniert im Viewport
- [x] Löschen entfernt alle selektierten Entities (mit Undo)

### 5.3 Entity Copy/Paste & Duplicate
**Aktueller Stand:** Vollständig implementiert. `EntityClipboard`-Struct mit 13 Komponenten-Snapshots. Ctrl+C kopiert, Ctrl+V pastet (+1x Offset, Name-Suffix „(Copy)"), Ctrl+D dupliziert ohne Clipboard zu überschreiben. Undo/Redo-Integration.

**Ziel:** Ctrl+C / Ctrl+V / Ctrl+D für Entities.

**Herangehensweise:**
- Ctrl+C: Entity-Snapshot (alle Komponenten) in einen internen Clipboard-Buffer
- Ctrl+V: Neuen Entity erstellen mit den Clipboard-Daten, leichter Position-Offset (+1,0,0)
- Ctrl+D: Duplicate = Copy + Paste in einem Schritt
- Undo/Redo-Integration: Jeder Paste/Duplicate wird als UndoRedo-Command registriert
- Multi-Select: Copy/Paste von mehreren Entities gleichzeitig

**Fortschrittsprüfung:**
- [x] Ctrl+C + Ctrl+V dupliziert eine Entity
- [x] Duplizierte Entity hat einen Positions-Offset
- [x] Ctrl+D funktioniert als Shortcut
- [x] Undo macht das Duplizieren rückgängig

### 5.4 Transform-Einrasten auf Oberflächen
**Aktueller Stand:** Implementiert. `dropSelectedEntitiesToSurface()` in `UIManager` setzt selektierte Entities per Raycast nach unten (-Y) auf die nächste Oberfläche. `computeEntityBottomOffset()` berechnet den Abstand vom Entity-Pivot zur Mesh-AABB-Unterseite (Stride-5-Vertex-Daten, skaliert mit Entity-Scale). Callback-Pattern (`RaycastDownFn = std::function<pair<bool,float>(float,float,float)>`) überbrückt die DLL-Grenze zwischen Renderer und Physics. End-Taste als Shortcut, Settings-Dropdown-Eintrag „Drop to Surface (End)". Undo/Redo-Unterstützung mit alter Y-Position. Multi-Select-fähig.

**Ziel:** Entity auf die nächste Oberfläche „fallen lassen" per Button oder Shortcut.

**Herangehensweise:**
- „Drop to Surface"-Button in der Viewport-Toolbar (oder Shortcut: End-Taste)
- Raycast von der Entity-Position nach unten (-Y)
- Entity-Position.y = Trefferpunkt.y + Entity-AABB-Halbhöhe
- Funktioniert auch auf Landscapes / HeightFields
- Optional: Ausrichtung der Entity-Rotation an der Oberflächen-Normale

**Fortschrittsprüfung:**
- [x] Entity wird korrekt auf der Oberfläche platziert
- [x] Funktioniert auf flachen und schrägen Oberflächen
- [x] Funktioniert auf Landscape-Terrain

---

## Phase 6 – Erweitertes Editor-Framework

> **Ziel:** Das Editor-Framework flexibler und erweiterbarer machen.

### 6.1 Docking-System
**Aktueller Stand:** Panels sind fest positioniert (Links: Outliner/Details, Unten: Content Browser, Oben: TitleBar/Toolbar).

**Ziel:** Panels können per Drag & Drop an verschiedene Positionen (Left, Right, Bottom, Tab) gedockt werden.

**Herangehensweise:**
- Jedes Panel bekommt ein `DockZone`-Enum (Left, Right, Bottom, Center, Float)
- Drag-Handle am oberen Rand jedes Panels
- Drop-Previews: Blaue Highlight-Fläche zeigt die Dock-Position
- Panels können als Tabs nebeneinander gestapelt werden
- Panel-Konfiguration wird in `config.ini` gespeichert
- Default-Layout als Fallback

**Komplexität:** Hoch. Alternativ: Preset-basierte Layouts (z.B. „Default", „Wide Monitor", „Scripting", „Art") die per Dropdown umschaltbar sind.

**Fortschrittsprüfung:**
- [ ] Mindestens 3 vordefinierte Layouts verfügbar
- [ ] Layout-Wechsel funktioniert ohne Crash
- [ ] Layout wird bei Neustart wiederhergestellt

### 6.2 Keyboard-Shortcut-System
**Aktueller Stand:** Implementiert. `ShortcutManager`-Singleton (`src/Core/ShortcutManager.h/.cpp`) mit Registry aller Aktionen (id, displayName, category, defaultCombo, currentCombo, callback). 20+ Shortcuts registriert in `main.cpp`. Konfigurations-UI in Editor Settings mit klickbaren Keybind-Buttons, Konflikt-Erkennung und „Reset All to Defaults“. F1 Shortcut-Hilfe Popup. Persistenz via `shortcuts.cfg`.

**Ziel:** Zentrales, konfigurierbares Shortcut-System.

**Herangehensweise:**
- `ShortcutManager` Singleton mit Registry aller Aktionen
- Jede Aktion: Name, Default-Shortcut, Callback
- Konfigurations-UI in Editor Settings: Liste aller Shortcuts, klickbar zum Ändern
- Shortcut-Konflikte werden erkannt und gewarnt
- Export/Import als JSON
- In-App Shortcut-Hilfe (Ctrl+? oder F1 → Liste aller Shortcuts)

**Fortschrittsprüfung:**
- [x] Alle bestehenden Shortcuts sind im ShortcutManager registriert
- [x] Editor Settings zeigen die Shortcut-Liste
- [x] Shortcuts können umbelegt werden
- [x] F1 zeigt die Shortcut-Hilfe

### 6.3 Benachrichtigungs- und Fortschritts-System
**Aktueller Stand:** Implementiert. `NotificationHistoryEntry`-Struct + `m_notificationHistory`-Deque (max 50 Einträge) + `openNotificationHistoryPopup()` für Verlauf. `ProgressBarHandle` mit `beginProgress()`/`updateProgress()`/`endProgress()` für nicht-blockierende Mehrfach-Progress-Bars. `NotificationLevel`-Enum (Info/Warning/Error) mit visueller Differenzierung. `refreshNotificationBadge()` zeigt Badge-Zähler in der StatusBar.

**Ziel:** Erweitertes Benachrichtigungssystem mit Verlauf und nicht-blockierendem Fortschritt.

**Herangehensweise:**
- Benachrichtigungs-Verlauf: Die letzten 50 Toasts werden in einer Liste gespeichert
- Klick auf ein Benachrichtigungs-Icon in der StatusBar → Benachrichtigungs-Panel klappt auf
- Nicht-blockierende Progress-Bars: Mehrere gleichzeitige Fortschrittsanzeigen in der StatusBar
- Priorisierung: Error-Benachrichtigungen bleiben länger sichtbar
- Sound-Feedback: Optionaler Ton bei Error-Benachrichtigungen

**Fortschrittsprüfung:**
- [x] Benachrichtigungs-Verlauf ist abrufbar
- [x] Mehrere Progress-Bars gleichzeitig möglich
- [x] Error-Benachrichtigungen heben sich visuell ab

---

## Phase 7 – Scripting & Debugging

> **Ziel:** Python-Scripting-Workflow massiv verbessern.

### 7.1 Integrierter Script-Editor
**Aktueller Stand:** Scripts werden extern bearbeitet. Kein In-Engine-Editing.

**Ziel:** Basis-Script-Editor als Tab mit Syntax-Highlighting, Auto-Completion und direkter Ausführung.

**Herangehensweise:**
- Doppelklick auf Script-Asset → öffnet Script-Editor-Tab
- Multi-Line-EntryBar als Code-Editor (existiert bereits als `isMultiline`)
- Syntax-Highlighting für Python (Keyword-Erkennung → farbige Text-Segmente via RichText)
- Auto-Indent (Tab → 4 Spaces, Enter behält Einrückung bei)
- Zeilen-Nummerierung (links)
- Basis-Completion: `engine.`-Prefix zeigt die verfügbaren API-Methoden (aus `engine.pyi` geparst)
- Error-Unterstreichung: Bei Python-Fehler wird die Zeile rot markiert (Feedback aus dem Script-Tick-Exception-Handler)
- Ctrl+S speichert das Script-Asset
- „Run Script"-Button (startet PIE mit Fokus auf das Script)

**Komplexität:** Hoch (vollwertiger Text-Editor ist aufwändig). Alternativ: Integration eines externen Editors per Shortcut (OS-Default-Editor öffnen), plus Auto-Reload bei Dateiänderung (Hot-Reload für Scripts).

**Fortschrittsprüfung:**
- [ ] Script wird im Tab angezeigt mit Syntax-Highlighting
- [ ] Zeilen-Nummerierung sichtbar
- [ ] Ctrl+S speichert
- [ ] Fehler-Zeilen werden markiert

### 7.2 Debug-Breakpoints & Variable Inspector
**Aktueller Stand:** Kein Debugging für Python-Scripts. Fehler werden nur im Log angezeigt.

**Ziel:** Basis-Debugging: Breakpoints setzen, Script pausieren, Variablen inspizieren.

**Herangehensweise:**
- Klick auf Zeilennummer im Script-Editor → Breakpoint (roter Punkt)
- Bei Breakpoint: PIE pausiert, Script-Zustand wird eingefroren
- Variable-Inspector-Panel: Zeigt alle lokalen Variablen des aktuellen Script-Scopes
- Step Over / Step Into / Continue Buttons
- Python `sys.settrace()` für Breakpoint-Implementierung
- Call-Stack-Anzeige

**Komplexität:** Sehr hoch. Erste Iteration: Nur Breakpoints + Variable-View, kein Stepping.

**Fortschrittsprüfung:**
- [ ] Breakpoints können gesetzt werden
- [ ] PIE pausiert am Breakpoint
- [ ] Lokale Variablen werden angezeigt
- [ ] Continue-Button setzt die Ausführung fort

### 7.3 Script-Hot-Reload
**Aktueller Stand:** Vollständig implementiert. `ScriptHotReload`-Klasse überwacht rekursiv das `Content/`-Verzeichnis auf `.py`-Dateiänderungen (analog zu `ShaderHotReload`, aber mit `recursive_directory_iterator`). Poll-Intervall 500ms. Bei Änderung wird das Script-Modul im Python-Interpreter neu geladen (alte ScriptState freigegeben, neue Modul-Funktionen extrahiert), wobei `startedEntities` erhalten bleibt (kein erneutes `onloaded`). Level-Skript wird ebenfalls erkannt und neu geladen. Toast-Benachrichtigung zeigt Erfolg/Fehler an. Toggle in Engine Settings → General → Scripting mit config.ini-Persistenz via `DiagnosticsManager::setState("ScriptHotReloadEnabled", ...)`.

**Ziel:** Wenn eine .py-Datei extern geändert wird, wird sie automatisch neu geladen.

**Herangehensweise:**
- File-Watcher auf das `Content/`-Verzeichnis (analog zu `ShaderHotReload`)
- Bei `.py`-Dateiänderung: Script-Modul im Python-Interpreter neu laden
- Falls PIE läuft: Script-Entity-Funktionen werden mit neuer Version aufgerufen
- Toast-Benachrichtigung: „Script {name} reloaded"
- Falls Syntax-Error: Error-Toast statt Reload

**Fortschrittsprüfung:**
- [x] Externe Script-Änderung wird erkannt
- [x] Script wird im laufenden PIE neu geladen
- [x] Syntax-Fehler werden als Toast angezeigt
- [x] Alter Script-Zustand bleibt erhalten (soweit möglich)

---

## Phase 8 – Polish & Barrierefreiheit

> **Ziel:** Letzte Schliffe für ein professionelles Endprodukt.

### 8.1 Tooltip-System
**Aktueller Stand:** Tooltip-System vollständig implementiert. `UIManager` verwaltet Tooltip-State (`m_tooltipTimer`, `m_tooltipText`, `m_tooltipPosition`, `m_tooltipVisible`). Hover-Tracking in `updateHoverStates()` erkennt Elemente mit `tooltipText`, startet Timer bei neuem Element, blendet sofort aus beim Verlassen. Timer-Advancement in `updateNotifications(dt)` löst nach `kTooltipDelay=0.45s` die Tooltip-Anzeige aus. Tooltip wird als temporäres `EditorWidget` mit z-Order 10000 erstellt (Background-Panel + Text), Position = Maus + 16px Offset (DPI-skaliert), Bildschirm-Clamping. Styling: `toastBackground`/`toastText` aus EditorTheme. Tooltips auf allen Toolbar-Buttons (Render Mode, Undo, Redo, PIE, Snap, CamSpeed, Stats, Settings), TitleBar-Buttons (Minimize, Maximize, Close), StatusBar-Buttons (Undo, Redo, Save All), Remove-Component-Buttons und Add-Component-Dropdown.

**Ziel:** Tooltips werden nach 0.45s Hover-Delay als schwebende Text-Box angezeigt.

**Herangehensweise:**
- Hover-Timer in `updateHoverStates()`: Element-Wechsel → Timer-Reset, gleich → akkumulieren
- Timer-Advancement in `updateNotifications(dt)`: nach `kTooltipDelay` → Tooltip-Widget erstellen
- Tooltip-Widget: `EditorWidget` z=10000, `toastBackground`/`toastText`, Bildschirm-Clamping
- Sofort ausblenden + Widget entfernen wenn die Maus das Element verlässt
- Tooltips auf: Toolbar (8), TitleBar (3), StatusBar (3), Remove Component (dynamisch), Add Component

**Fortschrittsprüfung:**
- [x] Hover über Button mit Tooltip zeigt die Beschreibung
- [x] Tooltip verschwindet beim Verlassen des Elements
- [x] Tooltip-Styling passt zum Theme
- [x] Alle wichtigen Buttons haben Tooltips

### 8.2 Onboarding / Erste-Schritte-Wizard
**Aktueller Stand:** Neues Projekt → leerer Editor. Kein Guidance für Erstbenutzer.

**Ziel:** Optionaler Erste-Schritte-Wizard beim ersten Projektstart.

**Herangehensweise:**
- Erkennung: `config.ini` Flag `OnboardingShown = false`
- Wizard-Overlay: Semi-transparenter Overlay mit Spotlights auf einzelne Panels
- 5-7 Schritte: Viewport (Navigation), Outliner (Entities), Details (Komponenten), Content Browser (Assets), Toolbar (PIE), Widget Editor, Scripting
- Jeder Schritt: Bild/Highlight + kurzer Text + „Next"/„Skip"/„Done"
- „Don't show again"-Checkbox
- Aufrufbar über Help-Menü: „Show Welcome Guide"

**Fortschrittsprüfung:**
- [ ] Wizard startet beim ersten Projekt-Öffnen
- [ ] Alle 5+ Schritte sind sichtbar und navigierbar
- [ ] „Skip" überspringt den Rest
- [ ] „Don't show again" verhindert erneutes Anzeigen

### 8.3 Keyboard-Navigation
**Aktueller Stand:** Implementiert. `handleKeyDown()` in `UIManager` verarbeitet Tab/Shift+Tab (EntryBar-Zyklen), Escape (Dropdown→Modal→Entry schließen), Pfeiltasten (Outliner- und Content-Browser-Navigation). Fokussierte EntryBars erhalten einen blauen Outline-Highlight.

**Ziel:** Grundlegende Keyboard-Navigation im Editor (Tab durch Felder, Enter zum Bestätigen).

**Herangehensweise:**
- Tab/Shift+Tab im Details-Panel: Springt zwischen EditBars
- Enter in Dialogen: Bestätigt den primären Button
- Escape: Schließt Modals/Dropdowns
- Pfeiltasten im Outliner: Entity-Auswahl navigieren
- Pfeiltasten im Content Browser: Grid-Navigation
- Fokus-Highlight: Subtiler blauer Rahmen um das fokussierte Element

**Fortschrittsprüfung:**
- [x] Tab springt durch Details-Panel-Felder
- [x] Enter bestätigt Dialoge
- [x] Escape schließt Dropdowns
- [x] Pfeiltasten navigieren den Outliner

### 8.4 Undo/Redo-Erweiterungen
**Aktueller Stand:** Undo/Redo existiert für Gizmo-Transforms, Entity-Löschen/Spawn und Widget-Editor-Operationen.

**Ziel:** Undo/Redo für ALLE Editor-Aktionen.

**Herangehensweise:**
- Details-Panel Wertänderungen → Undo (alten Wert speichern)
- Material-Zuweisungen → Undo
- Komponenten hinzufügen/entfernen → Undo
- Content Browser: Löschen, Umbenennen, Verschieben → Undo
- Asset-Imports → Undo (importierte Dateien entfernen)
- Gruppierte Undo-Commands (z.B. Multi-Entity-Delete als ein Undo-Schritt)

**Fortschrittsprüfung:**
- [x] Wertänderung im Details-Panel ist undoable
- [x] Komponenten-Hinzufügen ist undoable
- [x] Material-Zuweisung per Drag & Drop ist undoable
- [x] Kein Undo-Fall wurde vergessen

---

## Phase 9 – Erweiterte Visualisierung & Scene-Debugging

> **Ziel:** Entwickler brauchen visuelle Werkzeuge, um die unsichtbaren Systeme der Engine zu verstehen – Collider, Bones, Shader-Output, Render-Passes. Diese Phase macht das Unsichtbare sichtbar.

### 9.1 Shader Editor / Viewer Tab
**Priorität:** Mittel
**Aufwand:** Hoch

**Aktueller Stand:** Shader-Varianten-System existiert (`ShaderVariantKey`, `#define`-basierte Permutationen), Shader-Hot-Reload ist implementiert. Kein In-Engine-Viewer für Shader-Code.

**Beschreibung:** Dedizierter Tab zum Betrachten, Bearbeiten und Debuggen von GLSL-Shadern. Read-Only-Modus für Engine-Shader, editierbar für Custom-Shader mit Live-Preview.

**Herangehensweise:**
- Doppelklick auf ein Shader-Asset (oder via Renderer-Debug-Menü) → öffnet Shader-Editor-Tab
- **Links:** Shader-Liste (alle geladenen Shader-Programme, gruppiert nach Typ: World, UI, Shadow, Post-Process, Particle)
- **Mitte:** Code-Viewer mit Syntax-Highlighting (GLSL-Keywords, Typen, Uniforms, Kommentare)
- **Rechts:** Shader-Info-Panel: aktive Varianten-Flags, Uniform-Liste mit aktuellen Werten, Compile-Status
- Syntax-Highlighting: RichText-basiert, Keyword-Erkennung für `uniform`, `in`, `out`, `void`, `vec2/3/4`, `mat4`, `sampler2D`, `#define`, `#ifdef` etc.
- Fehler-Anzeige: Bei Compile-Fehler wird die fehlerhafte Zeile rot markiert + Fehlermeldung
- Shader-Varianten-Dropdown: Zwischen verschiedenen Permutationen eines Shaders wechseln
- „Reload Shader"-Button (löst Hot-Reload für den aktuellen Shader aus)
- Zeilennummerierung, Monospace-Font

**Fortschrittsprüfung:**
- [x] Shader-Code wird mit Syntax-Highlighting angezeigt
- [x] Alle geladenen Shader sind in der Liste verfügbar
- [ ] Uniform-Werte werden live angezeigt
- [ ] Compile-Fehler werden markiert
- [ ] Shader-Varianten können umgeschaltet werden

### 9.2 Collider & Physics Visualisierung
**Priorität:** Hoch (essentiell für Physics-Debugging)
**Aufwand:** Mittel

**Aktueller Stand:** ✅ Vollständig implementiert. Wireframe-Overlays für alle Collider-Typen (Box, Sphere, Capsule, Cylinder) mit Farb-Codierung nach MotionType (Grün=Static, Orange=Kinematic, Blau=Dynamic, Rot=Trigger/Sensor). Toggle-Button "Col" in der ViewportOverlay-Toolbar. Nutzt bestehenden Gizmo-Shader (GL_LINES). Berücksichtigt Entity-Transform (Position/Rotation) und Collider-Offset.

**Beschreibung:** Transparente Wireframe-Overlays für Collider-Shapes direkt im Viewport, toggelbar über die Toolbar.

**Herangehensweise:**
- Neuer Render-Pass nach dem Welt-Rendering (vor UI): Collider-Debug-Overlay
- Collider-Typen als Wireframe:
  - **Box:** 12 Linien (Kanten des Quaders), transformiert mit Entity-Transform + Collider-Offset
  - **Sphere:** 3 Kreise (XY, XZ, YZ Ebenen) als Linien-Approximation (32 Segmente)
  - **Capsule:** 2 Halbkugeln + 4 vertikale Linien
  - **Cylinder:** 2 Kreise + 4 vertikale Linien
- Farbe: Grün (statisch), Orange (kinematisch), Blau (dynamisch), Rot (Trigger)
- Toggle über Toolbar: „Col"-Button (neben Stats/Grid)
- Nur sichtbar wenn nicht im PIE-Modus
- Nutzt bestehenden Gizmo-Shader (GL_LINES) mit angepasster Farbe

**Fortschrittsprüfung:**
- [x] Box-Collider werden als Wireframe angezeigt
- [x] Sphere-Collider als Kreise sichtbar
- [x] Capsule-Collider korrekt dargestellt
- [x] Cylinder-Collider korrekt dargestellt
- [x] Toggle in der Toolbar funktioniert
- [x] Farbe unterscheidet Collider-Typen (Static/Kinematic/Dynamic/Sensor)

### 9.3 Bone / Skeleton Debug-Overlay
**Priorität:** Mittel (hilfreich für Animationsarbeit)
**Aufwand:** Mittel

**Aktueller Stand:** ✅ Vollständig implementiert. `renderBoneDebug()` in `OpenGLRenderer` zeichnet für selektierte Entities mit `SkeletalAnimator` die Bone-Hierarchie als Wireframe-Overlay. Bone-Positionen werden aus `finalBoneMatrices * inverse(offsetMatrix)` extrahiert und mit der Entity-Model-Matrix in Weltkoordinaten transformiert. Linien von jedem Bone zu seinem Parent (Cyan). Joint-Marker als 3D-Kreuz an jeder Bone-Position. Root-Bone hervorgehoben (Gelb, größerer Marker). "Bone"-Toggle-Button in der ViewportOverlay-Toolbar. Aktualisiert sich jeden Frame bei laufender Animation.

**Beschreibung:** Wireframe-Linien zwischen Bone-Positionen als Debug-Overlay im Viewport für Skinned Meshes.

**Herangehensweise:**
- Bone-Rendering: Linie von jedem Bone zu seinem Parent (Bone-Hierarchie aus `Skeleton`)
- Bone-Positionen: Aus den aktuellen `finalBoneMatrices` des `SkeletalAnimator` extrahieren (Position = letzte Spalte der Matrix × inverse(offsetMatrix) × Model-Matrix)
- Darstellung: Dünne Linien (cyan) + kleine 3D-Kreuz-Marker an Bone-Positionen
- Root-Bone hervorgehoben (gelb, größerer Marker)
- Toggle: „Bone"-Button in Viewport-Toolbar
- Nur für selektierte Entities (nicht für alle Skinned Meshes gleichzeitig → Performance)
- Aktualisiert sich jeden Frame wenn Animation läuft

**Fortschrittsprüfung:**
- [x] Bone-Linien sind im Viewport sichtbar
- [x] Positionen stimmen mit der aktuellen Animation überein
- [x] Toggle funktioniert
- [x] Root-Bone ist hervorgehoben
- [x] Performance-Impact < 0.5ms bei 100+ Bones

### 9.4 Render-Pass-Debugger
**Priorität:** Niedrig (eher für Engine-Entwickler als für Endnutzer)
**Aufwand:** Hoch

**Aktueller Stand:** Render-Modes existieren (Lit/Unlit/Wireframe/Normals/Depth/Shadow Map/Cascades/Instance Groups). Keine Möglichkeit, einzelne Render-Passes zu isolieren oder die Render-Pipeline zu inspizieren.

**Beschreibung:** Ein Debug-Tab oder Overlay, das die gesamte Render-Pipeline visualisiert: welche FBOs aktiv sind, welche Passes in welcher Reihenfolge laufen, und die Möglichkeit, einzelne Buffer (G-Buffer, Shadow Maps, Bloom-Mips) zu inspizieren.

**Herangehensweise:**
- **Pipeline-Übersicht:** Visualisierung der Render-Passes als Flow-Diagramm (Vertical StackPanel)
  - Shadow Pass (3 Cascades + Point-Shadows) → Geometry Pass → OIT Pass → Post-Process (Bloom, FXAA, SSAO) → UI Pass
- **Buffer-Inspector:** Klick auf einen Pass zeigt den zugehörigen FBO-Output als Textur
  - Shadow Maps: Tiefenvisualisierung (schwarz-weiß, linearisiert)
  - G-Buffer Channels (falls deferred): Albedo, Normal, Depth, Specular
  - Bloom-Mips: Einzelne Mip-Levels der Bloom-Kette
  - OIT-Buffer: Accum und Reveal-Texturen
- **Pro-Pass-Statistiken:** Draw Calls, Triangle Count, Render-Time (GPU Timer Query)
- Integration: Neuer Tab über Settings-Dropdown → „Render Debugger"
- Buffer-Texturen werden per `glGetTexImage` in CPU-Speicher gelesen und als UI-Image angezeigt

**Fortschrittsprüfung:**
- [x] Pipeline-Passes werden als Liste angezeigt
- [ ] Shadow Maps können inspiziert werden
- [x] Per-Pass-Draw-Call-Count ist sichtbar
- [ ] Bloom-Mips können einzeln betrachtet werden
- [ ] FBO-Texturen werden korrekt visualisiert

---

## Phase 10 – Build & Packaging

> **Ziel:** Die Engine soll nicht nur ein Editor sein, sondern auch fertige Spiele ausliefern können. Diese Phase schafft die Grundlage für das Packaging von Standalone-Builds.
>
> 🔄 **Status:** Phase 10.1 (Standalone Game Build) ist nach Abschluss von Phase 12.1 (Editor-Separation) neu implementiert. Build-Dialog im Editor mit Fortschrittsanzeige, 6-Schritt-Pipeline (OutputDir → Runtime-EXE+DLLs → Content → AssetRegistry → game.ini → Launch). Phase 10.2 (Asset-Cooking) und 10.3 (Build-Profile) stehen noch aus.

### 10.1 Standalone Game Build
**Priorität:** Hoch (langfristig das wichtigste Feature)
**Aufwand:** Sehr hoch

**Aktueller Stand:** ✅ Vollständig neu implementiert auf Basis von Phase 12.1 (Editor-Separation). Build-Dialog als PopupWindow mit Formular (Start Level, Window Title, Launch After Build, Output Dir). 6-Schritt-Pipeline in `main.cpp` mit Fortschrittsanzeige (in try/catch gewrappt für Crash-Saftey): (1) Output-Verzeichnis erstellen, (2) HorizonEngineRuntime.exe + alle DLLs kopieren, (3) Content-Ordner rekursiv kopieren, (4) AssetRegistry.bin in Config/ kopieren, (5) game.ini generieren (StartLevel, WindowTitle, BuildProfile=Development), (6) Optional Launch via ShellExecuteA/system. Runtime erkennt automatisch die native Auflösung und startet im Fullscreen-Modus. `UIManager::BuildGameConfig`-Struct, `openBuildGameDialog()`, `showBuildProgress()`/`updateBuildProgress()`/`closeBuildProgress()` für modale ProgressBar mit Popup-Lifetime-Schutz. Pre-fill von Start Level (erste Level-Asset aus Registry), Window Title (Projektname), Output Dir (Projekt/Build). "Build Game..." im Settings-Dropdown.

**Beschreibung:** Möglichkeit, ein Projekt als eigenständige Anwendung zu exportieren (ohne Editor-UI, nur Gameplay).

**Herangehensweise:**
- **Runtime-Executable:** Separates CMake-Target `EngineRuntime` das nur die Kernmodule enthält (Renderer, ECS, Physics, Audio, Scripting) ohne Editor-UI-Code
- **Level-Loader:** Startup-Level wird aus einer `game.ini` gelesen und automatisch geladen
- **Asset-Bundling:** Alle referenzierten Assets werden in ein `Data/`-Verzeichnis kopiert
- **Splash-Screen:** Optionaler Ladebildschirm während dem Level-Load
- **Build-Dialog im Editor:**
  - Zielverzeichnis wählen
  - Start-Level auswählen
  - Fenster-Titel vergeben
  - Launch After Build Checkbox
  - Fortschrittsbalken während dem Build (Crash-Safe)
- **Window-Einstellungen:** Fenster-Titel aus `game.ini`. Auflösung und Fullscreen-Modus werden automatisch ermittelt
- **Python-Runtime:** Embedded Python wird mitgepackt (nur benötigte Module)
- **DLL/SO-Dependencies:** Automatisches Kopieren aller Runtime-DLLs (SDL3, Assimp, PhysX, etc.)

**Fortschrittsprüfung:**
- [x] Build-Dialog im Editor verfügbar
- [x] Standalone-Executable startet ohne Editor-UI automatisch im Fullscreen (HorizonEngineRuntime.exe)
- [x] Erstes Level wird automatisch geladen (game.ini → StartLevel)
- [x] Assets sind korrekt gebundelt (Content-Ordner + AssetRegistry.bin)
- [ ] Python-Scripts funktionieren im Standalone-Build

### 10.2 Asset-Cooking-Pipeline
**Priorität:** Mittel
**Aufwand:** Hoch

**Aktueller Stand:** ⏸️ Asset-Cooking-Implementierung (cookAssetsForBuild, generateAssetManifest) wurde zusammen mit dem Build-Dialog entfernt. Wird im Rahmen des Build-Reworks neu implementiert.

**Beschreibung:** Beim Build werden Assets in ein optimiertes Runtime-Format konvertiert: Texturen komprimiert, Meshes in Binärformat, unnötige Metadaten entfernt.

**Herangehensweise:**
- **Textur-Cooking:** Unkomprimierte Texturen → DDS (BC1/BC3/BC7) mit Mipmap-Generierung
- **Mesh-Cooking:** JSON-basierte Mesh-Daten → Binärformat (Header + Vertex-Buffer + Index-Buffer), schnelleres Laden
- **Material-Cooking:** Shader-Varianten vorkompilieren (SPIR-V oder Precompiled GLSL), nur benötigte Varianten behalten
- **Asset-Manifest:** `manifest.json` listet alle Assets mit Pfaden, Typen und Größen für schnelles Lookup
- **Inkrementelles Cooking:** Nur geänderte Assets werden neu gecooked (Timestamp-basiert)
- **Cooking als Background-Task:** Progress-Bar im Editor

**Fortschrittsprüfung:**
- [ ] Texturen werden als DDS gecooked
- [ ] Mesh-Binärformat lädt schneller als JSON
- [ ] Asset-Manifest wird generiert (entfernt – Rework geplant)
- [ ] Inkrementelles Cooking funktioniert (entfernt – Rework geplant)
- [ ] Gecooked Build ist kleiner als Raw-Assets

### 10.3 Build-Konfigurationsprofile
**Priorität:** Mittel
**Aufwand:** Mittel

**Aktueller Stand:** ⏸️ BuildProfile-Enum und buildProfileToString() wurden zusammen mit dem Build-Dialog entfernt. Die Runtime-Profil-Lesung aus game.ini im main.cpp bleibt erhalten. Wird im Rahmen des Build-Reworks neu implementiert.

**Beschreibung:** Vordefinierte Build-Profile die verschiedene Optimierungsstufen und Feature-Sets steuern.

**Herangehensweise:**
- **3 Profile:**
  - **Debug:** Volle Validierung, Shader-Hot-Reload, Logging verbose, keine Asset-Compression, Collider-Visualization
  - **Development:** Moderate Optimierung, Hot-Reload aktiv, Logging normal, optionale Kompression
  - **Shipping:** Maximale Optimierung, kein Hot-Reload, Logging minimal, volle Asset-Compression, kein Profiler
- **Profil-Auswahl:** Dropdown im Build-Dialog und in der Toolbar
- **`#define`-basierte Feature-Gates:** `ENGINE_DEBUG`, `ENGINE_DEV`, `ENGINE_SHIPPING`
- **Profil-Persistenz:** In `game.ini` gespeichert
- **Profil-spezifische Einstellungen:** LogLevel, HotReload, Validation, Profiler, AssetCompression

**Fortschrittsprüfung:**
- [ ] 3 Profile sind auswählbar (entfernt – Rework geplant)
- [ ] Shipping-Build enthält kein Debug-Code
- [ ] Profil beeinflusst Asset-Cooking
- [ ] Render-Qualität ändert sich pro Profil

---

## Phase 11 – Erweiterte Editor-Workflows

> **Ziel:** Fortgeschrittene Workflows für erfahrene Nutzer und größere Projekte. Fokus auf Produktivität und kreative Werkzeuge.

### 11.1 Multi-Viewport-Layout ✅
**Priorität:** Mittel
**Aufwand:** Hoch

**Aktueller Stand:** ✅ Vollständig implementiert. Viewport kann in Single/Two Horizontal/Two Vertical/Quad aufgeteilt werden. Jeder Sub-Viewport hat eigene Kamera mit Preset (Perspective/Top/Front/Right). Aktiver Sub-Viewport wird per Klick ausgewählt (blauer Rahmen). Kamera-Steuerung (WASD + Maus) wird an den aktiven Sub-Viewport geroutet.

**Beschreibung:** Viewport in 2–4 Teilbereiche aufteilen (wie in Blender/3ds Max) für gleichzeitige Ansichten aus verschiedenen Winkeln.

**Implementierung:**
- **Renderer.h:** `ViewportLayout` Enum (Single/TwoHorizontal/TwoVertical/Quad), `SubViewportPreset` Enum, `SubViewportCamera` Struct, virtuelle API (set/getViewportLayout, set/getActiveSubViewport, getSubViewportCount, get/setSubViewportCamera, subViewportHitTest), statische String-Helfer
- **OpenGLRenderer.h/cpp:** `kMaxSubViewports=4`, `m_subViewportCameras[4]` Array, `ensureSubViewportCameras()` (initialisiert Perspective/Top/Front/Right), `computeSubViewportRects()` (Pixel-Rects mit 2px Gap), `subViewportHitTest()`, `m_currentSubViewportIndex` für Render-Steuerung
- **render():** Multi-Viewport-Schleife – ruft `renderWorld()` N-mal mit verschiedenem `m_currentSubViewportIndex` auf, synchronisiert Cam[0] von der Haupt-Editor-Kamera
- **renderWorld():** Sub-Viewport-Rect für glViewport/glScissor, erster Sub-Viewport bindet FBO + Full-Clear, nachfolgende nutzen Scissor + lokales Clear, Sub-Viewport-Kamera-View-Matrix für Index > 0, Gizmo/Selektion/Rubber-Band nur im aktiven Sub-Viewport, blauer Rahmen + Preset-Label
- **moveCamera/rotateCamera:** Routing an aktiven Sub-Viewport wenn Index > 0
- **AssetManager.cpp:** Layout-Button (▣) in ViewportOverlay-Toolbar
- **main.cpp:** Layout-Dropdown (Single/Two Horizontal/Two Vertical/Quad), Sub-Viewport-Auswahl per Linksklick via subViewportHitTest

**Fortschrittsprüfung:**
- [x] 4-View-Split funktioniert (Top/Front/Right/Perspective)
- [x] Jeder Sub-Viewport hat unabhängige Kamera-Navigation
- [x] Gizmo funktioniert im aktiven Sub-Viewport
- [x] Entity-Selektion synchronisiert sich über alle Viewports
- [x] Performance-Impact < 2× bei 4 Viewports

### 11.2 Cinematic Sequencer UI ✅
**Priorität:** Mittel
**Aufwand:** Hoch

**Aktueller Stand:** ✅ Vollständig implementiert. Dedizierter Sequencer-Tab mit Toolbar, Timeline, Keyframe-Liste und 3D-Spline-Visualisierung im Viewport. Playback-Controls (Play/Pause/Stop/Loop) steuern die bestehende Catmull-Rom-CameraPath-Engine.

**Beschreibung:** Visueller Editor für Kamera-Pfade und Cinematic-Sequenzen mit Timeline, Spline-Visualisierung im Viewport und Keyframe-Editing.

**Implementierung:**
- **Renderer.h:** Neue virtuelle Accessoren für CameraPath-Daten: `getCameraPathPoints`/`setCameraPathPoints`, `getCameraPathDuration`/`setCameraPathDuration`, `getCameraPathLoop`/`setCameraPathLoop`
- **OpenGLRenderer.h/cpp:** Override-Implementierungen der 6 neuen CameraPath-Accessoren
- **UIManager.h:** `SequencerState`-Struct (tabId, widgetId, playing, playbackSpeed, scrubberT, selectedKeyframe, showSplineInViewport, loopPlayback, pathDuration). `openSequencerTab`/`closeSequencerTab`/`isSequencerOpen` public API. Private Helfer: `refreshSequencerTimeline`, `buildSequencerToolbar`, `buildSequencerTimeline`, `buildSequencerKeyframeList`
- **UIManager.cpp Sequencer-Tab:** Toolbar mit Sequencer-Label, Add/Remove-Keyframe-Buttons, Play/Pause/Stop/Loop-Controls, Spline-Visibility-Toggle, Duration-Anzeige. Timeline-Bar mit Keyframe-Markern (farbcodiert selected=accent/normal=grau), rotem Scrubber bei Playback. Scrollbare Keyframe-Liste mit Index, Position (x,y,z), Rotation (Yaw/Pitch), Selected-Highlight. Klick auf Keyframe → Smooth-Camera-Transition zum Punkt. Add Keyframe → aktuelle Kamera-Position/Rotation. Periodic Refresh (0.1s) während Playback
- **OpenGLRenderer.cpp renderWorld():** 3D-Spline-Visualisierung wenn Sequencer offen: Catmull-Rom-Polyline (100 Segmente, orange GL_LINES), gelbe GL_POINTS für Kontrollpunkte, via m_gizmoProgram
- **main.cpp:** "Sequencer" Eintrag im Settings-Dropdown

**Fortschrittsprüfung:**
- [x] Kamera-Pfad wird als 3D-Spline im Viewport angezeigt
- [x] Kontrollpunkte als Markers in Timeline und Keyframe-Liste sichtbar
- [x] Timeline zeigt Keyframes auf der Zeitleiste
- [x] Playback-Preview funktioniert
- [ ] Sequenz kann als Python-Script exportiert werden (optional, Python-API bereits vorhanden)

### 11.3 Editor-Plugin-System (Python) ✅
**Priorität:** Niedrig (Infrastruktur für die Zukunft)
**Aufwand:** Sehr hoch

**Aktueller Stand:** ✅ Vollständig implementiert. Neues `engine.editor`-Python-Submodul mit 8 Funktionen (show_toast, get_selected_entities, get_asset_list, create_entity, select_entity, add_menu_item, register_tab, get_menu_items). Plugin-Discovery lädt `.py`-Dateien aus `Editor/Plugins/` beim Start. Hot-Reload überwacht Plugin-Verzeichnis und lädt bei Änderungen neu. Plugin-Menüeinträge erscheinen im Settings-Dropdown. Toast-Level-Konstanten (TOAST_INFO/SUCCESS/WARNING/ERROR). `engine.pyi` mit vollständigen Editor-Stubs aktualisiert.

**Beschreibung:** Python-basiertes Plugin-System, das Zugriff auf Editor-Funktionen erlaubt: eigene Tabs erstellen, Menüeinträge hinzufügen, Asset-Import-Pipelines erweitern.

**Implementierung:**
- **`engine.editor`-Modul:** `EditorMethods[]` PyMethodDef-Array + `EditorModule` PyModuleDef in `PythonScripting.cpp`. Via `AddSubmodule()` in `CreateEngineModule()` registriert.
  - `engine.editor.show_toast(message, level=0)` – Toast-Benachrichtigungen (Info/Success/Warning/Error)
  - `engine.editor.get_selected_entities()` – Liste der selektierten Entity-IDs
  - `engine.editor.get_asset_list(type_filter=-1)` – Asset-Registry als Dict-Liste (name/path/type)
  - `engine.editor.create_entity(name)` – Entity mit Name + Transform erstellen
  - `engine.editor.select_entity(entity_id)` – Entity im Editor selektieren
  - `engine.editor.add_menu_item(menu, name, callback)` – Eigene Menüpunkte im Settings-Dropdown
  - `engine.editor.register_tab(name, on_build_ui)` – Eigene Editor-Tabs registrieren
  - `engine.editor.get_menu_items()` – Alle registrierten Plugin-Menüeinträge abfragen
- **Plugin-Discovery:** `LoadEditorPlugins(projectRoot)` scannt `Editor/Plugins/*.py`, führt jede Datei via `PyRun_String()` aus
- **Hot-Reload:** `PollPluginHotReload()` via `ScriptHotReload`-Instanz (500ms Polling), bei Änderung werden alle Plugins neu geladen
- **Toast-Konstanten:** `TOAST_INFO=0`, `TOAST_SUCCESS=1`, `TOAST_WARNING=2`, `TOAST_ERROR=3`
- **main.cpp:** `LoadEditorPlugins()` nach Script-Hot-Reload-Init, `PollPluginHotReload()` im Main-Loop, Plugin-Menüeinträge im Settings-Dropdown
- **PythonScripting.h:** Neue API: `LoadEditorPlugins()`, `PollPluginHotReload()`, `GetPluginMenuItems()`, `GetPluginTabs()`, `InvokePluginMenuCallback()`
- **engine.pyi:** Vollständige `editor`-Klasse mit allen Methoden und Konstanten

**Fortschrittsprüfung:**
- [x] `engine.editor`-Modul ist verfügbar
- [x] Plugins in `Editor/Plugins/` werden automatisch geladen
- [x] Ein Plugin kann einen Toast anzeigen
- [x] Ein Plugin kann einen eigenen Tab erstellen
- [x] Hot-Reload funktioniert

### 11.4 Level-Streaming-UI ✅
**Priorität:** Niedrig (für größere Projekte relevant)
**Aufwand:** Hoch

**Aktueller Stand:** ✅ Implementiert. Sub-Level-System mit Streaming Volumes, Level-Composition-Panel und Viewport-Wireframe-Visualisierung.

**Beschreibung:** Editor-UI für Level-Streaming: mehrere Levels gleichzeitig geladen, visuelles Management der Stream-Bereiche.

**Herangehensweise:**
- **Persistent Level:** Ein Haupt-Level das immer geladen bleibt
- **Streaming Volumes:** Box-förmige Trigger-Bereiche die Sub-Levels laden/entladen
- **Level-Composition-Panel:** Neuer Panel-Bereich der alle geladenen Sub-Levels zeigt
  - Checkboxen: Geladen/Entladen
  - Visibility-Toggle: Im Editor ein-/ausblenden
  - Farbcode: Jedes Sub-Level hat eine Farbe für die Wireframe-Bounding-Box im Viewport
- **Viewport-Integration:** Streaming-Volumes als Wireframe-Boxen im Viewport
- **Runtime-Verhalten:** Kamera-Position triggert automatisches Laden/Entladen
- **Seamless Loading:** Sub-Levels werden in einem Background-Thread geladen

**Implementierungsdetails:**
- `Renderer.h`: `SubLevelEntry` (name, levelPath, loaded, visible, color) und `StreamingVolume` (center, halfExtents, subLevelIndex) Structs mit vollem Management-API (add/remove/toggle/updateLevelStreaming)
- `UIManager.h/cpp`: `LevelCompositionState` mit `openLevelCompositionTab()`/`closeLevelCompositionTab()`/`refreshLevelCompositionPanel()`, Toolbar (Add Sub-Level, Remove, Add Volume, Toggle Volumes), Sub-Level-Liste mit Farb-Indikator/Loaded/Visible-Toggles, Streaming-Volume-Liste
- `OpenGLRenderer.cpp`: `renderStreamingVolumeDebug()` – Wireframe-Box-Visualisierung (12 Kanten, farbig nach Sub-Level) via Gizmo-Shader
- `main.cpp`: "Level Composition" im Settings-Dropdown, `updateLevelStreaming()` im Main Loop (kamerabasiertes Auto-Load/Unload)

**Fortschrittsprüfung:**
- [x] Mehrere Levels können gleichzeitig geladen sein
- [x] Streaming-Volumes sind im Viewport sichtbar
- [x] Level-Composition-Panel zeigt alle Sub-Levels
- [x] Automatisches Laden/Entladen basierend auf Kamera-Position
- [x] Level-Übergang ist ohne Lade-Freeze

---

## Phase 12 – Editor-Separation-Rework (Architektur)

> **Ziel:** Den Editor grundlegend vom Engine-Kern trennen. Der Editor wird über eine klar definierte API an die Engine angebunden und kann per Präprozessor-Definition (`#if ENGINE_EDITOR`) beim Build für das fertige Game deaktiviert werden. Dies ist die Voraussetzung für ein sauberes Build & Packaging System (Phase 10).

### 12.1 Engine-Editor-API-Layer
**Priorität:** Hoch (Architektur-Grundlage)
**Aufwand:** Sehr hoch

**Aktueller Stand:** ✅ Abgeschlossen (Phase 1 – main.cpp Guards + CMake-Targets).

**Beschreibung:** Saubere Trennung von Engine-Kern und Editor-Code durch eine definierte API-Schicht.

**Herangehensweise:**
- **`ENGINE_EDITOR`-Präprozessor-Definition:** Alle Editor-spezifischen Code-Pfade werden mit `#if ENGINE_EDITOR` / `#endif` umschlossen
- **Engine-API-Header:** `EngineAPI.h` definiert die öffentliche Schnittstelle (Renderer, ECS, Physics, Audio, Scripting, AssetManager) ohne Editor-Abhängigkeiten
- **Editor als separates Modul:** UIManager, EditorTheme, EditorUIBuilder, ViewportOverlay-Toolbar, Gizmo-Rendering werden in ein `Editor/`-Verzeichnis verschoben
- **Editor-Hooks:** Engine stellt Callbacks/Events bereit (onEntitySelected, onLevelLoaded, onFrameRendered) die der Editor registriert
- **CMake-Targets:** Zwei Targets – `Engine` (Kern) und `EngineEditor` (Kern + Editor). `ENGINE_EDITOR` wird nur für `EngineEditor` definiert
- **Runtime-Build:** `Engine`-Target kompiliert ohne Editor-Code → kleinere Executable, kein Editor-UI-Overhead
- **Schrittweise Migration:** Editor-Code wird Datei für Datei extrahiert, nicht alles auf einmal

**Fortschrittsprüfung:**
- [x] `ENGINE_EDITOR`-Define existiert und wird in CMake gesetzt
- [x] UIManager ist hinter `#if ENGINE_EDITOR` isoliert (in main.cpp)
- [x] Engine kompiliert ohne Editor-Code (`ENGINE_EDITOR=0`)
- [x] Editor-Hooks sind definiert und funktional (via Callbacks & Guards)
- [x] Zwei CMake-Targets (Engine, EngineEditor) existieren
- [x] Phase 10 (Build & Packaging) baut auf dem Runtime-Target auf

---

## Fortschrittsübersicht

| Phase | Beschreibung | Priorität | Aufwand | Status |
|-------|-------------|-----------|---------|--------|
| **1.1** | Abgerundete Panels | Mittel | Mittel | ✅ |
| **1.2** | Schatten & Tiefe | Niedrig | Mittel | ✅ |
| **1.3** | Modernisierte Icons | Hoch | Mittel | ✅ |
| **1.4** | Überarbeitetes Spacing | Hoch | Gering | ✅ |
| **1.5** | Animierte Übergänge | Niedrig | Mittel | ✅ |
| **1.6** | Scrollbar-Design | Niedrig | Gering | ✅ |
| **2.1** | Console / Log-Viewer | Hoch | Mittel | ✅ |
| **2.2** | Material Editor Tab | Hoch | Hoch | ✅ |
| **2.3** | Texture Viewer | Mittel | Gering | ✅ |
| **2.4** | Animation Editor | Mittel–Hoch | Hoch | ✅ |
| **2.5** | Particle Editor | Mittel | Mittel | ✅ |
| **2.6** | Audio Preview | Niedrig–Mittel | Gering | ✅ |
| **2.7** | Profiler Tab | Mittel | Mittel | ✅ |
| **3.1** | Auto-Material bei Import | Hoch | Gering | ✅ |
| **3.2** | Entity Templates / Prefabs | Hoch | Mittel | ✅ |
| **3.3** | One-Click Scene Setup | Mittel | Gering | ✅ |
| **3.4** | Auto-LOD-Generierung | Mittel | Hoch | ❌ |
| **3.5** | Auto-Collider-Generierung | Mittel | Gering | ✅ |
| **3.6** | Intelligent Snap & Grid | Hoch | Mittel | ✅ |
| **4.1** | Asset-Thumbnails | Hoch | Mittel | ✅ |
| **4.2** | Erweiterte Suche & Filter | Hoch | Gering | ✅ |
| **4.3** | Drag & Drop Verbesserungen | Mittel | Mittel | 🔄 |
| **4.4** | Asset-Referenz-Tracking | Mittel | Mittel | ✅ |
| **5.1** | Viewport-Einstellungen Panel | Hoch | Gering | ✅ |
| **5.2** | Multi-Select & Group Ops | Hoch | Hoch | ✅ |
| **5.3** | Entity Copy/Paste | Hoch | Gering | ✅ |
| **5.4** | Transform-Einrasten | Mittel | Mittel | ✅ |
| **6.1** | Docking-System | Niedrig | Sehr hoch | ❌ |
| **6.2** | Keyboard-Shortcut-System | Mittel | Mittel | ✅ |
| **6.3** | Benachrichtigungs-System | Niedrig | Gering | ✅ |
| **7.1** | Integrierter Script-Editor | Mittel | Sehr hoch | ❌ |
| **7.2** | Debug-Breakpoints | Niedrig | Sehr hoch | ❌ |
| **7.3** | Script-Hot-Reload | Hoch | Mittel | ✅ |
| **8.1** | Tooltip-System | Hoch | Gering | ✅ |
| **8.2** | Onboarding-Wizard | Niedrig | Mittel | ❌ |
| **8.3** | Keyboard-Navigation | Mittel | Mittel | ✅ |
| **8.4** | Undo/Redo-Erweiterungen | Hoch | Mittel | ✅ |
| **9.1** | Shader Editor / Viewer Tab | Mittel | Hoch | 🔄 |
| **9.2** | Collider & Physics Visualisierung | Hoch | Mittel | ✅ |
| **9.3** | Bone / Skeleton Debug-Overlay | Mittel | Mittel | ✅ |
| **9.4** | Render-Pass-Debugger | Niedrig | Hoch | 🔄 |
| **10.1** | Standalone Game Build | Hoch | Sehr hoch | ✅ |
| **10.2** | Asset-Cooking-Pipeline | Mittel | Hoch | ⏸️ |
| **10.3** | Build-Konfigurationsprofile | Mittel | Mittel | ⏸️ |
| **11.1** | Multi-Viewport-Layout | Mittel | Hoch | ✅ |
| **11.2** | Cinematic Sequencer UI | Mittel | Hoch | ✅ |
| **11.3** | Editor-Plugin-System (Python) | Niedrig | Sehr hoch | ✅ |
| **11.4** | Level-Streaming-UI | Niedrig | Hoch | ✅ |
| **12.1** | Engine-Editor-API-Layer | Hoch | Sehr hoch | ✅ |

### Empfohlene Reihenfolge (Sprints)

**Sprint 1 – Quick Wins (Visual Refresh):** ✅ abgeschlossen
~~1.4 Spacing~~✅, ~~1.3 Icons~~✅, ~~8.1 Tooltips~~✅, ~~1.1 Rounded Panels~~✅

**Sprint 2 – Essenzielle Tabs:** ✅ abgeschlossen
~~2.1 Console~~✅, ~~5.1 Viewport-Einstellungen~~✅, ~~4.2 Suche & Filter~~✅

**Sprint 3 – Automatisierung:** ✅ abgeschlossen
~~3.1 Auto-Material~~✅, ~~3.5 Auto-Collider~~✅, ~~3.3 Scene Setup~~✅, ~~5.3 Copy/Paste~~✅

**Sprint 4 – Asset-Management:** ✅ abgeschlossen
~~4.1 Thumbnails~~✅, ~~2.2 Material Editor Tab~~✅, ~~2.3 Texture Viewer~~✅

**Sprint 5 – Viewport-Workflow:** ✅ abgeschlossen
~~3.6 Snap & Grid~~✅, ~~5.2 Multi-Select~~✅, ~~5.4 Surface-Snap~~✅

**Sprint 6 – Erweiterte Tabs:** ✅ abgeschlossen
~~2.5 Particle Editor~~✅, ~~2.4 Animation Editor~~✅, ~~2.7 Profiler~~✅

**Sprint 7 – Scripting:** 🔄 teilweise
~~7.3 Script Hot-Reload~~✅, 7.1 Script Editor ❌, ~~2.6 Audio Preview~~✅

**Sprint 8 – Framework & Polish:** ✅ abgeschlossen
~~6.2 Shortcut-System~~✅, ~~8.3 Keyboard-Navigation~~✅, ~~8.4 Undo/Redo~~✅, ~~3.2 Prefabs~~✅

**Sprint 9 – Advanced (Phase 1–6 Reste):**
3.4 Auto-LOD, ~~1.2 Elevation/Shadows~~✅, ~~1.5 Animations~~✅, 6.1 Docking

**Sprint 10 – Final Polish (Phase 6–8 Reste):**
~~6.3 Benachrichtigungs-System~~✅, 8.2 Onboarding, ~~1.6 Scrollbar~~✅, ~~4.3 Drag & Drop~~🔄, ~~4.4 Referenz-Tracking~~✅, 7.2 Debug-Breakpoints

**Sprint 11 – Visualisierung & Debugging (Phase 9):** 🔄 teilweise
~~9.2 Collider-Visualisierung~~✅, ~~9.3 Bone-Overlay~~✅, ~~9.1 Shader Editor~~🔄, ~~9.4 Render-Pass-Debugger~~🔄

**Sprint 12 – Build & Packaging (Phase 10):** 🔄 teilweise
~~10.1 Standalone Game Build~~✅, ~~10.2 Asset Cooking~~⏸️, ~~10.3 Build-Profile~~⏸️
→ 10.1 nach Sprint 14 (Editor-Separation) neu implementiert.

**Sprint 13 – Erweiterte Workflows (Phase 11):** ✅ abgeschlossen
~~11.2 Cinematic Sequencer~~✅, ~~11.1 Multi-Viewport~~✅, ~~11.3 Plugin-System~~✅, ~~11.4 Level-Streaming~~✅

**Sprint 14 – Editor-Separation-Rework (Phase 12):** ✅ abgeschlossen
~~12.1 Engine-Editor-API-Layer~~✅ → Zwei CMake-Targets, ENGINE_EDITOR-Guards
