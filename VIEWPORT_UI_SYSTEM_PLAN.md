# Viewport UI System – Implementierungsplan

> Detaillierter Plan zur Implementierung eines **Viewport-internen UI-Systems**, das unabhängig vom Editor-UI operiert und Widgets ausschließlich innerhalb des Viewport-Bereichs rendert. Ergänzt wird ein Editor-Fenster (Panel) zur visuellen Bearbeitung des Viewport-UI-Baums.

---

## Inhaltsverzeichnis

1. [Ziel & Abgrenzung](#1-ziel--abgrenzung)
2. [Ist-Analyse: Aktueller UI-Workflow](#2-ist-analyse-aktueller-ui-workflow)
3. [Architektur-Übersicht (Soll)](#3-architektur-übersicht-soll)
4. [Viewport UI Manager](#4-viewport-ui-manager)
5. [Viewport UI Rendering](#5-viewport-ui-rendering)
6. [Viewport UI Input-Routing](#6-viewport-ui-input-routing)
7. [Editor-Fenster: UI Designer Panel](#7-editor-fenster-ui-designer-panel)
8. [Serialisierung & Asset-Integration](#8-serialisierung--asset-integration)
9. [Python-Scripting-API](#9-python-scripting-api)
10. [Implementierungs-Reihenfolge (Schritte)](#10-implementierungs-reihenfolge-schritte)
11. [Datei-Übersicht (Neu/Modifiziert)](#11-datei-übersicht-neumodifiziert)
12. [Offene Entscheidungen](#12-offene-entscheidungen)

---

## 1. Ziel & Abgrenzung

### 1.1 Ziel
Ein **Runtime-UI-System**, das:
- Widgets **nur innerhalb des Viewport-Bereichs** rendert (nicht über Editor-Panels hinweg)
- Die **Viewport-Größe** als verfügbare Fläche nutzt (Bereich nach Abzug von TitleBar, StatusBar, Toolbar, WorldOutliner, EntityDetails, ContentBrowser)
- **Vollständig unabhängig** vom Editor-UI-System (`UIManager`) arbeitet
- Über ein **Editor-Fenster** (UI Designer Panel) zur Laufzeit im Editor bearbeitbar ist
- Im **PIE-Modus** (Play In Editor) als Spieler-UI funktioniert
- Per **Python-Scripting** zur Laufzeit steuerbar ist

### 1.2 Abgrenzung zum bestehenden Editor-UI
| Aspekt | Editor-UI (bestehend) | Viewport-UI (neu) |
|---|---|---|
| **UIManager** | `m_uiManager` in OpenGLRenderer | Neuer `ViewportUIManager` |
| **Koordinatensystem** | Fenster-Pixel (0,0 = oben-links Fenster) | Viewport-Pixel (0,0 = oben-links Viewport-Rect) |
| **Verfügbare Größe** | Gesamte Fenstergröße | Viewport-Content-Rect (nach Abzug der Editor-Panels) |
| **Rendering-Zeitpunkt** | Nach World-Blit, auf Default-FBO | In den aktiven Tab-FBO, **nach** World-Rendering, **vor** Blit auf Bildschirm |
| **Input-Routing** | Direkt über SDL-Events in main.cpp | Über Editor → Viewport-Weiterleitung (nur wenn Maus im Viewport-Rect) |
| **Sichtbarkeit** | Immer (Editor-Panels) | Nur im Viewport-Tab und im PIE-Modus |
| **Serialisierung** | `.asset`-Dateien (Widget-Typ) | Eigenes `.uiasset` oder erweitertes `.asset` mit ViewportUI-Flag |

---

## 2. Ist-Analyse: Aktueller UI-Workflow

### 2.1 Editor-UI-Pipeline (Zusammenfassung)

```
main.cpp → SDL_Event-Loop
  ├─ UIManager::handleMouseDown/Up/Scroll/TextInput/KeyDown
  └─ Renderer::render()
       ├─ renderWorld()          → 3D-Szene in Tab-FBO
       ├─ glBlitFramebuffer()    → Tab-FBO → Default-FBO
       ├─ renderUI()             → Editor-Widgets auf Default-FBO (über der Welt)
       └─ renderPopupWindows()   → Separate OS-Fenster
```

### 2.2 UIManager-Architektur
- **Singleton pro Renderer**: `OpenGLRenderer::m_uiManager`
- **Widget-Registrierung**: `registerWidget(id, widget, tabId)` — Widgets werden mit optionalem Tab-Scope registriert
- **Layout-System**: Dock-basiert (Top → Bottom → Left → Right → Other), verbraucht `available`-Rect schrittweise
- **Verfügbare Größe**: `setAvailableViewportSize(windowWidth, windowHeight)` — aktuell die **gesamte Fenstergröße**
- **Rendering**: Widgets werden in ein UI-FBO (`m_uiFbo`) gecacht und per Blit auf den Bildschirm gebracht
- **Z-Ordering**: Sortierung nach `Widget::getZOrder()`, gecacht mit Dirty-Flag
- **Input**: Hit-Test-basiert mit Bounds-Berechnung pro Element

### 2.3 Viewport-Content-Rect
Der Viewport-Bereich ist der Raum, der nach dem Dock-Layout der Editor-Panels übrig bleibt:
- **Oben**: TitleBar (100px) + ViewportOverlay/Toolbar (34px)
- **Links**: WorldOutliner + EntityDetails (280px)
- **Unten**: ContentBrowser (~250px) + StatusBar
- **Rechts**: (aktuell frei)

Dieses Rect wird aktuell **nicht explizit berechnet oder gespeichert** – es ergibt sich implizit aus dem Dock-Layout als `available`-Rect am Ende von `updateLayouts()`. Für das Viewport-UI muss dieses Rect erfasst und dem `ViewportUIManager` übergeben werden.

### 2.4 Rendering-Pipeline im Detail
```
render()
  ├─ SDL_GetWindowSizeInPixels → m_cachedWindowWidth/Height
  ├─ Tab-FBO sicherstellen (Viewport oder MeshViewer)
  ├─ renderWorld()
  │    ├─ Tab-FBO binden
  │    ├─ glClear (Color + Depth)
  │    ├─ Projection/View aufsetzen
  │    ├─ 3D-Entities rendern (Meshes, Lichter, Schatten)
  │    ├─ Skybox
  │    ├─ Gizmo
  │    ├─ Selection-Outline
  │    └─ [NEU: Viewport-UI hier rendern, vor FBO-Unbind]
  ├─ glBlitFramebuffer (Tab-FBO → Default-FBO)
  ├─ renderUI() (Editor-UI auf Default-FBO)
  │    ├─ UI-FBO sicherstellen
  │    ├─ Layout-Update (wenn dirty)
  │    ├─ Widgets zeichnen (Panel, Button, Text, etc.)
  │    └─ UI-FBO → Bildschirm blitten
  └─ renderPopupWindows()
```

### 2.5 Widget-Datenmodell
```
Widget (EngineObject)
  ├─ m_sizePixels, m_positionPixels, m_anchor
  ├─ m_fillX, m_fillY, m_absolutePosition
  ├─ m_zOrder
  └─ m_elements: vector<WidgetElement>
       ├─ type (Text, Button, Panel, StackPanel, Grid, ...)
       ├─ id, from, to, color, hoverColor
       ├─ text, font, fontSize
       ├─ minSize, padding, margin
       ├─ fillX, fillY, sizeToContent
       ├─ children: vector<WidgetElement> (Baum-Struktur)
       ├─ callbacks (onClicked, onValueChanged, ...)
       └─ computed: position, size, bounds (Layout-Ergebnis)
```

### 2.6 Vorhandene Steuerelemente (UIWidgets)
| Typ | Klasse | Verfügbar |
|---|---|---|
| Text | TextWidget | ✅ |
| Button | ButtonWidget | ✅ |
| Panel | — (direkt in WidgetElement) | ✅ |
| StackPanel | StackPanelWidget | ✅ |
| Grid | GridWidget | ✅ |
| Image | — (direkt in WidgetElement) | ✅ |
| EntryBar | EntryBarWidget | ✅ |
| Slider | SliderWidget | ✅ |
| ProgressBar | ProgressBarWidget | ✅ |
| CheckBox | CheckBoxWidget | ✅ |
| DropDown | DropDownWidget | ✅ |
| ColorPicker | ColorPickerWidget | ✅ |
| Separator | SeparatorWidget | ✅ |
| TreeView | TreeViewWidget | ✅ |
| TabView | TabViewWidget | ✅ |

---

## 3. Architektur-Übersicht (Soll)

```
┌────────────────────────────────────────────────────────┐
│                    Fenster (SDL)                        │
│  ┌─────────────────────────────────────────────────┐   │
│  │              TitleBar (Editor-UI)                │   │
│  ├────────┬────────────────────────────┬───────────┤   │
│  │Outliner│      VIEWPORT              │           │   │
│  │        │  ┌──────────────────────┐  │           │   │
│  │Entity  │  │   3D-Szene (World)   │  │           │   │
│  │Details │  │                      │  │           │   │
│  │        │  │  ┌────────────────┐  │  │           │   │
│  │        │  │  │ VIEWPORT-UI    │  │  │           │   │
│  │        │  │  │ (Buttons, HUD, │  │  │           │   │
│  │        │  │  │  Health Bar..) │  │  │           │   │
│  │        │  │  └────────────────┘  │  │           │   │
│  │        │  └──────────────────────┘  │           │   │
│  ├────────┴────────────────────────────┴───────────┤   │
│  │           ContentBrowser (Editor-UI)             │   │
│  ├─────────────────────────────────────────────────┤   │
│  │              StatusBar (Editor-UI)               │   │
│  └─────────────────────────────────────────────────┘   │
│                                                        │
│  ┌─────────────────────────┐                           │
│  │  UI Designer Panel      │ ← Neues Editor-Panel      │
│  │  (Popup oder docked)    │                           │
│  │  ┌───────────────────┐  │                           │
│  │  │ Control-Palette   │  │                           │
│  │  ├───────────────────┤  │                           │
│  │  │ Widget-Hierarchie │  │                           │
│  │  │ (TreeView)        │  │                           │
│  │  ├───────────────────┤  │                           │
│  │  │ Properties/Details│  │                           │
│  │  └───────────────────┘  │                           │
│  └─────────────────────────┘                           │
└────────────────────────────────────────────────────────┘
```

### 3.1 Kernkomponenten

| Komponente | Beschreibung |
|---|---|
| **ViewportUIManager** | Neuer Manager, analog zu `UIManager`, aber mit Viewport-Rect als Basis |
| **ViewportUIRenderer** | Rendering-Logik für Viewport-UI (in Tab-FBO, mit Viewport-Rect-Offset) |
| **UIDesignerPanel** | Editor-Fenster mit Control-Palette, Hierarchie-TreeView und Properties |
| **ViewportUIAsset** | Serialisierungs-Format für Viewport-UI-Layouts |

---

## 4. Viewport UI Manager

### 4.1 Klasse: `ViewportUIManager`
**Datei:** `src/Renderer/ViewportUIManager.h/.cpp`

```cpp
class ViewportUIManager
{
public:
    ViewportUIManager();
    ~ViewportUIManager();

    // Viewport-Rect (Pixel-Koordinaten relativ zum Fenster)
    void setViewportRect(float x, float y, float width, float height);
    Vec4 getViewportRect() const; // {x, y, w, h}
    Vec2 getViewportSize() const; // {w, h}

    // Widget-Verwaltung (flache Hierarchie: Root-Widget enthält den gesamten UI-Baum)
    void setRootWidget(const std::shared_ptr<Widget>& widget);
    std::shared_ptr<Widget> getRootWidget() const;
    void clearRootWidget();

    // Element-Zugriff
    WidgetElement* findElementById(const std::string& elementId);
    WidgetElement* getRootElement(); // Convenience: erstes Element des Root-Widgets

    // Layout
    void updateLayout(const std::function<Vec2(const std::string&, float)>& measureText);
    bool needsLayoutUpdate() const;
    void markLayoutDirty();

    // Input (Koordinaten in Fenster-Pixeln — werden intern in Viewport-lokal umgerechnet)
    bool handleMouseDown(const Vec2& windowPos, int button);
    bool handleMouseUp(const Vec2& windowPos, int button);
    bool handleScroll(const Vec2& windowPos, float delta);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key);
    void setMousePosition(const Vec2& windowPos);
    bool isPointerOverViewportUI(const Vec2& windowPos) const;

    // Rendering-Dirty-Tracking
    bool isRenderDirty() const;
    void clearRenderDirty();

    // Element-Selektion (für Designer-Panel)
    void setSelectedElementId(const std::string& elementId);
    const std::string& getSelectedElementId() const;
    using SelectionChangedCallback = std::function<void(const std::string& elementId)>;
    void setOnSelectionChanged(SelectionChangedCallback callback);

    // Sichtbarkeit (z.B. nur im PIE-Modus oder immer)
    void setVisible(bool visible);
    bool isVisible() const;

    // Serialisierung
    json toJson() const;
    bool loadFromJson(const json& data);

private:
    Vec4 m_viewportRect{}; // {x, y, w, h} in Fenster-Pixeln
    std::shared_ptr<Widget> m_rootWidget;
    std::string m_selectedElementId;
    SelectionChangedCallback m_onSelectionChanged;
    bool m_visible{ true };
    bool m_renderDirty{ true };
    WidgetElement* m_focusedEntry{ nullptr };
    Vec2 m_mousePosition{};

    // Koordinaten-Transformation
    Vec2 windowToViewport(const Vec2& windowPos) const;
    bool isInsideViewport(const Vec2& windowPos) const;
    WidgetElement* hitTest(const Vec2& viewportLocalPos) const;
};
```

### 4.2 Koordinaten-Transformation
- **Eingabe**: Fenster-Pixel-Koordinaten (wie sie von SDL kommen)
- **Transformation**: `viewportLocal = windowPos - viewportRect.xy`
- **Layout-Bereich**: `(0, 0)` bis `(viewportRect.w, viewportRect.h)`
- **Clip**: Alles außerhalb des Viewport-Rects wird ignoriert (Input und Rendering)

### 4.3 Viewport-Rect-Berechnung
Das Viewport-Rect muss nach dem Editor-UI-Layout berechnet werden. Es entspricht dem `available`-Rect am Ende von `UIManager::updateLayouts()`:

```cpp
// Am Ende von UIManager::updateLayouts():
m_viewportContentRect = { available.x, available.y, available.w, available.h };

// Wird dem ViewportUIManager übergeben:
viewportUIManager.setViewportRect(available.x, available.y, available.w, available.h);
```

**Implementierung**: `UIManager::updateLayouts()` speichert das finale `available`-Rect in einem neuen Member `m_viewportContentRect`. Dieser wird nach dem Layout-Update von `OpenGLRenderer::renderUI()` abgefragt und dem `ViewportUIManager` übergeben.

### 4.4 Widget-Baum vs. flache Liste
Im Gegensatz zum Editor-UIManager (der mehrere unabhängige Widgets verwaltet) hat der ViewportUIManager **ein einziges Root-Widget**, das den gesamten UI-Baum enthält:

```
Root-Widget (unsichtbar, füllt gesamtes Viewport-Rect)
  └─ StackPanel (Vertical)
       ├─ StackPanel "HUD_Top" (Horizontal, Top)
       │    ├─ Text "Health: 100"
       │    └─ ProgressBar (Health)
       ├─ [Mittlerer Bereich: leer, für 3D-Sicht]
       └─ StackPanel "HUD_Bottom" (Horizontal, Bottom)
            ├─ Button "Inventory"
            └─ Button "Map"
```

---

## 5. Viewport UI Rendering

### 5.1 Render-Zeitpunkt
Das Viewport-UI wird **innerhalb des Tab-FBO** gerendert, **nach** der 3D-Szene und **vor** dem Blit auf den Bildschirm:

```
renderWorld()
  ├─ 3D-Szene rendern
  ├─ Skybox
  ├─ Gizmo + Selection Outline
  └─ [NEU] renderViewportUI()   ← Hier einfügen

glBlitFramebuffer (Tab-FBO → Default-FBO)

renderUI() (Editor-UI auf Default-FBO)
```

### 5.2 Rendering-Logik
```cpp
void OpenGLRenderer::renderViewportUI()
{
    if (!m_viewportUIManager.isVisible() || !m_viewportUIManager.getRootWidget())
        return;

    // Viewport-Rect aus Editor-Layout
    const Vec4 vpRect = m_viewportUIManager.getViewportRect();

    // Depth-Test deaktivieren (UI über 3D)
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Ortho-Projektion für den Viewport-Bereich
    // Koordinaten: (0,0) oben-links Viewport bis (vpRect.w, vpRect.h) unten-rechts
    glm::mat4 uiProjection = glm::ortho(0.0f, vpRect.z, vpRect.w, 0.0f);

    // glViewport auf den Viewport-Bereich setzen (Tab-FBO-Koordinaten)
    // Da wir im Tab-FBO sind (der die gesamte Fenstergröße hat),
    // beschränken wir den Viewport auf den Content-Bereich
    glViewport((int)vpRect.x, m_cachedWindowHeight - (int)(vpRect.y + vpRect.w),
               (int)vpRect.z, (int)vpRect.w);
    // Alternativ: Scissor-Test statt Viewport-Änderung
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)vpRect.x, m_cachedWindowHeight - (int)(vpRect.y + vpRect.w),
              (int)vpRect.z, (int)vpRect.w);

    // Bestehende drawUI*-Methoden wiederverwenden (drawUIPanel, drawUIImage, etc.)
    // mit Viewport-lokalen Koordinaten
    renderViewportUIElements(m_viewportUIManager.getRootWidget()->getElements(),
                             0.0f, 0.0f, vpRect.z, vpRect.w, uiProjection);

    glDisable(GL_SCISSOR_TEST);
    // Viewport + Depth wiederherstellen
    glViewport(0, 0, m_cachedWindowWidth, m_cachedWindowHeight);
    glEnable(GL_DEPTH_TEST);
}
```

### 5.3 Wiederverwendung bestehender Zeichenfunktionen
Die vorhandenen `drawUIPanel`, `drawUIImage`, `drawUIOutline` und die Text-Rendering-Funktionen können 1:1 wiederverwendet werden. Der einzige Unterschied ist:
- **Ortho-Projektion**: Basiert auf Viewport-Größe statt Fenstergröße
- **glViewport/Scissor**: Beschränkt auf den Viewport-Bereich
- **Koordinaten**: Viewport-lokal (0,0 = Viewport oben-links)

### 5.4 FBO-Caching (optional, Phase 2)
Analog zum Editor-UI könnte ein eigenes FBO-Caching für die Viewport-UI implementiert werden. Für Phase 1 wird direkt in den Tab-FBO gerendert (kein separates Caching).

---

## 6. Viewport UI Input-Routing

### 6.1 Input-Kette
```
SDL_Event (main.cpp)
  ├─ Editor-UIManager::handleMouseDown()
  │    └─ return true → Event konsumiert (Editor-Panel getroffen)
  ├─ [NEU] ViewportUIManager::handleMouseDown()
  │    └─ isInsideViewport(pos) && hitTest(localPos)
  │         └─ return true → Event konsumiert (Viewport-UI-Element getroffen)
  └─ Standard-Verarbeitung (Kamera, Picking, etc.)
```

### 6.2 Prioritäts-Reihenfolge
1. **Editor-UI** hat höchste Priorität (Panels, Buttons, Dropdowns)
2. **Viewport-UI** hat mittlere Priorität (nur innerhalb Viewport-Rect)
3. **3D-Interaktion** (Kamera, Gizmo, Picking) hat niedrigste Priorität

### 6.3 Maus-Über-UI-Check
Die bestehende `isPointerOverUI()` in `UIManager` muss ergänzt werden:
```cpp
// In main.cpp Event-Loop:
bool overUI = uiManager.isPointerOverUI(mousePos);
if (!overUI)
{
    overUI = viewportUIManager.isPointerOverViewportUI(mousePos);
}
// Nur wenn !overUI → Kamera/Picking erlauben
```

---

## 7. Editor-Fenster: UI Designer Panel

### 7.1 Übersicht
Das UI Designer Panel ist ein neues Editor-Widget (kein Popup-Fenster), das im Viewport-Tab als **docked Panel auf der rechten Seite** oder als **separates Popup-Fenster** realisiert wird.

**Empfehlung**: Popup-Fenster (wie Landscape Manager / Engine Settings), da es nicht permanent sichtbar sein muss und wertvollen Viewport-Platz freihält.

### 7.2 Layout (3 Bereiche)

```
┌─────────────────────────────────┐
│  UI Designer                    │  ← Fenster-Titel
├─────────────────────────────────┤
│  ▾ Available Controls           │  ← Bereich 1: Control-Palette
│    📝 Text                      │
│    🔘 Button                    │
│    ▦  Panel                     │
│    ≡  StackPanel                │
│    ⊞  Grid                     │
│    ─  Separator                 │
│    ▐  Slider                    │
│    ▮  ProgressBar               │
│    ☑  CheckBox                  │
│    ▼  DropDown                  │
│    🖼 Image                     │
│    ⎆  EntryBar                  │
├─────────────────────────────────┤
│  ▾ Widget Hierarchy             │  ← Bereich 2: TreeView
│    ▾ Root                       │
│      ▾ StackPanel "HUD_Top"    │
│        ├─ Text "Health"         │
│        └─ ProgressBar "HP"      │
│      ▸ StackPanel "HUD_Bottom" │
├─────────────────────────────────┤
│  ▾ Properties                   │  ← Bereich 3: Details
│    ID:        [HUD_Top      ]   │
│    Type:      StackPanel        │
│    Position:  X[0] Y[0]        │
│    Size:      W[200] H[40]     │
│    Color:     [■ ████████]      │
│    FillX:     ☑                 │
│    FillY:     ☐                 │
│    Padding:   X[4] Y[4]        │
│    Font Size: [14           ]   │
│    ...                          │
└─────────────────────────────────┘
```

### 7.3 Bereich 1: Control-Palette
- **Liste aller verfügbaren Widget-Element-Typen** (Text, Button, Panel, StackPanel, Grid, etc.)
- **Aktion**: Klick auf ein Control → fügt es als Kind des aktuell selektierten Elements im Hierarchie-TreeView ein
- **Alternativ**: Drag & Drop von der Palette in den TreeView oder direkt in den Viewport
- Jedes Control wird mit sinnvollen Default-Werten erstellt (z.B. Button: 120x40px, Text: "New Text", StackPanel: Vertical)

### 7.4 Bereich 2: Widget-Hierarchie (TreeView)
- **TreeView-Widget** (bereits vorhanden: `TreeViewWidget`) zeigt den Element-Baum des Viewport-UI-Root-Widgets
- **Knoten**: Jeder Knoten zeigt `[Typ] "ID"` (z.B. `[StackPanel] "HUD_Top"`)
- **Aufklappbar**: Container-Elemente (StackPanel, Grid, Panel) sind aufklappbar
- **Selektion**: Klick auf einen Knoten → selektiert das Element → Properties werden aktualisiert
- **Highlight**: Selektiertes Element wird im Viewport visuell hervorgehoben (z.B. blauer Rahmen)
- **Kontextmenü** (Phase 2): Rechtsklick → Delete, Duplicate, Move Up/Down
- **Reihenfolge**: Die Reihenfolge im TreeView entspricht der Reihenfolge der `children`-Vektoren

### 7.5 Bereich 3: Properties/Details Panel
Zeigt die editierbaren Eigenschaften des selektierten Elements:

| Eigenschaft | Control-Typ | Anwendbar auf |
|---|---|---|
| `id` | EntryBar | Alle |
| `type` | Text (read-only) | Alle |
| `color` | ColorPicker (kompakt) | Alle |
| `hoverColor` | ColorPicker (kompakt) | Button, Panel |
| `textColor` | ColorPicker (kompakt) | Text, Button, EntryBar |
| `text` | EntryBar | Text, Button |
| `fontSize` | EntryBar (float) | Text, Button |
| `font` | EntryBar | Text, Button |
| `minSize` | Vec2 (2× EntryBar) | Alle |
| `padding` | Vec2 (2× EntryBar) | Alle |
| `margin` | Vec2 (2× EntryBar) | Alle |
| `fillX` | CheckBox | Alle |
| `fillY` | CheckBox | Alle |
| `sizeToContent` | CheckBox | Alle |
| `orientation` | DropDown (H/V) | StackPanel |
| `scrollable` | CheckBox | StackPanel, Grid |
| `imagePath` | EntryBar + DropdownButton | Image |
| `from` / `to` | Vec2 (je 2× EntryBar) | Alle |
| `isHitTestable` | CheckBox | Alle |
| `clickEvent` | EntryBar | Button |
| `value` | EntryBar | EntryBar, Slider |
| `minValue` / `maxValue` | EntryBar (float) | Slider, ProgressBar |
| `isPassword` | CheckBox | EntryBar |
| `wrapText` | CheckBox | Text |
| `textAlignH` | DropDown (Left/Center/Right) | Text, Button |
| `textAlignV` | DropDown (Top/Center/Bottom) | Text, Button |
| `items` | Mehrzeilige Liste | DropDown |
| `isChecked` | CheckBox | CheckBox |
| `shaderVertex` | EntryBar | Alle (optional) |
| `shaderFragment` | EntryBar | Alle (optional) |

### 7.6 Implementierung als Popup-Fenster
```cpp
// Öffnung über UIManager (analog zu Landscape Manager / Engine Settings):
void UIManager::openUIDesignerPopup();

// Oder über Menü-Button im ViewportOverlay (Settings DropDown):
// "Open UI Designer" → openUIDesignerPopup()
```

### 7.7 Bidirektionale Synchronisation
- **Designer → Viewport-UI**: Änderungen in Properties/Palette aktualisieren sofort den ViewportUIManager → Element wird im Viewport live aktualisiert
- **Viewport-UI → Designer**: Klick auf ein Element im Viewport selektiert es im TreeView und zeigt Properties
- **Dirty-Tracking**: Änderungen markieren das ViewportUI-Asset als unsaved

---

## 8. Serialisierung & Asset-Integration

### 8.1 Asset-Typ
Option A: **Neuer Asset-Typ `ViewportUI`** in `AssetTypes.h`:
```cpp
enum class AssetType {
    // ... bestehend ...
    ViewportUI   // Neuer Typ für Viewport-UI-Layouts
};
```

Option B: **Bestehenden Widget-Typ erweitern** mit einem Flag `"isViewportUI": true`.

**Empfehlung**: Option A (eigener Typ), da Viewport-UI-Layouts eine andere Semantik haben als Editor-Widgets (ein Root-Widget mit vollem Baum vs. einzelne Panels).

### 8.2 Datei-Format
```json
{
  "m_type": "ViewportUI",
  "m_name": "GameHUD",
  "m_viewportUI": {
    "rootWidget": {
      "m_sizePixels": { "x": 0, "y": 0 },
      "m_fillX": true,
      "m_fillY": true,
      "m_elements": [
        {
          "type": "StackPanel",
          "id": "HUD_Top",
          "orientation": "Horizontal",
          "fillX": true,
          "minSize": { "x": 0, "y": 40 },
          "children": [
            {
              "type": "Text",
              "id": "HealthLabel",
              "text": "Health: 100",
              "fontSize": 16,
              "textColor": { "x": 1, "y": 1, "z": 1, "w": 1 }
            },
            {
              "type": "ProgressBar",
              "id": "HealthBar",
              "minSize": { "x": 200, "y": 20 },
              "fillColor": { "x": 0, "y": 1, "z": 0, "w": 1 },
              "valueFloat": 0.75
            }
          ]
        }
      ]
    }
  }
}
```

### 8.3 Level-Integration
Jedes Level kann **ein Viewport-UI-Layout** referenzieren:
```json
// In Level-JSON:
{
  "Entities": [ ... ],
  "ViewportUI": "UI/GameHUD.asset"   // Pfad zum ViewportUI-Asset
}
```

Alternativ (Phase 2): Mehrere Viewport-UI-Layouts, schaltbar per Script.

### 8.4 AssetManager-Erweiterung
- `discoverAssetsAndBuildRegistry()` → erkennt `.asset`-Dateien mit `m_type: "ViewportUI"`
- `loadAsset()` → lädt ViewportUI-Assets und erstellt das Root-Widget
- `saveAsset()` → serialisiert den ViewportUI-Baum zurück in JSON

---

## 9. Python-Scripting-API

### 9.1 `engine.viewport_ui` Modul
```python
# Element-Zugriff
engine.viewport_ui.find_element("HealthBar")           # → Element-Handle
engine.viewport_ui.set_text("HealthLabel", "HP: 75")    # Text setzen
engine.viewport_ui.set_value("HealthBar", 0.75)         # ProgressBar/Slider-Wert
engine.viewport_ui.set_color("HealthBar", 1.0, 0.0, 0.0, 1.0)  # Farbe ändern
engine.viewport_ui.set_visible("HealthBar", True)       # Sichtbarkeit
engine.viewport_ui.set_enabled("PauseButton", False)    # Interaktivität

# Callbacks
engine.viewport_ui.on_clicked("PauseButton", pause_game)
engine.viewport_ui.on_value_changed("VolumeSlider", update_volume)

# Layout laden/wechseln
engine.viewport_ui.load_layout("UI/GameHUD.asset")
engine.viewport_ui.clear()
```

### 9.2 engine.pyi Update
```python
class viewport_ui:
    @staticmethod
    def find_element(element_id: str) -> bool: ...
    @staticmethod
    def set_text(element_id: str, text: str) -> None: ...
    @staticmethod
    def set_value(element_id: str, value: float) -> None: ...
    @staticmethod
    def set_color(element_id: str, r: float, g: float, b: float, a: float) -> None: ...
    @staticmethod
    def set_visible(element_id: str, visible: bool) -> None: ...
    @staticmethod
    def on_clicked(element_id: str, callback: Callable) -> None: ...
    @staticmethod
    def on_value_changed(element_id: str, callback: Callable[[float], None]) -> None: ...
    @staticmethod
    def load_layout(asset_path: str) -> bool: ...
    @staticmethod
    def clear() -> None: ...
```

---

## 10. Implementierungs-Reihenfolge (Schritte)

### Phase 1: Grundgerüst (MVP)

| Schritt | Beschreibung | Dateien | Abhängigkeit |
|---|---|---|---|
| **1.1** | `ViewportUIManager` Klasse erstellen (Header + Stub-Implementierung) | `ViewportUIManager.h/.cpp` | — |
| **1.2** | Viewport-Content-Rect aus `UIManager::updateLayouts()` erfassen und als Member `m_viewportContentRect` speichern | `UIManager.h/.cpp` | — |
| **1.3** | `ViewportUIManager` als Member in `OpenGLRenderer` hinzufügen | `OpenGLRenderer.h/.cpp` | 1.1 |
| **1.4** | Viewport-Rect nach jedem Layout-Update an `ViewportUIManager` übergeben | `OpenGLRenderer.cpp` (in `renderUI()`) | 1.2, 1.3 |
| **1.5** | `renderViewportUI()` Methode in `OpenGLRenderer` implementieren — Rendering innerhalb Tab-FBO mit Scissor/Viewport-Clipping | `OpenGLRenderer.cpp` | 1.3, 1.4 |
| **1.6** | `renderViewportUI()` Aufruf in `render()` einfügen (nach `renderWorld()`, vor `glBlitFramebuffer`) | `OpenGLRenderer.cpp` | 1.5 |
| **1.7** | Input-Routing für Viewport-UI in `main.cpp` einbauen (nach Editor-UI, vor Kamera/Picking) | `main.cpp` | 1.3 |
| **1.8** | Test: Manuell ein Root-Widget mit einem Text-Element erstellen und im Viewport anzeigen | — | 1.1–1.7 |

### Phase 2: Asset-Integration

| Schritt | Beschreibung | Dateien | Abhängigkeit |
|---|---|---|---|
| **2.1** | `AssetType::ViewportUI` zu `AssetTypes.h` hinzufügen | `AssetTypes.h` | — |
| **2.2** | Lade-/Speicher-Logik in `AssetManager` für ViewportUI-Assets implementieren | `AssetManager.h/.cpp` | 2.1 |
| **2.3** | `ViewportUI`-Feld in `EngineLevel`-JSON hinzufügen (optional, Pfad zum UI-Asset) | `EngineLevel.h/.cpp` | 2.1, 2.2 |
| **2.4** | Beim Level-Laden das Viewport-UI-Asset automatisch laden und in `ViewportUIManager` setzen | `RenderResourceManager.cpp` oder `OpenGLRenderer.cpp` | 2.2, 2.3 |
| **2.5** | `ViewportUIManager::toJson()` / `loadFromJson()` implementieren | `ViewportUIManager.cpp` | 2.2 |

### Phase 3: UI Designer Panel

| Schritt | Beschreibung | Dateien | Abhängigkeit |
|---|---|---|---|
| **3.1** | `UIDesignerPanel` Popup-Fenster-Klasse erstellen (analog zu LandscapeManager) | `UIDesignerPanel.h/.cpp` | — |
| **3.2** | Control-Palette implementieren (Liste aller WidgetElementTypes als Buttons) | `UIDesignerPanel.cpp` | 3.1 |
| **3.3** | Widget-Hierarchie-TreeView implementieren (rekursive Darstellung des Element-Baums) | `UIDesignerPanel.cpp` | 3.1 |
| **3.4** | Properties-Panel implementieren (dynamisch basierend auf selektiertem Element-Typ) | `UIDesignerPanel.cpp` | 3.1, 3.3 |
| **3.5** | Bidirektionale Synchronisation: Designer ↔ ViewportUIManager ↔ Viewport | `UIDesignerPanel.cpp`, `ViewportUIManager.cpp` | 3.3, 3.4, 1.3 |
| **3.6** | "Open UI Designer"-Button im ViewportOverlay/Toolbar oder Menü hinzufügen | `UIManager.cpp`, `main.cpp` | 3.1 |
| **3.7** | Selektions-Highlight im Viewport (blauer Rahmen um selektiertes Element) | `OpenGLRenderer.cpp` | 3.5 |

### Phase 4: Scripting & Polish

| Schritt | Beschreibung | Dateien | Abhängigkeit |
|---|---|---|---|
| **4.1** | `engine.viewport_ui` Python-Modul implementieren | `PythonScripting.cpp` | 1.3 |
| **4.2** | `engine.pyi` aktualisieren | `engine.pyi` | 4.1 |
| **4.3** | Undo/Redo für UI-Designer-Aktionen (Add/Remove/Edit Element) | `UIDesignerPanel.cpp` | 3.2–3.5 |
| **4.4** | Viewport-UI im PIE-Modus aktivieren (Input-Routing an Viewport-UI statt Editor) | `main.cpp`, `OpenGLRenderer.cpp` | 1.7, 4.1 |
| **4.5** | Drag & Drop aus Control-Palette in TreeView/Viewport | `UIDesignerPanel.cpp` | 3.2, 3.3 |
| **4.6** | Asset-Discovery für ViewportUI-Dateien im Content Browser | `AssetManager.cpp` | 2.1, 2.2 |
| **4.7** | `PROJECT_OVERVIEW.md` und `ENGINE_STATUS.md` aktualisieren | `PROJECT_OVERVIEW.md`, `ENGINE_STATUS.md` | Alle |

---

## 11. Datei-Übersicht (Neu/Modifiziert)

### 11.1 Neue Dateien

| Datei | Zweck |
|---|---|
| `src/Renderer/ViewportUIManager.h` | Header für Viewport-UI-Manager |
| `src/Renderer/ViewportUIManager.cpp` | Implementierung Viewport-UI-Manager |
| `src/Renderer/EditorWindows/UIDesignerPanel.h` | Header für UI Designer Popup |
| `src/Renderer/EditorWindows/UIDesignerPanel.cpp` | Implementierung UI Designer Popup |

### 11.2 Modifizierte Dateien

| Datei | Änderung |
|---|---|
| `src/Renderer/OpenGLRenderer/OpenGLRenderer.h` | `ViewportUIManager m_viewportUIManager` Member + `renderViewportUI()` Methode |
| `src/Renderer/OpenGLRenderer/OpenGLRenderer.cpp` | `renderViewportUI()` implementieren, Aufruf in `render()` einfügen |
| `src/Renderer/UIManager.h` | `m_viewportContentRect` Member + Getter |
| `src/Renderer/UIManager.cpp` | Viewport-Content-Rect am Ende von `updateLayouts()` speichern |
| `src/Renderer/Renderer.h` | `virtual ViewportUIManager& getViewportUIManager()` abstrakte Methode |
| `src/AssetManager/AssetTypes.h` | `AssetType::ViewportUI` hinzufügen |
| `src/AssetManager/AssetManager.h/.cpp` | Lade-/Speicher-/Discovery-Logik für ViewportUI |
| `src/Core/EngineLevel.h/.cpp` | `m_viewportUIPath` Member für Level-Integration |
| `src/main.cpp` | Input-Routing für Viewport-UI, "Open UI Designer" Event |
| `src/Scripting/PythonScripting.cpp` | `engine.viewport_ui` Modul |
| `src/Scripting/engine.pyi` | `viewport_ui` Stubs |
| `src/Renderer/CMakeLists.txt` | Neue Dateien zum Build hinzufügen |
| `PROJECT_OVERVIEW.md` | Viewport-UI-System Dokumentation |
| `ENGINE_STATUS.md` | Status-Updates |

---

## 12. Offene Entscheidungen

| # | Frage | Optionen | Empfehlung |
|---|---|---|---|
| 1 | **Designer als Popup oder Docked Panel?** | A) Popup (wie Landscape Manager) B) Docked (wie WorldOutliner) | A) Popup — spart Viewport-Platz, kann bei Bedarf geöffnet werden |
| 2 | **Viewport-UI Sichtbarkeit im Editor** | A) Immer sichtbar B) Nur wenn Designer offen C) Toggle | C) Toggle-Button im Toolbar |
| 3 | **Mehrere Viewport-UI-Layouts pro Level?** | A) Genau eines B) Mehrere, schaltbar per Script | A) für Phase 1, B) für Phase 2+ |
| 4 | **Drag & Drop in den Viewport** | A) Nur Palette → TreeView B) Palette → TreeView + Viewport | B) für bessere UX, aber höherer Aufwand |
| 5 | **Anchor-System für Viewport-UI** | A) Absolut (Pixel) B) Relativ (%) C) Anchor-basiert (wie CSS) | C) Anchor-basiert — nutzt bestehendes `WidgetAnchor` + `from/to` |
| 6 | **Neuer Asset-Typ vs. Widget-Erweiterung** | A) `AssetType::ViewportUI` B) Widget mit `isViewportUI`-Flag | A) klarer, eigener Lebenszyklus |
| 7 | **ViewportUI im MeshViewer-Tab?** | A) Nur Viewport-Tab B) Alle Tabs | A) nur Viewport-Tab — MeshViewer braucht kein Runtime-UI |
| 8 | **Z-Ordering Viewport-UI vs. Gizmo** | A) UI über Gizmo B) Gizmo über UI | A) UI über Gizmo — User klickt auf UI-Buttons, nicht durch sie hindurch |

---

> **Nächster Schritt**: Entscheidungen in Abschnitt 12 treffen und mit Phase 1, Schritt 1.1 beginnen.
