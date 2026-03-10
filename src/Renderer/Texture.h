#pragma once

#include "EngineObject.h"
#include <vector>

/// Compressed texture formats (GPU-native block compression)
enum class CompressedFormat
{
	None = 0,
	BC1,      // DXT1 – RGB, 4:1 compression (8 bytes per 4×4 block)
	BC1A,     // DXT1 – RGBA with 1-bit alpha
	BC2,      // DXT3 – RGBA with explicit alpha (16 bytes per 4×4 block)
	BC3,      // DXT5 – RGBA with interpolated alpha (16 bytes per 4×4 block)
	BC4,      // RGTC1 – single channel (8 bytes per 4×4 block)
	BC5,      // RGTC2 – two channels (16 bytes per 4×4 block)
	BC7       // BPTC – high-quality RGBA (16 bytes per 4×4 block)
};

/// Returns the byte size of one compressed 4×4 block for the given format.
inline int compressedBlockSize(CompressedFormat fmt)
{
	switch (fmt)
	{
	case CompressedFormat::BC1:
	case CompressedFormat::BC1A:
	case CompressedFormat::BC4:  return 8;
	case CompressedFormat::BC2:
	case CompressedFormat::BC3:
	case CompressedFormat::BC5:
	case CompressedFormat::BC7:  return 16;
	default: return 0;
	}
}

/// Describes one mip level of a compressed texture.
struct CompressedMipLevel
{
	int width{ 0 };
	int height{ 0 };
	std::vector<unsigned char> data;
};

class Texture : public EngineObject
{
public:
	Texture() = default;
	~Texture() override = default;

	int getWidth() const { return m_width; }
	int getHeight() const { return m_height; }
	int getChannels() const { return m_channels; }
	const std::vector<unsigned char>& getData() const { return m_data; }

	void setWidth(int w) { m_width = w; }
	void setHeight(int h) { m_height = h; }
	void setChannels(int c) { m_channels = c; }
	void setData(std::vector<unsigned char> data) { m_data = std::move(data); }

	// Compressed texture support
	bool isCompressed() const { return m_compressedFormat != CompressedFormat::None; }
	CompressedFormat getCompressedFormat() const { return m_compressedFormat; }
	void setCompressedFormat(CompressedFormat fmt) { m_compressedFormat = fmt; }

	const std::vector<CompressedMipLevel>& getCompressedMips() const { return m_compressedMips; }
	void setCompressedMips(std::vector<CompressedMipLevel> mips) { m_compressedMips = std::move(mips); }

	// Request driver-side compression on upload (for uncompressed textures)
	bool isCompressionRequested() const { return m_requestCompression; }
	void setRequestCompression(bool request) { m_requestCompression = request; }

private:
	int m_width{ 0 };
	int m_height{ 0 };
	int m_channels{ 0 };
	std::vector<unsigned char> m_data;

	CompressedFormat m_compressedFormat{ CompressedFormat::None };
	std::vector<CompressedMipLevel> m_compressedMips;
	bool m_requestCompression{ false };
};