#if ENGINE_EDITOR

#include "ScriptingInternal.h"
#include "PythonScripting.h"
#include "ScriptHotReload.h"

#include "../Core/ECS/ECS.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/UIManager.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <string>

namespace
{
    auto*& s_renderer = ScriptDetail::s_renderer;

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

    // ── engine.editor functions ─────────────────────────────────────────

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

    // ── Method table & module definition ────────────────────────────────

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

    PyModuleDef EditorModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.editor",
        "Editor plugin scripting API",
        -1,
        EditorMethods
    };
}

PyObject* CreateEditorModule()
{
    PyObject* module = PyModule_Create(&EditorModuleDef);
    if (module)
    {
        PyModule_AddIntConstant(module, "TOAST_INFO",    0);
        PyModule_AddIntConstant(module, "TOAST_SUCCESS", 1);
        PyModule_AddIntConstant(module, "TOAST_WARNING", 2);
        PyModule_AddIntConstant(module, "TOAST_ERROR",   3);
    }
    return module;
}

// ── Editor Plugin Discovery & Hot-Reload ────────────────────────────

namespace
{
    ScriptHotReload s_pluginHotReload;
    std::vector<std::string> s_loadedPluginPaths;

    void ExecutePluginFile(const std::string& absPath)
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
}

namespace Scripting
{
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
}

#endif // ENGINE_EDITOR
