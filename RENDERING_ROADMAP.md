# Rendering Roadmap – Fehlende Features

Priorisiert nach **Aufwand ↔ Nutzen** – oben = bestes Verhältnis, unten = hohes Risiko oder geringer Mehrwert.

---

## ~~1. Mipmaps systematisch aktivieren~~ ✅
**Aufwand:** Gering · **Nutzen:** Hoch

`glGenerateMipmap` wird jetzt konsequent für alle Bild-Texturen aufgerufen (Material-Texturen, Skybox-Cubemap, UI-Textur-Cache). Min-Filter auf `GL_LINEAR_MIPMAP_LINEAR` gesetzt. Framebuffer-/Shadow-/Depth-Texturen bewusst ausgenommen.

---

## ~~2. GPU Instanced Rendering~~ ✅
**Aufwand:** Mittel · **Nutzen:** Hoch

Draw-Liste wird jetzt nach (Material-Pointer, Obj-Pointer) sortiert und in Batches gruppiert. Nur Objekte mit gleichem Mesh UND gleichem Material (gleicher VAO, Shader, Texturen) werden per `glDrawElementsInstanced` / `glDrawArraysInstanced` in einem einzigen Draw-Call gerendert. SSBO-Management mit Buffer-Orphaning (`glBufferData(nullptr)` vor `glBufferSubData`), explizitem SSBO-Unbind nach jedem Instanced-Draw und `if/else`-Shader-Guard (statt Ternary) gegen spekulative SSBO-Zugriffe auf SIMD-GPUs.

---

## ~~3. Material-Editor (UI)~~ ✅
**Aufwand:** Mittel · **Nutzen:** Hoch

Popup-basierter Material-Editor (`openMaterialEditorPopup`) mit Material-Auswahl per Dropdown, PBR-Parameter-Editing (Metallic, Roughness, Shininess als Slider, PBR-Enabled-Checkbox), Textur-Slot-Bearbeitung (Diffuse, Specular, Normal, Emissive, MetallicRoughness) und Save/Close-Buttons. Erreichbar über Content-Browser-Doppelklick auf Material-Assets und über den Tools-Bereich in World Settings. Nutzt EditorUIBuilder-Helpers und speichert Änderungen direkt ins Asset-JSON.

---

## 4. Shader Hot-Reload
**Aufwand:** Gering · **Nutzen:** Mittel

Filewatcher auf das `shaders/`-Verzeichnis + Re-Compile/Re-Link bei Änderung. Spart bei Shader-Entwicklung den Engine-Neustart. Grundlage: Shader-Programm-Cache existiert bereits und kann per Key invalidiert werden.

---

## 5. Transparenz / Order-Independent Transparency
**Aufwand:** Mittel · **Nutzen:** Mittel

Alpha-Sorting existiert rudimentär (🟡). Für korrekte Transparenz bei überlappenden Objekten wird OIT benötigt (z.B. Weighted Blended OIT oder Linked-List). Ohne OIT sind Fenster, Glas, Wasser, Partikel etc. fehlerhaft.

---

## 6. LOD-System (Level of Detail)
**Aufwand:** Mittel · **Nutzen:** Hoch

Automatischer Mesh-Wechsel basierend auf Kamera-Distanz. Reduziert Polygon-Last in großen Szenen drastisch. Benötigt LOD-Gruppen im Mesh-Asset und Distance-Thresholds im Render-Loop. Assimp importiert bereits LOD-Varianten wenn vorhanden.

---

## 7. Skeletal Animation
**Aufwand:** Hoch · **Nutzen:** Hoch

Komplettfeature: Bone-Hierarchie parsen (Assimp liefert sie bereits), Skinning-Shader (Bone-Matrix-Palette im Vertex-Shader), Animation-Blending, AnimationComponent im ECS. Größtes fehlendes Render-Feature – ohne Skeletal Animation sind animierte Charaktere/Objekte nicht möglich.

---

## 8. Material-Instancing / Overrides
**Aufwand:** Mittel · **Nutzen:** Mittel

Erlaubt pro Entity individuelle Parameter-Overrides (z.B. Farbe, Roughness) ohne das Basis-Material zu duplizieren. Reduziert Asset-Anzahl und ermöglicht Runtime-Variation. Benötigt eine `MaterialInstance`-Schicht zwischen Asset und Render-Call.

---

## 9. Particle-System
**Aufwand:** Hoch · **Nutzen:** Mittel

GPU-Compute- oder Transform-Feedback-basierte Partikel für Feuer, Rauch, Funken, Magie. Benötigt eigenen Emitter-Component, Render-Pass mit Billboarding und ggf. Depth-Soft-Particles. Setzt funktionierendes Alpha-Blending / OIT voraus (→ Punkt 5).

---

## 10. Kamera-Überblendung
**Aufwand:** Gering · **Nutzen:** Gering–Mittel

Smooth-Interpolation zwischen zwei Kamera-Positionen/-Orientierungen über eine konfigurierbare Dauer. Nützlich für Cutscenes und Editor-Kamera-Transitions. Geringe Code-Menge (Lerp/Slerp im Kamera-Update), aber nur relevant wenn Cinematic-Content geplant ist.

---

## 11. Texture Compression (S3TC/BC)
**Aufwand:** Mittel · **Nutzen:** Mittel

GPU-native Kompression (BC1–BC7) reduziert VRAM-Verbrauch um 4–6× bei minimalem Qualitätsverlust. Benötigt Offline-Konvertierung (z.B. via `texconv`) und angepassten Upload-Pfad (`glCompressedTexImage2D`). Wichtig erst bei großen Szenen mit vielen hochauflösenden Texturen.

---

## 12. Texture-Streaming
**Aufwand:** Hoch · **Nutzen:** Mittel

Lädt Texturen nach Bedarf und Detailstufe (Mip-Streaming). Reduziert Startup-Ladezeiten und Peak-VRAM. Komplex: Async-Upload-Queue, Mip-Tail-Resident, Feedback-Buffer. Erst sinnvoll nach Texture Compression (→ Punkt 11).

---

## 13. Shader-Variants / Permutationen
**Aufwand:** Mittel · **Nutzen:** Gering–Mittel

Präprozessor-basierte Shader-Varianten (`#define HAS_NORMAL_MAP`, `#define USE_PBR`) statt Runtime-If-Branching. Verbessert GPU-Performance durch Branch-Elimination. Erfordert Variant-Registry und Cache-Management. Aktuell kein Bottleneck – eher relevant bei 50+ Materialtypen.

---

## 14. Cinematic-Kamera / Pfad-Follow
**Aufwand:** Mittel · **Nutzen:** Gering

Spline-basierte Kamera-Pfade (Catmull-Rom/Bézier) mit Editor-UI zum Platzieren von Kontrollpunkten. Rein Content-getrieben – nur nützlich wenn Cutscene-Workflow gebraucht wird.

---

## 15. Displacement Mapping
**Aufwand:** Mittel · **Nutzen:** Gering

Tessellation-Shader (Hull/Domain) für echte Geometrie-Verformung basierend auf Heightmaps. Visuell beeindruckend bei Terrain-Nahansichten, aber hoher GPU-Aufwand und nur in speziellen Szenarien vorteilhaft. Landscape-System nutzt bereits HeightField-Geometrie.

---

## 16. DirectX 11/12 / Vulkan Backend
**Aufwand:** Sehr hoch · **Nutzen:** Gering (aktuell)

Renderer-Abstraktion ist vorbereitet (Interfaces, Factory), aber ein zweites Backend ist ein eigenständiges Großprojekt (jeweils 5.000–15.000 Zeilen). Solange OpenGL 4.6 die Zielplattform abdeckt, kein dringender Bedarf.
