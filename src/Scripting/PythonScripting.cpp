#include "PythonScripting.h"

#ifdef _DEBUG
#define PYTHONSCRIPTING_DEBUG_RESTORE
#undef _DEBUG
#endif
#include <Python.h>
#ifdef PYTHONSCRIPTING_DEBUG_RESTORE
#define _DEBUG
#undef PYTHONSCRIPTING_DEBUG_RESTORE
#endif

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Core/EngineLevel.h"
#include "../Logger/Logger.h"
#include "../Renderer/UIManager.h"
#include <SDL3/SDL.h>
#include <cctype>
#include "../Renderer/Renderer.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace
{
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
                        PyObject* joined = PyUnicode_Join(PyUnicode_FromString(""), formatted);
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
    struct ScriptState
    {
        PyObject* module{ nullptr };
        PyObject* onLoadedFunc{ nullptr };
        PyObject* tickFunc{ nullptr };
        std::unordered_set<unsigned long> startedEntities;
    };

    struct LevelScriptState
    {
        PyObject* module{ nullptr };
        PyObject* onLoadedFunc{ nullptr };
        PyObject* onUnloadedFunc{ nullptr };
        bool loadedCalled{ false };
    };

    std::unordered_map<std::string, ScriptState> s_scripts;
    float s_lastDeltaSeconds = 0.0f;
    EngineLevel* s_lastLevel{ nullptr };
    std::string s_lastLevelScriptPath;
    LevelScriptState s_levelScript;
    PyObject* s_onKeyPressed{ nullptr };
    PyObject* s_onKeyReleased{ nullptr };
    std::unordered_map<int, std::vector<PyObject*>> s_keyPressedCallbacks;
    std::unordered_map<int, std::vector<PyObject*>> s_keyReleasedCallbacks;
    Renderer* s_renderer{ nullptr };

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
    }

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

        std::ifstream file(resolvedPath, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python script not found: " + resolvedPath.string(), Logger::LogLevel::ERROR);
            return false;
        }
        const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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
        PyObject* onLoadedFunc = PyDict_GetItemString(moduleDict, "onloaded");
        PyObject* tickFunc = PyDict_GetItemString(moduleDict, "tick");
        if (onLoadedFunc && PyCallable_Check(onLoadedFunc))
        {
            Py_INCREF(onLoadedFunc);
            state.onLoadedFunc = onLoadedFunc;
        }
        else
        {
            Logger::Instance().log(Logger::Category::Engine,
                "Python: script has no callable onloaded(): " + resolvedPath.string(),
                Logger::LogLevel::WARNING);
        }
        if (tickFunc && PyCallable_Check(tickFunc))
        {
            Py_INCREF(tickFunc);
            state.tickFunc = tickFunc;
        }
        else
        {
            Logger::Instance().log(Logger::Category::Engine,
                "Python: script has no callable tick(entity, dt): " + resolvedPath.string(),
                Logger::LogLevel::WARNING);
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
        std::ifstream file(resolvedPath, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python level script not found: " + resolvedPath.string(), Logger::LogLevel::ERROR);
            return false;
        }
        const std::string code((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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
    bool AddComponentByKind(ECS::Entity entity, int kind)
    {
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:
            return ECS::addComponent<ECS::TransformComponent>(entity);
        case ECS::ComponentKind::Mesh:
            return ECS::addComponent<ECS::MeshComponent>(entity);
        case ECS::ComponentKind::Material:
            return ECS::addComponent<ECS::MaterialComponent>(entity);
        case ECS::ComponentKind::Light:
            return ECS::addComponent<ECS::LightComponent>(entity);
        case ECS::ComponentKind::Camera:
            return ECS::addComponent<ECS::CameraComponent>(entity);
        case ECS::ComponentKind::Physics:
            return ECS::addComponent<ECS::PhysicsComponent>(entity);
        case ECS::ComponentKind::Script:
            return ECS::addComponent<ECS::ScriptComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::addComponent<ECS::NameComponent>(entity);
        default:
            return false;
        }
    }

    ECS::TransformComponent* GetTransformComponent(ECS::Entity entity)
    {
        return ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
    }

    bool RemoveComponentByKind(ECS::Entity entity, int kind)
    {
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:
            return ECS::removeComponent<ECS::TransformComponent>(entity);
        case ECS::ComponentKind::Mesh:
            return ECS::removeComponent<ECS::MeshComponent>(entity);
        case ECS::ComponentKind::Material:
            return ECS::removeComponent<ECS::MaterialComponent>(entity);
        case ECS::ComponentKind::Light:
            return ECS::removeComponent<ECS::LightComponent>(entity);
        case ECS::ComponentKind::Camera:
            return ECS::removeComponent<ECS::CameraComponent>(entity);
        case ECS::ComponentKind::Physics:
            return ECS::removeComponent<ECS::PhysicsComponent>(entity);
        case ECS::ComponentKind::Script:
            return ECS::removeComponent<ECS::ScriptComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::removeComponent<ECS::NameComponent>(entity);
        default:
            return false;
        }
    }

    bool RequireComponentByKind(ECS::Schema& schema, int kind)
    {
        switch (static_cast<ECS::ComponentKind>(kind))
        {
        case ECS::ComponentKind::Transform:
            schema.require<ECS::TransformComponent>();
            return true;
        case ECS::ComponentKind::Mesh:
            schema.require<ECS::MeshComponent>();
            return true;
        case ECS::ComponentKind::Material:
            schema.require<ECS::MaterialComponent>();
            return true;
        case ECS::ComponentKind::Light:
            schema.require<ECS::LightComponent>();
            return true;
        case ECS::ComponentKind::Camera:
            schema.require<ECS::CameraComponent>();
            return true;
        case ECS::ComponentKind::Physics:
            schema.require<ECS::PhysicsComponent>();
            return true;
        case ECS::ComponentKind::Script:
            schema.require<ECS::ScriptComponent>();
            return true;
        case ECS::ComponentKind::Name:
            schema.require<ECS::NameComponent>();
            return true;
        default:
            return false;
        }
    }

    PyObject* py_create_entity(PyObject*, PyObject*)
    {
        const ECS::Entity entity = ECS::createEntity();
        return PyLong_FromUnsignedLong(entity);
    }

    PyObject* py_attach_component(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int kind = 0;
        if (!PyArg_ParseTuple(args, "ki", &entity, &kind))
        {
            return nullptr;
        }
        const bool result = AddComponentByKind(static_cast<ECS::Entity>(entity), kind);
        if (!result)
        {
            PyErr_SetString(PyExc_ValueError, "Unknown component kind or add failed.");
            return nullptr;
        }
        Py_RETURN_TRUE;
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

    PyObject* py_set_on_key_pressed(PyObject*, PyObject* args)
    {
        PyObject* callback = Py_None;
        if (!PyArg_ParseTuple(args, "O", &callback))
        {
            return nullptr;
        }
        if (!StoreKeyCallback(callback, s_onKeyPressed))
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
        if (!StoreKeyCallback(callback, s_onKeyReleased))
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
        if (!StoreKeyCallbackForKey(callback, s_keyPressedCallbacks, key))
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
        if (!StoreKeyCallbackForKey(callback, s_keyReleasedCallbacks, key))
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

    PyObject* py_show_modal_message(PyObject*, PyObject* args)
    {
        const char* message = nullptr;
        PyObject* callback = Py_None;
        if (!PyArg_ParseTuple(args, "s|O", &message, &callback))
        {
            return nullptr;
        }
        if (callback != Py_None && !PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return nullptr;
        }

        std::function<void()> onClosed;
        if (callback != Py_None)
        {
            Py_INCREF(callback);
            std::shared_ptr<PyObject> callbackRef(callback, [](PyObject* obj)
                {
                    Py_DECREF(obj);
                });
            onClosed = [callbackRef]()
                {
                    PyGILState_STATE gilState = PyGILState_Ensure();
                    PyObject* result = PyObject_CallFunctionObjArgs(callbackRef.get(), nullptr);
                    if (!result)
                    {
                        LogPythonError("Python: modal close callback failed");
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                    PyGILState_Release(gilState);
                };
        }

        if (auto* uiManager = UIManager::GetActiveInstance())
        {
            uiManager->showModalMessage(message, std::move(onClosed));
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_close_modal_message(PyObject*, PyObject*)
    {
        if (auto* uiManager = UIManager::GetActiveInstance())
        {
            uiManager->closeModalMessage();
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_show_toast_message(PyObject*, PyObject* args)
    {
        const char* message = nullptr;
        float duration = 2.5f;
        if (!PyArg_ParseTuple(args, "s|f", &message, &duration))
        {
            return nullptr;
        }
        if (auto* uiManager = UIManager::GetActiveInstance())
        {
            uiManager->showToastMessage(message, duration);
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_get_transform(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        const auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            Py_RETURN_NONE;
        }
        PyObject* position = Py_BuildValue("(fff)", transform->position[0], transform->position[1], transform->position[2]);
        PyObject* rotation = Py_BuildValue("(fff)", transform->rotation[0], transform->rotation[1], transform->rotation[2]);
        PyObject* scale = Py_BuildValue("(fff)", transform->scale[0], transform->scale[1], transform->scale[2]);
        PyObject* result = Py_BuildValue("(OOO)", position, rotation, scale);
        Py_DECREF(position);
        Py_DECREF(rotation);
        Py_DECREF(scale);
        return result;
    }

    PyObject* py_set_position(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &x, &y, &z))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->position[0] = x;
        transform->position[1] = y;
        transform->position[2] = z;
        Py_RETURN_TRUE;
    }

    PyObject* py_translate(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float dx = 0.0f;
        float dy = 0.0f;
        float dz = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &dx, &dy, &dz))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->position[0] += dx;
        transform->position[1] += dy;
        transform->position[2] += dz;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_rotation(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &pitch, &yaw, &roll))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->rotation[0] = pitch;
        transform->rotation[1] = yaw;
        transform->rotation[2] = roll;
        Py_RETURN_TRUE;
    }

    PyObject* py_rotate(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float dp = 0.0f;
        float dy = 0.0f;
        float dr = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &dp, &dy, &dr))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->rotation[0] += dp;
        transform->rotation[1] += dy;
        transform->rotation[2] += dr;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_scale(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float sx = 1.0f;
        float sy = 1.0f;
        float sz = 1.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &sx, &sy, &sz))
        {
            return nullptr;
        }
        auto* transform = GetTransformComponent(static_cast<ECS::Entity>(entity));
        if (!transform)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no TransformComponent.");
            return nullptr;
        }
        transform->scale[0] = sx;
        transform->scale[1] = sy;
        transform->scale[2] = sz;
        Py_RETURN_TRUE;
    }


    PyObject* py_set_mesh(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        const char* path = nullptr;
        if (!PyArg_ParseTuple(args, "ks", &entity, &path))
        {
            return nullptr;
        }
        ECS::Entity ecsEntity = static_cast<ECS::Entity>(entity);
        if (!ECS::hasComponent<ECS::MeshComponent>(ecsEntity))
        {
            if (!ECS::addComponent<ECS::MeshComponent>(ecsEntity))
            {
                PyErr_SetString(PyExc_ValueError, "Failed to add MeshComponent.");
                return nullptr;
            }
        }

        int assetId = AssetManager::Instance().loadAsset(path, AssetType::Model3D, AssetManager::Sync);
        if (assetId == 0)
        {
            assetId = AssetManager::Instance().loadAsset(path, AssetType::Model2D, AssetManager::Sync);
        }
        if (assetId == 0)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to load mesh asset.");
            return nullptr;
        }

        ECS::MeshComponent comp{};
        comp.meshAssetPath = path;
        comp.meshAssetId = static_cast<unsigned int>(assetId);
        if (!ECS::setComponent<ECS::MeshComponent>(ecsEntity, comp))
        {
            PyErr_SetString(PyExc_ValueError, "Failed to set MeshComponent.");
            return nullptr;
        }

        Py_RETURN_TRUE;
    }

    PyObject* py_get_mesh(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        ECS::Entity ecsEntity = static_cast<ECS::Entity>(entity);
        const auto* comp = ECS::ECSManager::Instance().getComponent<ECS::MeshComponent>(ecsEntity);
        if (!comp || comp->meshAssetPath.empty())
        {
            Py_RETURN_NONE;
        }
        return PyUnicode_FromString(comp->meshAssetPath.c_str());
    }

    PyObject* py_get_delta_time(PyObject*, PyObject*)
    {
        return PyFloat_FromDouble(static_cast<double>(s_lastDeltaSeconds));
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

    PyObject* py_detach_component(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        int kind = 0;
        if (!PyArg_ParseTuple(args, "ki", &entity, &kind))
        {
            return nullptr;
        }
        const bool result = RemoveComponentByKind(static_cast<ECS::Entity>(entity), kind);
        if (!result)
        {
            PyErr_SetString(PyExc_ValueError, "Unknown component kind or remove failed.");
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_get_entities(PyObject*, PyObject* args)
    {
        PyObject* listObj = nullptr;
        if (!PyArg_ParseTuple(args, "O", &listObj))
        {
            return nullptr;
        }
        PyObject* seq = PySequence_Fast(listObj, "Expected a sequence of component kinds");
        if (!seq)
        {
            return nullptr;
        }

        ECS::Schema schema;
        const Py_ssize_t count = PySequence_Fast_GET_SIZE(seq);
        PyObject** items = PySequence_Fast_ITEMS(seq);
        for (Py_ssize_t i = 0; i < count; ++i)
        {
            const int kind = static_cast<int>(PyLong_AsLong(items[i]));
            if (PyErr_Occurred())
            {
                Py_DECREF(seq);
                return nullptr;
            }
            if (!RequireComponentByKind(schema, kind))
            {
                Py_DECREF(seq);
                PyErr_SetString(PyExc_ValueError, "Unknown component kind in schema.");
                return nullptr;
            }
        }

        const auto entities = ECS::getEntitiesMatchingSchema(schema);
        PyObject* result = PyList_New(static_cast<Py_ssize_t>(entities.size()));
        for (size_t i = 0; i < entities.size(); ++i)
        {
            PyList_SetItem(result, static_cast<Py_ssize_t>(i), PyLong_FromUnsignedLong(entities[i]));
        }
        Py_DECREF(seq);
        return result;
    }

    PyObject* py_load_asset(PyObject*, PyObject* args)
    {
        const char* path = nullptr;
        int type = 0;
        if (!PyArg_ParseTuple(args, "si", &path, &type))
        {
            return nullptr;
        }
        const int id = AssetManager::Instance().loadAsset(path, static_cast<AssetType>(type), AssetManager::Sync);
        return PyLong_FromLong(id);
    }

    PyObject* py_save_asset(PyObject*, PyObject* args)
    {
        unsigned long id = 0;
        int type = 0;
        if (!PyArg_ParseTuple(args, "ki", &id, &type))
        {
            return nullptr;
        }
        Asset asset;
        asset.ID = static_cast<unsigned int>(id);
        asset.type = static_cast<AssetType>(type);
        const bool result = AssetManager::Instance().saveAsset(asset, AssetManager::Sync);
        if (!result)
        {
            PyErr_SetString(PyExc_RuntimeError, "Failed to save asset.");
            return nullptr;
        }
        Py_RETURN_TRUE;
    }

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

    PyMethodDef EngineMethods[] = {
        { "create_entity", py_create_entity, METH_NOARGS, "Create an entity." },
        { "attach_component", py_attach_component, METH_VARARGS, "Attach a component by kind." },
        { "detach_component", py_detach_component, METH_VARARGS, "Detach a component by kind." },
        { "get_entities", py_get_entities, METH_VARARGS, "Get entities matching component kinds." },
        { "get_transform", py_get_transform, METH_VARARGS, "Get transform (pos, rot, scale)." },
        { "set_position", py_set_position, METH_VARARGS, "Set entity position." },
        { "translate", py_translate, METH_VARARGS, "Translate entity position." },
        { "set_rotation", py_set_rotation, METH_VARARGS, "Set entity rotation." },
        { "rotate", py_rotate, METH_VARARGS, "Rotate entity." },
        { "set_scale", py_set_scale, METH_VARARGS, "Set entity scale." },
        { "load_asset", py_load_asset, METH_VARARGS, "Load an asset by path and type." },
        { "save_asset", py_save_asset, METH_VARARGS, "Save an asset by id and type." },
        { "unload_asset", py_unload_asset, METH_VARARGS, "Unload an asset by id." },
        { "set_mesh", py_set_mesh, METH_VARARGS, "Set mesh asset for an entity." },
        { "get_mesh", py_get_mesh, METH_VARARGS, "Get mesh asset path for an entity." },
        { "get_delta_time", py_get_delta_time, METH_NOARGS, "Get last frame delta time." },
        { "get_state", py_get_state, METH_VARARGS, "Get engine state string." },
        { "set_state", py_set_state, METH_VARARGS, "Set engine state string." },
        { "show_modal_message", py_show_modal_message, METH_VARARGS, "Show a blocking modal message." },
        { "close_modal_message", py_close_modal_message, METH_NOARGS, "Close the active modal message." },
        { "show_toast_message", py_show_toast_message, METH_VARARGS, "Show a toast message." },
        { "set_on_key_pressed", py_set_on_key_pressed, METH_VARARGS, "Set a global key pressed callback." },
        { "set_on_key_released", py_set_on_key_released, METH_VARARGS, "Set a global key released callback." },
        { "register_key_pressed", py_register_key_pressed, METH_VARARGS, "Register a key pressed callback for a key." },
        { "register_key_released", py_register_key_released, METH_VARARGS, "Register a key released callback for a key." },
        { "is_shift_pressed", py_is_shift_pressed, METH_NOARGS, "Check if shift is pressed." },
        { "is_ctrl_pressed", py_is_ctrl_pressed, METH_NOARGS, "Check if ctrl is pressed." },
        { "is_alt_pressed", py_is_alt_pressed, METH_NOARGS, "Check if alt is pressed." },
        { "get_key", py_get_key, METH_VARARGS, "Resolve a keycode from a key name." },
        { "get_camera_position", py_get_camera_position, METH_NOARGS, "Get the camera position." },
        { "set_camera_position", py_set_camera_position, METH_VARARGS, "Set the camera position." },
        { "get_camera_rotation", py_get_camera_rotation, METH_NOARGS, "Get the camera rotation (yaw, pitch)." },
        { "set_camera_rotation", py_set_camera_rotation, METH_VARARGS, "Set the camera rotation (yaw, pitch)." },
        { "log", py_log, METH_VARARGS, "Log a message (level: 0=info,1=warn,2=error)." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef EngineModule = {
        PyModuleDef_HEAD_INIT,
        "engine",
        "Engine scripting API",
        -1,
        EngineMethods
    };

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
        PyModule_AddIntConstant(module, "Component_Script", static_cast<int>(ECS::ComponentKind::Script));
        PyModule_AddIntConstant(module, "Component_Name", static_cast<int>(ECS::ComponentKind::Name));

        PyModule_AddIntConstant(module, "Asset_Texture", static_cast<int>(AssetType::Texture));
        PyModule_AddIntConstant(module, "Asset_Material", static_cast<int>(AssetType::Material));
        PyModule_AddIntConstant(module, "Asset_Model2D", static_cast<int>(AssetType::Model2D));
        PyModule_AddIntConstant(module, "Asset_Model3D", static_cast<int>(AssetType::Model3D));
        PyModule_AddIntConstant(module, "Asset_PointLight", static_cast<int>(AssetType::PointLight));
        PyModule_AddIntConstant(module, "Asset_Level", static_cast<int>(AssetType::Level));
        PyModule_AddIntConstant(module, "Asset_Widget", static_cast<int>(AssetType::Widget));

        PyModule_AddIntConstant(module, "Log_Info", 0);
        PyModule_AddIntConstant(module, "Log_Warning", 1);
        PyModule_AddIntConstant(module, "Log_Error", 2);

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

        return module;
    }

    PyMODINIT_FUNC PyInit_engine()
    {
        return CreateEngineModule();
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
        Logger::Instance().log(Logger::Category::Engine, "Python: initialized", Logger::LogLevel::INFO);
        return Py_IsInitialized();
    }

    void Shutdown()
    {
        if (Py_IsInitialized())
        {
            Logger::Instance().log(Logger::Category::Engine, "Python: shutting down", Logger::LogLevel::INFO);
            for (auto& [path, state] : s_scripts)
            {
                ReleaseScriptState(state);
            }
            s_scripts.clear();
            ReleaseLevelScriptState();
            ClearKeyCallbacks();
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
        for (auto& [path, state] : s_scripts)
        {
            ReleaseScriptState(state);
        }
        s_scripts.clear();
        ReleaseLevelScriptState();
        ClearKeyCallbacks();
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
            const auto* component = ECS::ECSManager::Instance().getComponent<ECS::ScriptComponent>(entity);
            if (!component || component->scriptPath.empty())
            {
                continue;
            }

            auto& state = s_scripts[component->scriptPath];
            if (!state.module)
            {
                if (!LoadScriptModule(component->scriptPath, state))
                {
                    continue;
                }
            }

            if (state.onLoadedFunc &&
                state.startedEntities.find(static_cast<unsigned long>(entity)) == state.startedEntities.end())
            {
                PyObject* result = PyObject_CallFunction(state.onLoadedFunc, "k", static_cast<unsigned long>(entity));
                if (!result)
                {
                    LogPythonError("Python: onloaded failed for " + DescribeEntity(entity) + " (script: " + component->scriptPath + ")");
                }
                else
                {
                    Py_DECREF(result);
                }
                state.startedEntities.insert(static_cast<unsigned long>(entity));
            }

            if (state.tickFunc)
            {
                PyObject* result = PyObject_CallFunction(state.tickFunc, "kf", static_cast<unsigned long>(entity), deltaSeconds);
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

        PyGILState_Release(gilState);
    }

    void HandleKeyDown(int key)
    {
        if (!Py_IsInitialized())
        {
            return;
        }
        PyGILState_STATE gilState = PyGILState_Ensure();
        InvokeKeyCallbacks(s_onKeyPressed, key);
        InvokeKeyCallbacksForKey(s_keyPressedCallbacks, key);
        PyGILState_Release(gilState);
    }

    void HandleKeyUp(int key)
    {
        if (!Py_IsInitialized())
        {
            return;
        }
        PyGILState_STATE gilState = PyGILState_Ensure();
        InvokeKeyCallbacks(s_onKeyReleased, key);
        InvokeKeyCallbacksForKey(s_keyReleasedCallbacks, key);
        PyGILState_Release(gilState);
    }

    void SetRenderer(Renderer* renderer)
    {
        s_renderer = renderer;
    }
}
