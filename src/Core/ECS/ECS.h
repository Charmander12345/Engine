#pragma once
#include "DataStructs/SparseSet.h"
#include <vector>

namespace ECS
{

	struct ECSConfig
	{
		unsigned int maxEntities{ 10000 };
	};

	using Entity = unsigned int;
	class ECSManager
	{
	public:
		static ECSManager& Instance();
		void initialize(const ECSConfig& config);

		Entity createEntity();


	private:
		ECSManager() = default;
		~ECSManager() = default;
		ECSManager(const ECSManager&) = delete;
		ECSManager& operator=(const ECSManager&) = delete;
		Entity m_nextEntity{ 1 };
	};
}