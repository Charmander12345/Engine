#pragma once

#include "../../Basics/MathTypes.h"

namespace ecs
{
	struct TransformComponent
	{
		Vec3 position{};
		Vec3 rotation{};
		Vec3 scale{ 1.0f, 1.0f, 1.0f };
	};
}
