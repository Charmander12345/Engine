#pragma once

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
}
