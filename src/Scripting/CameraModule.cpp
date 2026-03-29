#include "ScriptingInternal.h"

#include "../Renderer/Renderer.h"
#include <vector>

namespace
{
    auto*& s_renderer = ScriptDetail::s_renderer;

    // ── engine.camera ───────────────────────────────────────────────────

    PyObject* py_get_camera_position(PyObject*, PyObject*)
    {
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        const Vec3 pos = s_renderer->getCameraPosition();
        return Py_BuildValue("(f,f,f)", pos.x, pos.y, pos.z);
    }

    PyObject* py_set_camera_position(PyObject*, PyObject* args)
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!PyArg_ParseTuple(args, "fff", &x, &y, &z))
        {
            return nullptr;
        }
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        s_renderer->setCameraPosition(Vec3{ x, y, z });
        Py_RETURN_TRUE;
    }

    PyObject* py_get_camera_rotation(PyObject*, PyObject*)
    {
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        const Vec2 rot = s_renderer->getCameraRotationDegrees();
        return Py_BuildValue("(f,f)", rot.x, rot.y);
    }

    PyObject* py_set_camera_rotation(PyObject*, PyObject* args)
    {
        float yaw = 0.0f;
        float pitch = 0.0f;
        if (!PyArg_ParseTuple(args, "ff", &yaw, &pitch))
        {
            return nullptr;
        }
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        s_renderer->setCameraRotationDegrees(yaw, pitch);
        Py_RETURN_TRUE;
    }

    PyObject* py_camera_transition_to(PyObject*, PyObject* args)
    {
        float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, duration = 1.0f;
        if (!PyArg_ParseTuple(args, "fffff|f", &x, &y, &z, &yaw, &pitch, &duration))
            return nullptr;
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        s_renderer->startCameraTransition(Vec3{ x, y, z }, yaw, pitch, duration);
        Py_RETURN_TRUE;
    }

    PyObject* py_camera_is_transitioning(PyObject*, PyObject*)
    {
        if (!s_renderer)
            Py_RETURN_FALSE;
        if (s_renderer->isCameraTransitioning())
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyObject* py_camera_cancel_transition(PyObject*, PyObject*)
    {
        if (s_renderer)
            s_renderer->cancelCameraTransition();
        Py_RETURN_TRUE;
    }

    // ---- Cinematic camera path API ----

    PyObject* py_camera_start_path(PyObject*, PyObject* args)
    {
        PyObject* pointsList = nullptr;
        float duration = 1.0f;
        int loopInt = 0;
        if (!PyArg_ParseTuple(args, "Of|p", &pointsList, &duration, &loopInt))
            return nullptr;
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        if (!PyList_Check(pointsList))
        {
            PyErr_SetString(PyExc_TypeError, "First argument must be a list of (x,y,z,yaw,pitch) tuples.");
            return nullptr;
        }
        const Py_ssize_t count = PyList_Size(pointsList);
        if (count < 2)
        {
            PyErr_SetString(PyExc_ValueError, "Camera path requires at least 2 points.");
            return nullptr;
        }
        std::vector<CameraPathPoint> points;
        points.reserve(static_cast<size_t>(count));
        for (Py_ssize_t i = 0; i < count; ++i)
        {
            PyObject* item = PyList_GetItem(pointsList, i);
            float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
            if (!PyArg_ParseTuple(item, "fffff", &x, &y, &z, &yaw, &pitch))
            {
                PyErr_Format(PyExc_TypeError, "Point %zd must be a tuple (x, y, z, yaw, pitch).", i);
                return nullptr;
            }
            CameraPathPoint pt;
            pt.position = Vec3{x, y, z};
            pt.yaw = yaw;
            pt.pitch = pitch;
            points.push_back(pt);
        }
        s_renderer->startCameraPath(points, duration, loopInt != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_camera_is_path_playing(PyObject*, PyObject*)
    {
        if (!s_renderer)
            Py_RETURN_FALSE;
        if (s_renderer->isCameraPathPlaying())
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    PyObject* py_camera_pause_path(PyObject*, PyObject*)
    {
        if (s_renderer)
            s_renderer->pauseCameraPath();
        Py_RETURN_TRUE;
    }

    PyObject* py_camera_resume_path(PyObject*, PyObject*)
    {
        if (s_renderer)
            s_renderer->resumeCameraPath();
        Py_RETURN_TRUE;
    }

    PyObject* py_camera_stop_path(PyObject*, PyObject*)
    {
        if (s_renderer)
            s_renderer->stopCameraPath();
        Py_RETURN_TRUE;
    }

    PyObject* py_camera_get_path_progress(PyObject*, PyObject*)
    {
        if (!s_renderer)
            return PyFloat_FromDouble(0.0);
        return PyFloat_FromDouble(static_cast<double>(s_renderer->getCameraPathProgress()));
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef CameraMethods[] = {
        { "get_camera_position", py_get_camera_position, METH_NOARGS, "Get the camera position." },
        { "set_camera_position", py_set_camera_position, METH_VARARGS, "Set the camera position." },
        { "get_camera_rotation", py_get_camera_rotation, METH_NOARGS, "Get the camera rotation (yaw, pitch)." },
        { "set_camera_rotation", py_set_camera_rotation, METH_VARARGS, "Set the camera rotation (yaw, pitch)." },
        { "transition_to", py_camera_transition_to, METH_VARARGS, "Smooth camera transition to (x,y,z,yaw,pitch[,duration])." },
        { "is_transitioning", py_camera_is_transitioning, METH_NOARGS, "Returns True while a camera transition is active." },
        { "cancel_transition", py_camera_cancel_transition, METH_NOARGS, "Cancel the active camera transition." },
        { "start_path", py_camera_start_path, METH_VARARGS, "Start a spline camera path (points, duration[, loop])." },
        { "is_path_playing", py_camera_is_path_playing, METH_NOARGS, "Returns True while a camera path is playing." },
        { "pause_path", py_camera_pause_path, METH_NOARGS, "Pause the active camera path." },
        { "resume_path", py_camera_resume_path, METH_NOARGS, "Resume a paused camera path." },
        { "stop_path", py_camera_stop_path, METH_NOARGS, "Stop the active camera path." },
        { "get_path_progress", py_camera_get_path_progress, METH_NOARGS, "Get normalised [0,1] progress of the camera path." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef CameraModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.camera",
        "Camera scripting API",
        -1,
        CameraMethods
    };
}

PyObject* CreateCameraModule()
{
    return PyModule_Create(&CameraModuleDef);
}
