#include "TextureViewerWindow.h"

#include "../../AssetManager/AssetManager.h"
#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Logger/Logger.h"
#include "../Texture.h"

#include <filesystem>

TextureViewerWindow::TextureViewerWindow() = default;

TextureViewerWindow::~TextureViewerWindow() = default;

bool TextureViewerWindow::initialize(const std::string& assetPath)
{
    auto& logger = Logger::Instance();

    if (assetPath.empty())
    {
        logger.log(Logger::Category::Rendering,
            "TextureViewer::initialize: assetPath is empty.", Logger::LogLevel::WARNING);
        return false;
    }

    m_assetPath = assetPath;
    logger.log(Logger::Category::Rendering,
        "TextureViewer::initialize: starting for '" + assetPath + "'", Logger::LogLevel::INFO);

    // Try to get file size on disk
    {
        const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
        std::filesystem::path diskPath;
        if (!projPath.empty())
            diskPath = std::filesystem::path(projPath) / "Content" / assetPath;
        if (!diskPath.empty() && std::filesystem::exists(diskPath))
        {
            std::error_code ec;
            m_fileSizeBytes = static_cast<size_t>(std::filesystem::file_size(diskPath, ec));
        }
    }

    auto asset = AssetManager::Instance().getLoadedAssetByPath(assetPath);
    if (!asset)
    {
        logger.log(Logger::Category::Rendering,
            "TextureViewer::initialize: asset not in memory for '" + assetPath + "'",
            Logger::LogLevel::WARNING);
        return false;
    }

    const auto& data = asset->getData();

    // Extract metadata from asset JSON
    if (data.contains("m_width") && data["m_width"].is_number())
        m_width = data["m_width"].get<int>();
    if (data.contains("m_height") && data["m_height"].is_number())
        m_height = data["m_height"].get<int>();
    if (data.contains("m_channels") && data["m_channels"].is_number())
        m_channels = data["m_channels"].get<int>();

    // Compressed texture metadata
    if (data.contains("m_compressed") && data["m_compressed"].is_boolean())
        m_compressed = data["m_compressed"].get<bool>();

    // Determine format string
    if (m_compressed)
    {
        if (data.contains("m_ddsPath"))
            m_formatString = "DDS (Block Compressed)";
        else
            m_formatString = "Compressed";
    }
    else
    {
        switch (m_channels)
        {
        case 1: m_formatString = "Grayscale (R8)"; break;
        case 2: m_formatString = "RG (RG16)"; break;
        case 3: m_formatString = "RGB (RGB24)"; break;
        case 4: m_formatString = "RGBA (RGBA32)"; break;
        default: m_formatString = "Unknown"; break;
        }
    }

    // Determine source file for extended info
    if (data.contains("m_sourcePath") && data["m_sourcePath"].is_string())
    {
        std::string sourcePath = data["m_sourcePath"].get<std::string>();
        std::string ext;
        if (!sourcePath.empty())
        {
            ext = std::filesystem::path(sourcePath).extension().string();
            for (auto& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (ext == ".dds")
        {
            m_formatString = "DDS (Block Compressed)";
            m_compressed = true;
        }
        else if (ext == ".png") m_formatString += " / PNG";
        else if (ext == ".jpg" || ext == ".jpeg") m_formatString += " / JPEG";
        else if (ext == ".tga") m_formatString += " / TGA";
        else if (ext == ".bmp") m_formatString += " / BMP";
        else if (ext == ".hdr") m_formatString += " / HDR";

        // Try to get source file size if asset file was small
        if (m_fileSizeBytes == 0)
        {
            const auto& projPath = DiagnosticsManager::Instance().getProjectInfo().projectPath;
            std::filesystem::path resolved;
            if (!projPath.empty())
                resolved = std::filesystem::path(projPath) / sourcePath;
            if (!resolved.empty() && std::filesystem::exists(resolved))
            {
                std::error_code ec;
                m_fileSizeBytes = static_cast<size_t>(std::filesystem::file_size(resolved, ec));
            }
        }
    }

    m_initialized = true;
    logger.log(Logger::Category::Rendering,
        "TextureViewer::initialize: success – " + std::to_string(m_width) + "x" + std::to_string(m_height)
        + " " + std::to_string(m_channels) + "ch " + m_formatString,
        Logger::LogLevel::INFO);
    return true;
}
