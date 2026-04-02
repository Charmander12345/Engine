#include "ScriptingInternal.h"

#include "../Scripting/ScriptingGlobalState.h"

namespace
{
    // ── engine.globalstate ──────────────────────────────────────────────

    PyObject* py_set_global(PyObject*, PyObject* args)
    {
        const char* name = nullptr;
        PyObject* value = nullptr;
        if (!PyArg_ParseTuple(args, "sO", &name, &value))
        {
            return nullptr;
        }

        GlobalVariable var{};
        if (PyBool_Check(value))
        {
            var.type = EngineVariableType::Boolean;
            var.boolean = (value == Py_True);
        }
        else if (PyLong_Check(value))
        {
            var.type = EngineVariableType::Number;
            var.number = static_cast<double>(PyLong_AsLongLong(value));
        }
        else if (PyFloat_Check(value))
        {
            var.type = EngineVariableType::Number;
            var.number = PyFloat_AsDouble(value);
        }
        else if (PyUnicode_Check(value))
        {
            var.type = EngineVariableType::String;
            const char* utf8 = PyUnicode_AsUTF8(value);
            if (!utf8)
            {
                return nullptr;
            }
            // Store a copy so the string outlives the Python object.
            size_t len = std::strlen(utf8);
            char* copy = new char[len + 1];
            std::memcpy(copy, utf8, len + 1);
            var.string = copy;
        }
        else if (value == Py_None)
        {
            var.type = EngineVariableType::None;
        }
        else
        {
            PyErr_SetString(PyExc_TypeError, "globalstate.set_global: unsupported value type (use number, string, bool, or None)");
            return nullptr;
        }

        // Free previous string allocation if overwriting a string variable.
        GlobalVariable* prev = ScriptingGlobalState::Instance().getVariable(name);
        if (prev && prev->type == EngineVariableType::String && prev->string)
        {
            delete[] prev->string;
        }

        ScriptingGlobalState::Instance().setVariable(name, var);
        Py_RETURN_TRUE;
    }

    PyObject* py_get_global(PyObject*, PyObject* args)
    {
        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "s", &name))
        {
            return nullptr;
        }

        GlobalVariable* var = ScriptingGlobalState::Instance().getVariable(name);
        if (!var)
        {
            Py_RETURN_NONE;
        }

        switch (var->type)
        {
        case EngineVariableType::Number:
            return PyFloat_FromDouble(var->number);
        case EngineVariableType::String:
            if (var->string)
            {
                return PyUnicode_FromString(var->string);
            }
            Py_RETURN_NONE;
        case EngineVariableType::Boolean:
            if (var->boolean)
            {
                Py_RETURN_TRUE;
            }
            Py_RETURN_FALSE;
        case EngineVariableType::None:
        default:
            Py_RETURN_NONE;
        }
    }

    PyObject* py_remove_global(PyObject*, PyObject* args)
    {
        const char* name = nullptr;
        if (!PyArg_ParseTuple(args, "s", &name))
        {
            return nullptr;
        }

        // Free string allocation before removing.
        GlobalVariable* var = ScriptingGlobalState::Instance().getVariable(name);
        if (var && var->type == EngineVariableType::String && var->string)
        {
            delete[] var->string;
        }

        if (ScriptingGlobalState::Instance().removeVariable(name))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

    PyObject* py_get_all_globals(PyObject*, PyObject*)
    {
        const auto& variables = ScriptingGlobalState::Instance().getAllVariables();
        PyObject* dict = PyDict_New();
        if (!dict)
        {
            return nullptr;
        }

        for (const auto& [key, var] : variables)
        {
            PyObject* pyValue = nullptr;
            switch (var.type)
            {
            case EngineVariableType::Number:
                pyValue = PyFloat_FromDouble(var.number);
                break;
            case EngineVariableType::String:
                pyValue = var.string ? PyUnicode_FromString(var.string) : Py_NewRef(Py_None);
                break;
            case EngineVariableType::Boolean:
                pyValue = var.boolean ? Py_NewRef(Py_True) : Py_NewRef(Py_False);
                break;
            case EngineVariableType::None:
            default:
                pyValue = Py_NewRef(Py_None);
                break;
            }
            if (pyValue)
            {
                PyDict_SetItemString(dict, key.c_str(), pyValue);
                Py_DECREF(pyValue);
            }
        }
        return dict;
    }

    PyObject* py_clear_globals(PyObject*, PyObject*)
    {
        // Free all string allocations before clearing.
        for (const auto& [key, var] : ScriptingGlobalState::Instance().getAllVariables())
        {
            if (var.type == EngineVariableType::String && var.string)
            {
                delete[] var.string;
            }
        }
        ScriptingGlobalState::Instance().clear();
        Py_RETURN_TRUE;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef GlobalStateMethods[] = {
        { "set_global",    py_set_global,    METH_VARARGS, "Set a global variable (number, string, bool, or None)." },
        { "get_global",    py_get_global,    METH_VARARGS, "Get a global variable by name. Returns None if not found." },
        { "remove_global", py_remove_global, METH_VARARGS, "Remove a global variable by name." },
        { "get_all",       py_get_all_globals, METH_NOARGS, "Get all global variables as a dict." },
        { "clear",         py_clear_globals,   METH_NOARGS, "Clear all global variables." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef GlobalStateModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.globalstate",
        "Global state for sharing data between scripts and entities",
        -1,
        GlobalStateMethods
    };
}

PyObject* CreateGlobalStateModule()
{
    return PyModule_Create(&GlobalStateModuleDef);
}
