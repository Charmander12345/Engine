#pragma once
#if ENGINE_EDITOR

#include <string>

class UIManager;
class Renderer;

/// Popup window that lets the user view and edit all project-wide settings:
/// General, Display, Window, Build Profiles, Packaging and Scripting.
/// Opened via EditorWindowManager::openProjectSettingsPopup().
class ProjectSettingsWindow
{
public:
    /// Build and show the popup.  The window is owned by the renderer's
    /// PopupWindow list; this function just fills its UIManager with widgets.
    static void open(Renderer* renderer);
};

#endif // ENGINE_EDITOR
