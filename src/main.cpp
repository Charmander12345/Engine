#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/RendererFactory.h"
#include "Renderer/ViewportUIManager.h"
#include "Renderer/SplashWindow.h"
#include "Renderer/UIWidget.h"
#include "Renderer/EditorWindows/PopupWindow.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetTypes.h"
#include "Core/ECS/ECS.h"
#include "Core/MathTypes.h"
#include "Core/AudioManager.h"
#include "Core/UndoRedoManager.h"
#include "Core/EngineLevel.h"
#include "Scripting/PythonScripting.h"
#include "Physics/PhysicsWorld.h"

using namespace std;


int main()
{
    auto& logger = Logger::Instance();
    logger.initialize();

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
    logTimed(Logger::Category::Engine, "Initialising SDL (video + audio)...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
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
    const bool useSplash = [&]() {
        if (auto v = diagnostics.getState("StartupMode"))
            return *v == "normal";
        return true; // default to normal (splash)
    }();

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

    std::string chosenPath;
    bool chosenIsNew = false;
    bool chosenSetDefault = false;
    bool chosenIncludeDefaultContent = true;
    DiagnosticsManager::RHIType chosenRHI = DiagnosticsManager::RHIType::OpenGL;
    bool projectChosen = false;
    bool startupSelectionCancelled = false;

    // Check for a persisted default project
    auto defaultProj = diagnostics.getState("DefaultProject");
    if (defaultProj && !defaultProj->empty() && std::filesystem::exists(*defaultProj))
    {
        chosenPath = *defaultProj;
        projectChosen = true;
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
                SDL_SetWindowHitTest(w, nullptr, nullptr); // use native titlebar hit-testing in startup window
                SDL_SetWindowBordered(w, true);
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

                if (!tempWindowOpen)
                {
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
        if (auto v = diag.getState("UIDebugEnabled"))
        {
            if ((*v == "1") != renderer->isUIDebugEnabled())
                renderer->toggleUIDebug();
        }
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
                    continue; // ignore – may be leftover from popup destruction
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

    // --- Phase 3: Load editor UI widgets ---
    showProgress("Loading editor UI...");

    std::function<void()> stopPIE;

    // PIE input capture state (declared here so lambdas can capture by reference)
    bool pieMouseCaptured = false;
    bool pieInputPaused = false;
    // Mouse position saved before any capture (right-click or PIE) so the cursor
    // can be warped back on release, keeping it visually fixed in the viewport.
    float preCaptureMouseX = 0.0f;
    float preCaptureMouseY = 0.0f;

    if (renderer)
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

                items.push_back({ "UI Designer", [&renderer]()
                    {
                        renderer->getUIManager().openUIDesignerTab();
                    }
                });

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
                            picker->color = renderer->getClearColor();
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
                                state->r = static_cast<int>(std::round(std::clamp(picker->color.x, 0.0f, 1.0f) * 255.0f));
                                state->g = static_cast<int>(std::round(std::clamp(picker->color.y, 0.0f, 1.0f) * 255.0f));
                                state->b = static_cast<int>(std::round(std::clamp(picker->color.z, 0.0f, 1.0f) * 255.0f));
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
                                    state->picker->color = color;
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
                                skyLabel.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
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
                                skyEntry.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                                skyEntry.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
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
                                clearBtn.textColor = Vec4{ 0.9f, 0.7f, 0.7f, 1.0f };
                                clearBtn.textAlignH = TextAlignH::Center;
                                clearBtn.textAlignV = TextAlignV::Center;
                                clearBtn.color = Vec4{ 0.3f, 0.15f, 0.15f, 0.8f };
                                clearBtn.hoverColor = Vec4{ 0.45f, 0.2f, 0.2f, 0.95f };
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

                        // --- Drag & Drop: asset dropped on viewport → spawn entity ---
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
                                                "Applied " + assetName + " → Entity " + std::to_string(targetEntity), 2.5f);
                                        }
                                    }
                                    else
                                    {
                                        // No entity under cursor — cannot apply, show hint
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

                        // --- Drag & Drop: asset dropped on content browser folder → move asset ---
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

                        // --- Drag & Drop: asset dropped on Outliner entity → apply to entity ---
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
                                        "Applied " + assetName + " → Entity " + std::to_string(entityId), 2.5f);
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

        // --- UndoRedo onChange → mark level dirty + refresh StatusBar ---
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
                const size_t total = am.getUnsavedAssetCount();
                if (total == 0)
                {
                    renderer->getUIManager().showToastMessage("Nothing to save.", 2.0f);
                    return;
                }
                auto& uiMgr = renderer->getUIManager();
                uiMgr.showSaveProgressModal(total);
                am.saveAllAssetsAsync(
                    [&uiMgr](size_t saved, size_t total)
                    {
                        uiMgr.updateSaveProgress(saved, total);
                    },
                    [&uiMgr](bool success)
                    {
                        UndoRedoManager::Instance().clear();
                        uiMgr.closeSaveProgressModal(success);
                    });
            });

    }

    // All subsystems initialised — render the first frame while the splash is
    // still visible so the main window never appears white / empty.
    if (renderer)
    {
        // Ensure all loaded widgets are marked dirty so the UI FBO is fully redrawn.
        renderer->getUIManager().markAllWidgetsDirty();

        renderer->getUIManager().showToastMessage("Engine ready!", 3.0f);
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_EVENT_QUIT)
                continue; // ignore – may be leftover from popup destruction
        }
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

    // Reset shutdown flag – only TitleBar.Close and SDL_EVENT_QUIT inside the
    // main loop should be able to shut the engine down from this point on.
    diagnostics.resetShutdownRequest();

    bool running = true;
    uint64_t frame = 0;
    logTimed(Logger::Category::Engine, "Entering main loop.", Logger::LogLevel::INFO);

    diagnostics.registerKeyUpHandler(SDLK_F1, [&]() {
        logTimed(Logger::Category::Input, "F1 pressed - saving all assets (async).", Logger::LogLevel::INFO);
        //assetManager.saveAllAssetsAsync();
        return true;
        });

    diagnostics.registerKeyUpHandler(SDLK_F2, [&]() {
        logTimed(Logger::Category::Input, "F2 pressed - opening import dialog.", Logger::LogLevel::INFO);
        assetManager.OpenImportDialog(renderer ? renderer->window() : nullptr, AssetType::Unknown, AssetManager::Async);
        return true;
        });

    diagnostics.registerKeyUpHandler(SDLK_DELETE, [&]() {
        if (!renderer) return false;
        auto& uiManager = renderer->getUIManager();

        // Check for widget editor element selection first (tab-level selection)
        if (uiManager.tryDeleteWidgetEditorElement())
        {
            return true;
        }

        // Check for selected asset in Content Browser grid
        const std::string selectedAsset = uiManager.getSelectedGridAsset();
        if (!selectedAsset.empty())
        {
            uiManager.showConfirmDialog(
                "Are you sure you want to delete this asset?\nThis cannot be undone.",
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

        // Otherwise try entity deletion
        const unsigned int selected = uiManager.getSelectedEntity();
        if (selected == 0) return false;

        auto& ecs = ECS::ECSManager::Instance();
        auto* level = diagnostics.getActiveLevelSoft();
        const auto entity = static_cast<ECS::Entity>(selected);

        // Get entity name for feedback
        std::string entityName = "Entity " + std::to_string(selected);
        if (const auto* nameComp = ecs.getComponent<ECS::NameComponent>(entity))
        {
            if (!nameComp->displayName.empty())
                entityName = nameComp->displayName;
        }

        // Snapshot components for undo
        auto savedTransform   = ecs.hasComponent<ECS::TransformComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::TransformComponent>(entity))   : std::nullopt;
        auto savedName        = ecs.hasComponent<ECS::NameComponent>(entity)        ? std::make_optional(*ecs.getComponent<ECS::NameComponent>(entity))        : std::nullopt;
        auto savedMesh        = ecs.hasComponent<ECS::MeshComponent>(entity)        ? std::make_optional(*ecs.getComponent<ECS::MeshComponent>(entity))        : std::nullopt;
        auto savedMaterial    = ecs.hasComponent<ECS::MaterialComponent>(entity)    ? std::make_optional(*ecs.getComponent<ECS::MaterialComponent>(entity))    : std::nullopt;
        auto savedLight       = ecs.hasComponent<ECS::LightComponent>(entity)       ? std::make_optional(*ecs.getComponent<ECS::LightComponent>(entity))       : std::nullopt;
        auto savedCamera      = ecs.hasComponent<ECS::CameraComponent>(entity)      ? std::make_optional(*ecs.getComponent<ECS::CameraComponent>(entity))      : std::nullopt;
        auto savedPhysics     = ecs.hasComponent<ECS::PhysicsComponent>(entity)     ? std::make_optional(*ecs.getComponent<ECS::PhysicsComponent>(entity))     : std::nullopt;
        auto savedScript      = ecs.hasComponent<ECS::ScriptComponent>(entity)      ? std::make_optional(*ecs.getComponent<ECS::ScriptComponent>(entity))      : std::nullopt;
        auto savedCollision   = ecs.hasComponent<ECS::CollisionComponent>(entity)   ? std::make_optional(*ecs.getComponent<ECS::CollisionComponent>(entity))   : std::nullopt;
        auto savedHeightField = ecs.hasComponent<ECS::HeightFieldComponent>(entity) ? std::make_optional(*ecs.getComponent<ECS::HeightFieldComponent>(entity)) : std::nullopt;

        // Remove from level tracking first
        if (level)
        {
            level->onEntityRemoved(entity);
        }

        // Remove from ECS
        ecs.removeEntity(entity);

        // Clear selection and deselect in renderer
        uiManager.selectEntity(0);
        renderer->setSelectedEntity(0);

        uiManager.refreshWorldOutliner();
        uiManager.showToastMessage("Deleted: " + entityName, 2.5f);

        Logger::Instance().log(Logger::Category::Engine,
            "Deleted entity " + std::to_string(selected) + " (" + entityName + ")",
            Logger::LogLevel::INFO);

        // Push undo/redo command
        UndoRedoManager::Command cmd;
        cmd.description = "Delete " + entityName;
        cmd.execute = [entity]()
            {
                auto& e = ECS::ECSManager::Instance();
                if (std::find(e.getEntitiesMatchingSchema(ECS::Schema()).begin(),
                              e.getEntitiesMatchingSchema(ECS::Schema()).end(), entity) != e.getEntitiesMatchingSchema(ECS::Schema()).end())
                {
                    auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
                    if (lvl) lvl->onEntityRemoved(entity);
                    e.removeEntity(entity);
                }
            };
        cmd.undo = [entity, savedTransform, savedName, savedMesh, savedMaterial, savedLight, savedCamera, savedPhysics, savedScript, savedCollision, savedHeightField]()
            {
                auto& e = ECS::ECSManager::Instance();
                e.createEntity(entity);
                if (savedTransform)   e.addComponent<ECS::TransformComponent>(entity, *savedTransform);
                if (savedName)        e.addComponent<ECS::NameComponent>(entity, *savedName);
                if (savedMesh)        e.addComponent<ECS::MeshComponent>(entity, *savedMesh);
                if (savedMaterial)    e.addComponent<ECS::MaterialComponent>(entity, *savedMaterial);
                if (savedLight)       e.addComponent<ECS::LightComponent>(entity, *savedLight);
                if (savedCamera)      e.addComponent<ECS::CameraComponent>(entity, *savedCamera);
                if (savedPhysics)     e.addComponent<ECS::PhysicsComponent>(entity, *savedPhysics);
                if (savedScript)      e.addComponent<ECS::ScriptComponent>(entity, *savedScript);
                if (savedCollision)   e.addComponent<ECS::CollisionComponent>(entity, *savedCollision);
                if (savedHeightField) e.addComponent<ECS::HeightFieldComponent>(entity, *savedHeightField);
                auto* lvl = DiagnosticsManager::Instance().getActiveLevelSoft();
                if (lvl) lvl->onEntityAdded(entity);
            };
        UndoRedoManager::Instance().pushCommand(std::move(cmd));

        return true;
        });

    bool rightMouseDown = false;
    float cameraSpeedMultiplier = 1.0f;
    bool showMetrics = true;
    bool showOcclusionStats = false;
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

    while (running)
    {
        const uint64_t frameStartCounter = SDL_GetPerformanceCounter();
        ++frame;

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
                        uiManager.handleMouseMotionForPan(mousePosPixels);
                        if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                        {
                            viewportUI->setMousePosition(mousePosPixels);
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
                        // Try gizmo interaction first; only pick entity if gizmo not hit
                        if (!renderer->beginGizmoDrag(static_cast<int>(event.button.x), static_cast<int>(event.button.y)))
                        {
                            renderer->requestPick(static_cast<int>(event.button.x), static_cast<int>(event.button.y));
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
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
                            continue;
                        }
                        if (renderer->isGizmoDragging())
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

                                auto& diagnostics = DiagnosticsManager::Instance();
                                auto& assetMgr = AssetManager::Instance();
                                const std::filesystem::path contentDir =
                                    std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
                                const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
                                std::error_code ec;
                                std::filesystem::create_directories(targetDir, ec);

                                std::string baseName = "NewLevel";
                                std::string fileName = baseName + ".map";
                                int counter = 1;
                                while (std::filesystem::exists(targetDir / fileName))
                                {
                                    fileName = baseName + std::to_string(counter++) + ".map";
                                }

                                auto level = std::make_unique<EngineLevel>();
                                const std::string displayName = std::filesystem::path(fileName).stem().string();
                                level->setName(displayName);
                                const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();
                                level->setPath(relPath);
                                level->setAssetType(AssetType::Level);
                                level->setLevelData(json::object());

                                auto saveResult = assetMgr.saveNewLevelAsset(level.get());
                                if (saveResult)
                                {
                                    AssetRegistryEntry entry;
                                    entry.name = displayName;
                                    entry.path = relPath;
                                    entry.type = AssetType::Level;
                                    assetMgr.registerAssetInRegistry(entry);
                                    uiMgr.refreshContentBrowser();
                                    uiMgr.showToastMessage("Created: " + fileName, 3.0f);
                                }
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
                                canvas.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
                                canvas.isCanvasRoot = true;

                                WidgetElement panel{};
                                panel.id = displayName + ".RootPanel";
                                panel.type = WidgetElementType::Panel;
                                panel.from = Vec2{ 0.0f, 0.0f };
                                panel.to = Vec2{ 1.0f, 1.0f };
                                panel.color = Vec4{ 0.08f, 0.08f, 0.10f, 0.75f };
                                panel.hitTestMode = HitTestMode::Enabled;

                                WidgetElement label{};
                                label.id = displayName + ".Label";
                                label.type = WidgetElementType::Text;
                                label.from = Vec2{ 0.05f, 0.05f };
                                label.to = Vec2{ 0.95f, 0.20f };
                                label.text = "New Widget";
                                label.font = "default.ttf";
                                label.fontSize = 18.0f;
                                label.textColor = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
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

                                constexpr int kPopupW = 460;
                                constexpr int kPopupH = 400;
                                PopupWindow* popup = renderer->openPopupWindow(
                                    "NewMaterial", "New Material", kPopupW, kPopupH);
                                if (!popup) return;
                                if (!popup->uiManager().getRegisteredWidgets().empty()) return;

                                const float W = static_cast<float>(kPopupW);
                                const float H = static_cast<float>(kPopupH);
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
                                    bg.color = Vec4{ 0.13f, 0.13f, 0.16f, 1.0f };
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
                                    title.fontSize = 15.0f;
                                    title.textColor = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
                                    title.textAlignV = TextAlignV::Center;
                                    title.padding = Vec2{ 6.0f, 0.0f };
                                    elements.push_back(title);
                                }

                                // Form layout as StackPanel
                                WidgetElement formStack;
                                formStack.type = WidgetElementType::StackPanel;
                                formStack.id = "NM.Form";
                                formStack.from = Vec2{ nx(16.0f), ny(44.0f) };
                                formStack.to = Vec2{ nx(W - 16.0f), ny(H - 50.0f) };
                                formStack.padding = Vec2{ 4.0f, 4.0f };
                                formStack.scrollable = true;

                                auto makeLabel = [](const std::string& id, const std::string& text) {
                                    WidgetElement lbl;
                                    lbl.type = WidgetElementType::Text;
                                    lbl.id = id;
                                    lbl.text = text;
                                    lbl.fontSize = 13.0f;
                                    lbl.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                                    lbl.fillX = true;
                                    lbl.minSize = Vec2{ 0.0f, 20.0f };
                                    lbl.runtimeOnly = true;
                                    return lbl;
                                };

                                auto makeEntry = [](const std::string& id, const std::string& value) {
                                    WidgetElement entry;
                                    entry.type = WidgetElementType::EntryBar;
                                    entry.id = id;
                                    entry.value = value;
                                    entry.fontSize = 13.0f;
                                    entry.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                                    entry.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                                    entry.fillX = true;
                                    entry.minSize = Vec2{ 0.0f, 24.0f };
                                    entry.padding = Vec2{ 6.0f, 4.0f };
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
                                    createBtn.fontSize = 14.0f;
                                    createBtn.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
                                    createBtn.textAlignH = TextAlignH::Center;
                                    createBtn.textAlignV = TextAlignV::Center;
                                    createBtn.color = Vec4{ 0.15f, 0.45f, 0.25f, 0.95f };
                                    createBtn.hoverColor = Vec4{ 0.2f, 0.6f, 0.35f, 1.0f };
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
                                    cancelBtn.fontSize = 14.0f;
                                    cancelBtn.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
                                    cancelBtn.textAlignH = TextAlignH::Center;
                                    cancelBtn.textAlignV = TextAlignV::Center;
                                    cancelBtn.color = Vec4{ 0.25f, 0.25f, 0.3f, 0.95f };
                                    cancelBtn.hoverColor = Vec4{ 0.35f, 0.35f, 0.42f, 1.0f };
                                    cancelBtn.shaderVertex = "button_vertex.glsl";
                                    cancelBtn.shaderFragment = "button_fragment.glsl";
                                    cancelBtn.hitTestMode = HitTestMode::Enabled;
                                    cancelBtn.onClicked = [popup]() { popup->close(); };
                                    elements.push_back(std::move(cancelBtn));
                                }

                                auto widget = std::make_shared<Widget>();
                                widget->setName("NewMaterial");
                                widget->setSizePixels(Vec2{ W, H });
                                widget->setElements(std::move(elements));
                                popup->uiManager().registerWidget("NewMaterial", widget);
                            }});

                        uiManager.showDropdownMenu(mousePos, items);
                        continue;
                    }
                }
                if (!isOverUI && !diagnostics.isPIEActive())
                {
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
                // Widget editor: end panning
                if (renderer)
                {
                    auto& uiManager = renderer->getUIManager();
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    if (uiManager.handleRightMouseUp(mousePos))
                    {
                        // Pan ended — don't fall through to camera right-click release
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
                }
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
                if (event.key.key == SDLK_F11)
                {
                    if (renderer)
                    {
                        renderer->toggleUIDebug();
						logTimed(Logger::Category::Input,
                            std::string("UI debug bounds: ") + (renderer->isUIDebugEnabled() ? "ON" : "OFF"),
                            Logger::LogLevel::INFO);
                    }
                    continue;
                }
                if (event.key.key == SDLK_F8)
                {
                    if (renderer)
                    {
                        renderer->toggleBoundsDebug();
                        logTimed(Logger::Category::Input,
                            std::string("Bounds debug boxes: ") + (renderer->isBoundsDebugEnabled() ? "ON" : "OFF"),
                            Logger::LogLevel::INFO);
                    }
                    continue;
                }
                if (event.key.key == SDLK_F10)
                {
                    showMetrics = !showMetrics;
                    continue;
                }
                if (event.key.key == SDLK_F9)
                {
                    showOcclusionStats = !showOcclusionStats;
                    continue;
                }
                if (event.key.key == SDLK_ESCAPE && diagnostics.isPIEActive() && stopPIE)
                {
                    stopPIE();
                    continue;
                }
                // Shift+F1: toggle PIE mouse capture (show cursor, pause PIE input)
                if (event.key.key == SDLK_F1 && (event.key.mod & SDL_KMOD_SHIFT) && diagnostics.isPIEActive())
                {
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
                    continue;
                }
                // Gizmo mode shortcuts (W/E/R) – only in editor mode, not when holding right-click, and not when a text entry is focused
                if (!diagnostics.isPIEActive() && !rightMouseDown && renderer && !renderer->getUIManager().hasEntryFocused())
                {
                    if (event.key.key == SDLK_W)
                    {
                        renderer->setGizmoMode(Renderer::GizmoMode::Translate);
                        continue;
                    }
                    if (event.key.key == SDLK_E)
                    {
                        renderer->setGizmoMode(Renderer::GizmoMode::Rotate);
                        continue;
                    }
                    if (event.key.key == SDLK_R)
                    {
                        renderer->setGizmoMode(Renderer::GizmoMode::Scale);
                        continue;
                    }
                }
                // Skip registered key handlers (F2/DELETE) when a text entry is focused
                if (!renderer || !renderer->getUIManager().hasEntryFocused())
                {
                    diagnostics.dispatchKeyUp(event.key.key);
                }
                if (diagnostics.isPIEActive())
                {
                    Scripting::HandleKeyUp(event.key.key);
                }
                if (event.key.key == SDLK_F12)
                {
					fpscap = !fpscap;
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                // Ctrl+Z = Undo, Ctrl+Y = Redo (skip when a text entry is focused)
                if (renderer && !diagnostics.isPIEActive() && (event.key.mod & SDL_KMOD_CTRL) && !renderer->getUIManager().hasEntryFocused())
                {
                    if (event.key.key == SDLK_Z)
                    {
                        auto& undo = UndoRedoManager::Instance();
                        if (undo.canUndo())
                        {
                            undo.undo();
                            renderer->getUIManager().markAllWidgetsDirty();
                        }
                        continue;
                    }
                    if (event.key.key == SDLK_Y)
                    {
                        auto& undo = UndoRedoManager::Instance();
                        if (undo.canRedo())
                        {
                            undo.redo();
                            renderer->getUIManager().markAllWidgetsDirty();
                        }
                        continue;
                    }
                    if (event.key.key == SDLK_S)
                    {
                        // Capture editor camera into the active level before saving
                        auto* lvl = diagnostics.getActiveLevelSoft();
                        if (lvl)
                        {
                            lvl->setEditorCameraPosition(renderer->getCameraPosition());
                            lvl->setEditorCameraRotation(renderer->getCameraRotationDegrees());
                            lvl->setHasEditorCamera(true);
                        }

                        auto& am = AssetManager::Instance();
                        const size_t total = am.getUnsavedAssetCount();
                        if (total > 0)
                        {
                            auto& uiMgr = renderer->getUIManager();
                            uiMgr.showSaveProgressModal(total);
                            am.saveAllAssetsAsync(
                                [&uiMgr = renderer->getUIManager()](size_t saved, size_t t) { uiMgr.updateSaveProgress(saved, t); },
                                [&uiMgr = renderer->getUIManager()](bool ok) {
                                    UndoRedoManager::Instance().clear();
                                    uiMgr.closeSaveProgressModal(ok);
                                });
                        }
                        else
                        {
                            renderer->getUIManager().showToastMessage("Nothing to save.", 2.0f);
                        }
                        continue;
                    }
                }
                if (renderer)
                {
                    auto& uiManager = renderer->getUIManager();
                    if (uiManager.handleKeyDown(event.key.key))
                    {
                        continue;
                    }
                    if (auto* viewportUI = renderer->getViewportUIManagerPtr())
                    {
                        if (viewportUI->handleKeyDown(event.key.key))
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
        }
        const uint64_t eventEndCounter = SDL_GetPerformanceCounter();
        cpuEventMs = (freq > 0.0) ? (static_cast<double>(eventEndCounter - eventStartCounter) / freq * 1000.0) : 0.0;

        const uint64_t inputEndCounter = SDL_GetPerformanceCounter();
        cpuInputMs = (freq > 0.0) ? (static_cast<double>(inputEndCounter - inputStartCounter) / freq * 1000.0) : 0.0;

        if (diagnostics.isShutdownRequested())
        {
            logTimed(Logger::Category::Engine, "Shutdown requested – exiting main loop.", Logger::LogLevel::INFO);
            running = false;
        }

        if (diagnostics.isPIEActive())
        {
            PhysicsWorld::Instance().step(static_cast<float>(dt));
            Scripting::UpdateScripts(static_cast<float>(dt));
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

    diagnostics.saveProjectConfig();
    diagnostics.saveConfig();

    audioManager.shutdown();

    renderer->shutdown();

    delete renderer;

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
