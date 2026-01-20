
#include "Object3D.h"
#include "Material.h"

void Object3D::render()
{
    if (m_material)
    {
        m_material->render();
    }
}
