# Gameplay UI System – Gesamtplan

> Konsolidierter Plan für das **Gameplay-UI-System** (ViewportUIManager + Python Scripting API) und den **UI Designer Tab** (Editor-Integration). Kombiniert und ersetzt die früheren Einzelpläne `VIEWPORT_UI_SYSTEM_PLAN.md` und `WIDGET_EDITOR_UX_PLAN.md`.

---

## Inhaltsverzeichnis

1. [Ziel & Abgrenzung](#1-ziel--abgrenzung)
2. [Architektur-Übersicht](#2-architektur-übersicht)
3. [Implementierungsstatus](#3-implementierungsstatus)
4. [ViewportUIManager – Runtime-System](#4-viewportuimanager--runtime-system)
5. [Anchor-System](#5-anchor-system)
6. [Rendering-Pipeline](#6-rendering-pipeline)
7. [Input-Routing](#7-input-routing)
8. [Python Scripting API (engine.viewport_ui)](#8-python-scripting-api-engineviewport_ui)
9. [Cursor-Steuerung & Kamera-Blockade](#9-cursor-steuerung--kamera-blockade)
10. [UI Designer Tab – Editor-Integration](#10-ui-designer-tab--editor-integration)
11. [Designer UX-Verbesserungen (Roadmap)](#11-designer-ux-verbesserungen-roadmap)
12. [Schritt-für-Schritt Implementierungsplan](#12-schritt-für-schritt-implementierungsplan)
13. [Datei-Übersicht](#13-datei-übersicht)

---

## 1. Ziel & Abgrenzung

### 1.1 Ziel
Ein **Gameplay-UI-System**, das:
- Widgets **ausschließlich innerhalb des Viewport-Bereichs** rendert (nicht über Editor-Panels hinweg)
- Die **Viewport-Größe** als verfügbare Fläche nutzt (nach Abzug von TitleBar, StatusBar, Outliner, ContentBrowser)
- **Vollständig unabhängig** vom Editor-UI-System (`UIManager`) arbeitet
- **Rein dynamisch per Python-Script** zur Laufzeit erstellt wird (keine Assets, kein Level-Bezug)
- Über einen **Editor-Tab** (UI Designer) zur Laufzeit inspiziert und bearbeitet werden kann
- Im **PIE-Modus** als Spieler-UI mit eigener Cursor-Steuerung funktioniert

### 1.2 Abgrenzung

| Aspekt | Editor-UI (bestehend) | Gameplay-UI (dieses System) |
|---|---|---|
| **Manager** | `UIManager` in OpenGLRenderer | `ViewportUIManager` in OpenGLRenderer |
| **Koordinatensystem** | Fenster-Pixel (0,0 = oben-links Fenster) | Viewport-Pixel (0,0 = oben-links Viewport-Rect) |
| **Verfügbare Größe** | Gesamte Fenstergröße | Viewport-Content-Rect |
| **Rendering** | Nach World-Blit, auf Default-FBO | In den Tab-FBO, nach World-Rendering, vor Blit |
| **Input** | Direkt über SDL-Events | Über Editor → Viewport-Weiterleitung (nur im Viewport-Rect) |
| **Serialisierung** | `.asset`-Dateien (Widget-Typ) | **Keine** — rein dynamisch per Script |
| **Persistenz** | Dauerhaft in Content | Nur während PIE; wird beim PIE-Stop zerstört |

### 1.3 Abgrenzung zum Widget Editor
Der bestehende **Widget Editor** (Tab-basiert, mit FBO-Preview) bearbeitet **Widget-Assets** (`.asset`-Dateien). Der neue **UI Designer Tab** operiert stattdessen auf dem **ViewportUIManager** (Runtime-Daten im Viewport). Der Viewport selbst dient als Live-Preview.

---

## 2. Architektur-Übersicht

```
┌────────────────────────────────────────────────────────┐
│                    Fenster (SDL)                        │
│  ┌─────────────────────────────────────────────────┐   │
│  │         TitleBar (Editor-UI)  [Tabs]            │   │
│  ├────────┬────────────────────────────┬───────────┤   │
│  │Outliner│      VIEWPORT              │           │   │
│  │        │  ┌──────────────────────┐  │           │   │
│  │Entity  │  │   3D-Szene (World)   │  │           │   │
│  │Details │  │                      │  │           │   │
│  │        │  │  ┌────────────────┐  │  │           │   │
│  │        │  │  │ GAMEPLAY-UI    │  │  │           │   │
│  │        │  │  │ (HUD, Buttons, │  │  │           │   │
│  │        │  │  │  Health Bar..) │  │  │           │   │
│  │        │  │  └────────────────┘  │  │           │   │
│  │        │  └──────────────────────┘  │           │   │
│  ├────────┴────────────────────────────┴───────────┤   │
│  │           ContentBrowser (Editor-UI)             │   │
│  ├─────────────────────────────────────────────────┤   │
│  │              StatusBar (Editor-UI)               │   │
│  └─────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────┘
```

**UI Designer Tab** (wie MeshViewer — eigener Tab in der TitleBar):
```
┌─────────────────────────────────────────────────────────────────────┐
│  [Viewport] [UI Designer] [Widget: myHUD.asset]                     │  Tab-Leiste
├──────────────┬──────────────────────────────────┬───────────────────┤
│  Controls    │                                  │  Properties       │
│  ──────────  │      Viewport (Live-Preview)     │  ────────────     │
│  📋 Panel    │      des Gameplay-UI              │  ID: [health_bg] │
│  📝 Text     │                                  │  Type: Panel      │
│  🏷 Label    │  ┌───────────────────────────┐   │  Anchor: TopLeft  │
│  🔘 Button   │  │  Health Bar   [FPS: 60]   │   │  Pos X: [20    ] │
│  🖼 Image    │  │                           │   │  Pos Y: [20    ] │
│  ▐ Slider    │  │              ┌──────────┐ │   │  W: [300   ]     │
│  ▮ Progress  │  │              │ Minimap  │ │   │  H: [40    ]     │
│  ──────────  │  │              └──────────┘ │   │  Color: [■ RGBA] │
│  Hierarchy   │  └───────────────────────────┘   │  Opacity: [0.8 ] │
│  ──────────  │                                  │  Visible: ☑       │
│  ▾ HUD (z=0) │                                  │                   │
│    ├ health_bg│                                  │                   │
│    ├ fps_text │                                  │                   │
│    └ minimap  │                                  │                   │
│  ▸ Menu(z=10)│                                  │                   │
├──────────────┴──────────────────────────────────┴───────────────────┤
│  Status: 2 Widgets, 6 Elements                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.1 Kernkomponenten

| Komponente | Beschreibung | Status |
|---|---|---|
| **ViewportUIManager** | Multi-Widget-Manager mit Canvas Panel, Anchor-System, Z-Order, Cursor-Flag | ✅ Implementiert |
| **renderViewportUI()** | Rendering in Tab-FBO mit Ortho-Offset + Scissor-Clip, ProgressBar/Slider | ✅ Implementiert |
| **engine.viewport_ui** | Python-Modul mit 28 Methoden (create/remove/add_*/set_*/show_cursor) | ✅ Implementiert |
| **engine.pyi** | IntelliSense-Stubs für alle 28 viewport_ui-Methoden | ✅ Implementiert |
| **UI Designer Tab** | Editor-Tab mit Hierarchie, Properties, Control-Palette, Sync | ✅ Implementiert |

---

## 3. Implementierungsstatus

### 3.1 Gesamtfortschritt

| Bereich | Status | Details |
|---|---|---|
| ViewportUIManager (Multi-Widget) | ✅ Fertig | `vector<WidgetEntry>`, createWidget/removeWidget/clearAll, sortWidgetsIfNeeded |
| WidgetAnchor Enum (10 Werte) | ✅ Fertig | TopLeft/TopRight/BottomLeft/BottomRight/Top/Bottom/Left/Right/Center/Stretch |
| Element-Anchor + AnchorOffset | ✅ Fertig | `WidgetElement::anchor`, `WidgetElement::anchorOffset`, `computeAnchorPivot()` |
| Anchor-Layout-Resolution | ✅ Fertig | `ResolveAnchorsRecursive()` in `updateLayout()` |
| Implicit Canvas Panel | ✅ Fertig | `createWidget()` erstellt Panel mit alpha=0, fillX/fillY |
| Multi-Widget Z-Order Rendering | ✅ Fertig | `getSortedWidgets()` + renderViewportUI-Loop |
| Multi-Widget Hit Testing | ✅ Fertig | Reverse-Z-Order-Iteration in hitTest/hitTestConst |
| ProgressBar/Slider Rendering | ✅ Fertig | Background + Fill/Track + Thumb in renderViewportUI |
| Gameplay Cursor Flag | ✅ Fertig | `m_gameplayCursorVisible`, SDL Show/Hide, Relative Mouse Mode |
| Kamera-Blockade bei Cursor | ✅ Fertig | `cursorBlocksCamera` in main.cpp Camera-Rotation-Guard |
| Python API (28 Methoden) | ✅ Fertig | parseAnchorString, getCanvasChildren, alle add_*/set_* |
| engine.pyi Stubs | ✅ Fertig | 28 Methoden mit korrekten Signaturen |
| PIE Auto-Cleanup | ✅ Fertig | `clearAllWidgets()` beim PIE-Stop in main.cpp |
| Input-Routing (3-stufig) | ✅ Fertig | Editor-UI → Viewport-UI → 3D-Interaktion |
| UI Designer Tab | ✅ Fertig | openUIDesignerTab(), Hierarchie, Properties, Sync, Highlight |
| Dokumentation | ✅ Fertig | PROJECT_OVERVIEW.md + ENGINE_STATUS.md aktualisiert |

### 3.2 Was funktioniert (Runtime)

1. **ViewportUIManager** — Vollständiger Multi-Widget-Manager mit `createWidget(name, zOrder)`, `removeWidget(name)`, `clearAllWidgets()`, Canvas Panel pro Widget, Z-Order-Sortierung
2. **Anchor-System** — 10-Werte-Enum, per-Element Anchor + Offset, `computeAnchorPivot()` berechnet Viewport-relative Pivots, `ResolveAnchorsRecursive()` setzt computedPositionPixels/computedSizePixels
3. **Rendering** — `renderViewportUI()` iteriert über getSortedWidgets(), rendert Panel/Text/Button/Image/ProgressBar/Slider, transparente Canvas-Panels werden übersprungen, isVisible-Check
4. **Input** — 3-stufige Priorität (Editor → Viewport-UI → 3D), Hover/Pressed-State-Tracking, reverse Z-Order Hit Testing
5. **Python API** — 28 Methoden in `engine.viewport_ui`: Widget-Lifecycle (create/remove/clear), Element-Erstellung (add_text/label/button/panel/image/progress_bar/slider mit widget_name + anchor), Property-Setter (set_text/color/text_color/opacity/visible/border/border_radius/tooltip/font_size/font_bold/font_italic/on_clicked/value/anchor/position/size), Cursor-Steuerung (show_cursor)
6. **Cursor-System** — `show_cursor(True/False)` steuert SDL Cursor + Relative Mouse Mode + Window Mouse Grab; bei sichtbarem Cursor wird Camera-Rotation in main.cpp automatisch blockiert

---

## 4. ViewportUIManager – Runtime-System

### 4.1 Klasse (aktueller Stand)

**Datei:** `src/Renderer/ViewportUIManager.h`

```cpp
class ViewportUIManager
{
public:
    // Viewport-Rect (Pixel-Koordinaten relativ zum Fenster)
    void setViewportRect(float x, float y, float width, float height);
    Vec4 getViewportRect() const;
    Vec2 getViewportSize() const;

    // Multi-Widget-API
    bool createWidget(const std::string& name, int zOrder = 0);
    bool removeWidget(const std::string& name);
    Widget* getWidget(const std::string& name);
    void clearAllWidgets();
    bool hasWidgets() const;

    struct WidgetEntry { std::string name; std::shared_ptr<Widget> widget; };
    const std::vector<WidgetEntry>& getSortedWidgets() const;

    // Element-Zugriff (sucht über alle Widgets)
    WidgetElement* findElementById(const std::string& elementId);
    WidgetElement* findElementById(const std::string& widgetName, const std::string& elementId);

    // Layout
    void updateLayout(const std::function<Vec2(const std::string&, float)>& measureText);
    bool needsLayoutUpdate() const;
    void markLayoutDirty();

    // Input (Fenster-Pixel → intern viewport-lokal)
    bool handleMouseDown(const Vec2& windowPos, int button);
    bool handleMouseUp(const Vec2& windowPos, int button);
    bool handleScroll(const Vec2& windowPos, float delta);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key);
    void setMousePosition(const Vec2& windowPos);
    bool isPointerOverViewportUI(const Vec2& windowPos) const;

    // Selektion (für Designer)
    void setSelectedElementId(const std::string& elementId);
    const std::string& getSelectedElementId() const;
    void setOnSelectionChanged(SelectionChangedCallback callback);

    // Sichtbarkeit & Cursor
    void setVisible(bool visible);
    bool isVisible() const;
    void setGameplayCursorVisible(bool visible);
    bool isGameplayCursorVisible() const;

    // Debug
    nlohmann::json toJson() const;
    bool loadFromJson(const nlohmann::json& data);

private:
    Vec4 m_viewportRect{};
    std::vector<WidgetEntry> m_widgets;       // sortiert nach z_order
    bool m_widgetOrderDirty{ false };
    bool m_gameplayCursorVisible{ false };
    // ... weitere private Members
};
```

### 4.2 Multi-Widget-Modell

Jedes Widget enthält ein **implizites Canvas Panel** als Wurzelelement:

```
Widget "HUD" (z_order=0)
  └─ [Canvas Panel] (transparent, füllt gesamten Viewport)
       ├─ Panel "health_bg"      → Anchor: TopLeft,    Offset: (20, 20),    Size: (300, 40)
       ├─ ProgressBar "health"   → Anchor: TopLeft,    Offset: (25, 25),    Size: (290, 30)
       ├─ Text "fps"             → Anchor: TopRight,   Offset: (-100, 10),  Size: (90, 20)
       └─ Panel "minimap_bg"     → Anchor: BottomRight, Offset: (-170, -170), Size: (150, 150)

Widget "Inventory" (z_order=10)
  └─ [Canvas Panel]
       ├─ Panel "inv_bg"         → Anchor: Center,     Offset: (-200, -150), Size: (400, 300)
       └─ Text "inv_title"       → Anchor: Center,     Offset: (-190, -140), Size: (380, 30)
```

### 4.3 Viewport-Rect-Quelle

| Modus | Quelle | Beschreibung |
|---|---|---|
| **Editor** | `UIManager::m_viewportContentRect` | Verfügbar nach Abzug aller Editor-Panels |
| **PIE** | `UIManager::m_viewportContentRect` | Dasselbe Rect (Gameplay-UI innerhalb Viewport) |
| **Standalone** | `SDL_GetWindowSizeInPixels()` | Gesamte Fenstergröße |

---

## 5. Anchor-System

### 5.1 WidgetAnchor Enum (10 Werte)

```cpp
enum class WidgetAnchor
{
    TopLeft,        // Pivot: (0, 0)
    TopRight,       // Pivot: (viewport.w, 0)
    BottomLeft,     // Pivot: (0, viewport.h)
    BottomRight,    // Pivot: (viewport.w, viewport.h)
    Top,            // Pivot: (viewport.w/2, 0)
    Bottom,         // Pivot: (viewport.w/2, viewport.h)
    Left,           // Pivot: (0, viewport.h/2)
    Right,          // Pivot: (viewport.w, viewport.h/2)
    Center,         // Pivot: (viewport.w/2, viewport.h/2)
    Stretch         // Element füllt gesamten Viewport
};
```

### 5.2 Anker-Berechnung

```
finalPos.x = anchorPivot.x + element.anchorOffset.x
finalPos.y = anchorPivot.y + element.anchorOffset.y
finalSize  = element.to - element.from   (= {w, h})
```

### 5.3 Per-Element-Anchor

Jedes `WidgetElement` hat eigene Anchor-Felder:
```cpp
struct WidgetElement
{
    // ... bestehende Felder ...
    WidgetAnchor anchor{ WidgetAnchor::TopLeft };
    Vec2 anchorOffset{ 0.0f, 0.0f };
};
```

### 5.4 Python Anchor-Strings

`parseAnchorString()` konvertiert Strings in Enum-Werte:
`"top_left"`, `"top_right"`, `"bottom_left"`, `"bottom_right"`, `"top"`, `"bottom"`, `"left"`, `"right"`, `"center"`, `"stretch"`

---

## 6. Rendering-Pipeline

### 6.1 Render-Zeitpunkt

```
render()
  ├─ renderWorld()           → 3D-Szene in Tab-FBO
  ├─ renderViewportUI()      → Gameplay-UI in Tab-FBO (nach World, vor Blit)
  ├─ glBlitFramebuffer()     → Tab-FBO → Default-FBO
  ├─ renderUI()              → Editor-UI auf Default-FBO
  └─ renderPopupWindows()
```

### 6.2 renderViewportUI() (implementiert)

- Prüft `hasWidgets()` → early return wenn leer
- Iteriert über `getSortedWidgets()` (niedrige Z-Order zuerst = hinten)
- Pro Element: `isVisible`-Check, transparente Canvas-Panels werden übersprungen (alpha == 0)
- Unterstützte Typen: Panel, Text, Label, Button, Image, ProgressBar (bg + fill), Slider (track + thumb)
- Ortho-Projektion mit Viewport-Offset, Scissor-Clipping auf Viewport-Rect

---

## 7. Input-Routing

### 7.1 Prioritäts-Kette

```
SDL_Event (main.cpp)
  ├─ 1. Editor-UIManager::handleMouseDown()
  │    └─ return true → Event konsumiert (Editor-Panel getroffen)
  ├─ 2. ViewportUIManager::handleMouseDown()
  │    └─ isInsideViewport(pos) && hitTest(localPos)
  │         └─ return true → Event konsumiert (Gameplay-UI-Element getroffen)
  └─ 3. Standard-Verarbeitung (Kamera, Gizmo, Picking)
```

### 7.2 Hit Testing (Multi-Widget)

```
hitTest(mousePos):
  for each widget in m_widgets (Z-Order ABSTEIGEND = vorderstes zuerst):
    if widget.isVisible():
      for each element in canvas.children (RÜCKWÄRTS = oberstes zuerst):
        if element.isHitTestable && element.containsPoint(mousePos):
          return element
  return nullptr
```

---

## 8. Python Scripting API (engine.viewport_ui)

### 8.1 Übersicht (28 Methoden)

| Kategorie | Methoden |
|---|---|
| **Widget-Lifecycle** | `create_widget(name, z_order)`, `remove_widget(name)`, `clear_all_widgets()` |
| **Sichtbarkeit** | `set_visible(visible)`, `show_cursor(visible)` |
| **Element-Erstellung** | `add_text(widget, id, text, x, y, w, h, anchor)`, `add_label(...)`, `add_button(...)`, `add_panel(widget, id, x, y, w, h, r, g, b, a, anchor)`, `add_image(widget, id, path, x, y, w, h, anchor)`, `add_progress_bar(widget, id, x, y, w, h, val, min, max, anchor)`, `add_slider(...)` |
| **Property-Setter** | `set_text(id, text)`, `set_color(id, r, g, b, a)`, `set_text_color(id, r, g, b, a)`, `set_opacity(id, opacity)`, `set_element_visible(id, visible)`, `set_border(id, thickness, r, g, b, a)`, `set_border_radius(id, radius)`, `set_tooltip(id, tooltip)`, `set_font_size(id, size)`, `set_font_bold(id, bold)`, `set_font_italic(id, italic)` |
| **Interaktion** | `set_on_clicked(id, callback)`, `set_value(id, value)` |
| **Layout-Runtime** | `set_anchor(id, anchor)`, `set_position(id, x, y)`, `set_size(id, w, h)` |

### 8.2 Alle add_*-Funktionen nehmen `widget_name` als ersten Parameter

Die Zuordnung ist explizit — jedes Element wird einem benannten Widget zugewiesen:
```python
ui = engine.viewport_ui
ui.create_widget("HUD", z_order=0)
ui.add_text("HUD", "health_label", "HP: 100", 20, 20, 200, 30, anchor="top_left")
```

### 8.3 Anchor als optionaler String-Parameter

Alle `add_*`-Methoden akzeptieren optional `anchor="..."` als letzten Parameter. Default: `"top_left"`.

### 8.4 Beispiel-Script

```python
import engine

def on_pie_start():
    ui = engine.viewport_ui
    ui.show_cursor(True)

    # HUD
    ui.create_widget("HUD", z_order=0)
    ui.add_panel("HUD", "health_bg", 20, 20, 300, 40, anchor="top_left")
    ui.add_progress_bar("HUD", "health_bar", 25, 25, 290, 30, value=0.75, anchor="top_left")
    ui.add_text("HUD", "health_text", "HP: 75/100", 30, 27, 200, 26, anchor="top_left")
    ui.add_text("HUD", "fps", "60 FPS", -100, 10, 90, 20, anchor="top_right")
    ui.add_panel("HUD", "minimap_bg", -170, -170, 150, 150, anchor="bottom_right")

    # Inventar (über HUD)
    ui.create_widget("Inventory", z_order=10)
    ui.add_panel("Inventory", "inv_bg", -200, -150, 400, 300, anchor="center")
    ui.add_text("Inventory", "inv_title", "Inventar", -190, -140, 380, 30, anchor="center")
    ui.set_element_visible("inv_bg", False)

    # Button-Callback
    ui.add_button("HUD", "menu_btn", "Menu", -70, -50, 120, 40, anchor="bottom")
    ui.set_on_clicked("menu_btn", lambda: print("Menu clicked!"))

def update_health(hp, max_hp):
    ui = engine.viewport_ui
    ui.set_value("health_bar", hp / max_hp)
    ui.set_text("health_text", f"HP: {hp}/{max_hp}")
```

---

## 9. Cursor-Steuerung & Kamera-Blockade

### 9.1 API

```python
engine.viewport_ui.show_cursor(True)   # Cursor zeigen, Kamera blockieren
engine.viewport_ui.show_cursor(False)  # Cursor verstecken, Kamera freigeben
```

### 9.2 Zustandstabelle

| Zustand | Cursor | Kamera-Input | UI-Interaktion |
|---|---|---|---|
| **PIE, Cursor versteckt** (Default) | Versteckt, relative mouse mode | Mausbewegung → Kamera-Rotation | Nicht möglich |
| **PIE, Cursor sichtbar** | Sichtbar, absolute mouse mode | **Blockiert** | Maus interagiert mit Gameplay-UI |
| **PIE pausiert (Shift+F1)** | Sichtbar, Editor-Cursor | Kein Kamera-Input | Editor + Viewport-UI zugänglich |

### 9.3 Implementierung

- `ViewportUIManager::m_gameplayCursorVisible` — Flag
- `py_vp_show_cursor()` — setzt Flag + `SDL_ShowCursor()`/`SDL_HideCursor()` + `SDL_SetWindowRelativeMouseMode()` + `SDL_SetWindowMouseGrab()`
- `main.cpp` Camera-Rotation-Guard: prüft `vpUI->isGameplayCursorVisible()` → blockiert `rotateCamera()` wenn `true`
- `clearAllWidgets()` setzt `m_gameplayCursorVisible = false` zurück

---

## 10. UI Designer Tab – Editor-Integration

### 10.1 Konzept

Der UI Designer ist ein **Editor-Tab** (wie der MeshViewer), der in der TitleBar-Tableiste erscheint. Er besteht aus:
- **Linkes Panel**: Control-Palette + Widget-Hierarchie
- **Mitte**: Der Viewport selbst (Live-Preview des Gameplay-UI)
- **Rechtes Panel**: Properties/Details des selektierten Elements

Der Viewport dient als **direkte Live-Preview** — kein separates FBO-Preview nötig, da die Gameplay-UI-Elemente im Viewport gerendert werden.

### 10.2 Öffnung

Der Tab wird über einen Button im ViewportOverlay (Settings-Dropdown oder dedizierter Button) geöffnet:
```cpp
// UIManager:
void openUIDesignerTab();
// → addTab("UIDesigner", "UI Designer", true);
// → setActiveTab("UIDesigner");
// → registriert Left/Right-Panel-Widgets mit tabId = "UIDesigner"
```

### 10.3 Layout (3 Bereiche)

#### Bereich 1: Control-Palette (Linkes Panel, oberer Teil)
- **Liste aller verfügbaren Element-Typen** für Gameplay-UI
- Klick auf ein Control → fügt es als Kind des Canvas Panels des selektierten Widgets hinzu
- Typen: Text, Label, Button, Panel, Image, ProgressBar, Slider
- (Keine StackPanel/Grid/ScrollView — Gameplay-UI nutzt Canvas-Panel-Positionierung)

#### Bereich 2: Widget-Hierarchie (Linkes Panel, unterer Teil)
- **Alle aktiven Viewport-Widgets** mit ihren Element-Kindern als Baum
- Knoten-Format: `[Typ] "ID"` (z.B. `[Panel] "health_bg"`)
- Widgets sind aufklappbar (zeigen Canvas-Panel-Kinder)
- Klick auf Knoten → selektiert Element → Properties aktualisieren
- Selektiertes Element wird im Viewport hervorgehoben (Outline)

#### Bereich 3: Properties/Details (Rechtes Panel)
- Zeigt editierbare Eigenschaften des selektierten Elements
- Direkte Änderungen → sofortige Aktualisierung im Viewport (bidirektional)

| Eigenschaft | Control | Anwendbar auf |
|---|---|---|
| `id` | EntryBar | Alle |
| `type` | Text (read-only) | Alle |
| `anchor` | DropDown (10 Werte) | Alle |
| `anchorOffset` | Vec2 (X/Y EntryBar) | Alle |
| `size` (w/h) | Vec2 (W/H EntryBar) | Alle |
| `color` | ColorPicker / RGBA | Alle |
| `hoverColor` | ColorPicker / RGBA | Button, Panel |
| `textColor` | ColorPicker / RGBA | Text, Button, Label |
| `text` | EntryBar | Text, Button, Label |
| `fontSize` | EntryBar (float) | Text, Button, Label |
| `opacity` | EntryBar (float 0-1) | Alle |
| `isVisible` | CheckBox | Alle |
| `isHitTestable` | CheckBox | Alle |
| `imagePath` | EntryBar | Image |
| `value` | EntryBar (float) | ProgressBar, Slider |
| `minValue` / `maxValue` | EntryBar (float) | ProgressBar, Slider |
| `borderThickness` | EntryBar (float) | Alle |
| `borderRadius` | EntryBar (float) | Alle |
| `tooltipText` | EntryBar | Alle |

### 10.4 Bidirektionale Synchronisation

- **Designer → Viewport**: Eigenschaft im Designer ändern → `ViewportUIManager` wird sofort aktualisiert → Layout wird dirty markiert → nächster Frame rendert das Update
- **Viewport → Designer**: Klick auf ein Element im Viewport (während Designer-Tab aktiv) → `onSelectionChanged` Callback → Hierarchie-Selektion + Properties aktualisieren
- **Script → Designer**: Wenn ein Python-Script Elemente hinzufügt/ändert → Hierarchie wird bei Bedarf refreshed (polling oder dirty-flag)

### 10.5 Implementierung (analog zum Widget Editor)

Der Code folgt dem etablierten Pattern aus `UIManager::openWidgetEditorPopup()`:

1. **Tab erstellen**: `addTab("UIDesigner", "UI Designer", true)` + `setActiveTab("UIDesigner")`
2. **Left/Right/Toolbar-Widgets registrieren**: `registerWidget(widgetId, widget, "UIDesigner")` — Tab-scoped, nur sichtbar wenn dieser Tab aktiv
3. **State-Struct**: Analog zu `WidgetEditorState` — speichert selektiertes Element, Widget-IDs der Panels
4. **Hierarchie-Refresh**: Traversiert `ViewportUIManager::getSortedWidgets()` und baut TreeView-Rows
5. **Properties-Refresh**: Liest Felder des selektierten `WidgetElement` und baut EntryBar/CheckBox/DropDown-Rows
6. **Tab-Close**: Unregistriert alle Widgets, entfernt Tab, cleared State

---

## 11. Designer UX-Verbesserungen (Roadmap)

> Zukünftige Verbesserungen, priorisiert nach Nutzen/Aufwand.

### Phase 1 – Grundlegende Bedienbarkeit
| Feature | Aufwand | Nutzen |
|---|---|---|
| Kontextmenü in Hierarchie (Löschen, Duplizieren, Verschieben) | Mittel | Hoch |
| Verbesserte Kontrollliste (Kategorie-Gruppen, Tooltips) | Niedrig | Mittel |
| ID-Validierung (Warnung bei Duplikaten) | Niedrig | Mittel |
| Drag & Drop in Hierarchie (Reihenfolge ändern) | Mittel | Hoch |

### Phase 2 – Visuelles Bearbeiten
| Feature | Aufwand | Nutzen |
|---|---|---|
| Direkte Manipulation im Viewport (Klick+Drag zum Verschieben) | Hoch | Sehr hoch |
| Resize-Handles (8 Griffe zum Größe ändern) | Hoch | Sehr hoch |
| Auflösungs-Preview (verschiedene Viewport-Größen simulieren) | Niedrig | Hoch |
| Snap-to-Grid (optionales Raster 8/16/32px) | Mittel | Hoch |
| Hilfslinien (Ausrichtung zu Geschwistern anzeigen) | Mittel | Hoch |

### Phase 3 – Produktivität
| Feature | Aufwand | Nutzen |
|---|---|---|
| Undo/Redo für Designer-Aktionen | Mittel | Sehr hoch |
| Copy/Paste von Elementen (Ctrl+C/V) | Niedrig | Hoch |
| Suche/Filter in Hierarchie | Niedrig | Mittel |
| Vorlagen-System (HUD Health Bar, Dialog-Box, etc.) | Mittel | Hoch |

### Phase 4 – Fortgeschritten
| Feature | Aufwand | Nutzen |
|---|---|---|
| Style-System (globale Farb-/Font-Definitionen) | Hoch | Hoch |
| Animationsvorschau (Opacity-Übergänge, Slide-In/Out) | Hoch | Mittel |
| Daten-Binding-Vorschau (`{playerName}`, `{health}`) | Mittel | Mittel |
| Python-Callback-Vorschlag (Auto-generierter Code) | Mittel | Mittel |

---

## 12. Schritt-für-Schritt Implementierungsplan

### ✅ Phase A: Runtime-System (ABGESCHLOSSEN)

| # | Schritt | Dateien | Status |
|---|---|---|---|
| A.1 | WidgetAnchor Enum erweitern (10 Werte) | `UIWidget.h` | ✅ |
| A.2 | `WidgetElement::anchor` + `anchorOffset` Felder | `UIWidget.h` | ✅ |
| A.3 | ViewportUIManager Multi-Widget Refactor | `ViewportUIManager.h/.cpp` | ✅ |
| A.4 | Canvas Panel implizit bei createWidget() | `ViewportUIManager.cpp` | ✅ |
| A.5 | Anchor-Resolution (ResolveAnchorsRecursive) | `ViewportUIManager.cpp` | ✅ |
| A.6 | Multi-Widget Z-Order Hit Testing | `ViewportUIManager.cpp` | ✅ |
| A.7 | renderViewportUI() Multi-Widget + ProgressBar/Slider | `OpenGLRenderer.cpp` | ✅ |
| A.8 | Gameplay Cursor Flag + SDL-Integration | `ViewportUIManager.h/.cpp`, `main.cpp` | ✅ |
| A.9 | Python API Rewrite (28 Methoden) | `PythonScripting.cpp` | ✅ |
| A.10 | engine.pyi Update | `engine.pyi` | ✅ |
| A.11 | PIE Auto-Cleanup (clearAllWidgets) | `main.cpp` | ✅ |

### ✅ Phase B: UI Designer Tab (ABGESCHLOSSEN)

| # | Schritt | Dateien | Status |
|---|---|---|---|
| B.1 | `openUIDesignerTab()` — Tab erstellen, UIDesignerState, Left/Right/Toolbar-Widgets | `UIManager.h`, `UIManager.cpp` | ✅ |
| B.2 | "UI Designer"-Button im Settings-Dropdown | `main.cpp` | ✅ |
| B.3 | Control-Palette (Panel, Text, Label, Button, Image, ProgressBar, Slider) | `UIManager.cpp` | ✅ |
| B.4 | Widget-Hierarchie TreeView (alle ViewportUIManager-Widgets + Canvas-Kinder) | `UIManager.cpp` | ✅ |
| B.5 | Properties-Panel (ID, Anchor, Offset, Size, Color, Opacity, Text, Value, etc.) | `UIManager.cpp` | ✅ |
| B.6 | Bidirektionale Synchronisation (Viewport-Klick → Designer, Designer → Viewport) | `UIManager.cpp` | ✅ |
| B.7 | Selektions-Highlight (orangener Outline-Rahmen in renderViewportUI) | `OpenGLRenderer.cpp` | ✅ |
| B.8 | Toolbar (Status-Anzeige, New Widget / Delete Widget Buttons) | `UIManager.cpp` | ✅ |

### 🟡 Phase C: Dokumentation & Verifikation

| # | Schritt | Dateien | Status |
|---|---|---|---|
| C.1 | Build-Verifikation (kompiliert alles?) | — | ✅ |
| C.2 | PROJECT_OVERVIEW.md aktualisieren | `PROJECT_OVERVIEW.md` | ✅ |
| C.3 | ENGINE_STATUS.md aktualisieren | `ENGINE_STATUS.md` | ✅ |

### 🔮 Phase D: Designer UX-Verbesserungen (Zukunft)

| # | Schritt | Beschreibung |
|---|---|---|
| D.1 | Kontextmenü in Hierarchie (Löschen, Duplizieren, Move Up/Down) |
| D.2 | Drag & Drop in Hierarchie (Reihenfolge ändern) |
| D.3 | Direkte Manipulation im Viewport (Klick+Drag) |
| D.4 | Resize-Handles im Viewport |
| D.5 | Undo/Redo Integration |
| D.6 | Copy/Paste |
| D.7 | Snap-to-Grid |
| D.8 | Vorlagen-System |

---

## 13. Datei-Übersicht

### 13.1 Bestehende Dateien (modifiziert)

| Datei | Änderung | Status |
|---|---|---|
| `src/Renderer/UIWidget.h` | WidgetAnchor (10 Werte), WidgetElement.anchor + anchorOffset | ✅ |
| `src/Renderer/ViewportUIManager.h` | Multi-Widget API, WidgetEntry, Cursor-Flag, computeAnchorPivot | ✅ |
| `src/Renderer/ViewportUIManager.cpp` | createWidget mit Canvas, ResolveAnchorsRecursive, Multi-Widget hitTest | ✅ |
| `src/Renderer/OpenGLRenderer/OpenGLRenderer.h` | `m_viewportUIManager` Member, `renderViewportUI()` | ✅ |
| `src/Renderer/OpenGLRenderer/OpenGLRenderer.cpp` | renderViewportUI() Multi-Widget, ProgressBar/Slider, Selektions-Highlight | ✅ |
| `src/Renderer/UIManager.h` | `m_viewportContentRect`, WidgetEditorState, UIDesignerState, openUIDesignerTab() | ✅ |
| `src/Renderer/UIManager.cpp` | openUIDesignerTab(), refreshUIDesignerHierarchy/Details, Sync, addElementToViewportWidget | ✅ |
| `src/Renderer/Renderer.h` | `getViewportUIManagerPtr()` | ✅ |
| `src/main.cpp` | Input-Routing, Cursor-Blockade, PIE-Cleanup, UI-Designer-Menüeintrag | ✅ |
| `src/Scripting/PythonScripting.cpp` | engine.viewport_ui (28 Methoden) | ✅ |
| `src/Scripting/engine.pyi` | viewport_ui Stubs (28 Methoden) | ✅ |

### 13.2 Designer-Tab (in bestehenden Dateien implementiert)

> Der Designer-Tab wird **nicht** als separate Klasse implementiert, sondern als UIManager-Methoden — genau wie der bestehende Widget-Editor.

| Methode | Beschreibung |
|---|---|
| `openUIDesignerTab()` | Tab erstellen, Left/Right/Toolbar-Widgets registrieren, Sync-Callback setzen |
| `closeUIDesignerTab()` | Tab schließen, Widgets unregistrieren, Callback entfernen |
| `refreshUIDesignerHierarchy()` | ViewportUIManager-Widgets traversieren, TreeView-Rows erstellen |
| `refreshUIDesignerDetails()` | Properties des selektierten Elements anzeigen (EntryBar/DropDown/CheckBox) |
| `selectUIDesignerElement()` | Element selektieren, Highlight + Panels aktualisieren |
| `addElementToViewportWidget()` | Element aus Control-Palette zum ViewportUIManager hinzufügen |
| `deleteSelectedUIDesignerElement()` | Selektiertes Element entfernen |

---

> **Status:** Phase A, B und C vollständig abgeschlossen. Nächste Schritte: Phase D (UX-Verbesserungen) bei Bedarf.
