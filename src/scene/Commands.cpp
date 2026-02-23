#include "Commands.h"
#include "common/VariantUtils.h"

namespace Scene {
namespace {
void restoreSelection(Document &document, ObjectId selectionId)
{
    if (selectionId == 0U) {
        document.clearSelection();
        return;
    }

    if (!document.selectObject(selectionId)) {
        document.clearSelection();
    }
}

/**
 * Creates an object and selects it.
 * @param[in,out] document Mutable document state.
 * @param[in] typedCommand Create payload.
 * @return True when document state changed.
 */
bool createObject(Document &document, const CreateObjectCommand &typedCommand)
{
    document.addObject(typedCommand.object, typedCommand.index);
    document.selectObject(typedCommand.object.id);
    return true;
}

/**
 * Deletes an object while preserving remaining selection state.
 * @param[in,out] document Mutable document state.
 * @param[in] typedCommand Delete payload.
 * @return True when document state changed.
 */
bool deleteObject(Document &document, const DeleteObjectCommand &typedCommand)
{
    std::size_t removedIndex = 0U;
    return document.removeObject(typedCommand.object.id, removedIndex).has_value();
}

/**
 * Applies object translation.
 * @param[in,out] document Mutable document state.
 * @param[in] typedCommand Translate payload.
 * @return True when document state changed.
 */
bool translateObject(Document &document, const TranslateObjectCommand &typedCommand)
{
    EditableObject *object = document.findObject(typedCommand.objectId);
    if (object == nullptr) {
        return false;
    }

    object->position = typedCommand.after;
    return true;
}

/**
 * Reverts object creation by removing the created object.
 * @param[in,out] document Mutable document state.
 * @param[in] typedCommand Create payload.
 * @return True when document state changed.
 */
bool undoCreateObject(Document &document, const CreateObjectCommand &typedCommand)
{
    std::size_t removedIndex = 0U;
    if (!document.removeObject(typedCommand.object.id, removedIndex).has_value()) {
        return false;
    }

    restoreSelection(document, typedCommand.previousSelection);
    return true;
}

/**
 * Reverts object deletion by restoring the deleted object.
 * @param[in,out] document Mutable document state.
 * @param[in] typedCommand Delete payload.
 * @return True when document state changed.
 */
bool undoDeleteObject(Document &document, const DeleteObjectCommand &typedCommand)
{
    document.addObject(typedCommand.object, typedCommand.index);
    restoreSelection(document, typedCommand.previousSelection);
    return true;
}

/**
 * Reverts object translation to the previous position.
 * @param[in,out] document Mutable document state.
 * @param[in] typedCommand Translate payload.
 * @return True when document state changed.
 */
bool undoTranslateObject(Document &document, const TranslateObjectCommand &typedCommand)
{
    EditableObject *object = document.findObject(typedCommand.objectId);
    if (object == nullptr) {
        return false;
    }

    object->position = typedCommand.before;
    return true;
}
} // namespace

/**
 * Applies a command in forward direction.
 * @param[in,out] document Mutable document state.
 * @param[in] command Command to apply.
 * @return True when document state changed.
 */
bool applyCommand(Document &document, const EditCommand &command)
{
    return std::visit(
        Util::overloads{
            [&document](const CreateObjectCommand &typedCommand) { return createObject(document, typedCommand); },
            [&document](const DeleteObjectCommand &typedCommand) { return deleteObject(document, typedCommand); },
            [&document](const TranslateObjectCommand &typedCommand) { return translateObject(document, typedCommand); } },
        command);
}

/**
 * Applies a command in reverse direction.
 * @param[in,out] document Mutable document state.
 * @param[in] command Command to reverse.
 * @return True when document state changed.
 */
bool undoCommand(Document &document, const EditCommand &command)
{
    return std::visit(
        Util::overloads{
            [&document](const CreateObjectCommand &typedCommand) { return undoCreateObject(document, typedCommand); },
            [&document](const DeleteObjectCommand &typedCommand) { return undoDeleteObject(document, typedCommand); },
            [&document](const TranslateObjectCommand &typedCommand) { return undoTranslateObject(document, typedCommand); } },
        command);
}

} // namespace Scene
