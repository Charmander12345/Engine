#if ENGINE_EDITOR

#include "EditorApp.h"
#include "../Core/IEditorBridge.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/UIManager.h"
#include "../Renderer/ViewportUIManager.h"
#include "../Renderer/EditorTheme.h"
#include "../Renderer/UIWidget.h"
#include "../Renderer/EditorUI/EditorWidget.h"
#include "../Renderer/EditorUIBuilder.h"
#include "../Renderer/EditorWindows/PopupWindow.h"
#include "../Renderer/EditorWindows/TextureViewerWindow.h"
#include "../Logger/Logger.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../BuildPipeline.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Core/ECS/ECS.h"
#include "../Core/UndoRedoManager.h"
#include "../Core/EngineLevel.h"
#include "../Core/ShortcutManager.h"
#include "../Physics/PhysicsWorld.h"
#include "../Scripting/Python/PythonScripting.h"
#include "../NativeScripting/NativeScriptManager.h"
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <regex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

// Construction / Destruction
EditorApp::EditorApp(IEditorBridge& bridge) : m_bridge(bridge) {}
EditorApp::~EditorApp()
{
    if (m_pieBuildThread.joinable())
        m_pieBuildThread.join();
}

// Lifecycle
void EditorApp::initialize()
{
    Logger::Instance().log(Logger::Category::Engine, "EditorApp: initializing...", Logger::LogLevel::INFO);
    m_playTexId = m_bridge.preloadUITexture("Play.tga");
    m_stopTexId = m_bridge.preloadUITexture("Stop.tga");
    registerWidgets();
    registerClickEvents();
    registerShortcuts();
    registerDragDropHandlers();
    registerBuildPipeline();
    auto& uiMgr = m_bridge.getUIManager();
    uiMgr.setNativeClassNamesProvider([]() {
        return NativeScriptManager::Instance().getAvailableClassNames();
    });
    uiMgr.rebuildEditorUIForDpi(EditorTheme::Get().dpiScale);
    uiMgr.markAllWidgetsDirty();
    uiMgr.showToastMessage("Engine ready!", UIManager::kToastMedium);
    Logger::Instance().log(Logger::Category::Engine, "EditorApp: initialization complete.", Logger::LogLevel::INFO);
}

bool EditorApp::processEvent(const SDL_Event& event)
{
    Renderer* renderer = m_bridge.getRenderer();
    if (renderer) { SDL_Event ev = event; if (renderer->routeEventToPopup(ev)) return true; }
    return false;
}

void EditorApp::tick(float dt)
{
    auto& uiMgr = m_bridge.getUIManager();
    uiMgr.pollBuildThread();
    uiMgr.pollToolchainDetection();
    pollPIEBuild();
}

void EditorApp::shutdown()
{
    Logger::Instance().log(Logger::Category::Engine, "EditorApp: shutting down...", Logger::LogLevel::INFO);
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return;
    m_bridge.captureEditorCameraToLevel();
    m_bridge.saveActiveLevel();
    Logger::Instance().log(Logger::Category::Engine, "EditorApp: shutdown complete.", Logger::LogLevel::INFO);
}

// PIE Control
void EditorApp::stopPIE()
{
    if (!m_bridge.isPIEActive()) return;
    Renderer* renderer = m_bridge.getRenderer();
    m_bridge.setPIEActive(false);
    if (renderer) renderer->clearActiveCameraEntity();
    m_bridge.stopAllAudio();
    m_bridge.shutdownPhysics();
    NativeScriptManager::Instance().shutdownScripts();
    NativeScriptManager::Instance().unloadGameplayDLL();
    m_bridge.reloadScripts();
    m_bridge.restoreEcsSnapshot();
    m_pieMouseCaptured = false;
    m_pieInputPaused = false;
    if (auto* w = m_bridge.getWindow())
    {
        SDL_SetWindowRelativeMouseMode(w, false);
        SDL_SetWindowMouseGrab(w, false);
        SDL_WarpMouseInWindow(w, m_preCaptureMouseX, m_preCaptureMouseY);
    }
    SDL_ShowCursor();
    auto& uiMgr = m_bridge.getUIManager();
    if (auto* el = uiMgr.findElementById("ViewportOverlay.PIE")) el->textureId = m_playTexId;
    uiMgr.markAllWidgetsDirty();
    uiMgr.refreshWorldOutliner();
    if (auto* vpUI = m_bridge.getViewportUIManager()) vpUI->clearAllWidgets();
    Logger::Instance().log(Logger::Category::Engine, "PIE: stopped.", Logger::LogLevel::INFO);
}

void EditorApp::startPIE()
{
    if (m_bridge.isPIEActive()) return;
    if (m_pieBuildRunning.load()) return; // build already in progress

    // Auto-build C++ game scripts before PIE when C++ scripting is enabled
    {
        const auto mode = DiagnosticsManager::Instance().getProjectInfo().scriptingMode;
        if (mode == DiagnosticsManager::ScriptingMode::CppOnly ||
            mode == DiagnosticsManager::ScriptingMode::Both)
        {
            buildGameScriptsForPIE(); // async — pollPIEBuild() will finish PIE start
            return;
        }
    }

    finishStartPIE();
}

void EditorApp::finishStartPIE()
{
    Renderer* renderer = m_bridge.getRenderer();
    m_bridge.snapshotEcsState();
    m_bridge.initializePhysicsForPIE();
    m_bridge.setPIEActive(true);
    const unsigned int activeCamEntity = m_bridge.findActiveCameraEntity();
    if (activeCamEntity != 0 && renderer)
    {
        renderer->setActiveCameraEntity(activeCamEntity);
        Logger::Instance().log(Logger::Category::Engine, "PIE: using entity camera " + std::to_string(activeCamEntity), Logger::LogLevel::INFO);
    }
    m_pieMouseCaptured = true;
    m_pieInputPaused = false;
    SDL_GetMouseState(&m_preCaptureMouseX, &m_preCaptureMouseY);
    if (auto* w = m_bridge.getWindow())
    {
        SDL_SetWindowRelativeMouseMode(w, true);
        SDL_SetWindowMouseGrab(w, true);
    }
    SDL_HideCursor();
    auto& uiMgr = m_bridge.getUIManager();
    if (auto* el = uiMgr.findElementById("ViewportOverlay.PIE")) el->textureId = m_stopTexId;
    uiMgr.markAllWidgetsDirty();
    Logger::Instance().log(Logger::Category::Engine, "PIE: started.", Logger::LogLevel::INFO);
}

// ── Pre-PIE C++ Game Scripts Build (async) ─────────────────────────────
// Helper: recursively find a WidgetElement by id.
static WidgetElement* FindPIEBuildElement(std::vector<WidgetElement>& elements, const std::string& id)
{
    for (auto& el : elements)
    {
        if (el.id == id) return &el;
        auto* found = FindPIEBuildElement(el.children, id);
        if (found) return found;
    }
    return nullptr;
}

void EditorApp::buildGameScriptsForPIE()
{
    namespace fs = std::filesystem;
    auto& logger = Logger::Instance();
    auto& uiMgr = m_bridge.getUIManager();

    // Verify CMake + toolchain (non-fatal: skip build, go straight to PIE)
    if (!uiMgr.isCMakeAvailable() || !uiMgr.isBuildToolchainAvailable())
    {
        logger.log(Logger::Category::Engine,
            "PIE build: CMake/toolchain not available – skipping C++ build.", Logger::LogLevel::WARNING);
        finishStartPIE();
        return;
    }

    const std::string projectPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
    if (projectPath.empty()) { finishStartPIE(); return; }

    const fs::path nativeDir = fs::path(projectPath) / "Content" / "Scripts" / "Native";
    if (!fs::exists(nativeDir) || !fs::is_directory(nativeDir)) { finishStartPIE(); return; }

    // Check if there are any .cpp source files to compile
    bool hasCppFiles = false;
    for (const auto& entry : fs::directory_iterator(nativeDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cpp")
        { hasCppFiles = true; break; }
    }
    if (!hasCppFiles) { finishStartPIE(); return; }

    // ── Auto-generate _AutoRegister.cpp ────────────────────────────────
    {
        std::vector<std::pair<std::string, std::string>> discovered;
        const std::regex classPattern(
            R"(class\s+(\w+)\s*(?:final\s*)?:\s*(?:public\s+)?INativeScript\b)");

        for (const auto& entry : fs::directory_iterator(nativeDir))
        {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (ext != ".h" && ext != ".hpp") continue;

            std::ifstream hFile(entry.path());
            if (!hFile.is_open()) continue;

            std::string line;
            while (std::getline(hFile, line))
            {
                std::smatch match;
                if (std::regex_search(line, match, classPattern))
                    discovered.emplace_back(match[1].str(), entry.path().filename().string());
            }
        }

        const fs::path autoRegPath = nativeDir / "_AutoRegister.cpp";
        std::ofstream autoReg(autoRegPath, std::ios::out | std::ios::trunc);
        if (autoReg.is_open())
        {
            autoReg << "// Auto-generated by Horizon Engine -- DO NOT EDIT\n";
            autoReg << "#include \"GameplayAPI.h\"\n";
            for (const auto& [cls, hdr] : discovered)
                autoReg << "#include \"" << hdr << "\"\n";
            autoReg << "\n";
            for (const auto& [cls, hdr] : discovered)
                autoReg << "REGISTER_NATIVE_SCRIPT(" << cls << ")\n";
            autoReg.close();

            if (!discovered.empty())
            {
                logger.log(Logger::Category::Engine,
                    "PIE build: auto-registered " + std::to_string(discovered.size()) +
                    " native script class(es).", Logger::LogLevel::INFO);
            }
        }
    }

    // Unload any previously loaded GameScripts DLL
    {
        auto& nsm = NativeScriptManager::Instance();
        if (nsm.isDLLLoaded())
        {
            nsm.shutdownScripts();
            nsm.unloadGameplayDLL();
        }
    }

    const std::string cmakePath = uiMgr.getCMakePath();
    const fs::path buildDir  = fs::path(projectPath) / "Binary" / "GameScripts";
    const fs::path outputDir = fs::path(projectPath) / "Engine";

    { std::error_code ec; fs::create_directories(buildDir, ec); fs::create_directories(outputDir, ec); }

    // ── Generate CMakeLists.txt ──────────────────────────────────────
    {
        const fs::path cmakeListsPath = nativeDir / "CMakeLists.txt";
        const char* bp = SDL_GetBasePath();
        const std::string editorBinDir = bp ? std::string(bp) : fs::current_path().string();

        std::ostringstream cmake;
        cmake << "cmake_minimum_required(VERSION 3.12)\n";
        cmake << "project(GameScripts LANGUAGES CXX)\n\n";
        cmake << "set(CMAKE_CXX_STANDARD 20)\n";
        cmake << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
        cmake << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
        cmake << "if(MSVC)\n";
        cmake << "    set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL\")\n";
        cmake << "endif()\n\n";
        cmake << "file(GLOB GAME_SOURCES \"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp\")\n";
        cmake << "file(GLOB GAME_HEADERS \"${CMAKE_CURRENT_SOURCE_DIR}/*.h\")\n\n";
        cmake << "add_library(GameScripts SHARED ${GAME_SOURCES} ${GAME_HEADERS})\n\n";
        cmake << "target_include_directories(GameScripts PRIVATE\n";
        cmake << "    \"" << (fs::path(editorBinDir) / "Tools" / "include" / "NativeScripting").generic_string() << "\"\n";
        cmake << ")\n\n";
        cmake << "target_link_directories(GameScripts PRIVATE\n";
        cmake << "    \"" << (fs::path(editorBinDir) / "Tools" / "lib").generic_string() << "\"\n";
        cmake << ")\n";
        cmake << "target_link_libraries(GameScripts PRIVATE Scripting)\n\n";
        cmake << "add_custom_command(TARGET GameScripts POST_BUILD\n";
        cmake << "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n";
        cmake << "        $<TARGET_FILE:GameScripts>\n";
        cmake << "        \"" << outputDir.generic_string() << "/GameScripts.dll\"\n";
        cmake << ")\n";

        std::ofstream out(cmakeListsPath, std::ios::out | std::ios::trunc);
        if (out.is_open()) out << cmake.str();
    }

    // ── Generate VS Code IntelliSense config ─────────────────────────
    generateVSCodeConfig(projectPath);

    // ── Open build progress popup ───────────────────────────────────
    Renderer* renderer = m_bridge.getRenderer();
    m_pieBuildOutputLines.clear();
    {
        std::lock_guard<std::mutex> lock(m_pieBuildMutex);
        m_pieBuildPendingLines.clear();
        m_pieBuildFinished = false;
        m_pieBuildSuccess = false;
    }

    if (renderer)
        m_pieBuildPopup = renderer->openPopupWindow("PIEBuildOutput", "C++ Script Build", 780, 460);

    if (m_pieBuildPopup)
    {
        m_pieBuildWidget = std::make_shared<EditorWidget>();
        m_pieBuildWidget->setName("PIEBuildProgress");
        m_pieBuildWidget->setAnchor(WidgetAnchor::TopLeft);
        m_pieBuildWidget->setFillX(true);
        m_pieBuildWidget->setFillY(true);
        m_pieBuildWidget->setZOrder(100);

        const auto& theme = EditorTheme::Get();

        WidgetElement panel{};
        panel.id = "PB.Panel";
        panel.type = WidgetElementType::StackPanel;
        panel.from = Vec2{ 0.0f, 0.0f };
        panel.to   = Vec2{ 1.0f, 1.0f };
        panel.padding = Vec2{ 14.0f, 10.0f };
        panel.orientation = StackOrientation::Vertical;
        panel.style.color = theme.windowBackground;
        panel.runtimeOnly = true;

        WidgetElement title{};
        title.id = "PB.Title";
        title.type = WidgetElementType::Text;
        title.text = "Building C++ Scripts...";
        title.font = theme.fontDefault;
        title.fontSize = theme.fontSizeHeading;
        title.textAlignH = TextAlignH::Left;
        title.style.textColor = theme.textPrimary;
        title.fillX = true;
        title.minSize = Vec2{ 0.0f, 28.0f };
        title.runtimeOnly = true;

        WidgetElement status{};
        status.id = "PB.Status";
        status.type = WidgetElementType::Text;
        status.text = "Configuring...";
        status.font = theme.fontDefault;
        status.fontSize = theme.fontSizeBody;
        status.textAlignH = TextAlignH::Left;
        status.style.textColor = theme.textSecondary;
        status.fillX = true;
        status.minSize = Vec2{ 0.0f, 20.0f };
        status.runtimeOnly = true;

        // Scrollable build output log
        WidgetElement outputPanel{};
        outputPanel.id = "PB.OutputScroll";
        outputPanel.type = WidgetElementType::StackPanel;
        outputPanel.orientation = StackOrientation::Vertical;
        outputPanel.fillX = true;
        outputPanel.fillY = true;
        outputPanel.scrollable = true;
        outputPanel.style.color = Vec4{ 0.04f, 0.04f, 0.04f, 0.95f };
        outputPanel.style.borderRadius = 4.0f;
        outputPanel.padding = Vec2{ 6.0f, 4.0f };
        outputPanel.runtimeOnly = true;

        // Close button (hidden during build)
        auto closeBtn = EditorUIBuilder::makePrimaryButton("PB.CloseBtn", "Close", [this]() {
            dismissPIEBuildPopup();
        });
        closeBtn.isCollapsed = true;

        panel.children.push_back(std::move(title));
        panel.children.push_back(std::move(status));
        panel.children.push_back(std::move(outputPanel));
        panel.children.push_back(std::move(closeBtn));

        std::vector<WidgetElement> elems;
        elems.push_back(std::move(panel));
        m_pieBuildWidget->setElements(std::move(elems));
        m_pieBuildWidget->markLayoutDirty();
        m_pieBuildPopup->uiManager().registerWidget("PIEBuildProgress", m_pieBuildWidget);
    }

    // ── Launch build thread ───────────────────────────────────────────
    if (m_pieBuildThread.joinable())
        m_pieBuildThread.join();

    m_pieBuildRunning.store(true);

    const std::string nativeDirStr = nativeDir.string();
    const std::string buildDirStr  = buildDir.string();

    m_pieBuildThread = std::thread([this, cmakePath, nativeDirStr, buildDirStr]()
    {
        auto appendLine = [this](const std::string& line) {
            std::lock_guard<std::mutex> lock(m_pieBuildMutex);
            m_pieBuildPendingLines.push_back(line);
        };

        // Helper: run a command, capture output line-by-line
#if defined(_WIN32)
        auto runCmd = [&](const std::string& cmd) -> int
        {
            appendLine("> " + cmd);

            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;

            HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
            if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
                return -1;
            SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

            STARTUPINFOA si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdOutput = hWritePipe;
            si.hStdError  = hWritePipe;

            PROCESS_INFORMATION pi{};
            std::string cmdLine = cmd;
            BOOL created = CreateProcessA(
                nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
            CloseHandle(hWritePipe);

            if (!created)
            {
                CloseHandle(hReadPipe);
                appendLine("[ERROR] Failed to launch process.");
                return -1;
            }

            char buf[4096];
            DWORD bytesRead = 0;
            std::string lineBuffer;
            while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
            {
                buf[bytesRead] = '\0';
                lineBuffer += buf;
                size_t pos;
                while ((pos = lineBuffer.find('\n')) != std::string::npos)
                {
                    std::string line = lineBuffer.substr(0, pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    appendLine(line);
                    lineBuffer.erase(0, pos + 1);
                }
            }
            if (!lineBuffer.empty())
                appendLine(lineBuffer);

            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hReadPipe);
            return static_cast<int>(exitCode);
        };
#else
        auto runCmd = [&](const std::string& cmd) -> int
        {
            appendLine("> " + cmd);
            std::string fullCmd = cmd + " 2>&1";
            FILE* pipe = popen(fullCmd.c_str(), "r");
            if (!pipe) return -1;
            char buf[4096];
            while (fgets(buf, sizeof(buf), pipe))
            {
                std::string line = buf;
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                    line.pop_back();
                appendLine(line);
            }
            return pclose(pipe);
        };
#endif

        bool success = true;

        // CMake configure
        {
            std::string configureCmd = "\"" + cmakePath + "\"";
            configureCmd += " -S \"" + nativeDirStr + "\"";
            configureCmd += " -B \"" + buildDirStr + "\"";
            int ret = runCmd(configureCmd);
            if (ret != 0)
            {
                appendLine("[ERROR] CMake configure failed (exit code " + std::to_string(ret) + ").");
                success = false;
            }
        }

        // CMake build
        if (success)
        {
            std::string buildCmd = "\"" + cmakePath + "\"";
            buildCmd += " --build \"" + buildDirStr + "\"";
            buildCmd += " --target GameScripts";
            buildCmd += " --config RelWithDebInfo";
            int ret = runCmd(buildCmd);
            if (ret != 0)
            {
                appendLine("[ERROR] CMake build failed (exit code " + std::to_string(ret) + ").");
                success = false;
            }
        }

        if (success)
            appendLine("[OK] Build completed successfully.");

        // Signal completion
        {
            std::lock_guard<std::mutex> lock(m_pieBuildMutex);
            m_pieBuildFinished = true;
            m_pieBuildSuccess = success;
        }
        m_pieBuildRunning.store(false);
    });

    logger.log(Logger::Category::Engine, "PIE build: compiling C++ game scripts (async)...", Logger::LogLevel::INFO);
}

void EditorApp::generateVSCodeConfig(const std::string& projectPath)
{
    namespace fs = std::filesystem;

    const char* bp = SDL_GetBasePath();
    const std::string editorBinDir = bp ? std::string(bp) : fs::current_path().string();
    const std::string includePath = (fs::path(editorBinDir) / "Tools" / "include" / "NativeScripting").generic_string();

    const fs::path vscodeDir = fs::path(projectPath) / "Content" / "Scripts" / ".vscode";
    const fs::path configPath = vscodeDir / "c_cpp_properties.json";

    std::error_code ec;
    fs::create_directories(vscodeDir, ec);

    std::ostringstream json;
    json << "{\n";
    json << "    \"configurations\": [\n";
    json << "        {\n";
    json << "            \"name\": \"Horizon Engine\",\n";
    json << "            \"includePath\": [\n";
    json << "                \"${workspaceFolder}/Native/**\",\n";
    json << "                \"" << includePath << "\"\n";
    json << "            ],\n";
    json << "            \"defines\": [\n";
    json << "                \"GAMEPLAY_EXPORTS\"\n";
    json << "            ],\n";
    json << "            \"cStandard\": \"c17\",\n";
    json << "            \"cppStandard\": \"c++20\",\n";
#ifdef _WIN32
    json << "            \"intelliSenseMode\": \"windows-msvc-x64\"\n";
#else
    json << "            \"intelliSenseMode\": \"linux-gcc-x64\"\n";
#endif
    json << "        }\n";
    json << "    ],\n";
    json << "    \"version\": 4\n";
    json << "}\n";

    std::ofstream out(configPath, std::ios::out | std::ios::trunc);
    if (out.is_open())
    {
        out << json.str();
        out.close();
        Logger::Instance().log(Logger::Category::Engine,
            "Generated VS Code IntelliSense config: " + configPath.string(),
            Logger::LogLevel::INFO);
    }
}

void EditorApp::pollPIEBuild()
{
    if (!m_pieBuildWidget) return;

    std::vector<std::string> newLines;
    bool finished = false;
    bool success  = false;

    {
        std::lock_guard<std::mutex> lock(m_pieBuildMutex);
        if (!m_pieBuildPendingLines.empty())
            newLines.swap(m_pieBuildPendingLines);
        if (m_pieBuildFinished)
        {
            finished = true;
            success  = m_pieBuildSuccess;
            m_pieBuildFinished = false;
        }
    }

    bool dirty = false;

    // Append new output lines to the scroll panel
    if (!newLines.empty())
    {
        for (auto& ln : newLines)
            m_pieBuildOutputLines.push_back(std::move(ln));

        auto& elems = m_pieBuildWidget->getElementsMutable();
        WidgetElement* outputScroll = FindPIEBuildElement(elems, "PB.OutputScroll");
        if (outputScroll)
        {
            const auto& theme = EditorTheme::Get();
            const float rowH = EditorTheme::Scaled(16.0f);

            outputScroll->children.clear();
            for (size_t i = 0; i < m_pieBuildOutputLines.size(); ++i)
            {
                const auto& line = m_pieBuildOutputLines[i];
                const bool isError = line.find("[ERROR]") != std::string::npos
                                  || line.find("error") != std::string::npos
                                  || line.find("FAILED") != std::string::npos;

                WidgetElement row{};
                row.id        = "PB.Row." + std::to_string(i);
                row.type      = WidgetElementType::Text;
                row.text      = line;
                row.font      = theme.fontDefault;
                row.fontSize  = theme.fontSizeSmall;
                row.style.textColor = isError
                    ? Vec4{ 0.95f, 0.3f, 0.3f, 1.0f }
                    : Vec4{ 0.75f, 0.80f, 0.75f, 1.0f };
                row.textAlignH = TextAlignH::Left;
                row.textAlignV = TextAlignV::Center;
                row.fillX     = true;
                row.minSize   = Vec2{ 0.0f, rowH };
                row.runtimeOnly = true;
                outputScroll->children.push_back(std::move(row));
            }

            // Auto-scroll to bottom
            const float totalH = rowH * static_cast<float>(m_pieBuildOutputLines.size());
            outputScroll->scrollOffset = totalH;
        }
        dirty = true;
    }

    // Handle build completion
    if (finished)
    {
        if (m_pieBuildThread.joinable())
            m_pieBuildThread.join();

        auto& logger = Logger::Instance();
        auto& uiMgr  = m_bridge.getUIManager();

        if (success)
        {
            // Load the built DLL on the main thread
            namespace fs = std::filesystem;
            const std::string projectPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
            const fs::path dllPath = fs::path(projectPath) / "Engine" / "GameScripts.dll";

            bool dllOk = false;
            if (fs::exists(dllPath))
            {
                auto& nsm = NativeScriptManager::Instance();
                if (nsm.loadGameplayDLL(dllPath.string()))
                {
                    nsm.initializeScripts();
                    logger.log(Logger::Category::Engine,
                        "PIE build: GameScripts.dll loaded successfully.", Logger::LogLevel::INFO);
                    dllOk = true;
                }
            }

            if (dllOk)
            {
                // Auto-close popup on success
                dismissPIEBuildPopup();
                uiMgr.showToastMessage("C++ scripts built successfully.", UIManager::kToastShort,
                    UIManager::NotificationLevel::Success);
                finishStartPIE();
            }
            else
            {
                // DLL load failed — treat as build error
                auto& elems = m_pieBuildWidget->getElementsMutable();
                WidgetElement* titleEl = FindPIEBuildElement(elems, "PB.Title");
                if (titleEl) titleEl->text = "Build Failed";
                WidgetElement* statusEl = FindPIEBuildElement(elems, "PB.Status");
                if (statusEl) statusEl->text = "Failed to load GameScripts.dll after build.";
                statusEl->style.textColor = Vec4{ 0.95f, 0.3f, 0.3f, 1.0f };
                WidgetElement* closeBtn = FindPIEBuildElement(elems, "PB.CloseBtn");
                if (closeBtn) closeBtn->isCollapsed = false;

                logger.log(Logger::Category::Engine,
                    "PIE build: failed to load GameScripts.dll.", Logger::LogLevel::WARNING);
                uiMgr.showToastMessage("C++ scripts build failed.", UIManager::kToastLong,
                    UIManager::NotificationLevel::Error);
            }
        }
        else
        {
            // Build failed — update popup to show error, keep open
            auto& elems = m_pieBuildWidget->getElementsMutable();
            WidgetElement* titleEl = FindPIEBuildElement(elems, "PB.Title");
            if (titleEl) titleEl->text = "Build Failed";
            WidgetElement* statusEl = FindPIEBuildElement(elems, "PB.Status");
            if (statusEl)
            {
                statusEl->text = "Compilation errors found. Review the output below.";
                statusEl->style.textColor = Vec4{ 0.95f, 0.3f, 0.3f, 1.0f };
            }
            WidgetElement* closeBtn = FindPIEBuildElement(elems, "PB.CloseBtn");
            if (closeBtn) closeBtn->isCollapsed = false;

            logger.log(Logger::Category::Engine,
                "PIE build: C++ game scripts build failed.", Logger::LogLevel::WARNING);
            uiMgr.showToastMessage("C++ scripts build failed.\nSee build output window for details.", UIManager::kToastLong,
                UIManager::NotificationLevel::Error);
        }
        dirty = true;
    }

    if (dirty && m_pieBuildWidget)
    {
        m_pieBuildWidget->markLayoutDirty();
        if (m_pieBuildPopup)
            m_pieBuildPopup->uiManager().markRenderDirty();
    }
}

void EditorApp::dismissPIEBuildPopup()
{
    if (m_pieBuildPopup)
    {
        m_pieBuildPopup->uiManager().unregisterWidget("PIEBuildProgress");
        Renderer* renderer = m_bridge.getRenderer();
        if (renderer)
            renderer->closePopupWindow("PIEBuildOutput");
        m_pieBuildPopup = nullptr;
    }
    m_pieBuildWidget.reset();
    m_pieBuildOutputLines.clear();
}

// Widget Registration
void EditorApp::registerWidgets()
{
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return;
    auto& uiMgr = m_bridge.getUIManager();

    auto loadWidget = [this](const std::string& assetName) -> std::shared_ptr<Widget>
    {
        const std::string path = m_bridge.getEditorWidgetPath(assetName);
        if (path.empty()) return nullptr;
        const int id = m_bridge.loadAsset(path, static_cast<int>(AssetType::Widget));
        if (id == 0) return nullptr;
        auto asset = m_bridge.getLoadedAssetByID(static_cast<unsigned int>(id));
        if (!asset) return nullptr;
        return m_bridge.createWidgetFromAsset(asset);
    };

    if (auto widget = loadWidget("TitleBar.asset"))
        uiMgr.registerWidget("TitleBar", widget);

    renderer->addTab("Viewport", "Viewport", false);

    if (auto widget = loadWidget("ViewportOverlay.asset"))
        uiMgr.registerWidget("ViewportOverlay", widget, "Viewport");

    // Restore snap & grid settings
    {
        if (auto v = m_bridge.getState("GridSnapEnabled"))
        {
            m_gridSnapEnabled = (*v == "1");
            renderer->setSnapEnabled(m_gridSnapEnabled);
            renderer->setGridVisible(m_gridSnapEnabled);
            if (auto* el = uiMgr.findElementById("ViewportOverlay.Snap"))
                el->style.textColor = m_gridSnapEnabled ? Vec4{1.0f,1.0f,1.0f,1.0f} : Vec4{0.45f,0.45f,0.45f,1.0f};
        }
        if (auto v = m_bridge.getState("GridSize"))
        {
            try {
                const float gs = std::stof(*v);
                if (gs > 0.0f)
                {
                    renderer->setGridSize(gs);
                    if (auto* el = uiMgr.findElementById("ViewportOverlay.GridSize"))
                    {
                        char buf[16]; std::snprintf(buf, sizeof(buf), "%.2g", static_cast<double>(gs));
                        el->text = buf;
                    }
                }
            } catch (...) {}
        }
        uiMgr.markAllWidgetsDirty();
    }

    // WorldSettings (color picker + skybox)
    if (auto widget = loadWidget("WorldSettings.asset"))
    {
        auto findElem = [](std::vector<WidgetElement>& elements, const std::string& id) -> WidgetElement*
        {
            const std::function<WidgetElement*(WidgetElement&)> find =
                [&](WidgetElement& e) -> WidgetElement* {
                    if (e.id == id) return &e;
                    for (auto& c : e.children) if (auto* h = find(c)) return h;
                    return nullptr;
                };
            for (auto& e : elements) if (auto* h = find(e)) return h;
            return nullptr;
        };

        if (auto* picker = findElem(widget->getElementsMutable(), "WorldSettings.ClearColor"))
        {
            picker->style.color = renderer->getClearColor();
            picker->onColorChanged = [renderer](const Vec4& color) { renderer->setClearColor(color); };
            WidgetElement* stack = nullptr;
            for (auto& child : picker->children)
                if (child.type == WidgetElementType::StackPanel) { stack = &child; break; }
            if (stack)
            {
                struct RgbState { int r{0}; int g{0}; int b{0}; WidgetElement* picker{nullptr}; };
                auto state = std::make_shared<RgbState>();
                state->r = static_cast<int>(std::round(std::clamp(picker->style.color.x, 0.0f, 1.0f) * 255.0f));
                state->g = static_cast<int>(std::round(std::clamp(picker->style.color.y, 0.0f, 1.0f) * 255.0f));
                state->b = static_cast<int>(std::round(std::clamp(picker->style.color.z, 0.0f, 1.0f) * 255.0f));
                state->picker = picker;
                const auto applyColor = [state]() {
                    if (!state->picker || !state->picker->onColorChanged) return;
                    Vec4 c{std::clamp(state->r/255.0f,0.0f,1.0f), std::clamp(state->g/255.0f,0.0f,1.0f),
                            std::clamp(state->b/255.0f,0.0f,1.0f), 1.0f};
                    state->picker->style.color = c; state->picker->onColorChanged(c);
                };
                auto configureEntry = [&](const std::string& id, int& channel) {
                    if (auto* entry = findElem(stack->children, id)) {
                        entry->value = std::to_string(channel);
                        entry->onValueChanged = [&channel, applyColor](const std::string& value) {
                            try { int p = std::stoi(value); channel = std::clamp(p, 0, 255); applyColor(); } catch (...) {}
                        };
                    }
                };
                configureEntry("WorldSettings.ClearColor.R", state->r);
                configureEntry("WorldSettings.ClearColor.G", state->g);
                configureEntry("WorldSettings.ClearColor.B", state->b);
            }
        }

        if (auto* skyboxEntry = findElem(widget->getElementsMutable(), "WorldSettings.SkyboxPath"))
        {
            skyboxEntry->value = renderer->getSkyboxPath();
            skyboxEntry->onValueChanged = [renderer](const std::string& value) {
                renderer->setSkyboxPath(value);
                auto& diag = DiagnosticsManager::Instance();
                if (auto* level = diag.getActiveLevelSoft()) level->setSkyboxPath(value);
                renderer->getUIManager().refreshStatusBar();
            };
            if (auto* clearBtn = findElem(widget->getElementsMutable(), "WorldSettings.SkyboxClear"))
                clearBtn->onClicked = [renderer]() {
                    renderer->setSkyboxPath("");
                    auto& diag = DiagnosticsManager::Instance();
                    if (auto* level = diag.getActiveLevelSoft()) level->setSkyboxPath("");
                    renderer->getUIManager().refreshStatusBar();
                    renderer->getUIManager().showToastMessage("Skybox cleared", UIManager::kToastMedium);
                };
        }
        else
        {
            auto& elements = widget->getElementsMutable();
            WidgetElement* rootStack = nullptr;
            for (auto& el : elements) if (el.type == WidgetElementType::StackPanel) { rootStack = &el; break; }
            if (rootStack)
            {
                WidgetElement skyLabel{}; skyLabel.id = "WorldSettings.SkyboxLabel"; skyLabel.type = WidgetElementType::Text;
                skyLabel.text = "Skybox Asset"; skyLabel.font = "default.ttf"; skyLabel.fontSize = 13.0f;
                skyLabel.style.textColor = Vec4{0.7f,0.75f,0.85f,1.0f}; skyLabel.fillX = true;
                skyLabel.minSize = Vec2{0.0f,22.0f}; skyLabel.padding = Vec2{4.0f,2.0f}; skyLabel.runtimeOnly = true;
                rootStack->children.push_back(std::move(skyLabel));

                WidgetElement skyEntry{}; skyEntry.id = "WorldSettings.SkyboxPath"; skyEntry.type = WidgetElementType::EntryBar;
                skyEntry.value = renderer->getSkyboxPath(); skyEntry.font = "default.ttf"; skyEntry.fontSize = 13.0f;
                skyEntry.style.textColor = Vec4{0.9f,0.9f,0.95f,1.0f}; skyEntry.style.color = Vec4{0.12f,0.12f,0.16f,0.9f};
                skyEntry.fillX = true; skyEntry.minSize = Vec2{0.0f,24.0f}; skyEntry.padding = Vec2{6.0f,4.0f};
                skyEntry.hitTestMode = HitTestMode::Enabled; skyEntry.runtimeOnly = true;
                skyEntry.onValueChanged = [renderer](const std::string& value) {
                    renderer->setSkyboxPath(value);
                    auto& diag = DiagnosticsManager::Instance();
                    if (auto* level = diag.getActiveLevelSoft()) level->setSkyboxPath(value);
                    renderer->getUIManager().refreshStatusBar();
                };
                rootStack->children.push_back(std::move(skyEntry));

                WidgetElement clearBtn{}; clearBtn.id = "WorldSettings.SkyboxClear"; clearBtn.type = WidgetElementType::Button;
                clearBtn.text = "Clear Skybox"; clearBtn.font = "default.ttf"; clearBtn.fontSize = 12.0f;
                clearBtn.style.textColor = Vec4{0.9f,0.7f,0.7f,1.0f}; clearBtn.textAlignH = TextAlignH::Center;
                clearBtn.textAlignV = TextAlignV::Center; clearBtn.style.color = Vec4{0.3f,0.15f,0.15f,0.8f};
                clearBtn.style.hoverColor = Vec4{0.45f,0.2f,0.2f,0.95f};
                clearBtn.shaderVertex = "button_vertex.glsl"; clearBtn.shaderFragment = "button_fragment.glsl";
                clearBtn.fillX = true; clearBtn.minSize = Vec2{0.0f,24.0f}; clearBtn.padding = Vec2{4.0f,2.0f};
                clearBtn.hitTestMode = HitTestMode::Enabled; clearBtn.runtimeOnly = true;
                clearBtn.onClicked = [renderer]() {
                    renderer->setSkyboxPath("");
                    auto& diag = DiagnosticsManager::Instance();
                    if (auto* level = diag.getActiveLevelSoft()) level->setSkyboxPath("");
                    renderer->getUIManager().refreshStatusBar();
                    renderer->getUIManager().showToastMessage("Skybox cleared", UIManager::kToastMedium);
                };
                rootStack->children.push_back(std::move(clearBtn));
            }
        }
        uiMgr.registerWidget("WorldSettings", widget, "Viewport");
    }

    if (auto widget = loadWidget("WorldOutliner.asset")) uiMgr.registerWidget("WorldOutliner", widget, "Viewport");
    if (auto widget = loadWidget("EntityDetails.asset")) uiMgr.registerWidget("EntityDetails", widget, "Viewport");
    if (auto widget = loadWidget("StatusBar.asset")) uiMgr.registerWidget("StatusBar", widget);

    {
        const std::string cbPath = m_bridge.getEditorWidgetPath("ContentBrowser.asset");
        if (!cbPath.empty()) {
            const int cbId = m_bridge.loadAsset(cbPath, static_cast<int>(AssetType::Widget));
            if (cbId != 0) {
                if (auto asset = m_bridge.getLoadedAssetByID(static_cast<unsigned int>(cbId)))
                    if (auto widget = m_bridge.createWidgetFromAsset(asset))
                        uiMgr.registerWidget("ContentBrowser", widget, "Viewport");
            }
        }
    }
}

// Click Event Registration
void EditorApp::registerClickEvents()
{
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return;
    auto& uiMgr = m_bridge.getUIManager();

    uiMgr.registerClickEvent("TitleBar.Close", []() { Logger::Instance().log(Logger::Category::Input, "TitleBar close button clicked.", Logger::LogLevel::INFO); DiagnosticsManager::Instance().requestShutdown(); });
    uiMgr.registerClickEvent("TitleBar.Minimize", [renderer]() { Logger::Instance().log(Logger::Category::Input, "TitleBar minimize button clicked.", Logger::LogLevel::INFO); if (auto* w = renderer->window()) SDL_MinimizeWindow(w); });
    uiMgr.registerClickEvent("TitleBar.Maximize", [renderer]() { Logger::Instance().log(Logger::Category::Input, "TitleBar maximize button clicked.", Logger::LogLevel::INFO); if (auto* w = renderer->window()) { if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED) SDL_RestoreWindow(w); else SDL_MaximizeWindow(w); } });
    uiMgr.registerClickEvent("TitleBar.Menu.File", []() { Logger::Instance().log(Logger::Category::Input, "Menu: File clicked.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("TitleBar.Menu.Edit", []() { Logger::Instance().log(Logger::Category::Input, "Menu: Edit clicked.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("TitleBar.Menu.Window", []() { Logger::Instance().log(Logger::Category::Input, "Menu: Window clicked.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("TitleBar.Menu.Build", []() { Logger::Instance().log(Logger::Category::Input, "Menu: Build clicked.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("TitleBar.Menu.Help", []() { Logger::Instance().log(Logger::Category::Input, "Menu: Help clicked.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("WorldSettings.Tools.Landscape", [renderer]() { renderer->getUIManager().openLandscapeManagerPopup(); });
    uiMgr.registerClickEvent("WorldSettings.Tools.MaterialEditor", [renderer]() { renderer->getUIManager().openMaterialEditorPopup(); });
    uiMgr.registerClickEvent("TitleBar.Tab.Viewport", [renderer]() { renderer->setActiveTab("Viewport"); renderer->getUIManager().markAllWidgetsDirty(); });
    uiMgr.registerClickEvent("ViewportOverlay.Select", []() { Logger::Instance().log(Logger::Category::Input, "Toolbar: Select mode.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("ViewportOverlay.Move", []() { Logger::Instance().log(Logger::Category::Input, "Toolbar: Move mode.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("ViewportOverlay.Rotate", []() { Logger::Instance().log(Logger::Category::Input, "Toolbar: Rotate mode.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("ViewportOverlay.Scale", []() { Logger::Instance().log(Logger::Category::Input, "Toolbar: Scale mode.", Logger::LogLevel::INFO); });
    uiMgr.registerClickEvent("ViewportOverlay.Undo", [renderer]() { UndoRedoManager::Instance().undo(); renderer->getUIManager().markAllWidgetsDirty(); renderer->getUIManager().refreshWorldOutliner(); });
    uiMgr.registerClickEvent("ViewportOverlay.Redo", [renderer]() { UndoRedoManager::Instance().redo(); renderer->getUIManager().markAllWidgetsDirty(); renderer->getUIManager().refreshWorldOutliner(); });

    uiMgr.registerClickEvent("ViewportOverlay.Snap", [this, renderer]() {
        m_gridSnapEnabled = !m_gridSnapEnabled; renderer->setSnapEnabled(m_gridSnapEnabled); renderer->setGridVisible(m_gridSnapEnabled);
        if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Snap")) { el->style.textColor = m_gridSnapEnabled ? Vec4{1,1,1,1} : Vec4{0.45f,0.45f,0.45f,1}; renderer->getUIManager().markAllWidgetsDirty(); }
        renderer->getUIManager().showToastMessage(m_gridSnapEnabled ? "Grid Snap: ON" : "Grid Snap: OFF", UIManager::kToastShort);
        DiagnosticsManager::Instance().setState("GridSnapEnabled", m_gridSnapEnabled ? "1" : "0");
    });

    uiMgr.registerClickEvent("ViewportOverlay.Colliders", [renderer]() {
        const bool v = !renderer->isCollidersVisible(); renderer->setCollidersVisible(v);
        if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Colliders")) { el->style.textColor = v ? Vec4{1,1,1,1} : Vec4{0.45f,0.45f,0.45f,1}; renderer->getUIManager().markAllWidgetsDirty(); }
        renderer->getUIManager().showToastMessage(v ? "Colliders: ON" : "Colliders: OFF", UIManager::kToastShort);
        DiagnosticsManager::Instance().setState("CollidersVisible", v ? "1" : "0");
    });

    uiMgr.registerClickEvent("ViewportOverlay.Bones", [renderer]() {
        const bool v = !renderer->isBonesVisible(); renderer->setBonesVisible(v);
        if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Bones")) { el->style.textColor = v ? Vec4{1,1,1,1} : Vec4{0.45f,0.45f,0.45f,1}; renderer->getUIManager().markAllWidgetsDirty(); }
        renderer->getUIManager().showToastMessage(v ? "Bones: ON" : "Bones: OFF", UIManager::kToastShort);
        DiagnosticsManager::Instance().setState("BonesVisible", v ? "1" : "0");
    });

    uiMgr.registerClickEvent("ViewportOverlay.Layout", [renderer]() {
        auto& ui = renderer->getUIManager(); if (ui.isDropdownMenuOpen()) { ui.closeDropdownMenu(); return; }
        auto* btn = ui.findElementById("ViewportOverlay.Layout"); Vec2 anchor{0,0};
        if (btn && btn->hasBounds) anchor = Vec2{btn->boundsMinPixels.x, btn->boundsMaxPixels.y+2.0f};
        struct LE { const char* l; Renderer::ViewportLayout v; };
        static const LE layouts[] = {{"Single",Renderer::ViewportLayout::Single},{"Two Horizontal",Renderer::ViewportLayout::TwoHorizontal},{"Two Vertical",Renderer::ViewportLayout::TwoVertical},{"Quad",Renderer::ViewportLayout::Quad}};
        const auto cur = renderer->getViewportLayout(); std::vector<UIManager::DropdownMenuItem> items;
        for (auto& e : layouts) { std::string lbl=e.l; auto lay=e.v; if (cur==lay) lbl="> "+lbl;
            items.push_back({lbl,[renderer,lay](){renderer->setViewportLayout(lay);renderer->setActiveSubViewport(0);renderer->getUIManager().showToastMessage(std::string("Layout: ")+Renderer::viewportLayoutToString(lay),UIManager::kToastShort);}}); }
        ui.showDropdownMenu(anchor,items);
    });

    uiMgr.registerClickEvent("ViewportOverlay.GridSize", [renderer]() {
        auto& ui = renderer->getUIManager(); if (ui.isDropdownMenuOpen()) { ui.closeDropdownMenu(); return; }
        auto* btn = ui.findElementById("ViewportOverlay.GridSize"); Vec2 anchor{0,0};
        if (btn && btn->hasBounds) anchor = Vec2{btn->boundsMinPixels.x, btn->boundsMaxPixels.y+2.0f};
        struct GE { const char* l; float v; };
        static const GE sizes[] = {{"0.25",0.25f},{"0.5",0.5f},{"1.0",1.0f},{"2.0",2.0f},{"5.0",5.0f},{"10.0",10.0f}};
        const float cur = renderer->getGridSize(); std::vector<UIManager::DropdownMenuItem> items;
        for (auto& s : sizes) { float val=s.v; std::string lbl=s.l; if (std::abs(cur-val)<0.01f) lbl="> "+lbl;
            items.push_back({lbl,[renderer,val](){renderer->setGridSize(val);
                if (auto* el=renderer->getUIManager().findElementById("ViewportOverlay.GridSize")){char buf[16];std::snprintf(buf,sizeof(buf),"%.2g",(double)val);el->text=buf;renderer->getUIManager().markAllWidgetsDirty();}
                DiagnosticsManager::Instance().setState("GridSize",std::to_string(val));}}); }
        ui.showDropdownMenu(anchor,items);
    });

    uiMgr.registerClickEvent("ViewportOverlay.CamSpeed", [this, renderer]() {
        auto& ui = renderer->getUIManager(); if (ui.isDropdownMenuOpen()) { ui.closeDropdownMenu(); return; }
        auto* btn = ui.findElementById("ViewportOverlay.CamSpeed"); Vec2 anchor{0,0};
        if (btn && btn->hasBounds) anchor = Vec2{btn->boundsMinPixels.x, btn->boundsMaxPixels.y+2.0f};
        struct SE { const char* l; float v; };
        static const SE speeds[] = {{"0.25x",0.25f},{"0.5x",0.5f},{"1.0x",1.0f},{"1.5x",1.5f},{"2.0x",2.0f},{"3.0x",3.0f},{"5.0x",5.0f}};
        std::vector<UIManager::DropdownMenuItem> items;
        for (auto& s : speeds) { float val=s.v; std::string lbl=s.l; if (std::abs(m_cameraSpeedMultiplier-val)<0.01f) lbl="> "+lbl;
            items.push_back({lbl,[this,renderer,val](){m_cameraSpeedMultiplier=val;
                if (auto* el=renderer->getUIManager().findElementById("ViewportOverlay.CamSpeed")){char buf[16];std::snprintf(buf,sizeof(buf),"%.1fx",val);el->text=buf;renderer->getUIManager().markAllWidgetsDirty();}}}); }
        ui.showDropdownMenu(anchor,items);
    });

    uiMgr.registerClickEvent("ViewportOverlay.Stats", [this, renderer]() {
        m_showMetrics = !m_showMetrics;
        if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Stats")) { el->style.textColor = m_showMetrics ? Vec4{1,1,1,1} : Vec4{0.45f,0.45f,0.45f,1}; renderer->getUIManager().markAllWidgetsDirty(); }
    });

    uiMgr.registerClickEvent("ViewportOverlay.RenderMode", [renderer]() {
        auto& ui = renderer->getUIManager(); if (ui.isDropdownMenuOpen()) { ui.closeDropdownMenu(); return; }
        auto* btn = ui.findElementById("ViewportOverlay.RenderMode"); Vec2 anchor{0,0};
        if (btn && btn->hasBounds) anchor = Vec2{btn->boundsMinPixels.x, btn->boundsMaxPixels.y+2.0f};
        struct ME { const char* l; Renderer::DebugRenderMode m; };
        static const ME modes[] = {{"Lit",Renderer::DebugRenderMode::Lit},{"Unlit",Renderer::DebugRenderMode::Unlit},{"Wireframe",Renderer::DebugRenderMode::Wireframe},{"Shadow Map",Renderer::DebugRenderMode::ShadowMap},{"Shadow Cascades",Renderer::DebugRenderMode::ShadowCascades},{"Instance Groups",Renderer::DebugRenderMode::InstanceGroups},{"Normals",Renderer::DebugRenderMode::Normals},{"Depth",Renderer::DebugRenderMode::Depth},{"Overdraw",Renderer::DebugRenderMode::Overdraw}};
        std::vector<UIManager::DropdownMenuItem> items;
        for (auto& m : modes) { std::string label=m.l; auto mode=m.m;
            items.push_back({label,[renderer,mode,label](){renderer->setDebugRenderMode(mode);auto* elem=renderer->getUIManager().findElementById("ViewportOverlay.RenderMode");if(elem)elem->text=label;renderer->getUIManager().markAllWidgetsDirty();}}); }
        ui.showDropdownMenu(anchor,items);
    });

    uiMgr.registerClickEvent("ViewportOverlay.Settings", [renderer]() {
        auto& ui = renderer->getUIManager(); if (ui.isDropdownMenuOpen()) { ui.closeDropdownMenu(); return; }
        auto* btn = ui.findElementById("ViewportOverlay.Settings"); Vec2 anchor{0,0};
        if (btn && btn->hasBounds) { constexpr float kW=180.0f; anchor = Vec2{btn->boundsMaxPixels.x-kW, btn->boundsMaxPixels.y+2.0f}; }
        std::vector<UIManager::DropdownMenuItem> items;
        items.push_back({"Engine Settings",[renderer](){renderer->getUIManager().openEngineSettingsPopup();}});
        items.push_back({"Editor Settings",[renderer](){renderer->getUIManager().openEditorSettingsPopup();}});
        items.push_back({"Console",[renderer](){renderer->getUIManager().openConsoleTab();}});
        items.push_back({"Profiler",[renderer](){renderer->getUIManager().openProfilerTab();}});
        items.push_back({"Shader Viewer",[renderer](){renderer->getUIManager().openShaderViewerTab();}});
        items.push_back({"Render Debugger",[renderer](){renderer->getUIManager().openRenderDebuggerTab();}});
        items.push_back({"Sequencer",[renderer](){renderer->getUIManager().openSequencerTab();}});
        items.push_back({"Level Composition",[renderer](){renderer->getUIManager().openLevelCompositionTab();}});
        items.push_back({"---",[](){}});
        items.push_back({"Build Game...",[renderer](){renderer->getUIManager().openBuildGameDialog();}});
        items.push_back({"Drop to Surface (End)",[renderer](){renderer->getUIManager().dropSelectedEntitiesToSurface([](float ox,float oy,float oz)->std::pair<bool,float>{auto hit=PhysicsWorld::Instance().raycast(ox,oy,oz,0,-1,0,10000.0f);return{hit.hit,hit.point[1]};});}});
        const auto& pluginItems = Scripting::GetPluginMenuItems();
        for (size_t i=0;i<pluginItems.size();++i){const auto& pi=pluginItems[i];items.push_back({"[Plugin] "+pi.name,[i](){Scripting::InvokePluginMenuCallback(i);}});}
        ui.showDropdownMenu(anchor,items);
    });

    uiMgr.registerClickEvent("ViewportOverlay.PIE", [this]() { if (!m_bridge.isPIEActive()) startPIE(); else stopPIE(); });

    uiMgr.registerClickEvent("ContentBrowser.PathBar.Import", [renderer]() {
        auto* window = renderer ? renderer->window() : nullptr;
        AssetManager::Instance().OpenImportDialog(window, AssetType::Unknown, AssetManager::Async);
    });

    m_bridge.setOnImportCompleted([renderer]() { if (renderer) renderer->getUIManager().refreshContentBrowser(); });

    UndoRedoManager::Instance().setOnChanged([renderer]() {
        auto& diag = DiagnosticsManager::Instance(); auto* level = diag.getActiveLevelSoft();
        if (level) level->setIsSaved(false); if (renderer) renderer->getUIManager().refreshStatusBar();
    });

    uiMgr.registerClickEvent("StatusBar.Undo", [renderer]() { auto& u=UndoRedoManager::Instance(); if (u.canUndo()){u.undo();renderer->getUIManager().markAllWidgetsDirty();} });
    uiMgr.registerClickEvent("StatusBar.Redo", [renderer]() { auto& u=UndoRedoManager::Instance(); if (u.canRedo()){u.redo();renderer->getUIManager().markAllWidgetsDirty();} });

    uiMgr.registerClickEvent("StatusBar.Save", [renderer]() {
        auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
        if (lvl) { lvl->setEditorCameraPosition(renderer->getCameraPosition()); lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees()); lvl->setHasEditorCamera(true); }
        auto& am = AssetManager::Instance(); if (am.getUnsavedAssetCount()==0) { renderer->getUIManager().showToastMessage("Nothing to save.",UIManager::kToastShort); return; }
        renderer->getUIManager().showUnsavedChangesDialog(nullptr);
    });

    uiMgr.registerClickEvent("StatusBar.Notifications", [renderer]() { renderer->getUIManager().openNotificationHistoryPopup(); });

    uiMgr.setOnLevelLoadRequested([renderer](const std::string& levelRelPath) {
        auto& diag = DiagnosticsManager::Instance();
        if (diag.isPIEActive()) { renderer->getUIManager().showToastMessage("Cannot switch levels during Play-In-Editor.",UIManager::kToastMedium); return; }
        const std::string levelName = std::filesystem::path(levelRelPath).stem().string();
        if (auto* lvl = diag.getActiveLevelSoft()) { lvl->setEditorCameraPosition(renderer->getCameraPosition()); lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees()); lvl->setHasEditorCamera(true); }
        auto doLoad = [renderer, levelRelPath, levelName]() {
            auto& diag = DiagnosticsManager::Instance(); auto& assetMgr = AssetManager::Instance(); auto& uiMgr = renderer->getUIManager();
            renderer->setRenderFrozen(true); uiMgr.showLevelLoadProgress(levelName); UndoRedoManager::Instance().clear();
            uiMgr.selectEntity(0); renderer->setSelectedEntity(0); diag.setScenePrepared(false);
            uiMgr.updateLevelLoadProgress("Loading level asset...");
            const std::string absPath = assetMgr.getAbsoluteContentPath(levelRelPath);
            if (absPath.empty()) { uiMgr.closeLevelLoadProgress(); renderer->setRenderFrozen(false); uiMgr.showToastMessage("Failed to load level: path not found.",UIManager::kToastMedium); return; }
            auto loadResult = assetMgr.loadLevelAsset(absPath);
            if (!loadResult.success) { uiMgr.closeLevelLoadProgress(); renderer->setRenderFrozen(false); uiMgr.showToastMessage("Failed to load level: "+loadResult.errorMessage,UIManager::kToastLong); return; }
            uiMgr.updateLevelLoadProgress("Restoring editor state...");
            if (auto* newLevel = diag.getActiveLevelSoft()) { renderer->setSkyboxPath(newLevel->getSkyboxPath()); if (newLevel->hasEditorCamera()) { renderer->setCameraPosition(newLevel->getEditorCameraPosition()); const auto& rot = newLevel->getEditorCameraRotation(); renderer->setCameraRotationDegrees(rot.x, rot.y); } }
            uiMgr.updateLevelLoadProgress("Preparing scene..."); renderer->setRenderFrozen(false);
            uiMgr.closeLevelLoadProgress(); uiMgr.refreshWorldOutliner(); uiMgr.refreshContentBrowser(); uiMgr.refreshStatusBar(); uiMgr.markAllWidgetsDirty();
            uiMgr.showToastMessage("Loaded: "+levelName,UIManager::kToastMedium);
        };
        renderer->getUIManager().showUnsavedChangesDialog(std::move(doLoad));
    });
}

// Drag & Drop Handlers
void EditorApp::registerDragDropHandlers()
{
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return;
    auto& uiMgr = m_bridge.getUIManager();

    uiMgr.setOnDropOnViewport([renderer](const std::string& payload, const Vec2& screenPos) {
        const auto sep = payload.find('|'); if (sep==std::string::npos) return;
        const int typeInt = std::atoi(payload.substr(0,sep).c_str());
        const std::string relPath = payload.substr(sep+1);
        const AssetType assetType = static_cast<AssetType>(typeInt);
        const std::string assetName = std::filesystem::path(relPath).stem().string();
        auto& ecs = ECS::ECSManager::Instance(); auto& diagnostics = DiagnosticsManager::Instance();
        const unsigned int targetEntity = renderer->pickEntityAtImmediate(static_cast<int>(screenPos.x),static_cast<int>(screenPos.y));
        const auto target = static_cast<ECS::Entity>(targetEntity);

        if (assetType == AssetType::Skybox) { renderer->setSkyboxPath(relPath); auto* level=diagnostics.getActiveLevelSoft(); if(level) level->setSkyboxPath(relPath); renderer->getUIManager().refreshStatusBar(); renderer->getUIManager().showToastMessage("Skybox: "+assetName,UIManager::kToastMedium); return; }

        if (assetType == AssetType::Prefab) {
            Vec3 sp{0,0,0}; if (!renderer->screenToWorldPos((int)screenPos.x,(int)screenPos.y,sp)){const Vec3 cp=renderer->getCameraPosition();const Vec2 cr=renderer->getCameraRotationDegrees();float y=cr.x*3.14159265f/180.f,p=cr.y*3.14159265f/180.f;sp.x=cp.x+cosf(y)*cosf(p)*5.f;sp.y=cp.y+sinf(p)*5.f;sp.z=cp.z+sinf(y)*cosf(p)*5.f;}
            renderer->getUIManager().spawnPrefabAtPosition(relPath,sp); return; }

        if (assetType == AssetType::Entity) {
            Vec3 sp{0,0,0}; if (!renderer->screenToWorldPos((int)screenPos.x,(int)screenPos.y,sp)){const Vec3 cp=renderer->getCameraPosition();const Vec2 cr=renderer->getCameraRotationDegrees();float y=cr.x*3.14159265f/180.f,p=cr.y*3.14159265f/180.f;sp.x=cp.x+cosf(y)*cosf(p)*5.f;sp.y=cp.y+sinf(p)*5.f;sp.z=cp.z+sinf(y)*cosf(p)*5.f;}
            renderer->getUIManager().spawnEntityAssetAtPosition(relPath,sp); return; }

        if (assetType != AssetType::Model3D) {
            if (targetEntity!=0) {
                switch(assetType){
                case AssetType::Material: case AssetType::Texture: {ECS::MaterialComponent mc{};mc.materialAssetPath=relPath;if(ecs.hasComponent<ECS::MaterialComponent>(target))ecs.setComponent<ECS::MaterialComponent>(target,mc);else ecs.addComponent<ECS::MaterialComponent>(target,mc);break;}
                case AssetType::Script: {ECS::LogicComponent sc{};sc.scriptPath=relPath;if(ecs.hasComponent<ECS::LogicComponent>(target))ecs.setComponent<ECS::LogicComponent>(target,sc);else ecs.addComponent<ECS::LogicComponent>(target,sc);break;}
                default:break;}
                diagnostics.invalidateEntity(targetEntity);auto*level=diagnostics.getActiveLevelSoft();if(level)level->setIsSaved(false);
                renderer->getUIManager().selectEntity(targetEntity);renderer->getUIManager().showToastMessage("Applied "+assetName+" \xe2\x86\x92 Entity "+std::to_string(targetEntity),UIManager::kToastMedium);
            } else { renderer->getUIManager().showToastMessage("No entity under cursor to apply "+assetName,UIManager::kToastMedium); }
            return; }

        Vec3 spawnPos{0,0,0}; if(!renderer->screenToWorldPos((int)screenPos.x,(int)screenPos.y,spawnPos)){const Vec3 cp=renderer->getCameraPosition();const Vec2 cr=renderer->getCameraRotationDegrees();float y=cr.x*3.14159265f/180.f,p=cr.y*3.14159265f/180.f;spawnPos.x=cp.x+cosf(y)*cosf(p)*5.f;spawnPos.y=cp.y+sinf(p)*5.f;spawnPos.z=cp.z+sinf(y)*cosf(p)*5.f;}

        const ECS::Entity entity = ecs.createEntity();
        ECS::TransformComponent transform{};transform.position[0]=spawnPos.x;transform.position[1]=spawnPos.y;transform.position[2]=spawnPos.z;ecs.addComponent<ECS::TransformComponent>(entity,transform);
        ECS::NameComponent nameComp;nameComp.displayName=assetName;ecs.addComponent<ECS::NameComponent>(entity,nameComp);
        ECS::MeshComponent mesh;mesh.meshAssetPath=relPath;ecs.addComponent<ECS::MeshComponent>(entity,mesh);

        {auto meshAsset=AssetManager::Instance().getLoadedAssetByPath(relPath);if(!meshAsset){int id=AssetManager::Instance().loadAsset(relPath,AssetType::Model3D);if(id>0)meshAsset=AssetManager::Instance().getLoadedAssetByID((unsigned int)id);}
         if(meshAsset){auto&ad=meshAsset->getData();if(ad.contains("m_materialAssetPaths")&&ad["m_materialAssetPaths"].is_array()&&!ad["m_materialAssetPaths"].empty()){std::string mp=ad["m_materialAssetPaths"][0].get<std::string>();if(!mp.empty()){ECS::MaterialComponent mc{};mc.materialAssetPath=mp;ecs.addComponent<ECS::MaterialComponent>(entity,mc);}}}}

        auto*level=diagnostics.getActiveLevelSoft();if(level)level->onEntityAdded(entity);
        renderer->getUIManager().refreshWorldOutliner();renderer->getUIManager().selectEntity((unsigned int)entity);renderer->getUIManager().showToastMessage("Spawned: "+assetName,UIManager::kToastMedium);

        auto spT=ecs.hasComponent<ECS::TransformComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::TransformComponent>(entity)):std::nullopt;
        auto spN=ecs.hasComponent<ECS::NameComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::NameComponent>(entity)):std::nullopt;
        auto spM=ecs.hasComponent<ECS::MeshComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::MeshComponent>(entity)):std::nullopt;
        auto spMa=ecs.hasComponent<ECS::MaterialComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::MaterialComponent>(entity)):std::nullopt;
        auto spL=ecs.hasComponent<ECS::LightComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::LightComponent>(entity)):std::nullopt;
        auto spC=ecs.hasComponent<ECS::CameraComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::CameraComponent>(entity)):std::nullopt;
        auto spP=ecs.hasComponent<ECS::PhysicsComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::PhysicsComponent>(entity)):std::nullopt;
        auto spS=ecs.hasComponent<ECS::LogicComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::LogicComponent>(entity)):std::nullopt;
        auto spCo=ecs.hasComponent<ECS::CollisionComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::CollisionComponent>(entity)):std::nullopt;
        auto spH=ecs.hasComponent<ECS::HeightFieldComponent>(entity)?std::make_optional(*ecs.getComponent<ECS::HeightFieldComponent>(entity)):std::nullopt;

        UndoRedoManager::Command spawnCmd;spawnCmd.description="Spawn "+assetName;
        spawnCmd.execute=[entity,spT,spN,spM,spMa,spL,spC,spP,spS,spCo,spH](){auto&e=ECS::ECSManager::Instance();e.createEntity(entity);
            if(spT)e.addComponent<ECS::TransformComponent>(entity,*spT);if(spN)e.addComponent<ECS::NameComponent>(entity,*spN);
            if(spM)e.addComponent<ECS::MeshComponent>(entity,*spM);if(spMa)e.addComponent<ECS::MaterialComponent>(entity,*spMa);
            if(spL)e.addComponent<ECS::LightComponent>(entity,*spL);if(spC)e.addComponent<ECS::CameraComponent>(entity,*spC);
            if(spP)e.addComponent<ECS::PhysicsComponent>(entity,*spP);if(spS)e.addComponent<ECS::LogicComponent>(entity,*spS);
            if(spCo)e.addComponent<ECS::CollisionComponent>(entity,*spCo);if(spH)e.addComponent<ECS::HeightFieldComponent>(entity,*spH);
            auto*lvl=DiagnosticsManager::Instance().getActiveLevelSoft();if(lvl)lvl->onEntityAdded(entity);};
        spawnCmd.undo=[entity](){auto&e=ECS::ECSManager::Instance();auto*lvl=DiagnosticsManager::Instance().getActiveLevelSoft();if(lvl)lvl->onEntityRemoved(entity);e.removeEntity(entity);};
        UndoRedoManager::Instance().pushCommand(std::move(spawnCmd));
    });

    uiMgr.setOnDropOnFolder([renderer](const std::string& payload, const std::string& folderPath) {
        const auto sep=payload.find('|');if(sep==std::string::npos)return;
        const std::string relPath=payload.substr(sep+1);const std::string fileName=std::filesystem::path(relPath).filename().string();
        const std::string newRelPath=folderPath.empty()?fileName:(folderPath+"/"+fileName);
        if(relPath==newRelPath)return;
        if(!AssetManager::Instance().moveAsset(relPath,newRelPath)){Logger::Instance().log(Logger::Category::AssetManagement,"Failed to move asset: "+relPath,Logger::LogLevel::ERROR);return;}
        DiagnosticsManager::Instance().setScenePrepared(false);renderer->getUIManager().refreshContentBrowser();renderer->getUIManager().showToastMessage("Moved: "+fileName,UIManager::kToastMedium);
    });

    uiMgr.setOnDropOnEntity([renderer](const std::string& payload, unsigned int entityId) {
        const auto sep=payload.find('|');if(sep==std::string::npos)return;
        const int typeInt=std::atoi(payload.substr(0,sep).c_str());const std::string relPath=payload.substr(sep+1);
        const AssetType assetType=static_cast<AssetType>(typeInt);const std::string assetName=std::filesystem::path(relPath).stem().string();
        auto&ecs=ECS::ECSManager::Instance();const auto entity=static_cast<ECS::Entity>(entityId);
        switch(assetType){
        case AssetType::Material:case AssetType::Texture:{ECS::MaterialComponent mc{};mc.materialAssetPath=relPath;if(ecs.hasComponent<ECS::MaterialComponent>(entity))ecs.setComponent<ECS::MaterialComponent>(entity,mc);else ecs.addComponent<ECS::MaterialComponent>(entity,mc);break;}
        case AssetType::Model3D:{ECS::MeshComponent meshComp{};meshComp.meshAssetPath=relPath;if(ecs.hasComponent<ECS::MeshComponent>(entity))ecs.setComponent<ECS::MeshComponent>(entity,meshComp);else ecs.addComponent<ECS::MeshComponent>(entity,meshComp);
            {auto meshAsset=AssetManager::Instance().getLoadedAssetByPath(relPath);if(!meshAsset){int id=AssetManager::Instance().loadAsset(relPath,AssetType::Model3D);if(id>0)meshAsset=AssetManager::Instance().getLoadedAssetByID((unsigned int)id);}
             if(meshAsset){auto&ad=meshAsset->getData();if(ad.contains("m_materialAssetPaths")&&ad["m_materialAssetPaths"].is_array()&&!ad["m_materialAssetPaths"].empty()){std::string mp=ad["m_materialAssetPaths"][0].get<std::string>();if(!mp.empty()){ECS::MaterialComponent mc{};mc.materialAssetPath=mp;if(ecs.hasComponent<ECS::MaterialComponent>(entity))ecs.setComponent<ECS::MaterialComponent>(entity,mc);else ecs.addComponent<ECS::MaterialComponent>(entity,mc);}}}}
            break;}
        case AssetType::Script:{ECS::LogicComponent sc{};sc.scriptPath=relPath;if(ecs.hasComponent<ECS::LogicComponent>(entity))ecs.setComponent<ECS::LogicComponent>(entity,sc);else ecs.addComponent<ECS::LogicComponent>(entity,sc);break;}
        default:break;}
        DiagnosticsManager::Instance().invalidateEntity(entityId);auto*level=DiagnosticsManager::Instance().getActiveLevelSoft();if(level)level->setIsSaved(false);
        renderer->getUIManager().showToastMessage("Applied "+assetName+" \xe2\x86\x92 Entity "+std::to_string(entityId),UIManager::kToastMedium);
    });
}

// Shortcut Registration
void EditorApp::registerShortcuts()
{
    auto& sm = ShortcutManager::Instance();
    using Phase = ShortcutManager::Phase;
    using Mod = ShortcutManager::Mod;

    // --- Editor-only KeyDown shortcuts (Ctrl+combos) ---

    sm.registerAction("Editor.Undo", "Undo", "Editor",
        { SDLK_Z, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            if (!m_bridge.canUndo()) return false;
            m_bridge.undo();
            m_bridge.getUIManager().markAllWidgetsDirty();
            return true;
        });

    sm.registerAction("Editor.Redo", "Redo", "Editor",
        { SDLK_Y, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            if (!m_bridge.canRedo()) return false;
            m_bridge.redo();
            m_bridge.getUIManager().markAllWidgetsDirty();
            return true;
        });

    sm.registerAction("Editor.Save", "Save", "Editor",
        { SDLK_S, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.captureEditorCameraToLevel();
            if (m_bridge.getUnsavedAssetCount() > 0)
                m_bridge.getUIManager().showUnsavedChangesDialog(nullptr);
            else
                m_bridge.getUIManager().showToastMessage("Nothing to save.", UIManager::kToastShort);
            return true;
        });

    sm.registerAction("Editor.SearchContentBrowser", "Search Content Browser", "Editor",
        { SDLK_F, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.getUIManager().focusContentBrowserSearch();
            return true;
        });

    sm.registerAction("Editor.CopyEntity", "Copy Entity", "Editor",
        { SDLK_C, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.getUIManager().copySelectedEntity();
            return true;
        });

    sm.registerAction("Editor.PasteEntity", "Paste Entity", "Editor",
        { SDLK_V, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.getUIManager().pasteEntity();
            return true;
        });

    sm.registerAction("Editor.DuplicateEntity", "Duplicate Entity", "Editor",
        { SDLK_D, Mod::Ctrl }, Phase::KeyDown,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.getUIManager().duplicateSelectedEntity();
            return true;
        });

    // --- Editor-only Debug visualization shortcuts ---

    sm.registerAction("Editor.ToggleUIDebug", "Toggle UI Debug", "Debug",
        { SDLK_F11, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer) return false;
            renderer->toggleUIDebug();
            Logger::Instance().log(Logger::Category::Input,
                std::string("UI debug bounds: ") + (renderer->isUIDebugEnabled() ? "ON" : "OFF"),
                Logger::LogLevel::INFO);
            return true;
        });

    sm.registerAction("Editor.ToggleBoundsDebug", "Toggle Bounds Debug", "Debug",
        { SDLK_F8, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer) return false;
            renderer->toggleBoundsDebug();
            Logger::Instance().log(Logger::Category::Input,
                std::string("Bounds debug boxes: ") + (renderer->isBoundsDebugEnabled() ? "ON" : "OFF"),
                Logger::LogLevel::INFO);
            return true;
        });

    // --- PIE shortcuts ---

    sm.registerAction("PIE.Stop", "Stop PIE", "PIE",
        { SDLK_ESCAPE, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            if (!m_bridge.isPIEActive()) return false;
            stopPIE();
            return true;
        });

    sm.registerAction("PIE.ToggleInput", "Toggle PIE Input", "PIE",
        { SDLK_F1, Mod::Shift }, Phase::KeyUp,
        [this]() -> bool {
            if (!m_bridge.isPIEActive()) return false;
            if (m_pieMouseCaptured && !m_pieInputPaused)
            {
                m_pieInputPaused = true;
                if (auto* w = m_bridge.getWindow())
                {
                    SDL_SetWindowRelativeMouseMode(w, false);
                    SDL_SetWindowMouseGrab(w, false);
                    SDL_WarpMouseInWindow(w, m_preCaptureMouseX, m_preCaptureMouseY);
                }
                SDL_ShowCursor();
                Logger::Instance().log(Logger::Category::Input, "PIE: input paused (Shift+F1), mouse released.", Logger::LogLevel::INFO);
            }
            return true;
        });

    // --- Gizmo shortcuts ---

    sm.registerAction("Gizmo.Translate", "Translate Mode", "Gizmo",
        { SDLK_W, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_rightMouseDown || m_bridge.getUIManager().hasEntryFocused()) return false;
            renderer->setGizmoMode(Renderer::GizmoMode::Translate);
            return true;
        });

    sm.registerAction("Gizmo.Rotate", "Rotate Mode", "Gizmo",
        { SDLK_E, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_rightMouseDown || m_bridge.getUIManager().hasEntryFocused()) return false;
            renderer->setGizmoMode(Renderer::GizmoMode::Rotate);
            return true;
        });

    sm.registerAction("Gizmo.Scale", "Scale Mode", "Gizmo",
        { SDLK_R, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_rightMouseDown || m_bridge.getUIManager().hasEntryFocused()) return false;
            renderer->setGizmoMode(Renderer::GizmoMode::Scale);
            return true;
        });

    // --- Editor-only KeyUp shortcuts ---

    sm.registerAction("Editor.FocusSelected", "Focus Selected", "Editor",
        { SDLK_F, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_rightMouseDown || m_bridge.getUIManager().hasEntryFocused()) return false;
            renderer->focusOnSelectedEntity();
            return true;
        });

    sm.registerAction("Editor.DropToSurface", "Drop to Surface", "Editor",
        { SDLK_END, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.isPIEActive() || m_rightMouseDown || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.getUIManager().dropSelectedEntitiesToSurface([](float ox, float oy, float oz) -> std::pair<bool, float>
            {
                auto hit = PhysicsWorld::Instance().raycast(ox, oy, oz, 0.0f, -1.0f, 0.0f, 10000.0f);
                return { hit.hit, hit.point[1] };
            });
            return true;
        });

    sm.registerAction("Editor.ShortcutHelp", "Shortcut Help", "Editor",
        { SDLK_F1, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (!renderer || m_bridge.getUIManager().hasEntryFocused()) return false;
            m_bridge.getUIManager().openShortcutHelpPopup();
            return true;
        });

    sm.registerAction("Editor.ImportDialog", "Open Import Dialog", "Editor",
        { SDLK_F2, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            Renderer* renderer = m_bridge.getRenderer();
            if (renderer && m_bridge.getUIManager().hasEntryFocused()) return false;
            Logger::Instance().log(Logger::Category::Input, "F2 pressed - opening import dialog.", Logger::LogLevel::INFO);
            m_bridge.importAsset(m_bridge.getWindow());
            return true;
        });

    sm.registerAction("Editor.Delete", "Delete", "Editor",
        { SDLK_DELETE, Mod::None }, Phase::KeyUp,
        [this]() -> bool {
            return handleDelete();
        });

    Logger::Instance().log(Logger::Category::Engine,
        "EditorApp: registered editor shortcuts with ShortcutManager.", Logger::LogLevel::INFO);
}

// Handle Delete shortcut (asset or entity deletion with undo support)
bool EditorApp::handleDelete()
{
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return false;
    auto& uiMgr = m_bridge.getUIManager();
    if (uiMgr.hasEntryFocused()) return false;

    if (uiMgr.tryDeleteWidgetEditorElement())
        return true;

    const std::string selectedAsset = uiMgr.getSelectedGridAsset();
    if (!selectedAsset.empty())
    {
        const auto refs = AssetManager::Instance().findReferencesTo(selectedAsset);
        std::string msg = "Are you sure you want to delete this asset?\nThis cannot be undone.";
        if (!refs.empty())
        {
            msg = "This asset is referenced by " + std::to_string(refs.size())
                + " other asset(s)/entity(ies).\nDeleting it may break those references.\n\nDelete anyway?";
        }
        uiMgr.showConfirmDialog(
            msg,
            [renderer, selectedAsset]()
            {
                auto& assetMgr = AssetManager::Instance();
                auto& ui = renderer->getUIManager();
                if (assetMgr.deleteAsset(selectedAsset, true))
                {
                    const std::string name = std::filesystem::path(selectedAsset).stem().string();
                    ui.clearSelectedGridAsset();
                    ui.refreshContentBrowser();
                    ui.showToastMessage("Deleted: " + name, UIManager::kToastMedium);
                }
                else
                {
                    ui.showToastMessage("Failed to delete asset.", UIManager::kToastMedium);
                }
            });
        return true;
    }

    const auto& selectedEntities = renderer->getSelectedEntities();
    if (selectedEntities.empty()) return false;

    auto& ecs = ECS::ECSManager::Instance();
    auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();

    struct EntitySnapshot
    {
        ECS::Entity entity;
        std::string name;
        std::optional<ECS::TransformComponent>       transform;
        std::optional<ECS::NameComponent>            nameComp;
        std::optional<ECS::MeshComponent>            mesh;
        std::optional<ECS::MaterialComponent>        material;
        std::optional<ECS::LightComponent>           light;
        std::optional<ECS::CameraComponent>          camera;
        std::optional<ECS::PhysicsComponent>         physics;
        std::optional<ECS::LogicComponent>           logic;
        std::optional<ECS::CollisionComponent>       collision;
        std::optional<ECS::HeightFieldComponent>     heightField;
    };

    std::vector<EntitySnapshot> snapshots;
    snapshots.reserve(selectedEntities.size());

    for (unsigned int sel : selectedEntities)
    {
        const auto entity = static_cast<ECS::Entity>(sel);
        EntitySnapshot snap;
        snap.entity = entity;
        snap.name = "Entity " + std::to_string(sel);
        if (const auto* nc = ecs.getComponent<ECS::NameComponent>(entity))
        {
            if (!nc->displayName.empty())
                snap.name = nc->displayName;
        }
        snap.transform   = ecs.hasComponent<ECS::TransformComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::TransformComponent>(entity))   : std::nullopt;
        snap.nameComp    = ecs.hasComponent<ECS::NameComponent>(entity)        ? std::make_optional(*ecs.getComponent<ECS::NameComponent>(entity))        : std::nullopt;
        snap.mesh        = ecs.hasComponent<ECS::MeshComponent>(entity)        ? std::make_optional(*ecs.getComponent<ECS::MeshComponent>(entity))        : std::nullopt;
        snap.material    = ecs.hasComponent<ECS::MaterialComponent>(entity)    ? std::make_optional(*ecs.getComponent<ECS::MaterialComponent>(entity))    : std::nullopt;
        snap.light       = ecs.hasComponent<ECS::LightComponent>(entity)       ? std::make_optional(*ecs.getComponent<ECS::LightComponent>(entity))       : std::nullopt;
        snap.camera      = ecs.hasComponent<ECS::CameraComponent>(entity)      ? std::make_optional(*ecs.getComponent<ECS::CameraComponent>(entity))      : std::nullopt;
        snap.physics     = ecs.hasComponent<ECS::PhysicsComponent>(entity)     ? std::make_optional(*ecs.getComponent<ECS::PhysicsComponent>(entity))     : std::nullopt;
        snap.logic       = ecs.hasComponent<ECS::LogicComponent>(entity)       ? std::make_optional(*ecs.getComponent<ECS::LogicComponent>(entity))       : std::nullopt;
        snap.collision   = ecs.hasComponent<ECS::CollisionComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::CollisionComponent>(entity))   : std::nullopt;
        snap.heightField = ecs.hasComponent<ECS::HeightFieldComponent>(entity) ? std::make_optional(*ecs.getComponent<ECS::HeightFieldComponent>(entity)) : std::nullopt;
        snapshots.push_back(std::move(snap));
    }

    for (const auto& snap : snapshots)
    {
        if (level) level->onEntityRemoved(snap.entity);
        ecs.removeEntity(snap.entity);
    }

    uiMgr.selectEntity(0);
    renderer->clearSelection();
    uiMgr.refreshWorldOutliner();

    std::string toastMsg = (snapshots.size() == 1)
        ? ("Deleted: " + snapshots[0].name)
        : ("Deleted " + std::to_string(snapshots.size()) + " entities");
    uiMgr.showToastMessage(toastMsg, UIManager::kToastMedium);

    Logger::Instance().log(Logger::Category::Engine,
        "Deleted " + std::to_string(snapshots.size()) + " entity(ies)",
        Logger::LogLevel::INFO);

    UndoRedoManager::Command cmd;
    cmd.description = (snapshots.size() == 1) ? ("Delete " + snapshots[0].name) : ("Delete " + std::to_string(snapshots.size()) + " entities");
    cmd.execute = [snapshots]()
        {
            auto& e = ECS::ECSManager::Instance();
            auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
            for (const auto& snap : snapshots)
            {
                if (std::find(e.getEntitiesMatchingSchema(ECS::Schema()).begin(),
                              e.getEntitiesMatchingSchema(ECS::Schema()).end(), snap.entity) != e.getEntitiesMatchingSchema(ECS::Schema()).end())
                {
                    if (lvl) lvl->onEntityRemoved(snap.entity);
                    e.removeEntity(snap.entity);
                }
            }
        };
    cmd.undo = [snapshots]()
        {
            auto& e = ECS::ECSManager::Instance();
            auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
            for (const auto& snap : snapshots)
            {
                e.createEntity(snap.entity);
                if (snap.transform)   e.addComponent<ECS::TransformComponent>(snap.entity, *snap.transform);
                if (snap.nameComp)    e.addComponent<ECS::NameComponent>(snap.entity, *snap.nameComp);
                if (snap.mesh)        e.addComponent<ECS::MeshComponent>(snap.entity, *snap.mesh);
                if (snap.material)    e.addComponent<ECS::MaterialComponent>(snap.entity, *snap.material);
                if (snap.light)       e.addComponent<ECS::LightComponent>(snap.entity, *snap.light);
                if (snap.camera)      e.addComponent<ECS::CameraComponent>(snap.entity, *snap.camera);
                if (snap.physics)     e.addComponent<ECS::PhysicsComponent>(snap.entity, *snap.physics);
                if (snap.logic)       e.addComponent<ECS::LogicComponent>(snap.entity, *snap.logic);
                if (snap.collision)   e.addComponent<ECS::CollisionComponent>(snap.entity, *snap.collision);
                if (snap.heightField) e.addComponent<ECS::HeightFieldComponent>(snap.entity, *snap.heightField);
                if (lvl) lvl->onEntityAdded(snap.entity);
            }
        };
    UndoRedoManager::Instance().pushCommand(std::move(cmd));
    return true;
}

// Content Browser Context Menu (Phase 7)
bool EditorApp::handleContentBrowserContextMenu(const Vec2& mousePos)
{
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return false;
    auto& uiManager = m_bridge.getUIManager();

    if (!uiManager.isOverContentBrowserGrid(mousePos))
        return false;

    if (uiManager.isDropdownMenuOpen())
        uiManager.closeDropdownMenu();

    std::vector<UIManager::DropdownMenuItem> items;

    items.push_back({ "New Folder", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            auto& diagnostics = DiagnosticsManager::Instance();
            const std::filesystem::path contentDir =
                std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
            const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            std::string baseName = "NewFolder";
            std::string folderName = baseName;
            int counter = 1;
            while (std::filesystem::exists(targetDir / folderName))
                folderName = baseName + std::to_string(counter++);

            std::filesystem::create_directory(targetDir / folderName, ec);
            if (!ec)
            {
                uiMgr.refreshContentBrowser();
                uiMgr.showToastMessage("Created folder: " + folderName, UIManager::kToastMedium);
            }
            else
            {
                uiMgr.showToastMessage("Failed to create folder.", UIManager::kToastMedium);
            }
        }});

    items.push_back({ "", {}, true });

    items.push_back({ "New Script", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            auto& diagnostics = DiagnosticsManager::Instance();
            auto& assetMgr = AssetManager::Instance();
            const std::filesystem::path contentDir =
                std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
            const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            std::string baseName = "NewScript";
            std::string fileName = baseName + ".py";
            int counter = 1;
            while (std::filesystem::exists(targetDir / fileName))
                fileName = baseName + std::to_string(counter++) + ".py";

            const std::filesystem::path filePath = targetDir / fileName;
            std::ofstream out(filePath, std::ios::out | std::ios::trunc);
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
                out.close();

                const std::string relPath = std::filesystem::relative(filePath, contentDir).generic_string();
                AssetRegistryEntry entry;
                entry.name = std::filesystem::path(fileName).stem().string();
                entry.path = relPath;
                entry.type = AssetType::Script;
                assetMgr.registerAssetInRegistry(entry);
                uiMgr.refreshContentBrowser();
                uiMgr.showToastMessage("Created: " + fileName, UIManager::kToastMedium);
            }
        }});

    items.push_back({ "New C++ Script", [this, renderer]()
        {
            auto& uiMgr = renderer->getUIManager();

            auto& diagnostics = DiagnosticsManager::Instance();
            const std::string projectPath = diagnostics.getProjectInfo().projectPath;
            const std::filesystem::path contentDir =
                std::filesystem::path(projectPath) / "Content";
            const std::filesystem::path nativeDir = contentDir / "Scripts" / "Native";
            std::error_code ec;
            std::filesystem::create_directories(nativeDir, ec);

            std::string baseName = "NewScript";
            std::string className = baseName;
            int counter = 1;
            while (std::filesystem::exists(nativeDir / (className + ".h")))
                className = baseName + std::to_string(counter++);

            // Generate header file
            {
                std::ofstream hdr(nativeDir / (className + ".h"), std::ios::out | std::ios::trunc);
                if (hdr.is_open())
                {
                    hdr << "#pragma once\n";
                    hdr << "#include \"GameplayAPI.h\"\n\n";
                    hdr << "class " << className << " : public INativeScript\n";
                    hdr << "{\n";
                    hdr << "public:\n";
                    hdr << "    void onLoaded() override;\n";
                    hdr << "    void tick(float dt) override;\n";
                    hdr << "};\n";
                    hdr.close();
                }
            }

            // Generate source file
            {
                std::ofstream src(nativeDir / (className + ".cpp"), std::ios::out | std::ios::trunc);
                if (src.is_open())
                {
                    src << "#include \"" << className << ".h\"\n\n";
                    src << "void " << className << "::onLoaded()\n";
                    src << "{\n";
                    src << "}\n\n";
                    src << "void " << className << "::tick(float dt)\n";
                    src << "{\n";
                    src << "}\n";
                    src.close();
                }
            }

            uiMgr.refreshContentBrowser();
            uiMgr.showToastMessage("Created C++ script: " + className + " (.h + .cpp)", UIManager::kToastMedium);
            generateVSCodeConfig(projectPath);
            Logger::Instance().log(Logger::Category::Engine,
                "Created C++ script: " + className + " in Content/Scripts/Native/",
                Logger::LogLevel::INFO);
        }});

    items.push_back({ "", {}, true });

    items.push_back({ "New Level", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            constexpr float kBaseW = 360.0f;
            constexpr float kBaseH = 240.0f;
            const int kPopupW = static_cast<int>(EditorTheme::Scaled(kBaseW));
            const int kPopupH = static_cast<int>(EditorTheme::Scaled(kBaseH));
            PopupWindow* popup = renderer->openPopupWindow(
                "NewLevel", "New Level", kPopupW, kPopupH);
            if (!popup) return;
            if (!popup->uiManager().getRegisteredWidgets().empty()) return;

            constexpr float W = kBaseW;
            constexpr float H = kBaseH;
            auto nx = [&](float px) { return px / W; };
            auto ny = [&](float py) { return py / H; };

            struct LevelState
            {
                std::string name = "NewLevel";
                std::string folder;
                int templateIndex = 0;
            };
            auto state = std::make_shared<LevelState>();
            state->folder = folder;

            std::vector<WidgetElement> elements;

            {
                WidgetElement bg;
                bg.type = WidgetElementType::Panel;
                bg.id = "NL.Bg";
                bg.from = Vec2{ 0.0f, 0.0f };
                bg.to = Vec2{ 1.0f, 1.0f };
                bg.style.color = EditorTheme::Get().panelBackground;
                elements.push_back(bg);
            }

            {
                WidgetElement title;
                title.type = WidgetElementType::Text;
                title.id = "NL.Title";
                title.from = Vec2{ nx(8.0f), 0.0f };
                title.to = Vec2{ 1.0f, ny(36.0f) };
                title.text = "New Level";
                title.fontSize = EditorTheme::Get().fontSizeHeading;
                title.style.textColor = EditorTheme::Get().titleBarText;
                title.textAlignV = TextAlignV::Center;
                title.padding = EditorTheme::Scaled(Vec2{ 6.0f, 0.0f });
                elements.push_back(title);
            }

            WidgetElement formStack;
            formStack.type = WidgetElementType::StackPanel;
            formStack.id = "NL.Form";
            formStack.from = Vec2{ nx(16.0f), ny(44.0f) };
            formStack.to = Vec2{ nx(W - 16.0f), ny(H - 50.0f) };
            formStack.padding = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
            formStack.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };

            {
                WidgetElement lbl;
                lbl.type = WidgetElementType::Text;
                lbl.id = "NL.NameLbl";
                lbl.text = "Name";
                lbl.fontSize = EditorTheme::Get().fontSizeBody;
                lbl.style.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                lbl.fillX = true;
                lbl.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
                lbl.runtimeOnly = true;
                formStack.children.push_back(std::move(lbl));
            }

            {
                WidgetElement entry;
                entry.type = WidgetElementType::EntryBar;
                entry.id = "NL.Name";
                entry.value = state->name;
                entry.fontSize = EditorTheme::Get().fontSizeBody;
                entry.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                entry.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                entry.fillX = true;
                entry.minSize = Vec2{ 0.0f, EditorTheme::Scaled(24.0f) };
                entry.padding = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
                entry.hitTestMode = HitTestMode::Enabled;
                entry.runtimeOnly = true;
                entry.onValueChanged = [state](const std::string& v) { state->name = v; };
                formStack.children.push_back(std::move(entry));
            }

            {
                WidgetElement typeLbl;
                typeLbl.type = WidgetElementType::Text;
                typeLbl.id = "NL.TypeLbl";
                typeLbl.text = "Template";
                typeLbl.fontSize = EditorTheme::Get().fontSizeBody;
                typeLbl.style.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                typeLbl.fillX = true;
                typeLbl.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
                typeLbl.runtimeOnly = true;
                formStack.children.push_back(std::move(typeLbl));
            }

            {
                WidgetElement dropdown;
                dropdown.type = WidgetElementType::DropdownButton;
                dropdown.id = "NL.Template";
                dropdown.text = "Empty";
                dropdown.items = { "Empty", "Basic Outdoor", "Prototype" };
                dropdown.fontSize = EditorTheme::Get().fontSizeBody;
                dropdown.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                dropdown.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                dropdown.style.hoverColor = Vec4{ 0.18f, 0.18f, 0.24f, 0.95f };
                dropdown.fillX = true;
                dropdown.minSize = Vec2{ 0.0f, EditorTheme::Scaled(24.0f) };
                dropdown.padding = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
                dropdown.hitTestMode = HitTestMode::Enabled;
                dropdown.runtimeOnly = true;
                dropdown.onSelectionChanged = [state](int index) { state->templateIndex = index; };
                formStack.children.push_back(std::move(dropdown));
            }

            elements.push_back(std::move(formStack));

            {
                WidgetElement createBtn;
                createBtn.type = WidgetElementType::Button;
                createBtn.id = "NL.Create";
                createBtn.from = Vec2{ nx(W - 180.0f), ny(H - 44.0f) };
                createBtn.to = Vec2{ nx(W - 100.0f), ny(H - 12.0f) };
                createBtn.text = "Create";
                createBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
                createBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
                createBtn.textAlignH = TextAlignH::Center;
                createBtn.textAlignV = TextAlignV::Center;
                createBtn.style.color = Vec4{ 0.15f, 0.45f, 0.25f, 0.95f };
                createBtn.style.hoverColor = Vec4{ 0.2f, 0.6f, 0.35f, 1.0f };
                createBtn.shaderVertex = "button_vertex.glsl";
                createBtn.shaderFragment = "button_fragment.glsl";
                createBtn.hitTestMode = HitTestMode::Enabled;
                createBtn.onClicked = [state, renderer, popup]()
                {
                    auto& ui = renderer->getUIManager();
                    const std::string levelName = state->name.empty() ? "NewLevel" : state->name;
                    const std::string relFolder = state->folder.empty() ? "Levels" : state->folder;
                    constexpr UIManager::SceneTemplate templates[] = {
                        UIManager::SceneTemplate::Empty,
                        UIManager::SceneTemplate::BasicOutdoor,
                        UIManager::SceneTemplate::Prototype,
                    };
                    const int idx = std::clamp(state->templateIndex, 0, 2);
                    ui.createNewLevelWithTemplate(templates[idx], levelName, relFolder);
                    popup->close();
                };
                elements.push_back(std::move(createBtn));
            }

            {
                WidgetElement cancelBtn;
                cancelBtn.type = WidgetElementType::Button;
                cancelBtn.id = "NL.Cancel";
                cancelBtn.from = Vec2{ nx(W - 90.0f), ny(H - 44.0f) };
                cancelBtn.to = Vec2{ nx(W - 16.0f), ny(H - 12.0f) };
                cancelBtn.text = "Cancel";
                cancelBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
                cancelBtn.style.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
                cancelBtn.textAlignH = TextAlignH::Center;
                cancelBtn.textAlignV = TextAlignV::Center;
                cancelBtn.style.color = Vec4{ 0.25f, 0.25f, 0.3f, 0.95f };
                cancelBtn.style.hoverColor = Vec4{ 0.35f, 0.35f, 0.42f, 1.0f };
                cancelBtn.shaderVertex = "button_vertex.glsl";
                cancelBtn.shaderFragment = "button_fragment.glsl";
                cancelBtn.hitTestMode = HitTestMode::Enabled;
                cancelBtn.onClicked = [popup]() { popup->close(); };
                elements.push_back(std::move(cancelBtn));
            }

            auto widget = std::make_shared<EditorWidget>();
            widget->setName("NewLevel");
            widget->setFillX(true);
            widget->setFillY(true);
            widget->setElements(std::move(elements));
            popup->uiManager().registerWidget("NewLevel", widget);
        }});

    items.push_back({ "New Widget", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            auto& diagnostics = DiagnosticsManager::Instance();
            auto& assetMgr = AssetManager::Instance();
            const std::filesystem::path contentDir =
                std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
            const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            std::string baseName = "NewWidget";
            std::string fileName = baseName + ".asset";
            int counter = 1;
            while (std::filesystem::exists(targetDir / fileName))
                fileName = baseName + std::to_string(counter++) + ".asset";

            const std::string displayName = std::filesystem::path(fileName).stem().string();
            const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();

            auto widgetAsset = std::make_shared<AssetData>();
            widgetAsset->setName(displayName);
            widgetAsset->setAssetType(AssetType::Widget);
            widgetAsset->setType(AssetType::Widget);
            widgetAsset->setPath(relPath);

            auto defaultWidget = std::make_shared<Widget>();
            defaultWidget->setName(displayName);
            defaultWidget->setSizePixels(Vec2{ 640.0f, 360.0f });

            WidgetElement canvas{};
            canvas.id = displayName + ".CanvasPanel";
            canvas.type = WidgetElementType::Panel;
            canvas.from = Vec2{ 0.0f, 0.0f };
            canvas.to = Vec2{ 1.0f, 1.0f };
            canvas.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            canvas.isCanvasRoot = true;

            WidgetElement panel{};
            panel.id = displayName + ".RootPanel";
            panel.type = WidgetElementType::Panel;
            panel.from = Vec2{ 0.0f, 0.0f };
            panel.to = Vec2{ 1.0f, 1.0f };
            panel.style.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.75f };
            panel.hitTestMode = HitTestMode::Enabled;

            WidgetElement label{};
            label.id = displayName + ".Label";
            label.type = WidgetElementType::Text;
            label.from = Vec2{ 0.05f, 0.05f };
            label.to = Vec2{ 0.95f, 0.20f };
            label.text = "New Widget";
            label.font = "default.ttf";
            label.fontSize = 18.0f;
            label.style.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
            panel.children.push_back(std::move(label));
            canvas.children.push_back(std::move(panel));

            std::vector<WidgetElement> elems;
            elems.push_back(std::move(canvas));
            defaultWidget->setElements(std::move(elems));
            widgetAsset->setData(defaultWidget->toJson());

            const unsigned int id = assetMgr.registerLoadedAsset(widgetAsset);
            if (id == 0)
            {
                uiMgr.showToastMessage("Failed to create widget asset.", UIManager::kToastMedium);
                return;
            }

            widgetAsset->setId(id);
            Asset asset;
            asset.ID = id;
            asset.type = AssetType::Widget;
            if (!assetMgr.saveAsset(asset, AssetManager::Sync))
            {
                uiMgr.showToastMessage("Failed to save widget asset.", UIManager::kToastMedium);
                return;
            }

            AssetRegistryEntry entry;
            entry.name = displayName;
            entry.path = relPath;
            entry.type = AssetType::Widget;
            assetMgr.registerAssetInRegistry(entry);

            uiMgr.refreshContentBrowser();
            uiMgr.showToastMessage("Created: " + fileName, UIManager::kToastMedium);
            uiMgr.openWidgetEditorPopup(relPath);
        }});

    items.push_back({ "New Input Action", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            auto& diagnostics = DiagnosticsManager::Instance();
            auto& assetMgr = AssetManager::Instance();
            const std::filesystem::path contentDir =
                std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
            const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            std::string baseName = "NewInputAction";
            std::string fileName = baseName + ".asset";
            int counter = 1;
            while (std::filesystem::exists(targetDir / fileName))
                fileName = baseName + std::to_string(counter++) + ".asset";

            const std::string displayName = std::filesystem::path(fileName).stem().string();
            const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();

            auto actionAsset = std::make_shared<AssetData>();
            actionAsset->setName(displayName);
            actionAsset->setAssetType(AssetType::InputAction);
            actionAsset->setType(AssetType::InputAction);
            actionAsset->setPath(relPath);

            nlohmann::json data = nlohmann::json::object();
            data["shift"] = false;
            data["ctrl"]  = false;
            data["alt"]   = false;
            actionAsset->setData(data);

            const unsigned int id = assetMgr.registerLoadedAsset(actionAsset);
            if (id == 0)
            {
                uiMgr.showToastMessage("Failed to create input action.", UIManager::kToastMedium);
                return;
            }

            actionAsset->setId(id);
            Asset asset;
            asset.ID   = id;
            asset.type = AssetType::InputAction;
            if (!assetMgr.saveAsset(asset, AssetManager::Sync))
            {
                uiMgr.showToastMessage("Failed to save input action.", UIManager::kToastMedium);
                return;
            }

            AssetRegistryEntry entry;
            entry.name = displayName;
            entry.path = relPath;
            entry.type = AssetType::InputAction;
            assetMgr.registerAssetInRegistry(entry);

            uiMgr.refreshContentBrowser();
            uiMgr.showToastMessage("Created: " + fileName, UIManager::kToastMedium);
            uiMgr.openInputActionEditorTab(relPath);
        }});

    items.push_back({ "New Input Mapping", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            auto& diagnostics = DiagnosticsManager::Instance();
            auto& assetMgr = AssetManager::Instance();
            const std::filesystem::path contentDir =
                std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
            const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            std::string baseName = "NewInputMapping";
            std::string fileName = baseName + ".asset";
            int counter = 1;
            while (std::filesystem::exists(targetDir / fileName))
                fileName = baseName + std::to_string(counter++) + ".asset";

            const std::string displayName = std::filesystem::path(fileName).stem().string();
            const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();

            auto mappingAsset = std::make_shared<AssetData>();
            mappingAsset->setName(displayName);
            mappingAsset->setAssetType(AssetType::InputMapping);
            mappingAsset->setType(AssetType::InputMapping);
            mappingAsset->setPath(relPath);

            nlohmann::json data = nlohmann::json::object();
            data["bindings"] = nlohmann::json::array();
            mappingAsset->setData(data);

            const unsigned int id = assetMgr.registerLoadedAsset(mappingAsset);
            if (id == 0)
            {
                uiMgr.showToastMessage("Failed to create input mapping.", UIManager::kToastMedium);
                return;
            }

            mappingAsset->setId(id);
            Asset asset;
            asset.ID   = id;
            asset.type = AssetType::InputMapping;
            if (!assetMgr.saveAsset(asset, AssetManager::Sync))
            {
                uiMgr.showToastMessage("Failed to save input mapping.", UIManager::kToastMedium);
                return;
            }

            AssetRegistryEntry entry;
            entry.name = displayName;
            entry.path = relPath;
            entry.type = AssetType::InputMapping;
            assetMgr.registerAssetInRegistry(entry);

            uiMgr.refreshContentBrowser();
            uiMgr.showToastMessage("Created: " + fileName, UIManager::kToastMedium);
            uiMgr.openInputMappingEditorTab(relPath);
        }});

    items.push_back({ "New Material", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            constexpr float kBaseW = 460.0f;
            constexpr float kBaseH = 400.0f;
            const int kPopupW = static_cast<int>(EditorTheme::Scaled(kBaseW));
            const int kPopupH = static_cast<int>(EditorTheme::Scaled(kBaseH));
            PopupWindow* popup = renderer->openPopupWindow(
                "NewMaterial", "New Material", kPopupW, kPopupH);
            if (!popup) return;
            if (!popup->uiManager().getRegisteredWidgets().empty()) return;

            constexpr float W = kBaseW;
            constexpr float H = kBaseH;
            auto nx = [&](float px) { return px / W; };
            auto ny = [&](float py) { return py / H; };

            struct MaterialState
            {
                std::string name = "NewMaterial";
                std::string vertexShader = "vertex.glsl";
                std::string fragmentShader = "fragment.glsl";
                std::string diffuseTexture;
                std::string specularTexture;
                float metallic = 0.0f;
                float roughness = 0.5f;
                float specularMultiplier = 1.0f;
                std::string folder;
            };
            auto mstate = std::make_shared<MaterialState>();
            mstate->folder = folder;

            std::vector<WidgetElement> elements;

            {
                WidgetElement bg;
                bg.type = WidgetElementType::Panel;
                bg.id = "NM.Bg";
                bg.from = Vec2{ 0.0f, 0.0f };
                bg.to = Vec2{ 1.0f, 1.0f };
                bg.style.color = EditorTheme::Get().panelBackground;
                elements.push_back(bg);
            }

            {
                WidgetElement title;
                title.type = WidgetElementType::Text;
                title.id = "NM.Title";
                title.from = Vec2{ nx(8.0f), 0.0f };
                title.to = Vec2{ 1.0f, ny(36.0f) };
                title.text = "New Material";
                title.fontSize = EditorTheme::Get().fontSizeHeading;
                title.style.textColor = EditorTheme::Get().titleBarText;
                title.textAlignV = TextAlignV::Center;
                title.padding = EditorTheme::Scaled(Vec2{ 6.0f, 0.0f });
                elements.push_back(title);
            }

            WidgetElement formStack;
            formStack.type = WidgetElementType::StackPanel;
            formStack.id = "NM.Form";
            formStack.from = Vec2{ nx(16.0f), ny(44.0f) };
            formStack.to = Vec2{ nx(W - 16.0f), ny(H - 50.0f) };
            formStack.padding = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
            formStack.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
            formStack.scrollable = true;

            auto makeLabel = [](const std::string& id, const std::string& text) {
                WidgetElement lbl;
                lbl.type = WidgetElementType::Text;
                lbl.id = id;
                lbl.text = text;
                lbl.fontSize = EditorTheme::Get().fontSizeBody;
                lbl.style.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                lbl.fillX = true;
                lbl.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
                lbl.runtimeOnly = true;
                return lbl;
            };

            auto makeEntry = [](const std::string& id, const std::string& value) {
                WidgetElement entry;
                entry.type = WidgetElementType::EntryBar;
                entry.id = id;
                entry.value = value;
                entry.fontSize = EditorTheme::Get().fontSizeBody;
                entry.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                entry.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                entry.fillX = true;
                entry.minSize = Vec2{ 0.0f, EditorTheme::Scaled(24.0f) };
                entry.padding = EditorTheme::Scaled(Vec2{ 6.0f, 4.0f });
                entry.hitTestMode = HitTestMode::Enabled;
                entry.runtimeOnly = true;
                return entry;
            };

            formStack.children.push_back(makeLabel("NM.NameLbl", "Name"));
            { auto e = makeEntry("NM.Name", mstate->name); e.onValueChanged = [mstate](const std::string& v) { mstate->name = v; }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.VsLbl", "Vertex Shader"));
            { auto e = makeEntry("NM.VertexShader", mstate->vertexShader); e.onValueChanged = [mstate](const std::string& v) { mstate->vertexShader = v; }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.FsLbl", "Fragment Shader"));
            { auto e = makeEntry("NM.FragmentShader", mstate->fragmentShader); e.onValueChanged = [mstate](const std::string& v) { mstate->fragmentShader = v; }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.DiffLbl", "Diffuse Texture (asset path)"));
            { auto e = makeEntry("NM.DiffuseTex", ""); e.onValueChanged = [mstate](const std::string& v) { mstate->diffuseTexture = v; }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.SpecLbl", "Specular Texture (asset path)"));
            { auto e = makeEntry("NM.SpecularTex", ""); e.onValueChanged = [mstate](const std::string& v) { mstate->specularTexture = v; }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.MetLbl", "Metallic"));
            { auto e = makeEntry("NM.Metallic", "0"); e.onValueChanged = [mstate](const std::string& v) { try { mstate->metallic = std::stof(v); } catch (...) {} }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.RoughLbl", "Roughness"));
            { auto e = makeEntry("NM.Roughness", "0.5"); e.onValueChanged = [mstate](const std::string& v) { try { mstate->roughness = std::stof(v); } catch (...) {} }; formStack.children.push_back(std::move(e)); }
            formStack.children.push_back(makeLabel("NM.SpecMulLbl", "Specular Multiplier"));
            { auto e = makeEntry("NM.SpecularMultiplier", "1"); e.onValueChanged = [mstate](const std::string& v) { try { mstate->specularMultiplier = std::stof(v); } catch (...) {} }; formStack.children.push_back(std::move(e)); }

            elements.push_back(std::move(formStack));

            {
                WidgetElement createBtn;
                createBtn.type = WidgetElementType::Button;
                createBtn.id = "NM.Create";
                createBtn.from = Vec2{ nx(W - 180.0f), ny(H - 44.0f) };
                createBtn.to = Vec2{ nx(W - 100.0f), ny(H - 12.0f) };
                createBtn.text = "Create";
                createBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
                createBtn.style.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
                createBtn.textAlignH = TextAlignH::Center;
                createBtn.textAlignV = TextAlignV::Center;
                createBtn.style.color = Vec4{ 0.15f, 0.45f, 0.25f, 0.95f };
                createBtn.style.hoverColor = Vec4{ 0.2f, 0.6f, 0.35f, 1.0f };
                createBtn.shaderVertex = "button_vertex.glsl";
                createBtn.shaderFragment = "button_fragment.glsl";
                createBtn.hitTestMode = HitTestMode::Enabled;
                createBtn.onClicked = [mstate, renderer, popup]()
                {
                    auto& diag = DiagnosticsManager::Instance();
                    auto& assetMgr = AssetManager::Instance();
                    const std::filesystem::path contentDir =
                        std::filesystem::path(diag.getProjectInfo().projectPath) / "Content";
                    const std::filesystem::path targetDir = mstate->folder.empty()
                        ? contentDir
                        : contentDir / mstate->folder;
                    std::error_code ec;
                    std::filesystem::create_directories(targetDir, ec);

                    const std::string matName = mstate->name.empty() ? "NewMaterial" : mstate->name;
                    std::string fileName = matName + ".asset";
                    int counter = 1;
                    while (std::filesystem::exists(targetDir / fileName))
                        fileName = matName + std::to_string(counter++) + ".asset";

                    auto mat = std::make_shared<AssetData>();
                    mat->setName(std::filesystem::path(fileName).stem().string());
                    mat->setAssetType(AssetType::Material);
                    mat->setType(AssetType::Material);
                    const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();
                    mat->setPath(relPath);

                    json matData = json::object();
                    if (!mstate->vertexShader.empty()) matData["m_shaderVertex"] = mstate->vertexShader;
                    if (!mstate->fragmentShader.empty()) matData["m_shaderFragment"] = mstate->fragmentShader;
                    std::vector<std::string> texPaths;
                    if (!mstate->diffuseTexture.empty()) texPaths.push_back(mstate->diffuseTexture);
                    if (!mstate->specularTexture.empty()) texPaths.push_back(mstate->specularTexture);
                    if (!texPaths.empty()) matData["m_textureAssetPaths"] = texPaths;
                    matData["m_metallic"] = mstate->metallic;
                    matData["m_roughness"] = mstate->roughness;
                    matData["m_specularMultiplier"] = mstate->specularMultiplier;
                    matData["m_pbrEnabled"] = true;
                    mat->setData(std::move(matData));

                    Asset asset;
                    asset.type = AssetType::Material;
                    auto id = assetMgr.registerLoadedAsset(mat);
                    if (id != 0)
                    {
                        mat->setId(id);
                        asset.ID = id;
                        assetMgr.saveAsset(asset, AssetManager::Sync);
                        AssetRegistryEntry entry;
                        entry.name = mat->getName();
                        entry.path = relPath;
                        entry.type = AssetType::Material;
                        assetMgr.registerAssetInRegistry(entry);
                    }

                    auto& ui = renderer->getUIManager();
                    ui.refreshContentBrowser();
                    ui.showToastMessage("Created: " + fileName, UIManager::kToastMedium);
                    popup->close();
                };
                elements.push_back(std::move(createBtn));
            }

            {
                WidgetElement cancelBtn;
                cancelBtn.type = WidgetElementType::Button;
                cancelBtn.id = "NM.Cancel";
                cancelBtn.from = Vec2{ nx(W - 90.0f), ny(H - 44.0f) };
                cancelBtn.to = Vec2{ nx(W - 16.0f), ny(H - 12.0f) };
                cancelBtn.text = "Cancel";
                cancelBtn.fontSize = EditorTheme::Get().fontSizeSubheading;
                cancelBtn.style.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
                cancelBtn.textAlignH = TextAlignH::Center;
                cancelBtn.textAlignV = TextAlignV::Center;
                cancelBtn.style.color = Vec4{ 0.25f, 0.25f, 0.3f, 0.95f };
                cancelBtn.style.hoverColor = Vec4{ 0.35f, 0.35f, 0.42f, 1.0f };
                cancelBtn.shaderVertex = "button_vertex.glsl";
                cancelBtn.shaderFragment = "button_fragment.glsl";
                cancelBtn.hitTestMode = HitTestMode::Enabled;
                cancelBtn.onClicked = [popup]() { popup->close(); };
                elements.push_back(std::move(cancelBtn));
            }

            auto widget = std::make_shared<EditorWidget>();
            widget->setName("NewMaterial");
            widget->setFillX(true);
            widget->setFillY(true);
            widget->setElements(std::move(elements));
            popup->uiManager().registerWidget("NewMaterial", widget);
        }});

    items.push_back({ "New Entity", [renderer]()
        {
            auto& uiMgr = renderer->getUIManager();
            const auto& folder = uiMgr.getSelectedBrowserFolder();
            if (folder == "__Shaders__") return;

            auto& diagnostics = DiagnosticsManager::Instance();
            auto& assetMgr = AssetManager::Instance();
            const std::filesystem::path contentDir =
                std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
            const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            std::string baseName = "NewEntity";
            std::string fileName = baseName + ".asset";
            int counter = 1;
            while (std::filesystem::exists(targetDir / fileName))
                fileName = baseName + std::to_string(counter++) + ".asset";

            const std::filesystem::path absPath = targetDir / fileName;
            const std::string displayName = std::filesystem::path(fileName).stem().string();
            const std::string relPath = std::filesystem::relative(absPath, contentDir).generic_string();

            // Build a minimal entity asset with a Name and Transform
            nlohmann::json fileJson;
            fileJson["magic"]   = 0x41535453;
            fileJson["version"] = 2;
            fileJson["type"]    = static_cast<int>(AssetType::Entity);
            fileJson["name"]    = displayName;
            fileJson["data"]    = nlohmann::json{
                {"components", nlohmann::json{
                    {"Name", {{"displayName", displayName}}},
                    {"Transform", {{"position", {0.0f, 0.0f, 0.0f}}, {"rotation", {0.0f, 0.0f, 0.0f}}, {"scale", {1.0f, 1.0f, 1.0f}}}}
                }}
            };

            std::ofstream out(absPath, std::ios::out | std::ios::trunc);
            if (!out.is_open())
            {
                uiMgr.showToastMessage("Failed to create entity file.", UIManager::kToastMedium);
                return;
            }
            out << fileJson.dump(4);
            out.close();

            AssetRegistryEntry entry;
            entry.name = displayName;
            entry.path = relPath;
            entry.type = AssetType::Entity;
            assetMgr.registerAssetInRegistry(entry);

            uiMgr.refreshContentBrowser();
            uiMgr.showToastMessage("Created: " + fileName, UIManager::kToastMedium);
            uiMgr.openEntityEditorTab(relPath);
        }});

    // ?? Save selected entity as Prefab ??
    {
        const auto& selectedEntities = renderer->getSelectedEntities();
        if (!selectedEntities.empty())
        {
            items.push_back({ "", {}, true });
            items.push_back({ "Save as Prefab", [renderer]()
                {
                    auto& uiMgr = renderer->getUIManager();
                    const auto& selEnts = renderer->getSelectedEntities();
                    if (selEnts.empty()) { uiMgr.showToastMessage("No entity selected.", UIManager::kToastMedium); return; }
                    const auto entity = static_cast<ECS::Entity>(*selEnts.begin());
                    auto& ecs = ECS::ECSManager::Instance();
                    std::string prefabName = "Entity " + std::to_string(entity);
                    if (const auto* nc = ecs.getComponent<ECS::NameComponent>(entity))
                    {
                        if (!nc->displayName.empty()) prefabName = nc->displayName;
                    }
                    const std::string folder = uiMgr.getSelectedBrowserFolder();
                    uiMgr.savePrefabFromEntity(entity, prefabName, folder);
                }});
        }
    }

    // ?? Asset Reference Tracking ??
    {
        const std::string selectedAsset = uiManager.getSelectedGridAsset();
        if (!selectedAsset.empty())
        {
            items.push_back({ "", {}, true });
            items.push_back({ "Find References", [renderer, selectedAsset]()
                {
                    auto& uiMgr = renderer->getUIManager();
                    auto& assetMgr = AssetManager::Instance();
                    const auto refs = assetMgr.findReferencesTo(selectedAsset);
                    std::vector<std::pair<std::string, std::string>> refItems;
                    refItems.reserve(refs.size());
                    for (const auto& r : refs)
                        refItems.push_back({ r.sourcePath, r.sourceType });
                    const std::string title = "References to " + std::filesystem::path(selectedAsset).stem().string();
                    uiMgr.openAssetReferencesPopup(title, selectedAsset, refItems);
                }});
            items.push_back({ "Show Dependencies", [renderer, selectedAsset]()
                {
                    auto& uiMgr = renderer->getUIManager();
                    auto& assetMgr = AssetManager::Instance();
                    const auto deps = assetMgr.getAssetDependencies(selectedAsset);
                    std::vector<std::pair<std::string, std::string>> depItems;
                    depItems.reserve(deps.size());
                    for (const auto& d : deps)
                    {
                        std::string depType = "Asset";
                        for (const auto& reg : assetMgr.getAssetRegistry())
                        {
                            if (reg.path == d)
                            {
                                if (reg.type == AssetType::Texture) depType = "Texture";
                                else if (reg.type == AssetType::Material) depType = "Material";
                                else if (reg.type == AssetType::Model3D) depType = "3D Object";
                                else if (reg.type == AssetType::Audio) depType = "Audio";
                                else if (reg.type == AssetType::Level) depType = "Level";
                                else if (reg.type == AssetType::Script) depType = "Script";
                                break;
                            }
                        }
                        depItems.push_back({ d, depType });
                    }
                    const std::string title = "Dependencies of " + std::filesystem::path(selectedAsset).stem().string();
                    uiMgr.openAssetReferencesPopup(title, selectedAsset, depItems);
                }});
        }
    }

    uiManager.showDropdownMenu(mousePos, items);
    return true;
}

// Build Pipeline Registration
void EditorApp::registerBuildPipeline()
{
    Renderer* renderer = m_bridge.getRenderer();
    if (!renderer) return;
    auto& uiMgr = m_bridge.getUIManager();
    uiMgr.startAsyncToolchainDetection();
    uiMgr.setOnBuildGame([renderer, &uiMgr = uiMgr](const UIManager::BuildGameConfig& config) { BuildPipeline::execute(config, uiMgr, renderer); });
}

#endif // ENGINE_EDITOR
