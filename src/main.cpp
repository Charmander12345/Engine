#include <iostream>
#include <filesystem>
#include <fstream>
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
#include "Core/EngineLevel.h"
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

    // Apply persisted engine settings
    {
        auto& diag = DiagnosticsManager::Instance();
        if (auto v = diag.getState("ShadowsEnabled"))
            glRenderer->setShadowsEnabled(*v != "0");
        if (auto v = diag.getState("OcclusionCullingEnabled"))
            glRenderer->setOcclusionCullingEnabled(*v != "0");
        if (auto v = diag.getState("UIDebugEnabled"))
        {
            if ((*v == "1") != glRenderer->isUIDebugEnabled())
                glRenderer->toggleUIDebug();
        }
        if (auto v = diag.getState("BoundsDebugEnabled"))
        {
            if ((*v == "1") != glRenderer->isBoundsDebugEnabled())
                glRenderer->toggleBoundsDebug();
        }
        if (auto v = diag.getState("VSyncEnabled"))
            glRenderer->setVSyncEnabled(*v == "1");
        if (auto v = diag.getState("WireframeEnabled"))
            glRenderer->setWireframeEnabled(*v == "1");
    }

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
                glRenderer->getUIManager().openLandscapeManagerPopup();
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

        glRenderer->getUIManager().registerClickEvent("ViewportOverlay.Settings", [&glRenderer]()
            {
                Logger::Instance().log(Logger::Category::Input, "Toolbar: Settings clicked.", Logger::LogLevel::INFO);

                auto& uiMgr = glRenderer->getUIManager();

                // Toggle dropdown menu
                if (uiMgr.isDropdownMenuOpen())
                {
                    uiMgr.closeDropdownMenu();
                    return;
                }

                // Anchor below the Settings button, right-aligned
                auto* btn = uiMgr.findElementById("ViewportOverlay.Settings");
                Vec2 anchor{ 0.0f, 0.0f };
                if (btn && btn->hasBounds)
                {
                    constexpr float kDropdownW = 180.0f;
                    anchor = Vec2{ btn->boundsMaxPixels.x - kDropdownW, btn->boundsMaxPixels.y + 2.0f };
                }

                std::vector<UIManager::DropdownMenuItem> items;
                items.push_back({ "Engine Settings", [&glRenderer]()
                    {
                        glRenderer->getUIManager().openEngineSettingsPopup();
                    }
                });

                uiMgr.showDropdownMenu(anchor, items);
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

                        // Skybox asset path entry
                        if (auto* skyboxEntry = findElementById(widget->getElementsMutable(), "WorldSettings.SkyboxPath"))
                        {
                            skyboxEntry->value = glRenderer->getSkyboxPath();
                            skyboxEntry->onValueChanged = [glRenderer](const std::string& value)
                                {
                                    glRenderer->setSkyboxPath(value);
                                    auto& diag = DiagnosticsManager::Instance();
                                    if (auto* level = diag.getActiveLevelSoft())
                                    {
                                        level->setSkyboxPath(value);
                                    }
                                    glRenderer->getUIManager().refreshStatusBar();
                                };

                            // Bind Clear button if it exists
                            if (auto* clearBtn = findElementById(widget->getElementsMutable(), "WorldSettings.SkyboxClear"))
                            {
                                clearBtn->onClicked = [glRenderer]()
                                    {
                                        glRenderer->setSkyboxPath("");
                                        auto& diag = DiagnosticsManager::Instance();
                                        if (auto* level = diag.getActiveLevelSoft())
                                        {
                                            level->setSkyboxPath("");
                                        }
                                        glRenderer->getUIManager().refreshStatusBar();
                                        glRenderer->getUIManager().showToastMessage("Skybox cleared", 2.5f);
                                    };
                            }
                        }
                        else
                        {
                            // Dynamically add Skybox section if the widget doesn't have it
                            auto& elements = widget->getElementsMutable();
                            WidgetElement* rootStack = nullptr;
                            for (auto& el : elements)
                            {
                                if (el.type == WidgetElementType::StackPanel)
                                {
                                    rootStack = &el;
                                    break;
                                }
                            }
                            if (rootStack)
                            {
                                WidgetElement skyLabel{};
                                skyLabel.id = "WorldSettings.SkyboxLabel";
                                skyLabel.type = WidgetElementType::Text;
                                skyLabel.text = "Skybox Asset";
                                skyLabel.font = "default.ttf";
                                skyLabel.fontSize = 13.0f;
                                skyLabel.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                                skyLabel.fillX = true;
                                skyLabel.minSize = Vec2{ 0.0f, 22.0f };
                                skyLabel.padding = Vec2{ 4.0f, 2.0f };
                                skyLabel.runtimeOnly = true;
                                rootStack->children.push_back(std::move(skyLabel));

                                WidgetElement skyEntry{};
                                skyEntry.id = "WorldSettings.SkyboxPath";
                                skyEntry.type = WidgetElementType::EntryBar;
                                skyEntry.value = glRenderer->getSkyboxPath();
                                skyEntry.font = "default.ttf";
                                skyEntry.fontSize = 13.0f;
                                skyEntry.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                                skyEntry.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                                skyEntry.fillX = true;
                                skyEntry.minSize = Vec2{ 0.0f, 24.0f };
                                skyEntry.padding = Vec2{ 6.0f, 4.0f };
                                skyEntry.isHitTestable = true;
                                skyEntry.runtimeOnly = true;
                                skyEntry.onValueChanged = [glRenderer](const std::string& value)
                                    {
                                        glRenderer->setSkyboxPath(value);
                                        auto& diag = DiagnosticsManager::Instance();
                                        if (auto* level = diag.getActiveLevelSoft())
                                        {
                                            level->setSkyboxPath(value);
                                        }
                                        glRenderer->getUIManager().refreshStatusBar();
                                    };
                                rootStack->children.push_back(std::move(skyEntry));

                                WidgetElement clearBtn{};
                                clearBtn.id = "WorldSettings.SkyboxClear";
                                clearBtn.type = WidgetElementType::Button;
                                clearBtn.text = "Clear Skybox";
                                clearBtn.font = "default.ttf";
                                clearBtn.fontSize = 12.0f;
                                clearBtn.textColor = Vec4{ 0.9f, 0.7f, 0.7f, 1.0f };
                                clearBtn.textAlignH = TextAlignH::Center;
                                clearBtn.textAlignV = TextAlignV::Center;
                                clearBtn.color = Vec4{ 0.3f, 0.15f, 0.15f, 0.8f };
                                clearBtn.hoverColor = Vec4{ 0.45f, 0.2f, 0.2f, 0.95f };
                                clearBtn.shaderVertex = "button_vertex.glsl";
                                clearBtn.shaderFragment = "button_fragment.glsl";
                                clearBtn.fillX = true;
                                clearBtn.minSize = Vec2{ 0.0f, 24.0f };
                                clearBtn.padding = Vec2{ 4.0f, 2.0f };
                                clearBtn.isHitTestable = true;
                                clearBtn.runtimeOnly = true;
                                clearBtn.onClicked = [glRenderer]()
                                    {
                                        glRenderer->setSkyboxPath("");
                                        auto& diag = DiagnosticsManager::Instance();
                                        if (auto* level = diag.getActiveLevelSoft())
                                        {
                                            level->setSkyboxPath("");
                                        }
                                        glRenderer->getUIManager().refreshStatusBar();
                                        glRenderer->getUIManager().showToastMessage("Skybox cleared", 2.5f);
                                    };
                                rootStack->children.push_back(std::move(clearBtn));
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

                                // --- Skybox: set as level skybox, no entity needed ---
                                if (assetType == AssetType::Skybox)
                                {
                                    glRenderer->setSkyboxPath(relPath);
                                    auto* level = diagnostics.getActiveLevelSoft();
                                    if (level)
                                    {
                                        level->setSkyboxPath(relPath);
                                    }
                                    Logger::Instance().log(Logger::Category::Engine,
                                        "Set skybox from drag: " + relPath, Logger::LogLevel::INFO);
                                    if (glRenderer)
                                    {
                                        glRenderer->getUIManager().refreshStatusBar();
                                        glRenderer->getUIManager().showToastMessage("Skybox: " + assetName, 2.5f);
                                    }
                                    return;
                                }

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
                                }

                                Logger::Instance().log(Logger::Category::Engine,
                                    "Spawned entity " + std::to_string(entity) + " (" + assetName + ") at ("
                                    + std::to_string(spawnPos.x) + ", " + std::to_string(spawnPos.y) + ", " + std::to_string(spawnPos.z) + ")",
                                    Logger::LogLevel::INFO);

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

        // Check for selected asset in Content Browser grid first
        const std::string selectedAsset = uiManager.getSelectedGridAsset();
        if (!selectedAsset.empty())
        {
            uiManager.showConfirmDialog(
                "Are you sure you want to delete this asset?\nThis cannot be undone.",
                [&glRenderer, selectedAsset]()
                {
                    auto& assetMgr = AssetManager::Instance();
                    auto& uiMgr = glRenderer->getUIManager();
                    if (assetMgr.deleteAsset(selectedAsset, true))
                    {
                        const std::string name = std::filesystem::path(selectedAsset).stem().string();
                        uiMgr.clearSelectedGridAsset();
                        uiMgr.refreshContentBrowser();
                        uiMgr.showToastMessage("Deleted: " + name, 3.0f);
                    }
                    else
                    {
                        uiMgr.showToastMessage("Failed to delete asset.", 3.0f);
                    }
                });
            return true;
        }

        // Otherwise try entity deletion
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
        }

        // Remove from ECS
        ecs.removeEntity(static_cast<ECS::Entity>(selected));

        // Clear selection and deselect in renderer
        uiManager.selectEntity(0);
        glRenderer->setSelectedEntity(0);

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

                    // Right-click context menu on Content Browser grid
                    if (isOverUI && uiManager.isOverContentBrowserGrid(mousePos))
                    {
                        if (uiManager.isDropdownMenuOpen())
                        {
                            uiManager.closeDropdownMenu();
                        }

                        std::vector<UIManager::DropdownMenuItem> items;

                        items.push_back({ "New Script", [&glRenderer]()
                            {
                                auto& uiMgr = glRenderer->getUIManager();
                                const auto& folder = uiMgr.getSelectedBrowserFolder();
                                if (folder == "__Shaders__") return;

                                auto& diagnostics = DiagnosticsManager::Instance();
                                auto& assetMgr = AssetManager::Instance();
                                const std::filesystem::path contentDir =
                                    std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
                                const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
                                std::error_code ec;
                                std::filesystem::create_directories(targetDir, ec);

                                // Find a unique name
                                std::string baseName = "NewScript";
                                std::string fileName = baseName + ".py";
                                int counter = 1;
                                while (std::filesystem::exists(targetDir / fileName))
                                {
                                    fileName = baseName + std::to_string(counter++) + ".py";
                                }

                                const std::filesystem::path filePath = targetDir / fileName;
                                std::ofstream out(filePath, std::ios::out | std::ios::trunc);
                                if (out.is_open())
                                {
                                    out << "import engine\n\n";
                                    out << "def onloaded(entity):\n";
                                    out << "    pass\n\n";
                                    out << "def tick(entity, dt):\n";
                                    out << "    pass\n";
                                    out.close();

                                    // Register in asset registry
                                    const std::string relPath = std::filesystem::relative(filePath, contentDir).generic_string();
                                    AssetRegistryEntry entry;
                                    entry.name = std::filesystem::path(fileName).stem().string();
                                    entry.path = relPath;
                                    entry.type = AssetType::Script;
                                    assetMgr.registerAssetInRegistry(entry);
                                    uiMgr.refreshContentBrowser();
                                    uiMgr.showToastMessage("Created: " + fileName, 3.0f);
                                }
                            }});

                        items.push_back({ "New Level", [&glRenderer]()
                            {
                                auto& uiMgr = glRenderer->getUIManager();
                                const auto& folder = uiMgr.getSelectedBrowserFolder();
                                if (folder == "__Shaders__") return;

                                auto& diagnostics = DiagnosticsManager::Instance();
                                auto& assetMgr = AssetManager::Instance();
                                const std::filesystem::path contentDir =
                                    std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
                                const std::filesystem::path targetDir = folder.empty() ? contentDir : contentDir / folder;
                                std::error_code ec;
                                std::filesystem::create_directories(targetDir, ec);

                                std::string baseName = "NewLevel";
                                std::string fileName = baseName + ".map";
                                int counter = 1;
                                while (std::filesystem::exists(targetDir / fileName))
                                {
                                    fileName = baseName + std::to_string(counter++) + ".map";
                                }

                                auto level = std::make_unique<EngineLevel>();
                                const std::string displayName = std::filesystem::path(fileName).stem().string();
                                level->setName(displayName);
                                const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();
                                level->setPath(relPath);
                                level->setAssetType(AssetType::Level);
                                level->setLevelData(json::object());

                                auto saveResult = assetMgr.saveNewLevelAsset(level.get());
                                if (saveResult)
                                {
                                    AssetRegistryEntry entry;
                                    entry.name = displayName;
                                    entry.path = relPath;
                                    entry.type = AssetType::Level;
                                    assetMgr.registerAssetInRegistry(entry);
                                    uiMgr.refreshContentBrowser();
                                    uiMgr.showToastMessage("Created: " + fileName, 3.0f);
                                }
                            }});

                        items.push_back({ "New Material", [&glRenderer]()
                            {
                                auto& uiMgr = glRenderer->getUIManager();
                                const auto& folder = uiMgr.getSelectedBrowserFolder();
                                if (folder == "__Shaders__") return;

                                constexpr int kPopupW = 460;
                                constexpr int kPopupH = 400;
                                PopupWindow* popup = glRenderer->openPopupWindow(
                                    "NewMaterial", "New Material", kPopupW, kPopupH);
                                if (!popup) return;
                                if (!popup->uiManager().getRegisteredWidgets().empty()) return;

                                const float W = static_cast<float>(kPopupW);
                                const float H = static_cast<float>(kPopupH);
                                auto nx = [&](float px) { return px / W; };
                                auto ny = [&](float py) { return py / H; };

                                struct MaterialState
                                {
                                    std::string name = "NewMaterial";
                                    std::string vertexShader = "vertex.glsl";
                                    std::string fragmentShader = "fragment.glsl";
                                    std::string diffuseTexture;
                                    std::string specularTexture;
                                    float shininess = 32.0f;
                                    std::string folder;
                                };
                                auto state = std::make_shared<MaterialState>();
                                state->folder = folder;

                                std::vector<WidgetElement> elements;

                                // Background
                                {
                                    WidgetElement bg;
                                    bg.type = WidgetElementType::Panel;
                                    bg.id = "NM.Bg";
                                    bg.from = Vec2{ 0.0f, 0.0f };
                                    bg.to = Vec2{ 1.0f, 1.0f };
                                    bg.color = Vec4{ 0.13f, 0.13f, 0.16f, 1.0f };
                                    elements.push_back(bg);
                                }

                                // Title
                                {
                                    WidgetElement title;
                                    title.type = WidgetElementType::Text;
                                    title.id = "NM.Title";
                                    title.from = Vec2{ nx(8.0f), 0.0f };
                                    title.to = Vec2{ 1.0f, ny(36.0f) };
                                    title.text = "New Material";
                                    title.fontSize = 15.0f;
                                    title.textColor = Vec4{ 0.92f, 0.92f, 0.95f, 1.0f };
                                    title.textAlignV = TextAlignV::Center;
                                    title.padding = Vec2{ 6.0f, 0.0f };
                                    elements.push_back(title);
                                }

                                // Form layout as StackPanel
                                WidgetElement formStack;
                                formStack.type = WidgetElementType::StackPanel;
                                formStack.id = "NM.Form";
                                formStack.from = Vec2{ nx(16.0f), ny(44.0f) };
                                formStack.to = Vec2{ nx(W - 16.0f), ny(H - 50.0f) };
                                formStack.padding = Vec2{ 4.0f, 4.0f };
                                formStack.scrollable = true;

                                auto makeLabel = [](const std::string& id, const std::string& text) {
                                    WidgetElement lbl;
                                    lbl.type = WidgetElementType::Text;
                                    lbl.id = id;
                                    lbl.text = text;
                                    lbl.fontSize = 13.0f;
                                    lbl.textColor = Vec4{ 0.7f, 0.75f, 0.85f, 1.0f };
                                    lbl.fillX = true;
                                    lbl.minSize = Vec2{ 0.0f, 20.0f };
                                    lbl.runtimeOnly = true;
                                    return lbl;
                                };

                                auto makeEntry = [](const std::string& id, const std::string& value) {
                                    WidgetElement entry;
                                    entry.type = WidgetElementType::EntryBar;
                                    entry.id = id;
                                    entry.value = value;
                                    entry.fontSize = 13.0f;
                                    entry.textColor = Vec4{ 0.9f, 0.9f, 0.95f, 1.0f };
                                    entry.color = Vec4{ 0.12f, 0.12f, 0.16f, 0.9f };
                                    entry.fillX = true;
                                    entry.minSize = Vec2{ 0.0f, 24.0f };
                                    entry.padding = Vec2{ 6.0f, 4.0f };
                                    entry.isHitTestable = true;
                                    entry.runtimeOnly = true;
                                    return entry;
                                };

                                formStack.children.push_back(makeLabel("NM.NameLbl", "Name"));
                                {
                                    auto entry = makeEntry("NM.Name", state->name);
                                    entry.onValueChanged = [state](const std::string& v) { state->name = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.VsLbl", "Vertex Shader"));
                                {
                                    auto entry = makeEntry("NM.VertexShader", state->vertexShader);
                                    entry.onValueChanged = [state](const std::string& v) { state->vertexShader = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.FsLbl", "Fragment Shader"));
                                {
                                    auto entry = makeEntry("NM.FragmentShader", state->fragmentShader);
                                    entry.onValueChanged = [state](const std::string& v) { state->fragmentShader = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.DiffLbl", "Diffuse Texture (asset path)"));
                                {
                                    auto entry = makeEntry("NM.DiffuseTex", "");
                                    entry.onValueChanged = [state](const std::string& v) { state->diffuseTexture = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.SpecLbl", "Specular Texture (asset path)"));
                                {
                                    auto entry = makeEntry("NM.SpecularTex", "");
                                    entry.onValueChanged = [state](const std::string& v) { state->specularTexture = v; };
                                    formStack.children.push_back(std::move(entry));
                                }

                                formStack.children.push_back(makeLabel("NM.ShinLbl", "Shininess"));
                                {
                                    auto entry = makeEntry("NM.Shininess", "32");
                                    entry.onValueChanged = [state](const std::string& v) {
                                        try { state->shininess = std::stof(v); } catch (...) {}
                                    };
                                    formStack.children.push_back(std::move(entry));
                                }

                                elements.push_back(std::move(formStack));

                                // Create button
                                {
                                    WidgetElement createBtn;
                                    createBtn.type = WidgetElementType::Button;
                                    createBtn.id = "NM.Create";
                                    createBtn.from = Vec2{ nx(W - 180.0f), ny(H - 44.0f) };
                                    createBtn.to = Vec2{ nx(W - 100.0f), ny(H - 12.0f) };
                                    createBtn.text = "Create";
                                    createBtn.fontSize = 14.0f;
                                    createBtn.textColor = Vec4{ 1.0f, 1.0f, 1.0f, 1.0f };
                                    createBtn.textAlignH = TextAlignH::Center;
                                    createBtn.textAlignV = TextAlignV::Center;
                                    createBtn.color = Vec4{ 0.15f, 0.45f, 0.25f, 0.95f };
                                    createBtn.hoverColor = Vec4{ 0.2f, 0.6f, 0.35f, 1.0f };
                                    createBtn.shaderVertex = "button_vertex.glsl";
                                    createBtn.shaderFragment = "button_fragment.glsl";
                                    createBtn.isHitTestable = true;
                                    createBtn.onClicked = [state, &glRenderer, popup]()
                                    {
                                        auto& diagnostics = DiagnosticsManager::Instance();
                                        auto& assetMgr = AssetManager::Instance();
                                        const std::filesystem::path contentDir =
                                            std::filesystem::path(diagnostics.getProjectInfo().projectPath) / "Content";
                                        const std::filesystem::path targetDir = state->folder.empty()
                                            ? contentDir
                                            : contentDir / state->folder;
                                        std::error_code ec;
                                        std::filesystem::create_directories(targetDir, ec);

                                        const std::string matName = state->name.empty() ? "NewMaterial" : state->name;
                                        std::string fileName = matName + ".asset";
                                        int counter = 1;
                                        while (std::filesystem::exists(targetDir / fileName))
                                        {
                                            fileName = matName + std::to_string(counter++) + ".asset";
                                        }

                                        auto mat = std::make_shared<AssetData>();
                                        mat->setName(std::filesystem::path(fileName).stem().string());
                                        mat->setAssetType(AssetType::Material);
                                        mat->setType(AssetType::Material);
                                        const std::string relPath = std::filesystem::relative(targetDir / fileName, contentDir).generic_string();
                                        mat->setPath(relPath);

                                        json matData = json::object();
                                        if (!state->vertexShader.empty())
                                            matData["m_shaderVertex"] = state->vertexShader;
                                        if (!state->fragmentShader.empty())
                                            matData["m_shaderFragment"] = state->fragmentShader;
                                        std::vector<std::string> texPaths;
                                        if (!state->diffuseTexture.empty())
                                            texPaths.push_back(state->diffuseTexture);
                                        if (!state->specularTexture.empty())
                                            texPaths.push_back(state->specularTexture);
                                        if (!texPaths.empty())
                                            matData["m_textureAssetPaths"] = texPaths;
                                        matData["m_shininess"] = state->shininess;
                                        mat->setData(std::move(matData));

                                        Asset asset;
                                        asset.type = AssetType::Material;
                                        auto id = assetMgr.registerLoadedAsset(mat);
                                        if (id != 0)
                                        {
                                            mat->setId(id);
                                            asset.ID = id;
                                            assetMgr.saveAsset(asset, AssetManager::Sync);
                                            AssetRegistryEntry entry;
                                            entry.name = mat->getName();
                                            entry.path = relPath;
                                            entry.type = AssetType::Material;
                                            assetMgr.registerAssetInRegistry(entry);
                                        }

                                        auto& uiMgr = glRenderer->getUIManager();
                                        uiMgr.refreshContentBrowser();
                                        uiMgr.showToastMessage("Created: " + fileName, 3.0f);

                                        popup->close();
                                    };
                                    elements.push_back(std::move(createBtn));
                                }

                                // Cancel button
                                {
                                    WidgetElement cancelBtn;
                                    cancelBtn.type = WidgetElementType::Button;
                                    cancelBtn.id = "NM.Cancel";
                                    cancelBtn.from = Vec2{ nx(W - 90.0f), ny(H - 44.0f) };
                                    cancelBtn.to = Vec2{ nx(W - 16.0f), ny(H - 12.0f) };
                                    cancelBtn.text = "Cancel";
                                    cancelBtn.fontSize = 14.0f;
                                    cancelBtn.textColor = Vec4{ 0.9f, 0.9f, 0.9f, 1.0f };
                                    cancelBtn.textAlignH = TextAlignH::Center;
                                    cancelBtn.textAlignV = TextAlignV::Center;
                                    cancelBtn.color = Vec4{ 0.25f, 0.25f, 0.3f, 0.95f };
                                    cancelBtn.hoverColor = Vec4{ 0.35f, 0.35f, 0.42f, 1.0f };
                                    cancelBtn.shaderVertex = "button_vertex.glsl";
                                    cancelBtn.shaderFragment = "button_fragment.glsl";
                                    cancelBtn.isHitTestable = true;
                                    cancelBtn.onClicked = [popup]() { popup->close(); };
                                    elements.push_back(std::move(cancelBtn));
                                }

                                auto widget = std::make_shared<Widget>();
                                widget->setName("NewMaterial");
                                widget->setSizePixels(Vec2{ W, H });
                                widget->setElements(std::move(elements));
                                popup->uiManager().registerWidget("NewMaterial", widget);
                            }});

                        uiManager.showDropdownMenu(mousePos, items);
                        continue;
                    }
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
