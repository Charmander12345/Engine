#include "ShaderViewerTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Logger/Logger.h"

#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <cstdio>

// ───────────────────────────────────────────────────────────────────────────
ShaderViewerTab::ShaderViewerTab(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer)
{
}

// ───────────────────────────────────────────────────────────────────────────
const std::string& ShaderViewerTab::getTabId() const
{
    return m_state.tabId;
}

bool ShaderViewerTab::isOpen() const
{
    return m_state.isOpen;
}

// ───────────────────────────────────────────────────────────────────────────
// open  – default (no initial file)
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::open()
{
    open(std::string{});
}

// ───────────────────────────────────────────────────────────────────────────
// open  – with optional initial file selection
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::open(const std::string& initialFile)
{
    if (!m_renderer)
        return;

    const std::string tabId = "ShaderViewer";

    // If already open, just switch to it
    if (m_state.isOpen)
    {
        m_renderer->setActiveTab(tabId);
        m_uiManager->markAllWidgetsDirty();
        return;
    }

    m_renderer->addTab(tabId, "Shader Viewer", true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "ShaderViewer.Main";

    // Clean up any stale registration
    m_uiManager->unregisterWidget(widgetId);

    // Initialise state
    m_state = {};
    m_state.tabId    = tabId;
    m_state.widgetId = widgetId;
    m_state.isOpen   = true;

    // Scan the shaders directory for .glsl files
    {
        const auto shadersDir = std::filesystem::current_path() / "shaders";
        if (std::filesystem::exists(shadersDir) && std::filesystem::is_directory(shadersDir))
        {
            for (const auto& entry : std::filesystem::directory_iterator(shadersDir))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".glsl")
                    m_state.shaderFiles.push_back(entry.path().filename().string());
            }
            std::sort(m_state.shaderFiles.begin(), m_state.shaderFiles.end());
        }

        // Select the first file by default (or the specified initial file)
        if (!initialFile.empty())
            m_state.selectedFile = initialFile;
        else if (!m_state.shaderFiles.empty())
            m_state.selectedFile = m_state.shaderFiles[0];
    }

    // Build the main widget (fills entire tab area)
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
        root.id          = "ShaderViewer.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // ── Toolbar row ──────────────────────────────────────────────────
        buildToolbar(root);

        // ── Separator ────────────────────────────────────────────────────
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // ── Content area (horizontal split: file list | code view) ──────
        {
            WidgetElement content{};
            content.id          = "ShaderViewer.Content";
            content.type        = WidgetElementType::StackPanel;
            content.fillX       = true;
            content.fillY       = true;
            content.orientation = StackOrientation::Horizontal;
            content.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
            content.runtimeOnly = true;

            // Left panel: file list
            {
                WidgetElement fileListPanel{};
                fileListPanel.id          = "ShaderViewer.FileListPanel";
                fileListPanel.type        = WidgetElementType::StackPanel;
                fileListPanel.fillY       = true;
                fileListPanel.scrollable  = true;
                fileListPanel.orientation = StackOrientation::Vertical;
                fileListPanel.minSize     = EditorTheme::Scaled(Vec2{ 180.0f, 0.0f });
                fileListPanel.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
                fileListPanel.style.color = Vec4{ 0.06f, 0.06f, 0.08f, 1.0f };
                fileListPanel.runtimeOnly = true;
                content.children.push_back(std::move(fileListPanel));
            }

            // Vertical separator
            {
                WidgetElement sep{};
                sep.type        = WidgetElementType::Panel;
                sep.fillY       = true;
                sep.minSize     = EditorTheme::Scaled(Vec2{ 1.0f, 0.0f });
                sep.style.color = theme.panelBorder;
                sep.runtimeOnly = true;
                content.children.push_back(std::move(sep));
            }

            // Right panel: code view
            {
                WidgetElement codePanel{};
                codePanel.id          = "ShaderViewer.CodePanel";
                codePanel.type        = WidgetElementType::StackPanel;
                codePanel.fillX       = true;
                codePanel.fillY       = true;
                codePanel.scrollable  = true;
                codePanel.orientation = StackOrientation::Vertical;
                codePanel.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 6.0f });
                codePanel.style.color = Vec4{ 0.08f, 0.09f, 0.11f, 1.0f };
                codePanel.runtimeOnly = true;
                content.children.push_back(std::move(codePanel));
            }

            root.children.push_back(std::move(content));
        }

        widget->setElements({ std::move(root) });
        m_uiManager->registerWidget(widgetId, widget, tabId);
    }

    // ── Tab / close click events ─────────────────────────────────────────
    const std::string tabBtnId   = "TitleBar.Tab." + tabId;
    const std::string closeBtnId = "TitleBar.TabClose." + tabId;

    m_uiManager->registerClickEvent(tabBtnId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        refresh();
    });

    m_uiManager->registerClickEvent(closeBtnId, [this]()
    {
        close();
    });

    // ── Toolbar button events ────────────────────────────────────────────
    m_uiManager->registerClickEvent("ShaderViewer.Reload", [this]()
    {
        if (m_renderer)
        {
            m_renderer->requestShaderReload();
            m_uiManager->showToastMessage("Shaders reloaded", UIManager::kToastShort);
        }
    });

    // ── File list click events ───────────────────────────────────────────
    for (const auto& filename : m_state.shaderFiles)
    {
        const std::string eventId = "ShaderViewer.File." + filename;
        m_uiManager->registerClickEvent(eventId, [this, filename]()
        {
            m_state.selectedFile = filename;
            refresh();
        });
    }

    // Initial population
    refresh();
}

// ───────────────────────────────────────────────────────────────────────────
// close
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    const std::string tabId = m_state.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_uiManager->unregisterWidget(m_state.widgetId);

    m_renderer->removeTab(tabId);
    m_state = {};
    m_uiManager->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
// buildToolbar
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "ShaderViewer.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 32.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    // Title label
    {
        WidgetElement title{};
        title.type            = WidgetElementType::Text;
        title.text            = "Shader Viewer";
        title.font            = theme.fontDefault;
        title.fontSize        = theme.fontSizeSubheading;
        title.style.textColor = theme.textPrimary;
        title.textAlignH      = TextAlignH::Left;
        title.textAlignV      = TextAlignV::Center;
        title.minSize         = EditorTheme::Scaled(Vec2{ 100.0f, 24.0f });
        title.runtimeOnly     = true;
        toolbar.children.push_back(std::move(title));
    }

    // Current file label
    {
        WidgetElement fileLabel{};
        fileLabel.id            = "ShaderViewer.CurrentFile";
        fileLabel.type          = WidgetElementType::Text;
        fileLabel.text          = m_state.selectedFile.empty()
                                  ? "(no file selected)"
                                  : m_state.selectedFile;
        fileLabel.font          = theme.fontDefault;
        fileLabel.fontSize      = theme.fontSizeSmall;
        fileLabel.style.textColor = theme.accent;
        fileLabel.textAlignH    = TextAlignH::Left;
        fileLabel.textAlignV    = TextAlignV::Center;
        fileLabel.minSize       = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
        fileLabel.runtimeOnly   = true;
        toolbar.children.push_back(std::move(fileLabel));
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

    // Reload button
    {
        WidgetElement btn{};
        btn.id              = "ShaderViewer.Reload";
        btn.type            = WidgetElementType::Button;
        btn.text            = "Reload Shaders";
        btn.font            = theme.fontDefault;
        btn.fontSize        = theme.fontSizeSmall;
        btn.style.textColor = theme.textPrimary;
        btn.style.color     = theme.accent;
        btn.style.hoverColor = theme.buttonHover;
        btn.textAlignH      = TextAlignH::Center;
        btn.textAlignV      = TextAlignV::Center;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
        btn.padding         = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
        btn.hitTestMode     = HitTestMode::Enabled;
        btn.runtimeOnly     = true;
        btn.clickEvent      = "ShaderViewer.Reload";
        toolbar.children.push_back(std::move(btn));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// buildFileList – populates the left panel with clickable file names
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::buildFileList(WidgetElement& fileListPanel)
{
    const auto& theme = EditorTheme::Get();
    const float rowH = EditorTheme::Scaled(22.0f);

    // Section heading
    {
        WidgetElement heading{};
        heading.type            = WidgetElementType::Text;
        heading.text            = "Shader Files";
        heading.font            = theme.fontDefault;
        heading.fontSize        = theme.fontSizeSmall;
        heading.style.textColor = theme.textMuted;
        heading.textAlignH      = TextAlignH::Left;
        heading.textAlignV      = TextAlignV::Center;
        heading.fillX           = true;
        heading.minSize         = Vec2{ 0.0f, rowH };
        heading.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 2.0f });
        heading.runtimeOnly     = true;
        fileListPanel.children.push_back(std::move(heading));
    }

    for (const auto& filename : m_state.shaderFiles)
    {
        const bool selected = (filename == m_state.selectedFile);

        WidgetElement row{};
        row.id              = "ShaderViewer.File." + filename;
        row.type            = WidgetElementType::Button;
        row.text            = filename;
        row.font            = theme.fontDefault;
        row.fontSize        = theme.fontSizeSmall;
        row.style.textColor = selected ? theme.textPrimary : theme.textSecondary;
        row.style.color     = selected ? theme.selectionHighlight : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        row.style.hoverColor = theme.treeRowHover;
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.fillX           = true;
        row.minSize         = Vec2{ 0.0f, rowH };
        row.padding         = EditorTheme::Scaled(Vec2{ 6.0f, 1.0f });
        row.hitTestMode     = HitTestMode::Enabled;
        row.runtimeOnly     = true;
        row.clickEvent      = "ShaderViewer.File." + filename;
        fileListPanel.children.push_back(std::move(row));
    }
}

// ───────────────────────────────────────────────────────────────────────────
// buildCodeView – reads the selected shader file and displays it with
//                 basic GLSL syntax highlighting
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::buildCodeView(WidgetElement& codePanel)
{
    const auto& theme = EditorTheme::Get();

    if (m_state.selectedFile.empty())
    {
        WidgetElement hint = EditorUIBuilder::makeSecondaryLabel("Select a shader file from the list.");
        codePanel.children.push_back(std::move(hint));
        return;
    }

    // Read the shader file
    const auto filePath = std::filesystem::current_path() / "shaders" / m_state.selectedFile;
    std::ifstream ifs(filePath);
    if (!ifs.is_open())
    {
        WidgetElement err{};
        err.type            = WidgetElementType::Text;
        err.text            = "Failed to open: " + m_state.selectedFile;
        err.font            = theme.fontDefault;
        err.fontSize        = theme.fontSizeSmall;
        err.style.textColor = theme.errorColor;
        err.textAlignH      = TextAlignH::Left;
        err.textAlignV      = TextAlignV::Center;
        err.fillX           = true;
        err.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        err.runtimeOnly     = true;
        codePanel.children.push_back(std::move(err));
        return;
    }

    // GLSL syntax-highlighting colour definitions
    const Vec4 colKeyword   { 0.40f, 0.60f, 1.00f, 1.0f };  // blue – keywords
    const Vec4 colType      { 0.30f, 0.80f, 0.55f, 1.0f };  // green – types
    const Vec4 colPreproc   { 0.70f, 0.45f, 0.85f, 1.0f };  // purple – preprocessor
    const Vec4 colComment   { 0.45f, 0.50f, 0.55f, 1.0f };  // gray – comments
    const Vec4 colNumber    { 0.85f, 0.65f, 0.30f, 1.0f };  // orange – numeric literals
    const Vec4 colString    { 0.85f, 0.55f, 0.40f, 1.0f };  // coral – string literals (rare in GLSL)
    const Vec4 colQualifier { 0.90f, 0.75f, 0.30f, 1.0f };  // gold – uniform/in/out qualifiers
    const Vec4 colDefault   = theme.textSecondary;            // default text

    // Keyword/type sets for classification
    static const std::unordered_set<std::string> keywords = {
        "if", "else", "for", "while", "do", "switch", "case", "default",
        "break", "continue", "return", "discard", "struct", "const",
        "true", "false", "void", "main"
    };
    static const std::unordered_set<std::string> types = {
        "float", "int", "uint", "bool", "double",
        "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
        "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4",
        "dvec2", "dvec3", "dvec4",
        "mat2", "mat3", "mat4", "mat2x2", "mat2x3", "mat2x4",
        "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4",
        "sampler1D", "sampler2D", "sampler3D", "samplerCube",
        "sampler2DShadow", "sampler2DArray", "samplerCubeShadow"
    };
    static const std::unordered_set<std::string> qualifiers = {
        "uniform", "in", "out", "inout", "attribute", "varying",
        "layout", "flat", "smooth", "noperspective",
        "highp", "mediump", "lowp", "precision"
    };

    // Helper: classify a token and return its colour
    auto tokenColor = [&](const std::string& token) -> Vec4
    {
        if (keywords.count(token))   return colKeyword;
        if (types.count(token))      return colType;
        if (qualifiers.count(token)) return colQualifier;
        return colDefault;
    };

    const float lineH = EditorTheme::Scaled(16.0f);
    int lineNumber = 0;
    bool inBlockComment = false;

    std::string line;
    while (std::getline(ifs, line))
    {
        ++lineNumber;

        // Determine dominant colour for the line (simplified approach:
        // one colour per line based on first significant token).
        Vec4 lineColor = colDefault;

        std::string trimmed = line;
        // ltrim
        size_t firstNonSpace = trimmed.find_first_not_of(" \t\r\n");
        if (firstNonSpace != std::string::npos)
            trimmed = trimmed.substr(firstNonSpace);
        else
            trimmed.clear();

        if (inBlockComment)
        {
            lineColor = colComment;
            if (trimmed.find("*/") != std::string::npos)
                inBlockComment = false;
        }
        else if (trimmed.empty())
        {
            lineColor = colDefault;
        }
        else if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/')
        {
            lineColor = colComment;
        }
        else if (trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '*')
        {
            lineColor = colComment;
            if (trimmed.find("*/") == std::string::npos)
                inBlockComment = true;
        }
        else if (trimmed[0] == '#')
        {
            lineColor = colPreproc;
        }
        else
        {
            // Extract the first identifier token
            std::string firstToken;
            for (size_t i = 0; i < trimmed.size(); ++i)
            {
                char c = trimmed[i];
                if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
                    firstToken += c;
                else
                    break;
            }
            if (!firstToken.empty())
                lineColor = tokenColor(firstToken);
        }

        // Build line number prefix
        char lineNumBuf[12];
        std::snprintf(lineNumBuf, sizeof(lineNumBuf), "%4d  ", lineNumber);

        WidgetElement row{};
        row.id              = "ShaderViewer.Line." + std::to_string(lineNumber);
        row.type            = WidgetElementType::Text;
        row.text            = std::string(lineNumBuf) + line;
        row.font            = theme.fontDefault;
        row.fontSize        = theme.fontSizeMonospace;
        row.style.textColor = lineColor;
        row.textAlignH      = TextAlignH::Left;
        row.textAlignV      = TextAlignV::Center;
        row.fillX           = true;
        row.minSize         = Vec2{ 0.0f, lineH };
        row.runtimeOnly     = true;

        codePanel.children.push_back(std::move(row));
    }

    // File info footer
    {
        WidgetElement footer{};
        footer.type            = WidgetElementType::Text;
        footer.text            = "── " + std::to_string(lineNumber) + " lines ──";
        footer.font            = theme.fontDefault;
        footer.fontSize        = theme.fontSizeCaption;
        footer.style.textColor = theme.textMuted;
        footer.textAlignH      = TextAlignH::Left;
        footer.textAlignV      = TextAlignV::Center;
        footer.fillX           = true;
        footer.minSize         = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        footer.padding         = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
        footer.runtimeOnly     = true;
        codePanel.children.push_back(std::move(footer));
    }
}

// ───────────────────────────────────────────────────────────────────────────
// refresh – rebuilds file list + code view
// ───────────────────────────────────────────────────────────────────────────
void ShaderViewerTab::refresh()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_uiManager->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    // Find the content area (horizontal StackPanel)
    WidgetElement* content = nullptr;
    for (auto& child : elements[0].children)
    {
        if (child.id == "ShaderViewer.Content")
        {
            content = &child;
            break;
        }
    }
    if (!content || content->children.size() < 3)
        return;

    // Also rebuild toolbar to update current file label
    {
        WidgetElement* toolbar = nullptr;
        for (auto& child : elements[0].children)
        {
            if (child.id == "ShaderViewer.Toolbar")
            {
                toolbar = &child;
                break;
            }
        }
        if (toolbar)
        {
            toolbar->children.clear();

            const auto& theme = EditorTheme::Get();

            // Title label
            {
                WidgetElement title{};
                title.type            = WidgetElementType::Text;
                title.text            = "Shader Viewer";
                title.font            = theme.fontDefault;
                title.fontSize        = theme.fontSizeSubheading;
                title.style.textColor = theme.textPrimary;
                title.textAlignH      = TextAlignH::Left;
                title.textAlignV      = TextAlignV::Center;
                title.minSize         = EditorTheme::Scaled(Vec2{ 100.0f, 24.0f });
                title.runtimeOnly     = true;
                toolbar->children.push_back(std::move(title));
            }

            // Current file label
            {
                WidgetElement fileLabel{};
                fileLabel.id            = "ShaderViewer.CurrentFile";
                fileLabel.type          = WidgetElementType::Text;
                fileLabel.text          = m_state.selectedFile.empty()
                                          ? "(no file selected)"
                                          : m_state.selectedFile;
                fileLabel.font          = theme.fontDefault;
                fileLabel.fontSize      = theme.fontSizeSmall;
                fileLabel.style.textColor = theme.accent;
                fileLabel.textAlignH    = TextAlignH::Left;
                fileLabel.textAlignV    = TextAlignV::Center;
                fileLabel.minSize       = EditorTheme::Scaled(Vec2{ 160.0f, 24.0f });
                fileLabel.runtimeOnly   = true;
                toolbar->children.push_back(std::move(fileLabel));
            }

            // Spacer
            {
                WidgetElement spacer{};
                spacer.type        = WidgetElementType::Panel;
                spacer.fillX       = true;
                spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
                spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                spacer.runtimeOnly = true;
                toolbar->children.push_back(std::move(spacer));
            }

            // Reload button
            {
                WidgetElement btn{};
                btn.id              = "ShaderViewer.Reload";
                btn.type            = WidgetElementType::Button;
                btn.text            = "Reload Shaders";
                btn.font            = theme.fontDefault;
                btn.fontSize        = theme.fontSizeSmall;
                btn.style.textColor = theme.textPrimary;
                btn.style.color     = theme.accent;
                btn.style.hoverColor = theme.buttonHover;
                btn.textAlignH      = TextAlignH::Center;
                btn.textAlignV      = TextAlignV::Center;
                btn.minSize         = EditorTheme::Scaled(Vec2{ 110.0f, 24.0f });
                btn.padding         = EditorTheme::Scaled(Vec2{ 8.0f, 2.0f });
                btn.hitTestMode     = HitTestMode::Enabled;
                btn.runtimeOnly     = true;
                btn.clickEvent      = "ShaderViewer.Reload";
                toolbar->children.push_back(std::move(btn));
            }
        }
    }

    // Rebuild file list (children[0])
    content->children[0].children.clear();
    buildFileList(content->children[0]);

    // Rebuild code view (children[2], after the separator at children[1])
    content->children[2].children.clear();
    buildCodeView(content->children[2]);

    m_uiManager->markAllWidgetsDirty();
}
