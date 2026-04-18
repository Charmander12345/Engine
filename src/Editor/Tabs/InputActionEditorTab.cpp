#include "InputActionEditorTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"
#include "../../Core/InputActionManager.h"

#include <filesystem>
#include <fstream>

using json = nlohmann::json;

InputActionEditorTab::InputActionEditorTab(UIManager* uiManager, Renderer* renderer)
	: m_ui(uiManager)
	, m_renderer(renderer)
{}

void InputActionEditorTab::open()
{
	// Default open with no asset — no-op
}

void InputActionEditorTab::open(const std::string& assetPath)
{
	if (!m_renderer || !m_ui)
		return;

	const std::string tabId = "InputActionEditor";

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
		m_ui->showToastMessage("Input Action asset not found: " + assetPath, UIManager::kToastMedium);
		return;
	}

	std::ifstream in(absPath);
	if (!in.is_open())
	{
		m_ui->showToastMessage("Failed to open input action asset.", UIManager::kToastMedium);
		return;
	}

	json fileJson = json::parse(in, nullptr, false);
	in.close();
	if (fileJson.is_discarded() || !fileJson.contains("data"))
	{
		m_ui->showToastMessage("Invalid input action asset format.", UIManager::kToastMedium);
		return;
	}

	m_renderer->addTab(tabId, "Input Action Editor", true);
	m_renderer->setActiveTab(tabId);

	const std::string widgetId = "InputActionEditor.Main";
	m_ui->unregisterWidget(widgetId);

	m_state = {};
	m_state.tabId     = tabId;
	m_state.widgetId  = widgetId;
	m_state.assetPath = assetPath;
	m_state.assetName = fileJson.value("name", std::filesystem::path(assetPath).stem().string());
	m_state.originalAssetName = m_state.assetName;
	m_state.isOpen    = true;
	m_state.isDirty   = false;
	m_state.actionData = fileJson["data"];

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
		root.id          = "InputActionEditor.Root";
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

		buildDetailsPanel(root);

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

	m_ui->registerClickEvent("InputActionEditor.Save", [this]()
	{
		save();
	});

	refresh();
}

void InputActionEditorTab::close()
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

void InputActionEditorTab::update(float deltaSeconds)
{
	if (!m_state.isOpen) return;
	m_state.refreshTimer += deltaSeconds;
	if (m_state.refreshTimer >= 0.5f)
	{
		m_state.refreshTimer = 0.0f;
	}
}

void InputActionEditorTab::save()
{
	if (!m_state.isOpen) return;

	auto& diagnostics = DiagnosticsManager::Instance();
	if (!diagnostics.isProjectLoaded()) return;

	// Rename asset file if name was changed
	if (m_state.assetName != m_state.originalAssetName && !m_state.assetName.empty())
	{
		if (AssetManager::Instance().renameAsset(m_state.assetPath, m_state.assetName))
		{
			const std::filesystem::path oldRel(m_state.assetPath);
			const std::string ext = oldRel.extension().string();
			const std::filesystem::path parentDir = oldRel.parent_path();
			m_state.assetPath = (parentDir / (m_state.assetName + ext)).generic_string();
			m_state.originalAssetName = m_state.assetName;
		}
		else
		{
			m_ui->showToastMessage("Failed to rename input action asset.", UIManager::kToastMedium);
			m_state.assetName = m_state.originalAssetName;
			refresh();
			return;
		}
	}

	const std::filesystem::path contentDir =
		std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
	const std::filesystem::path absPath = contentDir / m_state.assetPath;

	std::error_code ec;
	std::filesystem::create_directories(absPath.parent_path(), ec);

	std::ofstream out(absPath, std::ios::out | std::ios::trunc);
	if (!out.is_open())
	{
		m_ui->showToastMessage("Failed to save input action.", UIManager::kToastMedium);
		return;
	}

	json fileJson = json::object();
	fileJson["magic"]   = 0x41535453;
	fileJson["version"] = 2;
	fileJson["type"]    = static_cast<int>(AssetType::InputAction);
	fileJson["name"]    = m_state.assetName;
	fileJson["data"]    = m_state.actionData;

	out << fileJson.dump(4);
	out.close();

	// Update runtime InputActionManager
	InputActionManager::ActionDef def;
	def.name = m_state.assetName;
	def.requiredMods = 0;
	if (m_state.actionData.value("shift", false)) def.requiredMods |= InputActionManager::ModShift;
	if (m_state.actionData.value("ctrl", false))  def.requiredMods |= InputActionManager::ModCtrl;
	if (m_state.actionData.value("alt", false))   def.requiredMods |= InputActionManager::ModAlt;
	InputActionManager::Instance().addAction(def);

	m_state.isDirty = false;
	m_ui->showToastMessage("Input Action saved.", UIManager::kToastMedium);
}

void InputActionEditorTab::refresh()
{
	if (!m_state.isOpen || !m_ui) return;

	auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
	if (!entry || !entry->widget) return;

	auto& elements = entry->widget->getElementsMutable();
	if (elements.empty()) return;

	auto& root = elements[0];

	// Update toolbar title
	if (auto* titleEl = m_ui->findElementById("InputActionEditor.Title"))
		titleEl->text = "Input Action: " + m_state.assetName;

	// Find details panel and rebuild
	for (auto it = root.children.begin(); it != root.children.end(); ++it)
	{
		if (it->id == "InputActionEditor.DetailsPanel")
		{
			root.children.erase(it);
			break;
		}
	}
	buildDetailsPanel(root);
	entry->widget->markLayoutDirty();
}

void InputActionEditorTab::buildToolbar(WidgetElement& root)
{
	const auto& theme = EditorTheme::Get();

	WidgetElement toolbar{};
	toolbar.id          = "InputActionEditor.Toolbar";
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
		title.id            = "InputActionEditor.Title";
		title.type          = WidgetElementType::Text;
		title.text          = "Input Action: " + m_state.assetName;
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

	// Save button
	{
		WidgetElement saveBtn{};
		saveBtn.id            = "InputActionEditor.Save";
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

void InputActionEditorTab::buildDetailsPanel(WidgetElement& root)
{
	const auto& theme = EditorTheme::Get();

	WidgetElement panel{};
	panel.id          = "InputActionEditor.DetailsPanel";
	panel.type        = WidgetElementType::StackPanel;
	panel.fillX       = true;
	panel.fillY       = true;
	panel.orientation = StackOrientation::Vertical;
	panel.padding     = EditorTheme::Scaled(Vec2{ 16.0f, 12.0f });
	panel.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
	panel.runtimeOnly = true;

	// Action Name (editable)
	{
		WidgetElement nameRow = EditorUIBuilder::makeStringRow(
			"InputActionEditor.Name", "Action Name", m_state.assetName,
			[this](const std::string& newName)
			{
				if (!newName.empty() && newName != m_state.assetName)
				{
					m_state.assetName = newName;
					m_state.isDirty = true;
				}
			});
		panel.children.push_back(std::move(nameRow));
	}

	// Spacer
	{
		WidgetElement sp{};
		sp.type        = WidgetElementType::Panel;
		sp.fillX       = true;
		sp.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 8.0f });
		sp.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
		sp.runtimeOnly = true;
		panel.children.push_back(std::move(sp));
	}

	// Section label
	{
		WidgetElement lbl{};
		lbl.type          = WidgetElementType::Text;
		lbl.text          = "Required Modifiers";
		lbl.font          = theme.fontDefault;
		lbl.fontSize      = theme.fontSizeSubheading;
		lbl.style.textColor = theme.textSecondary;
		lbl.fillX         = true;
		lbl.minSize       = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
		lbl.runtimeOnly   = true;
		panel.children.push_back(std::move(lbl));
	}

	// Modifier checkboxes
	struct ModDef { const char* label; const char* key; };
	const ModDef mods[] = {
		{ "Shift", "shift" },
		{ "Ctrl",  "ctrl"  },
		{ "Alt",   "alt"   },
	};

	for (const auto& mod : mods)
	{
		const bool checked = m_state.actionData.value(mod.key, false);
		const std::string btnId = std::string("InputActionEditor.Mod.") + mod.key;

		WidgetElement row{};
		row.type        = WidgetElementType::StackPanel;
		row.fillX       = true;
		row.orientation = StackOrientation::Horizontal;
		row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 28.0f });
		row.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
		row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
		row.runtimeOnly = true;

		// Checkbox button
		WidgetElement cb{};
		cb.id               = btnId;
		cb.type             = WidgetElementType::Button;
		cb.text             = checked ? "[X]" : "[ ]";
		cb.font             = theme.fontDefault;
		cb.fontSize         = theme.fontSizeBody;
		cb.textAlignH       = TextAlignH::Center;
		cb.textAlignV       = TextAlignV::Center;
		cb.style.textColor  = theme.textPrimary;
		cb.style.color      = checked ? Vec4{ theme.accent.x, theme.accent.y, theme.accent.z, 0.3f } : theme.buttonSubtle;
		cb.style.hoverColor = theme.buttonSubtleHover;
		cb.style.borderRadius = theme.borderRadius;
		cb.minSize          = EditorTheme::Scaled(Vec2{ 32.0f, 22.0f });
		cb.hitTestMode      = HitTestMode::Enabled;
		cb.shaderVertex     = "button_vertex.glsl";
		cb.shaderFragment   = "button_fragment.glsl";
		cb.runtimeOnly      = true;

		const std::string modKey = mod.key;
		cb.onClicked = [this, modKey]()
		{
			bool current = m_state.actionData.value(modKey, false);
			m_state.actionData[modKey] = !current;
			m_state.isDirty = true;
			refresh();
		};
		row.children.push_back(std::move(cb));

		// Label
		WidgetElement lbl{};
		lbl.type          = WidgetElementType::Text;
		lbl.text          = mod.label;
		lbl.font          = theme.fontDefault;
		lbl.fontSize      = theme.fontSizeBody;
		lbl.style.textColor = theme.textPrimary;
		lbl.textAlignV    = TextAlignV::Center;
		lbl.minSize       = EditorTheme::Scaled(Vec2{ 80.0f, 22.0f });
		lbl.padding       = EditorTheme::Scaled(Vec2{ 6.0f, 0.0f });
		lbl.runtimeOnly   = true;
		row.children.push_back(std::move(lbl));

		panel.children.push_back(std::move(row));
	}

	root.children.push_back(std::move(panel));
}
