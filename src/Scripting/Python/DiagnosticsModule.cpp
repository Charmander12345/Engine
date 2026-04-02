#include "ScriptingInternal.h"

#include "../Diagnostics/DiagnosticsManager.h"
#include <SDL3/SDL.h>

namespace
{
    // ── engine.diagnostics ──────────────────────────────────────────────

    PyObject* py_get_delta_time(PyObject*, PyObject*)
    {
        return PyFloat_FromDouble(static_cast<double>(ScriptDetail::s_lastDeltaSeconds));
    }

    PyObject* py_get_engine_time(PyObject*, PyObject*)
    {
        return PyFloat_FromDouble(static_cast<double>(SDL_GetTicks()) / 1000.0);
    }

    PyObject* py_get_state(PyObject*, PyObject* args)
    {
        const char* key = nullptr;
        if (!PyArg_ParseTuple(args, "s", &key))
        {
            return nullptr;
        }
        auto value = DiagnosticsManager::Instance().getState(key);
        if (!value.has_value())
        {
            Py_RETURN_NONE;
        }
        return PyUnicode_FromString(value->c_str());
    }

    PyObject* py_set_state(PyObject*, PyObject* args)
    {
        const char* key = nullptr;
        const char* value = nullptr;
        if (!PyArg_ParseTuple(args, "ss", &key, &value))
        {
            return nullptr;
        }
        DiagnosticsManager::Instance().setState(key, value);
        Py_RETURN_TRUE;
    }

    // ── Hardware diagnostics ────────────────────────────────────────────

    PyObject* py_get_cpu_info(PyObject*, PyObject*)
    {
        const auto& hw = DiagnosticsManager::Instance().getHardwareInfo();
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "brand",          PyUnicode_FromString(hw.cpu.brand.c_str()));
        PyDict_SetItemString(dict, "physical_cores", PyLong_FromLong(hw.cpu.physicalCores));
        PyDict_SetItemString(dict, "logical_cores",  PyLong_FromLong(hw.cpu.logicalCores));
        return dict;
    }

    PyObject* py_get_gpu_info(PyObject*, PyObject*)
    {
        const auto& hw = DiagnosticsManager::Instance().getHardwareInfo();
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "renderer",       PyUnicode_FromString(hw.gpu.renderer.c_str()));
        PyDict_SetItemString(dict, "vendor",         PyUnicode_FromString(hw.gpu.vendor.c_str()));
        PyDict_SetItemString(dict, "driver_version", PyUnicode_FromString(hw.gpu.driverVersion.c_str()));
        PyDict_SetItemString(dict, "vram_total_mb",  PyLong_FromLongLong(hw.gpu.vramTotalMB));
        PyDict_SetItemString(dict, "vram_free_mb",   PyLong_FromLongLong(hw.gpu.vramFreeMB));
        return dict;
    }

    PyObject* py_get_ram_info(PyObject*, PyObject*)
    {
        const auto& hw = DiagnosticsManager::Instance().getHardwareInfo();
        PyObject* dict = PyDict_New();
        PyDict_SetItemString(dict, "total_mb",     PyLong_FromLongLong(hw.ram.totalMB));
        PyDict_SetItemString(dict, "available_mb", PyLong_FromLongLong(hw.ram.availableMB));
        return dict;
    }

    PyObject* py_get_monitor_info(PyObject*, PyObject*)
    {
        const auto& hw = DiagnosticsManager::Instance().getHardwareInfo();
        PyObject* list = PyList_New(0);
        for (const auto& m : hw.monitors)
        {
            PyObject* d = PyDict_New();
            PyDict_SetItemString(d, "name",         PyUnicode_FromString(m.name.c_str()));
            PyDict_SetItemString(d, "width",        PyLong_FromLong(m.width));
            PyDict_SetItemString(d, "height",       PyLong_FromLong(m.height));
            PyDict_SetItemString(d, "refresh_rate", PyLong_FromLong(m.refreshRate));
            PyDict_SetItemString(d, "dpi_scale",    PyFloat_FromDouble(static_cast<double>(m.dpiScale)));
            PyDict_SetItemString(d, "primary",      m.primary ? Py_True : Py_False);
            Py_INCREF(m.primary ? Py_True : Py_False);
            PyList_Append(list, d);
            Py_DECREF(d);
        }
        return list;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef DiagnosticsMethods[] = {
        { "get_delta_time", py_get_delta_time, METH_NOARGS, "Get last frame delta time." },
        { "get_engine_time", py_get_engine_time, METH_NOARGS, "Get seconds elapsed since engine start." },
        { "get_state", py_get_state, METH_VARARGS, "Get engine state string." },
        { "set_state", py_set_state, METH_VARARGS, "Set engine state string." },
        { "get_cpu_info", py_get_cpu_info, METH_NOARGS, "Get CPU info dict (brand, physical_cores, logical_cores)." },
        { "get_gpu_info", py_get_gpu_info, METH_NOARGS, "Get GPU info dict (renderer, vendor, driver_version, vram_total_mb, vram_free_mb)." },
        { "get_ram_info", py_get_ram_info, METH_NOARGS, "Get RAM info dict (total_mb, available_mb)." },
        { "get_monitor_info", py_get_monitor_info, METH_NOARGS, "Get list of monitor info dicts (name, width, height, refresh_rate, dpi_scale, primary)." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef DiagnosticsModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.diagnostics",
        "Diagnostics scripting API",
        -1,
        DiagnosticsMethods
    };
}

PyObject* CreateDiagnosticsModule()
{
    return PyModule_Create(&DiagnosticsModuleDef);
}
