
#include "Object3D.h"
#include "../Renderer/Material.h"
#include "../Renderer/OpenGLRenderer/OpenGLMaterial.h"
#include "MathTypes.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

void Object3D::render()
{
    if (m_material)
    {
        m_material->render();
    }
}

void Object3D::render(const Transform* worldTransform, const void* viewMatrix, const void* projectionMatrix)
{
    if (!m_material)
    {
        return;
    }

    // Cast to OpenGLMaterial to set matrices
    auto glMaterial = std::dynamic_pointer_cast<OpenGLMaterial>(m_material);
    if (!glMaterial)
    {
        // Fallback to simple render if not OpenGLMaterial
        m_material->render();
        return;
    }

    // Set model matrix from transform
    glm::mat4 modelMatrix(1.0f);
    if (worldTransform)
    {
        // Convert our Mat4 (column-major) to glm::mat4
        Mat4 engineMat = worldTransform->getMatrix4ColumnMajor();
        modelMatrix = glm::make_mat4(engineMat.m);
    }

    // Set view and projection matrices
    if (viewMatrix)
    {
        const glm::mat4* view = static_cast<const glm::mat4*>(viewMatrix);
        glMaterial->setViewMatrix(*view);
    }

    if (projectionMatrix)
    {
        const glm::mat4* proj = static_cast<const glm::mat4*>(projectionMatrix);
        glMaterial->setProjectionMatrix(*proj);
    }

    glMaterial->setModelMatrix(modelMatrix);
    glMaterial->render();
}
