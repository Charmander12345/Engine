#include "ArchetypeECS.h"
#include "MassProcessor.h"
#include <algorithm>
#include <cassert>
#include <memory>

namespace Mass
{

MassEntitySubsystem& MassEntitySubsystem::Instance()
{
	static MassEntitySubsystem s_instance;
	return s_instance;
}

// ── Entity Lifecycle ─────────────────────────────────────────────────

MassEntity MassEntitySubsystem::createEntity(const ArchetypeSignature& signature)
{
	MassEntity handle = allocateSlot();
	auto& archetype = getOrCreateArchetype(signature);

	auto [ci, ri] = archetype.allocateRow();

	auto& slot        = m_slots[handle.index];
	slot.archetypeIndex = archetype.archetypeIndex();
	slot.chunkIndex     = ci;
	slot.rowIndex       = ri;

	// Track which entity occupies this row
	auto& emap = m_archetypeEntities[archetype.archetypeIndex()];
	if (ci >= emap.chunkEntities.size())
		emap.chunkEntities.resize(static_cast<size_t>(ci) + 1);
	auto& chunkEnts = emap.chunkEntities[ci];
	if (ri >= chunkEnts.size())
		chunkEnts.resize(static_cast<size_t>(ri) + 1, InvalidMassEntity);
	chunkEnts[ri] = handle;

	return handle;
}

void MassEntitySubsystem::createEntities(const ArchetypeSignature& signature,
                                          size_t count,
                                          std::vector<MassEntity>* outHandles)
{
	if (outHandles) outHandles->reserve(outHandles->size() + count);
	for (size_t i = 0; i < count; ++i)
	{
		MassEntity e = createEntity(signature);
		if (outHandles) outHandles->push_back(e);
	}
}

void MassEntitySubsystem::destroyEntity(MassEntity entity)
{
	if (!isAlive(entity)) return;

	if (m_ticking)
	{
		m_commandBuffer.enqueue([this, entity]() { destroyEntity(entity); });
		return;
	}

	auto& slot = m_slots[entity.index];
	auto& archetype = *m_archetypes[slot.archetypeIndex];
	auto& emap = m_archetypeEntities[slot.archetypeIndex];

	uint32_t ci = slot.chunkIndex;
	uint32_t ri = slot.rowIndex;

	// Find entity that was in the last row (will be swapped into ri)
	size_t lastRow = archetype.getChunk(ci).count() - 1;
	MassEntity swappedEntity = InvalidMassEntity;
	if (static_cast<size_t>(ri) != lastRow && lastRow < emap.chunkEntities[ci].size())
	{
		swappedEntity = emap.chunkEntities[ci][lastRow];
	}

	// Perform swap-remove in the chunk
	MassEntity swapSignal = InvalidMassEntity;
	archetype.removeRow(ci, ri, swapSignal);

	// Update the entity map: remove last row, and if swapped, update the moved entity
	if (swappedEntity.isValid() && swappedEntity != entity)
	{
		emap.chunkEntities[ci][ri] = swappedEntity;
		// Update the slot of the swapped entity
		auto& swapSlot = m_slots[swappedEntity.index];
		swapSlot.rowIndex = ri;
	}

	// Shrink entity map
	if (!emap.chunkEntities[ci].empty())
	{
		size_t newSize = archetype.getChunk(ci).count();
		if (emap.chunkEntities[ci].size() > newSize)
			emap.chunkEntities[ci].resize(newSize);
	}

	freeSlot(entity);
}

bool MassEntitySubsystem::isAlive(MassEntity entity) const
{
	if (entity.index >= m_slots.size()) return false;
	const auto& slot = m_slots[entity.index];
	return slot.alive && slot.generation == entity.generation;
}

// ── Query / Iteration ────────────────────────────────────────────────

void MassEntitySubsystem::forEachChunk(const ArchetypeSignature& required,
                                        ChunkIteratorFn fn)
{
	ArchetypeSignature noExclude;
	forEachChunk(required, noExclude, fn);
}

void MassEntitySubsystem::forEachChunk(const ArchetypeSignature& required,
                                        const ArchetypeSignature& excluded,
                                        ChunkIteratorFn fn)
{
	for (auto& archetype : m_archetypes)
	{
		if (!archetype->matchesQuery(required)) continue;
		if (excluded.any() && !archetype->matchesExclusion(excluded)) continue;

		// Build column-index map for this archetype
		std::unordered_map<FragmentTypeID, uint32_t> colMap;
		for (size_t c = 0; c < archetype->fragmentInfos().size(); ++c)
			colMap[archetype->fragmentInfos()[c].id] = static_cast<uint32_t>(c);

		for (size_t ci = 0; ci < archetype->chunkCount(); ++ci)
		{
			auto& chunk = archetype->getChunk(static_cast<uint32_t>(ci));
			if (chunk.count() > 0)
				fn(chunk, colMap, chunk.count());
		}
	}
}

// ── Processor Management ─────────────────────────────────────────────

void MassEntitySubsystem::registerProcessor(MassProcessor* processor)
{
	if (std::find(m_processors.begin(), m_processors.end(), processor) == m_processors.end())
		m_processors.push_back(processor);
}

void MassEntitySubsystem::unregisterProcessor(MassProcessor* processor)
{
	m_processors.erase(
		std::remove(m_processors.begin(), m_processors.end(), processor),
		m_processors.end());
}

void MassEntitySubsystem::tick(float deltaTime)
{
	m_ticking = true;
	for (auto* proc : m_processors)
	{
		if (proc) proc->execute(*this, deltaTime);
	}
	m_ticking = false;
	flushCommands();
}

// ── Command Buffer ───────────────────────────────────────────────────

void MassEntitySubsystem::flushCommands()
{
	m_commandBuffer.flush();
}

// ── Lifecycle ────────────────────────────────────────────────────────

void MassEntitySubsystem::clear()
{
	m_archetypes.clear();
	m_archetypeMap.clear();
	m_archetypeEntities.clear();
	m_slots.clear();
	m_freeSlots.clear();
	m_aliveCount = 0;
	m_commandBuffer.flush();
	// Processors are not owned, so just clear the list
	m_processors.clear();
}

// ── Archetype Graph ──────────────────────────────────────────────────

Archetype& MassEntitySubsystem::getOrCreateArchetype(const ArchetypeSignature& sig)
{
	auto it = m_archetypeMap.find(sig);
	if (it != m_archetypeMap.end())
		return *m_archetypes[it->second];

	uint32_t idx = static_cast<uint32_t>(m_archetypes.size());
	auto ptr = std::make_unique<Archetype>();
	ptr->initialize(sig, idx);
	m_archetypes.push_back(std::move(ptr));
	m_archetypeMap[sig] = idx;
	m_archetypeEntities.emplace_back();
	return *m_archetypes.back();
}

void MassEntitySubsystem::moveEntity(MassEntity entity, const ArchetypeSignature& newSig)
{
	if (!isAlive(entity)) return;

	auto& slot = m_slots[entity.index];
	auto& oldArchetype = *m_archetypes[slot.archetypeIndex];
	auto& newArchetype = getOrCreateArchetype(newSig);

	// Allocate new row
	auto [newCI, newRI] = newArchetype.allocateRow();

	// Copy overlapping fragment data from old to new
	auto& oldChunk = oldArchetype.getChunk(slot.chunkIndex);
	auto& newChunk = newArchetype.getChunk(newCI);

	for (const auto& newFrag : newArchetype.fragmentInfos())
	{
		uint32_t oldCol = oldArchetype.getColumnIndex(newFrag.id);
		uint32_t newCol = newArchetype.getColumnIndex(newFrag.id);
		if (oldCol != UINT32_MAX && newCol != UINT32_MAX)
		{
			// Copy data
			uint8_t* dst = newChunk.columnData(newCol) + newRI * newFrag.size;
			const uint8_t* src = oldChunk.columnData(oldCol) + slot.rowIndex * newFrag.size;
			std::memcpy(dst, src, newFrag.size);
		}
	}

	// Remove from old archetype
	auto& oldEmap = m_archetypeEntities[slot.archetypeIndex];
	size_t oldLastRow = oldChunk.count() - 1;
	MassEntity swappedEntity = InvalidMassEntity;
	if (static_cast<size_t>(slot.rowIndex) != oldLastRow &&
	    oldLastRow < oldEmap.chunkEntities[slot.chunkIndex].size())
	{
		swappedEntity = oldEmap.chunkEntities[slot.chunkIndex][oldLastRow];
	}

	MassEntity swapSignal = InvalidMassEntity;
	oldArchetype.removeRow(slot.chunkIndex, slot.rowIndex, swapSignal);

	// Update swapped entity's slot
	if (swappedEntity.isValid() && swappedEntity != entity)
	{
		oldEmap.chunkEntities[slot.chunkIndex][slot.rowIndex] = swappedEntity;
		auto& swapSlot = m_slots[swappedEntity.index];
		swapSlot.rowIndex = slot.rowIndex;
	}

	// Shrink old entity map
	if (!oldEmap.chunkEntities[slot.chunkIndex].empty())
	{
		size_t newSize = oldChunk.count();
		if (oldEmap.chunkEntities[slot.chunkIndex].size() > newSize)
			oldEmap.chunkEntities[slot.chunkIndex].resize(newSize);
	}

	// Update slot to new archetype
	slot.archetypeIndex = newArchetype.archetypeIndex();
	slot.chunkIndex     = newCI;
	slot.rowIndex       = newRI;

	// Track in new entity map
	auto& newEmap = m_archetypeEntities[newArchetype.archetypeIndex()];
	if (newCI >= newEmap.chunkEntities.size())
		newEmap.chunkEntities.resize(static_cast<size_t>(newCI) + 1);
	auto& newChunkEnts = newEmap.chunkEntities[newCI];
	if (newRI >= newChunkEnts.size())
		newChunkEnts.resize(static_cast<size_t>(newRI) + 1, InvalidMassEntity);
	newChunkEnts[newRI] = entity;
}

// ── Entity Slot Management ───────────────────────────────────────────

MassEntity MassEntitySubsystem::allocateSlot()
{
	MassEntity handle;
	if (!m_freeSlots.empty())
	{
		uint32_t idx = m_freeSlots.back();
		m_freeSlots.pop_back();
		auto& slot   = m_slots[idx];
		slot.generation++;
		slot.alive = true;
		handle.index      = idx;
		handle.generation = slot.generation;
	}
	else
	{
		uint32_t idx = static_cast<uint32_t>(m_slots.size());
		MassEntitySlot slot;
		slot.generation = 1;
		slot.alive      = true;
		m_slots.push_back(slot);
		handle.index      = idx;
		handle.generation = 1;
	}
	++m_aliveCount;
	return handle;
}

void MassEntitySubsystem::freeSlot(MassEntity entity)
{
	assert(entity.index < m_slots.size());
	auto& slot = m_slots[entity.index];
	slot.alive = false;
	m_freeSlots.push_back(entity.index);
	--m_aliveCount;
}

// ── MassProcessor ────────────────────────────────────────────────────

void MassProcessor::execute(MassEntitySubsystem& subsystem, float deltaTime)
{
	if (!m_configured)
	{
		configure(m_query);
		m_configured = true;
	}

	subsystem.forEachChunk(
		m_query.required(),
		m_query.excluded(),
		[this, deltaTime](Chunk& chunk,
						   const std::unordered_map<FragmentTypeID, uint32_t>& colMap,
						   size_t count)
		{
			ChunkView view(chunk, colMap, count);
			executeChunk(view, deltaTime);
		});
}

} // namespace Mass
