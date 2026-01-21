#pragma once

#include <string>

#include "../../Basics/MathTypes.h"

namespace ecs
{
	struct RenderOverrides
	{
		// Minimal example override: per-instance tint color
		Vec3 tint{ 1.0f, 1.0f, 1.0f };
	};

	struct RenderComponent
	{
		// Asset references stored as relative-to-Content paths
		std::string meshAssetPath;
		std::string materialAssetPath;

		RenderOverrides overrides{};
	};
}
