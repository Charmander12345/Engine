#include <ECS/Components.h>
#include <ECS/ECS.h>

namespace ECS
{
	ECSManager& ECSManager::Instance()
	{
		static ECSManager instance;
		return instance;
	}
	void ECSManager::initialize(const ECSConfig& config)
	{

	}
	Entity ECSManager::createEntity()
	{
		return Entity();
	}
}
