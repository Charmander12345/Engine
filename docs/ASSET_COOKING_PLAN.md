# Asset-Cooking-Pipeline – Implementierungsplan (Phase 10.2)

> **Ziel:** Beim Build werden Assets in ein optimiertes Runtime-Format konvertiert.
> Texturen werden komprimiert, Meshes in ein kompaktes Binärformat überführt und
> unnötige Editor-Metadaten entfernt. Das Ergebnis: **kleinere Builds, schnelleres Laden, weniger RAM.**

---

## Inhaltsverzeichnis

1. [Ist-Zustand](#1-ist-zustand)
2. [Architektur-Übersicht](#2-architektur-übersicht)
3. [Cooked-Asset-Formate](#3-cooked-asset-formate)
4. [Neue Dateien & Klassen](#4-neue-dateien--klassen)
5. [Implementierungsschritte](#5-implementierungsschritte)
6. [Integration in die Build-Pipeline](#6-integration-in-die-build-pipeline)
7. [Runtime-Loader-Anpassungen](#7-runtime-loader-anpassungen)
8. [Inkrementelles Cooking](#8-inkrementelles-cooking)
9. [Profil-abhängiges Verhalten](#9-profil-abhängiges-verhalten)
10. [Reihenfolge & Aufwand](#10-reihenfolge--aufwand)

---

## 1. Ist-Zustand

### Was passiert aktuell beim Build (Step 5)?
```
<Projekt>/Content/ ──(rekursive Kopie)──► <Build>/Content/
<Engine>/Content/  ──(rekursive Kopie)──► <Build>/Content/   (skip_existing)
<Engine>/shaders/  ──(rekursive Kopie)──► <Build>/shaders/
Config/AssetRegistry.bin ──(Kopie)──► <Build>/Config/
```
→ **Rohe .asset-Dateien (JSON) werden 1:1 kopiert.**

### Aktuelle Asset-Formate (Editor/Disk)
| Asset-Typ | Format auf Disk | Runtime-Verarbeitung |
|-----------|----------------|---------------------|
| **Texture** | `.asset` (JSON-Metadaten) + separate Bilddatei (PNG/JPG/DDS) via `m_sourcePath` | `stbi_load()` oder `loadDDS()` zur Laufzeit |
| **Model3D** | `.asset` (JSON mit `m_vertices[]`, `m_indices[]`, `m_boneIds[]`, `m_boneWeights[]` als Float-Arrays) | JSON-Parse → `ReadFloatArray()` → `BuildVerticesWithFlatNormals()` → GPU-Upload |
| **Material** | `.asset` (JSON mit Textur-Referenzen, Shader-Pfaden, Parametern) | JSON-Parse, Textur-Referenzen aufgelöst |
| **Audio** | `.asset` (JSON-Metadaten) + separate `.wav`-Datei via `m_sourcePath` | SDL_Audio lädt WAV direkt |
| **Level** | `.asset` (JSON mit Entity-Definitionen, Komponenten, Szenen-Daten) | JSON-Parse → ECS-Aufbau |
| **Widget** | `.asset` (JSON mit UI-Hierarchie) | JSON-Parse → Widget-Aufbau |
| **Skybox** | `.asset` (JSON mit 6 Textur-Pfaden) | Texturen via stbi/DDS geladen |
| **Script** | `.py`-Datei direkt | Python-Interpreter führt direkt aus |
| **Shader** | `.glsl`-Datei direkt | OpenGL kompiliert zur Laufzeit |
| **Prefab** | `.asset` (JSON mit Entity-Template) | JSON-Parse |

### Vorhandene Infrastruktur (können wir nutzen)
- **`DDSLoader`** – Liest BC1–BC7 DDS-Dateien → `Texture`-Objekt mit `CompressedMipLevel`
- **`CompressedFormat`-Enum** – `None/BC1/BC1A/BC2/BC3/BC4/BC5/BC7`
- **`Texture::m_compressedMips`** – Vektor von Mip-Levels mit Rohformat-Daten
- **`OpenGLRenderer`** – Kann bereits komprimierte Texturen hochladen (glCompressedTexImage2D)
- **`readAssetFromDisk()`** – Statische, thread-sichere Lese-Funktion
- **`BuildProfile::compressAssets`** – Flag existiert bereits in den Build-Profilen
- **Thread-Pool** im `AssetManager` – Kann für paralleles Cooking genutzt werden

---

## 2. Architektur-Übersicht

```
Editor-Assets (.asset JSON + Quelldateien)
    │
    ▼
┌──────────────────────────────────┐
│         AssetCooker              │   ← Neue Klasse
│  ┌────────────┐ ┌─────────────┐ │
│  │TextureCook │ │  MeshCook   │ │
│  │ PNG→DDS    │ │ JSON→Binary │ │
│  └────────────┘ └─────────────┘ │
│  ┌────────────┐ ┌─────────────┐ │
│  │MaterialCook│ │  LevelCook  │ │
│  │ Strip Meta │ │ Strip Meta  │ │
│  └────────────┘ └─────────────┘ │
│  ┌────────────┐ ┌─────────────┐ │
│  │ AudioCook  │ │GenericCopy  │ │
│  │  WAV→WAV   │ │Script/Shader│ │
│  └────────────┘ └─────────────┘ │
│         ┌──────────────┐        │
│         │CookManifest  │        │
│         │ manifest.json│        │
│         └──────────────┘        │
└──────────────────────────────────┘
    │
    ▼
Cooked-Assets (.cooked Binär + optimierte Dateien)
    │
    ▼
<Build>/Content/ + <Build>/Content/manifest.json
```

---

## 3. Cooked-Asset-Formate

### 3.1 Texturen (PNG/JPG → DDS mit Mipmaps)

**Eingabe:** `.asset` JSON + separate PNG/JPG/TGA-Datei (`m_sourcePath`)

**Ausgabe:** `.dds` (BC3 für RGBA, BC1 für RGB ohne Alpha, BC7 für High-Quality bei Shipping)

**Ablauf:**
1. `stbi_load()` → Rohpixel (RGBA)
2. Mipmap-Kette generieren (Box-Filter, `floor(log2(max(w,h)))+1` Stufen)
3. Jedes Mip-Level per BCn komprimieren (über eine Software-Bibliothek, z.B. `stb_dxt.h` für BC1/BC3 oder `bc7enc` für BC7)
4. DDS-Header schreiben + Mip-Daten anhängen
5. `.cooked`-Metadaten-Datei schreiben (oder `.dds` direkt mit Pfad-Mapping im Manifest)

**Kompressionsformat-Auswahl:**
| Quell-Format | Profile Debug/Dev | Profile Shipping |
|-------------|-------------------|------------------|
| RGB (3ch)   | BC1 (schnell)     | BC7 (Qualität)   |
| RGBA (4ch)  | BC3 (schnell)     | BC7 (Qualität)   |
| Greyscale   | BC4               | BC4              |
| Normal-Map  | BC5               | BC5              |

**Bereits DDS:** Wird 1:1 kopiert (bereits optimiert).

### 3.2 Meshes (JSON Float-Arrays → Binär)

**Eingabe:** `.asset` JSON mit `m_vertices`, `m_indices`, `m_boneIds`, `m_boneWeights`

**Ausgabe:** `.cooked` Binärdatei

**Binärformat:**
```
[CookedMeshHeader]           (feste Größe)
  magic:         uint32    = 0x434D5348 ("CMSH")
  version:       uint32    = 1
  vertexCount:   uint32
  indexCount:     uint32
  vertexStride:  uint32    (Bytes pro Vertex: 56 für Standard, 88 für Skinned)
  flags:         uint32    (Bit 0: hasBones, Bit 1: hasNormals, ...)
  boundsMin:     float[3]
  boundsMax:     float[3]

[Vertex-Daten]               (vertexCount × vertexStride Bytes)
  Bereits in GPU-fertigem Layout:
  Standard: pos(3f) + normal(3f) + uv(2f) + tangent(3f) + bitangent(3f) = 14 floats = 56 Bytes
  Skinned:  Standard + boneIds(4f) + boneWeights(4f) = 22 floats = 88 Bytes

[Index-Daten]                (indexCount × 4 Bytes, uint32)
```

**Vorteil:** Kein JSON-Parsing, kein `ReadFloatArray()`, kein `BuildVerticesWithFlatNormals()` zur Laufzeit. Daten können direkt per `memcpy` in den GPU-Buffer. ~10-50× schnelleres Laden für große Meshes.

### 3.3 Materialien (JSON → Stripped JSON)

**Eingabe:** `.asset` JSON mit Textur-Pfaden, Shader-Overrides, Parametern

**Ausgabe:** `.cooked` JSON (kompakt, ohne Editor-Metadaten)

**Änderungen:**
- Editor-only Felder entfernen (z.B. `m_editorNotes`, Import-History)
- Textur-Pfade auf Cooked-Pfade umschreiben (`.png` → `.dds`)
- JSON kompakt serialisieren (kein Pretty-Print, spart ~30-40% Dateigröße)

### 3.4 Levels (JSON → Stripped JSON)

**Eingabe:** `.asset` JSON mit Entity-Definitionen, Editor-Kamera-Position, etc.

**Ausgabe:** `.cooked` JSON

**Änderungen:**
- Editor-Camera-Daten entfernen
- Editor-only Entity-Marker entfernen
- Asset-Pfade auf Cooked-Pfade umschreiben
- Kompakt serialisieren

### 3.5 Audio (WAV → WAV Kopie)

**Eingabe:** `.asset` JSON + separate `.wav`-Datei

**Ausgabe:** `.wav` direkt (keine Konvertierung nötig, SDL_Audio erwartet WAV)

**Zukunft:** Optional OGG/Opus-Kompression (Phase 10.2.1 – nicht im Erstrelease).

### 3.6 Scripts & Shaders (1:1 Kopie)

Python-Scripts (`.py`) und GLSL-Shaders (`.glsl`) werden 1:1 kopiert.
**Zukunft:** Shader-Vorkompilierung (SPIR-V) als separate Phase.

### 3.7 Widgets, Skyboxes, Prefabs (JSON → Stripped JSON)

Wie Materialien/Levels: Editor-Metadaten entfernen, Pfade umschreiben, kompakt serialisieren.

---

## 4. Neue Dateien & Klassen

```
src/
  AssetManager/
    AssetCooker.h          ← Neue Datei: Hauptklasse
    AssetCooker.cpp         ← Neue Datei: Implementierung
    CookManifest.h         ← Neue Datei: Manifest-Struktur
    CookManifest.cpp       ← Neue Datei: Manifest lesen/schreiben
```

### 4.1 `AssetCooker` (Hauptklasse)

```cpp
// Konzept-Skizze (nicht finaler Code)
class AssetCooker
{
public:
    struct CookConfig
    {
        std::string projectRoot;           // <Projekt>-Pfad
        std::string engineRoot;            // Engine-Installationspfad
        std::string outputDir;             // <Build>/Content/
        bool compressAssets;               // Aus BuildProfile
        std::string buildType;             // "Debug" / "Release" / "RelWithDebInfo"
        int maxThreads;                    // Thread-Pool-Größe
    };

    struct CookResult
    {
        bool success;
        size_t totalAssets;
        size_t cookedAssets;
        size_t skippedAssets;              // (inkrementell – unverändert)
        size_t failedAssets;
        std::vector<std::string> errors;
        double elapsedSeconds;
    };

    // Alle Assets kochen (mit Progress-Callback)
    CookResult cookAll(const CookConfig& config,
                       std::function<void(size_t done, size_t total, const std::string& current)> onProgress = {},
                       std::atomic<bool>* cancelFlag = nullptr);

private:
    bool cookTexture(const AssetRegistryEntry& entry, const CookConfig& config);
    bool cookMesh(const AssetRegistryEntry& entry, const CookConfig& config);
    bool cookMaterial(const AssetRegistryEntry& entry, const CookConfig& config);
    bool cookLevel(const AssetRegistryEntry& entry, const CookConfig& config);
    bool cookAudio(const AssetRegistryEntry& entry, const CookConfig& config);
    bool cookWidget(const AssetRegistryEntry& entry, const CookConfig& config);
    bool cookSkybox(const AssetRegistryEntry& entry, const CookConfig& config);
    bool copyAsset(const AssetRegistryEntry& entry, const CookConfig& config);

    CookManifest m_manifest;
};
```

### 4.2 `CookManifest`

```cpp
// Konzept-Skizze
struct CookManifestEntry
{
    std::string originalPath;    // Content-relativer Pfad der Quelldatei
    std::string cookedPath;      // Content-relativer Pfad im Build
    AssetType type;
    uint64_t sourceHash;         // Für inkrementelles Cooking
    size_t cookedSize;           // In Bytes
};

class CookManifest
{
public:
    void addEntry(const CookManifestEntry& entry);
    bool saveToFile(const std::string& path) const;       // manifest.json
    bool loadFromFile(const std::string& path);
    const CookManifestEntry* findByOriginalPath(const std::string& path) const;
    const std::vector<CookManifestEntry>& entries() const;

private:
    std::vector<CookManifestEntry> m_entries;
};
```

---

## 5. Implementierungsschritte

### Schritt 1: Grundgerüst (AssetCooker + CookManifest)
- `AssetCooker.h/cpp` und `CookManifest.h/cpp` erstellen
- `cookAll()` iteriert über `AssetManager::getAssetRegistry()`
- Dispatch per `AssetType` auf die jeweilige `cook*`-Methode
- Fallback: `copyAsset()` kopiert unverändert
- `manifest.json` wird am Ende geschrieben
- CMakeLists.txt anpassen (neue Dateien hinzufügen)

### Schritt 2: Textur-Cooking
- `stb_dxt.h` (Header-only, Public Domain) einbinden für BC1/BC3-Kompression
- Mipmap-Generator implementieren (einfacher Box-Filter)
- DDS-Writer implementieren (DDS-Header + Mip-Chain)
- `cookTexture()`: Quellbild laden → Mipmaps → BCn komprimieren → DDS schreiben
- Bereits vorhandene DDS-Dateien 1:1 kopieren

### Schritt 3: Mesh-Cooking
- `.cooked` Binärformat definieren (CMSH-Header + Vertex-Daten + Index-Daten)
- `cookMesh()`: JSON laden → `ReadFloatArray()` + `BuildVerticesWithFlatNormals()` → Binär schreiben
- Bounds (AABB) im Header vorberechnen

### Schritt 4: Material/Level/Widget/Skybox-Cooking
- JSON laden, Editor-only Felder entfernen
- Textur-Pfade auf `.dds` umschreiben
- Kompakt (ohne Indentation) serialisieren

### Schritt 5: Audio/Script/Shader-Kopie
- WAV-Quelldateien direkt kopieren
- `.py` und `.glsl` 1:1 kopieren

### Schritt 6: Build-Pipeline-Integration
- Step 5 in `main.cpp` von "Copy Content" auf "Cook Assets" umbauen
- `AssetCooker::cookAll()` aufrufen statt `std::filesystem::copy()`
- Progress-Callback an `appendBuildOutput()` weiterleiten
- Shaders weiterhin 1:1 kopieren (separate Logik)
- Engine-Content ebenfalls durch den Cooker verarbeiten

### Schritt 7: Runtime-Loader-Anpassungen
- `OpenGLObject3D::prepare()` → Cooked-Binärformat erkennen und laden (statt JSON → ReadFloatArray)
- Textur-Loading → `.dds` bevorzugen wenn vorhanden
- `AssetManager::readAssetFromDisk()` → Cooked-Format erkennen (Magic-Bytes)

### Schritt 8: Inkrementelles Cooking
- Beim Cooking: Source-File-Hash (z.B. xxHash oder einfacher CRC32) berechnen
- `manifest.json` aus vorherigem Build laden
- Nur Assets mit geändertem Hash oder fehlender Cooked-Datei neu kochen
- Deutlich schnellere Re-Builds

---

## 6. Integration in die Build-Pipeline

### Vorher (aktuell – Step 5):
```
Step 5: "Copying game content..."
  └── std::filesystem::copy(srcContent, dstContent, recursive | overwrite)
  └── std::filesystem::copy(srcShaders, dstShaders, recursive | overwrite)
  └── std::filesystem::copy(srcEngContent, dstContent, recursive | overwrite | skip_existing)
  └── Copy AssetRegistry.bin
```

### Nachher (mit Cooking):
```
Step 5: "Cooking assets..."  (umbenannt)
  └── AssetCooker cooker;
  └── CookConfig cfg { projectRoot, engineRoot, outputDir, profile.compressAssets, ... };
  └── cooker.cookAll(cfg, progressCallback, &cancelFlag);
      ├── Projekt-Content kochen → <Build>/Content/
      ├── Engine-Content kochen → <Build>/Content/ (skip_existing)
      └── manifest.json schreiben → <Build>/Content/manifest.json
  └── Shaders kopieren → <Build>/shaders/ (weiterhin 1:1)
  └── AssetRegistry.bin kopieren → <Build>/Config/
```

### kTotalSteps bleibt bei 7 (Step 5 wird ersetzt, nicht hinzugefügt).

---

## 7. Runtime-Loader-Anpassungen

### 7.1 Cooked-Mesh-Loader (OpenGLObject3D)
```
Aktuell:
  JSON parse → ReadFloatArray("m_vertices") → BuildVerticesWithFlatNormals() → GPU Upload

Nachher (Cooked):
  Binärdatei öffnen → CMSH-Header lesen → Vertex-Daten direkt in Buffer → GPU Upload
  (Kein JSON, kein Float-Parsing, kein Normal-Berechnung)
```

**Erkennung:** Erste 4 Bytes = `0x434D5348` ("CMSH") → Cooked-Pfad.
Andernfalls → Fallback auf JSON-Pfad (Editor-Modus).

### 7.2 Cooked-Texture-Loader
Texturen im Cooked-Build verweisen auf `.dds` statt `.png`.
Der bestehende `DDSLoader` kann diese bereits laden.
Im Material-JSON werden die Pfade auf `.dds` umgeschrieben.
→ **Kein neuer Loader nötig, `DDSLoader` reicht.**

### 7.3 Stripped-JSON-Loader
Materialien/Levels/Widgets/Skyboxes im Cooked-Format sind immer noch JSON,
nur kompakter. Der bestehende JSON-Loader funktioniert ohne Änderung.

---

## 8. Inkrementelles Cooking

### Strategie: Hash-basiert (nicht Timestamp)
Timestamps sind unzuverlässig (Git-Clone, Kopien, Zeitzonen).
→ **Datei-Hash** (CRC32 oder xxHash) des Quell-Assets + aller referenzierten Dateien.

### Ablauf:
1. Altes `manifest.json` aus `<Build>/Content/` laden (falls vorhanden)
2. Für jedes Asset in der Registry:
   a. Quelldatei(en) hashen (`.asset` + z.B. referenzierte `.png`)
   b. Hash mit Manifest-Eintrag vergleichen
   c. Wenn identisch UND Cooked-Datei existiert → **überspringen**
   d. Sonst → **kochen** und neuen Hash im Manifest speichern
3. Neues `manifest.json` schreiben

### Erwartete Beschleunigung: ~80-95% bei typischen Re-Builds (nur wenige Assets ändern sich).

---

## 9. Profil-abhängiges Verhalten

Das `BuildProfile`-Struct hat bereits ein `compressAssets`-Flag:

| Setting | Debug | Development | Shipping |
|---------|-------|-------------|----------|
| `compressAssets` | `false` | `false` | `true` |

### Auswirkung auf Cooking:
| Aktion | compressAssets = false | compressAssets = true |
|--------|----------------------|----------------------|
| **Texturen** | 1:1 Kopie (PNG/JPG/DDS) | PNG/JPG → DDS (BC1/BC3/BC7 mit Mipmaps) |
| **Meshes** | JSON → Binär (CMSH) | JSON → Binär (CMSH) – immer, da deutlich schneller |
| **Materialien** | Stripped JSON | Stripped JSON |
| **Levels** | Stripped JSON | Stripped JSON |
| **Audio** | 1:1 Kopie | 1:1 Kopie (Zukunft: OGG) |

→ **Meshes werden immer gecooked** (auch bei `compressAssets=false`), da das Binärformat rein vorteilhaft ist (schnelleres Laden, keine Qualitätseinbußen).
→ **Texturen werden nur bei `compressAssets=true` komprimiert**, da BCn verlustbehaftet ist und Debug-Builds Originaldaten behalten sollten.

---

## 10. Reihenfolge & Aufwand

| # | Schritt | Aufwand | Abhängigkeit |
|---|---------|---------|-------------|
| 1 | AssetCooker + CookManifest Grundgerüst | Klein | – |
| 2 | Mesh-Cooking (JSON → CMSH Binär) | Mittel | Schritt 1 |
| 3 | Runtime CMSH-Loader (OpenGLObject3D) | Mittel | Schritt 2 |
| 4 | Material/Level/Widget/Skybox Stripping | Klein | Schritt 1 |
| 5 | Textur-Cooking (PNG → DDS mit Mipmaps) | Hoch | Schritt 1, `stb_dxt.h` |
| 6 | Build-Pipeline-Integration (Step 5 ersetzen) | Mittel | Schritte 1-5 |
| 7 | Inkrementelles Cooking (Hash + Manifest) | Mittel | Schritt 6 |
| 8 | Audio/Script/Shader-Kopie-Logik | Klein | Schritt 1 |

### Empfohlene Implementierungsreihenfolge:
**1 → 2 → 3 → 4 → 8 → 6 → 5 → 7**

Begründung: Mesh-Cooking bringt den größten Performance-Gewinn (JSON→Binär),
ist einfacher als Textur-Cooking (kein BCn-Encoder nötig) und validiert das
Grundgerüst. Textur-Cooking und inkrementelles Cooking kommen danach.

### Geschätzter Gesamtaufwand: **Hoch** (mehrere Implementierungsrunden)

---

## Externe Abhängigkeiten

| Bibliothek | Zweck | Lizenz | Header-only? |
|-----------|-------|--------|-------------|
| `stb_dxt.h` | BC1/BC3 Block-Kompression | Public Domain | ✅ Ja |
| `stb_image_resize2.h` | Mipmap-Generierung (Downscaling) | Public Domain | ✅ Ja |

> `stb_dxt.h` und `stb_image_resize2.h` sind vom selben Autor wie das bereits
> verwendete `stb_image.h` und passen nahtlos in die bestehende Codebasis.

**Für BC7 (Shipping):** Optional `bc7enc_rdo` oder `ISPCTextureCompressor`.
Kann als spätere Erweiterung kommen – BC1/BC3 reichen für den Erstrelease.

---

## Offene Fragen / Entscheidungen

1. **Cooked-Format für Editor-Nutzung?**
   → Nein. Der Editor arbeitet immer mit den Roh-Assets. Cooking ist nur für Builds.

2. **Soll der Cooker als separates Executable laufen?**
   → Nein (vorerst). Er läuft in-process im Build-Thread. Ein separates Tool
   könnte später für CI/CD-Pipelines sinnvoll sein.

3. **Soll `AssetRegistry.bin` angepasst werden?**
   → Ja, minimal: Die Pfade im Registry für den Build sollten auf die Cooked-Pfade
   verweisen. Alternative: Das `manifest.json` ersetzt die Registry im Build.

4. **Shader-Vorkompilierung (SPIR-V)?**
   → Nicht in Phase 10.2. Bleibt für eine spätere Phase vorbehalten, da der
   aktuelle Renderer OpenGL-basiert ist und GLSL direkt kompiliert.
