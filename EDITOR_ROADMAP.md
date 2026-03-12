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
9. [Fortschrittsübersicht](#fortschrittsübersicht)

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
| Material Editor Tab (voller Tab, nicht nur Popup) | ❌ |
| Shader Editor / Viewer | ❌ |
| Console / Log-Viewer | ❌ |
| Animation Editor (Skeletal) | ❌ |
| Particle Editor | ❌ |
| Texture Viewer | ❌ |
| Audio Preview | ❌ |
| Blueprint / Visual Scripting | ❌ |
| Profiler / Performance-Monitor | ❌ |
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
**Aktueller Stand:** Kein visuelles Tiefensystem. Alle Panels wirken „flach" auf einer Ebene.

**Ziel:** Leichter Drop-Shadow auf schwebenden Elementen (Dropdowns, Modals, Toasts, Kontextmenüs) für visuelle Hierarchie.

**Herangehensweise:**
- Neues `elevation`-Feld (int, 0–5) in `WidgetElement` – bestimmt die Shadow-Intensität
- Shadow-Rendering als leicht versetzte, verschwommene Hintergrund-Rect unter dem Element (2-Pass: erst Shadow, dann Content)
- `EditorTheme` um `shadowColor` (Vec4, z.B. `{0, 0, 0, 0.3}`) und `shadowOffset` (Vec2, z.B. `{2, 3}`) erweitern
- Elevation-Stufen: 0 = kein Shadow (normale Panels), 1 = leicht (Sektions-Header), 2 = mittel (Dropdowns), 3 = stark (Modals), 4–5 = extra (Drag-Ghost)
- Modals, Toasts und Dropdown-Menüs bekommen automatisch `elevation = 3`

**Fortschrittsprüfung:**
- [ ] Dropdown-Menüs haben sichtbaren Schatten
- [ ] Modal-Dialoge heben sich visuell vom Hintergrund ab
- [ ] Toasts wirken wie schwebende Karten
- [ ] Elevation 0 Panels haben keinen Schatten (kein Overhead)

### 1.3 Modernisierte Icon-Sprache
**Aktueller Stand:** Icons werden als PNG-Texturen geladen (`Editor/Textures/`) und per Tint-Color eingefärbt. Es gibt keine Icon-Font und keine SVG-Unterstützung.

**Ziel:** Konsistente, skalierbare Icon-Sprache für den gesamten Editor.

**Herangehensweise:**
- **Option A (empfohlen):** Icon-Atlas – Eine einzelne PNG-Sprite-Sheet mit allen Editor-Icons (32×32 pro Icon, z.B. 16×16 Grid = 256 Icons). UV-Lookup statt vieler Einzeltexturen. `EditorTheme` bekommt eine `iconAtlasPath`-Eigenschaft.
- **Option B:** Icon-Font (z.B. Material Icons oder FontAwesome als TTF einbetten). Der vorhandene FreeType-basierte `OpenGLTextRenderer` kann TTF-Glyphen rendern. Neues `drawIcon(iconId, pos, size, color)` API.
- Standard-Icon-Set definieren: Ordner, Datei, Mesh, Material, Texture, Audio, Script, Shader, Light, Camera, Entity, Play, Pause, Stop, Save, Undo, Redo, Delete, Add, Search, Settings, Filter, Collapse, Expand, Eye, EyeOff, Lock, Unlock, Pin, Link, Unlink
- Alle hardcoded Text-Symbole (▾, ▸, ◆, ×, +, ■, ▶) durch Icon-Referenzen ersetzen

**Fortschrittsprüfung:**
- [ ] Mindestens 20 Editor-Icons als Atlas oder Icon-Font verfügbar
- [ ] Content Browser nutzt neue Icons statt farbiger Quadrate
- [ ] Outliner Entity-Einträge haben Typ-Icons (Mesh, Light, Camera, etc.)
- [ ] Toolbar-Buttons nutzen Icons statt Text

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
- [ ] Button-Hover hat sichtbaren Farbübergang (~120ms)
- [ ] Toast-Notifications faden ein/aus statt zu poppen
- [ ] Performance-Impact: < 0.1ms pro Frame Overhead

### 1.6 Verbessertes Scrollbar-Design
**Aktueller Stand:** Scrollbalken existieren als dünne Overlay-Tracks (`scrollbarTrack`/`scrollbarThumb` im Theme). Design ist funktional, aber nicht besonders modern.

**Ziel:** Overlapping Scrollbars, die nur bei Hover/Scroll erscheinen (macOS-Stil).

**Herangehensweise:**
- Scrollbar-Sichtbarkeit standardmäßig auf `auto-hide` (erscheint bei Scroll-Aktivität, blendet nach 1.5s aus)
- Scrollbar-Breite: 6px (statt 8px), bei Hover auf 10px verbreitern
- Runde Scroll-Thumb-Enden (via `borderRadius` auf dem Thumb-Panel)
- `EditorTheme` um `scrollbarAutoHide` (bool, default true) und `scrollbarWidth` (float, 6.0) erweitern

**Fortschrittsprüfung:**
- [ ] Scrollbars verschwinden wenn nicht gescrollt wird
- [ ] Scrollbars verbreitern sich beim Hover
- [ ] Scrollbar-Design ist konsistent in allen scrollbaren Panels

---

## Phase 2 – Fehlende Editor-Tabs

> **Ziel:** Alle wichtigen Asset-Typen und Workflows in dedizierten Editor-Tabs abbilden.

### 2.1 Console / Log-Viewer Tab
**Priorität:** Hoch (essentiell für Debugging und Scripting)

**Beschreibung:** Ein dedizierter Tab (oder festes Bottom-Panel) für Live-Log-Output. Zeigt Nachrichten des Logger-Systems (INFO/WARNING/ERROR/FATAL), Python-Print-Output, und Engine-Diagnosen.

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
- [ ] Python `print()` erscheint im Console-Tab
- [ ] Performance: 10.000+ Einträge ohne Stutter

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
- [ ] Doppelklick auf Material im Content Browser öffnet Material-Tab
- [ ] Preview-Sphäre zeigt das Material korrekt an
- [ ] PBR-Slider-Änderungen sind sofort in der Preview sichtbar
- [ ] Textur-Drag & Drop funktioniert
- [ ] Material-Änderungen werden korrekt gespeichert

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
- [ ] Textur wird korrekt dargestellt
- [ ] Zoom/Pan funktioniert
- [ ] Einzelne RGBA-Kanäle können isoliert werden
- [ ] Metadaten werden angezeigt
- [ ] DDS-komprimierte Texturen werden korrekt dargestellt

### 2.4 Animation Editor Tab
**Priorität:** Mittel–Hoch (Skeletal Animation ist bereits implementiert, aber es gibt keine Editing-UI)

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
- [ ] Clip-Auswahl wechselt die laufende Animation
- [ ] Scrubber steuert die Animation-Zeit
- [ ] Bone-Hierarchie wird als Tree angezeigt
- [ ] Speed-Slider beeinflusst die Abspielgeschwindigkeit

### 2.5 Particle Editor Tab
**Priorität:** Mittel (Particle-System existiert, aber Parameter nur über Python API oder ECS editierbar)

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
- [ ] Preview zeigt Partikel in Echtzeit
- [ ] Alle 20 Parameter sind editierbar
- [ ] Änderungen sind sofort sichtbar
- [ ] Mindestens 3 Presets verfügbar
- [ ] Parameter werden korrekt in ECS zurückgeschrieben

### 2.6 Audio Preview Panel
**Priorität:** Niedrig–Mittel

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
- [ ] Audio kann abgespielt werden
- [ ] Scrubber zeigt die aktuelle Position
- [ ] Metadaten werden angezeigt
- [ ] Waveform wird visuell dargestellt

### 2.7 Profiler / Performance-Monitor Tab
**Priorität:** Mittel (F10 Overlay existiert, aber kein dedizierter Tab mit Verlaufshistorie)

**Beschreibung:** Dedizierter Tab für Performance-Analyse mit Echtzeit-Graphen und Frame-Analyse.

**Herangehensweise:**
- Die existierenden GPU Timer Queries und CPU-Metriken nutzen
- **Oben:** FPS-Graph (letzte 300 Frames als Liniendiagramm)
- **Mitte:** Aufschlüsselungs-Balken pro Frame: World-Rendering, UI-Rendering, Layout, ECS, Physics
- **Unten:** Tabellarische Detailansicht (Draw Calls, Triangles, Texture Memory, etc.)
- Frame-Capture: Button zum „Einfrieren" des aktuellen Frames für Detail-Analyse
- CSV-Export für externe Analyse-Tools
- Warnschwellen: Zeilen werden rot wenn > 16.6ms (60fps), gelb wenn > 8.3ms (120fps)

**Fortschrittsprüfung:**
- [ ] FPS-Graph zeigt Live-Daten
- [ ] Aufschlüsselung nach Rendering-Phase sichtbar
- [ ] Frame-Einfrieren funktioniert
- [ ] Draw-Call-Count wird korrekt angezeigt

---

## Phase 3 – Automatisierung & Smart-Features

> **Ziel:** Dem Entwickler möglichst viel Arbeit abnehmen. Intelligente Defaults, Auto-Konfiguration, One-Click-Workflows.

### 3.1 Auto-Material-Erstellung bei Model-Import
**Aktueller Stand:** Assimp extrahiert bereits Materialien und Texturen beim Import. Allerdings muss der Entwickler die Material-Assets manuell zuweisen.

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
- [ ] FBX-Import erzeugt automatisch Material-Assets
- [ ] Texturen werden automatisch extrahiert und referenziert
- [ ] Mesh im Viewport zeigt sofort die korrekten Texturen
- [ ] Toast-Nachricht informiert über den Import-Umfang

### 3.2 Smart Entity Templates / Prefabs
**Aktueller Stand:** Keine Prefab- oder Template-Funktionalität. Jede Entity muss manuell konfiguriert werden.

**Ziel:** Vorgefertigte Entity-Templates für häufige Anwendungsfälle, die per Drag & Drop instanziiert werden können.

**Herangehensweise:**
- Neuer Asset-Typ `AssetType::Prefab` (JSON: Liste von Entities mit Komponenten)
- Content Browser → Rechtsklick → „New Prefab" erstellt ein Prefab aus der aktuell selektierten Entity (mit allen Komponenten)
- Drag & Drop eines Prefab-Assets auf den Viewport erzeugt alle Entities mit ihren Komponenten
- Eingebaute Templates:
  - **Empty Entity** (nur Transform)
  - **Point Light** (Transform + Light + Name)
  - **Directional Light** (Transform + Light)
  - **Camera** (Transform + Camera)
  - **Static Mesh** (Transform + Mesh + Material)
  - **Physics Object** (Transform + Mesh + Material + Physics)
  - **Particle Emitter** (Transform + ParticleEmitter)
- „Templates"-Sektion im Content Browser oder als klappbare Palette im Outliner

**Fortschrittsprüfung:**
- [ ] „Create Prefab" aus selektierter Entity funktioniert
- [ ] Prefab-Asset im Content Browser sichtbar
- [ ] Drag & Drop erzeugt korrekte Entity mit allen Komponenten
- [ ] Mindestens 5 Built-in-Templates verfügbar

### 3.3 One-Click Scene Setup
**Aktueller Stand:** Neue Levels sind leer. Der Entwickler muss manuell Lichter, Kamera und Skybox hinzufügen.

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
- [ ] „New Level"-Dialog zeigt Template-Auswahl
- [ ] „Basic Outdoor" erzeugt Level mit Licht, Skybox und Boden
- [ ] „Prototype" erzeugt Level mit Grid und Basis-Geometrie
- [ ] Editor-Kamera blickt auf die generierte Szene

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
**Aktueller Stand:** Collision-Collider (Box/Sphere/Capsule/Cylinder/HeightField) müssen manuell konfiguriert werden.

**Ziel:** Bei Hinzufügen einer `PhysicsComponent` automatisch einen passenden Collider vorschlagen.

**Herangehensweise:**
- Mesh-AABB auslesen → Box-Collider als Default
- Heuristik: Wenn das Mesh annähernd kugelförmig ist (AABB-Aspektverhältnis ≈ 1:1:1) → Sphere
- Heuristik: Wenn das Mesh langgestreckt ist → Capsule
- „Auto-Fit Collider"-Button in den Physics-Details
- Collider-Visualisierung als transparente Wireframe-Overlay im Viewport

**Fortschrittsprüfung:**
- [ ] „Add Component → Physics" erzeugt automatisch passenden Collider
- [ ] „Auto-Fit"-Button berechnet Collider-Dimensionen aus dem Mesh
- [ ] Collider-Wireframe ist im Viewport sichtbar

### 3.6 Intelligent Snap & Grid
**Aktueller Stand:** Kein Snapping, kein Grid-System im Viewport.

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
- [ ] Grid-Overlay im Viewport sichtbar (toggelbar)
- [ ] Ctrl+Move rastet auf Grid
- [ ] Surface-Snap platziert Entity auf Oberfläche
- [ ] Rotation-Snap funktioniert in 15°-Schritten

---

## Phase 4 – Content-Browser-Erweiterungen

> **Ziel:** Content Browser zu einem vollwertigen Asset-Management-Tool ausbauen.

### 4.1 Asset-Thumbnails
**Aktueller Stand:** Assets werden als farbige Quadrate mit Typ-Icon dargestellt. Keine Vorschaubilder.

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
- [ ] Texturen zeigen ihr Bild als Thumbnail
- [ ] 3D-Modelle zeigen einen kleinen Render
- [ ] Materialien zeigen eine Kugel mit dem Material
- [ ] Thumbnails werden gecacht und schnell geladen

### 4.2 Erweiterte Such- und Filterfunktion
**Aktueller Stand:** Kein Suchfeld im Content Browser.

**Ziel:** Echtzeit-Suche über alle Assets, Filter nach Typ.

**Herangehensweise:**
- Such-EntryBar oben im Content Browser (über Breadcrumbs)
- Echtzeit-Filter: Während der Eingabe werden nur matchende Assets angezeigt
- Suchmodus: Durchsucht ALLE Ordner (nicht nur den aktuellen), zeigt Ergebnisse als flache Liste
- Filter-Toggles: Buttons für jeden Asset-Typ (Mesh, Material, Texture, Script, Audio, Shader, Level, Widget)
- Kombinierbar: Suche + Typ-Filter gleichzeitig
- Keyboard-Shortcut: Ctrl+F fokussiert das Suchfeld

**Fortschrittsprüfung:**
- [ ] Suchfeld ist sichtbar im Content Browser
- [ ] Echtzeit-Filtern funktioniert
- [ ] Typ-Filter-Buttons reduzieren die Ergebnisse
- [ ] Ctrl+F fokussiert das Suchfeld

### 4.3 Drag & Drop Verbesserungen
**Aktueller Stand:** Drag & Drop von Assets auf den Viewport zum Spawnen existiert. Textur-Drag auf Entity-Details existiert.

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
- [ ] Material-Drop auf Entity im Viewport weist das Material zu
- [ ] Script-Drop auf Entity fügt ScriptComponent hinzu
- [ ] Dateien aus dem OS-Explorer können in den Content Browser gedroppt werden
- [ ] Visueller Ghost-Preview bei Drag

### 4.4 Asset-Referenz-Tracking
**Aktueller Stand:** Keine Information darüber, welche Assets ein Asset referenzieren oder von welchen Assets es referenziert wird.

**Ziel:** „Find References" und „Used By"-Ansichten für Assets.

**Herangehensweise:**
- Referenz-Graph: Beim Laden eines Levels alle Mesh→Material→Texture-Beziehungen aufbauen
- Content Browser Rechtsklick → „Find References" zeigt eine Liste aller Entities/Assets die dieses Asset nutzen
- Content Browser Rechtsklick → „Show Dependencies" zeigt alle Assets die dieses Asset referenziert
- Warnung beim Löschen: „This asset is used by {n} entities. Delete anyway?"
- Warnung als Icon im Grid-Tile: Unreferenzierte Assets werden dezent markiert

**Fortschrittsprüfung:**
- [ ] „Find References" zeigt korrekte Ergebnis-Liste
- [ ] Lösch-Warnung erscheint bei referenzierten Assets
- [ ] Unreferenzierte Assets sind visuell erkennbar

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
**Aktueller Stand:** Nur eine Entity gleichzeitig selektierbar.

**Ziel:** Mehrfachauswahl und Gruppenoperationen.

**Herangehensweise:**
- Ctrl+Klick: Entity zur Selektion hinzufügen
- Shift+Klick: Bereichsauswahl im Outliner
- Gizmo-Operations auf alle selektierten Entities gleichzeitig
- Rechtsklick-Kontextmenü für Gruppen: Delete All, Duplicate All, Group (Parent-Child)
- Rubber-Band-Selection: Rechteck im Viewport aufziehen um mehrere Entities zu selektieren
- Selektion in `std::unordered_set<uint32_t>` statt `uint32_t`

**Fortschrittsprüfung:**
- [ ] Ctrl+Klick fügt Entities zur Selektion hinzu
- [ ] Gizmo bewegt alle selektierten Entities gleichzeitig
- [ ] Rubber-Band-Selection funktioniert im Viewport
- [ ] Löschen entfernt alle selektierten Entities (mit Undo)

### 5.3 Entity Copy/Paste & Duplicate
**Aktueller Stand:** Kein Copy/Paste für Entities.

**Ziel:** Ctrl+C / Ctrl+V / Ctrl+D für Entities.

**Herangehensweise:**
- Ctrl+C: Entity-Snapshot (alle Komponenten) in einen internen Clipboard-Buffer
- Ctrl+V: Neuen Entity erstellen mit den Clipboard-Daten, leichter Position-Offset (+1,0,0)
- Ctrl+D: Duplicate = Copy + Paste in einem Schritt
- Undo/Redo-Integration: Jeder Paste/Duplicate wird als UndoRedo-Command registriert
- Multi-Select: Copy/Paste von mehreren Entities gleichzeitig

**Fortschrittsprüfung:**
- [ ] Ctrl+C + Ctrl+V dupliziert eine Entity
- [ ] Duplizierte Entity hat einen Positions-Offset
- [ ] Ctrl+D funktioniert als Shortcut
- [ ] Undo macht das Duplizieren rückgängig

### 5.4 Transform-Einrasten auf Oberflächen
**Aktueller Stand:** Kein Surface-Snapping.

**Ziel:** Entity auf die nächste Oberfläche „fallen lassen" per Button oder Shortcut.

**Herangehensweise:**
- „Drop to Surface"-Button in der Viewport-Toolbar (oder Shortcut: End-Taste)
- Raycast von der Entity-Position nach unten (-Y)
- Entity-Position.y = Trefferpunkt.y + Entity-AABB-Halbhöhe
- Funktioniert auch auf Landscapes / HeightFields
- Optional: Ausrichtung der Entity-Rotation an der Oberflächen-Normale

**Fortschrittsprüfung:**
- [ ] Entity wird korrekt auf der Oberfläche platziert
- [ ] Funktioniert auf flachen und schrägen Oberflächen
- [ ] Funktioniert auf Landscape-Terrain

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
**Aktueller Stand:** Einige Shortcuts sind hardcoded (W/E/R, Ctrl+Z/Y/S, F2, Delete, F8–F12).

**Ziel:** Zentrales, konfigurierbares Shortcut-System.

**Herangehensweise:**
- `ShortcutManager` Singleton mit Registry aller Aktionen
- Jede Aktion: Name, Default-Shortcut, Callback
- Konfigurations-UI in Editor Settings: Liste aller Shortcuts, klickbar zum Ändern
- Shortcut-Konflikte werden erkannt und gewarnt
- Export/Import als JSON
- In-App Shortcut-Hilfe (Ctrl+? oder F1 → Liste aller Shortcuts)

**Fortschrittsprüfung:**
- [ ] Alle bestehenden Shortcuts sind im ShortcutManager registriert
- [ ] Editor Settings zeigen die Shortcut-Liste
- [ ] Shortcuts können umbelegt werden
- [ ] F1 zeigt die Shortcut-Hilfe

### 6.3 Benachrichtigungs- und Fortschritts-System
**Aktueller Stand:** Toast-Benachrichtigungen und modale Progress-Dialoge existieren.

**Ziel:** Erweitertes Benachrichtigungssystem mit Verlauf und nicht-blockierendem Fortschritt.

**Herangehensweise:**
- Benachrichtigungs-Verlauf: Die letzten 50 Toasts werden in einer Liste gespeichert
- Klick auf ein Benachrichtigungs-Icon in der StatusBar → Benachrichtigungs-Panel klappt auf
- Nicht-blockierende Progress-Bars: Mehrere gleichzeitige Fortschrittsanzeigen in der StatusBar
- Priorisierung: Error-Benachrichtigungen bleiben länger sichtbar
- Sound-Feedback: Optionaler Ton bei Error-Benachrichtigungen

**Fortschrittsprüfung:**
- [ ] Benachrichtigungs-Verlauf ist abrufbar
- [ ] Mehrere Progress-Bars gleichzeitig möglich
- [ ] Error-Benachrichtigungen heben sich visuell ab

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
**Aktueller Stand:** Shader Hot-Reload existiert, aber kein Script Hot-Reload.

**Ziel:** Wenn eine .py-Datei extern geändert wird, wird sie automatisch neu geladen.

**Herangehensweise:**
- File-Watcher auf das `Content/`-Verzeichnis (analog zu `ShaderHotReload`)
- Bei `.py`-Dateiänderung: Script-Modul im Python-Interpreter neu laden
- Falls PIE läuft: Script-Entity-Funktionen werden mit neuer Version aufgerufen
- Toast-Benachrichtigung: „Script {name} reloaded"
- Falls Syntax-Error: Error-Toast statt Reload

**Fortschrittsprüfung:**
- [ ] Externe Script-Änderung wird erkannt
- [ ] Script wird im laufenden PIE neu geladen
- [ ] Syntax-Fehler werden als Toast angezeigt
- [ ] Alter Script-Zustand bleibt erhalten (soweit möglich)

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
**Aktueller Stand:** Focus-System existiert für Gameplay-UI (`ViewportUIManager`), aber nicht für Editor-UI.

**Ziel:** Grundlegende Keyboard-Navigation im Editor (Tab durch Felder, Enter zum Bestätigen).

**Herangehensweise:**
- Tab/Shift+Tab im Details-Panel: Springt zwischen EditBars
- Enter in Dialogen: Bestätigt den primären Button
- Escape: Schließt Modals/Dropdowns
- Pfeiltasten im Outliner: Entity-Auswahl navigieren
- Pfeiltasten im Content Browser: Grid-Navigation
- Fokus-Highlight: Subtiler blauer Rahmen um das fokussierte Element

**Fortschrittsprüfung:**
- [ ] Tab springt durch Details-Panel-Felder
- [ ] Enter bestätigt Dialoge
- [ ] Escape schließt Dropdowns
- [ ] Pfeiltasten navigieren den Outliner

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
- [ ] Wertänderung im Details-Panel ist undoable
- [ ] Komponenten-Hinzufügen ist undoable
- [ ] Material-Zuweisung per Drag & Drop ist undoable
- [ ] Kein Undo-Fall wurde vergessen

---

## Fortschrittsübersicht

| Phase | Beschreibung | Priorität | Aufwand | Status |
|-------|-------------|-----------|---------|--------|
| **1.1** | Abgerundete Panels | Mittel | Mittel | ✅ |
| **1.2** | Schatten & Tiefe | Niedrig | Mittel | ❌ |
| **1.3** | Modernisierte Icons | Hoch | Mittel | ❌ |
| **1.4** | Überarbeitetes Spacing | Hoch | Gering | ✅ |
| **1.5** | Animierte Übergänge | Niedrig | Mittel | ❌ |
| **1.6** | Scrollbar-Design | Niedrig | Gering | ❌ |
| **2.1** | Console / Log-Viewer | Hoch | Mittel | ✅ |
| **2.2** | Material Editor Tab | Hoch | Hoch | ❌ |
| **2.3** | Texture Viewer | Mittel | Gering | ❌ |
| **2.4** | Animation Editor | Mittel–Hoch | Hoch | ❌ |
| **2.5** | Particle Editor | Mittel | Mittel | ❌ |
| **2.6** | Audio Preview | Niedrig–Mittel | Gering | ❌ |
| **2.7** | Profiler Tab | Mittel | Mittel | ❌ |
| **3.1** | Auto-Material bei Import | Hoch | Gering | ❌ |
| **3.2** | Entity Templates / Prefabs | Hoch | Mittel | ❌ |
| **3.3** | One-Click Scene Setup | Mittel | Gering | ❌ |
| **3.4** | Auto-LOD-Generierung | Mittel | Hoch | ❌ |
| **3.5** | Auto-Collider-Generierung | Mittel | Gering | ❌ |
| **3.6** | Intelligent Snap & Grid | Hoch | Mittel | ❌ |
| **4.1** | Asset-Thumbnails | Hoch | Mittel | ❌ |
| **4.2** | Erweiterte Suche & Filter | Hoch | Gering | ❌ |
| **4.3** | Drag & Drop Verbesserungen | Mittel | Mittel | ❌ |
| **4.4** | Asset-Referenz-Tracking | Mittel | Mittel | ❌ |
| **5.1** | Viewport-Einstellungen Panel | Hoch | Gering | ✅ |
| **5.2** | Multi-Select & Group Ops | Hoch | Hoch | ❌ |
| **5.3** | Entity Copy/Paste | Hoch | Gering | ❌ |
| **5.4** | Transform-Einrasten | Mittel | Mittel | ❌ |
| **6.1** | Docking-System | Niedrig | Sehr hoch | ❌ |
| **6.2** | Keyboard-Shortcut-System | Mittel | Mittel | ❌ |
| **6.3** | Benachrichtigungs-System | Niedrig | Gering | ❌ |
| **7.1** | Integrierter Script-Editor | Mittel | Sehr hoch | ❌ |
| **7.2** | Debug-Breakpoints | Niedrig | Sehr hoch | ❌ |
| **7.3** | Script-Hot-Reload | Hoch | Mittel | ❌ |
| **8.1** | Tooltip-System | Hoch | Gering | ✅ |
| **8.2** | Onboarding-Wizard | Niedrig | Mittel | ❌ |
| **8.3** | Keyboard-Navigation | Mittel | Mittel | ❌ |
| **8.4** | Undo/Redo-Erweiterungen | Hoch | Mittel | ❌ |

### Empfohlene Reihenfolge (Sprints)

**Sprint 1 – Quick Wins (Visual Refresh):**
~~1.4 Spacing~~✅, 1.3 Icons, ~~8.1 Tooltips~~✅, ~~1.1 Rounded Panels~~✅

**Sprint 2 – Essenzielle Tabs:**
2.1 Console, 5.1 Viewport-Einstellungen, 4.2 Suche & Filter

**Sprint 3 – Automatisierung:**
3.1 Auto-Material, 3.5 Auto-Collider, 3.3 Scene Setup, 5.3 Copy/Paste

**Sprint 4 – Asset-Management:**
4.1 Thumbnails, 2.2 Material Editor Tab, 2.3 Texture Viewer

**Sprint 5 – Viewport-Workflow:**
3.6 Snap & Grid, 5.2 Multi-Select, 5.4 Surface-Snap

**Sprint 6 – Erweiterte Tabs:**
2.5 Particle Editor, 2.4 Animation Editor, 2.7 Profiler

**Sprint 7 – Scripting:**
7.3 Script Hot-Reload, 7.1 Script Editor, 2.6 Audio Preview

**Sprint 8 – Framework & Polish:**
6.2 Shortcut-System, 8.3 Keyboard-Navigation, 8.4 Undo/Redo, 3.2 Prefabs

**Sprint 9 – Advanced:**
3.4 Auto-LOD, 1.2 Elevation/Shadows, 1.5 Animations, 6.1 Docking

**Sprint 10 – Final Polish:**
6.3 Benachrichtigungs-System, 8.2 Onboarding, 1.6 Scrollbar, 4.3 Drag & Drop, 4.4 Referenz-Tracking, 7.2 Debug-Breakpoints
