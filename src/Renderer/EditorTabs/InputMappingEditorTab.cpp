#include "InputMappingEditorTab.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"
#include "../../Core/InputActionManager.h"

#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

InputMappingEditorTab::InputMappingEditorTab(UIManager* uiManager, Renderer* renderer)
	: m_ui(uiManager)
	, m_renderer(renderer)
{}

void InputMappingEditorTab::open()
{
	// Default open with no asset — no-op
}

void InputMappingEditorTab::open(const std::string& assetPath)
{
	if (!m_renderer || !m_ui)
		return;

	const std::string tabId = "InputMappingEditor";

	if (m_state.isOpen && m_state.assetPath == assetPath)
	{
		m_renderer->setActiveTab(tabId);
		m_ui->markAllWidgetsDirty();
		return;
	}

	if (m_state.isOpen)
		close();

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded())
	{
		m_ui->showToastMessage("No project loaded.", UIManager::kToastMedium);
		return;
	}

	const std::filesystem::path contentDir =
		std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const std::filesystem::path absPath = contentDir / assetPath;

	if (!std::filesystem::exists(absPath))
	{
		m_ui->showToastMessage("Input Mapping asset not found: " + assetPath, UIManager::kToastMedium);
		return;
	}

	std::ifstream in(absPath);
	if (!in.is_open())
	{
		m_ui->showToastMessage("Failed to open input mapping asset.", UIManager::kToastMedium);
		return;
	}

	json fileJson = json::parse(in, nullptr, false);
	in.close();
	if (fileJson.is_discarded() || !fileJson.contains("data"))
	{
		m_ui->showToastMessage("Invalid input mapping asset format.", UIManager::kToastMedium);
		return;
	}

	m_renderer->addTab(tabId, "Input Mapping Editor", true);
	m_renderer->setActiveTab(tabId);

	const std::string widgetId = "InputMappingEditor.Main";
	m_ui->unregisterWidget(widgetId);

	m_state = {};
	m_state.tabId       = tabId;
	m_state.widgetId    = widgetId;
	m_state.assetPath   = assetPath;
	m_state.assetName   = fileJson.value("name", std::filesystem::path(assetPath).stem().string());
	m_state.isOpen      = true;
	m_state.isDirty     = false;
	m_state.mappingData = fileJson["data"];

	if (!m_state.mappingData.contains("bindings"))
		m_state.mappingData["bindings"] = json::array();

	{
		auto widget = std::make_shared<EditorWidget>();
		widget->setName(widgetId);
		widget->setAnchor(WidgetAnchor::TopLeft);
		widget->setFillX(true);
		widget->setFillY(true);
		widget->setSizePixels(Vec2{ 0.0f, 0.0f });
		widget->setZOrder(2);

		const auto& theme = EditorTheme::Get();

		WidgetElement root{};
		root.id          = "InputMappingEditor.Root";
		root.type        = WidgetElementType::StackPanel;
		root.from        = Vec2{ 0.0f, 0.0f };
		root.to          = Vec2{ 1.0f, 1.0f };
		root.fillX       = true;
		root.fillY       = true;
		root.orientation = StackOrientation::Vertical;
		root.style.color = theme.panelBackground;
		root.runtimeOnly = true;

		buildToolbar(root);

		{
			WidgetElement sep{};
			sep.type        = WidgetElementType::Panel;
			sep.fillX       = true;
			sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
			sep.style.color = theme.panelBorder;
			sep.runtimeOnly = true;
			root.children.push_back(std::move(sep));
		}

		buildBindingsList(root);

		widget->setElements({ std::move(root) });
		m_ui->registerWidget(widgetId, widget, tabId);
	}

	const std::string tabBtnId   = "TitleBar.Tab." + tabId;
	const std::string closeBtnId = "TitleBar.TabClose." + tabId;

	m_ui->registerClickEvent(tabBtnId, [this, tabId]()
	{
		if (m_renderer)
			m_renderer->setActiveTab(tabId);
		refresh();
	});

	m_ui->registerClickEvent(closeBtnId, [this]()
	{
		close();
	});

	m_ui->registerClickEvent("InputMappingEditor.Save", [this]()
	{
		save();
	});

	m_ui->registerClickEvent("InputMappingEditor.AddBinding", [this]()
	{
		auto actions = getInputActionNames();
		if (actions.empty())
		{
			m_ui->showToastMessage("No Input Actions found. Create one first.", UIManager::kToastMedium);
			return;
		}
		json newBinding = json::object();
		newBinding["action"] = actions.front();
		newBinding["key"] = 0;
		m_state.mappingData["bindings"].push_back(newBinding);
		m_state.isDirty = true;
		refresh();
	});

	refresh();
}

void InputMappingEditorTab::close()
{
	if (!m_state.isOpen || !m_renderer)
		return;

	const std::string tabId = m_state.tabId;

	if (m_renderer->getActiveTabId() == tabId)
		m_renderer->setActiveTab("Viewport");

	m_ui->unregisterWidget(m_state.widgetId);
	m_renderer->removeTab(tabId);
	m_state = {};
	m_ui->markAllWidgetsDirty();
}

void InputMappingEditorTab::update(float deltaSeconds)
{
	if (!m_state.isOpen) return;
	m_state.refreshTimer += deltaSeconds;
	if (m_state.refreshTimer >= 0.5f)
	{
		m_state.refreshTimer = 0.0f;
	}
}

void InputMappingEditorTab::save()
{
	if (!m_state.isOpen) return;

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded()) return;

	const std::filesystem::path contentDir =
		std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const std::filesystem::path absPath = contentDir / m_state.assetPath;

	std::error_code ec;
	std::filesystem::create_directories(absPath.parent_path(), ec);

	std::ofstream out(absPath, std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		m_ui->showToastMessage("Failed to save input mapping.", UIManager::kToastMedium);
		return;
	}

	json fileJson = json::object();
	fileJson["magic"]   = 0x41535453;
	fileJson["version"] = 2;
	fileJson["type"]    = static_cast<int>(AssetType::InputMapping);
	fileJson["name"]    = m_state.assetName;
	fileJson["data"]    = m_state.mappingData;

	out << fileJson.dump(4);
	out.close();

	// Update runtime InputActionManager bindings
	auto& mgr = InputActionManager::Instance();
	// Reload all bindings from this mapping
	mgr.clearBindings();
	if (m_state.mappingData.contains("bindings"))
	{
		for (const auto& b : m_state.mappingData["bindings"])
		{
			InputActionManager::KeyBinding kb;
			kb.actionName = b.value("action", "");
			kb.keycode    = b.value("key", 0u);
			if (!kb.actionName.empty() && kb.keycode != 0)
				mgr.addBinding(kb);
		}
	}

	m_state.isDirty = false;
	m_ui->showToastMessage("Input Mapping saved.", UIManager::kToastMedium);
}

void InputMappingEditorTab::refresh()
{
	if (!m_state.isOpen || !m_ui) return;

	auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
	if (!entry || !entry->widget) return;

	auto& elements = entry->widget->getElementsMutable();
	if (elements.empty()) return;

	auto& root = elements[0];

	for (auto it = root.children.begin(); it != root.children.end(); ++it)
	{
		if (it->id == "InputMappingEditor.BindingsPanel")
		{
			root.children.erase(it);
			break;
		}
	}
	buildBindingsList(root);
	entry->widget->markLayoutDirty();
}

std::vector<std::string> InputMappingEditorTab::getInputActionNames() const
{
	std::vector<std::string> names;
	const auto& registry = AssetManager::Instance().getAssetRegistry();
	for (const auto& e : registry)
	{
		if (e.type == AssetType::InputAction)
			names.push_back(e.name);
	}
	return names;
}

void InputMappingEditorTab::buildToolbar(WidgetElement& root)
{
	const auto& theme = EditorTheme::Get();

	WidgetElement toolbar{};
	toolbar.id          = "InputMappingEditor.Toolbar";
	toolbar.type        = WidgetElementType::StackPanel;
	toolbar.fillX       = true;
	toolbar.orientation = StackOrientation::Horizontal;
	toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
	toolbar.padding     = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
	toolbar.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 1.0f };
	toolbar.runtimeOnly = true;

	// Title
	{
		WidgetElement title{};
		title.id            = "InputMappingEditor.Title";
		title.type          = WidgetElementType::Text;
		title.text          = "Input Mapping: " + m_state.assetName;
		title.font          = theme.fontDefault;
		title.fontSize      = theme.fontSizeSubheading;
		title.textAlignH    = TextAlignH::Left;
		title.textAlignV    = TextAlignV::Center;
		title.style.textColor = theme.textPrimary;
		title.minSize       = EditorTheme::Scaled(Vec2{ 200.0f, 24.0f });
		title.runtimeOnly   = true;
		toolbar.children.push_back(std::move(title));
	}

	// Spacer
	{
		WidgetElement spacer{};
		spacer.type        = WidgetElementType::Panel;
		spacer.fillX       = true;
		spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
		spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
		spacer.runtimeOnly = true;
		toolbar.children.push_back(std::move(spacer));
	}

	// Add Binding button
	{
		WidgetElement addBtn{};
		addBtn.id            = "InputMappingEditor.AddBinding";
		addBtn.type          = WidgetElementType::Button;
		addBtn.text          = "+ Add Binding";
		addBtn.font          = theme.fontDefault;
		addBtn.fontSize      = theme.fontSizeBody;
		addBtn.textAlignH    = TextAlignH::Center;
		addBtn.textAlignV    = TextAlignV::Center;
		addBtn.style.textColor = theme.textPrimary;
		addBtn.style.color     = theme.buttonSubtle;
		addBtn.style.hoverColor = theme.buttonSubtleHover;
		addBtn.style.borderRadius = theme.borderRadius;
		addBtn.minSize       = EditorTheme::Scaled(Vec2{ 90.0f, 24.0f });
		addBtn.hitTestMode   = HitTestMode::Enabled;
		addBtn.shaderVertex  = "button_vertex.glsl";
		addBtn.shaderFragment = "button_fragment.glsl";
		addBtn.runtimeOnly   = true;
		toolbar.children.push_back(std::move(addBtn));
	}

	// Save button
	{
		WidgetElement saveBtn{};
		saveBtn.id            = "InputMappingEditor.Save";
		saveBtn.type          = WidgetElementType::Button;
		saveBtn.text          = "Save";
		saveBtn.font          = theme.fontDefault;
		saveBtn.fontSize      = theme.fontSizeBody;
		saveBtn.textAlignH    = TextAlignH::Center;
		saveBtn.textAlignV    = TextAlignV::Center;
		saveBtn.style.textColor = theme.textPrimary;
		saveBtn.style.color     = theme.accent;
		saveBtn.style.hoverColor = Vec4{ theme.accent.x * 1.15f, theme.accent.y * 1.15f, theme.accent.z * 1.15f, 1.0f };
		saveBtn.style.borderRadius = theme.borderRadius;
		saveBtn.minSize       = EditorTheme::Scaled(Vec2{ 60.0f, 24.0f });
		saveBtn.hitTestMode   = HitTestMode::Enabled;
		saveBtn.shaderVertex  = "button_vertex.glsl";
		saveBtn.shaderFragment = "button_fragment.glsl";
		saveBtn.runtimeOnly   = true;
		toolbar.children.push_back(std::move(saveBtn));
	}

	root.children.push_back(std::move(toolbar));
}

void InputMappingEditorTab::buildBindingsList(WidgetElement& root)
{
	const auto& theme = EditorTheme::Get();

	WidgetElement panel{};
	panel.id          = "InputMappingEditor.BindingsPanel";
	panel.type        = WidgetElementType::StackPanel;
	panel.fillX       = true;
	panel.fillY       = true;
	panel.scrollable  = true;
	panel.orientation = StackOrientation::Vertical;
	panel.padding     = EditorTheme::Scaled(Vec2{ 16.0f, 12.0f });
	panel.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
	panel.runtimeOnly = true;

	// Header row
	{
		WidgetElement header{};
		header.type        = WidgetElementType::StackPanel;
		header.fillX       = true;
		header.orientation = StackOrientation::Horizontal;
		header.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 24.0f });
		header.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
		header.runtimeOnly = true;

		WidgetElement actionLbl{};
		actionLbl.type          = WidgetElementType::Text;
		actionLbl.text          = "Action";
		actionLbl.font          = theme.fontDefault;
		actionLbl.fontSize      = theme.fontSizeSubheading;
		actionLbl.style.textColor = theme.textSecondary;
		actionLbl.minSize       = EditorTheme::Scaled(Vec2{ 200.0f, 20.0f });
		actionLbl.runtimeOnly   = true;
		header.children.push_back(std::move(actionLbl));

		WidgetElement keyLbl{};
		keyLbl.type          = WidgetElementType::Text;
		keyLbl.text          = "Key";
		keyLbl.font          = theme.fontDefault;
		keyLbl.fontSize      = theme.fontSizeSubheading;
		keyLbl.style.textColor = theme.textSecondary;
		keyLbl.minSize       = EditorTheme::Scaled(Vec2{ 150.0f, 20.0f });
		keyLbl.runtimeOnly   = true;
		header.children.push_back(std::move(keyLbl));

		panel.children.push_back(std::move(header));
	}

	// Separator
	{
		WidgetElement sep{};
		sep.type        = WidgetElementType::Panel;
		sep.fillX       = true;
		sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
		sep.style.color = theme.panelBorder;
		sep.runtimeOnly = true;
		panel.children.push_back(std::move(sep));
	}

	if (!m_state.mappingData.contains("bindings"))
		m_state.mappingData["bindings"] = json::array();

	auto& bindings = m_state.mappingData["bindings"];

	for (size_t i = 0; i < bindings.size(); ++i)
	{
		const std::string actionName = bindings[i].value("action", "");
		const uint32_t keycode       = bindings[i].value("key", 0u);

		const std::string keyName = (keycode != 0)
			? std::string(SDL_GetKeyName(static_cast<SDL_Keycode>(keycode)))
			: "<none>";

		const std::string rowId = "InputMappingEditor.Binding." + std::to_string(i);

		WidgetElement row{};
		row.id          = rowId;
		row.type        = WidgetElementType::StackPanel;
		row.fillX       = true;
		row.orientation = StackOrientation::Horizontal;
		row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 28.0f });
		row.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
		row.style.color = (i % 2 == 0)
			? Vec4{ 0.10f, 0.10f, 0.12f, 1.0f }
			: Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
		row.runtimeOnly = true;

		// Action name label (clickable to cycle through available actions)
		{
			WidgetElement actionBtn{};
			actionBtn.id            = rowId + ".Action";
			actionBtn.type          = WidgetElementType::Button;
			actionBtn.text          = actionName.empty() ? "<select>" : actionName;
			actionBtn.font          = theme.fontDefault;
			actionBtn.fontSize      = theme.fontSizeBody;
			actionBtn.textAlignH    = TextAlignH::Left;
			actionBtn.textAlignV    = TextAlignV::Center;
			actionBtn.style.textColor = theme.textPrimary;
			actionBtn.style.color     = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			actionBtn.style.hoverColor = theme.buttonSubtleHover;
			actionBtn.minSize       = EditorTheme::Scaled(Vec2{ 200.0f, 22.0f });
			actionBtn.hitTestMode   = HitTestMode::Enabled;
			actionBtn.shaderVertex  = "button_vertex.glsl";
			actionBtn.shaderFragment = "button_fragment.glsl";
			actionBtn.runtimeOnly   = true;
			actionBtn.padding       = EditorTheme::Scaled(Vec2{ 4.0f, 0.0f });

			const size_t idx = i;
			actionBtn.onClicked = [this, idx]()
			{
				auto actions = getInputActionNames();
				if (actions.empty()) return;
				auto& bindings = m_state.mappingData["bindings"];
				if (idx >= bindings.size()) return;
				std::string current = bindings[idx].value("action", "");
				auto it = std::find(actions.begin(), actions.end(), current);
				size_t next = 0;
				if (it != actions.end())
					next = (std::distance(actions.begin(), it) + 1) % actions.size();
				bindings[idx]["action"] = actions[next];
				m_state.isDirty = true;
				refresh();
			};
			row.children.push_back(std::move(actionBtn));
		}

		// Key name label (clickable to rebind via dropdown)
		{
			WidgetElement keyBtn{};
			keyBtn.id            = rowId + ".Key";
			keyBtn.type          = WidgetElementType::Button;
			keyBtn.text          = keyName;
			keyBtn.font          = theme.fontDefault;
			keyBtn.fontSize      = theme.fontSizeBody;
			keyBtn.textAlignH    = TextAlignH::Left;
			keyBtn.textAlignV    = TextAlignV::Center;
			keyBtn.style.textColor = theme.textPrimary;
			keyBtn.style.color     = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			keyBtn.style.hoverColor = theme.buttonSubtleHover;
			keyBtn.minSize       = EditorTheme::Scaled(Vec2{ 150.0f, 22.0f });
			keyBtn.hitTestMode   = HitTestMode::Enabled;
			keyBtn.shaderVertex  = "button_vertex.glsl";
			keyBtn.shaderFragment = "button_fragment.glsl";
			keyBtn.runtimeOnly   = true;
			keyBtn.padding       = EditorTheme::Scaled(Vec2{ 4.0f, 0.0f });

			const size_t idx = i;
			keyBtn.onClicked = [this, idx]()
			{
				// Show a dropdown of common keys
				std::vector<UIManager::DropdownMenuItem> items;
				struct KeyEntry { const char* label; SDL_Keycode code; };
				const KeyEntry commonKeys[] = {
					{ "A", SDLK_A }, { "B", SDLK_B }, { "C", SDLK_C }, { "D", SDLK_D },
					{ "E", SDLK_E }, { "F", SDLK_F }, { "G", SDLK_G }, { "H", SDLK_H },
					{ "I", SDLK_I }, { "J", SDLK_J }, { "K", SDLK_K }, { "L", SDLK_L },
					{ "M", SDLK_M }, { "N", SDLK_N }, { "O", SDLK_O }, { "P", SDLK_P },
					{ "Q", SDLK_Q }, { "R", SDLK_R }, { "S", SDLK_S }, { "T", SDLK_T },
					{ "U", SDLK_U }, { "V", SDLK_V }, { "W", SDLK_W }, { "X", SDLK_X },
					{ "Y", SDLK_Y }, { "Z", SDLK_Z },
					{ "Space", SDLK_SPACE }, { "Return", SDLK_RETURN }, { "Escape", SDLK_ESCAPE },
					{ "Tab", SDLK_TAB }, { "Backspace", SDLK_BACKSPACE },
					{ "Up", SDLK_UP }, { "Down", SDLK_DOWN }, { "Left", SDLK_LEFT }, { "Right", SDLK_RIGHT },
					{ "F1", SDLK_F1 }, { "F2", SDLK_F2 }, { "F3", SDLK_F3 }, { "F4", SDLK_F4 },
					{ "F5", SDLK_F5 }, { "F6", SDLK_F6 }, { "F7", SDLK_F7 }, { "F8", SDLK_F8 },
					{ "0", SDLK_0 }, { "1", SDLK_1 }, { "2", SDLK_2 }, { "3", SDLK_3 },
					{ "4", SDLK_4 }, { "5", SDLK_5 }, { "6", SDLK_6 }, { "7", SDLK_7 },
					{ "8", SDLK_8 }, { "9", SDLK_9 },
				};
				for (const auto& k : commonKeys)
				{
					const SDL_Keycode kc = k.code;
					items.push_back({ k.label, [this, idx, kc]()
					{
						auto& bindings = m_state.mappingData["bindings"];
						if (idx < bindings.size())
						{
							bindings[idx]["key"] = static_cast<uint32_t>(kc);
							m_state.isDirty = true;
							refresh();
						}
					}});
				}
				m_ui->showDropdownMenu(Vec2{ 0.0f, 0.0f }, items, EditorTheme::Scaled(120.0f));
			};
			row.children.push_back(std::move(keyBtn));
		}

		// Remove button
		{
			WidgetElement removeBtn{};
			removeBtn.id            = rowId + ".Remove";
			removeBtn.type          = WidgetElementType::Button;
			removeBtn.text          = "X";
			removeBtn.font          = theme.fontDefault;
			removeBtn.fontSize      = theme.fontSizeCaption;
			removeBtn.textAlignH    = TextAlignH::Center;
			removeBtn.textAlignV    = TextAlignV::Center;
			removeBtn.style.textColor = Vec4{ 1.0f, 0.3f, 0.3f, 1.0f };
			removeBtn.style.color     = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			removeBtn.style.hoverColor = Vec4{ 0.3f, 0.1f, 0.1f, 1.0f };
			removeBtn.minSize       = EditorTheme::Scaled(Vec2{ 24.0f, 22.0f });
			removeBtn.hitTestMode   = HitTestMode::Enabled;
			removeBtn.shaderVertex  = "button_vertex.glsl";
			removeBtn.shaderFragment = "button_fragment.glsl";
			removeBtn.runtimeOnly   = true;

			const size_t idx = i;
			removeBtn.onClicked = [this, idx]()
			{
				auto& bindings = m_state.mappingData["bindings"];
				if (idx < bindings.size())
				{
					bindings.erase(idx);
					m_state.isDirty = true;
					refresh();
				}
			};
			row.children.push_back(std::move(removeBtn));
		}

		panel.children.push_back(std::move(row));
	}

	root.children.push_back(std::move(panel));
}
