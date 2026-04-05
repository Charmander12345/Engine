#if ENGINE_EDITOR

#include "ProjectSelector.h"

#include <filesystem>
#include <cstdlib>
#include <SDL3/SDL.h>

#include "../Renderer/Renderer.h"
#include "../Renderer/RendererFactory.h"
#include "../Renderer/UIManager.h"
#include "../Logger/Logger.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Core/MathTypes.h"

// ---------------------------------------------------------------------------
ProjectSelection showProjectSelection(RendererBackend backend)
{
    auto& diagnostics = DiagnosticsManager::Instance();

    auto logTimed = [](Logger::Category category, const std::string& message, Logger::LogLevel level)
    {
        Logger::Instance().log(category, message, level);
    };

    ProjectSelection result;

    // 1. Check for a persisted default project ---------------------------------
    {
        auto defaultProj = diagnostics.getState("DefaultProject");
        if (defaultProj && !defaultProj->empty() && std::filesystem::exists(*defaultProj))
        {
            result.path    = *defaultProj;
            result.chosen  = true;
            return result;
        }
    }

    // 2. Show project selection screen via a temporary renderer -----------------
    logTimed(Logger::Category::Engine,
             "No valid default project found. Opening project selection...",
             Logger::LogLevel::INFO);

    auto* tempRenderer = RendererFactory::createRenderer(backend);
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
            [&](const std::string& path, bool isNew, bool setAsDefault,
                bool includeDefaultContent, DiagnosticsManager::RHIType selectedRHI,
                DiagnosticsManager::ScriptingMode scriptingMode)
            {
                result.path                  = path;
                result.isNew                 = isNew;
                result.setAsDefault          = setAsDefault;
                result.includeDefaultContent  = includeDefaultContent;
                result.rhi                   = selectedRHI;
                result.scriptingMode         = scriptingMode;
                result.chosen                = true;
            });

        bool tempWindowOpen = true;
        while (!result.chosen && tempWindowOpen)
        {
            auto& tempUi = tempRenderer->getUIManager();
            SDL_Event ev;
            while (SDL_PollEvent(&ev))
            {
                if (ev.type == SDL_EVENT_QUIT)
                {
                    diagnostics.requestShutdown();
                    result.cancelled = true;
                    tempWindowOpen   = false;
                    break;
                }

                if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    tempWindowId != 0 && ev.window.windowID == tempWindowId)
                {
                    diagnostics.requestShutdown();
                    result.cancelled = true;
                    tempWindowOpen   = false;
                    break;
                }

                if (ev.type == SDL_EVENT_MOUSE_MOTION)
                {
                    tempUi.setMousePosition(Vec2{ ev.motion.x, ev.motion.y });
                }

                if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                {
                    const Vec2 mousePos{ static_cast<float>(ev.button.x),
                                         static_cast<float>(ev.button.y) };
                    tempUi.setMousePosition(mousePos);
                    if (tempUi.handleMouseDown(mousePos, ev.button.button))
                        continue;
                }

                if (ev.type == SDL_EVENT_MOUSE_BUTTON_UP)
                {
                    const Vec2 mousePos{ static_cast<float>(ev.button.x),
                                         static_cast<float>(ev.button.y) };
                    tempUi.setMousePosition(mousePos);
                    if (tempUi.handleMouseUp(mousePos, ev.button.button))
                        continue;
                }

                if (ev.type == SDL_EVENT_MOUSE_WHEEL)
                {
                    if (tempUi.handleScroll(tempUi.getMousePosition(), ev.wheel.y))
                        continue;
                }

                if (ev.type == SDL_EVENT_TEXT_INPUT)
                {
                    if (tempUi.handleTextInput(ev.text.text))
                        continue;
                }

                if (ev.type == SDL_EVENT_KEY_DOWN)
                {
                    if (tempUi.handleKeyDown(ev.key.key))
                        continue;
                }

                if (tempRenderer->routeEventToPopup(ev))
                    continue;
            }

            if (!tempWindowOpen || diagnostics.isShutdownRequested())
            {
                result.cancelled = true;
                tempWindowOpen   = false;
                logTimed(Logger::Category::Engine,
                         "Project selection window was closed without choosing a project.",
                         Logger::LogLevel::INFO);
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

    // 3. Cancelled? Return early -----------------------------------------------
    if (result.cancelled || diagnostics.isShutdownRequested())
    {
        result.cancelled = true;
        return result;
    }

    // 4. Fallback: SampleProject -----------------------------------------------
    if (!result.chosen)
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

        result.path                  = (downloadsPath / "SampleProject").string();
        result.isNew                 = !std::filesystem::exists(result.path);
        result.setAsDefault          = true;
        result.includeDefaultContent = true;
        result.chosen                = true;
        logTimed(Logger::Category::Engine,
                 "Fallback to SampleProject: " + result.path,
                 Logger::LogLevel::INFO);
    }

    return result;
}

#endif // ENGINE_EDITOR
