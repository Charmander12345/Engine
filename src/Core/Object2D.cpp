#include "Object2D.h"
#include "EngineObject.h"
#include "Material.h"

void Object2D::render()
{
    if (m_material)
    {
        m_material->render();
    }
}
