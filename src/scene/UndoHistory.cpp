#include "UndoHistory.h"

#include <utility>

namespace Scene
{

void UndoHistory::recordApplied(EditCommand command)
{
    mUndoStack.push_back(std::move(command));
    mRedoStack.clear();
}

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

void UndoHistory::pushRedo(EditCommand command)
{
    mRedoStack.push_back(std::move(command));
}

void UndoHistory::pushUndo(EditCommand command)
{
    mUndoStack.push_back(std::move(command));
}

} // namespace Scene
