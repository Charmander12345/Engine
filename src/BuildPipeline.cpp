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

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
#include "Renderer/EditorTabs/BuildSystemUI.h"
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
        constexpr int kTotalSteps = 8;
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

              // Build the cmake configure command
              std::string configureCmd = "\"" + cmakePath + "\"";
              configureCmd += " -S \"" + engineSourceDir + "\"";
              configureCmd += " -B \"" + buildDir + "\"";
#if defined(CMAKE_GENERATOR)
              configureCmd += " -G \"" CMAKE_GENERATOR "\"";
#endif
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

          // Step 3: CMake build
          if (ok && !checkCancelled())
          {
              advanceStep("CMake build...");

              // Clean the deploy directory before building so that stale
              // DLLs from a previous build configuration (e.g. OpenAL32d.dll
              // left over from a Debug build) don't contaminate the output.
              {
                  std::error_code ec;
                  std::filesystem::remove_all(deployDir, ec);
              }

              std::string buildCmd = "\"" + cmakePath + "\"";
              buildCmd += " --build \"" + config.binaryDir + "\"";
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

              // Copy DLLs from the binary cache into Engine/ subdirectory.
              // The binary cache may contain editor-only DLLs from previous
              // builds (e.g. AssetManager.dll, Scripting.dll, Renderer.dll).
              // The runtime links against the *Runtime variants instead, so
              // we skip the editor-only DLLs to keep the game build clean.
              const std::filesystem::path engineDir = dstDir / "Engine";
              if (ok)
              {
                  std::error_code mkEc;
                  std::filesystem::create_directories(engineDir, mkEc);
                  const std::filesystem::path builtDir = builtExe.parent_path();

                  // Editor-only shared libraries that must NOT be deployed.
                  // The runtime target links *Runtime variants instead.
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

                  // Copy PDBs from binary cache into Symbols/ for non-Shipping
                  if (config.profile.name != "Shipping")
                  {
                      const auto symbolsDir = dstDir / "Symbols";
                      std::error_code mkEc;
                      std::filesystem::create_directories(symbolsDir, mkEc);
                      for (const auto& entry : std::filesystem::directory_iterator(builtDir))
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

        // Step 5: Cook assets + copy shaders + asset registry
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

            // Step 6: Package content into HPK archive
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

        // Step 7: Generate game.ini with profile settings
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

        // Step 8: Done
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
