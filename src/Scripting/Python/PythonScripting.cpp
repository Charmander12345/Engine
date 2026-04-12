#include "PythonScripting.h"
#include "ScriptingInternal.h"
#include "ScriptHotReload.h"

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Core/EngineLevel.h"
#include "../Logger/Logger.h"
#include <cctype>
#include "../Renderer/Renderer.h"
#include "../Physics/PhysicsWorld.h"
#include "../Core/InputActionManager.h"
#include "../NativeScripting/NativeScriptManager.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>

// Definitions for shared scripting state declared in ScriptingInternal.h.
// Extracted module files (MathModule.cpp, PhysicsModule.cpp) access these via the ScriptDetail namespace.
namespace ScriptDetail
{
    Renderer* s_renderer = nullptr;
    PyObject* s_onCollision = nullptr;
    float s_lastDeltaSeconds = 0.0f;

    void LogPythonError(const std::string& context)
    {
        if (!PyErr_Occurred())
        {
            return;
        }
        PyObject* excType = nullptr;
        PyObject* excValue = nullptr;
        PyObject* excTrace = nullptr;
        PyErr_Fetch(&excType, &excValue, &excTrace);
        PyErr_NormalizeException(&excType, &excValue, &excTrace);

        std::string message = context;
        if (excValue)
        {
            PyObject* text = PyObject_Str(excValue);
            if (text)
            {
                const char* utf8 = PyUnicode_AsUTF8(text);
                if (utf8)
                {
                    message += ": ";
                    message += utf8;
                }
                Py_DECREF(text);
            }
        }

        if (excType || excValue || excTrace)
        {
            PyObject* tracebackModule = PyImport_ImportModule("traceback");
            if (tracebackModule)
            {
                PyObject* formatFunc = PyObject_GetAttrString(tracebackModule, "format_exception");
                if (formatFunc && PyCallable_Check(formatFunc))
                {
                    PyObject* formatted = PyObject_CallFunctionObjArgs(formatFunc,
                        excType ? excType : Py_None,
                        excValue ? excValue : Py_None,
                        excTrace ? excTrace : Py_None,
                        nullptr);
                    if (formatted)
                    {
                        PyObject* separator = PyUnicode_FromString("");
                        PyObject* joined = separator ? PyUnicode_Join(separator, formatted) : nullptr;
                        Py_XDECREF(separator);
                        if (joined)
                        {
                            const char* utf8 = PyUnicode_AsUTF8(joined);
                            if (utf8 && *utf8)
                            {
                                message += "\n";
                                message += utf8;
                            }
                            Py_DECREF(joined);
                        }
                        Py_DECREF(formatted);
                    }
                }
                Py_XDECREF(formatFunc);
                Py_DECREF(tracebackModule);
            }
        }

        Logger::Instance().log(Logger::Category::Engine, message, Logger::LogLevel::ERROR);
        DiagnosticsManager::Instance().enqueueModalNotification(message, true);

        Py_XDECREF(excType);
        Py_XDECREF(excValue);
        Py_XDECREF(excTrace);
    }
}

namespace
{
    // Alias shared state into the anonymous namespace so existing code stays unchanged.
    auto*& s_renderer    = ScriptDetail::s_renderer;
    auto*& s_onCollision = ScriptDetail::s_onCollision;
    auto& s_lastDeltaSeconds = ScriptDetail::s_lastDeltaSeconds;
    auto& LogPythonError = ScriptDetail::LogPythonError;
    std::string DescribeEntity(ECS::Entity entity)
    {
        std::string label = "entity " + std::to_string(entity);
        if (const auto* nameComponent = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(entity))
        {
            if (!nameComponent->displayName.empty())
            {
                label += " (" + nameComponent->displayName + ")";
            }
        }
        return label;
    }

    struct ScriptState
    {
        PyObject* module{ nullptr };
        PyObject* onLoadedFunc{ nullptr };
        PyObject* tickFunc{ nullptr };
        PyObject* onBeginOverlapFunc{ nullptr };
        PyObject* onEndOverlapFunc{ nullptr };
        PyObject* scriptClass{ nullptr }; // Script subclass (class-based model)
        std::unordered_set<unsigned long> startedEntities;
        bool loadFailed{ false };
    };

    struct LevelScriptState
    {
        PyObject* module{ nullptr };
        PyObject* onLoadedFunc{ nullptr };
        PyObject* onUnloadedFunc{ nullptr };
        bool loadedCalled{ false };
    };

    std::unordered_map<std::string, ScriptState> s_scripts;
    // Per-entity Script class instances: key = (scriptPath, entity)
    std::unordered_map<unsigned long, PyObject*> s_entityInstances;
    EngineLevel* s_lastLevel{ nullptr };
    std::string s_lastLevelScriptPath;
    LevelScriptState s_levelScript;
    std::unordered_map<int, std::shared_ptr<PyObject>> s_assetLoadCallbacks;
    std::mutex s_assetLoadCallbacksMutex;

    // Script Hot-Reload state
    ScriptHotReload s_scriptHotReload;
    bool s_scriptHotReloadEnabled{ true };

    void ReleaseScriptState(ScriptState& state)
    {
        if (state.onLoadedFunc)
        {
            Py_DECREF(state.onLoadedFunc);
            state.onLoadedFunc = nullptr;
        }
        if (state.tickFunc)
        {
            Py_DECREF(state.tickFunc);
            state.tickFunc = nullptr;
        }
        if (state.onBeginOverlapFunc)
        {
            Py_DECREF(state.onBeginOverlapFunc);
            state.onBeginOverlapFunc = nullptr;
        }
        if (state.onEndOverlapFunc)
        {
            Py_DECREF(state.onEndOverlapFunc);
            state.onEndOverlapFunc = nullptr;
        }
        if (state.scriptClass)
        {
            Py_DECREF(state.scriptClass);
            state.scriptClass = nullptr;
        }
        if (state.module)
        {
            Py_DECREF(state.module);
            state.module = nullptr;
        }
        state.startedEntities.clear();
    }

    void ReleaseLevelScriptState()
    {
        if (s_levelScript.onLoadedFunc)
        {
            Py_DECREF(s_levelScript.onLoadedFunc);
            s_levelScript.onLoadedFunc = nullptr;
        }
        if (s_levelScript.onUnloadedFunc)
        {
            Py_DECREF(s_levelScript.onUnloadedFunc);
            s_levelScript.onUnloadedFunc = nullptr;
        }
        if (s_levelScript.module)
        {
            Py_DECREF(s_levelScript.module);
            s_levelScript.module = nullptr;
        }
        s_levelScript.loadedCalled = false;
    }

    void ClearAssetLoadCallbacks()
    {
        std::lock_guard<std::mutex> lock(s_assetLoadCallbacksMutex);
        s_assetLoadCallbacks.clear();
    }

    void ProcessAssetLoadCallbacks()
    {
        std::vector<std::pair<std::shared_ptr<PyObject>, int>> ready;
        {
            std::lock_guard<std::mutex> lock(s_assetLoadCallbacksMutex);
            for (auto it = s_assetLoadCallbacks.begin(); it != s_assetLoadCallbacks.end();)
            {
                int assetId = 0;
                if (AssetManager::Instance().tryConsumeAssetLoadResult(it->first, assetId))
                {
                    ready.emplace_back(it->second, assetId);
                    it = s_assetLoadCallbacks.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (auto& [callback, assetId] : ready)
        {
            if (!callback)
            {
                continue;
            }
            PyObject* result = PyObject_CallFunction(callback.get(), "i", assetId);
            if (!result)
            {
                LogPythonError("Python: load_asset_async callback failed");
            }
            else
            {
                Py_DECREF(result);
            }
        }
    }

    // ── Cross-language: C++ -> Python call handler ──────────────────────

    PyObject* ScriptValueToPyObject(const ScriptValue& val)
    {
        switch (val.type)
        {
        case ScriptValue::Float:  return PyFloat_FromDouble(val.floatVal);
        case ScriptValue::Int:    return PyLong_FromLong(val.intVal);
        case ScriptValue::Bool:   if (val.boolVal) { Py_RETURN_TRUE; } else { Py_RETURN_FALSE; }
        case ScriptValue::String: return PyUnicode_FromString(val.stringVal.c_str());
        case ScriptValue::Vec3:   return Py_BuildValue("(fff)", val.vec3Val[0], val.vec3Val[1], val.vec3Val[2]);
        case ScriptValue::None:
        default:                  Py_RETURN_NONE;
        }
    }

    ScriptValue PyObjectToScriptValue(PyObject* obj)
    {
        if (!obj || obj == Py_None)
        {
            return ScriptValue{};
        }
        if (PyBool_Check(obj))
        {
            return ScriptValue::makeBool(obj == Py_True);
        }
        if (PyLong_Check(obj))
        {
            return ScriptValue::makeInt(static_cast<int>(PyLong_AsLong(obj)));
        }
        if (PyFloat_Check(obj))
        {
            return ScriptValue::makeFloat(static_cast<float>(PyFloat_AsDouble(obj)));
        }
        if (PyUnicode_Check(obj))
        {
            const char* str = PyUnicode_AsUTF8(obj);
            return str ? ScriptValue::makeString(str) : ScriptValue{};
        }
        if (PyTuple_Check(obj) && PyTuple_Size(obj) == 3)
        {
            float x = 0, y = 0, z = 0;
            PyObject* px = PyTuple_GetItem(obj, 0);
            PyObject* py = PyTuple_GetItem(obj, 1);
            PyObject* pz = PyTuple_GetItem(obj, 2);
            if ((PyFloat_Check(px) || PyLong_Check(px)) &&
                (PyFloat_Check(py) || PyLong_Check(py)) &&
                (PyFloat_Check(pz) || PyLong_Check(pz)))
            {
                x = static_cast<float>(PyFloat_AsDouble(px));
                y = static_cast<float>(PyFloat_AsDouble(py));
                z = static_cast<float>(PyFloat_AsDouble(pz));
                return ScriptValue::makeVec3(x, y, z);
            }
        }
        return ScriptValue{};
    }

    ScriptValue HandleCppCallsPython(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args)
    {
        if (!Py_IsInitialized())
        {
            Logger::Instance().log(Logger::Category::Engine,
                "Python: callFunction failed – interpreter not initialized",
                Logger::LogLevel::WARNING);
            return ScriptValue{};
        }

        // Find the entity's Python script
        const auto* comp = ECS::ECSManager::Instance().getComponent<ECS::LogicComponent>(entity);
        if (!comp || comp->scriptPath.empty())
        {
            Logger::Instance().log(Logger::Category::Engine,
                "Python: callFunction(\"" + std::string(funcName ? funcName : "?") +
                "\") failed – " + DescribeEntity(entity) + " has no LogicComponent / script path",
                Logger::LogLevel::WARNING);
            return ScriptValue{};
        }

        auto it = s_scripts.find(comp->scriptPath);
        if (it == s_scripts.end() || !it->second.module)
        {
            Logger::Instance().log(Logger::Category::Engine,
                "Python: callFunction(\"" + std::string(funcName ? funcName : "?") +
                "\") failed – script module not loaded: " + comp->scriptPath,
                Logger::LogLevel::WARNING);
            return ScriptValue{};
        }

        PyGILState_STATE gilState = PyGILState_Ensure();

        PyObject* moduleDict = PyModule_GetDict(it->second.module);
        PyObject* func = moduleDict ? PyDict_GetItemString(moduleDict, funcName) : nullptr;
        if (!func || !PyCallable_Check(func))
        {
            Logger::Instance().log(Logger::Category::Engine,
                "Python: callFunction(\"" + std::string(funcName ? funcName : "?") +
                "\") failed – function not found in " + comp->scriptPath,
                Logger::LogLevel::WARNING);
            PyGILState_Release(gilState);
            return ScriptValue{};
        }

        // Build args tuple: (entity, arg1, arg2, ...)
        ScriptDetail::s_currentEntity = static_cast<unsigned long>(entity);
        PyObject* pyArgs = PyTuple_New(1 + static_cast<Py_ssize_t>(args.size()));
        PyTuple_SetItem(pyArgs, 0, PyLong_FromUnsignedLong(entity));
        for (size_t i = 0; i < args.size(); ++i)
        {
            PyTuple_SetItem(pyArgs, 1 + static_cast<Py_ssize_t>(i), ScriptValueToPyObject(args[i]));
        }

        PyObject* result = PyObject_CallObject(func, pyArgs);
        Py_DECREF(pyArgs);

        ScriptValue retVal;
        if (!result)
        {
            LogPythonError("Python: cross-call to " + std::string(funcName) + " failed for " + DescribeEntity(entity));
        }
        else
        {
            retVal = PyObjectToScriptValue(result);
            Py_DECREF(result);
        }

        PyGILState_Release(gilState);
        return retVal;
    }

    std::filesystem::path ResolveScriptPath(const std::string& scriptPath)
    {
        std::filesystem::path resolved(scriptPath);
        if (resolved.is_relative())
        {
            const std::string abs = AssetManager::Instance().getAbsoluteContentPath(scriptPath);
            if (!abs.empty())
            {
                resolved = abs;
            }
        }
        return resolved;
    }

    bool LoadScriptModule(const std::string& scriptPath, ScriptState& state)
    {
        if (state.module)
        {
            return true;
        }

        std::filesystem::path resolvedPath = ResolveScriptPath(scriptPath);
        Logger::Instance().log(Logger::Category::Engine,
            "Python: resolved script path: " + resolvedPath.string(),
            Logger::LogLevel::INFO);

        std::string code;
        {
            std::ifstream file(resolvedPath, std::ios::in | std::ios::binary);
            if (file.is_open())
            {
                code.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            else
            {
                // HPK fallback: read script from the mounted archive
                auto* hpk = HPKReader::GetMounted();
                if (hpk)
                {
                    std::string vpath = hpk->makeVirtualPath(resolvedPath.string());
                    if (!vpath.empty())
                    {
                        auto buf = hpk->readFile(vpath);
                        if (buf && !buf->empty())
                        {
                            code.assign(buf->data(), buf->size());
                            Logger::Instance().log(Logger::Category::Engine,
                                "Python: loaded script from HPK: " + vpath,
                                Logger::LogLevel::INFO);
                        }
                    }
                }
                if (code.empty())
                {
                    Logger::Instance().log(Logger::Category::Engine, "Python script not found: " + resolvedPath.string(), Logger::LogLevel::ERROR);
                    return false;
                }
            }
        }
        if (code.empty())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python script empty: " + resolvedPath.string(), Logger::LogLevel::ERROR);
            return false;
        }

        const std::string moduleName = "script_" + std::to_string(std::hash<std::string>{}(scriptPath));
        PyObject* module = PyModule_New(moduleName.c_str());
        if (!module)
        {
            LogPythonError("Python: failed to create module for " + resolvedPath.string());
            return false;
        }

        PyObject* moduleDict = PyModule_GetDict(module);
        PyObject* builtins = PyEval_GetBuiltins();
        if (moduleDict && builtins)
        {
            PyDict_SetItemString(moduleDict, "__builtins__", builtins);
        }
        PyDict_SetItemString(moduleDict, "__file__", PyUnicode_FromString(resolvedPath.string().c_str()));

        PyObject* engineModule = PyImport_ImportModule("engine");
        if (!engineModule)
        {
            LogPythonError("Python: failed to import engine module for " + resolvedPath.string());
            Py_DECREF(module);
            return false;
        }
        PyDict_SetItemString(moduleDict, "engine", engineModule);
        Py_DECREF(engineModule);

        PyObject* result = PyRun_StringFlags(code.c_str(), Py_file_input, moduleDict, moduleDict, nullptr);
        if (!result)
        {
            LogPythonError("Python: failed to execute script " + resolvedPath.string());
            Py_DECREF(module);
            return false;
        }
        Py_DECREF(result);

        state.module = module;

        // ── Detect class-based Script subclass ──────────────────────────
        PyObject* engineMod = PyImport_ImportModule("engine");
        PyObject* scriptBaseClass = engineMod ? PyObject_GetAttrString(engineMod, "Script") : nullptr;
        Py_XDECREF(engineMod);

        if (scriptBaseClass)
        {
            // Walk module dict to find a class that is a subclass of engine.Script
            PyObject* key = nullptr;
            PyObject* value = nullptr;
            Py_ssize_t pos = 0;
            while (PyDict_Next(moduleDict, &pos, &key, &value))
            {
                if (!PyType_Check(value)) continue;
                if (value == scriptBaseClass) continue; // skip the base class itself
                if (PyObject_IsSubclass(value, scriptBaseClass))
                {
                    Py_INCREF(value);
                    state.scriptClass = value;
                    Logger::Instance().log(Logger::Category::Engine,
                        "Python: detected Script subclass in " + resolvedPath.string(),
                        Logger::LogLevel::INFO);
                    break;
                }
            }
            Py_DECREF(scriptBaseClass);
        }

        if (!state.scriptClass)
        {
            // ── Function-based model (legacy + new naming) ──────────────
            // Primary: on_loaded, fallback: onloaded
            PyObject* onLoadedFunc = PyDict_GetItemString(moduleDict, "on_loaded");
            if (!onLoadedFunc || !PyCallable_Check(onLoadedFunc))
                onLoadedFunc = PyDict_GetItemString(moduleDict, "onloaded");
            PyObject* tickFunc = PyDict_GetItemString(moduleDict, "tick");
            if (onLoadedFunc && PyCallable_Check(onLoadedFunc))
            {
                Py_INCREF(onLoadedFunc);
                state.onLoadedFunc = onLoadedFunc;
            }
            if (tickFunc && PyCallable_Check(tickFunc))
            {
                Py_INCREF(tickFunc);
                state.tickFunc = tickFunc;
            }

            // Primary: on_begin_overlap, fallback: on_entity_begin_overlap
            PyObject* beginOverlapFunc = PyDict_GetItemString(moduleDict, "on_begin_overlap");
            if (!beginOverlapFunc || !PyCallable_Check(beginOverlapFunc))
                beginOverlapFunc = PyDict_GetItemString(moduleDict, "on_entity_begin_overlap");
            if (beginOverlapFunc && PyCallable_Check(beginOverlapFunc))
            {
                Py_INCREF(beginOverlapFunc);
                state.onBeginOverlapFunc = beginOverlapFunc;
            }

            // Primary: on_end_overlap, fallback: on_entity_end_overlap
            PyObject* endOverlapFunc = PyDict_GetItemString(moduleDict, "on_end_overlap");
            if (!endOverlapFunc || !PyCallable_Check(endOverlapFunc))
                endOverlapFunc = PyDict_GetItemString(moduleDict, "on_entity_end_overlap");
            if (endOverlapFunc && PyCallable_Check(endOverlapFunc))
            {
                Py_INCREF(endOverlapFunc);
                state.onEndOverlapFunc = endOverlapFunc;
            }
        }

        return true;
    }

    bool LoadLevelScriptModule(const std::string& scriptPath)
    {
        if (s_levelScript.module)
        {
            return true;
        }

        std::filesystem::path resolvedPath = ResolveScriptPath(scriptPath);
        std::string code;
        {
            std::ifstream file(resolvedPath, std::ios::in | std::ios::binary);
            if (file.is_open())
            {
                code.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            }
            else
            {
                // HPK fallback: read level script from the mounted archive
                auto* hpk = HPKReader::GetMounted();
                if (hpk)
                {
                    std::string vpath = hpk->makeVirtualPath(resolvedPath.string());
                    if (!vpath.empty())
                    {
                        auto buf = hpk->readFile(vpath);
                        if (buf && !buf->empty())
                        {
                            code.assign(buf->data(), buf->size());
                            Logger::Instance().log(Logger::Category::Engine,
                                "Python: loaded level script from HPK: " + vpath,
                                Logger::LogLevel::INFO);
                        }
                    }
                }
                if (code.empty())
                {
                    Logger::Instance().log(Logger::Category::Engine, "Python level script not found: " + resolvedPath.string(), Logger::LogLevel::ERROR);
                    return false;
                }
            }
        }
        if (code.empty())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python level script empty: " + resolvedPath.string(), Logger::LogLevel::ERROR);
            return false;
        }

        const std::string moduleName = "level_script_" + std::to_string(std::hash<std::string>{}(scriptPath));
        PyObject* module = PyModule_New(moduleName.c_str());
        if (!module)
        {
            LogPythonError("Python: failed to create level module for " + resolvedPath.string());
            return false;
        }

        PyObject* moduleDict = PyModule_GetDict(module);
        PyObject* builtins = PyEval_GetBuiltins();
        if (moduleDict && builtins)
        {
            PyDict_SetItemString(moduleDict, "__builtins__", builtins);
        }
        PyDict_SetItemString(moduleDict, "__file__", PyUnicode_FromString(resolvedPath.string().c_str()));

        PyObject* engineModule = PyImport_ImportModule("engine");
        if (!engineModule)
        {
            LogPythonError("Python: failed to import engine module for " + resolvedPath.string());
            Py_DECREF(module);
            return false;
        }
        PyDict_SetItemString(moduleDict, "engine", engineModule);
        Py_DECREF(engineModule);

        PyObject* result = PyRun_StringFlags(code.c_str(), Py_file_input, moduleDict, moduleDict, nullptr);
        if (!result)
        {
            LogPythonError("Python: failed to execute level script " + resolvedPath.string());
            Py_DECREF(module);
            return false;
        }
        Py_DECREF(result);

        s_levelScript.module = module;
        PyObject* onLoadedFunc = PyDict_GetItemString(moduleDict, "on_level_loaded");
        PyObject* onUnloadedFunc = PyDict_GetItemString(moduleDict, "on_level_unloaded");
        if (onLoadedFunc && PyCallable_Check(onLoadedFunc))
        {
            Py_INCREF(onLoadedFunc);
            s_levelScript.onLoadedFunc = onLoadedFunc;
        }
        if (onUnloadedFunc && PyCallable_Check(onUnloadedFunc))
        {
            Py_INCREF(onUnloadedFunc);
            s_levelScript.onUnloadedFunc = onUnloadedFunc;
        }
        return true;
    }

    PyObject* py_is_asset_loaded(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        if (!PyArg_ParseTuple(args, "s", &path))
        {
            return nullptr;
        }

        if (AssetManager::Instance().isAssetLoaded(path))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }


    PyObject* py_load_asset(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        int type = 0;
        int allowGc = 0;
        if (!PyArg_ParseTuple(args, "si|p", &path, &type, &allowGc))
        {
            return nullptr;
        }

        std::string resolvedPath = path;
        const std::filesystem::path pathValue(resolvedPath);
        std::string extension = pathValue.extension().string();
        for (auto& c : extension)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const bool isAudioWav = static_cast<AssetType>(type) == AssetType::Audio && extension == ".wav";
        if (pathValue.is_relative())
        {
            const auto absPath = AssetManager::Instance().getAbsoluteContentPath(resolvedPath);
            if (!absPath.empty())
            {
                resolvedPath = absPath;
            }
        }

        const int id = isAudioWav
            ? AssetManager::Instance().loadAudioFromContentPath(resolvedPath, false)
            : AssetManager::Instance().loadAsset(resolvedPath, static_cast<AssetType>(type), AssetManager::Sync);
        if (allowGc != 0 && id != 0)
        {
            AssetManager::Instance().markAssetGcEligible(static_cast<unsigned int>(id), true);
        }
        return PyLong_FromLong(id);
    }

    PyObject* py_load_asset_async(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        int type = 0;
        PyObject* callback = Py_None;
        int allowGc = 0;
        if (!PyArg_ParseTuple(args, "si|Op", &path, &type, &callback, &allowGc))
        {
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

        std::string resolvedPath = path;
        const std::filesystem::path pathValue(resolvedPath);
        if (pathValue.is_relative())
        {
            const auto absPath = AssetManager::Instance().getAbsoluteContentPath(resolvedPath);
            if (!absPath.empty())
            {
                resolvedPath = absPath;
            }
        }

        const int jobId = AssetManager::Instance().loadAssetAsync(resolvedPath, static_cast<AssetType>(type), allowGc != 0);
        if (jobId == 0)
        {
            if (callbackRef)
            {
                PyObject* result = PyObject_CallFunction(callbackRef.get(), "i", 0);
                if (!result)
                {
                    LogPythonError("Python: load_asset_async callback failed");
                }
                else
                {
                    Py_DECREF(result);
                }
            }
            return PyLong_FromLong(0);
        }

        if (callbackRef)
        {
            std::lock_guard<std::mutex> lock(s_assetLoadCallbacksMutex);
            s_assetLoadCallbacks[jobId] = callbackRef;
        }

        return PyLong_FromLong(jobId);
    }

#if ENGINE_EDITOR
    PyObject* py_save_asset(PyObject*, PyObject* args)
    {
        unsigned long id = 0;
        int type = 0;
        int sync = 1;
        if (!PyArg_ParseTuple(args, "ki|p", &id, &type, &sync))
        {
            return nullptr;
        }
        Asset asset;
        asset.ID = static_cast<unsigned int>(id);
        asset.type = static_cast<AssetType>(type);
        const auto syncState = sync ? AssetManager::Sync : AssetManager::Async;
        const bool result = AssetManager::Instance().saveAsset(asset, syncState);
        if (!result)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to save asset.");
            return nullptr;
        }
        Py_RETURN_TRUE;
    }
#endif

    PyObject* py_unload_asset(PyObject*, PyObject* args)
    {
        unsigned long id = 0;
        if (!PyArg_ParseTuple(args, "k", &id))
        {
            return nullptr;
        }
        const bool result = AssetManager::Instance().unloadAsset(static_cast<unsigned int>(id));
        if (!result)
        {
            PyErr_SetString(PyExc_ValueError, "Asset not found.");
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyMethodDef AssetMethods[] = {
        { "is_asset_loaded", py_is_asset_loaded, METH_VARARGS, "Check if an asset is loaded by path." },
        { "load_asset", py_load_asset, METH_VARARGS, "Load an asset by path and type (sync)." },
        { "load_asset_async", py_load_asset_async, METH_VARARGS, "Load an asset by path and type (async)." },
#if ENGINE_EDITOR
        { "save_asset", py_save_asset, METH_VARARGS, "Save an asset by id and type." },
#endif
        { "unload_asset", py_unload_asset, METH_VARARGS, "Unload an asset by id." },
        { nullptr, nullptr, 0, nullptr }
    };


    PyMethodDef EngineMethods[] = {
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef EngineModule = {
        PyModuleDef_HEAD_INIT,
        "engine",
        "Engine scripting API",
        -1,
        EngineMethods
    };


    PyModuleDef AssetModule = {
        PyModuleDef_HEAD_INIT,
        "engine.assetmanagement",
        "Asset management scripting API",
        -1,
        AssetMethods
    };


    bool AddSubmodule(PyObject* parent, PyObject* submodule, const char* name)
    {
        if (!parent || !submodule || !name)
        {
            return false;
        }

        if (PyModule_AddObject(parent, name, submodule) < 0)
        {
            Py_DECREF(submodule);
            return false;
        }

        std::string fullName = std::string("engine.") + name;
        if (PyDict_SetItemString(PyImport_GetModuleDict(), fullName.c_str(), submodule) < 0)
        {
            return false;
        }

        return true;
    }


    PyObject* CreateEngineModule()
    {
        PyObject* module = PyModule_Create(&EngineModule);
        if (!module)
        {
            return nullptr;
        }

        PyModule_AddIntConstant(module, "Component_Transform", static_cast<int>(ECS::ComponentKind::Transform));
        PyModule_AddIntConstant(module, "Component_Mesh", static_cast<int>(ECS::ComponentKind::Mesh));
        PyModule_AddIntConstant(module, "Component_Material", static_cast<int>(ECS::ComponentKind::Material));
        PyModule_AddIntConstant(module, "Component_Light", static_cast<int>(ECS::ComponentKind::Light));
        PyModule_AddIntConstant(module, "Component_Camera", static_cast<int>(ECS::ComponentKind::Camera));
        PyModule_AddIntConstant(module, "Component_Physics", static_cast<int>(ECS::ComponentKind::Physics));
        PyModule_AddIntConstant(module, "Component_Constraint", static_cast<int>(ECS::ComponentKind::Constraint));
        PyModule_AddIntConstant(module, "Component_Logic", static_cast<int>(ECS::ComponentKind::Logic));
        PyModule_AddIntConstant(module, "Component_Name", static_cast<int>(ECS::ComponentKind::Name));
        PyModule_AddIntConstant(module, "Component_Collision", static_cast<int>(ECS::ComponentKind::Collision));
        PyModule_AddIntConstant(module, "Component_Animation", static_cast<int>(ECS::ComponentKind::Animation));
        PyModule_AddIntConstant(module, "Component_ParticleEmitter", static_cast<int>(ECS::ComponentKind::ParticleEmitter));

        PyModule_AddIntConstant(module, "Asset_Texture", static_cast<int>(AssetType::Texture));
        PyModule_AddIntConstant(module, "Asset_Material", static_cast<int>(AssetType::Material));
        PyModule_AddIntConstant(module, "Asset_Model2D", static_cast<int>(AssetType::Model2D));
        PyModule_AddIntConstant(module, "Asset_Model3D", static_cast<int>(AssetType::Model3D));
        PyModule_AddIntConstant(module, "Asset_PointLight", static_cast<int>(AssetType::PointLight));
        PyModule_AddIntConstant(module, "Asset_Audio", static_cast<int>(AssetType::Audio));
        PyModule_AddIntConstant(module, "Asset_Script", static_cast<int>(AssetType::Script));
        PyModule_AddIntConstant(module, "Asset_Shader", static_cast<int>(AssetType::Shader));
        PyModule_AddIntConstant(module, "Asset_Level", static_cast<int>(AssetType::Level));
        PyModule_AddIntConstant(module, "Asset_Widget", static_cast<int>(AssetType::Widget));
        PyModule_AddIntConstant(module, "Asset_NativeScript", static_cast<int>(AssetType::NativeScript));

        PyModule_AddIntConstant(module, "Log_Info", 0);
        PyModule_AddIntConstant(module, "Log_Warning", 1);
        PyModule_AddIntConstant(module, "Log_Error", 2);

        PyObject* entityModule = CreateEntityModule();
        PyObject* assetModule = PyModule_Create(&AssetModule);
        PyObject* audioModule = CreateAudioModule();
        PyObject* inputModule = CreateInputModule();
        PyObject* uiModule = CreateUIModule();
        PyObject* cameraModule = CreateCameraModule();
        PyObject* diagnosticsModule = CreateDiagnosticsModule();
        PyObject* loggingModule = CreateLoggingModule();
        PyObject* physicsModule = CreatePhysicsModule();
        PyObject* mathModule = CreateMathModule();
        PyObject* particleModule = CreateParticleModule();
        PyObject* globalStateModule = CreateGlobalStateModule();
        PyObject* timerModule = CreateTimerModule();
        PyObject* animationModule = CreateAnimationModule();
#if ENGINE_EDITOR
        PyObject* editorModule = CreateEditorModule();
#endif

        if (!entityModule || !assetModule || !audioModule || !inputModule || !uiModule || !cameraModule || !diagnosticsModule || !loggingModule || !physicsModule || !mathModule || !particleModule || !globalStateModule || !timerModule || !animationModule
#if ENGINE_EDITOR
            || !editorModule
#endif
            )
        {
            Py_XDECREF(entityModule);
            Py_XDECREF(assetModule);
            Py_XDECREF(audioModule);
            Py_XDECREF(inputModule);
            Py_XDECREF(uiModule);
            Py_XDECREF(cameraModule);
            Py_XDECREF(diagnosticsModule);
            Py_XDECREF(loggingModule);
            Py_XDECREF(physicsModule);
            Py_XDECREF(mathModule);
            Py_XDECREF(particleModule);
            Py_XDECREF(globalStateModule);
            Py_XDECREF(timerModule);
            Py_XDECREF(animationModule);
#if ENGINE_EDITOR
            Py_XDECREF(editorModule);
#endif
            Py_DECREF(module);
            return nullptr;
        }

        PyModule_AddIntConstant(assetModule, "Asset_Texture", static_cast<int>(AssetType::Texture));
        PyModule_AddIntConstant(assetModule, "Asset_Material", static_cast<int>(AssetType::Material));
        PyModule_AddIntConstant(assetModule, "Asset_Model2D", static_cast<int>(AssetType::Model2D));
        PyModule_AddIntConstant(assetModule, "Asset_Model3D", static_cast<int>(AssetType::Model3D));
        PyModule_AddIntConstant(assetModule, "Asset_PointLight", static_cast<int>(AssetType::PointLight));
        PyModule_AddIntConstant(assetModule, "Asset_Audio", static_cast<int>(AssetType::Audio));
        PyModule_AddIntConstant(assetModule, "Asset_Script", static_cast<int>(AssetType::Script));
        PyModule_AddIntConstant(assetModule, "Asset_Shader", static_cast<int>(AssetType::Shader));
        PyModule_AddIntConstant(assetModule, "Asset_Level", static_cast<int>(AssetType::Level));
        PyModule_AddIntConstant(assetModule, "Asset_Widget", static_cast<int>(AssetType::Widget));
        PyModule_AddIntConstant(assetModule, "Asset_NativeScript", static_cast<int>(AssetType::NativeScript));


        if (!AddSubmodule(module, entityModule, "entity") ||
            !AddSubmodule(module, assetModule, "assetmanagement") ||
            !AddSubmodule(module, audioModule, "audio") ||
            !AddSubmodule(module, inputModule, "input") ||
            !AddSubmodule(module, uiModule, "ui") ||
            !AddSubmodule(module, cameraModule, "camera") ||
            !AddSubmodule(module, diagnosticsModule, "diagnostics") ||
            !AddSubmodule(module, loggingModule, "logging") ||
            !AddSubmodule(module, physicsModule, "physics") ||
            !AddSubmodule(module, mathModule, "math") ||
            !AddSubmodule(module, particleModule, "particle") ||
            !AddSubmodule(module, globalStateModule, "globalstate") ||
            !AddSubmodule(module, timerModule, "timer") ||
            !AddSubmodule(module, animationModule, "animation")
#if ENGINE_EDITOR
            || !AddSubmodule(module, editorModule, "editor")
#endif
            )
        {
            Py_DECREF(module);
            return nullptr;
        }


        return module;
    }

    // The Script base class is injected into the engine module so user scripts can inherit from it.
    static const char* kScriptBaseClassDef = R"(
class Script:
    """Base class for Python gameplay scripts.

    Subclass this and override lifecycle methods:
        on_loaded()          – called once when the entity is first seen
        tick(dt)             – called every frame
        on_begin_overlap(other) – called when a physics overlap begins
        on_end_overlap(other)   – called when a physics overlap ends
        on_destroy()         – called when the entity is removed

    The 'entity' property is set automatically by the engine.
    """
    def __init__(self):
        self._entity = 0

    @property
    def entity(self):
        return self._entity

    def on_loaded(self): pass
    def tick(self, dt): pass
    def on_begin_overlap(self, other_entity): pass
    def on_end_overlap(self, other_entity): pass
    def on_destroy(self): pass
)";

    bool InjectScriptBaseClass(PyObject* engineModule)
    {
        PyObject* moduleDict = PyModule_GetDict(engineModule);
        if (!moduleDict) return false;
        PyObject* result = PyRun_StringFlags(kScriptBaseClassDef, Py_file_input, moduleDict, moduleDict, nullptr);
        if (!result)
        {
            LogPythonError("Python: failed to inject Script base class");
            return false;
        }
        Py_DECREF(result);
        return true;
    }

    PyMODINIT_FUNC PyInit_engine()
    {
        PyObject* mod = CreateEngineModule();
        if (mod)
        {
            InjectScriptBaseClass(mod);
        }
        return mod;
    }

    // ── PyLogWriter – redirects sys.stdout / sys.stderr to Logger ────────
    struct PyLogWriter
    {
        PyObject_HEAD
        Logger::LogLevel level;
        std::string      buffer;
    };

    static void PyLogWriter_dealloc(PyLogWriter* self)
    {
        self->buffer.~basic_string();
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }

    static PyObject* PyLogWriter_write(PyLogWriter* self, PyObject* args)
    {
        const char* text = nullptr;
        if (!PyArg_ParseTuple(args, "s", &text))
            return nullptr;

        if (text)
        {
            self->buffer += text;
            // Flush complete lines to the Logger
            std::string::size_type pos;
            while ((pos = self->buffer.find('\n')) != std::string::npos)
            {
                std::string line = self->buffer.substr(0, pos);
                self->buffer.erase(0, pos + 1);
                if (!line.empty())
                {
                    Logger::Instance().log(Logger::Category::Scripting, line, self->level);
                }
            }
        }
        Py_RETURN_NONE;
    }

    static PyObject* PyLogWriter_flush(PyLogWriter* self, PyObject*)
    {
        // Flush any remaining partial line
        if (!self->buffer.empty())
        {
            Logger::Instance().log(Logger::Category::Scripting, self->buffer, self->level);
            self->buffer.clear();
        }
        Py_RETURN_NONE;
    }

    static PyMethodDef PyLogWriter_methods[] = {
        { "write", reinterpret_cast<PyCFunction>(PyLogWriter_write), METH_VARARGS, "Write text to the engine logger." },
        { "flush", reinterpret_cast<PyCFunction>(PyLogWriter_flush), METH_NOARGS,  "Flush buffered text to the engine logger." },
        { nullptr, nullptr, 0, nullptr }
    };

    static PyTypeObject PyLogWriterType = {
        PyVarObject_HEAD_INIT(nullptr, 0)
        "engine.LogWriter",                     // tp_name
        sizeof(PyLogWriter),                    // tp_basicsize
        0,                                      // tp_itemsize
        reinterpret_cast<destructor>(PyLogWriter_dealloc), // tp_dealloc
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        Py_TPFLAGS_DEFAULT,                     // tp_flags
        "Redirects output to the engine Logger",// tp_doc
        0, 0, 0, 0, 0, 0,
        PyLogWriter_methods,                    // tp_methods
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        PyType_GenericNew,                      // tp_new
    };

    // Creates and installs LogWriter instances on sys.stdout / sys.stderr
    void InstallPythonLogRedirect()
    {
        if (PyType_Ready(&PyLogWriterType) < 0)
        {
            Logger::Instance().log(Logger::Category::Engine, "Python: failed to ready LogWriter type", Logger::LogLevel::WARNING);
            return;
        }

        auto makeWriter = [](Logger::LogLevel level) -> PyObject*
        {
            PyLogWriter* w = PyObject_New(PyLogWriter, &PyLogWriterType);
            if (!w) return nullptr;
            new (&w->buffer) std::string();
            w->level = level;
            return reinterpret_cast<PyObject*>(w);
        };

        PyObject* stdoutWriter = makeWriter(Logger::LogLevel::INFO);
        PyObject* stderrWriter = makeWriter(Logger::LogLevel::ERROR);

        if (stdoutWriter)
            PySys_SetObject("stdout", stdoutWriter);
        if (stderrWriter)
            PySys_SetObject("stderr", stderrWriter);

        Py_XDECREF(stdoutWriter);
        Py_XDECREF(stderrWriter);

        Logger::Instance().log(Logger::Category::Engine, "Python: sys.stdout/stderr redirected to Logger", Logger::LogLevel::INFO);
    }
}

namespace Scripting
{
    bool Initialize()
    {
        if (Py_IsInitialized())
        {
            return true;
        }
        Logger::Instance().log(Logger::Category::Engine, "Python: initializing embedded interpreter", Logger::LogLevel::INFO);
        if (PyImport_AppendInittab("engine", &PyInit_engine) == -1)
        {
            Logger::Instance().log(Logger::Category::Engine, "Python: failed to append engine module", Logger::LogLevel::ERROR);
            return false;
        }
        Py_Initialize();
        if (!Py_IsInitialized())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python: initialization failed", Logger::LogLevel::ERROR);
            return false;
        }
        InstallPythonLogRedirect();

        // Register cross-language bridge: C++ scripts can call Python functions
        NativeScriptManager::Instance().setPythonCallHandler(HandleCppCallsPython);

        Logger::Instance().log(Logger::Category::Engine, "Python: initialized", Logger::LogLevel::INFO);
        return Py_IsInitialized();
    }

    void Shutdown()
    {
        if (Py_IsInitialized())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python: shutting down", Logger::LogLevel::INFO);
            for (auto& [entity, inst] : s_entityInstances)
            {
                Py_XDECREF(inst);
            }
            s_entityInstances.clear();
            for (auto& [path, state] : s_scripts)
            {
                ReleaseScriptState(state);
            }
            s_scripts.clear();
            ReleaseLevelScriptState();
            ScriptDetail::ClearKeyCallbacks();
            ScriptDetail::ClearTimers();
            ClearAssetLoadCallbacks();
            s_lastLevel = nullptr;
            s_lastLevelScriptPath.clear();
            Py_Finalize();
        }
    }

    void ReloadScripts()
    {
        if (!Py_IsInitialized())
        {
            return;
        }

        Logger::Instance().log(Logger::Category::Engine, "Python: reloading scripts", Logger::LogLevel::INFO);
        PyGILState_STATE gilState = PyGILState_Ensure();
        for (auto& [entity, inst] : s_entityInstances)
        {
            Py_XDECREF(inst);
        }
        s_entityInstances.clear();
        for (auto& [path, state] : s_scripts)
        {
            ReleaseScriptState(state);
        }
        s_scripts.clear();
        ReleaseLevelScriptState();
        ScriptDetail::ClearKeyCallbacks();
        ScriptDetail::ClearTimers();
        ClearAssetLoadCallbacks();
        s_lastLevel = nullptr;
        s_lastLevelScriptPath.clear();
        PyGILState_Release(gilState);
    }

    void UpdateScripts(float deltaSeconds)
    {
        if (!Py_IsInitialized())
        {
            return;
        }

        s_lastDeltaSeconds = deltaSeconds;


        auto& diagnostics = DiagnosticsManager::Instance();
        EngineLevel* level = diagnostics.getActiveLevelSoft();
        const bool scenePrepared = diagnostics.isScenePrepared();
        const std::string levelScriptPath = level ? level->getLevelScriptPath() : std::string{};
        const bool levelChanged = level != s_lastLevel || levelScriptPath != s_lastLevelScriptPath;

        PyGILState_STATE gilState = PyGILState_Ensure();
        ProcessAssetLoadCallbacks();
        ScriptDetail::ProcessTimers(deltaSeconds);
        if (levelChanged)
        {
            if (s_levelScript.onUnloadedFunc && s_levelScript.loadedCalled)
            {
                PyObject* result = PyObject_CallFunction(s_levelScript.onUnloadedFunc, nullptr);
                if (!result)
                {
                    PyErr_Print();
                }
                else
                {
                    Py_DECREF(result);
                }
            }
            ReleaseLevelScriptState();
            s_lastLevel = level;
            s_lastLevelScriptPath = levelScriptPath;
			if (level && s_lastLevelScriptPath.empty())
			{
				const std::string relPath = (std::filesystem::path("Scripts") / "LevelScript.py").generic_string();
				const std::string absPath = AssetManager::Instance().getAbsoluteContentPath(relPath);
				if (!absPath.empty())
				{
					std::error_code ec;
					std::filesystem::create_directories(std::filesystem::path(absPath).parent_path(), ec);
					if (!std::filesystem::exists(absPath))
					{
						std::ofstream out(absPath, std::ios::out | std::ios::trunc);
						if (out.is_open())
						{
							out << "def on_level_loaded():\n";
							out << "    pass\n\n";
							out << "def on_level_unloaded():\n";
							out << "    pass\n";
						}
					}
					level->setLevelScriptPath(relPath);
					s_lastLevelScriptPath = relPath;
					Logger::Instance().log(Logger::Category::Engine,
						"Python: no level script assigned; created dummy script at " + relPath,
						Logger::LogLevel::WARNING);
				}
				else
				{
					Logger::Instance().log(Logger::Category::Engine,
						"Python: active level has no level script assigned and no project path is available.",
						Logger::LogLevel::ERROR);
				}
			}
            for (auto& [path, state] : s_scripts)
            {
                state.startedEntities.clear();
            }
        }

		if (level && !s_lastLevelScriptPath.empty())
        {
            if (!LoadLevelScriptModule(s_lastLevelScriptPath))
            {
                s_levelScript.loadedCalled = true;
            }
			else if (!s_levelScript.loadedCalled)
            {
				if (s_levelScript.onLoadedFunc)
                {
					PyObject* result = PyObject_CallFunction(s_levelScript.onLoadedFunc, nullptr);
					if (!result)
					{
						LogPythonError("Python: on_level_loaded failed (level script: " + s_lastLevelScriptPath + ")");
					}
					else
					{
						Py_DECREF(result);
					}
                }
                s_levelScript.loadedCalled = true;
            }
        }

        if (!level)
        {
            PyGILState_Release(gilState);
            return;
        }

		if (!diagnostics.isScenePrepared())
		{
			PyGILState_Release(gilState);
			return;
		}

		const auto& entities = level->getScriptEntities();

        for (const auto entity : entities)
        {
            const auto* component = ECS::ECSManager::Instance().getComponent<ECS::LogicComponent>(entity);
            if (!component || component->scriptPath.empty())
            {
                continue;
            }

            auto& state = s_scripts[component->scriptPath];
            if (!state.module)
            {
                if (state.loadFailed)
                    continue;
                if (!LoadScriptModule(component->scriptPath, state))
                {
                    state.loadFailed = true;
                    continue;
                }
            }

            const unsigned long entityUL = static_cast<unsigned long>(entity);
            ScriptDetail::s_currentEntity = entityUL;

            if (state.scriptClass)
            {
                // ── Class-based model ──────────────────────────────────
                auto instIt = s_entityInstances.find(entityUL);
                if (instIt == s_entityInstances.end())
                {
                    // Create instance and set entity
                    PyObject* instance = PyObject_CallNoArgs(state.scriptClass);
                    if (!instance)
                    {
                        LogPythonError("Python: failed to create Script instance for " + DescribeEntity(entity));
                        continue;
                    }
                    PyObject* pyEntity = PyLong_FromUnsignedLong(entityUL);
                    PyObject_SetAttrString(instance, "_entity", pyEntity);
                    Py_DECREF(pyEntity);
                    s_entityInstances[entityUL] = instance;
                    instIt = s_entityInstances.find(entityUL);

                    // Call on_loaded
                    PyObject* result = PyObject_CallMethod(instance, "on_loaded", nullptr);
                    if (!result)
                    {
                        LogPythonError("Python: on_loaded failed for " + DescribeEntity(entity));
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                    state.startedEntities.insert(entityUL);
                }

                PyObject* instance = instIt->second;
                PyObject* result = PyObject_CallMethod(instance, "tick", "f", deltaSeconds);
                if (!result)
                {
                    LogPythonError("Python: tick failed for " + DescribeEntity(entity));
                }
                else
                {
                    Py_DECREF(result);
                }
            }
            else
            {
                // ── Function-based model (legacy) ──────────────────────
                if (state.onLoadedFunc &&
                    state.startedEntities.find(entityUL) == state.startedEntities.end())
                {
                    PyObject* result = PyObject_CallFunction(state.onLoadedFunc, "k", entityUL);
                    if (!result)
                    {
                        LogPythonError("Python: on_loaded failed for " + DescribeEntity(entity) + " (script: " + component->scriptPath + ")");
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                    state.startedEntities.insert(entityUL);
                }

                if (state.tickFunc)
                {
                    PyObject* result = PyObject_CallFunction(state.tickFunc, "kf", entityUL, deltaSeconds);
                    if (!result)
                    {
                        LogPythonError("Python: tick failed for " + DescribeEntity(entity) + " (script: " + component->scriptPath + ")");
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                }
            }
        }

        // Dispatch overlap events from physics to entity scripts
        if (diagnostics.isPIEActive())
        {
            auto& physics = PhysicsWorld::Instance();

            auto dispatchOverlap = [&](ECS::Entity entity, ECS::Entity otherEntity, bool isBegin)
            {
                const auto* sc = ECS::ECSManager::Instance().getComponent<ECS::LogicComponent>(entity);
                if (!sc || sc->scriptPath.empty()) return;
                auto it = s_scripts.find(sc->scriptPath);
                if (it == s_scripts.end() || !it->second.module) return;
                ScriptDetail::s_currentEntity = static_cast<unsigned long>(entity);

                if (it->second.scriptClass)
                {
                    // Class-based overlap dispatch
                    auto instIt = s_entityInstances.find(static_cast<unsigned long>(entity));
                    if (instIt == s_entityInstances.end()) return;
                    const char* method = isBegin ? "on_begin_overlap" : "on_end_overlap";
                    PyObject* result = PyObject_CallMethod(instIt->second, method, "k",
                        static_cast<unsigned long>(otherEntity));
                    if (!result)
                    {
                        LogPythonError(std::string("Python: ") + method + " failed for " + DescribeEntity(entity));
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                }
                else
                {
                    // Function-based overlap dispatch
                    PyObject* func = isBegin ? it->second.onBeginOverlapFunc : it->second.onEndOverlapFunc;
                    if (!func) return;
                    PyObject* result = PyObject_CallFunction(func, "kk",
                        static_cast<unsigned long>(entity),
                        static_cast<unsigned long>(otherEntity));
                    if (!result)
                    {
                        LogPythonError(std::string("Python: ") +
                            (isBegin ? "on_begin_overlap" : "on_end_overlap") +
                            " failed for " + DescribeEntity(entity));
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                }
            };

            for (const auto& ev : physics.getBeginOverlapEvents())
            {
                dispatchOverlap(static_cast<ECS::Entity>(ev.entityA), static_cast<ECS::Entity>(ev.entityB), true);
                dispatchOverlap(static_cast<ECS::Entity>(ev.entityB), static_cast<ECS::Entity>(ev.entityA), true);
            }
            for (const auto& ev : physics.getEndOverlapEvents())
            {
                dispatchOverlap(static_cast<ECS::Entity>(ev.entityA), static_cast<ECS::Entity>(ev.entityB), false);
                dispatchOverlap(static_cast<ECS::Entity>(ev.entityB), static_cast<ECS::Entity>(ev.entityA), false);
            }
        }

        PyGILState_Release(gilState);
    }

    void HandleKeyDown(int key)
    {
        if (!Py_IsInitialized())
        {
            return;
        }
        PyGILState_STATE gilState = PyGILState_Ensure();
        ScriptDetail::InvokeKeyCallbacks(ScriptDetail::s_onKeyPressed, key);
        ScriptDetail::InvokeKeyCallbacksForKey(ScriptDetail::s_keyPressedCallbacks, key);
        PyGILState_Release(gilState);
    }

    void HandleKeyUp(int key)
    {
        if (!Py_IsInitialized())
        {
            return;
        }
        PyGILState_STATE gilState = PyGILState_Ensure();
        ScriptDetail::InvokeKeyCallbacks(ScriptDetail::s_onKeyReleased, key);
        ScriptDetail::InvokeKeyCallbacksForKey(ScriptDetail::s_keyReleasedCallbacks, key);
        PyGILState_Release(gilState);
    }

    void SetRenderer(Renderer* renderer)
    {
        s_renderer = renderer;

        // Set up the InputActionManager dispatch hook for Python action callbacks
        InputActionManager::Instance().setDispatchHook(
            [](const std::string& actionName, bool pressed)
            {
                if (!Py_IsInitialized()) return;
                PyGILState_STATE gilState = PyGILState_Ensure();

                auto& map = pressed
                    ? ScriptDetail::s_actionPressedCallbacks
                    : ScriptDetail::s_actionReleasedCallbacks;
                auto it = map.find(actionName);
                if (it != map.end())
                {
                    for (auto* cb : it->second)
                    {
                        if (!cb) continue;
                        PyObject* result = PyObject_CallNoArgs(cb);
                        if (!result)
                            ScriptDetail::LogPythonError("Python: input action callback failed");
                        else
                            Py_DECREF(result);
                    }
                }

                PyGILState_Release(gilState);
            });
    }

    // ── Script Hot-Reload ─────────────────────────────────────────────

    void InitScriptHotReload(const std::string& contentDirectory)
    {
        if (contentDirectory.empty())
            return;
        s_scriptHotReload.init(contentDirectory, 0.5);
    }

    void PollScriptHotReload()
    {
        // Sync enabled flag from persisted config (UI toggle writes to DiagnosticsManager)
        if (auto v = DiagnosticsManager::Instance().getState("ScriptHotReloadEnabled"))
            s_scriptHotReloadEnabled = (*v != "false");

        if (!s_scriptHotReloadEnabled || !s_scriptHotReload.isInitialized())
            return;

        auto changed = s_scriptHotReload.poll();
        if (changed.empty())
            return;

        if (!Py_IsInitialized())
            return;

        auto& diagnostics = DiagnosticsManager::Instance();
        const std::string contentDir = s_scriptHotReload.directory();

        PyGILState_STATE gilState = PyGILState_Ensure();

        int reloadedCount = 0;

        for (const auto& absPath : changed)
        {
            // Convert absolute path to a relative content path for matching
            std::filesystem::path absFile(absPath);
            std::filesystem::path contentRoot(contentDir);
            std::string relPath;

            // Try to get a Content-relative path (e.g. "Scripts/MyScript.py")
            auto rel = absFile.lexically_relative(contentRoot);
            if (!rel.empty() && rel.native()[0] != '.')
            {
                relPath = rel.generic_string();
            }
            else
            {
                relPath = absFile.filename().generic_string();
            }

            // Also build the "Content/..." prefixed variant
            std::string contentPrefixed = "Content/" + relPath;

            // Check if this script is currently loaded (try both path forms)
            ScriptState* matchedState = nullptr;
            std::string matchedKey;

            for (auto& [key, state] : s_scripts)
            {
                if (key == relPath || key == contentPrefixed || key == absPath)
                {
                    matchedState = &state;
                    matchedKey = key;
                    break;
                }
                // Also try comparing resolved absolute paths
                std::filesystem::path resolved = ResolveScriptPath(key);
                std::error_code ec;
                if (std::filesystem::equivalent(resolved, absFile, ec) && !ec)
                {
                    matchedState = &state;
                    matchedKey = key;
                    break;
                }
            }

            if (matchedState)
            {
                // Release old module and re-load
                std::unordered_set<unsigned long> savedEntities = std::move(matchedState->startedEntities);
                ReleaseScriptState(*matchedState);

                if (LoadScriptModule(matchedKey, *matchedState))
                {
                    // Preserve started-entities set so onloaded is NOT re-called for
                    // entities that already received it (prevents duplicate init).
                    // Scripts that want fresh init on reload can implement an on_reload() hook.
                    matchedState->startedEntities = std::move(savedEntities);
                    ++reloadedCount;

                    Logger::Instance().log(Logger::Category::Engine,
                        "ScriptHotReload: reloaded " + matchedKey,
                        Logger::LogLevel::INFO);
                }
                else
                {
                    Logger::Instance().log(Logger::Category::Engine,
                        "ScriptHotReload: failed to reload " + matchedKey,
                        Logger::LogLevel::ERROR);

                    diagnostics.enqueueToastNotification("Script reload failed: " + matchedKey, 4.0f, DiagnosticsManager::NotificationLevel::Error);
                }
            }

            // Check level script
            if (!s_lastLevelScriptPath.empty() && s_levelScript.module)
            {
                std::filesystem::path resolvedLevel = ResolveScriptPath(s_lastLevelScriptPath);
                std::error_code ec;
                if (std::filesystem::equivalent(resolvedLevel, absFile, ec) && !ec)
                {
                    bool wasLoaded = s_levelScript.loadedCalled;
                    ReleaseLevelScriptState();
                    if (LoadLevelScriptModule(s_lastLevelScriptPath))
                    {
                        if (wasLoaded && s_levelScript.onLoadedFunc)
                        {
                            PyObject* result = PyObject_CallFunction(s_levelScript.onLoadedFunc, nullptr);
                            if (!result)
                                LogPythonError("Python: on_level_loaded failed after hot-reload");
                            else
                                Py_DECREF(result);
                        }
                        s_levelScript.loadedCalled = wasLoaded;
                        ++reloadedCount;
                        Logger::Instance().log(Logger::Category::Engine,
                            "ScriptHotReload: reloaded level script " + s_lastLevelScriptPath,
                            Logger::LogLevel::INFO);
                    }
                    else
                    {
                        Logger::Instance().log(Logger::Category::Engine,
                            "ScriptHotReload: failed to reload level script",
                            Logger::LogLevel::ERROR);
                        diagnostics.enqueueToastNotification("Level script reload failed", 4.0f, DiagnosticsManager::NotificationLevel::Error);
                    }
                }
            }
        }

        PyGILState_Release(gilState);

        if (reloadedCount > 0)
        {
            std::string msg = "Script reloaded (" + std::to_string(reloadedCount) + " file" +
                (reloadedCount > 1 ? "s" : "") + ")";
            diagnostics.enqueueToastNotification(msg, 3.0f, DiagnosticsManager::NotificationLevel::Success);
        }
    }

    bool IsScriptHotReloadEnabled()
    {
        return s_scriptHotReloadEnabled;
    }

    void SetScriptHotReloadEnabled(bool enabled)
    {
        s_scriptHotReloadEnabled = enabled;
    }

}
