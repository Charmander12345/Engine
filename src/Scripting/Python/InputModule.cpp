#include "ScriptingInternal.h"

#include "../Physics/PhysicsWorld.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <unordered_set>

// Definitions for input callback state declared in ScriptingInternal.h.
namespace ScriptDetail
{
    PyObject* s_onKeyPressed{ nullptr };
    PyObject* s_onKeyReleased{ nullptr };
    std::unordered_map<int, std::vector<PyObject*>> s_keyPressedCallbacks;
    std::unordered_map<int, std::vector<PyObject*>> s_keyReleasedCallbacks;

    void InvokeKeyCallbacks(PyObject* callback, int key)
    {
        if (!callback)
        {
            return;
        }
        PyObject* result = PyObject_CallFunction(callback, "i", key);
        if (!result)
        {
            LogPythonError("Python: key callback failed");
            return;
        }
        Py_DECREF(result);
    }

    void InvokeKeyCallbacksForKey(const std::unordered_map<int, std::vector<PyObject*>>& map, int key)
    {
        auto it = map.find(key);
        if (it == map.end())
        {
            return;
        }
        for (auto* callback : it->second)
        {
            InvokeKeyCallbacks(callback, key);
        }
    }

    void ClearKeyCallbacks()
    {
        if (s_onKeyPressed)
        {
            Py_DECREF(s_onKeyPressed);
            s_onKeyPressed = nullptr;
        }
        if (s_onKeyReleased)
        {
            Py_DECREF(s_onKeyReleased);
            s_onKeyReleased = nullptr;
        }
        for (auto& [key, callbacks] : s_keyPressedCallbacks)
        {
            for (auto* callback : callbacks)
            {
                Py_DECREF(callback);
            }
        }
        for (auto& [key, callbacks] : s_keyReleasedCallbacks)
        {
            for (auto* callback : callbacks)
            {
                Py_DECREF(callback);
            }
        }
        s_keyPressedCallbacks.clear();
        s_keyReleasedCallbacks.clear();
        if (s_onCollision)
        {
            Py_DECREF(s_onCollision);
            s_onCollision = nullptr;
        }
        PhysicsWorld::Instance().setCollisionCallback(nullptr);
    }
}

namespace
{
    // ── engine.input helpers ────────────────────────────────────────────

    bool StoreKeyCallback(PyObject* callback, PyObject*& target)
    {
        if (callback == Py_None)
        {
            if (target)
            {
                Py_DECREF(target);
                target = nullptr;
            }
            return true;
        }
        if (!PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return false;
        }
        Py_INCREF(callback);
        if (target)
        {
            Py_DECREF(target);
        }
        target = callback;
        return true;
    }

    bool StoreKeyCallbackForKey(PyObject* callback, std::unordered_map<int, std::vector<PyObject*>>& map, int key)
    {
        if (!PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return false;
        }
        Py_INCREF(callback);
        map[key].push_back(callback);
        return true;
    }

    std::string NormalizeKeyConstantName(const char* name)
    {
        std::string result;
        bool lastUnderscore = false;
        for (const unsigned char c : std::string(name))
        {
            if (std::isalnum(c))
            {
                result.push_back(static_cast<char>(std::toupper(c)));
                lastUnderscore = false;
            }
            else if (!lastUnderscore)
            {
                result.push_back('_');
                lastUnderscore = true;
            }
        }
        while (!result.empty() && result.back() == '_')
        {
            result.pop_back();
        }
        if (result.empty())
        {
            return {};
        }
        return "Key_" + result;
    }

    void PopulateKeyConstants(PyObject* module)
    {
        PyObject* keyDict = PyDict_New();
        std::unordered_set<std::string> addedKeys;
        for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode)
        {
            const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode));
            if (!name || !*name)
            {
                continue;
            }
            const SDL_Keycode keycode = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), SDL_KMOD_NONE, false);
            PyObject* keyValue = PyLong_FromLong(static_cast<long>(keycode));
            PyDict_SetItemString(keyDict, name, keyValue);
            Py_DECREF(keyValue);

            const std::string constantName = NormalizeKeyConstantName(name);
            if (!constantName.empty() && addedKeys.insert(constantName).second)
            {
                PyModule_AddIntConstant(module, constantName.c_str(), static_cast<int>(keycode));
            }
        }
        PyModule_AddObject(module, "Keys", keyDict);
    }

    // ── engine.input functions ──────────────────────────────────────────

    PyObject* py_set_on_key_pressed(PyObject*, PyObject* args)
    {
        PyObject* callback = Py_None;
        if (!PyArg_ParseTuple(args, "O", &callback))
        {
            return nullptr;
        }
        if (!StoreKeyCallback(callback, ScriptDetail::s_onKeyPressed))
        {
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_set_on_key_released(PyObject*, PyObject* args)
    {
        PyObject* callback = Py_None;
        if (!PyArg_ParseTuple(args, "O", &callback))
        {
            return nullptr;
        }
        if (!StoreKeyCallback(callback, ScriptDetail::s_onKeyReleased))
        {
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_register_key_pressed(PyObject*, PyObject* args)
    {
        int key = 0;
        PyObject* callback = nullptr;
        if (!PyArg_ParseTuple(args, "iO", &key, &callback))
        {
            return nullptr;
        }
        if (!StoreKeyCallbackForKey(callback, ScriptDetail::s_keyPressedCallbacks, key))
        {
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_register_key_released(PyObject*, PyObject* args)
    {
        int key = 0;
        PyObject* callback = nullptr;
        if (!PyArg_ParseTuple(args, "iO", &key, &callback))
        {
            return nullptr;
        }
        if (!StoreKeyCallbackForKey(callback, ScriptDetail::s_keyReleasedCallbacks, key))
        {
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_is_shift_pressed(PyObject*, PyObject*)
    {
        const SDL_Keymod mods = SDL_GetModState();
        if (mods & SDL_KMOD_SHIFT)
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_is_ctrl_pressed(PyObject*, PyObject*)
    {
        const SDL_Keymod mods = SDL_GetModState();
        if (mods & SDL_KMOD_CTRL)
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_is_alt_pressed(PyObject*, PyObject*)
    {
        const SDL_Keymod mods = SDL_GetModState();
        if (mods & SDL_KMOD_ALT)
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_get_key(PyObject*, PyObject* args)
    {
        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "s", &name))
        {
            return nullptr;
        }
        const SDL_Keycode key = SDL_GetKeyFromName(name);
        return PyLong_FromLong(static_cast<long>(key));
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef InputMethods[] = {
        { "set_on_key_pressed", py_set_on_key_pressed, METH_VARARGS, "Set a global key pressed callback." },
        { "set_on_key_released", py_set_on_key_released, METH_VARARGS, "Set a global key released callback." },
        { "register_key_pressed", py_register_key_pressed, METH_VARARGS, "Register a key pressed callback for a key." },
        { "register_key_released", py_register_key_released, METH_VARARGS, "Register a key released callback for a key." },
        { "is_shift_pressed", py_is_shift_pressed, METH_NOARGS, "Check if shift is pressed." },
        { "is_ctrl_pressed", py_is_ctrl_pressed, METH_NOARGS, "Check if ctrl is pressed." },
        { "is_alt_pressed", py_is_alt_pressed, METH_NOARGS, "Check if alt is pressed." },
        { "get_key", py_get_key, METH_VARARGS, "Resolve a keycode from a key name." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef InputModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.input",
        "Input scripting API",
        -1,
        InputMethods
    };
}

PyObject* CreateInputModule()
{
    PyObject* module = PyModule_Create(&InputModuleDef);
    if (module)
    {
        PopulateKeyConstants(module);
    }
    return module;
}
