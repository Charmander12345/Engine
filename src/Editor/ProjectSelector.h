#pragma once
#if ENGINE_EDITOR

/// @file ProjectSelector.h
/// @brief Encapsulates the startup project-selection flow.
///
/// Extracted from main.cpp (Phase 9 of the editor-separation plan).
/// Shows a temporary renderer window with the project-selection screen
/// and returns the user's choice.  Falls back to a SampleProject in
/// the user's Downloads folder when no project is selected.

#include <string>
#include "../Renderer/RendererFactory.h"
#include "../Diagnostics/DiagnosticsManager.h"

/// Result of the project-selection flow.
struct ProjectSelection
{
    std::string path;
    bool        isNew{ false };
    bool        setAsDefault{ false };
    bool        includeDefaultContent{ true };
    DiagnosticsManager::RHIType rhi{ DiagnosticsManager::RHIType::OpenGL };
    bool        chosen{ false };
    bool        cancelled{ false };
};

/// Runs the project-selection screen using a temporary renderer.
/// The function is blocking — it returns once the user picks a project,
/// closes the window, or the fallback kicks in.
ProjectSelection showProjectSelection(RendererBackend backend);

#endif // ENGINE_EDITOR
