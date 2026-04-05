#pragma once

#include "IEditorTab.h"
#include <string>
#include "../../AssetManager/json.hpp"

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab for editing InputAction assets.
/// Shows the action name and modifier checkboxes (Shift, Ctrl, Alt).
class InputActionEditorTab final : public IEditorTab
{
public:
	struct State
	{
		std::string tabId;
		std::string widgetId;
		std::string assetPath;
		std::string assetName;
		std::string originalAssetName;
		nlohmann::json actionData;
		bool isOpen{ false };
		bool isDirty{ false };
		float refreshTimer{ 0.0f };
	};

	explicit InputActionEditorTab(UIManager* uiManager, Renderer* renderer);

	void open() override;
	void close() override;
	bool isOpen() const override { return m_state.isOpen; }
	void update(float deltaSeconds) override;
	const std::string& getTabId() const override { return m_state.tabId; }

	void open(const std::string& assetPath);
	void refresh();
	void save();

	const State& getState() const { return m_state; }

private:
	void buildToolbar(WidgetElement& root);
	void buildDetailsPanel(WidgetElement& root);

	UIManager* m_ui{ nullptr };
	Renderer*  m_renderer{ nullptr };
	State      m_state;
};
