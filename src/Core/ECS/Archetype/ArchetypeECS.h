#pragma once
#include "ArchetypeTypes.h"
#include "Archetype.h"
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>

namespace Mass
{

// Forward declarations
class MassProcessor;

/// Callback type for iterating over chunks.
/// Parameters: Chunk&, column-index-map (fragmentTypeID -> colIdx), entity count.
using ChunkIteratorFn = std::function<void(Chunk& chunk,
	const std::unordered_map<FragmentTypeID, uint32_t>& columnMap,
	size_t entityCount)>;

/// The MassEntitySubsystem is a standalone archetype-based ECS that operates
/// alongside the existing sparse-set ECSManager.  It is designed for mass-
/// simulation of >10K homogeneous entities (crowds, projectile swarms,
/// vegetation, etc.).
///
/// Key design:
///   - Entities are lightweight handles (index + generation)
///   - Archetype = unique set of fragment types
///   - Chunks store entities in SoA layout (~64KB each)
///   - Structural changes are deferred via MassCommandBuffer
///   - Processors iterate over matching archetype chunks
class MassEntitySubsystem
{
public:
	static MassEntitySubsystem& Instance();

	// ── Entity Lifecycle ─────────────────────────────────────────────

	/// Create a new entity with the given fragment types.
	/// Fragment values are default-initialised (zero).
	MassEntity createEntity(const ArchetypeSignature& signature);

	/// Create a batch of entities with the same signature.
	void createEntities(const ArchetypeSignature& signature, size_t count,
	                    std::vector<MassEntity>* outHandles = nullptr);

	/// Destroy an entity.  Deferred if called during processor execution.
	void destroyEntity(MassEntity entity);

	/// Check if an entity handle is still alive.
	bool isAlive(MassEntity entity) const;

	/// Total number of alive mass entities.
	size_t entityCount() const { return m_aliveCount; }

	// ── Fragment Access ──────────────────────────────────────────────

	/// Get typed fragment pointer for an entity.
	template<typename T>
	T* getFragment(MassEntity entity);

	template<typename T>
	const T* getFragment(MassEntity entity) const;

	/// Set fragment value for an entity.
	template<typename T>
	bool setFragment(MassEntity entity, const T& value);

	// ── Structural Changes ───────────────────────────────────────────

	/// Add a fragment type to an existing entity (moves it to a new archetype).
	template<typename T>
	void addFragment(MassEntity entity, const T& value = {});

	/// Remove a fragment type from an existing entity (moves it to a new archetype).
	template<typename T>
	void removeFragment(MassEntity entity);

	// ── Query / Iteration ────────────────────────────────────────────

	/// Iterate over all chunks matching the required signature.
	void forEachChunk(const ArchetypeSignature& required, ChunkIteratorFn fn);

	/// Iterate over all chunks matching required but excluding excluded.
	void forEachChunk(const ArchetypeSignature& required,
	                  const ArchetypeSignature& excluded,
	                  ChunkIteratorFn fn);

	// ── Processor Management ─────────────────────────────────────────

	/// Register a processor.  Ownership is NOT taken (caller manages lifetime).
	void registerProcessor(MassProcessor* processor);

	/// Unregister a processor.
	void unregisterProcessor(MassProcessor* processor);

	/// Execute all registered processors for the given delta time.
	void tick(float deltaTime);

	// ── Command Buffer ───────────────────────────────────────────────

	MassCommandBuffer& getCommandBuffer() { return m_commandBuffer; }

	/// Flush pending deferred commands.
	void flushCommands();

	// ── Lifecycle ────────────────────────────────────────────────────

	/// Clear all entities, archetypes, and processors.
	void clear();

	// ── Debug / Stats ────────────────────────────────────────────────

	size_t archetypeCount() const { return m_archetypes.size(); }

private:
	MassEntitySubsystem() = default;
	~MassEntitySubsystem() = default;
	MassEntitySubsystem(const MassEntitySubsystem&) = delete;
	MassEntitySubsystem& operator=(const MassEntitySubsystem&) = delete;

	// ── Archetype Graph ──────────────────────────────────────────────

	/// Find or create an archetype for the given signature.
	Archetype& getOrCreateArchetype(const ArchetypeSignature& sig);

	/// Move entity from current archetype to a new one (add/remove fragment).
	void moveEntity(MassEntity entity, const ArchetypeSignature& newSig);

	// ── Entity Slot Management ───────────────────────────────────────

	MassEntity allocateSlot();
	void       freeSlot(MassEntity entity);

	std::vector<MassEntitySlot>        m_slots;
	std::vector<uint32_t>              m_freeSlots;
	size_t                             m_aliveCount{ 0 };

	// ── Archetypes ───────────────────────────────────────────────────

	std::vector<std::unique_ptr<Archetype>> m_archetypes;

	/// Maps signature hash -> archetype index for O(1) lookup.
	struct SigHash
	{
		size_t operator()(const ArchetypeSignature& sig) const
		{
			return std::hash<std::string>()(sig.to_string());
		}
	};
	std::unordered_map<ArchetypeSignature, uint32_t, SigHash> m_archetypeMap;

	// ── Per-archetype entity tracking ────────────────────────────────
	/// Each archetype chunk needs to know which MassEntity occupies each row.
	/// We store parallel arrays of MassEntity handles per archetype.
	struct ArchetypeEntityMap
	{
		/// chunkEntities[chunkIndex][row] = MassEntity handle
		std::vector<std::vector<MassEntity>> chunkEntities;
	};
	std::vector<ArchetypeEntityMap>    m_archetypeEntities;

	// ── Processors ───────────────────────────────────────────────────

	std::vector<MassProcessor*>        m_processors;
	bool                               m_ticking{ false };

	// ── Command Buffer ───────────────────────────────────────────────

	MassCommandBuffer                  m_commandBuffer;
};

// ── Template Implementations ─────────────────────────────────────────

template<typename T>
T* MassEntitySubsystem::getFragment(MassEntity entity)
{
	if (!isAlive(entity)) return nullptr;
	const auto& slot = m_slots[entity.index];
	return m_archetypes[slot.archetypeIndex]->getFragment<T>(slot.chunkIndex, slot.rowIndex);
}

template<typename T>
const T* MassEntitySubsystem::getFragment(MassEntity entity) const
{
	if (!isAlive(entity)) return nullptr;
	const auto& slot = m_slots[entity.index];
	return m_archetypes[slot.archetypeIndex]->getFragment<T>(slot.chunkIndex, slot.rowIndex);
}

template<typename T>
bool MassEntitySubsystem::setFragment(MassEntity entity, const T& value)
{
	if (!isAlive(entity)) return false;
	const auto& slot = m_slots[entity.index];
	return m_archetypes[slot.archetypeIndex]->setFragment<T>(slot.chunkIndex, slot.rowIndex, value);
}

template<typename T>
void MassEntitySubsystem::addFragment(MassEntity entity, const T& value)
{
	if (!isAlive(entity)) return;
	const auto& slot = m_slots[entity.index];
	auto newSig = m_archetypes[slot.archetypeIndex]->signature();
	FragmentTypeID tid = getFragmentTypeID<T>();
	if (newSig.test(tid)) return; // already has it
	newSig.set(tid);

	// Ensure fragment is registered
	FragmentRegistry::Instance().registerFragment<T>();

	if (m_ticking)
	{
		// Defer structural change
		MassEntity captured = entity;
		T capturedVal = value;
		m_commandBuffer.enqueue([this, captured, newSig, capturedVal, tid]()
		{
			moveEntity(captured, newSig);
			// Set the new fragment value
			if (isAlive(captured))
			{
				const auto& s = m_slots[captured.index];
				m_archetypes[s.archetypeIndex]->setFragment<T>(s.chunkIndex, s.rowIndex, capturedVal);
			}
		});
	}
	else
	{
		moveEntity(entity, newSig);
		const auto& s = m_slots[entity.index];
		m_archetypes[s.archetypeIndex]->setFragment<T>(s.chunkIndex, s.rowIndex, value);
	}
}

template<typename T>
void MassEntitySubsystem::removeFragment(MassEntity entity)
{
	if (!isAlive(entity)) return;
	const auto& slot = m_slots[entity.index];
	auto newSig = m_archetypes[slot.archetypeIndex]->signature();
	FragmentTypeID tid = getFragmentTypeID<T>();
	if (!newSig.test(tid)) return; // doesn't have it
	newSig.reset(tid);

	if (m_ticking)
	{
		MassEntity captured = entity;
		m_commandBuffer.enqueue([this, captured, newSig]()
		{
			moveEntity(captured, newSig);
		});
	}
	else
	{
		moveEntity(entity, newSig);
	}
}

} // namespace Mass
