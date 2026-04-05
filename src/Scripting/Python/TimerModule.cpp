#include "ScriptingInternal.h"

#include <vector>
#include <algorithm>

namespace ScriptDetail
{
    struct TimerEntry
    {
        int id{ 0 };
        float remaining{ 0.0f };
        float interval{ 0.0f };
        bool repeat{ false };
        PyObject* callback{ nullptr };
    };

    static std::vector<TimerEntry> s_timers;
    static int s_nextTimerId = 1;

    void ProcessTimers(float dt)
    {
        // Process in a copy-safe manner since callbacks might add/remove timers
        size_t count = s_timers.size();
        for (size_t i = 0; i < count; ++i)
        {
            auto& timer = s_timers[i];
            timer.remaining -= dt;
            if (timer.remaining <= 0.0f)
            {
                if (timer.callback)
                {
                    PyObject* result = PyObject_CallNoArgs(timer.callback);
                    if (!result)
                    {
                        LogPythonError("Python: timer callback failed (id=" + std::to_string(timer.id) + ")");
                    }
                    else
                    {
                        Py_DECREF(result);
                    }
                }

                if (timer.repeat && timer.interval > 0.0f)
                {
                    timer.remaining = timer.interval;
                }
                else
                {
                    // Mark for removal
                    timer.remaining = -999.0f;
                }
            }
        }

        // Remove expired one-shot timers
        for (auto it = s_timers.begin(); it != s_timers.end(); )
        {
            if (it->remaining <= -999.0f)
            {
                Py_XDECREF(it->callback);
                it = s_timers.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void ClearTimers()
    {
        for (auto& timer : s_timers)
        {
            Py_XDECREF(timer.callback);
        }
        s_timers.clear();
        s_nextTimerId = 1;
    }
}

namespace
{
    // delay(seconds, callback) -> timer_id
    PyObject* py_delay(PyObject*, PyObject* args)
    {
        float seconds = 0.0f;
        PyObject* callback = nullptr;
        if (!PyArg_ParseTuple(args, "fO", &seconds, &callback))
        {
            return nullptr;
        }
        if (!PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return nullptr;
        }
        if (seconds < 0.0f) seconds = 0.0f;

        Py_INCREF(callback);
        int id = ScriptDetail::s_nextTimerId++;
        ScriptDetail::s_timers.push_back({ id, seconds, 0.0f, false, callback });
        return PyLong_FromLong(id);
    }

    // schedule(interval, callback, repeat=True) -> timer_id
    PyObject* py_schedule(PyObject*, PyObject* args, PyObject* kwargs)
    {
        float interval = 0.0f;
        PyObject* callback = nullptr;
        int repeat = 1;
        static const char* kwlist[] = { "interval", "callback", "repeat", nullptr };
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "fO|p",
            const_cast<char**>(kwlist), &interval, &callback, &repeat))
        {
            return nullptr;
        }
        if (!PyCallable_Check(callback))
        {
            PyErr_SetString(PyExc_TypeError, "callback must be callable");
            return nullptr;
        }
        if (interval <= 0.0f)
        {
            PyErr_SetString(PyExc_ValueError, "interval must be > 0");
            return nullptr;
        }

        Py_INCREF(callback);
        int id = ScriptDetail::s_nextTimerId++;
        ScriptDetail::s_timers.push_back({ id, interval, interval, repeat != 0, callback });
        return PyLong_FromLong(id);
    }

    // cancel(timer_id) -> bool
    PyObject* py_cancel(PyObject*, PyObject* args)
    {
        int timerId = 0;
        if (!PyArg_ParseTuple(args, "i", &timerId))
        {
            return nullptr;
        }
        for (auto it = ScriptDetail::s_timers.begin(); it != ScriptDetail::s_timers.end(); ++it)
        {
            if (it->id == timerId)
            {
                Py_XDECREF(it->callback);
                ScriptDetail::s_timers.erase(it);
                Py_RETURN_TRUE;
            }
        }
        Py_RETURN_FALSE;
    }

    // cancel_all() -> None
    PyObject* py_cancel_all(PyObject*, PyObject*)
    {
        ScriptDetail::ClearTimers();
        Py_RETURN_NONE;
    }

    PyMethodDef TimerMethods[] = {
        { "delay", py_delay, METH_VARARGS, "Schedule a one-shot callback after N seconds. Returns timer id." },
        { "schedule", reinterpret_cast<PyCFunction>(py_schedule), METH_VARARGS | METH_KEYWORDS,
          "Schedule a repeating callback every N seconds. Returns timer id." },
        { "cancel", py_cancel, METH_VARARGS, "Cancel a timer by id. Returns True if found." },
        { "cancel_all", py_cancel_all, METH_NOARGS, "Cancel all active timers." },
        { nullptr, nullptr, 0, nullptr }
    };

    PyModuleDef TimerModuleDef = {
        PyModuleDef_HEAD_INIT,
        "engine.timer",
        "Timer and scheduling API",
        -1,
        TimerMethods
    };
}

PyObject* CreateTimerModule()
{
    return PyModule_Create(&TimerModuleDef);
}
