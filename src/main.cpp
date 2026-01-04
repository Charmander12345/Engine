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

using namespace std;

Renderer* renderer = nullptr;

int main()
{
    auto& logger = Logger::Instance();
    logger.initialize();

    std::string cwd = std::filesystem::current_path().string();
    logger.log("Startup path: " + cwd, Logger::LogLevel::INFO);

    // Log the current working directory
    logger.log("Current working directory: " + std::filesystem::current_path().string(), Logger::LogLevel::INFO);

    // SDL Initialisierung
    logger.log("Initialising SDL...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logger.log(std::string("Failed to initialise SDL: ") + SDL_GetError(), Logger::LogLevel::ERROR);
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
        logger.log("Failed to initialise renderer.", Logger::LogLevel::ERROR);
        return -1;
    }

    logger.log("Renderer initialised successfully.", Logger::LogLevel::INFO);
#if defined(_WIN32)
    FreeConsole();
#endif
    logger.log("Setup complete. Entering main loop.", Logger::LogLevel::INFO);

    bool running = true;
    
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
            }
        }
        renderer->clear();
        renderer->render();
        renderer->present();
        
        SDL_Delay(16.6);
    }

    delete renderer;
	diagnostics.saveConfig();
    SDL_Quit();
    return 0;
}
