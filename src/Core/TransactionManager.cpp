#include "TransactionManager.h"
#include "UndoRedoManager.h"

#include <cassert>
#include <memory>

// ═══════════════════════════════════════════════════════════════════════
// TransactionManager
// ═══════════════════════════════════════════════════════════════════════

TransactionManager& TransactionManager::Instance()
{
    static TransactionManager instance;
    return instance;
}

void TransactionManager::beginTransaction(const char* description)
{
    assert(!m_active && "Nested transactions are not supported");
    m_active = true;
    m_description = description;
    m_entries.clear();
    m_postRestoreCallbacks.clear();
}

void TransactionManager::recordSnapshot(void* target, size_t size)
{
    assert(m_active && "recordSnapshot called outside a transaction");

    Entry entry;
    entry.capture = [target, size]() -> std::vector<uint8_t> {
        std::vector<uint8_t> data(size);
        std::memcpy(data.data(), target, size);
        return data;
    };
    entry.restore = [target, size](const std::vector<uint8_t>& data) {
        std::memcpy(target, data.data(), size);
    };
    entry.before = entry.capture();
    m_entries.push_back(std::move(entry));
}

void TransactionManager::recordEntry(
    std::function<std::vector<uint8_t>()> capture,
    std::function<void(const std::vector<uint8_t>&)> restore)
{
    assert(m_active && "recordEntry called outside a transaction");

    Entry entry;
    entry.capture = std::move(capture);
    entry.restore = std::move(restore);
    entry.before = entry.capture();
    m_entries.push_back(std::move(entry));
}

void TransactionManager::addPostRestoreCallback(std::function<void()> callback)
{
    assert(m_active && "addPostRestoreCallback called outside a transaction");
    m_postRestoreCallbacks.push_back(std::move(callback));
}

void TransactionManager::endTransaction()
{
    assert(m_active && "endTransaction called without beginTransaction");
    m_active = false;

    if (m_entries.empty())
    {
        m_postRestoreCallbacks.clear();
        return;
    }

    // Build committed data: capture "after" state for every entry.
    struct Committed
    {
        std::function<void(const std::vector<uint8_t>&)> restore;
        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
    };

    auto committed = std::make_shared<std::vector<Committed>>();
    committed->reserve(m_entries.size());

    for (auto& e : m_entries)
    {
        Committed c;
        c.restore = std::move(e.restore);
        c.before  = std::move(e.before);
        c.after   = e.capture();
        committed->push_back(std::move(c));
    }

    auto callbacks = std::make_shared<std::vector<std::function<void()>>>(
        std::move(m_postRestoreCallbacks));

    std::string desc = std::move(m_description);

    m_entries.clear();
    m_postRestoreCallbacks.clear();

    // Push a single command to the existing UndoRedoManager.
    UndoRedoManager::Instance().pushCommand({
        std::move(desc),
        // execute (redo) – restore "after" snapshots
        [committed, callbacks]() {
            for (auto& c : *committed)
                c.restore(c.after);
            for (auto& cb : *callbacks)
                cb();
        },
        // undo – restore "before" snapshots
        [committed, callbacks]() {
            for (auto& c : *committed)
                c.restore(c.before);
            for (auto& cb : *callbacks)
                cb();
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════
// ScopedTransaction
// ═══════════════════════════════════════════════════════════════════════

ScopedTransaction::ScopedTransaction(const char* description)
{
    TransactionManager::Instance().beginTransaction(description);
}

ScopedTransaction::~ScopedTransaction()
{
    TransactionManager::Instance().endTransaction();
}

void ScopedTransaction::snapshot(void* target, size_t size)
{
    TransactionManager::Instance().recordSnapshot(target, size);
}

void ScopedTransaction::entry(
    std::function<std::vector<uint8_t>()> capture,
    std::function<void(const std::vector<uint8_t>&)> restore)
{
    TransactionManager::Instance().recordEntry(std::move(capture), std::move(restore));
}

void ScopedTransaction::onRestore(std::function<void()> callback)
{
    TransactionManager::Instance().addPostRestoreCallback(std::move(callback));
}
