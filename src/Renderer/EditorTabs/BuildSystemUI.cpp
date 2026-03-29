#include "BuildSystemUI.h"
#include "../UIManager.h"
#include "../Renderer.h"
#include "../EditorTheme.h"
#include "../EditorUIBuilder.h"
#include "../EditorUI/EditorWidget.h"
#include "../EditorWindows/PopupWindow.h"
#include "../../Logger/Logger.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"

#include "../../AssetManager/json.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

#if defined(_WIN32)
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <Windows.h>
#   include <shellapi.h>
#endif

#include <SDL3/SDL.h>

using json = nlohmann::json;
using NotificationLevel = DiagnosticsManager::NotificationLevel;

namespace {
    WidgetElement* FindElementById(WidgetElement& element, const std::string& id)
    {
        if (element.id == id) return &element;
        for (auto& child : element.children)
        {
            if (auto* match = FindElementById(child, id))
                return match;
        }
        return nullptr;
    }
    WidgetElement* FindElementById(std::vector<WidgetElement>& elements, const std::string& id)
    {
        for (auto& element : elements)
        {
            if (auto* match = FindElementById(element, id))
                return match;
        }
        return nullptr;
    }
} // anonymous namespace

#if ENGINE_EDITOR

BuildSystemUI::BuildSystemUI(UIManager* uiManager, Renderer* renderer)
    : m_uiManager(uiManager), m_renderer(renderer) {}

BuildSystemUI::~BuildSystemUI()
{
    if (m_buildThread.joinable())
        m_buildThread.join();
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Build Profiles (Phase 10.3) ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬

void BuildSystemUI::loadBuildProfiles()
{
    m_buildProfiles.clear();

    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return;

    const auto profileDir = std::filesystem::path(projPath) / "Config" / "BuildProfiles";

    // Load existing profiles from JSON files
    if (std::filesystem::exists(profileDir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(profileDir))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;

            try
            {
                std::ifstream ifs(entry.path());
                if (!ifs.is_open()) continue;
                auto j = nlohmann::json::parse(ifs);

                BuildProfile p;
                if (j.contains("name"))            p.name            = j["name"].get<std::string>();
                if (j.contains("cmakeBuildType"))   p.cmakeBuildType  = j["cmakeBuildType"].get<std::string>();
                if (j.contains("logLevel"))         p.logLevel        = j["logLevel"].get<std::string>();
                if (j.contains("enableHotReload"))  p.enableHotReload = j["enableHotReload"].get<bool>();
                if (j.contains("enableValidation")) p.enableValidation= j["enableValidation"].get<bool>();
                if (j.contains("enableProfiler"))   p.enableProfiler  = j["enableProfiler"].get<bool>();
                if (j.contains("compressAssets"))    p.compressAssets  = j["compressAssets"].get<bool>();

                m_buildProfiles.push_back(std::move(p));
            }
            catch (...) { /* skip malformed files */ }
        }
    }

    // If no profiles exist, create 3 defaults
    if (m_buildProfiles.empty())
    {
        BuildProfile debug;
        debug.name            = "Debug";
        debug.cmakeBuildType  = "Debug";
        debug.logLevel        = "verbose";
        debug.enableHotReload = true;
        debug.enableValidation= true;
        debug.enableProfiler  = true;
        debug.compressAssets  = false;

        BuildProfile dev;
        dev.name              = "Development";
        dev.cmakeBuildType    = "RelWithDebInfo";
        dev.logLevel          = "info";
        dev.enableHotReload   = true;
        dev.enableValidation  = false;
        dev.enableProfiler    = true;
        dev.compressAssets    = false;

        BuildProfile ship;
        ship.name             = "Shipping";
        ship.cmakeBuildType   = "Release";
        ship.logLevel         = "error";
        ship.enableHotReload  = false;
        ship.enableValidation = false;
        ship.enableProfiler   = false;
        ship.compressAssets   = true;

        m_buildProfiles.push_back(debug);
        m_buildProfiles.push_back(dev);
        m_buildProfiles.push_back(ship);

        for (const auto& p : m_buildProfiles)
            saveBuildProfile(p);
    }
}

void BuildSystemUI::saveBuildProfile(const BuildProfile& profile)
{
    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return;

    const auto profileDir = std::filesystem::path(projPath) / "Config" / "BuildProfiles";
    std::error_code ec;
    std::filesystem::create_directories(profileDir, ec);

    nlohmann::json j;
    j["name"]            = profile.name;
    j["cmakeBuildType"]  = profile.cmakeBuildType;
    j["logLevel"]        = profile.logLevel;
    j["enableHotReload"] = profile.enableHotReload;
    j["enableValidation"]= profile.enableValidation;
    j["enableProfiler"]  = profile.enableProfiler;
    j["compressAssets"]  = profile.compressAssets;

    const auto filePath = profileDir / (profile.name + ".json");
    std::ofstream ofs(filePath);
    if (ofs.is_open())
        ofs << j.dump(4);
}

void BuildSystemUI::deleteBuildProfile(const std::string& name)
{
    const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projPath.empty()) return;

    const auto filePath = std::filesystem::path(projPath) / "Config" / "BuildProfiles" / (name + ".json");
    std::error_code ec;
    std::filesystem::remove(filePath, ec);

    m_buildProfiles.erase(
        std::remove_if(m_buildProfiles.begin(), m_buildProfiles.end(),
            [&](const BuildProfile& p) { return p.name == name; }),
        m_buildProfiles.end());
}

void BuildSystemUI::openBuildGameDialog()
{
    if (!m_renderer) return;

    // Ensure profiles are loaded
    if (m_buildProfiles.empty())
        loadBuildProfiles();

    constexpr float kBaseW = 520.0f;
    constexpr float kBaseH = 520.0f;
    const int kPopupW = static_cast<int>(EditorTheme::Scaled(kBaseW));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(kBaseH));
    PopupWindow* popup = m_renderer->openPopupWindow(
        "BuildGame", "Build Game", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return;

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    // Shared mutable state for the form
    struct FormState
    {
        std::string startLevel;
        std::string windowTitle = "Game";
        int profileIndex = 0;
        bool launchAfter = true;
        bool cleanBuild = false;
    };
    auto formState = std::make_shared<FormState>();

    // Collect all available levels for the dropdown
    std::vector<std::string> levelPaths;
    int preSelectedLevelIdx = -1;
    {
        auto& diag = DiagnosticsManager::Instance();
        auto& assetMgr = AssetManager::Instance();
        const auto& registry = assetMgr.getAssetRegistry();
        for (const auto& entry : registry)
        {
            if (entry.type == AssetType::Level)
                levelPaths.push_back(entry.path);
        }
        if (!levelPaths.empty())
        {
            preSelectedLevelIdx = 0;
            formState->startLevel = levelPaths[0];
        }

        // Pre-fill window title from project name
        formState->windowTitle = diag.getProjectInfo().projectName;
        if (formState->windowTitle.empty())
            formState->windowTitle = "Game";
    }

    // Standardized output and binary dirs
    std::string defaultOutputDir;
    std::string defaultBinaryDir;
    {
        auto& diag = DiagnosticsManager::Instance();
        const auto& projPath = diag.getProjectInfo().projectPath;
        if (!projPath.empty())
        {
            defaultOutputDir = (std::filesystem::path(projPath) / "Build").string();
            defaultBinaryDir = (std::filesystem::path(projPath) / "Binary").string();
        }
    }

    // Build profile names for dropdown
    std::vector<std::string> profileNames;
    int preSelectedProfileIdx = 0;
    for (size_t i = 0; i < m_buildProfiles.size(); ++i)
    {
        profileNames.push_back(m_buildProfiles[i].name);
        if (m_buildProfiles[i].name == "Development")
            preSelectedProfileIdx = static_cast<int>(i);
    }
    formState->profileIndex = preSelectedProfileIdx;

    std::vector<WidgetElement> elements;

    // Background
    {
        WidgetElement bg;
        bg.type = WidgetElementType::Panel;
        bg.id = "BG.Bg";
        bg.from = Vec2{ 0.0f, 0.0f };
        bg.to = Vec2{ 1.0f, 1.0f };
        bg.style.color = EditorTheme::Get().panelBackground;
        elements.push_back(std::move(bg));
    }

    // Title
    {
        WidgetElement title;
        title.type = WidgetElementType::Text;
        title.id = "BG.Title";
        title.from = Vec2{ nx(16.0f), ny(8.0f) };
        title.to = Vec2{ nx(W - 16.0f), ny(40.0f) };
        title.text = "Build Game";
        title.fontSize = EditorTheme::Get().fontSizeHeading;
        title.style.textColor = EditorTheme::Get().titleBarText;
        title.textAlignV = TextAlignV::Center;
        title.padding = Vec2{ 6.0f, 0.0f };
        elements.push_back(std::move(title));
    }

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Form fields ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    const float rowH = 24.0f;
    const float entryH = 22.0f;
    const float labelW = 120.0f;
    const float gap = 6.0f;
    const float leftPad = 20.0f;
    const float rightPad = 20.0f;
    float curY = 50.0f;

    auto addLabel = [&](const std::string& id, const std::string& text, float y)
    {
        WidgetElement lbl;
        lbl.type = WidgetElementType::Text;
        lbl.id = id;
        lbl.from = Vec2{ nx(leftPad), ny(y) };
        lbl.to = Vec2{ nx(leftPad + labelW), ny(y + rowH) };
        lbl.text = text;
        lbl.fontSize = EditorTheme::Get().fontSizeBody;
        lbl.style.textColor = EditorTheme::Get().textPrimary;
        lbl.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(lbl));
    };

    auto addEntry = [&](const std::string& id, const std::string& defaultVal, float y) -> std::string
    {
        WidgetElement entry;
        entry.type = WidgetElementType::EntryBar;
        entry.id = id;
        entry.from = Vec2{ nx(leftPad + labelW + gap), ny(y) };
        entry.to = Vec2{ nx(W - rightPad), ny(y + entryH) };
        entry.text = defaultVal;
        entry.fontSize = EditorTheme::Get().fontSizeBody;
        entry.style.color = EditorTheme::Get().inputBackground;
        entry.style.textColor = EditorTheme::Get().textPrimary;
        entry.padding = Vec2{ 6.0f, 4.0f };
        entry.minSize = Vec2{ 0.0f, entryH };
        entry.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(entry));
        return id;
    };

    // Build Profile (DropDown)
    addLabel("BG.Lbl.Profile", "Build Profile:", curY);
    {
        WidgetElement dd;
        dd.type = WidgetElementType::DropDown;
        dd.id = "BG.DD.Profile";
        dd.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        dd.to = Vec2{ nx(W - rightPad), ny(curY + entryH) };
        dd.items = profileNames;
        dd.selectedIndex = preSelectedProfileIdx;
        if (preSelectedProfileIdx >= 0 && preSelectedProfileIdx < static_cast<int>(profileNames.size()))
            dd.text = profileNames[static_cast<size_t>(preSelectedProfileIdx)];
        dd.fontSize = EditorTheme::Get().fontSizeBody;
        dd.style.color = EditorTheme::Get().inputBackground;
        dd.style.textColor = EditorTheme::Get().textPrimary;
        dd.padding = Vec2{ 6.0f, 4.0f };
        dd.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(dd));
    }
    curY += rowH + gap;

    // Profile info line (read-only, shows brief profile settings)
    {
        std::string profileInfo;
        if (!m_buildProfiles.empty())
        {
            const auto& p = m_buildProfiles[static_cast<size_t>(preSelectedProfileIdx)];
            profileInfo = "CMake: " + p.cmakeBuildType + "  |  Log: " + p.logLevel
                + "  |  HotReload: " + (p.enableHotReload ? "on" : "off")
                + "  |  Profiler: " + (p.enableProfiler ? "on" : "off");
        }
        WidgetElement info;
        info.type = WidgetElementType::Text;
        info.id = "BG.ProfileInfo";
        info.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        info.to = Vec2{ nx(W - rightPad), ny(curY + rowH) };
        info.text = profileInfo;
        info.fontSize = EditorTheme::Get().fontSizeSmall;
        info.style.textColor = EditorTheme::Get().textMuted;
        info.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(info));
    }
    curY += rowH + gap;

    // Start Level (DropDown)
    addLabel("BG.Lbl.StartLevel", "Start Level:", curY);
    {
        WidgetElement dd;
        dd.type = WidgetElementType::DropDown;
        dd.id = "BG.DD.StartLevel";
        dd.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        dd.to = Vec2{ nx(W - rightPad), ny(curY + entryH) };
        dd.items = levelPaths;
        dd.selectedIndex = preSelectedLevelIdx;
        if (preSelectedLevelIdx >= 0 && preSelectedLevelIdx < static_cast<int>(levelPaths.size()))
            dd.text = levelPaths[static_cast<size_t>(preSelectedLevelIdx)];
        dd.fontSize = EditorTheme::Get().fontSizeBody;
        dd.style.color = EditorTheme::Get().inputBackground;
        dd.style.textColor = EditorTheme::Get().textPrimary;
        dd.padding = Vec2{ 6.0f, 4.0f };
        dd.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(dd));
    }
    curY += rowH + gap;

    // Window Title
    addLabel("BG.Lbl.Title", "Window Title:", curY);
    addEntry("BG.Entry.Title", formState->windowTitle, curY);
    curY += rowH + gap;

    // Launch after build checkbox
    addLabel("BG.Lbl.Launch", "Launch after:", curY);
    {
        WidgetElement chk;
        chk.type = WidgetElementType::CheckBox;
        chk.id = "BG.Chk.Launch";
        chk.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        chk.to = Vec2{ nx(leftPad + labelW + gap + 20.0f), ny(curY + rowH) };
        chk.isChecked = true;
        chk.style.color = EditorTheme::Get().inputBackground;
        chk.style.fillColor = EditorTheme::Get().accent;
        chk.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(chk));
    }
    curY += rowH + gap;

    // Clean build checkbox
    addLabel("BG.Lbl.Clean", "Clean build:", curY);
    {
        WidgetElement chk;
        chk.type = WidgetElementType::CheckBox;
        chk.id = "BG.Chk.Clean";
        chk.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        chk.to = Vec2{ nx(leftPad + labelW + gap + 20.0f), ny(curY + rowH) };
        chk.isChecked = false;
        chk.style.color = EditorTheme::Get().inputBackground;
        chk.style.fillColor = EditorTheme::Get().accent;
        chk.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(chk));
    }
    curY += rowH + gap;

    // Output directory (read-only info)
    addLabel("BG.Lbl.Output", "Output Dir:", curY);
    {
        WidgetElement outputText;
        outputText.type = WidgetElementType::Text;
        outputText.id = "BG.Text.Output";
        outputText.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        outputText.to = Vec2{ nx(W - rightPad), ny(curY + rowH) };
        outputText.text = defaultOutputDir;
        outputText.fontSize = EditorTheme::Get().fontSizeSmall;
        outputText.style.textColor = EditorTheme::Get().textSecondary;
        outputText.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(outputText));
    }
    curY += rowH + gap;

    // Binary cache directory (read-only info)
    addLabel("BG.Lbl.Binary", "Binary Cache:", curY);
    {
        WidgetElement binaryText;
        binaryText.type = WidgetElementType::Text;
        binaryText.id = "BG.Text.Binary";
        binaryText.from = Vec2{ nx(leftPad + labelW + gap), ny(curY) };
        binaryText.to = Vec2{ nx(W - rightPad), ny(curY + rowH) };
        binaryText.text = defaultBinaryDir;
        binaryText.fontSize = EditorTheme::Get().fontSizeSmall;
        binaryText.style.textColor = EditorTheme::Get().textSecondary;
        binaryText.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(binaryText));
    }
    curY += rowH + gap;

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Info text ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    {
        WidgetElement info;
        info.type = WidgetElementType::Text;
        info.id = "BG.Info";
        info.from = Vec2{ nx(leftPad), ny(curY + gap) };
        info.to = Vec2{ nx(W - rightPad), ny(curY + gap + rowH) };
        info.text = "Output goes to <Project>/Build, binaries cached in <Project>/Binary.";
        info.fontSize = EditorTheme::Get().fontSizeSmall;
        info.style.textColor = EditorTheme::Get().textSecondary;
        info.textAlignV = TextAlignV::Center;
        elements.push_back(std::move(info));
    }
    curY += rowH + gap * 2;

    // ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Buttons ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬
    const float btnW = 100.0f;
    const float btnH = 30.0f;
    const float btnGap = 10.0f;
    const float btnY = H - 16.0f - btnH;

    // Build button
    {
        WidgetElement buildBtn;
        buildBtn.type = WidgetElementType::Button;
        buildBtn.id = "BG.Btn.Build";
        buildBtn.from = Vec2{ nx(W - rightPad - btnW * 2 - btnGap), ny(btnY) };
        buildBtn.to = Vec2{ nx(W - rightPad - btnW - btnGap), ny(btnY + btnH) };
        buildBtn.text = "Build";
        buildBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
        buildBtn.style.color = EditorTheme::Get().accent;
        buildBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
        buildBtn.textAlignH = TextAlignH::Center;
        buildBtn.textAlignV = TextAlignV::Center;
        buildBtn.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(buildBtn));
    }

    // Cancel button
    {
        WidgetElement cancelBtn;
        cancelBtn.type = WidgetElementType::Button;
        cancelBtn.id = "BG.Btn.Cancel";
        cancelBtn.from = Vec2{ nx(W - rightPad - btnW), ny(btnY) };
        cancelBtn.to = Vec2{ nx(W - rightPad), ny(btnY + btnH) };
        cancelBtn.text = "Cancel";
        cancelBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
        cancelBtn.style.color = EditorTheme::Get().buttonDefault;
        cancelBtn.style.textColor = EditorTheme::Get().textPrimary;
        cancelBtn.textAlignH = TextAlignH::Center;
        cancelBtn.textAlignV = TextAlignV::Center;
        cancelBtn.hitTestMode = HitTestMode::Enabled;
        elements.push_back(std::move(cancelBtn));
    }

    auto widget = std::make_shared<EditorWidget>();
    widget->setName("BuildGameForm");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));

    popup->uiManager().registerWidget("BuildGameForm", widget);

    // Cancel button closes the popup
    popup->uiManager().registerClickEvent("BG.Btn.Cancel", [popup]()
    {
        popup->close();
    });

    // Build button: gather form values and invoke the build callback
    auto* parentBuildUI = this;
    auto* parentUIMgr = m_uiManager;
    auto profilesCopy = std::make_shared<std::vector<BuildProfile>>(m_buildProfiles);
    popup->uiManager().registerClickEvent("BG.Btn.Build",
        [popup, parentBuildUI, parentUIMgr, formState, profilesCopy, defaultOutputDir, defaultBinaryDir]()
    {
        auto& popupUI = popup->uiManager();

        BuildGameConfig config;

        // Read profile selection
        int profileIdx = 0;
        if (auto* el = popupUI.findElementById("BG.DD.Profile"))
            profileIdx = el->selectedIndex;
        if (profileIdx >= 0 && profileIdx < static_cast<int>(profilesCopy->size()))
            config.profile = (*profilesCopy)[static_cast<size_t>(profileIdx)];

        // Read other form values
        if (auto* el = popupUI.findElementById("BG.DD.StartLevel"))
            config.startLevel = el->text;
        if (auto* el = popupUI.findElementById("BG.Entry.Title"))
            config.windowTitle = el->text;
        if (auto* el = popupUI.findElementById("BG.Chk.Launch"))
            config.launchAfterBuild = el->isChecked;
        if (auto* el = popupUI.findElementById("BG.Chk.Clean"))
            config.cleanBuild = el->isChecked;

        // Standardized paths
        config.outputDir = defaultOutputDir;
        config.binaryDir = defaultBinaryDir;

        if (config.startLevel.empty())
        {
            parentUIMgr->showToastMessage("Start Level is required.", UIManager::kToastMedium, NotificationLevel::Warning);
            return;
        }
        if (config.outputDir.empty())
        {
            parentUIMgr->showToastMessage("Output directory is required.", UIManager::kToastMedium, NotificationLevel::Warning);
            return;
        }

        if (parentBuildUI->m_onBuildGame)
            parentBuildUI->m_onBuildGame(config);

        popup->close();
    });
}

// ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ Build progress modal ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬ÃƒÂ¢Ã¢â‚¬ÂÃ¢â€šÂ¬

void BuildSystemUI::showBuildProgress()
{
    m_buildOutputLines.clear();
    m_buildCancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        m_buildPendingLines.clear();
        m_buildPendingStepDirty = false;
        m_buildPendingFinished = false;
    }

    // Open a separate OS window for build output
    if (m_renderer)
    {
        m_buildPopup = m_renderer->openPopupWindow("BuildOutput", "Build Output", 820, 520);
    }
    if (!m_buildPopup)
    {
        // Fallback: just log a warning and bail
        m_uiManager->showToastMessage("Failed to open build output window.", 4.0f, NotificationLevel::Error);
        return;
    }

    m_buildProgressWidget = std::make_shared<EditorWidget>();
    m_buildProgressWidget->setName("BuildProgress");
    m_buildProgressWidget->setAnchor(WidgetAnchor::TopLeft);
    m_buildProgressWidget->setFillX(true);
    m_buildProgressWidget->setFillY(true);
    m_buildProgressWidget->setZOrder(100);

    // Full-window background panel as StackPanel
    WidgetElement panel{};
    panel.id = "BP.Panel";
    panel.type = WidgetElementType::StackPanel;
    panel.from = Vec2{ 0.0f, 0.0f };
    panel.to = Vec2{ 1.0f, 1.0f };
    panel.padding = Vec2{ 18.0f, 12.0f };
    panel.orientation = StackOrientation::Vertical;
    panel.style.color = EditorTheme::Get().windowBackground;
    panel.runtimeOnly = true;

    WidgetElement title{};
    title.id = "BP.Title";
    title.type = WidgetElementType::Text;
    title.text = "Building Game...";
    title.font = EditorTheme::Get().fontDefault;
    title.fontSize = EditorTheme::Get().fontSizeHeading;
    title.textAlignH = TextAlignH::Center;
    title.style.textColor = EditorTheme::Get().textPrimary;
    title.fillX = true;
    title.minSize = Vec2{ 0.0f, 28.0f };
    title.runtimeOnly = true;

    WidgetElement status{};
    status.id = "BP.Status";
    status.type = WidgetElementType::Text;
    status.text = "Preparing...";
    status.font = EditorTheme::Get().fontDefault;
    status.fontSize = EditorTheme::Get().fontSizeBody;
    status.textAlignH = TextAlignH::Center;
    status.style.textColor = EditorTheme::Get().textSecondary;
    status.fillX = true;
    status.minSize = Vec2{ 0.0f, 22.0f };
    status.runtimeOnly = true;

    WidgetElement counter{};
    counter.id = "BP.Counter";
    counter.type = WidgetElementType::Text;
    counter.text = "0 / 0";
    counter.font = EditorTheme::Get().fontDefault;
    counter.fontSize = EditorTheme::Get().fontSizeSubheading;
    counter.textAlignH = TextAlignH::Center;
    counter.style.textColor = EditorTheme::Get().textSecondary;
    counter.fillX = true;
    counter.minSize = Vec2{ 0.0f, 20.0f };
    counter.runtimeOnly = true;

    WidgetElement progress{};
    progress.id = "BP.Bar";
    progress.type = WidgetElementType::ProgressBar;
    progress.fillX = true;
    progress.minSize = Vec2{ 0.0f, 18.0f };
    progress.minValue = 0.0f;
    progress.maxValue = 1.0f;
    progress.valueFloat = 0.0f;
    progress.style.color = EditorTheme::Get().sliderTrack;
    progress.style.fillColor = Vec4{
        EditorTheme::Get().accent.x,
        EditorTheme::Get().accent.y,
        EditorTheme::Get().accent.z, 0.95f };
    progress.runtimeOnly = true;

    // Result text (hidden during build, shown after completion)
    WidgetElement resultText{};
    resultText.id = "BP.Result";
    resultText.type = WidgetElementType::Text;
    resultText.text = "";
    resultText.font = EditorTheme::Get().fontDefault;
    resultText.fontSize = EditorTheme::Get().fontSizeBody;
    resultText.textAlignH = TextAlignH::Center;
    resultText.style.textColor = EditorTheme::Get().textPrimary;
    resultText.fillX = true;
    resultText.minSize = Vec2{ 0.0f, 24.0f };
    resultText.runtimeOnly = true;
    resultText.isCollapsed = true;

    // Close button (hidden during build, shown after completion)
    auto closeBtn = EditorUIBuilder::makePrimaryButton("BP.CloseBtn", "Close", [this]() {
        dismissBuildProgress();
    });
    closeBtn.isCollapsed = true;

    panel.children.push_back(std::move(title));
    panel.children.push_back(std::move(status));
    panel.children.push_back(std::move(counter));
    panel.children.push_back(std::move(progress));

    // Scrollable build output log (children added dynamically per-line by pollBuildThread)
    WidgetElement outputPanel{};
    outputPanel.id = "BP.OutputScroll";
    outputPanel.type = WidgetElementType::StackPanel;
    outputPanel.orientation = StackOrientation::Vertical;
    outputPanel.fillX = true;
    outputPanel.fillY = true;
    outputPanel.scrollable = true;
    outputPanel.style.color = Vec4{ 0.04f, 0.04f, 0.04f, 0.95f };
    outputPanel.style.borderRadius = 4.0f;
    outputPanel.padding = Vec2{ 6.0f, 4.0f };
    outputPanel.runtimeOnly = true;

    panel.children.push_back(std::move(outputPanel));
    panel.children.push_back(std::move(resultText));

    // Abort Build button (visible during build, hidden after completion)
    auto abortBtn = EditorUIBuilder::makeButton("BP.AbortBtn", "Abort Build", [this]() {
        m_buildCancelRequested.store(true);
        appendBuildOutput("[INFO] Build cancellation requested...");
    });
    abortBtn.style.color = Vec4{ 0.7f, 0.15f, 0.15f, 1.0f };
    abortBtn.style.hoverColor = Vec4{ 0.85f, 0.2f, 0.2f, 1.0f };
    abortBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
    panel.children.push_back(std::move(abortBtn));

    panel.children.push_back(std::move(closeBtn));

    std::vector<WidgetElement> elems;
    elems.push_back(std::move(panel));
    m_buildProgressWidget->setElements(std::move(elems));
    m_buildProgressWidget->markLayoutDirty();

    m_buildPopup->uiManager().registerWidget("BuildProgress", m_buildProgressWidget);
}

void BuildSystemUI::updateBuildProgress(const std::string& status, int step, int totalSteps)
{
    if (!m_buildProgressWidget) return;

    auto& elems = m_buildProgressWidget->getElementsMutable();

    WidgetElement* statusEl = FindElementById(elems, "BP.Status");
    if (statusEl)
        statusEl->text = status;

    WidgetElement* counterEl = FindElementById(elems, "BP.Counter");
    if (counterEl)
        counterEl->text = std::to_string(step) + " / " + std::to_string(totalSteps);

    WidgetElement* barEl = FindElementById(elems, "BP.Bar");
    if (barEl)
    {
        barEl->maxValue = static_cast<float>(totalSteps);
        barEl->valueFloat = static_cast<float>(step);
    }

    m_buildProgressWidget->markLayoutDirty();
    m_uiManager->markRenderDirty();
}

void BuildSystemUI::closeBuildProgress(bool success, const std::string& message)
{
    if (!m_buildProgressWidget) return;

    auto& elems = m_buildProgressWidget->getElementsMutable();

    // Update title to reflect completion
    WidgetElement* titleEl = FindElementById(elems, "BP.Title");
    if (titleEl)
        titleEl->text = success ? "Build Completed" : "Build Failed";

    // Hide the progress bar and counter
    WidgetElement* barEl = FindElementById(elems, "BP.Bar");
    if (barEl) barEl->isCollapsed = true;

    WidgetElement* counterEl = FindElementById(elems, "BP.Counter");
    if (counterEl) counterEl->isCollapsed = true;

    // Update status text
    WidgetElement* statusEl = FindElementById(elems, "BP.Status");
    if (statusEl)
        statusEl->text = success ? "The game was built successfully." : "The build encountered errors.";

    // Show result message
    WidgetElement* resultEl = FindElementById(elems, "BP.Result");
    if (resultEl)
    {
        resultEl->isCollapsed = false;
        if (!message.empty())
            resultEl->text = message;
        else
            resultEl->text = success ? "Build completed successfully!" : "Build failed.";
        resultEl->style.textColor = success
            ? Vec4{ 0.3f, 0.9f, 0.4f, 1.0f }
            : Vec4{ 0.95f, 0.3f, 0.3f, 1.0f };
    }

    // Show the close button, hide the abort button
    WidgetElement* closeBtn = FindElementById(elems, "BP.CloseBtn");
    if (closeBtn) closeBtn->isCollapsed = false;

    WidgetElement* abortBtn = FindElementById(elems, "BP.AbortBtn");
    if (abortBtn) abortBtn->isCollapsed = true;

    m_buildProgressWidget->markLayoutDirty();
    m_uiManager->markRenderDirty();
}

void BuildSystemUI::dismissBuildProgress()
{
    if (m_buildPopup)
    {
        m_buildPopup->uiManager().unregisterWidget("BuildProgress");
        if (m_renderer)
            m_renderer->closePopupWindow("BuildOutput");
        m_buildPopup = nullptr;
    }
    m_buildProgressWidget.reset();
}

void BuildSystemUI::appendBuildOutput(const std::string& line)
{
    std::lock_guard<std::mutex> lock(m_buildMutex);
    m_buildPendingLines.push_back(line);
}

void BuildSystemUI::pollBuildThread()
{
    if (!m_buildProgressWidget)
        return;

    std::vector<std::string> newLines;
    bool stepDirty = false;
    std::string stepStatus;
    int stepNum = 0;
    int stepTotal = 0;
    bool finished = false;
    bool success = false;
    std::string errorMsg;

    {
        std::lock_guard<std::mutex> lock(m_buildMutex);
        if (!m_buildPendingLines.empty())
        {
            newLines.swap(m_buildPendingLines);
        }
        if (m_buildPendingStepDirty)
        {
            stepDirty = true;
            stepStatus = m_buildPendingStatus;
            stepNum = m_buildPendingStep;
            stepTotal = m_buildPendingTotalSteps;
            m_buildPendingStepDirty = false;
        }
        if (m_buildPendingFinished)
        {
            finished = true;
            success = m_buildPendingSuccess;
            errorMsg = m_buildPendingErrorMsg;
            m_buildPendingFinished = false;
        }
    }

    bool dirty = false;

    if (!newLines.empty())
    {
        for (auto& ln : newLines)
            m_buildOutputLines.push_back(std::move(ln));

        auto& elems = m_buildProgressWidget->getElementsMutable();
        WidgetElement* outputScroll = FindElementById(elems, "BP.OutputScroll");
        if (outputScroll)
        {
            const auto& theme = EditorTheme::Get();
            const float rowH = EditorTheme::Scaled(16.0f);

            // Rebuild all rows from m_buildOutputLines
            outputScroll->children.clear();
            for (size_t i = 0; i < m_buildOutputLines.size(); ++i)
            {
                WidgetElement row{};
                row.id        = "BP.Row." + std::to_string(i);
                row.type      = WidgetElementType::Text;
                row.text      = m_buildOutputLines[i];
                row.font      = theme.fontDefault;
                row.fontSize  = theme.fontSizeSmall;
                row.style.textColor = Vec4{ 0.75f, 0.80f, 0.75f, 1.0f };
                row.textAlignH = TextAlignH::Left;
                row.textAlignV = TextAlignV::Center;
                row.fillX     = true;
                row.minSize   = Vec2{ 0.0f, rowH };
                row.runtimeOnly = true;
                outputScroll->children.push_back(std::move(row));
            }

            // Auto-scroll to bottom
            outputScroll->scrollOffset = 999999.0f;
        }

        dirty = true;
    }

    if (stepDirty)
    {
        updateBuildProgress(stepStatus, stepNum, stepTotal);
        dirty = true;
    }

    if (finished)
    {
        if (m_buildThread.joinable())
            m_buildThread.join();
        m_buildRunning.store(false);
        closeBuildProgress(success, errorMsg);
        dirty = true;
    }

    if (dirty)
    {
        if (m_buildProgressWidget)
            m_buildProgressWidget->markLayoutDirty();
        if (m_buildPopup && m_buildPopup->isOpen())
            m_buildPopup->uiManager().markRenderDirty();
        m_uiManager->markRenderDirty();
    }
}

// ---------------------------------------------------------------------------
// Silent process helpers (no console window) for CMake / Toolchain detection
// ---------------------------------------------------------------------------
#if defined(_WIN32)
namespace {

// Run a shell command silently (CREATE_NO_WINDOW) and return the exit code.
static int shellExecSilent(const std::string& shellCmd)
{
    std::string cmdLine = "cmd.exe /c " + shellCmd;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) return -1;

    WaitForSingleObject(pi.hProcess, 10000); // 10 s timeout
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

// Run a shell command silently and capture the first line of stdout.
static std::string shellReadSilent(const std::string& shellCmd)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return {};
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    std::string cmdLine = "cmd.exe /c " + shellCmd;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hWrite);

    if (!ok)
    {
        CloseHandle(hRead);
        return {};
    }

    std::string result;
    char buf[1024];
    DWORD bytesRead = 0;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
    {
        buf[bytesRead] = '\0';
        result += buf;
        if (result.find('\n') != std::string::npos)
            break;
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto pos = result.find('\n');
    if (pos != std::string::npos)
        result = result.substr(0, pos);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}

} // anonymous namespace
#endif // _WIN32

// ---------------------------------------------------------------------------
// detectCMake ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ locate cmake executable
// ---------------------------------------------------------------------------
bool BuildSystemUI::detectCMake()
{
    m_cmakeAvailable = false;
    m_cmakePath.clear();

    // Helper: run a command silently and check exit code
    auto tryExec = [](const std::string& path) -> bool
    {
#if defined(_WIN32)
        const std::string cmd = "\"" + path + "\" --version >nul 2>&1";
        return shellExecSilent(cmd) == 0;
#else
        const std::string cmd = "\"" + path + "\" --version >/dev/null 2>&1";
        return std::system(cmd.c_str()) == 0;
#endif
    };

    // Helper: read the first line of output from a command
#if defined(_WIN32)
    auto readFirstLine = [](const std::string& cmd) -> std::string
    {
        return shellReadSilent(cmd);
    };
#endif

    // 1. Bundled location: <engine>/Tools/cmake/bin/cmake.exe
    {
        const char* bp = SDL_GetBasePath();
        if (bp)
        {
            auto bundled = std::filesystem::path(bp) / "Tools" / "cmake" / "bin" / "cmake.exe";
            if (std::filesystem::exists(bundled) && tryExec(bundled.string()))
            {
                m_cmakePath = bundled.string();
                m_cmakeAvailable = true;
                return true;
            }
        }
    }

    // 2. VS-bundled cmake via vswhere (Windows)
#if defined(_WIN32)
    {
        const std::string vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
        if (std::filesystem::exists(vswhere))
        {
            std::string vsPath = readFirstLine(
                "\"" + vswhere + "\" -latest -property installationPath 2>nul");
            if (!vsPath.empty())
            {
                auto vsCmake = std::filesystem::path(vsPath)
                    / "Common7" / "IDE" / "CommonExtensions"
                    / "Microsoft" / "CMake" / "CMake" / "bin" / "cmake.exe";
                if (std::filesystem::exists(vsCmake) && tryExec(vsCmake.string()))
                {
                    m_cmakePath = vsCmake.string();
                    m_cmakeAvailable = true;
                    return true;
                }
            }
        }
    }
#endif

    // 3. System PATH ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ resolve to absolute path
    if (tryExec("cmake"))
    {
#if defined(_WIN32)
        std::string resolved = readFirstLine("where cmake 2>nul");
        if (!resolved.empty() && std::filesystem::exists(resolved))
            m_cmakePath = resolved;
        else
            m_cmakePath = "cmake";
#else
        m_cmakePath = "cmake";
#endif
        m_cmakeAvailable = true;
        return true;
    }

    // 4. Common install locations (Windows)
#if defined(_WIN32)
    {
        static const char* candidates[] = {
            "C:\\Program Files\\CMake\\bin\\cmake.exe",
            "C:\\Program Files (x86)\\CMake\\bin\\cmake.exe",
        };
        for (const auto* c : candidates)
        {
            if (std::filesystem::exists(c) && tryExec(c))
            {
                m_cmakePath = c;
                m_cmakeAvailable = true;
                return true;
            }
        }
    }
#endif

    return false;
}

// ---------------------------------------------------------------------------
// showCMakeInstallPrompt ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ modal popup if CMake is missing
// ---------------------------------------------------------------------------
void BuildSystemUI::showCMakeInstallPrompt()
{
    m_uiManager->showConfirmDialog(
        "CMake wird zum Bauen des Spiels benoetigt, "
        "wurde aber nicht gefunden.\n\n"
        "Soll die CMake-Downloadseite geoeffnet werden?",
        [this]()
        {
            // Open the CMake download page in the default browser
#if defined(_WIN32)
            ShellExecuteA(nullptr, "open",
                "https://cmake.org/download/", nullptr, nullptr, SW_SHOWNORMAL);
#else
            std::system("xdg-open https://cmake.org/download/ &");
#endif
            m_uiManager->showToastMessage("Bitte CMake installieren und den Editor neu starten.", 8.0f,
                NotificationLevel::Warning);
        },
        [this]()
        {
            m_uiManager->showToastMessage("CMake nicht verfuegbar ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ Build Game deaktiviert.", UIManager::kToastLong,
                NotificationLevel::Warning);
        }
    );
}

// ---------------------------------------------------------------------------
// detectBuildToolchain ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ check for MSVC / Clang / GCC
// ---------------------------------------------------------------------------
bool BuildSystemUI::detectBuildToolchain()
{
    m_toolchainAvailable = false;
    m_toolchainInfo = {};

#if defined(_WIN32)
    // Helper: read first line from a command (silent, no console window)
    auto readLine = [](const std::string& cmd) -> std::string
    {
        return shellReadSilent(cmd);
    };

    // 1. Try vswhere to find Visual Studio
    const std::string vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
    if (std::filesystem::exists(vswhere))
    {
        std::string vsPath = readLine(
            "\"" + vswhere + "\" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>nul");
        if (!vsPath.empty() && std::filesystem::exists(vsPath))
        {
            m_toolchainInfo.vsInstallPath = vsPath;
            m_toolchainInfo.name = "MSVC";

            // Get VS product display version (e.g. "18.4.1")
            std::string ver = readLine(
                "\"" + vswhere + "\" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog.productDisplayVersion 2>nul");
            if (!ver.empty())
                m_toolchainInfo.version = ver;

            // Try to find cl.exe
            auto vcToolsDir = std::filesystem::path(vsPath) / "VC" / "Tools" / "MSVC";
            if (std::filesystem::exists(vcToolsDir))
            {
                // Pick the latest version subdirectory
                std::string latest;
                for (auto& entry : std::filesystem::directory_iterator(vcToolsDir))
                {
                    if (entry.is_directory())
                    {
                        std::string name = entry.path().filename().string();
                        if (name > latest)
                            latest = name;
                    }
                }
                if (!latest.empty())
                {
                    auto cl = vcToolsDir / latest / "bin" / "Hostx64" / "x64" / "cl.exe";
                    if (std::filesystem::exists(cl))
                        m_toolchainInfo.compilerPath = cl.string();
                }
            }

            m_toolchainAvailable = true;
            return true;
        }
    }

    // 2. Check for cl.exe in PATH
    if (shellExecSilent("where cl.exe >nul 2>&1") == 0)
    {
        std::string clPath = shellReadSilent("where cl.exe 2>nul");
        m_toolchainInfo.name = "MSVC";
        m_toolchainInfo.compilerPath = clPath;
        m_toolchainAvailable = true;
        return true;
    }

    // 3. Check for clang-cl in PATH
    if (shellExecSilent("where clang-cl.exe >nul 2>&1") == 0)
    {
        std::string clangPath = shellReadSilent("where clang-cl.exe 2>nul");
        m_toolchainInfo.name = "Clang-CL";
        m_toolchainInfo.compilerPath = clangPath;
        m_toolchainAvailable = true;
        return true;
    }

#else  // Linux / macOS
    auto tryWhich = [](const char* tool) -> std::string
    {
        std::string cmd = std::string("which ") + tool + " 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return {};
        char buf[512];
        std::string result;
        if (fgets(buf, sizeof(buf), pipe))
            result = buf;
        pclose(pipe);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    };

    std::string gpp = tryWhich("g++");
    if (!gpp.empty())
    {
        m_toolchainInfo.name = "GCC";
        m_toolchainInfo.compilerPath = gpp;
        m_toolchainAvailable = true;
        return true;
    }

    std::string clangpp = tryWhich("clang++");
    if (!clangpp.empty())
    {
        m_toolchainInfo.name = "Clang";
        m_toolchainInfo.compilerPath = clangpp;
        m_toolchainAvailable = true;
        return true;
    }
#endif

    return false;
}

// ---------------------------------------------------------------------------
// showToolchainInstallPrompt ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ modal popup if no C++ compiler found
// ---------------------------------------------------------------------------
void BuildSystemUI::showToolchainInstallPrompt()
{
    m_uiManager->showConfirmDialog(
        "Keine C++ Build-Toolchain (MSVC / Clang) gefunden.\n\n"
        "Zum Bauen des Spiels wird ein C++ Compiler benoetigt.\n"
        "Bitte installiere Visual Studio mit der Workload\n"
        "\"Desktopentwicklung mit C++\".\n\n"
        "Soll die Visual Studio-Downloadseite geoeffnet werden?",
        [this]()
        {
#if defined(_WIN32)
            ShellExecuteA(nullptr, "open",
                "https://visualstudio.microsoft.com/downloads/", nullptr, nullptr, SW_SHOWNORMAL);
#else
            std::system("xdg-open https://visualstudio.microsoft.com/downloads/ &");
#endif
            m_uiManager->showToastMessage("Bitte C++ Toolchain installieren und den Editor neu starten.", 8.0f,
                NotificationLevel::Warning);
        },
        [this]()
        {
            m_uiManager->showToastMessage("Keine Build-Toolchain ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ Build Game deaktiviert.", UIManager::kToastLong,
                NotificationLevel::Warning);
        }
    );
}

// ---------------------------------------------------------------------------
// startAsyncToolchainDetection ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ run CMake + toolchain detection on a
//                                 background thread (non-blocking)
// ---------------------------------------------------------------------------
void BuildSystemUI::startAsyncToolchainDetection()
{
    m_toolDetectDone.store(false);
    m_toolDetectPolled = false;

    std::thread([this]()
    {
        detectCMake();
        detectBuildToolchain();
        m_toolDetectDone.store(true);
    }).detach();
}

// ---------------------------------------------------------------------------
// pollToolchainDetection ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Å“ call once per frame from the main thread.
//                          When detection finishes, logs results and shows
//                          install prompts if needed.
// ---------------------------------------------------------------------------
void BuildSystemUI::pollToolchainDetection()
{
    if (m_toolDetectPolled || !m_toolDetectDone.load())
        return;
    m_toolDetectPolled = true;

    auto& logger = Logger::Instance();

    if (!m_cmakeAvailable)
    {
        logger.log(Logger::Category::Engine,
            "CMake not found \xe2\x80\x93 Build Game will not be available.",
            Logger::LogLevel::WARNING);
        showCMakeInstallPrompt();
    }
    else
    {
        logger.log(Logger::Category::Engine,
            "CMake found: " + m_cmakePath,
            Logger::LogLevel::INFO);
    }

    if (!m_toolchainAvailable)
    {
        logger.log(Logger::Category::Engine,
            "C++ toolchain not found \xe2\x80\x93 Build Game will not be available.",
            Logger::LogLevel::WARNING);
        showToolchainInstallPrompt();
    }
    else
    {
        logger.log(Logger::Category::Engine,
            "Toolchain: " + m_toolchainInfo.name + " " + m_toolchainInfo.version,
            Logger::LogLevel::INFO);
    }
}

#endif // ENGINE_EDITOR


