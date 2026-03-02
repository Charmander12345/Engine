# Widget Editor – UX-Verbesserungsplan

> Umfassender Plan, um den Widget Editor so benutzerfreundlich und intuitiv wie möglich für Spieleentwickler zu gestalten.

---

## Inhaltsverzeichnis

1. [Aktueller Zustand](#1-aktueller-zustand)
2. [Behobene Probleme](#2-behobene-probleme)
3. [Phase 1 – Grundlegende Bedienbarkeit](#3-phase-1--grundlegende-bedienbarkeit)
4. [Phase 2 – Visuelles Bearbeiten & WYSIWYG](#4-phase-2--visuelles-bearbeiten--wysiwyg)
5. [Phase 3 – Produktivität & Workflow](#5-phase-3--produktivität--workflow)
6. [Phase 4 – Fortgeschrittene Features](#6-phase-4--fortgeschrittene-features)
7. [Phase 5 – Polish & Qualität](#7-phase-5--polish--qualität)
8. [UI-Referenzlayout](#8-ui-referenzlayout)
9. [Priorisierung](#9-priorisierung)

---

## 1. Aktueller Zustand

Der Widget Editor besteht aus:
- **Linkes Panel**: Kontrollliste (verfügbare Steuerelemente) + Hierarchiebaum
- **Rechtes Panel**: Details/Properties des selektierten Elements
- **Mitte**: Canvas mit FBO-Preview des bearbeiteten Widgets (Zoom + Pan)
- **Toolbar**: Save-Button + Dirty-Indicator

### Bekannte Einschränkungen
- Kein visuelles Verschieben/Größenänderung von Elementen per Maus im Canvas
- Kein Kontextmenü (Rechtsklick) in der Hierarchie
- Keine Undo/Redo-Integration für Widget-Editor-Änderungen
- Keine Vorschau-Auflösungsumschaltung (z.B. 1920×1080, 1280×720, mobil)
- Kein Snap-to-Grid oder Hilfslinien
- Kein Copy/Paste von Elementen
- Keine Mehrfachauswahl

---

## 2. Behobene Probleme

### 2.1 Preview-Klick-Selektion (behoben)
**Problem**: Klick auf das Preview wählte nicht korrekt das richtige Steuerelement aus.
**Ursache**: Hit-Test verwendete `boundsMinPixels/boundsMaxPixels` (expandierte Bounding Box inkl. Kinder) statt `computedPositionPixels/computedSizePixels` (eigentliches visuelles Rect des Elements).
**Lösung**: Hit-Test verwendet nun das visuelle Rect des Elements. Zusätzlich erhalten Elemente ohne ID beim Laden automatisch eine generierte ID, sodass sie selektierbar sind.

### 2.2 Outline-Darstellung (behoben)
**Problem**: Outlines zeigten Dreiecke (Diagonale im Quad) statt einer sauberen Rechteck-Umrandung.
**Ursache**: `drawUIOutline` verwendete `glPolygonMode(GL_LINE)` auf einem Quad (2 Dreiecke → 6 Kanten inkl. Diagonale).
**Lösung**: Outline wird nun als 4 dünne Rechtecke (oben, unten, links, rechts) gezeichnet – saubere Darstellung ohne Diagonale.

### 2.3 Alignment-Dropdowns (behoben)
**Problem**: Horizontale/Vertikale Ausrichtung wurde per Texteingabe mit Hint gesteuert.
**Lösung**: Ersetzt durch echte DropDown-Widgets mit den Optionen Left/Center/Right/Fill bzw. Top/Center/Bottom/Fill.

### 2.4 Details-Panel-Kategorisierung (behoben)
**Problem**: Properties waren nicht optimal gruppiert.
**Lösung**: Neu organisiert in: Identity (Typ, editierbare ID) → Transform (From/To) → Layout (Alignment, Min/Max, Padding) → Appearance → typspezifische Sektionen.

---

## 3. Phase 1 – Grundlegende Bedienbarkeit

### 3.1 Kontextmenü in der Hierarchie
- **Rechtsklick auf Element** → Kontextmenü mit:
  - `Löschen` – Entfernt das Element und alle Kinder
  - `Duplizieren` – Kopiert das Element als Geschwister
  - `Nach oben/unten verschieben` – Reihenfolge im Parent ändern
  - `Neues Kind hinzufügen` → Untermenü mit allen Steuerelementtypen
  - `In Container einwickeln` → Wrappt das Element in ein neues StackPanel/Panel
- **Tastaturkürzel**: `Delete` = Löschen, `Ctrl+D` = Duplizieren

### 3.2 Verbessertes Drag & Drop in der Hierarchie
- **Visuelle Drop-Indikatoren**: Linie zwischen Elementen zeigt Einfügeposition an
- **Drop-on-Container**: Hervorhebung, wenn ein Element über einen Container gezogen wird (wird als Kind eingefügt)
- **Reihenfolge ändern**: Zwischen Geschwistern verschieben per Drag
- **Eltern wechseln**: Von einem Container in einen anderen ziehen

### 3.3 ID-Validierung
- Warnung bei doppelten IDs (rote Umrandung im Eingabefeld)
- Automatische ID-Generierung beim Hinzufügen neuer Elemente (bereits implementiert)
- ID-Feld markiert Änderungen sofort in Hierarchie und Preview

### 3.4 Verbesserte Kontrollliste
- **Kategorie-Gruppen**: Steuerelemente nach Typ gruppieren:
  - **Basis**: Panel, Text, Label, Image, Separator
  - **Eingabe**: Button, ToggleButton, RadioButton, EntryBar, CheckBox, DropDown, Slider
  - **Container**: StackPanel, ScrollView, Grid
  - **Anzeige**: ProgressBar, ColorPicker
- **Icons** vor jedem Steuerelementtyp für schnellere visuelle Erkennung
- **Tooltip** bei Hover über ein Steuerelement → kurze Beschreibung

---

## 4. Phase 2 – Visuelles Bearbeiten & WYSIWYG

### 4.1 Direkte Manipulation im Canvas
- **Klick + Drag**: Element im Canvas verschieben (aktualisiert `from/to` in Echtzeit)
- **Resize-Handles**: 8 Griffe (Ecken + Kanten) um Elemente visuell zu vergrößern/verkleinern
- **Rotation** (optional): Für zukünftige Unterstützung, vorerst deaktiviert
- **Selection Overlay**: Orange Outline (bereits vorhanden) + blaue Resize-Handles

### 4.2 Auflösungs-Preview
- **Dropdown in der Toolbar**: Auflösungsauswahl
  - `Custom` (aktuell: Widget-Größe)
  - `1920 × 1080` (Full HD)
  - `1280 × 720` (HD)
  - `800 × 600` (klein)
  - `375 × 667` (Mobil Portrait)
  - `667 × 375` (Mobil Landscape)
- **FBO-Größe** wird entsprechend angepasst, Widget wird in die gewählte Auflösung gerendert
- **Responsive-Test**: Schnell zwischen Auflösungen wechseln, um Layout-Verhalten zu prüfen

### 4.3 Snap & Hilfslinien
- **Snap-to-Grid**: Optionales Grid-Raster im Canvas (z.B. 8px, 16px, 32px)
- **Snap-to-Element**: Beim Verschieben/Resizen an andere Elemente „einrasten"
- **Hilfslinien**: Blaue gestrichelte Linien beim Drag zeigen Ausrichtung zu Geschwistern
- **Zentrierungshinweis**: Linie zeigt an, wenn Element horizontal/vertikal zentriert ist

### 4.4 Zoom & Navigation verbessern
- **Zoom auf Maus-Position**: Scrollrad zoomt auf die aktuelle Mausposition (nicht auf die Mitte)
- **Zoom-Anzeige**: Zoom-Level in der Toolbar anzeigen (z.B. "100%")
- **Fit to Canvas**: Button oder `Ctrl+0` → Widget wird so skaliert, dass es komplett sichtbar ist
- **Mini-Map** (optional, Phase 4): Kleines Übersichtsfenster bei sehr großen Widgets

---

## 5. Phase 3 – Produktivität & Workflow

### 5.1 Undo/Redo
- **Jede Eigenschaftsänderung** erzeugt einen Undo-Eintrag
- **Element hinzufügen/löschen** erzeugt einen Undo-Eintrag
- **Drag & Drop in Hierarchie** erzeugt einen Undo-Eintrag
- **Ctrl+Z** / **Ctrl+Y** für Undo/Redo
- Integration mit dem bestehenden `UndoRedoManager`

### 5.2 Copy/Paste
- **Ctrl+C**: Selektiertes Element (inkl. Kinder) in Clipboard kopieren
- **Ctrl+V**: Clipboard-Element als Kind des selektierten Containers einfügen
- **Ctrl+X**: Ausschneiden (Kopieren + Löschen)
- Clipboard als JSON-String (interoperabel mit externen Editoren)

### 5.3 Mehrfachauswahl
- **Ctrl+Klick**: Mehrere Elemente selektieren
- **Shift+Klick**: Bereich selektieren (im Hierarchiebaum)
- **Gemeinsame Properties**: Im Details-Panel werden nur gemeinsame Properties angezeigt
- **Gruppen-Verschieben**: Alle selektierten Elemente gleichzeitig verschieben
- **Gruppen-Löschen**: Alle selektierten Elemente löschen

### 5.4 Vorlagen-System (Templates)
- **Template speichern**: Selektiertes Element (inkl. Kinder) als wiederverwendbare Vorlage speichern
- **Template einfügen**: Vorlagen-Bibliothek in der Kontrollliste (eigene Kategorie)
- **Eingebaute Vorlagen**:
  - HUD Health Bar (ProgressBar + Text)
  - Dialog-Box (Panel + Text + Button)
  - Inventar-Slot (Panel + Image + Text)
  - Menü-Button-Reihe (StackPanel + 3 Buttons)
- **Template-Format**: JSON-Datei im Projekt-Ordner (`Content/Templates/UI/`)

### 5.5 Suche & Filter
- **Suchfeld** in der Hierarchie: Filtert Elemente nach ID oder Typ
- **Highlight**: Gefundene Elemente werden in der Hierarchie und im Canvas hervorgehoben
- **Nächstes/Vorheriges Ergebnis**: Pfeil-Buttons oder Enter/Shift+Enter

---

## 6. Phase 4 – Fortgeschrittene Features

### 6.1 Animationsvorschau
- **Einfache Animationen**: Opacity-Übergänge, Slide-In/Out testen
- **Vorschau-Button**: „Play" in der Toolbar → simuliert Animationen im Canvas
- **Keyframe-Editor** (Zukunft): Visuelle Zeitlinie für UI-Animationen

### 6.2 Daten-Binding-Vorschau
- **Platzhalter-Werte**: Text-Elemente mit `{playerName}`, `{health}` → im Editor als Vorschau angezeigt
- **Testdaten**: JSON-Datei mit Testdaten, die im Editor zur Vorschau geladen werden
- **Live-Preview** im PIE-Modus: Widget mit echten Spieldaten anzeigen

### 6.3 Style-System
- **Globale Stile**: Farb-/Font-Definitionen, die mehrere Elemente referenzieren
- **Theme-Unterstützung**: Light/Dark/Custom-Theme für das gesamte Widget
- **Style-Editor**: Eigenes Panel für Style-Definitionen
- **Vererbung**: Kind-Elemente erben Stile vom Eltern-Element (sofern nicht überschrieben)

### 6.4 Responsive Layout
- **Anker-System**: Elemente an Kanten/Mitte verankern (Left, Right, Center, Stretch)
- **Breakpoints**: Verschiedene Layouts für verschiedene Auflösungen
- **Auto-Layout**: StackPanel/Grid berechnen Kindergrößen automatisch
- **Fluid-Größen**: Prozentuale Größenangaben relativ zum Eltern-Element

### 6.5 Event-System im Editor
- **Event-Namen zuweisen**: Jedem interaktiven Element einen Event-Namen geben
- **Event-Liste**: Übersicht aller definierten Events im Widget
- **Python-Callback-Vorschlag**: Automatisch generierter Python-Code für Event-Handling
- **Verbindungslinien** (optional): Visuelle Darstellung welche Events verbunden sind

---

## 7. Phase 5 – Polish & Qualität

### 7.1 Barrierefreiheit des Editors
- **Tastaturnavigation**: Tab/Shift+Tab durch alle Properties
- **Pfeiltasten**: In der Hierarchie navigieren
- **Enter**: Element selektieren oder Property bestätigen
- **Escape**: Selektion aufheben oder Dropdown schließen

### 7.2 Visuelle Verbesserungen
- **Element-Typ-Icons** in der Hierarchie (z.B. 📝 Text, 🔘 Button, ▦ Panel)
- **Farbige Bounding-Boxen** im Canvas (verschiedene Farben je nach Typ)
- **Transparenz-Schachbrettmuster** im Canvas-Hintergrund (wie in Bildbearbeitungsprogrammen)
- **Hover-Highlight** im Canvas: Beim Überfahren eines Elements wird es leicht hervorgehoben
- **Breadcrumb-Pfad**: Am oberen Rand des Details-Panels den Pfad zum selektierten Element anzeigen

### 7.3 Performance
- **Lazy Rendering**: FBO-Preview nur neu rendern, wenn tatsächlich etwas geändert wurde
- **Hierarchie-Caching**: TreeView nur aktualisieren, wenn sich der Baum ändert
- **Details-Caching**: Properties nur neu bauen, wenn ein anderes Element selektiert wird

### 7.4 Fehlerbehandlung & Feedback
- **Toast-Nachrichten**: Bei Speichern, Fehler, Warnung
- **Validierung**: Warnung bei fehlenden Pflichtfeldern (z.B. Text ohne Font)
- **Auto-Save**: Optionaler Auto-Save alle X Minuten
- **Recover**: Ungespeicherte Änderungen beim Schließen warnen

### 7.5 Dokumentation & Onboarding
- **Tooltip-Texte**: Jede Property im Details-Panel bekommt einen Tooltip mit Erklärung
- **Willkommens-Overlay**: Beim ersten Öffnen eines leeren Widgets Tipps anzeigen
- **Tastenkürzel-Übersicht**: `?` oder F1 → Dialog mit allen Shortcuts

---

## 8. UI-Referenzlayout

```
┌─────────────────────────────────────────────────────────────────────┐
│  [Save] [Undo] [Redo] │ Zoom: 100% [Fit] │ Resolution: 1920×1080  │ Toolbar
├──────────────┬──────────────────────────────────┬───────────────────┤
│  ▾ Basis     │                                  │  Identity         │
│    📋 Panel  │      ┌────────────────────┐      │  Type: Button     │
│    📝 Text   │      │   Preview Canvas   │      │  ID: [myBtn    ]  │
│    🏷 Label  │      │                    │      │─────────────────  │
│    🖼 Image  │      │   ┌──────────┐     │      │  Transform        │
│    ── Sep.   │      │   │ Button ▣ │     │      │  From X: [0.30 ]  │
│  ▾ Eingabe   │      │   └──────────┘     │      │  From Y: [0.40 ]  │
│    🔘 Button │      │                    │      │  To X:   [0.70 ]  │
│    ☑ Check   │      └────────────────────┘      │  To Y:   [0.55 ]  │
│    ▼ DropDn  │                                  │─────────────────  │
│    ▐ Slider  │                                  │  Layout           │
│  ▾ Container │                                  │  H Align: [Fill▼] │
│    ≡ Stack   │                                  │  V Align: [Top ▼] │
│    ⊞ Grid    │                                  │  Min W:   [0    ] │
│  ─────────── │                                  │  Min H:   [0    ] │
│  Hierarchy   │                                  │─────────────────  │
│  ▾ Root      │                                  │  Appearance       │
│    ▾ Stack   │                                  │  Color: [■ RGBA]  │
│      ├ Text  │                                  │  Opacity: [1.0 ]  │
│      └ Btn ← │ (selected)                       │  Visible: ☑       │
│              │                                  │─────────────────  │
│              │                                  │  Text             │
│              │                                  │  Text: [Button ]  │
│              │                                  │  Font Size: [14]  │
├──────────────┴──────────────────────────────────┴───────────────────┤
│  Status: Widget: myHUD.widget (modified*)                           │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 9. Priorisierung

| Priorität | Feature | Phase | Aufwand | Nutzen |
|---|---|---|---|---|
| 🔴 Hoch | Kontextmenü Hierarchie | 1 | Mittel | Hoch |
| 🔴 Hoch | Direkte Manipulation im Canvas | 2 | Hoch | Sehr hoch |
| 🔴 Hoch | Undo/Redo | 3 | Mittel | Sehr hoch |
| 🟡 Mittel | Verbessertes Drag & Drop | 1 | Mittel | Hoch |
| 🟡 Mittel | Auflösungs-Preview | 2 | Niedrig | Hoch |
| 🟡 Mittel | Copy/Paste | 3 | Niedrig | Hoch |
| 🟡 Mittel | Kategorisierte Kontrollliste | 1 | Niedrig | Mittel |
| 🟡 Mittel | Snap & Hilfslinien | 2 | Mittel | Hoch |
| 🟢 Niedrig | Mehrfachauswahl | 3 | Hoch | Mittel |
| 🟢 Niedrig | Vorlagen-System | 3 | Mittel | Hoch |
| 🟢 Niedrig | Animationsvorschau | 4 | Hoch | Mittel |
| 🟢 Niedrig | Style-System | 4 | Hoch | Hoch |
| 🟢 Niedrig | Daten-Binding-Vorschau | 4 | Mittel | Mittel |
| 🔵 Zukunft | Responsive Breakpoints | 4 | Sehr hoch | Hoch |
| 🔵 Zukunft | Event-Visualisierung | 4 | Mittel | Mittel |
| 🔵 Zukunft | Auto-Save | 5 | Niedrig | Mittel |
| 🔵 Zukunft | Tooltip-Dokumentation | 5 | Niedrig | Mittel |

---

### Empfohlene Implementierungsreihenfolge

1. **Sofort** (Phase 1): Kontextmenü, verbessertes DnD, Kategorien in Kontrollliste
2. **Kurz** (Phase 2): Canvas-Manipulation, Auflösungs-Preview, Snap-to-Grid
3. **Mittel** (Phase 3): Undo/Redo, Copy/Paste, Suche
4. **Lang** (Phase 4): Templates, Styles, Animationen, Responsive
5. **Fortlaufend** (Phase 5): Polish, Performance, Doku
