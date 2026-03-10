#include "DDSLoader.h"
#include "Texture.h"
#include "Logger.h"

#include <fstream>
#include <cstring>
#include <algorithm>

// -----------------------------------------------------------------------
// DDS file format constants
// -----------------------------------------------------------------------

static constexpr uint32_t DDS_MAGIC = 0x20534444; // 'DDS '

// DDS_HEADER::dwFlags
static constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;

// DDS_PIXELFORMAT::dwFlags
static constexpr uint32_t DDPF_FOURCC = 0x4;

// FourCC helpers
static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(a)
         | (static_cast<uint32_t>(b) << 8)
         | (static_cast<uint32_t>(c) << 16)
         | (static_cast<uint32_t>(d) << 24);
}

static constexpr uint32_t FOURCC_DXT1 = MakeFourCC('D', 'X', 'T', '1');
static constexpr uint32_t FOURCC_DXT3 = MakeFourCC('D', 'X', 'T', '3');
static constexpr uint32_t FOURCC_DXT5 = MakeFourCC('D', 'X', 'T', '5');
static constexpr uint32_t FOURCC_ATI1 = MakeFourCC('A', 'T', 'I', '1');
static constexpr uint32_t FOURCC_ATI2 = MakeFourCC('A', 'T', 'I', '2');
static constexpr uint32_t FOURCC_BC4U = MakeFourCC('B', 'C', '4', 'U');
static constexpr uint32_t FOURCC_BC5U = MakeFourCC('B', 'C', '5', 'U');
static constexpr uint32_t FOURCC_DX10 = MakeFourCC('D', 'X', '1', '0');

// DXGI formats for BC7
static constexpr uint32_t DXGI_FORMAT_BC7_UNORM      = 98;
static constexpr uint32_t DXGI_FORMAT_BC7_UNORM_SRGB  = 99;
// Additional DXGI formats for completeness
static constexpr uint32_t DXGI_FORMAT_BC1_UNORM       = 71;
static constexpr uint32_t DXGI_FORMAT_BC1_UNORM_SRGB  = 72;
static constexpr uint32_t DXGI_FORMAT_BC2_UNORM       = 74;
static constexpr uint32_t DXGI_FORMAT_BC2_UNORM_SRGB  = 75;
static constexpr uint32_t DXGI_FORMAT_BC3_UNORM       = 77;
static constexpr uint32_t DXGI_FORMAT_BC3_UNORM_SRGB  = 78;
static constexpr uint32_t DXGI_FORMAT_BC4_UNORM       = 80;
static constexpr uint32_t DXGI_FORMAT_BC5_UNORM       = 83;

// -----------------------------------------------------------------------
// DDS header structs (packed)
// -----------------------------------------------------------------------

#pragma pack(push, 1)

struct DDSPixelFormat
{
    uint32_t size;
    uint32_t flags;
    uint32_t fourCC;
    uint32_t rgbBitCount;
    uint32_t rBitMask;
    uint32_t gBitMask;
    uint32_t bBitMask;
    uint32_t aBitMask;
};

struct DDSHeader
{
    uint32_t size;          // must be 124
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth;
    uint32_t mipMapCount;
    uint32_t reserved1[11];
    DDSPixelFormat pixelFormat;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct DDSHeaderDX10
{
    uint32_t dxgiFormat;
    uint32_t resourceDimension;
    uint32_t miscFlag;
    uint32_t arraySize;
    uint32_t miscFlags2;
};

#pragma pack(pop)

// -----------------------------------------------------------------------
// Helper: compute byte size of a mip level for block-compressed data
// -----------------------------------------------------------------------

static size_t computeMipSize(int width, int height, int blockBytes)
{
    const int blocksX = std::max(1, (width + 3) / 4);
    const int blocksY = std::max(1, (height + 3) / 4);
    return static_cast<size_t>(blocksX) * blocksY * blockBytes;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

std::shared_ptr<Texture> loadDDS(const std::string& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open())
    {
        Logger::Instance().log("DDSLoader: failed to open file: " + filePath, Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Read magic number
    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != DDS_MAGIC)
    {
        Logger::Instance().log("DDSLoader: invalid DDS magic in: " + filePath, Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Read main header
    DDSHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.size != 124)
    {
        Logger::Instance().log("DDSLoader: unexpected header size in: " + filePath, Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Determine compressed format
    CompressedFormat format = CompressedFormat::None;
    bool hasDX10 = false;

    if (header.pixelFormat.flags & DDPF_FOURCC)
    {
        const uint32_t fourCC = header.pixelFormat.fourCC;

        if (fourCC == FOURCC_DXT1)
        {
            format = CompressedFormat::BC1;
        }
        else if (fourCC == FOURCC_DXT3)
        {
            format = CompressedFormat::BC2;
        }
        else if (fourCC == FOURCC_DXT5)
        {
            format = CompressedFormat::BC3;
        }
        else if (fourCC == FOURCC_ATI1 || fourCC == FOURCC_BC4U)
        {
            format = CompressedFormat::BC4;
        }
        else if (fourCC == FOURCC_ATI2 || fourCC == FOURCC_BC5U)
        {
            format = CompressedFormat::BC5;
        }
        else if (fourCC == FOURCC_DX10)
        {
            hasDX10 = true;
        }
        else
        {
            Logger::Instance().log("DDSLoader: unsupported FourCC in: " + filePath, Logger::LogLevel::ERROR);
            return nullptr;
        }
    }
    else
    {
        Logger::Instance().log("DDSLoader: non-FourCC DDS files not supported: " + filePath, Logger::LogLevel::ERROR);
        return nullptr;
    }

    // Read DX10 extended header if present
    if (hasDX10)
    {
        DDSHeaderDX10 dx10{};
        in.read(reinterpret_cast<char*>(&dx10), sizeof(dx10));

        switch (dx10.dxgiFormat)
        {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:  format = CompressedFormat::BC1;  break;
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:  format = CompressedFormat::BC2;  break;
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:  format = CompressedFormat::BC3;  break;
        case DXGI_FORMAT_BC4_UNORM:       format = CompressedFormat::BC4;  break;
        case DXGI_FORMAT_BC5_UNORM:       format = CompressedFormat::BC5;  break;
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:  format = CompressedFormat::BC7;  break;
        default:
            Logger::Instance().log("DDSLoader: unsupported DXGI format (" + std::to_string(dx10.dxgiFormat) + ") in: " + filePath, Logger::LogLevel::ERROR);
            return nullptr;
        }
    }

    if (format == CompressedFormat::None)
    {
        Logger::Instance().log("DDSLoader: could not determine compressed format in: " + filePath, Logger::LogLevel::ERROR);
        return nullptr;
    }

    const int blockBytes = compressedBlockSize(format);
    const int width = static_cast<int>(header.width);
    const int height = static_cast<int>(header.height);
    int mipCount = 1;
    if (header.flags & DDSD_MIPMAPCOUNT)
    {
        mipCount = std::max(1, static_cast<int>(header.mipMapCount));
    }

    // Read mip levels
    std::vector<CompressedMipLevel> mips;
    mips.reserve(static_cast<size_t>(mipCount));

    int mipW = width;
    int mipH = height;

    for (int i = 0; i < mipCount; ++i)
    {
        const size_t mipSize = computeMipSize(mipW, mipH, blockBytes);

        CompressedMipLevel level;
        level.width = mipW;
        level.height = mipH;
        level.data.resize(mipSize);
        in.read(reinterpret_cast<char*>(level.data.data()), static_cast<std::streamsize>(mipSize));

        if (!in)
        {
            Logger::Instance().log("DDSLoader: unexpected end of file at mip " + std::to_string(i) + " in: " + filePath, Logger::LogLevel::ERROR);
            return nullptr;
        }

        mips.push_back(std::move(level));

        mipW = std::max(1, mipW / 2);
        mipH = std::max(1, mipH / 2);
    }

    // Build Texture object
    auto texture = std::make_shared<Texture>();
    texture->setWidth(width);
    texture->setHeight(height);

    // Set channel count based on format for informational purposes
    switch (format)
    {
    case CompressedFormat::BC1:  texture->setChannels(3); break;
    case CompressedFormat::BC1A: texture->setChannels(4); break;
    case CompressedFormat::BC2:
    case CompressedFormat::BC3:
    case CompressedFormat::BC7:  texture->setChannels(4); break;
    case CompressedFormat::BC4:  texture->setChannels(1); break;
    case CompressedFormat::BC5:  texture->setChannels(2); break;
    default: texture->setChannels(4); break;
    }

    texture->setCompressedFormat(format);
    texture->setCompressedMips(std::move(mips));

    Logger::Instance().log("DDSLoader: loaded " + filePath +
        " (" + std::to_string(width) + "x" + std::to_string(height) +
        ", " + std::to_string(mipCount) + " mips, BC" +
        std::to_string(static_cast<int>(format)) + ")",
        Logger::LogLevel::INFO);

    return texture;
}
