#pragma once

#include <string>
#include <vector>

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

#if ENGINE_EDITOR
    // Editor Plugin System (Phase 11.3)
    void LoadEditorPlugins(const std::string& projectRoot);
    void PollPluginHotReload();

    struct PluginMenuItem
    {
        std::string menu;
        std::string name;
    };
    struct PluginTab
    {
        std::string name;
    };
    const std::vector<PluginMenuItem>& GetPluginMenuItems();
    const std::vector<PluginTab>& GetPluginTabs();
    void InvokePluginMenuCallback(size_t index);
#endif
}
