#pragma once

#include "IEditorTab.h"
#include <string>
#include <vector>
#include "../../AssetManager/json.hpp"

class UIManager;
class Renderer;
struct WidgetElement;

/// Editor tab for editing InputMapping assets.
/// Shows a list of action→key bindings that can be added/removed/changed.
class InputMappingEditorTab final : public IEditorTab
{
public:
	struct State
	{
		std::string tabId;
		std::string widgetId;
		std::string assetPath;
		std::string assetName;
		nlohmann::json mappingData; // { "bindings": [ { "action": "...", "key": <int> }, ... ] }
		bool isOpen{ false };
		bool isDirty{ false };
		float refreshTimer{ 0.0f };
		int  selectedBinding{ -1 };
		int  listeningBindingIndex{ -1 }; // >= 0 while waiting for a key press
	};

	explicit InputMappingEditorTab(UIManager* uiManager, Renderer* renderer);

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
	void buildBindingsList(WidgetElement& root);

	/// Collect all InputAction asset names from the registry.
	std::vector<std::string> getInputActionNames() const;

	UIManager* m_ui{ nullptr };
	Renderer*  m_renderer{ nullptr };
	State      m_state;
};
