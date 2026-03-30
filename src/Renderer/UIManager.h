#pragma once

#include <string>
#include <vector>
#include <deque>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <optional>
#include <mutex>
#include <thread>
#include <atomic>

#include "../Core/MathTypes.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Core/ECS/ECS.h"
#include "UIWidget.h"
#include "EditorUI/EditorWidget.h"

class EngineLevel;
class Renderer;
#if ENGINE_EDITOR
class PopupWindow;
class ConsoleTab;
class ProfilerTab;
class AudioPreviewTab;
class ParticleEditorTab;
class ShaderViewerTab;
class RenderDebuggerTab;
class SequencerTab;
class LevelCompositionTab;
class AnimationEditorTab;
class UIDesignerTab;
class WidgetEditorTab;
class ContentBrowserPanel;
class OutlinerPanel;
#include "EditorTabs/BuildSystemUI.h"
#include "EditorTabs/EditorDialogs.h"
#include "EditorTabs/OutlinerPanel.h"
#endif
class ViewportUIManager;

class UIManager
{
#if ENGINE_EDITOR
	friend class ContentBrowserPanel;
	friend class OutlinerPanel;
	friend class EditorDialogs;
	friend class BuildSystemUI;
#endif
public:
	struct UIEntry
	{
		std::string id;
	};

	struct WidgetEntry
	{
		std::string id;
		std::shared_ptr<EditorWidget> widget;
		uint64_t runtimeId{ 0 };
		std::string tabId;  // empty = global (always visible), non-empty = only visible when this tab is active
	};

    UIManager();
    ~UIManager();

	void setRenderer(Renderer* renderer)
	{
		m_renderer = renderer;
#if ENGINE_EDITOR
		if (m_buildSystemUI) m_buildSystemUI->setRenderer(renderer);
		if (m_editorDialogs) m_editorDialogs->setRenderer(renderer);
		if (m_outlinerPanel) m_outlinerPanel->setRenderer(renderer);
#endif
	}
	Renderer* getRenderer() const { return m_renderer; }

    Vec2 getAvailableViewportSize() const;
    void setAvailableViewportSize(const Vec2& size);

    void registerUI(const std::string& id);
    const std::vector<UIEntry>& getRegisteredUI() const;

	void registerWidget(const std::string& id, const std::shared_ptr<EditorWidget>& widget);
	void registerWidget(const std::string& id, const std::shared_ptr<EditorWidget>& widget, const std::string& tabId);
	// Transition overload: accept gameplay Widget and convert to EditorWidget internally
	void registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget);
	void registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget, const std::string& tabId);
	const std::vector<WidgetEntry>& getRegisteredWidgets() const;
	std::vector<WidgetEntry>& getRegisteredWidgetsMutable();
	const std::vector<const WidgetEntry*>& getWidgetsOrderedByZ() const;
	void unregisterWidget(const std::string& id);
	WidgetElement* findElementById(const std::string& elementId);

    void setActiveTabId(const std::string& tabId);
    const std::string& getActiveTabId() const;

    void updateLayouts(const std::function<Vec2(const std::string&, float)>& measureText);
    bool needsLayoutUpdate() const;
    Vec4 getViewportContentRect() const { return m_viewportContentRect; }
    bool handleMouseDown(const Vec2& screenPos, int button);
    bool handleMouseUp(const Vec2& screenPos, int button);
    bool handleScroll(const Vec2& screenPos, float delta);
    bool handleRightMouseDown(const Vec2& screenPos);
	bool handleRightMouseUp(const Vec2& screenPos);
	void handleMouseMotion(const Vec2& screenPos);
    bool handleTextInput(const std::string& text);
	bool handleKeyDown(int key);

	#if ENGINE_EDITOR
	// Key capture: when set, handleKeyDown forwards key+mods to this callback first.
	// If callback returns true, the event is consumed. Used for shortcut rebinding.
	using KeyCaptureCallback = std::function<bool(uint32_t sdlKey, uint16_t sdlMod)>;
	void setKeyCaptureCallback(KeyCaptureCallback cb) { m_keyCaptureCallback = std::move(cb); }
	void clearKeyCaptureCallback() { m_keyCaptureCallback = nullptr; }
#endif
    void setMousePosition(const Vec2& screenPos);
    const Vec2& getMousePosition() const { return m_mousePosition; }
	bool isPointerOverUI(const Vec2& screenPos) const;
	bool hasClickEvent(const std::string& eventId) const;
	void registerClickEvent(const std::string& eventId, std::function<void()> callback);
	void markAllWidgetsDirty();
	#if ENGINE_EDITOR
	void rebuildAllEditorUI();
	void rebuildEditorUIForDpi(float newDpi);
	void applyThemeToAllEditorWidgets();

	bool isUIRenderingPaused() const { return m_uiRenderingPaused; }
#endif

	bool isRenderDirty() const;
	void clearRenderDirty();
	void markRenderDirty() { m_renderDirty = true; }
	void clearLastHoveredElement() { m_lastHoveredElement = nullptr; }

    void showModalMessage(const std::string& message, std::function<void()> onClosed = {});
    void closeModalMessage();
	#if ENGINE_EDITOR
	void showConfirmDialog(const std::string& message, std::function<void()> onConfirm, std::function<void()> onCancel = {});
	void showConfirmDialogWithCheckbox(const std::string& message, const std::string& checkboxLabel, bool checkedByDefault,
		std::function<void(bool checked)> onConfirm, std::function<void()> onCancel = {});
#endif
	// Notification levels for priority-based styling (alias from DiagnosticsManager)
	using NotificationLevel = DiagnosticsManager::NotificationLevel;

	// Standard toast durations (seconds)
	static constexpr float kToastShort  = 1.5f;  // brief confirmations (e.g. "Copied", "Keyframe removed")
	static constexpr float kToastMedium = 3.0f;  // normal info/success messages
	static constexpr float kToastLong   = 5.0f;  // warnings/errors that need attention

	void showToastMessage(const std::string& message, float durationSeconds);
	void showToastMessage(const std::string& message, float durationSeconds, NotificationLevel level);
	void updateNotifications(float deltaSeconds);

	#if ENGINE_EDITOR
	// Notification history
	struct NotificationHistoryEntry
	{
		std::string message;
		NotificationLevel level{ NotificationLevel::Info };
		uint64_t timestampMs{ 0 };  // SDL_GetTicks() at time of notification
	};
	const std::deque<NotificationHistoryEntry>& getNotificationHistory() const { return m_notificationHistory; }
	size_t getUnreadNotificationCount() const { return m_unreadNotificationCount; }
	void clearUnreadNotifications() { m_unreadNotificationCount = 0; }
	void openNotificationHistoryPopup();
	void refreshNotificationBadge();

	struct DropdownMenuItem
	{
		std::string label;
		std::function<void()> onClick;
		bool isSeparator{ false };
	};
	void showDropdownMenu(const Vec2& anchorPixels, const std::vector<DropdownMenuItem>& items, float minWidth = 0.0f);
	void closeDropdownMenu();
	bool isDropdownMenuOpen() const { return m_dropdownVisible; }

	void openLandscapeManagerPopup();
	void openEngineSettingsPopup();
	void openEditorSettingsPopup();
	void openShortcutHelpPopup();
	void openAssetReferencesPopup(const std::string& title, const std::string& assetPath,
		const std::vector<std::pair<std::string, std::string>>& items);

	// Non-blocking progress bars in StatusBar
	struct ProgressBarHandle { uint64_t id{ 0 }; };
	ProgressBarHandle beginProgress(const std::string& label, float total = 1.0f);
	void updateProgress(ProgressBarHandle handle, float current, const std::string& label = {});
	void endProgress(ProgressBarHandle handle);
	void openWidgetEditorPopup(const std::string& relativeAssetPath);
	void openMaterialEditorPopup(const std::string& materialAssetPath = {});
	void openUIDesignerTab();
	void closeUIDesignerTab();
	bool isUIDesignerOpen() const;

	// Console / Log-Viewer tab
	void openConsoleTab();
	void closeConsoleTab();
	bool isConsoleOpen() const;

	// Profiler / Performance-Monitor tab
	void openProfilerTab();
	void closeProfilerTab();
	bool isProfilerOpen() const;

	// Audio Preview tab
	void openAudioPreviewTab(const std::string& assetPath);
	void closeAudioPreviewTab();
	bool isAudioPreviewOpen() const;

	// Particle Editor tab
	void openParticleEditorTab(ECS::Entity entity);
	void closeParticleEditorTab();
	bool isParticleEditorOpen() const;

	// Shader Viewer tab
	void openShaderViewerTab();
	void closeShaderViewerTab();
	bool isShaderViewerOpen() const;

	// Render-Pass-Debugger tab
	void openRenderDebuggerTab();
	void closeRenderDebuggerTab();
	bool isRenderDebuggerOpen() const;

	// Cinematic Sequencer tab (Phase 11.2)
	void openSequencerTab();
	void closeSequencerTab();
	bool isSequencerOpen() const;

	// Level Composition tab (Phase 11.4)
	void openLevelCompositionTab();
	void closeLevelCompositionTab();
	bool isLevelCompositionOpen() const;
	void refreshLevelCompositionPanel();

	// Animation Editor tab (Phase 2.4)
	void openAnimationEditorTab(ECS::Entity entity);
	void closeAnimationEditorTab();
	bool isAnimationEditorOpen() const;

	// Entity clipboard (Copy/Paste/Duplicate)
	void copySelectedEntity();
	bool pasteEntity();
	bool duplicateSelectedEntity();
	bool hasEntityClipboard() const;

	// Auto-Collider: compute and apply a fitted CollisionComponent from mesh AABB
	bool autoFitColliderForEntity(ECS::Entity entity);

	// Surface-Snap: raycast selected entities downward and place them on the hit surface.
	// The caller provides a raycast function (origin xyz, direction xyz, maxDist) → (hit, hitY).
	using RaycastDownFn = std::function<std::pair<bool, float>(float ox, float oy, float oz)>;
	void dropSelectedEntitiesToSurface(const RaycastDownFn& raycastDown);

	// Compute mesh AABB min-Y offset (distance from pivot to bottom) for an entity.
	// Returns 0 if no mesh data is available.
	float computeEntityBottomOffset(ECS::Entity entity) const;

	// Scene templates for new levels
	enum class SceneTemplate { Empty, BasicOutdoor, Prototype };
	void createNewLevelWithTemplate(SceneTemplate tmpl, const std::string& levelName = "NewLevel", const std::string& relFolder = "Levels");

	// Prefab / Entity Templates
	bool savePrefabFromEntity(ECS::Entity entity, const std::string& name, const std::string& folder);
	bool spawnPrefabAtPosition(const std::string& prefabRelPath, const Vec3& pos);
	bool spawnBuiltinTemplate(const std::string& templateName, const Vec3& pos);
    void openProjectScreen(std::function<void(const std::string& projectPath, bool isNew, bool setAsDefault, bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI)> onProjectChosen);

	// Returns true if the active tab is a widget editor and had a selected element to delete
	bool tryDeleteWidgetEditorElement();

	void applyPendingThemeUpdate();
#endif // ENGINE_EDITOR

	static UIManager* GetActiveInstance();
	static void SetActiveInstance(UIManager* instance);

	WidgetEntry* findWidgetEntry(const std::string& id);
	const WidgetEntry* findWidgetEntry(const std::string& id) const;
private:
	WidgetElement* hitTest(const Vec2& screenPos, bool logDetails = false) const;

	#if ENGINE_EDITOR
	KeyCaptureCallback m_keyCaptureCallback;
#endif
	void updateHoverStates();
	void updateHoverTransitions(float deltaSeconds);
	void updateHoverTransitionsRecursive(WidgetElement& element, float deltaSeconds);
	void updateScrollbarVisibility(float deltaSeconds);
	void updateScrollbarVisibilityRecursive(WidgetElement& element, float deltaSeconds);
	void setFocusedEntry(WidgetElement* element);
	void collectFocusableEntries(WidgetElement& element, std::vector<WidgetElement*>& out);
	void cycleFocusedEntry(bool reverse);
	#if ENGINE_EDITOR
#endif
	std::shared_ptr<EditorWidget> createToastWidget(const std::string& message, const std::string& name, NotificationLevel level = NotificationLevel::Info) const;
	void updateToastStackLayout();

    Renderer* m_renderer{ nullptr };
    Vec2 m_availableViewportSize{};
    Vec4 m_viewportContentRect{};
    Vec2 m_mousePosition{};
    bool m_hasMousePosition{ false };
    std::unordered_map<std::string, std::function<void()>> m_clickEvents;
    std::vector<UIEntry> m_entries;
    std::vector<WidgetEntry> m_widgets;
    uint64_t m_nextWidgetRuntimeId{ 1 };
    mutable std::vector<const WidgetEntry*> m_widgetOrderCache;
    mutable bool m_widgetOrderDirty{ true };
    mutable Vec2 m_lastPointerQueryPos{};
    mutable bool m_hasPointerQueryPos{ false };
    mutable bool m_lastPointerOverUI{ false };
    mutable bool m_pointerCacheDirty{ true };
	WidgetElement* m_focusedEntry{ nullptr };
	bool m_renderDirty{ true };
	bool m_themeDirty{ false };
	bool m_uiRenderingPaused{ false };
	std::string m_activeTabId{ "Viewport" };

	// Reusable scratch vectors for updateLayouts (avoid heap allocation every layout pass)
	std::vector<WidgetEntry*> m_layoutOrderedScratch;
	std::vector<WidgetEntry*> m_layoutTopScratch;
	std::vector<WidgetEntry*> m_layoutBottomScratch;
	std::vector<WidgetEntry*> m_layoutLeftScratch;
	std::vector<WidgetEntry*> m_layoutRightScratch;
	std::vector<WidgetEntry*> m_layoutOtherScratch;

	float m_notificationPollTimer{ 0.0f };
	struct ToastNotification
	{
		std::string id;
		std::shared_ptr<EditorWidget> widget;
		float timer{ 0.0f };
		float duration{ 0.0f };
		NotificationLevel level{ NotificationLevel::Info };
	};
	std::vector<ToastNotification> m_toasts;
	uint64_t m_nextToastId{ 1 };

#if ENGINE_EDITOR
	// Notification history (circular buffer of last 50)
	static constexpr size_t kMaxNotificationHistory = 50;
	std::deque<NotificationHistoryEntry> m_notificationHistory;
	size_t m_unreadNotificationCount{ 0 };

	// Non-blocking progress bars
	struct ProgressEntry
	{
		uint64_t id{ 0 };
		std::string label;
		float current{ 0.0f };
		float total{ 1.0f };
	};
	std::vector<ProgressEntry> m_progressBars;
	uint64_t m_nextProgressId{ 1 };

	// Dropdown menu state
	std::shared_ptr<EditorWidget> m_dropdownWidget;
	bool m_dropdownVisible{ false };
	std::string m_dropdownSourceId;  // id of the DropdownButton that opened the menu
#endif // ENGINE_EDITOR

	void bindClickEventsForWidget(const std::shared_ptr<EditorWidget>& widget);
	void bindClickEventsForElement(WidgetElement& element);

#if ENGINE_EDITOR
	// World Outliner + Entity Details (extracted to EditorTabs/OutlinerPanel)
	std::unique_ptr<OutlinerPanel> m_outlinerPanel;
	size_t m_levelChangedCallbackToken{ 0 };
	void populateOutlinerWidget(const std::shared_ptr<EditorWidget>& widget);
	void populateOutlinerDetails(unsigned int entity);
	void navigateOutlinerByArrow(int direction);

	// Content Browser (extracted to EditorTabs/ContentBrowserPanel)
	std::unique_ptr<ContentBrowserPanel> m_contentBrowserPanel;
#endif // ENGINE_EDITOR

	// Double-click detection
	std::string m_lastClickedElementId;
	uint64_t m_lastClickTimeMs{ 0 };

	// Hover tracking (avoids full tree walk per mouse move)
	WidgetElement* m_lastHoveredElement{ nullptr };

	// Tooltip state
	float m_tooltipTimer{ 0.0f };         // seconds hovering on the same element
	std::string m_tooltipText;            // text to show (empty = hidden)
	Vec2 m_tooltipPosition{};             // screen-space anchor
	bool m_tooltipVisible{ false };
	static constexpr float kTooltipDelay = 0.45f; // seconds before showing

#if ENGINE_EDITOR
	// Level-load request callback
	std::function<void(const std::string&)> m_onLevelLoadRequested;

	// Widget editor tab (extracted to EditorTabs/WidgetEditorTab.h)
	std::unique_ptr<WidgetEditorTab> m_widgetEditorTab;
	void refreshWidgetEditorHierarchy(const std::string& tabId);
	void refreshWidgetEditorDetails(const std::string& tabId);
	void selectWidgetEditorElement(const std::string& tabId, const std::string& elementId);
	void applyWidgetEditorTransform(const std::string& tabId);
	void addElementToEditedWidget(const std::string& tabId, const std::string& elementType);
	void saveWidgetEditorAsset(const std::string& tabId);
	void markWidgetEditorDirty(const std::string& tabId);
	void refreshWidgetEditorToolbar(const std::string& tabId);
	void deleteSelectedWidgetEditorElement(const std::string& tabId);
	std::string resolveHierarchyRowElementId(const std::string& tabId, const std::string& rowId) const;
	void moveWidgetEditorElement(const std::string& tabId, const std::string& draggedId, const std::string& targetId);
	void refreshWidgetEditorTimeline(const std::string& tabId);
	void buildTimelineTrackRows(const std::string& tabId, WidgetElement& container);
	void buildTimelineRulerAndKeyframes(const std::string& tabId, WidgetElement& container);
	void handleTimelineMouseDown(const std::string& tabId, const Vec2& localPos, float trackAreaWidth);
	void handleTimelineMouseMove(const std::string& tabId, const Vec2& localPos, float trackAreaWidth);
	void handleTimelineMouseUp(const std::string& tabId);

	// UI Designer tab (extracted to EditorTabs/UIDesignerTab.h)
	std::unique_ptr<UIDesignerTab> m_uiDesignerTab;
	ViewportUIManager* getViewportUIManager() const;
	void refreshUIDesignerHierarchy();
	void refreshUIDesignerDetails();
	void selectUIDesignerElement(const std::string& widgetName, const std::string& elementId);
	void addElementToViewportWidget(const std::string& elementType);
	void deleteSelectedUIDesignerElement();

	// Console / Log-Viewer tab (extracted to EditorTabs/ConsoleTab.h)
	std::unique_ptr<ConsoleTab> m_consoleTab;

	// Profiler / Performance-Monitor tab (extracted to EditorTabs/ProfilerTab.h)
	std::unique_ptr<ProfilerTab> m_profilerTab;

	// Audio Preview tab (extracted to EditorTabs/AudioPreviewTab.h)
	std::unique_ptr<AudioPreviewTab> m_audioPreviewTab;

	// Particle Editor tab (extracted to EditorTabs/ParticleEditorTab.h)
	std::unique_ptr<ParticleEditorTab> m_particleEditorTab;

	// Shader Viewer tab (extracted to EditorTabs/ShaderViewerTab.h)
	std::unique_ptr<ShaderViewerTab> m_shaderViewerTab;

	// Render-Pass-Debugger tab (extracted to EditorTabs/RenderDebuggerTab.h)
	std::unique_ptr<RenderDebuggerTab> m_renderDebuggerTab;

	// Cinematic Sequencer tab (extracted to EditorTabs/SequencerTab.h)
	std::unique_ptr<SequencerTab> m_sequencerTab;

	// Level Composition tab (extracted to EditorTabs/LevelCompositionTab.h)
	std::unique_ptr<LevelCompositionTab> m_levelCompositionTab;

	// Animation Editor tab (extracted to EditorTabs/AnimationEditorTab.h)
	std::unique_ptr<AnimationEditorTab> m_animationEditorTab;

public:
	bool getWidgetEditorCanvasRect(Vec4& outRect) const;
	bool isWidgetEditorContentWidget(const std::string& widgetId) const;

	// FBO preview accessors for the renderer (delegates to WidgetEditorTab)
	struct WidgetEditorPreviewInfo
	{
		std::shared_ptr<Widget> editedWidget;
		std::string selectedElementId;
		std::string hoveredElementId;
		float zoom{ 1.0f };
		Vec2 panOffset{};
		bool dirty{ false };
		std::string tabId;
	};
	bool getWidgetEditorPreviewInfo(WidgetEditorPreviewInfo& out) const;
	void clearWidgetEditorPreviewDirty();
	bool selectWidgetEditorElementAtPos(const Vec2& screenPos);
	void updateWidgetEditorHover(const Vec2& screenPos);

public:
	void refreshWorldOutliner();
	void refreshContentBrowser(const std::string& subfolder = "");
	void focusContentBrowserSearch();
	void requestLevelLoad(const std::string& levelRelPath);
	void selectEntity(unsigned int entity);
	unsigned int getSelectedEntity() const;
	void applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity);
	void invalidateHoveredElement();
	std::string getSelectedBrowserFolder() const;

	// Grid asset selection
	std::string getSelectedGridAsset() const;
	void clearSelectedGridAsset();

	// True when a text entry bar has keyboard focus (blocks engine shortcuts).
	bool hasEntryFocused() const { return m_focusedEntry != nullptr; }

	// Returns true if screenPos is over the content browser grid area
	bool isOverContentBrowserGrid(const Vec2& screenPos) const;

	void refreshStatusBar();
	void showSaveProgressModal(size_t total);
	void updateSaveProgress(size_t saved, size_t total);
	void closeSaveProgressModal(bool success);

	// Unsaved-changes dialog: shows a checkbox list of all unsaved assets.
	// onDone is called after saving completes (or user skips saving).
	void showUnsavedChangesDialog(std::function<void()> onDone);

	// Level load progress modal
	void showLevelLoadProgress(const std::string& levelName);
	void updateLevelLoadProgress(const std::string& status);
	void closeLevelLoadProgress();

	// Level-load request callback (registered by main.cpp to handle level switch orchestration)
	using LevelLoadCallback = std::function<void(const std::string& levelRelPath)>;
	void setOnLevelLoadRequested(LevelLoadCallback callback) { m_onLevelLoadRequested = std::move(callback); }
#endif // ENGINE_EDITOR

public:
	// Drag & Drop
	bool isDragging() const { return m_dragging; }
	const std::string& getDragPayload() const { return m_dragPayload; }
	const std::string& getDragSourceId() const { return m_dragSourceId; }
	void cancelDrag();

#if ENGINE_EDITOR
	// Callback invoked when an asset is dropped on the viewport (not over UI)
	using DropOnViewportCallback = std::function<void(const std::string& payload, const Vec2& screenPos)>;
	void setOnDropOnViewport(DropOnViewportCallback callback) { m_onDropOnViewport = std::move(callback); }

	// Callback invoked when an asset is dropped on a content browser folder
	using DropOnFolderCallback = std::function<void(const std::string& payload, const std::string& folderPath)>;
	void setOnDropOnFolder(DropOnFolderCallback callback) { m_onDropOnFolder = std::move(callback); }

	// Callback invoked when an asset is dropped on an entity in the Outliner
	using DropOnEntityCallback = std::function<void(const std::string& payload, unsigned int entity)>;
	void setOnDropOnEntity(DropOnEntityCallback callback) { m_onDropOnEntity = std::move(callback); }

	// ── Build System UI (extracted to EditorTabs/BuildSystemUI) ──────────
	// Type aliases for external consumers (main.cpp, BuildPipeline)
	using BuildProfile     = BuildSystemUI::BuildProfile;
	using BuildGameConfig  = BuildSystemUI::BuildGameConfig;
	using ToolchainInfo    = BuildSystemUI::ToolchainInfo;
	using BuildGameCallback = BuildSystemUI::BuildGameCallback;

	BuildSystemUI& getBuildSystemUI();

	void loadBuildProfiles();
	void saveBuildProfile(const BuildProfile& profile);
	void deleteBuildProfile(const std::string& name);
	const std::vector<BuildProfile>& getBuildProfiles() const;

	void openBuildGameDialog();
	void setOnBuildGame(BuildGameCallback cb);

	void showBuildProgress();
	void updateBuildProgress(const std::string& status, int step, int totalSteps);
	void closeBuildProgress(bool success, const std::string& message = {});
	void dismissBuildProgress();

	void appendBuildOutput(const std::string& line);
	void pollBuildThread();
	bool isBuildRunning() const;

	bool detectCMake();
	bool isCMakeAvailable() const;
	const std::string& getCMakePath() const;
	void showCMakeInstallPrompt();

	bool detectBuildToolchain();
	bool isBuildToolchainAvailable() const;
	const ToolchainInfo& getBuildToolchain() const;
	void showToolchainInstallPrompt();

	void startAsyncToolchainDetection();
	void pollToolchainDetection();

	// ── Editor Dialogs (extracted to EditorTabs/EditorDialogs) ──────────
	EditorDialogs& getEditorDialogs();
#endif // ENGINE_EDITOR

private:
	// Drag & Drop state
	bool m_dragging{ false };
	bool m_dragPending{ false };
	Vec2 m_dragStartPos{};
	std::string m_dragPayload;
	std::string m_dragSourceId;
	std::string m_dragLabel;
	std::string m_sliderDragElementId;
	std::function<void(const std::string&, const Vec2&)> m_onDropOnViewport;
	std::function<void(const std::string&, const std::string&)> m_onDropOnFolder;
	std::function<void(const std::string&, unsigned int)> m_onDropOnEntity;

#if ENGINE_EDITOR
	// Build System UI (extracted to EditorTabs/BuildSystemUI)
	std::unique_ptr<BuildSystemUI> m_buildSystemUI;

	// Editor Dialogs (extracted to EditorTabs/EditorDialogs)
	std::unique_ptr<EditorDialogs> m_editorDialogs;
#endif // ENGINE_EDITOR
};
