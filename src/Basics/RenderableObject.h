#pragma once

#include <memory>
#include <vector>
#include <cstdint>
#include "EngineObject.h"

class Material;

class RenderableObject : public EngineObject
{
public:
    RenderableObject() = default;
    ~RenderableObject() override = default;

    void setMaterial(const std::shared_ptr<Material>& mat) { m_material = mat; }
    std::shared_ptr<Material> getMaterial() const { return m_material; }

    // Geometry (indexed)
    void setVertices(const std::vector<float>& vertices) { m_vertices = vertices; }
    const std::vector<float>& getVertices() const { return m_vertices; }

    void setIndices(const std::vector<uint32_t>& indices) { m_indices = indices; }
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    void render();

    // Virtual method to determine dimensionality without dynamic_cast
    virtual bool is3D() const { return false; }

protected:
    std::shared_ptr<Material> m_material;
    std::vector<float> m_vertices;
    std::vector<uint32_t> m_indices;
};
