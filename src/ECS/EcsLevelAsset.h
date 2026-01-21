#pragma once

#include <memory>

#include "../Basics/EngineObject.h"
#include "../AssetManager/AssetTypes.h"

#include "World.h"

namespace ecs
{
	class EcsLevelAsset : public EngineObject
	{
	public:
		EcsLevelAsset()
		{
			setAssetType(AssetType::Level);
		}

		World& world() { return m_world; }
		const World& world() const { return m_world; }

	private:
		World m_world;
	};
}
