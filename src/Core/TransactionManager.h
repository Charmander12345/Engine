#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// TransactionManager – snapshot-based undo/redo transactions.
//
// Builds on top of UndoRedoManager: each completed transaction becomes
// a single UndoRedoManager::Command with before/after byte snapshots.
//
// Two recording modes:
//   1. recordSnapshot(ptr, size)  – raw memcpy, for stable addresses.
//   2. recordEntry(capture, restore) – lambda-based, for relocatable
//      data such as ECS components looked up by entity ID.
//
// Usage:
//   {
//       ScopedTransaction txn("Move actor");
//       txn.snapshot(transformPtr, sizeof(TransformComponent));
//       transformPtr->position[0] = newX;
//   }
//   // Ctrl+Z restores the old position
// ═══════════════════════════════════════════════════════════════════════

class TransactionManager
{
public:
    static TransactionManager& Instance();

    /// Begin a new transaction. Nested transactions are not supported.
    void beginTransaction(const char* description);

    /// End the current transaction – captures "after" state for every
    /// recorded entry and pushes a single command to UndoRedoManager.
    void endTransaction();

    /// True while between begin/end.
    bool isActive() const { return m_active; }

    // ── Recording helpers (call between begin/end) ──────────────────

    /// Record a raw memory snapshot.  The pointer must remain valid
    /// at least until endTransaction() captures the "after" state.
    /// On undo/redo the exact same pointer is written back – use only
    /// for memory that does NOT relocate (globals, stable allocations).
    void recordSnapshot(void* target, size_t size);

    /// Record via capture/restore lambdas – safe for relocatable data.
    ///   capture()     → called now (before) and at endTransaction (after).
    ///   restore(data) → called on undo or redo.
    void recordEntry(
        std::function<std::vector<uint8_t>()> capture,
        std::function<void(const std::vector<uint8_t>&)> restore);

    /// Add a callback that fires after every undo **or** redo of this
    /// transaction (e.g. physics invalidation, UI refresh).
    void addPostRestoreCallback(std::function<void()> callback);

private:
    TransactionManager() = default;
    ~TransactionManager() = default;
    TransactionManager(const TransactionManager&) = delete;
    TransactionManager& operator=(const TransactionManager&) = delete;

    struct Entry
    {
        std::function<std::vector<uint8_t>()> capture;
        std::function<void(const std::vector<uint8_t>&)> restore;
        std::vector<uint8_t> before;
    };

    bool                                    m_active{ false };
    std::string                             m_description;
    std::vector<Entry>                      m_entries;
    std::vector<std::function<void()>>      m_postRestoreCallbacks;
};

// ═══════════════════════════════════════════════════════════════════════
// ScopedTransaction – RAII guard.
//
// Constructor calls beginTransaction, destructor calls endTransaction.
// Provides convenience wrappers that delegate to TransactionManager.
// ═══════════════════════════════════════════════════════════════════════

class ScopedTransaction
{
public:
    explicit ScopedTransaction(const char* description);
    ~ScopedTransaction();

    ScopedTransaction(const ScopedTransaction&)            = delete;
    ScopedTransaction& operator=(const ScopedTransaction&) = delete;

    /// Record a raw memory snapshot (see TransactionManager::recordSnapshot).
    void snapshot(void* target, size_t size);

    /// Record via capture/restore lambdas (see TransactionManager::recordEntry).
    void entry(
        std::function<std::vector<uint8_t>()> capture,
        std::function<void(const std::vector<uint8_t>&)> restore);

    /// Add a post-undo/redo callback.
    void onRestore(std::function<void()> callback);
};
