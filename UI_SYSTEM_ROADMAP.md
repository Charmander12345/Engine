# UI-System Roadmap – Weg zu einem UMG-vollständigen System

> Stufenplan, um das bestehende UI-System (Widget Editor + ViewportUIManager + Python API)
> auf ein **Unreal-UMG-ähnliches Niveau** zu heben.
> Jede Phase ist in sich abgeschlossen und liefert eigenständigen Mehrwert.

---

## Inhaltsverzeichnis

1. [Aktueller Stand (Ist-Zustand)](#1-aktueller-stand)
2. [Ziel-Zustand (UMG-Parität)](#2-ziel-zustand)
3. [Lückenanalyse (Gap Analysis)](#3-lückenanalyse)
4. [Phase 1 – Layout-Fundament](#4-phase-1--layout-fundament)
5. [Phase 2 – Styling & Visual Polish](#5-phase-2--styling--visual-polish)
6. [Phase 3 – Animations-System](#6-phase-3--animations-system)
7. [Phase 4 – Erweiterte Widget-Typen](#7-phase-4--erweiterte-widget-typen)
8. [Phase 5 – Input, Navigation & Fokus](#8-phase-5--input-navigation--fokus)
9. [Phase 6 – Data Binding & Events](#9-phase-6--data-binding--events)
10. [Phase 7 – Rendering-Erweiterungen](#10-phase-7--rendering-erweiterungen)
11. [Phase 8 – Integration & Plattform](#11-phase-8--integration--plattform)
12. [Phase 9 – Editor-Tooling](#12-phase-9--editor-tooling)
13. [Priorisierung & Zeitschätzung](#13-priorisierung--zeitschätzung)
14. [Abhängigkeitsdiagramm](#14-abhängigkeitsdiagramm)

---

## 1. Aktueller Stand

### 1.1 Vorhandene Bausteine

| Bereich | Was existiert |
|---------|---------------|
| **Layout-Panels** | Canvas Panel, StackPanel (H/V), Grid, ScrollView |
| **Anchor-System** | 10-Werte-Enum (TopLeft…Stretch), Offset pro Element, `ResolveAnchorsRecursive` |
| **Hit-Testing** | `HitTestMode` (Enabled / DisabledSelf / DisabledAll), rekursive Tiefensuche |
| **Widget-Typen** | Panel, Text, Label, Button, Image, EntryBar, Slider, ProgressBar, CheckBox, DropDown, DropdownButton, TreeView, TabView, Separator, ScrollView, ToggleButton, RadioButton, ColorPicker |
| **Styling** | Per-Element Color, HoverColor, TextColor, FillColor, Border (Farbe/Dicke/Radius), Opacity, Gradient, Bold/Italic |
| **Widget-Assets** | JSON-basiert, Canvas-Root, Content-Browser-Integration |
| **Widget-Editor** | Tab-basiert, FBO-Preview, Drag-&-Drop-Palette, Hierarchie, Details-Panel, Zoom/Pan, Undo/Redo, Save |
| **Runtime-API** | `engine.ui.spawn_widget()` / `remove_widget()` / `show_cursor()` / `clear_all_widgets()` |
| **Viewport-UI** | ViewportUIManager, Viewport-Rect-Clipping, Multi-Widget, Z-Order |
| **Input-Routing** | 3-stufig (Editor → Viewport-UI → 3D), Hover/Pressed/Focused |

### 1.2 Architektur-Diagramm (aktuell)

```
Widget-Asset (.asset JSON)
    │
    ▼
┌──────────────┐    spawn_widget()    ┌─────────────────────┐
│ Widget Editor │ ──────────────────► │ ViewportUIManager   │
│ (Design-Time) │                     │ (Runtime-Rendering)  │
└──────────────┘                     └─────────────────────┘
    │ save                                │ render
    ▼                                     ▼
AssetManager                        OpenGLRenderer
    │                                (renderViewportUI)
    ▼
Content-Verzeichnis (.asset)
```

---

## 2. Ziel-Zustand

Ein UI-System, das die folgenden **UMG-Kernkonzepte** abdeckt:

1. **Deklaratives Layout** – Panels ordnen Kinder automatisch an, Anchor+Alignment kontrolliert Position
2. **Brush/9-Slice-Styling** – Jedes Widget nutzt Brushes (Solid, Image, 9-Slice, Material) statt roher Farben
3. **Widget-Animationen** – Keyframe-basierte Übergänge (Fade, Slide, Scale, Color) mit Easing
4. **Data Binding** – Properties an Datenquellen binden (Gesundheit → ProgressBar-Value, Spielername → Text)
5. **Render Transforms** – Per-Widget Translate/Rotate/Scale/Shear ohne Layout zu beeinflussen
6. **Focus & Navigation** – Gamepad/Keyboard-Navigation mit Focus-Chain und Tab-Order
7. **Widget-Komposition** – Slots, Named Slots, User Widgets (Prefabs mit exponierten Properties)
8. **Performance** – Virtualisierung, Invalidation Box, Retainer Box (Off-Screen-Caching)
9. **Rich Text** – Inline-Styles, eingebettete Bilder, Markup
10. **Lokalisierung** – Sprachschlüssel statt Klartext

---

## 3. Lückenanalyse

| Feature | UMG | Eigenes System | Aufwand |
|---------|-----|----------------|---------|
| Wrap Box Panel | ✅ | ❌ | Klein |
| Uniform Grid Panel | ✅ | ❌ | Klein |
| Size Box | ✅ | ❌ | Klein |
| Scale Box | ✅ | ❌ | Klein |
| Widget Switcher | ✅ | ❌ | Klein |
| Overlay Panel | ✅ | ❌ | Klein |
| 9-Slice / Brush-System | ✅ | ❌ | Mittel |
| Render Transforms | ✅ | ❌ | Mittel |
| Opacity-Vererbung | ✅ | ❌ | Klein |
| Clipping-Modi (Bounds/None/Inherit) | ✅ | ❌ | Mittel |
| Animations (Widget Animation) | ✅ | ❌ | Groß |
| Data Binding (Property Binding) | ✅ | ❌ | Groß |
| Rich Text Block | ✅ | ✅ | Mittel |
| Virtualisierte Listen (ListView/TileView) | ✅ | ❌ | Groß |
| Focus / Keyboard Navigation | ✅ | ✅ | Mittel |
| Gamepad-Navigation | ✅ | ❌ | Mittel |
| Named Slots / User Widgets | ✅ | 🟡 (Assets) | Mittel |
| Retainer Box (Off-Screen-Caching) | ✅ | ❌ | Mittel |
| Invalidation Box | ✅ | ❌ | Mittel |
| Safe Zone | ✅ | ❌ | Klein |
| 3D World Widget Component | ✅ | ❌ | Groß |
| Material als Brush | ✅ | ❌ | Mittel |
| Drag & Drop (Runtime, UI-intern) | ✅ | 🟡 (nur Editor) | Mittel |
| Lokalisierung (FText) | ✅ | ❌ | Mittel |
| Accessibility (Screen Reader) | ✅ | ❌ | Groß |
| Circular Throbber / Spinner | ✅ | ✅ | Klein |
| Editable Text (Multiline) | ✅ | ✅ | Mittel |
| Border Widget | ✅ | ✅ | Klein |

---

## 4. Phase 1 – Layout-Fundament

> **Ziel:** Alle Layout-Panels, die UMG bietet, verfügbar machen.
> **Voraussetzungen:** Keine – baut auf bestehendem StackPanel/Grid auf.

### 4.1 Wrap Box

**Was:** Container, der Kinder in eine Zeile legt und bei Platzmangel automatisch umbricht.

**Datenmodell:**
```cpp
// WidgetElementType::WrapBox
// WidgetElement-Felder:
float spacing;           // Abstand zwischen Kindern (bereits vorhanden)
StackOrientation orientation; // Horizontal → Zeile, Vertical → Spalte
```

**Layout-Algorithmus:**
1. Iteriere Kinder, summiere Breiten
2. Wenn `currentRowWidth + childWidth > availableWidth` → neue Zeile starten
3. Zeile Y-Offset = vorherige Zeile Y + max(childHeight in Zeile)

**Dateien:** `UIWidgets/WrapBoxWidget.h`, Layout in `UIManager.cpp` (measureElement/arrangeElement)

### 4.2 Uniform Grid Panel

**Was:** Grid, bei dem alle Zellen die gleiche Größe haben.

```cpp
// WidgetElementType::UniformGrid
int columns;   // Spaltenzahl (0 = automatisch)
int rows;      // Zeilenzahl (0 = automatisch)
```

**Layout:** `cellWidth = availableWidth / columns`, `cellHeight = availableHeight / rows`

### 4.3 Size Box

**Was:** Container für genau ein Kind, das Breite/Höhe-Constraints erzwingt.

```cpp
// WidgetElementType::SizeBox
float widthOverride;   // 0 = kein Override
float heightOverride;
float minDesiredWidth;
float maxDesiredWidth;
float minDesiredHeight;
float maxDesiredHeight;
```

### 4.4 Scale Box

**Was:** Container, der sein Kind auf die verfügbare Fläche skaliert (Contain, Cover, Fill, Custom).

```cpp
enum class ScaleMode { Contain, Cover, Fill, ScaleDown, UserSpecified };
// WidgetElementType::ScaleBox
ScaleMode scaleMode;
float userScale;   // nur bei UserSpecified
```

**Rendering:** `glm::scale` auf die Kinder-Ortho-Matrix anwenden.

### 4.5 Widget Switcher

**Was:** Container, der nur ein Kind gleichzeitig anzeigt (Index-basiert).

```cpp
// WidgetElementType::WidgetSwitcher
int activeChildIndex;
```

**Layout:** Nur `children[activeChildIndex]` wird gemessen und gezeichnet.

### 4.6 Overlay Panel

**Was:** Container, der alle Kinder übereinander stapelt (wie Canvas, aber mit Alignment).

```cpp
// WidgetElementType::Overlay
// Kinder werden übereinander gerendert, jedes mit eigenem H/V-Alignment.
```

### 4.7 Checkliste Phase 1

- [x] `WrapBox` – WidgetElementType, Layout, Editor-Palette, Serialisierung
- [x] `UniformGrid` – WidgetElementType, Layout, Editor-Palette, Serialisierung
- [x] `SizeBox` – WidgetElementType, Layout, Editor-Palette, Serialisierung
- [x] `ScaleBox` – WidgetElementType, Layout, Rendering, Editor-Palette, Serialisierung
- [x] `WidgetSwitcher` – WidgetElementType, Layout, Editor-Palette, Serialisierung
- [x] `Overlay` – WidgetElementType, Layout, Editor-Palette, Serialisierung
- [x] Details-Panel-Properties für alle neuen Typen
- [x] JSON-Serialisierung in `UIWidget.cpp` (readElement/writeElement)
- [x] `engine.pyi` aktualisieren
- [x] Dokumentation (`ENGINE_STATUS.md`, `PROJECT_OVERVIEW.md`)

---

## 5. Phase 2 – Styling & Visual Polish

> **Ziel:** Visuelles Styling von rohen RGBA-Werten auf ein Brush-basiertes System umstellen.
> **Voraussetzungen:** Phase 1 (optional, aber empfohlen).

### 5.1 Brush-System

**Konzept:** Ein `UIBrush` beschreibt, wie eine Fläche gefüllt wird.

```cpp
enum class BrushType { None, SolidColor, Image, NineSlice, LinearGradient, Material };

struct UIBrush
{
    BrushType type{ BrushType::SolidColor };
    Vec4 color{ 1,1,1,1 };
    Vec4 colorEnd{ 0,0,0,0 };       // für Gradient
    float gradientAngle{ 0.0f };     // 0=vertikal, 90=horizontal
    std::string imagePath;
    unsigned int textureId{ 0 };
    Vec4 imageMargin{ 0,0,0,0 };    // 9-Slice Ränder (L,T,R,B)
    Vec2 imageTiling{ 1,1 };
    std::string materialPath;        // für Material-Brush (Phase 7)
};
```

**Integration:**
- `WidgetElement::color` → `WidgetElement::background` (UIBrush)
- `WidgetElement::hoverColor` → `WidgetElement::hoverBrush` (UIBrush)
- `WidgetElement::fillColor` → `WidgetElement::fillBrush` (UIBrush)
- Rückwärtskompatibilität: Beim JSON-Laden alte `color`-Felder → SolidColor-Brush

### 5.2 9-Slice-Rendering

**Was:** Ein Bild wird in 9 Bereiche unterteilt (4 Ecken, 4 Kanten, 1 Mitte). Ecken bleiben fix, Kanten und Mitte werden gestreckt.

**Implementierung:**
1. Neuer Fragment-Shader `ui_nine_slice.frag`:
   - Uniform: `margin` (L,T,R,B in Pixeln), `textureSize`, `rectSize`
   - Berechnet UV-Mapping pro Fragment basierend auf Position relativ zu den Rändern
2. `drawUIBrush()` in `OpenGLRenderer` statt `drawUIPanel()` / `drawUIImage()`

### 5.3 Render Transforms

**Was:** Per-Element visuelle Transformation (Translate, Rotate, Scale, Shear) die das Layout nicht beeinflusst.

```cpp
struct RenderTransform
{
    Vec2 translation{ 0,0 };
    float rotation{ 0.0f };      // Grad
    Vec2 scale{ 1,1 };
    Vec2 shear{ 0,0 };
    Vec2 pivot{ 0.5f, 0.5f };   // Normalisiert (0,0)=oben-links, (0.5,0.5)=Mitte
};
```

**Integration:**
- `WidgetElement::renderTransform` (neues Feld)
- `toMatrix()` → 3×3 Transformationsmatrix, wird vor dem Element-Rendering auf die Ortho-Projektion multipliziert
- **Wichtig:** Nur visuell – `computedPositionPixels`/`computedSizePixels` bleiben unverändert
- Hit-Testing: Mausposition muss mit der inversen RenderTransform-Matrix transformiert werden

### 5.4 Opacity-Vererbung

**Was:** `opacity` eines Elternelements wird auf alle Kinder multipliziert.

```
effectiveOpacity = element.opacity * parent.effectiveOpacity
```

**Implementierung:** Vor dem Rendering wird `effectiveOpacity` rekursiv berechnet und als Uniform an den Shader übergeben.

### 5.5 Clipping-Modi

**Was:** Wie ein Element seinen Zeichenbereich beschneidet.

```cpp
enum class ClipMode { None, ClipToBounds, InheritFromParent };
// WidgetElement::clipMode
```

**Implementierung:** Verschachtelte `glScissor`-Aufrufe mit `glScissor`-Stack (Push/Pop):
1. `ClipToBounds` → neuen Scissor-Rect setzen (Schnittmenge mit aktuellem)
2. `None` → Scissor des Elternelements beibehalten
3. `InheritFromParent` → explizit den Scissor-Rect des Eltern übernehmen

### 5.6 Checkliste Phase 2

- [x] `UIBrush` Struct + Serialisierung
- [x] 9-Slice Fragment-Shader (Fallback auf Image-Stretch, dedizierter Shader als spätere Verfeinerung)
- [x] `drawUIBrush()` Renderer-Funktion
- [x] Migration bestehender `color`-Felder → Brush (mit Rückwärtskompatibilität)
- [x] `RenderTransform` Struct + Serialisierung
- [x] RenderTransform im Renderer anwenden (Visual + HitTest) – `ComputeRenderTransformMatrix()` in allen drei Render-Pfaden, RAII-Restore; `InverseTransformPoint()` in ViewportUIManager Hit-Testing
- [x] Opacity-Vererbung (rekursive Berechnung + Shader-Uniform)
- [x] `ClipMode` Enum + Scissor-Stack – RAII-basierter GL-Scissor-Stack in allen drei Render-Pfaden (verschachtelte ClipToBounds-Intersection)
- [x] Brush-Picker im Widget-Editor-Details-Panel
- [x] Dokumentation

---

## 6. Phase 3 – Animations-System

> **Ziel:** Keyframe-basierte Widget-Animationen wie UMGs Widget Animation Blueprints.
> **Voraussetzungen:** Phase 2 (Render Transforms, Opacity-Vererbung).

### 6.1 Architektur

```
WidgetAnimation
  ├─ name: string
  ├─ duration: float (Sekunden)
  ├─ isLooping: bool
  ├─ playbackSpeed: float
  └─ tracks: vector<AnimationTrack>
       ├─ targetElementId: string
       ├─ property: AnimatableProperty (enum)
       └─ keyframes: vector<Keyframe>
            ├─ time: float (0.0 – duration)
            ├─ value: float / Vec2 / Vec4
            └─ easing: EasingFunction (enum)
```

### 6.2 Animierbare Properties

```cpp
enum class AnimatableProperty
{
    // Transform
    RenderTranslationX, RenderTranslationY,
    RenderRotation,
    RenderScaleX, RenderScaleY,
    RenderShearX, RenderShearY,
    // Appearance
    Opacity,
    ColorR, ColorG, ColorB, ColorA,
    // Layout (vorsichtig – triggert Re-Layout)
    PositionX, PositionY,
    SizeX, SizeY,
    // Content
    FontSize
};
```

### 6.3 Easing-Funktionen

```cpp
enum class EasingFunction
{
    Linear,
    EaseInQuad, EaseOutQuad, EaseInOutQuad,
    EaseInCubic, EaseOutCubic, EaseInOutCubic,
    EaseInElastic, EaseOutElastic, EaseInOutElastic,
    EaseInBounce, EaseOutBounce, EaseInOutBounce,
    EaseInBack, EaseOutBack, EaseInOutBack,
    Custom  // Bezier-Kurve
};
```

### 6.4 Runtime-API

```cpp
class WidgetAnimationPlayer
{
public:
    void play(const std::string& animName, bool fromStart = true);
    void playReverse(const std::string& animName);
    void pause();
    void stop();
    void setSpeed(float speed);
    float getCurrentTime() const;
    bool isPlaying() const;
    void tick(float deltaTime);   // wird pro Frame aufgerufen
};
```

**Widget-Integration:**
```cpp
class Widget
{
    // ... bestehend ...
    std::vector<WidgetAnimation> animations;
    WidgetAnimationPlayer animPlayer;
};
```

### 6.5 Python API

```python
# engine.ui Erweiterung
engine.ui.play_animation(widget_id, "FadeIn")
engine.ui.stop_animation(widget_id, "FadeIn")
engine.ui.set_animation_speed(widget_id, "FadeIn", 2.0)
```

### 6.6 Editor-Integration (Spätere Verfeinerung)

- **Animations-Timeline** im Widget-Editor (wie UMGs Animation-Tab)
- Keyframe-Punkte auf einer Zeitachse platzieren
- Easing-Kurven visuell bearbeiten
- Live-Preview im FBO

### 6.7 Checkliste Phase 3

- [x] `WidgetAnimation`, `AnimationTrack`, `Keyframe` Datenstrukturen
- [x] `EasingFunction` – Implementierung aller Standard-Easing-Kurven
- [x] `WidgetAnimationPlayer` – play/pause/stop/tick/reverse
- [x] Tick-Integration in `ViewportUIManager::update()` / `OpenGLRenderer::render()`
- [x] Property-Application (Track-Wert → WidgetElement-Feld setzen)
- [x] JSON-Serialisierung (Animationen als Teil des Widget-Assets)
- [x] Python API (`play_animation`, `stop_animation`, `set_animation_speed`)
- [x] `engine.pyi` aktualisieren
- [x] Basis-Animations-Panel im Widget-Editor (Liste + Play/Stop)
- [x] Animations-Timeline-Panel im Widget-Editor (Unreal-Style Bottom-Dock mit Track-Management, Keyframe-Diamanten, Scrubber, Drag-&-Drop)
- [x] Redesigned Timeline-Panel: Links Animationsliste + Rechts Timeline mit Tree-View (aufklappbare Element-Header), kleinere Keyframe-Diamanten (9px), draggbare Scrubber-Linie (Echtzeit-Preview), draggbare End-Line (Dauer ändern), Echtzeit-Keyframe-Dragging
- [x] Dokumentation

---

## 7. Phase 4 – Erweiterte Widget-Typen

> **Ziel:** Fehlende Standardwidgets, die UMG bietet.
> **Voraussetzungen:** Phase 2 (Brush für Rich Text, 9-Slice).

### 7.1 Rich Text Block

**Was:** Text mit Inline-Formatierung (fett, kursiv, Farbe, Bilder).

**Markup-Syntax (vereinfacht):**
```
<b>Fett</b> <i>Kursiv</i> <color=#FF0000>Rot</color> <img src="icon.png" w=16 h=16/>
```

**Implementierung:**
1. Parser: Markup → Segment-Liste (`[{text, style, image?}, ...]`)
2. Zeilenumbruch-Algorithmus mit gemischten Segmenten
3. Rendering: Segment-weise Text-/Image-Zeichnung

**WidgetElementType:** `RichText`

### 7.2 Virtualisierte Liste (ListView / TileView)

**Was:** Rendert nur sichtbare Einträge einer großen Datenmenge.

```cpp
// WidgetElementType::ListView / TileView
struct VirtualListConfig
{
    int totalItemCount;
    float itemHeight;        // ListView: feste Höhe pro Eintrag
    float itemWidth;         // TileView: feste Breite pro Tile
    int columnsPerRow;       // TileView: Tiles pro Zeile
    std::function<void(int index, WidgetElement& itemTemplate)> onGenerateItem;
};
```

**Layout:**
1. Berechne sichtbaren Index-Bereich: `firstVisible = scrollOffset / itemHeight`, `count = viewHeight / itemHeight + 2`
2. Pool von `count` Item-Elementen recyceln
3. `onGenerateItem(index, element)` füllt Inhalt

### 7.3 Circular Throbber / Spinner

**Was:** Animiertes Lade-Symbol (rotierende Punkte oder Ring).

```cpp
// WidgetElementType::Spinner
int dotCount;           // Anzahl Punkte
float animationSpeed;   // Umdrehungen/Sekunde
```

**Rendering:** `N` Kreise auf einem Kreis verteilt, Opacity fällt ab.

### 7.4 Editable Text (Multiline)

**Was:** Erweiterung der bestehenden `EntryBar` um mehrzeilige Texteingabe.

```cpp
// Neues Feld in WidgetElement:
bool isMultiline{ false };
int maxLines{ 0 };  // 0 = unbegrenzt
```

### 7.5 Border Widget

**Was:** Ein dediziertes Container-Element, das ein Kind mit konfigurierbarem Rahmen umgibt (separate Brush für Background und Border).

```cpp
// WidgetElementType::Border
UIBrush backgroundBrush;
UIBrush borderBrush;
float borderThicknessLeft, borderThicknessTop, borderThicknessRight, borderThicknessBottom;
Vec2 contentPadding;
```

### 7.6 Checkliste Phase 4

- [x] Rich Text Block – Parser, Zeilenumbruch, Segment-Rendering
- [x] ListView – Virtualisierung, Scissor-Clipping, alternating Rows, Scroll-Offset
- [x] TileView – Grid-basierte Virtualisierung, columnsPerRow, itemWidth/itemHeight
- [x] Spinner – Rendering, Animation (dotCount, speed, elapsed tick)
- [x] Multiline EntryBar – Textumbruch, Cursor-Positionierung, Selection
- [x] Border Widget – Separate Border-Brush, per-Seite Dicke, contentPadding
- [x] Alle (Border/Spinner): WidgetElementType, Editor-Palette, Details-Panel, JSON-Serialisierung
- [x] Dokumentation (Border/Spinner): engine.pyi, ENGINE_STATUS.md, PROJECT_OVERVIEW.md
- [x] Multiline EntryBar: isMultiline, maxLines, mehrzeiliges Rendering, Editor-Details, EntryBarWidget.h, engine.pyi
- [x] ListView/TileView: Rendering (3 Pfade), Layout, Palette, Details-Panel, JSON, ListViewWidget.h, TileViewWidget.h, engine.pyi

---

## 8. Phase 5 – Input, Navigation & Fokus

> **Ziel:** Gamepad/Keyboard-Navigation wie in UMG.
> **Voraussetzungen:** Keine.

### 8.1 Focus-System

```cpp
struct FocusConfig
{
    bool isFocusable{ false };
    int tabIndex{ -1 };                  // -1 = automatisch
    std::string focusUp;                 // Element-ID für Hoch-Navigation
    std::string focusDown;
    std::string focusLeft;
    std::string focusRight;
};
// WidgetElement::focusConfig (neues Feld)
```

**Focus-Manager (in ViewportUIManager):**
```cpp
class FocusManager
{
    std::string m_focusedElementId;
    void moveFocus(Direction dir);        // Up/Down/Left/Right
    void setFocus(const std::string& id);
    void clearFocus();
    void tabToNext();
    void tabToPrevious();
};
```

### 8.2 Keyboard-Navigation

| Taste | Aktion |
|-------|--------|
| Tab | Nächstes fokussierbares Element |
| Shift+Tab | Vorheriges fokussierbares Element |
| Enter / Space | Aktivieren (Button-Click, CheckBox-Toggle) |
| Pfeiltasten | `focusUp/Down/Left/Right` oder Scrollen |
| Escape | Fokus verlassen / Menü schließen |

### 8.3 Gamepad-Mapping

```cpp
struct GamepadUIMapping
{
    // D-Pad / Left Stick → Navigation (moveFocus)
    // A / Cross → Bestätigen (activate)
    // B / Circle → Zurück (escape/back)
    // LB/RB → Tab-Navigation
};
```

### 8.4 Runtime Drag & Drop

**Was:** Elemente innerhalb der Gameplay-UI per Maus/Touch ziehen.

```cpp
struct DragDropOperation
{
    std::string sourceElementId;
    std::string payload;           // Frei definierbarer String
    WidgetElement* dragVisual;     // Visuelles Feedback während Drag
};

// WidgetElement-Erweiterungen:
bool acceptsDrop{ false };
std::function<bool(const DragDropOperation&)> onDragOver;
std::function<void(const DragDropOperation&)> onDrop;
std::function<void()> onDragStart;
```

### 8.5 Checkliste Phase 5

- [x] `FocusConfig` Struct + FocusManager-Klasse
- [x] Tab/Shift+Tab-Navigation
- [x] Pfeiltasten-Navigation (explizite Richtung oder Auto-Spatial)
- [x] Enter/Space-Aktivierung
- [x] Visuelles Fokus-Feedback (Focus-Brush / Outline)
- [x] Gamepad-Input-Adapter (SDL GameController → FocusManager)
- [x] Runtime Drag & Drop (DragDropOperation, Callbacks)
- [x] Python API: `set_focusable()`, `set_focus()`, `clear_focus()`, `get_focused_element()`
- [x] `engine.pyi` aktualisieren
- [x] Dokumentation

---

## 9. Phase 6 – Data Binding & Events

> **Ziel:** UI-Properties an Datenquellen binden, wie UMGs Property Binding.
> **Voraussetzungen:** Keine.

### 9.1 Binding-Architektur

```
Datenquelle (z.B. Python-Variable, ECS-Component)
    │
    ▼
┌──────────────┐     update()     ┌────────────────┐
│ BindingProxy │ ───────────────► │ WidgetElement   │
│  (Polling)   │                  │  .property      │
└──────────────┘                  └────────────────┘
```

**Zwei Ansätze (pragmatisch):**

1. **Polling (einfach):** Pro Frame die Binding-Expression evaluieren und Property setzen
2. **Observable (fortgeschritten):** Änderungs-Notifications, nur bei tatsächlicher Wertänderung aktualisieren

### 9.2 Binding-Definition

```cpp
struct PropertyBinding
{
    std::string targetProperty;   // z.B. "text", "value", "opacity", "color.r"
    std::string expression;       // z.B. "player.health / player.max_health"
    std::string sourceType;       // "python", "ecs_component", "global"
};

// WidgetElement:
std::vector<PropertyBinding> bindings;
```

### 9.3 Python-seitige Bindings

```python
# Option A: Explizites Binding
engine.ui.bind_property(widget_id, "health_bar", "value", lambda: player.hp / player.max_hp)

# Option B: Deklarativ im Widget-Asset (JSON)
# "bindings": [{ "property": "value", "expression": "player_health_percent" }]
```

### 9.4 Event-System

```cpp
// Erweiterte Event-Callbacks
struct UIEvent
{
    std::string type;            // "clicked", "hovered", "value_changed", "focus_gained"
    std::string elementId;
    std::string widgetId;
    // event-spezifische Daten
};

// Global Event Bus
using UIEventHandler = std::function<void(const UIEvent&)>;
void registerUIEventHandler(const std::string& eventType, UIEventHandler handler);
```

### 9.5 Checkliste Phase 6

- [ ] `PropertyBinding` Struct + Serialisierung
- [ ] Binding-Evaluator (Polling: Python-Callback pro Frame aufrufen)
- [ ] Binding-Integration in `ViewportUIManager::update()`
- [ ] Python API: `bind_property()`, `unbind_property()`
- [ ] Event-System (`UIEvent`, `registerUIEventHandler`)
- [ ] Python API: `on_event()` / `register_event_handler()`
- [ ] Binding-Editor im Widget-Editor-Details-Panel (Property → Expression)
- [ ] `engine.pyi` aktualisieren
- [ ] Dokumentation

---

## 10. Phase 7 – Rendering-Erweiterungen

> **Ziel:** Performance-Optimierung und erweiterte visuelle Möglichkeiten.
> **Voraussetzungen:** Phase 2 (Brush-System), Phase 3 (Animationen).

### 10.1 Retainer Box (Off-Screen-Caching)

**Was:** Rendert den Inhalt in einen FBO und blittet nur die Textur, solange sich nichts ändert.

```cpp
// WidgetElementType::RetainerBox
int renderPhase;        // 0 = jedes Frame, 3 = alle 3 Frames, etc.
```

**Implementierung:** Eigenes FBO pro RetainerBox, `dirty`-Flag, nur bei Änderung neu rendern.

### 10.2 Invalidation Box

**Was:** Ähnlich RetainerBox, aber feiner granular – nur bei expliziter Invalidierung.

### 10.3 Material als Brush

**Was:** Ein OpenGL-Material (Shader + Uniforms) als Brush verwenden (z.B. animierte Hintergründe).

```cpp
// BrushType::Material
// Verwendet den Material-Asset-Pfad, kompiliert den Shader, setzt Uniforms (Time, Resolution, etc.)
```

### 10.4 3D World Widget Component

**Was:** Ein Widget im 3D-Weltkoordinatensystem rendern (z.B. Namenschilder über NPCs).

```cpp
// Neue ECS-Komponente: WorldWidgetComponent
struct WorldWidgetComponent
{
    std::string widgetAssetPath;
    Vec2 drawSize;              // Größe in Welt-Einheiten
    bool isTwoSided;
    bool isScreenFacing;        // Billboard
    float maxRenderDistance;
};
```

**Rendering:**
1. Widget in FBO rendern (wie Widget-Editor-Preview)
2. FBO-Textur als Quad in der 3D-Szene zeichnen (Billboard oder fixe Orientierung)

### 10.5 Safe Zone

**Was:** Padding, das sich an die sichere Anzeigebereich-Größe des Geräts anpasst (TV-Overscan, Notch).

```cpp
// WidgetElementType::SafeZone
// Liest Plattform-spezifische Safe-Area-Insets und wendet sie als Padding an
```

### 10.6 Checkliste Phase 7

- [ ] RetainerBox – FBO pro Box, Dirty-Tracking, Blit
- [ ] InvalidationBox – Feingranulare Invalidierung
- [ ] Material-Brush – Shader-Kompilierung, Time/Resolution-Uniforms
- [ ] WorldWidgetComponent – ECS-Komponente, FBO-Rendering, Billboard
- [ ] SafeZone – Plattform-Insets, SDL_GetDisplayUsableBounds
- [ ] Dokumentation

---

## 11. Phase 8 – Integration & Plattform

> **Ziel:** Lokalisierung, Accessibility, plattformübergreifende Features.
> **Voraussetzungen:** Phase 4 (Rich Text), Phase 5 (Focus-System).

### 11.1 Lokalisierung

**Konzept:** Text wird über Schlüssel referenziert, nicht über Klartext.

```cpp
struct LocalizedText
{
    std::string key;           // z.B. "UI_HEALTH_LABEL"
    std::string fallback;      // "Health" (falls Schlüssel nicht gefunden)
};

// Sprachdateien: Content/Localization/de.json, en.json, fr.json, ...
// { "UI_HEALTH_LABEL": "Gesundheit", "UI_SAVE": "Speichern", ... }
```

**Integration:**
- `WidgetElement::text` → kann ein `$KEY`-Prefix enthalten → wird beim Rendering aufgelöst
- `LocalizationManager` Singleton: `getString(key, locale)` mit Fallback-Kette

### 11.2 Accessibility

```cpp
struct AccessibilityInfo
{
    std::string label;         // Screen-Reader-Text
    std::string description;   // Erweiterte Beschreibung
    std::string role;          // "button", "slider", "text", "heading"
    bool isAccessible{ true };
};
// WidgetElement::accessibility (neues Feld)
```

**Umsetzung (Stufenweise):**
1. Semantische Rollen für alle Widgets definieren
2. Screen-Reader-Output über Plattform-API (Windows: UI Automation, macOS: NSAccessibility)
3. High-Contrast-Modus (alternative Brush-Sets)

### 11.3 Named Slots / User Widgets

**Was:** Widget-Assets mit exponierten "Slots", die zur Laufzeit mit Inhalt gefüllt werden können.

```cpp
// Im Widget-Asset:
// Ein Element mit type=Slot und slotName="Content"
// WidgetElementType::Slot
std::string slotName;

// Runtime:
void setSlotContent(const std::string& widgetId, const std::string& slotName, const WidgetElement& content);
```

**Anwendung:** Wiederverwendbare UI-Prefabs (z.B. "DialogBox" mit Slot für "Content" und "Buttons").

### 11.4 Checkliste Phase 8

- [ ] `LocalizationManager` – JSON-Sprachdateien laden, getString, Fallback
- [ ] Text-Rendering mit `$KEY`-Auflösung
- [ ] Sprach-Umschaltung zur Laufzeit
- [ ] `AccessibilityInfo` Struct
- [ ] Screen-Reader-Integration (Windows UI Automation)
- [ ] High-Contrast-Modus
- [ ] `Slot` WidgetElementType + `setSlotContent()`
- [ ] Python API: `set_locale()`, `set_slot_content()`
- [ ] Dokumentation

---

## 12. Phase 9 – Editor-Tooling

> **Ziel:** Widget-Editor auf UMG-Designer-Niveau bringen.
> **Voraussetzungen:** Alle vorherigen Phasen (nutzt deren Features).

### 12.1 Animations-Timeline

- ✅ Horizontale Zeitachse im Widget-Editor (Bottom-Dock-Panel, 250px)
- ✅ Pro Track eine Zeile (Element + Property) mit Add/Remove-Buttons
- ✅ Keyframes als Diamant-Punkte (◆), per Drag-&-Drop verschiebbar
- ✅ Scrubber zum Vorspulen (Klick auf Ruler setzt Position)
- ✅ Play/Stop-Controls + Animations-Selektor + New/Delete
- [ ] Easing-Kurven visuell als Verbindungslinien
- [ ] Live-Preview im FBO bei Scrubber-Bewegung

### 12.2 Binding-Editor

- Property-Dropdown → Binding-Expression-Feld
- Auto-Complete für bekannte Python-Variablen
- Live-Preview des gebundenen Werts

### 12.3 Brush-Picker

- Visueller Brush-Editor statt roher RGBA-Eingabe
- Typ-Selector (Solid / Image / 9-Slice / Gradient / Material)
- Image-Browser mit Asset-Auswahl
- 9-Slice-Margin-Editor mit Preview
- Gradient-Editor mit Farbstops und Winkel

### 12.4 Responsive-Preview

- Preview in verschiedenen Auflösungen (1920×1080, 1280×720, 800×600, 2560×1440)
- Anchor-Verhalten live beobachten
- Portrait/Landscape-Umschaltung

### 12.5 Widget-Templates

- Vorgefertigte Widget-Vorlagen (HUD, Dialog, Inventory, Menu)
- Erstelle neues Widget → Template-Auswahl
- Templates als spezielle Assets im Editor-Content-Verzeichnis

### 12.6 Copy/Paste

- Ctrl+C / Ctrl+V für Elemente im Widget-Editor
- Element wird als JSON in die Zwischenablage kopiert
- Paste fügt als Geschwisterelement ein

### 12.7 Multi-Selektion

- Shift+Klick / Ctrl+Klick für mehrere Elemente
- Gruppierte Property-Bearbeitung (gemeinsame Werte)
- Gruppen-Move / -Resize

### 12.8 Checkliste Phase 9

- [ ] Animations-Timeline (Zeitachse, Tracks, Keyframes, Scrubber)
- [ ] Binding-Editor (Expression-Feld, Auto-Complete)
- [ ] Brush-Picker (Typ-Auswahl, 9-Slice-Editor, Gradient-Editor)
- [ ] Responsive-Preview (Auflösungs-Switcher)
- [ ] Widget-Templates (Template-Assets, Auswahldialog)
- [ ] Copy/Paste (JSON-Zwischenablage)
- [ ] Multi-Selektion (Shift/Ctrl, Gruppen-Properties)
- [ ] Dokumentation

---

## 13. Priorisierung & Zeitschätzung

### 13.1 Prioritätsmatrix

| Phase | Priorität | Aufwand | Nutzen | Empfohlene Reihenfolge |
|-------|-----------|---------|--------|------------------------|
| 1. Layout-Fundament | ⭐⭐⭐ Hoch | Klein | Hoch | 1. |
| 2. Styling & Visual | ⭐⭐⭐ Hoch | Mittel | Hoch | 2. |
| 5. Input & Navigation | ⭐⭐⭐ Hoch | Mittel | Hoch | 3. |
| 3. Animations | ⭐⭐ Mittel | Groß | Hoch | 4. |
| 6. Data Binding | ⭐⭐ Mittel | Mittel | Hoch | 5. |
| 4. Erweiterte Widgets | ⭐⭐ Mittel | Mittel | Mittel | 6. |
| 7. Rendering-Erw. | ⭐ Niedrig | Groß | Mittel | 7. |
| 8. Integration | ⭐ Niedrig | Groß | Mittel | 8. |
| 9. Editor-Tooling | ⭐ Niedrig | Groß | Hoch | 9. (fortlaufend) |

### 13.2 Zeitschätzung (grob)

| Phase | Geschätzter Aufwand |
|-------|---------------------|
| Phase 1 – Layout-Fundament | 1–2 Wochen |
| Phase 2 – Styling & Visual | 2–3 Wochen |
| Phase 3 – Animations | 3–4 Wochen |
| Phase 4 – Erweiterte Widgets | 2–3 Wochen |
| Phase 5 – Input & Navigation | 2–3 Wochen |
| Phase 6 – Data Binding | 2–3 Wochen |
| Phase 7 – Rendering-Erw. | 3–4 Wochen |
| Phase 8 – Integration | 4–6 Wochen |
| Phase 9 – Editor-Tooling | 4–6 Wochen (fortlaufend) |
| **Gesamt** | **~23–34 Wochen** |

### 13.3 Empfohlener Startpfad (Minimal Viable UMG)

Für ein **spielbares UI-System** mit UMG-Feeling reichen die ersten 6 Phasen:

```
Phase 1 (Layout) → Phase 2 (Styling) → Phase 5 (Input) → Phase 3 (Animations)
                                                           → Phase 6 (Binding)
                                                           → Phase 4 (Widgets)
```

Das ergibt nach **~12–18 Wochen** ein UI-System mit:
- ✅ Alle Standard-Layout-Panels
- ✅ Brush-basiertes Styling mit 9-Slice
- ✅ Gamepad/Keyboard-Navigation
- ✅ Widget-Animationen (Fade, Slide, Scale)
- ✅ Data Binding (Gesundheit, Score, etc.)
- ✅ Rich Text und virtualisierte Listen

---

## 14. Abhängigkeitsdiagramm

```
Phase 1 (Layout) ─────────────────────────────────────────────────┐
    │                                                              │
    ▼                                                              │
Phase 2 (Styling) ─────────────────┐                              │
    │                               │                              │
    ├──► Phase 3 (Animations)       │                              │
    │        │                      │                              │
    │        ▼                      ▼                              │
    │   Phase 7 (Rendering) ◄── Phase 4 (Widgets)                 │
    │        │                      │                              │
    │        ▼                      ▼                              │
    │   Phase 8 (Integration) ◄── Phase 5 (Input/Navigation)      │
    │                               │                              │
    │                               ▼                              │
    │                          Phase 6 (Data Binding)              │
    │                                                              │
    └──────────────────────── Phase 9 (Editor) ◄───────────────────┘
                               (fortlaufend, nutzt alles)
```

**Legende:**
- `→` / `▼` = "hängt ab von"
- Phase 1 ist Voraussetzung für fast alles
- Phase 9 läuft parallel und integriert Features jeder abgeschlossenen Phase

---

## Zusammenfassung

Dieses System baut **inkrementell** auf dem bereits soliden Fundament (Widget-Editor, ViewportUIManager, Anchor-System, HitTestMode, 18+ Widget-Typen) auf. Jede Phase liefert eigenständigen Wert und kann unabhängig abgeschlossen werden. Der empfohlene Startpfad (Phasen 1–6) bringt das System in **~3–4 Monaten** auf ein Niveau, das mit den meisten UMG-Features vergleichbar ist. Die späteren Phasen (7–9) fügen Enterprise-Grade-Features hinzu (Lokalisierung, Accessibility, 3D-Widgets, fortgeschrittenes Editor-Tooling).
