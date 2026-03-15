#pragma once

#include <string>

class Renderer;

namespace Scripting
{
    bool Initialize();
    void Shutdown();
    void UpdateScripts(float deltaSeconds);
    void ReloadScripts();
    void HandleKeyDown(int key);
    void HandleKeyUp(int key);
    void SetRenderer(Renderer* renderer);

    // Script Hot-Reload: watches Content directory for .py changes
    void InitScriptHotReload(const std::string& contentDirectory);
    void PollScriptHotReload();
    bool IsScriptHotReloadEnabled();
    void SetScriptHotReloadEnabled(bool enabled);
}
