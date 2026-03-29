#include "ScriptingInternal.h"

#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/UIManager.h"
#include "../Renderer/ViewportUIManager.h"
#include "../Renderer/UIWidget.h"
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <cmath>

namespace
{
    auto*& s_renderer = ScriptDetail::s_renderer;

    // ── engine.ui ───────────────────────────────────────────────────────

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
                        ScriptDetail::LogPythonError("Python: modal close callback failed");
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

    // ── Method table & module definition ────────────────────────────────

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

    PyModuleDef UiModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.ui",
        "UI scripting API",
        -1,
        UiMethods
    };
}

PyObject* CreateUIModule()
{
    return PyModule_Create(&UiModuleDef);
}
