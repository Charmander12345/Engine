#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include "../../Diagnostics/DiagnosticsManager.h"

class UIManager;
class Renderer;
class EditorWidget;

/// Manages all modal dialogs, confirm dialogs, save-progress modals,
/// unsaved-changes dialogs, level-load progress, and the project screen.
/// Extracted from UIManager (Section 1.5).
class EditorDialogs
{
public:
    EditorDialogs(UIManager* uiManager, Renderer* renderer);
    ~EditorDialogs() = default;

    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

    // ── Modal Messages ──────────────────────────────────────────────────
    void showModalMessage(const std::string& message, std::function<void()> onClosed = {});
    void closeModalMessage();
    bool isModalVisible() const { return m_modalVisible; }

    // ── Confirm Dialogs ─────────────────────────────────────────────────
#if ENGINE_EDITOR
    void showConfirmDialog(const std::string& message,
                           std::function<void()> onConfirm,
                           std::function<void()> onCancel = {},
                           const std::string& confirmLabel = "Delete",
                           const std::string& cancelLabel = "Cancel");
    void showConfirmDialogWithCheckbox(const std::string& message,
                                       const std::string& checkboxLabel,
                                       bool checkedByDefault,
                                       std::function<void(bool checked)> onConfirm,
                                       std::function<void()> onCancel = {},
                                       const std::string& confirmLabel = "Delete",
                                       const std::string& cancelLabel = "Cancel");

    // ── Save Progress Modal ─────────────────────────────────────────────
    void showSaveProgressModal(size_t total);
    void updateSaveProgress(size_t saved, size_t total);
    void closeSaveProgressModal(bool success);

    // ── Unsaved Changes Dialog ──────────────────────────────────────────
    void showUnsavedChangesDialog(std::function<void()> onDone);

    // ── Level Load Progress ─────────────────────────────────────────────
    void showLevelLoadProgress(const std::string& levelName);
    void updateLevelLoadProgress(const std::string& status);
    void closeLevelLoadProgress();

    // ── Project Screen ──────────────────────────────────────────────────
    void openProjectScreen(std::function<void(const std::string& projectPath,
                                              bool isNew,
                                              bool setAsDefault,
                                              bool includeDefaultContent,
                                              DiagnosticsManager::RHIType selectedRHI,
                                              DiagnosticsManager::ScriptingMode scriptingMode)> onProjectChosen);
#endif // ENGINE_EDITOR

    // ── Accessors (for theme application / transient widgets) ───────────
    const std::shared_ptr<EditorWidget>& getModalWidget() const { return m_modalWidget; }
    const std::shared_ptr<EditorWidget>& getSaveProgressWidget() const { return m_saveProgressWidget; }

private:
    void ensureModalWidget();

    UIManager* m_uiManager{ nullptr };
    Renderer*  m_renderer{ nullptr };

    // Modal message state
    std::shared_ptr<EditorWidget> m_modalWidget;
    std::string m_modalMessage;
    bool m_modalVisible{ false };
    std::function<void()> m_modalOnClosed;

    struct ModalRequest
    {
        std::string message;
        std::function<void()> onClosed;
    };
    std::vector<ModalRequest> m_modalQueue;

#if ENGINE_EDITOR
    // Save progress modal state
    std::shared_ptr<EditorWidget> m_saveProgressWidget;
    bool m_saveProgressVisible{ false };
    size_t m_saveProgressTotal{ 0 };
    size_t m_saveProgressSaved{ 0 };

    // Level load progress modal state
    std::shared_ptr<EditorWidget> m_levelLoadProgressWidget;
#endif // ENGINE_EDITOR
};
