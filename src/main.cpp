#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/OpenGLRenderer/OpenGLRenderer.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetTypes.h"
#include "Core/ECS/ECS.h"
#include "Core/MathTypes.h"

using namespace std;


int main()
{
    auto& logger = Logger::Instance();
    logger.initialize();

    logger.log(Logger::Category::Engine, "Engine starting...", Logger::LogLevel::INFO);

    auto& assetManager = AssetManager::Instance();
    logger.log(Logger::Category::AssetManagement, "Initialising AssetManager...", Logger::LogLevel::INFO);
    if (!assetManager.initialize())
    {
        logger.log(Logger::Category::AssetManagement, "AssetManager initialisation failed.", Logger::LogLevel::FATAL);
        return -1;
    }

    logger.log(Logger::Category::AssetManagement, "AssetManager initialised successfully.", Logger::LogLevel::INFO);

    std::string cwd = std::filesystem::current_path().string();
    logger.log(Logger::Category::Engine, "Startup path: " + cwd, Logger::LogLevel::INFO);

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
    logger.log(Logger::Category::Engine, "Loading project...", Logger::LogLevel::INFO);
    if (!assetManager.loadProject(projectRoot.string()))
    {
        logger.log(Logger::Category::Project, "Project not found. Creating default project: SampleProject", Logger::LogLevel::WARNING);
        assetManager.createProject(downloadsPath.string(), "SampleProject", { "SampleProject", "1.0", "1.0", "", DiagnosticsManager::RHIType::OpenGL });
    }

    logger.log(Logger::Category::Engine, "Initialising SDL (video + audio)...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        logger.log(Logger::Category::Engine, std::string("Failed to initialise SDL: ") + SDL_GetError(), Logger::LogLevel::FATAL);
        return -1;
    }
    logger.log(Logger::Category::Engine, "SDL initialised successfully.", Logger::LogLevel::INFO);

    auto& diagnostics = DiagnosticsManager::Instance();
    //diagnostics.setRHIType(DiagnosticsManager::RHIType::OpenGL);
    if (!diagnostics.loadConfig())
    {
        diagnostics.setWindowSize(Vec2{ 800.0f, 600.0f });
        diagnostics.setWindowState(DiagnosticsManager::WindowState::Maximized);
    }

    logger.log(Logger::Category::Rendering, "Initialising Renderer (OpenGL)...", Logger::LogLevel::INFO);
    Renderer* renderer = new OpenGLRenderer();

    if (!renderer->initialize())
    {
        logger.log(Logger::Category::Rendering, "Failed to initialise renderer.", Logger::LogLevel::FATAL);
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
    }

    logger.log(Logger::Category::Rendering, std::string("Renderer initialised successfully: ") + renderer->name(), Logger::LogLevel::INFO);

#if defined(_WIN32)
    FreeConsole();
#endif

    auto& ecs = ECS::ECSManager::Instance();
    ecs.initialize({});
    ecs.createEntity();

    if (auto* glRenderer = dynamic_cast<OpenGLRenderer*>(renderer))
    {
        glRenderer->getUIManager().registerClickEvent("TitleBar.Close", []()
            {
                Logger::Instance().log(Logger::Category::Input, "TitleBar close button clicked.", Logger::LogLevel::INFO);
                DiagnosticsManager::Instance().requestShutdown();
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
    }

    bool running = true;
    uint64_t frame = 0;
    logger.log(Logger::Category::Engine, "Entering main loop.", Logger::LogLevel::INFO);

    diagnostics.registerKeyUpHandler(SDLK_ESCAPE, [&]() {
        logger.log(Logger::Category::Input, "Escape pressed - exiting.", Logger::LogLevel::INFO);
        running = false;
        return true;
        });

    diagnostics.registerKeyUpHandler(SDLK_F1, [&]() {
        logger.log(Logger::Category::Input, "F1 pressed - saving all assets (async).", Logger::LogLevel::INFO);
        //assetManager.saveAllAssetsAsync();
        return true;
        });

    diagnostics.registerKeyUpHandler(SDLK_F2, [&]() {
        logger.log(Logger::Category::Input, "F2 pressed - opening import dialog.", Logger::LogLevel::INFO);
        //assetManager.importAssetWithDialog(nullptr, AssetType::Unknown);
        return true;
        });


    bool rightMouseDown = false;
    float cameraSpeedMultiplier = 1.0f;

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    double fpsTimer = 0.0;
    uint32_t fpsFrames = 0;
    double fpsValue = 0.0;

    uint64_t lastGcCounter = lastCounter;
    constexpr double kGcIntervalSec = 60.0;
    uint64_t gcRuns = 0;

    bool fpscap = true;

    while (running)
    {
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

        if (freq > 0.0 && (static_cast<double>(now - lastGcCounter) / freq) >= kGcIntervalSec)
        {
            assetManager.collectGarbage();
            lastGcCounter = now;


			logger.log(Logger::Category::Rendering, "Delta time (dt): " + std::to_string(dt) + " seconds.", Logger::LogLevel::INFO);
            ++gcRuns;
            if ((gcRuns % 12) == 0)
            {
                logger.log(Logger::Category::AssetManagement, "Periodic GC runs=" + std::to_string(gcRuns), Logger::LogLevel::INFO);
            }
        }

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

        Vec2 mousePosPixels{};
        bool isOverUI = false;
        if (auto* glRenderer = dynamic_cast<OpenGLRenderer*>(renderer))
        {
            float mouseX = 0.0f;
            float mouseY = 0.0f;
            SDL_GetMouseState(&mouseX, &mouseY);
            mousePosPixels = Vec2{ mouseX, mouseY };
            auto& uiManager = glRenderer->getUIManager();
            uiManager.setMousePosition(mousePosPixels);
            isOverUI = uiManager.isPointerOverUI(mousePosPixels);
        }

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                logger.log(Logger::Category::Input, "SDL_EVENT_QUIT received.", Logger::LogLevel::INFO);
                running = false;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (auto* glRenderer = dynamic_cast<OpenGLRenderer*>(renderer))
                {
                    mousePosPixels = Vec2{ event.motion.x, event.motion.y };
                    auto& uiManager = glRenderer->getUIManager();
                    uiManager.setMousePosition(mousePosPixels);
                    isOverUI = uiManager.isPointerOverUI(mousePosPixels);
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (auto* glRenderer = dynamic_cast<OpenGLRenderer*>(renderer))
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    auto& uiManager = glRenderer->getUIManager();
                    uiManager.setMousePosition(mousePos);
                    if (uiManager.handleMouseDown(mousePos, event.button.button))
                    {
                        continue;
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
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

            if (event.type == SDL_EVENT_MOUSE_WHEEL && rightMouseDown)
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

            if (event.type == SDL_EVENT_MOUSE_MOTION && rightMouseDown)
            {
                // Use a frame-rate independent sensitivity (degrees per pixel).
                // Relative mouse motion already represents physical movement, not a per-frame quantity.
                const float sensitivity = 0.12f; // deg per pixel
                renderer->rotateCamera(static_cast<float>(event.motion.xrel) * sensitivity,
                    -static_cast<float>(event.motion.yrel) * sensitivity);
            }

            if (event.type == SDL_EVENT_KEY_UP)
            {
                if (event.key.key == SDLK_F11)
                {
                    if (auto* glRenderer = dynamic_cast<OpenGLRenderer*>(renderer))
                    {
                        glRenderer->toggleUIDebug();
                        Logger::Instance().log(Logger::Category::Input,
                            std::string("UI debug bounds: ") + (glRenderer->isUIDebugEnabled() ? "ON" : "OFF"),
                            Logger::LogLevel::INFO);
                    }
                    continue;
                }
                diagnostics.dispatchKeyUp(event.key.key);
                if (event.key.key == SDLK_F12)
                {
					fpscap = !fpscap;
					SDL_GL_SetSwapInterval(fpscap ? 1 : 0);
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                diagnostics.dispatchKeyDown(event.key.key);
            }
        }

        if (diagnostics.isShutdownRequested())
        {
            running = false;
        }

        if (auto* glRenderer = dynamic_cast<OpenGLRenderer*>(renderer))
        {
            glRenderer->queueText("FPS: " + std::to_string(static_cast<int>(fpsValue + 0.5)),
                Vec2{ 0.02f, 0.05f },
                0.6f,
                Vec4{ 1.0f, 1.0f, 1.0f, 1.0f });

            std::ostringstream speedStream;
            speedStream << std::fixed << std::setprecision(1) << cameraSpeedMultiplier;
            glRenderer->queueText("Speed: x" + speedStream.str(),
                Vec2{ 0.02f, 0.09f },
                0.4f,
                Vec4{ 0.9f, 0.9f, 0.9f, 1.0f });
        }

        renderer->clear();
        renderer->render();
        renderer->present();

        if (fpscap)
        {
            SDL_Delay(16);
        }

        if ((frame % 600) == 0)
        {
            logger.log(Logger::Category::Engine, "Heartbeat: frame=" + std::to_string(frame), Logger::LogLevel::INFO);
        }
    }

    logger.log(Logger::Category::Engine, "Shutting down...", Logger::LogLevel::INFO);

	while (diagnostics.isActionInProgress())
    {
        logger.log(Logger::Category::Engine, "Waiting for ongoing actions to complete...", Logger::LogLevel::INFO);
        SDL_Delay(100);
    }

    logger.log(Logger::Category::Diagnostics, "Saving configs...", Logger::LogLevel::INFO);
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

    renderer->shutdown();

    delete renderer;

    logger.log(Logger::Category::Engine, "SDL_Quit()", Logger::LogLevel::INFO);
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

    logger.log(Logger::Category::Engine, "Engine shutdown complete.", Logger::LogLevel::INFO);
    return 0;
}
