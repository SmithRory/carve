#pragma once

#include <vector>

#include "scene/EditCommand.h"

namespace Scene
{

/**
 * Encapsulates undo/redo command stacks for editor actions.
 */
class UndoHistory
{
public:
    /**
     * Records a command whose effect has already been applied.
     * @param[in] command Command to push onto undo stack.
     */
    void recordApplied(EditCommand command);

    /**
     * Pops one command from undo stack.
     * @param[out] outCommand Popped undo command.
     * @return True when a command was available.
     */
    bool popUndo(EditCommand &outCommand);
    /**
     * Pops one command from redo stack.
     * @param[out] outCommand Popped redo command.
     * @return True when a command was available.
     */
    bool popRedo(EditCommand &outCommand);

    /**
     * Pushes a command onto redo stack.
     * @param[in] command Command to push.
     */
    void pushRedo(EditCommand command);
    /**
     * Pushes a command onto undo stack.
     * @param[in] command Command to push.
     */
    void pushUndo(EditCommand command);

private:
    std::vector<EditCommand> mUndoStack;
    std::vector<EditCommand> mRedoStack;
};

} // namespace Scene
