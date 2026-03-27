#include "OpenGLObject3D.h"

#include <filesystem>
#include <vector>
#include <limits>
#include <fstream>
#include <cstring>

#include "OpenGLMaterial.h"
#include "OpenGLShader.h"
#include "Logger.h"

#include "../../Core/Asset.h"
#include "../../AssetManager/AssetCooker.h"
#include "../../AssetManager/HPKArchive.h"

#include <unordered_map>
#include <glm/glm.hpp>

namespace
{
    std::string ResolveShaderPath(const std::string& filename)
    {
        static std::unordered_map<std::string, std::string> s_shaderPathCache;
        auto it = s_shaderPathCache.find(filename);
        if (it != s_shaderPathCache.end())
        {
            return it->second;
        }
        std::filesystem::path base = std::filesystem::current_path();
        std::filesystem::path shadersfolder = base / "shaders" / filename;
        std::string result;
        if (std::filesystem::exists(shadersfolder))
        {
            result = shadersfolder.string();
        }
        else
        {
            // HPK fallback: return path anyway if shader exists in archive
            auto* hpk = HPKReader::GetMounted();
            if (hpk)
            {
                std::string vpath = "shaders/" + filename;
                if (hpk->contains(vpath))
                {
                    result = shadersfolder.string();
                    Logger::Instance().log("HPK ResolveShaderPath3D: " + filename + " -> " + vpath, Logger::LogLevel::INFO);
                }
                else
                {
                    Logger::Instance().log("HPK ResolveShaderPath3D: " + filename + " not found in archive", Logger::LogLevel::WARNING);
                }
            }
            else
            {
                Logger::Instance().log("HPK not mounted in ResolveShaderPath3D: " + filename, Logger::LogLevel::WARNING);
            }
        }
        s_shaderPathCache[filename] = result;
        return result;
    }

    std::vector<float> ReadFloatArray(const json& data, const char* key)
    {
        if (!data.is_object())
        {
            return {};
        }
        auto it = data.find(key);
        if (it == data.end() || !it->is_array())
        {
            return {};
        }
        return it->get<std::vector<float>>();
    }

    std::vector<uint32_t> ReadIndexArray(const json& data, const char* key)
    {
        if (!data.is_object())
        {
            return {};
        }
        auto it = data.find(key);
        if (it == data.end() || !it->is_array())
        {
            return {};
        }
        return it->get<std::vector<uint32_t>>();
    }

    std::vector<float> BuildVerticesWithFlatNormals(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
    {
        constexpr size_t inputStride = 5;
        if (vertices.empty() || (vertices.size() % inputStride) != 0)
        {
            return {};
        }

        const size_t vertexCount = vertices.size() / inputStride;
        glm::vec3 meshCenter(0.0f);
        for (size_t i = 0; i < vertexCount; ++i)
        {
            const size_t base = i * inputStride;
            meshCenter += glm::vec3(vertices[base + 0], vertices[base + 1], vertices[base + 2]);
        }
        if (vertexCount > 0)
        {
            meshCenter /= static_cast<float>(vertexCount);
        }
        // Output: pos3 + normal3 + uv2 + tangent3 + bitangent3 = 14 floats per vertex
        std::vector<float> result;
        result.reserve((indices.empty() ? vertexCount : indices.size()) * 14);

        const auto addTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2)
        {
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            {
                return;
            }

            const size_t base0 = static_cast<size_t>(i0) * inputStride;
            const size_t base1 = static_cast<size_t>(i1) * inputStride;
            const size_t base2 = static_cast<size_t>(i2) * inputStride;

            const glm::vec3 p0(vertices[base0 + 0], vertices[base0 + 1], vertices[base0 + 2]);
            const glm::vec3 p1(vertices[base1 + 0], vertices[base1 + 1], vertices[base1 + 2]);
            const glm::vec3 p2(vertices[base2 + 0], vertices[base2 + 1], vertices[base2 + 2]);

            const glm::vec3 edge1 = p1 - p0;
            const glm::vec3 edge2 = p2 - p0;
            glm::vec3 faceNormal = glm::cross(edge2, edge1);
            if (glm::length(faceNormal) > 0.0f)
            {
                faceNormal = glm::normalize(faceNormal);
            }

            const glm::vec3 faceCenter = (p0 + p1 + p2) / 3.0f;
            if (glm::dot(faceNormal, faceCenter - meshCenter) < 0.0f)
            {
                faceNormal = -faceNormal;
            }

            // Compute tangent/bitangent from UV edges
            const glm::vec2 uv0(vertices[base0 + 3], vertices[base0 + 4]);
            const glm::vec2 uv1(vertices[base1 + 3], vertices[base1 + 4]);
            const glm::vec2 uv2(vertices[base2 + 3], vertices[base2 + 4]);
            const glm::vec2 deltaUV1 = uv1 - uv0;
            const glm::vec2 deltaUV2 = uv2 - uv0;

            float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
            glm::vec3 tangent(1.0f, 0.0f, 0.0f);
            glm::vec3 bitangent(0.0f, 1.0f, 0.0f);
            if (std::abs(denom) > 1e-6f)
            {
                float f = 1.0f / denom;
                tangent = glm::normalize(f * (deltaUV2.y * edge1 - deltaUV1.y * edge2));
                bitangent = glm::normalize(f * (-deltaUV2.x * edge1 + deltaUV1.x * edge2));
            }

            auto appendVertex = [&](size_t base)
            {
                result.push_back(vertices[base + 0]); // pos
                result.push_back(vertices[base + 1]);
                result.push_back(vertices[base + 2]);
                result.push_back(faceNormal.x);        // normal
                result.push_back(faceNormal.y);
                result.push_back(faceNormal.z);
                result.push_back(vertices[base + 3]);  // uv
                result.push_back(vertices[base + 4]);
                result.push_back(tangent.x);           // tangent
                result.push_back(tangent.y);
                result.push_back(tangent.z);
                result.push_back(bitangent.x);         // bitangent
                result.push_back(bitangent.y);
                result.push_back(bitangent.z);
            };

            appendVertex(base0);
            appendVertex(base1);
            appendVertex(base2);
        };

        if (!indices.empty())
        {
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                addTriangle(indices[i], indices[i + 1], indices[i + 2]);
            }
        }
        else
        {
            for (size_t i = 0; i + 2 < vertexCount; i += 3)
            {
                addTriangle(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
            }
        }

        return result;
    }

    std::unordered_map<std::string, std::shared_ptr<OpenGLMaterial>> s_materialCache;

    // Try to find a .cooked (CMSH) file for this mesh asset path
    std::string FindCookedMeshPath(const std::string& assetPath)
    {
        namespace fs = std::filesystem;
        fs::path p(assetPath);
        fs::path cookedPath = p;
        cookedPath.replace_extension(".cooked");
        if (fs::exists(cookedPath))
            return cookedPath.string();

        // HPK fallback: check if .cooked exists in the archive
        auto* hpk = HPKReader::GetMounted();
        if (hpk)
        {
            std::string vpath = hpk->makeVirtualPath(cookedPath.string());
            if (!vpath.empty() && hpk->contains(vpath))
                return cookedPath.string(); // Return the path; LoadCookedMesh will read from HPK
        }
        return {};
    }

    // Detect CMSH magic in the first 4 bytes of a file
    bool IsCookedMesh(const std::string& filePath)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (in)
        {
            uint32_t magic = 0;
            in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
            return magic == CMSH_MAGIC;
        }

        // HPK fallback: read the first 4 bytes from the archive
        auto* hpk = HPKReader::GetMounted();
        if (hpk)
        {
            std::string vpath = hpk->makeVirtualPath(filePath);
            if (!vpath.empty())
            {
                auto buf = hpk->readFile(vpath);
                if (buf && buf->size() >= sizeof(uint32_t))
                {
                    uint32_t magic = 0;
                    std::memcpy(&magic, buf->data(), sizeof(magic));
                    return magic == CMSH_MAGIC;
                }
            }
        }
        return false;
    }

    struct CookedMeshData
    {
        std::vector<float> vertexData;
        uint32_t vertexCount{ 0 };
        uint32_t vertexStride{ 0 };
        bool hasBones{ false };
        glm::vec3 boundsMin{ 0.f };
        glm::vec3 boundsMax{ 0.f };
        json metaJson;  // skeleton/animation/shader data
    };

    bool LoadCookedMesh(const std::string& path, CookedMeshData& out)
    {
        // Try disk first
        std::ifstream in(path, std::ios::binary);
        if (in)
        {
            CookedMeshHeader header{};
            if (!in.read(reinterpret_cast<char*>(&header), sizeof(header)))
                return false;

            if (header.magic != CMSH_MAGIC || header.version != CMSH_VERSION)
                return false;

            out.vertexCount  = header.vertexCount;
            out.vertexStride = header.vertexStride;
            out.hasBones     = (header.flags & CMSH_FLAG_HAS_BONES) != 0;
            out.boundsMin    = glm::vec3(header.boundsMin[0], header.boundsMin[1], header.boundsMin[2]);
            out.boundsMax    = glm::vec3(header.boundsMax[0], header.boundsMax[1], header.boundsMax[2]);

            const size_t floatsPerVertex = header.vertexStride / sizeof(float);
            const size_t totalFloats = static_cast<size_t>(header.vertexCount) * floatsPerVertex;
            out.vertexData.resize(totalFloats);
            if (!in.read(reinterpret_cast<char*>(out.vertexData.data()), totalFloats * sizeof(float)))
                return false;

            uint32_t blobSize = 0;
            if (in.read(reinterpret_cast<char*>(&blobSize), sizeof(blobSize)) && blobSize > 0)
            {
                std::string blob(blobSize, '\0');
                if (in.read(blob.data(), blobSize))
                {
                    out.metaJson = json::parse(blob, nullptr, false);
                    if (out.metaJson.is_discarded())
                        out.metaJson = json::object();
                }
            }
            return true;
        }

        // HPK fallback
        auto* hpk = HPKReader::GetMounted();
        if (!hpk) return false;

        std::string vpath = hpk->makeVirtualPath(path);
        if (vpath.empty()) return false;

        auto buf = hpk->readFile(vpath);
        if (!buf || buf->size() < sizeof(CookedMeshHeader))
            return false;

        const char* ptr = buf->data();
        const char* end = ptr + buf->size();

        CookedMeshHeader header{};
        std::memcpy(&header, ptr, sizeof(header));
        ptr += sizeof(header);

        if (header.magic != CMSH_MAGIC || header.version != CMSH_VERSION)
            return false;

        out.vertexCount  = header.vertexCount;
        out.vertexStride = header.vertexStride;
        out.hasBones     = (header.flags & CMSH_FLAG_HAS_BONES) != 0;
        out.boundsMin    = glm::vec3(header.boundsMin[0], header.boundsMin[1], header.boundsMin[2]);
        out.boundsMax    = glm::vec3(header.boundsMax[0], header.boundsMax[1], header.boundsMax[2]);

        const size_t floatsPerVertex = header.vertexStride / sizeof(float);
        const size_t totalFloats = static_cast<size_t>(header.vertexCount) * floatsPerVertex;
        const size_t dataBytes = totalFloats * sizeof(float);
        if (ptr + dataBytes > end) return false;

        out.vertexData.resize(totalFloats);
        std::memcpy(out.vertexData.data(), ptr, dataBytes);
        ptr += dataBytes;

        if (ptr + 4 <= end)
        {
            uint32_t blobSize = 0;
            std::memcpy(&blobSize, ptr, sizeof(blobSize));
            ptr += 4;
            if (blobSize > 0 && ptr + blobSize <= end)
            {
                std::string blob(ptr, blobSize);
                out.metaJson = json::parse(blob, nullptr, false);
                if (out.metaJson.is_discarded())
                    out.metaJson = json::object();
            }
        }
        return true;
    }
}

OpenGLObject3D::OpenGLObject3D(const std::shared_ptr<AssetData>& asset)
    : m_asset(asset)
{
}

bool OpenGLObject3D::hasLocalBounds() const
{
    return m_hasLocalBounds;
}

Vec3 OpenGLObject3D::getLocalBoundsMin() const
{
    return Vec3{ m_localBoundsMin.x, m_localBoundsMin.y, m_localBoundsMin.z };
}

Vec3 OpenGLObject3D::getLocalBoundsMax() const
{
    return Vec3{ m_localBoundsMax.x, m_localBoundsMax.y, m_localBoundsMax.z };
}

GLuint OpenGLObject3D::getProgram() const
{
    return m_material ? m_material->getProgram() : 0;
}

GLuint OpenGLObject3D::getVao() const
{
    return m_material ? m_material->getVao() : 0;
}

int OpenGLObject3D::getVertexCount() const
{
    return m_material ? m_material->getVertexCount() : 0;
}

int OpenGLObject3D::getIndexCount() const
{
    return m_material ? m_material->getIndexCount() : 0;
}

void OpenGLObject3D::ClearCache()
{
    s_materialCache.clear();
}

bool OpenGLObject3D::prepare()
{
    auto& logger = Logger::Instance();
    if (!m_asset)
        return false;

    if (m_material)
        return true;

    const std::string path = m_asset->getPath();
    std::string cacheKeySuffix = m_materialCacheKeySuffix;
    if (!m_fragmentShaderOverride.empty())
    {
        if (!cacheKeySuffix.empty()) cacheKeySuffix += "|";
        cacheKeySuffix += m_fragmentShaderOverride;
    }
    const std::string cacheKey = cacheKeySuffix.empty() ? path : (path + "|" + cacheKeySuffix);
    if (!cacheKey.empty())
    {
        auto it = s_materialCache.find(cacheKey);
        if (it != s_materialCache.end())
        {
            m_material = it->second;
            return true;
        }
    }

    const auto& data = m_asset->getData();

    // ── Try cooked binary mesh (CMSH) first ─────────────────────────────
    const std::string cookedPath = FindCookedMeshPath(m_asset->getPath());
    if (!cookedPath.empty() && IsCookedMesh(cookedPath))
    {
        CookedMeshData cmsh;
        if (LoadCookedMesh(cookedPath, cmsh) && cmsh.vertexCount > 0)
        {
            logger.log(Logger::Category::Rendering,
                "OpenGLObject3D: loading cooked mesh (" + std::to_string(cmsh.vertexCount) + " verts)",
                Logger::LogLevel::INFO);

            // Extract shader overrides from cooked metadata
            std::string vertexOverride, fragmentOverride;
            if (cmsh.metaJson.is_object())
            {
                if (cmsh.metaJson.contains("m_shaderVertex"))
                    vertexOverride = cmsh.metaJson.at("m_shaderVertex").get<std::string>();
                if (cmsh.metaJson.contains("m_shaderFragment"))
                    fragmentOverride = cmsh.metaJson.at("m_shaderFragment").get<std::string>();
            }

            bool hasBones = cmsh.hasBones;
            m_isSkinned = hasBones;

            const std::string defaultVS = hasBones ? "skinned_vertex.glsl" : "vertex.glsl";
            const std::string vsPath = ResolveShaderPath(vertexOverride.empty() ? defaultVS : vertexOverride);
            const std::string fsName = !m_fragmentShaderOverride.empty() ? m_fragmentShaderOverride
                                     : !fragmentOverride.empty()        ? fragmentOverride
                                     :                                    "fragment.glsl";
            const std::string fsPath = ResolveShaderPath(fsName);
            if (vsPath.empty() || fsPath.empty())
            {
                logger.log(Logger::Category::Rendering, "OpenGLObject3D: CMSH – shader not found.", Logger::LogLevel::ERROR);
                return false;
            }

            auto vs = std::make_shared<OpenGLShader>();
            auto fs = std::make_shared<OpenGLShader>();
            if (!vs->loadFromFile(Shader::Type::Vertex, vsPath) ||
                !fs->loadFromFile(Shader::Type::Fragment, fsPath))
            {
                logger.log(Logger::Category::Rendering, "OpenGLObject3D: CMSH – shader compile failed.", Logger::LogLevel::ERROR);
                return false;
            }

            auto mat = std::make_shared<OpenGLMaterial>();
            mat->addShader(vs);
            mat->addShader(fs);
            mat->setFragmentShaderPath(fsPath);
            mat->setVertexData(cmsh.vertexData);
            mat->setIndexData({});

            // Bounds from header
            m_localBoundsMin = cmsh.boundsMin;
            m_localBoundsMax = cmsh.boundsMax;
            m_hasLocalBounds = true;

            // Set vertex layout
            if (hasBones)
            {
                const GLsizei stride = static_cast<GLsizei>(22 * sizeof(float));
                std::vector<OpenGLMaterial::LayoutElement> layout;
                layout.push_back({ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
                layout.push_back({ 1, 3, GL_FLOAT, GL_FALSE, stride, size_t(3 * sizeof(float)) });
                layout.push_back({ 2, 2, GL_FLOAT, GL_FALSE, stride, size_t(6 * sizeof(float)) });
                layout.push_back({ 3, 3, GL_FLOAT, GL_FALSE, stride, size_t(8 * sizeof(float)) });
                layout.push_back({ 4, 3, GL_FLOAT, GL_FALSE, stride, size_t(11 * sizeof(float)) });
                layout.push_back({ 5, 4, GL_FLOAT, GL_FALSE, stride, size_t(14 * sizeof(float)) });
                layout.push_back({ 6, 4, GL_FLOAT, GL_FALSE, stride, size_t(18 * sizeof(float)) });
                mat->setLayout(layout);
            }
            else
            {
                const GLsizei stride = static_cast<GLsizei>(14 * sizeof(float));
                std::vector<OpenGLMaterial::LayoutElement> layout;
                layout.push_back({ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
                layout.push_back({ 1, 3, GL_FLOAT, GL_FALSE, stride, size_t(3 * sizeof(float)) });
                layout.push_back({ 2, 2, GL_FLOAT, GL_FALSE, stride, size_t(6 * sizeof(float)) });
                layout.push_back({ 3, 3, GL_FLOAT, GL_FALSE, stride, size_t(8 * sizeof(float)) });
                layout.push_back({ 4, 3, GL_FLOAT, GL_FALSE, stride, size_t(11 * sizeof(float)) });
                mat->setLayout(layout);
            }

            // Load skeleton from cooked metadata JSON
            if (hasBones && cmsh.metaJson.contains("m_bones"))
            {
                m_skeleton = std::make_shared<Skeleton>();
                const auto& bonesJson = cmsh.metaJson.at("m_bones");
                for (const auto& bj : bonesJson)
                {
                    BoneInfo bone;
                    bone.name = bj.at("name").get<std::string>();
                    if (bj.contains("offsetMatrix"))
                    {
                        const auto& om = bj.at("offsetMatrix");
                        for (int i = 0; i < 16 && i < static_cast<int>(om.size()); ++i)
                            bone.offsetMatrix.m[i] = om[i].get<float>();
                    }
                    m_skeleton->boneNameToIndex[bone.name] = static_cast<int>(m_skeleton->bones.size());
                    m_skeleton->bones.push_back(bone);
                }
                if (cmsh.metaJson.contains("m_nodes"))
                {
                    for (const auto& nj : cmsh.metaJson.at("m_nodes"))
                    {
                        Skeleton::Node node;
                        node.name = nj.at("name").get<std::string>();
                        node.parentIndex = nj.value("parent", -1);
                        node.boneIndex = nj.value("boneIndex", -1);
                        if (nj.contains("transform"))
                        {
                            const auto& t = nj.at("transform");
                            for (int i = 0; i < 16 && i < static_cast<int>(t.size()); ++i)
                                node.localTransform.m[i] = t[i].get<float>();
                        }
                        m_skeleton->nodes.push_back(node);
                    }
                    for (int i = 0; i < static_cast<int>(m_skeleton->nodes.size()); ++i)
                    {
                        int parent = m_skeleton->nodes[i].parentIndex;
                        if (parent >= 0 && parent < static_cast<int>(m_skeleton->nodes.size()))
                            m_skeleton->nodes[parent].children.push_back(i);
                    }
                }
                if (cmsh.metaJson.contains("m_animations"))
                {
                    for (const auto& aj : cmsh.metaJson.at("m_animations"))
                    {
                        AnimationClip clip;
                        clip.name = aj.value("name", "");
                        clip.duration = aj.value("duration", 0.0f);
                        clip.ticksPerSecond = aj.value("ticksPerSecond", 25.0f);
                        if (aj.contains("channels"))
                        {
                            for (const auto& cj : aj.at("channels"))
                            {
                                BoneChannel ch;
                                ch.boneName = cj.value("boneName", "");
                                auto bit = m_skeleton->boneNameToIndex.find(ch.boneName);
                                ch.boneIndex = (bit != m_skeleton->boneNameToIndex.end()) ? bit->second : -1;
                                if (cj.contains("positionKeys"))
                                    for (const auto& pk : cj.at("positionKeys"))
                                    {
                                        VectorKey k; k.time = pk.value("t", 0.f);
                                        if (pk.contains("v") && pk.at("v").size() >= 3)
                                        { k.value[0] = pk.at("v")[0].get<float>(); k.value[1] = pk.at("v")[1].get<float>(); k.value[2] = pk.at("v")[2].get<float>(); }
                                        ch.positionKeys.push_back(k);
                                    }
                                if (cj.contains("rotationKeys"))
                                    for (const auto& rk : cj.at("rotationKeys"))
                                    {
                                        QuatKey k; k.time = rk.value("t", 0.f);
                                        if (rk.contains("q") && rk.at("q").size() >= 4)
                                        { k.value.x = rk.at("q")[0].get<float>(); k.value.y = rk.at("q")[1].get<float>(); k.value.z = rk.at("q")[2].get<float>(); k.value.w = rk.at("q")[3].get<float>(); }
                                        ch.rotationKeys.push_back(k);
                                    }
                                if (cj.contains("scalingKeys"))
                                    for (const auto& sk : cj.at("scalingKeys"))
                                    {
                                        VectorKey k; k.time = sk.value("t", 0.f);
                                        if (sk.contains("v") && sk.at("v").size() >= 3)
                                        { k.value[0] = sk.at("v")[0].get<float>(); k.value[1] = sk.at("v")[1].get<float>(); k.value[2] = sk.at("v")[2].get<float>(); }
                                        ch.scalingKeys.push_back(k);
                                    }
                                clip.channels.push_back(ch);
                            }
                        }
                        m_skeleton->animations.push_back(clip);
                    }
                }
                logger.log(Logger::Category::Rendering,
                    "OpenGLObject3D: CMSH skeleton – " + std::to_string(m_skeleton->bones.size()) + " bones, "
                    + std::to_string(m_skeleton->animations.size()) + " anim(s)",
                    Logger::LogLevel::INFO);
            }

            if (!mat->build())
            {
                logger.log(Logger::Category::Rendering, "OpenGLObject3D: CMSH – material build failed.", Logger::LogLevel::ERROR);
                return false;
            }

            m_material = mat;
            if (!cacheKey.empty())
                s_materialCache[cacheKey] = m_material;
            return true;
        }
    }

    // ── Fallback: JSON asset loading (editor / uncooked) ────────────────
    std::string vertexOverride;
    std::string fragmentOverride;
    if (data.is_object())
    {
        if (data.contains("m_shaderVertex"))
        {
            vertexOverride = data.at("m_shaderVertex").get<std::string>();
        }
        if (data.contains("m_shaderFragment"))
        {
            fragmentOverride = data.at("m_shaderFragment").get<std::string>();
        }
    }

    // Detect skinned mesh
    bool hasBones = data.is_object() && data.contains("m_hasBones") && data.at("m_hasBones").get<bool>();
    m_isSkinned = hasBones;

    // Use skinned vertex shader for meshes with bones
    const std::string defaultVertexShader = hasBones ? "skinned_vertex.glsl" : "vertex.glsl";
    const std::string vertexPath = ResolveShaderPath(vertexOverride.empty() ? defaultVertexShader : vertexOverride);
    const std::string fragmentName = !m_fragmentShaderOverride.empty() ? m_fragmentShaderOverride
                                   : !fragmentOverride.empty()       ? fragmentOverride
                                   :                                   "fragment.glsl";
    const std::string fragmentPath = ResolveShaderPath(fragmentName);
    if (vertexPath.empty() || fragmentPath.empty())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Couldn't locate vertex.glsl and/or fragment.glsl.", Logger::LogLevel::ERROR);
        return false;
    }

    auto vertexShader = std::make_shared<OpenGLShader>();
    auto fragmentShader = std::make_shared<OpenGLShader>();

    if (!vertexShader->loadFromFile(Shader::Type::Vertex, vertexPath))
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to load/compile vertex shader.", Logger::LogLevel::ERROR);
        return false;
    }
    if (!fragmentShader->loadFromFile(Shader::Type::Fragment, fragmentPath))
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to load/compile fragment shader.", Logger::LogLevel::ERROR);
        return false;
    }

    auto mat = std::make_shared<OpenGLMaterial>();
    mat->addShader(vertexShader);
    mat->addShader(fragmentShader);
    mat->setFragmentShaderPath(fragmentPath);

    auto vertices = ReadFloatArray(data, "m_vertices");
    auto indices = ReadIndexArray(data, "m_indices");
    const auto vCount = vertices.size();
    const auto iCount = indices.size();
    logger.log(Logger::Category::Rendering,
        "OpenGLObject3D: vertexFloats=" + std::to_string(vCount) +
        ", indexCount=" + std::to_string(iCount),
        Logger::LogLevel::INFO);

    if (vCount == 0)
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: WARNING: vertices are empty.", Logger::LogLevel::WARNING);
    }
    else if ((vCount % 5) != 0)
    {
        logger.log(Logger::Category::Rendering,
            "OpenGLObject3D: WARNING: vertex float count is not divisible by 5 (expected layout: pos3+uv2).",
            Logger::LogLevel::WARNING);
    }

    if (vCount == 0)
    {
        return false;
    }

    if ((vCount % 5) == 0)
    {
        glm::vec3 minPos(std::numeric_limits<float>::max());
        glm::vec3 maxPos(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i + 4 < vCount; i += 5)
        {
            glm::vec3 pos(vertices[i + 0], vertices[i + 1], vertices[i + 2]);
            minPos = glm::min(minPos, pos);
            maxPos = glm::max(maxPos, pos);
        }

        m_localBoundsMin = minPos;
        m_localBoundsMax = maxPos;
        m_hasLocalBounds = true;
    }

    auto verticesWithNormals = BuildVerticesWithFlatNormals(vertices, indices);
    if (verticesWithNormals.empty())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to build vertex normals.", Logger::LogLevel::ERROR);
        return false;
    }

    // For skinned meshes, append bone IDs (as floats) and bone weights per vertex
    if (hasBones && data.contains("m_boneIds") && data.contains("m_boneWeights"))
    {
        auto boneIds = ReadFloatArray(data, "m_boneIds");       // 4 ints (as float) per original vertex
        auto boneWeights = ReadFloatArray(data, "m_boneWeights"); // 4 floats per original vertex
        const size_t origVertexCount = vCount / 5;
        // BuildVerticesWithFlatNormals expands indexed vertices: each output vertex comes from
        // an original vertex index. We need to re-expand bone data the same way.
        // The output has verticesWithNormals.size()/14 vertices.
        const size_t expandedCount = verticesWithNormals.size() / 14;

        // Build an index list that maps expanded vertices to original vertices
        std::vector<uint32_t> expandedToOrig;
        expandedToOrig.reserve(expandedCount);
        if (!indices.empty()) {
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                expandedToOrig.push_back(indices[i]);
                expandedToOrig.push_back(indices[i+1]);
                expandedToOrig.push_back(indices[i+2]);
            }
        } else {
            for (size_t i = 0; i < origVertexCount; ++i) {
                expandedToOrig.push_back(static_cast<uint32_t>(i));
            }
        }

        // Rebuild with 22 floats per vertex: 14 (pos+norm+uv+tan+bitan) + 4 (boneIds as float) + 4 (boneWeights)
        std::vector<float> skinnedVertices;
        skinnedVertices.reserve(expandedCount * 22);
        for (size_t v = 0; v < expandedCount; ++v)
        {
            // Copy existing 14 floats
            for (int j = 0; j < 14; ++j)
                skinnedVertices.push_back(verticesWithNormals[v * 14 + j]);

            uint32_t origIdx = (v < expandedToOrig.size()) ? expandedToOrig[v] : 0;
            // Bone IDs (4 floats representing ints)
            for (int j = 0; j < 4; ++j) {
                size_t bi = origIdx * 4 + j;
                skinnedVertices.push_back((bi < boneIds.size()) ? boneIds[bi] : 0.0f);
            }
            // Bone Weights (4 floats)
            for (int j = 0; j < 4; ++j) {
                size_t bi = origIdx * 4 + j;
                skinnedVertices.push_back((bi < boneWeights.size()) ? boneWeights[bi] : 0.0f);
            }
        }
        mat->setVertexData(skinnedVertices);
    }
    else
    {
        mat->setVertexData(verticesWithNormals);
    }
    mat->setIndexData({});

    if (hasBones && data.contains("m_boneIds"))
    {
        // Skinned layout: pos3 + norm3 + uv2 + tan3 + bitan3 + boneIds(4 int-as-float) + boneWeights4 = 22 floats
        const GLsizei stride = static_cast<GLsizei>(22 * sizeof(float));
        std::vector<OpenGLMaterial::LayoutElement> layout;
        layout.push_back(OpenGLMaterial::LayoutElement{ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
        layout.push_back(OpenGLMaterial::LayoutElement{ 1, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(3 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 2, 2, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(6 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 3, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(8 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 4, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(11 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 5, 4, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(14 * sizeof(float)) }); // boneIds (as float, cast to int in shader)
        layout.push_back(OpenGLMaterial::LayoutElement{ 6, 4, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(18 * sizeof(float)) }); // boneWeights
        mat->setLayout(layout);
    }
    else
    {
        // Default layout: positions (x,y,z) + normals (x,y,z) + texcoords (u,v) + tangent (x,y,z) + bitangent (x,y,z)
        const GLsizei stride = static_cast<GLsizei>(14 * sizeof(float));
        std::vector<OpenGLMaterial::LayoutElement> layout;
        layout.push_back(OpenGLMaterial::LayoutElement{ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
        layout.push_back(OpenGLMaterial::LayoutElement{ 1, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(3 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 2, 2, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(6 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 3, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(8 * sizeof(float)) });
        layout.push_back(OpenGLMaterial::LayoutElement{ 4, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(11 * sizeof(float)) });
        mat->setLayout(layout);
    }

    // Load skeleton from asset data if bones are present
    if (hasBones && data.contains("m_bones"))
    {
        m_skeleton = std::make_shared<Skeleton>();
        // Load bones
        const auto& bonesJson = data.at("m_bones");
        for (const auto& bj : bonesJson)
        {
            BoneInfo bone;
            bone.name = bj.at("name").get<std::string>();
            if (bj.contains("offsetMatrix"))
            {
                const auto& om = bj.at("offsetMatrix");
                for (int i = 0; i < 16 && i < static_cast<int>(om.size()); ++i)
                    bone.offsetMatrix.m[i] = om[i].get<float>();
            }
            m_skeleton->boneNameToIndex[bone.name] = static_cast<int>(m_skeleton->bones.size());
            m_skeleton->bones.push_back(bone);
        }
        // Load node hierarchy
        if (data.contains("m_nodes"))
        {
            const auto& nodesJson = data.at("m_nodes");
            for (const auto& nj : nodesJson)
            {
                Skeleton::Node node;
                node.name = nj.at("name").get<std::string>();
                node.parentIndex = nj.value("parent", -1);
                node.boneIndex = nj.value("boneIndex", -1);
                if (nj.contains("transform"))
                {
                    const auto& t = nj.at("transform");
                    for (int i = 0; i < 16 && i < static_cast<int>(t.size()); ++i)
                        node.localTransform.m[i] = t[i].get<float>();
                }
                m_skeleton->nodes.push_back(node);
            }
            // Build children lists
            for (int i = 0; i < static_cast<int>(m_skeleton->nodes.size()); ++i)
            {
                int parent = m_skeleton->nodes[i].parentIndex;
                if (parent >= 0 && parent < static_cast<int>(m_skeleton->nodes.size()))
                    m_skeleton->nodes[parent].children.push_back(i);
            }
        }
        // Load animations
        if (data.contains("m_animations"))
        {
            const auto& animsJson = data.at("m_animations");
            for (const auto& aj : animsJson)
            {
                AnimationClip clip;
                clip.name = aj.value("name", "");
                clip.duration = aj.value("duration", 0.0f);
                clip.ticksPerSecond = aj.value("ticksPerSecond", 25.0f);
                if (aj.contains("channels"))
                {
                    for (const auto& cj : aj.at("channels"))
                    {
                        BoneChannel ch;
                        ch.boneName = cj.value("boneName", "");
                        auto bit = m_skeleton->boneNameToIndex.find(ch.boneName);
                        ch.boneIndex = (bit != m_skeleton->boneNameToIndex.end()) ? bit->second : -1;
                        if (cj.contains("positionKeys"))
                        {
                            for (const auto& pk : cj.at("positionKeys"))
                            {
                                VectorKey k;
                                k.time = pk.value("t", 0.0f);
                                if (pk.contains("v") && pk.at("v").size() >= 3)
                                {
                                    k.value[0] = pk.at("v")[0].get<float>();
                                    k.value[1] = pk.at("v")[1].get<float>();
                                    k.value[2] = pk.at("v")[2].get<float>();
                                }
                                ch.positionKeys.push_back(k);
                            }
                        }
                        if (cj.contains("rotationKeys"))
                        {
                            for (const auto& rk : cj.at("rotationKeys"))
                            {
                                QuatKey k;
                                k.time = rk.value("t", 0.0f);
                                if (rk.contains("q") && rk.at("q").size() >= 4)
                                {
                                    k.value.x = rk.at("q")[0].get<float>();
                                    k.value.y = rk.at("q")[1].get<float>();
                                    k.value.z = rk.at("q")[2].get<float>();
                                    k.value.w = rk.at("q")[3].get<float>();
                                }
                                ch.rotationKeys.push_back(k);
                            }
                        }
                        if (cj.contains("scalingKeys"))
                        {
                            for (const auto& sk : cj.at("scalingKeys"))
                            {
                                VectorKey k;
                                k.time = sk.value("t", 0.0f);
                                if (sk.contains("v") && sk.at("v").size() >= 3)
                                {
                                    k.value[0] = sk.at("v")[0].get<float>();
                                    k.value[1] = sk.at("v")[1].get<float>();
                                    k.value[2] = sk.at("v")[2].get<float>();
                                }
                                ch.scalingKeys.push_back(k);
                            }
                        }
                        clip.channels.push_back(ch);
                    }
                }
                m_skeleton->animations.push_back(clip);
            }
        }
        logger.log(Logger::Category::Rendering,
            "OpenGLObject3D: Loaded skeleton with " + std::to_string(m_skeleton->bones.size()) + " bones, "
            + std::to_string(m_skeleton->animations.size()) + " animation(s)",
            Logger::LogLevel::INFO);
    }

    if (!mat->build())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to build OpenGL material.", Logger::LogLevel::ERROR);
        return false;
    }

    m_material = mat;
    if (!cacheKey.empty())
    {
        s_materialCache[cacheKey] = m_material;
    }
    return true;
}

void OpenGLObject3D::setLightData(const glm::vec3& position, const glm::vec3& color, float intensity)
{
    if (!m_material)
    {
        return;
    }
    m_material->setLightData(position, color, intensity);
}

void OpenGLObject3D::setLights(const std::vector<OpenGLMaterial::LightData>& lights)
{
    if (!m_material)
    {
        return;
    }
    m_material->setLights(lights);
}

void OpenGLObject3D::setShadowData(GLuint shadowMapArray, const glm::mat4* matrices, const int* lightIndices, int count)
{
    if (!m_material)
    {
        return;
    }
    m_material->setShadowData(shadowMapArray, matrices, lightIndices, count);
}

void OpenGLObject3D::setPointShadowData(GLuint cubeArray, const glm::vec3* positions, const float* farPlanes, const int* lightIndices, int count)
{
    if (!m_material)
    {
        return;
    }
    m_material->setPointShadowData(cubeArray, positions, farPlanes, lightIndices, count);
}

void OpenGLObject3D::setFogData(bool enabled, const glm::vec3& color, float density)
{
    if (!m_material)
    {
        return;
    }
    m_material->setFogData(enabled, color, density);
}

void OpenGLObject3D::setCsmData(GLuint texArray, const glm::mat4* matrices, const float* splits,
                                int lightIndex, bool enabled, const glm::mat4& viewMatrix)
{
    if (!m_material)
    {
        return;
    }
    m_material->setCsmData(texArray, matrices, splits, lightIndex, enabled, viewMatrix);
}

void OpenGLObject3D::setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection)
{
    if (!m_material)
        return;

    m_material->setModelMatrix(model);
    m_material->setViewMatrix(view);
    m_material->setProjectionMatrix(projection);
}

void OpenGLObject3D::render()
{
    if (m_material)
    {
        m_material->render();
    }
}

void OpenGLObject3D::renderBatchContinuation()
{
    if (m_material)
    {
        m_material->renderBatchContinuation();
    }
}

void OpenGLObject3D::setTextures(const std::vector<std::shared_ptr<Texture>>& textures)
{
    if (m_material)
    {
        m_material->setTextures(textures);

        // Compute shader variant key from active texture slots
        ShaderVariantKey key = SVF_NONE;
        if (!textures.empty() && textures[0])        key |= SVF_HAS_DIFFUSE_MAP;
        if (textures.size() >= 2 && textures[1])     key |= SVF_HAS_SPECULAR_MAP;
        if (textures.size() >= 3 && textures[2])     key |= SVF_HAS_NORMAL_MAP;
        if (textures.size() >= 4 && textures[3])     key |= SVF_HAS_EMISSIVE_MAP;
        if (textures.size() >= 5 && textures[4])     key |= SVF_HAS_METALLIC_ROUGHNESS;
        m_variantKey = key;
        m_material->setVariantKey(key);
    }
}

void OpenGLObject3D::setShininess(float shininess)
{
    if (m_material)
    {
        m_material->setShininess(shininess);
    }
}

void OpenGLObject3D::setPbrData(bool enabled, float metallic, float roughness)
{
    if (m_material)
    {
        m_material->setPbrData(enabled, metallic, roughness);
        if (enabled)
            m_variantKey |= SVF_PBR_ENABLED;
        else
            m_variantKey &= ~SVF_PBR_ENABLED;
    }
}

void OpenGLObject3D::setDebugMode(int mode)
{
    if (m_material)
    {
        m_material->setDebugMode(mode);
    }
}

void OpenGLObject3D::setDebugColor(const glm::vec3& color)
{
    if (m_material)
    {
        m_material->setDebugColor(color);
    }
}

void OpenGLObject3D::setNearFarPlanes(float nearPlane, float farPlane)
{
    if (m_material)
    {
        m_material->setNearFarPlanes(nearPlane, farPlane);
    }
}

void OpenGLObject3D::setSkinned(bool skinned)
{
    m_isSkinned = skinned;
    if (m_material)
    {
        m_material->setSkinned(skinned);
    }
}

void OpenGLObject3D::setBoneMatrices(const float* data, int count)
{
    if (m_material)
    {
        m_material->setBoneMatrices(data, count);
    }
}
