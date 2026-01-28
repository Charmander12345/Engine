#pragma once

#include "EngineObject.h"
#include <vector>

class Texture : public EngineObject
{
#define NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Texture, m_width, m_height, m_channels, m_data)
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

private:
	int m_width{ 0 };
	int m_height{ 0 };
	int m_channels{ 0 };
	std::vector<unsigned char> m_data;
};