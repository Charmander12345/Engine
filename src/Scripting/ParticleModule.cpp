#include "ScriptingInternal.h"

#include "../Core/ECS/ECS.h"

namespace
{
    // ── engine.particle ─────────────────────────────────────────────────

    PyObject* py_set_emitter(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        const char* key = nullptr;
        float value = 0.0f;
        if (!PyArg_ParseTuple(args, "Isf", &entity, &key, &value))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        std::string k(key);
        if      (k == "emissionRate")  c->emissionRate  = value;
        else if (k == "lifetime")      c->lifetime      = value;
        else if (k == "speed")         c->speed         = value;
        else if (k == "speedVariance") c->speedVariance = value;
        else if (k == "size")          c->size          = value;
        else if (k == "sizeEnd")       c->sizeEnd       = value;
        else if (k == "gravity")       c->gravity       = value;
        else if (k == "coneAngle")     c->coneAngle     = value;
        else if (k == "maxParticles")  c->maxParticles  = static_cast<int>(value);
        else if (k == "colorR")        c->colorR        = value;
        else if (k == "colorG")        c->colorG        = value;
        else if (k == "colorB")        c->colorB        = value;
        else if (k == "colorA")        c->colorA        = value;
        else if (k == "colorEndR")     c->colorEndR     = value;
        else if (k == "colorEndG")     c->colorEndG     = value;
        else if (k == "colorEndB")     c->colorEndB     = value;
        else if (k == "colorEndA")     c->colorEndA     = value;
        else { PyErr_Format(PyExc_KeyError, "Unknown emitter key: %s", key); return nullptr; }
        Py_RETURN_TRUE;
    }

    PyObject* py_set_emitter_enabled(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        int enabled = 1;
        if (!PyArg_ParseTuple(args, "I|p", &entity, &enabled))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        c->enabled = (enabled != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_set_emitter_color(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        float r = 1, g = 1, b = 1, a = 1;
        if (!PyArg_ParseTuple(args, "Iffff", &entity, &r, &g, &b, &a))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        c->colorR = r; c->colorG = g; c->colorB = b; c->colorA = a;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_emitter_end_color(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        float r = 1, g = 0, b = 0, a = 0;
        if (!PyArg_ParseTuple(args, "Iffff", &entity, &r, &g, &b, &a))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        c->colorEndR = r; c->colorEndG = g; c->colorEndB = b; c->colorEndA = a;
        Py_RETURN_TRUE;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef ParticleMethods[] = {
        { "set_emitter", py_set_emitter, METH_VARARGS, "Set a particle emitter property by key (entity, key, value)." },
        { "set_enabled", py_set_emitter_enabled, METH_VARARGS, "Enable/disable a particle emitter (entity, enabled)." },
        { "set_color", py_set_emitter_color, METH_VARARGS, "Set emitter start color (entity, r, g, b, a)." },
        { "set_end_color", py_set_emitter_end_color, METH_VARARGS, "Set emitter end color (entity, r, g, b, a)." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef ParticleModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.particle",
        "Particle emitter scripting API",
        -1,
        ParticleMethods
    };
}

PyObject* CreateParticleModule()
{
    return PyModule_Create(&ParticleModuleDef);
}
