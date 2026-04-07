#include "ScriptingInternal.h"

#include "../Core/ECS/ECS.h"
#include "../Physics/PhysicsWorld.h"

namespace
{
    // ── engine.physics ──────────────────────────────────────────────────

    static ECS::PhysicsComponent* getPhysicsComponentOrSetError(uint32_t entity)
    {
        auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(static_cast<ECS::Entity>(entity));
        if (!pc)
            PyErr_SetString(PyExc_ValueError, "Entity has no PhysicsComponent.");
        return pc;
    }

    PyObject* py_set_velocity(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0, y = 0, z = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
            return nullptr;

        auto* pc = getPhysicsComponentOrSetError(static_cast<uint32_t>(entity));
        if (!pc) return nullptr;

        pc->velocity[0] = x; pc->velocity[1] = y; pc->velocity[2] = z;
        PhysicsWorld::Instance().setVelocity(static_cast<uint32_t>(entity), x, y, z);
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

        auto* pc = getPhysicsComponentOrSetError(static_cast<uint32_t>(entity));
        if (!pc) return nullptr;
        if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic) Py_RETURN_NONE;

        PhysicsWorld::Instance().addForce(static_cast<uint32_t>(entity), fx, fy, fz);
        Py_RETURN_NONE;
    }

    PyObject* py_add_impulse(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float ix = 0, iy = 0, iz = 0;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &ix, &iy, &iz))
            return nullptr;

        auto* pc = getPhysicsComponentOrSetError(static_cast<uint32_t>(entity));
        if (!pc) return nullptr;
        if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic) Py_RETURN_NONE;

        pc->velocity[0] += ix;
        pc->velocity[1] += iy;
        pc->velocity[2] += iz;
        PhysicsWorld::Instance().addImpulse(static_cast<uint32_t>(entity), ix, iy, iz);
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

        auto* pc = getPhysicsComponentOrSetError(static_cast<uint32_t>(entity));
        if (!pc) return nullptr;

        pc->angularVelocity[0] = x; pc->angularVelocity[1] = y; pc->angularVelocity[2] = z;
        PhysicsWorld::Instance().setAngularVelocity(static_cast<uint32_t>(entity), x, y, z);
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

    PyObject* py_overlap_sphere(PyObject*, PyObject* args)
    {
        float cx = 0, cy = 0, cz = 0, radius = 1.0f;
        if (!PyArg_ParseTuple(args, "ffff", &cx, &cy, &cz, &radius))
            return nullptr;

        auto entities = PhysicsWorld::Instance().overlapSphere(cx, cy, cz, radius);
        PyObject* list = PyList_New(static_cast<Py_ssize_t>(entities.size()));
        for (size_t i = 0; i < entities.size(); ++i)
            PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i),
                PyLong_FromUnsignedLong(static_cast<unsigned long>(entities[i])));
        return list;
    }

    PyObject* py_overlap_box(PyObject*, PyObject* args)
    {
        float cx = 0, cy = 0, cz = 0, hx = 0.5f, hy = 0.5f, hz = 0.5f;
        float ex = 0, ey = 0, ez = 0;
        if (!PyArg_ParseTuple(args, "ffffff|fff", &cx, &cy, &cz, &hx, &hy, &hz, &ex, &ey, &ez))
            return nullptr;

        auto entities = PhysicsWorld::Instance().overlapBox(cx, cy, cz, hx, hy, hz, ex, ey, ez);
        PyObject* list = PyList_New(static_cast<Py_ssize_t>(entities.size()));
        for (size_t i = 0; i < entities.size(); ++i)
            PyList_SET_ITEM(list, static_cast<Py_ssize_t>(i),
                PyLong_FromUnsignedLong(static_cast<unsigned long>(entities[i])));
        return list;
    }

    PyObject* py_sweep_sphere(PyObject*, PyObject* args)
    {
        float ox = 0, oy = 0, oz = 0, radius = 0.5f;
        float dx = 0, dy = 0, dz = 0, maxDist = 1000.0f;
        if (!PyArg_ParseTuple(args, "fffffff|f", &ox, &oy, &oz, &radius, &dx, &dy, &dz, &maxDist))
            return nullptr;

        auto hit = PhysicsWorld::Instance().sweepSphere(ox, oy, oz, radius, dx, dy, dz, maxDist);
        if (!hit.hit) Py_RETURN_NONE;

        return Py_BuildValue("{s:k,s:(fff),s:(fff),s:f}",
            "entity", static_cast<unsigned long>(hit.entity),
            "point", hit.point[0], hit.point[1], hit.point[2],
            "normal", hit.normal[0], hit.normal[1], hit.normal[2],
            "distance", hit.distance);
    }

    PyObject* py_sweep_box(PyObject*, PyObject* args)
    {
        float ox = 0, oy = 0, oz = 0;
        float hx = 0.5f, hy = 0.5f, hz = 0.5f;
        float dx = 0, dy = 0, dz = 0, maxDist = 1000.0f;
        if (!PyArg_ParseTuple(args, "fffffffff|f", &ox, &oy, &oz, &hx, &hy, &hz, &dx, &dy, &dz, &maxDist))
            return nullptr;

        auto hit = PhysicsWorld::Instance().sweepBox(ox, oy, oz, hx, hy, hz, dx, dy, dz, maxDist);
        if (!hit.hit) Py_RETURN_NONE;

        return Py_BuildValue("{s:k,s:(fff),s:(fff),s:f}",
            "entity", static_cast<unsigned long>(hit.entity),
            "point", hit.point[0], hit.point[1], hit.point[2],
            "normal", hit.normal[0], hit.normal[1], hit.normal[2],
            "distance", hit.distance);
    }

    PyObject* py_add_force_at_position(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float fx = 0, fy = 0, fz = 0, px = 0, py_pos = 0, pz = 0;
        if (!PyArg_ParseTuple(args, "kffffff", &entity, &fx, &fy, &fz, &px, &py_pos, &pz))
            return nullptr;
        PhysicsWorld::Instance().addForceAtPosition(
            static_cast<uint32_t>(entity), fx, fy, fz, px, py_pos, pz);
        Py_RETURN_NONE;
    }

    PyObject* py_add_impulse_at_position(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float ix = 0, iy = 0, iz = 0, px = 0, py_pos = 0, pz = 0;
        if (!PyArg_ParseTuple(args, "kffffff", &entity, &ix, &iy, &iz, &px, &py_pos, &pz))
            return nullptr;
        PhysicsWorld::Instance().addImpulseAtPosition(
            static_cast<uint32_t>(entity), ix, iy, iz, px, py_pos, pz);
        Py_RETURN_NONE;
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
        { "overlap_sphere", py_overlap_sphere, METH_VARARGS, "Overlap sphere(cx,cy,cz,radius) -> [entity, ...]." },
        { "overlap_box", py_overlap_box, METH_VARARGS, "Overlap box(cx,cy,cz,hx,hy,hz[,ex,ey,ez]) -> [entity, ...]." },
        { "sweep_sphere", py_sweep_sphere, METH_VARARGS, "Sweep sphere(ox,oy,oz,radius,dx,dy,dz[,maxDist]) -> dict or None." },
        { "sweep_box", py_sweep_box, METH_VARARGS, "Sweep box(ox,oy,oz,hx,hy,hz,dx,dy,dz[,maxDist]) -> dict or None." },
        { "add_force_at_position", py_add_force_at_position, METH_VARARGS, "Apply force at world position (entity,fx,fy,fz,px,py,pz)." },
        { "add_impulse_at_position", py_add_impulse_at_position, METH_VARARGS, "Apply impulse at world position (entity,ix,iy,iz,px,py,pz)." },
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
