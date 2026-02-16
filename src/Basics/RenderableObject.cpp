#include "RenderableObject.h"
#include "Material.h"

void RenderableObject::render()
{
    if (m_material)
    {
        m_material->render();
    }
}
