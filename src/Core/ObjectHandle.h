#pragma once

#include <cstdint>
#include <vector>
#include <cassert>

/// A safe, weak handle to a managed object (Actor, Component, etc.).
/// Uses index + generation to detect stale references without dangling pointers.
/// Inspired by Unreal Engine's FWeakObjectPtr / GUObjectArray design.
///
/// Size: 8 bytes (fits in a register on x64).
struct ObjectHandle
{
	uint32_t index{ 0 };       // Slot index in the global array
	uint32_t generation{ 0 };  // Must match slot's generation to be valid

	bool operator==(ObjectHandle other) const { return index == other.index && generation == other.generation; }
	bool operator!=(ObjectHandle other) const { return !(*this == other); }

	bool isNull() const { return index == 0 && generation == 0; }

	static const ObjectHandle Null;
};

inline const ObjectHandle ObjectHandle::Null{ 0, 0 };

/// Hash support for ObjectHandle.
namespace std {
template<>
struct hash<ObjectHandle> {
	size_t operator()(ObjectHandle h) const noexcept {
		return std::hash<uint64_t>{}(
			(static_cast<uint64_t>(h.index) << 32) | h.generation);
	}
};
}

/// A single slot in the ObjectSlotArray.
struct ObjectSlot
{
	void*    ptr{ nullptr };   // Pointer to the managed object (nullptr if free)
	uint32_t generation{ 1 }; // Incremented on each reuse – starts at 1 so gen 0 is always invalid
	uint32_t flags{ 0 };      // Reserved for future use (GC flags, root-set, etc.)
};

/// Flag bits for ObjectSlot::flags.
enum class EObjectFlags : uint32_t
{
	None         = 0,
	RootSet      = 1 << 0,  // Prevents garbage collection
	PendingKill  = 1 << 1,  // Marked for deferred destruction
	Transactional= 1 << 2,  // Participates in undo/redo
};

/// Chunked global object array for O(1) access, no full-array reallocation,
/// and generation-safe weak handles.
///
/// Design mirrors UE's GUObjectArray at a smaller scale:
/// - Chunked: grows without moving existing pointers.
/// - Free-list: reuses slots after destruction.
/// - Generation counter: detects stale handles.
class ObjectSlotArray
{
public:
	static constexpr uint32_t CHUNK_SIZE = 4096;

	static ObjectSlotArray& Instance()
	{
		static ObjectSlotArray instance;
		return instance;
	}

	/// Allocate a slot for an object.  Returns a valid ObjectHandle.
	ObjectHandle allocate(void* ptr)
	{
		uint32_t slotIndex;

		if (!m_freeList.empty())
		{
			slotIndex = m_freeList.back();
			m_freeList.pop_back();
		}
		else
		{
			slotIndex = m_nextIndex++;
			ensureCapacity(slotIndex);
		}

		ObjectSlot& slot = getSlot(slotIndex);
		slot.ptr   = ptr;
		slot.flags = 0;
		// generation was already incremented on free (or starts at 1)

		return ObjectHandle{ slotIndex, slot.generation };
	}

	/// Release a slot (object destroyed).  Increments generation.
	void release(ObjectHandle handle)
	{
		if (!isValid(handle))
			return;

		ObjectSlot& slot = getSlot(handle.index);
		slot.ptr = nullptr;
		slot.flags = 0;
		slot.generation++;  // Invalidates all existing handles to this slot
		m_freeList.push_back(handle.index);
	}

	/// Resolve a handle to a raw pointer.  Returns nullptr if stale.
	void* resolve(ObjectHandle handle) const
	{
		if (handle.isNull())
			return nullptr;
		if (handle.index >= m_nextIndex)
			return nullptr;

		const ObjectSlot& slot = getSlotConst(handle.index);
		if (slot.generation != handle.generation)
			return nullptr;
		return slot.ptr;
	}

	/// Check if a handle still refers to a live object.
	bool isValid(ObjectHandle handle) const
	{
		return resolve(handle) != nullptr;
	}

	/// Get the slot for direct flag manipulation (unsafe – caller must verify handle).
	ObjectSlot& getSlot(uint32_t index)
	{
		uint32_t chunk = index / CHUNK_SIZE;
		uint32_t within = index % CHUNK_SIZE;
		return m_chunks[chunk][within];
	}

	const ObjectSlot& getSlotConst(uint32_t index) const
	{
		uint32_t chunk = index / CHUNK_SIZE;
		uint32_t within = index % CHUNK_SIZE;
		return m_chunks[chunk][within];
	}

	/// Total allocated slots (including free).
	uint32_t capacity() const { return m_nextIndex; }

	/// Clear everything (shutdown only).
	void clear()
	{
		for (auto* chunk : m_chunks)
			delete[] chunk;
		m_chunks.clear();
		m_freeList.clear();
		m_nextIndex = 1;  // slot 0 is reserved (null handle)
		ensureCapacity(0);
	}

private:
	ObjectSlotArray()
	{
		// Reserve slot 0 as "null" – never allocated to a real object
		ensureCapacity(0);
		m_nextIndex = 1;
	}

	~ObjectSlotArray()
	{
		for (auto* chunk : m_chunks)
			delete[] chunk;
	}

	ObjectSlotArray(const ObjectSlotArray&) = delete;
	ObjectSlotArray& operator=(const ObjectSlotArray&) = delete;

	void ensureCapacity(uint32_t index)
	{
		uint32_t chunkNeeded = index / CHUNK_SIZE;
		while (m_chunks.size() <= chunkNeeded)
		{
			auto* chunk = new ObjectSlot[CHUNK_SIZE];
			m_chunks.push_back(chunk);
		}
	}

	std::vector<ObjectSlot*> m_chunks;
	std::vector<uint32_t>    m_freeList;
	uint32_t                 m_nextIndex{ 1 };
};

/// Typed wrapper around ObjectHandle for type-safe resolution.
template<typename T>
struct TObjectHandle
{
	ObjectHandle handle;

	TObjectHandle() = default;
	explicit TObjectHandle(ObjectHandle h) : handle(h) {}

	T* get() const { return static_cast<T*>(ObjectSlotArray::Instance().resolve(handle)); }
	bool isValid() const { return ObjectSlotArray::Instance().isValid(handle); }
	bool isNull() const { return handle.isNull(); }

	T* operator->() const { return get(); }
	T& operator*() const { return *get(); }
	explicit operator bool() const { return isValid(); }

	bool operator==(TObjectHandle other) const { return handle == other.handle; }
	bool operator!=(TObjectHandle other) const { return handle != other.handle; }
};
