#pragma once

#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include "EngineObject.h"

class Material;
class MaterialAsset;
class Texture;

class Object3D : public EngineObject
{
public:
    Object3D() = default;
    ~Object3D() override = default;

    void setMaterial(const std::shared_ptr<Material>& mat) { m_material = mat; }
    std::shared_ptr<Material> getMaterial() const { return m_material; }

    // Geometry (indexed)
    void setVertices(const std::vector<float>& vertices) { m_vertices = vertices; }
    const std::vector<float>& getVertices() const { return m_vertices; }

    void setIndices(const std::vector<uint32_t>& indices) { m_indices = indices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    void setMaterialAssetPath(const std::string& path) { m_materialAssetPath = path; }
    const std::string& getMaterialAssetPath() const { return m_materialAssetPath; }

    // Loaded dependencies (CPU-side assets)
    void setLoadedMaterialAsset(const std::shared_ptr<MaterialAsset>& mat) { m_loadedMaterialAsset = mat; }
    std::shared_ptr<MaterialAsset> getLoadedMaterialAsset() const { return m_loadedMaterialAsset; }

    void setLoadedTextures(const std::vector<std::shared_ptr<Texture>>& textures) { m_loadedTextures = textures; }
    const std::vector<std::shared_ptr<Texture>>& getLoadedTextures() const { return m_loadedTextures; }

    void render();
    void render(const class Transform* worldTransform, const void* viewMatrix, const void* projectionMatrix);
private:
    std::shared_ptr<Material> m_material;

    // unique/interleaved vertices (layout-dependent) + indices
    std::vector<float> m_vertices;
    std::vector<uint32_t> m_indices;

    std::string m_materialAssetPath;

    std::shared_ptr<MaterialAsset> m_loadedMaterialAsset;
    std::vector<std::shared_ptr<Texture>> m_loadedTextures;
};
