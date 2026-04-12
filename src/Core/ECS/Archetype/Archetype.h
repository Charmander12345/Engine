#pragma once
#include "ArchetypeTypes.h"
#include "Chunk.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cassert>

namespace Mass
{

/// An Archetype groups all entities that share the exact same set of fragment
/// types.  Entities are stored across one or more Chunks in SoA layout.
class Archetype
{
public:
	Archetype() = default;

	// Move-only (Chunk is non-copyable)
	Archetype(const Archetype&) = delete;
	Archetype& operator=(const Archetype&) = delete;
	Archetype(Archetype&&) noexcept = default;
	Archetype& operator=(Archetype&&) noexcept = default;

	/// Initialise the archetype for the given signature.
	void initialize(const ArchetypeSignature& sig, uint32_t archetypeIndex)
	{
		m_signature      = sig;
		m_archetypeIndex = archetypeIndex;

		// Collect FragmentTypeInfos from the registry for every bit set in sig
		const auto& reg = FragmentRegistry::Instance();
		m_fragmentInfos.clear();
		for (size_t i = 0; i < MaxFragmentTypes && i < reg.count(); ++i)
		{
			if (sig.test(i))
				m_fragmentInfos.push_back(reg.getInfo(static_cast<FragmentTypeID>(i)));
		}

		// Sort by typeID for deterministic column order
		std::sort(m_fragmentInfos.begin(), m_fragmentInfos.end(),
			[](const FragmentTypeInfo& a, const FragmentTypeInfo& b) { return a.id < b.id; });

		// Build column-index lookup: fragmentTypeID -> column index
		m_columnLookup.clear();
		for (size_t c = 0; c < m_fragmentInfos.size(); ++c)
			m_columnLookup[m_fragmentInfos[c].id] = static_cast<uint32_t>(c);

		// Compute chunk layout
		m_chunkCapacity = computeChunkLayout(m_fragmentInfos, m_columnLayouts);
	}

	/// Allocate a new entity row.  Returns (chunkIndex, rowIndex).
	std::pair<uint32_t, uint32_t> allocateRow()
	{
		// Find a non-full chunk or create one
		for (uint32_t ci = 0; ci < static_cast<uint32_t>(m_chunks.size()); ++ci)
		{
			if (!m_chunks[ci].isFull())
			{
				size_t row = m_chunks[ci].pushBack();
				++m_entityCount;
				return { ci, static_cast<uint32_t>(row) };
			}
		}
		// Need a new chunk
		uint32_t ci = static_cast<uint32_t>(m_chunks.size());
		m_chunks.emplace_back();
		m_chunks.back().allocate(m_columnLayouts, m_chunkCapacity);
		size_t row = m_chunks.back().pushBack();
		++m_entityCount;
		return { ci, static_cast<uint32_t>(row) };
	}

	/// Remove entity at (chunkIndex, row) via swap-remove.
	/// Returns the entity handle that was swapped into this slot (so the caller
	/// can update its slot), or InvalidMassEntity if the row was the last.
	void removeRow(uint32_t chunkIndex, uint32_t row,
	               MassEntity& outSwappedEntity)
	{
		assert(chunkIndex < m_chunks.size());
		auto& chunk = m_chunks[chunkIndex];
		size_t lastRow = chunk.count() - 1;

		outSwappedEntity = InvalidMassEntity;
		if (static_cast<size_t>(row) != lastRow)
		{
			// The entity at lastRow will be moved to row — caller must look it up
			// We signal this by setting outSwappedEntity to a sentinel with
			// chunkIndex/lastRow so the caller can find the correct entity.
			outSwappedEntity.index = chunkIndex;
			outSwappedEntity.generation = static_cast<uint32_t>(lastRow);
		}

		chunk.swapRemove(row);
		--m_entityCount;
	}

	// ── Fragment Access ──────────────────────────────────────────────

	/// Get column index for a fragment type.  Returns UINT32_MAX if not present.
	uint32_t getColumnIndex(FragmentTypeID typeID) const
	{
		auto it = m_columnLookup.find(typeID);
		return (it != m_columnLookup.end()) ? it->second : UINT32_MAX;
	}

	/// Check if this archetype contains the given fragment type.
	bool hasFragment(FragmentTypeID typeID) const
	{
		return m_columnLookup.count(typeID) > 0;
	}

	/// Get typed pointer to fragment data for an entity at (chunk, row).
	template<typename T>
	T* getFragment(uint32_t chunkIndex, uint32_t row)
	{
		uint32_t col = getColumnIndex(getFragmentTypeID<T>());
		if (col == UINT32_MAX) return nullptr;
		return m_chunks[chunkIndex].getElement<T>(col, row);
	}

	template<typename T>
	const T* getFragment(uint32_t chunkIndex, uint32_t row) const
	{
		uint32_t col = getColumnIndex(getFragmentTypeID<T>());
		if (col == UINT32_MAX) return nullptr;
		return m_chunks[chunkIndex].getElement<T>(col, row);
	}

	/// Set fragment data for an entity at (chunk, row).
	template<typename T>
	bool setFragment(uint32_t chunkIndex, uint32_t row, const T& value)
	{
		T* ptr = getFragment<T>(chunkIndex, row);
		if (!ptr) return false;
		*ptr = value;
		return true;
	}

	// ── Accessors ────────────────────────────────────────────────────

	const ArchetypeSignature& signature() const { return m_signature; }
	uint32_t archetypeIndex() const { return m_archetypeIndex; }
	size_t entityCount() const { return m_entityCount; }
	size_t chunkCount() const { return m_chunks.size(); }
	size_t chunkCapacity() const { return m_chunkCapacity; }

	Chunk& getChunk(uint32_t index) { return m_chunks[index]; }
	const Chunk& getChunk(uint32_t index) const { return m_chunks[index]; }

	const std::vector<FragmentTypeInfo>& fragmentInfos() const { return m_fragmentInfos; }
	const std::vector<ColumnLayout>& columnLayouts() const { return m_columnLayouts; }

	/// Returns true if this archetype's signature is a superset of \p required.
	bool matchesQuery(const ArchetypeSignature& required) const
	{
		return (m_signature & required) == required;
	}

	/// Returns true if this archetype's signature has NONE of the \p excluded bits.
	bool matchesExclusion(const ArchetypeSignature& excluded) const
	{
		return (m_signature & excluded).none();
	}

private:
	ArchetypeSignature               m_signature;
	uint32_t                         m_archetypeIndex{ 0 };
	std::vector<FragmentTypeInfo>    m_fragmentInfos;
	std::vector<ColumnLayout>        m_columnLayouts;
	std::unordered_map<FragmentTypeID, uint32_t> m_columnLookup;
	std::vector<Chunk>               m_chunks;
	size_t                           m_chunkCapacity{ 0 };
	size_t                           m_entityCount{ 0 };
};

} // namespace Mass
