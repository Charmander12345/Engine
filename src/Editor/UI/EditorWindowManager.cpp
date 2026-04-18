#if ENGINE_EDITOR

/// @file EditorWindowManager.cpp
/// @brief See header.  Phase-1 implementation: thin facade delegating to the
/// existing UIManager open/close API via IEditorBridge.

#include "EditorWindowManager.h"
#include "../../Core/IEditorBridge.h"
#include "../../Renderer/UIManager.h"
#include "../Tabs/ConsoleTab.h"
#include "../Tabs/ProfilerTab.h"
#include "../Tabs/NotificationsTab.h"
#include "../Tabs/AudioPreviewTab.h"
#include "../Tabs/ParticleEditorTab.h"
#include "../Tabs/ShaderViewerTab.h"
#include "../Tabs/RenderDebuggerTab.h"
#include "../Tabs/SequencerTab.h"
#include "../Tabs/LevelCompositionTab.h"
#include "../Tabs/AnimationEditorTab.h"
#include "../Tabs/EntityEditorTab.h"
#include "../Tabs/ActorEditorTab.h"
#include "../Tabs/InputActionEditorTab.h"
#include "../Tabs/InputMappingEditorTab.h"
#include "../Tabs/UIDesignerTab.h"
#include "../Tabs/WidgetEditorTab.h"

namespace Editor
{
    EditorWindowManager::EditorWindowManager(IEditorBridge& bridge)
        : m_bridge(bridge)
    {
    }

    // Helper to get UIManager/Renderer for tab construction
    static UIManager* uiMgr(IEditorBridge& b) { return &b.getUIManager(); }
    static Renderer*  ren(IEditorBridge& b)    { return b.getUIManager().getRenderer(); }

    // ── Console ─────────────────────────────────────────────────────
    void EditorWindowManager::openConsole()
    {
        if (!m_consoleTab)
            m_consoleTab = std::make_unique<ConsoleTab>(uiMgr(m_bridge), ren(m_bridge));
        m_consoleTab->open();
    }
    void EditorWindowManager::closeConsole()       { if (m_consoleTab) m_consoleTab->close(); }
    bool EditorWindowManager::isConsoleOpen() const { return m_consoleTab && m_consoleTab->isOpen(); }

    // ── Profiler ────────────────────────────────────────────────────
    void EditorWindowManager::openProfiler()
    {
        if (!m_profilerTab)
            m_profilerTab = std::make_unique<ProfilerTab>(uiMgr(m_bridge), ren(m_bridge));
        m_profilerTab->open();
    }
    void EditorWindowManager::closeProfiler()       { if (m_profilerTab) m_profilerTab->close(); }
    bool EditorWindowManager::isProfilerOpen() const { return m_profilerTab && m_profilerTab->isOpen(); }

    // ── Notifications ───────────────────────────────────────────────
    void EditorWindowManager::openNotifications()
    {
        if (!m_notificationsTab)
            m_notificationsTab = std::make_unique<NotificationsTab>(uiMgr(m_bridge), ren(m_bridge));
        m_notificationsTab->open();
    }
    void EditorWindowManager::closeNotifications()       { if (m_notificationsTab) m_notificationsTab->close(); }
    bool EditorWindowManager::isNotificationsOpen() const { return m_notificationsTab && m_notificationsTab->isOpen(); }

    // ── Audio Preview ───────────────────────────────────────────────
    void EditorWindowManager::openAudioPreview(const std::string& assetPath)
    {
        if (!m_audioPreviewTab)
            m_audioPreviewTab = std::make_unique<AudioPreviewTab>(uiMgr(m_bridge), ren(m_bridge));
        m_audioPreviewTab->open(assetPath);
    }
    void EditorWindowManager::closeAudioPreview()       { if (m_audioPreviewTab) m_audioPreviewTab->close(); }
    bool EditorWindowManager::isAudioPreviewOpen() const { return m_audioPreviewTab && m_audioPreviewTab->isOpen(); }

    // ── Particle Editor ─────────────────────────────────────────────
    void EditorWindowManager::openParticleEditor(ECS::Entity entity)
    {
        if (!m_particleEditorTab)
            m_particleEditorTab = std::make_unique<ParticleEditorTab>(uiMgr(m_bridge), ren(m_bridge));
        m_particleEditorTab->open(entity);
    }
    void EditorWindowManager::closeParticleEditor()       { if (m_particleEditorTab) m_particleEditorTab->close(); }
    bool EditorWindowManager::isParticleEditorOpen() const { return m_particleEditorTab && m_particleEditorTab->isOpen(); }

    // ── Shader Viewer ───────────────────────────────────────────────
    void EditorWindowManager::openShaderViewer()
    {
        if (!m_shaderViewerTab)
            m_shaderViewerTab = std::make_unique<ShaderViewerTab>(uiMgr(m_bridge), ren(m_bridge));
        m_shaderViewerTab->open();
    }
    void EditorWindowManager::closeShaderViewer()       { if (m_shaderViewerTab) m_shaderViewerTab->close(); }
    bool EditorWindowManager::isShaderViewerOpen() const { return m_shaderViewerTab && m_shaderViewerTab->isOpen(); }

    // ── Render Debugger ─────────────────────────────────────────────
    void EditorWindowManager::openRenderDebugger()
    {
        if (!m_renderDebuggerTab)
            m_renderDebuggerTab = std::make_unique<RenderDebuggerTab>(uiMgr(m_bridge), ren(m_bridge));
        m_renderDebuggerTab->open();
    }
    void EditorWindowManager::closeRenderDebugger()       { if (m_renderDebuggerTab) m_renderDebuggerTab->close(); }
    bool EditorWindowManager::isRenderDebuggerOpen() const { return m_renderDebuggerTab && m_renderDebuggerTab->isOpen(); }

    // ── Sequencer ───────────────────────────────────────────────────
    void EditorWindowManager::openSequencer()
    {
        if (!m_sequencerTab)
            m_sequencerTab = std::make_unique<SequencerTab>(uiMgr(m_bridge), ren(m_bridge));
        m_sequencerTab->open();
    }
    void EditorWindowManager::closeSequencer()       { if (m_sequencerTab) m_sequencerTab->close(); }
    bool EditorWindowManager::isSequencerOpen() const { return m_sequencerTab && m_sequencerTab->isOpen(); }

    // ── Level Composition ───────────────────────────────────────────
    void EditorWindowManager::openLevelComposition()
    {
        if (!m_levelCompositionTab)
            m_levelCompositionTab = std::make_unique<LevelCompositionTab>(uiMgr(m_bridge), ren(m_bridge));
        m_levelCompositionTab->open();
    }
    void EditorWindowManager::closeLevelComposition()       { if (m_levelCompositionTab) m_levelCompositionTab->close(); }
    bool EditorWindowManager::isLevelCompositionOpen() const { return m_levelCompositionTab && m_levelCompositionTab->isOpen(); }

    // ── Animation Editor ────────────────────────────────────────────
    void EditorWindowManager::openAnimationEditor(ECS::Entity entity)
    {
        if (!m_animationEditorTab)
            m_animationEditorTab = std::make_unique<AnimationEditorTab>(uiMgr(m_bridge), ren(m_bridge));
        m_animationEditorTab->open(entity);
    }
    void EditorWindowManager::closeAnimationEditor()       { if (m_animationEditorTab) m_animationEditorTab->close(); }
    bool EditorWindowManager::isAnimationEditorOpen() const { return m_animationEditorTab && m_animationEditorTab->isOpen(); }

    // ── Tick all managed tabs ───────────────────────────────────────
    void EditorWindowManager::updateTabs(float deltaSeconds)
    {
        if (m_consoleTab)          m_consoleTab->update(deltaSeconds);
        if (m_profilerTab)         m_profilerTab->update(deltaSeconds);
        if (m_notificationsTab)    m_notificationsTab->update(deltaSeconds);
        if (m_particleEditorTab)   m_particleEditorTab->update(deltaSeconds);
        if (m_renderDebuggerTab)   m_renderDebuggerTab->update(deltaSeconds);
        if (m_sequencerTab)        m_sequencerTab->update(deltaSeconds);
        if (m_entityEditorTab)     m_entityEditorTab->update(deltaSeconds);
        if (m_actorEditorTab)      m_actorEditorTab->update(deltaSeconds);
        if (m_inputActionEditorTab)  m_inputActionEditorTab->update(deltaSeconds);
        if (m_inputMappingEditorTab) m_inputMappingEditorTab->update(deltaSeconds);
        if (m_uiDesignerTab)       m_uiDesignerTab->update(deltaSeconds);
    }

    // ── Entity Editor ───────────────────────────────────────────────
    void EditorWindowManager::openEntityEditor(const std::string& assetPath)
    {
        if (!m_entityEditorTab)
            m_entityEditorTab = std::make_unique<EntityEditorTab>(uiMgr(m_bridge), ren(m_bridge));
        m_entityEditorTab->open(assetPath);
    }
    void EditorWindowManager::closeEntityEditor()       { if (m_entityEditorTab) m_entityEditorTab->close(); }
    bool EditorWindowManager::isEntityEditorOpen() const { return m_entityEditorTab && m_entityEditorTab->isOpen(); }

    // ── Actor Editor ────────────────────────────────────────────────
    void EditorWindowManager::openActorEditor(const std::string& assetPath)
    {
        if (!m_actorEditorTab)
            m_actorEditorTab = std::make_unique<ActorEditorTab>(uiMgr(m_bridge), ren(m_bridge));
        m_actorEditorTab->open(assetPath);
    }
    void EditorWindowManager::closeActorEditor()       { if (m_actorEditorTab) m_actorEditorTab->close(); }
    bool EditorWindowManager::isActorEditorOpen() const { return m_actorEditorTab && m_actorEditorTab->isOpen(); }
    ActorEditorTab* EditorWindowManager::getActorEditorTab() const { return m_actorEditorTab.get(); }

    // ── Input Action / Mapping Editors ──────────────────────────────
    void EditorWindowManager::openInputActionEditor(const std::string& assetPath)
    {
        if (!m_inputActionEditorTab)
            m_inputActionEditorTab = std::make_unique<InputActionEditorTab>(uiMgr(m_bridge), ren(m_bridge));
        m_inputActionEditorTab->open(assetPath);
    }
    void EditorWindowManager::closeInputActionEditor()       { if (m_inputActionEditorTab) m_inputActionEditorTab->close(); }
    bool EditorWindowManager::isInputActionEditorOpen() const { return m_inputActionEditorTab && m_inputActionEditorTab->isOpen(); }

    void EditorWindowManager::openInputMappingEditor(const std::string& assetPath)
    {
        if (!m_inputMappingEditorTab)
            m_inputMappingEditorTab = std::make_unique<InputMappingEditorTab>(uiMgr(m_bridge), ren(m_bridge));
        m_inputMappingEditorTab->open(assetPath);
    }
    void EditorWindowManager::closeInputMappingEditor()       { if (m_inputMappingEditorTab) m_inputMappingEditorTab->close(); }
    bool EditorWindowManager::isInputMappingEditorOpen() const { return m_inputMappingEditorTab && m_inputMappingEditorTab->isOpen(); }

    // ── UI Designer ─────────────────────────────────────────────────
    void EditorWindowManager::openUIDesigner()
    {
        if (!m_uiDesignerTab)
            m_uiDesignerTab = std::make_unique<UIDesignerTab>(uiMgr(m_bridge), ren(m_bridge));
        m_uiDesignerTab->open();
    }
    void EditorWindowManager::closeUIDesigner()       { if (m_uiDesignerTab) m_uiDesignerTab->close(); }
    bool EditorWindowManager::isUIDesignerOpen() const { return m_uiDesignerTab && m_uiDesignerTab->isOpen(); }

    // ── Popups ──────────────────────────────────────────────────────
    void EditorWindowManager::openWidgetEditorPopup(const std::string& relativeAssetPath)   { m_bridge.getUIManager().openWidgetEditorPopup(relativeAssetPath); }
    void EditorWindowManager::openMaterialEditorPopup(const std::string& materialAssetPath) { m_bridge.getUIManager().openMaterialEditorPopup(materialAssetPath); }
    void EditorWindowManager::openLandscapeManagerPopup() { m_bridge.getUIManager().openLandscapeManagerPopup(); }
    void EditorWindowManager::openEngineSettingsPopup()   { m_bridge.getUIManager().openEngineSettingsPopup(); }
    void EditorWindowManager::openEditorSettingsPopup()   { m_bridge.getUIManager().openEditorSettingsPopup(); }
    void EditorWindowManager::openWorkspaceToolsPopup()   { m_bridge.getUIManager().openWorkspaceToolsPopup(); }
    void EditorWindowManager::openShortcutHelpPopup()     { m_bridge.getUIManager().openShortcutHelpPopup(); }
}

#endif // ENGINE_EDITOR
