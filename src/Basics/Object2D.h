#pragma once

#include <memory>
#include <string>
#include <vector>
#include "EngineObject.h"

class Material;

class Object2D : public EngineObject
{
public:
    Object2D() = default;
    ~Object2D() override = default;

    void setMaterial(const std::shared_ptr<Material>& mat) { m_material = mat; }
    std::shared_ptr<Material> getMaterial() const { return m_material; }

    // Geometry (indexed)
    void setVertices(const std::vector<float>& vertices) { m_vertices = vertices; }
    const std::vector<float>& getVertices() const { return m_vertices; }

    void setIndices(const std::vector<uint32_t>& indices) { m_indices = indices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    void render();

private:
    std::shared_ptr<Material> m_material;

    // unique/interleaved vertices (layout-dependent) + indices
    std::vector<float> m_vertices;
    std::vector<uint32_t> m_indices;

    // Material owns its own asset path/textures if needed.
};
