#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "../Core/MathTypes.h"
#include "UIWidget.h"

class EngineLevel;

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
        std::shared_ptr<Widget> widget;
        uint64_t runtimeId{ 0 };
    };

    UIManager();
    ~UIManager() = default;

    Vec2 getAvailableViewportSize() const;
    void setAvailableViewportSize(const Vec2& size);

    void registerUI(const std::string& id);
    const std::vector<UIEntry>& getRegisteredUI() const;

    void registerWidget(const std::string& id, const std::shared_ptr<Widget>& widget);
    const std::vector<WidgetEntry>& getRegisteredWidgets() const;
    const std::vector<const WidgetEntry*>& getWidgetsOrderedByZ() const;
    void unregisterWidget(const std::string& id);
    WidgetElement* findElementById(const std::string& elementId);

    void updateLayouts(const std::function<Vec2(const std::string&, float)>& measureText);
    bool needsLayoutUpdate() const;
    bool handleMouseDown(const Vec2& screenPos, int button);
    bool handleMouseUp(const Vec2& screenPos, int button);
    bool handleScroll(const Vec2& screenPos, float delta);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key);
    void setMousePosition(const Vec2& screenPos);
    const Vec2& getMousePosition() const { return m_mousePosition; }
    bool isPointerOverUI(const Vec2& screenPos) const;
    bool hasClickEvent(const std::string& eventId) const;
    void registerClickEvent(const std::string& eventId, std::function<void()> callback);
    void markAllWidgetsDirty();

    bool isRenderDirty() const;
    void clearRenderDirty();

    void showModalMessage(const std::string& message, std::function<void()> onClosed = {});
    void closeModalMessage();
    void showToastMessage(const std::string& message, float durationSeconds);
    void updateNotifications(float deltaSeconds);

    static UIManager* GetActiveInstance();
    static void SetActiveInstance(UIManager* instance);

private:
    WidgetEntry* findWidgetEntry(const std::string& id);
    const WidgetEntry* findWidgetEntry(const std::string& id) const;
    WidgetElement* hitTest(const Vec2& screenPos, bool logDetails = false) const;
    void populateOutlinerWidget(const std::shared_ptr<Widget>& widget);
    void populateOutlinerDetails(unsigned int entity);
    void populateContentBrowserWidget(const std::shared_ptr<Widget>& widget);
    void updateHoverStates();
    void setFocusedEntry(WidgetElement* element);
    void ensureModalWidget();
    std::shared_ptr<Widget> createToastWidget(const std::string& message, const std::string& name) const;
    void updateToastStackLayout();

    Vec2 m_availableViewportSize{};
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

    std::shared_ptr<Widget> m_modalWidget;
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
        std::shared_ptr<Widget> widget;
        float timer{ 0.0f };
        float duration{ 0.0f };
    };
    std::vector<ModalRequest> m_modalQueue;
    std::vector<ToastNotification> m_toasts;
    uint64_t m_nextToastId{ 1 };

	void bindClickEventsForWidget(const std::shared_ptr<Widget>& widget);
	void bindClickEventsForElement(WidgetElement& element);
	EngineLevel* m_outlinerLevel{ nullptr };
	unsigned int m_outlinerSelectedEntity{ 0 };
	std::string m_contentBrowserPath;  // current subfolder relative to Content (empty = root)
	std::string m_selectedBrowserFolder; // folder highlighted in tree (shown in grid)
	std::unordered_set<std::string> m_expandedFolders; // set of expanded folder paths

	// Double-click detection
	std::string m_lastClickedElementId;
	uint64_t m_lastClickTimeMs{ 0 };

	// Hover tracking (avoids full tree walk per mouse move)
	WidgetElement* m_lastHoveredElement{ nullptr };

	// Save progress modal state
	std::shared_ptr<Widget> m_saveProgressWidget;
	bool m_saveProgressVisible{ false };
	size_t m_saveProgressTotal{ 0 };
	size_t m_saveProgressSaved{ 0 };

public:
	void refreshWorldOutliner();
	void refreshContentBrowser(const std::string& subfolder = "");
	void selectEntity(unsigned int entity);
	unsigned int getSelectedEntity() const { return m_outlinerSelectedEntity; }

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
