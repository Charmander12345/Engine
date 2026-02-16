#pragma once

#include <vector>

#include "Entity.h"
#include "SparseSet.h"
#include "Components/TransformComponent.h"
#include "Components/NameComponent.h"
#include "Components/RenderComponent.h"
#include "Components/GroupComponent.h"
#include "Components/IdComponent.h"

namespace ecs
{
	class World
	{
	public:
		World();

		// Reserve capacity for expected number of entities to reduce allocations
		void reserveEntityCapacity(std::size_t capacity);

		EntityHandle createEntity();
		EntityHandle createEntityWithId(std::uint64_t guid);
		bool destroyEntity(EntityHandle e);
		bool isAlive(EntityHandle e) const;
		std::size_t aliveCount() const;

		TransformComponent& emplaceTransform(EntityHandle e, const TransformComponent& t);
		bool removeTransform(EntityHandle e);
		bool hasTransform(EntityHandle e) const;
		TransformComponent* tryGetTransform(EntityHandle e);
		const TransformComponent* tryGetTransform(EntityHandle e) const;

		SparseSet<TransformComponent>& transforms();
		const SparseSet<TransformComponent>& transforms() const;

		NameComponent& emplaceName(EntityHandle e, const NameComponent& c);
		bool removeName(EntityHandle e);
		bool hasName(EntityHandle e) const;
		NameComponent* tryGetName(EntityHandle e);
		const NameComponent* tryGetName(EntityHandle e) const;
		SparseSet<NameComponent>& names();
		const SparseSet<NameComponent>& names() const;

		RenderComponent& emplaceRender(EntityHandle e, const RenderComponent& c);
		bool removeRender(EntityHandle e);
		bool hasRender(EntityHandle e) const;
		RenderComponent* tryGetRender(EntityHandle e);
		const RenderComponent* tryGetRender(EntityHandle e) const;
		SparseSet<RenderComponent>& renders();
		const SparseSet<RenderComponent>& renders() const;

		GroupComponent& emplaceGroup(EntityHandle e, const GroupComponent& c);
		bool removeGroup(EntityHandle e);
		bool hasGroup(EntityHandle e) const;
		GroupComponent* tryGetGroup(EntityHandle e);
		const GroupComponent* tryGetGroup(EntityHandle e) const;
		SparseSet<GroupComponent>& groups();
		const SparseSet<GroupComponent>& groups() const;

		IdComponent& emplaceId(EntityHandle e, const IdComponent& c);
		bool removeId(EntityHandle e);
		bool hasId(EntityHandle e) const;
		IdComponent* tryGetId(EntityHandle e);
		const IdComponent* tryGetId(EntityHandle e) const;
		SparseSet<IdComponent>& ids();
		const SparseSet<IdComponent>& ids() const;

		template <typename Fn>
		void forEachTransformRender(Fn&& fn)
		{
			// Iterate over the smaller set for speed.
			if (m_transforms.size() <= m_renders.size())
			{
				for (auto eid : m_transforms.entities())
				{
					auto* t = m_transforms.tryGet(eid);
					auto* r = m_renders.tryGet(eid);
					if (t && r)
						fn(EntityHandle{ eid, m_generations[eid] }, *t, *r);
				}
			}
			else
			{
				for (auto eid : m_renders.entities())
				{
					auto* t = m_transforms.tryGet(eid);
					auto* r = m_renders.tryGet(eid);
					if (t && r)
						fn(EntityHandle{ eid, m_generations[eid] }, *t, *r);
				}
			}
		}

	private:
		std::vector<Generation> m_generations;
		std::vector<EntityId> m_freeList;
		std::vector<bool> m_alive;
		std::size_t m_aliveCount{ 0 };

		SparseSet<TransformComponent> m_transforms;
		SparseSet<NameComponent> m_names;
		SparseSet<RenderComponent> m_renders;
		SparseSet<GroupComponent> m_groups;
		SparseSet<IdComponent> m_ids;
	};
}
