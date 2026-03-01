#include "UndoRedoManager.h"

const std::string UndoRedoManager::s_empty;

UndoRedoManager& UndoRedoManager::Instance()
{
    static UndoRedoManager instance;
    return instance;
}

void UndoRedoManager::pushCommand(Command command)
{
    m_undoStack.push_back(std::move(command));
    if (m_undoStack.size() > kMaxUndoDepth)
    {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
    notifyChanged();
}

void UndoRedoManager::executeCommand(Command command)
{
    command.execute();
    pushCommand(std::move(command));
}

bool UndoRedoManager::canUndo() const
{
    return !m_undoStack.empty();
}

bool UndoRedoManager::canRedo() const
{
    return !m_redoStack.empty();
}

void UndoRedoManager::undo()
{
    if (m_undoStack.empty())
    {
        return;
    }
    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    cmd.undo();
    m_redoStack.push_back(std::move(cmd));
    notifyChanged();
}

void UndoRedoManager::redo()
{
    if (m_redoStack.empty())
    {
        return;
    }
    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    cmd.execute();
    m_undoStack.push_back(std::move(cmd));
    notifyChanged();
}

void UndoRedoManager::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
}

const std::string& UndoRedoManager::lastUndoDescription() const
{
    return m_undoStack.empty() ? s_empty : m_undoStack.back().description;
}

const std::string& UndoRedoManager::lastRedoDescription() const
{
    return m_redoStack.empty() ? s_empty : m_redoStack.back().description;
}

void UndoRedoManager::notifyChanged()
{
    if (m_onChanged)
    {
        m_onChanged();
    }
}
