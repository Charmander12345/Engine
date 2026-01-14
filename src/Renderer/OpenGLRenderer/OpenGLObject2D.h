#pragma once

#include <memory>
#include <glm/glm.hpp>

class Object2D;
class OpenGLMaterial;

class OpenGLObject2D
{
public:
    explicit OpenGLObject2D(std::shared_ptr<Object2D> cpuObject);

    bool prepare();
    void setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
    void render();

    std::shared_ptr<Object2D> getCpuObject() const { return m_cpuObject; }

private:
    std::shared_ptr<Object2D> m_cpuObject;
    std::shared_ptr<OpenGLMaterial> m_material;
};
