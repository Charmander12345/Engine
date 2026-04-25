#pragma once
#include <string>

class ActorEditorTab;

/// Abstract interface for managing editor tabs/popups.
/// Implemented by EditorWindowManager to decouple tab classes from UIManager.
class ITabOpener
{
public:
    virtual ~ITabOpener() = default;

    // Tabs with asset/entity parameters
    virtual void openWidgetEditorPopup(const std::string& relativeAssetPath) = 0;
    virtual void openAudioPreview(const std::string& assetPath) = 0;
    virtual void openEntityEditor(const std::string& assetPath) = 0;
    virtual void openActorEditor(const std::string& assetPath) = 0;
    virtual void openInputActionEditor(const std::string& assetPath) = 0;
    virtual void openInputMappingEditor(const std::string& assetPath) = 0;
    virtual void openSkeletalMeshEditor(const std::string& assetPath) = 0;
    virtual void openParticleEditor(unsigned int entity) = 0;
    virtual void openAnimationEditor(unsigned int entity) = 0;

    // Simple tabs (no parameters)
    virtual void openConsole() = 0;
    virtual void openProfiler() = 0;
    virtual void openNotifications() = 0;
    virtual void openShaderViewer() = 0;
    virtual void openRenderDebugger() = 0;
    virtual void openSequencer() = 0;
    virtual void openLevelComposition() = 0;
    virtual void openUIDesigner() = 0;

    // Close methods
    virtual void closeConsole() = 0;
    virtual void closeProfiler() = 0;
    virtual void closeNotifications() = 0;
    virtual void closeAudioPreview() = 0;
    virtual void closeParticleEditor() = 0;
    virtual void closeShaderViewer() = 0;
    virtual void closeRenderDebugger() = 0;
    virtual void closeSequencer() = 0;
    virtual void closeLevelComposition() = 0;
    virtual void closeAnimationEditor() = 0;
    virtual void closeEntityEditor() = 0;
    virtual void closeActorEditor() = 0;
    virtual void closeInputActionEditor() = 0;
    virtual void closeInputMappingEditor() = 0;
    virtual void closeSkeletalMeshEditor() = 0;
    virtual void closeUIDesigner() = 0;

    // Query methods
    virtual bool isConsoleOpen() const = 0;
    virtual bool isProfilerOpen() const = 0;
    virtual bool isNotificationsOpen() const = 0;
    virtual bool isAudioPreviewOpen() const = 0;
    virtual bool isParticleEditorOpen() const = 0;
    virtual bool isShaderViewerOpen() const = 0;
    virtual bool isRenderDebuggerOpen() const = 0;
    virtual bool isSequencerOpen() const = 0;
    virtual bool isLevelCompositionOpen() const = 0;
    virtual bool isAnimationEditorOpen() const = 0;
    virtual bool isEntityEditorOpen() const = 0;
    virtual bool isActorEditorOpen() const = 0;
    virtual bool isInputActionEditorOpen() const = 0;
    virtual bool isInputMappingEditorOpen() const = 0;
    virtual bool isSkeletalMeshEditorOpen() const = 0;
    virtual bool isUIDesignerOpen() const = 0;

    // Typed tab accessors (for renderer level-swap etc.)
    virtual ActorEditorTab* getActorEditorTab() const = 0;
    virtual class SkeletalMeshEditorTab* getSkeletalMeshEditorTab() const = 0;

    // Tick all managed tabs
    virtual void updateTabs(float deltaSeconds) = 0;
};
