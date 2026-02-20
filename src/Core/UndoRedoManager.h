#pragma once

#include <functional>
#include <string>
#include <vector>

class UndoRedoManager
{
public:
    struct Command
    {
        std::string description;
        std::function<void()> execute;
        std::function<void()> undo;
    };

    static UndoRedoManager& Instance();

    // Push a command that has ALREADY been applied (old state captured before,
    // new state is the current state).  Clears the redo stack.
    void pushCommand(Command command);

    // Legacy helper – calls execute() then pushes.
    void executeCommand(Command command);

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void clear();

    size_t undoCount() const { return m_undoStack.size(); }
    size_t redoCount() const { return m_redoStack.size(); }
    const std::string& lastUndoDescription() const;
    const std::string& lastRedoDescription() const;

    // Fires after every push / undo / redo / clear so listeners can react
    // (e.g. mark the active level dirty, refresh status bar).
    void setOnChanged(std::function<void()> callback) { m_onChanged = std::move(callback); }

private:
    UndoRedoManager() = default;
    ~UndoRedoManager() = default;
    UndoRedoManager(const UndoRedoManager&) = delete;
    UndoRedoManager& operator=(const UndoRedoManager&) = delete;

    void notifyChanged();

    static constexpr size_t kMaxUndoDepth = 100;
    std::vector<Command> m_undoStack;
    std::vector<Command> m_redoStack;
    std::function<void()> m_onChanged;
    static const std::string s_empty;
};
