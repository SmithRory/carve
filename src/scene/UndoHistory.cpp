#include "UndoHistory.h"

#include <utility>

namespace Scene
{

/**
 * Records a command whose effect has already been applied.
 * @param[in] command Command to push onto undo stack.
 */
void UndoHistory::recordApplied(EditCommand command)
{
    mUndoStack.push_back(std::move(command));
    mRedoStack.clear();
}

/**
 * Pops one command from undo stack.
 * @param[out] outCommand Popped undo command.
 * @return True when a command was available.
 */
bool UndoHistory::popUndo(EditCommand &outCommand)
{
    if (mUndoStack.empty())
    {
        return false;
    }

    outCommand = mUndoStack.back();
    mUndoStack.pop_back();
    return true;
}

/**
 * Pops one command from redo stack.
 * @param[out] outCommand Popped redo command.
 * @return True when a command was available.
 */
bool UndoHistory::popRedo(EditCommand &outCommand)
{
    if (mRedoStack.empty())
    {
        return false;
    }

    outCommand = mRedoStack.back();
    mRedoStack.pop_back();
    return true;
}

/**
 * Pushes a command onto redo stack.
 * @param[in] command Command to push.
 */
void UndoHistory::pushRedo(EditCommand command)
{
    mRedoStack.push_back(std::move(command));
}

/**
 * Pushes a command onto undo stack.
 * @param[in] command Command to push.
 */
void UndoHistory::pushUndo(EditCommand command)
{
    mUndoStack.push_back(std::move(command));
}

} // namespace Scene
