#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <thread>
#include <mutex>
#include <atomic>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/RendererFactory.h"
#include "Renderer/SplashWindow.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetTypes.h"
#include "Core/ECS/ECS.h"
#include "Core/MathTypes.h"
#include "Core/AudioManager.h"
#include "Core/EngineLevel.h"
#include "Scripting/PythonScripting.h"
#include "Physics/PhysicsWorld.h"

#include "Renderer/ViewportUIManager.h"
#include "Renderer/UIWidget.h"
#include "Renderer/EditorWindows/PopupWindow.h"
#include "Renderer/EditorWindows/TextureViewerWindow.h"
#include "Renderer/EditorTheme.h"
#if ENGINE_EDITOR
#include "Core/UndoRedoManager.h"
#include "Core/ShortcutManager.h"
#endif

using namespace std;


int main()
{
    auto& logger = Logger::Instance();
    logger.initialize();
    Logger::installCrashHandler();

    auto logTimed = [&](Logger::Category category, const std::string& message, Logger::LogLevel level)
    {
        logger.log(category, message, level);
    };

    logTimed(Logger::Category::Engine, "Engine starting...", Logger::LogLevel::INFO);

    // Hide the console immediately so the user never sees it.
#if defined(_WIN32)
    FreeConsole();
    logger.setSuppressStdout(true);
#endif

    // --- Phase 1: Show something on screen as fast as possible ---
    logTimed(Logger::Category::Engine, "Initialising SDL (video + audio + gamepad)...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        logTimed(Logger::Category::Engine, std::string("Failed to initialise SDL: ") + SDL_GetError(), Logger::LogLevel::FATAL);
        return -1;
    }
    logTimed(Logger::Category::Engine, "SDL initialised successfully.", Logger::LogLevel::INFO);

    auto& diagnostics = DiagnosticsManager::Instance();
    if (!diagnostics.loadConfig())
    {
        diagnostics.setWindowSize(Vec2{ 800.0f, 600.0f });
        diagnostics.setWindowState(DiagnosticsManager::WindowState::Maximized);
    }

    // Determine startup mode ("fast" = toast progress in main window, "normal" = splash screen)
    // Moved after runtime mode detection below; declared here for scope.
    bool useSplash = true;

    // Determine renderer backend from config (default: OpenGL)
    const RendererBackend activeBackend = [&]() {
        switch (diagnostics.getRHIType())
        {
        case DiagnosticsManager::RHIType::OpenGL:    return RendererBackend::OpenGL;
        case DiagnosticsManager::RHIType::Vulkan:    return RendererBackend::Vulkan;
        case DiagnosticsManager::RHIType::DirectX12: return RendererBackend::DirectX12;
        default:                                     return RendererBackend::OpenGL;
        }
    }();
    logTimed(Logger::Category::Engine, std::string("Selected renderer backend: ") + DiagnosticsManager::rhiTypeToString(diagnostics.getRHIType()), Logger::LogLevel::INFO);

    // --- Runtime mode detection ---
    // Prefer compile-time baked game config (set via -DGAME_START_LEVEL at build).
    // Fall back to game.ini next to the executable for legacy/debug builds.
    bool isRuntimeMode = false;
    std::string rtStartLevel;
    std::string rtWindowTitle = "Game";
    std::string rtBuildProfile = "Development";
    std::string rtLogLevel = "info";
    bool rtEnableHotReload = true;
    bool rtEnableValidation = false;
    bool rtEnableProfiler = true;

#if defined(GAME_START_LEVEL)
    // Baked-in game config — the project was compiled directly into the binary.
    isRuntimeMode = true;
    rtStartLevel = GAME_START_LEVEL;
#   if defined(GAME_WINDOW_TITLE_BAKED)
    rtWindowTitle = GAME_WINDOW_TITLE_BAKED;
#   endif
    rtBuildProfile = "Shipping";
    rtEnableHotReload = false;
    logTimed(Logger::Category::Engine, "Runtime mode (compiled-in). StartLevel=" + rtStartLevel, Logger::LogLevel::INFO);
#else
    {
        const char* bp = SDL_GetBasePath();
        if (bp)
        {
            const std::filesystem::path iniPath = std::filesystem::path(bp) / "game.ini";
            if (std::filesystem::exists(iniPath))
            {
                std::ifstream iniFile(iniPath);
                if (iniFile.is_open())
                {
                    std::string line;
                    while (std::getline(iniFile, line))
                    {
                        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                        const auto eq = line.find('=');
                        if (eq == std::string::npos) continue;
                        const std::string key = line.substr(0, eq);
                        const std::string val = line.substr(eq + 1);
                        if (key == "StartLevel")         rtStartLevel = val;
                        else if (key == "WindowTitle")   rtWindowTitle = val;
                        else if (key == "BuildProfile")  rtBuildProfile = val;
                        else if (key == "LogLevel")      rtLogLevel = val;
                        else if (key == "EnableHotReload")  rtEnableHotReload = (val == "true" || val == "1");
                        else if (key == "EnableValidation") rtEnableValidation = (val == "true" || val == "1");
                        else if (key == "EnableProfiler")   rtEnableProfiler = (val == "true" || val == "1");
                    }
                    isRuntimeMode = true;
                    logTimed(Logger::Category::Engine, "Runtime mode detected (game.ini). Profile=" + rtBuildProfile + " StartLevel=" + rtStartLevel, Logger::LogLevel::INFO);
                }
            }
        }
    }
#endif

    std::string chosenPath;
    bool chosenIsNew = false;
    bool chosenSetDefault = false;
    bool chosenIncludeDefaultContent = true;
    DiagnosticsManager::RHIType chosenRHI = DiagnosticsManager::RHIType::OpenGL;
    bool projectChosen = false;
    bool startupSelectionCancelled = false;

    // Runtime mode: use the executable directory as the project path
    if (isRuntimeMode)
    {
        const char* bp = SDL_GetBasePath();
        if (bp)
        {
            chosenPath = std::filesystem::path(bp).string();
            // Remove trailing separator if present
            while (!chosenPath.empty() && (chosenPath.back() == '/' || chosenPath.back() == '\\'))
                chosenPath.pop_back();
            projectChosen = true;
        }

        // Apply profile-based log level
        if (rtLogLevel == "verbose")
            Logger::Instance().setMinimumLogLevel(Logger::LogLevel::INFO);
        else if (rtLogLevel == "error")
            Logger::Instance().setMinimumLogLevel(Logger::LogLevel::ERROR);
        else
            Logger::Instance().setMinimumLogLevel(Logger::LogLevel::INFO);

        // Suppress stdout in Shipping builds
        if (rtBuildProfile == "Shipping")
            Logger::Instance().setSuppressStdout(true);
    }

    // Determine startup mode now that isRuntimeMode is known
    if (isRuntimeMode)
        useSplash = false; // Runtime mode skips splash entirely
    else if (auto v = diagnostics.getState("StartupMode"))
        useSplash = (*v == "normal");

    // Check for a persisted default project
#if ENGINE_EDITOR
    if (!projectChosen)
    {
        auto defaultProj = diagnostics.getState("DefaultProject");
        if (defaultProj && !defaultProj->empty() && std::filesystem::exists(*defaultProj))
        {
            chosenPath = *defaultProj;
            projectChosen = true;
        }
    }

    // If no default project, show project selection screen using a temporary renderer
    if (!projectChosen)
    {
        logTimed(Logger::Category::Engine, "No valid default project found. Opening project selection...", Logger::LogLevel::INFO);

        auto* tempRenderer = RendererFactory::createRenderer(activeBackend);
        if (tempRenderer->initialize())
        {
            SDL_WindowID tempWindowId = 0;
            if (auto* w = tempRenderer->window())
            {
                SDL_SetWindowBordered(w, false);
                SDL_SetWindowResizable(w, false);
                SDL_SetWindowFullscreen(w, 0);
                SDL_RestoreWindow(w);
                SDL_SetWindowSize(w, 720, 540);
                SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                SDL_ShowWindow(w);
                SDL_RaiseWindow(w);
                SDL_StartTextInput(w);
                tempWindowId = SDL_GetWindowID(w);
            }

            tempRenderer->getUIManager().openProjectScreen(
                [&](const std::string& path, bool isNew, bool setAsDefault, bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI)
                {
                    chosenPath = path;
                    chosenIsNew = isNew;
                    chosenSetDefault = setAsDefault;
                    chosenIncludeDefaultContent = includeDefaultContent;
                    chosenRHI = selectedRHI;
                    projectChosen = true;
                });

            bool tempWindowOpen = true;
            while (!projectChosen && tempWindowOpen)
            {
                auto& tempUi = tempRenderer->getUIManager();
                SDL_Event ev;
                while (SDL_PollEvent(&ev))
                {
                    if (ev.type == SDL_EVENT_QUIT)
                    {
                        diagnostics.requestShutdown();
                        startupSelectionCancelled = true;
                        tempWindowOpen = false;
                        break;
                    }

                    if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && tempWindowId != 0 && ev.window.windowID == tempWindowId)
                    {
                        diagnostics.requestShutdown();
                        startupSelectionCancelled = true;
                        tempWindowOpen = false;
                        break;
                    }

                    if (ev.type == SDL_EVENT_MOUSE_MOTION)
                    {
                        tempUi.setMousePosition(Vec2{ ev.motion.x, ev.motion.y });
                    }

                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                    {
                        const Vec2 mousePos{ static_cast<float>(ev.button.x), static_cast<float>(ev.button.y) };
                        tempUi.setMousePosition(mousePos);
                        if (tempUi.handleMouseDown(mousePos, ev.button.button))
                        {
                            continue;
                        }
                    }

                    if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP)
                    {
                        const Vec2 mousePos{ static_cast<float>(ev.button.x), static_cast<float>(ev.button.y) };
                        tempUi.setMousePosition(mousePos);
                        if (tempUi.handleMouseUp(mousePos, ev.button.button))
                        {
                            continue;
                        }
                    }

                    if (ev.type == SDL_EVENT_MOUSE_WHEEL)
                    {
                        if (tempUi.handleScroll(tempUi.getMousePosition(), ev.wheel.y))
                        {
                            continue;
                        }
                    }

                    if (ev.type == SDL_EVENT_TEXT_INPUT)
                    {
                        if (tempUi.handleTextInput(ev.text.text))
                        {
                            continue;
                        }
                    }

                    if (ev.type == SDL_EVENT_KEY_DOWN)
                    {
                        if (tempUi.handleKeyDown(ev.key.key))
                        {
                            continue;
                        }
                    }

                    if (tempRenderer->routeEventToPopup(ev))
                        continue;
                }

                if (!tempWindowOpen || diagnostics.isShutdownRequested())
                {
                    startupSelectionCancelled = true;
                    tempWindowOpen = false;
                    logTimed(Logger::Category::Engine, "Project selection window was closed without choosing a project.", Logger::LogLevel::INFO);
                    break;
                }

                tempRenderer->render();
                tempRenderer->present();

                SDL_Delay(16);
            }

            // Drain quit events
            SDL_Event drain;
            while (SDL_PollEvent(&drain))
            {
                if (drain.type == SDL_EVENT_QUIT) continue;
            }
        }
        if (auto* w = tempRenderer->window())
        {
            SDL_StopTextInput(w);
        }
        tempRenderer->shutdown();
        delete tempRenderer;
    }

    if (startupSelectionCancelled || diagnostics.isShutdownRequested())
    {
        logTimed(Logger::Category::Engine, "Startup project selection cancelled. Exiting engine.", Logger::LogLevel::INFO);
        SDL_Quit();
        return 0;
    }

    // Fallback: if still no project chosen, use SampleProject
    if (!projectChosen)
    {
        std::filesystem::path downloadsPath;
#if defined(_WIN32)
        if (const char* userProfile = std::getenv("USERPROFILE"))
        {
            downloadsPath = std::filesystem::path(userProfile) / "Downloads";
        }
#else
        if (const char* home = std::getenv("HOME"))
        {
            downloadsPath = std::filesystem::path(home) / "Downloads";
        }
#endif
        if (downloadsPath.empty())
        {
            downloadsPath = std::filesystem::current_path();
        }

        chosenPath = (downloadsPath / "SampleProject").string();
        chosenIsNew = !std::filesystem::exists(chosenPath);
        chosenSetDefault = true;
        chosenIncludeDefaultContent = true;
        projectChosen = true;
        logTimed(Logger::Category::Engine, "Fallback to SampleProject: " + chosenPath, Logger::LogLevel::INFO);
    }
#endif // ENGINE_EDITOR

    // Runtime mode without ENGINE_EDITOR: require game.ini or fail
#if !ENGINE_EDITOR
    if (!projectChosen)
    {
        logTimed(Logger::Category::Engine,
            "Runtime build: no game.ini found next to executable. Shutting down.",
            Logger::LogLevel::FATAL);
        SDL_Quit();
        return -1;
    }
#endif

    // Now we have a project to load/create. Show SplashWindow.
    auto splash = RendererFactory::createSplashWindow(activeBackend);
    if (useSplash)
    {
        if (splash->create())
        {
            splash->setStatus("Initializing renderer...");
            splash->render();
        }
    }

    logTimed(Logger::Category::Rendering, std::string("Initialising Renderer (") + DiagnosticsManager::rhiTypeToString(diagnostics.getRHIType()) + ")...", Logger::LogLevel::INFO);
    Renderer* renderer = RendererFactory::createRenderer(activeBackend);

    if (!renderer->initialize())
    {
        logTimed(Logger::Category::Rendering, "Failed to initialise renderer.", Logger::LogLevel::FATAL);
        delete renderer;
        SDL_Quit();
        return -1;
    }

    SDL_ShowCursor();
    if (auto* w = renderer->window())
    {
        const Vec2 windowSize = diagnostics.getWindowSize();
        if (windowSize.x > 0.0f && windowSize.y > 0.0f)
        {
            SDL_SetWindowSize(w, static_cast<int>(windowSize.x), static_cast<int>(windowSize.y));
        }

        switch (diagnostics.getWindowState())
        {
        case DiagnosticsManager::WindowState::Fullscreen:
            SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN);
            break;
        case DiagnosticsManager::WindowState::Normal:
            SDL_RestoreWindow(w);
            break;
        case DiagnosticsManager::WindowState::Maximized:
        default:
            SDL_MaximizeWindow(w);
            break;
        }

        SDL_SetWindowRelativeMouseMode(w, false);
        SDL_StartTextInput(w);

        // Runtime mode: auto-detect display resolution and always start fullscreen
        if (isRuntimeMode)
        {
            SDL_SetWindowTitle(w, rtWindowTitle.c_str());
            const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
            if (dm)
            {
                SDL_SetWindowSize(w, dm->w, dm->h);
            }
            SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN);
        }
    }

    logTimed(Logger::Category::Rendering, std::string("Renderer initialised successfully: ") + renderer->name(), Logger::LogLevel::INFO);
    renderer->setVSyncEnabled(false);

    // Apply persisted engine settings
    {
        auto& diag = DiagnosticsManager::Instance();
        if (auto v = diag.getState("ShadowsEnabled"))
            renderer->setShadowsEnabled(*v != "0");
        if (auto v = diag.getState("OcclusionCullingEnabled"))
            renderer->setOcclusionCullingEnabled(*v != "0");
        // UIDebugEnabled is intentionally not restored from config;
        // debug draw outlines should always start disabled.
        if (auto v = diag.getState("BoundsDebugEnabled"))
        {
            if ((*v == "1") != renderer->isBoundsDebugEnabled())
                renderer->toggleBoundsDebug();
        }
        if (auto v = diag.getState("HeightFieldDebugEnabled"))
            renderer->setHeightFieldDebugEnabled(*v == "1");
        if (auto v = diag.getState("VSyncEnabled"))
            renderer->setVSyncEnabled(*v == "1");
        if (auto v = diag.getState("WireframeEnabled"))
            renderer->setWireframeEnabled(*v == "1");
        if (auto v = diag.getState("PostProcessingEnabled"))
            renderer->setPostProcessingEnabled(*v != "0");
        if (auto v = diag.getState("GammaCorrectionEnabled"))
            renderer->setGammaCorrectionEnabled(*v != "0");
        if (auto v = diag.getState("ToneMappingEnabled"))
            renderer->setToneMappingEnabled(*v != "0");
        if (auto v = diag.getState("AntiAliasingMode"))
        {
            int mode = 0;
            try { mode = std::stoi(*v); } catch (...) {}
            renderer->setAntiAliasingMode(static_cast<Renderer::AntiAliasingMode>(mode));
        }
        // Legacy: migrate old FxaaEnabled to new mode
        else if (auto fv = diag.getState("FxaaEnabled"))
        {
            if (*fv == "1")
                renderer->setAntiAliasingMode(Renderer::AntiAliasingMode::FXAA);
        }
        if (auto v = diag.getState("FogEnabled"))
            renderer->setFogEnabled(*v == "1");
        if (auto v = diag.getState("BloomEnabled"))
            renderer->setBloomEnabled(*v == "1");
        if (auto v = diag.getState("SsaoEnabled"))
            renderer->setSsaoEnabled(*v == "1");
        if (auto v = diag.getState("CsmEnabled"))
            renderer->setCsmEnabled(*v != "0");
        if (auto v = diag.getState("TextureCompressionEnabled"))
            renderer->setTextureCompressionEnabled(*v == "1");
        if (auto v = diag.getState("TextureStreamingEnabled"))
            renderer->setTextureStreamingEnabled(*v == "1");
        if (auto v = diag.getState("DisplacementMappingEnabled"))
            renderer->setDisplacementMappingEnabled(*v == "1");
        if (auto v = diag.getState("DisplacementScale"))
        {
            try { renderer->setDisplacementScale(std::stof(*v)); } catch (...) {}
        }
        if (auto v = diag.getState("TessellationLevel"))
        {
            try { renderer->setTessellationLevel(std::stof(*v)); } catch (...) {}
        }
    }

    // In fast mode, show the main window immediately (splash mode keeps it hidden until ready).
    if (!useSplash)
    {
        if (auto* w = renderer->window())
        {
            SDL_ShowWindow(w);
        }
    }

    // Helper: show progress during subsystem init.
    auto showProgress = [&](const std::string& msg)
    {
        if (splash->isOpen())
        {
            splash->setStatus(msg);
            splash->render();
        }
        else if (renderer)
        {
            renderer->getUIManager().showToastMessage(msg, 3.0f);
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_EVENT_QUIT)
                    continue; // ignore â€“ may be leftover from popup destruction
            }
            renderer->getUIManager().updateNotifications(0.016f);
            renderer->render();
            renderer->present();
        }
    };

    // --- Phase 2: Initialise subsystems one by one, showing progress ---

    // Scripting
    showProgress("Initializing scripting...");
    logTimed(Logger::Category::Engine, "Initialising Python scripting...", Logger::LogLevel::INFO);
    if (!Scripting::Initialize())
    {
        logTimed(Logger::Category::Engine, "Failed to initialize Python scripting.", Logger::LogLevel::ERROR);
    }
    Scripting::SetRenderer(renderer);

    // Audio
    showProgress("Initializing audio...");
    auto& audioManager = AudioManager::Instance();
    logTimed(Logger::Category::Engine, "Initialising AudioManager (OpenAL)...", Logger::LogLevel::INFO);
    if (!audioManager.initialize())
    {
        logTimed(Logger::Category::Engine, "AudioManager initialization failed.", Logger::LogLevel::ERROR);
    }

    #if ENGINE_EDITOR
    // --- Detect DPI scale early so widget assets are created at correct size ---
    {
        float dpiScale = 1.0f;
        if (auto savedScale = diagnostics.getState("UIScale"); savedScale && !savedScale->empty())
        {
            try { dpiScale = std::stof(*savedScale); }
            catch (...) { dpiScale = 1.0f; }
        }
        else
        {
            const auto& hwInfo = diagnostics.getHardwareInfo();
            for (const auto& mon : hwInfo.monitors)
            {
                if (mon.primary && mon.dpiScale > 0.0f)
                {
                    dpiScale = mon.dpiScale;
                    break;
                }
            }
        }
        if (dpiScale < 0.5f)  dpiScale = 0.5f;
        if (dpiScale > 4.0f)  dpiScale = 4.0f;

        EditorTheme::Get().applyDpiScale(dpiScale);
        logTimed(Logger::Category::Engine, "DPI scale set to " + std::to_string(dpiScale) + " before asset init.", Logger::LogLevel::INFO);
    }
#endif // ENGINE_EDITOR

    // Asset system
    showProgress("Initializing asset system...");
    auto& assetManager = AssetManager::Instance();
    logTimed(Logger::Category::AssetManagement, "Initialising AssetManager...", Logger::LogLevel::INFO);
    if (!assetManager.initialize())
    {
        logTimed(Logger::Category::AssetManagement, "AssetManager initialisation failed.", Logger::LogLevel::FATAL);
        delete renderer;
        SDL_Quit();
        return -1;
    }
    logTimed(Logger::Category::AssetManagement, "AssetManager initialised successfully.", Logger::LogLevel::INFO);

    // Load project
    showProgress("Loading project...");
    std::string cwd = std::filesystem::current_path().string();
    logTimed(Logger::Category::Engine, "Startup path: " + cwd, Logger::LogLevel::INFO);

    bool projectLoaded = false;

    if (chosenIsNew)
    {
        const std::string parentDir = std::filesystem::path(chosenPath).parent_path().string();
        const std::string projName = std::filesystem::path(chosenPath).filename().string();
        logTimed(Logger::Category::Engine, "Creating new project: " + projName + " at " + parentDir, Logger::LogLevel::INFO);
        if (assetManager.createProject(parentDir, projName, { projName, "1.0", "1.0", "", chosenRHI }, AssetManager::Sync, chosenIncludeDefaultContent))
        {
            projectLoaded = true;
        }
    }
    else
    {
        logTimed(Logger::Category::Engine, "Loading project: " + chosenPath, Logger::LogLevel::INFO);
        if (assetManager.loadProject(chosenPath))
        {
            projectLoaded = true;
        }
    }

    if (projectLoaded)
    {
        diagnostics.addKnownProject(diagnostics.getProjectInfo().projectPath);
        if (chosenSetDefault)
            diagnostics.setState("DefaultProject", diagnostics.getProjectInfo().projectPath);
    }
    else
    {
        logTimed(Logger::Category::Engine,
            "No project could be loaded or created. Shutting down.",
            Logger::LogLevel::FATAL);
        renderer->shutdown();
        delete renderer;
        SDL_Quit();
        Scripting::Shutdown();
        return -1;
    }

    #if ENGINE_EDITOR
    // --- Phase 2b: Load saved editor theme ---
    {
        if (auto savedTheme = diagnostics.getState("EditorTheme"); savedTheme && !savedTheme->empty())
            EditorTheme::Get().loadThemeByName(*savedTheme);
    }
#endif // ENGINE_EDITOR

    // --- Phase 2c: Initialise Script Hot-Reload ---
    {
        const auto& projectPath = diagnostics.getProjectInfo().projectPath;
        if (!projectPath.empty())
        {
            const std::string contentDir = (std::filesystem::path(projectPath) / "Content").string();
            Scripting::InitScriptHotReload(contentDir);
        }
        if (auto saved = diagnostics.getState("ScriptHotReloadEnabled"); saved && *saved == "false")
            Scripting::SetScriptHotReloadEnabled(false);
    }

    #if ENGINE_EDITOR
    // --- Phase 2d: Load Editor Plugins ---
    {
        const auto& projectPath = diagnostics.getProjectInfo().projectPath;
        if (!projectPath.empty())
        {
            Scripting::LoadEditorPlugins(projectPath);
        }
    }
#endif // ENGINE_EDITOR

    // PIE input capture state (declared before ENGINE_EDITOR block – used in game loop)
    bool pieMouseCaptured = false;
    bool pieInputPaused = false;
    float preCaptureMouseX = 0.0f;
    float preCaptureMouseY = 0.0f;

    // Gamepad state – keep the first connected gamepad open for UI navigation
    SDL_Gamepad* activeGamepad = nullptr;

    // Viewport state variables (used in game loop)
    float cameraSpeedMultiplier = 1.0f;
    bool showMetrics = true;
    bool showOcclusionStats = false;

    // --- Phase 3: Load editor UI widgets ---
#if ENGINE_EDITOR
    showProgress("Loading editor UI...");

    std::function<void()> stopPIE;
    bool gridSnapEnabled = false;

    if (renderer && !isRuntimeMode)
    {
        const unsigned int playTexId = renderer->preloadUITexture("Play.tga");
        const unsigned int stopTexId = renderer->preloadUITexture("Stop.tga");

        stopPIE = [&renderer, playTexId, &pieMouseCaptured, &pieInputPaused, &preCaptureMouseX, &preCaptureMouseY]()
            {
                auto& diag = DiagnosticsManager::Instance();
                if (!diag.isPIEActive())
                {
                    return;
                }
                diag.setPIEActive(false);
                renderer->clearActiveCameraEntity();
                AudioManager::Instance().stopAll();
                PhysicsWorld::Instance().shutdown();
                Scripting::ReloadScripts();
                auto* level = diag.getActiveLevelSoft();
                if (level)
                {
                    level->restoreEcsSnapshot();
                }
                // Restore mouse state from PIE capture
                pieMouseCaptured = false;
                pieInputPaused = false;
                if (auto* w = renderer->window())
                {
                    SDL_SetWindowRelativeMouseMode(w, false);
                    SDL_SetWindowMouseGrab(w, false);
                    SDL_WarpMouseInWindow(w, preCaptureMouseX, preCaptureMouseY);
                }
                SDL_ShowCursor();
                auto& uiMgr = renderer->getUIManager();
                if (auto* el = uiMgr.findElementById("ViewportOverlay.PIE"))
                {
                    el->textureId = playTexId;
                }
                uiMgr.markAllWidgetsDirty();
                uiMgr.refreshWorldOutliner();
                // Destroy all script-spawned viewport widgets
                if (auto* vpUI = renderer->getViewportUIManagerPtr())
                {
                    vpUI->clearAllWidgets();
                }
                Logger::Instance().log(Logger::Category::Engine, "PIE: stopped.", Logger::LogLevel::INFO);
            };

        renderer->getUIManager().registerClickEvent("TitleBar.Close", []()
            {
                Logger::Instance().log(Logger::Category::Input, "TitleBar close button clicked.", Logger::LogLevel::INFO);
                DiagnosticsManager::Instance().requestShutdown();
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Minimize", [&renderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "TitleBar minimize button clicked.", Logger::LogLevel::INFO);
                if (auto* w = renderer->window())
                {
                    SDL_MinimizeWindow(w);
                }
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Maximize", [&renderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "TitleBar maximize button clicked.", Logger::LogLevel::INFO);
                if (auto* w = renderer->window())
                {
                    if (SDL_GetWindowFlags(w) & SDL_WINDOW_MAXIMIZED)
                    {
                        SDL_RestoreWindow(w);
                    }
                    else
                    {
                        SDL_MaximizeWindow(w);
                    }
                }
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Menu.File", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: File clicked.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Menu.Edit", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Edit clicked.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Menu.Window", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Window clicked.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("WorldSettings.Tools.Landscape", [&renderer]()
            {
                renderer->getUIManager().openLandscapeManagerPopup();
            });

        renderer->getUIManager().registerClickEvent("WorldSettings.Tools.MaterialEditor", [&renderer]()
            {
                renderer->getUIManager().openMaterialEditorPopup();
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Menu.Build", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Build clicked.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("TitleBar.Menu.Help", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Help clicked.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Select", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Select mode.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Move", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Move mode.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Rotate", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Rotate mode.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Scale", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Scale mode.", Logger::LogLevel::INFO);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Undo", [&renderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Undo.", Logger::LogLevel::INFO);
                UndoRedoManager::Instance().undo();
                renderer->getUIManager().markAllWidgetsDirty();
                renderer->getUIManager().refreshWorldOutliner();
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Redo", [&renderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Redo.", Logger::LogLevel::INFO);
                UndoRedoManager::Instance().redo();
                renderer->getUIManager().markAllWidgetsDirty();
                renderer->getUIManager().refreshWorldOutliner();
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Snap", [&renderer, &gridSnapEnabled]()
            {
                gridSnapEnabled = !gridSnapEnabled;
                renderer->setSnapEnabled(gridSnapEnabled);
                renderer->setGridVisible(gridSnapEnabled);
                Logger::Instance().log(Logger::Category::Input,
                    std::string("Toolbar: Grid Snap ") + (gridSnapEnabled ? "ON" : "OFF"),
                    Logger::LogLevel::INFO);
                if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Snap"))
                {
                    el->style.textColor = gridSnapEnabled
                        ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                        : Vec4{ 0.45f, 0.45f, 0.45f, 1.0f };
                    renderer->getUIManager().markAllWidgetsDirty();
                }
                renderer->getUIManager().showToastMessage(
                    gridSnapEnabled ? "Grid Snap: ON" : "Grid Snap: OFF", 1.5f);

                DiagnosticsManager::Instance().setState("GridSnapEnabled", gridSnapEnabled ? "1" : "0");
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Colliders", [&renderer]()
            {
                const bool visible = !renderer->isCollidersVisible();
                renderer->setCollidersVisible(visible);
                Logger::Instance().log(Logger::Category::Input,
                    std::string("Toolbar: Colliders ") + (visible ? "ON" : "OFF"),
                    Logger::LogLevel::INFO);
                if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Colliders"))
                {
                    el->style.textColor = visible
                        ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                        : Vec4{ 0.45f, 0.45f, 0.45f, 1.0f };
                    renderer->getUIManager().markAllWidgetsDirty();
                }
                renderer->getUIManager().showToastMessage(
                    visible ? "Colliders: ON" : "Colliders: OFF", 1.5f);

                DiagnosticsManager::Instance().setState("CollidersVisible", visible ? "1" : "0");
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Bones", [&renderer]()
            {
                const bool visible = !renderer->isBonesVisible();
                renderer->setBonesVisible(visible);
                Logger::Instance().log(Logger::Category::Input,
                    std::string("Toolbar: Bones ") + (visible ? "ON" : "OFF"),
                    Logger::LogLevel::INFO);
                if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Bones"))
                {
                    el->style.textColor = visible
                        ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                        : Vec4{ 0.45f, 0.45f, 0.45f, 1.0f };
                    renderer->getUIManager().markAllWidgetsDirty();
                }
                renderer->getUIManager().showToastMessage(
                    visible ? "Bones: ON" : "Bones: OFF", 1.5f);

                DiagnosticsManager::Instance().setState("BonesVisible", visible ? "1" : "0");
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Layout", [&renderer]()
            {
                auto& uiMgr = renderer->getUIManager();
                if (uiMgr.isDropdownMenuOpen())
                {
                    uiMgr.closeDropdownMenu();
                    return;
                }

                auto* btn = uiMgr.findElementById("ViewportOverlay.Layout");
                Vec2 anchor{ 0.0f, 0.0f };
                if (btn && btn->hasBounds)
                {
                    anchor = Vec2{ btn->boundsMinPixels.x, btn->boundsMaxPixels.y + 2.0f };
                }

                struct LayoutEntry { const char* label; Renderer::ViewportLayout layout; };
                static const LayoutEntry layouts[] = {
                    { "Single",           Renderer::ViewportLayout::Single },
                    { "Two Horizontal",   Renderer::ViewportLayout::TwoHorizontal },
                    { "Two Vertical",     Renderer::ViewportLayout::TwoVertical },
                    { "Quad",             Renderer::ViewportLayout::Quad },
                };

                const auto current = renderer->getViewportLayout();
                std::vector<UIManager::DropdownMenuItem> items;
                for (auto& e : layouts)
                {
                    std::string lbl = e.label;
                    auto lay = e.layout;
                    if (current == lay)
                        lbl = "> " + lbl;
                    items.push_back({ lbl, [&renderer, lay]()
                        {
                            renderer->setViewportLayout(lay);
                            renderer->setActiveSubViewport(0);
                            Logger::Instance().log(Logger::Category::Input,
                                std::string("Viewport Layout: ") + Renderer::viewportLayoutToString(lay),
                                Logger::LogLevel::INFO);
                            renderer->getUIManager().showToastMessage(
                                std::string("Layout: ") + Renderer::viewportLayoutToString(lay), 1.5f);
                        }
                    });
                }
                uiMgr.showDropdownMenu(anchor, items);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.GridSize", [&renderer]()
            {
                auto& uiMgr = renderer->getUIManager();
                if (uiMgr.isDropdownMenuOpen())
                {
                    uiMgr.closeDropdownMenu();
                    return;
                }

                auto* btn = uiMgr.findElementById("ViewportOverlay.GridSize");
                Vec2 anchor{ 0.0f, 0.0f };
                if (btn && btn->hasBounds)
                {
                    anchor = Vec2{ btn->boundsMinPixels.x, btn->boundsMaxPixels.y + 2.0f };
                }

                struct GridEntry { const char* label; float value; };
                static const GridEntry sizes[] = {
                    { "0.25", 0.25f }, { "0.5",  0.5f  }, { "1.0",  1.0f },
                    { "2.0",  2.0f  }, { "5.0",  5.0f  }, { "10.0", 10.0f }
                };

                const float currentSize = renderer->getGridSize();
                std::vector<UIManager::DropdownMenuItem> items;
                for (auto& s : sizes)
                {
                    float val = s.value;
                    std::string lbl = s.label;
                    if (std::abs(currentSize - val) < 0.01f)
                        lbl = "> " + lbl;
                    items.push_back({ lbl, [&renderer, val]()
                        {
                            renderer->setGridSize(val);
                            if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.GridSize"))
                            {
                                char buf[16];
                                std::snprintf(buf, sizeof(buf), "%.2g", static_cast<double>(val));
                                el->text = buf;
                                renderer->getUIManager().markAllWidgetsDirty();
                            }
                            DiagnosticsManager::Instance().setState("GridSize", std::to_string(val));
                        }
                    });
                }
                uiMgr.showDropdownMenu(anchor, items);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.CamSpeed", [&renderer, &cameraSpeedMultiplier]()
            {
                auto& uiMgr = renderer->getUIManager();
                if (uiMgr.isDropdownMenuOpen())
                {
                    uiMgr.closeDropdownMenu();
                    return;
                }

                auto* btn = uiMgr.findElementById("ViewportOverlay.CamSpeed");
                Vec2 anchor{ 0.0f, 0.0f };
                if (btn && btn->hasBounds)
                {
                    constexpr float kDropdownW = 80.0f;
                    anchor = Vec2{ btn->boundsMinPixels.x, btn->boundsMaxPixels.y + 2.0f };
                }

                struct SpeedEntry { const char* label; float value; };
                static const SpeedEntry speeds[] = {
                    { "0.25x", 0.25f }, { "0.5x", 0.5f }, { "1.0x", 1.0f },
                    { "1.5x", 1.5f },   { "2.0x", 2.0f }, { "3.0x", 3.0f },
                    { "5.0x", 5.0f }
                };

                std::vector<UIManager::DropdownMenuItem> items;
                for (auto& s : speeds)
                {
                    float val = s.value;
                    std::string lbl = s.label;
                    if (std::abs(cameraSpeedMultiplier - val) < 0.01f)
                        lbl = "> " + lbl;
                    items.push_back({ lbl, [&renderer, &cameraSpeedMultiplier, val]()
                        {
                            cameraSpeedMultiplier = val;
                            if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.CamSpeed"))
                            {
                                char buf[16];
                                std::snprintf(buf, sizeof(buf), "%.1fx", val);
                                el->text = buf;
                                renderer->getUIManager().markAllWidgetsDirty();
                            }
                        }
                    });
                }
                uiMgr.showDropdownMenu(anchor, items);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Stats", [&renderer, &showMetrics]()
            {
                showMetrics = !showMetrics;
                Logger::Instance().log(Logger::Category::Input,
                    std::string("Toolbar: Stats ") + (showMetrics ? "ON" : "OFF"),
                    Logger::LogLevel::INFO);
                if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Stats"))
                {
                    el->style.textColor = showMetrics
                        ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                        : Vec4{ 0.45f, 0.45f, 0.45f, 1.0f };
                    renderer->getUIManager().markAllWidgetsDirty();
                }
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.RenderMode", [&renderer]()
            {
                auto& uiMgr = renderer->getUIManager();

                if (uiMgr.isDropdownMenuOpen())
                {
                    uiMgr.closeDropdownMenu();
                    return;
                }

                auto* btn = uiMgr.findElementById("ViewportOverlay.RenderMode");
                Vec2 anchor{ 0.0f, 0.0f };
                if (btn && btn->hasBounds)
                {
                    anchor = Vec2{ btn->boundsMinPixels.x, btn->boundsMaxPixels.y + 2.0f };
                }

                struct ModeEntry { const char* label; Renderer::DebugRenderMode mode; };
                static const ModeEntry modes[] = {
                    { "Lit",              Renderer::DebugRenderMode::Lit },
                    { "Unlit",            Renderer::DebugRenderMode::Unlit },
                    { "Wireframe",        Renderer::DebugRenderMode::Wireframe },
                    { "Shadow Map",       Renderer::DebugRenderMode::ShadowMap },
                    { "Shadow Cascades",  Renderer::DebugRenderMode::ShadowCascades },
                    { "Instance Groups",  Renderer::DebugRenderMode::InstanceGroups },
                    { "Normals",          Renderer::DebugRenderMode::Normals },
                    { "Depth",            Renderer::DebugRenderMode::Depth },
                    { "Overdraw",         Renderer::DebugRenderMode::Overdraw },
                };

                std::vector<UIManager::DropdownMenuItem> items;
                for (auto& m : modes)
                {
                    std::string label = m.label;
                    auto mode = m.mode;
                    items.push_back({ label, [&renderer, mode, label]()
                        {
                            renderer->setDebugRenderMode(mode);
                            auto* elem = renderer->getUIManager().findElementById("ViewportOverlay.RenderMode");
                            if (elem) elem->text = label;
                            renderer->getUIManager().markAllWidgetsDirty();
                        }
                    });
                }

                uiMgr.showDropdownMenu(anchor, items);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.Settings", [&renderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Settings clicked.", Logger::LogLevel::INFO);

                auto& uiMgr = renderer->getUIManager();

                // Toggle dropdown menu
                if (uiMgr.isDropdownMenuOpen())
                {
                    uiMgr.closeDropdownMenu();
                    return;
                }

                // Anchor below the Settings button, right-aligned
                auto* btn = uiMgr.findElementById("ViewportOverlay.Settings");
                Vec2 anchor{ 0.0f, 0.0f };
                if (btn && btn->hasBounds)
                {
                    constexpr float kDropdownW = 180.0f;
                    anchor = Vec2{ btn->boundsMaxPixels.x - kDropdownW, btn->boundsMaxPixels.y + 2.0f };
                }

                std::vector<UIManager::DropdownMenuItem> items;
                items.push_back({ "Engine Settings", [&renderer]()
                    {
                        renderer->getUIManager().openEngineSettingsPopup();
                    }
                });

                items.push_back({ "Editor Settings", [&renderer]()
                    {
                        renderer->getUIManager().openEditorSettingsPopup();
                    }
                });

                items.push_back({ "Console", [&renderer]()
                    {
                        renderer->getUIManager().openConsoleTab();
                    }
                });

                items.push_back({ "Profiler", [&renderer]()
                    {
                        renderer->getUIManager().openProfilerTab();
                    }
                });

                items.push_back({ "Shader Viewer", [&renderer]()
                    {
                        renderer->getUIManager().openShaderViewerTab();
                    }
                });

                items.push_back({ "Render Debugger", [&renderer]()
                    {
                        renderer->getUIManager().openRenderDebuggerTab();
                    }
                });

                items.push_back({ "Sequencer", [&renderer]()
                    {
                        renderer->getUIManager().openSequencerTab();
                    }
                });

                items.push_back({ "Level Composition", [&renderer]()
                    {
                        renderer->getUIManager().openLevelCompositionTab();
                    }
                });

                items.push_back({ "---", [](){} });  // separator

                items.push_back({ "Build Game...", [&renderer]()
                    {
                        renderer->getUIManager().openBuildGameDialog();
                    }
                });

                items.push_back({ "Drop to Surface (End)", [&renderer]()
                    {
                        renderer->getUIManager().dropSelectedEntitiesToSurface([](float ox, float oy, float oz) -> std::pair<bool, float>
                        {
                            auto hit = PhysicsWorld::Instance().raycast(ox, oy, oz, 0.0f, -1.0f, 0.0f, 10000.0f);
                            return { hit.hit, hit.point[1] };
                        });
                    }
                });

                // Plugin menu items (Phase 11.3)
                const auto& pluginItems = Scripting::GetPluginMenuItems();
                for (size_t i = 0; i < pluginItems.size(); ++i)
                {
                    const auto& pi = pluginItems[i];
                    items.push_back({ "[Plugin] " + pi.name, [i]()
                        {
                            Scripting::InvokePluginMenuCallback(i);
                        }
                    });
                }

                uiMgr.showDropdownMenu(anchor, items);
            });

        renderer->getUIManager().registerClickEvent("ViewportOverlay.PIE", [&renderer, playTexId, stopTexId, stopPIE, &pieMouseCaptured, &pieInputPaused, &preCaptureMouseX, &preCaptureMouseY]()
            {
                auto& diag = DiagnosticsManager::Instance();
                const bool wasActive = diag.isPIEActive();

                if (!wasActive)
                {
                    auto* level = diag.getActiveLevelSoft();
                    if (!level)
                    {
                        Logger::Instance().log(Logger::Category::Engine, "PIE: no active level to play.", Logger::LogLevel::WARNING);
                        return;
                    }
                    level->snapshotEcsState();

                    // Determine selected physics backend from settings
                    PhysicsWorld::Backend selectedBackend = PhysicsWorld::Backend::Jolt;
                    if (auto v = diag.getState("PhysicsBackend"))
                    {
#ifdef ENGINE_PHYSX_BACKEND_AVAILABLE
                        if (*v == "PhysX") selectedBackend = PhysicsWorld::Backend::PhysX;
#endif
                    }
                    PhysicsWorld::Instance().initialize(selectedBackend);

                    // Apply persisted physics settings
                    {
                        auto& physics = PhysicsWorld::Instance();
                        auto toFloat = [&](const std::string& key, float fallback) -> float {
                            if (auto v = diag.getState(key)) {
                                try { return std::stof(*v); } catch (...) {}
                            }
                            return fallback;
                        };
                        float gx = toFloat("PhysicsGravityX", 0.0f);
                        float gy = toFloat("PhysicsGravityY", -9.81f);
                        float gz = toFloat("PhysicsGravityZ", 0.0f);
                        physics.setGravity(gx, gy, gz);
                        physics.setFixedTimestep(toFloat("PhysicsFixedTimestep", 1.0f / 60.0f));
                        physics.setSleepThreshold(toFloat("PhysicsSleepThreshold", 0.05f));
                    }

                    diag.setPIEActive(true);

                    // Find the first entity with an active CameraComponent
                    {
                        ECS::Schema camSchema;
                        camSchema.require<ECS::CameraComponent>().require<ECS::TransformComponent>();
                        auto& ecs = ECS::ECSManager::Instance();
                        const auto camEntities = ecs.getEntitiesMatchingSchema(camSchema);
                        unsigned int activeCamEntity = 0;
                        for (const auto e : camEntities)
                        {
                            const auto* cam = ecs.getComponent<ECS::CameraComponent>(e);
                            if (cam && cam->isActive)
                            {
                                activeCamEntity = static_cast<unsigned int>(e);
                                break;
                            }
                        }
                        // Fallback: use the first camera entity if none is marked active
                        if (activeCamEntity == 0 && !camEntities.empty())
                        {
                            activeCamEntity = static_cast<unsigned int>(camEntities.front());
                        }
                        if (activeCamEntity != 0)
                        {
                            renderer->setActiveCameraEntity(activeCamEntity);
                            Logger::Instance().log(Logger::Category::Engine, "PIE: using entity camera " + std::to_string(activeCamEntity), Logger::LogLevel::INFO);
                        }
                    }

                    // Capture mouse for PIE: hide cursor, enable relative mouse mode
                    pieMouseCaptured = true;
                    pieInputPaused = false;
                    SDL_GetMouseState(&preCaptureMouseX, &preCaptureMouseY);
                    if (auto* w = renderer->window())
                    {
                        SDL_SetWindowRelativeMouseMode(w, true);
                        SDL_SetWindowMouseGrab(w, true);
                    }
                    SDL_HideCursor();

                    auto& uiMgr = renderer->getUIManager();
                    if (auto* el = uiMgr.findElementById("ViewportOverlay.PIE"))
                    {
                        el->textureId = stopTexId;
                    }
                    uiMgr.markAllWidgetsDirty();
                    Logger::Instance().log(Logger::Category::Engine, "PIE: started.", Logger::LogLevel::INFO);
                }
                else
                {
                    stopPIE();
                }
            });

        const std::string widgetPath = assetManager.getEditorWidgetPath("TitleBar.asset");
        if (!widgetPath.empty())
        {
            const int widgetId = assetManager.loadAsset(widgetPath, AssetType::Widget, AssetManager::Sync);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    if (auto widget = renderer->createWidgetFromAsset(asset))
                    {
                                    renderer->getUIManager().registerWidget("TitleBar", widget);
                                    }
                                }
                            }
                        }

                        renderer->addTab("Viewport", "Viewport", false);

                        renderer->getUIManager().registerClickEvent("TitleBar.Tab.Viewport", [&renderer]()
                            {
                                renderer->setActiveTab("Viewport");
                                renderer->getUIManager().markAllWidgetsDirty();
                            });

                        const std::string toolbarPath = assetManager.getEditorWidgetPath("ViewportOverlay.asset");
                        if (!toolbarPath.empty())
                        {
                            const int widgetId = assetManager.loadAsset(toolbarPath, AssetType::Widget, AssetManager::Sync);
                            if (widgetId != 0)
                            {
                                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                                {
                                    if (auto widget = renderer->createWidgetFromAsset(asset))
                                    {
                                        renderer->getUIManager().registerWidget("ViewportOverlay", widget, "Viewport");
                                    }
                                }
                            }
                        }

                        // Restore snap & grid settings from config
                        {
                            auto& diag = DiagnosticsManager::Instance();
                            if (auto v = diag.getState("GridSnapEnabled"))
                            {
                                gridSnapEnabled = (*v == "1");
                                renderer->setSnapEnabled(gridSnapEnabled);
                                renderer->setGridVisible(gridSnapEnabled);
                                if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.Snap"))
                                {
                                    el->style.textColor = gridSnapEnabled
                                        ? Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }
                                        : Vec4{ 0.45f, 0.45f, 0.45f, 1.0f };
                                }
                            }
                            if (auto v = diag.getState("GridSize"))
                            {
                                try {
                                    const float gs = std::stof(*v);
                                    if (gs > 0.0f)
                                    {
                                        renderer->setGridSize(gs);
                                        if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.GridSize"))
                                        {
                                            char buf[16];
                                            std::snprintf(buf, sizeof(buf), "%.2g", static_cast<double>(gs));
                                            el->text = buf;
                                        }
                                    }
                                } catch (...) {}
                            }
                            renderer->getUIManager().markAllWidgetsDirty();
                        }

        const std::string worldSettingsPath = assetManager.getEditorWidgetPath("WorldSettings.asset");
        if (!worldSettingsPath.empty())
        {
            const int widgetId = assetManager.loadAsset(worldSettingsPath, AssetType::Widget, AssetManager::Sync);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    if (auto widget = renderer->createWidgetFromAsset(asset))
                    {
                        auto findElementById = [](std::vector<WidgetElement>& elements, const std::string& id) -> WidgetElement*
                        {
                            const std::function<WidgetElement*(WidgetElement&)> findRecursive =
                                [&](WidgetElement& element) -> WidgetElement*
                                {
                                    if (element.id == id)
                                    {
                                        return &element;
                                    }
                                    for (auto& child : element.children)
                                    {
                                        if (auto* hit = findRecursive(child))
                                        {
                                            return hit;
                                        }
                                    }
                                    return nullptr;
                                };

                            for (auto& element : elements)
                            {
                                if (auto* hit = findRecursive(element))
                                {
                                    return hit;
                                }
                            }
                            return nullptr;
                        };

                        if (auto* picker = findElementById(widget->getElementsMutable(), "WorldSettings.ClearColor"))
                        {
                            picker->style.color = renderer->getClearColor();
                            picker->onColorChanged = [renderer](const Vec4& color)
                                {
                                    renderer->setClearColor(color);
                                };

                            auto& children = picker->children;
                            WidgetElement* stack = nullptr;
                            for (auto& child : children)
                            {
                                if (child.type == WidgetElementType::StackPanel)
                                {
                                    stack = &child;
                                    break;
                                }
                            }

                            if (stack)
                            {
                                struct RgbState
                                {
                                    int r{ 0 };
                                    int g{ 0 };
                                    int b{ 0 };
                                    WidgetElement* picker{ nullptr };
                                };

                                auto state = std::make_shared<RgbState>();
                                state->r = static_cast<int>(std::round(std::clamp(picker->style.color.x, 0.0f, 1.0f) * 255.0f));
                                state->g = static_cast<int>(std::round(std::clamp(picker->style.color.y, 0.0f, 1.0f) * 255.0f));
                                state->b = static_cast<int>(std::round(std::clamp(picker->style.color.z, 0.0f, 1.0f) * 255.0f));
                                state->picker = picker;

                                const auto applyColor = [state]()
                                {
                                    if (!state->picker || !state->picker->onColorChanged)
                                    {
                                        return;
                                    }
                                    Vec4 color{
                                        std::clamp(state->r / 255.0f, 0.0f, 1.0f),
                                        std::clamp(state->g / 255.0f, 0.0f, 1.0f),
                                        std::clamp(state->b / 255.0f, 0.0f, 1.0f),
                                        1.0f
                                    };
                                    state->picker->style.color = color;
                                    state->picker->onColorChanged(color);
                                };

                                auto configureEntry = [&](const std::string& id, int& channel)
                                {
                                    if (auto* entry = findElementById(stack->children, id))
                                    {
                                        entry->value = std::to_string(channel);
                                        entry->onValueChanged = [&channel, applyColor](const std::string& value)
                                            {
                                                try
                                                {
                                                    int parsed = std::stoi(value);
                                                    channel = std::clamp(parsed, 0, 255);
                                                    applyColor();
                                                }
                                                catch (...)
                                                {
                                                }
                                            };
                                    }
                                };

                                configureEntry("WorldSettings.ClearColor.R", state->r);
                                configureEntry("WorldSettings.ClearColor.G", state->g);
                                configureEntry("WorldSettings.ClearColor.B", state->b);
                            }
                        }

                        // Skybox asset path entry
                        if (auto* skyboxEntry = findElementById(widget->getElementsMutable(), "WorldSettings.SkyboxPath"))
                        {
                            skyboxEntry->value = renderer->getSkyboxPath();
                            skyboxEntry->onValueChanged = [renderer](const std::string& value)
                                {
                                    renderer->setSkyboxPath(value);
                                    auto& diag = DiagnosticsManager::Instance();
                                    if (auto* level = diag.getActiveLevelSoft())
                                    {
                                        level->setSkyboxPath(value);
                                    }
                                    renderer->getUIManager().refreshStatusBar();
                                };

                            // Bind Clear button if it exists
                            if (auto* clearBtn = findElementById(widget->getElementsMutable(), "WorldSettings.SkyboxClear"))
                            {
                                clearBtn->onClicked = [renderer]()
                                    {
                                        renderer->setSkyboxPath("");
                                        auto& diag = DiagnosticsManager::Instance();
                                        if (auto* level = diag.getActiveLevelSoft())
                                        {
                                            level->setSkyboxPath("");
                                        }
                                        renderer->getUIManager().refreshStatusBar();
                                        renderer->getUIManager().showToastMessage("Skybox cleared", 2.5f);
                                    };
                            }
                        }
                        else
                        {
                            // Dynamically add Skybox section if the widget doesn't have it
                            auto& elements = widget->getElementsMutable();
                            WidgetElement* rootStack = nullptr;
                            for (auto& el : elements)
                            {
                                if (el.type == WidgetElementType::StackPanel)
                                {
                                    rootStack = &el;
                                    break;
                                }
                            }
                            if (rootStack)
                            {
                                WidgetElement skyLabel{};
                                skyLabel.id = "WorldSettings.SkyboxLabel";
                                skyLabel.type = WidgetElementType::Text;
                                skyLabel.text = "Skybox Asset";
                                skyLabel.font = "default.ttf";
                                skyLabel.fontSize = 13.0f;
                                skyLabel.style.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                                skyLabel.fillX = true;
                                skyLabel.minSize = Vec2{ 0.0f, 22.0f };
                                skyLabel.padding = Vec2{ 4.0f, 2.0f };
                                skyLabel.runtimeOnly = true;
                                rootStack->children.push_back(std::move(skyLabel));

                                WidgetElement skyEntry{};
                                skyEntry.id = "WorldSettings.SkyboxPath";
                                skyEntry.type = WidgetElementType::EntryBar;
                                skyEntry.value = renderer->getSkyboxPath();
                                skyEntry.font = "default.ttf";
                                skyEntry.fontSize = 13.0f;
                                skyEntry.style.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                                skyEntry.style.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                                skyEntry.fillX = true;
                                skyEntry.minSize = Vec2{ 0.0f, 24.0f };
                                skyEntry.padding = Vec2{ 6.0f, 4.0f };
                                skyEntry.hitTestMode = HitTestMode::Enabled;
                                skyEntry.runtimeOnly = true;
                                skyEntry.onValueChanged = [renderer](const std::string& value)
                                    {
                                        renderer->setSkyboxPath(value);
                                        auto& diag = DiagnosticsManager::Instance();
                                        if (auto* level = diag.getActiveLevelSoft())
                                        {
                                            level->setSkyboxPath(value);
                                        }
                                        renderer->getUIManager().refreshStatusBar();
                                    };
                                rootStack->children.push_back(std::move(skyEntry));

                                WidgetElement clearBtn{};
                                clearBtn.id = "WorldSettings.SkyboxClear";
                                clearBtn.type = WidgetElementType::Button;
                                clearBtn.text = "Clear Skybox";
                                clearBtn.font = "default.ttf";
                                clearBtn.fontSize = 12.0f;
                                clearBtn.style.textColor = Vec4{ 0.9f, 0.7f, 0.7f, 1.0f };
                                clearBtn.textAlignH = TextAlignH::Center;
                                clearBtn.textAlignV = TextAlignV::Center;
                                clearBtn.style.color = Vec4{ 0.3f, 0.15f, 0.15f, 0.8f };
                                clearBtn.style.hoverColor = Vec4{ 0.45f, 0.2f, 0.2f, 0.95f };
                                clearBtn.shaderVertex = "button_vertex.glsl";
                                clearBtn.shaderFragment = "button_fragment.glsl";
                                clearBtn.fillX = true;
                                clearBtn.minSize = Vec2{ 0.0f, 24.0f };
                                clearBtn.padding = Vec2{ 4.0f, 2.0f };
                                clearBtn.hitTestMode = HitTestMode::Enabled;
                                clearBtn.runtimeOnly = true;
                                clearBtn.onClicked = [renderer]()
                                    {
                                        renderer->setSkyboxPath("");
                                        auto& diag = DiagnosticsManager::Instance();
                                        if (auto* level = diag.getActiveLevelSoft())
                                        {
                                            level->setSkyboxPath("");
                                        }
                                        renderer->getUIManager().refreshStatusBar();
                                        renderer->getUIManager().showToastMessage("Skybox cleared", 2.5f);
                                    };
                                rootStack->children.push_back(std::move(clearBtn));
                            }
                        }

                        renderer->getUIManager().registerWidget("WorldSettings", widget, "Viewport");
                    }
                }
            }
        }

        const std::string outlinerPath = assetManager.getEditorWidgetPath("WorldOutliner.asset");
        if (!outlinerPath.empty())
        {
            const int widgetId = assetManager.loadAsset(outlinerPath, AssetType::Widget, AssetManager::Sync);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    if (auto widget = renderer->createWidgetFromAsset(asset))
                    {
                        renderer->getUIManager().registerWidget("WorldOutliner", widget, "Viewport");
                    }
                }
            }
        }

        const std::string entityDetailsPath = assetManager.getEditorWidgetPath("EntityDetails.asset");
        if (!entityDetailsPath.empty())
        {
            const int widgetId = assetManager.loadAsset(entityDetailsPath, AssetType::Widget, AssetManager::Sync);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    if (auto widget = renderer->createWidgetFromAsset(asset))
                    {
                        renderer->getUIManager().registerWidget("EntityDetails", widget, "Viewport");
                    }
                }
            }
        }

        // StatusBar must be registered BEFORE ContentBrowser so it docks at the
        // very bottom (first BottomLeft entry consumes the lowest strip).
        const std::string statusBarPath = assetManager.getEditorWidgetPath("StatusBar.asset");
        if (!statusBarPath.empty())
        {
            const int widgetId = assetManager.loadAsset(statusBarPath, AssetType::Widget, AssetManager::Sync);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    if (auto widget = renderer->createWidgetFromAsset(asset))
                    {
                        renderer->getUIManager().registerWidget("StatusBar", widget);
                    }
                }
            }
        }

        const std::string contentBrowserPath = assetManager.getEditorWidgetPath("ContentBrowser.asset");
        logTimed(Logger::Category::UI, "[ContentBrowser] main: resolved path='" + contentBrowserPath + "'", Logger::LogLevel::INFO);
        if (!contentBrowserPath.empty())
        {
            const int widgetId = assetManager.loadAsset(contentBrowserPath, AssetType::Widget, AssetManager::Sync);
            logTimed(Logger::Category::UI, "[ContentBrowser] main: loadAsset returned widgetId=" + std::to_string(widgetId), Logger::LogLevel::INFO);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    logTimed(Logger::Category::UI, "[ContentBrowser] main: asset loaded name='" + asset->getName() + "' type=" + std::to_string(static_cast<int>(asset->getAssetType())), Logger::LogLevel::INFO);
                    if (auto widget = renderer->createWidgetFromAsset(asset))
                    {
                        logTimed(Logger::Category::UI, "[ContentBrowser] main: widget created name='" + widget->getName() + "' elements=" + std::to_string(widget->getElements().size()), Logger::LogLevel::INFO);
                        renderer->getUIManager().registerWidget("ContentBrowser", widget, "Viewport");
                        logTimed(Logger::Category::UI, "[ContentBrowser] main: registerWidget('ContentBrowser') completed", Logger::LogLevel::INFO);

                        renderer->getUIManager().registerClickEvent("ContentBrowser.PathBar.Import", [&renderer]()
                            {
                                Logger::Instance().log(Logger::Category::AssetManagement, "Import button clicked.", Logger::LogLevel::INFO);
                                auto* window = renderer ? renderer->window() : nullptr;
                                AssetManager::Instance().OpenImportDialog(window, AssetType::Unknown, AssetManager::Async);
                            });

                        assetManager.setOnImportCompleted([&renderer]()
                            {
                                if (renderer)
                                {
                                    renderer->getUIManager().refreshContentBrowser();
                                }
                            });

                        // --- Drag & Drop: asset dropped on viewport â†’ spawn entity ---
                        renderer->getUIManager().setOnDropOnViewport([&renderer](const std::string& payload, const Vec2& screenPos)
                            {
                                const auto sep = payload.find('|');
                                if (sep == std::string::npos) return;

                                const int typeInt = std::atoi(payload.substr(0, sep).c_str());
                                const std::string relPath = payload.substr(sep + 1);
                                const AssetType assetType = static_cast<AssetType>(typeInt);
                                const std::string assetName = std::filesystem::path(relPath).stem().string();
                                auto& ecs = ECS::ECSManager::Instance();
                                auto& diagnostics = DiagnosticsManager::Instance();

                                // Pick the entity under the cursor (fresh pick buffer render)
                                const unsigned int targetEntity = renderer->pickEntityAtImmediate(
                                    static_cast<int>(screenPos.x), static_cast<int>(screenPos.y));
                                const auto target = static_cast<ECS::Entity>(targetEntity);

                                // --- Skybox: set as level skybox, no entity needed ---
                                if (assetType == AssetType::Skybox)
                                {
                                    renderer->setSkyboxPath(relPath);
                                    auto* level = diagnostics.getActiveLevelSoft();
                                    if (level)
                                    {
                                        level->setSkyboxPath(relPath);
                                    }
                                    Logger::Instance().log(Logger::Category::Engine,
                                        "Set skybox from drag: " + relPath, Logger::LogLevel::INFO);
                                    if (renderer)
                                    {
                                        renderer->getUIManager().refreshStatusBar();
                                        renderer->getUIManager().showToastMessage("Skybox: " + assetName, 2.5f);
                                    }
                                    return;
                                }

                                // --- Prefab: spawn prefab entities at drop position ---
                                if (assetType == AssetType::Prefab)
                                {
                                    Vec3 spawnPos{ 0.0f, 0.0f, 0.0f };
                                    if (!renderer->screenToWorldPos(static_cast<int>(screenPos.x), static_cast<int>(screenPos.y), spawnPos))
                                    {
                                        const Vec3 camPos = renderer->getCameraPosition();
                                        const Vec2 camRot = renderer->getCameraRotationDegrees();
                                        const float yaw = camRot.x * 3.14159265f / 180.0f;
                                        const float pitch = camRot.y * 3.14159265f / 180.0f;
                                        spawnPos.x = camPos.x + cosf(yaw) * cosf(pitch) * 5.0f;
                                        spawnPos.y = camPos.y + sinf(pitch) * 5.0f;
                                        spawnPos.z = camPos.z + sinf(yaw) * cosf(pitch) * 5.0f;
                                    }
                                    renderer->getUIManager().spawnPrefabAtPosition(relPath, spawnPos);
                                    return;
                                }

                                // --- Non-Model3D types: apply to existing entity, or abort if none ---
                                // Materials/Textures can only be applied to existing entities, never spawned alone.
                                // Scripts likewise attach to existing entities.
                                if (assetType != AssetType::Model3D)
                                {
                                    if (targetEntity != 0)
                                    {
                                        switch (assetType)
                                        {
                                        case AssetType::Material:
                                        case AssetType::Texture:
                                        {
                                            ECS::MaterialComponent matComp{};
                                            matComp.materialAssetPath = relPath;
                                            if (ecs.hasComponent<ECS::MaterialComponent>(target))
                                                ecs.setComponent<ECS::MaterialComponent>(target, matComp);
                                            else
                                                ecs.addComponent<ECS::MaterialComponent>(target, matComp);
                                            break;
                                        }
                                        case AssetType::Script:
                                        {
                                            ECS::ScriptComponent scriptComp{};
                                            scriptComp.scriptPath = relPath;
                                            if (ecs.hasComponent<ECS::ScriptComponent>(target))
                                                ecs.setComponent<ECS::ScriptComponent>(target, scriptComp);
                                            else
                                                ecs.addComponent<ECS::ScriptComponent>(target, scriptComp);
                                            break;
                                        }
                                        default:
                                            break;
                                        }

                                        diagnostics.invalidateEntity(targetEntity);
                                        auto* level = diagnostics.getActiveLevelSoft();
                                        if (level) level->setIsSaved(false);
                                        Logger::Instance().log(Logger::Category::Engine,
                                            "Applied '" + assetName + "' to entity " + std::to_string(targetEntity),
                                            Logger::LogLevel::INFO);
                                        if (renderer)
                                        {
                                            renderer->getUIManager().selectEntity(targetEntity);
                                            renderer->getUIManager().showToastMessage(
                                                "Applied " + assetName + " â†’ Entity " + std::to_string(targetEntity), 2.5f);
                                        }
                                    }
                                    else
                                    {
                                        // No entity under cursor â€” cannot apply, show hint
                                        if (renderer)
                                        {
                                            renderer->getUIManager().showToastMessage(
                                                "No entity under cursor to apply " + assetName, 2.5f);
                                        }
                                    }
                                    return;
                                }

                                // --- Model3D: always spawn a new entity ---
                                Vec3 spawnPos{ 0.0f, 0.0f, 0.0f };
                                if (!renderer->screenToWorldPos(static_cast<int>(screenPos.x), static_cast<int>(screenPos.y), spawnPos))
                                {
                                    const Vec3 camPos = renderer->getCameraPosition();
                                    const Vec2 camRot = renderer->getCameraRotationDegrees();
                                    const float yaw = camRot.x * 3.14159265f / 180.0f;
                                    const float pitch = camRot.y * 3.14159265f / 180.0f;
                                    spawnPos.x = camPos.x + cosf(yaw) * cosf(pitch) * 5.0f;
                                    spawnPos.y = camPos.y + sinf(pitch) * 5.0f;
                                    spawnPos.z = camPos.z + sinf(yaw) * cosf(pitch) * 5.0f;
                                }

                                const ECS::Entity entity = ecs.createEntity();

                                ECS::TransformComponent transform{};
                                transform.position[0] = spawnPos.x;
                                transform.position[1] = spawnPos.y;
                                transform.position[2] = spawnPos.z;
                                ecs.addComponent<ECS::TransformComponent>(entity, transform);

                                ECS::NameComponent nameComp;
                                nameComp.displayName = assetName;
                                ecs.addComponent<ECS::NameComponent>(entity, nameComp);

                                ECS::MeshComponent mesh;
                                mesh.meshAssetPath = relPath;
                                ecs.addComponent<ECS::MeshComponent>(entity, mesh);

                                // Auto-add MaterialComponent if the mesh asset references a material
                                {
                                    auto meshAsset = AssetManager::Instance().getLoadedAssetByPath(relPath);
                                    if (!meshAsset)
                                    {
                                        int id = AssetManager::Instance().loadAsset(relPath, AssetType::Model3D);
                                        if (id > 0)
                                            meshAsset = AssetManager::Instance().getLoadedAssetByID(static_cast<unsigned int>(id));
                                    }
                                    if (meshAsset)
                                    {
                                        auto& assetData = meshAsset->getData();
                                        if (assetData.contains("m_materialAssetPaths") && assetData["m_materialAssetPaths"].is_array()
                                            && !assetData["m_materialAssetPaths"].empty())
                                        {
                                            std::string matPath = assetData["m_materialAssetPaths"][0].get<std::string>();
                                            if (!matPath.empty())
                                            {
                                                ECS::MaterialComponent matComp{};
                                                matComp.materialAssetPath = matPath;
                                                ecs.addComponent<ECS::MaterialComponent>(entity, matComp);
                                            }
                                        }
                                    }
                                }

                                auto* level = diagnostics.getActiveLevelSoft();
                                if (level)
                                {
                                    level->onEntityAdded(entity);
                                }

                                Logger::Instance().log(Logger::Category::Engine,
                                    "Spawned entity " + std::to_string(entity) + " (" + assetName + ") at ("
                                    + std::to_string(spawnPos.x) + ", " + std::to_string(spawnPos.y) + ", " + std::to_string(spawnPos.z) + ")",
                                    Logger::LogLevel::INFO);

                                if (renderer)
                                {
                                    renderer->getUIManager().refreshWorldOutliner();
                                    renderer->getUIManager().selectEntity(static_cast<unsigned int>(entity));
                                    renderer->getUIManager().showToastMessage("Spawned: " + assetName, 2.5f);
                                }

                                // Snapshot components for spawn undo/redo
                                auto spSavedTransform   = ecs.hasComponent<ECS::TransformComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::TransformComponent>(entity))   : std::nullopt;
                                auto spSavedName        = ecs.hasComponent<ECS::NameComponent>(entity)        ? std::make_optional(*ecs.getComponent<ECS::NameComponent>(entity))        : std::nullopt;
                                auto spSavedMesh        = ecs.hasComponent<ECS::MeshComponent>(entity)        ? std::make_optional(*ecs.getComponent<ECS::MeshComponent>(entity))        : std::nullopt;
                                auto spSavedMaterial    = ecs.hasComponent<ECS::MaterialComponent>(entity)    ? std::make_optional(*ecs.getComponent<ECS::MaterialComponent>(entity))    : std::nullopt;
                                auto spSavedLight       = ecs.hasComponent<ECS::LightComponent>(entity)       ? std::make_optional(*ecs.getComponent<ECS::LightComponent>(entity))       : std::nullopt;
                                auto spSavedCamera      = ecs.hasComponent<ECS::CameraComponent>(entity)      ? std::make_optional(*ecs.getComponent<ECS::CameraComponent>(entity))      : std::nullopt;
                                auto spSavedPhysics     = ecs.hasComponent<ECS::PhysicsComponent>(entity)     ? std::make_optional(*ecs.getComponent<ECS::PhysicsComponent>(entity))     : std::nullopt;
                                auto spSavedScript      = ecs.hasComponent<ECS::ScriptComponent>(entity)      ? std::make_optional(*ecs.getComponent<ECS::ScriptComponent>(entity))      : std::nullopt;
                                auto spSavedCollision   = ecs.hasComponent<ECS::CollisionComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::CollisionComponent>(entity))   : std::nullopt;
                                auto spSavedHeightField = ecs.hasComponent<ECS::HeightFieldComponent>(entity) ? std::make_optional(*ecs.getComponent<ECS::HeightFieldComponent>(entity)) : std::nullopt;

                                // Push undo/redo for entity spawn
                                UndoRedoManager::Command spawnCmd;
                                spawnCmd.description = "Spawn " + assetName;
                                spawnCmd.execute = [entity, spSavedTransform, spSavedName, spSavedMesh, spSavedMaterial, spSavedLight, spSavedCamera, spSavedPhysics, spSavedScript, spSavedCollision, spSavedHeightField]()
                                    {
                                        auto& e = ECS::ECSManager::Instance();
                                        e.createEntity(entity);
                                        if (spSavedTransform)   e.addComponent<ECS::TransformComponent>(entity, *spSavedTransform);
                                        if (spSavedName)        e.addComponent<ECS::NameComponent>(entity, *spSavedName);
                                        if (spSavedMesh)        e.addComponent<ECS::MeshComponent>(entity, *spSavedMesh);
                                        if (spSavedMaterial)    e.addComponent<ECS::MaterialComponent>(entity, *spSavedMaterial);
                                        if (spSavedLight)       e.addComponent<ECS::LightComponent>(entity, *spSavedLight);
                                        if (spSavedCamera)      e.addComponent<ECS::CameraComponent>(entity, *spSavedCamera);
                                        if (spSavedPhysics)     e.addComponent<ECS::PhysicsComponent>(entity, *spSavedPhysics);
                                        if (spSavedScript)      e.addComponent<ECS::ScriptComponent>(entity, *spSavedScript);
                                        if (spSavedCollision)   e.addComponent<ECS::CollisionComponent>(entity, *spSavedCollision);
                                        if (spSavedHeightField) e.addComponent<ECS::HeightFieldComponent>(entity, *spSavedHeightField);
                                        auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
                                        if (lvl) lvl->onEntityAdded(entity);
                                    };
                                spawnCmd.undo = [entity]()
                                    {
                                        auto& e = ECS::ECSManager::Instance();
                                        auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
                                        if (lvl) lvl->onEntityRemoved(entity);
                                        e.removeEntity(entity);
                                    };
                                UndoRedoManager::Instance().pushCommand(std::move(spawnCmd));
                            });

                        // --- Drag & Drop: asset dropped on content browser folder â†’ move asset ---
                        renderer->getUIManager().setOnDropOnFolder([&renderer](const std::string& payload, const std::string& folderPath)
                            {
                                const auto sep = payload.find('|');
                                if (sep == std::string::npos) return;

                                const std::string relPath = payload.substr(sep + 1);
                                const std::string fileName = std::filesystem::path(relPath).filename().string();
                                const std::string newRelPath = folderPath.empty()
                                    ? fileName
                                    : (folderPath + "/" + fileName);

                                if (relPath == newRelPath) return;

                                if (!AssetManager::Instance().moveAsset(relPath, newRelPath))
                                {
                                    Logger::Instance().log(Logger::Category::AssetManagement,
                                        "Failed to move asset: " + relPath, Logger::LogLevel::ERROR);
                                    return;
                                }

                                DiagnosticsManager::Instance().setScenePrepared(false);
                                if (renderer)
                                {
                                    renderer->getUIManager().refreshContentBrowser();
                                    renderer->getUIManager().showToastMessage("Moved: " + fileName, 2.5f);
                                }
                            });

                        // --- Drag & Drop: asset dropped on Outliner entity â†’ apply to entity ---
                        renderer->getUIManager().setOnDropOnEntity([&renderer](const std::string& payload, unsigned int entityId)
                            {
                                const auto sep = payload.find('|');
                                if (sep == std::string::npos) return;

                                const int typeInt = std::atoi(payload.substr(0, sep).c_str());
                                const std::string relPath = payload.substr(sep + 1);
                                const AssetType assetType = static_cast<AssetType>(typeInt);
                                const std::string assetName = std::filesystem::path(relPath).stem().string();
                                auto& ecs = ECS::ECSManager::Instance();
                                const auto entity = static_cast<ECS::Entity>(entityId);

                                switch (assetType)
                                {
                                case AssetType::Material:
                                case AssetType::Texture:
                                {
                                    ECS::MaterialComponent matComp{};
                                    matComp.materialAssetPath = relPath;
                                    if (ecs.hasComponent<ECS::MaterialComponent>(entity))
                                        ecs.setComponent<ECS::MaterialComponent>(entity, matComp);
                                    else
                                        ecs.addComponent<ECS::MaterialComponent>(entity, matComp);
                                    break;
                                }
                                case AssetType::Model3D:
                                {
                                    ECS::MeshComponent meshComp{};
                                    meshComp.meshAssetPath = relPath;
                                    if (ecs.hasComponent<ECS::MeshComponent>(entity))
                                        ecs.setComponent<ECS::MeshComponent>(entity, meshComp);
                                    else
                                        ecs.addComponent<ECS::MeshComponent>(entity, meshComp);

                                    // Auto-add MaterialComponent if the mesh asset references a material
                                    {
                                        auto meshAsset = AssetManager::Instance().getLoadedAssetByPath(relPath);
                                        if (!meshAsset)
                                        {
                                            int id = AssetManager::Instance().loadAsset(relPath, AssetType::Model3D);
                                            if (id > 0)
                                                meshAsset = AssetManager::Instance().getLoadedAssetByID(static_cast<unsigned int>(id));
                                        }
                                        if (meshAsset)
                                        {
                                            auto& assetData = meshAsset->getData();
                                            if (assetData.contains("m_materialAssetPaths") && assetData["m_materialAssetPaths"].is_array()
                                                && !assetData["m_materialAssetPaths"].empty())
                                            {
                                                std::string matPath = assetData["m_materialAssetPaths"][0].get<std::string>();
                                                if (!matPath.empty())
                                                {
                                                    ECS::MaterialComponent matComp{};
                                                    matComp.materialAssetPath = matPath;
                                                    if (ecs.hasComponent<ECS::MaterialComponent>(entity))
                                                        ecs.setComponent<ECS::MaterialComponent>(entity, matComp);
                                                    else
                                                        ecs.addComponent<ECS::MaterialComponent>(entity, matComp);
                                                }
                                            }
                                        }
                                    }
                                    break;
                                }
                                case AssetType::Script:
                                {
                                    ECS::ScriptComponent scriptComp{};
                                    scriptComp.scriptPath = relPath;
                                    if (ecs.hasComponent<ECS::ScriptComponent>(entity))
                                        ecs.setComponent<ECS::ScriptComponent>(entity, scriptComp);
                                    else
                                        ecs.addComponent<ECS::ScriptComponent>(entity, scriptComp);
                                    break;
                                }
                                default:
                                    break;
                                }

                                DiagnosticsManager::Instance().invalidateEntity(entityId);
                                auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
                                if (level)
                                {
                                    level->setIsSaved(false);
                                }
                                Logger::Instance().log(Logger::Category::Engine,
                                    "Applied '" + assetName + "' to entity " + std::to_string(entityId),
                                    Logger::LogLevel::INFO);
                                if (renderer)
                                {
                                    renderer->getUIManager().showToastMessage(
                                        "Applied " + assetName + " â†’ Entity " + std::to_string(entityId), 2.5f);
                                }
                            });
                    }
                    else
                    {
                        logTimed(Logger::Category::UI, "[ContentBrowser] main: createWidgetFromAsset returned nullptr", Logger::LogLevel::WARNING);
                    }
                }
                else
                {
                    logTimed(Logger::Category::UI, "[ContentBrowser] main: getLoadedAssetByID returned nullptr for id=" + std::to_string(widgetId), Logger::LogLevel::WARNING);
                }
            }
            else
            {
                logTimed(Logger::Category::UI, "[ContentBrowser] main: loadAsset failed (returned 0)", Logger::LogLevel::WARNING);
            }
        }
        else
        {
            logTimed(Logger::Category::UI, "[ContentBrowser] main: getEditorWidgetPath returned empty path", Logger::LogLevel::WARNING);
        }

        // --- UndoRedo onChange â†’ mark level dirty + refresh StatusBar ---
        UndoRedoManager::Instance().setOnChanged([&renderer]()
            {
                auto& diag = DiagnosticsManager::Instance();
                auto* level = diag.getActiveLevelSoft();
                if (level)
                {
                    level->setIsSaved(false);
                }
                if (renderer)
                {
                    renderer->getUIManager().refreshStatusBar();
                }
            });

        renderer->getUIManager().registerClickEvent("StatusBar.Undo", [&renderer]()
            {
                auto& undo = UndoRedoManager::Instance();
                if (undo.canUndo())
                {
                    undo.undo();
                    renderer->getUIManager().markAllWidgetsDirty();
                }
            });

        renderer->getUIManager().registerClickEvent("StatusBar.Redo", [&renderer]()
            {
                auto& undo = UndoRedoManager::Instance();
                if (undo.canRedo())
                {
                    undo.redo();
                    renderer->getUIManager().markAllWidgetsDirty();
                }
            });

        renderer->getUIManager().registerClickEvent("StatusBar.Save", [&renderer]()
            {
                // Capture editor camera into the active level before saving
                auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
                if (lvl)
                {
                    lvl->setEditorCameraPosition(renderer->getCameraPosition());
                    lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees());
                    lvl->setHasEditorCamera(true);
                }

                auto& am = AssetManager::Instance();
                if (am.getUnsavedAssetCount() == 0)
                {
                    renderer->getUIManager().showToastMessage("Nothing to save.", 2.0f);
                    return;
                }
                renderer->getUIManager().showUnsavedChangesDialog(nullptr);
            });

        renderer->getUIManager().registerClickEvent("StatusBar.Notifications", [&renderer]()
            {
                renderer->getUIManager().openNotificationHistoryPopup();
            });

        // --- Level loading: double-click a .map in Content Browser ---
        renderer->getUIManager().setOnLevelLoadRequested([&renderer](const std::string& levelRelPath)
            {
                auto& diag = DiagnosticsManager::Instance();

                // Don't switch levels during PIE
                if (diag.isPIEActive())
                {
                    renderer->getUIManager().showToastMessage("Cannot switch levels during Play-In-Editor.", 3.0f);
                    return;
                }

                const std::string levelName = std::filesystem::path(levelRelPath).stem().string();

                // Capture editor camera before switching
                if (auto* lvl = diag.getActiveLevelSoft())
                {
                    lvl->setEditorCameraPosition(renderer->getCameraPosition());
                    lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees());
                    lvl->setHasEditorCamera(true);
                }

                // The actual load procedure (called after save dialog resolves)
                auto doLoad = [&renderer, levelRelPath, levelName]()
                {
                    auto& logger = Logger::Instance();
                    auto& diag = DiagnosticsManager::Instance();
                    auto& assetMgr = AssetManager::Instance();
                    auto& uiMgr = renderer->getUIManager();

                    // 1) Freeze rendering — last frame stays visible
                    renderer->setRenderFrozen(true);

                    // 2) Show loading progress modal
                    uiMgr.showLevelLoadProgress(levelName);

                    // 3) Clear UndoRedo for old level
                    UndoRedoManager::Instance().clear();

                    // 4) Deselect everything
                    uiMgr.selectEntity(0);
                    renderer->setSelectedEntity(0);

                    // 5) Mark scene unprepared (forces re-prepare on next renderWorld)
                    diag.setScenePrepared(false);

                    // 6) Load the new level asset
                    uiMgr.updateLevelLoadProgress("Loading level asset...");
                    const std::string absPath = assetMgr.getAbsoluteContentPath(levelRelPath);
                    if (absPath.empty())
                    {
                        logger.log(Logger::Category::AssetManagement,
                            "Level load: failed to resolve path: " + levelRelPath, Logger::LogLevel::ERROR);
                        uiMgr.closeLevelLoadProgress();
                        renderer->setRenderFrozen(false);
                        uiMgr.showToastMessage("Failed to load level: path not found.", 3.0f);
                        return;
                    }

                    auto loadResult = assetMgr.loadLevelAsset(absPath);
                    if (!loadResult.success)
                    {
                        logger.log(Logger::Category::AssetManagement,
                            "Level load: failed: " + loadResult.errorMessage, Logger::LogLevel::ERROR);
                        uiMgr.closeLevelLoadProgress();
                        renderer->setRenderFrozen(false);
                        uiMgr.showToastMessage("Failed to load level: " + loadResult.errorMessage, 4.0f);
                        return;
                    }

                    // 7) Restore editor camera if the level has one
                    uiMgr.updateLevelLoadProgress("Restoring editor state...");
                    if (auto* newLevel = diag.getActiveLevelSoft())
                    {
                        // Apply skybox
                        renderer->setSkyboxPath(newLevel->getSkyboxPath());

                        if (newLevel->hasEditorCamera())
                        {
                            renderer->setCameraPosition(newLevel->getEditorCameraPosition());
                            const auto& rot = newLevel->getEditorCameraRotation();
                            renderer->setCameraRotationDegrees(rot.x, rot.y);
                        }
                    }

                    // 8) Unfreeze rendering — renderWorld will detect the new level
                    //    and call prepareActiveLevel + buildRenderablesForSchema
                    uiMgr.updateLevelLoadProgress("Preparing scene...");
                    renderer->setRenderFrozen(false);

                    // 9) Clean up modal and refresh UI
                    uiMgr.closeLevelLoadProgress();
                    uiMgr.refreshWorldOutliner();
                    uiMgr.refreshContentBrowser();
                    uiMgr.refreshStatusBar();
                    uiMgr.markAllWidgetsDirty();

                    logger.log(Logger::Category::Engine,
                        "Level loaded: " + levelName + " (" + levelRelPath + ")",
                        Logger::LogLevel::INFO);
                    uiMgr.showToastMessage("Loaded: " + levelName, 3.0f);
                };

                // Show unsaved changes dialog (or proceed directly if nothing is dirty)
                renderer->getUIManager().showUnsavedChangesDialog(std::move(doLoad));
            });

        // --- Build Game callback (Phase 10) – CMake-based compilation ---
        renderer->getUIManager().setOnBuildGame([&renderer, &assetManager, &diagnostics, &logTimed](const UIManager::BuildGameConfig& config)
            {
                auto& uiMgr = renderer->getUIManager();
                logTimed(Logger::Category::Engine, "Build Game requested. Output: " + config.outputDir, Logger::LogLevel::INFO);

                // Verify CMake is available
                if (!uiMgr.isCMakeAvailable())
                {
                    uiMgr.showToastMessage("CMake is not available. Cannot build game.", 4.0f,
                        UIManager::NotificationLevel::Error);
                    return;
                }

                // Verify a C++ build toolchain is available
                if (!uiMgr.isBuildToolchainAvailable())
                {
                    uiMgr.showToastMessage("No C++ build toolchain (MSVC/Clang) found. Cannot build game.", 4.0f,
                        UIManager::NotificationLevel::Error);
                    return;
                }

                if (uiMgr.isBuildRunning())
                {
                    uiMgr.showToastMessage("A build is already in progress.", 3.0f,
                        UIManager::NotificationLevel::Warning);
                    return;
                }

                uiMgr.showBuildProgress();

                // Capture values needed by the thread (no references to stack locals)
                const std::string cmakePath = uiMgr.getCMakePath();
#if defined(ENGINE_SOURCE_DIR)
                const std::string engineSourceDir = ENGINE_SOURCE_DIR;
#else
                const std::string engineSourceDir;
#endif
                const std::string projectPath = diagnostics.getProjectInfo().projectPath;
                const std::string toolchainName = uiMgr.getBuildToolchain().name;
                const std::string toolchainVersion = uiMgr.getBuildToolchain().version;

                UIManager* uiPtr = &uiMgr;

                uiMgr.m_buildRunning.store(true);

                if (uiMgr.m_buildThread.joinable())
                    uiMgr.m_buildThread.join();

                uiMgr.m_buildThread = std::thread([uiPtr, config, cmakePath, engineSourceDir, projectPath, toolchainName, toolchainVersion]()
                {
                    constexpr int kTotalSteps = 6;
                    int step = 0;
                    bool ok = true;
                    std::string errorMsg;

                    // Log build environment info
                    uiPtr->appendBuildOutput("CMake: " + cmakePath);
                    {
                        std::string tcInfo = "Toolchain: " + toolchainName;
                        if (!toolchainVersion.empty())
                            tcInfo += " " + toolchainVersion;
                        uiPtr->appendBuildOutput(tcInfo);
                    }
                    uiPtr->appendBuildOutput("");

                    // Thread-safe helper to push step progress
                    auto advanceStep = [&](const std::string& status)
                    {
                        ++step;
                        uiPtr->appendBuildOutput("[Step " + std::to_string(step) + "/" + std::to_string(kTotalSteps) + "] " + status);
                        {
                            std::lock_guard<std::mutex> lock(uiPtr->m_buildMutex);
                            uiPtr->m_buildPendingStatus = status;
                            uiPtr->m_buildPendingStep = step;
                            uiPtr->m_buildPendingTotalSteps = kTotalSteps;
                            uiPtr->m_buildPendingStepDirty = true;
                        }
                    };

                    // Check if build was cancelled – sets ok=false and returns true
                    auto checkCancelled = [&]() -> bool
                    {
                        if (uiPtr->m_buildCancelRequested.load())
                        {
                            ok = false;
                            errorMsg = "Build cancelled by user.";
                            uiPtr->appendBuildOutput("[INFO] Build cancelled.");
                            return true;
                        }
                        return false;
                    };

                    // Run a command, capture stdout+stderr line-by-line, return exit code
                    auto runCmdWithOutput = [&](const std::string& cmd) -> int
                    {
#if defined(_WIN32)
                        // Use CreateProcess with CREATE_NO_WINDOW to hide the console.
                        // Redirect stdout+stderr through a pipe.
                        SECURITY_ATTRIBUTES sa{};
                        sa.nLength = sizeof(sa);
                        sa.bInheritHandle = TRUE;
                        sa.lpSecurityDescriptor = nullptr;

                        HANDLE hReadPipe = nullptr;
                        HANDLE hWritePipe = nullptr;
                        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
                        {
                            uiPtr->appendBuildOutput("[ERROR] Failed to create pipe.");
                            return -1;
                        }
                        // Ensure the read handle is NOT inherited
                        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

                        STARTUPINFOA si{};
                        si.cb = sizeof(si);
                        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
                        si.wShowWindow = SW_HIDE;
                        si.hStdOutput = hWritePipe;
                        si.hStdError = hWritePipe;

                        PROCESS_INFORMATION pi{};
                        // cmd /c wraps the command so redirections work
                        std::string fullCmd = "cmd /c \"" + cmd + " 2>&1\"";
                        BOOL created = CreateProcessA(
                            nullptr, fullCmd.data(),
                            nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr,
                            &si, &pi);
                        // Close our copy of the write end so ReadFile will eventually return 0
                        CloseHandle(hWritePipe);

                        if (!created)
                        {
                            CloseHandle(hReadPipe);
                            uiPtr->appendBuildOutput("[ERROR] Failed to start process (CreateProcess).");
                            return -1;
                        }

                        // Read output line by line
                        {
                            char buffer[512];
                            DWORD bytesRead = 0;
                            std::string lineBuffer;
                            while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0)
                            {
                                // Check for cancel request
                                if (uiPtr->m_buildCancelRequested.load())
                                {
                                    TerminateProcess(pi.hProcess, 1);
                                    break;
                                }
                                buffer[bytesRead] = '\0';
                                lineBuffer += buffer;
                                // Split into lines
                                size_t pos;
                                while ((pos = lineBuffer.find('\n')) != std::string::npos)
                                {
                                    std::string line = lineBuffer.substr(0, pos);
                                    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                                        line.pop_back();
                                    if (!line.empty())
                                        uiPtr->appendBuildOutput(line);
                                    lineBuffer.erase(0, pos + 1);
                                }
                            }
                            // Flush remaining
                            while (!lineBuffer.empty() && (lineBuffer.back() == '\r' || lineBuffer.back() == '\n'))
                                lineBuffer.pop_back();
                            if (!lineBuffer.empty())
                                uiPtr->appendBuildOutput(lineBuffer);
                        }
                        CloseHandle(hReadPipe);

                        WaitForSingleObject(pi.hProcess, INFINITE);
                        DWORD exitCode = 0;
                        GetExitCodeProcess(pi.hProcess, &exitCode);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        return static_cast<int>(exitCode);
#else
                        const std::string fullCmd = cmd + " 2>&1";
                        FILE* pipe = popen(fullCmd.c_str(), "r");
                        if (!pipe)
                        {
                            uiPtr->appendBuildOutput("[ERROR] Failed to start process.");
                            return -1;
                        }

                        char buffer[512];
                        while (fgets(buffer, sizeof(buffer), pipe))
                        {
                            std::string line(buffer);
                            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                                line.pop_back();
                            if (!line.empty())
                                uiPtr->appendBuildOutput(line);
                        }

                        int ret = pclose(pipe);
                        return ret;
#endif
                    };

                  try
                  {
                    // Step 1: Create output directory & build directory
                    advanceStep("Creating output directory...");
                    {
                        std::error_code ec;
                        std::filesystem::create_directories(config.outputDir, ec);
                        if (ec)
                        {
                            ok = false;
                            errorMsg = "Failed to create output directory: " + ec.message();
                        }
                    }

                    const std::filesystem::path buildDir = std::filesystem::path(config.outputDir) / "_build";

                    // Step 2: CMake configure
                    if (ok && !checkCancelled())
                    {
                        advanceStep("Configuring CMake build...");

                        if (engineSourceDir.empty())
                        {
                            ok = false;
                            errorMsg = "ENGINE_SOURCE_DIR is not defined. Cannot compile runtime.";
                        }
                        else
                        {
                            std::error_code ec;
                            std::filesystem::create_directories(buildDir, ec);

                            auto escapeForCMake = [](const std::string& s) -> std::string
                            {
                                std::string out;
                                for (char c : s)
                                {
                                    if (c == '\\') out += '/';
                                    else out += c;
                                }
                                return out;
                            };

                            std::string generatorArg;
#if defined(CMAKE_GENERATOR)
                            generatorArg = " -G \"" + std::string(CMAKE_GENERATOR) + "\"";
#endif

                            std::string configCmd = "\"" + cmakePath + "\""
                                " -S \"" + escapeForCMake(engineSourceDir) + "\""
                                " -B \"" + escapeForCMake(buildDir.string()) + "\""
                                + generatorArg +
                                " -DENGINE_BUILD_RUNTIME=ON"
                                " -DENGINE_BUILD_TESTS=OFF"
                                " -DGAME_START_LEVEL=\"" + config.startLevel + "\""
                                " -DGAME_WINDOW_TITLE=\"" + config.windowTitle + "\"";

                            uiPtr->appendBuildOutput("> " + configCmd);
                            int ret = runCmdWithOutput(configCmd);
                            if (ret != 0)
                            {
                                ok = false;
                                errorMsg = "CMake configure failed (exit code " + std::to_string(ret) + ")";
                                uiPtr->appendBuildOutput("[ERROR] " + errorMsg);
                            }
                        }
                    }

                    // Step 3: CMake build
                    if (ok && !checkCancelled())
                    {
                        advanceStep("Compiling game runtime...");

                        std::string buildCmd = "\"" + cmakePath + "\""
                            " --build \"" + buildDir.string() + "\""
                            " --target HorizonEngineRuntime"
                            " --config Release";

                        uiPtr->appendBuildOutput("> " + buildCmd);
                        int ret = runCmdWithOutput(buildCmd);
                        if (ret != 0)
                        {
                            ok = false;
                            errorMsg = "CMake build failed (exit code " + std::to_string(ret) + ")";
                            uiPtr->appendBuildOutput("[ERROR] " + errorMsg);
                        }
                    }

                    // Step 4: Deploy exe + DLLs
                    if (ok && !checkCancelled())
                    {
                        advanceStep("Deploying runtime...");

                        const std::filesystem::path builtExe = buildDir / "Release" / "HorizonEngineRuntime.exe";
                        const std::filesystem::path dstExe = std::filesystem::path(config.outputDir) / (config.windowTitle + ".exe");
                        const std::filesystem::path builtDir = buildDir / "Release";

                        if (std::filesystem::exists(builtExe))
                        {
                            std::error_code ec;
                            std::filesystem::copy_file(builtExe, dstExe,
                                std::filesystem::copy_options::overwrite_existing, ec);
                            if (ec)
                            {
                                ok = false;
                                errorMsg = "Failed to copy runtime exe: " + ec.message();
                            }

                            if (ok && std::filesystem::exists(builtDir))
                            {
                                for (const auto& entry : std::filesystem::directory_iterator(builtDir))
                                {
                                    if (!entry.is_regular_file()) continue;
                                    const auto ext = entry.path().extension().string();
                                    if (ext == ".dll" || ext == ".DLL")
                                    {
                                        std::error_code copyEc;
                                        std::filesystem::copy_file(entry.path(),
                                            std::filesystem::path(config.outputDir) / entry.path().filename(),
                                            std::filesystem::copy_options::overwrite_existing, copyEc);
                                    }
                                }
                            }

                            // Copy Python runtime (DLL + zip) from engine directory
                            if (ok && !engineSourceDir.empty())
                            {
                                // Check engine build dir first, then engine base dir
                                std::vector<std::filesystem::path> searchDirs;
                                searchDirs.push_back(builtDir);
                                const char* bp = SDL_GetBasePath();
                                if (bp) searchDirs.push_back(std::filesystem::path(bp));
                                searchDirs.push_back(std::filesystem::path(engineSourceDir));

                                for (const auto& searchDir : searchDirs)
                                {
                                    if (!std::filesystem::exists(searchDir)) continue;
                                    for (const auto& entry : std::filesystem::directory_iterator(searchDir))
                                    {
                                        if (!entry.is_regular_file()) continue;
                                        const auto fname = entry.path().filename().string();
                                        const auto ext = entry.path().extension().string();
                                        // Match python3*.dll and python3*.zip
                                        if ((ext == ".dll" || ext == ".zip") && fname.size() >= 8 && fname.substr(0, 6) == "python")
                                        {
                                            const auto dst = std::filesystem::path(config.outputDir) / entry.path().filename();
                                            if (!std::filesystem::exists(dst))
                                            {
                                                std::error_code copyEc;
                                                std::filesystem::copy_file(entry.path(), dst,
                                                    std::filesystem::copy_options::overwrite_existing, copyEc);
                                                if (!copyEc)
                                                    uiPtr->appendBuildOutput("  Copied Python file: " + fname);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            ok = false;
                            errorMsg = "Built runtime not found at: " + builtExe.string();
                        }
                    }

                    // Step 5: Copy Content + shaders + asset registry
                    if (ok && !checkCancelled())
                    {
                        advanceStep("Copying game content...");

                        const std::filesystem::path dstDir = config.outputDir;

                        const std::filesystem::path srcContent = std::filesystem::path(projectPath) / "Content";
                        const std::filesystem::path dstContent = dstDir / "Content";
                        if (std::filesystem::exists(srcContent))
                        {
                            std::error_code ec;
                            std::filesystem::copy(srcContent, dstContent,
                                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                            if (ec)
                            {
                                ok = false;
                                errorMsg = "Failed to copy Content: " + ec.message();
                            }
                        }

                        if (ok)
                        {
                            const std::filesystem::path srcShaders = std::filesystem::path(engineSourceDir) / "src" / "Renderer" / "OpenGLRenderer" / "shaders";
                            const std::filesystem::path dstShaders = dstDir / "shaders";
                            if (std::filesystem::exists(srcShaders))
                            {
                                std::error_code ec;
                                std::filesystem::copy(srcShaders, dstShaders,
                                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                            }
                        }

                        if (ok)
                        {
                            const std::filesystem::path srcEngContent = std::filesystem::path(engineSourceDir) / "Content";
                            const std::filesystem::path dstEngContent = dstDir / "Content";
                            if (std::filesystem::exists(srcEngContent))
                            {
                                std::error_code ec;
                                std::filesystem::copy(srcEngContent, dstEngContent,
                                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::skip_existing, ec);
                            }
                        }

                        if (ok)
                        {
                            const std::filesystem::path dstConfig = dstDir / "Config";
                            std::error_code ec;
                            std::filesystem::create_directories(dstConfig, ec);
                            const std::filesystem::path srcReg = std::filesystem::path(projectPath) / "Config" / "AssetRegistry.bin";
                            if (std::filesystem::exists(srcReg))
                            {
                                std::filesystem::copy_file(srcReg, dstConfig / "AssetRegistry.bin",
                                    std::filesystem::copy_options::overwrite_existing, ec);
                            }
                        }
                    }

                    // Step 6: Done
                    advanceStep(ok ? "Build complete!" : "Build failed.");

                    if (ok)
                    {
                        uiPtr->appendBuildOutput("Build completed successfully. Output: " + config.outputDir);

                        // Clean up build intermediates
                        {
                            std::error_code ec;
                            std::filesystem::remove_all(std::filesystem::path(config.outputDir) / "_build", ec);
                        }

                        // Launch if requested
                        if (config.launchAfterBuild)
                        {
                            const std::filesystem::path exePath =
                                std::filesystem::path(config.outputDir) / (config.windowTitle + ".exe");
                            if (std::filesystem::exists(exePath))
                            {
                                uiPtr->appendBuildOutput("Launching: " + exePath.string());
#if defined(_WIN32)
                                ShellExecuteA(nullptr, "open", exePath.string().c_str(),
                                    nullptr, config.outputDir.c_str(), SW_SHOWNORMAL);
#else
                                std::string cmd = "\"" + exePath.string() + "\" &";
                                std::system(cmd.c_str());
#endif
                            }
                        }
                    }
                    else
                    {
                        uiPtr->appendBuildOutput("Build failed: " + errorMsg);
                    }

                  }
                  catch (const std::exception& e)
                  {
                      ok = false;
                      errorMsg = std::string("Build crashed: ") + e.what();
                      uiPtr->appendBuildOutput("[FATAL] " + errorMsg);
                  }
                  catch (...)
                  {
                      ok = false;
                      errorMsg = "Build crashed: unknown exception";
                      uiPtr->appendBuildOutput("[FATAL] " + errorMsg);
                  }

                  // Signal completion to the main thread
                  {
                      std::lock_guard<std::mutex> lock(uiPtr->m_buildMutex);
                      uiPtr->m_buildPendingFinished = true;
                      uiPtr->m_buildPendingSuccess = ok;
                      uiPtr->m_buildPendingErrorMsg = errorMsg;
                  }
                }); // end std::thread
            });

    }
#endif // ENGINE_EDITOR  (Phase 3: editor UI setup)

    // --- Runtime mode: load start level and enter game mode ---
    if (isRuntimeMode && renderer && !rtStartLevel.empty())
    {
        logTimed(Logger::Category::Engine, "Runtime mode: loading start level: " + rtStartLevel, Logger::LogLevel::INFO);

        const std::string absLevelPath = assetManager.getAbsoluteContentPath(rtStartLevel);
        if (!absLevelPath.empty())
        {
            auto loadResult = assetManager.loadLevelAsset(absLevelPath);
            if (loadResult.success)
            {
                diagnostics.setScenePrepared(false);
                // Activate PIE-like mode so scripts run
                diagnostics.setPIEActive(true);
                logTimed(Logger::Category::Engine, "Runtime mode: level loaded successfully.", Logger::LogLevel::INFO);
            }
            else
            {
                logTimed(Logger::Category::Engine, "Runtime mode: failed to load level – " + loadResult.errorMessage, Logger::LogLevel::ERROR);
            }
        }
        else
        {
            logTimed(Logger::Category::Engine, "Runtime mode: could not resolve level path: " + rtStartLevel, Logger::LogLevel::ERROR);
        }
    }

    // All subsystems initialised
    // still visible so the main window never appears white / empty.
    if (renderer)
    {
#if ENGINE_EDITOR
        if (!isRuntimeMode)
        {
            // Run the full DPI rebuild at startup so that every widget element
            // picks up the correct DPI-scaled sizes, theme colours, and dynamic
            // content.  This is the same path that runs when the user changes
            // the DPI slider at runtime, and it guarantees consistency:
            //   1) regenerate widget JSON assets (ensures _dpiScale matches)
            //   2) reload all element trees from JSON (sizes, padding, fontSize)
            //   3) apply theme colours to all elements
            //   4) re-populate dynamic widgets (Outliner, Details, ContentBrowser)
            renderer->getUIManager().rebuildEditorUIForDpi(EditorTheme::Get().dpiScale);

            // Ensure all loaded widgets are marked dirty so the UI FBO is fully redrawn.
            renderer->getUIManager().markAllWidgetsDirty();

            renderer->getUIManager().showToastMessage("Engine ready!", 3.0f);

            // --- CMake detection (needed for Build Game) ---
            {
                auto& uiMgr = renderer->getUIManager();
                if (uiMgr.detectCMake())
                {
                    logTimed(Logger::Category::Engine,
                        "CMake found: " + uiMgr.getCMakePath(),
                        Logger::LogLevel::INFO);
                }
                else
                {
                    logTimed(Logger::Category::Engine,
                        "CMake not found – Build Game will not be available.",
                        Logger::LogLevel::WARNING);
                    uiMgr.showCMakeInstallPrompt();
                }

                // --- Build toolchain detection (MSVC / Clang) ---
                if (uiMgr.detectBuildToolchain())
                {
                    const auto& tc = uiMgr.getBuildToolchain();
                    std::string info = "Build toolchain: " + tc.name;
                    if (!tc.version.empty())
                        info += " " + tc.version;
                    if (!tc.compilerPath.empty())
                        info += " (" + tc.compilerPath + ")";
                    logTimed(Logger::Category::Engine, info, Logger::LogLevel::INFO);
                }
                else
                {
                    logTimed(Logger::Category::Engine,
                        "No C++ build toolchain found – Build Game will not be available.",
                        Logger::LogLevel::WARNING);
                    if (uiMgr.isCMakeAvailable())
                        uiMgr.showToolchainInstallPrompt();
                }
            }
        }
#endif // ENGINE_EDITOR
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                continue;
        }
        if (!isRuntimeMode)
            renderer->getUIManager().updateNotifications(0.016f);
        renderer->render();
        renderer->present();
    }

    // Now that the framebuffer has real content, show the main window FIRST
    // so that SDL doesn't think the application is closing when we destroy the splash.
    if (auto* w = renderer->window())
    {
        SDL_ShowWindow(w);
        SDL_RaiseWindow(w);
    }
    if (splash->isOpen())
    {
        splash->close();
    }

    // Ensure no stale shutdown flag from intermediate event pumps.
    if (diagnostics.isShutdownRequested())
    {
        logTimed(Logger::Category::Engine,
            "Clearing stale shutdown request before main loop (caused by popup lifecycle events).",
            Logger::LogLevel::WARNING);
    }

    // Reset shutdown flag â€“ only TitleBar.Close and SDL_EVENT_QUIT inside the
    // main loop should be able to shut the engine down from this point on.
    diagnostics.resetShutdownRequest();

    bool running = true;
    uint64_t frame = 0;
    logTimed(Logger::Category::Engine, "Entering main loop.", Logger::LogLevel::INFO);

    bool rightMouseDown = false;
    bool texViewerPanning = false;
    Vec2 mousePosPixels{};
    bool hasMousePos = false;
    bool isOverUI = false;

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    double fpsTimer = 0.0;
    uint32_t fpsFrames = 0;
    double fpsValue = 0.0;

    double metricsUpdateTimer = 0.0;
    constexpr double kMetricsUpdateIntervalSec = 0.25;
    bool metricsUpdatePending = true;
    std::string fpsText = "FPS: 0";
    std::string speedText = "Speed: x1.0";
    std::string cpuText;
    std::string renderText;
    std::string uiText;
    std::string inputText;
    std::string otherText;
    std::string gcText;
    std::string ecsText;
    std::string frameText;
    std::string occlusionText;

    uint64_t lastGcCounter = lastCounter;
    constexpr double kGcIntervalSec = 60.0;
    uint64_t gcRuns = 0;

    bool fpscap = true;
    double cpuInputMs = 0.0;
    double cpuEventMs = 0.0;
    double cpuRenderMs = 0.0;
    double cpuOtherMs = 0.0;
    double cpuLoggerMs = 0.0;
    double cpuGcMs = 0.0;

#if ENGINE_EDITOR
    // ─── Register all keyboard shortcuts with ShortcutManager ───
    {
        auto& sm = ShortcutManager::Instance();
        using Phase = ShortcutManager::Phase;
        using Mod = ShortcutManager::Mod;

        // --- KeyDown shortcuts (Ctrl+combos) ---

        sm.registerAction("Editor.Undo", "Undo", "Editor",
            { SDLK_Z, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                auto& undo = UndoRedoManager::Instance();
                if (!undo.canUndo()) return false;
                undo.undo();
                renderer->getUIManager().markAllWidgetsDirty();
                return true;
            });

        sm.registerAction("Editor.Redo", "Redo", "Editor",
            { SDLK_Y, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                auto& undo = UndoRedoManager::Instance();
                if (!undo.canRedo()) return false;
                undo.redo();
                renderer->getUIManager().markAllWidgetsDirty();
                return true;
            });

        sm.registerAction("Editor.Save", "Save", "Editor",
            { SDLK_S, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                auto* lvl = diagnostics.getActiveLevelSoft();
                if (lvl)
                {
                    lvl->setEditorCameraPosition(renderer->getCameraPosition());
                    lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees());
                    lvl->setHasEditorCamera(true);
                }
                auto& am = AssetManager::Instance();
                if (am.getUnsavedAssetCount() > 0)
                    renderer->getUIManager().showUnsavedChangesDialog(nullptr);
                else
                    renderer->getUIManager().showToastMessage("Nothing to save.", 2.0f);
                return true;
            });

        sm.registerAction("Editor.SearchContentBrowser", "Search Content Browser", "Editor",
            { SDLK_F, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->getUIManager().focusContentBrowserSearch();
                return true;
            });

        sm.registerAction("Editor.CopyEntity", "Copy Entity", "Editor",
            { SDLK_C, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->getUIManager().copySelectedEntity();
                return true;
            });

        sm.registerAction("Editor.PasteEntity", "Paste Entity", "Editor",
            { SDLK_V, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->getUIManager().pasteEntity();
                return true;
            });

        sm.registerAction("Editor.DuplicateEntity", "Duplicate Entity", "Editor",
            { SDLK_D, Mod::Ctrl }, Phase::KeyDown,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->getUIManager().duplicateSelectedEntity();
                return true;
            });

        // --- KeyUp shortcuts ---

        sm.registerAction("Editor.ToggleUIDebug", "Toggle UI Debug", "Debug",
            { SDLK_F11, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer) return false;
                renderer->toggleUIDebug();
                logTimed(Logger::Category::Input,
                    std::string("UI debug bounds: ") + (renderer->isUIDebugEnabled() ? "ON" : "OFF"),
                    Logger::LogLevel::INFO);
                return true;
            });

        sm.registerAction("Editor.ToggleBoundsDebug", "Toggle Bounds Debug", "Debug",
            { SDLK_F8, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer) return false;
                renderer->toggleBoundsDebug();
                logTimed(Logger::Category::Input,
                    std::string("Bounds debug boxes: ") + (renderer->isBoundsDebugEnabled() ? "ON" : "OFF"),
                    Logger::LogLevel::INFO);
                return true;
            });

        sm.registerAction("Editor.ToggleMetrics", "Toggle Metrics", "Debug",
            { SDLK_F10, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                showMetrics = !showMetrics;
                return true;
            });

        sm.registerAction("Editor.ToggleOcclusionStats", "Toggle Occlusion Stats", "Debug",
            { SDLK_F9, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                showOcclusionStats = !showOcclusionStats;
                return true;
            });

        sm.registerAction("PIE.Stop", "Stop PIE", "PIE",
            { SDLK_ESCAPE, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!diagnostics.isPIEActive() || !stopPIE) return false;
                stopPIE();
                return true;
            });

        sm.registerAction("PIE.ToggleInput", "Toggle PIE Input", "PIE",
            { SDLK_F1, Mod::Shift }, Phase::KeyUp,
            [&]() -> bool {
                if (!diagnostics.isPIEActive()) return false;
                if (pieMouseCaptured && !pieInputPaused)
                {
                    pieInputPaused = true;
                    if (auto* w = renderer->window())
                    {
                        SDL_SetWindowRelativeMouseMode(w, false);
                        SDL_SetWindowMouseGrab(w, false);
                        SDL_WarpMouseInWindow(w, preCaptureMouseX, preCaptureMouseY);
                    }
                    SDL_ShowCursor();
                    logTimed(Logger::Category::Input, "PIE: input paused (Shift+F1), mouse released.", Logger::LogLevel::INFO);
                }
                return true;
            });

        sm.registerAction("Gizmo.Translate", "Translate Mode", "Gizmo",
            { SDLK_W, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || rightMouseDown || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->setGizmoMode(Renderer::GizmoMode::Translate);
                return true;
            });

        sm.registerAction("Gizmo.Rotate", "Rotate Mode", "Gizmo",
            { SDLK_E, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || rightMouseDown || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->setGizmoMode(Renderer::GizmoMode::Rotate);
                return true;
            });

        sm.registerAction("Gizmo.Scale", "Scale Mode", "Gizmo",
            { SDLK_R, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || rightMouseDown || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->setGizmoMode(Renderer::GizmoMode::Scale);
                return true;
            });

        sm.registerAction("Editor.FocusSelected", "Focus Selected", "Editor",
            { SDLK_F, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || rightMouseDown || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->focusOnSelectedEntity();
                return true;
            });

        sm.registerAction("Editor.DropToSurface", "Drop to Surface", "Editor",
            { SDLK_END, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer || diagnostics.isPIEActive() || rightMouseDown || renderer->getUIManager().hasEntryFocused()) return false;
                renderer->getUIManager().dropSelectedEntitiesToSurface([](float ox, float oy, float oz) -> std::pair<bool, float>
                {
                    auto hit = PhysicsWorld::Instance().raycast(ox, oy, oz, 0.0f, -1.0f, 0.0f, 10000.0f);
                    return { hit.hit, hit.point[1] };
                });
                return true;
            });

        sm.registerAction("Editor.ToggleFPSCap", "Toggle FPS Cap", "Debug",
            { SDLK_F12, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                fpscap = !fpscap;
                return true;
            });

        sm.registerAction("Editor.ShortcutHelp", "Shortcut Help", "Editor",
            { SDLK_F1, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer) return false;
                if (renderer->getUIManager().hasEntryFocused()) return false;
                renderer->getUIManager().openShortcutHelpPopup();
                return true;
            });

        sm.registerAction("Editor.ImportDialog", "Open Import Dialog", "Editor",
            { SDLK_F2, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (renderer && renderer->getUIManager().hasEntryFocused()) return false;
                logTimed(Logger::Category::Input, "F2 pressed - opening import dialog.", Logger::LogLevel::INFO);
                assetManager.OpenImportDialog(renderer ? renderer->window() : nullptr, AssetType::Unknown, AssetManager::Async);
                return true;
            });

        sm.registerAction("Editor.Delete", "Delete", "Editor",
            { SDLK_DELETE, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                if (!renderer) return false;
                if (renderer->getUIManager().hasEntryFocused()) return false;
                auto& uiManager = renderer->getUIManager();

                if (uiManager.tryDeleteWidgetEditorElement())
                    return true;

                const std::string selectedAsset = uiManager.getSelectedGridAsset();
                if (!selectedAsset.empty())
                {
                    const auto refs = AssetManager::Instance().findReferencesTo(selectedAsset);
                    std::string msg = "Are you sure you want to delete this asset?\nThis cannot be undone.";
                    if (!refs.empty())
                    {
                        msg = "This asset is referenced by " + std::to_string(refs.size())
                            + " other asset(s)/entity(ies).\nDeleting it may break those references.\n\nDelete anyway?";
                    }
                    uiManager.showConfirmDialog(
                        msg,
                        [&renderer, selectedAsset]()
                        {
                            auto& assetMgr = AssetManager::Instance();
                            auto& uiMgr = renderer->getUIManager();
                            if (assetMgr.deleteAsset(selectedAsset, true))
                            {
                                const std::string name = std::filesystem::path(selectedAsset).stem().string();
                                uiMgr.clearSelectedGridAsset();
                                uiMgr.refreshContentBrowser();
                                uiMgr.showToastMessage("Deleted: " + name, 3.0f);
                            }
                            else
                            {
                                uiMgr.showToastMessage("Failed to delete asset.", 3.0f);
                            }
                        });
                    return true;
                }

                const auto& selectedEntities = renderer->getSelectedEntities();
                if (selectedEntities.empty()) return false;

                auto& ecs = ECS::ECSManager::Instance();
                auto* level = diagnostics.getActiveLevelSoft();

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
                    std::optional<ECS::ScriptComponent>          script;
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
                    snap.script      = ecs.hasComponent<ECS::ScriptComponent>(entity)      ? std::make_optional(*ecs.getComponent<ECS::ScriptComponent>(entity))      : std::nullopt;
                    snap.collision   = ecs.hasComponent<ECS::CollisionComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::CollisionComponent>(entity))   : std::nullopt;
                    snap.heightField = ecs.hasComponent<ECS::HeightFieldComponent>(entity) ? std::make_optional(*ecs.getComponent<ECS::HeightFieldComponent>(entity)) : std::nullopt;
                    snapshots.push_back(std::move(snap));
                }

                for (const auto& snap : snapshots)
                {
                    if (level) level->onEntityRemoved(snap.entity);
                    ecs.removeEntity(snap.entity);
                }

                uiManager.selectEntity(0);
                renderer->clearSelection();
                uiManager.refreshWorldOutliner();

                std::string toastMsg = (snapshots.size() == 1)
                    ? ("Deleted: " + snapshots[0].name)
                    : ("Deleted " + std::to_string(snapshots.size()) + " entities");
                uiManager.showToastMessage(toastMsg, 2.5f);

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
                            if (snap.script)      e.addComponent<ECS::ScriptComponent>(snap.entity, *snap.script);
                            if (snap.collision)   e.addComponent<ECS::CollisionComponent>(snap.entity, *snap.collision);
                            if (snap.heightField) e.addComponent<ECS::HeightFieldComponent>(snap.entity, *snap.heightField);
                            if (lvl) lvl->onEntityAdded(snap.entity);
                        }
                    };
                UndoRedoManager::Instance().pushCommand(std::move(cmd));
                return true;
            });

        logTimed(Logger::Category::Engine, "Registered " + std::to_string(sm.getActions().size()) + " keyboard shortcuts.", Logger::LogLevel::INFO);

        // Load user shortcut overrides from project config
        {
            const auto& projPath = diagnostics.getProjectInfo().projectPath;
            if (!projPath.empty())
            {
                const std::string shortcutFile = (std::filesystem::path(projPath) / "shortcuts.cfg").string();
                if (std::filesystem::exists(shortcutFile))
                {
                    sm.loadFromFile(shortcutFile);
                    logTimed(Logger::Category::Engine, "Loaded shortcut overrides from " + shortcutFile, Logger::LogLevel::INFO);
                }
            }
        }
    }
#endif // ENGINE_EDITOR (keyboard shortcuts)

    while (running)
    {
        const uint64_t frameStartCounter = SDL_GetPerformanceCounter();
        ++frame;

        // Poll the build thread for pending UI updates (non-blocking)
        renderer->getUIManager().pollBuildThread();

        const uint64_t now = SDL_GetPerformanceCounter();
        const double dt = (freq > 0.0) ? (static_cast<double>(now - lastCounter) / freq) : 0.016;
        lastCounter = now;

        fpsTimer += dt;
        ++fpsFrames;
        if (fpsTimer >= 1.0)
        {
            fpsValue = static_cast<double>(fpsFrames) / fpsTimer;
            fpsFrames = 0;
            fpsTimer = 0.0;
        }

        metricsUpdateTimer += dt;
        if (metricsUpdateTimer >= kMetricsUpdateIntervalSec)
        {
            metricsUpdateTimer = 0.0;
            metricsUpdatePending = true;
        }

        audioManager.update();

        cpuLoggerMs = 0.0;
        cpuGcMs = 0.0;

        if (freq > 0.0 && (static_cast<double>(now - lastGcCounter) / freq) >= kGcIntervalSec)
        {
            const uint64_t gcStart = SDL_GetPerformanceCounter();
            assetManager.collectGarbage();
            const uint64_t gcEnd = SDL_GetPerformanceCounter();
            cpuGcMs = (freq > 0.0) ? (static_cast<double>(gcEnd - gcStart) / freq * 1000.0) : 0.0;
            lastGcCounter = now;

			++gcRuns;
            if ((gcRuns % 12) == 0)
            {
				logTimed(Logger::Category::AssetManagement, "Periodic GC runs=" + std::to_string(gcRuns), Logger::LogLevel::INFO);
            }
        }

        const uint64_t inputStartCounter = SDL_GetPerformanceCounter();

        // Basic movement (camera-relative)
        const float moveSpeed = static_cast<float>(3.0 * dt * cameraSpeedMultiplier); // units/sec
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys)
        {
            const bool inPIE = diagnostics.isPIEActive();
            const bool laptopMode = [&]() {
                if (auto v = diagnostics.getState("LaptopMode")) return *v == "1";
                return false;
            }();
            // In PIE (and not paused): always move. Outside PIE: require right-click (unless laptop mode).
            const bool canMove = (inPIE && pieMouseCaptured && !pieInputPaused) || (!inPIE && (rightMouseDown || laptopMode));
            if (canMove)
            {
                if (keys[SDL_SCANCODE_W]) renderer->moveCamera(+moveSpeed, 0.0f, 0.0f);
                if (keys[SDL_SCANCODE_S]) renderer->moveCamera(-moveSpeed, 0.0f, 0.0f);
                if (keys[SDL_SCANCODE_A]) renderer->moveCamera(0.0f, -moveSpeed, 0.0f);
                if (keys[SDL_SCANCODE_D]) renderer->moveCamera(0.0f, +moveSpeed, 0.0f);
                if (keys[SDL_SCANCODE_Q]) renderer->moveCamera(0.0f, 0.0f, -moveSpeed);
                if (keys[SDL_SCANCODE_E]) renderer->moveCamera(0.0f, 0.0f, +moveSpeed);
            }
        }

        if (!hasMousePos)
        {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            SDL_GetMouseState(&mouseX, &mouseY);
            mousePosPixels = Vec2{ mouseX, mouseY };
            hasMousePos = true;
            if (renderer && !(diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused))
            {
                auto& uiManager = renderer->getUIManager();
                uiManager.setMousePosition(mousePosPixels);
                if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                {
                    viewportUI->setMousePosition(mousePosPixels);
                    isOverUI = uiManager.isPointerOverUI(mousePosPixels) || viewportUI->isPointerOverViewportUI(mousePosPixels);
                }
                else
                {
                    isOverUI = uiManager.isPointerOverUI(mousePosPixels);
                }
            }
        }

        const uint64_t eventStartCounter = SDL_GetPerformanceCounter();
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			// Route events belonging to popup windows first.
			if (renderer && renderer->routeEventToPopup(event))
			{
				continue;
			}

			if (event.type == SDL_EVENT_QUIT)
			{
				logTimed(Logger::Category::Input, "SDL_EVENT_QUIT received.", Logger::LogLevel::INFO);
				running = false;
			}

            if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (renderer)
                {
                    mousePosPixels = Vec2{ event.motion.x, event.motion.y };
                    hasMousePos = true;

                    // During active PIE capture, skip UI updates so editor stays inert (no hover effects)
                    if (!(diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused))
                    {
                        auto& uiManager = renderer->getUIManager();
                        uiManager.setMousePosition(mousePosPixels);
                        uiManager.handleMouseMotion(mousePosPixels);
                        if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                        {
                            viewportUI->setMousePosition(mousePosPixels);
                            viewportUI->handleMouseMove(mousePosPixels);
                            isOverUI = uiManager.isPointerOverUI(mousePosPixels) || viewportUI->isPointerOverViewportUI(mousePosPixels);
                        }
                        else
                        {
                            isOverUI = uiManager.isPointerOverUI(mousePosPixels);
                        }

                        // Update gizmo drag if active
                        if (renderer->isGizmoDragging())
                        {
                            renderer->updateGizmoDrag(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y));
                        }

                        // Update rubber-band rectangle if active
                        if (renderer->isRubberBandActive())
                        {
                            renderer->updateRubberBand(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y));
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                // During active PIE capture, ignore left-click so editor UI stays inert
                if (diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused)
                    continue;

                if (renderer)
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    mousePosPixels = mousePos;
                    hasMousePos = true;
                    auto& uiManager = renderer->getUIManager();
                    uiManager.setMousePosition(mousePos);
                    ViewportUIManager* viewportUI = renderer->getViewportUIManagerPtr();
                    if (viewportUI)
                    {
                        viewportUI->setMousePosition(mousePos);
                    }
                    const bool overEditorUI = uiManager.isPointerOverUI(mousePos);
                    const bool overViewportUI = viewportUI ? viewportUI->isPointerOverViewportUI(mousePos) : false;
                    isOverUI = overEditorUI || overViewportUI;
                    if (uiManager.handleMouseDown(mousePos, event.button.button))
                    {
                        continue;
                    }
                    if (viewportUI && viewportUI->handleMouseDown(mousePos, event.button.button))
                    {
                        isOverUI = true;
                        continue;
                    }
                    if (!isOverUI)
                    {
                        // Texture viewer: left-click starts panning in laptop mode
                        const bool laptopModeLocal = [&]() {
                            if (auto v = diagnostics.getState("LaptopMode")) return *v == "1";
                            return false;
                        }();
                        if (laptopModeLocal)
                        {
                            auto* texViewer = renderer->getTextureViewer(renderer->getActiveTabId());
                            if (texViewer)
                            {
                                texViewerPanning = true;
                                continue;
                            }
                        }

                        // PIE: recapture mouse when clicking on viewport while input is paused
                        if (diagnostics.isPIEActive() && pieInputPaused)
                        {
                            pieInputPaused = false;
                            SDL_GetMouseState(&preCaptureMouseX, &preCaptureMouseY);
                            if (auto* w = renderer->window())
                            {
                                SDL_SetWindowRelativeMouseMode(w, true);
                                SDL_SetWindowMouseGrab(w, true);
                            }
                            SDL_HideCursor();
                            logTimed(Logger::Category::Input, "PIE: input resumed (viewport click), mouse captured.", Logger::LogLevel::INFO);
                            continue;
                        }
                        // Multi-viewport: set active sub-viewport on click
                        if (renderer->getSubViewportCount() > 1)
                        {
                            const int hitSV = renderer->subViewportHitTest(
                                static_cast<int>(event.button.x), static_cast<int>(event.button.y));
                            if (hitSV >= 0)
                                renderer->setActiveSubViewport(hitSV);
                        }
                        // Try gizmo interaction first; only pick/rubber-band if gizmo not hit
                        if (!renderer->beginGizmoDrag(static_cast<int>(event.button.x), static_cast<int>(event.button.y)))
                        {
                            // Start rubber-band selection; resolved on mouse-up
                            renderer->beginRubberBand(static_cast<int>(event.button.x), static_cast<int>(event.button.y));
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                // End texture viewer panning (laptop mode left-click)
                if (texViewerPanning)
                {
                    texViewerPanning = false;
                    continue;
                }

                if (diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused)
                    continue;

                if (renderer)
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    auto& uiManager = renderer->getUIManager();
                    ViewportUIManager* viewportUI = renderer->getViewportUIManagerPtr();
                    if (viewportUI)
                    {
                        viewportUI->setMousePosition(mousePos);
                    }
                    if (uiManager.isDragging())
                    {
                        uiManager.handleMouseUp(mousePos, event.button.button);
                    }
                    else
                    {
                        // Always forward mouse-up so deferred clicks on draggable elements fire
                        uiManager.handleMouseUp(mousePos, event.button.button);
                        if (viewportUI && viewportUI->handleMouseUp(mousePos, event.button.button))
                        {
                            if (renderer->isRubberBandActive())
                                renderer->cancelRubberBand();
                            continue;
                        }
                        if (renderer->isRubberBandActive())
                        {
                            const bool ctrlHeld = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
                            // endRubberBand resolves the marquee selection when the rect is large enough;
                            // otherwise we fall back to a single-pixel pick.
                            const float dx = std::abs(event.button.x - renderer->getRubberBandStart().x);
                            const float dy = std::abs(event.button.y - renderer->getRubberBandStart().y);
                            renderer->endRubberBand(ctrlHeld);
                            if (dx <= 4.0f || dy <= 4.0f)
                            {
                                renderer->requestPick(static_cast<int>(event.button.x), static_cast<int>(event.button.y), ctrlHeld);
                            }
                        }
                        else if (renderer->isGizmoDragging())
                        {
                            renderer->endGizmoDrag();
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                if (diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused)
                    continue;

                if (renderer)
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    mousePosPixels = mousePos;
                    hasMousePos = true;
                    auto& uiManager = renderer->getUIManager();
                    uiManager.setMousePosition(mousePos);
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        viewportUI->setMousePosition(mousePos);
                        isOverUI = uiManager.isPointerOverUI(mousePos) || viewportUI->isPointerOverViewportUI(mousePos);
                    }
                    else
                    {
                        isOverUI = uiManager.isPointerOverUI(mousePos);
                    }

                    // Widget editor: right-click starts panning
                    if (uiManager.handleRightMouseDown(mousePos))
                    {
                        continue;
                    }

                    // Right-click context menu on Content Browser grid
                    if (isOverUI && uiManager.isOverContentBrowserGrid(mousePos))
                    {
                        if (uiManager.isDropdownMenuOpen())
                        {
                            uiManager.closeDropdownMenu();
                        }

                        std::vector<UIManager::DropdownMenuItem> items;

                        items.push_back({ "New Folder", [&renderer]()
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
                                {
                                    folderName = baseName + std::to_string(counter++);
                                }

                                std::filesystem::create_directory(targetDir / folderName, ec);
                                if (!ec)
                                {
                                    uiMgr.refreshContentBrowser();
                                    uiMgr.showToastMessage("Created folder: " + folderName, 3.0f);
                                }
                                else
                                {
                                    uiMgr.showToastMessage("Failed to create folder.", 3.0f);
                                }
                            }});

                        items.push_back({ "", {}, true });

                        items.push_back({ "New Script", [&renderer]()
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

                                // Find a unique name
                                std::string baseName = "NewScript";
                                std::string fileName = baseName + ".py";
                                int counter = 1;
                                while (std::filesystem::exists(targetDir / fileName))
                                {
                                    fileName = baseName + std::to_string(counter++) + ".py";
                                }

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

                                    // Register in asset registry
                                    const std::string relPath = std::filesystem::relative(filePath, contentDir).generic_string();
                                    AssetRegistryEntry entry;
                                    entry.name = std::filesystem::path(fileName).stem().string();
                                    entry.path = relPath;
                                    entry.type = AssetType::Script;
                                    assetMgr.registerAssetInRegistry(entry);
                                    uiMgr.refreshContentBrowser();
                                    uiMgr.showToastMessage("Created: " + fileName, 3.0f);
                                }
                            }});

                        items.push_back({ "New Level", [&renderer]()
                            {
                                auto& uiMgr = renderer->getUIManager();
                                const auto& folder = uiMgr.getSelectedBrowserFolder();
                                if (folder == "__Shaders__") return;

                                constexpr float kBaseW = 360.0f;
                                constexpr float kBaseH = 180.0f;
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
                                };
                                auto state = std::make_shared<LevelState>();
                                state->folder = folder;

                                std::vector<WidgetElement> elements;

                                // Background
                                {
                                    WidgetElement bg;
                                    bg.type = WidgetElementType::Panel;
                                    bg.id = "NL.Bg";
                                    bg.from = Vec2{ 0.0f, 0.0f };
                                    bg.to = Vec2{ 1.0f, 1.0f };
                                    bg.style.color = EditorTheme::Get().panelBackground;
                                    elements.push_back(bg);
                                }

                                // Title
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

                                // Form layout
                                WidgetElement formStack;
                                formStack.type = WidgetElementType::StackPanel;
                                formStack.id = "NL.Form";
                                formStack.from = Vec2{ nx(16.0f), ny(44.0f) };
                                formStack.to = Vec2{ nx(W - 16.0f), ny(H - 50.0f) };
                                formStack.padding = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });

                                // Name label
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

                                // Name entry
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

                                elements.push_back(std::move(formStack));

                                // Create button
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
                                    createBtn.onClicked = [state, &renderer, popup]()
                                    {
                                        auto& uiMgr = renderer->getUIManager();
                                        const std::string levelName = state->name.empty() ? "NewLevel" : state->name;
                                        const std::string relFolder = state->folder.empty() ? "Levels" : state->folder;
                                        uiMgr.createNewLevelWithTemplate(UIManager::SceneTemplate::Empty, levelName, relFolder);
                                        popup->close();
                                    };
                                    elements.push_back(std::move(createBtn));
                                }

                                // Cancel button
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

                        items.push_back({ "New Widget", [&renderer]()
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
                                {
                                    fileName = baseName + std::to_string(counter++) + ".asset";
                                }

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

                                std::vector<WidgetElement> elements;
                                elements.push_back(std::move(canvas));
                                defaultWidget->setElements(std::move(elements));
                                widgetAsset->setData(defaultWidget->toJson());

                                const unsigned int id = assetMgr.registerLoadedAsset(widgetAsset);
                                if (id == 0)
                                {
                                    uiMgr.showToastMessage("Failed to create widget asset.", 3.0f);
                                    return;
                                }

                                widgetAsset->setId(id);
                                Asset asset;
                                asset.ID = id;
                                asset.type = AssetType::Widget;
                                if (!assetMgr.saveAsset(asset, AssetManager::Sync))
                                {
                                    uiMgr.showToastMessage("Failed to save widget asset.", 3.0f);
                                    return;
                                }

                                AssetRegistryEntry entry;
                                entry.name = displayName;
                                entry.path = relPath;
                                entry.type = AssetType::Widget;
                                assetMgr.registerAssetInRegistry(entry);

                                uiMgr.refreshContentBrowser();
                                uiMgr.showToastMessage("Created: " + fileName, 3.0f);
                                uiMgr.openWidgetEditorPopup(relPath);
                            }});

                        items.push_back({ "New Material", [&renderer]()
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
                                    float shininess = 32.0f;
                                    std::string folder;
                                };
                                auto state = std::make_shared<MaterialState>();
                                state->folder = folder;

                                std::vector<WidgetElement> elements;

                                // Background
                                {
                                    WidgetElement bg;
                                    bg.type = WidgetElementType::Panel;
                                    bg.id = "NM.Bg";
                                    bg.from = Vec2{ 0.0f, 0.0f };
                                    bg.to = Vec2{ 1.0f, 1.0f };
                                    bg.style.color = EditorTheme::Get().panelBackground;
                                    elements.push_back(bg);
                                }

                                // Title
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

                                // Form layout as StackPanel
                                WidgetElement formStack;
                                formStack.type = WidgetElementType::StackPanel;
                                formStack.id = "NM.Form";
                                formStack.from = Vec2{ nx(16.0f), ny(44.0f) };
                                formStack.to = Vec2{ nx(W - 16.0f), ny(H - 50.0f) };
                                formStack.padding = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
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
                                {
                                    auto entry = makeEntry("NM.Name", state->name);
                                    entry.onValueChanged = [state](const std::string& v) { state->name = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.VsLbl", "Vertex Shader"));
                                {
                                    auto entry = makeEntry("NM.VertexShader", state->vertexShader);
                                    entry.onValueChanged = [state](const std::string& v) { state->vertexShader = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.FsLbl", "Fragment Shader"));
                                {
                                    auto entry = makeEntry("NM.FragmentShader", state->fragmentShader);
                                    entry.onValueChanged = [state](const std::string& v) { state->fragmentShader = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.DiffLbl", "Diffuse Texture (asset path)"));
                                {
                                    auto entry = makeEntry("NM.DiffuseTex", "");
                                    entry.onValueChanged = [state](const std::string& v) { state->diffuseTexture = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.SpecLbl", "Specular Texture (asset path)"));
                                {
                                    auto entry = makeEntry("NM.SpecularTex", "");
                                    entry.onValueChanged = [state](const std::string& v) { state->specularTexture = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.ShinLbl", "Shininess"));
                                {
                                    auto entry = makeEntry("NM.Shininess", "32");
                                    entry.onValueChanged = [state](const std::string& v) {
                                        try { state->shininess = std::stof(v); } catch (...) {}
                                    };
                                    formStack.children.push_back(std::move(entry));
                                }

                                elements.push_back(std::move(formStack));

                                // Create button
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
                                    createBtn.onClicked = [state, &renderer, popup]()
                                    {
                                        auto& diagnostics = DiagnosticsManager::Instance();
                                        auto& assetMgr = AssetManager::Instance();
                                        const std::filesystem::path contentDir =
                                            std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
                                        const std::filesystem::path targetDir = state->folder.empty()
                                            ? contentDir
                                            : contentDir / state->folder;
                                        std::error_code ec;
                                        std::filesystem::create_directories(targetDir, ec);

                                        const std::string matName = state->name.empty() ? "NewMaterial" : state->name;
                                        std::string fileName = matName + ".asset";
                                        int counter = 1;
                                        while (std::filesystem::exists(targetDir / fileName))
                                        {
                                            fileName = matName + std::to_string(counter++) + ".asset";
                                        }

                                        auto mat = std::make_shared<AssetData>();
                                        mat->setName(std::filesystem::path(fileName).stem().string());
                                        mat->setAssetType(AssetType::Material);
                                        mat->setType(AssetType::Material);
                                        const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();
                                        mat->setPath(relPath);

                                        json matData = json::object();
                                        if (!state->vertexShader.empty())
                                            matData["m_shaderVertex"] = state->vertexShader;
                                        if (!state->fragmentShader.empty())
                                            matData["m_shaderFragment"] = state->fragmentShader;
                                        std::vector<std::string> texPaths;
                                        if (!state->diffuseTexture.empty())
                                            texPaths.push_back(state->diffuseTexture);
                                        if (!state->specularTexture.empty())
                                            texPaths.push_back(state->specularTexture);
                                        if (!texPaths.empty())
                                            matData["m_textureAssetPaths"] = texPaths;
                                        matData["m_shininess"] = state->shininess;
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

                                        auto& uiMgr = renderer->getUIManager();
                                        uiMgr.refreshContentBrowser();
                                        uiMgr.showToastMessage("Created: " + fileName, 3.0f);

                                        popup->close();
                                    };
                                    elements.push_back(std::move(createBtn));
                                }

                                // Cancel button
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

                        // ── Save selected entity as Prefab ──
                        {
                            const auto& selectedEntities = renderer->getSelectedEntities();
                            if (!selectedEntities.empty())
                            {
                                items.push_back({ "", {}, true }); // separator
                                items.push_back({ "Save as Prefab", [&renderer]()
                                    {
                                        auto& uiMgr = renderer->getUIManager();
                                        const auto& selEnts = renderer->getSelectedEntities();
                                        if (selEnts.empty()) { uiMgr.showToastMessage("No entity selected.", 2.5f); return; }
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

                        // ── Asset Reference Tracking ──
                        {
                            const std::string selectedAsset = uiManager.getSelectedGridAsset();
                            if (!selectedAsset.empty())
                            {
                                items.push_back({ "", {}, true }); // separator
                                items.push_back({ "Find References", [&renderer, selectedAsset]()
                                    {
                                        auto& uiMgr = renderer->getUIManager();
                                        auto& assetMgr = AssetManager::Instance();
                                        const auto refs = assetMgr.findReferencesTo(selectedAsset);
                                        std::vector<std::pair<std::string, std::string>> items;
                                        items.reserve(refs.size());
                                        for (const auto& r : refs)
                                            items.push_back({ r.sourcePath, r.sourceType });
                                        const std::string title = "References to " + std::filesystem::path(selectedAsset).stem().string();
                                        uiMgr.openAssetReferencesPopup(title, selectedAsset, items);
                                    }});
                                items.push_back({ "Show Dependencies", [&renderer, selectedAsset]()
                                    {
                                        auto& uiMgr = renderer->getUIManager();
                                        auto& assetMgr = AssetManager::Instance();
                                        const auto deps = assetMgr.getAssetDependencies(selectedAsset);
                                        std::vector<std::pair<std::string, std::string>> items;
                                        items.reserve(deps.size());
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
                                            items.push_back({ d, depType });
                                        }
                                        const std::string title = "Dependencies of " + std::filesystem::path(selectedAsset).stem().string();
                                        uiMgr.openAssetReferencesPopup(title, selectedAsset, items);
                                    }});
                            }
                        }

                        uiManager.showDropdownMenu(mousePos, items);
                        continue;
                    }
                }
                if (!isOverUI && !diagnostics.isPIEActive())
                {
                    // Texture viewer: right-click starts panning instead of camera rotation
                    auto* texViewer = renderer->getTextureViewer(renderer->getActiveTabId());
                    if (texViewer)
                    {
                        texViewerPanning = true;
                        continue;
                    }

                    rightMouseDown = true;
                    SDL_GetMouseState(&preCaptureMouseX, &preCaptureMouseY);
                    if (auto* w = renderer->window())
                    {
                        SDL_SetWindowRelativeMouseMode(w, true);
                        SDL_SetWindowMouseGrab(w, true);
                    }
                    SDL_HideCursor();
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
            {
                // Texture viewer: end panning
                if (texViewerPanning)
                {
                    texViewerPanning = false;
                    continue;
                }

                // Widget editor: end panning
                if (renderer)
                {
                    auto& uiManager = renderer->getUIManager();
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    if (uiManager.handleRightMouseUp(mousePos))
                    {
                        // Pan ended â€” don't fall through to camera right-click release
                    }
                    else if (rightMouseDown)
                    {
                        rightMouseDown = false;
                        if (auto* w = renderer->window())
                        {
                            SDL_SetWindowRelativeMouseMode(w, false);
                            SDL_SetWindowMouseGrab(w, false);
                            SDL_WarpMouseInWindow(w, preCaptureMouseX, preCaptureMouseY);
                        }
                        SDL_ShowCursor();
                    }
                }
                else if (rightMouseDown)
                {
                    rightMouseDown = false;
                }
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                if (renderer && !(diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused))
                {
                    auto& uiManager = renderer->getUIManager();
                    if (uiManager.handleScroll(mousePosPixels, event.wheel.y))
                    {
                        continue;
                    }
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        if (viewportUI->handleScroll(mousePosPixels, event.wheel.y))
                        {
                            continue;
                        }
                    }
                }

                // Texture viewer zoom
                if (renderer)
                {
                    auto* texViewer = renderer->getTextureViewer(renderer->getActiveTabId());
                    if (texViewer)
                    {
                        const float zoomFactor = (event.wheel.y > 0.0f) ? 1.15f : (1.0f / 1.15f);
                        float newZoom = texViewer->getZoom() * zoomFactor;
                        newZoom = std::max(0.05f, std::min(newZoom, 50.0f));
                        texViewer->setZoom(newZoom);
                        renderer->getUIManager().markAllWidgetsDirty();
                        continue;
                    }
                }

                if (rightMouseDown)
                {
                    const float step = 0.1f;
                    if (event.wheel.y > 0.0f)
                    {
                        cameraSpeedMultiplier = std::min(5.0f, cameraSpeedMultiplier + step);
                    }
                    else if (event.wheel.y < 0.0f)
                    {
                        cameraSpeedMultiplier = std::max(0.5f, cameraSpeedMultiplier - step);
                    }
                    // Update CamSpeed button label
                    if (auto* el = renderer->getUIManager().findElementById("ViewportOverlay.CamSpeed"))
                    {
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "%.1fx", cameraSpeedMultiplier);
                        el->text = buf;
                        renderer->getUIManager().markAllWidgetsDirty();
                    }
                }
            }

            // Texture viewer pan via right-click drag (or left-click in laptop mode)
            if (event.type == SDL_EVENT_MOUSE_MOTION && texViewerPanning)
            {
                if (renderer)
                {
                    auto* texViewer = renderer->getTextureViewer(renderer->getActiveTabId());
                    if (texViewer)
                    {
                        texViewer->setPanX(texViewer->getPanX() + event.motion.xrel);
                        texViewer->setPanY(texViewer->getPanY() + event.motion.yrel);
                        renderer->getUIManager().markAllWidgetsDirty();
                    }
                }
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && (rightMouseDown || (diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused)))
            {
                // Block camera rotation when gameplay cursor is visible
                bool cursorBlocksCamera = false;
                if (diagnostics.isPIEActive() && pieMouseCaptured && !pieInputPaused)
                {
                    if (auto* vpUI = renderer->getViewportUIManagerPtr())
                        cursorBlocksCamera = vpUI->isGameplayCursorVisible();
                }
                if (!cursorBlocksCamera)
                {
                    // Use a frame-rate independent sensitivity (degrees per pixel).
                    // Relative mouse motion already represents physical movement, not a per-frame quantity.
                    const float sensitivity = 0.12f; // deg per pixel
                    renderer->rotateCamera(static_cast<float>(event.motion.xrel) * sensitivity,
                        -static_cast<float>(event.motion.yrel) * sensitivity);
                }
            }

            if (event.type == SDL_EVENT_TEXT_INPUT)
            {
                if (renderer)
                {
                    auto& uiManager = renderer->getUIManager();
                    if (uiManager.handleTextInput(event.text.text))
                    {
                        continue;
                    }
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        if (viewportUI->handleTextInput(event.text.text))
                        {
                            continue;
                        }
                    }
                }
            }


            if (event.type == SDL_EVENT_KEY_UP)
            {
#if ENGINE_EDITOR
                if (ShortcutManager::Instance().handleKey(event.key.key, event.key.mod, ShortcutManager::Phase::KeyUp))
                {
                    continue;
                }
#endif // ENGINE_EDITOR
                if (diagnostics.isPIEActive())
                {
                    Scripting::HandleKeyUp(event.key.key);
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
#if ENGINE_EDITOR
                if (ShortcutManager::Instance().handleKey(event.key.key, event.key.mod, ShortcutManager::Phase::KeyDown))
                {
                    continue;
                }
#endif // ENGINE_EDITOR
                if (renderer)
                {
                    auto& uiManager = renderer->getUIManager();
                    if (uiManager.handleKeyDown(event.key.key))
                    {
                        continue;
                    }
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        if (viewportUI->handleKeyDown(event.key.key, static_cast<int>(event.key.mod)))
                        {
                            continue;
                        }
                    }
                }
                diagnostics.dispatchKeyDown(event.key.key);
                if (diagnostics.isPIEActive())
                {
                    Scripting::HandleKeyDown(event.key.key);
                }
            }


            // --- Gamepad events ---
            if (event.type == SDL_EVENT_GAMEPAD_ADDED)
            {
                if (!activeGamepad)
                {
                    activeGamepad = SDL_OpenGamepad(event.gdevice.which);
                }
            }
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
            {
                if (activeGamepad)
                {
                    SDL_CloseGamepad(activeGamepad);
                    activeGamepad = nullptr;
                }
            }
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP)
            {
                const bool pressed = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
                if (renderer)
                {
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        viewportUI->handleGamepadButton(event.gbutton.button, pressed);
                    }
                }
            }
            if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
            {
                if (renderer)
                {
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        const float normalized = static_cast<float>(event.gaxis.value) / 32767.0f;
                        viewportUI->handleGamepadAxis(event.gaxis.axis, normalized);
                    }
                }
            }

            // --- OS file drop: import files dragged from the OS file explorer ---
            if (event.type == SDL_EVENT_DROP_FILE)
            {
                const char* droppedFile = event.drop.data;
                if (droppedFile && droppedFile[0] != '\0')
                {
                    const std::string filePath(droppedFile);
                    Logger::Instance().log(Logger::Category::AssetManagement,
                        "OS file drop received: " + filePath, Logger::LogLevel::INFO);

                    auto& am = AssetManager::Instance();
                    auto& diag = DiagnosticsManager::Instance();

                    if (!diag.isProjectLoaded())
                    {
                        if (renderer)
                            renderer->getUIManager().showToastMessage("Cannot import: no project loaded.", 3.0f);
                    }
                    else
                    {
                        auto action = diag.registerAction(DiagnosticsManager::ActionType::ImportingAsset);
                        am.importAssetFromPath(filePath, AssetType::Unknown, action.ID);
                        if (renderer)
                        {
                            const std::string fileName = std::filesystem::path(filePath).filename().string();
                            renderer->getUIManager().showToastMessage("Importing: " + fileName, 2.5f);
                        }
                    }
                }
            }
        }
        const uint64_t eventEndCounter = SDL_GetPerformanceCounter();
        cpuEventMs = (freq > 0.0) ? (static_cast<double>(eventEndCounter - eventStartCounter) / freq * 1000.0) : 0.0;

        const uint64_t inputEndCounter = SDL_GetPerformanceCounter();
        cpuInputMs = (freq > 0.0) ? (static_cast<double>(inputEndCounter - inputStartCounter) / freq * 1000.0) : 0.0;

        if (diagnostics.isShutdownRequested())
        {
            logTimed(Logger::Category::Engine, "Shutdown requested â€“ exiting main loop.", Logger::LogLevel::INFO);
            running = false;
        }

        if (diagnostics.isPIEActive())
        {
            PhysicsWorld::Instance().step(static_cast<float>(dt));
            Scripting::UpdateScripts(static_cast<float>(dt));
        }

        // Poll for .py file changes and hot-reload if needed (self-throttled to 500ms)
        Scripting::PollScriptHotReload();
        Scripting::PollPluginHotReload();

        // Level-Streaming: auto-load/unload sub-levels based on camera position (Phase 11.4)
        if (renderer)
        {
            renderer->updateLevelStreaming(renderer->getCameraPosition());
        }

        if (renderer)
        {
            renderer->getUIManager().updateNotifications(static_cast<float>(dt));
        }

        if (renderer)
        {
            // Only show performance stats on the Viewport tab, not in Mesh Viewer tabs
            const bool isViewportTab = (renderer->getActiveTabId() == "Viewport");

            if (metricsUpdatePending)
            {
                fpsText = "FPS: " + std::to_string(static_cast<int>(fpsValue + 0.5));

                char speedBuf[32];
                std::snprintf(speedBuf, sizeof(speedBuf), "Speed: x%.1f", cameraSpeedMultiplier);
                speedText = speedBuf;
            }

            if (isViewportTab)
            {
                renderer->queueText(fpsText,
                    Vec2{ 0.02f, 0.05f },
                    0.6f,
                    Vec4{ 1.0f, 1.0f, 1.0f, 1.0f });

                renderer->queueText(speedText,
                    Vec2{ 0.02f, 0.09f },
                    0.4f,
                    Vec4{ 0.9f, 0.9f, 0.9f, 1.0f });
            }
        }

        const uint64_t renderStartCounter = SDL_GetPerformanceCounter();
        renderer->render();
        renderer->present();
        const uint64_t renderEndCounter = SDL_GetPerformanceCounter();
        cpuRenderMs = (freq > 0.0) ? (static_cast<double>(renderEndCounter - renderStartCounter) / freq * 1000.0) : 0.0;

        const uint64_t frameEndCounter = SDL_GetPerformanceCounter();
        const double cpuFrameMs = (freq > 0.0) ? (static_cast<double>(frameEndCounter - frameStartCounter) / freq * 1000.0) : 0.0;
        cpuOtherMs = std::max(0.0, cpuFrameMs - cpuInputMs - cpuRenderMs);

        // Push frame metrics to DiagnosticsManager for the Profiler tab
        {
            DiagnosticsManager::FrameMetrics fm{};
            fm.fps           = fpsValue;
            fm.cpuFrameMs    = cpuFrameMs;
            fm.gpuFrameMs    = renderer->getLastGpuFrameMs();
            fm.cpuWorldMs    = renderer->getLastCpuRenderWorldMs();
            fm.cpuUiMs       = renderer->getLastCpuRenderUiMs();
            fm.cpuUiLayoutMs = renderer->getLastCpuUiLayoutMs();
            fm.cpuUiDrawMs   = renderer->getLastCpuUiDrawMs();
            fm.cpuEcsMs      = renderer->getLastCpuEcsMs();
            fm.cpuInputMs    = cpuInputMs;
            fm.cpuEventMs    = cpuEventMs;
            fm.cpuGcMs       = cpuGcMs;
            fm.cpuRenderMs   = cpuRenderMs;
            fm.cpuOtherMs    = cpuOtherMs;
            fm.visibleCount  = renderer->getLastVisibleCount();
            fm.hiddenCount   = renderer->getLastHiddenCount();
            fm.totalCount    = renderer->getLastTotalCount();
            diagnostics.pushFrameMetrics(fm);
        }

        if (renderer)
        {
            const bool isViewportTab = (renderer->getActiveTabId() == "Viewport");

            if (metricsUpdatePending)
            {
                char buf[128];

                std::snprintf(buf, sizeof(buf), "CPU: %.3f ms | GPU: %.3f ms",
                    cpuFrameMs, renderer->getLastGpuFrameMs());
                cpuText = buf;

                std::snprintf(buf, sizeof(buf), "World: %.3f ms | UI: %.3f ms",
                    renderer->getLastCpuRenderWorldMs(), renderer->getLastCpuRenderUiMs());
                renderText = buf;

                std::snprintf(buf, sizeof(buf), "UI Layout: %.3f ms | UI Draw: %.3f ms",
                    renderer->getLastCpuUiLayoutMs(), renderer->getLastCpuUiDrawMs());
                uiText = buf;

                std::snprintf(buf, sizeof(buf), "Input: %.3f ms | Events: %.3f ms",
                    cpuInputMs, cpuEventMs);
                inputText = buf;

                std::snprintf(buf, sizeof(buf), "Render: %.3f ms | Other: %.3f ms",
                    cpuRenderMs, cpuOtherMs);
                otherText = buf;

                std::snprintf(buf, sizeof(buf), "GC: %.3f ms | Logger: %.3f ms",
                    cpuGcMs, cpuLoggerMs);
                gcText = buf;

                std::snprintf(buf, sizeof(buf), "ECS: %.3f ms",
                    renderer->getLastCpuEcsMs());
                ecsText = buf;

                std::snprintf(buf, sizeof(buf), "Frame: %.3f ms", cpuFrameMs);
                frameText = buf;

                std::snprintf(buf, sizeof(buf), "Visible: %u | Hidden: %u | Total: %u",
                    renderer->getLastVisibleCount(), renderer->getLastHiddenCount(),
                    renderer->getLastTotalCount());
                occlusionText = buf;
            }

            if (showMetrics && isViewportTab)
            {
                if (!cpuText.empty())
                {
                    renderer->queueText(cpuText, Vec2{ 0.02f, 0.13f }, 0.4f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!renderText.empty())
                {
                    renderer->queueText(renderText, Vec2{ 0.02f, 0.17f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!uiText.empty())
                {
                    renderer->queueText(uiText, Vec2{ 0.02f, 0.21f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!inputText.empty())
                {
                    renderer->queueText(inputText, Vec2{ 0.02f, 0.25f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!otherText.empty())
                {
                    renderer->queueText(otherText, Vec2{ 0.02f, 0.29f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!gcText.empty())
                {
                    renderer->queueText(gcText, Vec2{ 0.02f, 0.33f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!ecsText.empty())
                {
                    renderer->queueText(ecsText, Vec2{ 0.02f, 0.37f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!frameText.empty())
                {
                    renderer->queueText(frameText, Vec2{ 0.02f, 0.41f }, 0.35f, Vec4{ 0.7f, 1.0f, 0.7f, 1.0f });
                }
            }
            if (showOcclusionStats && isViewportTab && !occlusionText.empty())
            {
                renderer->queueText(occlusionText, Vec2{ 0.02f, 0.45f }, 0.35f, Vec4{ 1.0f, 0.85f, 0.4f, 1.0f });
            }
        }

        if (metricsUpdatePending)
        {
            metricsUpdatePending = false;
        }

        if (fpscap)
        {
            const uint64_t frameEndCounter = SDL_GetPerformanceCounter();
            const double frameMs = (freq > 0.0) ? (static_cast<double>(frameEndCounter - frameStartCounter) / freq * 1000.0) : 0.0;
            const double remainingMs = 16.66 - frameMs;
            if (remainingMs > 0.0)
            {
                SDL_Delay(static_cast<Uint32>(remainingMs + 0.5));
            }
        }
    }

    logTimed(Logger::Category::Engine, "Shutting down...", Logger::LogLevel::INFO);

	while (diagnostics.isActionInProgress())
    {
        logTimed(Logger::Category::Engine, "Waiting for ongoing actions to complete...", Logger::LogLevel::INFO);
        SDL_Delay(100);
    }

    logTimed(Logger::Category::Diagnostics, "Saving configs...", Logger::LogLevel::INFO);
    if (auto* w = renderer->window())
    {
        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSize(w, &windowW, &windowH);
        diagnostics.setWindowSize(Vec2{ static_cast<float>(windowW), static_cast<float>(windowH) });

        const Uint32 flags = SDL_GetWindowFlags(w);
        DiagnosticsManager::WindowState state = DiagnosticsManager::WindowState::Normal;
        if ((flags & SDL_WINDOW_FULLSCREEN) != 0)
        {
            state = DiagnosticsManager::WindowState::Fullscreen;
        }
        else if ((flags & SDL_WINDOW_MAXIMIZED) != 0)
        {
            state = DiagnosticsManager::WindowState::Maximized;
        }
        diagnostics.setWindowState(state);
    }

    // Capture editor camera into the level so it persists across sessions
#if ENGINE_EDITOR
    if (!isRuntimeMode)
    {
        auto* lvl = diagnostics.getActiveLevelSoft();
        if (lvl)
        {
            lvl->setEditorCameraPosition(renderer->getCameraPosition());
            lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees());
            lvl->setHasEditorCamera(true);
            assetManager.saveActiveLevel();
        }
    }

    // Save shortcut overrides
    if (!isRuntimeMode)
    {
        const auto& projPath = diagnostics.getProjectInfo().projectPath;
        if (!projPath.empty())
        {
            const std::string shortcutFile = (std::filesystem::path(projPath) / "shortcuts.cfg").string();
            ShortcutManager::Instance().saveToFile(shortcutFile);
        }
    }

    if (!isRuntimeMode)
    {
        diagnostics.saveProjectConfig();
        diagnostics.saveConfig();
    }
#endif // ENGINE_EDITOR (shutdown saves)

    audioManager.shutdown();

    renderer->shutdown();

    delete renderer;

    if (activeGamepad)
    {
        SDL_CloseGamepad(activeGamepad);
        activeGamepad = nullptr;
    }

    logTimed(Logger::Category::Engine, "SDL_Quit()", Logger::LogLevel::INFO);
    SDL_Quit();

    if (logger.hasErrorsOrFatal())
    {
        const auto& logFile = logger.getLogFilename();
        if (!logFile.empty())
        {
#if defined(_WIN32)
            ShellExecuteA(nullptr, "open", logFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
            std::string command = "open \"" + logFile + "\"";
            std::system(command.c_str());
#else
            std::string command = "xdg-open \"" + logFile + "\"";
            std::system(command.c_str());
#endif
        }
    }

    logTimed(Logger::Category::Engine, "Engine shutdown complete.", Logger::LogLevel::INFO);
    Scripting::Shutdown();
    return 0;
}
