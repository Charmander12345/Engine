#pragma once
#if ENGINE_EDITOR

#include "Renderer/UIManager.h"

class Renderer;

/// Editor-only: Executes the full game build pipeline (CMake configure/build,
/// asset cooking, HPK packaging, deployment) on a background thread.
/// Extracted from the monolithic main.cpp lambda to keep main() manageable.
class BuildPipeline
{
public:
    /// Kick off a build.  Called on the main thread from the UIManager
    /// "Build Game" callback.  Spawns a worker thread internally.
    /// @param config   Build configuration chosen by the user.
    /// @param uiMgr    UIManager – used for progress/output and thread state.
    /// @param renderer Renderer – used to sync current render settings before building.
    static void execute(const UIManager::BuildGameConfig& config,
                        UIManager& uiMgr,
                        Renderer* renderer);
};

#endif // ENGINE_EDITOR
