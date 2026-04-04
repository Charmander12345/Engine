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

#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
class Renderer;

// Shared scripting state accessible from extracted module files.
namespace ScriptDetail
{
    extern Renderer* s_renderer;
    extern PyObject* s_onCollision;
    extern float s_lastDeltaSeconds;

    /// The entity whose script is currently executing (set before onloaded/tick/overlap calls).
    /// Used by register_key_pressed/register_key_released to capture the registering entity.
    extern unsigned long s_currentEntity;

    /// Per-key callback with the entity that registered it.
    struct EntityCallback
    {
        PyObject* callback{ nullptr };
        unsigned long entity{ 0 };
    };

    // Input callback state (shared between InputModule and PythonScripting HandleKey*)
    extern PyObject* s_onKeyPressed;
    extern PyObject* s_onKeyReleased;
    extern std::unordered_map<int, std::vector<EntityCallback>> s_keyPressedCallbacks;
    extern std::unordered_map<int, std::vector<EntityCallback>> s_keyReleasedCallbacks;
    extern std::unordered_map<std::string, std::vector<PyObject*>> s_actionPressedCallbacks;
    extern std::unordered_map<std::string, std::vector<PyObject*>> s_actionReleasedCallbacks;

    // Shared helper: log a Python exception with traceback to Logger + modal notification.
    void LogPythonError(const std::string& context);

    // Input callback helpers (used by HandleKeyDown/HandleKeyUp in PythonScripting.cpp)
    void InvokeKeyCallbacks(PyObject* callback, int key);
    void InvokeKeyCallbacksForKey(const std::unordered_map<int, std::vector<EntityCallback>>& map, int key);
    void ClearKeyCallbacks();
}

// Module creation functions (implemented in separate .cpp files)
PyObject* CreateMathModule();
PyObject* CreatePhysicsModule();
PyObject* CreateEntityModule();
PyObject* CreateAudioModule();
PyObject* CreateInputModule();
PyObject* CreateCameraModule();
PyObject* CreateParticleModule();
PyObject* CreateDiagnosticsModule();
PyObject* CreateLoggingModule();
PyObject* CreateUIModule();
PyObject* CreateGlobalStateModule();
#if ENGINE_EDITOR
PyObject* CreateEditorModule();
#endif
