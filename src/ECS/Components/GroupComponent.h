#pragma once

#include <string>

namespace ecs
{
	struct GroupComponent
	{
		std::string groupId;
		bool instanced{ false };
	};
}
