#pragma once

// Centralised Python.h include with MSVC debug workaround.
// Every scripting TU should include this header instead of <Python.h> directly.
#ifdef _DEBUG
#define PYTHONSCRIPTING_DEBUG_RESTORE
#undef _DEBUG
#endif
#include <Python.h>
#ifdef PYTHONSCRIPTING_DEBUG_RESTORE
#define _DEBUG
#undef PYTHONSCRIPTING_DEBUG_RESTORE
#endif

// Forward declarations
class Renderer;

// Shared scripting state accessible from extracted module files.
namespace ScriptDetail
{
    extern Renderer* s_renderer;
    extern PyObject* s_onCollision;
}

// Module creation functions (implemented in separate .cpp files)
PyObject* CreateMathModule();
PyObject* CreatePhysicsModule();
