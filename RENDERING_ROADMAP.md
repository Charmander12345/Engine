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

## ~~4. Shader Hot-Reload~~ ✅
**Aufwand:** Gering · **Nutzen:** Mittel

`ShaderHotReload`-Klasse überwacht das `shaders/`-Verzeichnis per `std::filesystem::last_write_time` (500 ms Poll-Intervall). Bei Änderung werden automatisch alle betroffenen Shader-Programme neu kompiliert und gelinkt: Material-Cache (`s_materialCache`) und RenderResourceManager-Cache werden invalidiert, UI-Quad-Programme gelöscht und neu erstellt, PostProcessStack-Programme (Resolve, Bloom, SSAO) über `reloadPrograms()` neu gebaut, und die Render-Entries komplett aus dem ECS neu aufgebaut. Kein Engine-Neustart nötig.

---

## ~~5. Transparenz / Order-Independent Transparency~~ ✅
**Aufwand:** Mittel · **Nutzen:** Mittel

Weighted Blended OIT (McGuire & Bavoil 2013) implementiert. Draw-Liste wird automatisch in opake und transparente Objekte partitioniert (Auto-Detect über RGBA-Diffuse-Textur mit 4 Kanälen). Opaker Pass rendert normal mit Depth-Write, danach OIT Transparent Pass in separates FBO (RGBA16F Accumulation + R8 Revealage) mit Per-Attachment-Blending (`glBlendFunci`), Depth-Read ohne Write. Depth-Buffer wird per `glBlitFramebuffer` vom HDR-FBO übernommen. Abschließend Fullscreen-Composite-Pass über die opake Szene. OIT toggle über `setOitEnabled()`. Fragment-Shader unterstützt `uOitEnabled`-Uniform für dualen MRT-Output (`layout(location=0/1)`).

---

## ~~6. LOD-System (Level of Detail)~~ ✅
**Aufwand:** Mittel · **Nutzen:** Hoch

`LodComponent` im ECS ermöglicht pro Entity mehrere Mesh-Varianten mit aufsteigenden `maxDistance`-Schwellwerten. Alle LOD-Meshes werden beim Scene-Prepare geladen. Im Render-Loop wird die Kamera-Distanz zum Objekt berechnet und das passende Mesh ausgewählt. Letzte Stufe (`maxDistance <= 0`) dient als Fallback für maximale Entfernung. LOD-Daten werden mit dem Level serialisiert/deserialisiert (JSON „Lod“-Komponente).

---

## ~~7. Skeletal Animation~~ ✅
**Aufwand:** Hoch · **Nutzen:** Hoch

Komplettfeature implementiert: Bone-Hierarchie wird beim Assimp-Import extrahiert (`aiProcess_LimitBoneWeights`), inklusive Offset-Matrizen, Node-Hierarchie und Animation-Keyframes. Skinning-Shader (`skinned_vertex.glsl`) mit `uBoneMatrices[128]`-Palette im Vertex-Shader. `SkeletalAnimator` (SkeletalData.h) für Runtime-Playback mit Keyframe-Interpolation (Lerp/Slerp). `AnimationComponent` im ECS (12. Komponente). Erweiterter Vertex-Layout (22 Floats: +boneIds4+boneWeights4). Shadow-Passes mit Skinning-Support. Auto-Play der ersten Animation pro Skinned-Entity.

---

## ~~8. Material-Instancing / Overrides~~ ✅
**Aufwand:** Mittel · **Nutzen:** Mittel

Per-Entity-Material-Overrides (Farb-Tint, Metallic, Roughness, Shininess, Emissive) ohne das Basis-Material zu duplizieren. `MaterialOverrides`-Struct in `MaterialComponent` speichert optionale Overrides mit `hasXxx`-Flags. Renderer wendet Overrides per Draw-Call an; Entities mit Overrides brechen GPU-Instancing-Batches auf. Vollständig serialisiert (JSON „MaterialOverrides") und per Python-Scripting-API (`entity.set_material_override_*`/`entity.get_material_override_*`) steuerbar.

---

## 9. Particle-System
**Aufwand:** Hoch · **Nutzen:** Mittel

GPU-Compute- oder Transform-Feedback-basierte Partikel für Feuer, Rauch, Funken, Magie. Benötigt eigenen Emitter-Component, Render-Pass mit Billboarding und ggf. Depth-Soft-Particles. Setzt funktionierendes Alpha-Blending / OIT voraus (→ Punkt 5).

---

## ~~10. Kamera-Überblendung~~ ✅
**Aufwand:** Gering · **Nutzen:** Gering–Mittel

Smooth-Interpolation zwischen zwei Kamera-Positionen/-Orientierungen über eine konfigurierbare Dauer. `CameraTransition`-Struct in `Renderer.h` mit Start-/End-Pos/Rot, Duration und Elapsed. `OpenGLRenderer` implementiert `startCameraTransition()`, `isCameraTransitioning()`, `cancelCameraTransition()` mit Smooth-Step-Easing (3t²−2t³). Während einer Transition sind `moveCamera()` und `rotateCamera()` blockiert. Python-API: `engine.camera.transition_to(x,y,z,yaw,pitch,duration)`, `engine.camera.is_transitioning()`, `engine.camera.cancel_transition()`.

---

## 11. Texture Compression (S3TC/BC)
**Aufwand:** Mittel · **Nutzen:** Mittel

GPU-native Kompression (BC1–BC7) reduziert VRAM-Verbrauch um 4–6× bei minimalem Qualitätsverlust. Benötigt Offline-Konvertierung (z.B. via `texconv`) und angepassten Upload-Pfad (`glCompressedTexImage2D`). Wichtig erst bei großen Szenen mit vielen hochauflösenden Texturen.

---

## 12. Texture-Streaming
**Aufwand:** Hoch · **Nutzen:** Mittel

Lädt Texturen nach Bedarf und Detailstufe (Mip-Streaming). Reduziert Startup-Ladezeiten und Peak-VRAM. Komplex: Async-Upload-Queue, Mip-Tail-Resident, Feedback-Buffer. Erst sinnvoll nach Texture Compression (→ Punkt 11).

---

## ~~13. Shader-Variants / Permutationen~~ ✅
**Aufwand:** Mittel · **Nutzen:** Gering–Mittel

Präprozessor-basierte Shader-Varianten implementiert. `ShaderVariantKey` (Bitmask mit 8 Feature-Flags: `HAS_DIFFUSE_MAP`, `HAS_SPECULAR_MAP`, `HAS_NORMAL_MAP`, `HAS_EMISSIVE_MAP`, `HAS_METALLIC_ROUGHNESS_MAP`, `PBR_ENABLED`, `FOG_ENABLED`, `OIT_ENABLED`). `buildVariantDefines()` generiert `#define`-Block der nach `#version` injiziert wird. `OpenGLShader::loadFromFileWithDefines()` kompiliert Shader mit Defines. Fragment-Shader nutzt `#ifdef`/`#else`-Guards für alle Textur-Sampling- und Feature-Branches (Diffuse, Specular, Normal, Emissive, Fog, OIT) – bei gesetztem Define wird der Branch eliminiert, ohne Define greift der bestehende Uniform-Fallback. `OpenGLMaterial::setVariantKey()` rekompiliert Fragment-Shader und relinkt Programm mit neuem Variant-Key. `cacheUniformLocations()` als eigenständige Methode extrahiert für Relink. `OpenGLObject3D::setTextures()` berechnet automatisch den Variant-Key aus aktiven Textur-Slots und aktiviert die passende Permutation.

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
