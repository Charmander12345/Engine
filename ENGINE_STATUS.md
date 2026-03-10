# Engine – Status & Roadmap

> Übersicht über den aktuellen Implementierungsstand und offene Punkte – pro Modul gegliedert.
> Branch: `Json_and_ecs` | Stand: aktuell

---

## Letzte Änderung (Viewport)

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
| Screen-to-World (Depth-Buffer Unproject)  | ✅     |
| Selection-Outline (Edge-Detection)        | ✅     |
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
| Mesh-Viewer-Tabs (Doppelklick auf Model3D) | ✅ |
| Widget-Editor-Tabs (Doppelklick auf Widget, FBO-Preview + Zoom/Pan + Outline-Selektion + Hierarchie + Details) | ✅ |
| **Tab-Scoped UI** (Viewport-Widgets + ContentBrowser nur bei Viewport-Tab, Mesh-Viewer-Props nur bei deren Tab) | ✅ |
| **Level-Swap bei Tab-Wechsel** (`swapActiveLevel` + Camera Save/Restore) | ✅ |
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
- Content Browser: "New Level" erstellt leeres Level-Asset (`.map`)
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
| **Per-Tab Entity-Selektion** (Selection-State wird beim Tab-Wechsel gespeichert/wiederhergestellt) | ✅ |
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
| Script Hot-Reload                   | ❌     |

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
- Kein Hot-Reload bei Script-Änderung (nur bei PIE-Neustart)
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
| **Drag & Drop (Editor)**        | ✅     | Model3D→Spawn (Depth-Raycast + Undo/Redo), Material/Script→Apply (pickEntityAtImmediate), Asset-Move mit tiefem Referenz-Scan aller .asset-Dateien, Entf zum Löschen (mit Undo/Redo), EntityDetails Drop-Zones mit Typ-Validierung |
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
| **Undo/Redo**                    | ✅     | Command-Pattern für Editor-Aktionen (UndoRedoManager-Singleton, Ctrl+Z/Y, StatusBar-Buttons). Entity-Löschen (DELETE) mit vollständigem Komponenten-Snapshot, Entity-Spawn (Drag-and-Drop) und Landscape-Erstellung erzeugen Undo/Redo-Actions |
| **Editor-Gizmos**               | ✅     | Translate/Rotate/Scale-Gizmos für Entity-Manipulation (W/E/R Shortcuts)      |
| **Shadow Mapping (Dir/Spot)**    | ✅     | Multi-Light Shadow Maps für bis zu 4 Directional/Spot Lights, 5×5 PCF       |
| **Shadow Mapping (Point Lights)**| ✅     | Omnidirektionale Cube-Map Shadows für bis zu 4 Point Lights via Geometry-Shader |
| **Popup-UI Refactoring**         | ✅     | Landscape-Manager- und Engine-Settings-Popup-Erstellung aus `main.cpp` in `UIManager` verschoben (`openLandscapeManagerPopup`, `openEngineSettingsPopup`). UIManager hält jetzt einen Back-Pointer auf `OpenGLRenderer`. |
| **Performance-Optimierungen**    | ✅     | O(1)-Asset-Lookup via `m_loadedAssetsByPath`-Index (statt O(n)-Scan), Shader-Pfad-Cache in `OpenGLObject3D`, deduplizierte Model-Matrix-Berechnung in `renderWorld()`. |
| **Paralleles Asset-Laden**       | ✅     | Dreiphasen-Architektur: `readAssetFromDisk()` (thread-safe Disk-I/O + CPU), `finalizeAssetLoad()` (Registration), GPU-Upload. Thread-Pool mit `hardware_concurrency()` Threads + globaler Job-Queue. `loadBatchParallel()` dispatched in den Pool mit Batch-Wait (atomic counter + CV). `preloadLevelAssets()` warmed den Cache beim Scene-Prepare mit allen Mesh-, Material- und Textur-Assets. |
| **Physik-System (Jolt)**         | ✅     | `PhysicsWorld`-Singleton mit Backend-Abstraktion (`IPhysicsBackend`). `JoltBackend` (Jolt Physics v5.5.1). Zwei ECS-Komponenten: `CollisionComponent` + `PhysicsComponent`. BodyDesc/BodyState für backend-agnostische Body-Verwaltung. ECS↔Backend-Sync in PhysicsWorld, alle Jolt-spezifischen Typen in JoltBackend isoliert. |
| **Physik-System (PhysX)**        | ✅     | `PhysXBackend` (NVIDIA PhysX 5.6.1, `external/PhysX/`). Optional via `ENGINE_PHYSX_BACKEND` CMake-Option. Kontakt-Callbacks (`SimCallbackImpl`), Euler↔Quat-Konvertierung, PVD-Support. `PhysicsWorld::Backend`-Enum (Jolt/PhysX) für Backend-Auswahl bei `initialize()`. |

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

---

*Generiert aus Analyse des Quellcodes. Stand: aktueller Branch `Json_and_ecs`.*
