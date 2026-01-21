#pragma once

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "Entity.h"

namespace ecs
{
	template <typename T>
	class SparseSet
	{
	public:
		static constexpr std::uint32_t kNpos = std::numeric_limits<std::uint32_t>::max();

		bool has(EntityId id) const
		{
			return id < m_sparse.size() && m_sparse[id] != kNpos;
		}

		T* tryGet(EntityId id)
		{
			if (!has(id))
				return nullptr;
			return &m_dense[m_sparse[id]];
		}

		const T* tryGet(EntityId id) const
		{
			if (!has(id))
				return nullptr;
			return &m_dense[m_sparse[id]];
		}

		T& get(EntityId id)
		{
			return m_dense[m_sparse[id]];
		}

		const T& get(EntityId id) const
		{
			return m_dense[m_sparse[id]];
		}

		template <typename... Args>
		T& emplace(EntityId id, Args&&... args)
		{
			ensureSparse(id);
			if (has(id))
			{
				m_dense[m_sparse[id]] = T(std::forward<Args>(args)...);
				return m_dense[m_sparse[id]];
			}

			const std::uint32_t denseIndex = static_cast<std::uint32_t>(m_dense.size());
			m_dense.emplace_back(std::forward<Args>(args)...);
			m_denseEntities.push_back(id);
			m_sparse[id] = denseIndex;
			return m_dense.back();
		}

		bool remove(EntityId id)
		{
			if (!has(id))
				return false;

			const std::uint32_t idx = m_sparse[id];
			const std::uint32_t last = static_cast<std::uint32_t>(m_dense.size() - 1);

			if (idx != last)
			{
				m_dense[idx] = std::move(m_dense[last]);
				m_denseEntities[idx] = m_denseEntities[last];
				m_sparse[m_denseEntities[idx]] = idx;
			}

			m_dense.pop_back();
			m_denseEntities.pop_back();
			m_sparse[id] = kNpos;
			return true;
		}

		void clear()
		{
			m_dense.clear();
			m_denseEntities.clear();
			m_sparse.clear();
		}

		const std::vector<EntityId>& entities() const { return m_denseEntities; }
		std::vector<EntityId>& entities() { return m_denseEntities; }

		const std::vector<T>& data() const { return m_dense; }
		std::vector<T>& data() { return m_dense; }

		std::size_t size() const { return m_dense.size(); }

	private:
		void ensureSparse(EntityId id)
		{
			if (id >= m_sparse.size())
			{
				m_sparse.resize(static_cast<std::size_t>(id) + 1, kNpos);
			}
		}

		std::vector<T> m_dense;
		std::vector<EntityId> m_denseEntities;
		std::vector<std::uint32_t> m_sparse;
	};
}
