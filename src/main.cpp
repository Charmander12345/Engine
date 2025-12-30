#include <iostream>
#include <chrono>
#include <thread>
#include "Renderer/Renderer.h"
#include <SDL3/SDL.h>
#include <glad/gl.h>

#if defined(_WIN32)
#include <Windows.h>
#endif

using namespace std;

int main()
{
    //SDL Initialisierung
	cout << "Initialisiere SDL..." << endl;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL konnte nicht initialisiert werden: " << SDL_GetError() << std::endl;
        return -1;
    }

	cout << "SDL erfolgreich initialisiert." << endl;
	cout << "Initialisiere Fenster..." << endl;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow("Engine Project", 800, 600, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_MINIMIZED);
    if (!window) {
        std::cerr << "Fenster konnte nicht erstellt werden: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

	cout << "Fenster erfolgreich erstellt." << endl;

	cout << "Erstelle OpenGL Context..." << endl;

	SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "OpenGL Kontext konnte nicht erstellt werden: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
	}

	cout << "OpenGL Context erfolgreich erstellt." << endl;

	cout << "Initialisiere GLAD..." << endl;

	if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) 
    {
        std::cerr << "GLAD konnte nicht initialisiert werden." << std::endl;
        SDL_GL_DestroyContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
	}

	cout << "GLAD erfolgreich initialisiert." << endl;
#if defined(_WIN32)
    FreeConsole();
	//DestroyWindow(GetConsoleWindow());
#endif
	cout << "Setuup complete. Entering main loop." << endl;

    bool running = true;
	SDL_ShowWindow(window);
	SDL_RestoreWindow(window);
    
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_UP) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
			}
        }
        glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        SDL_GL_SwapWindow(window);
        SDL_Delay(16.6);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
