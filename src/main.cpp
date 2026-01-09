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
    auto& assetManager = AssetManager::Instance();
    assetManager.initialize();
    logger.initialize();

    std::string cwd = std::filesystem::current_path().string();
    logger.log("Startup path: " + cwd, Logger::LogLevel::INFO);

    // SDL Initialisierung
    logger.log("Initialising SDL...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        logger.log(std::string("Failed to initialise SDL: ") + SDL_GetError(), Logger::LogLevel::FATAL);
        return -1;
    }
    auto& diagnostics = DiagnosticsManager::Instance();
    diagnostics.setRHIType(DiagnosticsManager::RHIType::OpenGL);
    logger.log("Selected RHI: " + DiagnosticsManager::rhiTypeToString(diagnostics.getRHIType()), Logger::LogLevel::INFO);

    logger.log("SDL initialised successfully.", Logger::LogLevel::INFO);

    logger.log("Initialising Renderer...", Logger::LogLevel::INFO);
    Renderer* renderer = new OpenGLRenderer();

    if (!renderer->initialize())
    {
        logger.log("Failed to initialise renderer.", Logger::LogLevel::FATAL);
        delete renderer;
        SDL_Quit();
        return -1;
    }

    logger.log("Renderer initialised successfully.", Logger::LogLevel::INFO);
#if defined(_WIN32)
    FreeConsole();
#endif
    logger.log("Setup complete.", Logger::LogLevel::INFO);

    if (!assetManager.loadProject("C:/Users/conno/Downloads/SampleProject"))
    {
		assetManager.createProject(cwd, "SampleProject", { "SampleProject", "1.0", "1.0", DiagnosticsManager::RHIType::OpenGL });
    }

    bool running = true;
    logger.log("Entering main loop.", Logger::LogLevel::INFO);
    while (running) 
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) 
        {
            if (event.type == SDL_EVENT_QUIT) 
            {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_UP) 
            {
                if (event.key.key == SDLK_ESCAPE) 
                {
                    running = false;
                }
                if (event.key.key == SDLK_F1)
                {
					assetManager.saveAllAssets();
				}
            }
        }
        renderer->clear();
        renderer->render();
        renderer->present();
        
        SDL_Delay(16);
    }

    delete renderer;
    diagnostics.saveConfig();
    SDL_Quit();
    return 0;
}
