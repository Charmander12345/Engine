#include <iostream>
#include <filesystem>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/OpenGLRenderer/OpenGLRenderer.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetManager.h"

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

    logger.log(Logger::Category::Engine, "Loading project...", Logger::LogLevel::INFO);
    if (!assetManager.loadProject("SampleProject"))
    {
        logger.log(Logger::Category::Project, "Project not found. Creating default project: SampleProject", Logger::LogLevel::WARNING);
        assetManager.createProject(cwd, "SampleProject", { "SampleProject", "1.0", "1.0", "", DiagnosticsManager::RHIType::OpenGL });
    }

    logger.log(Logger::Category::Engine, "Initialising SDL (video + audio)...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        logger.log(Logger::Category::Engine, std::string("Failed to initialise SDL: ") + SDL_GetError(), Logger::LogLevel::FATAL);
        return -1;
    }
    logger.log(Logger::Category::Engine, "SDL initialised successfully.", Logger::LogLevel::INFO);

    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setRHIType(DiagnosticsManager::RHIType::OpenGL);
    logger.log(Logger::Category::Diagnostics,
        std::string("Selected RHI: ") + DiagnosticsManager::rhiTypeToString(diagnostics.getRHIType()),
        Logger::LogLevel::INFO);

    logger.log(Logger::Category::Rendering, "Initialising Renderer (OpenGL)...", Logger::LogLevel::INFO);
    Renderer* renderer = new OpenGLRenderer();

    if (!renderer->initialize())
    {
        logger.log(Logger::Category::Rendering, "Failed to initialise renderer.", Logger::LogLevel::FATAL);
        delete renderer;
        SDL_Quit();
        return -1;
    }

    logger.log(Logger::Category::Rendering, std::string("Renderer initialised successfully: ") + renderer->name(), Logger::LogLevel::INFO);

#if defined(_WIN32)
    FreeConsole();
#endif

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
        assetManager.saveAllAssetsAsync();
        return true;
        });

    diagnostics.registerKeyUpHandler(SDLK_F2, [&]() {
        logger.log(Logger::Category::Input, "F2 pressed - opening import dialog.", Logger::LogLevel::INFO);
        assetManager.importAssetWithDialog(nullptr, AssetType::Unknown);
        return true;
        });

    bool rightMouseDown = false;

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running)
    {
        ++frame;

        const uint64_t now = SDL_GetPerformanceCounter();
        const double dt = (freq > 0.0) ? (static_cast<double>(now - lastCounter) / freq) : 0.016;
        lastCounter = now;

        // Basic movement (world-space for now)
        const float moveSpeed = static_cast<float>(3.0 * dt); // units/sec
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys)
        {
            if (keys[SDL_SCANCODE_W]) renderer->moveCamera(0.0f, 0.0f, -moveSpeed);
            if (keys[SDL_SCANCODE_S]) renderer->moveCamera(0.0f, 0.0f, +moveSpeed);
            if (keys[SDL_SCANCODE_A]) renderer->moveCamera(-moveSpeed, 0.0f, 0.0f);
            if (keys[SDL_SCANCODE_D]) renderer->moveCamera(+moveSpeed, 0.0f, 0.0f);
            if (keys[SDL_SCANCODE_Q]) renderer->moveCamera(0.0f, -moveSpeed, 0.0f);
            if (keys[SDL_SCANCODE_E]) renderer->moveCamera(0.0f, +moveSpeed, 0.0f);
        }

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                logger.log(Logger::Category::Input, "SDL_EVENT_QUIT received.", Logger::LogLevel::INFO);
                running = false;
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                rightMouseDown = true;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
            {
                rightMouseDown = false;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && rightMouseDown)
            {
                const float sensitivity = static_cast<float>(12.0 * dt); // deg/sec per pixel
                renderer->rotateCamera(static_cast<float>(event.motion.xrel) * sensitivity,
                    -static_cast<float>(event.motion.yrel) * sensitivity);
            }

            if (event.type == SDL_EVENT_KEY_UP)
            {
                diagnostics.dispatchKeyUp(event.key.key);
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                diagnostics.dispatchKeyDown(event.key.key);
            }
        }

        assetManager.pump();

        renderer->clear();
        renderer->render();
        renderer->present();

        SDL_Delay(16);

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

    delete renderer;

    logger.log(Logger::Category::Diagnostics, "Saving configs...", Logger::LogLevel::INFO);
    diagnostics.saveProjectConfig();
    diagnostics.saveConfig();

    logger.log(Logger::Category::Engine, "SDL_Quit()", Logger::LogLevel::INFO);
    SDL_Quit();

    logger.log(Logger::Category::Engine, "Engine shutdown complete.", Logger::LogLevel::INFO);
    return 0;
}
