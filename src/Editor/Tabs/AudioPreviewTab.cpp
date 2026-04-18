#include "AudioPreviewTab.h"
#include "../../Renderer/UIManager.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/EditorTheme.h"
#include "../../Renderer/EditorUIBuilder.h"
#include "../../Renderer/EditorUI/EditorWidget.h"
#include "../../AssetManager/AssetManager.h"
#include "../../Core/AudioManager.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Logger/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <sstream>

// ───────────────────────────────────────────────────────────────────────────
AudioPreviewTab::AudioPreviewTab(UIManager* uiManager, Renderer* renderer)
    : m_ui(uiManager)
    , m_renderer(renderer)
{}

// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::open()
{
    // Default open with no asset – no-op (use open(assetPath) instead)
}

// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::open(const std::string& assetPath)
{
    if (!m_renderer)
        return;

    const std::string tabId = "AudioPreview";

    // If already open with the same asset, just switch to it
    if (m_state.isOpen && m_state.assetPath == assetPath)
    {
        m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
        return;
    }

    // If open with a different asset, close first
    if (m_state.isOpen)
        close();

    // --- Resolve and load the asset ---
    auto& assetMgr = AssetManager::Instance();
    const std::string absPath = assetMgr.getAbsoluteContentPath(assetPath);
    if (absPath.empty())
    {
        m_ui->showToastMessage("Failed to resolve audio asset.", UIManager::kToastMedium, UIManager::NotificationLevel::Error);
        return;
    }

    auto existingAsset = assetMgr.getLoadedAssetByPath(assetPath);
    if (!existingAsset)
    {
        const int loadId = assetMgr.loadAsset(absPath, AssetType::Audio, AssetManager::Sync);
        if (loadId == 0)
        {
            m_ui->showToastMessage("Failed to load audio asset.", UIManager::kToastMedium, UIManager::NotificationLevel::Error);
            return;
        }
        existingAsset = assetMgr.getLoadedAssetByPath(assetPath);
    }

    if (!existingAsset)
    {
        m_ui->showToastMessage("Audio asset not found in memory.", UIManager::kToastMedium, UIManager::NotificationLevel::Error);
        return;
    }

    const auto& data = existingAsset->getData();

    // --- Create tab ---
    m_renderer->addTab(tabId, std::filesystem::path(assetPath).stem().string(), true);
    m_renderer->setActiveTab(tabId);

    const std::string widgetId = "AudioPreview.Main";
    m_ui->unregisterWidget(widgetId);

    // --- Initialise state ---
    m_state = {};
    m_state.tabId       = tabId;
    m_state.widgetId    = widgetId;
    m_state.assetPath   = assetPath;
    m_state.isOpen      = true;
    m_state.volume      = 1.0f;
    m_state.displayName = std::filesystem::path(assetPath).stem().string();

    // Extract metadata
    if (data.contains("m_channels") && data["m_channels"].is_number())
        m_state.channels = data["m_channels"].get<int>();
    if (data.contains("m_sampleRate") && data["m_sampleRate"].is_number())
        m_state.sampleRate = data["m_sampleRate"].get<int>();
    if (data.contains("m_format") && data["m_format"].is_number())
        m_state.format = data["m_format"].get<int>();
    if (data.contains("m_data") && data["m_data"].is_array())
        m_state.dataBytes = data["m_data"].size();

    // Compute duration
    if (m_state.sampleRate > 0 && m_state.channels > 0)
    {
        int bytesPerSample = 2; // default: 16-bit
        // SDL_AUDIO_U8 = 0x0008
        if (m_state.format == 0x0008)
            bytesPerSample = 1;
        const size_t frameSize = static_cast<size_t>(m_state.channels) * bytesPerSample;
        if (frameSize > 0)
            m_state.durationSeconds = static_cast<float>(m_state.dataBytes) / static_cast<float>(frameSize * m_state.sampleRate);
    }

    // Extract spatial audio settings
    if (data.contains("m_is3D") && data["m_is3D"].is_boolean())
        m_state.is3D = data["m_is3D"].get<bool>();
    if (data.contains("m_minDistance") && data["m_minDistance"].is_number())
        m_state.minDistance = data["m_minDistance"].get<float>();
    if (data.contains("m_maxDistance") && data["m_maxDistance"].is_number())
        m_state.maxDistance = data["m_maxDistance"].get<float>();
    if (data.contains("m_rolloffFactor") && data["m_rolloffFactor"].is_number())
        m_state.rolloffFactor = data["m_rolloffFactor"].get<float>();

    // --- Build the widget ---
    {
        auto widget = std::make_shared<EditorWidget>();
        widget->setName(widgetId);
        widget->setAnchor(WidgetAnchor::TopLeft);
        widget->setFillX(true);
        widget->setFillY(true);
        widget->setSizePixels(Vec2{ 0.0f, 0.0f });
        widget->setZOrder(2);

        const auto& theme = EditorTheme::Get();

        WidgetElement root{};
        root.id          = "AudioPreview.Root";
        root.type        = WidgetElementType::StackPanel;
        root.from        = Vec2{ 0.0f, 0.0f };
        root.to          = Vec2{ 1.0f, 1.0f };
        root.fillX       = true;
        root.fillY       = true;
        root.orientation = StackOrientation::Vertical;
        root.style.color = theme.panelBackground;
        root.runtimeOnly = true;

        // Toolbar
        buildToolbar(root);

        // Separator
        {
            WidgetElement sep{};
            sep.type        = WidgetElementType::Panel;
            sep.fillX       = true;
            sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
            sep.style.color = theme.panelBorder;
            sep.runtimeOnly = true;
            root.children.push_back(std::move(sep));
        }

        // Scrollable content area
        {
            WidgetElement content{};
            content.id          = "AudioPreview.Content";
            content.type        = WidgetElementType::StackPanel;
            content.fillX       = true;
            content.fillY       = true;
            content.scrollable  = true;
            content.orientation = StackOrientation::Vertical;
            content.padding     = EditorTheme::Scaled(Vec2{ 16.0f, 12.0f });
            content.style.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
            content.runtimeOnly = true;

            // Waveform
            buildWaveform(content);

            // Metadata
            buildMetadata(content);

            root.children.push_back(std::move(content));
        }

        widget->setElements({ std::move(root) });
        m_ui->registerWidget(widgetId, widget, tabId);
    }

    // --- Tab / close click events ---
    m_ui->registerClickEvent("TitleBar.Tab." + tabId, [this, tabId]()
    {
        if (m_renderer)
            m_renderer->setActiveTab(tabId);
        m_ui->markAllWidgetsDirty();
    });

    m_ui->registerClickEvent("TitleBar.TabClose." + tabId, [this]()
    {
        close();
    });

    // --- Playback button events ---
    m_ui->registerClickEvent("AudioPreview.Play", [this]()
    {
        if (m_state.isPlaying)
            return;
        auto& audioMgr = AudioManager::Instance();
        auto& assetMgr = AssetManager::Instance();
        auto asset = assetMgr.getLoadedAssetByPath(m_state.assetPath);
        if (asset)
        {
            unsigned int handle = audioMgr.playAudioAsset(asset->getId(), false, m_state.volume);
            if (handle != 0)
            {
                m_state.playHandle = handle;
                m_state.isPlaying = true;
                refresh();
            }
        }
    });

    m_ui->registerClickEvent("AudioPreview.Stop", [this]()
    {
        if (m_state.playHandle != 0)
        {
            AudioManager::Instance().stopSource(m_state.playHandle);
            m_state.playHandle = 0;
        }
        m_state.isPlaying = false;
        refresh();
    });

    refresh();

    Logger::Instance().log(Logger::Category::UI,
        "Audio Preview opened: " + assetPath, Logger::LogLevel::INFO);
}

// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::close()
{
    if (!m_state.isOpen || !m_renderer)
        return;

    // Stop any playing audio
    if (m_state.playHandle != 0)
    {
        AudioManager::Instance().stopSource(m_state.playHandle);
    }

    const std::string tabId = m_state.tabId;

    if (m_renderer->getActiveTabId() == tabId)
        m_renderer->setActiveTab("Viewport");

    m_ui->unregisterWidget(m_state.widgetId);

    m_renderer->removeTab(tabId);
    m_state = {};
    m_ui->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
// refresh – rebuilds content of the widget
// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::refresh()
{
    if (!m_state.isOpen)
        return;

    auto* entry = m_ui->findWidgetEntry(m_state.widgetId);
    if (!entry || !entry->widget)
        return;

    auto& elements = entry->widget->getElementsMutable();
    if (elements.empty())
        return;

    auto& root = elements[0];
    root.children.clear();

    const auto& theme = EditorTheme::Get();
    root.style.color = theme.panelBackground;

    // Toolbar
    buildToolbar(root);

    // Separator
    {
        WidgetElement sep{};
        sep.type        = WidgetElementType::Panel;
        sep.fillX       = true;
        sep.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        sep.style.color = theme.panelBorder;
        sep.runtimeOnly = true;
        root.children.push_back(std::move(sep));
    }

    // Content area
    {
        WidgetElement content{};
        content.id          = "AudioPreview.Content";
        content.type        = WidgetElementType::StackPanel;
        content.fillX       = true;
        content.fillY       = true;
        content.scrollable  = true;
        content.orientation = StackOrientation::Vertical;
        content.padding     = EditorTheme::Scaled(Vec2{ 16.0f, 12.0f });
        content.style.color = Vec4{ 0.10f, 0.10f, 0.13f, 1.0f };
        content.runtimeOnly = true;

        buildWaveform(content);
        buildMetadata(content);

        root.children.push_back(std::move(content));
    }

    entry->widget->markLayoutDirty();
    m_ui->markAllWidgetsDirty();
}

// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::buildToolbar(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    WidgetElement toolbar{};
    toolbar.id          = "AudioPreview.Toolbar";
    toolbar.type        = WidgetElementType::StackPanel;
    toolbar.fillX       = true;
    toolbar.orientation = StackOrientation::Horizontal;
    toolbar.padding     = EditorTheme::Scaled(Vec2{ 8.0f, 4.0f });
    toolbar.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 36.0f });
    toolbar.style.color = Vec4{ 0.14f, 0.15f, 0.19f, 1.0f };
    toolbar.runtimeOnly = true;

    auto makeToolBtn = [&](const std::string& id, const std::string& label, bool active) -> WidgetElement
    {
        WidgetElement btn{};
        btn.id              = id;
        btn.type            = WidgetElementType::Button;
        btn.text            = label;
        btn.font            = theme.fontDefault;
        btn.fontSize        = theme.fontSizeSmall;
        btn.style.textColor = active ? theme.textPrimary : theme.textMuted;
        btn.style.color     = active ? theme.accent : theme.buttonDefault;
        btn.style.hoverColor = theme.buttonHover;
        btn.textAlignH      = TextAlignH::Center;
        btn.textAlignV      = TextAlignV::Center;
        btn.minSize         = EditorTheme::Scaled(Vec2{ 60.0f, 26.0f });
        btn.padding         = EditorTheme::Scaled(Vec2{ 10.0f, 2.0f });
        btn.hitTestMode     = HitTestMode::Enabled;
        btn.runtimeOnly     = true;
        btn.clickEvent      = id;
        return btn;
    };

    // Play / Stop buttons
    toolbar.children.push_back(makeToolBtn("AudioPreview.Play", m_state.isPlaying ? "Playing..." : "Play", !m_state.isPlaying));
    toolbar.children.push_back(makeToolBtn("AudioPreview.Stop", "Stop", m_state.isPlaying));

    // Spacer
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 1.0f });
        spacer.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
        spacer.runtimeOnly = true;
        toolbar.children.push_back(std::move(spacer));
    }

    // Volume label
    {
        WidgetElement volLabel{};
        volLabel.type          = WidgetElementType::Text;
        volLabel.text          = "Volume";
        volLabel.font          = theme.fontDefault;
        volLabel.fontSize      = theme.fontSizeSmall;
        volLabel.style.textColor = theme.textSecondary;
        volLabel.minSize       = EditorTheme::Scaled(Vec2{ 50.0f, 26.0f });
        volLabel.textAlignV    = TextAlignV::Center;
        volLabel.runtimeOnly   = true;
        toolbar.children.push_back(std::move(volLabel));
    }

    // Volume slider
    {
        WidgetElement slider = EditorUIBuilder::makeSliderRow(
            "AudioPreview.Volume", "",
            m_state.volume, 0.0f, 1.0f,
            [this](float v)
            {
                m_state.volume = v;
                if (m_state.playHandle != 0)
                    AudioManager::Instance().setHandleGain(m_state.playHandle, v);
            });
        slider.minSize = EditorTheme::Scaled(Vec2{ 140.0f, 26.0f });
        toolbar.children.push_back(std::move(slider));
    }

    // Asset name label (right side)
    {
        WidgetElement nameLabel{};
        nameLabel.type          = WidgetElementType::Text;
        nameLabel.text          = m_state.displayName;
        nameLabel.font          = theme.fontDefault;
        nameLabel.fontSize      = theme.fontSizeSmall;
        nameLabel.style.textColor = theme.textMuted;
        nameLabel.minSize       = EditorTheme::Scaled(Vec2{ 80.0f, 26.0f });
        nameLabel.textAlignV    = TextAlignV::Center;
        nameLabel.textAlignH    = TextAlignH::Right;
        nameLabel.runtimeOnly   = true;
        nameLabel.padding       = EditorTheme::Scaled(Vec2{ 8.0f, 0.0f });
        toolbar.children.push_back(std::move(nameLabel));
    }

    root.children.push_back(std::move(toolbar));
}

// ───────────────────────────────────────────────────────────────────────────
// buildWaveform – simple bar-chart visualisation of audio samples
// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::buildWaveform(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();

    // Section heading
    {
        WidgetElement heading = EditorUIBuilder::makeHeading("Waveform");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    // Waveform container
    WidgetElement waveContainer{};
    waveContainer.id          = "AudioPreview.Waveform";
    waveContainer.type        = WidgetElementType::StackPanel;
    waveContainer.fillX       = true;
    waveContainer.orientation = StackOrientation::Horizontal;
    waveContainer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 100.0f });
    waveContainer.style.color = Vec4{ 0.06f, 0.07f, 0.09f, 1.0f };
    waveContainer.padding     = EditorTheme::Scaled(Vec2{ 4.0f, 4.0f });
    waveContainer.runtimeOnly = true;

    // Try to read raw sample data for waveform visualisation
    auto& assetMgr = AssetManager::Instance();
    auto asset = assetMgr.getLoadedAssetByPath(m_state.assetPath);

    const int numBars = 80;
    std::vector<float> barHeights(numBars, 0.0f);

    if (asset)
    {
        const auto& data = asset->getData();
        if (data.contains("m_data") && data["m_data"].is_array())
        {
            const auto& rawArr = data["m_data"];
            const size_t totalBytes = rawArr.size();

            const bool is8bit = (m_state.format == 0x0008);
            const int bytesPerSample = is8bit ? 1 : 2;
            const int frameSize = m_state.channels * bytesPerSample;
            if (frameSize > 0 && totalBytes > 0)
            {
                const size_t totalFrames = totalBytes / frameSize;
                const size_t framesPerBar = (totalFrames > 0) ? std::max<size_t>(1, totalFrames / numBars) : 1;

                for (int bar = 0; bar < numBars; ++bar)
                {
                    const size_t startFrame = bar * framesPerBar;
                    const size_t endFrame = std::min<size_t>(startFrame + framesPerBar, totalFrames);
                    float maxAmp = 0.0f;

                    // Sample a subset of frames to avoid reading millions of JSON elements
                    const size_t step = std::max<size_t>(1, (endFrame - startFrame) / 32);
                    for (size_t f = startFrame; f < endFrame; f += step)
                    {
                        const size_t byteOffset = f * frameSize;
                        if (is8bit)
                        {
                            if (byteOffset < totalBytes)
                            {
                                float sample = static_cast<float>(rawArr[byteOffset].get<int>()) / 255.0f;
                                sample = std::abs(sample - 0.5f) * 2.0f;
                                maxAmp = std::max(maxAmp, sample);
                            }
                        }
                        else
                        {
                            if (byteOffset + 1 < totalBytes)
                            {
                                int lo = rawArr[byteOffset].get<int>() & 0xFF;
                                int hi = rawArr[byteOffset + 1].get<int>() & 0xFF;
                                int16_t s16 = static_cast<int16_t>(lo | (hi << 8));
                                float sample = std::abs(static_cast<float>(s16)) / 32768.0f;
                                maxAmp = std::max(maxAmp, sample);
                            }
                        }
                    }
                    barHeights[bar] = maxAmp;
                }
            }
        }
    }

    // Build bars
    const float maxBarH = EditorTheme::Scaled(90.0f);
    const Vec4 barColor = Vec4{ 0.30f, 0.60f, 1.00f, 0.85f };
    const Vec4 barBg    = Vec4{ 0.12f, 0.13f, 0.16f, 1.0f };

    for (int i = 0; i < numBars; ++i)
    {
        WidgetElement barCol{};
        barCol.type        = WidgetElementType::StackPanel;
        barCol.orientation = StackOrientation::Vertical;
        barCol.fillX       = true;
        barCol.fillY       = true;
        barCol.runtimeOnly = true;

        float h = barHeights[i] * maxBarH;
        float emptyH = maxBarH - h;

        // Top empty space
        {
            WidgetElement empty{};
            empty.type        = WidgetElementType::Panel;
            empty.fillX       = true;
            empty.minSize     = Vec2{ 0.0f, std::max(emptyH, 1.0f) };
            empty.style.color = Vec4{ 0, 0, 0, 0 };
            empty.runtimeOnly = true;
            barCol.children.push_back(std::move(empty));
        }

        // Bar
        {
            WidgetElement bar{};
            bar.type        = WidgetElementType::Panel;
            bar.fillX       = true;
            bar.minSize     = Vec2{ 0.0f, std::max(h, 1.0f) };
            bar.style.color = barColor;
            bar.runtimeOnly = true;
            barCol.children.push_back(std::move(bar));
        }

        waveContainer.children.push_back(std::move(barCol));
    }

    root.children.push_back(std::move(waveContainer));
}

// ───────────────────────────────────────────────────────────────────────────
void AudioPreviewTab::buildMetadata(WidgetElement& root)
{
    const auto& theme = EditorTheme::Get();
    const auto& st = m_state;

    // Spacer
    {
        WidgetElement sp{};
        sp.type        = WidgetElementType::Panel;
        sp.fillX       = true;
        sp.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 10.0f });
        sp.style.color = Vec4{ 0, 0, 0, 0 };
        sp.runtimeOnly = true;
        root.children.push_back(std::move(sp));
    }

    // Section heading
    {
        WidgetElement heading = EditorUIBuilder::makeHeading("Metadata");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    auto addInfoRow = [&](const std::string& label, const std::string& value)
    {
        WidgetElement row{};
        row.type        = WidgetElementType::StackPanel;
        row.orientation = StackOrientation::Horizontal;
        row.fillX       = true;
        row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 22.0f });
        row.runtimeOnly = true;

        WidgetElement lbl{};
        lbl.type          = WidgetElementType::Text;
        lbl.text          = label;
        lbl.font          = theme.fontDefault;
        lbl.fontSize      = theme.fontSizeBody;
        lbl.style.textColor = theme.textSecondary;
        lbl.minSize       = EditorTheme::Scaled(Vec2{ 120.0f, 20.0f });
        lbl.textAlignV    = TextAlignV::Center;
        lbl.runtimeOnly   = true;
        row.children.push_back(std::move(lbl));

        WidgetElement val{};
        val.type          = WidgetElementType::Text;
        val.text          = value;
        val.font          = theme.fontDefault;
        val.fontSize      = theme.fontSizeBody;
        val.style.textColor = theme.textPrimary;
        val.fillX         = true;
        val.minSize       = EditorTheme::Scaled(Vec2{ 0.0f, 20.0f });
        val.textAlignV    = TextAlignV::Center;
        val.runtimeOnly   = true;
        row.children.push_back(std::move(val));

        root.children.push_back(std::move(row));
    };

    addInfoRow("Path", st.assetPath);
    addInfoRow("Channels", std::to_string(st.channels) + (st.channels == 1 ? " (Mono)" : st.channels == 2 ? " (Stereo)" : ""));
    addInfoRow("Sample Rate", std::to_string(st.sampleRate) + " Hz");

    // Format string
    {
        std::string fmtStr = "Unknown";
        if (st.format == 0x0008) fmtStr = "8-bit Unsigned";
        else if (st.format != 0) fmtStr = "16-bit Signed";
        addInfoRow("Format", fmtStr);
    }

    // Duration
    {
        int totalSec = static_cast<int>(st.durationSeconds);
        int minutes = totalSec / 60;
        int seconds = totalSec % 60;
        int millis = static_cast<int>((st.durationSeconds - totalSec) * 1000);
        char durBuf[32];
        std::snprintf(durBuf, sizeof(durBuf), "%d:%02d.%03d", minutes, seconds, millis);
        addInfoRow("Duration", durBuf);
    }

    // Data size
    {
        std::string sizeStr;
        if (st.dataBytes >= 1024 * 1024)
            sizeStr = std::to_string(st.dataBytes / (1024 * 1024)) + " MB";
        else if (st.dataBytes >= 1024)
            sizeStr = std::to_string(st.dataBytes / 1024) + " KB";
        else
            sizeStr = std::to_string(st.dataBytes) + " B";
        addInfoRow("Data Size", sizeStr);
    }

    // File size on disk
    {
        const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
        if (!projPath.empty())
        {
            std::filesystem::path diskPath = std::filesystem::path(projPath) / "Content" / st.assetPath;
            if (std::filesystem::exists(diskPath))
            {
                std::error_code ec;
                size_t bytes = static_cast<size_t>(std::filesystem::file_size(diskPath, ec));
                std::string sizeStr;
                if (bytes >= 1024 * 1024)
                    sizeStr = std::to_string(bytes / (1024 * 1024)) + " MB";
                else if (bytes >= 1024)
                    sizeStr = std::to_string(bytes / 1024) + " KB";
                else
                    sizeStr = std::to_string(bytes) + " B";
                addInfoRow("File Size", sizeStr);
            }
        }
    }

    // ── Spatial Audio Settings ────────────────────────────────────────────
    {
        WidgetElement spacer{};
        spacer.type        = WidgetElementType::Panel;
        spacer.fillX       = true;
        spacer.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 10.0f });
        spacer.style.color = Vec4{ 0, 0, 0, 0 };
        spacer.runtimeOnly = true;
        root.children.push_back(std::move(spacer));
    }
    {
        WidgetElement heading = EditorUIBuilder::makeHeading("Spatial Audio");
        heading.padding = EditorTheme::Scaled(Vec2{ 0.0f, 4.0f });
        root.children.push_back(std::move(heading));
    }

    // Helper to persist spatial settings to the audio asset JSON and re-save
    auto saveSpatialSettings = [this]()
    {
        auto& assetMgr = AssetManager::Instance();
        auto asset = assetMgr.getLoadedAssetByPath(m_state.assetPath);
        if (!asset) return;
        auto data = asset->getData();
        data["m_is3D"]           = m_state.is3D;
        data["m_minDistance"]     = m_state.minDistance;
        data["m_maxDistance"]     = m_state.maxDistance;
        data["m_rolloffFactor"]  = m_state.rolloffFactor;
        asset->setData(data);
        asset->setIsSaved(false);
        Asset assetRef;
        assetRef.type = AssetType::Audio;
        assetRef.ID = asset->getId();
        assetMgr.saveAsset(assetRef);
    };

    // 2D / 3D dropdown
    {
        WidgetElement dropdown = EditorUIBuilder::makeDropDown(
            "AudioPreview.Spatial.Mode",
            { "2D (Non-Spatial)", "3D (Positional)" },
            m_state.is3D ? 1 : 0,
            [this, saveSpatialSettings](int idx) {
                m_state.is3D = (idx == 1);
                saveSpatialSettings();
                refresh();
            });
        dropdown.fillX = true;
        dropdown.runtimeOnly = true;
        root.children.push_back(std::move(dropdown));
    }

    // 3D-only settings
    if (m_state.is3D)
    {
        auto addFloatRow = [&](const std::string& id, const std::string& label, float value,
                               std::function<void(float)> onChanged)
        {
            WidgetElement row{};
            row.type        = WidgetElementType::StackPanel;
            row.orientation = StackOrientation::Horizontal;
            row.fillX       = true;
            row.minSize     = EditorTheme::Scaled(Vec2{ 0.0f, 26.0f });
            row.runtimeOnly = true;

            WidgetElement lbl{};
            lbl.type            = WidgetElementType::Text;
            lbl.text            = label;
            lbl.font            = theme.fontDefault;
            lbl.fontSize        = theme.fontSizeBody;
            lbl.style.textColor = theme.textSecondary;
            lbl.minSize         = EditorTheme::Scaled(Vec2{ 120.0f, 24.0f });
            lbl.textAlignV      = TextAlignV::Center;
            lbl.runtimeOnly     = true;
            row.children.push_back(std::move(lbl));

            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f", value);
            WidgetElement input = EditorUIBuilder::makeEntryBar(id, buf, [onChanged](const std::string& text) {
                try { onChanged(std::stof(text)); } catch (...) {}
            });
            input.fillX       = true;
            input.runtimeOnly = true;
            row.children.push_back(std::move(input));
            root.children.push_back(std::move(row));
        };

        addFloatRow("AudioPreview.Spatial.MinDist", "Min Distance", m_state.minDistance,
            [this, saveSpatialSettings](float val) {
                m_state.minDistance = val;
                saveSpatialSettings();
            });

        addFloatRow("AudioPreview.Spatial.MaxDist", "Max Distance", m_state.maxDistance,
            [this, saveSpatialSettings](float val) {
                m_state.maxDistance = val;
                saveSpatialSettings();
            });

        addFloatRow("AudioPreview.Spatial.Rolloff", "Rolloff Factor", m_state.rolloffFactor,
            [this, saveSpatialSettings](float val) {
                m_state.rolloffFactor = val;
                saveSpatialSettings();
            });
    }

    addInfoRow("Audio Mode", m_state.is3D ? "3D Positional" : "2D Non-Spatial");
}
