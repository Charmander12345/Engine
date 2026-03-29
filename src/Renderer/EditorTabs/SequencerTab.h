#pragma once

#include "IEditorTab.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

// ===========================================================================
// SequencerTab – extracted from UIManager (Section 1.1)
// Cinematic camera-path sequencer with keyframe editing and playback.
// ===========================================================================
class SequencerTab : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        bool        isOpen{ false };
        float       refreshTimer{ 0.0f };
        // Playback
        bool        playing{ false };
        float       playbackSpeed{ 1.0f };
        float       scrubberT{ 0.0f };       // normalised 0..1
        // Editing
        int         selectedKeyframe{ -1 };   // -1 = none
        bool        showSplineInViewport{ true };
        bool        loopPlayback{ false };
        float       pathDuration{ 5.0f };     // seconds
    };

    SequencerTab(UIManager* uiManager, Renderer* renderer);
    ~SequencerTab() override = default;

    // IEditorTab interface
    void               open() override;
    void               close() override;
    bool               isOpen() const override;
    void               update(float deltaSeconds) override;
    const std::string& getTabId() const override { return m_state.tabId; }

private:
    void refresh();
    void buildToolbar(WidgetElement& root);
    void buildTimeline(WidgetElement& root);
    void buildKeyframeList(WidgetElement& root);

    UIManager* m_uiManager = nullptr;
    Renderer*  m_renderer  = nullptr;
    State      m_state;
};
