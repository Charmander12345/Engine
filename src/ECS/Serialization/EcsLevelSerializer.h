#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

namespace ecs
{
	class World;
}

namespace ecs::ser
{
	constexpr std::uint32_t kEcsLevelMagic = 0x4C564C45; // 'E''L''V''L'
	constexpr std::uint32_t kEcsLevelVersion = 1;

	bool saveWorld(const std::string& filePath, const ecs::World& world);
	bool loadWorld(const std::string& filePath, ecs::World& world);

	bool saveWorldToStream(std::ostream& out, const ecs::World& world);
	bool loadWorldFromStream(std::istream& in, ecs::World& world);
}
