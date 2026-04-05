#include "ScriptingInternal.h"

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../NativeScripting/NativeScriptManager.h"

#include <cmath>

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
        case ECS::ComponentKind::Logic:
            return ECS::addComponent<ECS::LogicComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::addComponent<ECS::NameComponent>(entity);
        case ECS::ComponentKind::ParticleEmitter:
            return ECS::addComponent<ECS::ParticleEmitterComponent>(entity);
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
        case ECS::ComponentKind::Logic:
            return ECS::removeComponent<ECS::LogicComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::removeComponent<ECS::NameComponent>(entity);
        case ECS::ComponentKind::ParticleEmitter:
            return ECS::removeComponent<ECS::ParticleEmitterComponent>(entity);
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
        case ECS::ComponentKind::Logic:
            schema.require<ECS::LogicComponent>();
            return true;
        case ECS::ComponentKind::Name:
            schema.require<ECS::NameComponent>();
            return true;
        case ECS::ComponentKind::ParticleEmitter:
            schema.require<ECS::ParticleEmitterComponent>();
            return true;
        default:
            return false;
        }
    }

    // ── engine.entity functions ─────────────────────────────────────────

    PyObject* py_self(PyObject*, PyObject*)
    {
        return PyLong_FromUnsignedLong(ScriptDetail::s_currentEntity);
    }

    PyObject* py_get_position(PyObject*, PyObject* args)
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
        return Py_BuildValue("(fff)", transform->position[0], transform->position[1], transform->position[2]);
    }

    PyObject* py_get_rotation(PyObject*, PyObject* args)
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
        return Py_BuildValue("(fff)", transform->rotation[0], transform->rotation[1], transform->rotation[2]);
    }

    PyObject* py_get_scale(PyObject*, PyObject* args)
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
        return Py_BuildValue("(fff)", transform->scale[0], transform->scale[1], transform->scale[2]);
    }

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
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
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
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
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
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
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
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
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
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
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

    // ── Cross-language helpers ──────────────────────────────────────────

    ScriptValue PyObjectToScriptValue(PyObject* obj)
    {
        if (!obj || obj == Py_None)
        {
            return ScriptValue{};
        }
        if (PyBool_Check(obj))
        {
            return ScriptValue::makeBool(obj == Py_True);
        }
        if (PyLong_Check(obj))
        {
            return ScriptValue::makeInt(static_cast<int>(PyLong_AsLong(obj)));
        }
        if (PyFloat_Check(obj))
        {
            return ScriptValue::makeFloat(static_cast<float>(PyFloat_AsDouble(obj)));
        }
        if (PyUnicode_Check(obj))
        {
            const char* str = PyUnicode_AsUTF8(obj);
            return str ? ScriptValue::makeString(str) : ScriptValue{};
        }
        if (PyTuple_Check(obj) && PyTuple_Size(obj) == 3)
        {
            PyObject* px = PyTuple_GetItem(obj, 0);
            PyObject* py = PyTuple_GetItem(obj, 1);
            PyObject* pz = PyTuple_GetItem(obj, 2);
            if ((PyFloat_Check(px) || PyLong_Check(px)) &&
                (PyFloat_Check(py) || PyLong_Check(py)) &&
                (PyFloat_Check(pz) || PyLong_Check(pz)))
            {
                float x = static_cast<float>(PyFloat_AsDouble(px));
                float y = static_cast<float>(PyFloat_AsDouble(py));
                float z = static_cast<float>(PyFloat_AsDouble(pz));
                return ScriptValue::makeVec3(x, y, z);
            }
        }
        return ScriptValue{};
    }

    PyObject* ScriptValueToPyObject(const ScriptValue& val)
    {
        switch (val.type)
        {
        case ScriptValue::Float:  return PyFloat_FromDouble(val.floatVal);
        case ScriptValue::Int:    return PyLong_FromLong(val.intVal);
        case ScriptValue::Bool:   if (val.boolVal) { Py_RETURN_TRUE; } else { Py_RETURN_FALSE; }
        case ScriptValue::String: return PyUnicode_FromString(val.stringVal.c_str());
        case ScriptValue::Vec3:   return Py_BuildValue("(fff)", val.vec3Val[0], val.vec3Val[1], val.vec3Val[2]);
        case ScriptValue::None:
        default:                  Py_RETURN_NONE;
        }
    }

    // call_function(entity, func_name, *args) -> value
    // Unified: automatically routes to C++ or Python, caller doesn't need to know.
    PyObject* py_call_function(PyObject*, PyObject* args)
    {
        Py_ssize_t argCount = PyTuple_Size(args);
        if (argCount < 2)
        {
            PyErr_SetString(PyExc_TypeError, "call_function requires at least (entity, func_name)");
            return nullptr;
        }

        PyObject* pyEntity = PyTuple_GetItem(args, 0);
        PyObject* pyFunc   = PyTuple_GetItem(args, 1);
        if (!pyEntity || !PyLong_Check(pyEntity) || !pyFunc || !PyUnicode_Check(pyFunc))
        {
            PyErr_SetString(PyExc_TypeError, "call_function(entity: int, func_name: str, ...)");
            return nullptr;
        }
        unsigned long entity = PyLong_AsUnsignedLong(pyEntity);
        const char* funcName = PyUnicode_AsUTF8(pyFunc);

        std::vector<ScriptValue> scriptArgs;
        for (Py_ssize_t i = 2; i < argCount; ++i)
        {
            scriptArgs.push_back(PyObjectToScriptValue(PyTuple_GetItem(args, i)));
        }

        ScriptValue result = NativeScriptManager::Instance().callFunction(
            static_cast<ECS::Entity>(entity), funcName, scriptArgs);
        return ScriptValueToPyObject(result);
    }

    // ── Entity query functions ─────────────────────────────────────────

    PyObject* py_find_entity_by_name(PyObject*, PyObject* args)
    {
        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "s", &name))
        {
            return nullptr;
        }
        if (!name) Py_RETURN_NONE;

        ECS::Schema schema;
        schema.require<ECS::NameComponent>();
        auto entities = ECS::ECSManager::Instance().getEntitiesMatchingSchema(schema);
        for (auto entity : entities)
        {
            const auto* nc = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(entity);
            if (nc && nc->displayName == name)
            {
                return PyLong_FromUnsignedLong(entity);
            }
        }
        return PyLong_FromUnsignedLong(0);
    }

    PyObject* py_get_entity_name(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        const auto* nc = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(static_cast<ECS::Entity>(entity));
        if (!nc || nc->displayName.empty())
        {
            return PyUnicode_FromString("");
        }
        return PyUnicode_FromString(nc->displayName.c_str());
    }

    PyObject* py_set_entity_name(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "ks", &entity, &name))
        {
            return nullptr;
        }
        auto ecsEntity = static_cast<ECS::Entity>(entity);
        auto* nc = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(ecsEntity);
        if (!nc)
        {
            if (!ECS::addComponent<ECS::NameComponent>(ecsEntity))
            {
                Py_RETURN_FALSE;
            }
            nc = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(ecsEntity);
            if (!nc) Py_RETURN_FALSE;
        }
        nc->displayName = name ? name : "";
        Py_RETURN_TRUE;
    }

    PyObject* py_is_entity_valid(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        if (entity == 0) Py_RETURN_FALSE;
        ECS::Schema schema;
        auto entities = ECS::ECSManager::Instance().getEntitiesMatchingSchema(schema);
        for (auto e : entities)
        {
            if (e == static_cast<ECS::Entity>(entity)) Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_remove_entity(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        if (ECS::ECSManager::Instance().removeEntity(static_cast<ECS::Entity>(entity)))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_has_component(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int kind = 0;
        if (!PyArg_ParseTuple(args, "ki", &entity, &kind))
        {
            return nullptr;
        }
        auto ecsEntity = static_cast<ECS::Entity>(entity);
        bool result = false;
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:      result = ECS::hasComponent<ECS::TransformComponent>(ecsEntity); break;
        case ECS::ComponentKind::Mesh:            result = ECS::hasComponent<ECS::MeshComponent>(ecsEntity); break;
        case ECS::ComponentKind::Material:        result = ECS::hasComponent<ECS::MaterialComponent>(ecsEntity); break;
        case ECS::ComponentKind::Light:           result = ECS::hasComponent<ECS::LightComponent>(ecsEntity); break;
        case ECS::ComponentKind::Camera:          result = ECS::hasComponent<ECS::CameraComponent>(ecsEntity); break;
        case ECS::ComponentKind::Physics:         result = ECS::hasComponent<ECS::PhysicsComponent>(ecsEntity); break;
        case ECS::ComponentKind::Collision:       result = ECS::hasComponent<ECS::CollisionComponent>(ecsEntity); break;
        case ECS::ComponentKind::Logic:           result = ECS::hasComponent<ECS::LogicComponent>(ecsEntity); break;
        case ECS::ComponentKind::Name:            result = ECS::hasComponent<ECS::NameComponent>(ecsEntity); break;
        case ECS::ComponentKind::ParticleEmitter: result = ECS::hasComponent<ECS::ParticleEmitterComponent>(ecsEntity); break;
        default:
            PyErr_SetString(PyExc_ValueError, "Unknown component kind.");
            return nullptr;
        }
        if (result) Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyObject* py_get_entity_count(PyObject*, PyObject*)
    {
        ECS::Schema schema;
        auto entities = ECS::ECSManager::Instance().getEntitiesMatchingSchema(schema);
        return PyLong_FromSsize_t(static_cast<Py_ssize_t>(entities.size()));
    }

    PyObject* py_get_all_entities(PyObject*, PyObject*)
    {
        ECS::Schema schema;
        auto entities = ECS::ECSManager::Instance().getEntitiesMatchingSchema(schema);
        PyObject* result = PyList_New(static_cast<Py_ssize_t>(entities.size()));
        for (size_t i = 0; i < entities.size(); ++i)
        {
            PyList_SetItem(result, static_cast<Py_ssize_t>(i), PyLong_FromUnsignedLong(entities[i]));
        }
        return result;
    }

    PyObject* py_distance_between(PyObject*, PyObject* args)
    {
        unsigned long a = 0;
        unsigned long b = 0;
        if (!PyArg_ParseTuple(args, "kk", &a, &b))
        {
            return nullptr;
        }
        const auto* ta = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(static_cast<ECS::Entity>(a));
        const auto* tb = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(static_cast<ECS::Entity>(b));
        if (!ta || !tb)
        {
            PyErr_SetString(PyExc_ValueError, "One or both entities have no TransformComponent.");
            return nullptr;
        }
        float dx = ta->position[0] - tb->position[0];
        float dy = ta->position[1] - tb->position[1];
        float dz = ta->position[2] - tb->position[2];
        return PyFloat_FromDouble(std::sqrt(dx * dx + dy * dy + dz * dz));
    }

    // ── Transform Parenting ─────────────────────────────────────────────

    PyObject* py_set_parent(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        unsigned long parent = 0;
        if (!PyArg_ParseTuple(args, "kk", &entity, &parent))
            return nullptr;
        if (ECS::ECSManager::Instance().setParent(static_cast<ECS::Entity>(entity), static_cast<ECS::Entity>(parent)))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyObject* py_remove_parent(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        if (ECS::ECSManager::Instance().removeParent(static_cast<ECS::Entity>(entity)))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyObject* py_get_parent(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        ECS::Entity parent = ECS::ECSManager::Instance().getParent(static_cast<ECS::Entity>(entity));
        if (parent == ECS::InvalidEntity)
            return PyLong_FromUnsignedLong(0);
        return PyLong_FromUnsignedLong(parent);
    }

    PyObject* py_get_children(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        const auto& children = ECS::ECSManager::Instance().getChildren(static_cast<ECS::Entity>(entity));
        PyObject* list = PyList_New(static_cast<Py_ssize_t>(children.size()));
        for (size_t i = 0; i < children.size(); ++i)
            PyList_SetItem(list, static_cast<Py_ssize_t>(i), PyLong_FromUnsignedLong(children[i]));
        return list;
    }

    PyObject* py_get_child_count(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        return PyLong_FromSsize_t(static_cast<Py_ssize_t>(
            ECS::ECSManager::Instance().getChildren(static_cast<ECS::Entity>(entity)).size()));
    }

    PyObject* py_get_root(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        return PyLong_FromUnsignedLong(ECS::ECSManager::Instance().getRoot(static_cast<ECS::Entity>(entity)));
    }

    PyObject* py_is_ancestor_of(PyObject*, PyObject* args)
    {
        unsigned long ancestor = 0;
        unsigned long descendant = 0;
        if (!PyArg_ParseTuple(args, "kk", &ancestor, &descendant))
            return nullptr;
        if (ECS::ECSManager::Instance().isAncestorOf(static_cast<ECS::Entity>(ancestor), static_cast<ECS::Entity>(descendant)))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyObject* py_get_local_position(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        const auto* tc = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!tc) Py_RETURN_NONE;
        return Py_BuildValue("(fff)", tc->localPosition[0], tc->localPosition[1], tc->localPosition[2]);
    }

    PyObject* py_set_local_position(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
            return nullptr;
        auto* tc = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!tc) { PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent."); return nullptr; }
        tc->localPosition[0] = x; tc->localPosition[1] = y; tc->localPosition[2] = z;
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
        Py_RETURN_TRUE;
    }

    PyObject* py_get_local_rotation(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        const auto* tc = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!tc) Py_RETURN_NONE;
        return Py_BuildValue("(fff)", tc->localRotation[0], tc->localRotation[1], tc->localRotation[2]);
    }

    PyObject* py_set_local_rotation(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
            return nullptr;
        auto* tc = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!tc) { PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent."); return nullptr; }
        tc->localRotation[0] = x; tc->localRotation[1] = y; tc->localRotation[2] = z;
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
        Py_RETURN_TRUE;
    }

    PyObject* py_get_local_scale(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        const auto* tc = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!tc) Py_RETURN_NONE;
        return Py_BuildValue("(fff)", tc->localScale[0], tc->localScale[1], tc->localScale[2]);
    }

    PyObject* py_set_local_scale(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 1, y = 1, z = 1;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
            return nullptr;
        auto* tc = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!tc) { PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent."); return nullptr; }
        tc->localScale[0] = x; tc->localScale[1] = y; tc->localScale[2] = z;
        ECS::ECSManager::Instance().markTransformDirty(static_cast<ECS::Entity>(entity));
        Py_RETURN_TRUE;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef EntityMethods[] = {
        { "self", py_self, METH_NOARGS, "Get the entity currently executing this script." },
        { "create_entity", py_create_entity, METH_NOARGS, "Create an entity." },
        { "remove_entity", py_remove_entity, METH_VARARGS, "Remove an entity." },
        { "attach_component", py_attach_component, METH_VARARGS, "Attach a component by kind." },
        { "detach_component", py_detach_component, METH_VARARGS, "Detach a component by kind." },
        { "has_component", py_has_component, METH_VARARGS, "Check if entity has a component by kind." },
        { "get_entities", py_get_entities, METH_VARARGS, "Get entities matching component kinds." },
        { "get_all_entities", py_get_all_entities, METH_NOARGS, "Get all entities in the scene." },
        { "get_entity_count", py_get_entity_count, METH_NOARGS, "Get the total number of entities." },
        { "find_entity_by_name", py_find_entity_by_name, METH_VARARGS, "Find an entity by display name." },
        { "get_entity_name", py_get_entity_name, METH_VARARGS, "Get the display name of an entity." },
        { "set_entity_name", py_set_entity_name, METH_VARARGS, "Set the display name of an entity." },
        { "is_entity_valid", py_is_entity_valid, METH_VARARGS, "Check if an entity id is valid." },
        { "distance_between", py_distance_between, METH_VARARGS, "Get distance between two entities." },
        { "get_transform", py_get_transform, METH_VARARGS, "Get transform (pos, rot, scale)." },
        { "get_position", py_get_position, METH_VARARGS, "Get entity position -> (x, y, z)." },
        { "get_rotation", py_get_rotation, METH_VARARGS, "Get entity rotation -> (pitch, yaw, roll)." },
        { "get_scale", py_get_scale, METH_VARARGS, "Get entity scale -> (sx, sy, sz)." },
        { "set_position", py_set_position, METH_VARARGS, "Set entity position." },
        { "translate", py_translate, METH_VARARGS, "Translate entity position." },
        { "set_rotation", py_set_rotation, METH_VARARGS, "Set entity rotation." },
        { "rotate", py_rotate, METH_VARARGS, "Rotate entity." },
        { "set_scale", py_set_scale, METH_VARARGS, "Set entity scale." },
        { "set_mesh", py_set_mesh, METH_VARARGS, "Set mesh asset for an entity." },
        { "get_mesh", py_get_mesh, METH_VARARGS, "Get mesh asset path for an entity." },
        { "get_light_color", py_get_light_color, METH_VARARGS, "Get light color (entity) -> (r, g, b)." },
        { "set_light_color", py_set_light_color, METH_VARARGS, "Set light color (entity, r, g, b)." },
        { "call_function", py_call_function, METH_VARARGS, "Call a function on any entity's script (C++ or Python, auto-routed)." },
        { "set_parent", py_set_parent, METH_VARARGS, "Set parent entity (entity, parent) -> bool." },
        { "remove_parent", py_remove_parent, METH_VARARGS, "Remove parent from entity (entity) -> bool." },
        { "get_parent", py_get_parent, METH_VARARGS, "Get parent entity id (entity) -> int (0 if none)." },
        { "get_children", py_get_children, METH_VARARGS, "Get list of child entity ids (entity) -> [int]." },
        { "get_child_count", py_get_child_count, METH_VARARGS, "Get number of children (entity) -> int." },
        { "get_root", py_get_root, METH_VARARGS, "Get root ancestor entity (entity) -> int." },
        { "is_ancestor_of", py_is_ancestor_of, METH_VARARGS, "Check if ancestor is transitive parent of descendant." },
        { "get_local_position", py_get_local_position, METH_VARARGS, "Get local position (entity) -> (x,y,z)." },
        { "set_local_position", py_set_local_position, METH_VARARGS, "Set local position (entity, x, y, z)." },
        { "get_local_rotation", py_get_local_rotation, METH_VARARGS, "Get local rotation (entity) -> (p,y,r)." },
        { "set_local_rotation", py_set_local_rotation, METH_VARARGS, "Set local rotation (entity, p, y, r)." },
        { "get_local_scale", py_get_local_scale, METH_VARARGS, "Get local scale (entity) -> (sx,sy,sz)." },
        { "set_local_scale", py_set_local_scale, METH_VARARGS, "Set local scale (entity, sx, sy, sz)." },
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
