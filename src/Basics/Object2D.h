#pragma once

#include <memory>
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

    void setVertices(const std::vector<float>& vertices) { m_vertices = vertices; }
    const std::vector<float>& getVertices() const { return m_vertices; }

private:
    std::shared_ptr<Material> m_material;
    std::vector<float> m_vertices;
};
