#include "ScriptingInternal.h"

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"

namespace
{
    // ── engine.entity helpers ───────────────────────────────────────────

    std::string DescribeEntity(ECS::Entity entity)
    {
        std::string label = "entity " + std::to_string(entity);
        if (const auto* nameComponent = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(entity))
        {
            if (!nameComponent->displayName.empty())
            {
                label += " (" + nameComponent->displayName + ")";
            }
        }
        return label;
    }

    ECS::TransformComponent* GetTransformComponent(ECS::Entity entity)
    {
        return ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
    }

    bool AddComponentByKind(ECS::Entity entity, int kind)
    {
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:
            return ECS::addComponent<ECS::TransformComponent>(entity);
        case ECS::ComponentKind::Mesh:
            return ECS::addComponent<ECS::MeshComponent>(entity);
        case ECS::ComponentKind::Material:
            return ECS::addComponent<ECS::MaterialComponent>(entity);
        case ECS::ComponentKind::Light:
            return ECS::addComponent<ECS::LightComponent>(entity);
        case ECS::ComponentKind::Camera:
            return ECS::addComponent<ECS::CameraComponent>(entity);
        case ECS::ComponentKind::Physics:
            return ECS::addComponent<ECS::PhysicsComponent>(entity);
        case ECS::ComponentKind::Collision:
            return ECS::addComponent<ECS::CollisionComponent>(entity);
        case ECS::ComponentKind::Script:
            return ECS::addComponent<ECS::ScriptComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::addComponent<ECS::NameComponent>(entity);
        case ECS::ComponentKind::ParticleEmitter:
            return ECS::addComponent<ECS::ParticleEmitterComponent>(entity);
        case ECS::ComponentKind::NativeScript:
            return ECS::addComponent<ECS::NativeScriptComponent>(entity);
        default:
            return false;
        }
    }

    bool RemoveComponentByKind(ECS::Entity entity, int kind)
    {
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:
            return ECS::removeComponent<ECS::TransformComponent>(entity);
        case ECS::ComponentKind::Mesh:
            return ECS::removeComponent<ECS::MeshComponent>(entity);
        case ECS::ComponentKind::Material:
            return ECS::removeComponent<ECS::MaterialComponent>(entity);
        case ECS::ComponentKind::Light:
            return ECS::removeComponent<ECS::LightComponent>(entity);
        case ECS::ComponentKind::Camera:
            return ECS::removeComponent<ECS::CameraComponent>(entity);
        case ECS::ComponentKind::Physics:
            return ECS::removeComponent<ECS::PhysicsComponent>(entity);
        case ECS::ComponentKind::Collision:
            return ECS::removeComponent<ECS::CollisionComponent>(entity);
        case ECS::ComponentKind::Script:
            return ECS::removeComponent<ECS::ScriptComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::removeComponent<ECS::NameComponent>(entity);
        case ECS::ComponentKind::ParticleEmitter:
            return ECS::removeComponent<ECS::ParticleEmitterComponent>(entity);
        case ECS::ComponentKind::NativeScript:
            return ECS::removeComponent<ECS::NativeScriptComponent>(entity);
        default:
            return false;
        }
    }

    bool RequireComponentByKind(ECS::Schema& schema, int kind)
    {
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:
            schema.require<ECS::TransformComponent>();
            return true;
        case ECS::ComponentKind::Mesh:
            schema.require<ECS::MeshComponent>();
            return true;
        case ECS::ComponentKind::Material:
            schema.require<ECS::MaterialComponent>();
            return true;
        case ECS::ComponentKind::Light:
            schema.require<ECS::LightComponent>();
            return true;
        case ECS::ComponentKind::Camera:
            schema.require<ECS::CameraComponent>();
            return true;
        case ECS::ComponentKind::Physics:
            schema.require<ECS::PhysicsComponent>();
            return true;
        case ECS::ComponentKind::Collision:
            schema.require<ECS::CollisionComponent>();
            return true;
        case ECS::ComponentKind::Script:
            schema.require<ECS::ScriptComponent>();
            return true;
        case ECS::ComponentKind::Name:
            schema.require<ECS::NameComponent>();
            return true;
        case ECS::ComponentKind::ParticleEmitter:
            schema.require<ECS::ParticleEmitterComponent>();
            return true;
        case ECS::ComponentKind::NativeScript:
            schema.require<ECS::NativeScriptComponent>();
            return true;
        default:
            return false;
        }
    }

    // ── engine.entity functions ─────────────────────────────────────────

    PyObject* py_create_entity(PyObject*, PyObject*)
    {
        const ECS::Entity entity = ECS::createEntity();
        return PyLong_FromUnsignedLong(entity);
    }

    PyObject* py_attach_component(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int kind = 0;
        if (!PyArg_ParseTuple(args, "ki", &entity, &kind))
        {
            return nullptr;
        }
        const bool result = AddComponentByKind(static_cast<ECS::Entity>(entity), kind);
        if (!result)
        {
            PyErr_SetString(PyExc_ValueError, "Unknown component kind or add failed.");
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_detach_component(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int kind = 0;
        if (!PyArg_ParseTuple(args, "ki", &entity, &kind))
        {
            return nullptr;
        }
        const bool result = RemoveComponentByKind(static_cast<ECS::Entity>(entity), kind);
        if (!result)
        {
            PyErr_SetString(PyExc_ValueError, "Unknown component kind or remove failed.");
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_get_entities(PyObject*, PyObject* args)
    {
        PyObject* listObj = nullptr;
        if (!PyArg_ParseTuple(args, "O", &listObj))
        {
            return nullptr;
        }
        PyObject* seq = PySequence_Fast(listObj, "Expected a sequence of component kinds");
        if (!seq)
        {
            return nullptr;
        }

        ECS::Schema schema;
        const Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
        PyObject** items = PySequence_Fast_ITEMS(seq);
        for (Py_ssize_t i = 0; i < count; ++i)
        {
            const int kind = static_cast<int>(PyLong_AsLong(items[i]));
            if (PyErr_Occurred())
            {
                Py_DECREF(seq);
                return nullptr;
            }
            if (!RequireComponentByKind(schema, kind))
            {
                Py_DECREF(seq);
                PyErr_SetString(PyExc_ValueError, "Unknown component kind in schema.");
                return nullptr;
            }
        }

        const auto entities = ECS::getEntitiesMatchingSchema(schema);
        PyObject* result = PyList_New(static_cast<Py_ssize_t>(entities.size()));
        for (size_t i = 0; i < entities.size(); ++i)
        {
            PyList_SetItem(result, static_cast<Py_ssize_t>(i), PyLong_FromUnsignedLong(entities[i]));
        }
        Py_DECREF(seq);
        return result;
    }

    PyObject* py_get_transform(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        const auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            Py_RETURN_NONE;
        }
        PyObject* position = Py_BuildValue("(fff)", transform->position[0], transform->position[1], transform->position[2]);
        PyObject* rotation = Py_BuildValue("(fff)", transform->rotation[0], transform->rotation[1], transform->rotation[2]);
        PyObject* scale = Py_BuildValue("(fff)", transform->scale[0], transform->scale[1], transform->scale[2]);
        PyObject* result = Py_BuildValue("(OOO)", position, rotation, scale);
        Py_DECREF(position);
        Py_DECREF(rotation);
        Py_DECREF(scale);
        return result;
    }

    PyObject* py_set_position(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->position[0] = x;
        transform->position[1] = y;
        transform->position[2] = z;
        Py_RETURN_TRUE;
    }

    PyObject* py_translate(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float dx = 0.0f;
        float dy = 0.0f;
        float dz = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &dx, &dy, &dz))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->position[0] += dx;
        transform->position[1] += dy;
        transform->position[2] += dz;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_rotation(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &pitch, &yaw, &roll))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->rotation[0] = pitch;
        transform->rotation[1] = yaw;
        transform->rotation[2] = roll;
        Py_RETURN_TRUE;
    }

    PyObject* py_rotate(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float dp = 0.0f;
        float dy = 0.0f;
        float dr = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &dp, &dy, &dr))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->rotation[0] += dp;
        transform->rotation[1] += dy;
        transform->rotation[2] += dr;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_scale(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float sx = 1.0f;
        float sy = 1.0f;
        float sz = 1.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &sx, &sy, &sz))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->scale[0] = sx;
        transform->scale[1] = sy;
        transform->scale[2] = sz;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_mesh(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        const char* path = nullptr;
        if (!PyArg_ParseTuple(args, "ks", &entity, &path))
        {
            return nullptr;
        }
        ECS::Entity ecsEntity = static_cast<ECS::Entity>(entity);
        if (!ECS::hasComponent<ECS::MeshComponent>(ecsEntity))
        {
            if (!ECS::addComponent<ECS::MeshComponent>(ecsEntity))
            {
                PyErr_SetString(PyExc_ValueError, "Failed to add MeshComponent.");
                return nullptr;
            }
        }

        int assetId = AssetManager::Instance().loadAsset(path, AssetType::Model3D, AssetManager::Sync);
        if (assetId == 0)
        {
            assetId = AssetManager::Instance().loadAsset(path, AssetType::Model2D, AssetManager::Sync);
        }
        if (assetId == 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to load mesh asset.");
            return nullptr;
        }

        ECS::MeshComponent comp{};
        comp.meshAssetPath = path;
        comp.meshAssetId = static_cast<unsigned int>(assetId);
        if (!ECS::setComponent<ECS::MeshComponent>(ecsEntity, comp))
        {
            PyErr_SetString(PyExc_ValueError, "Failed to set MeshComponent.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_get_mesh(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        ECS::Entity ecsEntity = static_cast<ECS::Entity>(entity);
        const auto* comp = ECS::ECSManager::Instance().getComponent<ECS::MeshComponent>(ecsEntity);
        if (!comp || comp->meshAssetPath.empty())
        {
            Py_RETURN_NONE;
        }
        return PyUnicode_FromString(comp->meshAssetPath.c_str());
    }

    PyObject* py_get_light_color(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        const auto* comp = ECS::ECSManager::Instance().getComponent<ECS::LightComponent>(static_cast<ECS::Entity>(entity));
        if (!comp)
        {
            Py_RETURN_NONE;
        }
        return Py_BuildValue("(fff)", comp->color[0], comp->color[1], comp->color[2]);
    }

    PyObject* py_set_light_color(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &r, &g, &b))
        {
            return nullptr;
        }
        auto* comp = ECS::ECSManager::Instance().getComponent<ECS::LightComponent>(static_cast<ECS::Entity>(entity));
        if (!comp)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no LightComponent.");
            return nullptr;
        }
        comp->color[0] = r;
        comp->color[1] = g;
        comp->color[2] = b;
        Py_RETURN_TRUE;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef EntityMethods[] = {
        { "create_entity", py_create_entity, METH_NOARGS, "Create an entity." },
        { "attach_component", py_attach_component, METH_VARARGS, "Attach a component by kind." },
        { "detach_component", py_detach_component, METH_VARARGS, "Detach a component by kind." },
        { "get_entities", py_get_entities, METH_VARARGS, "Get entities matching component kinds." },
        { "get_transform", py_get_transform, METH_VARARGS, "Get transform (pos, rot, scale)." },
        { "set_position", py_set_position, METH_VARARGS, "Set entity position." },
        { "translate", py_translate, METH_VARARGS, "Translate entity position." },
        { "set_rotation", py_set_rotation, METH_VARARGS, "Set entity rotation." },
        { "rotate", py_rotate, METH_VARARGS, "Rotate entity." },
        { "set_scale", py_set_scale, METH_VARARGS, "Set entity scale." },
        { "set_mesh", py_set_mesh, METH_VARARGS, "Set mesh asset for an entity." },
        { "get_mesh", py_get_mesh, METH_VARARGS, "Get mesh asset path for an entity." },
        { "get_light_color", py_get_light_color, METH_VARARGS, "Get light color (entity) -> (r, g, b)." },
        { "set_light_color", py_set_light_color, METH_VARARGS, "Set light color (entity, r, g, b)." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef EntityModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.entity",
        "Entity scripting API",
        -1,
        EntityMethods
    };
}

PyObject* CreateEntityModule()
{
    return PyModule_Create(&EntityModuleDef);
}
