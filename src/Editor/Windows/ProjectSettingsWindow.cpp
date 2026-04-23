#if ENGINE_EDITOR

#include "ProjectSettingsWindow.h"

#include "../../Renderer/Renderer.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/UIWidget.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Logger/Logger.h"
#include "../Windows/PopupWindow.h"

#include <filesystem>
#include <sstream>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Build-profile data model persisted via DiagnosticsManager project states.
// Keys: "BuildProfile.<name>.BuildType"    (Debug/Development/Shipping)
//       "BuildProfile.<name>.WindowTitle"
//       "BuildProfile.<name>.WindowMode"   (Fullscreen/Windowed/Borderless)
//       "BuildProfile.<name>.ResX"
//       "BuildProfile.<name>.ResY"
//       "BuildProfile.<name>.VSync"        (0/1)
//       "BuildProfile.<name>.HotReload"    (0/1)
//       "BuildProfile.<name>.Profiler"     (0/1)
//       "BuildProfile.<name>.Validation"   (0/1)
//       "BuildProfile.<name>.PackageCompression" (None/Fast/Best)
//       "BuildProfile.<name>.OutputDir"
//       "BuildProfileList"                 (comma-separated names)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // ── Helpers ──────────────────────────────────────────────────────────────

    std::string ps(const std::string& key)
    {
        auto v = DiagnosticsManager::Instance().getProjectState(key);
        return v ? *v : "";
    }
    void setSave(const std::string& key, const std::string& value)
    {
        DiagnosticsManager::Instance().setProjectState(key, value);
        DiagnosticsManager::Instance().saveProjectConfig();
    }

    // Build profile list helpers
    std::vector<std::string> getProfileList()
    {
        const std::string raw = ps("BuildProfileList");
        if (raw.empty()) return {};
        std::vector<std::string> result;
        std::stringstream ss(raw);
        std::string token;
        while (std::getline(ss, token, ','))
            if (!token.empty()) result.push_back(token);
        return result;
    }
    void saveProfileList(const std::vector<std::string>& list)
    {
        std::string raw;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (i > 0) raw += ',';
            raw += list[i];
        }
        setSave("BuildProfileList", raw);
    }
    std::string profileKey(const std::string& name, const std::string& field)
    {
        return "BuildProfile." + name + "." + field;
    }
    std::string profileGet(const std::string& name, const std::string& field, const std::string& def = "")
    {
        const std::string v = ps(profileKey(name, field));
        return v.empty() ? def : v;
    }
    void profileSet(const std::string& name, const std::string& field, const std::string& value)
    {
        setSave(profileKey(name, field), value);
    }

    // ── State ────────────────────────────────────────────────────────────────

    struct PSWState
    {
        int  activeCategory { 0 };   // 0=General 1=Display 2=Window 3=BuildProfiles 4=Packaging 5=Scripting
        int  selectedProfile{ -1 };  // index into profile list when category==3
        bool dirty          { false };
    };

    // ── Content area rebuild ──────────────────────────────────────────────────

    void buildContent(PopupWindow* popup,
                      const std::shared_ptr<PSWState>& state,
                      float W, float H,
                      float kSidebarW, float kTitleH,
                      Renderer* renderer)
    {
        auto& pMgr = popup->uiManager();
        auto* entry = pMgr.findElementById("PSW.ContentArea");
        if (!entry) return;
        entry->children.clear();

        const float kRowH      = EditorTheme::Scaled(30.0f);
        const float kLabelW    = EditorTheme::Scaled(160.0f);
        const float kGapW      = EditorTheme::Scaled(10.0f);
        const float contentW   = W - kSidebarW;
        const float kPad       = EditorTheme::Scaled(14.0f);
        const auto& theme      = EditorTheme::Get();

        // ── Widget builder lambdas ────────────────────────────────────────
        const auto addSectionLabel = [&](const std::string& id, const std::string& label)
        {
            WidgetElement sec;
            sec.type      = WidgetElementType::Text;
            sec.id        = id;
            sec.text      = label;
            sec.fontSize  = theme.fontSizeBody;
            sec.style.textColor = theme.textMuted;
            sec.textAlignV      = TextAlignV::Center;
            sec.padding   = Vec2{ 4.0f, 0.0f };
            sec.minSize   = Vec2{ contentW - kPad * 2.0f, kRowH };
            entry->children.push_back(sec);
        };

        const auto addSeparator = [&](const std::string& id)
        {
            WidgetElement sep;
            sep.type      = WidgetElementType::Panel;
            sep.id        = id;
            sep.style.color     = theme.panelBorder;
            sep.minSize   = Vec2{ contentW - kPad * 2.0f, 2.0f };
            entry->children.push_back(sep);
        };

        const auto addTextRow = [&](const std::string& id, const std::string& label,
            const std::string& value, std::function<void(const std::string&)> onChange)
        {
            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = id + ".Row";
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - kPad * 2.0f, kRowH };
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding     = Vec2{ 4.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV      = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            WidgetElement eb;
            eb.type              = WidgetElementType::EntryBar;
            eb.id                = id;
            eb.value             = value;
            eb.fontSize          = theme.fontSizeMonospace;
            eb.style.color       = theme.inputBackground;
            eb.style.hoverColor  = theme.inputBackgroundHover;
            eb.style.textColor   = theme.inputText;
            eb.padding           = Vec2{ 6.0f, 4.0f };
            eb.hitTestMode       = HitTestMode::Enabled;
            eb.minSize           = Vec2{ contentW - kPad * 2.0f - kLabelW - kGapW, kRowH };
            eb.onValueChanged    = std::move(onChange);
            row.children.push_back(eb);
            entry->children.push_back(row);
        };

        const auto addDropdownRow = [&](const std::string& id, const std::string& label,
            const std::vector<std::string>& items, int selected,
            std::function<void(int)> onChange)
        {
            WidgetElement row;
            row.type        = WidgetElementType::StackPanel;
            row.id          = id + ".Row";
            row.orientation = StackOrientation::Horizontal;
            row.minSize     = Vec2{ contentW - kPad * 2.0f, kRowH };
            row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            row.padding     = Vec2{ 4.0f, 2.0f };

            WidgetElement lbl;
            lbl.type      = WidgetElementType::Text;
            lbl.id        = id + ".Lbl";
            lbl.text      = label;
            lbl.fontSize  = theme.fontSizeBody;
            lbl.style.textColor = theme.textPrimary;
            lbl.textAlignV      = TextAlignV::Center;
            lbl.minSize   = Vec2{ kLabelW, kRowH };
            row.children.push_back(lbl);

            WidgetElement dd;
            dd.type              = WidgetElementType::DropDown;
            dd.id                = id;
            dd.items             = items;
            dd.selectedIndex     = selected;
            dd.fontSize          = theme.fontSizeMonospace;
            dd.style.color       = theme.inputBackground;
            dd.style.hoverColor  = theme.inputBackgroundHover;
            dd.style.textColor   = theme.inputText;
            dd.padding           = Vec2{ 6.0f, 4.0f };
            dd.hitTestMode       = HitTestMode::Enabled;
            dd.minSize           = Vec2{ contentW - kPad * 2.0f - kLabelW - kGapW, kRowH };
            dd.onSelectionChanged = std::move(onChange);
            row.children.push_back(dd);
            entry->children.push_back(row);
        };

        const auto addCheckboxRow = [&](const std::string& id, const std::string& label,
            bool checked, std::function<void(bool)> onChange)
        {
            WidgetElement cb;
            cb.type             = WidgetElementType::CheckBox;
            cb.id               = id;
            cb.text             = label;
            cb.fontSize         = theme.fontSizeBody;
            cb.isChecked        = checked;
            cb.style.color      = theme.inputBackground;
            cb.style.hoverColor = theme.checkboxHover;
            cb.style.fillColor  = theme.checkboxChecked;
            cb.style.textColor  = theme.textPrimary;
            cb.padding          = Vec2{ 6.0f, 4.0f };
            cb.hitTestMode      = HitTestMode::Enabled;
            cb.minSize          = Vec2{ contentW - kPad * 2.0f, kRowH };
            cb.onCheckedChanged = std::move(onChange);
            entry->children.push_back(cb);
        };

        const auto addButton = [&](const std::string& id, const std::string& label,
            const Vec4& color, const Vec4& hover, std::function<void()> onClick)
        {
            WidgetElement btn;
            btn.type            = WidgetElementType::Button;
            btn.id              = id;
            btn.text            = label;
            btn.fontSize        = theme.fontSizeBody;
            btn.style.color     = color;
            btn.style.hoverColor = hover;
            btn.style.textColor = theme.buttonText;
            btn.textAlignH      = TextAlignH::Center;
            btn.textAlignV      = TextAlignV::Center;
            btn.padding         = Vec2{ 8.0f, 4.0f };
            btn.hitTestMode     = HitTestMode::Enabled;
            btn.minSize         = Vec2{ contentW - kPad * 2.0f, kRowH };
            btn.onClicked       = std::move(onClick);
            entry->children.push_back(btn);
        };

        // ── Category content ─────────────────────────────────────────────
        auto& diag = DiagnosticsManager::Instance();
        const auto& proj = diag.getProjectInfo();

        if (state->activeCategory == 0) // ── General ────────────────────
        {
            addSectionLabel("PSW.C.Sec.ProjectInfo", "Project Info");

            addTextRow("PSW.C.ProjectName", "Project Name",
                proj.projectName,
                [](const std::string& v) {
                    auto& d = DiagnosticsManager::Instance();
                    auto info = d.getProjectInfo();
                    info.projectName = v;
                    d.setProjectInfo(info);
                    d.saveProjectConfig();
                });

            addTextRow("PSW.C.ProjectVersion", "Project Version",
                proj.projectVersion,
                [](const std::string& v) {
                    auto& d = DiagnosticsManager::Instance();
                    auto info = d.getProjectInfo();
                    info.projectVersion = v;
                    d.setProjectInfo(info);
                    d.saveProjectConfig();
                });

            addTextRow("PSW.C.EngineVersion", "Engine Version",
                proj.engineVersion,
                [](const std::string& v) {
                    auto& d = DiagnosticsManager::Instance();
                    auto info = d.getProjectInfo();
                    info.engineVersion = v;
                    d.setProjectInfo(info);
                    d.saveProjectConfig();
                });

            addSeparator("PSW.C.Sep.Company");
            addSectionLabel("PSW.C.Sec.Company", "Publisher");

            const std::string company = ps("CompanyName");
            addTextRow("PSW.C.CompanyName", "Company Name", company,
                [](const std::string& v) { setSave("CompanyName", v); });

            const std::string copyright = ps("CopyrightNotice");
            addTextRow("PSW.C.Copyright", "Copyright Notice", copyright,
                [](const std::string& v) { setSave("CopyrightNotice", v); });

            addSeparator("PSW.C.Sep.Path");
            addSectionLabel("PSW.C.Sec.Path", "Paths");

            WidgetElement pathLbl;
            pathLbl.type      = WidgetElementType::Text;
            pathLbl.id        = "PSW.C.PathValue";
            pathLbl.text      = proj.projectPath.empty() ? "(no project loaded)" : proj.projectPath;
            pathLbl.fontSize  = theme.fontSizeSmall;
            pathLbl.style.textColor = theme.textMuted;
            pathLbl.textAlignV      = TextAlignV::Center;
            pathLbl.padding   = Vec2{ 4.0f, 0.0f };
            pathLbl.minSize   = Vec2{ contentW - kPad * 2.0f, kRowH };
            entry->children.push_back(pathLbl);
        }
        else if (state->activeCategory == 1) // ── Display ────────────────
        {
            addSectionLabel("PSW.C.Sec.WinTitle", "Window");

            const std::string winTitle = ps("WindowTitle");
            addTextRow("PSW.C.WindowTitle", "Window Title",
                winTitle.empty() ? proj.projectName : winTitle,
                [](const std::string& v) { setSave("WindowTitle", v); });

            const std::string iconPath = ps("WindowIconPath");
            addTextRow("PSW.C.IconPath", "Icon Path (.ico / .png)", iconPath,
                [](const std::string& v) { setSave("WindowIconPath", v); });

            addSeparator("PSW.C.Sep.Splash");
            addSectionLabel("PSW.C.Sec.Splash", "Splash Screen");

            const std::string splashPath = ps("SplashImagePath");
            addTextRow("PSW.C.SplashPath", "Splash Image Path", splashPath,
                [](const std::string& v) { setSave("SplashImagePath", v); });

            const bool showSplash = ps("GameShowSplash") != "0";
            addCheckboxRow("PSW.C.ShowSplash", "Show Splash Screen on Startup", showSplash,
                [](bool v) { setSave("GameShowSplash", v ? "1" : "0"); });

            addSeparator("PSW.C.Sep.Cursor");
            addSectionLabel("PSW.C.Sec.Cursor", "Cursor");

            const bool hideCursor = ps("HideSystemCursor") == "1";
            addCheckboxRow("PSW.C.HideCursor", "Hide System Cursor in Game Mode", hideCursor,
                [](bool v) { setSave("HideSystemCursor", v ? "1" : "0"); });
        }
        else if (state->activeCategory == 2) // ── Window ─────────────────
        {
            addSectionLabel("PSW.C.Sec.WinMode", "Window Mode");

            const std::string savedMode = ps("DefaultWindowMode");
            const std::vector<std::string> winModes = { "Windowed", "Fullscreen", "Borderless Fullscreen" };
            int selMode = 0;
            if (savedMode == "Fullscreen")           selMode = 1;
            else if (savedMode == "Borderless")      selMode = 2;

            addDropdownRow("PSW.C.WinMode", "Default Window Mode", winModes, selMode,
                [](int i) {
                    const char* modes[] = { "Windowed", "Fullscreen", "Borderless" };
                    setSave("DefaultWindowMode", modes[i]);
                });

            addSeparator("PSW.C.Sep.Res");
            addSectionLabel("PSW.C.Sec.Res", "Default Resolution");

            const std::string defResX = ps("DefaultResX");
            const std::string defResY = ps("DefaultResY");
            addTextRow("PSW.C.ResX", "Width (px)", defResX.empty() ? "1920" : defResX,
                [](const std::string& v) { setSave("DefaultResX", v); });
            addTextRow("PSW.C.ResY", "Height (px)", defResY.empty() ? "1080" : defResY,
                [](const std::string& v) { setSave("DefaultResY", v); });

            addSeparator("PSW.C.Sep.VSync2");
            addSectionLabel("PSW.C.Sec.VSync2", "Sync");

            const bool vsync = ps("DefaultVSync") != "0";
            addCheckboxRow("PSW.C.VSync", "Vertical Sync (default)", vsync,
                [](bool v) { setSave("DefaultVSync", v ? "1" : "0"); });

            const bool borderless = ps("AllowWindowResize") == "1";
            addCheckboxRow("PSW.C.AllowResize", "Allow Window Resize", borderless,
                [](bool v) { setSave("AllowWindowResize", v ? "1" : "0"); });

            addSeparator("PSW.C.Sep.MinRes");
            addSectionLabel("PSW.C.Sec.MinRes", "Minimum Resolution");

            const std::string minW = ps("MinWindowW");
            const std::string minH = ps("MinWindowH");
            addTextRow("PSW.C.MinW", "Min Width (px)", minW.empty() ? "800" : minW,
                [](const std::string& v) { setSave("MinWindowW", v); });
            addTextRow("PSW.C.MinH", "Min Height (px)", minH.empty() ? "600" : minH,
                [](const std::string& v) { setSave("MinWindowH", v); });
        }
        else if (state->activeCategory == 3) // ── Build Profiles ──────────
        {
            auto profiles = getProfileList();
            const bool hasProfiles = !profiles.empty();

            addSectionLabel("PSW.C.Sec.Profiles", "Build Profiles");

            // Profile list
            for (int i = 0; i < static_cast<int>(profiles.size()); ++i)
            {
                const std::string& pName = profiles[i];
                const bool isSel = (state->selectedProfile == i);

                WidgetElement profileRow;
                profileRow.type        = WidgetElementType::StackPanel;
                profileRow.id          = "PSW.C.Profile.Row." + std::to_string(i);
                profileRow.orientation = StackOrientation::Horizontal;
                profileRow.minSize     = Vec2{ contentW - kPad * 2.0f, kRowH };
                profileRow.style.color = isSel ? theme.selectionHighlight : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                profileRow.style.hoverColor = theme.selectionHighlightHover;
                profileRow.hitTestMode = HitTestMode::Enabled;
                profileRow.padding     = Vec2{ 6.0f, 2.0f };

                const std::string buildType = profileGet(pName, "BuildType", "Development");
                const std::string rowText   = pName + "  [" + buildType + "]";

                WidgetElement nameLbl;
                nameLbl.type      = WidgetElementType::Text;
                nameLbl.id        = "PSW.C.Profile.Lbl." + std::to_string(i);
                nameLbl.text      = rowText;
                nameLbl.fontSize  = theme.fontSizeBody;
                nameLbl.style.textColor = isSel ? theme.textPrimary : theme.textSecondary;
                nameLbl.textAlignV      = TextAlignV::Center;
                nameLbl.fillX     = true;
                nameLbl.minSize   = Vec2{ 0.0f, kRowH };

                const int capturedIdx = i;
                profileRow.onClicked = [state, popup, W, H, kSidebarW, kTitleH, renderer, capturedIdx]()
                {
                    state->selectedProfile = capturedIdx;
                    buildContent(popup, state, W, H, kSidebarW, kTitleH, renderer);
                    popup->uiManager().markAllWidgetsDirty();
                };
                profileRow.children.push_back(nameLbl);

                // Delete button
                WidgetElement delBtn;
                delBtn.type          = WidgetElementType::Button;
                delBtn.id            = "PSW.C.Profile.Del." + std::to_string(i);
                delBtn.text          = "x";
                delBtn.fontSize      = theme.fontSizeSmall;
                delBtn.style.color   = theme.buttonDanger;
                delBtn.style.hoverColor = theme.buttonDangerHover;
                delBtn.style.textColor = theme.buttonDangerText;
                delBtn.textAlignH    = TextAlignH::Center;
                delBtn.textAlignV    = TextAlignV::Center;
                delBtn.hitTestMode   = HitTestMode::Enabled;
                delBtn.padding       = Vec2{ 4.0f, 2.0f };
                delBtn.minSize       = Vec2{ EditorTheme::Scaled(28.0f), kRowH };
                delBtn.onClicked     = [state, popup, W, H, kSidebarW, kTitleH, renderer, capturedIdx, pName]()
                {
                    auto plist = getProfileList();
                    // Remove profile keys
                    static const std::vector<std::string> kProfileFields =
                        { "BuildType", "WindowTitle", "WindowMode",
                          "ResX", "ResY", "VSync", "HotReload",
                          "Profiler", "Validation", "PackageCompression", "OutputDir" };
                    for (const auto& field : kProfileFields)
                        DiagnosticsManager::Instance().setProjectState(profileKey(pName, field), "");
                    plist.erase(plist.begin() + capturedIdx);
                    saveProfileList(plist);
                    state->selectedProfile = -1;
                    buildContent(popup, state, W, H, kSidebarW, kTitleH, renderer);
                    popup->uiManager().markAllWidgetsDirty();
                };
                profileRow.children.push_back(delBtn);
                entry->children.push_back(profileRow);
            }

            // "Add Profile" button
            addButton("PSW.C.Profile.Add", hasProfiles ? "+ New Profile" : "+ Add Build Profile",
                theme.buttonDefault, theme.buttonHover,
                [state, popup, W, H, kSidebarW, kTitleH, renderer]()
                {
                    auto plist = getProfileList();
                    // Generate a unique name
                    std::string name = "Profile";
                    int suffix = static_cast<int>(plist.size()) + 1;
                    while (std::find(plist.begin(), plist.end(), name + std::to_string(suffix)) != plist.end())
                        ++suffix;
                    name += std::to_string(suffix);
                    plist.push_back(name);
                    saveProfileList(plist);
                    profileSet(name, "BuildType", "Development");
                    profileSet(name, "WindowMode", "Fullscreen");
                    profileSet(name, "VSync", "1");
                    profileSet(name, "HotReload", "0");
                    profileSet(name, "Profiler", "0");
                    profileSet(name, "PackageCompression", "Fast");
                    state->selectedProfile = static_cast<int>(plist.size()) - 1;
                    buildContent(popup, state, W, H, kSidebarW, kTitleH, renderer);
                    popup->uiManager().markAllWidgetsDirty();
                });

            // Profile editor (shown when a profile is selected)
            if (state->selectedProfile >= 0 && state->selectedProfile < static_cast<int>(profiles.size()))
            {
                addSeparator("PSW.C.Sep.ProfEdit");
                const std::string& pName = profiles[state->selectedProfile];

                addSectionLabel("PSW.C.Sec.ProfEdit", "Edit: " + pName);

                // Profile name
                addTextRow("PSW.C.ProfName", "Profile Name", pName,
                    [state, pName, popup, W, H, kSidebarW, kTitleH, renderer](const std::string& newName)
                    {
                        if (newName.empty() || newName == pName) return;
                        auto plist = getProfileList();
                        if (std::find(plist.begin(), plist.end(), newName) != plist.end()) return;
                        // Copy all fields to new name
                        static const std::vector<std::string> kProfileFields =
                            { "BuildType", "WindowTitle", "WindowMode",
                              "ResX", "ResY", "VSync", "HotReload",
                              "Profiler", "Validation", "PackageCompression", "OutputDir" };
                        for (const auto& field : kProfileFields)
                        {
                            const std::string v = profileGet(pName, field);
                            if (!v.empty()) profileSet(newName, field, v);
                            DiagnosticsManager::Instance().setProjectState(profileKey(pName, field), "");
                        }
                        const auto it = std::find(plist.begin(), plist.end(), pName);
                        if (it != plist.end()) *it = newName;
                        saveProfileList(plist);
                        buildContent(popup, state, W, H, kSidebarW, kTitleH, renderer);
                        popup->uiManager().markAllWidgetsDirty();
                    });

                // Build type
                const std::string bt = profileGet(pName, "BuildType", "Development");
                const std::vector<std::string> buildTypes = { "Debug", "Development", "Shipping" };
                int btIdx = 1;
                if (bt == "Debug") btIdx = 0;
                else if (bt == "Shipping") btIdx = 2;
                addDropdownRow("PSW.C.ProfBuildType", "Build Type", buildTypes, btIdx,
                    [pName](int i) {
                        const char* types[] = { "Debug", "Development", "Shipping" };
                        profileSet(pName, "BuildType", types[i]);
                    });

                // Window title override
                addTextRow("PSW.C.ProfWinTitle", "Window Title Override",
                    profileGet(pName, "WindowTitle"),
                    [pName](const std::string& v) { profileSet(pName, "WindowTitle", v); });

                // Window mode
                const std::string wm = profileGet(pName, "WindowMode", "Fullscreen");
                const std::vector<std::string> wmModes = { "Windowed", "Fullscreen", "Borderless Fullscreen" };
                int wmIdx = 1;
                if (wm == "Windowed")   wmIdx = 0;
                else if (wm == "Borderless") wmIdx = 2;
                addDropdownRow("PSW.C.ProfWinMode", "Window Mode", wmModes, wmIdx,
                    [pName](int i) {
                        const char* modes[] = { "Windowed", "Fullscreen", "Borderless" };
                        profileSet(pName, "WindowMode", modes[i]);
                    });

                // Resolution
                addTextRow("PSW.C.ProfResX", "Resolution X",
                    profileGet(pName, "ResX", "1920"),
                    [pName](const std::string& v) { profileSet(pName, "ResX", v); });
                addTextRow("PSW.C.ProfResY", "Resolution Y",
                    profileGet(pName, "ResY", "1080"),
                    [pName](const std::string& v) { profileSet(pName, "ResY", v); });

                // Feature toggles
                addCheckboxRow("PSW.C.ProfVSync", "VSync",
                    profileGet(pName, "VSync", "1") == "1",
                    [pName](bool v) { profileSet(pName, "VSync", v ? "1" : "0"); });
                addCheckboxRow("PSW.C.ProfHotReload", "Script Hot-Reload",
                    profileGet(pName, "HotReload", "0") == "1",
                    [pName](bool v) { profileSet(pName, "HotReload", v ? "1" : "0"); });
                addCheckboxRow("PSW.C.ProfProfiler", "Enable Profiler / Metrics",
                    profileGet(pName, "Profiler", "0") == "1",
                    [pName](bool v) { profileSet(pName, "Profiler", v ? "1" : "0"); });
                addCheckboxRow("PSW.C.ProfValidation", "Enable Validation Layers",
                    profileGet(pName, "Validation", "0") == "1",
                    [pName](bool v) { profileSet(pName, "Validation", v ? "1" : "0"); });

                // Compression
                const std::string comp = profileGet(pName, "PackageCompression", "Fast");
                const std::vector<std::string> compModes = { "None", "Fast", "Best" };
                int compIdx = 1;
                if (comp == "None") compIdx = 0;
                else if (comp == "Best") compIdx = 2;
                addDropdownRow("PSW.C.ProfComp", "Package Compression", compModes, compIdx,
                    [pName](int i) {
                        const char* modes[] = { "None", "Fast", "Best" };
                        profileSet(pName, "PackageCompression", modes[i]);
                    });

                // Output dir
                addTextRow("PSW.C.ProfOutputDir", "Output Directory",
                    profileGet(pName, "OutputDir"),
                    [pName](const std::string& v) { profileSet(pName, "OutputDir", v); });
            }
        }
        else if (state->activeCategory == 4) // ── Packaging ───────────────
        {
            addSectionLabel("PSW.C.Sec.Pkg", "Package Content");

            const std::string outDir = ps("PackageOutputDir");
            addTextRow("PSW.C.PkgOutDir", "Output Directory",
                outDir.empty() ? "Build/Output" : outDir,
                [](const std::string& v) { setSave("PackageOutputDir", v); });

            const std::string compStr = ps("PackageCompression");
            const std::vector<std::string> compModes = { "None", "Fast (LZ4)", "Best (Zstd)" };
            int compSel = 1;
            if (compStr == "None")  compSel = 0;
            else if (compStr == "Best" || compStr == "Best (Zstd)") compSel = 2;
            addDropdownRow("PSW.C.PkgComp", "Compression Mode", compModes, compSel,
                [](int i) {
                    const char* modes[] = { "None", "Fast", "Best" };
                    setSave("PackageCompression", modes[i]);
                });

            addSeparator("PSW.C.Sep.PkgAssets");
            addSectionLabel("PSW.C.Sec.PkgAssets", "Asset Inclusion");

            const bool inclShaders = ps("PackageIncludeShaders") != "0";
            addCheckboxRow("PSW.C.PkgShaders", "Include Shader Sources", inclShaders,
                [](bool v) { setSave("PackageIncludeShaders", v ? "1" : "0"); });

            const bool inclDebugSymbols = ps("PackageIncludeDebugSymbols") == "1";
            addCheckboxRow("PSW.C.PkgDebug", "Include Debug Symbols", inclDebugSymbols,
                [](bool v) { setSave("PackageIncludeDebugSymbols", v ? "1" : "0"); });

            const bool stripMetadata = ps("PackageStripMetadata") != "0";
            addCheckboxRow("PSW.C.PkgStrip", "Strip Asset Metadata", stripMetadata,
                [](bool v) { setSave("PackageStripMetadata", v ? "1" : "0"); });

            addSeparator("PSW.C.Sep.PkgHPK");
            addSectionLabel("PSW.C.Sec.PkgHPK", "HPK Archive");

            const bool singleHPK = ps("PackageSingleHPK") != "0";
            addCheckboxRow("PSW.C.PkgSingleHPK", "Pack into single content.hpk", singleHPK,
                [](bool v) { setSave("PackageSingleHPK", v ? "1" : "0"); });

            const bool encryptHPK = ps("PackageEncryptHPK") == "1";
            addCheckboxRow("PSW.C.PkgEncrypt", "Encrypt HPK Archive", encryptHPK,
                [](bool v) { setSave("PackageEncryptHPK", v ? "1" : "0"); });

            const std::string hpkChunk = ps("PackageHPKChunkSizeMB");
            addTextRow("PSW.C.PkgChunk", "HPK Chunk Size (MB, 0=no split)",
                hpkChunk.empty() ? "0" : hpkChunk,
                [](const std::string& v) { setSave("PackageHPKChunkSizeMB", v); });

            addSeparator("PSW.C.Sep.PkgBuild");
            addButton("PSW.C.PkgBuildBtn", "Build Game (current profile)",
                theme.buttonPrimary, theme.buttonPrimaryHover,
                [renderer]()
                {
                    if (renderer)
                        renderer->getUIManager().openBuildGameDialog();
                });
        }
        else if (state->activeCategory == 5) // ── Scripting ───────────────
        {
            addSectionLabel("PSW.C.Sec.ScriptMode", "Scripting Mode");

            const std::string smStr = ps("ScriptingMode");
            const std::vector<std::string> smModes = { "Python Only", "C++ Only", "Both (Python + C++)" };
            int smSel = 2; // Both
            if (smStr == "PythonOnly") smSel = 0;
            else if (smStr == "CppOnly") smSel = 1;
            addDropdownRow("PSW.C.ScriptMode", "Scripting Mode", smModes, smSel,
                [](int i) {
                    const char* modes[] = { "PythonOnly", "CppOnly", "Both" };
                    setSave("ScriptingMode", modes[i]);
                });

            addSeparator("PSW.C.Sep.ScriptReload");
            addSectionLabel("PSW.C.Sec.ScriptReload", "Hot-Reload");

            const bool hrEnabled = ps("ScriptHotReloadEnabled") != "false";
            addCheckboxRow("PSW.C.ScriptHotReload", "Script Hot-Reload (Editor)", hrEnabled,
                [](bool v) {
                    setSave("ScriptHotReloadEnabled", v ? "true" : "false");
                });

            const bool hrNative = ps("NativeScriptHotReload") == "1";
            addCheckboxRow("PSW.C.NativeHotReload", "C++ Native Script Hot-Reload", hrNative,
                [](bool v) { setSave("NativeScriptHotReload", v ? "1" : "0"); });

            addSeparator("PSW.C.Sep.ScriptAPI");
            addSectionLabel("PSW.C.Sec.ScriptAPI", "Python Environment");

            const std::string pyInterp = ps("PythonInterpreterPath");
            addTextRow("PSW.C.PyInterp", "Interpreter Path (override)", pyInterp,
                [](const std::string& v) { setSave("PythonInterpreterPath", v); });

            const bool exposeAPI = ps("ExposeFullAPI") != "0";
            addCheckboxRow("PSW.C.ExposeAPI", "Expose Full Engine API to Python", exposeAPI,
                [](bool v) { setSave("ExposeFullAPI", v ? "1" : "0"); });
        }

        pMgr.markAllWidgetsDirty();
    }

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
void ProjectSettingsWindow::open(Renderer* renderer)
{
    if (!renderer) return;

    const int kPopupW = static_cast<int>(EditorTheme::Scaled(720.0f));
    const int kPopupH = static_cast<int>(EditorTheme::Scaled(560.0f));

    PopupWindow* popup = renderer->openPopupWindow(
        "ProjectSettings", "Project Settings", kPopupW, kPopupH);
    if (!popup) return;
    if (!popup->uiManager().getRegisteredWidgets().empty()) return; // already built

    const float W = static_cast<float>(kPopupW);
    const float H = static_cast<float>(kPopupH);
    auto nx = [&](float px) { return px / W; };
    auto ny = [&](float py) { return py / H; };

    const float kSidebarW = EditorTheme::Scaled(148.0f);
    const float kTitleH   = EditorTheme::Scaled(44.0f);

    const auto& theme = EditorTheme::Get();
    const std::vector<std::string> categories =
        { "General", "Display", "Window", "Build Profiles", "Packaging", "Scripting" };

    auto state = std::make_shared<PSWState>();

    std::vector<WidgetElement> elements;

    // ── Background ─────────────────────────────────────────────────────────
    {
        WidgetElement bg;
        bg.type        = WidgetElementType::Panel;
        bg.id          = "PSW.Bg";
        bg.from        = Vec2{ 0.0f, 0.0f };
        bg.to          = Vec2{ 1.0f, 1.0f };
        bg.style.color = theme.panelBackground;
        elements.push_back(bg);
    }

    // ── Title bar ──────────────────────────────────────────────────────────
    {
        WidgetElement titleBg;
        titleBg.type        = WidgetElementType::Panel;
        titleBg.id          = "PSW.TitleBg";
        titleBg.from        = Vec2{ 0.0f, 0.0f };
        titleBg.to          = Vec2{ 1.0f, ny(kTitleH) };
        titleBg.style.color = theme.titleBarBackground;
        elements.push_back(titleBg);

        WidgetElement titleText;
        titleText.type           = WidgetElementType::Text;
        titleText.id             = "PSW.TitleText";
        titleText.from           = Vec2{ nx(12.0f), 0.0f };
        titleText.to             = Vec2{ nx(W - 12.0f), ny(kTitleH) };
        titleText.text           = "Project Settings";
        titleText.fontSize       = theme.fontSizeSubheading;
        titleText.style.textColor = theme.titleBarText;
        titleText.textAlignV     = TextAlignV::Center;
        titleText.padding        = Vec2{ 6.0f, 0.0f };
        elements.push_back(titleText);
    }

    // ── Sidebar background ─────────────────────────────────────────────────
    {
        WidgetElement sidebarBg;
        sidebarBg.type        = WidgetElementType::Panel;
        sidebarBg.id          = "PSW.SidebarBg";
        sidebarBg.from        = Vec2{ 0.0f, ny(kTitleH) };
        sidebarBg.to          = Vec2{ nx(kSidebarW), 1.0f };
        sidebarBg.style.color = theme.panelBackgroundAlt;
        elements.push_back(sidebarBg);

        WidgetElement sidebarSep;
        sidebarSep.type        = WidgetElementType::Panel;
        sidebarSep.id          = "PSW.SidebarSep";
        sidebarSep.from        = Vec2{ nx(kSidebarW - 1.0f), ny(kTitleH) };
        sidebarSep.to          = Vec2{ nx(kSidebarW),         1.0f };
        sidebarSep.style.color = Vec4{ theme.panelBorder.x, theme.panelBorder.y, theme.panelBorder.z, 1.0f };
        elements.push_back(sidebarSep);
    }

    // ── Sidebar category buttons ───────────────────────────────────────────
    const float kCatH   = EditorTheme::Scaled(32.0f);
    const float kCatGap = EditorTheme::Scaled(2.0f);
    const float kCatY0  = kTitleH + EditorTheme::Scaled(8.0f);

    for (int i = 0; i < static_cast<int>(categories.size()); ++i)
    {
        const float y0 = kCatY0 + i * (kCatH + kCatGap);
        const bool isCurrent = (i == state->activeCategory);

        WidgetElement catBtn;
        catBtn.type          = WidgetElementType::Button;
        catBtn.id            = "PSW.Cat." + std::to_string(i);
        catBtn.from          = Vec2{ nx(4.0f), ny(y0) };
        catBtn.to            = Vec2{ nx(kSidebarW - 4.0f), ny(y0 + kCatH) };
        catBtn.text          = categories[i];
        catBtn.fontSize      = theme.fontSizeBody;
        catBtn.style.color   = isCurrent ? theme.accent : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        catBtn.style.hoverColor = isCurrent ? theme.accentHover : theme.buttonSubtleHover;
        catBtn.style.textColor  = isCurrent ? theme.buttonPrimaryText : theme.textSecondary;
        catBtn.textAlignH    = TextAlignH::Left;
        catBtn.textAlignV    = TextAlignV::Center;
        catBtn.padding       = Vec2{ 10.0f, 0.0f };
        catBtn.hitTestMode   = HitTestMode::Enabled;
        catBtn.shaderVertex  = "button_vertex.glsl";
        catBtn.shaderFragment = "button_fragment.glsl";

        const int capturedCat = i;
        catBtn.onClicked = [state, popup, W, H, kSidebarW, kTitleH, renderer,
                            capturedCat, categories, kCatH, kCatGap, kCatY0, nx, ny,
                            &theme = EditorTheme::Get()]() mutable
        {
            state->activeCategory = capturedCat;
            state->selectedProfile = -1;
            // Update button styles
            for (int j = 0; j < static_cast<int>(categories.size()); ++j)
            {
                auto* el = popup->uiManager().findElementById("PSW.Cat." + std::to_string(j));
                if (el)
                {
                    const bool sel = (j == capturedCat);
                    el->style.color     = sel ? EditorTheme::Get().accent : Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                    el->style.hoverColor = sel ? EditorTheme::Get().accentHover : EditorTheme::Get().buttonSubtleHover;
                    el->style.textColor = sel ? EditorTheme::Get().buttonPrimaryText : EditorTheme::Get().textSecondary;
                }
            }
            buildContent(popup, state, W, H, kSidebarW, kTitleH, renderer);
            popup->uiManager().markAllWidgetsDirty();
        };
        elements.push_back(catBtn);
    }

    // ── Scrollable content area ────────────────────────────────────────────
    {
        WidgetElement contentArea;
        contentArea.type        = WidgetElementType::StackPanel;
        contentArea.id          = "PSW.ContentArea";
        contentArea.from        = Vec2{ nx(kSidebarW + 1.0f), ny(kTitleH) };
        contentArea.to          = Vec2{ 1.0f, 1.0f };
        contentArea.orientation = StackOrientation::Vertical;
        contentArea.style.color = theme.panelBackground;
        contentArea.padding     = Vec2{ EditorTheme::Scaled(14.0f), EditorTheme::Scaled(12.0f) };
        contentArea.scrollable  = true;
        elements.push_back(contentArea);
    }

    // Register the widget
    auto widget = std::make_shared<EditorWidget>();
    widget->setName("ProjectSettings.Main");
    widget->setFillX(true);
    widget->setFillY(true);
    widget->setElements(std::move(elements));
    popup->uiManager().registerWidget("ProjectSettings.Main", widget);

    // Build initial content
    buildContent(popup, state, W, H, kSidebarW, kTitleH, renderer);
}

#endif // ENGINE_EDITOR
