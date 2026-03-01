#pragma once

#include "../../src/Renderer/Renderer.h"
#include "../../src/Renderer/UIManager.h"

/// Minimal renderer backend that performs no GPU work.
/// Used for automated testing of engine logic without requiring a graphics context.
class NullRenderer : public Renderer
{
public:
    NullRenderer() { m_uiManager.setRenderer(this); }
    ~NullRenderer() override = default;

    // --- Core ---
    bool initialize() override { return true; }
    void shutdown() override {}
    void clear() override {}
    void render() override {}
    void present() override {}
    const std::string& name() const override { static const std::string n = "NullRenderer"; return n; }

    SDL_Window* window() const override { return nullptr; }

    // --- Camera (no-op stubs) ---
    void moveCamera(float, float, float) override {}
    void rotateCamera(float, float) override {}
    Vec3 getCameraPosition() const override { return {}; }
    void setCameraPosition(const Vec3&) override {}
    Vec2 getCameraRotationDegrees() const override { return {}; }
    void setCameraRotationDegrees(float, float) override {}
    void setActiveCameraEntity(unsigned int) override {}
    unsigned int getActiveCameraEntity() const override { return 0; }
    void clearActiveCameraEntity() override {}
    bool screenToWorldPos(int, int, Vec3&) const override { return false; }

    // --- UIManager ---
    UIManager& getUIManager() override { return m_uiManager; }
    const UIManager& getUIManager() const override { return m_uiManager; }

private:
    UIManager m_uiManager;
};
