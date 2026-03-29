#include "PythonScripting.h"
#include "ScriptingInternal.h"
#include "ScriptHotReload.h"

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Core/EngineLevel.h"
#include "../Core/AudioManager.h"
#include "../Logger/Logger.h"
#include "../Renderer/UIManager.h"
#include "../Renderer/ViewportUIManager.h"
#include "../Renderer/UIWidget.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <cmath>
#include "../Renderer/Renderer.h"
#include "../Renderer/ViewportUIManager.h"
#include "../Physics/PhysicsWorld.h"

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
}

namespace
{
    // Alias shared state into the anonymous namespace so existing code stays unchanged.
    auto*& s_renderer    = ScriptDetail::s_renderer;
    auto*& s_onCollision = ScriptDetail::s_onCollision;
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
        PyObject* onBeginOverlapFunc{ nullptr };
        PyObject* onEndOverlapFunc{ nullptr };
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
    float s_lastDeltaSeconds = 0.0f;
    EngineLevel* s_lastLevel{ nullptr };
    std::string s_lastLevelScriptPath;
    LevelScriptState s_levelScript;
    PyObject* s_onKeyPressed{ nullptr };
    PyObject* s_onKeyReleased{ nullptr };
    std::unordered_map<int, std::vector<PyObject*>> s_keyPressedCallbacks;
    std::unordered_map<int, std::vector<PyObject*>> s_keyReleasedCallbacks;
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
        if (s_onCollision)
        {
            Py_DECREF(s_onCollision);
            s_onCollision = nullptr;
        }
        PhysicsWorld::Instance().setCollisionCallback(nullptr);
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

        PyObject* beginOverlapFunc = PyDict_GetItemString(moduleDict, "on_entity_begin_overlap");
        if (beginOverlapFunc && PyCallable_Check(beginOverlapFunc))
        {
            Py_INCREF(beginOverlapFunc);
            state.onBeginOverlapFunc = beginOverlapFunc;
        }

        PyObject* endOverlapFunc = PyDict_GetItemString(moduleDict, "on_entity_end_overlap");
        if (endOverlapFunc && PyCallable_Check(endOverlapFunc))
        {
            Py_INCREF(endOverlapFunc);
            state.onEndOverlapFunc = endOverlapFunc;
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
        case ECS::ComponentKind::Collision:
            return ECS::addComponent<ECS::CollisionComponent>(entity);
        case ECS::ComponentKind::Script:
            return ECS::addComponent<ECS::ScriptComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::addComponent<ECS::NameComponent>(entity);
        case ECS::ComponentKind::ParticleEmitter:
            return ECS::addComponent<ECS::ParticleEmitterComponent>(entity);
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
        case ECS::ComponentKind::Collision:
            return ECS::removeComponent<ECS::CollisionComponent>(entity);
        case ECS::ComponentKind::Script:
            return ECS::removeComponent<ECS::ScriptComponent>(entity);
        case ECS::ComponentKind::Name:
            return ECS::removeComponent<ECS::NameComponent>(entity);
        case ECS::ComponentKind::ParticleEmitter:
            return ECS::removeComponent<ECS::ParticleEmitterComponent>(entity);
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
        case ECS::ComponentKind::Collision:
            schema.require<ECS::CollisionComponent>();
            return true;
        case ECS::ComponentKind::Script:
            schema.require<ECS::ScriptComponent>();
            return true;
        case ECS::ComponentKind::Name:
            schema.require<ECS::NameComponent>();
            return true;
        case ECS::ComponentKind::ParticleEmitter:
            schema.require<ECS::ParticleEmitterComponent>();
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

    PyObject* py_spawn_widget(PyObject*, PyObject* args)
    {
        const char* contentPath = nullptr;
        if (!PyArg_ParseTuple(args, "s", &contentPath))
        {
            return nullptr;
        }

        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }

        if (!contentPath || !*contentPath)
        {
            PyErr_SetString(PyExc_ValueError, "content_path must be non-empty.");
            return nullptr;
        }

        auto* vpUI = s_renderer->getViewportUIManagerPtr();
        if (!vpUI)
        {
            PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available.");
            return nullptr;
        }

        auto& assetManager = AssetManager::Instance();
        // Auto-append .asset extension if not already present
        std::string resolvedPath(contentPath);
        if (resolvedPath.size() < 6 || resolvedPath.substr(resolvedPath.size() - 6) != ".asset")
        {
            resolvedPath += ".asset";
        }
        const std::string absolutePath = assetManager.getAbsoluteContentPath(resolvedPath);
        if (absolutePath.empty())
        {
            PyErr_SetString(PyExc_FileNotFoundError, "Cannot resolve content path.");
            return nullptr;
        }

        const int assetId = assetManager.loadAsset(absolutePath, AssetType::Widget, AssetManager::Sync);
        if (assetId == 0)
        {
            Py_RETURN_NONE;
        }

        auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
        if (!asset)
        {
            Py_RETURN_NONE;
        }

        auto widget = s_renderer->createWidgetFromAsset(asset);
        if (!widget)
        {
            Py_RETURN_NONE;
        }

        // Generate a unique name for the spawned widget
        static int s_spawnCounter = 0;
        const std::string widgetName = "_spawned_" + std::to_string(++s_spawnCounter);
        widget->setName(widgetName);
        if (!vpUI->createWidget(widgetName, 0))
        {
            Py_RETURN_NONE;
        }
        // Copy elements from loaded widget into the created widget's canvas
        if (auto* destWidget = vpUI->getWidget(widgetName))
        {
            destWidget->setElements(widget->getElements());
            vpUI->markLayoutDirty();
        }

        return PyUnicode_FromString(widgetName.c_str());
    }

    PyObject* py_remove_widget(PyObject*, PyObject* args)
    {
        const char* widgetId = nullptr;
        if (!PyArg_ParseTuple(args, "s", &widgetId))
        {
            return nullptr;
        }

        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }

        if (!widgetId || !*widgetId)
        {
            PyErr_SetString(PyExc_ValueError, "widget_id must be non-empty.");
            return nullptr;
        }

        auto* vpUI = s_renderer->getViewportUIManagerPtr();
        if (!vpUI)
        {
            Py_RETURN_FALSE;
        }

        if (vpUI->removeWidget(widgetId))
        {
            Py_RETURN_TRUE;
        }
        Py_RETURN_FALSE;
    }

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
                    LogPythonError("Python: create_audio_from_asset_async callback failed");
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

    // ---- Particle emitter API ----

    PyObject* py_set_emitter(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        const char* key = nullptr;
        float value = 0.0f;
        if (!PyArg_ParseTuple(args, "Isf", &entity, &key, &value))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        std::string k(key);
        if      (k == "emissionRate")  c->emissionRate  = value;
        else if (k == "lifetime")      c->lifetime      = value;
        else if (k == "speed")         c->speed         = value;
        else if (k == "speedVariance") c->speedVariance = value;
        else if (k == "size")          c->size          = value;
        else if (k == "sizeEnd")       c->sizeEnd       = value;
        else if (k == "gravity")       c->gravity       = value;
        else if (k == "coneAngle")     c->coneAngle     = value;
        else if (k == "maxParticles")  c->maxParticles  = static_cast<int>(value);
        else if (k == "colorR")        c->colorR        = value;
        else if (k == "colorG")        c->colorG        = value;
        else if (k == "colorB")        c->colorB        = value;
        else if (k == "colorA")        c->colorA        = value;
        else if (k == "colorEndR")     c->colorEndR     = value;
        else if (k == "colorEndG")     c->colorEndG     = value;
        else if (k == "colorEndB")     c->colorEndB     = value;
        else if (k == "colorEndA")     c->colorEndA     = value;
        else { PyErr_Format(PyExc_KeyError, "Unknown emitter key: %s", key); return nullptr; }
        Py_RETURN_TRUE;
    }

    PyObject* py_set_emitter_enabled(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        int enabled = 1;
        if (!PyArg_ParseTuple(args, "I|p", &entity, &enabled))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        c->enabled = (enabled != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_set_emitter_color(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        float r = 1, g = 1, b = 1, a = 1;
        if (!PyArg_ParseTuple(args, "Iffff", &entity, &r, &g, &b, &a))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        c->colorR = r; c->colorG = g; c->colorB = b; c->colorA = a;
        Py_RETURN_TRUE;
    }

    PyObject* py_set_emitter_end_color(PyObject*, PyObject* args)
    {
        unsigned int entity = 0;
        float r = 1, g = 0, b = 0, a = 0;
        if (!PyArg_ParseTuple(args, "Iffff", &entity, &r, &g, &b, &a))
            return nullptr;
        auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
        if (!c) { PyErr_SetString(PyExc_RuntimeError, "Entity has no ParticleEmitter."); return nullptr; }
        c->colorEndR = r; c->colorEndG = g; c->colorEndB = b; c->colorEndA = a;
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

    PyObject* py_get_light_color(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        if (!PyArg_ParseTuple(args, "k", &entity))
        {
            return nullptr;
        }
        const auto* comp = ECS::ECSManager::Instance().getComponent<ECS::LightComponent>(static_cast<ECS::Entity>(entity));
        if (!comp)
        {
            Py_RETURN_NONE;
        }
        return Py_BuildValue("(fff)", comp->color[0], comp->color[1], comp->color[2]);
    }

    PyObject* py_set_light_color(PyObject*, PyObject* args)
    {
        unsigned long entity = 0;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        if (!PyArg_ParseTuple(args, "kfff", &entity, &r, &g, &b))
        {
            return nullptr;
        }
        auto* comp = ECS::ECSManager::Instance().getComponent<ECS::LightComponent>(static_cast<ECS::Entity>(entity));
        if (!comp)
        {
            PyErr_SetString(PyExc_ValueError, "Entity has no LightComponent.");
            return nullptr;
        }
        comp->color[0] = r;
        comp->color[1] = g;
        comp->color[2] = b;
        Py_RETURN_TRUE;
    }

    PyObject* py_get_delta_time(PyObject*, PyObject*)
    {
        return PyFloat_FromDouble(static_cast<double>(s_lastDeltaSeconds));
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

    // ── Hardware diagnostics ────────────────────────────────────────────────

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

    PyMethodDef EntityMethods[] = {
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
        { "set_mesh", py_set_mesh, METH_VARARGS, "Set mesh asset for an entity." },
        { "get_mesh", py_get_mesh, METH_VARARGS, "Get mesh asset path for an entity." },
        { "get_light_color", py_get_light_color, METH_VARARGS, "Get light color (entity) -> (r, g, b)." },
        { "set_light_color", py_set_light_color, METH_VARARGS, "Set light color (entity, r, g, b)." },
        { nullptr, nullptr, 0, nullptr }
    };

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

    // (engine.viewport_ui module removed — runtime widgets are spawned via engine.ui.spawn_widget)

    PyObject* py_ui_show_cursor(PyObject*, PyObject* args)
    {
        int visible = 1;
        if (!PyArg_ParseTuple(args, "i", &visible)) return nullptr;
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }
        vp->setGameplayCursorVisible(visible != 0);
        if (visible)
        {
            SDL_ShowCursor();
            if (auto* w = s_renderer->window())
            {
                SDL_SetWindowRelativeMouseMode(w, false);
                SDL_SetWindowMouseGrab(w, false);
            }
        }
        else
        {
            SDL_HideCursor();
            if (auto* w = s_renderer->window())
            {
                SDL_SetWindowRelativeMouseMode(w, true);
                SDL_SetWindowMouseGrab(w, true);
            }
        }
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_clear_all_widgets(PyObject*, PyObject*)
    {
        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }
        vp->clearAllWidgets();
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_play_animation(PyObject*, PyObject* args)
    {
        const char* widgetId = nullptr;
        const char* animationName = nullptr;
        int fromStart = 1;
        if (!PyArg_ParseTuple(args, "ss|p", &widgetId, &animationName, &fromStart))
        {
            return nullptr;
        }

        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }

        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp)
        {
            PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available.");
            return nullptr;
        }

        Widget* widget = vp->getWidget(widgetId ? widgetId : "");
        if (!widget)
        {
            Py_RETURN_FALSE;
        }

        widget->animationPlayer().play(animationName ? animationName : "", fromStart != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_stop_animation(PyObject*, PyObject* args)
    {
        const char* widgetId = nullptr;
        const char* animationName = nullptr;
        if (!PyArg_ParseTuple(args, "ss", &widgetId, &animationName))
        {
            return nullptr;
        }

        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }

        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp)
        {
            PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available.");
            return nullptr;
        }

        Widget* widget = vp->getWidget(widgetId ? widgetId : "");
        if (!widget)
        {
            Py_RETURN_FALSE;
        }

        auto& player = widget->animationPlayer();
        if (!player.getCurrentAnimation().empty() && player.getCurrentAnimation() != (animationName ? animationName : ""))
        {
            Py_RETURN_FALSE;
        }

        player.stop();
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_set_animation_speed(PyObject*, PyObject* args)
    {
        const char* widgetId = nullptr;
        const char* animationName = nullptr;
        float speed = 1.0f;
        if (!PyArg_ParseTuple(args, "ssf", &widgetId, &animationName, &speed))
        {
            return nullptr;
        }

        if (!s_renderer)
        {
            PyErr_SetString(PyExc_RuntimeError, "Renderer not available.");
            return nullptr;
        }

        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp)
        {
            PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available.");
            return nullptr;
        }

        Widget* widget = vp->getWidget(widgetId ? widgetId : "");
        if (!widget)
        {
            Py_RETURN_FALSE;
        }

        bool updated = false;
        for (auto& animation : widget->getAnimationsMutable())
        {
            if (animation.name == (animationName ? animationName : ""))
            {
                animation.playbackSpeed = std::max(0.0f, speed);
                updated = true;
                break;
            }
        }

        if (!updated)
        {
            Py_RETURN_FALSE;
        }

        Py_RETURN_TRUE;
    }

    // ── Focus API ────────────────────────────────────────────────────────

    PyObject* py_ui_set_focus(PyObject*, PyObject* args)
    {
        const char* elementId = nullptr;
        if (!PyArg_ParseTuple(args, "s", &elementId))
            return nullptr;

        if (!s_renderer) { PyErr_SetString(PyExc_RuntimeError, "Renderer not available."); return nullptr; }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }

        vp->setFocus(elementId ? elementId : "");
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_clear_focus(PyObject*, PyObject*)
    {
        if (!s_renderer) { PyErr_SetString(PyExc_RuntimeError, "Renderer not available."); return nullptr; }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }

        vp->clearFocus();
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_get_focused_element(PyObject*, PyObject*)
    {
        if (!s_renderer) { PyErr_SetString(PyExc_RuntimeError, "Renderer not available."); return nullptr; }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }

        const std::string& id = vp->getFocusedElementId();
        if (id.empty())
            Py_RETURN_NONE;
        return PyUnicode_FromString(id.c_str());
    }

    PyObject* py_ui_set_focusable(PyObject*, PyObject* args)
    {
        const char* elementId = nullptr;
        int focusable = 1;
        if (!PyArg_ParseTuple(args, "s|p", &elementId, &focusable))
            return nullptr;

        if (!s_renderer) { PyErr_SetString(PyExc_RuntimeError, "Renderer not available."); return nullptr; }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }

        vp->setFocusable(elementId ? elementId : "", focusable != 0);
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_set_draggable(PyObject*, PyObject* args)
    {
        const char* elementId = nullptr;
        int enabled = 1;
        const char* payload = "";
        if (!PyArg_ParseTuple(args, "s|ps", &elementId, &enabled, &payload))
            return nullptr;

        if (!s_renderer) { PyErr_SetString(PyExc_RuntimeError, "Renderer not available."); return nullptr; }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }

        auto* elem = vp->findElementById(elementId ? elementId : "");
        if (!elem) { Py_RETURN_FALSE; }

        elem->isDraggable = (enabled != 0);
        elem->dragPayload = payload ? payload : "";
        Py_RETURN_TRUE;
    }

    PyObject* py_ui_set_drop_target(PyObject*, PyObject* args)
    {
        const char* elementId = nullptr;
        int enabled = 1;
        if (!PyArg_ParseTuple(args, "s|p", &elementId, &enabled))
            return nullptr;

        if (!s_renderer) { PyErr_SetString(PyExc_RuntimeError, "Renderer not available."); return nullptr; }
        auto* vp = s_renderer->getViewportUIManagerPtr();
        if (!vp) { PyErr_SetString(PyExc_RuntimeError, "ViewportUIManager not available."); return nullptr; }

        auto* elem = vp->findElementById(elementId ? elementId : "");
        if (!elem) { Py_RETURN_FALSE; }

        elem->acceptsDrop = (enabled != 0);
        Py_RETURN_TRUE;
    }

    PyMethodDef UiMethods[] = {
        { "show_modal_message", py_show_modal_message, METH_VARARGS, "Show a blocking modal message." },
        { "close_modal_message", py_close_modal_message, METH_NOARGS, "Close the active modal message." },
        { "show_toast_message", py_show_toast_message, METH_VARARGS, "Show a toast message." },
        { "spawn_widget", py_spawn_widget, METH_VARARGS, "Spawn a viewport widget from a content-relative path; returns widget id string." },
        { "remove_widget", py_remove_widget, METH_VARARGS, "Remove a viewport widget by its id." },
        { "play_animation", py_ui_play_animation, METH_VARARGS, "Play a widget animation by name." },
        { "stop_animation", py_ui_stop_animation, METH_VARARGS, "Stop a widget animation by name." },
        { "set_animation_speed", py_ui_set_animation_speed, METH_VARARGS, "Set playback speed for a widget animation by name." },
        { "show_cursor", py_ui_show_cursor, METH_VARARGS, "Show/hide gameplay cursor (blocks camera when visible)." },
        { "clear_all_widgets", py_ui_clear_all_widgets, METH_NOARGS, "Remove all spawned viewport widgets." },
        { "set_focus", py_ui_set_focus, METH_VARARGS, "Set focus to a viewport UI element by id." },
        { "clear_focus", py_ui_clear_focus, METH_NOARGS, "Clear focus from the currently focused viewport UI element." },
        { "get_focused_element", py_ui_get_focused_element, METH_NOARGS, "Get the id of the currently focused viewport UI element, or None." },
        { "set_focusable", py_ui_set_focusable, METH_VARARGS, "Set whether a viewport UI element is focusable (element_id, focusable=True)." },
        { "set_draggable", py_ui_set_draggable, METH_VARARGS, "Set element as draggable (element_id, enabled=True, payload='')." },
        { "set_drop_target", py_ui_set_drop_target, METH_VARARGS, "Set element as a drop target (element_id, enabled=True)." },
        { nullptr, nullptr, 0, nullptr }
    };

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

    PyMethodDef ParticleMethods[] = {
        { "set_emitter", py_set_emitter, METH_VARARGS, "Set a particle emitter property by key (entity, key, value)." },
        { "set_enabled", py_set_emitter_enabled, METH_VARARGS, "Enable/disable a particle emitter (entity, enabled)." },
        { "set_color", py_set_emitter_color, METH_VARARGS, "Set emitter start color (entity, r, g, b, a)." },
        { "set_end_color", py_set_emitter_end_color, METH_VARARGS, "Set emitter end color (entity, r, g, b, a)." },
        { nullptr, nullptr, 0, nullptr }
    };

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

    PyMethodDef LoggingMethods[] = {
        { "log", py_log, METH_VARARGS, "Log a message (level: 0=info,1=warn,2=error)." },
        { nullptr, nullptr, 0, nullptr }
    };

    // ── engine.editor ───────────────────────────────────────────────────

#if ENGINE_EDITOR
    // Stored Python callbacks for plugin menu items and custom tabs
    struct InternalPluginMenuItem
    {
        std::string menu;
        std::string name;
        PyObject* callback{ nullptr };
    };
    std::vector<InternalPluginMenuItem> s_pluginMenuItems;

    struct InternalPluginTab
    {
        std::string name;
        PyObject* onBuildUI{ nullptr };
    };
    std::vector<InternalPluginTab> s_pluginTabs;

    // Plugin file tracking for hot-reload
    ScriptHotReload s_pluginHotReload;
    std::vector<std::string> s_loadedPluginPaths;

    PyObject* py_editor_show_toast(PyObject*, PyObject* args)
    {
        const char* message = nullptr;
        int level = 0; // 0=Info, 1=Success, 2=Warning, 3=Error
        if (!PyArg_ParseTuple(args, "s|i", &message, &level))
            return nullptr;

        if (!s_renderer)
            Py_RETURN_NONE;

        auto notifLevel = DiagnosticsManager::NotificationLevel::Info;
        switch (level)
        {
        case 1: notifLevel = DiagnosticsManager::NotificationLevel::Success; break;
        case 2: notifLevel = DiagnosticsManager::NotificationLevel::Warning; break;
        case 3: notifLevel = DiagnosticsManager::NotificationLevel::Error;   break;
        default: break;
        }

        s_renderer->getUIManager().showToastMessage(message, UIManager::kToastMedium, notifLevel);
        Py_RETURN_NONE;
    }

    PyObject* py_editor_get_selected_entities(PyObject*, PyObject*)
    {
        if (!s_renderer)
            return PyList_New(0);

        const auto& selected = s_renderer->getSelectedEntities();
        PyObject* list = PyList_New(static_cast<Py_ssize_t>(selected.size()));
        Py_ssize_t idx = 0;
        for (unsigned int e : selected)
        {
            PyList_SET_ITEM(list, idx++, PyLong_FromUnsignedLong(e));
        }
        return list;
    }

    PyObject* py_editor_get_asset_list(PyObject*, PyObject* args)
    {
        int typeFilter = -1; // -1 = all
        if (!PyArg_ParseTuple(args, "|i", &typeFilter))
            return nullptr;

        const auto& registry = AssetManager::Instance().getAssetRegistry();
        PyObject* list = PyList_New(0);
        for (const auto& entry : registry)
        {
            if (typeFilter >= 0 && static_cast<int>(entry.type) != typeFilter)
                continue;
            PyObject* dict = PyDict_New();
            PyDict_SetItemString(dict, "name", PyUnicode_FromString(entry.name.c_str()));
            PyDict_SetItemString(dict, "path", PyUnicode_FromString(entry.path.c_str()));
            PyDict_SetItemString(dict, "type", PyLong_FromLong(static_cast<long>(entry.type)));
            PyList_Append(list, dict);
            Py_DECREF(dict);
        }
        return list;
    }

    PyObject* py_editor_create_entity(PyObject*, PyObject* args)
    {
        const char* name = "New Entity";
        if (!PyArg_ParseTuple(args, "|s", &name))
            return nullptr;

        const ECS::Entity entity = ECS::createEntity();
        ECS::addComponent<ECS::NameComponent>(entity);
        auto* nameComp = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(entity);
        if (nameComp)
            nameComp->displayName = name;

        // Also add a transform so the entity is visible
        ECS::addComponent<ECS::TransformComponent>(entity);

        return PyLong_FromUnsignedLong(entity);
    }

    PyObject* py_editor_select_entity(PyObject*, PyObject* args)
    {
        unsigned long entityId = 0;
        if (!PyArg_ParseTuple(args, "k", &entityId))
            return nullptr;

        if (!s_renderer)
            Py_RETURN_NONE;

        s_renderer->getUIManager().selectEntity(static_cast<unsigned int>(entityId));
        Py_RETURN_NONE;
    }

    PyObject* py_editor_add_menu_item(PyObject*, PyObject* args)
    {
        const char* menu = nullptr;
        const char* name = nullptr;
        PyObject* callback = nullptr;
        if (!PyArg_ParseTuple(args, "ssO", &menu, &name, &callback))
            return nullptr;

        if (!PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return nullptr;
        }

        Py_INCREF(callback);
        s_pluginMenuItems.push_back({ menu, name, callback });
        Py_RETURN_NONE;
    }

    PyObject* py_editor_get_menu_items(PyObject*, PyObject*)
    {
        PyObject* list = PyList_New(0);
        for (const auto& item : s_pluginMenuItems)
        {
            PyObject* dict = PyDict_New();
            PyDict_SetItemString(dict, "menu", PyUnicode_FromString(item.menu.c_str()));
            PyDict_SetItemString(dict, "name", PyUnicode_FromString(item.name.c_str()));
            PyList_Append(list, dict);
            Py_DECREF(dict);
        }
        return list;
    }

    PyObject* py_editor_register_tab(PyObject*, PyObject* args)
    {
        const char* name = nullptr;
        PyObject* onBuildUI = nullptr;
        if (!PyArg_ParseTuple(args, "sO", &name, &onBuildUI))
            return nullptr;

        if (!PyCallable_Check(onBuildUI))
        {
            PyErr_SetString(PyExc_TypeError, "on_build_ui must be callable");
            return nullptr;
        }

        Py_INCREF(onBuildUI);
        s_pluginTabs.push_back({ name, onBuildUI });
        Py_RETURN_NONE;
    }

    PyMethodDef EditorMethods[] = {
        { "show_toast",            py_editor_show_toast,            METH_VARARGS, "Show a toast notification (message, level=0). Levels: 0=Info, 1=Success, 2=Warning, 3=Error." },
        { "get_selected_entities", py_editor_get_selected_entities, METH_NOARGS,  "Get list of currently selected entity IDs." },
        { "get_asset_list",        py_editor_get_asset_list,        METH_VARARGS, "Get asset registry entries. Optional type filter (int)." },
        { "create_entity",         py_editor_create_entity,         METH_VARARGS, "Create a named entity with Transform. Optional name string." },
        { "select_entity",         py_editor_select_entity,         METH_VARARGS, "Select an entity by ID in the editor." },
        { "add_menu_item",         py_editor_add_menu_item,         METH_VARARGS, "Add a custom menu item (menu, name, callback)." },
        { "register_tab",          py_editor_register_tab,          METH_VARARGS, "Register a custom editor tab (name, on_build_ui_callback)." },
        { "get_menu_items",        py_editor_get_menu_items,        METH_NOARGS,  "Get all registered plugin menu items." },
        { nullptr, nullptr, 0, nullptr }
    };
#endif

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

    PyModuleDef EntityModule = {
        PyModuleDef_HEAD_INIT,
        "engine.entity",
        "Entity scripting API",
        -1,
        EntityMethods
    };

    PyModuleDef AssetModule = {
        PyModuleDef_HEAD_INIT,
        "engine.assetmanagement",
        "Asset management scripting API",
        -1,
        AssetMethods
    };

    PyModuleDef AudioModule = {
        PyModuleDef_HEAD_INIT,
        "engine.audio",
        "Audio scripting API",
        -1,
        AudioMethods
    };

    PyModuleDef InputModule = {
        PyModuleDef_HEAD_INIT,
        "engine.input",
        "Input scripting API",
        -1,
        InputMethods
    };

    PyModuleDef UiModule = {
        PyModuleDef_HEAD_INIT,
        "engine.ui",
        "UI scripting API",
        -1,
        UiMethods
    };

    PyModuleDef CameraModule = {
        PyModuleDef_HEAD_INIT,
        "engine.camera",
        "Camera scripting API",
        -1,
        CameraMethods
    };

    PyModuleDef DiagnosticsModule = {
        PyModuleDef_HEAD_INIT,
        "engine.diagnostics",
        "Diagnostics scripting API",
        -1,
        DiagnosticsMethods
    };

    PyModuleDef LoggingModule = {
        PyModuleDef_HEAD_INIT,
        "engine.logging",
        "Logging scripting API",
        -1,
        LoggingMethods
    };

    PyModuleDef ParticleModule = {
        PyModuleDef_HEAD_INIT,
        "engine.particle",
        "Particle emitter scripting API",
        -1,
        ParticleMethods
    };

#if ENGINE_EDITOR
    PyModuleDef EditorModule = {
        PyModuleDef_HEAD_INIT,
        "engine.editor",
        "Editor plugin scripting API",
        -1,
        EditorMethods
    };
#endif

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

        PyModule_AddIntConstant(module, "Log_Info", 0);
        PyModule_AddIntConstant(module, "Log_Warning", 1);
        PyModule_AddIntConstant(module, "Log_Error", 2);

        PyObject* entityModule = PyModule_Create(&EntityModule);
        PyObject* assetModule = PyModule_Create(&AssetModule);
        PyObject* audioModule = PyModule_Create(&AudioModule);
        PyObject* inputModule = PyModule_Create(&InputModule);
        PyObject* uiModule = PyModule_Create(&UiModule);
        PyObject* cameraModule = PyModule_Create(&CameraModule);
        PyObject* diagnosticsModule = PyModule_Create(&DiagnosticsModule);
        PyObject* loggingModule = PyModule_Create(&LoggingModule);
        PyObject* physicsModule = CreatePhysicsModule();
        PyObject* mathModule = CreateMathModule();
        PyObject* particleModule = PyModule_Create(&ParticleModule);
#if ENGINE_EDITOR
        PyObject* editorModule = PyModule_Create(&EditorModule);
#endif

        if (!entityModule || !assetModule || !audioModule || !inputModule || !uiModule || !cameraModule || !diagnosticsModule || !loggingModule || !physicsModule || !mathModule || !particleModule
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

        PopulateKeyConstants(inputModule);

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
            !AddSubmodule(module, particleModule, "particle")
#if ENGINE_EDITOR
            || !AddSubmodule(module, editorModule, "editor")
#endif
            )
        {
            Py_DECREF(module);
            return nullptr;
        }

#if ENGINE_EDITOR
        // Toast-level constants on the editor submodule
        PyModule_AddIntConstant(editorModule, "TOAST_INFO",    0);
        PyModule_AddIntConstant(editorModule, "TOAST_SUCCESS", 1);
        PyModule_AddIntConstant(editorModule, "TOAST_WARNING", 2);
        PyModule_AddIntConstant(editorModule, "TOAST_ERROR",   3);
#endif

        return module;
    }

    PyMODINIT_FUNC PyInit_engine()
    {
        return CreateEngineModule();
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
        for (auto& [path, state] : s_scripts)
        {
            ReleaseScriptState(state);
        }
        s_scripts.clear();
        ReleaseLevelScriptState();
        ClearKeyCallbacks();
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
                if (state.loadFailed)
                    continue;
                if (!LoadScriptModule(component->scriptPath, state))
                {
                    state.loadFailed = true;
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

        // Dispatch overlap events from physics to entity scripts
        if (diagnostics.isPIEActive())
        {
            auto& physics = PhysicsWorld::Instance();

            auto dispatchOverlap = [&](ECS::Entity entity, ECS::Entity otherEntity, bool isBegin)
            {
                const auto* sc = ECS::ECSManager::Instance().getComponent<ECS::ScriptComponent>(entity);
                if (!sc || sc->scriptPath.empty()) return;
                auto it = s_scripts.find(sc->scriptPath);
                if (it == s_scripts.end() || !it->second.module) return;
                PyObject* func = isBegin ? it->second.onBeginOverlapFunc : it->second.onEndOverlapFunc;
                if (!func) return;
                PyObject* result = PyObject_CallFunction(func, "kk",
                    static_cast<unsigned long>(entity),
                    static_cast<unsigned long>(otherEntity));
                if (!result)
                {
                    LogPythonError(std::string("Python: ") +
                        (isBegin ? "on_entity_begin_overlap" : "on_entity_end_overlap") +
                        " failed for " + DescribeEntity(entity));
                }
                else
                {
                    Py_DECREF(result);
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

    #if ENGINE_EDITOR
    // ── Editor Plugin Discovery & Hot-Reload ────────────────────────────

    static void ExecutePluginFile(const std::string& absPath)
    {
        if (!Py_IsInitialized())
            return;

        std::ifstream file(absPath);
        if (!file.is_open())
            return;

        std::string code((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        file.close();

        PyGILState_STATE gilState = PyGILState_Ensure();

        PyObject* globals = PyDict_New();
        PyObject* builtins = PyEval_GetBuiltins();
        Py_INCREF(builtins);
        PyDict_SetItemString(globals, "__builtins__", builtins);
        Py_DECREF(builtins);

        // Set __file__ so the plugin knows its own path
        PyObject* pyPath = PyUnicode_FromString(absPath.c_str());
        PyDict_SetItemString(globals, "__file__", pyPath);
        Py_DECREF(pyPath);

        PyObject* result = PyRun_String(code.c_str(), Py_file_input, globals, globals);
        if (!result)
        {
            PyErr_Print();
            Logger::Instance().log(Logger::Category::Scripting,
                "Plugin load failed: " + absPath, Logger::LogLevel::ERROR);
        }
        else
        {
            Py_DECREF(result);
            Logger::Instance().log(Logger::Category::Scripting,
                "Plugin loaded: " + absPath, Logger::LogLevel::INFO);
        }

        Py_DECREF(globals);
        PyGILState_Release(gilState);
    }

    void LoadEditorPlugins(const std::string& projectRoot)
    {
        if (!Py_IsInitialized() || projectRoot.empty())
            return;

        namespace fs = std::filesystem;
        fs::path pluginDir = fs::path(projectRoot) / "Editor" / "Plugins";
        if (!fs::exists(pluginDir) || !fs::is_directory(pluginDir))
            return;

        // Clear previously registered plugin items and tabs
        for (auto& item : s_pluginMenuItems)
        {
            Py_XDECREF(item.callback);
        }
        s_pluginMenuItems.clear();

        for (auto& tab : s_pluginTabs)
        {
            Py_XDECREF(tab.onBuildUI);
        }
        s_pluginTabs.clear();
        s_loadedPluginPaths.clear();

        for (const auto& entry : fs::directory_iterator(pluginDir))
        {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".py")
                continue;

            std::string absPath = entry.path().generic_string();
            ExecutePluginFile(absPath);
            s_loadedPluginPaths.push_back(absPath);
        }

        // Initialize plugin hot-reload watcher
        s_pluginHotReload.init(pluginDir.generic_string(), 0.5);

        if (!s_loadedPluginPaths.empty())
        {
            auto& diag = DiagnosticsManager::Instance();
            diag.enqueueToastNotification(
                "Plugins loaded (" + std::to_string(s_loadedPluginPaths.size()) + ")",
                3.0f, DiagnosticsManager::NotificationLevel::Success);
        }
    }

    void PollPluginHotReload()
    {
        if (!s_pluginHotReload.isInitialized())
            return;

        auto changed = s_pluginHotReload.poll();
        if (changed.empty())
            return;

        auto& diag = DiagnosticsManager::Instance();

        // Reload all plugins (simple approach: re-run all plugin files)
        for (auto& item : s_pluginMenuItems)
        {
            Py_XDECREF(item.callback);
        }
        s_pluginMenuItems.clear();

        for (auto& tab : s_pluginTabs)
        {
            Py_XDECREF(tab.onBuildUI);
        }
        s_pluginTabs.clear();

        for (const auto& path : s_loadedPluginPaths)
        {
            ExecutePluginFile(path);
        }

        diag.enqueueToastNotification(
            "Plugins reloaded (" + std::to_string(changed.size()) + " changed)",
            3.0f, DiagnosticsManager::NotificationLevel::Success);
    }

    const std::vector<Scripting::PluginMenuItem>& GetPluginMenuItems()
    {
        static std::vector<Scripting::PluginMenuItem> result;
        result.clear();
        result.reserve(s_pluginMenuItems.size());
        for (const auto& item : s_pluginMenuItems)
            result.push_back({ item.menu, item.name });
        return result;
    }

    const std::vector<Scripting::PluginTab>& GetPluginTabs()
    {
        static std::vector<Scripting::PluginTab> result;
        result.clear();
        result.reserve(s_pluginTabs.size());
        for (const auto& tab : s_pluginTabs)
            result.push_back({ tab.name });
        return result;
    }

    void InvokePluginMenuCallback(size_t index)
    {
        if (index >= s_pluginMenuItems.size() || !s_pluginMenuItems[index].callback)
            return;
        if (!Py_IsInitialized())
            return;

        PyGILState_STATE gilState = PyGILState_Ensure();
        PyObject* result = PyObject_CallNoArgs(s_pluginMenuItems[index].callback);
        if (!result)
            PyErr_Print();
        else
            Py_DECREF(result);
        PyGILState_Release(gilState);
    }
#endif
}
