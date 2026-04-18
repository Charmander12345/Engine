#pragma once
#if ENGINE_EDITOR

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>

class UIManager;
class Renderer;
class EditorWidget;
class PopupWindow;

/// Manages all Build-Game UI: profiles, build dialog, progress popup,
/// CMake/toolchain detection.  Extracted from UIManager (Section 1.4).
class BuildSystemUI
{
public:
    BuildSystemUI(UIManager* uiManager, Renderer* renderer);
    ~BuildSystemUI();

    void setRenderer(Renderer* renderer) { m_renderer = renderer; }

    // ── Build Profiles ───────────────────────────────────────────────────
    struct BuildProfile
    {
        std::string name            = "Development";
        std::string cmakeBuildType  = "RelWithDebInfo";
        std::string logLevel        = "info";
        bool enableHotReload        = true;
        bool enableValidation       = false;
        bool enableProfiler         = true;
        bool compressAssets         = false;
    };

    void loadBuildProfiles();
    void saveBuildProfile(const BuildProfile& profile);
    void deleteBuildProfile(const std::string& name);
    const std::vector<BuildProfile>& getBuildProfiles() const { return m_buildProfiles; }

    // ── Build Game ───────────────────────────────────────────────────────
    struct BuildGameConfig
    {
        std::string  startLevel;
        std::string  windowTitle   = "Game";
        std::string  outputDir;
        std::string  binaryDir;
        BuildProfile profile;
        bool         launchAfterBuild = true;
        bool         cleanBuild       = false;
    };

    using BuildGameCallback = std::function<void(const BuildGameConfig& config)>;
    void setOnBuildGame(BuildGameCallback cb) { m_onBuildGame = std::move(cb); }

    void openBuildGameDialog();

    // Build progress popup
    void showBuildProgress();
    void updateBuildProgress(const std::string& status, int step, int totalSteps);
    void closeBuildProgress(bool success, const std::string& message = {});
    void dismissBuildProgress();

    // Thread-safe: append a line to the build output log
    void appendBuildOutput(const std::string& line);
    // Main-thread: poll the build thread for pending UI updates
    void pollBuildThread();
    bool isBuildRunning() const { return m_buildRunning.load(); }

    // ── CMake Detection ──────────────────────────────────────────────────
    bool detectCMake();
    bool isCMakeAvailable() const { return m_cmakeAvailable.load(); }
    const std::string& getCMakePath() const { return m_cmakePath; }
    void showCMakeInstallPrompt();

    // ── Build Toolchain Detection ────────────────────────────────────────
    struct ToolchainInfo
    {
        std::string name;
        std::string version;
        std::string compilerPath;
        std::string vsInstallPath;
    };
    bool detectBuildToolchain();
    bool isBuildToolchainAvailable() const { return m_toolchainAvailable.load(); }
    const ToolchainInfo& getBuildToolchain() const { return m_toolchainInfo; }
    void showToolchainInstallPrompt();

    // ── Async Detection ──────────────────────────────────────────────────
    void startAsyncToolchainDetection();
    void pollToolchainDetection();

    // ── Auto-Install (runs tools/bootstrap.ps1 on a background thread) ──
    void promptAndInstallTools();
    bool isToolInstallRunning() const { return m_toolInstallRunning.load(); }
    void pollToolInstall();

    // Public build thread state (accessed from build lambda in main.cpp)
    std::thread m_buildThread;
    std::atomic<bool> m_buildRunning{ false };
    std::atomic<bool> m_buildCancelRequested{ false };
    std::mutex m_buildMutex;
    std::vector<std::string> m_buildPendingLines;
    std::string m_buildPendingStatus;
    int m_buildPendingStep{ 0 };
    int m_buildPendingTotalSteps{ 0 };
    bool m_buildPendingStepDirty{ false };
    bool m_buildPendingFinished{ false };
    bool m_buildPendingSuccess{ false };
    std::string m_buildPendingErrorMsg;

private:
    UIManager* m_uiManager{ nullptr };
    Renderer*  m_renderer{ nullptr };

    BuildGameCallback m_onBuildGame;
    std::shared_ptr<EditorWidget> m_buildProgressWidget;
    PopupWindow* m_buildPopup{ nullptr };
    std::vector<BuildProfile> m_buildProfiles;

    std::vector<std::string> m_buildOutputLines;

    // CMake state
    std::atomic<bool> m_cmakeAvailable{ false };
    std::string m_cmakePath;

    // Build toolchain state
    std::atomic<bool> m_toolchainAvailable{ false };
    ToolchainInfo m_toolchainInfo;

    // Async detection state
    std::atomic<bool> m_toolDetectDone{ false };
    bool m_toolDetectPolled{ false };

    // Auto-install state
    std::atomic<bool> m_toolInstallRunning{ false };
    std::atomic<bool> m_toolInstallDone{ false };
    std::atomic<bool> m_toolInstallSuccess{ false };
    std::string       m_toolInstallError;
    std::thread       m_toolInstallThread;
    bool              m_toolInstallPolled{ false };

    void runBootstrapInstall();
};

#endif // ENGINE_EDITOR
