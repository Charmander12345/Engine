#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/OpenGLRenderer/OpenGLRenderer.h"
#include "Renderer/UIWidget.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetTypes.h"
#include "Core/ECS/ECS.h"
#include "Core/MathTypes.h"
#include "Core/AudioManager.h"
#include "Scripting/PythonScripting.h"

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

    if (!Scripting::Initialize())
    {
        logTimed(Logger::Category::Engine, "Failed to initialize Python scripting.", Logger::LogLevel::ERROR);
    }

    auto& assetManager = AssetManager::Instance();
    logTimed(Logger::Category::AssetManagement, "Initialising AssetManager...", Logger::LogLevel::INFO);
        if (!assetManager.initialize())
        {
            logTimed(Logger::Category::AssetManagement, "AssetManager initialisation failed.", Logger::LogLevel::FATAL);
            return -1;
        }

    logTimed(Logger::Category::AssetManagement, "AssetManager initialised successfully.", Logger::LogLevel::INFO);

    std::string cwd = std::filesystem::current_path().string();
    logTimed(Logger::Category::Engine, "Startup path: " + cwd, Logger::LogLevel::INFO);

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

    const std::filesystem::path projectRoot = downloadsPath / "SampleProject";
    logTimed(Logger::Category::Engine, "Loading project...", Logger::LogLevel::INFO);
    if (!assetManager.loadProject(projectRoot.string()))
    {
        logTimed(Logger::Category::Project, "Project not found. Creating default project: SampleProject", Logger::LogLevel::WARNING);
        assetManager.createProject(downloadsPath.string(), "SampleProject", { "SampleProject", "1.0", "1.0", "", DiagnosticsManager::RHIType::OpenGL });
    }

    logTimed(Logger::Category::Engine, "Initialising SDL (video + audio)...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        logTimed(Logger::Category::Engine, std::string("Failed to initialise SDL: ") + SDL_GetError(), Logger::LogLevel::FATAL);
        return -1;
    }
    logTimed(Logger::Category::Engine, "SDL initialised successfully.", Logger::LogLevel::INFO);

    auto& audioManager = AudioManager::Instance();
    logTimed(Logger::Category::Engine, "Initialising AudioManager (OpenAL)...", Logger::LogLevel::INFO);
    if (!audioManager.initialize())
    {
        logTimed(Logger::Category::Engine, "AudioManager initialization failed.", Logger::LogLevel::ERROR);
    }

    auto& diagnostics = DiagnosticsManager::Instance();
    //diagnostics.setRHIType(DiagnosticsManager::RHIType::OpenGL);
    if (!diagnostics.loadConfig())
    {
        diagnostics.setWindowSize(Vec2{ 800.0f, 600.0f });
        diagnostics.setWindowState(DiagnosticsManager::WindowState::Maximized);
    }

    logTimed(Logger::Category::Rendering, "Initialising Renderer (OpenGL)...", Logger::LogLevel::INFO);
    auto* glRenderer = new OpenGLRenderer();
    Renderer* renderer = glRenderer;

    if (!renderer->initialize())
    {
        logTimed(Logger::Category::Rendering, "Failed to initialise renderer.", Logger::LogLevel::FATAL);
        delete renderer;
        SDL_Quit();
        return -1;
    }

    Scripting::SetRenderer(renderer);

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
    SDL_GL_SetSwapInterval(0);

#if defined(_WIN32)
    FreeConsole();
    logger.setSuppressStdout(true);
#endif

    // Show the engine window now that the console is gone.
    if (auto* w = renderer->window())
    {
        SDL_ShowWindow(w);
    }

    std::function<void()> stopPIE;

    if (glRenderer)
    {
        const GLuint playTexId = glRenderer->preloadUITexture("Play.tga");
        const GLuint stopTexId = glRenderer->preloadUITexture("Stop.tga");

        stopPIE = [&glRenderer, playTexId]()
            {
                auto& diag = DiagnosticsManager::Instance();
                if (!diag.isPIEActive())
                {
                    return;
                }
                diag.setPIEActive(false);
                AudioManager::Instance().stopAll();
                Scripting::ReloadScripts();
                auto* level = diag.getActiveLevelSoft();
                if (level)
                {
                    level->restoreEcsSnapshot();
                }
                auto& uiMgr = glRenderer->getUIManager();
                if (auto* el = uiMgr.findElementById("ViewportOverlay.PIE"))
                {
                    el->textureId = playTexId;
                }
                uiMgr.markAllWidgetsDirty();
                uiMgr.refreshWorldOutliner();
                Logger::Instance().log(Logger::Category::Engine, "PIE: stopped.", Logger::LogLevel::INFO);
            };

        glRenderer->getUIManager().registerClickEvent("TitleBar.Close", []()
            {
                Logger::Instance().log(Logger::Category::Input, "TitleBar close button clicked.", Logger::LogLevel::INFO);
                DiagnosticsManager::Instance().requestShutdown();
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Minimize", [&renderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "TitleBar minimize button clicked.", Logger::LogLevel::INFO);
                if (auto* w = renderer->window())
                {
                    SDL_MinimizeWindow(w);
                }
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Maximize", [&renderer]()
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

        glRenderer->getUIManager().registerClickEvent("TitleBar.Menu.File", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: File clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Menu.Edit", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Edit clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Menu.Window", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Window clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Menu.Tools", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Tools clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Menu.Build", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Build clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("TitleBar.Menu.Help", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Menu: Help clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.Select", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Select mode.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.Move", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Move mode.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.Rotate", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Rotate mode.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.Scale", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Scale mode.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.Settings", []()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Settings clicked.", Logger::LogLevel::INFO);
            });

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.PIE", [&glRenderer, playTexId, stopTexId, stopPIE]()
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
                    diag.setPIEActive(true);
                    auto& uiMgr = glRenderer->getUIManager();
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
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
                    {
                                    glRenderer->getUIManager().registerWidget("TitleBar", widget);
                                    }
                                }
                            }
                        }

                        glRenderer->addTab("Viewport", "Viewport", false);

                        glRenderer->getUIManager().registerClickEvent("TitleBar.Tab.Viewport", [&glRenderer]()
                            {
                                glRenderer->setActiveTab("Viewport");
                                glRenderer->getUIManager().markAllWidgetsDirty();
                            });

                        const std::string toolbarPath = assetManager.getEditorWidgetPath("ViewportOverlay.asset");
        if (!toolbarPath.empty())
        {
            const int widgetId = assetManager.loadAsset(toolbarPath, AssetType::Widget, AssetManager::Sync);
            if (widgetId != 0)
            {
                if (auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(widgetId)))
                {
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
                    {
                        glRenderer->getUIManager().registerWidget("ViewportOverlay", widget);
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
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
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
                            picker->color = glRenderer->getClearColor();
                            picker->onColorChanged = [glRenderer](const Vec4& color)
                                {
                                    glRenderer->setClearColor(color);
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

                        glRenderer->getUIManager().registerWidget("WorldSettings", widget);
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
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
                    {
                        glRenderer->getUIManager().registerWidget("WorldOutliner", widget);
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
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
                    {
                        glRenderer->getUIManager().registerWidget("EntityDetails", widget);
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
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
                    {
                        logTimed(Logger::Category::UI, "[ContentBrowser] main: widget created name='" + widget->getName() + "' elements=" + std::to_string(widget->getElements().size()), Logger::LogLevel::INFO);
                        glRenderer->getUIManager().registerWidget("ContentBrowser", widget);
                        logTimed(Logger::Category::UI, "[ContentBrowser] main: registerWidget('ContentBrowser') completed", Logger::LogLevel::INFO);
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

    }

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
        //assetManager.importAssetWithDialog(nullptr, AssetType::Unknown);
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

        auto logTimed = [&](Logger::Category category, const std::string& message, Logger::LogLevel level)
        {
            const uint64_t logStart = SDL_GetPerformanceCounter();
            logger.log(category, message, level);
            const uint64_t logEnd = SDL_GetPerformanceCounter();
            if (freq > 0.0)
            {
                cpuLoggerMs += (static_cast<double>(logEnd - logStart) / freq * 1000.0);
            }
        };

        if (freq > 0.0 && (static_cast<double>(now - lastGcCounter) / freq) >= kGcIntervalSec)
        {
            const uint64_t gcStart = SDL_GetPerformanceCounter();
            assetManager.collectGarbage();
            const uint64_t gcEnd = SDL_GetPerformanceCounter();
            cpuGcMs = (freq > 0.0) ? (static_cast<double>(gcEnd - gcStart) / freq * 1000.0) : 0.0;
            lastGcCounter = now;

			logTimed(Logger::Category::Rendering, "Delta time (dt): " + std::to_string(dt) + " seconds.", Logger::LogLevel::INFO);
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
            if (keys[SDL_SCANCODE_W]) renderer->moveCamera(+moveSpeed, 0.0f, 0.0f);
            if (keys[SDL_SCANCODE_S]) renderer->moveCamera(-moveSpeed, 0.0f, 0.0f);
            if (keys[SDL_SCANCODE_A]) renderer->moveCamera(0.0f, -moveSpeed, 0.0f);
            if (keys[SDL_SCANCODE_D]) renderer->moveCamera(0.0f, +moveSpeed, 0.0f);
            if (keys[SDL_SCANCODE_Q]) renderer->moveCamera(0.0f, 0.0f, -moveSpeed);
            if (keys[SDL_SCANCODE_E]) renderer->moveCamera(0.0f, 0.0f, +moveSpeed);
        }

        if (!hasMousePos)
        {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            SDL_GetMouseState(&mouseX, &mouseY);
            mousePosPixels = Vec2{ mouseX, mouseY };
            hasMousePos = true;
            if (glRenderer)
            {
                auto& uiManager = glRenderer->getUIManager();
                uiManager.setMousePosition(mousePosPixels);
                isOverUI = uiManager.isPointerOverUI(mousePosPixels);
            }
        }

        const uint64_t eventStartCounter = SDL_GetPerformanceCounter();
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
				logTimed(Logger::Category::Input, "SDL_EVENT_QUIT received.", Logger::LogLevel::INFO);
                running = false;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (glRenderer)
                {
                    mousePosPixels = Vec2{ event.motion.x, event.motion.y };
                    hasMousePos = true;
                    auto& uiManager = glRenderer->getUIManager();
                    uiManager.setMousePosition(mousePosPixels);
                    isOverUI = uiManager.isPointerOverUI(mousePosPixels);

                    // Update gizmo drag if active
                    if (glRenderer->isGizmoDragging())
                    {
                        glRenderer->updateGizmoDrag(static_cast<int>(event.motion.x), static_cast<int>(event.motion.y));
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (glRenderer)
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    mousePosPixels = mousePos;
                    hasMousePos = true;
                    auto& uiManager = glRenderer->getUIManager();
                    uiManager.setMousePosition(mousePos);
                    isOverUI = uiManager.isPointerOverUI(mousePos);
                    if (uiManager.handleMouseDown(mousePos, event.button.button))
                    {
                        continue;
                    }
                    if (!isOverUI)
                    {
                        // Try gizmo interaction first; only pick entity if gizmo not hit
                        if (!glRenderer->beginGizmoDrag(static_cast<int>(event.button.x), static_cast<int>(event.button.y)))
                        {
                            glRenderer->requestPick(static_cast<int>(event.button.x), static_cast<int>(event.button.y));
                        }
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                if (glRenderer && glRenderer->isGizmoDragging())
                {
                    glRenderer->endGizmoDrag();
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                if (glRenderer)
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    mousePosPixels = mousePos;
                    hasMousePos = true;
                    auto& uiManager = glRenderer->getUIManager();
                    uiManager.setMousePosition(mousePos);
                    isOverUI = uiManager.isPointerOverUI(mousePos);
                }
                if (!isOverUI)
                {
                    rightMouseDown = true;
                    if (auto* w = renderer->window())
                    {
                        SDL_SetWindowRelativeMouseMode(w, true);
                    }
                    SDL_HideCursor();
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
            {
                rightMouseDown = false;
                if (auto* w = renderer->window())
                {
                    SDL_SetWindowRelativeMouseMode(w, false);
                }
                SDL_ShowCursor();
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                if (glRenderer)
                {
                    auto& uiManager = glRenderer->getUIManager();
                    if (uiManager.handleScroll(mousePosPixels, event.wheel.y))
                    {
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
                }
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && rightMouseDown)
            {
                // Use a frame-rate independent sensitivity (degrees per pixel).
                // Relative mouse motion already represents physical movement, not a per-frame quantity.
                const float sensitivity = 0.12f; // deg per pixel
                renderer->rotateCamera(static_cast<float>(event.motion.xrel) * sensitivity,
                    -static_cast<float>(event.motion.yrel) * sensitivity);
            }

            if (event.type == SDL_EVENT_TEXT_INPUT)
            {
                if (glRenderer)
                {
                    auto& uiManager = glRenderer->getUIManager();
                    if (uiManager.handleTextInput(event.text.text))
                    {
                        continue;
                    }
                }
            }

            if (event.type == SDL_EVENT_KEY_UP)
            {
                if (event.key.key == SDLK_F11)
                {
                    if (glRenderer)
                    {
                        glRenderer->toggleUIDebug();
						logTimed(Logger::Category::Input,
                            std::string("UI debug bounds: ") + (glRenderer->isUIDebugEnabled() ? "ON" : "OFF"),
                            Logger::LogLevel::INFO);
                    }
                    continue;
                }
                if (event.key.key == SDLK_F8)
                {
                    if (glRenderer)
                    {
                        glRenderer->toggleBoundsDebug();
                        logTimed(Logger::Category::Input,
                            std::string("Bounds debug boxes: ") + (glRenderer->isBoundsDebugEnabled() ? "ON" : "OFF"),
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
                // Gizmo mode shortcuts (W/E/R) – only in editor mode
                if (!diagnostics.isPIEActive() && glRenderer)
                {
                    if (event.key.key == SDLK_W)
                    {
                        glRenderer->setGizmoMode(OpenGLRenderer::GizmoMode::Translate);
                        continue;
                    }
                    if (event.key.key == SDLK_E)
                    {
                        glRenderer->setGizmoMode(OpenGLRenderer::GizmoMode::Rotate);
                        continue;
                    }
                    if (event.key.key == SDLK_R)
                    {
                        glRenderer->setGizmoMode(OpenGLRenderer::GizmoMode::Scale);
                        continue;
                    }
                }
                diagnostics.dispatchKeyUp(event.key.key);
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
                if (glRenderer)
                {
                    auto& uiManager = glRenderer->getUIManager();
                    if (uiManager.handleKeyDown(event.key.key))
                    {
                        continue;
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
            running = false;
        }

        if (diagnostics.isPIEActive())
        {
            Scripting::UpdateScripts(static_cast<float>(dt));
        }

        if (glRenderer)
        {
            glRenderer->getUIManager().updateNotifications(static_cast<float>(dt));
        }

        if (glRenderer)
        {
            if (metricsUpdatePending)
            {
                fpsText = "FPS: " + std::to_string(static_cast<int>(fpsValue + 0.5));

                char speedBuf[32];
                std::snprintf(speedBuf, sizeof(speedBuf), "Speed: x%.1f", cameraSpeedMultiplier);
                speedText = speedBuf;
            }

            glRenderer->queueText(fpsText,
                Vec2{ 0.02f, 0.05f },
                0.6f,
                Vec4{ 1.0f, 1.0f, 1.0f, 1.0f });

            glRenderer->queueText(speedText,
                Vec2{ 0.02f, 0.09f },
                0.4f,
                Vec4{ 0.9f, 0.9f, 0.9f, 1.0f });
        }

        const uint64_t renderStartCounter = SDL_GetPerformanceCounter();
        renderer->render();
        renderer->present();
        const uint64_t renderEndCounter = SDL_GetPerformanceCounter();
        cpuRenderMs = (freq > 0.0) ? (static_cast<double>(renderEndCounter - renderStartCounter) / freq * 1000.0) : 0.0;

        const uint64_t frameEndCounter = SDL_GetPerformanceCounter();
        const double cpuFrameMs = (freq > 0.0) ? (static_cast<double>(frameEndCounter - frameStartCounter) / freq * 1000.0) : 0.0;
        cpuOtherMs = std::max(0.0, cpuFrameMs - cpuInputMs - cpuRenderMs);
        if (glRenderer)
        {
            if (metricsUpdatePending)
            {
                char buf[128];

                std::snprintf(buf, sizeof(buf), "CPU: %.3f ms | GPU: %.3f ms",
                    cpuFrameMs, glRenderer->getLastGpuFrameMs());
                cpuText = buf;

                std::snprintf(buf, sizeof(buf), "World: %.3f ms | UI: %.3f ms",
                    glRenderer->getLastCpuRenderWorldMs(), glRenderer->getLastCpuRenderUiMs());
                renderText = buf;

                std::snprintf(buf, sizeof(buf), "UI Layout: %.3f ms | UI Draw: %.3f ms",
                    glRenderer->getLastCpuUiLayoutMs(), glRenderer->getLastCpuUiDrawMs());
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
                    glRenderer->getLastCpuEcsMs());
                ecsText = buf;

                std::snprintf(buf, sizeof(buf), "Frame: %.3f ms", cpuFrameMs);
                frameText = buf;

                std::snprintf(buf, sizeof(buf), "Visible: %u | Hidden: %u | Total: %u",
                    glRenderer->getLastVisibleCount(), glRenderer->getLastHiddenCount(),
                    glRenderer->getLastTotalCount());
                occlusionText = buf;
            }

            if (showMetrics)
            {
                if (!cpuText.empty())
                {
                    glRenderer->queueText(cpuText, Vec2{ 0.02f, 0.13f }, 0.4f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!renderText.empty())
                {
                    glRenderer->queueText(renderText, Vec2{ 0.02f, 0.17f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!uiText.empty())
                {
                    glRenderer->queueText(uiText, Vec2{ 0.02f, 0.21f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!inputText.empty())
                {
                    glRenderer->queueText(inputText, Vec2{ 0.02f, 0.25f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!otherText.empty())
                {
                    glRenderer->queueText(otherText, Vec2{ 0.02f, 0.29f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!gcText.empty())
                {
                    glRenderer->queueText(gcText, Vec2{ 0.02f, 0.33f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!ecsText.empty())
                {
                    glRenderer->queueText(ecsText, Vec2{ 0.02f, 0.37f }, 0.35f, Vec4{ 0.8f, 0.9f, 1.0f, 1.0f });
                }
                if (!frameText.empty())
                {
                    glRenderer->queueText(frameText, Vec2{ 0.02f, 0.41f }, 0.35f, Vec4{ 0.7f, 1.0f, 0.7f, 1.0f });
                }
            }
            if (showOcclusionStats && !occlusionText.empty())
            {
                glRenderer->queueText(occlusionText, Vec2{ 0.02f, 0.45f }, 0.35f, Vec4{ 1.0f, 0.85f, 0.4f, 1.0f });
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
