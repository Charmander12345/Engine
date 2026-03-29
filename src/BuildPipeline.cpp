#include "BuildPipeline.h"
#if ENGINE_EDITOR

#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <array>
#include <string>
#include <cstdio>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#endif

#include "Renderer/Renderer.h"
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
        uiMgr.showToastMessage("A build is already in progress.", 3.0f,
            UIManager::NotificationLevel::Warning);
        return;
    }

    // Verify CMake and toolchain are available
    if (!uiMgr.isCMakeAvailable())
    {
        uiMgr.showToastMessage("CMake is required to build the game.\nPlease install CMake and restart the editor.", 5.0f,
            UIManager::NotificationLevel::Error);
        return;
    }
    if (!uiMgr.isBuildToolchainAvailable())
    {
        uiMgr.showToastMessage("C++ toolchain is required to build the game.\nPlease install Visual Studio with C++ workload.", 5.0f,
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

    UIManager* uiPtr = &uiMgr;

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

    uiMgr.m_buildRunning.store(true);

    if (uiMgr.m_buildThread.joinable())
        uiMgr.m_buildThread.join();

    uiMgr.m_buildThread = std::thread([uiPtr, config, cmakePath, engineSourceDir, toolchainName, toolchainVersion, editorBaseDir, projectPath]()
    {
        constexpr int kTotalSteps = 8;
        int step = 0;
        bool ok = true;
        std::string errorMsg;

        // Log build environment info
        uiPtr->appendBuildOutput("Profile: " + config.profile.name + " (" + config.profile.cmakeBuildType + ")");
        uiPtr->appendBuildOutput("CMake: " + cmakePath);
        uiPtr->appendBuildOutput("Toolchain: " + toolchainName + " " + toolchainVersion);
        uiPtr->appendBuildOutput("Engine Source: " + engineSourceDir);
        uiPtr->appendBuildOutput("Binary Cache: " + config.binaryDir);
        uiPtr->appendBuildOutput("");

            // Thread-safe helper to push step progress
            auto advanceStep = [&](const std::string& status)
            {
                ++step;
                uiPtr->appendBuildOutput("[Step " + std::to_string(step) + "/" + std::to_string(kTotalSteps) + "] " + status);
                {
                    std::lock_guard<std::mutex> lock(uiPtr->m_buildMutex);
                    uiPtr->m_buildPendingStatus = status;
                    uiPtr->m_buildPendingStep = step;
                    uiPtr->m_buildPendingTotalSteps = kTotalSteps;
                    uiPtr->m_buildPendingStepDirty = true;
                }
            };

            // Check if build was cancelled – sets ok=false and returns true
            auto checkCancelled = [&]() -> bool
            {
                if (uiPtr->m_buildCancelRequested.load())
                {
                    ok = false;
                    errorMsg = "Build cancelled by user.";
                    uiPtr->appendBuildOutput("[INFO] Build cancelled.");
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
                      uiPtr->appendBuildOutput("  Cleaned output directory.");
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
              uiPtr->appendBuildOutput("> " + cmd);

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
                  uiPtr->appendBuildOutput("  [ERROR] Failed to launch process.");
                  return -1;
              }

              // Read output line by line
              char buf[4096];
              DWORD bytesRead = 0;
              std::string lineBuffer;
              while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
              {
                  if (uiPtr->m_buildCancelRequested.load())
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
                      uiPtr->appendBuildOutput("  " + line);
                      lineBuffer.erase(0, pos + 1);
                  }
              }
              if (!lineBuffer.empty())
                  uiPtr->appendBuildOutput("  " + lineBuffer);

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
              uiPtr->appendBuildOutput("> " + cmd);
              std::string fullCmd = cmd + " 2>&1";
              FILE* pipe = popen(fullCmd.c_str(), "r");
              if (!pipe) return -1;
              char buf[4096];
              while (fgets(buf, sizeof(buf), pipe))
              {
                  if (uiPtr->m_buildCancelRequested.load()) break;
                  std::string line = buf;
                  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                      line.pop_back();
                  uiPtr->appendBuildOutput("  " + line);
              }
              return pclose(pipe);
          };
#endif

          // Step 2: CMake configure
          if (ok && !checkCancelled())
          {
              advanceStep("CMake configure...");

              const std::string buildDir = config.binaryDir;

              // Optionally clean the binary cache
              if (config.cleanBuild)
              {
                  uiPtr->appendBuildOutput("  Clean build: removing " + buildDir);
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

              // Find the built runtime exe in the binary cache
              // Multi-config generators put it under <config>/ subdir
              std::filesystem::path builtExe;
              {
                  auto tryPath = std::filesystem::path(config.binaryDir) / config.profile.cmakeBuildType / "HorizonEngineRuntime.exe";
                  if (std::filesystem::exists(tryPath))
                      builtExe = tryPath;
                  else
                  {
                      // Single-config generators put it directly in the build dir
                      tryPath = std::filesystem::path(config.binaryDir) / "HorizonEngineRuntime.exe";
                      if (std::filesystem::exists(tryPath))
                          builtExe = tryPath;
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
                          uiPtr->appendBuildOutput("  Copied runtime: " + builtExe.string());
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
                              uiPtr->appendBuildOutput("  Copied PDB: Symbols/" + config.windowTitle + ".pdb");
                      }
                  }
              }

              // Copy DLLs from the binary cache (Runtime DLLs built alongside the exe)
              if (ok)
              {
                  const std::filesystem::path builtDir = builtExe.parent_path();
                  for (const auto& entry : std::filesystem::directory_iterator(builtDir))
                  {
                      if (!entry.is_regular_file()) continue;
                      const auto ext = entry.path().extension().string();
                      if (ext != ".dll" && ext != ".DLL") continue;

                      std::error_code copyEc;
                      std::filesystem::copy_file(entry.path(),
                          dstDir / entry.path().filename(),
                          std::filesystem::copy_options::overwrite_existing, copyEc);
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

              // Copy remaining DLLs from editor directory (e.g. Python, plugins)
              if (ok)
              {
                  // DLLs already present in the output (from binary cache) are skipped
                  for (const auto& entry : std::filesystem::directory_iterator(editorDir))
                  {
                      if (!entry.is_regular_file()) continue;
                      const auto ext = entry.path().extension().string();
                      if (ext != ".dll" && ext != ".DLL") continue;

                      const auto dst = dstDir / entry.path().filename();
                      if (std::filesystem::exists(dst)) continue; // already deployed from binary cache

                      // Skip editor-only DLLs that the runtime does not need
                      const std::string fname = entry.path().filename().string();
                      static const std::array<std::string, 4> editorOnlyDlls = {
                          "AssetManager.dll", "Scripting.dll", "Renderer.dll", "Landscape.dll"
                      };
                      bool isEditorOnly = false;
                      for (const auto& edDll : editorOnlyDlls)
                      {
                          if (_stricmp(fname.c_str(), edDll.c_str()) == 0)
                          {
                              isEditorOnly = true;
                              break;
                          }
                      }
                      if (isEditorOnly) continue;

                      std::error_code copyEc;
                      std::filesystem::copy_file(entry.path(), dst,
                          std::filesystem::copy_options::overwrite_existing, copyEc);
                  }
              }

              // Copy Python runtime (DLL + zip) from editor directory
              if (ok)
              {
                  for (const auto& entry : std::filesystem::directory_iterator(editorDir))
                  {
                      if (!entry.is_regular_file()) continue;
                      const auto fname = entry.path().filename().string();
                      const auto fext = entry.path().extension().string();
                      if ((fext == ".dll" || fext == ".zip") && fname.size() >= 8 && fname.substr(0, 6) == "python")
                      {
                          const auto dst = dstDir / entry.path().filename();
                          if (!std::filesystem::exists(dst))
                          {
                              std::error_code copyEc;
                              std::filesystem::copy_file(entry.path(), dst,
                                  std::filesystem::copy_options::overwrite_existing, copyEc);
                              if (!copyEc)
                                  uiPtr->appendBuildOutput("  Copied Python file: " + fname);
                          }
                      }
                  }
              }

              // Copy CrashHandler from editor's Tools/ directory
              if (ok)
              {
                  const auto srcCrashHandler = editorDir / "Tools" / "CrashHandler.exe";
                  if (std::filesystem::exists(srcCrashHandler))
                  {
                      const auto dstTools = dstDir / "Tools";
                      std::error_code ec;
                      std::filesystem::create_directories(dstTools, ec);
                      std::filesystem::copy_file(srcCrashHandler, dstTools / "CrashHandler.exe",
                          std::filesystem::copy_options::overwrite_existing, ec);
                      if (!ec)
                          uiPtr->appendBuildOutput("  Copied CrashHandler.exe");

                      // Also copy CrashHandler PDB for non-Shipping
                      if (config.profile.name != "Shipping")
                      {
                          const auto srcPdb = editorDir / "Tools" / "CrashHandler.pdb";
                          if (std::filesystem::exists(srcPdb))
                          {
                              std::error_code ec2;
                              std::filesystem::copy_file(srcPdb, dstTools / "CrashHandler.pdb",
                                  std::filesystem::copy_options::overwrite_existing, ec2);
                          }
                      }
                  }
                  else
                  {
                      uiPtr->appendBuildOutput("  [WARN] CrashHandler.exe not found at: " + srcCrashHandler.string());
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
                        uiPtr->appendBuildOutput("  [" + std::to_string(done) + "/" + std::to_string(total) + "] " + current);
                    },
                    &uiPtr->m_buildCancelRequested);

                uiPtr->appendBuildOutput("Asset cooking complete: "
                    + std::to_string(result.cookedAssets) + " cooked, "
                    + std::to_string(result.skippedAssets) + " skipped, "
                    + std::to_string(result.failedAssets) + " failed ("
                    + std::to_string(static_cast<int>(result.elapsedSeconds * 1000)) + " ms)");

                if (!result.success)
                {
                    ok = false;
                    errorMsg = "Asset cooking failed.";
                    for (const auto& e : result.errors)
                        uiPtr->appendBuildOutput("  [ERROR] " + e);
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
                    uiPtr->appendBuildOutput("  Copied project config: defaults.ini");
                }
            }

            }

            // Step 6: Package content into HPK archive
            if (ok && !checkCancelled())
            {
                advanceStep("Packaging content into HPK...");

            const std::filesystem::path dstDir = config.outputDir;
            const std::filesystem::path hpkPath = dstDir / "content.hpk";

            HPKWriter writer;
            if (!writer.begin(hpkPath.string()))
            {
                ok = false;
                errorMsg = "Failed to create HPK archive: " + hpkPath.string();
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
                        uiPtr->appendBuildOutput("  [WARN] Failed to pack: " + vpath);
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
                    uiPtr->appendBuildOutput("  HPK archive: " + hpkPath.string()
                        + " (" + std::to_string(writer.getFileCount()) + " files)");

                    // Remove loose directories now that they're packed
                    std::error_code ec;
                    std::filesystem::remove_all(dstDir / "Content", ec);
                    std::filesystem::remove_all(dstDir / "shaders", ec);
                    std::filesystem::remove_all(dstDir / "Config", ec);
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
                        uiPtr->appendBuildOutput("  Copied engine config: config/config.ini");
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
                uiPtr->appendBuildOutput("  Written: " + iniPath.string());
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
            uiPtr->appendBuildOutput("Build completed successfully. Output: " + config.outputDir);

            // Launch if requested
            if (config.launchAfterBuild)
            {
                const std::filesystem::path exePath =
                    std::filesystem::path(config.outputDir) / (config.windowTitle + ".exe");
                if (std::filesystem::exists(exePath))
                {
                    uiPtr->appendBuildOutput("Launching: " + exePath.string());
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
            uiPtr->appendBuildOutput("Build failed: " + errorMsg);
        }

      }
      catch (const std::exception& e)
      {
          ok = false;
          errorMsg = std::string("Build crashed: ") + e.what();
          uiPtr->appendBuildOutput("[FATAL] " + errorMsg);
      }
      catch (...)
      {
          ok = false;
          errorMsg = "Build crashed: unknown exception";
          uiPtr->appendBuildOutput("[FATAL] " + errorMsg);
      }

      // Signal completion to the main thread
      {
          std::lock_guard<std::mutex> lock(uiPtr->m_buildMutex);
          uiPtr->m_buildPendingFinished = true;
          uiPtr->m_buildPendingSuccess = ok;
          uiPtr->m_buildPendingErrorMsg = errorMsg;
      }
    }); // end std::thread
}

#endif // ENGINE_EDITOR
