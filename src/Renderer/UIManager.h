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
#endif
class ViewportUIManager;

class UIManager
{
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

    void setRenderer(Renderer* renderer) { m_renderer = renderer; }
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

    void showModalMessage(const std::string& message, std::function<void()> onClosed = {});
    void closeModalMessage();
	#if ENGINE_EDITOR
	void showConfirmDialog(const std::string& message, std::function<void()> onConfirm, std::function<void()> onCancel = {});
	void showConfirmDialogWithCheckbox(const std::string& message, const std::string& checkboxLabel, bool checkedByDefault,
		std::function<void(bool checked)> onConfirm, std::function<void()> onCancel = {});
#endif
	// Notification levels for priority-based styling (alias from DiagnosticsManager)
	using NotificationLevel = DiagnosticsManager::NotificationLevel;

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

private:
	WidgetEntry* findWidgetEntry(const std::string& id);
	const WidgetEntry* findWidgetEntry(const std::string& id) const;
	WidgetElement* hitTest(const Vec2& screenPos, bool logDetails = false) const;

	#if ENGINE_EDITOR
	KeyCaptureCallback m_keyCaptureCallback;
	void populateOutlinerWidget(const std::shared_ptr<EditorWidget>& widget);
	void populateOutlinerDetails(unsigned int entity);
	void populateContentBrowserWidget(const std::shared_ptr<EditorWidget>& widget);
	std::unordered_set<std::string> buildReferencedAssetSet() const;
	void applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity);
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
	void navigateOutlinerByArrow(int direction);      // -1 = up, +1 = down
	void navigateContentBrowserByArrow(int dCol, int dRow); // column/row delta
#endif
	void ensureModalWidget();
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

	std::shared_ptr<EditorWidget> m_modalWidget;
    std::string m_modalMessage;
    bool m_modalVisible{ false };
    std::function<void()> m_modalOnClosed;
    float m_notificationPollTimer{ 0.0f };
    struct ModalRequest
    {
        std::string message;
        std::function<void()> onClosed;
    };
	struct ToastNotification
	{
		std::string id;
		std::shared_ptr<EditorWidget> widget;
		float timer{ 0.0f };
		float duration{ 0.0f };
		NotificationLevel level{ NotificationLevel::Info };
	};
	std::vector<ModalRequest> m_modalQueue;
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
	EngineLevel* m_outlinerLevel{ nullptr };
	size_t m_levelChangedCallbackToken{ 0 };
	unsigned int m_outlinerSelectedEntity{ 0 };

	// Entity clipboard for Copy/Paste
	struct EntityClipboard
	{
		bool valid{ false };
		std::optional<ECS::TransformComponent>       transform;
		std::optional<ECS::MeshComponent>            mesh;
		std::optional<ECS::MaterialComponent>        material;
		std::optional<ECS::LightComponent>           light;
		std::optional<ECS::CameraComponent>          camera;
		std::optional<ECS::PhysicsComponent>         physics;
		std::optional<ECS::ScriptComponent>          script;
		std::optional<ECS::NameComponent>            name;
		std::optional<ECS::CollisionComponent>       collision;
		std::optional<ECS::HeightFieldComponent>     heightField;
		std::optional<ECS::LodComponent>             lod;
		std::optional<ECS::AnimationComponent>       animation;
		std::optional<ECS::ParticleEmitterComponent> particleEmitter;
	};
	EntityClipboard m_entityClipboard;

	std::string m_contentBrowserPath;  // current subfolder relative to Content (empty = root)
	std::string m_selectedBrowserFolder; // folder highlighted in tree (shown in grid)
	std::string m_selectedGridAsset;     // relative asset path selected in grid (empty = none)
	std::unordered_set<std::string> m_expandedFolders; // set of expanded folder paths
	std::string m_browserSearchText;     // real-time search filter text
	uint16_t m_browserTypeFilter{ 0xFFFF }; // bitmask: 1 bit per AssetType (0xFFFF = all)
	bool m_registryWasReady{ false }; // tracks previous registry state for change detection
	bool m_renamingGridAsset{ false };  // true while inline rename entry bar is shown
	std::string m_renameOriginalPath;   // relPath of the asset being renamed
	uint64_t m_lastEcsComponentVersion{ 0 }; // tracks ECS component changes for auto-refresh
	uint64_t m_lastRegistryVersion{ 0 };     // tracks asset registry changes for auto-refresh
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
	// Save progress modal state
	std::shared_ptr<EditorWidget> m_saveProgressWidget;
	bool m_saveProgressVisible{ false };
	size_t m_saveProgressTotal{ 0 };
	size_t m_saveProgressSaved{ 0 };

	// Level load progress modal state
	std::shared_ptr<EditorWidget> m_levelLoadProgressWidget;

	// Level-load request callback
	std::function<void(const std::string&)> m_onLevelLoadRequested;

	// Widget editor state (per open editor tab)
	struct WidgetEditorState
	{
		std::string tabId;
		std::string assetPath;
		std::shared_ptr<Widget> editedWidget;          // the widget being edited
		std::string selectedElementId;                  // id of the currently selected element
		std::string contentWidgetId;
		std::string leftWidgetId;
		std::string rightWidgetId;
		std::string canvasWidgetId;
		std::string toolbarWidgetId;

		unsigned int assetId{ 0 };
		bool isDirty{ false };
		bool showAnimationsPanel{ false };
		std::string selectedAnimationName;

		// Bottom animation timeline panel
		std::string bottomWidgetId;
		float timelineScrubTime{ 0.0f };      // current scrubber position in seconds
		float timelineZoom{ 1.0f };            // horizontal zoom for timeline
		float timelineScrollX{ 0.0f };         // horizontal scroll offset
		int selectedTrackIndex{ -1 };          // which track row is selected
		int draggingKeyframeTrack{ -1 };       // track index of keyframe being dragged (-1=none)
		int draggingKeyframeIndex{ -1 };       // keyframe index being dragged
		bool isDraggingScrubber{ false };       // true while dragging the scrubber
		bool isDraggingEndLine{ false };        // true while dragging the end-of-animation duration line
		std::set<std::string> expandedTimelineElements; // element IDs expanded in tree-view

		// Zoom & pan
		float zoom{ 1.0f };
		Vec2 panOffset{ 0.0f, 0.0f };
		bool isPanning{ false };
		Vec2 panStartMouse{};
		Vec2 panStartOffset{};

		// Original (unscaled) preview placement
		Vec2 basePreviewPos{};
		Vec2 basePreviewSize{};

		// FBO preview dirty flag – set true whenever the preview needs re-rendering
		bool previewDirty{ true };

		// Hover tracking for canvas preview
		std::string hoveredElementId;
	};
	std::unordered_map<std::string, WidgetEditorState> m_widgetEditorStates; // key = tabId
	void refreshWidgetEditorHierarchy(const std::string& tabId);
	void refreshWidgetEditorDetails(const std::string& tabId);
	void selectWidgetEditorElement(const std::string& tabId, const std::string& elementId);
	void applyWidgetEditorTransform(const std::string& tabId);
	WidgetEditorState* getActiveWidgetEditorState();
	bool isOverWidgetEditorCanvas(const Vec2& screenPos) const;
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

	// UI Designer state (Gameplay UI designer tab)
	struct UIDesignerState
	{
		std::string tabId;
		std::string leftWidgetId;
		std::string rightWidgetId;
		std::string toolbarWidgetId;
		std::string selectedWidgetName;   // currently selected viewport widget
		std::string selectedElementId;    // currently selected element within that widget
		bool isOpen{ false };
	};
	UIDesignerState m_uiDesignerState;
	void refreshUIDesignerHierarchy();
	void refreshUIDesignerDetails();
	void selectUIDesignerElement(const std::string& widgetName, const std::string& elementId);
	void addElementToViewportWidget(const std::string& elementType);
	void deleteSelectedUIDesignerElement();
	ViewportUIManager* getViewportUIManager() const;

	// Console / Log-Viewer tab state
	struct ConsoleState
	{
		std::string tabId;
		std::string widgetId;
		uint64_t lastSeenSequenceId{ 0 };
		uint8_t levelFilter{ 0xFF }; // bitmask: bit0=INFO, bit1=WARNING, bit2=ERROR, bit3=FATAL
		std::string searchText;
		bool autoScroll{ true };
		bool isOpen{ false };
		float refreshTimer{ 0.0f };
	};
	ConsoleState m_consoleState;
	void refreshConsoleLog();
	void buildConsoleToolbar(WidgetElement& root);

	// Profiler / Performance-Monitor tab state
	struct ProfilerState
	{
		std::string tabId;
		std::string widgetId;
		bool isOpen{ false };
		bool frozen{ false };
		float refreshTimer{ 0.0f };
	};
	ProfilerState m_profilerState;
	void refreshProfilerMetrics();
	void buildProfilerToolbar(WidgetElement& root);

	// Audio Preview tab state
	struct AudioPreviewState
	{
		std::string tabId;
		std::string widgetId;
		std::string assetPath;       // Content-relative path of the open audio asset
		bool isOpen{ false };
		bool isPlaying{ false };
		unsigned int playHandle{ 0 }; // AudioManager source handle
		float volume{ 1.0f };
		// Metadata extracted from asset JSON
		int channels{ 0 };
		int sampleRate{ 0 };
		int format{ 0 };
		size_t dataBytes{ 0 };
		float durationSeconds{ 0.0f };
		std::string displayName;
	};
	AudioPreviewState m_audioPreviewState;
	void refreshAudioPreview();
	void buildAudioPreviewToolbar(WidgetElement& root);
	void buildAudioPreviewWaveform(WidgetElement& root);
	void buildAudioPreviewMetadata(WidgetElement& root);

	// Particle Editor tab state
	struct ParticleEditorState
	{
		std::string tabId;
		std::string widgetId;
		ECS::Entity linkedEntity{ 0 };
		bool isOpen{ false };
		float refreshTimer{ 0.0f };
		int presetIndex{ -1 };    // currently selected preset (-1 = custom)
	};
	ParticleEditorState m_particleEditorState;
	void refreshParticleEditor();
	void buildParticleEditorToolbar(WidgetElement& root);
	void buildParticleEditorParams(WidgetElement& root);
	void applyParticlePreset(int presetIndex);

	// Shader Viewer tab state
	struct ShaderViewerState
	{
		std::string tabId;
		std::string widgetId;
		bool isOpen{ false };
		std::string selectedFile;              // currently viewed shader filename
		std::vector<std::string> shaderFiles;  // cached list of .glsl filenames
	};
	ShaderViewerState m_shaderViewerState;
	void refreshShaderViewer();
	void buildShaderViewerToolbar(WidgetElement& root);
	void buildShaderFileList(WidgetElement& root);
	void buildShaderCodeView(WidgetElement& root);

	// Render-Pass-Debugger tab state
	struct RenderDebuggerState
	{
		std::string tabId;
		std::string widgetId;
		bool isOpen{ false };
		float refreshTimer{ 0.0f };
	};
	RenderDebuggerState m_renderDebuggerState;
	void refreshRenderDebugger();
	void buildRenderDebuggerToolbar(WidgetElement& root);

	// Cinematic Sequencer tab state (Phase 11.2)
	struct SequencerState
	{
		std::string tabId;
		std::string widgetId;
		bool isOpen{ false };
		float refreshTimer{ 0.0f };
		// Playback
		bool playing{ false };
		float playbackSpeed{ 1.0f };
		float scrubberT{ 0.0f };       // normalised 0..1
		// Editing
		int selectedKeyframe{ -1 };     // -1 = none
		bool showSplineInViewport{ true };
		bool loopPlayback{ false };
		float pathDuration{ 5.0f };     // seconds
	};
	SequencerState m_sequencerState;
	void refreshSequencerTimeline();
	void buildSequencerToolbar(WidgetElement& root);
	void buildSequencerTimeline(WidgetElement& root);
	void buildSequencerKeyframeList(WidgetElement& root);

	// Level Composition tab state (Phase 11.4)
	struct LevelCompositionState
	{
		std::string tabId;
		std::string widgetId;
		bool isOpen{ false };
		float refreshTimer{ 0.0f };
		int selectedSubLevel{ -1 };
	};
	LevelCompositionState m_levelCompositionState;
	void buildLevelCompositionToolbar(WidgetElement& root);
	void buildLevelCompositionSubLevelList(WidgetElement& root);
	void buildLevelCompositionVolumeList(WidgetElement& root);

	// Animation Editor tab state (Phase 2.4)
	struct AnimationEditorState
	{
		std::string tabId;
		std::string widgetId;
		ECS::Entity linkedEntity{ 0 };
		bool isOpen{ false };
		float refreshTimer{ 0.0f };
		int selectedClip{ -1 };
	};
	AnimationEditorState m_animationEditorState;
	void refreshAnimationEditor();
	void buildAnimationEditorToolbar(WidgetElement& root);
	void buildAnimationEditorClipList(WidgetElement& root);
	void buildAnimationEditorControls(WidgetElement& root);
	void buildAnimationEditorBoneTree(WidgetElement& root);

public:
	bool getWidgetEditorCanvasRect(Vec4& outRect) const;
	bool isWidgetEditorContentWidget(const std::string& widgetId) const;

	// FBO preview accessors for the renderer
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
	void selectEntity(unsigned int entity);
	unsigned int getSelectedEntity() const { return m_outlinerSelectedEntity; }
	const std::string& getSelectedBrowserFolder() const { return m_selectedBrowserFolder; }

	// Grid asset selection
	const std::string& getSelectedGridAsset() const { return m_selectedGridAsset; }
	void clearSelectedGridAsset() { m_selectedGridAsset.clear(); }

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

	// ── Build Profiles (Phase 10.3) ───────────────────────────────────────
	struct BuildProfile
	{
		std::string name            = "Development";
		std::string cmakeBuildType  = "RelWithDebInfo";  // "Debug", "RelWithDebInfo", "Release"
		std::string logLevel        = "info";             // "verbose", "info", "warning", "error"
		bool enableHotReload        = true;
		bool enableValidation       = false;
		bool enableProfiler         = true;
		bool compressAssets         = false;
	};

	void loadBuildProfiles();
	void saveBuildProfile(const BuildProfile& profile);
	void deleteBuildProfile(const std::string& name);
	const std::vector<BuildProfile>& getBuildProfiles() const { return m_buildProfiles; }

	// ── Build Game (Phase 10) ──────────────────────────────────────────────
	struct BuildGameConfig
	{
		std::string  startLevel;                     // relative to Content (e.g. "Levels/MyLevel.level")
		std::string  windowTitle   = "Game";
		std::string  outputDir;                      // standardised: <project>/Build
		std::string  binaryDir;                      // binary cache: <project>/Binary
		BuildProfile profile;                        // active build profile
		bool         launchAfterBuild = true;
		bool         cleanBuild       = false;        // delete binary cache before compiling (disables incremental)
	};

	void openBuildGameDialog();

	using BuildGameCallback = std::function<void(const BuildGameConfig& config)>;
	void setOnBuildGame(BuildGameCallback cb) { m_onBuildGame = std::move(cb); }

	// Build progress popup (called by the build pipeline)
	void showBuildProgress();
	void updateBuildProgress(const std::string& status, int step, int totalSteps);
	void closeBuildProgress(bool success, const std::string& message = {});
	void dismissBuildProgress();

	// Thread-safe: append a line to the build output log (called from build thread)
	void appendBuildOutput(const std::string& line);
	// Main-thread: poll the build thread for pending UI updates. Call once per frame.
	void pollBuildThread();
	bool isBuildRunning() const { return m_buildRunning.load(); }

	// ── CMake Detection ───────────────────────────────────────────────────
	// Call once at startup to locate CMake.  Returns true if CMake is available.
	bool detectCMake();
	bool isCMakeAvailable() const { return m_cmakeAvailable.load(); }
	const std::string& getCMakePath() const { return m_cmakePath; }
	// Show a popup asking the user to install CMake.
	void showCMakeInstallPrompt();

	// ── Build Toolchain Detection ─────────────────────────────────────────
	struct ToolchainInfo
	{
		std::string name;           // e.g. "MSVC", "Clang", "GCC"
		std::string version;        // e.g. "2026", "19.50.35727"
		std::string compilerPath;   // path to cl.exe / clang++ / g++
		std::string vsInstallPath;  // VS installation path (empty if not VS)
	};
	bool detectBuildToolchain();
	bool isBuildToolchainAvailable() const { return m_toolchainAvailable.load(); }
	const ToolchainInfo& getBuildToolchain() const { return m_toolchainInfo; }
	void showToolchainInstallPrompt();

	// ── Async Detection (non-blocking startup) ───────────────────────────
	// Starts CMake + toolchain detection on a background thread.
	void startAsyncToolchainDetection();
	// Main-thread poll: once detection finishes, logs results and shows prompts.
	void pollToolchainDetection();
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
	// Build Game state
	BuildGameCallback m_onBuildGame;
	std::shared_ptr<EditorWidget> m_buildProgressWidget;
	PopupWindow* m_buildPopup{ nullptr };  // separate OS window for build output
	std::vector<BuildProfile> m_buildProfiles;           // loaded build profiles

public:
	// Build thread state – public so the build lambda (registered from main.cpp) can
	// push progress/output from the worker thread via the mutex-protected fields.
	std::thread m_buildThread;
	std::atomic<bool> m_buildRunning{ false };
	std::atomic<bool> m_buildCancelRequested{ false };
	std::mutex m_buildMutex;
	std::vector<std::string> m_buildPendingLines;      // lines queued by build thread
	std::string m_buildPendingStatus;                   // status text queued by build thread
	int m_buildPendingStep{ 0 };
	int m_buildPendingTotalSteps{ 0 };
	bool m_buildPendingStepDirty{ false };
	bool m_buildPendingFinished{ false };
	bool m_buildPendingSuccess{ false };
	std::string m_buildPendingErrorMsg;

private:
	std::vector<std::string> m_buildOutputLines;        // full log (main-thread only)

	// CMake state
	std::atomic<bool> m_cmakeAvailable{ false };
	std::string m_cmakePath;

	// Build toolchain state
	std::atomic<bool> m_toolchainAvailable{ false };
	ToolchainInfo m_toolchainInfo;

	// Async detection state
	std::atomic<bool> m_toolDetectDone{ false };
	bool m_toolDetectPolled{ false };
#endif // ENGINE_EDITOR
};
