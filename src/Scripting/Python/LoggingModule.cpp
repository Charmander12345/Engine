#include "ScriptingInternal.h"

#include "../Logger/Logger.h"

namespace
{
    // ── engine.logging ──────────────────────────────────────────────────

    Logger::LogLevel ResolveLogLevel(int level)
    {
        switch (level)
        {
        case 1:
            return Logger::LogLevel::WARNING;
        case 2:
            return Logger::LogLevel::ERROR;
        default:
            return Logger::LogLevel::INFO;
        }
    }

    PyObject* py_log(PyObject*, PyObject* args)
    {
        const char* message = nullptr;
        int level = 0;
        if (!PyArg_ParseTuple(args, "s|i", &message, &level))
        {
            return nullptr;
        }
        Logger::Instance().log(Logger::Category::Scripting, message, ResolveLogLevel(level));
        Py_RETURN_TRUE;
    }

    // ── Method table & module definition ────────────────────────────────

    PyMethodDef LoggingMethods[] = {
        { "log", py_log, METH_VARARGS, "Log a message (level: 0=info,1=warn,2=error)." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef LoggingModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.logging",
        "Logging scripting API",
        -1,
        LoggingMethods
    };
}

PyObject* CreateLoggingModule()
{
    return PyModule_Create(&LoggingModuleDef);
}
