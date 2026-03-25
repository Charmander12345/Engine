#if ENGINE_EDITOR
#include "AssetCooker.h"
#include "AssetManager.h"
#include "json.hpp"
#include "Logger.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstring>
#include <limits>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────
// CookManifest
// ─────────────────────────────────────────────────────────────────────────
void CookManifest::addEntry(const CookManifestEntry& entry)
{
    m_entries.push_back(entry);
}

bool CookManifest::saveToFile(const std::string& path) const
{
    json j = json::array();
    for (const auto& e : m_entries)
    {
        j.push_back({
            { "originalPath", e.originalPath },
            { "cookedPath",   e.cookedPath },
            { "type",         static_cast<int>(e.type) },
            { "sourceHash",   e.sourceHash },
            { "cookedSize",   e.cookedSize }
        });
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << j.dump();
    return out.good();
}

bool CookManifest::loadFromFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto j = json::parse(text, nullptr, false);
    if (!j.is_array()) return false;

    m_entries.clear();
    for (const auto& e : j)
    {
        CookManifestEntry entry;
        entry.originalPath = e.value("originalPath", "");
        entry.cookedPath   = e.value("cookedPath", "");
        entry.type         = static_cast<AssetType>(e.value("type", 0));
        entry.sourceHash   = e.value("sourceHash", uint64_t(0));
        entry.cookedSize   = e.value("cookedSize", size_t(0));
        m_entries.push_back(entry);
    }
    return true;
}

const CookManifestEntry* CookManifest::findByOriginalPath(const std::string& path) const
{
    for (const auto& e : m_entries)
    {
        if (e.originalPath == path) return &e;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────
// Simple FNV-1a hash
// ─────────────────────────────────────────────────────────────────────────
uint64_t AssetCooker::hashFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;

    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    constexpr uint64_t prime = 1099511628211ULL;
    char buf[8192];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0)
    {
        const auto count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i)
        {
            hash ^= static_cast<uint64_t>(static_cast<uint8_t>(buf[i]));
            hash *= prime;
        }
    }
    return hash;
}

// ─────────────────────────────────────────────────────────────────────────
// Mesh cooking helpers (mirrors OpenGLObject3D logic)
// ─────────────────────────────────────────────────────────────────────────
namespace
{
    struct Vec3f { float x, y, z; };

    std::vector<float> ReadFloatArray(const json& data, const char* key)
    {
        if (!data.is_object()) return {};
        auto it = data.find(key);
        if (it == data.end() || !it->is_array()) return {};
        return it->get<std::vector<float>>();
    }

    std::vector<uint32_t> ReadIndexArray(const json& data, const char* key)
    {
        if (!data.is_object()) return {};
        auto it = data.find(key);
        if (it == data.end() || !it->is_array()) return {};
        return it->get<std::vector<uint32_t>>();
    }

    // Mirrors BuildVerticesWithFlatNormals from OpenGLObject3D.cpp
    std::vector<float> BuildVerticesWithFlatNormals(const std::vector<float>& vertices,
                                                     const std::vector<uint32_t>& indices,
                                                     Vec3f& outMin, Vec3f& outMax)
    {
        constexpr size_t inputStride = 5;
        if (vertices.empty() || (vertices.size() % inputStride) != 0) return {};

        const size_t vertexCount = vertices.size() / inputStride;

        // Compute bounds + mesh center
        Vec3f minP{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
        Vec3f maxP{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
        float cx = 0.f, cy = 0.f, cz = 0.f;
        for (size_t i = 0; i < vertexCount; ++i)
        {
            const size_t b = i * inputStride;
            float px = vertices[b + 0], py = vertices[b + 1], pz = vertices[b + 2];
            cx += px; cy += py; cz += pz;
            if (px < minP.x) minP.x = px; if (py < minP.y) minP.y = py; if (pz < minP.z) minP.z = pz;
            if (px > maxP.x) maxP.x = px; if (py > maxP.y) maxP.y = py; if (pz > maxP.z) maxP.z = pz;
        }
        if (vertexCount > 0)
        {
            float inv = 1.0f / static_cast<float>(vertexCount);
            cx *= inv; cy *= inv; cz *= inv;
        }
        outMin = minP;
        outMax = maxP;

        // Output: pos3 + normal3 + uv2 + tangent3 + bitangent3 = 14 floats
        std::vector<float> result;
        result.reserve((indices.empty() ? vertexCount : indices.size()) * 14);

        auto addTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2)
        {
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) return;
            const size_t b0 = size_t(i0) * inputStride;
            const size_t b1 = size_t(i1) * inputStride;
            const size_t b2 = size_t(i2) * inputStride;

            float p0x = vertices[b0], p0y = vertices[b0+1], p0z = vertices[b0+2];
            float p1x = vertices[b1], p1y = vertices[b1+1], p1z = vertices[b1+2];
            float p2x = vertices[b2], p2y = vertices[b2+1], p2z = vertices[b2+2];

            // face normal = cross(edge2, edge1)
            float e1x = p1x-p0x, e1y = p1y-p0y, e1z = p1z-p0z;
            float e2x = p2x-p0x, e2y = p2y-p0y, e2z = p2z-p0z;
            float nx = e2y*e1z - e2z*e1y;
            float ny = e2z*e1x - e2x*e1z;
            float nz = e2x*e1y - e2y*e1x;
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 0.f) { float inv = 1.f/len; nx *= inv; ny *= inv; nz *= inv; }

            float fcx = (p0x+p1x+p2x)/3.f, fcy = (p0y+p1y+p2y)/3.f, fcz = (p0z+p1z+p2z)/3.f;
            float dot = nx*(fcx-cx) + ny*(fcy-cy) + nz*(fcz-cz);
            if (dot < 0.f) { nx = -nx; ny = -ny; nz = -nz; }

            // tangent/bitangent
            float uv0u = vertices[b0+3], uv0v = vertices[b0+4];
            float uv1u = vertices[b1+3], uv1v = vertices[b1+4];
            float uv2u = vertices[b2+3], uv2v = vertices[b2+4];
            float du1 = uv1u-uv0u, dv1 = uv1v-uv0v;
            float du2 = uv2u-uv0u, dv2 = uv2v-uv0v;
            float denom = du1*dv2 - du2*dv1;
            float tx=1,ty=0,tz=0, bx=0,by=1,bz=0;
            if (std::abs(denom) > 1e-6f)
            {
                float f = 1.f/denom;
                tx = f*(dv2*e1x - dv1*e2x); ty = f*(dv2*e1y - dv1*e2y); tz = f*(dv2*e1z - dv1*e2z);
                float tl = std::sqrt(tx*tx+ty*ty+tz*tz); if(tl>0.f){tx/=tl;ty/=tl;tz/=tl;}
                bx = f*(-du2*e1x + du1*e2x); by = f*(-du2*e1y + du1*e2y); bz = f*(-du2*e1z + du1*e2z);
                float bl = std::sqrt(bx*bx+by*by+bz*bz); if(bl>0.f){bx/=bl;by/=bl;bz/=bl;}
            }

            auto emit = [&](size_t base)
            {
                result.push_back(vertices[base+0]); result.push_back(vertices[base+1]); result.push_back(vertices[base+2]);
                result.push_back(nx); result.push_back(ny); result.push_back(nz);
                result.push_back(vertices[base+3]); result.push_back(vertices[base+4]);
                result.push_back(tx); result.push_back(ty); result.push_back(tz);
                result.push_back(bx); result.push_back(by); result.push_back(bz);
            };
            emit(b0); emit(b1); emit(b2);
        };

        if (!indices.empty())
        {
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
                addTriangle(indices[i], indices[i+1], indices[i+2]);
        }
        else
        {
            for (size_t i = 0; i + 2 < vertexCount; i += 3)
                addTriangle(uint32_t(i), uint32_t(i+1), uint32_t(i+2));
        }
        return result;
    }
}

// ─────────────────────────────────────────────────────────────────────────
// cookMesh  – JSON → CMSH binary
// ─────────────────────────────────────────────────────────────────────────
bool AssetCooker::cookMesh(const AssetRegistryEntry& entry,
                           const std::string& absAssetPath,
                           const std::string& outputPath)
{
    auto& logger = Logger::Instance();

    // Read asset from disk (reuses AssetManager static helper)
    auto raw = AssetManager::readAssetFromDisk(absAssetPath, AssetType::Model3D);
    if (!raw.success)
    {
        logger.log(Logger::Category::AssetManagement,
            "AssetCooker: failed to read mesh: " + absAssetPath + " – " + raw.errorMessage,
            Logger::LogLevel::ERROR);
        return false;
    }
    const auto& data = raw.data;

    auto vertices = ReadFloatArray(data, "m_vertices");
    auto indices  = ReadIndexArray(data, "m_indices");
    if (vertices.empty())
    {
        logger.log(Logger::Category::AssetManagement,
            "AssetCooker: empty mesh vertices: " + absAssetPath, Logger::LogLevel::WARNING);
        return false;
    }

    bool hasBones = data.is_object() && data.contains("m_hasBones") && data.at("m_hasBones").get<bool>();

    Vec3f boundsMin{}, boundsMax{};
    auto expanded = BuildVerticesWithFlatNormals(vertices, indices, boundsMin, boundsMax);
    if (expanded.empty()) return false;

    // For skinned meshes, append boneIds + boneWeights per expanded vertex
    std::vector<float> finalVertexData;
    uint32_t vertexStride;
    if (hasBones && data.contains("m_boneIds") && data.contains("m_boneWeights"))
    {
        auto boneIds     = ReadFloatArray(data, "m_boneIds");
        auto boneWeights = ReadFloatArray(data, "m_boneWeights");
        const size_t origVtxCount = vertices.size() / 5;
        const size_t expandedCount = expanded.size() / 14;

        // Build expanded→original index mapping
        std::vector<uint32_t> expandedToOrig;
        expandedToOrig.reserve(expandedCount);
        if (!indices.empty())
        {
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                expandedToOrig.push_back(indices[i]);
                expandedToOrig.push_back(indices[i+1]);
                expandedToOrig.push_back(indices[i+2]);
            }
        }
        else
        {
            for (size_t i = 0; i < origVtxCount; ++i)
                expandedToOrig.push_back(static_cast<uint32_t>(i));
        }

        finalVertexData.reserve(expandedCount * 22);
        for (size_t v = 0; v < expandedCount; ++v)
        {
            for (int j = 0; j < 14; ++j)
                finalVertexData.push_back(expanded[v * 14 + j]);

            uint32_t origIdx = (v < expandedToOrig.size()) ? expandedToOrig[v] : 0;
            for (int j = 0; j < 4; ++j)
            {
                size_t bi = origIdx * 4 + j;
                finalVertexData.push_back((bi < boneIds.size()) ? boneIds[bi] : 0.f);
            }
            for (int j = 0; j < 4; ++j)
            {
                size_t bi = origIdx * 4 + j;
                finalVertexData.push_back((bi < boneWeights.size()) ? boneWeights[bi] : 0.f);
            }
        }
        vertexStride = 22 * sizeof(float); // 88 bytes
    }
    else
    {
        finalVertexData = std::move(expanded);
        vertexStride = 14 * sizeof(float); // 56 bytes
    }

    const uint32_t vtxCount = static_cast<uint32_t>(finalVertexData.size() / (vertexStride / sizeof(float)));

    // Count bones and animations for header
    uint32_t boneCount = 0;
    uint32_t animCount = 0;
    if (hasBones && data.contains("m_bones"))
        boneCount = static_cast<uint32_t>(data.at("m_bones").size());
    if (data.contains("m_animations"))
        animCount = static_cast<uint32_t>(data.at("m_animations").size());

    // Build skeleton/animation JSON blob (kept as JSON for complex tree structures)
    std::string skeletonBlob;
    if (hasBones)
    {
        json skelJson;
        if (data.contains("m_bones"))      skelJson["m_bones"]      = data.at("m_bones");
        if (data.contains("m_nodes"))       skelJson["m_nodes"]      = data.at("m_nodes");
        if (data.contains("m_animations"))  skelJson["m_animations"] = data.at("m_animations");
        if (data.contains("m_hasBones"))    skelJson["m_hasBones"]   = data.at("m_hasBones");

        // Preserve shader overrides
        if (data.contains("m_shaderVertex"))   skelJson["m_shaderVertex"]   = data.at("m_shaderVertex");
        if (data.contains("m_shaderFragment")) skelJson["m_shaderFragment"] = data.at("m_shaderFragment");

        skeletonBlob = skelJson.dump();
    }
    else
    {
        // Still preserve shader overrides for non-skinned meshes
        json meta;
        if (data.contains("m_shaderVertex"))   meta["m_shaderVertex"]   = data.at("m_shaderVertex");
        if (data.contains("m_shaderFragment")) meta["m_shaderFragment"] = data.at("m_shaderFragment");
        if (!meta.empty())
            skeletonBlob = meta.dump();
    }

    // Ensure output directory exists
    fs::create_directories(fs::path(outputPath).parent_path());

    // Write CMSH file
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) return false;

    CookedMeshHeader header{};
    header.magic        = CMSH_MAGIC;
    header.version      = CMSH_VERSION;
    header.vertexCount  = vtxCount;
    header.indexCount    = 0;
    header.vertexStride = vertexStride;
    header.flags        = hasBones ? CMSH_FLAG_HAS_BONES : 0;
    header.boundsMin[0] = boundsMin.x; header.boundsMin[1] = boundsMin.y; header.boundsMin[2] = boundsMin.z;
    header.boundsMax[0] = boundsMax.x; header.boundsMax[1] = boundsMax.y; header.boundsMax[2] = boundsMax.z;
    header.boneCount    = boneCount;
    header.animationCount = animCount;

    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    out.write(reinterpret_cast<const char*>(finalVertexData.data()),
              finalVertexData.size() * sizeof(float));

    // Append JSON metadata blob size + data
    uint32_t blobSize = static_cast<uint32_t>(skeletonBlob.size());
    out.write(reinterpret_cast<const char*>(&blobSize), sizeof(blobSize));
    if (blobSize > 0)
        out.write(skeletonBlob.data(), blobSize);

    logger.log(Logger::Category::AssetManagement,
        "AssetCooker: cooked mesh " + entry.name + " → " + std::to_string(vtxCount)
        + " verts, " + std::to_string(vertexStride) + "B stride, "
        + std::to_string(boneCount) + " bones",
        Logger::LogLevel::INFO);

    return out.good();
}

// ─────────────────────────────────────────────────────────────────────────
// cookStrippedJson – strip editor metadata, compact serialize
// ─────────────────────────────────────────────────────────────────────────
bool AssetCooker::cookStrippedJson(const AssetRegistryEntry& entry,
                                    const std::string& absAssetPath,
                                    const std::string& outputPath)
{
    auto raw = AssetManager::readAssetFromDisk(absAssetPath, entry.type);
    if (!raw.success) return false;

    json& data = raw.data;

    // Remove editor-only fields
    static const std::vector<std::string> editorFields = {
        "m_editorNotes", "m_importHistory", "m_editorCamera",
        "m_editorCameraPosition", "m_editorCameraRotation", "m_editorCameraYaw",
        "m_editorCameraPitch", "m_editorCameraSpeed", "m_editorCameraFov",
        "m_editorOnly", "m_editorTags"
    };
    for (const auto& field : editorFields)
    {
        if (data.contains(field))
            data.erase(field);
    }

    fs::create_directories(fs::path(outputPath).parent_path());

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) return false;
    out << data.dump(); // compact (no indentation)
    return out.good();
}

// ─────────────────────────────────────────────────────────────────────────
// cookAudio – copy the source WAV/audio file directly
// ─────────────────────────────────────────────────────────────────────────
bool AssetCooker::cookAudio(const AssetRegistryEntry& entry,
                             const std::string& absAssetPath,
                             const std::string& outputPath)
{
    // Read the .asset JSON to find the source file path
    auto raw = AssetManager::readAssetFromDisk(absAssetPath, AssetType::Audio);
    if (!raw.success) return false;

    // Copy the .asset itself (stripped)
    json& data = raw.data;
    fs::create_directories(fs::path(outputPath).parent_path());
    {
        std::ofstream out(outputPath, std::ios::binary);
        if (!out) return false;
        out << data.dump();
    }

    // Also copy the source audio file if referenced
    if (data.contains("m_sourcePath"))
    {
        std::string srcRelative = data.at("m_sourcePath").get<std::string>();
        if (!srcRelative.empty())
        {
            fs::path assetDir = fs::path(absAssetPath).parent_path();
            fs::path srcAbs = assetDir / srcRelative;
            if (fs::exists(srcAbs))
            {
                fs::path dstDir = fs::path(outputPath).parent_path();
                fs::path dstAudio = dstDir / srcRelative;
                fs::create_directories(dstAudio.parent_path());
                std::error_code ec;
                fs::copy_file(srcAbs, dstAudio, fs::copy_options::overwrite_existing, ec);
            }
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// copyAsset – 1:1 copy
// ─────────────────────────────────────────────────────────────────────────
bool AssetCooker::copyAsset(const std::string& absPath, const std::string& outputPath)
{
    fs::create_directories(fs::path(outputPath).parent_path());
    std::error_code ec;
    fs::copy_file(absPath, outputPath, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

// ─────────────────────────────────────────────────────────────────────────
// cookAll – main entry point
// ─────────────────────────────────────────────────────────────────────────
AssetCooker::CookResult AssetCooker::cookAll(const CookConfig& config,
                                              ProgressCallback onProgress,
                                              std::atomic<bool>* cancelFlag)
{
    auto& logger = Logger::Instance();
    CookResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    const auto& registry = AssetManager::Instance().getAssetRegistry();
    result.totalAssets = registry.size();

    // Try to load previous manifest for incremental cooking
    const std::string manifestPath = (fs::path(config.outputDir) / "manifest.json").string();
    m_previousManifest.loadFromFile(manifestPath);
    m_manifest.clear();

    const fs::path projectContentRoot = fs::path(config.projectRoot) / "Content";
    const fs::path engineContentRoot  = fs::path(config.engineRoot) / "Content";

    size_t done = 0;
    for (const auto& entry : registry)
    {
        if (cancelFlag && cancelFlag->load())
        {
            result.success = false;
            result.errors.push_back("Cancelled by user.");
            break;
        }

        ++done;
        if (onProgress) onProgress(done, result.totalAssets, entry.name);

        // Resolve absolute source path
        std::string absPath;
        {
            fs::path candidate = projectContentRoot / entry.path;
            if (fs::exists(candidate))
                absPath = candidate.lexically_normal().string();
            else
            {
                candidate = engineContentRoot / entry.path;
                if (fs::exists(candidate))
                    absPath = candidate.lexically_normal().string();
            }
        }
        if (absPath.empty())
        {
            logger.log(Logger::Category::AssetManagement,
                "AssetCooker: source not found for " + entry.name + " (" + entry.path + ")",
                Logger::LogLevel::WARNING);
            ++result.failedAssets;
            result.errors.push_back("Source not found: " + entry.path);
            continue;
        }

        // Determine output path
        const std::string cookedExtension = (entry.type == AssetType::Model3D) ? ".cooked" : "";
        std::string outputRelPath = entry.path;
        if (!cookedExtension.empty())
        {
            // Replace extension: Models/Cube.asset → Models/Cube.cooked
            fs::path p(outputRelPath);
            p.replace_extension(cookedExtension);
            outputRelPath = p.string();
        }
        const std::string outputAbs = (fs::path(config.outputDir) / outputRelPath).string();

        // Incremental check: compare file hash with previous manifest
        uint64_t currentHash = hashFile(absPath);
        const auto* prevEntry = m_previousManifest.findByOriginalPath(entry.path);
        if (prevEntry && prevEntry->sourceHash == currentHash && fs::exists(outputAbs))
        {
            // Unchanged – reuse previous cook
            m_manifest.addEntry(*prevEntry);
            ++result.skippedAssets;
            continue;
        }

        // Cook based on type
        bool ok = false;
        switch (entry.type)
        {
        case AssetType::Model3D:
            ok = cookMesh(entry, absPath, outputAbs);
            break;

        case AssetType::Material:
        case AssetType::Level:
        case AssetType::Widget:
        case AssetType::Skybox:
        case AssetType::Prefab:
            ok = cookStrippedJson(entry, absPath, outputAbs);
            break;

        case AssetType::Audio:
            ok = cookAudio(entry, absPath, outputAbs);
            break;

        case AssetType::Script:
        case AssetType::Shader:
        case AssetType::Texture: // Texture compression deferred (Phase 10.2 step 5)
        default:
            ok = copyAsset(absPath, outputAbs);
            break;
        }

        if (ok)
        {
            CookManifestEntry me;
            me.originalPath = entry.path;
            me.cookedPath   = outputRelPath;
            me.type         = entry.type;
            me.sourceHash   = currentHash;
            me.cookedSize   = fs::exists(outputAbs) ? static_cast<size_t>(fs::file_size(outputAbs)) : 0;
            m_manifest.addEntry(me);
            ++result.cookedAssets;
        }
        else
        {
            ++result.failedAssets;
            result.errors.push_back("Failed to cook: " + entry.path);
        }
    }

    // Save manifest
    fs::create_directories(fs::path(manifestPath).parent_path());
    m_manifest.saveToFile(manifestPath);

    auto endTime = std::chrono::high_resolution_clock::now();
    result.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

    logger.log(Logger::Category::AssetManagement,
        "AssetCooker: done – " + std::to_string(result.cookedAssets) + " cooked, "
        + std::to_string(result.skippedAssets) + " skipped, "
        + std::to_string(result.failedAssets) + " failed in "
        + std::to_string(result.elapsedSeconds) + "s",
        Logger::LogLevel::INFO);

    if (result.failedAssets > 0) result.success = false;
    return result;
}

#endif // ENGINE_EDITOR
