// ---------------------------------------------------------------------------
// AssetManagerEditorWidgets.cpp - Split implementation file for AssetManager
// Contains: Editor widget asset generation, default assets creation
// ---------------------------------------------------------------------------
#include "AssetManager.h"

#if ENGINE_EDITOR

#include <filesystem>
#include <fstream>
#include <cstdint>

#include "AssetTypes.h"

#include "../Renderer/EditorTheme.h"

namespace fs = std::filesystem;

// Duplicated from AssetManager.cpp (file-scope static needed by ensureEditorWidgetsCreated / ensureDefaultAssetsCreated)
static bool readAssetHeaderType(const fs::path& absoluteAssetPath, AssetType& outType)
{
	std::ifstream in(absoluteAssetPath, std::ios::binary);
	if (!in.is_open()) return false;

	char first = 0;
	if (!in.get(first)) return false;
	if (first == '{')
	{
		in.unget();
		json j = json::parse(in, nullptr, false);
		if (j.is_discarded()) return false;
		if (!j.is_object() || !j.contains("type")) return false;
		outType = static_cast<AssetType>(j.at("type").get<int>());
		return true;
	}

	in.unget();
	uint32_t magic = 0;
	uint32_t version = 0;
	if (!in.read(reinterpret_cast<char*>(&magic), sizeof(magic))) return false;
	if (!in.read(reinterpret_cast<char*>(&version), sizeof(version))) return false;
	if (magic != 0x41535453 || version != 2) return false;

	int32_t typeInt = 0;
	if (!in.read(reinterpret_cast<char*>(&typeInt), sizeof(typeInt))) return false;
	outType = static_cast<AssetType>(typeInt);
	return true;
}

static fs::path getEditorWidgetsRootPath()
{
    return fs::current_path() / "Editor" / "Widgets";
}

void AssetManager::ensureEditorWidgetsCreated()
{
    auto& logger = Logger::Instance();

    // Current UI layout version. Bump this to force all editor widget assets to regenerate.
    static constexpr int kEditorWidgetUIVersion = 9;

    // Returns true only when the file on disk contains a matching _uiVersion AND _dpiScale field.
    auto checkUIVersion = [&](const fs::path& abs) -> bool {
        if (!fs::exists(abs)) return false;
        AssetType headerType{ AssetType::Unknown };
        if (!readAssetHeaderType(abs, headerType) || headerType != AssetType::Widget) return false;
        std::ifstream in(abs, std::ios::in | std::ios::binary);
        if (!in.is_open()) return false;
        json fileJson = json::parse(in, nullptr, false);
        if (fileJson.is_discarded() || !fileJson.is_object() || !fileJson.contains("data")) return false;
        const auto& data = fileJson.at("data");
        if (!data.is_object()) return false;
        if (!data.contains("_uiVersion") || !data.at("_uiVersion").is_number_integer() ||
            data.at("_uiVersion").get<int>() < kEditorWidgetUIVersion) return false;
        if (!data.contains("_dpiScale") || !data.at("_dpiScale").is_number()) return false;
        return std::abs(data.at("_dpiScale").get<float>() - EditorTheme::Get().dpiScale) < 0.01f;
    };

    // Writes a widget asset JSON to disk.
    auto writeWidgetAsset = [&](const fs::path& abs, const std::string& name, const json& widgetData) {
        std::ofstream out(abs, std::ios::out | std::ios::trunc);
        if (out.is_open()) {
            json fileJson = json::object();
            fileJson["magic"] = 0x41535453;
            fileJson["version"] = 2;
            fileJson["type"] = static_cast<int>(AssetType::Widget);
            fileJson["name"] = name;
            fileJson["data"] = widgetData;
            out << fileJson.dump(4);
            if (!out.good()) logger.log(Logger::Category::AssetManagement, "Failed to write editor widget asset.", Logger::LogLevel::ERROR);
        } else {
            logger.log(Logger::Category::AssetManagement, "Failed to open editor widget asset for writing.", Logger::LogLevel::ERROR);
        }
    };

    // DPI helpers â€“ scale any base pixel value by the current DPI factor.
    const EditorTheme& t = EditorTheme::Get();
    const float dpi = t.dpiScale;
    auto S = [dpi](float px) -> float { return px * dpi; };

    // Ensure Editor/Textures icons exist
    {
        const fs::path texturesDir = fs::current_path() / "Editor" / "Textures";
        std::error_code ec;
        fs::create_directories(texturesDir, ec);

        auto writeTga = [&](const std::string& name, const std::vector<uint8_t>& rgba, int w, int h)
        {
            const fs::path path = texturesDir / name;
            if (fs::exists(path))
            {
                return;
            }
            std::ofstream out(path, std::ios::binary);
            if (!out.is_open())
            {
                return;
            }
            uint8_t header[18] = {};
            header[2] = 2;
            header[12] = static_cast<uint8_t>(w & 0xFF);
            header[13] = static_cast<uint8_t>((w >> 8) & 0xFF);
            header[14] = static_cast<uint8_t>(h & 0xFF);
            header[15] = static_cast<uint8_t>((h >> 8) & 0xFF);
            header[16] = 32;
            header[17] = 0x28;
            out.write(reinterpret_cast<const char*>(header), 18);
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const int idx = (y * w + x) * 4;
                    const uint8_t bgra[4] = { rgba[idx + 2], rgba[idx + 1], rgba[idx], rgba[idx + 3] };
                    out.write(reinterpret_cast<const char*>(bgra), 4);
                }
            }
        };

        constexpr int sz = 24;
        {
            std::vector<uint8_t> play(sz * sz * 4, 0);
            for (int y = 0; y < sz; ++y)
            {
                const float fy = static_cast<float>(y) / static_cast<float>(sz - 1);
                const float halfH = 0.5f - std::abs(fy - 0.5f);
                const int maxX = static_cast<int>(halfH * 2.0f * static_cast<float>(sz) * 0.85f);
                for (int x = 0; x < sz; ++x)
                {
                    const int idx = (y * sz + x) * 4;
                    if (x >= 4 && x < 4 + maxX)
                    {
                        play[idx] = 220; play[idx + 1] = 220; play[idx + 2] = 220; play[idx + 3] = 255;
                    }
                }
            }
            writeTga("Play.tga", play, sz, sz);
        }
        {
            std::vector<uint8_t> stop(sz * sz * 4, 0);
            for (int y = 0; y < sz; ++y)
            {
                for (int x = 0; x < sz; ++x)
                {
                    const int idx = (y * sz + x) * 4;
                    if (x >= 4 && x < sz - 4 && y >= 4 && y < sz - 4)
                    {
                        stop[idx] = 220; stop[idx + 1] = 100; stop[idx + 2] = 100; stop[idx + 3] = 255;
                    }
                }
            }
            writeTga("Stop.tga", stop, sz, sz);
        }
    }

    const std::string defaultWidgetRel = "TitleBar.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", S(100.0f)} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 0;
        widgetJson["_uiVersion"] = kEditorWidgetUIVersion;
        widgetJson["_dpiScale"] = dpi;

        json elements = json::array();

        // Full-area dark background
        json bgPanel = json::object();
        bgPanel["id"] = "TitleBar.Background";
        bgPanel["type"] = "Panel";
        bgPanel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        bgPanel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        bgPanel["fillX"] = true;
        bgPanel["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        bgPanel["shaderVertex"] = "panel_vertex.glsl";
        bgPanel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(bgPanel);

        // Slightly darker strip behind tab row (bottom 50%)
        json tabRowBg = json::object();
        tabRowBg["id"] = "TitleBar.TabRowBg";
        tabRowBg["type"] = "Panel";
        tabRowBg["from"] = json{ {"x", 0.0f}, {"y", 0.5f} };
        tabRowBg["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        tabRowBg["fillX"] = true;
        tabRowBg["color"] = json{ {"x", 0.08f}, {"y", 0.08f}, {"z", 0.08f}, {"w", 1.0f} };
        tabRowBg["shaderVertex"] = "panel_vertex.glsl";
        tabRowBg["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(tabRowBg);

        // ---- Row 1: Title row (top portion, y=0..0.5, 50px) ----

        // App title "HorizonEngine" on the left
        json label = json::object();
        label["id"] = "TitleBar.Label";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.01f}, {"y", 0.0f} };
        label["to"] = json{ {"x", 0.2f}, {"y", 0.5f} };
        label["color"] = json{ {"x", 0.85f}, {"y", 0.85f}, {"z", 0.85f}, {"w", 1.0f} };
        label["text"] = "HorizonEngine";
        label["font"] = "default.ttf";
        label["fontSize"] = t.fontSizeHeading;
        label["textAlignH"] = "Left";
        label["textAlignV"] = "Center";
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        // Project name in the center
        json projectLabel = json::object();
        projectLabel["id"] = "TitleBar.ProjectName";
        projectLabel["type"] = "Text";
        projectLabel["from"] = json{ {"x", 0.3f}, {"y", 0.0f} };
        projectLabel["to"] = json{ {"x", 0.7f}, {"y", 0.5f} };
        projectLabel["color"] = json{ {"x", 0.6f}, {"y", 0.6f}, {"z", 0.6f}, {"w", 1.0f} };
        projectLabel["text"] = "Project";
        projectLabel["font"] = "default.ttf";
        projectLabel["fontSize"] = t.fontSizeBody;
        projectLabel["textAlignH"] = "Center";
        projectLabel["textAlignV"] = "Center";
        projectLabel["sizeToContent"] = true;
        projectLabel["shaderVertex"] = "text_vertex.glsl";
        projectLabel["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(projectLabel);

        // Window controls (Minimize, Maximize, Close) on the far right
        json buttonStack = json::object();
        buttonStack["id"] = "TitleBar.Buttons";
        buttonStack["type"] = "StackPanel";
        buttonStack["from"] = json{ {"x", 0.88f}, {"y", 0.0f} };
        buttonStack["to"] = json{ {"x", 1.0f}, {"y", 0.5f} };
        buttonStack["orientation"] = "Horizontal";
        buttonStack["padding"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        buttonStack["sizeToContent"] = true;

        json btnMin = json::object();
        btnMin["id"] = "TitleBar.Minimize";
        btnMin["type"] = "Button";
        btnMin["clickEvent"] = "TitleBar.Minimize";
        btnMin["fillX"] = true;
        btnMin["fillY"] = true;
        btnMin["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        btnMin["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.2f}, {"w", 1.0f} };
        btnMin["textColor"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        btnMin["text"] = "_";
        btnMin["font"] = "default.ttf";
        btnMin["fontSize"] = t.fontSizeHeading;
        btnMin["textAlignH"] = "Center";
        btnMin["textAlignV"] = "Center";
        btnMin["minSize"] = json{ {"x", S(46.0f)}, {"y", 0.0f} };
        btnMin["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
        btnMin["borderRadius"] = 0.0f;
        btnMin["tooltipText"] = "Minimize";
        btnMin["shaderVertex"] = "button_vertex.glsl";
        btnMin["shaderFragment"] = "button_fragment.glsl";

        json btnMax = json::object();
        btnMax["id"] = "TitleBar.Maximize";
        btnMax["type"] = "Button";
        btnMax["clickEvent"] = "TitleBar.Maximize";
        btnMax["fillX"] = true;
        btnMax["fillY"] = true;
        btnMax["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        btnMax["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.2f}, {"w", 1.0f} };
        btnMax["textColor"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        btnMax["text"] = "[ ]";
        btnMax["font"] = "default.ttf";
        btnMax["fontSize"] = t.fontSizeHeading;
        btnMax["textAlignH"] = "Center";
        btnMax["textAlignV"] = "Center";
        btnMax["minSize"] = json{ {"x", S(46.0f)}, {"y", 0.0f} };
        btnMax["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
        btnMax["borderRadius"] = 0.0f;
        btnMax["tooltipText"] = "Maximize / Restore";
        btnMax["shaderVertex"] = "button_vertex.glsl";
        btnMax["shaderFragment"] = "button_fragment.glsl";

        json btnClose = json::object();
        btnClose["id"] = "TitleBar.Close";
        btnClose["type"] = "Button";
        btnClose["clickEvent"] = "TitleBar.Close";
        btnClose["fillX"] = true;
        btnClose["fillY"] = true;
        btnClose["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.1f}, {"w", 1.0f} };
        btnClose["hoverColor"] = json{ {"x", 0.7f}, {"y", 0.15f}, {"z", 0.15f}, {"w", 1.0f} };
        btnClose["textColor"] = json{ {"x", 1.0f}, {"y", 1.0f}, {"z", 1.0f}, {"w", 1.0f} };
        btnClose["text"] = "X";
        btnClose["font"] = "default.ttf";
        btnClose["fontSize"] = t.fontSizeHeading;
        btnClose["textAlignH"] = "Center";
        btnClose["textAlignV"] = "Center";
        btnClose["minSize"] = json{ {"x", S(46.0f)}, {"y", 0.0f} };
        btnClose["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
        btnClose["borderRadius"] = 0.0f;
        btnClose["tooltipText"] = "Close";
        btnClose["shaderVertex"] = "button_vertex.glsl";
        btnClose["shaderFragment"] = "button_fragment.glsl";

        buttonStack["children"] = json::array({ btnMin, btnMax, btnClose });
        elements.push_back(buttonStack);

        // ---- Row 2: Tab strip (bottom portion, y=0.5..1.0, 50px) ----

        json tabBar = json::object();
        tabBar["id"] = "TitleBar.Tabs";
        tabBar["type"] = "StackPanel";
        tabBar["from"] = json{ {"x", 0.0f}, {"y", 0.5f} };
        tabBar["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        tabBar["orientation"] = "Horizontal";
        tabBar["fillX"] = true;
        tabBar["fillY"] = true;
        tabBar["padding"] = json{ {"x", S(4.0f)}, {"y", 0.0f} };
        tabBar["sizeToContent"] = true;

        json tabViewport = json::object();
        tabViewport["id"] = "TitleBar.Tab.Viewport";
        tabViewport["type"] = "Button";
        tabViewport["clickEvent"] = "TitleBar.Tab.Viewport";
        tabViewport["fillY"] = true;
        tabViewport["color"] = json{ {"x", 0.14f}, {"y", 0.14f}, {"z", 0.14f}, {"w", 1.0f} };
        tabViewport["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.2f}, {"w", 1.0f} };
        tabViewport["textColor"] = json{ {"x", 0.9f}, {"y", 0.9f}, {"z", 0.9f}, {"w", 1.0f} };
        tabViewport["text"] = "Viewport";
        tabViewport["font"] = "default.ttf";
        tabViewport["fontSize"] = t.fontSizeBody;
        tabViewport["textAlignH"] = "Center";
        tabViewport["textAlignV"] = "Center";
        tabViewport["minSize"] = json{ {"x", S(90.0f)}, {"y", 0.0f} };
        tabViewport["padding"] = json{ {"x", S(10.0f)}, {"y", 0.0f} };
        tabViewport["borderRadius"] = S(4.0f);
        tabViewport["shaderVertex"] = "button_vertex.glsl";
        tabViewport["shaderFragment"] = "button_fragment.glsl";

        tabBar["children"] = json::array({ tabViewport });
        elements.push_back(tabBar);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("TitleBar");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(defaultWidgetRel);
        if (!checkUIVersion(abs))
            writeWidgetAsset(abs, widget->getName(), widget->getData());
    }

    const std::string toolbarWidgetRel = "ViewportOverlay.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", S(34.0f)} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 0;
        widgetJson["_uiVersion"] = kEditorWidgetUIVersion;
        widgetJson["_dpiScale"] = dpi;

        json elements = json::array();

        // Dark toolbar background
        json bg = json::object();
        bg["id"] = "ViewportOverlay.Background";
        bg["type"] = "Panel";
        bg["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        bg["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        bg["fillX"] = true;
        bg["color"] = json{ {"x", 0.12f}, {"y", 0.12f}, {"z", 0.12f}, {"w", 1.0f} };
        bg["shaderVertex"] = "panel_vertex.glsl";
        bg["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(bg);

        // Single horizontal StackPanel as the toolbar strip
        json toolbar = json::object();
        toolbar["id"] = "ViewportOverlay.Toolbar";
        toolbar["type"] = "StackPanel";
        toolbar["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        toolbar["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        toolbar["orientation"] = "Horizontal";
        toolbar["fillX"] = true;
        toolbar["fillY"] = true;
        toolbar["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };

        // â”€â”€ Left group â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Render mode dropdown button
        json btnRenderMode = json::object();
        btnRenderMode["id"] = "ViewportOverlay.RenderMode";
        btnRenderMode["type"] = "Button";
        btnRenderMode["clickEvent"] = "ViewportOverlay.RenderMode";
        btnRenderMode["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnRenderMode["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnRenderMode["textColor"] = json{ {"x", 0.85f}, {"y", 0.85f}, {"z", 0.85f}, {"w", 1.0f} };
        btnRenderMode["text"] = "Lit";
        btnRenderMode["font"] = "default.ttf";
        btnRenderMode["fontSize"] = t.fontSizeBody;
        btnRenderMode["textAlignH"] = "Center";
        btnRenderMode["textAlignV"] = "Center";
        btnRenderMode["minSize"] = json{ {"x", S(50.0f)}, {"y", 0.0f} };
        btnRenderMode["fillY"] = true;
        btnRenderMode["padding"] = json{ {"x", S(8.0f)}, {"y", S(2.0f)} };
        btnRenderMode["tooltipText"] = "Render Mode";
        btnRenderMode["shaderVertex"] = "button_vertex.glsl";
        btnRenderMode["shaderFragment"] = "button_fragment.glsl";

        // Thin separator
        auto makeSep = [&](const std::string& sepId) -> json {
            json s = json::object();
            s["id"] = sepId;
            s["type"] = "Panel";
            s["minSize"] = json{ {"x", S(1.0f)}, {"y", 0.0f} };
            s["fillY"] = true;
            s["color"] = json{ {"x", 0.25f}, {"y", 0.25f}, {"z", 0.30f}, {"w", 0.6f} };
            s["shaderVertex"] = "panel_vertex.glsl";
            s["shaderFragment"] = "panel_fragment.glsl";
            return s;
        };

        // Undo button (icon)
        json btnUndo = json::object();
        btnUndo["id"] = "ViewportOverlay.Undo";
        btnUndo["type"] = "Button";
        btnUndo["clickEvent"] = "ViewportOverlay.Undo";
        btnUndo["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnUndo["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnUndo["imagePath"] = "undo.png";
        btnUndo["minSize"] = json{ {"x", S(28.0f)}, {"y", 0.0f} };
        btnUndo["fillY"] = true;
        btnUndo["sizeToContent"] = true;
        btnUndo["padding"] = json{ {"x", S(5.0f)}, {"y", S(5.0f)} };
        btnUndo["tooltipText"] = "Undo (Ctrl+Z)";
        btnUndo["shaderVertex"] = "button_vertex.glsl";
        btnUndo["shaderFragment"] = "button_fragment.glsl";

        // Redo button (icon)
        json btnRedo = json::object();
        btnRedo["id"] = "ViewportOverlay.Redo";
        btnRedo["type"] = "Button";
        btnRedo["clickEvent"] = "ViewportOverlay.Redo";
        btnRedo["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnRedo["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnRedo["imagePath"] = "redo.png";
        btnRedo["minSize"] = json{ {"x", S(28.0f)}, {"y", 0.0f} };
        btnRedo["fillY"] = true;
        btnRedo["sizeToContent"] = true;
        btnRedo["padding"] = json{ {"x", S(5.0f)}, {"y", S(5.0f)} };
        btnRedo["tooltipText"] = "Redo (Ctrl+Y)";
        btnRedo["shaderVertex"] = "button_vertex.glsl";
        btnRedo["shaderFragment"] = "button_fragment.glsl";

        // â”€â”€ Center spacer
        json spacerLeft = json::object();
        spacerLeft["id"] = "ViewportOverlay.SpacerL";
        spacerLeft["type"] = "Panel";
        spacerLeft["fillX"] = true;
        spacerLeft["fillY"] = true;
        spacerLeft["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        // PIE (Play) button â€“ fills full toolbar height
        json btnPIE = json::object();
        btnPIE["id"] = "ViewportOverlay.PIE";
        btnPIE["type"] = "Button";
        btnPIE["clickEvent"] = "ViewportOverlay.PIE";
        btnPIE["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnPIE["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnPIE["imagePath"] = "Play.tga";
        btnPIE["minSize"] = json{ {"x", S(36.0f)}, {"y", 0.0f} };
        btnPIE["fillY"] = true;
        btnPIE["sizeToContent"] = true;
        btnPIE["padding"] = json{ {"x", S(6.0f)}, {"y", S(2.0f)} };
        btnPIE["tooltipText"] = "Play / Stop (F8)";
        btnPIE["shaderVertex"] = "button_vertex.glsl";
        btnPIE["shaderFragment"] = "button_fragment.glsl";

        json spacerRight = json::object();
        spacerRight["id"] = "ViewportOverlay.SpacerR";
        spacerRight["type"] = "Panel";
        spacerRight["fillX"] = true;
        spacerRight["fillY"] = true;
        spacerRight["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        // â”€â”€ Right group â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        // Grid snap toggle (dummy)
        json btnSnap = json::object();
        btnSnap["id"] = "ViewportOverlay.Snap";
        btnSnap["type"] = "Button";
        btnSnap["clickEvent"] = "ViewportOverlay.Snap";
        btnSnap["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnSnap["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnSnap["textColor"] = json{ {"x", 0.45f}, {"y", 0.45f}, {"z", 0.45f}, {"w", 1.0f} };
        btnSnap["text"] = "#";
        btnSnap["font"] = "default.ttf";
        btnSnap["fontSize"] = t.fontSizeBody;
        btnSnap["textAlignH"] = "Center";
        btnSnap["textAlignV"] = "Center";
        btnSnap["minSize"] = json{ {"x", S(28.0f)}, {"y", 0.0f} };
        btnSnap["fillY"] = true;
        btnSnap["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnSnap["tooltipText"] = "Grid Snap";
        btnSnap["shaderVertex"] = "button_vertex.glsl";
        btnSnap["shaderFragment"] = "button_fragment.glsl";

        // Grid size dropdown
        json btnGridSize = json::object();
        btnGridSize["id"] = "ViewportOverlay.GridSize";
        btnGridSize["type"] = "Button";
        btnGridSize["clickEvent"] = "ViewportOverlay.GridSize";
        btnGridSize["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnGridSize["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnGridSize["textColor"] = json{ {"x", 0.70f}, {"y", 0.70f}, {"z", 0.70f}, {"w", 1.0f} };
        btnGridSize["text"] = "1.0";
        btnGridSize["font"] = "default.ttf";
        btnGridSize["fontSize"] = t.fontSizeSmall;
        btnGridSize["textAlignH"] = "Center";
        btnGridSize["textAlignV"] = "Center";
        btnGridSize["minSize"] = json{ {"x", S(32.0f)}, {"y", 0.0f} };
        btnGridSize["fillY"] = true;
        btnGridSize["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnGridSize["tooltipText"] = "Grid Size";
        btnGridSize["shaderVertex"] = "button_vertex.glsl";
        btnGridSize["shaderFragment"] = "button_fragment.glsl";

        // Camera speed button
        json btnCamSpeed = json::object();
        btnCamSpeed["id"] = "ViewportOverlay.CamSpeed";
        btnCamSpeed["type"] = "Button";
        btnCamSpeed["clickEvent"] = "ViewportOverlay.CamSpeed";
        btnCamSpeed["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnCamSpeed["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnCamSpeed["textColor"] = json{ {"x", 0.70f}, {"y", 0.70f}, {"z", 0.70f}, {"w", 1.0f} };
        btnCamSpeed["text"] = "1.0x";
        btnCamSpeed["font"] = "default.ttf";
        btnCamSpeed["fontSize"] = t.fontSizeSmall;
        btnCamSpeed["textAlignH"] = "Center";
        btnCamSpeed["textAlignV"] = "Center";
        btnCamSpeed["minSize"] = json{ {"x", S(36.0f)}, {"y", 0.0f} };
        btnCamSpeed["fillY"] = true;
        btnCamSpeed["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnCamSpeed["tooltipText"] = "Camera Speed";
        btnCamSpeed["shaderVertex"] = "button_vertex.glsl";
        btnCamSpeed["shaderFragment"] = "button_fragment.glsl";

        // Stats toggle
        json btnStats = json::object();
        btnStats["id"] = "ViewportOverlay.Stats";
        btnStats["type"] = "Button";
        btnStats["clickEvent"] = "ViewportOverlay.Stats";
        btnStats["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnStats["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnStats["textColor"] = json{ {"x", 0.70f}, {"y", 0.70f}, {"z", 0.70f}, {"w", 1.0f} };
        btnStats["text"] = "Stats";
        btnStats["font"] = "default.ttf";
        btnStats["fontSize"] = t.fontSizeSmall;
        btnStats["textAlignH"] = "Center";
        btnStats["textAlignV"] = "Center";
        btnStats["minSize"] = json{ {"x", S(38.0f)}, {"y", 0.0f} };
        btnStats["fillY"] = true;
        btnStats["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnStats["tooltipText"] = "Performance Stats";
        btnStats["shaderVertex"] = "button_vertex.glsl";
        btnStats["shaderFragment"] = "button_fragment.glsl";

        // Collider visualization toggle
        json btnColliders = json::object();
        btnColliders["id"] = "ViewportOverlay.Colliders";
        btnColliders["type"] = "Button";
        btnColliders["clickEvent"] = "ViewportOverlay.Colliders";
        btnColliders["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnColliders["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnColliders["textColor"] = json{ {"x", 0.45f}, {"y", 0.45f}, {"z", 0.45f}, {"w", 1.0f} };
        btnColliders["text"] = "Col";
        btnColliders["font"] = "default.ttf";
        btnColliders["fontSize"] = t.fontSizeSmall;
        btnColliders["textAlignH"] = "Center";
        btnColliders["textAlignV"] = "Center";
        btnColliders["minSize"] = json{ {"x", S(32.0f)}, {"y", 0.0f} };
        btnColliders["fillY"] = true;
        btnColliders["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnColliders["tooltipText"] = "Show Colliders";
        btnColliders["shaderVertex"] = "button_vertex.glsl";
        btnColliders["shaderFragment"] = "button_fragment.glsl";

        // Bone visualization toggle
        json btnBones = json::object();
        btnBones["id"] = "ViewportOverlay.Bones";
        btnBones["type"] = "Button";
        btnBones["clickEvent"] = "ViewportOverlay.Bones";
        btnBones["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnBones["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnBones["textColor"] = json{ {"x", 0.45f}, {"y", 0.45f}, {"z", 0.45f}, {"w", 1.0f} };
        btnBones["text"] = "Bone";
        btnBones["font"] = "default.ttf";
        btnBones["fontSize"] = t.fontSizeSmall;
        btnBones["textAlignH"] = "Center";
        btnBones["textAlignV"] = "Center";
        btnBones["minSize"] = json{ {"x", S(36.0f)}, {"y", 0.0f} };
        btnBones["fillY"] = true;
        btnBones["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnBones["tooltipText"] = "Show Bones (selected entity)";
        btnBones["shaderVertex"] = "button_vertex.glsl";
        btnBones["shaderFragment"] = "button_fragment.glsl";

        // Viewport Layout button
        json btnLayout = json::object();
        btnLayout["id"] = "ViewportOverlay.Layout";
        btnLayout["type"] = "Button";
        btnLayout["clickEvent"] = "ViewportOverlay.Layout";
        btnLayout["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnLayout["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnLayout["textColor"] = json{ {"x", 0.70f}, {"y", 0.70f}, {"z", 0.70f}, {"w", 1.0f} };
        btnLayout["text"] = "\xe2\x96\xa3";
        btnLayout["font"] = "default.ttf";
        btnLayout["fontSize"] = t.fontSizeSmall;
        btnLayout["textAlignH"] = "Center";
        btnLayout["textAlignV"] = "Center";
        btnLayout["minSize"] = json{ {"x", S(28.0f)}, {"y", 0.0f} };
        btnLayout["fillY"] = true;
        btnLayout["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        btnLayout["tooltipText"] = "Viewport Layout";
        btnLayout["shaderVertex"] = "button_vertex.glsl";
        btnLayout["shaderFragment"] = "button_fragment.glsl";

        // Settings button
        json btnSettings = json::object();
        btnSettings["id"] = "ViewportOverlay.Settings";
        btnSettings["type"] = "Button";
        btnSettings["clickEvent"] = "ViewportOverlay.Settings";
        btnSettings["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };
        btnSettings["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.22f}, {"z", 0.22f}, {"w", 1.0f} };
        btnSettings["imagePath"] = "Settings.png";
        btnSettings["minSize"] = json{ {"x", S(28.0f)}, {"y", 0.0f} };
        btnSettings["fillY"] = true;
        btnSettings["sizeToContent"] = true;
        btnSettings["padding"] = json{ {"x", S(5.0f)}, {"y", S(5.0f)} };
        btnSettings["tooltipText"] = "Settings";
        btnSettings["shaderVertex"] = "button_vertex.glsl";
        btnSettings["shaderFragment"] = "button_fragment.glsl";

        toolbar["children"] = json::array({
            btnRenderMode, makeSep("ViewportOverlay.Sep1"),
            btnUndo, btnRedo, makeSep("ViewportOverlay.Sep2"),
            spacerLeft, btnPIE, spacerRight,
            makeSep("ViewportOverlay.Sep3"),
            btnSnap, btnGridSize, btnCamSpeed, btnStats, btnColliders, btnBones, btnLayout,
            makeSep("ViewportOverlay.Sep4"), btnSettings
        });
        elements.push_back(toolbar);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("ViewportOverlay");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(toolbarWidgetRel);
        if (!checkUIVersion(abs))
            writeWidgetAsset(abs, widget->getName(), widget->getData());
    }

    createWorldSettingsWidgetAsset();

    const std::string outlinerWidgetRel = "WorldOutliner.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", S(240.0f)}, {"y", 0.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopRight";
        widgetJson["m_fillY"] = true;
        widgetJson["m_zOrder"] = 1;
        widgetJson["_uiVersion"] = kEditorWidgetUIVersion;
        widgetJson["_dpiScale"] = dpi;

        json elements = json::array();
        json panel = json::object();
        panel["id"] = "Outliner.Background";
        panel["type"] = "Panel";
        panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        panel["fillX"] = true;
        panel["fillY"] = true;
        panel["color"] = json{ {"x", 0.07f}, {"y", 0.08f}, {"z", 0.1f}, {"w", 0.88f} };
        panel["shaderVertex"] = "panel_vertex.glsl";
        panel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(panel);

        json label = json::object();
        label["id"] = "Outliner.Title";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.05f}, {"y", 0.02f} };
        label["to"] = json{ {"x", 0.95f}, {"y", 0.1f} };
        label["color"] = json{ {"x", 0.88f}, {"y", 0.88f}, {"z", 0.90f}, {"w", 1.0f} };
        label["text"] = "Outliner";
        label["font"] = "default.ttf";
        label["fontSize"] = t.fontSizeSubheading;
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        json listPanel = json::object();
        listPanel["id"] = "Outliner.EntityList";
        listPanel["type"] = "StackPanel";
        listPanel["from"] = json{ {"x", 0.05f}, {"y", 0.12f} };
        listPanel["to"] = json{ {"x", 0.95f}, {"y", 0.44f} };
        listPanel["orientation"] = "Vertical";
        listPanel["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
        listPanel["fillX"] = true;
        listPanel["sizeToContent"] = false;
        listPanel["scrollable"] = true;
        elements.push_back(listPanel);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("WorldOutliner");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(outlinerWidgetRel);
        if (!checkUIVersion(abs))
            writeWidgetAsset(abs, widget->getName(), widget->getData());
    }

    const std::string entityDetailsWidgetRel = "EntityDetails.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", S(240.0f)}, {"y", 0.0f} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "TopRight";
        widgetJson["m_zOrder"] = 2;
        widgetJson["_uiVersion"] = kEditorWidgetUIVersion;
        widgetJson["_dpiScale"] = dpi;

        json elements = json::array();
        json panel = json::object();
        panel["id"] = "Details.Background";
        panel["type"] = "Panel";
        panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        panel["fillX"] = true;
        panel["fillY"] = true;
        panel["color"] = json{ {"x", 0.07f}, {"y", 0.08f}, {"z", 0.1f}, {"w", 0.9f} };
        panel["shaderVertex"] = "panel_vertex.glsl";
        panel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(panel);

        json label = json::object();
        label["id"] = "Details.Title";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.05f}, {"y", 0.0f} };
        label["to"] = json{ {"x", 0.95f}, {"y", 0.1f} };
        label["color"] = json{ {"x", 0.88f}, {"y", 0.88f}, {"z", 0.90f}, {"w", 1.0f} };
        label["text"] = "Details";
        label["font"] = "default.ttf";
        label["fontSize"] = t.fontSizeSubheading;
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        json contentPanel = json::object();
        contentPanel["id"] = "Details.Content";
        contentPanel["type"] = "StackPanel";
        contentPanel["from"] = json{ {"x", 0.02f}, {"y", 0.12f} };
        contentPanel["to"] = json{ {"x", 0.98f}, {"y", 0.98f} };
        contentPanel["orientation"] = "Vertical";
        contentPanel["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
        contentPanel["fillX"] = true;
        contentPanel["sizeToContent"] = false;
        contentPanel["scrollable"] = true;
        contentPanel["color"] = json{ {"x", 0.08f}, {"y", 0.09f}, {"z", 0.12f}, {"w", 0.65f} };
        elements.push_back(contentPanel);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("EntityDetails");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(entityDetailsWidgetRel);
        if (!checkUIVersion(abs))
            writeWidgetAsset(abs, widget->getName(), widget->getData());
    }

    const std::string contentBrowserWidgetRel = "ContentBrowser.asset";
    {
        logger.log(Logger::Category::AssetManagement, "[ContentBrowser] ensureEditorWidgetsCreated: building ContentBrowser widget asset definition", Logger::LogLevel::INFO);

        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", S(190.0f)} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "BottomLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 2;
        widgetJson["_uiVersion"] = kEditorWidgetUIVersion;
        widgetJson["_dpiScale"] = dpi;

        json elements = json::array();
        json panel = json::object();
        panel["id"] = "ContentBrowser.Background";
        panel["type"] = "Panel";
        panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        panel["fillX"] = true;
        panel["fillY"] = true;
        panel["color"] = json{ {"x", 0.08f}, {"y", 0.09f}, {"z", 0.12f}, {"w", 0.9f} };
        panel["shaderVertex"] = "panel_vertex.glsl";
        panel["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(panel);

        json label = json::object();
        label["id"] = "ContentBrowser.Title";
        label["type"] = "Text";
        label["from"] = json{ {"x", 0.02f}, {"y", 0.05f} };
        label["to"] = json{ {"x", 0.6f}, {"y", 0.2f} };
        label["color"] = json{ {"x", 0.88f}, {"y", 0.88f}, {"z", 0.90f}, {"w", 1.0f} };
        label["text"] = "Content Browser";
        label["font"] = "default.ttf";
        label["fontSize"] = t.fontSizeSubheading;
        label["sizeToContent"] = true;
        label["shaderVertex"] = "text_vertex.glsl";
        label["shaderFragment"] = "text_fragment.glsl";
        elements.push_back(label);

        // Path bar (breadcrumb) between title row and grid
        json pathBar = json::object();
        pathBar["id"] = "ContentBrowser.PathBar";
        pathBar["type"] = "StackPanel";
        pathBar["from"] = json{ {"x", 0.25f}, {"y", 0.01f} };
        pathBar["to"] = json{ {"x", 0.98f}, {"y", 0.14f} };
        pathBar["orientation"] = "Horizontal";
        pathBar["padding"] = json{ {"x", S(2.0f)}, {"y", 0.0f} };
        pathBar["color"] = json{ {"x", 0.09f}, {"y", 0.10f}, {"z", 0.13f}, {"w", 0.9f} };
        pathBar["shaderVertex"] = "panel_vertex.glsl";
        pathBar["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(pathBar);

        json treePanel = json::object();
        treePanel["id"] = "ContentBrowser.Tree";
        treePanel["type"] = "TreeView";
        treePanel["from"] = json{ {"x", 0.0f}, {"y", 0.22f} };
        treePanel["to"] = json{ {"x", 0.22f}, {"y", 0.95f} };
        treePanel["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
        treePanel["fillY"] = false;
        treePanel["sizeToContent"] = false;
        treePanel["scrollable"] = true;
        treePanel["color"] = json{ {"x", 0.07f}, {"y", 0.08f}, {"z", 0.10f}, {"w", 0.95f} };
        elements.push_back(treePanel);

        json grid = json::object();
        grid["id"] = "ContentBrowser.Grid";
        grid["type"] = "Grid";
        grid["from"] = json{ {"x", 0.25f}, {"y", 0.16f} };
        grid["to"] = json{ {"x", 0.98f}, {"y", 0.95f} };
        grid["padding"] = json{ {"x", S(8.0f)}, {"y", S(8.0f)} };
        grid["scrollable"] = true;
        grid["color"] = json{ {"x", 0.06f}, {"y", 0.07f}, {"z", 0.09f}, {"w", 0.0f} };
        elements.push_back(grid);

        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("ContentBrowser");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        std::error_code ec;
        fs::create_directories(widgetsRoot, ec);
        const fs::path abs = widgetsRoot / fs::path(contentBrowserWidgetRel);
        if (!checkUIVersion(abs))
            writeWidgetAsset(abs, widget->getName(), widget->getData());
    }

    const std::string statusBarWidgetRel = "StatusBar.asset";
    {
        json widgetJson = json::object();
        widgetJson["m_sizePixels"] = json{ {"x", 0.0f}, {"y", S(28.0f)} };
        widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        widgetJson["m_anchor"] = "BottomLeft";
        widgetJson["m_fillX"] = true;
        widgetJson["m_zOrder"] = 3;
        widgetJson["_uiVersion"] = kEditorWidgetUIVersion;
        widgetJson["_dpiScale"] = dpi;

        json elements = json::array();

        json bg = json::object();
        bg["id"] = "StatusBar.Background";
        bg["type"] = "Panel";
        bg["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        bg["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        bg["fillX"] = true;
        bg["fillY"] = true;
        bg["color"] = json{ {"x", 0.1f}, {"y", 0.1f}, {"z", 0.13f}, {"w", 0.95f} };
        bg["shaderVertex"] = "panel_vertex.glsl";
        bg["shaderFragment"] = "panel_fragment.glsl";
        elements.push_back(bg);

        json row = json::object();
        row["id"] = "StatusBar.Row";
        row["type"] = "StackPanel";
        row["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
        row["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
        row["orientation"] = "Horizontal";
        row["fillX"] = true;
        row["fillY"] = true;
        row["padding"] = json{ {"x", S(4.0f)}, {"y", S(2.0f)} };
        row["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        json undoBtn = json::object();
        undoBtn["id"] = "StatusBar.Undo";
        undoBtn["type"] = "Button";
        undoBtn["text"] = "Undo";
        undoBtn["font"] = "default.ttf";
        undoBtn["fontSize"] = t.fontSizeBody;
        undoBtn["textAlignH"] = "Center";
        undoBtn["textAlignV"] = "Center";
        undoBtn["padding"] = json{ {"x", S(8.0f)}, {"y", S(4.0f)} };
        undoBtn["minSize"] = json{ {"x", S(60.0f)}, {"y", S(22.0f)} };
        undoBtn["color"] = json{ {"x", 0.16f}, {"y", 0.16f}, {"z", 0.2f}, {"w", 0.95f} };
        undoBtn["hoverColor"] = json{ {"x", 0.24f}, {"y", 0.24f}, {"z", 0.3f}, {"w", 0.98f} };
        undoBtn["textColor"] = json{ {"x", 0.7f}, {"y", 0.7f}, {"z", 0.75f}, {"w", 1.0f} };
        undoBtn["borderRadius"] = S(4.0f);
        undoBtn["shaderVertex"] = "button_vertex.glsl";
        undoBtn["shaderFragment"] = "button_fragment.glsl";
        undoBtn["isHitTestable"] = true;
        undoBtn["tooltipText"] = "Undo last action (Ctrl+Z)";
        undoBtn["clickEvent"] = "StatusBar.Undo";

        json redoBtn = json::object();
        redoBtn["id"] = "StatusBar.Redo";
        redoBtn["type"] = "Button";
        redoBtn["text"] = "Redo";
        redoBtn["font"] = "default.ttf";
        redoBtn["fontSize"] = t.fontSizeBody;
        redoBtn["textAlignH"] = "Center";
        redoBtn["textAlignV"] = "Center";
        redoBtn["padding"] = json{ {"x", S(8.0f)}, {"y", S(4.0f)} };
        redoBtn["minSize"] = json{ {"x", S(60.0f)}, {"y", S(22.0f)} };
        redoBtn["color"] = json{ {"x", 0.16f}, {"y", 0.16f}, {"z", 0.2f}, {"w", 0.95f} };
        redoBtn["hoverColor"] = json{ {"x", 0.24f}, {"y", 0.24f}, {"z", 0.3f}, {"w", 0.98f} };
        redoBtn["textColor"] = json{ {"x", 0.7f}, {"y", 0.7f}, {"z", 0.75f}, {"w", 1.0f} };
        redoBtn["borderRadius"] = S(4.0f);
        redoBtn["shaderVertex"] = "button_vertex.glsl";
        redoBtn["shaderFragment"] = "button_fragment.glsl";
        redoBtn["isHitTestable"] = true;
        redoBtn["tooltipText"] = "Redo last action (Ctrl+Y)";
        redoBtn["clickEvent"] = "StatusBar.Redo";

        json spacer = json::object();
        spacer["id"] = "StatusBar.Spacer";
        spacer["type"] = "Panel";
        spacer["fillX"] = true;
        spacer["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        json dirtyLabel = json::object();
        dirtyLabel["id"] = "StatusBar.DirtyLabel";
        dirtyLabel["type"] = "Text";
        dirtyLabel["text"] = "No unsaved changes";
        dirtyLabel["font"] = "default.ttf";
        dirtyLabel["fontSize"] = t.fontSizeBody;
        dirtyLabel["textAlignH"] = "Center";
        dirtyLabel["textAlignV"] = "Center";
        dirtyLabel["textColor"] = json{ {"x", 0.6f}, {"y", 0.6f}, {"z", 0.65f}, {"w", 1.0f} };
        dirtyLabel["minSize"] = json{ {"x", 0.0f}, {"y", S(22.0f)} };
        dirtyLabel["padding"] = json{ {"x", S(8.0f)}, {"y", 0.0f} };

        json saveBtn = json::object();
        saveBtn["id"] = "StatusBar.Save";
        saveBtn["type"] = "Button";
        saveBtn["text"] = "Save All";
        saveBtn["font"] = "default.ttf";
        saveBtn["fontSize"] = t.fontSizeBody;
        saveBtn["textAlignH"] = "Center";
        saveBtn["textAlignV"] = "Center";
        saveBtn["padding"] = json{ {"x", S(10.0f)}, {"y", S(4.0f)} };
        saveBtn["minSize"] = json{ {"x", S(72.0f)}, {"y", S(22.0f)} };
        saveBtn["color"] = json{ {"x", 0.15f}, {"y", 0.35f}, {"z", 0.15f}, {"w", 0.95f} };
        saveBtn["hoverColor"] = json{ {"x", 0.2f}, {"y", 0.5f}, {"z", 0.2f}, {"w", 0.98f} };
        saveBtn["textColor"] = json{ {"x", 0.95f}, {"y", 0.95f}, {"z", 0.95f}, {"w", 1.0f} };
        saveBtn["borderRadius"] = S(4.0f);
        saveBtn["shaderVertex"] = "button_vertex.glsl";
        saveBtn["shaderFragment"] = "button_fragment.glsl";
        saveBtn["isHitTestable"] = true;
        saveBtn["tooltipText"] = "Save all unsaved assets (Ctrl+S)";
        saveBtn["clickEvent"] = "StatusBar.Save";

        json notifSpacer = json::object();
        notifSpacer["id"] = "StatusBar.NotifSpacer";
        notifSpacer["type"] = "Panel";
        notifSpacer["minSize"] = json{ {"x", S(6.0f)}, {"y", 0.0f} };
        notifSpacer["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

        json notifBtn = json::object();
        notifBtn["id"] = "StatusBar.Notifications";
        notifBtn["type"] = "Button";
        notifBtn["text"] = "\xF0\x9F\x94\x94";  // ðŸ”” bell emoji
        notifBtn["font"] = "default.ttf";
        notifBtn["fontSize"] = t.fontSizeBody;
        notifBtn["textAlignH"] = "Center";
        notifBtn["textAlignV"] = "Center";
        notifBtn["padding"] = json{ {"x", S(6.0f)}, {"y", S(4.0f)} };
        notifBtn["minSize"] = json{ {"x", S(32.0f)}, {"y", S(22.0f)} };
        notifBtn["color"] = json{ {"x", 0.16f}, {"y", 0.16f}, {"z", 0.2f}, {"w", 0.95f} };
        notifBtn["hoverColor"] = json{ {"x", 0.24f}, {"y", 0.24f}, {"z", 0.3f}, {"w", 0.98f} };
        notifBtn["textColor"] = json{ {"x", 0.7f}, {"y", 0.7f}, {"z", 0.75f}, {"w", 1.0f} };
        notifBtn["borderRadius"] = S(4.0f);
        notifBtn["shaderVertex"] = "button_vertex.glsl";
        notifBtn["shaderFragment"] = "button_fragment.glsl";
        notifBtn["isHitTestable"] = true;
        notifBtn["tooltipText"] = "Notification History";
        notifBtn["clickEvent"] = "StatusBar.Notifications";

        row["children"] = json::array({ undoBtn, redoBtn, spacer, dirtyLabel, saveBtn, notifSpacer, notifBtn });
        elements.push_back(row);
        widgetJson["m_elements"] = elements;

        auto widget = std::make_shared<AssetData>();
        widget->setName("StatusBar");
        widget->setData(std::move(widgetJson));

        const fs::path widgetsRoot = getEditorWidgetsRootPath();
        const fs::path abs = widgetsRoot / fs::path(statusBarWidgetRel);
        if (!checkUIVersion(abs))
            writeWidgetAsset(abs, widget->getName(), widget->getData());
    }

    // WorldGrid material â€“ stored in engine Content/Materials (not project Content)
    {
        const fs::path materialsDir = fs::current_path() / "Content" / "Materials";
        std::error_code ec;
        fs::create_directories(materialsDir, ec);
        const fs::path abs = materialsDir / "WorldGrid.asset";
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Material;
        }
        if (!existsAndOk)
        {
            std::ofstream out(abs, std::ios::out | std::ios::trunc);
            if (out.is_open())
            {
                json matData = json::object();
                matData["m_shaderFragment"] = "grid_fragment.glsl";

                json fileJson = json::object();
                fileJson["magic"] = 0x41535453;
                fileJson["version"] = 2;
                fileJson["type"] = static_cast<int>(AssetType::Material);
                fileJson["name"] = "WorldGrid";
                fileJson["data"] = matData;
                out << fileJson.dump(4);
            }
        }
    }
}

                void AssetManager::createWorldSettingsWidgetAsset()
{
    auto& logger = Logger::Instance();
    const std::string worldSettingsWidgetRel = "WorldSettings.asset";
    static constexpr int kUIVersion = 5; // bump to force regeneration

    const EditorTheme& t = EditorTheme::Get();
    const float dpi = t.dpiScale;
    auto S = [dpi](float px) -> float { return px * dpi; };

    json widgetJson = json::object();
    widgetJson["m_sizePixels"] = json{ {"x", S(220.0f)}, {"y", 0.0f} };
    widgetJson["m_positionPixels"] = json{ {"x", 0.0f}, {"y", 0.0f} };
    widgetJson["m_anchor"] = "TopLeft";
    widgetJson["m_fillY"] = true;
    widgetJson["m_zOrder"] = 1;
    widgetJson["_uiVersion"] = kUIVersion;
    widgetJson["_dpiScale"] = dpi;

    json elements = json::array();

    json panel = json::object();
    panel["id"] = "WorldSettings.Background";
    panel["type"] = "Panel";
    panel["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
    panel["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
    panel["fillX"] = true;
    panel["fillY"] = true;
    panel["color"] = json{ {"x", 0.09f}, {"y", 0.10f}, {"z", 0.12f}, {"w", 0.97f} };
    panel["shaderVertex"] = "panel_vertex.glsl";
    panel["shaderFragment"] = "panel_fragment.glsl";
    elements.push_back(panel);

    json stack = json::object();
    stack["id"] = "WorldSettings.Stack";
    stack["type"] = "StackPanel";
    stack["from"] = json{ {"x", 0.0f}, {"y", 0.0f} };
    stack["to"] = json{ {"x", 1.0f}, {"y", 1.0f} };
    stack["fillX"] = true;
    stack["fillY"] = true;
    stack["sizeToContent"] = true;
    stack["padding"] = json{ {"x", S(8.0f)}, {"y", S(8.0f)} };
    stack["orientation"] = "Vertical";
    stack["scrollable"] = true;
    stack["color"] = json{ {"x", 0.0f}, {"y", 0.0f}, {"z", 0.0f}, {"w", 0.0f} };

    json title = json::object();
    title["id"] = "WorldSettings.Title";
    title["type"] = "Text";
    title["text"] = "World Settings";
    title["font"] = "default.ttf";
    title["fontSize"] = t.fontSizeSubheading;
    title["textAlignH"] = "Left";
    title["textAlignV"] = "Center";
    title["padding"] = json{ {"x", S(2.0f)}, {"y", S(3.0f)} };
    title["textColor"] = json{ {"x", 0.95f}, {"y", 0.95f}, {"z", 0.95f}, {"w", 1.0f} };
    title["minSize"] = json{ {"x", 0.0f}, {"y", S(20.0f)} };

    json clearLabel = json::object();
    clearLabel["id"] = "WorldSettings.ClearColor.Label";
    clearLabel["type"] = "Text";
    clearLabel["text"] = "Clear Color";
    clearLabel["font"] = "default.ttf";
    clearLabel["fontSize"] = t.fontSizeSmall;
    clearLabel["textAlignH"] = "Left";
    clearLabel["textAlignV"] = "Center";
    clearLabel["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
    clearLabel["textColor"] = json{ {"x", 0.70f}, {"y", 0.70f}, {"z", 0.72f}, {"w", 1.0f} };
    clearLabel["minSize"] = json{ {"x", 0.0f}, {"y", S(16.0f)} };

    json colorPicker = json::object();
    colorPicker["id"] = "WorldSettings.ClearColor";
    colorPicker["type"] = "ColorPicker";
    colorPicker["compact"] = false;
    colorPicker["minSize"] = json{ {"x", S(180.0f)}, {"y", S(60.0f)} };

    json separator = json::object();
    separator["id"] = "WorldSettings.Tools.Sep";
    separator["type"] = "Panel";
    separator["color"] = json{ {"x", 0.2f}, {"y", 0.2f}, {"z", 0.22f}, {"w", 0.7f} };
    separator["minSize"] = json{ {"x", 0.0f}, {"y", S(1.0f)} };
    separator["padding"] = json{ {"x", 0.0f}, {"y", S(6.0f)} };

    json toolsLabel = json::object();
    toolsLabel["id"] = "WorldSettings.Tools.Label";
    toolsLabel["type"] = "Text";
    toolsLabel["text"] = "Tools";
    toolsLabel["font"] = "default.ttf";
    toolsLabel["fontSize"] = t.fontSizeSmall;
    toolsLabel["textAlignH"] = "Left";
    toolsLabel["textAlignV"] = "Center";
    toolsLabel["padding"] = json{ {"x", S(2.0f)}, {"y", S(2.0f)} };
    toolsLabel["textColor"] = json{ {"x", 0.60f}, {"y", 0.60f}, {"z", 0.62f}, {"w", 1.0f} };
    toolsLabel["minSize"] = json{ {"x", 0.0f}, {"y", S(16.0f)} };

    auto makeToolBtn = [&S, &t](const std::string& id, const std::string& text, const std::string& clickEvent) {
        json btn = json::object();
        btn["id"] = id;
        btn["type"] = "Button";
        btn["text"] = text;
        btn["font"] = "default.ttf";
        btn["fontSize"] = t.fontSizeBody;
        btn["textAlignH"] = "Center";
        btn["textAlignV"] = "Center";
        btn["padding"] = json{ {"x", S(6.0f)}, {"y", S(3.0f)} };
        btn["minSize"] = json{ {"x", 0.0f}, {"y", S(22.0f)} };
        btn["color"] = json{ {"x", 0.16f}, {"y", 0.17f}, {"z", 0.20f}, {"w", 1.0f} };
        btn["hoverColor"] = json{ {"x", 0.22f}, {"y", 0.23f}, {"z", 0.28f}, {"w", 1.0f} };
        btn["textColor"] = json{ {"x", 0.88f}, {"y", 0.88f}, {"z", 0.90f}, {"w", 1.0f} };
        btn["borderRadius"] = S(5.0f);
        btn["shaderVertex"] = "button_vertex.glsl";
        btn["shaderFragment"] = "button_fragment.glsl";
        btn["isHitTestable"] = true;
        btn["clickEvent"] = clickEvent;
        return btn;
    };

    stack["children"] = json::array({
        title, clearLabel, colorPicker, separator, toolsLabel,
        makeToolBtn("WorldSettings.Tools.Landscape", "Landscape Manager...", "WorldSettings.Tools.Landscape"),
        makeToolBtn("WorldSettings.Tools.MaterialEditor", "Material Editor...", "WorldSettings.Tools.MaterialEditor")
    });

    elements.push_back(stack);
    widgetJson["m_elements"] = elements;

    auto widget = std::make_shared<AssetData>();
    widget->setName("WorldSettings");
    widget->setData(std::move(widgetJson));

    const fs::path widgetsRoot = getEditorWidgetsRootPath();
    std::error_code ec;
    fs::create_directories(widgetsRoot, ec);
    const fs::path abs = widgetsRoot / fs::path(worldSettingsWidgetRel);
    bool existsAndOk = false;
    if (fs::exists(abs))
    {
        AssetType headerType{ AssetType::Unknown };
        existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Widget;
        if (existsAndOk)
        {
            std::ifstream in(abs, std::ios::in | std::ios::binary);
            if (in.is_open())
            {
                json fileJson = json::parse(in, nullptr, false);
                existsAndOk = false;
                if (!fileJson.is_discarded() && fileJson.is_object() && fileJson.contains("data"))
                {
                    const auto& data = fileJson.at("data");
                    if (data.is_object() && data.contains("_uiVersion") &&
                        data.at("_uiVersion").is_number_integer() &&
                        data.at("_uiVersion").get<int>() >= kUIVersion &&
                        data.contains("_dpiScale") && data.at("_dpiScale").is_number() &&
                        std::abs(data.at("_dpiScale").get<float>() - dpi) < 0.01f)
                    {
                        existsAndOk = true;
                    }
                }
            }
        }
    }
    if (!existsAndOk)
    {
        std::ofstream out(abs, std::ios::out | std::ios::trunc);
        if (out.is_open())
        {
            json fileJson = json::object();
            fileJson["magic"] = 0x41535453;
            fileJson["version"] = 2;
            fileJson["type"] = static_cast<int>(AssetType::Widget);
            fileJson["name"] = widget->getName();
            fileJson["data"] = widget->getData();
            out << fileJson.dump(4);
            if (!out.good())
            {
                logger.log(Logger::Category::AssetManagement, "Failed to write editor widget asset.", Logger::LogLevel::ERROR);
            }
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Failed to open editor widget asset for writing.", Logger::LogLevel::ERROR);
        }
    }
}

void AssetManager::ensureDefaultAssetsCreated()
{
    auto& diagnostics = DiagnosticsManager::Instance();
    auto& logger = Logger::Instance();
    if (!diagnostics.isProjectLoaded() || diagnostics.getProjectInfo().projectPath.empty())
    {
        // No project context => can't create/save project assets.
		logger.log(Logger::Category::AssetManagement, "Cannot ensure default assets: no project loaded.", Logger::LogLevel::ERROR);
        return;
    }

    
    logger.log(Logger::Category::AssetManagement, "Ensuring default assets exist for project...", Logger::LogLevel::INFO);

    const fs::path contentRoot = fs::path(diagnostics.getProjectInfo().projectPath) / "Content";

    const auto ensureOnDisk = [&](const std::string& relPath, AssetType expectedType, const std::shared_ptr<AssetData>& obj, bool forceOverwrite = false) -> bool
    {
        const fs::path abs = contentRoot / fs::path(relPath);

        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == expectedType;
        }

        if (forceOverwrite && existsAndOk)
        {
            existsAndOk = false;
        }

        if (existsAndOk)
        {
            logger.log(Logger::Category::AssetManagement, "Default asset OK: " + relPath, Logger::LogLevel::INFO);
            return true;
        }

        if (!obj)
        {
            logger.log(Logger::Category::AssetManagement, "Default asset missing/invalid and cannot be created: " + relPath, Logger::LogLevel::ERROR);
            return false;
        }

        logger.log(
            Logger::Category::AssetManagement,
            std::string("Default asset ") + (fs::exists(abs) ? "invalid (type mismatch), overwriting: " : "missing, creating: ") + relPath,
            Logger::LogLevel::WARNING);

        obj->setPath(relPath);
        obj->setAssetType(expectedType);
        obj->setType(expectedType);
        obj->setIsSaved(false);
		auto id = registerLoadedAsset(obj);
        if (id != 0)
        {
            obj->setId(id);
        }
		Asset asset;
		asset.ID = id;
		asset.type = expectedType;
        return saveAsset(asset);
    };

    // 1) wall texture
    const std::string wallTexRel = (fs::path("Textures") / "wall.asset").generic_string();
    {
        auto tex = std::make_shared<AssetData>();
        tex->setName("wall");
        json texData = json::object();
        const fs::path wallPngPath = fs::current_path() / "Content" / "Textures" / "wall.jpg";
        if (fs::exists(wallPngPath))
        {
            texData["m_sourcePath"] = (fs::path("Content") / "Textures" / "wall.jpg").generic_string();
        }
        else
        {
            int width = 2;
            int height = 2;
            int channels = 4;
            std::vector<unsigned char> pixels{
                255,   0,   0, 255,   0, 255,   0, 255,
                  0,   0, 255, 255, 255, 255,   0, 255
            };
            texData["m_width"] = width;
            texData["m_height"] = height;
            texData["m_channels"] = channels;
            texData["m_data"] = std::move(pixels);
        }
        tex->setData(std::move(texData));
        const bool forceOverwrite = fs::exists(wallPngPath);
        ensureOnDisk(wallTexRel, AssetType::Texture, tex, forceOverwrite);
    }

	// 2) wall material
	const std::string wallMatRel = (fs::path("Materials") / "wall.asset").generic_string();
	{
		auto mat = std::make_shared<AssetData>();
		mat->setName("DefaultDebugMaterial");
		json matData = json::object();
		matData["m_textureAssetPaths"] = std::vector<std::string>{ wallTexRel };
		mat->setData(std::move(matData));
		ensureOnDisk(wallMatRel, AssetType::Material, mat);
	}

	// 2b) container2 textures + material (diffuse + specular)
	const std::string container2TexRel = (fs::path("Textures") / "container2.asset").generic_string();
	{
		auto tex = std::make_shared<AssetData>();
		tex->setName("container2");
		json texData = json::object();
		const fs::path container2PngPath = fs::current_path() / "Content" / "Textures" / "container2.png";
		if (fs::exists(container2PngPath))
		{
			texData["m_sourcePath"] = (fs::path("Content") / "Textures" / "container2.png").generic_string();
		}
		else
		{
			int width = 2;
			int height = 2;
			int channels = 4;
			std::vector<unsigned char> pixels{
				200, 150, 100, 255, 180, 130,  80, 255,
				160, 110,  60, 255, 140,  90,  40, 255
			};
			texData["m_width"] = width;
			texData["m_height"] = height;
			texData["m_channels"] = channels;
			texData["m_data"] = std::move(pixels);
		}
		tex->setData(std::move(texData));
		const bool forceOverwrite = fs::exists(container2PngPath);
		ensureOnDisk(container2TexRel, AssetType::Texture, tex, forceOverwrite);
	}

	const std::string container2SpecTexRel = (fs::path("Textures") / "container2_specular.asset").generic_string();
	{
		auto tex = std::make_shared<AssetData>();
		tex->setName("container2_specular");
		json texData = json::object();
		const fs::path container2SpecPath = fs::current_path() / "Content" / "Textures" / "container2_specular.png";
		if (fs::exists(container2SpecPath))
		{
			texData["m_sourcePath"] = (fs::path("Content") / "Textures" / "container2_specular.png").generic_string();
		}
		else
		{
			int width = 2;
			int height = 2;
			int channels = 4;
			std::vector<unsigned char> pixels{
				128, 128, 128, 255, 200, 200, 200, 255,
				200, 200, 200, 255, 128, 128, 128, 255
			};
			texData["m_width"] = width;
			texData["m_height"] = height;
			texData["m_channels"] = channels;
			texData["m_data"] = std::move(pixels);
		}
		tex->setData(std::move(texData));
		const bool forceOverwrite = fs::exists(fs::current_path() / "Content" / "Textures" / "container2_specular.png");
		ensureOnDisk(container2SpecTexRel, AssetType::Texture, tex, forceOverwrite);
	}

	const std::string containerMatRel = (fs::path("Materials") / "container.asset").generic_string();
	{
		auto mat = std::make_shared<AssetData>();
		mat->setName("ContainerMaterial");
		json matData = json::object();
		matData["m_textureAssetPaths"] = std::vector<std::string>{ container2TexRel, container2SpecTexRel };
		matData["m_shininess"] = 64.0f;
		mat->setData(std::move(matData));
		ensureOnDisk(containerMatRel, AssetType::Material, mat);
	}

	const auto scriptingMode = diagnostics.getProjectInfo().scriptingMode;
	const bool wantPython = (scriptingMode == DiagnosticsManager::ScriptingMode::PythonOnly || scriptingMode == DiagnosticsManager::ScriptingMode::Both);
	const bool wantCpp    = (scriptingMode == DiagnosticsManager::ScriptingMode::CppOnly    || scriptingMode == DiagnosticsManager::ScriptingMode::Both);

	const std::string defaultCubeNames[] = { "DefaultCube1", "DefaultCube2", "DefaultCube3", "DefaultCube4", "DefaultCube5" };
	const std::string defaultCubeScripts[5] = {
		(fs::path("Scripts") / "DefaultCube1.py").generic_string(),
		(fs::path("Scripts") / "DefaultCube2.py").generic_string(),
		(fs::path("Scripts") / "DefaultCube3.py").generic_string(),
		(fs::path("Scripts") / "DefaultCube4.py").generic_string(),
		(fs::path("Scripts") / "DefaultCube5.py").generic_string()
	};

	// Auto-create Python scripts
	if (wantPython)
	{
		for (const auto& scriptRel : defaultCubeScripts)
		{
			const fs::path absScript = contentRoot / fs::path(scriptRel);
			if (fs::exists(absScript)) continue;
			std::error_code ec;
			fs::create_directories(absScript.parent_path(), ec);
			std::ofstream out(absScript, std::ios::out | std::ios::trunc);
			if (out.is_open())
			{
				out << "import engine\n\n";
				out << "def onloaded(entity):\n";
				out << "    pass\n\n";
				out << "def tick(entity, dt):\n";
				out << "    pass\n\n";
				out << "def on_entity_begin_overlap(entity, other_entity):\n";
				out << "    pass\n\n";
				out << "def on_entity_end_overlap(entity, other_entity):\n";
				out << "    pass\n";
			}
		}
	}

	// Auto-create C++ class files
	if (wantCpp)
	{
		const fs::path nativeDir = contentRoot / "Scripts" / "Native";
		std::error_code ec;
		fs::create_directories(nativeDir, ec);
		for (const auto& name : defaultCubeNames)
		{
			const fs::path headerFile = nativeDir / (name + ".h");
			if (!fs::exists(headerFile))
			{
				std::ofstream out(headerFile);
				if (out.is_open())
				{
					out << "#pragma once\n";
					out << "#include \"INativeScript.h\"\n\n";
					out << "class " << name << " : public INativeScript\n";
					out << "{\n";
					out << "public:\n";
					out << "    void onLoaded() override;\n";
					out << "    void tick(float deltaTime) override;\n";
					out << "    void onBeginOverlap(ECS::Entity other) override;\n";
					out << "    void onEndOverlap(ECS::Entity other) override;\n";
					out << "    void onDestroy() override;\n";
					out << "};\n";
				}
			}
			const fs::path cppFile = nativeDir / (name + ".cpp");
			if (!fs::exists(cppFile))
			{
				std::ofstream out(cppFile);
				if (out.is_open())
				{
					out << "#include \"" << name << ".h\"\n\n";
					out << "void " << name << "::onLoaded()\n";
					out << "{\n}\n\n";
					out << "void " << name << "::tick(float deltaTime)\n";
					out << "{\n}\n\n";
					out << "void " << name << "::onBeginOverlap(ECS::Entity other)\n";
					out << "{\n}\n\n";
					out << "void " << name << "::onEndOverlap(ECS::Entity other)\n";
					out << "{\n}\n\n";
					out << "void " << name << "::onDestroy()\n";
					out << "{\n}\n";
				}
			}
		}
	}

    // 3) default 3D quad model

    const std::vector<float> cubeVertices{
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
     0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
     0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
     0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
     0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
    };
    const std::string quad3dRel = "default_quad3d.asset";
    const std::string pointLightRel = (fs::path("Lights") / "PointLight.asset").generic_string();
    {

        const fs::path abs = contentRoot / fs::path(quad3dRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Model3D;
        }

        if (existsAndOk)
        {
            logger.log(Logger::Category::AssetManagement, "Default asset OK: " + quad3dRel, Logger::LogLevel::INFO);
        }
        else
        {
            auto quad = std::make_shared<AssetData>();
            quad->setName("DefaultQuad3D");
				// Material assets/textures are handled by Material; Object3D only stores a runtime Material instance.
            json quadData = json::object();
            quadData["m_vertices"] = cubeVertices;
            quadData["m_indices"] = std::vector<uint32_t>{};
            quad->setData(std::move(quadData));

            ensureOnDisk(quad3dRel, AssetType::Model3D, quad);
        }
    }

    {
        const fs::path abs = contentRoot / fs::path(pointLightRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::PointLight;
        }

        if (!existsAndOk)
        {
            auto lightAsset = std::make_shared<AssetData>();
            lightAsset->setName("PointLight");
            json lightData = json::object();
            lightData["m_vertices"] = cubeVertices;
            lightData["m_indices"] = std::vector<uint32_t>{};
            lightData["m_shaderVertex"] = "vertex.glsl";
            lightData["m_shaderFragment"] = "light_fragment.glsl";
            lightAsset->setData(std::move(lightData));
            ensureOnDisk(pointLightRel, AssetType::PointLight, lightAsset);
        }
    }

    const std::string defaultLevelRel = (fs::path("Levels") / "DefaultLevel.map").generic_string();
    {
        std::error_code ec;
        const fs::path scriptsRoot = contentRoot / "Scripts";
        fs::create_directories(scriptsRoot, ec);

        const std::string defaultLevelScriptRel = (fs::path("Scripts") / "LevelScript.py").generic_string();
        const fs::path defaultLevelScriptAbs = scriptsRoot / "LevelScript.py";
        if (!fs::exists(defaultLevelScriptAbs))
        {
            std::ofstream scriptOut(defaultLevelScriptAbs, std::ios::out | std::ios::trunc);
            if (scriptOut.is_open())
            {
                scriptOut << "def on_level_loaded():\n";
                scriptOut << "    pass\n\n";
                scriptOut << "def on_level_unloaded():\n";
                scriptOut << "    pass\n";
            }
        }

        json levelJson = json::object();
        levelJson["Objects"] = json::array();
        levelJson["Groups"] = json::array();

        json entities = json::array();
        json entity = json::object();
        entity["id"] = 1;

        json components = json::object();
        components["Transform"] = json{
            {"position", json::array({ 0.0f, 0.0f, 0.0f })},
            {"rotation", json::array({ 0.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components["Material"] = json{ {"materialAssetPath", wallMatRel} };
        components["Name"] = json{ {"displayName", "Cube A"} };
        {
            json logicJson;
            if (wantPython) logicJson["scriptPath"] = defaultCubeScripts[0];
            if (wantCpp)    logicJson["nativeClassName"] = defaultCubeNames[0];
            components["Logic"] = logicJson;
        }
        entity["components"] = components;
        entities.push_back(entity);

        json entity2 = json::object();
        entity2["id"] = 2;

        json components2 = json::object();
        components2["Transform"] = json{
            {"position", json::array({ 2.0f, 0.0f, 0.0f })},
            {"rotation", json::array({ 0.0f, 45.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components2["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components2["Material"] = json{ {"materialAssetPath", wallMatRel} };
        {
            json logicJson;
            if (wantPython) logicJson["scriptPath"] = defaultCubeScripts[1];
            if (wantCpp)    logicJson["nativeClassName"] = defaultCubeNames[1];
            components2["Logic"] = logicJson;
        }
        components2["Name"] = json{ {"displayName", "Cube B"} };
        entity2["components"] = components2;
        entities.push_back(entity2);

        json entity3 = json::object();
        entity3["id"] = 3;

        json components3 = json::object();
        components3["Transform"] = json{
            {"position", json::array({ -2.0f, 0.0f, 0.0f })},
            {"rotation", json::array({ 0.0f, -30.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components3["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components3["Material"] = json{ {"materialAssetPath", wallMatRel} };
        {
            json logicJson;
            if (wantPython) logicJson["scriptPath"] = defaultCubeScripts[2];
            if (wantCpp)    logicJson["nativeClassName"] = defaultCubeNames[2];
            components3["Logic"] = logicJson;
        }
        components3["Name"] = json{ {"displayName", "Cube C"} };
        entity3["components"] = components3;
        entities.push_back(entity3);

        json entity4 = json::object();
        entity4["id"] = 4;

        json components4 = json::object();
        components4["Transform"] = json{
            {"position", json::array({ 0.0f, 0.0f, 2.5f })},
            {"rotation", json::array({ 0.0f, 90.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components4["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components4["Material"] = json{ {"materialAssetPath", containerMatRel} };
        {
            json logicJson;
            if (wantPython) logicJson["scriptPath"] = defaultCubeScripts[3];
            if (wantCpp)    logicJson["nativeClassName"] = defaultCubeNames[3];
            components4["Logic"] = logicJson;
        }
        components4["Name"] = json{ {"displayName", "Cube D"} };
        entity4["components"] = components4;
        entities.push_back(entity4);

        json entity5 = json::object();
        entity5["id"] = 5;

        json components5 = json::object();
        components5["Transform"] = json{
            {"position", json::array({ 0.0f, 0.0f, -2.5f })},
            {"rotation", json::array({ 0.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 1.0f, 1.0f, 1.0f })}
        };
        components5["Mesh"] = json{ {"meshAssetPath", quad3dRel} };
        components5["Material"] = json{ {"materialAssetPath", containerMatRel} };
        {
            json logicJson;
            if (wantPython) logicJson["scriptPath"] = defaultCubeScripts[4];
            if (wantCpp)    logicJson["nativeClassName"] = defaultCubeNames[4];
            components5["Logic"] = logicJson;
        }
        components5["Name"] = json{ {"displayName", "Cube E"} };
        entity5["components"] = components5;
        entities.push_back(entity5);

        json lightEntity = json::object();
        lightEntity["id"] = 6;

        json lightComponents = json::object();
        lightComponents["Transform"] = json{
            {"position", json::array({ 1.0f, 1.2f, 2.5f })},
            {"rotation", json::array({ 0.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 0.25f, 0.25f, 0.25f })}
        };
        lightComponents["Mesh"] = json{ {"meshAssetPath", pointLightRel} };
        lightComponents["Material"] = json{ {"materialAssetPath", wallMatRel} };
        lightComponents["Name"] = json{ {"displayName", "Point Light"} };
        lightComponents["Light"] = json{
            {"type", static_cast<int>(ECS::LightComponent::LightType::Point)},
            {"color", json::array({ 1.0f, 1.0f, 1.0f })},
            {"intensity", 1.0f},
            {"range", 10.0f},
            {"spotAngle", 30.0f}
        };
        lightEntity["components"] = lightComponents;
        entities.push_back(lightEntity);

        json dirLightEntity = json::object();
        dirLightEntity["id"] = 7;

        json dirLightComponents = json::object();
        dirLightComponents["Transform"] = json{
            {"position", json::array({ 0.0f, 5.0f, 0.0f })},
            {"rotation", json::array({ 50.0f, -30.0f, 0.0f })},
            {"scale", json::array({ 0.15f, 0.15f, 0.15f })}
        };
        dirLightComponents["Mesh"] = json{ {"meshAssetPath", pointLightRel} };
        dirLightComponents["Material"] = json{ {"materialAssetPath", wallMatRel} };
        dirLightComponents["Name"] = json{ {"displayName", "Directional Light"} };
        dirLightComponents["Light"] = json{
            {"type", static_cast<int>(ECS::LightComponent::LightType::Directional)},
            {"color", json::array({ 0.9f, 0.85f, 0.7f })},
            {"intensity", 0.4f},
            {"range", 0.0f},
            {"spotAngle", 0.0f}
        };
        dirLightEntity["components"] = dirLightComponents;
        entities.push_back(dirLightEntity);

        json spotLightEntity = json::object();
        spotLightEntity["id"] = 8;

        json spotLightComponents = json::object();
        spotLightComponents["Transform"] = json{
            {"position", json::array({ 2.0f, 2.5f, 0.0f })},
            {"rotation", json::array({ 60.0f, 0.0f, 0.0f })},
            {"scale", json::array({ 0.15f, 0.15f, 0.15f })}
        };
        spotLightComponents["Mesh"] = json{ {"meshAssetPath", pointLightRel} };
        spotLightComponents["Material"] = json{ {"materialAssetPath", wallMatRel} };
        spotLightComponents["Name"] = json{ {"displayName", "Spot Light"} };
        spotLightComponents["Light"] = json{
            {"type", static_cast<int>(ECS::LightComponent::LightType::Spot)},
            {"color", json::array({ 0.2f, 0.8f, 1.0f })},
            {"intensity", 3.5f},
            {"range", 25.0f},
            {"spotAngle", 25.0f}
        };
        spotLightEntity["components"] = spotLightComponents;
        entities.push_back(spotLightEntity);

        levelJson["Entities"] = entities;

        auto defaultLevel = std::make_unique<EngineLevel>();
        defaultLevel->setName("DefaultLevel");
        defaultLevel->setPath(defaultLevelRel);
        defaultLevel->setAssetType(AssetType::Level);
        defaultLevel->setLevelData(levelJson);
        const fs::path abs = contentRoot / fs::path(defaultLevelRel);
        bool existsAndOk = false;
        if (fs::exists(abs))
        {
            AssetType headerType{ AssetType::Unknown };
            existsAndOk = readAssetHeaderType(abs, headerType) && headerType == AssetType::Level;
        }
        if (!existsAndOk)
        {
            logger.log(Logger::Category::AssetManagement, "Default level missing/invalid, creating: " + defaultLevelRel, Logger::LogLevel::WARNING);
            if (!saveLevelAsset(defaultLevel).success)
            {
                logger.log(Logger::Category::AssetManagement, "Failed to save default level asset.", Logger::LogLevel::ERROR);
            }
            diagnostics.setActiveLevel(std::move(defaultLevel));
        }
        else
        {
            logger.log(Logger::Category::AssetManagement, "Default level OK, loading from disk: " + defaultLevelRel, Logger::LogLevel::INFO);
            auto loadResult = loadLevelAsset(abs.string());
            if (!loadResult.success)
            {
                logger.log(Logger::Category::AssetManagement, "Failed to load existing level from disk, falling back to defaults: " + loadResult.errorMessage, Logger::LogLevel::WARNING);
                diagnostics.setActiveLevel(std::move(defaultLevel));
            }
        }
		diagnostics.setScenePrepared(false);
	}

	// Default Skybox assets â€” generate from engine-bundled skybox texture sets
	{
		const fs::path engineSkyboxRoot = fs::current_path() / "Content" / "Textures" / "SkyBoxes";
		const std::string skyboxNames[] = { "Sunrise", "Daytime" };
		// Each canonical face name maps to alternative file names (e.g. top/up, bottom/down)
		struct FaceAlias { std::string canonical; std::vector<std::string> names; };
		const FaceAlias faceAliases[] = {
			{ "right",  { "right" } },
			{ "left",   { "left" } },
			{ "top",    { "top", "up" } },
			{ "bottom", { "bottom", "down" } },
			{ "front",  { "front" } },
			{ "back",   { "back" } }
		};
		const std::string faceExts[] = { ".jpg", ".png", ".bmp" };

		for (const auto& skyboxName : skyboxNames)
		{
			const fs::path engineSkyboxDir = engineSkyboxRoot / skyboxName;
			if (!fs::exists(engineSkyboxDir) || !fs::is_directory(engineSkyboxDir))
			{
				continue;
			}

			// Check that at least one face image exists
			bool hasAnyFace = false;
			for (const auto& fa : faceAliases)
			{
				for (const auto& name : fa.names)
				{
					for (const auto& ext : faceExts)
					{
						if (fs::exists(engineSkyboxDir / (name + ext)))
						{
							hasAnyFace = true;
							break;
						}
					}
					if (hasAnyFace) break;
				}
				if (hasAnyFace) break;
			}
			if (!hasAnyFace)
			{
				logger.log(Logger::Category::AssetManagement,
					"Skybox '" + skyboxName + "' folder exists but contains no face images, skipping.",
					Logger::LogLevel::WARNING);
				continue;
			}

			// Copy face images to project Content/Skyboxes/<name>/
			const fs::path projectSkyboxDir = contentRoot / "Skyboxes" / skyboxName;
			std::error_code ec;
			fs::create_directories(projectSkyboxDir, ec);

			json facesJson = json::object();
			for (const auto& fa : faceAliases)
			{
				bool found = false;
				for (const auto& name : fa.names)
				{
					for (const auto& ext : faceExts)
					{
						const fs::path srcFace = engineSkyboxDir / (name + ext);
						if (fs::exists(srcFace))
						{
							// Copy with the original filename so the renderer can find it
							const fs::path destFace = projectSkyboxDir / (name + ext);
							fs::copy_file(srcFace, destFace, fs::copy_options::skip_existing, ec);
							facesJson[fa.canonical] = fs::relative(destFace, contentRoot).generic_string();
							found = true;
							break;
						}
					}
					if (found) break;
				}
			}

			// Create the .asset file
			const std::string skyboxAssetRel = (fs::path("Skyboxes") / (skyboxName + ".asset")).generic_string();
			auto skybox = std::make_shared<AssetData>();
			skybox->setName(skyboxName);
			json skyboxData = json::object();
			skyboxData["faces"] = facesJson;
			skyboxData["folderPath"] = fs::relative(projectSkyboxDir, fs::path(diagnostics.getProjectInfo().projectPath)).generic_string();
			skybox->setData(std::move(skyboxData));
			ensureOnDisk(skyboxAssetRel, AssetType::Skybox, skybox);
		}
	}

	// Default editor themes (Dark + Light JSON files in Editor/Themes/)
	EditorTheme::EnsureDefaultThemes();

	logger.log(Logger::Category::AssetManagement, "Default assets ensured.", Logger::LogLevel::INFO);
}
#endif
