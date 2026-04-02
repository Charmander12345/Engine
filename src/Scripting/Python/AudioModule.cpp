#include "ScriptingInternal.h"

#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Core/AudioManager.h"

namespace
{
    // ── engine.audio ────────────────────────────────────────────────────

    PyObject* py_set_audio_volume(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        float gain = 1.0f;
        if (!PyArg_ParseTuple(args, "kf", &handle, &gain))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().setHandleGain(static_cast<unsigned int>(handle), gain))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_get_audio_volume(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        auto gain = AudioManager::Instance().getHandleGain(static_cast<unsigned int>(handle));
        if (!gain.has_value())
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        return PyFloat_FromDouble(static_cast<double>(gain.value()));
    }

    PyObject* py_is_audio_playing(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (AudioManager::Instance().isSourcePlaying(static_cast<unsigned int>(handle)))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_is_audio_playing_path(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        if (!PyArg_ParseTuple(args, "s", &path))
        {
            return nullptr;
        }

        if (AssetManager::Instance().isAudioPlayingContentPath(path))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_create_audio(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        int loop = 0;
        float gain = 1.0f;
        int keepLoaded = 0;
        if (!PyArg_ParseTuple(args, "s|ifp", &path, &loop, &gain, &keepLoaded))
        {
            return nullptr;
        }

        const std::string absPath = AssetManager::Instance().getAbsoluteContentPath(path);
        if (absPath.empty())
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to resolve audio content path.");
            return nullptr;
        }

        const unsigned int handle = AudioManager::Instance().createAudioPathAsync(absPath, loop != 0, gain);
        if (handle == 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create audio handle.");
            return nullptr;
        }

        return PyLong_FromUnsignedLong(handle);
    }

    PyObject* py_create_audio_from_asset(PyObject*, PyObject* args)
    {
        unsigned long assetId = 0;
        int loop = 0;
        float gain = 1.0f;
        if (!PyArg_ParseTuple(args, "k|if", &assetId, &loop, &gain))
        {
            return nullptr;
        }

        if (assetId == 0)
        {
            PyErr_SetString(PyExc_ValueError, "Asset id must be non-zero.");
            return nullptr;
        }

        const unsigned int handle = AudioManager::Instance().createAudioHandle(static_cast<unsigned int>(assetId), loop != 0, gain);
        if (handle == 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create audio handle.");
            return nullptr;
        }

        return PyLong_FromUnsignedLong(handle);
    }

    PyObject* py_create_audio_from_asset_async(PyObject*, PyObject* args)
    {
        unsigned long assetId = 0;
        PyObject* callback = Py_None;
        int loop = 0;
        float gain = 1.0f;
        if (!PyArg_ParseTuple(args, "kO|if", &assetId, &callback, &loop, &gain))
        {
            return nullptr;
        }

        if (assetId == 0)
        {
            PyErr_SetString(PyExc_ValueError, "Asset id must be non-zero.");
            return nullptr;
        }

        if (callback != Py_None && !PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return nullptr;
        }

        std::shared_ptr<PyObject> callbackRef;
        if (callback != Py_None)
        {
            Py_INCREF(callback);
            callbackRef = std::shared_ptr<PyObject>(callback, [](PyObject* obj)
                {
                    Py_DECREF(obj);
                });
        }

        auto wrappedCallback = [callbackRef](unsigned int realHandle)
            {
                if (!callbackRef)
                {
                    return;
                }
                PyObject* result = PyObject_CallFunction(callbackRef.get(), "k", static_cast<unsigned long>(realHandle));
                if (!result)
                {
                    ScriptDetail::LogPythonError("Python: create_audio_from_asset_async callback failed");
                }
                else
                {
                    Py_DECREF(result);
                }
            };

        const unsigned int handle = AudioManager::Instance().createAudioHandleAsync(
            static_cast<unsigned int>(assetId), std::move(wrappedCallback), loop != 0, gain);
        if (handle == 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to create async audio handle.");
            return nullptr;
        }

        return PyLong_FromUnsignedLong(handle);
    }

    PyObject* py_play_audio(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        int loop = 0;
        float gain = 1.0f;
        int keepLoaded = 0;
        if (!PyArg_ParseTuple(args, "s|ifp", &path, &loop, &gain, &keepLoaded))
        {
            return nullptr;
        }

        const std::string absPath = AssetManager::Instance().getAbsoluteContentPath(path);
        if (absPath.empty())
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to resolve audio content path.");
            return nullptr;
        }

        const unsigned int handle = AudioManager::Instance().playAudioPathAsync(absPath, loop != 0, gain);
        if (handle == 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to play audio asset.");
            return nullptr;
        }

        return PyLong_FromUnsignedLong(handle);
    }

    PyObject* py_play_audio_handle(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().playHandle(static_cast<unsigned int>(handle)))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_pause_audio(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().pauseSource(static_cast<unsigned int>(handle)))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_pause_audio_handle(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().pauseSource(static_cast<unsigned int>(handle)))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_stop_audio(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().stopSource(static_cast<unsigned int>(handle)))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_stop_audio_handle(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().stopSource(static_cast<unsigned int>(handle)))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_invalidate_audio_handle(PyObject*, PyObject* args)
    {
        unsigned long handle = 0;
        if (!PyArg_ParseTuple(args, "k", &handle))
        {
            return nullptr;
        }

        if (!AudioManager::Instance().invalidateHandle(static_cast<unsigned int>(handle)))
        {
            PyErr_SetString(PyExc_ValueError, "Audio handle not found.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef AudioMethods[] = {
        { "create_audio", py_create_audio, METH_VARARGS, "Create an audio handle from a Content-relative path." },
        { "create_audio_from_asset", py_create_audio_from_asset, METH_VARARGS, "Create an audio handle from an asset id." },
        { "create_audio_from_asset_async", py_create_audio_from_asset_async, METH_VARARGS, "Create an audio handle asynchronously from an asset id with callback." },
        { "play_audio", py_play_audio, METH_VARARGS, "Play an audio asset by Content-relative path." },
        { "play_audio_handle", py_play_audio_handle, METH_VARARGS, "Play an audio handle." },
        { "set_audio_volume", py_set_audio_volume, METH_VARARGS, "Set audio handle volume." },
        { "get_audio_volume", py_get_audio_volume, METH_VARARGS, "Get audio handle volume." },
        { "pause_audio", py_pause_audio, METH_VARARGS, "Pause a playing audio handle." },
        { "pause_audio_handle", py_pause_audio_handle, METH_VARARGS, "Pause a playing audio handle." },
        { "is_audio_playing", py_is_audio_playing, METH_VARARGS, "Check if an audio handle is playing." },
        { "is_audio_playing_path", py_is_audio_playing_path, METH_VARARGS, "Check if an audio path is playing." },
        { "stop_audio", py_stop_audio, METH_VARARGS, "Stop a playing audio handle." },
        { "stop_audio_handle", py_stop_audio_handle, METH_VARARGS, "Stop a playing audio handle." },
        { "invalidate_audio_handle", py_invalidate_audio_handle, METH_VARARGS, "Invalidate an audio handle." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef AudioModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.audio",
        "Audio scripting API",
        -1,
        AudioMethods
    };
}

PyObject* CreateAudioModule()
{
    return PyModule_Create(&AudioModuleDef);
}
