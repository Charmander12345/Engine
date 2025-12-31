#include <iostream>
#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/OpenGLRenderer/OpenGLRenderer.h"
#include "Logger/Logger.h"

using namespace std;

int main()
{
    auto& logger = Logger::Instance();
    logger.initialize("engine_log.txt");

    //SDL Initialisierung
    logger.log("Initialisiere SDL...", Logger::LogLevel::INFO);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logger.log(std::string("SDL konnte nicht initialisiert werden: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        return -1;
    }

    logger.log("SDL erfolgreich initialisiert.", Logger::LogLevel::INFO);
    logger.log("Initialisiere Fenster...", Logger::LogLevel::INFO);

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("Engine Project", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_MINIMIZED);
    if (!window) {
        logger.log(std::string("Fenster konnte nicht erstellt werden: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_Quit();
        return -1;
    }

    logger.log("Fenster erfolgreich erstellt.", Logger::LogLevel::INFO);

    logger.log("Erstelle OpenGL Context...", Logger::LogLevel::INFO);

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        logger.log(std::string("OpenGL Kontext konnte nicht erstellt werden: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    if (!SDL_GL_MakeCurrent(window, glContext)) {
        logger.log(std::string("Kontext konnte nicht aktuell gesetzt werden: ") + SDL_GetError(), Logger::LogLevel::ERROR);
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    logger.log("OpenGL Context erfolgreich erstellt.", Logger::LogLevel::INFO);

    logger.log("Initialisiere Renderer...", Logger::LogLevel::INFO);
    Renderer* renderer = new OpenGLRenderer();

    if (!renderer->initialize(window))
    {
        logger.log("GLAD konnte nicht initialisiert werden.", Logger::LogLevel::ERROR);
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }

    logger.log("GLAD erfolgreich initialisiert.", Logger::LogLevel::INFO);
#if defined(_WIN32)
    FreeConsole();
    //DestroyWindow(GetConsoleWindow());
#endif
    logger.log("Setup complete. Entering main loop.", Logger::LogLevel::INFO);

    bool running = true;
    bool consoleOpen = false;
    SDL_ShowWindow(window);
    SDL_RestoreWindow(window);
    
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_UP) {
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
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
