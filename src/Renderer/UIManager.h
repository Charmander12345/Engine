#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>

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
    bool handleScroll(const Vec2& screenPos, float delta);
    bool handleTextInput(const std::string& text);
    bool handleKeyDown(int key);
    void setMousePosition(const Vec2& screenPos);
    bool isPointerOverUI(const Vec2& screenPos) const;
    bool hasClickEvent(const std::string& eventId) const;
    void registerClickEvent(const std::string& eventId, std::function<void()> callback);
    void markAllWidgetsDirty();

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

public:
	void refreshWorldOutliner();
};
