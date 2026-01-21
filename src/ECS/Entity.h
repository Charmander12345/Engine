#pragma once

#include <cstdint>

namespace ecs
{
	using EntityId = std::uint32_t;
	using Generation = std::uint32_t;

	constexpr EntityId kInvalidEntityId = 0;

	struct EntityHandle
	{
		EntityId id{ kInvalidEntityId };
		Generation generation{ 0 };

		constexpr bool isValid() const noexcept { return id != kInvalidEntityId; }

		friend constexpr bool operator==(const EntityHandle& a, const EntityHandle& b) noexcept
		{
			return a.id == b.id && a.generation == b.generation;
		}
		friend constexpr bool operator!=(const EntityHandle& a, const EntityHandle& b) noexcept
		{
			return !(a == b);
		}
	};
}
