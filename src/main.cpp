#include <iostream>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cstdio>
#include <cmath>
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
#include "AssetManager/HPKArchive.h"
#include "Core/ECS/ECS.h"
#include "Core/MathTypes.h"
#include "Core/AudioManager.h"
#include "Core/EngineLevel.h"
#include "Scripting/Python/PythonScripting.h"
#include "Physics/PhysicsWorld.h"
#include "NativeScripting/NativeScriptManager.h"
#include "NativeScripting/GameplayAPIInternal.h"
#include "NativeScripting/GameplayAPI.h"
#include "Core/Actor/World.h"
#include "CrashProtocol.h"

#include "Renderer/ViewportUIManager.h"
#include "Renderer/UIManager.h"
#include "Renderer/UIWidget.h"
#if ENGINE_EDITOR
#include "Renderer/EditorWindows/TextureViewerWindow.h"
#include "Renderer/EditorTheme.h"
#include "Core/IEditorBridge.h"
#include "Core/EditorBridgeImpl.h"
#include "Editor/EditorApp.h"
#include "Editor/ProjectSelector.h"
#endif
#if !defined(ENGINE_BUILD_SHIPPING)
#include "Core/ShortcutManager.h"
#endif
#include "Core/InputActionManager.h"

using namespace std;


int main()
{
#if defined(_WIN32) && !ENGINE_EDITOR
    // Game build: DLLs live in Engine/ next to the exe's directory.
    // SetDllDirectory must be called before any delay-loaded DLL is used.
    {
        wchar_t exeDir[MAX_PATH]{};
        GetModuleFileNameW(NULL, exeDir, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        std::wstring engineDir = std::wstring(exeDir) + L"Engine";
        SetDllDirectoryW(engineDir.c_str());
    }
#endif

    auto& logger = Logger::Instance();
#if !ENGINE_EDITOR
    // Game build: place logs and tools under Engine/ subdirectory
    logger.setLogDirectory((std::filesystem::current_path() / "Engine" / "Logs").string());
    logger.setToolsDirectory((std::filesystem::current_path() / "Engine" / "Tools").string());
#endif
    logger.initialize();
    Logger::installCrashHandler();
    logger.startCrashHandler();

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
    // Match rtBuildProfile to the compile-time build profile define
#   if defined(ENGINE_BUILD_DEBUG)
    rtBuildProfile = "Debug";
    rtEnableHotReload = true;
    rtEnableProfiler = true;
#   elif defined(ENGINE_BUILD_DEVELOPMENT)
    rtBuildProfile = "Development";
    rtEnableHotReload = true;
    rtEnableProfiler = true;
#   else
    rtBuildProfile = "Shipping";
    rtEnableHotReload = false;
    rtEnableProfiler = false;
#   endif
    logTimed(Logger::Category::Engine, "Runtime mode (compiled-in). Profile=" + rtBuildProfile + " StartLevel=" + rtStartLevel, Logger::LogLevel::INFO);
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
                        else {
                            // Unknown key — treat as engine state (renderer settings etc.)
                            diagnostics.setState(key, val);
                        }
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
    DiagnosticsManager::ScriptingMode chosenScriptingMode = DiagnosticsManager::ScriptingMode::Both;
    bool projectChosen = false;

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

            // Sync working directory with exe directory so that all
            // current_path()-based asset/shader lookups resolve correctly
            // (HPK virtual-path resolution depends on this).
            std::error_code cwdEc;
            std::filesystem::current_path(chosenPath, cwdEc);

            // Re-try loading config/config.ini now that current_path points
            // to the exe directory where the build placed the loose file.
            // Use merge mode so that engine settings already loaded from
            // game.ini (via setState) are preserved — config.ini only fills
            // in keys that game.ini didn't provide (e.g. RHI, WindowSize).
            {
                std::error_code ec2;
                auto cfgFile = std::filesystem::path(chosenPath) / "config" / "config.ini";
                if (std::filesystem::exists(cfgFile, ec2) && !ec2)
                    diagnostics.loadConfig(/*merge=*/true);
            }
        }

        // Apply profile-based log level
        if (rtLogLevel == "verbose")
            Logger::Instance().setMinimumLogLevel(Logger::LogLevel::INFO);
        else if (rtLogLevel == "error")
            Logger::Instance().setMinimumLogLevel(Logger::LogLevel::ERROR);
        else
            Logger::Instance().setMinimumLogLevel(Logger::LogLevel::INFO);

        // Suppress stdout in Shipping builds (compile-time for baked, runtime for game.ini)
#if defined(ENGINE_BUILD_SHIPPING)
        Logger::Instance().setSuppressStdout(true);
#else
        if (rtBuildProfile == "Shipping")
            Logger::Instance().setSuppressStdout(true);
#endif
    }

    // Determine startup mode now that isRuntimeMode is known
    if (isRuntimeMode)
        useSplash = false; // Runtime mode skips splash entirely
    else if (auto v = diagnostics.getState("StartupMode"))
        useSplash = (*v == "normal");

    // Project selection (editor only) — extracted to src/Editor/ProjectSelector.cpp
#if ENGINE_EDITOR
    if (!projectChosen)
    {
        auto selection = showProjectSelection(activeBackend);
        if (selection.cancelled)
        {
            logTimed(Logger::Category::Engine, "Startup project selection cancelled. Exiting engine.", Logger::LogLevel::INFO);
            SDL_Quit();
            return 0;
        }
        chosenPath                  = std::move(selection.path);
        chosenIsNew                 = selection.isNew;
        chosenSetDefault            = selection.setAsDefault;
        chosenIncludeDefaultContent = selection.includeDefaultContent;
        chosenRHI                   = selection.rhi;
        chosenScriptingMode         = selection.scriptingMode;
        projectChosen               = selection.chosen;
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

    // Runtime mode: early-mount HPK so shaders are available during renderer init.
    // AssetManager::loadProject() will mount its own reader later and replace the
    // global singleton; the early reader is then released.
    std::unique_ptr<HPKReader> earlyHpkReader;
    if (isRuntimeMode && !chosenPath.empty())
    {
        // New layout: Content/content.hpk; legacy fallback: content.hpk in root
        auto hpkPath = std::filesystem::path(chosenPath) / "Content" / "content.hpk";
        if (!std::filesystem::exists(hpkPath))
            hpkPath = std::filesystem::path(chosenPath) / "content.hpk";
        if (std::filesystem::exists(hpkPath))
        {
            earlyHpkReader = std::make_unique<HPKReader>();
            if (earlyHpkReader->mount(hpkPath.string()))
            {
                HPKReader::SetMounted(earlyHpkReader.get());
                logTimed(Logger::Category::Engine,
                    "Early-mounted content archive: " + hpkPath.string()
                    + " (" + std::to_string(earlyHpkReader->getFileCount()) + " files)",
                    Logger::LogLevel::INFO);
            }
            else
            {
                logTimed(Logger::Category::Engine,
                    "Failed to early-mount content archive: " + hpkPath.string(),
                    Logger::LogLevel::WARNING);
                earlyHpkReader.reset();
            }
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
        #if ENGINE_EDITOR
        if (auto v = diag.getState("BoundsDebugEnabled"))
        {
            if ((*v == "1") != renderer->isBoundsDebugEnabled())
                renderer->toggleBoundsDebug();
        }
        if (auto v = diag.getState("HeightFieldDebugEnabled"))
            renderer->setHeightFieldDebugEnabled(*v == "1");
#endif
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

    // Runtime mode: apply sensible rendering defaults when no persisted editor config exists.
    // Without these, gamma correction / tone mapping / post-processing stay at renderer
    // defaults (typically off), which makes the scene appear washed-out and too bright.
    if (isRuntimeMode)
    {
        auto& diag = DiagnosticsManager::Instance();
        if (!diag.getState("PostProcessingEnabled"))
            renderer->setPostProcessingEnabled(true);
        if (!diag.getState("GammaCorrectionEnabled"))
            renderer->setGammaCorrectionEnabled(true);
        if (!diag.getState("ToneMappingEnabled"))
            renderer->setToneMappingEnabled(true);
        if (!diag.getState("ShadowsEnabled"))
            renderer->setShadowsEnabled(true);
        if (!diag.getState("CsmEnabled"))
            renderer->setCsmEnabled(true);
        if (!diag.getState("FogEnabled"))
            renderer->setFogEnabled(false);
        if (!diag.getState("BloomEnabled"))
            renderer->setBloomEnabled(false);
        if (!diag.getState("SsaoEnabled"))
            renderer->setSsaoEnabled(false);
        if (!diag.getState("OcclusionCullingEnabled"))
            renderer->setOcclusionCullingEnabled(true);
        if (!diag.getState("AntiAliasingMode"))
            renderer->setAntiAliasingMode(Renderer::AntiAliasingMode::FXAA);
        if (!diag.getState("WireframeEnabled"))
            renderer->setWireframeEnabled(false);
        #if ENGINE_EDITOR
                if (!diag.getState("HeightFieldDebugEnabled"))
                    renderer->setHeightFieldDebugEnabled(false);
        #endif
                if (!diag.getState("TextureCompressionEnabled"))
            renderer->setTextureCompressionEnabled(false);
        if (!diag.getState("TextureStreamingEnabled"))
            renderer->setTextureStreamingEnabled(false);
        if (!diag.getState("DisplacementMappingEnabled"))
            renderer->setDisplacementMappingEnabled(false);
        if (!diag.getState("DisplacementScale"))
            renderer->setDisplacementScale(0.5f);
        if (!diag.getState("TessellationLevel"))
            renderer->setTessellationLevel(16.0f);
        if (!diag.getState("VSyncEnabled"))
            renderer->setVSyncEnabled(true);
    }

    // Send initial hardware + engine info to CrashHandler
    {
        const auto& hw = diagnostics.getHardwareInfo();
        char hwBuf[512];
        std::snprintf(hwBuf, sizeof(hwBuf),
            "CPU=%s (%uC/%uT)\nGPU=%s\nGPU Vendor=%s\nGPU Driver=%s\nVRAM=%lld MB (free: %lld MB)\nRAM=%lld MB (available: %lld MB)\n",
            hw.cpu.brand.c_str(), hw.cpu.physicalCores, hw.cpu.logicalCores,
            hw.gpu.renderer.c_str(), hw.gpu.vendor.c_str(), hw.gpu.driverVersion.c_str(),
            hw.gpu.vramTotalMB, hw.gpu.vramFreeMB,
            hw.ram.totalMB, hw.ram.availableMB);
        std::string hwStr(hwBuf);
        for (size_t i = 0; i < hw.monitors.size(); ++i)
        {
            const auto& mon = hw.monitors[i];
            char monBuf[256];
            std::snprintf(monBuf, sizeof(monBuf),
                "Monitor[%zu]=%s %dx%d@%dHz DPI=%.2f%s\n",
                i, mon.name.c_str(), mon.width, mon.height,
                mon.refreshRate, static_cast<double>(mon.dpiScale),
                mon.primary ? " (primary)" : "");
            hwStr += monBuf;
        }
        logger.sendToCrashHandler(CrashProtocol::Tag::Hardware, hwStr);
        logger.sendToCrashHandler(CrashProtocol::Tag::EngineVer, renderer->name());
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
            renderer->getUIManager().showToastMessage(msg, UIManager::kToastMedium);
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
    GameplayAPI::setRendererInternal(renderer);

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

#if ENGINE_EDITOR
    if (chosenIsNew)
    {
        const std::string parentDir = std::filesystem::path(chosenPath).parent_path().string();
        const std::string projName = std::filesystem::path(chosenPath).filename().string();
        logTimed(Logger::Category::Engine, "Creating new project: " + projName + " at " + parentDir, Logger::LogLevel::INFO);
        if (assetManager.createProject(parentDir, projName, { projName, "1.0", "1.0", "", chosenRHI, chosenScriptingMode }, AssetManager::Sync, chosenIncludeDefaultContent))
        {
            projectLoaded = true;
        }
    }
    else
#endif
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

        // Release the early HPK reader; loadProject has mounted its own.
        if (earlyHpkReader)
        {
            earlyHpkReader.reset();
            logTimed(Logger::Category::Engine, "Released early HPK reader (AssetManager owns it now).", Logger::LogLevel::INFO);
        }
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
#if !defined(ENGINE_BUILD_SHIPPING)
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
#endif // !ENGINE_BUILD_SHIPPING

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

    // ── EditorApp: create the editor lifecycle object ──────────────────
#if ENGINE_EDITOR
    std::unique_ptr<EditorBridgeImpl> editorBridge;
    std::unique_ptr<EditorApp> editorApp;
    if (renderer && !isRuntimeMode)
    {
        editorBridge = std::make_unique<EditorBridgeImpl>(renderer);
        editorApp = std::make_unique<EditorApp>(*editorBridge);

        // Auto-generate VS Code IntelliSense config for new C++ projects
        if (chosenIsNew && (chosenScriptingMode == DiagnosticsManager::ScriptingMode::CppOnly ||
                           chosenScriptingMode == DiagnosticsManager::ScriptingMode::Both))
        {
            editorApp->generateVSCodeConfig(diagnostics.getProjectInfo().projectPath);
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
    // Show metrics in editor and in runtime dev/debug builds (EnableProfiler=true).
    // Completely compiled out in Shipping builds.
#if defined(ENGINE_BUILD_SHIPPING)
    bool showMetrics = false;
    bool showOcclusionStats = false;
#else
    bool showMetrics = !isRuntimeMode || rtEnableProfiler;
    bool showOcclusionStats = false;
#endif

    // --- Phase 3: Load editor UI widgets ---
#if ENGINE_EDITOR
    showProgress("Loading editor UI...");
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

                // Initialize physics (same setup as editor PIE)
                PhysicsWorld::Instance().initialize(PhysicsWorld::Backend::Jolt);
                PhysicsWorld::Instance().setGravity(0.0f, -9.81f, 0.0f);
                PhysicsWorld::Instance().setFixedTimestep(1.0f / 60.0f);
                PhysicsWorld::Instance().setSleepThreshold(0.05f);

                // Runtime mode: physics and scripts run unconditionally (no PIE needed)

                // Apply per-level settings (skybox) to the renderer
                if (auto* newLevel = diagnostics.getActiveLevelSoft())
                {
                    renderer->setSkyboxPath(newLevel->getSkyboxPath());
                }

                // Find the first active CameraComponent and hand control to it.
                // NOTE: ECS entities are created during scene preparation (deferred),
                // so camera assignment is also deferred to the game loop.

                // Capture mouse for game input
                pieMouseCaptured = true;
                pieInputPaused = false;
                if (auto* w = renderer->window())
                {
                    SDL_SetWindowRelativeMouseMode(w, true);
                    SDL_SetWindowMouseGrab(w, true);
                }
                SDL_HideCursor();

                logTimed(Logger::Category::Engine, "Runtime mode: level loaded successfully.", Logger::LogLevel::INFO);

                // Load C++ gameplay scripts DLL (GameScripts.dll in Engine/)
                // NOTE: initializeScripts() is deferred until after scene preparation
                // creates ECS entities from the level JSON (see game loop below).
                {
                    auto dllPath = std::filesystem::path(chosenPath) / "Engine" / "GameScripts.dll";
                    if (std::filesystem::exists(dllPath))
                    {
                        if (NativeScriptManager::Instance().loadGameplayDLL(dllPath.string()))
                        {
                            logTimed(Logger::Category::Engine, "Loaded C++ gameplay DLL: " + dllPath.string(), Logger::LogLevel::INFO);
                        }
                        else
                        {
                            logTimed(Logger::Category::Engine, "Failed to load C++ gameplay DLL: " + dllPath.string(), Logger::LogLevel::ERROR);
                        }
                    }
                    else
                    {
                        logTimed(Logger::Category::Engine, "Runtime: no GameScripts.dll found at " + dllPath.string() + " (no C++ scripts to load).", Logger::LogLevel::INFO);
                    }
                }
            }
            else
            {
                logTimed(Logger::Category::Engine, "Runtime mode: failed to load level - " + loadResult.errorMessage, Logger::LogLevel::ERROR);
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
        if (editorApp)
        {
            // EditorApp::initialize() handles: build pipeline registration,
            // DPI rebuild, widget dirty-marking, and the "Engine ready!" toast.
            editorApp->initialize();
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

    // Deferred native-script initialization: the DLL is loaded before scene
    // preparation, but ECS entities don't exist yet.  Once the renderer's
    // prepareActiveLevel() deserialises the level JSON into ECS, we call
    // initializeScripts() exactly once.
    bool rtScriptsNeedInit = isRuntimeMode && NativeScriptManager::Instance().isDLLLoaded();
    bool rtCameraNeedInit  = isRuntimeMode;

    // Actor system world – ticked alongside native scripts & physics.
    World actorWorld;
    GameplayAPI::setWorld(&actorWorld);

    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    const uint64_t engineStartCounter = lastCounter;

    double fpsTimer = 0.0;
    uint32_t fpsFrames = 0;
    double fpsValue = 0.0;

    #if !defined(ENGINE_BUILD_SHIPPING)
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
#endif

    uint64_t lastGcCounter = lastCounter;
    constexpr double kGcIntervalSec = 60.0;
    uint64_t gcRuns = 0;

    // CrashHandler heartbeat / state sync timer
    double crashHandlerTimer = 0.0;
    constexpr double kCrashHandlerIntervalSec = 2.0;

    bool fpscap = true;
    double cpuInputMs = 0.0;
    double cpuEventMs = 0.0;
    double cpuRenderMs = 0.0;
    double cpuOtherMs = 0.0;
    double cpuLoggerMs = 0.0;
    double cpuGcMs = 0.0;

#if !defined(ENGINE_BUILD_SHIPPING)
    // ─── Register shared keyboard shortcuts with ShortcutManager ───
    // Editor-only shortcuts are registered in EditorApp::registerShortcuts().
    {
        auto& sm = ShortcutManager::Instance();
        using Phase = ShortcutManager::Phase;
        using Mod = ShortcutManager::Mod;

        sm.registerAction("Editor.ToggleMetrics", "Toggle Metrics", "Debug",
            { SDLK_F10, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                showMetrics = !showMetrics;
#if ENGINE_EDITOR
                if (editorApp) editorApp->setShowMetrics(showMetrics);
#endif
                return true;
            });

        sm.registerAction("Editor.ToggleOcclusionStats", "Toggle Occlusion Stats", "Debug",
            { SDLK_F9, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                showOcclusionStats = !showOcclusionStats;
#if ENGINE_EDITOR
                if (editorApp) editorApp->setShowOcclusionStats(showOcclusionStats);
#endif
                return true;
            });

        sm.registerAction("Editor.ToggleFPSCap", "Toggle FPS Cap", "Debug",
            { SDLK_F12, Mod::None }, Phase::KeyUp,
            [&]() -> bool {
                fpscap = !fpscap;
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
#endif // !ENGINE_BUILD_SHIPPING (keyboard shortcuts)

    while (running)
    {
        const uint64_t frameStartCounter = SDL_GetPerformanceCounter();
        ++frame;

        #if ENGINE_EDITOR
                if (editorApp)
                    editorApp->tick(0.0f);

                // Sync PIE state from EditorApp (it's the authoritative source)
                if (editorApp)
                {
                    pieMouseCaptured = editorApp->isPIEMouseCaptured();
                    pieInputPaused = editorApp->isPIEInputPaused();
                    preCaptureMouseX = editorApp->getPreCaptureMouseX();
                    preCaptureMouseY = editorApp->getPreCaptureMouseY();
                    showMetrics = editorApp->isShowMetrics();
                    showOcclusionStats = editorApp->isShowOcclusionStats();
                    cameraSpeedMultiplier = editorApp->getCameraSpeedMultiplier();
                }
        #endif

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

#if !defined(ENGINE_BUILD_SHIPPING)
        metricsUpdateTimer += dt;
        if (metricsUpdateTimer >= kMetricsUpdateIntervalSec)
        {
            metricsUpdateTimer = 0.0;
            metricsUpdatePending = true;
        }

#endif
        // ── CrashHandler heartbeat + state sync (every ~2s) ─────────────
        crashHandlerTimer += dt;
        if (crashHandlerTimer >= kCrashHandlerIntervalSec)
        {
            crashHandlerTimer = 0.0;

            // Check if process is still alive; restart if needed
            logger.ensureCrashHandlerAlive();

            // Heartbeat
            logger.sendToCrashHandler(CrashProtocol::Tag::Heartbeat,
                std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()));

            // Engine state (all DiagnosticsManager states)
            {
                auto states = diagnostics.getStates();
                std::string stateStr;
                for (const auto& [k, v] : states)
                    stateStr += k + "=" + v + "\n";
                if (!stateStr.empty())
                    logger.sendToCrashHandler(CrashProtocol::Tag::EngineState, stateStr);
            }

            // Project info
            if (diagnostics.isProjectLoaded())
            {
                const auto& proj = diagnostics.getProjectInfo();
                auto* level = diagnostics.getActiveLevelSoft();
                std::string levelName = level ? level->getName() : "";
                logger.sendToCrashHandler(CrashProtocol::Tag::Project,
                    proj.projectName + "|" + proj.projectVersion + "|" + proj.projectPath + "|" + levelName);
            }

            // Frame metrics
            {
                const auto& m = diagnostics.getLatestMetrics();
                char fsBuf[256];
                std::snprintf(fsBuf, sizeof(fsBuf),
                    "FPS=%.0f | CPU=%.3fms | GPU=%.3fms | Entities=%u (visible=%u, hidden=%u)",
                    m.fps, m.cpuFrameMs, m.gpuFrameMs, m.totalCount, m.visibleCount, m.hiddenCount);
                logger.sendToCrashHandler(CrashProtocol::Tag::FrameInfo, fsBuf);
            }

            // Uptime
            {
                double uptimeSec = (freq > 0.0) ? (static_cast<double>(SDL_GetPerformanceCounter() - engineStartCounter) / freq) : 0.0;
                char uptimeBuf[64];
                std::snprintf(uptimeBuf, sizeof(uptimeBuf), "%.1f", uptimeSec);
                logger.sendToCrashHandler(CrashProtocol::Tag::Uptime, uptimeBuf);
            }
        }

        audioManager.update();

        // Sync OpenAL listener with active camera for 3D spatial audio
        if (renderer)
        {
            auto& ecs = ECS::ECSManager::Instance();
            unsigned int camEntity = renderer->getActiveCameraEntity();
            if (camEntity != 0)
            {
                const auto* tc = ecs.getComponent<ECS::TransformComponent>(camEntity);
                if (tc)
                {
                    // Rotation is Euler angles in degrees (YXZ order)
                    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
                    float rx = tc->rotation[0] * kDeg2Rad;
                    float ry = tc->rotation[1] * kDeg2Rad;
                    float cx = std::cos(rx), sx = std::sin(rx);
                    float cy = std::cos(ry), sy = std::sin(ry);
                    // Forward = -Z rotated by Y then X
                    float fwdX = -sy * cx;
                    float fwdY =  sx;
                    float fwdZ = -cy * cx;
                    // Up = +Y rotated
                    float upX = sy * sx;
                    float upY = cx;
                    float upZ = cy * sx;
                    audioManager.updateListenerTransform(
                        tc->position[0], tc->position[1], tc->position[2],
                        fwdX, fwdY, fwdZ,
                        upX, upY, upZ);
                }
            }
        }

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
            // In runtime or PIE (and not paused): always move. Outside PIE: require right-click (unless laptop mode).
            const bool canMove = ((isRuntimeMode || inPIE) && pieMouseCaptured && !pieInputPaused) || (!inPIE && !isRuntimeMode && (rightMouseDown || laptopMode));
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
            if (renderer && !((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused))
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
#if ENGINE_EDITOR
			if (editorApp && editorApp->processEvent(event))
			{
				continue;
			}
#endif

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

                    // During active runtime/PIE capture, skip UI updates so editor stays inert (no hover effects)
                    if (!((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused))
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

                        #if ENGINE_EDITOR
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
#endif
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                // During active runtime/PIE capture, ignore left-click so editor UI stays inert
                if ((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused)
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
#if ENGINE_EDITOR
                            auto* texViewer = renderer->getTextureViewer(renderer->getActiveTabId());
                            if (texViewer)
                            {
                                texViewerPanning = true;
                                continue;
                            }
#endif
                        }

                        // PIE/Runtime: recapture mouse
                        if ((isRuntimeMode || diagnostics.isPIEActive()) && pieInputPaused)
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
                        #if ENGINE_EDITOR
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
#endif
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

                if ((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused)
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
#if ENGINE_EDITOR
                            if (renderer->isRubberBandActive())
                                renderer->cancelRubberBand();
#endif
                            continue;
                        }
#if ENGINE_EDITOR
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
#endif
                    }
                }
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                if ((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused)
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
#if ENGINE_EDITOR
                    if (editorApp && isOverUI && editorApp->handleContentBrowserContextMenu(mousePos))
                    {
                        continue;
                    }
#endif // ENGINE_EDITOR
                }
                if (!isOverUI && !diagnostics.isPIEActive() && !isRuntimeMode)
                {
#if ENGINE_EDITOR
                    // Texture viewer: right-click starts panning instead of camera rotation
                    auto* texViewer = renderer->getTextureViewer(renderer->getActiveTabId());
                    if (texViewer)
                    {
                        texViewerPanning = true;
                        continue;
                    }
#endif

                    rightMouseDown = true;
#if ENGINE_EDITOR
                    if (editorApp) editorApp->setRightMouseDown(true);
#endif
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
#if ENGINE_EDITOR
                        if (editorApp) editorApp->setRightMouseDown(false);
#endif
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
#if ENGINE_EDITOR
                    if (editorApp) editorApp->setRightMouseDown(false);
#endif
                }
            }

            if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                if (renderer && !((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused))
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
#if ENGINE_EDITOR
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
#endif

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
#if ENGINE_EDITOR
                    if (editorApp) editorApp->setCameraSpeedMultiplier(cameraSpeedMultiplier);
#endif
                }
            }

            // Texture viewer pan via right-click drag (or left-click in laptop mode)
            if (event.type == SDL_EVENT_MOUSE_MOTION && texViewerPanning)
            {
#if ENGINE_EDITOR
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
#endif
                continue;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION && (rightMouseDown || ((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused)))
            {
                // Block camera rotation when gameplay cursor is visible
                bool cursorBlocksCamera = false;
                if ((isRuntimeMode || diagnostics.isPIEActive()) && pieMouseCaptured && !pieInputPaused)
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
#if !defined(ENGINE_BUILD_SHIPPING)
                if (ShortcutManager::Instance().handleKey(event.key.key, event.key.mod, ShortcutManager::Phase::KeyUp))
                {
                    continue;
                }
#endif // !ENGINE_BUILD_SHIPPING
                if (isRuntimeMode || diagnostics.isPIEActive())
                {
                    Scripting::HandleKeyUp(event.key.key);
                    InputActionManager::Instance().handleKeyUp(event.key.key, event.key.mod);
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
#if !defined(ENGINE_BUILD_SHIPPING)
                if (ShortcutManager::Instance().handleKey(event.key.key, event.key.mod, ShortcutManager::Phase::KeyDown))
                {
                    continue;
                }
#endif // !ENGINE_BUILD_SHIPPING

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
                if (isRuntimeMode || diagnostics.isPIEActive())
                {
                    Scripting::HandleKeyDown(event.key.key);
                    InputActionManager::Instance().handleKeyDown(event.key.key, event.key.mod);
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

            #if ENGINE_EDITOR
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
                            renderer->getUIManager().showToastMessage("Cannot import: no project loaded.", UIManager::kToastMedium);
                    }
                    else
                    {
                        auto action = diag.registerAction(DiagnosticsManager::ActionType::ImportingAsset);
                        am.importAssetFromPath(filePath, AssetType::Unknown, action.ID);
                        if (renderer)
                        {
                            const std::string fileName = std::filesystem::path(filePath).filename().string();
                            renderer->getUIManager().showToastMessage("Importing: " + fileName, UIManager::kToastMedium);
                        }
                    }
                }
            }
#endif // ENGINE_EDITOR
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

        if (isRuntimeMode || diagnostics.isPIEActive())
        {
            // Deferred native-script init: run once after scene preparation
            // has created ECS entities from the level JSON.
            if (rtScriptsNeedInit && diagnostics.isScenePrepared())
            {
                NativeScriptManager::Instance().initializeScripts();
                rtScriptsNeedInit = false;
            }

            // Deferred camera assignment: find the first active CameraComponent
            // now that ECS entities exist after scene preparation.
            if (rtCameraNeedInit && diagnostics.isScenePrepared() && renderer)
            {
                auto& ecs = ECS::ECSManager::Instance();
                ECS::Schema camSchema;
                camSchema.require<ECS::CameraComponent>().require<ECS::TransformComponent>();
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
                if (activeCamEntity == 0 && !camEntities.empty())
                    activeCamEntity = static_cast<unsigned int>(camEntities.front());
                if (activeCamEntity != 0)
                {
                    renderer->setActiveCameraEntity(activeCamEntity);
                    logTimed(Logger::Category::Engine,
                        "Runtime: using entity camera " + std::to_string(activeCamEntity),
                        Logger::LogLevel::INFO);
                }
                rtCameraNeedInit = false;
            }

            NativeScriptManager::Instance().updateScripts(static_cast<float>(dt));

            // Pre-physics tick group (gameplay logic that must run before physics)
            actorWorld.tickGroup(ETickGroup::PrePhysics, static_cast<float>(dt));

            PhysicsWorld::Instance().step(static_cast<float>(dt));

            // During-physics tick group (actors that tick alongside physics)
            actorWorld.tickGroup(ETickGroup::DuringPhysics, static_cast<float>(dt));

            // Dispatch physics overlap events to C++ native scripts
            {
                auto& physics = PhysicsWorld::Instance();
                auto& nativeScripts = NativeScriptManager::Instance();
                for (const auto& ev : physics.getBeginOverlapEvents())
                {
                    nativeScripts.dispatchBeginOverlap(static_cast<ECS::Entity>(ev.entityA), static_cast<ECS::Entity>(ev.entityB));
                    nativeScripts.dispatchBeginOverlap(static_cast<ECS::Entity>(ev.entityB), static_cast<ECS::Entity>(ev.entityA));
                    actorWorld.dispatchBeginOverlap(static_cast<ECS::Entity>(ev.entityA), static_cast<ECS::Entity>(ev.entityB));
                }
                for (const auto& ev : physics.getEndOverlapEvents())
                {
                    nativeScripts.dispatchEndOverlap(static_cast<ECS::Entity>(ev.entityA), static_cast<ECS::Entity>(ev.entityB));
                    nativeScripts.dispatchEndOverlap(static_cast<ECS::Entity>(ev.entityB), static_cast<ECS::Entity>(ev.entityA));
                    actorWorld.dispatchEndOverlap(static_cast<ECS::Entity>(ev.entityA), static_cast<ECS::Entity>(ev.entityB));
                }
            }

            // Post-physics tick group (most actors default here)
            actorWorld.tickGroup(ETickGroup::PostPhysics, static_cast<float>(dt));

            // Post-update-work tick group (final cleanup / camera follow)
            actorWorld.tickGroup(ETickGroup::PostUpdateWork, static_cast<float>(dt));

            // Flush deferred actor destructions
            actorWorld.flushDestroys();

            Scripting::UpdateScripts(static_cast<float>(dt));
        }

        // Poll for .py file changes and hot-reload if needed (self-throttled to 500ms)
#if !defined(ENGINE_BUILD_SHIPPING)
        Scripting::PollScriptHotReload();
#endif
#if ENGINE_EDITOR
        Scripting::PollPluginHotReload();
#endif

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
#if !defined(ENGINE_BUILD_SHIPPING)
            // Only show performance stats on the Viewport tab, not in Mesh Viewer tabs
#if ENGINE_EDITOR
            const bool isViewportTab = (renderer->getActiveTabId() == "Viewport");
#else
            const bool isViewportTab = true;
#endif

            if (metricsUpdatePending)
            {
                fpsText = "FPS: " + std::to_string(static_cast<int>(fpsValue + 0.5));

                char speedBuf[32];
                std::snprintf(speedBuf, sizeof(speedBuf), "Speed: x%.1f", cameraSpeedMultiplier);
                speedText = speedBuf;
            }

            if (showMetrics && isViewportTab)
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
#endif // !ENGINE_BUILD_SHIPPING
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
#if !defined(ENGINE_BUILD_SHIPPING)
#if ENGINE_EDITOR
            const bool isViewportTab = (renderer->getActiveTabId() == "Viewport");
#else
            const bool isViewportTab = true;
#endif

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
#endif // !ENGINE_BUILD_SHIPPING
        }

#if !defined(ENGINE_BUILD_SHIPPING)
        if (metricsUpdatePending)
        {
            metricsUpdatePending = false;
        }
#endif

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

        const SDL_WindowFlags flags = SDL_GetWindowFlags(w);
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

    // Editor shutdown: save camera, level, shortcuts, configs
#if ENGINE_EDITOR
    if (editorApp)
    {
        editorApp->shutdown();
    }
#if !defined(ENGINE_BUILD_SHIPPING)
    if (!isRuntimeMode)
    {
        const auto& projPath = diagnostics.getProjectInfo().projectPath;
        if (!projPath.empty())
        {
            const std::string shortcutFile = (std::filesystem::path(projPath) / "shortcuts.cfg").string();
            ShortcutManager::Instance().saveToFile(shortcutFile);
        }
    }
#endif // !ENGINE_BUILD_SHIPPING
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

#if !defined(ENGINE_BUILD_SHIPPING)
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
#endif // !ENGINE_BUILD_SHIPPING

    NativeScriptManager::Instance().shutdownScripts();
    NativeScriptManager::Instance().unloadGameplayDLL();
    logTimed(Logger::Category::Engine, "Engine shutdown complete.", Logger::LogLevel::INFO);
    Scripting::Shutdown();
    logger.stopCrashHandler();
    return 0;
}
