#pragma once

#include <memory>
#include <string>
#include <vector>
#include "EngineObject.h"

class Material;

class Object3D : public EngineObject
{
public:
    Object3D() = default;
    ~Object3D() override = default;

    void setMaterial(const std::shared_ptr<Material>& mat) { m_material = mat; }
    std::shared_ptr<Material> getMaterial() const { return m_material; }

    void setVertices(const std::vector<float>& vertices) { m_vertices = vertices; }
    const std::vector<float>& getVertices() const { return m_vertices; }

    void setMaterialAssetPath(const std::string& path) { m_materialAssetPath = path; }
    const std::string& getMaterialAssetPath() const { return m_materialAssetPath; }

    void render();
private:
    std::shared_ptr<Material> m_material;
    std::vector<float> m_vertices;
    std::string m_materialAssetPath;
};
