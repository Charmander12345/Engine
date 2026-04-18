#pragma once

#include "IEditorTab.h"
#include <string>
#include <vector>

class UIManager;
class Renderer;
struct WidgetElement;

// ===========================================================================
// ShaderViewerTab – extracted from UIManager (Section 1.1)
// Displays .glsl shader files with basic syntax highlighting.
// ===========================================================================
class ShaderViewerTab : public IEditorTab
{
public:
    struct State
    {
        std::string tabId;
        std::string widgetId;
        bool        isOpen{ false };
        std::string selectedFile;                // currently viewed shader filename
        std::vector<std::string> shaderFiles;    // cached list of .glsl filenames
    };

    ShaderViewerTab(UIManager* uiManager, Renderer* renderer);
    ~ShaderViewerTab() override = default;

    // IEditorTab interface
    void        open() override;
    void        close() override;
    bool        isOpen() const override;
    void        update(float /*deltaSeconds*/) override {}   // no periodic refresh
    const std::string& getTabId() const override;

    // Shader-specific
    void open(const std::string& initialFile);

private:
    void refresh();
    void buildToolbar(WidgetElement& root);
    void buildFileList(WidgetElement& fileListPanel);
    void buildCodeView(WidgetElement& codePanel);

    UIManager* m_uiManager = nullptr;
    Renderer*  m_renderer  = nullptr;
    State      m_state;
};
