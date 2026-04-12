#include "ScriptingInternal.h"

#include "../Renderer/Renderer.h"

namespace
{
    // ── engine.animation ────────────────────────────────────────────────

    PyObject* py_is_skinned(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        return PyBool_FromLong(ScriptDetail::s_renderer->isEntitySkinned(static_cast<unsigned int>(entity)));
    }

    PyObject* py_get_clip_count(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        int count = ScriptDetail::s_renderer
            ? ScriptDetail::s_renderer->getEntityAnimationClipCount(static_cast<unsigned int>(entity))
            : 0;
        return PyLong_FromLong(count);
    }

    PyObject* py_find_clip_by_name(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "ks", &entity, &name))
            return nullptr;
        int idx = ScriptDetail::s_renderer
            ? ScriptDetail::s_renderer->findEntityAnimationClipByName(static_cast<unsigned int>(entity), name)
            : -1;
        return PyLong_FromLong(idx);
    }

    PyObject* py_play(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int clip = 0;
        int loop = 1;
        if (!PyArg_ParseTuple(args, "ki|p", &entity, &clip, &loop))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->playEntityAnimation(static_cast<unsigned int>(entity), clip, loop != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_stop(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->stopEntityAnimation(static_cast<unsigned int>(entity));
        Py_RETURN_TRUE;
    }

    PyObject* py_set_speed(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float speed = 1.0f;
        if (!PyArg_ParseTuple(args, "kf", &entity, &speed))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->setEntityAnimationSpeed(static_cast<unsigned int>(entity), speed);
        Py_RETURN_TRUE;
    }

    PyObject* py_crossfade(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int toClip = 0;
        float duration = 0.25f;
        int loop = 1;
        if (!PyArg_ParseTuple(args, "kif|p", &entity, &toClip, &duration, &loop))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->crossfadeEntityAnimation(
            static_cast<unsigned int>(entity), toClip, duration, loop != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_is_crossfading(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        return PyBool_FromLong(ScriptDetail::s_renderer->isEntityCrossfading(static_cast<unsigned int>(entity)));
    }

    PyObject* py_play_on_layer(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int layer = 0, clip = 0, loop = 1;
        float crossfadeDur = 0.0f;
        if (!PyArg_ParseTuple(args, "kii|pf", &entity, &layer, &clip, &loop, &crossfadeDur))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->playEntityAnimationOnLayer(
            static_cast<unsigned int>(entity), layer, clip, loop != 0, crossfadeDur);
        Py_RETURN_TRUE;
    }

    PyObject* py_stop_layer(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int layer = 0;
        if (!PyArg_ParseTuple(args, "ki", &entity, &layer))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->stopEntityAnimationLayer(static_cast<unsigned int>(entity), layer);
        Py_RETURN_TRUE;
    }

    PyObject* py_set_layer_weight(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int layer = 0;
        float weight = 1.0f;
        if (!PyArg_ParseTuple(args, "kif", &entity, &layer, &weight))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->setEntityLayerWeight(static_cast<unsigned int>(entity), layer, weight);
        Py_RETURN_TRUE;
    }

    PyObject* py_get_layer_count(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
            return nullptr;
        int count = ScriptDetail::s_renderer
            ? ScriptDetail::s_renderer->getEntityAnimationLayerCount(static_cast<unsigned int>(entity))
            : 0;
        return PyLong_FromLong(count);
    }

    PyObject* py_set_layer_count(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int count = 1;
        if (!PyArg_ParseTuple(args, "ki", &entity, &count))
            return nullptr;
        if (!ScriptDetail::s_renderer) Py_RETURN_FALSE;
        ScriptDetail::s_renderer->setEntityAnimationLayerCount(static_cast<unsigned int>(entity), count);
        Py_RETURN_TRUE;
    }

    PyMethodDef AnimationMethods[] = {
        { "is_skinned",       py_is_skinned,       METH_VARARGS, "Check if entity has skeletal mesh (entity)." },
        { "get_clip_count",   py_get_clip_count,   METH_VARARGS, "Get animation clip count (entity)." },
        { "find_clip",        py_find_clip_by_name, METH_VARARGS, "Find clip index by name (entity, name) -> int (-1 if not found)." },
        { "play",             py_play,              METH_VARARGS, "Play animation clip (entity, clip[, loop])." },
        { "stop",             py_stop,              METH_VARARGS, "Stop animation (entity)." },
        { "set_speed",        py_set_speed,         METH_VARARGS, "Set playback speed (entity, speed)." },
        { "crossfade",        py_crossfade,         METH_VARARGS, "Crossfade to clip (entity, clip, duration[, loop])." },
        { "is_crossfading",   py_is_crossfading,    METH_VARARGS, "Check if crossfading (entity)." },
        { "play_on_layer",    py_play_on_layer,     METH_VARARGS, "Play on layer (entity, layer, clip[, loop, crossfade_dur])." },
        { "stop_layer",       py_stop_layer,        METH_VARARGS, "Stop animation layer (entity, layer)." },
        { "set_layer_weight", py_set_layer_weight,  METH_VARARGS, "Set layer blend weight (entity, layer, weight)." },
        { "get_layer_count",  py_get_layer_count,   METH_VARARGS, "Get layer count (entity)." },
        { "set_layer_count",  py_set_layer_count,   METH_VARARGS, "Set layer count (entity, count)." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef AnimationModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.animation",
        "Skeletal animation scripting API (blending, crossfade, layers)",
        -1,
        AnimationMethods
    };
}

PyObject* CreateAnimationModule()
{
    return PyModule_Create(&AnimationModuleDef);
}
