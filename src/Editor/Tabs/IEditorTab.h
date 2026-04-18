#pragma once

#include <string>

class UIManager;
class Renderer;

/// Common interface for all editor tabs extracted from UIManager.
/// Each tab owns its state and UI-building logic, delegating shared
/// operations (widget registration, toasts, click events, etc.) back
/// to UIManager through its stored pointer.
class IEditorTab
{
public:
    virtual ~IEditorTab() = default;

    /// Open the tab (create widgets, register click events, switch to tab).
    virtual void open() = 0;

    /// Close the tab (unregister widgets, remove tab, reset state).
    virtual void close() = 0;

    /// Whether the tab is currently open.
    virtual bool isOpen() const = 0;

    /// Timer-driven update called every frame from updateNotifications().
    /// Implementations should self-throttle (e.g. 0.25s / 0.5s intervals).
    virtual void update(float deltaSeconds) = 0;

    /// Unique tab identifier (e.g. "Console", "Profiler").
    virtual const std::string& getTabId() const = 0;
};
