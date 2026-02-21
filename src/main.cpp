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
#include "Renderer/PopupWindow.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetManager.h"
#include "AssetManager/AssetTypes.h"
#include "Core/ECS/ECS.h"
#include "Core/MathTypes.h"
#include "Core/AudioManager.h"
#include "Core/UndoRedoManager.h"
#include "Scripting/PythonScripting.h"
#include "LandscapeManager.h"

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
                glRenderer->clearActiveCameraEntity();
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

        glRenderer->getUIManager().registerClickEvent("WorldSettings.Tools.Landscape", [&glRenderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "WorldSettings: Tools -> Landscape Manager.", Logger::LogLevel::INFO);

                // ---- Open or focus the popup --------------------------------
                constexpr int kPopupW = 420;
                constexpr int kPopupH = 340;
                PopupWindow* popup = glRenderer->openPopupWindow(
                    "LandscapeManager", "Landscape Manager", kPopupW, kPopupH);
                if (!popup) return;

                // Don't rebuild the UI if it already has widgets.
                if (!popup->uiManager().getRegisteredWidgets().empty()) return;

                // ---- Shared mutable state for the form ----------------------
                struct LandscapeFormState
                {
                    std::string name        = "Landscape";
                    std::string widthStr    = "100";
                    std::string depthStr    = "100";
                    std::string subdivXStr  = "32";
                    std::string subdivZStr  = "32";
                };
                auto state = std::make_shared<LandscapeFormState>();

                // ---- Build Widget with explicit normalized coordinates -------
                // Popup size: 420 x 340
                const float W = static_cast<float>(kPopupW);
                const float H = static_cast<float>(kPopupH);

                // Helper lambdas for normalized coords
                auto nx = [&](float px) { return px / W; };
                auto ny = [&](float py) { return py / H; };

                const auto makeLabel = [&](const std::string& id, const std::string& text,
                    float x0, float y0, float x1, float y1) -> WidgetElement
                {
                    WidgetElement e;
                    e.type      = WidgetElementType::Text;
                    e.id        = id;
                    e.from      = Vec2{ nx(x0), ny(y0) };
                    e.to        = Vec2{ nx(x1), ny(y1) };
                    e.text      = text;
                    e.fontSize  = 13.0f;
                    e.textColor = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
                    e.textAlignV = TextAlignV::Center;
                    e.padding   = Vec2{ 6.0f, 0.0f };
                    return e;
                };

                const auto makeEntry = [&](const std::string& id, const std::string& val,
                    float x0, float y0, float x1, float y1) -> WidgetElement
                {
                    WidgetElement e;
                    e.type         = WidgetElementType::EntryBar;
                    e.id           = id;
                    e.from         = Vec2{ nx(x0), ny(y0) };
                    e.to           = Vec2{ nx(x1), ny(y1) };
                    e.value        = val;
                    e.fontSize     = 13.0f;
                    e.color        = Vec4{ 0.18f, 0.18f, 0.22f, 1.0f };
                    e.hoverColor   = Vec4{ 0.22f, 0.22f, 0.27f, 1.0f };
                    e.textColor    = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
                    e.padding      = Vec2{ 6.0f, 4.0f };
                    e.isHitTestable = true;
                    return e;
                };

                std::vector<WidgetElement> elements;

                // Background
                {
                    WidgetElement bg;
                    bg.type  = WidgetElementType::Panel;
                    bg.id    = "LM.Bg";
                    bg.from  = Vec2{ 0.0f, 0.0f };
                    bg.to    = Vec2{ 1.0f, 1.0f };
                    bg.color = Vec4{ 0.13f, 0.13f, 0.16f, 1.0f };
                    elements.push_back(bg);
                }

                // Title bar
                {
                    WidgetElement title;
                    title.type  = WidgetElementType::Panel;
                    title.id    = "LM.TitleBg";
                    title.from  = Vec2{ 0.0f, 0.0f };
                    title.to    = Vec2{ 1.0f, ny(44.0f) };
                    title.color = Vec4{ 0.09f, 0.09f, 0.13f, 1.0f };
                    elements.push_back(title);

                    elements.push_back(makeLabel("LM.TitleText", "Landscape Manager",
                        8.0f, 0.0f, W - 8.0f, 44.0f));
                }

                // Form rows – each 28px tall, starting at y=56, gap=8px
                constexpr float kRowH = 28.0f;
                constexpr float kRowGap = 8.0f;
                constexpr float kFormY0 = 54.0f;
                constexpr float kLabelX1 = 130.0f;
                constexpr float kEntryX0 = 138.0f;
                constexpr float kEntryX1 = W - 12.0f;

                const auto rowY0 = [&](int row) { return kFormY0 + row * (kRowH + kRowGap); };
                const auto rowY1 = [&](int row) { return rowY0(row) + kRowH; };

                // Row 0: Name
                elements.push_back(makeLabel("LM.NameLabel", "Name:", 12.0f, rowY0(0), kLabelX1, rowY1(0)));
                {
                    auto e = makeEntry("LM.NameEntry", state->name, kEntryX0, rowY0(0), kEntryX1, rowY1(0));
                    e.onValueChanged = [state](const std::string& v) { state->name = v; };
                    elements.push_back(e);
                }

                // Row 1: Width
                elements.push_back(makeLabel("LM.WidthLabel", "Width:", 12.0f, rowY0(1), kLabelX1, rowY1(1)));
                {
                    auto e = makeEntry("LM.WidthEntry", state->widthStr, kEntryX0, rowY0(1), kEntryX1, rowY1(1));
                    e.onValueChanged = [state](const std::string& v) { state->widthStr = v; };
                    elements.push_back(e);
                }

                // Row 2: Depth
                elements.push_back(makeLabel("LM.DepthLabel", "Depth:", 12.0f, rowY0(2), kLabelX1, rowY1(2)));
                {
                    auto e = makeEntry("LM.DepthEntry", state->depthStr, kEntryX0, rowY0(2), kEntryX1, rowY1(2));
                    e.onValueChanged = [state](const std::string& v) { state->depthStr = v; };
                    elements.push_back(e);
                }

                // Row 3: Subdivisions X
                elements.push_back(makeLabel("LM.SubXLabel", "Subdiv X:", 12.0f, rowY0(3), kLabelX1, rowY1(3)));
                {
                    auto e = makeEntry("LM.SubXEntry", state->subdivXStr, kEntryX0, rowY0(3), kEntryX1, rowY1(3));
                    e.onValueChanged = [state](const std::string& v) { state->subdivXStr = v; };
                    elements.push_back(e);
                }

                // Row 4: Subdivisions Z
                elements.push_back(makeLabel("LM.SubZLabel", "Subdiv Z:", 12.0f, rowY0(4), kLabelX1, rowY1(4)));
                {
                    auto e = makeEntry("LM.SubZEntry", state->subdivZStr, kEntryX0, rowY0(4), kEntryX1, rowY1(4));
                    e.onValueChanged = [state](const std::string& v) { state->subdivZStr = v; };
                    elements.push_back(e);
                }

                // Divider
                {
                    WidgetElement sep;
                    sep.type  = WidgetElementType::Panel;
                    sep.id    = "LM.Sep";
                    sep.from  = Vec2{ nx(8.0f), ny(rowY1(4) + 12.0f) };
                    sep.to    = Vec2{ nx(W - 8.0f), ny(rowY1(4) + 14.0f) };
                    sep.color = Vec4{ 0.28f, 0.28f, 0.32f, 1.0f };
                    elements.push_back(sep);
                }

                // Button row
                const float btnY0 = rowY1(4) + 20.0f;
                const float btnY1 = btnY0 + 34.0f;

                // Create button
                {
                    WidgetElement btn;
                    btn.type          = WidgetElementType::Button;
                    btn.id            = "LM.CreateBtn";
                    btn.from          = Vec2{ nx(W - 220.0f), ny(btnY0) };
                    btn.to            = Vec2{ nx(W - 114.0f), ny(btnY1) };
                    btn.text          = "Create";
                    btn.fontSize      = 13.0f;
                    btn.color         = Vec4{ 0.12f, 0.32f, 0.12f, 1.0f };
                    btn.hoverColor    = Vec4{ 0.18f, 0.46f, 0.18f, 1.0f };
                    btn.textColor     = Vec4{ 0.95f, 0.95f, 0.95f, 1.0f };
                    btn.textAlignH    = TextAlignH::Center;
                    btn.textAlignV    = TextAlignV::Center;
                    btn.padding       = Vec2{ 8.0f, 4.0f };
                    btn.isHitTestable = true;
                    btn.onClicked     = [state, &glRenderer]()
                    {
                        LandscapeParams p;
                        p.name   = state->name.empty() ? "Landscape" : state->name;
                        try { p.width  = std::stof(state->widthStr); }  catch(...) { p.width  = 100.0f; }
                        try { p.depth  = std::stof(state->depthStr); }  catch(...) { p.depth  = 100.0f; }
                        try { p.subdivisionsX = std::stoi(state->subdivXStr); } catch(...) { p.subdivisionsX = 32; }
                        try { p.subdivisionsZ = std::stoi(state->subdivZStr); } catch(...) { p.subdivisionsZ = 32; }

                        const ECS::Entity entity = LandscapeManager::spawnLandscape(p);
                        if (entity != 0)
                        {
                            glRenderer->getUIManager().refreshWorldOutliner();
                            glRenderer->getUIManager().selectEntity(entity);
                            glRenderer->getUIManager().showToastMessage(
                                "Landscape created: " + p.name, 3.0f);
                        }
                        else
                        {
                            glRenderer->getUIManager().showToastMessage(
                                "Failed to create landscape.", 3.0f);
                        }
                        glRenderer->closePopupWindow("LandscapeManager");
                    };
                    elements.push_back(btn);
                }

                // Cancel button
                {
                    WidgetElement btn;
                    btn.type          = WidgetElementType::Button;
                    btn.id            = "LM.CancelBtn";
                    btn.from          = Vec2{ nx(W - 104.0f), ny(btnY0) };
                    btn.to            = Vec2{ nx(W - 12.0f),  ny(btnY1) };
                    btn.text          = "Cancel";
                    btn.fontSize      = 13.0f;
                    btn.color         = Vec4{ 0.22f, 0.22f, 0.25f, 1.0f };
                    btn.hoverColor    = Vec4{ 0.32f, 0.32f, 0.36f, 1.0f };
                    btn.textColor     = Vec4{ 0.85f, 0.85f, 0.88f, 1.0f };
                    btn.textAlignH    = TextAlignH::Center;
                    btn.textAlignV    = TextAlignV::Center;
                    btn.padding       = Vec2{ 8.0f, 4.0f };
                    btn.isHitTestable = true;
                    btn.onClicked     = [&glRenderer]()
                    {
                        glRenderer->closePopupWindow("LandscapeManager");
                    };
                    elements.push_back(btn);
                }

                // Assemble widget and register it in the popup UIManager
                auto widget = std::make_shared<Widget>();
                widget->setName("LandscapeManagerWidget");
                widget->setFillX(true);
                widget->setFillY(true);
                widget->setElements(std::move(elements));
                popup->uiManager().registerWidget("LandscapeManager.Main", widget);
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
                            glRenderer->setActiveCameraEntity(activeCamEntity);
                            Logger::Instance().log(Logger::Category::Engine, "PIE: using entity camera " + std::to_string(activeCamEntity), Logger::LogLevel::INFO);
                        }
                    }

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
                    if (auto widget = glRenderer->createWidgetFromAsset(asset))
                    {
                        glRenderer->getUIManager().registerWidget("StatusBar", widget);
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

                        glRenderer->getUIManager().registerClickEvent("ContentBrowser.PathBar.Import", [&renderer]()
                            {
                                Logger::Instance().log(Logger::Category::AssetManagement, "Import button clicked.", Logger::LogLevel::INFO);
                                auto* window = renderer ? renderer->window() : nullptr;
                                AssetManager::Instance().OpenImportDialog(window, AssetType::Unknown, AssetManager::Async);
                            });

                        assetManager.setOnImportCompleted([&glRenderer]()
                            {
                                if (glRenderer)
                                {
                                    glRenderer->getUIManager().refreshContentBrowser();
                                }
                            });

                        // --- Drag & Drop: asset dropped on viewport → spawn entity ---
                        glRenderer->getUIManager().setOnDropOnViewport([&glRenderer](const std::string& payload, const Vec2& screenPos)
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
                                const unsigned int targetEntity = glRenderer->pickEntityAtImmediate(
                                    static_cast<int>(screenPos.x), static_cast<int>(screenPos.y));
                                const auto target = static_cast<ECS::Entity>(targetEntity);

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
                                            auto* mat = ecs.getComponent<ECS::MaterialComponent>(target);
                                            if (mat)
                                                mat->materialAssetPath = relPath;
                                            else
                                            {
                                                ECS::MaterialComponent m;
                                                m.materialAssetPath = relPath;
                                                ecs.addComponent<ECS::MaterialComponent>(target, m);
                                            }
                                            break;
                                        }
                                        case AssetType::Script:
                                        {
                                            auto* sc = ecs.getComponent<ECS::ScriptComponent>(target);
                                            if (sc)
                                                sc->scriptPath = relPath;
                                            else
                                            {
                                                ECS::ScriptComponent s;
                                                s.scriptPath = relPath;
                                                ecs.addComponent<ECS::ScriptComponent>(target, s);
                                            }
                                            break;
                                        }
                                        default:
                                            break;
                                        }

                                        diagnostics.setScenePrepared(false);
                                        auto* level = diagnostics.getActiveLevelSoft();
                                        if (level) level->setIsSaved(false);
                                        Logger::Instance().log(Logger::Category::Engine,
                                            "Applied '" + assetName + "' to entity " + std::to_string(targetEntity),
                                            Logger::LogLevel::INFO);
                                        if (glRenderer)
                                        {
                                            glRenderer->getUIManager().refreshWorldOutliner();
                                            glRenderer->getUIManager().selectEntity(targetEntity);
                                            glRenderer->getUIManager().showToastMessage(
                                                "Applied " + assetName + " → Entity " + std::to_string(targetEntity), 2.5f);
                                        }
                                    }
                                    else
                                    {
                                        // No entity under cursor — cannot apply, show hint
                                        if (glRenderer)
                                        {
                                            glRenderer->getUIManager().showToastMessage(
                                                "No entity under cursor to apply " + assetName, 2.5f);
                                        }
                                    }
                                    return;
                                }

                                // --- Model3D: always spawn a new entity ---
                                Vec3 spawnPos{ 0.0f, 0.0f, 0.0f };
                                if (!glRenderer->screenToWorldPos(static_cast<int>(screenPos.x), static_cast<int>(screenPos.y), spawnPos))
                                {
                                    const Vec3 camPos = glRenderer->getCameraPosition();
                                    const Vec2 camRot = glRenderer->getCameraRotationDegrees();
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

                                auto* level = diagnostics.getActiveLevelSoft();
                                if (level)
                                {
                                    level->onEntityAdded(entity);
                                    level->setIsSaved(false);
                                }

                                Logger::Instance().log(Logger::Category::Engine,
                                    "Spawned entity " + std::to_string(entity) + " (" + assetName + ") at ("
                                    + std::to_string(spawnPos.x) + ", " + std::to_string(spawnPos.y) + ", " + std::to_string(spawnPos.z) + ")",
                                    Logger::LogLevel::INFO);

                                diagnostics.setScenePrepared(false);
                                if (glRenderer)
                                {
                                    glRenderer->getUIManager().refreshWorldOutliner();
                                    glRenderer->getUIManager().selectEntity(static_cast<unsigned int>(entity));
                                    glRenderer->getUIManager().showToastMessage("Spawned: " + assetName, 2.5f);
                                }
                            });

                        // --- Drag & Drop: asset dropped on content browser folder → move asset ---
                        glRenderer->getUIManager().setOnDropOnFolder([&glRenderer](const std::string& payload, const std::string& folderPath)
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
                                if (glRenderer)
                                {
                                    glRenderer->getUIManager().refreshContentBrowser();
                                    glRenderer->getUIManager().showToastMessage("Moved: " + fileName, 2.5f);
                                }
                            });

                        // --- Drag & Drop: asset dropped on Outliner entity → apply to entity ---
                        glRenderer->getUIManager().setOnDropOnEntity([&glRenderer](const std::string& payload, unsigned int entityId)
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
                                    auto* mat = ecs.getComponent<ECS::MaterialComponent>(entity);
                                    if (mat)
                                    {
                                        mat->materialAssetPath = relPath;
                                    }
                                    else
                                    {
                                        ECS::MaterialComponent newMat;
                                        newMat.materialAssetPath = relPath;
                                        ecs.addComponent<ECS::MaterialComponent>(entity, newMat);
                                    }
                                    break;
                                }
                                case AssetType::Model3D:
                                {
                                    auto* mesh = ecs.getComponent<ECS::MeshComponent>(entity);
                                    if (mesh)
                                    {
                                        mesh->meshAssetPath = relPath;
                                    }
                                    else
                                    {
                                        ECS::MeshComponent newMesh;
                                        newMesh.meshAssetPath = relPath;
                                        ecs.addComponent<ECS::MeshComponent>(entity, newMesh);
                                    }
                                    break;
                                }
                                case AssetType::Script:
                                {
                                    auto* script = ecs.getComponent<ECS::ScriptComponent>(entity);
                                    if (script)
                                    {
                                        script->scriptPath = relPath;
                                    }
                                    else
                                    {
                                        ECS::ScriptComponent newScript;
                                        newScript.scriptPath = relPath;
                                        ecs.addComponent<ECS::ScriptComponent>(entity, newScript);
                                    }
                                    break;
                                }
                                default:
                                    break;
                                }

                                DiagnosticsManager::Instance().setScenePrepared(false);
                                auto* level = DiagnosticsManager::Instance().getActiveLevelSoft();
                                if (level)
                                {
                                    level->setIsSaved(false);
                                }
                                Logger::Instance().log(Logger::Category::Engine,
                                    "Applied '" + assetName + "' to entity " + std::to_string(entityId),
                                    Logger::LogLevel::INFO);
                                if (glRenderer)
                                {
                                    glRenderer->getUIManager().refreshWorldOutliner();
                                    glRenderer->getUIManager().showToastMessage(
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
        UndoRedoManager::Instance().setOnChanged([&glRenderer]()
            {
                auto& diag = DiagnosticsManager::Instance();
                auto* level = diag.getActiveLevelSoft();
                if (level)
                {
                    level->setIsSaved(false);
                }
                if (glRenderer)
                {
                    glRenderer->getUIManager().refreshStatusBar();
                }
            });

        glRenderer->getUIManager().registerClickEvent("StatusBar.Undo", [&glRenderer]()
            {
                auto& undo = UndoRedoManager::Instance();
                if (undo.canUndo())
                {
                    undo.undo();
                    glRenderer->getUIManager().markAllWidgetsDirty();
                }
            });

        glRenderer->getUIManager().registerClickEvent("StatusBar.Redo", [&glRenderer]()
            {
                auto& undo = UndoRedoManager::Instance();
                if (undo.canRedo())
                {
                    undo.redo();
                    glRenderer->getUIManager().markAllWidgetsDirty();
                }
            });

        glRenderer->getUIManager().registerClickEvent("StatusBar.Save", [&glRenderer, &renderer]()
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
                    glRenderer->getUIManager().showToastMessage("Nothing to save.", 2.0f);
                    return;
                }
                auto& uiMgr = glRenderer->getUIManager();
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
        if (!glRenderer) return false;
        auto& uiManager = glRenderer->getUIManager();
        const unsigned int selected = uiManager.getSelectedEntity();
        if (selected == 0) return false;

        auto& ecs = ECS::ECSManager::Instance();
        auto* level = diagnostics.getActiveLevelSoft();

        // Get entity name for feedback
        std::string entityName = "Entity " + std::to_string(selected);
        if (const auto* nameComp = ecs.getComponent<ECS::NameComponent>(static_cast<ECS::Entity>(selected)))
        {
            if (!nameComp->displayName.empty())
                entityName = nameComp->displayName;
        }

        // Remove from level tracking first
        if (level)
        {
            level->onEntityRemoved(static_cast<ECS::Entity>(selected));
            level->setIsSaved(false);
        }

        // Remove from ECS
        ecs.removeEntity(static_cast<ECS::Entity>(selected));

        // Clear selection and deselect in renderer
        uiManager.selectEntity(0);
        glRenderer->setSelectedEntity(0);

        diagnostics.setScenePrepared(false);
        uiManager.refreshWorldOutliner();
        uiManager.showToastMessage("Deleted: " + entityName, 2.5f);

        Logger::Instance().log(Logger::Category::Engine,
            "Deleted entity " + std::to_string(selected) + " (" + entityName + ")",
            Logger::LogLevel::INFO);
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
			// Route events belonging to popup windows first.
			if (glRenderer && glRenderer->routeEventToPopup(event))
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
                if (glRenderer)
                {
                    const Vec2 mousePos{ static_cast<float>(event.button.x), static_cast<float>(event.button.y) };
                    auto& uiManager = glRenderer->getUIManager();
                    if (uiManager.isDragging())
                    {
                        uiManager.handleMouseUp(mousePos, event.button.button);
                    }
                    else
                    {
                        // Always forward mouse-up so deferred clicks on draggable elements fire
                        uiManager.handleMouseUp(mousePos, event.button.button);
                        if (glRenderer->isGizmoDragging())
                        {
                            glRenderer->endGizmoDrag();
                        }
                    }
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
                // Ctrl+Z = Undo, Ctrl+Y = Redo
                if (glRenderer && !diagnostics.isPIEActive() && (event.key.mod & SDL_KMOD_CTRL))
                {
                    if (event.key.key == SDLK_Z)
                    {
                        auto& undo = UndoRedoManager::Instance();
                        if (undo.canUndo())
                        {
                            undo.undo();
                            glRenderer->getUIManager().markAllWidgetsDirty();
                        }
                        continue;
                    }
                    if (event.key.key == SDLK_Y)
                    {
                        auto& undo = UndoRedoManager::Instance();
                        if (undo.canRedo())
                        {
                            undo.redo();
                            glRenderer->getUIManager().markAllWidgetsDirty();
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
                            auto& uiMgr = glRenderer->getUIManager();
                            uiMgr.showSaveProgressModal(total);
                            am.saveAllAssetsAsync(
                                [&uiMgr = glRenderer->getUIManager()](size_t saved, size_t t) { uiMgr.updateSaveProgress(saved, t); },
                                [&uiMgr = glRenderer->getUIManager()](bool ok) {
                                    UndoRedoManager::Instance().clear();
                                    uiMgr.closeSaveProgressModal(ok);
                                });
                        }
                        else
                        {
                            glRenderer->getUIManager().showToastMessage("Nothing to save.", 2.0f);
                        }
                        continue;
                    }
                }
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
