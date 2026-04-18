#pragma once
#if ENGINE_EDITOR

/// @file EditorWindowManager.h
/// @brief Editor-side facade for opening / closing editor-specific tabs and popups.
///
/// Phase 1 of the Editor/Engine separation (see
/// `docs/EDITOR_ENGINE_SEPARATION_PLAN.md`).  This class lives entirely in the
/// `src/Editor/` module and is the *only* place editor code is allowed to ask
/// for editor-specific UI surfaces (Console, Profiler, Material Editor, …).
///
/// Today the implementation simply forwards to the existing `UIManager`
/// `openXYZTab()` / `isXYZOpen()` API via the `IEditorBridge`.  Subsequent
/// commits will move that lifecycle logic out of `UIManager` and into this
/// class, leaving `UIManager` with nothing but generic widget rendering.
///
/// Engine-side translation units must never include this header — it is
/// guarded by `ENGINE_EDITOR` so a runtime build cannot accidentally pull it
/// in.

#include <string>
#include <memory>

#include "../Tabs/ITabOpener.h"

class IEditorBridge;
class ActorEditorTab;
class UIManager;
class Renderer;
class ConsoleTab;
class ProfilerTab;
class NotificationsTab;
class AudioPreviewTab;
class ParticleEditorTab;
class ShaderViewerTab;
class RenderDebuggerTab;
class SequencerTab;
class LevelCompositionTab;
class AnimationEditorTab;
class EntityEditorTab;
class InputActionEditorTab;
class InputMappingEditorTab;
class UIDesignerTab;
class WidgetEditorTab;

namespace ECS { using Entity = unsigned int; }

namespace Editor
{
    class EditorWindowManager : public ITabOpener
    {
    public:
        explicit EditorWindowManager(IEditorBridge& bridge);
        ~EditorWindowManager() = default;

        EditorWindowManager(const EditorWindowManager&) = delete;
        EditorWindowManager& operator=(const EditorWindowManager&) = delete;

        // ── Console / Logs ───────────────────────────────────────────
        void openConsole() override;
        void closeConsole() override;
        bool isConsoleOpen() const override;

        // ── Profiler ─────────────────────────────────────────────────
        void openProfiler() override;
        void closeProfiler() override;
        bool isProfilerOpen() const override;

        // ── Notifications ────────────────────────────────────────────
        void openNotifications() override;
        void closeNotifications() override;
        bool isNotificationsOpen() const override;

        // ── Audio Preview ────────────────────────────────────────────
        void openAudioPreview(const std::string& assetPath) override;
        void closeAudioPreview() override;
        bool isAudioPreviewOpen() const override;

        // ── Particle Editor ──────────────────────────────────────────
        void openParticleEditor(ECS::Entity entity) override;
        void closeParticleEditor() override;
        bool isParticleEditorOpen() const override;

        // ── Shader Viewer ────────────────────────────────────────────
        void openShaderViewer() override;
        void closeShaderViewer() override;
        bool isShaderViewerOpen() const override;

        // ── Render Debugger ──────────────────────────────────────────
        void openRenderDebugger() override;
        void closeRenderDebugger() override;
        bool isRenderDebuggerOpen() const override;

        // ── Sequencer ────────────────────────────────────────────────
        void openSequencer() override;
        void closeSequencer() override;
        bool isSequencerOpen() const override;

        // ── Level Composition ────────────────────────────────────────
        void openLevelComposition() override;
        void closeLevelComposition() override;
        bool isLevelCompositionOpen() const override;

        // ── Animation Editor ─────────────────────────────────────────
        void openAnimationEditor(ECS::Entity entity) override;
        void closeAnimationEditor() override;
        bool isAnimationEditorOpen() const override;

        // ── Entity Editor ────────────────────────────────────────────
        void openEntityEditor(const std::string& assetPath) override;
        void closeEntityEditor() override;
        bool isEntityEditorOpen() const override;

        // ── Actor Editor ─────────────────────────────────────────────
        void openActorEditor(const std::string& assetPath) override;
        void closeActorEditor() override;
        bool isActorEditorOpen() const override;
        ActorEditorTab* getActorEditorTab() const override;

        // ── Input Action / Mapping Editors ───────────────────────────
        void openInputActionEditor(const std::string& assetPath) override;
        void closeInputActionEditor() override;
        bool isInputActionEditorOpen() const override;

        void openInputMappingEditor(const std::string& assetPath) override;
        void closeInputMappingEditor() override;
        bool isInputMappingEditorOpen() const override;

        // ── UI Designer ──────────────────────────────────────────────
        void openUIDesigner() override;
        void closeUIDesigner() override;
        bool isUIDesignerOpen() const override;

        // ── Tick all managed tabs ────────────────────────────────────
        void updateTabs(float deltaSeconds) override;

        // ── Popups (long-form windows, no tab counterpart) ───────────
        void openWidgetEditorPopup(const std::string& relativeAssetPath);
        void openMaterialEditorPopup(const std::string& materialAssetPath = {});
        void openLandscapeManagerPopup();
        void openEngineSettingsPopup();
        void openEditorSettingsPopup();
        void openWorkspaceToolsPopup();
        void openShortcutHelpPopup();

    private:
        IEditorBridge& m_bridge;

        // Owned simple tabs
        std::unique_ptr<ConsoleTab>            m_consoleTab;
        std::unique_ptr<ProfilerTab>           m_profilerTab;
        std::unique_ptr<NotificationsTab>      m_notificationsTab;
        std::unique_ptr<AudioPreviewTab>       m_audioPreviewTab;
        std::unique_ptr<ParticleEditorTab>     m_particleEditorTab;
        std::unique_ptr<ShaderViewerTab>       m_shaderViewerTab;
        std::unique_ptr<RenderDebuggerTab>     m_renderDebuggerTab;
        std::unique_ptr<SequencerTab>          m_sequencerTab;
        std::unique_ptr<LevelCompositionTab>   m_levelCompositionTab;
        std::unique_ptr<AnimationEditorTab>    m_animationEditorTab;
        std::unique_ptr<EntityEditorTab>        m_entityEditorTab;
        std::unique_ptr<ActorEditorTab>         m_actorEditorTab;
        std::unique_ptr<InputActionEditorTab>   m_inputActionEditorTab;
        std::unique_ptr<InputMappingEditorTab>  m_inputMappingEditorTab;
        std::unique_ptr<UIDesignerTab>          m_uiDesignerTab;
        std::unique_ptr<WidgetEditorTab>        m_widgetEditorTab;
    };
}

#endif // ENGINE_EDITOR
