#pragma once

#include "IEditorTab.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

namespace ECS { using Entity = unsigned int; }

/// Editor tab: Particle Editor.
/// Extracted from UIManager.cpp – owns the ParticleEditorState and all
/// open/close/refresh/build/preset logic that was previously inline.
class ParticleEditorTab final : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        ECS::Entity linkedEntity{ 0 };
        bool        isOpen{ false };
        float       refreshTimer{ 0.0f };
        int         presetIndex{ -1 };    // currently selected preset (-1 = custom)
    };

    explicit ParticleEditorTab(UIManager* uiManager, Renderer* renderer);

    // IEditorTab
    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Open for a specific entity.
    void open(ECS::Entity entity);

    /// Rebuild the parameter area.
    void refresh();

    /// Apply a named preset to the linked entity.
    void applyPreset(int presetIndex);

    /// Read-only access to the state.
    const State& getState() const { return m_state; }

private:
    void buildToolbar(WidgetElement& root);

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
