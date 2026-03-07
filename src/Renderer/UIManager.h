#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <set>

#include "../Core/MathTypes.h"
#include "../AssetManager/AssetTypes.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "UIWidget.h"
#include "EditorUI/EditorWidget.h"

class EngineLevel;
class Renderer;
class PopupWindow;
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
    void handleMouseMotionForPan(const Vec2& screenPos);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key);
    void setMousePosition(const Vec2& screenPos);
    const Vec2& getMousePosition() const { return m_mousePosition; }
    bool isPointerOverUI(const Vec2& screenPos) const;
    bool hasClickEvent(const std::string& eventId) const;
	void registerClickEvent(const std::string& eventId, std::function<void()> callback);
	void markAllWidgetsDirty();
	void rebuildAllEditorUI();
	void applyThemeToAllEditorWidgets();

	bool isRenderDirty() const;
    void clearRenderDirty();

    void showModalMessage(const std::string& message, std::function<void()> onClosed = {});
    void closeModalMessage();
    void showConfirmDialog(const std::string& message, std::function<void()> onConfirm, std::function<void()> onCancel = {});
    void showConfirmDialogWithCheckbox(const std::string& message, const std::string& checkboxLabel, bool checkedByDefault,
        std::function<void(bool checked)> onConfirm, std::function<void()> onCancel = {});
    void showToastMessage(const std::string& message, float durationSeconds);
    void updateNotifications(float deltaSeconds);

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
	void openWidgetEditorPopup(const std::string& relativeAssetPath);
    void openUIDesignerTab();
    void closeUIDesignerTab();
    bool isUIDesignerOpen() const;
    void openProjectScreen(std::function<void(const std::string& projectPath, bool isNew, bool setAsDefault, bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI)> onProjectChosen);

    // Returns true if the active tab is a widget editor and had a selected element to delete
    bool tryDeleteWidgetEditorElement();

	static UIManager* GetActiveInstance();
	static void SetActiveInstance(UIManager* instance);

	void applyPendingThemeUpdate();

private:
	WidgetEntry* findWidgetEntry(const std::string& id);
	const WidgetEntry* findWidgetEntry(const std::string& id) const;
    WidgetElement* hitTest(const Vec2& screenPos, bool logDetails = false) const;
	void populateOutlinerWidget(const std::shared_ptr<EditorWidget>& widget);
	void populateOutlinerDetails(unsigned int entity);
	void populateContentBrowserWidget(const std::shared_ptr<EditorWidget>& widget);
    void applyAssetToEntity(AssetType type, const std::string& assetPath, unsigned int entity);
    void updateHoverStates();
    void setFocusedEntry(WidgetElement* element);
    void ensureModalWidget();
	std::shared_ptr<EditorWidget> createToastWidget(const std::string& message, const std::string& name) const;
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
	};
    std::vector<ModalRequest> m_modalQueue;
    std::vector<ToastNotification> m_toasts;
    uint64_t m_nextToastId{ 1 };

    // Dropdown menu state
	std::shared_ptr<EditorWidget> m_dropdownWidget;
    bool m_dropdownVisible{ false };
    std::string m_dropdownSourceId;  // id of the DropdownButton that opened the menu

	void bindClickEventsForWidget(const std::shared_ptr<EditorWidget>& widget);
	void bindClickEventsForElement(WidgetElement& element);
	EngineLevel* m_outlinerLevel{ nullptr };
	size_t m_levelChangedCallbackToken{ 0 };
	unsigned int m_outlinerSelectedEntity{ 0 };
	std::string m_contentBrowserPath;  // current subfolder relative to Content (empty = root)
	std::string m_selectedBrowserFolder; // folder highlighted in tree (shown in grid)
	std::string m_selectedGridAsset;     // relative asset path selected in grid (empty = none)
	std::unordered_set<std::string> m_expandedFolders; // set of expanded folder paths
	bool m_registryWasReady{ false }; // tracks previous registry state for change detection
	bool m_renamingGridAsset{ false };  // true while inline rename entry bar is shown
	std::string m_renameOriginalPath;   // relPath of the asset being renamed
	uint64_t m_lastEcsComponentVersion{ 0 }; // tracks ECS component changes for auto-refresh
	uint64_t m_lastRegistryVersion{ 0 };     // tracks asset registry changes for auto-refresh

	// Double-click detection
	std::string m_lastClickedElementId;
	uint64_t m_lastClickTimeMs{ 0 };

	// Hover tracking (avoids full tree walk per mouse move)
	WidgetElement* m_lastHoveredElement{ nullptr };

	// Save progress modal state
	std::shared_ptr<EditorWidget> m_saveProgressWidget;
	bool m_saveProgressVisible{ false };
	size_t m_saveProgressTotal{ 0 };
	size_t m_saveProgressSaved{ 0 };

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

	// Drag & Drop
	bool isDragging() const { return m_dragging; }
	const std::string& getDragPayload() const { return m_dragPayload; }
	const std::string& getDragSourceId() const { return m_dragSourceId; }
	void cancelDrag();

	// Callback invoked when an asset is dropped on the viewport (not over UI)
	using DropOnViewportCallback = std::function<void(const std::string& payload, const Vec2& screenPos)>;
	void setOnDropOnViewport(DropOnViewportCallback callback) { m_onDropOnViewport = std::move(callback); }

	// Callback invoked when an asset is dropped on a content browser folder
	using DropOnFolderCallback = std::function<void(const std::string& payload, const std::string& folderPath)>;
	void setOnDropOnFolder(DropOnFolderCallback callback) { m_onDropOnFolder = std::move(callback); }

	// Callback invoked when an asset is dropped on an entity in the Outliner
	using DropOnEntityCallback = std::function<void(const std::string& payload, unsigned int entity)>;
	void setOnDropOnEntity(DropOnEntityCallback callback) { m_onDropOnEntity = std::move(callback); }

private:
	// Drag & Drop state
	bool m_dragging{ false };
	bool m_dragPending{ false };
	Vec2 m_dragStartPos{};
	std::string m_dragPayload;
	std::string m_dragSourceId;
	std::string m_dragLabel;
	DropOnViewportCallback m_onDropOnViewport;
	DropOnFolderCallback m_onDropOnFolder;
	DropOnEntityCallback m_onDropOnEntity;
};
