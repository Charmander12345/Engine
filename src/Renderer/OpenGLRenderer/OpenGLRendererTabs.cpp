// OpenGLRendererTabs.cpp – Editor tab management methods split from OpenGLRenderer.cpp
// Contains: addTab, removeTab, rebuildTitleBarTabs, setActiveTab,
//           getActiveTabId, getTabs, releaseAllTabFbos

#include "OpenGLRenderer.h"
#include <algorithm>

#include "../../Diagnostics/DiagnosticsManager.h"
#include "../EditorTheme.h"
#include "../EditorTabs/ActorEditorTab.h"

// ─── Editor tab system ──────────────────────────────────────────────────────
#if ENGINE_EDITOR
void OpenGLRenderer::addTab(const std::string& id, const std::string& tabName, bool closable)
{
	for (const auto& tab : m_editorTabs)
	{
		if (tab.id == id)
		{
			return;
		}
	}
	EditorTab tab;
	tab.id = id;
	tab.name = tabName;
	tab.closable = closable;
	tab.active = m_editorTabs.empty();
	if (tab.active)
	{
		m_activeTabId = id;
	}
	m_editorTabs.push_back(std::move(tab));

	rebuildTitleBarTabs();
}

void OpenGLRenderer::removeTab(const std::string& id)
{
	auto it = std::find_if(m_editorTabs.begin(), m_editorTabs.end(),
		[&](const EditorTab& tab) { return tab.id == id; });
	if (it == m_editorTabs.end() || !it->closable)
	{
		return;
	}
	const bool wasActive = it->active;
	it->renderTarget.reset();
	m_editorTabs.erase(it);
	if (wasActive && !m_editorTabs.empty())
	{
		m_editorTabs.front().active = true;
		m_activeTabId = m_editorTabs.front().id;
	}

	rebuildTitleBarTabs();
}

void OpenGLRenderer::rebuildTitleBarTabs()
{
	auto* tabsStack = m_uiManager.findElementById("TitleBar.Tabs");
	if (!tabsStack)
	{
		return;
	}

	const auto isDynamicTabButton = [](const WidgetElement& el)
	{
		if (!el.runtimeOnly)
			return false;
		const bool isTab = el.id.rfind("TitleBar.Tab.", 0) == 0;
		const bool isClose = el.id.rfind("TitleBar.TabClose.", 0) == 0;
		if (!isTab && !isClose)
			return false;
		if (el.id == "TitleBar.Tab.Viewport" || el.id == "TitleBar.TabClose.Viewport")
			return false;
		return true;
	};

	tabsStack->children.erase(
		std::remove_if(tabsStack->children.begin(), tabsStack->children.end(), isDynamicTabButton),
		tabsStack->children.end());

	for (const auto& tab : m_editorTabs)
	{
		if (tab.id == "Viewport")
		{
			continue;
		}

		const std::string tabBtnId = "TitleBar.Tab." + tab.id;
		WidgetElement tabBtn{};
		tabBtn.type = WidgetElementType::Button;
		tabBtn.id = tabBtnId;
		tabBtn.clickEvent = tabBtnId;
		tabBtn.text = tab.name;
		tabBtn.font = EditorTheme::Get().fontDefault;
		tabBtn.fontSize = EditorTheme::Get().fontSizeSmall;
		tabBtn.textAlignH = TextAlignH::Center;
		tabBtn.textAlignV = TextAlignV::Center;
		tabBtn.fillY = true;
		tabBtn.style.color = EditorTheme::Get().panelBackground;
		tabBtn.style.hoverColor = EditorTheme::Get().buttonHover;
		tabBtn.style.textColor = EditorTheme::Get().textPrimary;
		tabBtn.minSize = Vec2{ EditorTheme::Scaled(90.0f), 0.0f };
		tabBtn.padding = Vec2{ 6.0f, 0.0f };
		tabBtn.hitTestMode = HitTestMode::Enabled;
		tabBtn.runtimeOnly = true;
		tabsStack->children.push_back(std::move(tabBtn));

		if (tab.closable)
		{
			const std::string closeBtnId = "TitleBar.TabClose." + tab.id;
			WidgetElement closeBtn{};
			closeBtn.type = WidgetElementType::Button;
			closeBtn.id = closeBtnId;
			closeBtn.clickEvent = closeBtnId;
			closeBtn.text = "x";
			closeBtn.font = EditorTheme::Get().fontDefault;
			closeBtn.fontSize = EditorTheme::Get().fontSizeSmall;
			closeBtn.textAlignH = TextAlignH::Center;
			closeBtn.textAlignV = TextAlignV::Center;
			closeBtn.fillY = true;
			closeBtn.style.color = EditorTheme::Get().panelBackground;
			closeBtn.style.hoverColor = EditorTheme::Get().buttonDangerHover;
			closeBtn.style.textColor = EditorTheme::Get().textMuted;
			closeBtn.minSize = Vec2{ EditorTheme::Scaled(24.0f), 0.0f };
			closeBtn.padding = Vec2{ 2.0f, 0.0f };
			closeBtn.hitTestMode = HitTestMode::Enabled;
			closeBtn.runtimeOnly = true;
			tabsStack->children.push_back(std::move(closeBtn));
		}
	}

	m_uiManager.markAllWidgetsDirty();
}

void OpenGLRenderer::setActiveTab(const std::string& id)
{
	// Block tab switching during PIE
	if (DiagnosticsManager::Instance().isPIEActive())
	{
		return;
	}

	if (m_activeTabId == id)
	{
		return;
	}

	auto& diag = DiagnosticsManager::Instance();
	const std::string oldTabId = m_activeTabId;

	// --- Level swap: leaving a viewer/editor tab → give level back ---
	if (oldTabId != "Viewport")
	{
		// Save per-tab entity selection
		m_tabSelectedEntities[oldTabId] = m_selectedEntities;

		// Determine which viewer/editor owns this tab
		auto meshIt = m_meshViewers.find(oldTabId);
		auto matIt  = m_materialEditors.find(oldTabId);
		const bool isMeshViewer = (meshIt != m_meshViewers.end() && meshIt->second);
		const bool isMatEditor  = (matIt  != m_materialEditors.end() && matIt->second);
		auto* actorTab = m_uiManager.getActorEditorTab();
		const bool isActorEditor = (actorTab && actorTab->isOpen() && actorTab->getTabId() == oldTabId);

		if (isMeshViewer || isMatEditor || isActorEditor)
		{
			// Save camera state into the viewer's runtime level before swapping out
			auto returnedLevel = diag.swapActiveLevel(std::move(m_savedViewportLevel));
			if (returnedLevel)
			{
				if (m_camera)
				{
					returnedLevel->setEditorCameraPosition(m_camera->getPosition());
					returnedLevel->setEditorCameraRotation(m_camera->getRotationDegrees());
					returnedLevel->setHasEditorCamera(true);
				}
				returnedLevel->resetPreparedState();
				if (isMeshViewer)
					meshIt->second->giveRuntimeLevel(std::move(returnedLevel));
				else if (isMatEditor)
					matIt->second->giveRuntimeLevel(std::move(returnedLevel));
				else
					actorTab->giveRuntimeLevel(std::move(returnedLevel));
			}
		}
	}
	else
	{
		// Leaving Viewport: save the editor camera state and selection
		m_savedViewportSelectedEntities = m_selectedEntities;
		if (m_camera)
		{
			m_savedCameraPos = m_camera->getPosition();
			m_savedCameraRot = m_camera->getRotationDegrees();
		}
	}

	// Clear selection before switching tabs
	m_selectedEntities.clear();
	m_uiManager.selectEntity(0);

	// --- Level swap: entering a viewer/editor tab → swap in its level ---
	if (id != "Viewport")
	{
		std::unique_ptr<EngineLevel> incomingLevel;

		auto meshIt = m_meshViewers.find(id);
		auto matIt  = m_materialEditors.find(id);
		auto* actorTab = m_uiManager.getActorEditorTab();

		if (meshIt != m_meshViewers.end() && meshIt->second)
			incomingLevel = meshIt->second->takeRuntimeLevel();
		else if (matIt != m_materialEditors.end() && matIt->second)
			incomingLevel = matIt->second->takeRuntimeLevel();
		else if (actorTab && actorTab->isOpen() && actorTab->getTabId() == id)
			incomingLevel = actorTab->takeRuntimeLevel();

		if (incomingLevel)
		{
			incomingLevel->resetPreparedState();
			m_savedViewportLevel = diag.swapActiveLevel(std::move(incomingLevel));
			if (m_savedViewportLevel)
			{
				m_savedViewportLevel->resetPreparedState();
			}
			diag.setScenePrepared(false);
		}

		// Restore per-tab entity selection
		auto selIt = m_tabSelectedEntities.find(id);
		if (selIt != m_tabSelectedEntities.end())
		{
			m_selectedEntities = selIt->second;
			m_uiManager.selectEntity(m_selectedEntities.empty() ? 0u : *m_selectedEntities.begin());
		}
	}
	else
	{
		// Returning to Viewport: restore the saved level
		if (m_savedViewportLevel)
		{
			m_savedViewportLevel->resetPreparedState();
			auto old = diag.swapActiveLevel(std::move(m_savedViewportLevel));
			if (old)
			{
				old->resetPreparedState();
				auto meshIt = m_meshViewers.find(oldTabId);
				auto matIt  = m_materialEditors.find(oldTabId);
				auto* actorTab = m_uiManager.getActorEditorTab();
				if (meshIt != m_meshViewers.end() && meshIt->second)
					meshIt->second->giveRuntimeLevel(std::move(old));
				else if (matIt != m_materialEditors.end() && matIt->second)
					matIt->second->giveRuntimeLevel(std::move(old));
				else if (actorTab && actorTab->isOpen() && actorTab->getTabId() == oldTabId)
					actorTab->giveRuntimeLevel(std::move(old));
			}
			diag.setScenePrepared(false);
		}
		// Restore editor camera and selection
		if (m_camera)
		{
			m_camera->setPosition(m_savedCameraPos);
			m_camera->setRotationDegrees(m_savedCameraRot.x, m_savedCameraRot.y);
		}
		m_selectedEntities = m_savedViewportSelectedEntities;
		m_uiManager.selectEntity(m_selectedEntities.empty() ? 0u : *m_selectedEntities.begin());
	}

	// Snapshot the current active tab so it can be displayed as a cached image later
	for (auto& tab : m_editorTabs)
	{
		if (tab.active && tab.renderTarget && tab.renderTarget->isValid())
		{
			tab.renderTarget->takeSnapshot();
		}
		tab.active = (tab.id == id);
		if (tab.active)
		{
			m_activeTabId = id;
		}
	}

	// Update the UIManager's active tab for widget filtering
	m_uiManager.setActiveTabId(id);
}

const std::string& OpenGLRenderer::getActiveTabId() const
{
	return m_activeTabId;
}

const std::vector<EditorTab>& OpenGLRenderer::getTabs() const
{
	return m_editorTabs;
}

void OpenGLRenderer::releaseAllTabFbos()
{
	for (auto& tab : m_editorTabs)
	{
		tab.renderTarget.reset();
	}
}
#endif // ENGINE_EDITOR
