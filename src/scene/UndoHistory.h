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
    void recordApplied(EditCommand command);
    bool popUndo(EditCommand &outCommand);
    bool popRedo(EditCommand &outCommand);
    void pushRedo(EditCommand command);
    void pushUndo(EditCommand command);

private:
    std::vector<EditCommand> mUndoStack;
    std::vector<EditCommand> mRedoStack;
};

} // namespace Scene
