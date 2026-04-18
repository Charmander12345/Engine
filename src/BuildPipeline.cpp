#include "BuildPipeline.h"
#if ENGINE_EDITOR

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <cstdio>
#include <cctype>
#include <regex>
#include <sstream>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Editor/Tabs/BuildSystemUI.h"
#include "Logger/Logger.h"
#include "Diagnostics/DiagnosticsManager.h"
#include "AssetManager/AssetCooker.h"
#include "AssetManager/HPKArchive.h"

void BuildPipeline::execute(const UIManager::BuildGameConfig& config,
                            UIManager& uiMgr,
                            Renderer* renderer)
{
    Logger::Instance().log(Logger::Category::Engine,
        "Build Game requested. Output: " + config.outputDir, Logger::LogLevel::INFO);

    if (uiMgr.isBuildRunning())
    {
        uiMgr.showToastMessage("A build is already in progress.", UIManager::kToastMedium,
            UIManager::NotificationLevel::Warning);
        return;
    }

    // Verify CMake and toolchain are available
    if (!uiMgr.isCMakeAvailable())
    {
        uiMgr.showToastMessage("CMake is required to build the game.\nPlease install CMake and restart the editor.", UIManager::kToastLong,
            UIManager::NotificationLevel::Error);
        return;
    }
    if (!uiMgr.isBuildToolchainAvailable())
    {
        uiMgr.showToastMessage("C++ toolchain is required to build the game.\nPlease install Visual Studio with C++ workload.", UIManager::kToastLong,
            UIManager::NotificationLevel::Error);
        return;
    }

    const std::string cmakePath = uiMgr.getCMakePath();
    const std::string engineSourceDir = ENGINE_SOURCE_DIR;
    const std::string toolchainName = uiMgr.getBuildToolchain().name;
    const std::string toolchainVersion = uiMgr.getBuildToolchain().version;

    const char* bp = SDL_GetBasePath();
    const std::string editorBaseDir = bp ? std::string(bp) : std::string();

    uiMgr.showBuildProgress();

    // Capture values needed by the thread (no references to stack locals)
    auto& diagnostics = DiagnosticsManager::Instance();
    const std::string projectPath = diagnostics.getProjectInfo().projectPath;

    BuildSystemUI& buildUI = uiMgr.getBuildSystemUI();
    BuildSystemUI* buildPtr = &buildUI;

    // Sync current renderer state to DiagnosticsManager so that
    // game.ini (written by the build thread) captures all settings.
    // These are only stored in DiagnosticsManager when the user
    // toggles them in the UI, so they may be missing otherwise.
    if (renderer)
    {
        auto& diag = DiagnosticsManager::Instance();
        diag.setState("PostProcessingEnabled", renderer->isPostProcessingEnabled() ? "1" : "0");
        diag.setState("GammaCorrectionEnabled", renderer->isGammaCorrectionEnabled() ? "1" : "0");
        diag.setState("ToneMappingEnabled", renderer->isToneMappingEnabled() ? "1" : "0");
        diag.setState("ShadowsEnabled", renderer->isShadowsEnabled() ? "1" : "0");
        diag.setState("CsmEnabled", renderer->isCsmEnabled() ? "1" : "0");
        diag.setState("FogEnabled", renderer->isFogEnabled() ? "1" : "0");
        diag.setState("BloomEnabled", renderer->isBloomEnabled() ? "1" : "0");
        diag.setState("SsaoEnabled", renderer->isSsaoEnabled() ? "1" : "0");
        diag.setState("OcclusionCullingEnabled", renderer->isOcclusionCullingEnabled() ? "1" : "0");
        diag.setState("AntiAliasingMode", std::to_string(static_cast<int>(renderer->getAntiAliasingMode())));
        diag.setState("WireframeEnabled", renderer->isWireframeEnabled() ? "1" : "0");
        diag.setState("VSyncEnabled", renderer->isVSyncEnabled() ? "1" : "0");
        diag.setState("HeightFieldDebugEnabled", renderer->isHeightFieldDebugEnabled() ? "1" : "0");
        diag.setState("TextureCompressionEnabled", renderer->isTextureCompressionEnabled() ? "1" : "0");
        diag.setState("TextureStreamingEnabled", renderer->isTextureStreamingEnabled() ? "1" : "0");
        diag.setState("DisplacementMappingEnabled", renderer->isDisplacementMappingEnabled() ? "1" : "0");
        diag.setState("DisplacementScale", std::to_string(renderer->getDisplacementScale()));
        diag.setState("TessellationLevel", std::to_string(renderer->getTessellationLevel()));

        // Flush to disk so the build thread copies an up-to-date config.ini
        diag.saveConfig();
    }

    buildUI.m_buildRunning.store(true);

    if (buildUI.m_buildThread.joinable())
        buildUI.m_buildThread.join();

    buildUI.m_buildThread = std::thread([buildPtr, config, cmakePath, engineSourceDir, toolchainName, toolchainVersion, editorBaseDir, projectPath]()
    {
        constexpr int kTotalSteps = 10;
        int step = 0;
        bool ok = true;
        std::string errorMsg;

        // Log build environment info
        buildPtr->appendBuildOutput("Profile: " + config.profile.name + " (" + config.profile.cmakeBuildType + ")");
        buildPtr->appendBuildOutput("CMake: " + cmakePath);
        buildPtr->appendBuildOutput("Toolchain: " + toolchainName + " " + toolchainVersion);
        buildPtr->appendBuildOutput("Engine Source: " + engineSourceDir);
        buildPtr->appendBuildOutput("Binary Cache: " + config.binaryDir);
        buildPtr->appendBuildOutput("");

            // Thread-safe helper to push step progress
            auto advanceStep = [&](const std::string& status)
            {
                ++step;
                buildPtr->appendBuildOutput("[Step " + std::to_string(step) + "/" + std::to_string(kTotalSteps) + "] " + status);
                {
                    std::lock_guard<std::mutex> lock(buildPtr->m_buildMutex);
                    buildPtr->m_buildPendingStatus = status;
                    buildPtr->m_buildPendingStep = step;
                    buildPtr->m_buildPendingTotalSteps = kTotalSteps;
                    buildPtr->m_buildPendingStepDirty = true;
                }
            };

            // Check if build was cancelled – sets ok=false and returns true
            auto checkCancelled = [&]() -> bool
            {
                if (buildPtr->m_buildCancelRequested.load())
                {
                    ok = false;
                    errorMsg = "Build cancelled by user.";
                    buildPtr->appendBuildOutput("[INFO] Build cancelled.");
                    return true;
                }
                return false;
            };

        try
        {
          // Step 1: Create output directory (clean any previous build)
          advanceStep("Preparing output directory...");
          {
              const std::filesystem::path outDir = config.outputDir;
              // Remove all existing files and subdirectories so no
              // stale artefacts from a previous build remain.
              if (std::filesystem::exists(outDir))
              {
                  std::error_code ec;
                  std::filesystem::remove_all(outDir, ec);
                  if (ec)
                  {
                      ok = false;
                      errorMsg = "Failed to clean output directory: " + ec.message();
                  }
                  else
                  {
                      buildPtr->appendBuildOutput("  Cleaned output directory.");
                  }
              }

              if (ok)
              {
                  std::error_code ec;
                  std::filesystem::create_directories(outDir, ec);
                  if (ec)
                  {
                      ok = false;
                      errorMsg = "Failed to create output directory: " + ec.message();
                  }
              }
          }

          // Helper: run a command, capture output line-by-line (no visible console)
#if defined(_WIN32)
          auto runCmdWithOutput = [&](const std::string& cmd) -> int
          {
              buildPtr->appendBuildOutput("> " + cmd);

              SECURITY_ATTRIBUTES sa{};
              sa.nLength = sizeof(sa);
              sa.bInheritHandle = TRUE;

              HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
              if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
                  return -1;
              SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

              STARTUPINFOA si{};
              si.cb = sizeof(si);
              si.dwFlags = STARTF_USESTDHANDLES;
              si.hStdOutput = hWritePipe;
              si.hStdError = hWritePipe;

              PROCESS_INFORMATION pi{};
              std::string cmdLine = cmd;
              BOOL created = CreateProcessA(
                  nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
              CloseHandle(hWritePipe);

              if (!created)
              {
                  CloseHandle(hReadPipe);
                  buildPtr->appendBuildOutput("  [ERROR] Failed to launch process.");
                  return -1;
              }

              // Read output line by line
              char buf[4096];
              DWORD bytesRead = 0;
              std::string lineBuffer;
              while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
              {
                  if (buildPtr->m_buildCancelRequested.load())
                  {
                      TerminateProcess(pi.hProcess, 1);
                      break;
                  }
                  buf[bytesRead] = '\0';
                  lineBuffer += buf;
                  size_t pos;
                  while ((pos = lineBuffer.find('\n')) != std::string::npos)
                  {
                      std::string line = lineBuffer.substr(0, pos);
                      if (!line.empty() && line.back() == '\r') line.pop_back();
                      buildPtr->appendBuildOutput("  " + line);
                      lineBuffer.erase(0, pos + 1);
                  }
              }
              if (!lineBuffer.empty())
                  buildPtr->appendBuildOutput("  " + lineBuffer);

              WaitForSingleObject(pi.hProcess, INFINITE);
              DWORD exitCode = 0;
              GetExitCodeProcess(pi.hProcess, &exitCode);
              CloseHandle(pi.hProcess);
              CloseHandle(pi.hThread);
              CloseHandle(hReadPipe);
              return static_cast<int>(exitCode);
          };
#else
          auto runCmdWithOutput = [&](const std::string& cmd) -> int
          {
              buildPtr->appendBuildOutput("> " + cmd);
              std::string fullCmd = cmd + " 2>&1";
              FILE* pipe = popen(fullCmd.c_str(), "r");
              if (!pipe) return -1;
              char buf[4096];
              while (fgets(buf, sizeof(buf), pipe))
              {
                  if (buildPtr->m_buildCancelRequested.load()) break;
                  std::string line = buf;
                  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                      line.pop_back();
                  buildPtr->appendBuildOutput("  " + line);
              }
              return pclose(pipe);
          };
#endif

          // Compute the deploy directory once – used by Step 2 (configure)
          // and Step 4 (deploy).  Isolates game build outputs so that
          // editor DLLs from previous builds don't contaminate the output.
          const std::string buildDir = config.binaryDir;
          const std::string deployDir = (std::filesystem::path(buildDir) / "deploy").string();

          // ── Detect pre-built engine libraries for lightweight game build ──
          // If the editor deployed .lib files into Tools/lib/ we can skip
          // the full engine configure+build and only compile main.cpp.
          const std::filesystem::path editorToolsLib = std::filesystem::path(editorBaseDir) / "Tools" / "lib";
          const std::filesystem::path editorToolsInclude = std::filesystem::path(editorBaseDir) / "Tools" / "include";
          const bool hasPrebuiltLibs = std::filesystem::exists(editorToolsLib / "ScriptingRuntime.lib")
                                    && std::filesystem::exists(editorToolsLib / "RendererRuntime.lib")
                                    && std::filesystem::exists(editorToolsLib / "AssetManagerRuntime.lib")
                                    && std::filesystem::exists(editorToolsLib / "Core.lib")
                                    && std::filesystem::exists(editorToolsLib / "SDL3.lib");

          // Step 2: CMake configure
          if (ok && !checkCancelled())
          {
              advanceStep("CMake configure...");

              // Optionally clean the binary cache
              if (config.cleanBuild)
              {
                  buildPtr->appendBuildOutput("  Clean build: removing " + buildDir);
                  std::error_code ec;
                  std::filesystem::remove_all(buildDir, ec);
              }

              {
                  std::error_code ec;
                  std::filesystem::create_directories(buildDir, ec);
              }

              if (hasPrebuiltLibs)
              {
                  // ── Lightweight game build ──────────────────────────────
                  // Generate a minimal CMakeLists.txt that only compiles
                  // main.cpp and links against pre-built engine libraries.
                  // This avoids recompiling the entire engine.
                  buildPtr->appendBuildOutput("  Using pre-built engine libraries (lightweight build)");

                  const std::filesystem::path lightCMake = std::filesystem::path(buildDir) / "CMakeLists.txt";
                  {
                      std::ostringstream cm;
                      cm << "cmake_minimum_required(VERSION 3.12)\n";
                      cm << "project(HorizonEngineGameBuild LANGUAGES C CXX)\n\n";
                      cm << "set(CMAKE_CXX_STANDARD 20)\n";
                      cm << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
                      cm << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
                      cm << "if(MSVC)\n";
                      cm << "    set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL\")\n";
                      cm << "endif()\n\n";

                      // Engine source for headers
                      const auto srcDir = std::filesystem::path(engineSourceDir);
                      cm << "set(ENGINE_SRC \"" << srcDir.generic_string() << "/src\")\n";
                      cm << "set(ENGINE_EXT \"" << srcDir.generic_string() << "/external\")\n";
                      cm << "set(EDITOR_BASE \"" << std::filesystem::path(editorBaseDir).generic_string() << "\")\n\n";

                      cm << "add_executable(HorizonEngineRuntime \"${ENGINE_SRC}/main.cpp\")\n\n";

                      cm << "target_include_directories(HorizonEngineRuntime PRIVATE\n";
                      cm << "    \"${ENGINE_SRC}\"\n";
                      cm << "    \"${ENGINE_SRC}/Renderer\"\n";
                      cm << "    \"${ENGINE_SRC}/Logger\"\n";
                      cm << "    \"${ENGINE_SRC}/AssetManager\"\n";
                      cm << "    \"${ENGINE_SRC}/Diagnostics\"\n";
                      cm << "    \"${ENGINE_SRC}/Core\"\n";
                      cm << "    \"${ENGINE_SRC}/Landscape\"\n";
                      cm << "    \"${ENGINE_SRC}/Physics\"\n";
                      cm << "    \"${ENGINE_SRC}/Scripting\"\n";
                      cm << "    \"${ENGINE_SRC}/NativeScripting\"\n";
                      cm << "    \"${EDITOR_BASE}/Tools/include\"\n";  // SDL3 headers
                      cm << "    \"" << srcDir.generic_string() << "/CrashHandler\"\n";
                      cm << ")\n\n";

                      cm << "target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_EDITOR=0)\n";
                      if (!config.startLevel.empty())
                          cm << "target_compile_definitions(HorizonEngineRuntime PRIVATE GAME_START_LEVEL=\"" << config.startLevel << "\")\n";
                      if (!config.windowTitle.empty())
                          cm << "target_compile_definitions(HorizonEngineRuntime PRIVATE GAME_WINDOW_TITLE_BAKED=\"" << config.windowTitle << "\")\n";

                      // Build profile
                      if (config.profile.name == "Debug")
                          cm << "target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_BUILD_DEBUG=1)\n";
                      else if (config.profile.name == "Development")
                          cm << "target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_BUILD_DEVELOPMENT=1)\n";
                      else
                          cm << "target_compile_definitions(HorizonEngineRuntime PRIVATE ENGINE_BUILD_SHIPPING=1)\n";

                      cm << "\n# Link against pre-built engine libraries\n";
                      cm << "target_link_directories(HorizonEngineRuntime PRIVATE\n";
                      cm << "    \"${EDITOR_BASE}/Tools/lib\"\n";
                      cm << ")\n";
                      cm << "target_link_libraries(HorizonEngineRuntime PRIVATE\n";
                      cm << "    ScriptingRuntime RendererRuntime AssetManagerRuntime\n";
                      cm << "    Core Logger Diagnostics Physics\n";
                      cm << "    LandscapeRuntime SDL3\n";
                      cm << ")\n\n";

                      // DELAYLOAD for runtime DLLs
                      cm << "if(MSVC)\n";
                      cm << "    target_link_options(HorizonEngineRuntime PRIVATE\n";
                      cm << "        /DELAYLOAD:SDL3.dll\n";
                      cm << "        /DELAYLOAD:Logger.dll\n";
                      cm << "        /DELAYLOAD:Diagnostics.dll\n";
                      cm << "        /DELAYLOAD:Core.dll\n";
                      cm << "        /DELAYLOAD:Physics.dll\n";
                      cm << "        /DELAYLOAD:AssetManagerRuntime.dll\n";
                      cm << "        /DELAYLOAD:ScriptingRuntime.dll\n";
                      cm << "        /DELAYLOAD:RendererRuntime.dll\n";
                      cm << "    )\n";
                      cm << "    target_link_libraries(HorizonEngineRuntime PRIVATE delayimp)\n";
                      cm << "endif()\n\n";

                      // Windows GUI settings
                      cm << "if(WIN32)\n";
                      cm << "    set_target_properties(HorizonEngineRuntime PROPERTIES WIN32_EXECUTABLE OFF)\n";
                      cm << "endif()\n";

                      std::ofstream out(lightCMake, std::ios::out | std::ios::trunc);
                      if (out.is_open())
                          out << cm.str();
                  }

                  // Configure the lightweight project
                  std::string configureCmd = "\"" + cmakePath + "\"";
                  configureCmd += " -S \"" + buildDir + "\"";
                  configureCmd += " -B \"" + buildDir + "\"";

                  int ret = runCmdWithOutput(configureCmd);
                  if (ret != 0)
                  {
                      ok = false;
                      errorMsg = "CMake configure failed (exit code " + std::to_string(ret) + ").";
                  }
              }
              else
              {
                  // ── Full engine build (fallback) ───────────────────────
                  buildPtr->appendBuildOutput("  Pre-built libraries not found, using full engine build");

                  std::string configureCmd = "\"" + cmakePath + "\"";
                  configureCmd += " -S \"" + engineSourceDir + "\"";
                  configureCmd += " -B \"" + buildDir + "\"";
                  configureCmd += " -DENGINE_BUILD_RUNTIME=ON";
                  configureCmd += " -DGAME_START_LEVEL=\"" + config.startLevel + "\"";
                  configureCmd += " -DGAME_WINDOW_TITLE=\"" + config.windowTitle + "\"";
                  configureCmd += " -DENGINE_BUILD_PROFILE=" + config.profile.name;
                  configureCmd += " -DENGINE_DEPLOY_DIR=\"" + deployDir + "\"";

                  int ret = runCmdWithOutput(configureCmd);
                  if (ret != 0)
                  {
                      ok = false;
                      errorMsg = "CMake configure failed (exit code " + std::to_string(ret) + ").";
                  }
              }
          }

          // Step 3: CMake build
          if (ok && !checkCancelled())
          {
              advanceStep("CMake build...");

              if (!hasPrebuiltLibs)
              {
                  // Full build: clean deploy dir to avoid stale DLLs
                  std::error_code ec;
                  std::filesystem::remove_all(deployDir, ec);
              }

              std::string buildCmd = "\"" + cmakePath + "\"";
              buildCmd += " --build \"" + buildDir + "\"";
              buildCmd += " --target HorizonEngineRuntime";
              buildCmd += " --config " + config.profile.cmakeBuildType;

              int ret = runCmdWithOutput(buildCmd);
              if (ret != 0)
              {
                  ok = false;
                  errorMsg = "CMake build failed (exit code " + std::to_string(ret) + ").";
              }
          }

          // Step 4: Deploy built runtime exe + DLLs
          if (ok && !checkCancelled())
          {
              advanceStep("Deploying runtime...");

              const std::filesystem::path editorDir = editorBaseDir;
              const std::filesystem::path dstDir = config.outputDir;

              // Find the built runtime exe in the deploy directory.
              // ENGINE_DEPLOY_DIR flattens all configs into one folder.
              std::filesystem::path builtExe;
              {
                  auto tryPath = std::filesystem::path(deployDir) / "HorizonEngineRuntime.exe";
                  if (std::filesystem::exists(tryPath))
                      builtExe = tryPath;
                  else
                  {
                      // Fallback: multi-config subdir in binary cache
                      tryPath = std::filesystem::path(config.binaryDir) / config.profile.cmakeBuildType / "HorizonEngineRuntime.exe";
                      if (std::filesystem::exists(tryPath))
                          builtExe = tryPath;
                      else
                      {
                          // Single-config generators
                          tryPath = std::filesystem::path(config.binaryDir) / "HorizonEngineRuntime.exe";
                          if (std::filesystem::exists(tryPath))
                              builtExe = tryPath;
                      }
                  }
              }

              if (builtExe.empty())
              {
                  ok = false;
                  errorMsg = "Built runtime exe not found in binary cache: " + config.binaryDir;
              }
              else
              {
                  // Copy runtime exe as <WindowTitle>.exe
                  const std::filesystem::path dstExe = dstDir / (config.windowTitle + ".exe");
                  {
                      std::error_code ec;
                      std::filesystem::copy_file(builtExe, dstExe,
                          std::filesystem::copy_options::overwrite_existing, ec);
                      if (ec)
                      {
                          ok = false;
                          errorMsg = "Failed to copy runtime exe: " + ec.message();
                      }
                      else
                      {
                          buildPtr->appendBuildOutput("  Copied runtime: " + builtExe.string());
                      }
                  }

                  // Copy PDB for non-Shipping into Symbols/ subdirectory
                  if (ok && config.profile.name != "Shipping")
                  {
                      auto builtPdb = builtExe;
                      builtPdb.replace_extension(".pdb");
                      if (std::filesystem::exists(builtPdb))
                      {
                          const auto symbolsDir = dstDir / "Symbols";
                          std::error_code ec;
                          std::filesystem::create_directories(symbolsDir, ec);
                          std::filesystem::copy_file(builtPdb,
                              symbolsDir / (config.windowTitle + ".pdb"),
                              std::filesystem::copy_options::overwrite_existing, ec);
                          if (!ec)
                              buildPtr->appendBuildOutput("  Copied PDB: Symbols/" + config.windowTitle + ".pdb");
                      }
                  }
              }

              // Copy DLLs into Engine/ subdirectory.
              const std::filesystem::path engineDir = dstDir / "Engine";
              if (ok)
              {
                  std::error_code mkEc;
                  std::filesystem::create_directories(engineDir, mkEc);

                  if (hasPrebuiltLibs)
                  {
                      // Lightweight build: copy runtime DLLs from editor deploy dir.
                      // Shared engine DLLs live in the editor root, runtime-variant
                      // DLLs are deployed to Tools/lib/ alongside the .lib files.
                      auto isRuntimeDll = [](const std::string& filename) -> bool
                      {
                          std::string lower = filename;
                          std::transform(lower.begin(), lower.end(), lower.begin(),
                              [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                          // Editor-only DLLs to skip
                          if (lower == "assetmanager.dll" || lower == "scripting.dll" || lower == "renderer.dll")
                              return false;
                          // Skip the editor exe
                          if (lower == "horizonengine.exe")
                              return false;
                          return true;
                      };

                      // 1) Copy shared DLLs from editor root (Core, Logger, SDL3, etc.)
                      for (const auto& entry : std::filesystem::directory_iterator(editorDir))
                      {
                          if (!entry.is_regular_file()) continue;
                          const auto ext = entry.path().extension().string();
                          std::string extLower = ext;
                          std::transform(extLower.begin(), extLower.end(), extLower.begin(),
                              [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                          if (extLower != ".dll") continue;

                          const auto fname = entry.path().filename().string();
                          if (!isRuntimeDll(fname))
                          {
                              buildPtr->appendBuildOutput("  Skipped editor DLL: " + fname);
                              continue;
                          }

                          std::error_code copyEc;
                          std::filesystem::copy_file(entry.path(),
                              engineDir / entry.path().filename(),
                              std::filesystem::copy_options::overwrite_existing, copyEc);
                          if (!copyEc)
                              buildPtr->appendBuildOutput("  Deployed DLL: " + fname);
                      }

                      // 2) Copy runtime-variant DLLs from Tools/lib/
                      //    (ScriptingRuntime.dll, RendererRuntime.dll, AssetManagerRuntime.dll)
                      if (std::filesystem::exists(editorToolsLib))
                      {
                          for (const auto& entry : std::filesystem::directory_iterator(editorToolsLib))
                          {
                              if (!entry.is_regular_file()) continue;
                              const auto ext = entry.path().extension().string();
                              std::string extLower = ext;
                              std::transform(extLower.begin(), extLower.end(), extLower.begin(),
                                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                              if (extLower != ".dll") continue;

                              std::error_code copyEc;
                              std::filesystem::copy_file(entry.path(),
                                  engineDir / entry.path().filename(),
                                  std::filesystem::copy_options::overwrite_existing, copyEc);
                              if (!copyEc)
                                  buildPtr->appendBuildOutput("  Deployed runtime DLL: " + entry.path().filename().string());
                          }
                      }
                  }
                  else
                  {
                      // Full build: copy DLLs from the binary cache.
                      const std::filesystem::path builtDir = builtExe.parent_path();

                      // Editor-only shared libraries that must NOT be deployed.
                      auto isEditorOnlyDll = [](const std::string& filename) -> bool
                      {
                          std::string lower = filename;
                          std::transform(lower.begin(), lower.end(), lower.begin(),
                              [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                          return lower == "assetmanager.dll"
                              || lower == "scripting.dll"
                              || lower == "renderer.dll";
                      };

                      for (const auto& entry : std::filesystem::directory_iterator(builtDir))
                      {
                          if (!entry.is_regular_file()) continue;
                          const auto ext = entry.path().extension().string();
                          if (ext != ".dll" && ext != ".DLL") continue;

                          const auto fname = entry.path().filename().string();
                          if (isEditorOnlyDll(fname))
                          {
                              buildPtr->appendBuildOutput("  Skipped editor DLL: " + fname);
                              continue;
                          }

                          std::error_code copyEc;
                          std::filesystem::copy_file(entry.path(),
                              engineDir / entry.path().filename(),
                              std::filesystem::copy_options::overwrite_existing, copyEc);
                          if (!copyEc)
                              buildPtr->appendBuildOutput("  Deployed DLL: " + fname);
                      }
                  }

                  // Copy PDBs from binary cache into Symbols/ for non-Shipping
                  if (config.profile.name != "Shipping")
                  {
                      const auto symbolsDir = dstDir / "Symbols";
                      std::error_code mkEc;
                      std::filesystem::create_directories(symbolsDir, mkEc);
                      const auto pdbDir = builtExe.parent_path();
                      for (const auto& entry : std::filesystem::directory_iterator(pdbDir))
                      {
                          if (!entry.is_regular_file()) continue;
                          if (entry.path().extension().string() != ".pdb") continue;
                          std::error_code copyEc;
                          std::filesystem::copy_file(entry.path(),
                              symbolsDir / entry.path().filename(),
                              std::filesystem::copy_options::overwrite_existing, copyEc);
                      }
                  }
              }

              // Copy Python runtime (DLL + zip) from editor directory into Engine/
              // Python is an external dependency (not built by CMake) so it won't
              // be in the deploy dir — copy from the editor's directory.
              if (ok)
              {
                  for (const auto& entry : std::filesystem::directory_iterator(editorDir))
                  {
                      if (!entry.is_regular_file()) continue;
                      const auto fname = entry.path().filename().string();
                      const auto fext = entry.path().extension().string();
                      if ((fext == ".dll" || fext == ".zip") && fname.size() >= 8 && fname.substr(0, 6) == "python")
                      {
                          const auto dst = engineDir / entry.path().filename();
                          if (!std::filesystem::exists(dst))
                          {
                              std::error_code copyEc;
                              std::filesystem::copy_file(entry.path(), dst,
                                  std::filesystem::copy_options::overwrite_existing, copyEc);
                              if (!copyEc)
                                  buildPtr->appendBuildOutput("  Copied Python file: " + fname);
                          }
                      }
                  }
              }

              // Copy CrashHandler from editor's Tools/ directory into Engine/Tools/
              if (ok)
              {
                  const auto srcCrashHandler = editorDir / "Tools" / "CrashHandler.exe";
                  if (std::filesystem::exists(srcCrashHandler))
                  {
                      const auto dstTools = engineDir / "Tools";
                      std::error_code ec;
                      std::filesystem::create_directories(dstTools, ec);
                      std::filesystem::copy_file(srcCrashHandler, dstTools / "CrashHandler.exe",
                          std::filesystem::copy_options::overwrite_existing, ec);
                      if (!ec)
                          buildPtr->appendBuildOutput("  Copied CrashHandler.exe");

                      // Also copy CrashHandler PDB for non-Shipping
                      if (config.profile.name != "Shipping")
                      {
                          const auto srcPdb = editorDir / "Tools" / "CrashHandler.pdb";
                          if (std::filesystem::exists(srcPdb))
                          {
                              std::error_code ec2;
                              std::filesystem::copy_file(srcPdb, engineDir / "Tools" / "CrashHandler.pdb",
                                  std::filesystem::copy_options::overwrite_existing, ec2);
                          }
                      }
                  }
                  else
                  {
                      buildPtr->appendBuildOutput("  [WARN] CrashHandler.exe not found at: " + srcCrashHandler.string());
                  }
              }
          }

        // Step 5: Bundle VC++ Redistributable
        if (ok && !checkCancelled())
        {
            advanceStep("Bundling VC++ Redistributable...");

            const std::filesystem::path dstDir = config.outputDir;

            // Search for vc_redist.x64.exe in several locations
            std::filesystem::path vcRedistSrc;
            {
                // 1. Tools/ next to editor exe
                auto tryPath = std::filesystem::path(editorBaseDir) / "Tools" / "vc_redist.x64.exe";
                if (std::filesystem::exists(tryPath))
                    vcRedistSrc = tryPath;

                // 2. tools/ next to editor exe (lowercase)
                if (vcRedistSrc.empty())
                {
                    tryPath = std::filesystem::path(editorBaseDir) / "tools" / "vc_redist.x64.exe";
                    if (std::filesystem::exists(tryPath))
                        vcRedistSrc = tryPath;
                }

                // 3. Engine source tools/ directory
                if (vcRedistSrc.empty())
                {
                    tryPath = std::filesystem::path(engineSourceDir) / "tools" / "vc_redist.x64.exe";
                    if (std::filesystem::exists(tryPath))
                        vcRedistSrc = tryPath;
                }

                // 4. Engine source Tools/ directory
                if (vcRedistSrc.empty())
                {
                    tryPath = std::filesystem::path(engineSourceDir) / "Tools" / "vc_redist.x64.exe";
                    if (std::filesystem::exists(tryPath))
                        vcRedistSrc = tryPath;
                }
            }

            // If not found locally, try to download it
            if (vcRedistSrc.empty())
            {
                buildPtr->appendBuildOutput("  vc_redist.x64.exe not found locally, downloading...");

                const std::filesystem::path toolsDir = std::filesystem::path(editorBaseDir) / "Tools";
                {
                    std::error_code ec;
                    std::filesystem::create_directories(toolsDir, ec);
                }
                const std::filesystem::path downloadDst = toolsDir / "vc_redist.x64.exe";

#if defined(_WIN32)
                // Use PowerShell to download (same pattern as bootstrap.ps1)
                std::string dlCmd = "powershell -NoProfile -ExecutionPolicy Bypass -Command \"";
                dlCmd += "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; ";
                dlCmd += "Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' ";
                dlCmd += "-OutFile '";
                dlCmd += downloadDst.string();
                dlCmd += "' -UseBasicParsing\"";

                int dlRet = runCmdWithOutput(dlCmd);
                if (dlRet == 0 && std::filesystem::exists(downloadDst))
                {
                    vcRedistSrc = downloadDst;
                    buildPtr->appendBuildOutput("  Downloaded vc_redist.x64.exe successfully.");
                }
                else
                {
                    buildPtr->appendBuildOutput("  [WARN] Failed to download vc_redist.x64.exe.");
                    buildPtr->appendBuildOutput("  [WARN] Users may need to install the VC++ Redistributable manually.");
                    buildPtr->appendBuildOutput("  [WARN] Download: https://aka.ms/vs/17/release/vc_redist.x64.exe");
                }
#else
                buildPtr->appendBuildOutput("  [INFO] VC++ Redistributable is Windows-only, skipping.");
#endif
            }

            // Copy to game output
            if (!vcRedistSrc.empty())
            {
                const auto dstRedist = dstDir / "vc_redist.x64.exe";
                std::error_code ec;
                std::filesystem::copy_file(vcRedistSrc, dstRedist,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec)
                    buildPtr->appendBuildOutput("  Bundled: vc_redist.x64.exe");
                else
                    buildPtr->appendBuildOutput("  [WARN] Failed to copy vc_redist.x64.exe: " + ec.message());
            }

            // Generate launcher batch script that checks for VC runtime
            // and installs it silently if missing before starting the game.
            {
                const std::filesystem::path batPath = dstDir / (config.windowTitle + ".bat");
                std::ofstream bat(batPath);
                if (bat.is_open())
                {
                    bat << "@echo off\r\n";
                    bat << ":: Auto-generated by HorizonEngine Build\r\n";
                    bat << ":: Checks for VC++ Runtime and installs if missing, then launches the game.\r\n";
                    bat << "\r\n";
                    bat << ":: Check if vcruntime140.dll is loadable\r\n";
                    bat << "where /Q vcruntime140.dll >nul 2>&1\r\n";
                    bat << "if %ERRORLEVEL% EQU 0 goto :launch\r\n";
                    bat << "\r\n";
                    bat << ":: Check System32 directly\r\n";
                    bat << "if exist \"%SystemRoot%\\System32\\vcruntime140.dll\" goto :launch\r\n";
                    bat << "\r\n";
                    bat << ":: VC++ Runtime not found - install it\r\n";
                    bat << "echo Installing Microsoft Visual C++ Runtime...\r\n";
                    bat << "if exist \"%~dp0vc_redist.x64.exe\" (\r\n";
                    bat << "    \"%~dp0vc_redist.x64.exe\" /install /quiet /norestart\r\n";
                    bat << "    if %ERRORLEVEL% NEQ 0 (\r\n";
                    bat << "        echo.\r\n";
                    bat << "        echo VC++ Runtime installation failed. Please install manually:\r\n";
                    bat << "        echo https://aka.ms/vs/17/release/vc_redist.x64.exe\r\n";
                    bat << "        pause\r\n";
                    bat << "        exit /b 1\r\n";
                    bat << "    )\r\n";
                    bat << ") else (\r\n";
                    bat << "    echo.\r\n";
                    bat << "    echo Missing: vc_redist.x64.exe\r\n";
                    bat << "    echo Please download and install the VC++ Redistributable:\r\n";
                    bat << "    echo https://aka.ms/vs/17/release/vc_redist.x64.exe\r\n";
                    bat << "    pause\r\n";
                    bat << "    exit /b 1\r\n";
                    bat << ")\r\n";
                    bat << "\r\n";
                    bat << ":launch\r\n";
                    bat << "start \"\" \"%~dp0" << config.windowTitle << ".exe\"\r\n";
                    bat.close();
                    buildPtr->appendBuildOutput("  Generated launcher: " + batPath.filename().string());
                }
            }
        }

        // Step 6: Cook assets + copy shaders + asset registry
        if (ok && !checkCancelled())
        {
            advanceStep("Cooking assets...");

            const std::filesystem::path dstDir = config.outputDir;

            // Cook project assets via AssetCooker
            {
                AssetCooker cooker;
                AssetCooker::CookConfig cookCfg;
                cookCfg.projectRoot    = projectPath;
                cookCfg.engineRoot     = editorBaseDir;
                cookCfg.outputDir      = (dstDir / "Content").string();
                cookCfg.compressAssets  = config.profile.compressAssets;
                cookCfg.buildType       = config.profile.name;

                auto result = cooker.cookAll(cookCfg,
                    [&](size_t done, size_t total, const std::string& current)
                    {
                        buildPtr->appendBuildOutput("  [" + std::to_string(done) + "/" + std::to_string(total) + "] " + current);
                    },
                    &buildPtr->m_buildCancelRequested);

                buildPtr->appendBuildOutput("Asset cooking complete: "
                    + std::to_string(result.cookedAssets) + " cooked, "
                    + std::to_string(result.skippedAssets) + " skipped, "
                    + std::to_string(result.failedAssets) + " failed ("
                    + std::to_string(static_cast<int>(result.elapsedSeconds * 1000)) + " ms)");

                if (!result.success)
                {
                    ok = false;
                    errorMsg = "Asset cooking failed.";
                    for (const auto& e : result.errors)
                        buildPtr->appendBuildOutput("  [ERROR] " + e);
                }
            }

            // Copy shaders (not part of asset registry)
            if (ok)
            {
                const std::filesystem::path srcShaders = std::filesystem::path(editorBaseDir) / "shaders";
                const std::filesystem::path dstShaders = dstDir / "shaders";
                if (std::filesystem::exists(srcShaders))
                {
                    std::error_code ec;
                    std::filesystem::copy(srcShaders, dstShaders,
                        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
                }
            }

            // Copy engine-bundled Content (shipped assets not in project registry)
            if (ok)
            {
                const std::filesystem::path srcEngContent = std::filesystem::path(editorBaseDir) / "Content";
                const std::filesystem::path dstEngContent = dstDir / "Content";
                if (std::filesystem::exists(srcEngContent))
                {
                    std::error_code ec;
                    std::filesystem::copy(srcEngContent, dstEngContent,
                        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::skip_existing, ec);
                }
            }

            // Copy AssetRegistry.bin + project defaults.ini
            if (ok)
            {
                const std::filesystem::path dstConfig = dstDir / "Config";
                std::error_code ec;
                std::filesystem::create_directories(dstConfig, ec);
                const std::filesystem::path srcReg = std::filesystem::path(projectPath) / "Config" / "AssetRegistry.bin";
                if (std::filesystem::exists(srcReg))
                {
                    std::filesystem::copy_file(srcReg, dstConfig / "AssetRegistry.bin",
                        std::filesystem::copy_options::overwrite_existing, ec);
                }
                // Copy project config (defaults.ini) so engine settings persist in the build
                const std::filesystem::path srcDefaults = std::filesystem::path(projectPath) / "Config" / "defaults.ini";
                if (std::filesystem::exists(srcDefaults))
                {
                    std::filesystem::copy_file(srcDefaults, dstConfig / "defaults.ini",
                        std::filesystem::copy_options::overwrite_existing, ec);
                    buildPtr->appendBuildOutput("  Copied project config: defaults.ini");
                }
            }

            }

          // Step 7: Build C++ GameScripts DLL
          if (ok && !checkCancelled())
          {
              advanceStep("Building C++ game scripts...");

              const std::filesystem::path nativeDir = std::filesystem::path(projectPath) / "Content" / "Scripts" / "Native";
              bool hasNativeCpp = false;

              if (std::filesystem::exists(nativeDir) && std::filesystem::is_directory(nativeDir))
              {
                  for (const auto& entry : std::filesystem::directory_iterator(nativeDir))
                  {
                      if (entry.is_regular_file() && entry.path().extension() == ".cpp")
                      { hasNativeCpp = true; break; }
                  }
              }

              if (hasNativeCpp)
              {
                  // ── Auto-generate _AutoRegister.cpp ──
                  {
                      std::vector<std::pair<std::string, std::string>> discovered;
                      const std::regex classPattern(
                          R"(class\s+(\w+)\s*(?:final\s*)?:\s*(?:public\s+)?INativeScript\b)");

                      for (const auto& entry : std::filesystem::directory_iterator(nativeDir))
                      {
                          if (!entry.is_regular_file()) continue;
                          const auto ext = entry.path().extension().string();
                          if (ext != ".h" && ext != ".hpp") continue;

                          std::ifstream hFile(entry.path());
                          if (!hFile.is_open()) continue;

                          std::string hLine;
                          while (std::getline(hFile, hLine))
                          {
                              std::smatch match;
                              if (std::regex_search(hLine, match, classPattern))
                                  discovered.emplace_back(match[1].str(), entry.path().filename().string());
                          }
                      }

                      const auto autoRegPath = nativeDir / "_AutoRegister.cpp";
                      std::ofstream autoReg(autoRegPath, std::ios::out | std::ios::trunc);
                      if (autoReg.is_open())
                      {
                          autoReg << "// Auto-generated by Horizon Engine -- DO NOT EDIT\n";
                          autoReg << "#include \"GameplayAPI.h\"\n";
                          for (const auto& [cls, hdr] : discovered)
                              autoReg << "#include \"" << hdr << "\"\n";
                          autoReg << "\n";
                          for (const auto& [cls, hdr] : discovered)
                              autoReg << "REGISTER_NATIVE_SCRIPT(" << cls << ")\n";
                          autoReg.close();
                      }

                      buildPtr->appendBuildOutput("  Auto-registered " + std::to_string(discovered.size()) + " native script class(es).");
                  }

                  // ── Generate CMakeLists.txt for GameScripts ──
                  {
                      const auto cmakeListsPath = nativeDir / "CMakeLists.txt";
                      const auto toolsInclude = std::filesystem::path(editorBaseDir) / "Tools" / "include" / "NativeScripting";
                      const auto toolsLib = std::filesystem::path(editorBaseDir) / "Tools" / "lib";

                      std::ostringstream cm;
                      cm << "cmake_minimum_required(VERSION 3.12)\n";
                      cm << "project(GameScripts LANGUAGES CXX)\n\n";
                      cm << "set(CMAKE_CXX_STANDARD 20)\n";
                      cm << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
                      cm << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
                      cm << "if(MSVC)\n";
                      cm << "    set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL\")\n";
                      cm << "endif()\n\n";
                      cm << "file(GLOB GAME_SOURCES \"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp\")\n";
                      cm << "file(GLOB GAME_HEADERS \"${CMAKE_CURRENT_SOURCE_DIR}/*.h\")\n\n";
                      cm << "add_library(GameScripts SHARED ${GAME_SOURCES} ${GAME_HEADERS})\n\n";
                      cm << "target_include_directories(GameScripts PRIVATE\n";
                      cm << "    \"" << toolsInclude.generic_string() << "\"\n";
                      cm << ")\n\n";
                      cm << "target_link_directories(GameScripts PRIVATE\n";
                      cm << "    \"" << toolsLib.generic_string() << "\"\n";
                      cm << ")\n";
                      cm << "target_link_libraries(GameScripts PRIVATE ScriptingRuntime)\n";

                      std::ofstream out(cmakeListsPath, std::ios::out | std::ios::trunc);
                      if (out.is_open()) out << cm.str();
                  }

                  // ── CMake configure GameScripts ──
                  const std::string gsBuildDir = (std::filesystem::path(config.binaryDir) / "GameScripts").string();
                  {
                      std::error_code ec;
                      std::filesystem::create_directories(gsBuildDir, ec);
                  }

                  {
                      std::string gsConfigureCmd = "\"" + cmakePath + "\"";
                      gsConfigureCmd += " -S \"" + nativeDir.string() + "\"";
                      gsConfigureCmd += " -B \"" + gsBuildDir + "\"";

                      int ret = runCmdWithOutput(gsConfigureCmd);
                      if (ret != 0)
                      {
                          ok = false;
                          errorMsg = "GameScripts CMake configure failed (exit code " + std::to_string(ret) + ").";
                      }
                  }

                  // ── CMake build GameScripts ──
                  if (ok && !checkCancelled())
                  {
                      std::string gsBuildCmd = "\"" + cmakePath + "\"";
                      gsBuildCmd += " --build \"" + gsBuildDir + "\"";
                      gsBuildCmd += " --target GameScripts";
                      gsBuildCmd += " --config " + config.profile.cmakeBuildType;

                      int ret = runCmdWithOutput(gsBuildCmd);
                      if (ret != 0)
                      {
                          ok = false;
                          errorMsg = "GameScripts build failed (exit code " + std::to_string(ret) + ").";
                      }
                  }

                  // ── Deploy GameScripts.dll to output Engine/ directory ──
                  if (ok)
                  {
                      const std::filesystem::path engineOutDir = std::filesystem::path(config.outputDir) / "Engine";
                      std::error_code ec;
                      std::filesystem::create_directories(engineOutDir, ec);

                      // Search for the built DLL in several possible locations
                      std::filesystem::path builtDll;
                      {
                          // Multi-config generator (MSVC): <buildDir>/<Config>/GameScripts.dll
                          auto tryPath = std::filesystem::path(gsBuildDir) / config.profile.cmakeBuildType / "GameScripts.dll";
                          if (std::filesystem::exists(tryPath))
                              builtDll = tryPath;
                          // Single-config generator
                          if (builtDll.empty())
                          {
                              tryPath = std::filesystem::path(gsBuildDir) / "GameScripts.dll";
                              if (std::filesystem::exists(tryPath))
                                  builtDll = tryPath;
                          }
                      }

                      if (!builtDll.empty())
                      {
                          std::filesystem::copy_file(builtDll, engineOutDir / "GameScripts.dll",
                              std::filesystem::copy_options::overwrite_existing, ec);
                          if (!ec)
                              buildPtr->appendBuildOutput("  Deployed GameScripts.dll to Engine/");
                          else
                              buildPtr->appendBuildOutput("  [WARN] Failed to copy GameScripts.dll: " + ec.message());

                          // Copy PDB for non-Shipping builds
                          if (config.profile.name != "Shipping")
                          {
                              auto builtPdb = builtDll;
                              builtPdb.replace_extension(".pdb");
                              if (std::filesystem::exists(builtPdb))
                              {
                                  const auto symbolsDir = std::filesystem::path(config.outputDir) / "Symbols";
                                  std::filesystem::create_directories(symbolsDir, ec);
                                  std::filesystem::copy_file(builtPdb, symbolsDir / "GameScripts.pdb",
                                      std::filesystem::copy_options::overwrite_existing, ec);
                                  if (!ec)
                                      buildPtr->appendBuildOutput("  Copied GameScripts.pdb to Symbols/");
                              }
                          }
                      }
                      else
                      {
                          ok = false;
                          errorMsg = "GameScripts.dll not found after build in: " + gsBuildDir;
                      }
                  }
              }
              else
              {
                  buildPtr->appendBuildOutput("  No C++ game scripts found, skipping.");
              }
          }

            // Step 8: Package content into HPK archive
            if (ok && !checkCancelled())
            {
                advanceStep("Packaging content into HPK...");

            const std::filesystem::path dstDir = config.outputDir;
            // Write HPK to a temporary location in root, then move to Content/
            const std::filesystem::path hpkPathTemp = dstDir / "content.hpk";

            HPKWriter writer;
            if (!writer.begin(hpkPathTemp.string()))
            {
                ok = false;
                errorMsg = "Failed to create HPK archive: " + hpkPathTemp.string();
            }

            // Collect all files from Content/, shaders/, Config/
            auto packDirectory = [&](const std::filesystem::path& dir)
            {
                if (!ok || !std::filesystem::exists(dir)) return;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
                {
                    if (!entry.is_regular_file()) continue;
                    if (checkCancelled()) { ok = false; errorMsg = "Cancelled."; return; }

                    auto relPath = std::filesystem::relative(entry.path(), dstDir);
                    std::string vpath = relPath.generic_string();
                    if (!writer.addFileFromDisk(vpath, entry.path().string()))
                    {
                        buildPtr->appendBuildOutput("  [WARN] Failed to pack: " + vpath);
                    }
                }
            };

            if (ok) packDirectory(dstDir / "Content");
            if (ok) packDirectory(dstDir / "shaders");
            if (ok) packDirectory(dstDir / "Config");

            if (ok)
            {
                if (!writer.finalize())
                {
                    ok = false;
                    errorMsg = "Failed to finalize HPK archive.";
                }
                else
                {
                    buildPtr->appendBuildOutput("  HPK archive: " + hpkPathTemp.string()
                        + " (" + std::to_string(writer.getFileCount()) + " files)");

                    // Remove loose directories now that they're packed
                    std::error_code ec;
                    std::filesystem::remove_all(dstDir / "Content", ec);
                    std::filesystem::remove_all(dstDir / "shaders", ec);
                    std::filesystem::remove_all(dstDir / "Config", ec);

                    // Move HPK into Content/ subdirectory for clean layout
                    const std::filesystem::path contentDir = dstDir / "Content";
                    std::filesystem::create_directories(contentDir, ec);
                    const std::filesystem::path hpkFinal = contentDir / "content.hpk";
                    std::filesystem::rename(hpkPathTemp, hpkFinal, ec);
                    if (ec)
                    {
                        // Fallback: try copy + delete if rename fails (cross-device)
                        std::filesystem::copy_file(hpkPathTemp, hpkFinal,
                            std::filesystem::copy_options::overwrite_existing, ec);
                        std::filesystem::remove(hpkPathTemp, ec);
                    }
                }
            }

            // Copy engine config as a loose file AFTER HPK packaging.
            // Must happen after remove_all("Config") because on Windows
            // NTFS (case-insensitive) "Config/" and "config/" are the
            // same directory — packing + deleting "Config" would also
            // remove "config/config.ini" if it were placed earlier.
            if (ok)
            {
                const std::filesystem::path srcEngineConfig = std::filesystem::current_path() / "config" / "config.ini";
                if (std::filesystem::exists(srcEngineConfig))
                {
                    const std::filesystem::path dstEngineConfig = dstDir / "config" / "config.ini";
                    std::error_code ec2;
                    std::filesystem::create_directories(dstEngineConfig.parent_path(), ec2);
                    std::filesystem::copy_file(srcEngineConfig, dstEngineConfig,
                        std::filesystem::copy_options::overwrite_existing, ec2);
                    if (!ec2)
                        buildPtr->appendBuildOutput("  Copied engine config: config/config.ini");
                }
            }
        }

        // Step 9: Generate game.ini with profile settings
        if (ok && !checkCancelled())
        {
            advanceStep("Generating game.ini...");

            const std::filesystem::path iniPath = std::filesystem::path(config.outputDir) / "game.ini";
            std::ofstream iniFile(iniPath);
            if (iniFile.is_open())
            {
                iniFile << "# Generated by HorizonEngine Build\n";
                iniFile << "StartLevel=" << config.startLevel << "\n";
                iniFile << "WindowTitle=" << config.windowTitle << "\n";
                iniFile << "BuildProfile=" << config.profile.name << "\n";
                iniFile << "LogLevel=" << config.profile.logLevel << "\n";
                iniFile << "EnableHotReload=" << (config.profile.enableHotReload ? "true" : "false") << "\n";
                iniFile << "EnableValidation=" << (config.profile.enableValidation ? "true" : "false") << "\n";
                iniFile << "EnableProfiler=" << (config.profile.enableProfiler ? "true" : "false") << "\n";

                // Persist all engine/renderer settings so the runtime
                // can restore them without needing config/config.ini
                iniFile << "\n# Engine Settings\n";
                auto allStates = DiagnosticsManager::Instance().getStates();
                for (const auto& [sKey, sVal] : allStates)
                {
                    // Skip editor-only keys that are not useful at runtime
                    if (sKey == "RHI" || sKey == "WindowWidth" || sKey == "WindowHeight"
                        || sKey == "WindowState" || sKey == "KnownProjects"
                        || sKey == "StartupMode")
                        continue;
                    iniFile << sKey << "=" << sVal << "\n";
                }

                iniFile.close();
                buildPtr->appendBuildOutput("  Written: " + iniPath.string());
            }
            else
            {
                ok = false;
                errorMsg = "Failed to write game.ini";
            }
        }

        // Step 10: Done
        advanceStep(ok ? "Build complete!" : "Build failed.");

        if (ok)
        {
            buildPtr->appendBuildOutput("Build completed successfully. Output: " + config.outputDir);

            // Launch if requested
            if (config.launchAfterBuild)
            {
                const std::filesystem::path exePath =
                    std::filesystem::path(config.outputDir) / (config.windowTitle + ".exe");
                if (std::filesystem::exists(exePath))
                {
                    buildPtr->appendBuildOutput("Launching: " + exePath.string());
#if defined(_WIN32)
                    ShellExecuteA(nullptr, "open", exePath.string().c_str(),
                        nullptr, config.outputDir.c_str(), SW_SHOWNORMAL);
#else
                    std::string cmd = "\"" + exePath.string() + "\" &";
                    std::system(cmd.c_str());
#endif
                }
            }
        }
        else
        {
            buildPtr->appendBuildOutput("Build failed: " + errorMsg);
        }

      }
      catch (const std::exception& e)
      {
          ok = false;
          errorMsg = std::string("Build crashed: ") + e.what();
          buildPtr->appendBuildOutput("[FATAL] " + errorMsg);
      }
      catch (...)
      {
          ok = false;
          errorMsg = "Build crashed: unknown exception";
          buildPtr->appendBuildOutput("[FATAL] " + errorMsg);
      }

      // Signal completion to the main thread
      {
          std::lock_guard<std::mutex> lock(buildPtr->m_buildMutex);
          buildPtr->m_buildPendingFinished = true;
          buildPtr->m_buildPendingSuccess = ok;
          buildPtr->m_buildPendingErrorMsg = errorMsg;
      }
    }); // end std::thread
}

#endif // ENGINE_EDITOR
