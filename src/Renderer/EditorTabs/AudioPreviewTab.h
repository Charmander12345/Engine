#pragma once

#include "IEditorTab.h"
#include <string>

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab: Audio Preview / Playback.
/// Extracted from UIManager.cpp – owns the AudioPreviewState and all
/// open/close/refresh/build logic that was previously inline.
class AudioPreviewTab final : public IEditorTab
{
public:
    struct State
    {
        std::string  tabId;
        std::string  widgetId;
        std::string  assetPath;       // Content-relative path of the open audio asset
        bool         isOpen{ false };
        bool         isPlaying{ false };
        unsigned int playHandle{ 0 }; // AudioManager source handle
        float        volume{ 1.0f };
        // Metadata extracted from asset JSON
        int          channels{ 0 };
        int          sampleRate{ 0 };
        int          format{ 0 };
        size_t       dataBytes{ 0 };
        float        durationSeconds{ 0.0f };
        std::string  displayName;
    };

    explicit AudioPreviewTab(UIManager* uiManager, Renderer* renderer);

    // IEditorTab
    void open() override;
    void close() override;
    bool isOpen() const override { return m_state.isOpen; }
    void update(float /*deltaSeconds*/) override {}   // no periodic refresh needed
    const std::string& getTabId() const override { return m_state.tabId; }

    /// Open with a specific asset path.
    void open(const std::string& assetPath);

    /// Rebuild the widget content.
    void refresh();

    /// Read-only access to the state.
    const State& getState() const { return m_state; }

private:
    void buildToolbar(WidgetElement& root);
    void buildWaveform(WidgetElement& root);
    void buildMetadata(WidgetElement& root);

    UIManager* m_ui{ nullptr };
    Renderer*  m_renderer{ nullptr };
    State      m_state;
};
