#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <set>
#include "../../Renderer/UIWidget.h"

class UIManager;
class Renderer;
class EditorWidget;

class WidgetEditorTab
{
public:
	WidgetEditorTab(UIManager* uiManager, Renderer* renderer)
		: m_uiManager(uiManager), m_renderer(renderer) {}
	~WidgetEditorTab() = default;

	// State per open editor tab
	struct State
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

	// FBO preview accessors for the renderer
	struct PreviewInfo
	{
		std::shared_ptr<Widget> editedWidget;
		std::string selectedElementId;
		std::string hoveredElementId;
		float zoom{ 1.0f };
		Vec2 panOffset{};
		bool dirty{ false };
		std::string tabId;
	};

	// Public API
	void openTab(const std::string& relativeAssetPath);
	bool tryDeleteSelectedElement();
	bool getCanvasRect(Vec4& outRect) const;
	bool isContentWidget(const std::string& widgetId) const;
	bool getPreviewInfo(PreviewInfo& out) const;
	void clearPreviewDirty();
	bool selectElementAtPos(const Vec2& screenPos);
	void updateHover(const Vec2& screenPos);

	// Accessor for UIManager general mouse handlers
	State* getActiveState();
	bool isOverCanvas(const Vec2& screenPos) const;

	// Internal methods (delegation from UIManager)
	void selectElement(const std::string& tabId, const std::string& elementId);
	void applyTransform(const std::string& tabId);
	void saveAsset(const std::string& tabId);
	void markDirty(const std::string& tabId);
	void refreshToolbar(const std::string& tabId);
	void deleteSelectedElement(const std::string& tabId);
	void addElement(const std::string& tabId, const std::string& elementType);
	std::string resolveHierarchyRowElementId(const std::string& tabId, const std::string& rowId) const;
	void moveElement(const std::string& tabId, const std::string& draggedId, const std::string& targetId);
	void refreshHierarchy(const std::string& tabId);
	void refreshDetails(const std::string& tabId);
	void refreshTimeline(const std::string& tabId);
	void buildTimelineTrackRows(const std::string& tabId, WidgetElement& container);
	void buildTimelineRulerAndKeyframes(const std::string& tabId, WidgetElement& container);
	void handleTimelineMouseDown(const std::string& tabId, const Vec2& localPos, float trackAreaWidth);
	void handleTimelineMouseMove(const std::string& tabId, const Vec2& localPos, float trackAreaWidth);
	void handleTimelineMouseUp(const std::string& tabId);

	// Map access
	std::unordered_map<std::string, State>& getStates() { return m_states; }
	const std::unordered_map<std::string, State>& getStates() const { return m_states; }
	bool hasState(const std::string& tabId) const { return m_states.count(tabId) > 0; }

private:
	UIManager* m_uiManager{ nullptr };
	Renderer* m_renderer{ nullptr };
	std::unordered_map<std::string, State> m_states;
};
