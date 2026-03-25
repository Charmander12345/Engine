#pragma once

#include <cstdint>

// ── Cooked Mesh Header (CMSH) — shared between editor cooker and runtime loader ──
static constexpr uint32_t CMSH_MAGIC   = 0x434D5348; // "CMSH"
static constexpr uint32_t CMSH_VERSION = 1;

enum CmshFlags : uint32_t
{
    CMSH_FLAG_HAS_BONES   = 1 << 0,
    CMSH_FLAG_HAS_NORMALS = 1 << 1,
};

struct CookedMeshHeader
{
    uint32_t magic{ CMSH_MAGIC };
    uint32_t version{ CMSH_VERSION };
    uint32_t vertexCount{ 0 };
    uint32_t indexCount{ 0 };       // always 0 – indices baked into vertex stream
    uint32_t vertexStride{ 0 };     // bytes per vertex (56 standard, 88 skinned)
    uint32_t flags{ 0 };
    float    boundsMin[3]{ 0.f, 0.f, 0.f };
    float    boundsMax[3]{ 0.f, 0.f, 0.f };
    uint32_t boneCount{ 0 };
    uint32_t animationCount{ 0 };
    uint32_t reserved[2]{ 0, 0 };   // future use
};

#if ENGINE_EDITOR

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include "AssetTypes.h"

struct AssetRegistryEntry;

// ── Cook Manifest ────────────────────────────────────────────────────────
struct CookManifestEntry
{
    std::string originalPath;   // Content-relative source path
    std::string cookedPath;     // Content-relative output path
    AssetType   type{ AssetType::Unknown };
    uint64_t    sourceHash{ 0 };
    size_t      cookedSize{ 0 };
};

class CookManifest
{
public:
    void addEntry(const CookManifestEntry& entry);
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);
    const CookManifestEntry* findByOriginalPath(const std::string& path) const;
    const std::vector<CookManifestEntry>& entries() const { return m_entries; }
    void clear() { m_entries.clear(); }

private:
    std::vector<CookManifestEntry> m_entries;
};

// ── Asset Cooker ─────────────────────────────────────────────────────────
class AssetCooker
{
public:
    struct CookConfig
    {
        std::string projectRoot;        // <Project> path
        std::string engineRoot;         // Engine source directory
        std::string outputDir;          // <Build>/Content/
        bool        compressAssets{ false };
        std::string buildType;          // "Debug" / "Release" / "RelWithDebInfo"
        int         maxThreads{ 4 };
    };

    struct CookResult
    {
        bool   success{ true };
        size_t totalAssets{ 0 };
        size_t cookedAssets{ 0 };
        size_t skippedAssets{ 0 };
        size_t failedAssets{ 0 };
        std::vector<std::string> errors;
        double elapsedSeconds{ 0.0 };
    };

    using ProgressCallback = std::function<void(size_t done, size_t total, const std::string& current)>;

    CookResult cookAll(const CookConfig& config,
                       ProgressCallback onProgress = {},
                       std::atomic<bool>* cancelFlag = nullptr);

private:
    bool cookMesh(const AssetRegistryEntry& entry,
                  const std::string& absAssetPath,
                  const std::string& outputPath);

    bool cookStrippedJson(const AssetRegistryEntry& entry,
                          const std::string& absAssetPath,
                          const std::string& outputPath);

    bool cookAudio(const AssetRegistryEntry& entry,
                   const std::string& absAssetPath,
                   const std::string& outputPath);

    bool copyAsset(const std::string& absAssetPath,
                   const std::string& outputPath);

    // Compute a simple hash for incremental cooking
    static uint64_t hashFile(const std::string& path);

    CookManifest m_manifest;
    CookManifest m_previousManifest;
};

#endif // ENGINE_EDITOR
