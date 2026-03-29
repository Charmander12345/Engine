#pragma once
#include <string>
#include <unordered_set>
#include <memory>

class UIManager;
class Renderer;
class EditorWidget;
struct Vec2;

class ContentBrowserPanel
{
public:
	ContentBrowserPanel(UIManager* uiManager, Renderer* renderer)
		: m_uiManager(uiManager), m_renderer(renderer) {}
	~ContentBrowserPanel() = default;

	void refresh(const std::string& subfolder = "");
	void focusSearch();
	void populateWidget(const std::shared_ptr<EditorWidget>& widget);
	std::unordered_set<std::string> buildReferencedAssetSet() const;
	void navigateByArrow(int dCol, int dRow);
	bool isOverGrid(const Vec2& screenPos) const;

	// Accessors for UIManager cross-references (handleKeyDown, etc.)
	const std::string& getSelectedBrowserFolder() const { return m_selectedBrowserFolder; }
	const std::string& getSelectedGridAsset() const { return m_selectedGridAsset; }
	void clearSelectedGridAsset() { m_selectedGridAsset.clear(); }
	bool isRenamingGridAsset() const { return m_renamingGridAsset; }
	void cancelRename();
	void startRename();
	bool registryWasReady() const { return m_registryWasReady; }
	void setRegistryWasReady(bool v) { m_registryWasReady = v; }

private:
	UIManager* m_uiManager{ nullptr };
	Renderer* m_renderer{ nullptr };

	std::string m_contentBrowserPath;
	std::string m_selectedBrowserFolder;
	std::string m_selectedGridAsset;
	std::unordered_set<std::string> m_expandedFolders;
	std::string m_browserSearchText;
	uint16_t m_browserTypeFilter{ 0xFFFF };
	bool m_registryWasReady{ false };
	bool m_renamingGridAsset{ false };
	std::string m_renameOriginalPath;
};
