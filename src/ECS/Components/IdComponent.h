#pragma once

#include <cstdint>
#include <random>

namespace ecs
{
	struct IdComponent
	{
		std::uint64_t guid{};
	};

	inline std::uint64_t generateGuid64()
	{
		static thread_local std::mt19937_64 rng{ std::random_device{}() };
		std::uniform_int_distribution<std::uint64_t> dist;
		std::uint64_t v = 0;
		do
		{
			v = dist(rng);
		} while (v == 0);
		return v;
	}
}
