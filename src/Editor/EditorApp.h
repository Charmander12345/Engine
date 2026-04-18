#pragma once
#if ENGINE_EDITOR

/// @file EditorApp.h
/// @brief Main editor lifecycle class.
///
/// EditorApp receives an IEditorBridge reference (the engine-exposed API) and
/// manages all editor-specific setup: widget loading, click-event registration,
/// shortcut registration, drag-and-drop handling, PIE control, context menus,
/// and the build pipeline.
///
/// main.cpp creates an EditorApp when ENGINE_EDITOR=1, calls initialize()
/// after the renderer is ready, routes SDL events through processEvent(),
/// calls tick() each frame, and shutdown() at exit.
///
/// The goal: main.cpp shrinks to a thin engine loop with only a handful of
/// #if ENGINE_EDITOR guards (instantiate EditorApp + 4 call sites).

#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <SDL3/SDL.h>
#include "../Core/MathTypes.h"

class IEditorBridge;
class Renderer;
class UIManager;
namespace Editor { class EditorWindowManager; }

class EditorApp
{
public:
    explicit EditorApp(IEditorBridge& bridge);
    ~EditorApp();

    /// Called once after renderer + asset system are ready.
    /// Sets up all editor widgets, click events, shortcuts, drag-and-drop
    /// handlers, and the build pipeline.
    void initialize();

    /// Route an SDL event through the editor.
    /// Returns true if the editor consumed the event (caller should skip
    /// further processing).
    bool processEvent(const SDL_Event& event);

    /// Called once per frame after event processing, before render.
    /// Handles per-frame editor updates (build thread polling, toolchain
    /// detection, notification updates, etc.).
    void tick(float dt);

    /// Called during shutdown to save editor state (camera, shortcuts,
    /// project config, etc.).
    void shutdown();

    // ── PIE state (exposed so main.cpp game loop can read them) ─────
    bool isPIEMouseCaptured() const { return m_pieMouseCaptured; }
    bool isPIEInputPaused()   const { return m_pieInputPaused; }

    /// Stop PIE from outside (e.g. runtime-mode cleanup).
    void stopPIE();

    // ── Grid snap state ─────────────────────────────────────────────
    bool isGridSnapEnabled() const { return m_gridSnapEnabled; }

    // ── Camera speed multiplier (shared with main loop for metrics) ──
    float getCameraSpeedMultiplier() const { return m_cameraSpeedMultiplier; }
    void  setCameraSpeedMultiplier(float v) { m_cameraSpeedMultiplier = v; }

    // ── Metrics display state ───────────────────────────────────────
    bool isShowMetrics()        const { return m_showMetrics; }
    bool isShowOcclusionStats() const { return m_showOcclusionStats; }
    void setShowMetrics(bool v)        { m_showMetrics = v; }
    void setShowOcclusionStats(bool v) { m_showOcclusionStats = v; }

    // Pre-capture mouse position (needed by main loop for camera release)
    float getPreCaptureMouseX() const { return m_preCaptureMouseX; }
    float getPreCaptureMouseY() const { return m_preCaptureMouseY; }
    void  setPreCaptureMousePos(float x, float y) { m_preCaptureMouseX = x; m_preCaptureMouseY = y; }

    // Mark PIE mouse/input state (needed by main loop)
    void setPIEMouseCaptured(bool v) { m_pieMouseCaptured = v; }
    void setPIEInputPaused(bool v)   { m_pieInputPaused = v; }

    // Right-mouse state (synced from main loop, used by gizmo shortcuts)
    bool isRightMouseDown() const { return m_rightMouseDown; }
    void setRightMouseDown(bool v) { m_rightMouseDown = v; }

    /// Handle right-click context menu on the content browser grid.
    /// Returns true if a context menu was shown.
    bool handleContentBrowserContextMenu(const Vec2& mousePos);

    /// Generate VS Code IntelliSense config (.vscode/c_cpp_properties.json)
    /// for C++ native scripting in the given project.
    void generateVSCodeConfig(const std::string& projectPath);

    /// Editor-side facade for opening / closing editor-specific tabs and
    /// popups.  Available after `initialize()` has been called.
    Editor::EditorWindowManager* getWindowManager() const { return m_windowManager.get(); }

private:
    void registerWidgets();
    void registerClickEvents();
    void registerShortcuts();
    void registerDragDropHandlers();
    void registerBuildPipeline();
    void startPIE();
    void finishStartPIE();
    void buildGameScriptsForPIE();
    void pollPIEBuild();
    void dismissPIEBuildPopup();
    bool handleDelete();

    IEditorBridge& m_bridge;

    // PIE
    bool  m_pieMouseCaptured{ false };
    bool  m_pieInputPaused{ false };
    float m_preCaptureMouseX{ 0.0f };
    float m_preCaptureMouseY{ 0.0f };

    // Editor state
    bool  m_gridSnapEnabled{ false };
    float m_cameraSpeedMultiplier{ 1.0f };
    bool  m_showMetrics{ true };
    bool  m_showOcclusionStats{ false };
    bool  m_rightMouseDown{ false };

    // Preloaded texture IDs
    unsigned int m_playTexId{ 0 };
    unsigned int m_stopTexId{ 0 };

    // ── Async PIE build state ────────────────────────────────────────
    std::thread              m_pieBuildThread;
    std::mutex               m_pieBuildMutex;
    std::atomic<bool>        m_pieBuildRunning{ false };
    std::vector<std::string> m_pieBuildPendingLines;
    bool                     m_pieBuildFinished{ false };
    bool                     m_pieBuildSuccess{ false };
    std::vector<std::string> m_pieBuildOutputLines;
    std::string              m_pieBuildFingerprint;
    std::shared_ptr<class EditorWidget> m_pieBuildWidget;
    class PopupWindow*       m_pieBuildPopup{ nullptr };

    // ── Editor UI facade ─────────────────────────────────────────────
    std::unique_ptr<Editor::EditorWindowManager> m_windowManager;
};

#endif // ENGINE_EDITOR
