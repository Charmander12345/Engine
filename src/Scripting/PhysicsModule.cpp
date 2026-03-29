#include "ScriptingInternal.h"

#include "../Core/ECS/ECS.h"
#include "../Physics/PhysicsWorld.h"

namespace
{
    // ── engine.physics ──────────────────────────────────────────────────

    PyObject* py_set_velocity(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
            return nullptr;
        auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc) { PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent."); return nullptr; }
        pc->velocity[0] = x; pc->velocity[1] = y; pc->velocity[2] = z;
        Py_RETURN_NONE;
    }

    PyObject* py_get_velocity(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        const auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc) { PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent."); return nullptr; }
        return Py_BuildValue("(fff)", pc->velocity[0], pc->velocity[1], pc->velocity[2]);
    }

    PyObject* py_add_force(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float fx = 0, fy = 0, fz = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &fx, &fy, &fz))
            return nullptr;
        auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc) { PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent."); return nullptr; }
        if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic || pc->mass <= 0.0f) Py_RETURN_NONE;
        const float invMass = 1.0f / pc->mass;
        pc->velocity[0] += fx * invMass;
        pc->velocity[1] += fy * invMass;
        pc->velocity[2] += fz * invMass;
        Py_RETURN_NONE;
    }

    PyObject* py_add_impulse(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float ix = 0, iy = 0, iz = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &ix, &iy, &iz))
            return nullptr;
        auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc) { PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent."); return nullptr; }
        if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic) Py_RETURN_NONE;
        pc->velocity[0] += ix;
        pc->velocity[1] += iy;
        pc->velocity[2] += iz;
        Py_RETURN_NONE;
    }

    PyObject* py_set_gravity(PyObject*, PyObject* args)
    {
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "fff", &x, &y, &z))
            return nullptr;
        PhysicsWorld::Instance().setGravity(x, y, z);
        Py_RETURN_NONE;
    }

    PyObject* py_get_gravity(PyObject*, PyObject*)
    {
        float x, y, z;
        PhysicsWorld::Instance().getGravity(x, y, z);
        return Py_BuildValue("(fff)", x, y, z);
    }

    PyObject* py_set_angular_velocity(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
            return nullptr;
        auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc) { PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent."); return nullptr; }
        pc->angularVelocity[0] = x; pc->angularVelocity[1] = y; pc->angularVelocity[2] = z;
        Py_RETURN_NONE;
    }

    PyObject* py_get_angular_velocity(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        const auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc) { PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent."); return nullptr; }
        return Py_BuildValue("(fff)", pc->angularVelocity[0], pc->angularVelocity[1], pc->angularVelocity[2]);
    }

    PyObject* py_set_on_collision(PyObject*, PyObject* args)
    {
        PyObject* callback = Py_None;
        if (!PyArg_ParseTuple(args, "O", &callback))
            return nullptr;

        if (callback == Py_None)
        {
            if (ScriptDetail::s_onCollision) { Py_DECREF(ScriptDetail::s_onCollision); ScriptDetail::s_onCollision = nullptr; }
            PhysicsWorld::Instance().setCollisionCallback(nullptr);
            Py_RETURN_NONE;
        }

        if (!PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "Argument must be callable or None.");
            return nullptr;
        }

        Py_INCREF(callback);
        if (ScriptDetail::s_onCollision) Py_DECREF(ScriptDetail::s_onCollision);
        ScriptDetail::s_onCollision = callback;

        PhysicsWorld::Instance().setCollisionCallback([](const PhysicsWorld::CollisionEvent& ev)
        {
            if (!ScriptDetail::s_onCollision) return;
            PyGILState_STATE gil = PyGILState_Ensure();
            PyObject* result = PyObject_CallFunction(
                ScriptDetail::s_onCollision, "kk(fff)f(fff)",
                static_cast<unsigned long>(ev.entityA),
                static_cast<unsigned long>(ev.entityB),
                ev.normal[0], ev.normal[1], ev.normal[2],
                ev.depth,
                ev.contactPoint[0], ev.contactPoint[1], ev.contactPoint[2]);
            if (!result) PyErr_Print();
            else Py_DECREF(result);
            PyGILState_Release(gil);
        });

        Py_RETURN_NONE;
    }

    PyObject* py_raycast(PyObject*, PyObject* args)
    {
        float ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0;
        float maxDist = 1000.0f;
        if (!PyArg_ParseTuple(args, "ffffff|f", &ox, &oy, &oz, &dx, &dy, &dz, &maxDist))
            return nullptr;

        auto hit = PhysicsWorld::Instance().raycast(ox, oy, oz, dx, dy, dz, maxDist);
        if (!hit.hit)
            Py_RETURN_NONE;

        return Py_BuildValue("{s:k,s:(fff),s:(fff),s:f}",
            "entity", static_cast<unsigned long>(hit.entity),
            "point", hit.point[0], hit.point[1], hit.point[2],
            "normal", hit.normal[0], hit.normal[1], hit.normal[2],
            "distance", hit.distance);
    }

    PyObject* py_is_body_sleeping(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        if (PhysicsWorld::Instance().isBodySleeping(static_cast<uint32_t>(entity)))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyMethodDef PhysicsMethods[] = {
        { "set_velocity", py_set_velocity, METH_VARARGS, "Set linear velocity (entity, x, y, z)." },
        { "get_velocity", py_get_velocity, METH_VARARGS, "Get linear velocity (entity) -> (x, y, z)." },
        { "add_force", py_add_force, METH_VARARGS, "Apply force (entity, fx, fy, fz). Divided by mass." },
        { "add_impulse", py_add_impulse, METH_VARARGS, "Apply velocity impulse (entity, ix, iy, iz)." },
        { "set_angular_velocity", py_set_angular_velocity, METH_VARARGS, "Set angular velocity (entity, x, y, z)." },
        { "get_angular_velocity", py_get_angular_velocity, METH_VARARGS, "Get angular velocity (entity) -> (x, y, z)." },
        { "set_gravity", py_set_gravity, METH_VARARGS, "Set world gravity (x, y, z)." },
        { "get_gravity", py_get_gravity, METH_NOARGS, "Get world gravity -> (x, y, z)." },
        { "set_on_collision", py_set_on_collision, METH_VARARGS, "Register a collision callback(entityA, entityB, normal, depth, point)." },
        { "raycast", py_raycast, METH_VARARGS, "Raycast(ox,oy,oz,dx,dy,dz[,maxDist]) -> dict or None." },
        { "is_body_sleeping", py_is_body_sleeping, METH_VARARGS, "Check if entity physics body is sleeping." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef PhysicsModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.physics",
        "Physics scripting API",
        -1,
        PhysicsMethods
    };
}

PyObject* CreatePhysicsModule()
{
    return PyModule_Create(&PhysicsModuleDef);
}
