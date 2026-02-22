#include "CommandOps.h"

#include <ranges>
#include <type_traits>

namespace Scene
{
namespace
{
void restoreSelection(Document &document, ObjectId selectionId)
{
    if (selectionId == 0U)
    {
        document.clearSelection();
        return;
    }

    if (!document.selectObject(selectionId))
    {
        document.clearSelection();
    }
}
} // namespace

/**
 * Applies a command in forward direction.
 * @param[in,out] document Mutable document state.
 * @param[in] command Command to apply.
 * @return True when document state changed.
 */
bool applyCommandForward(Document &document, const EditCommand &command)
{
    return std::visit(
        [&document](const auto &typedCommand) -> bool
    {
        using T = std::decay_t<decltype(typedCommand)>;

        if constexpr (std::is_same_v<T, CreateObjectCommand>)
        {
            document.addObject(typedCommand.object, typedCommand.index);
            document.selectObject(typedCommand.object.id);
            return true;
        }
        else if constexpr (std::is_same_v<T, DeleteObjectCommand>)
        {
            std::size_t removedIndex = 0U;
            if (!document.removeObject(typedCommand.object.id, removedIndex).has_value())
            {
                return false;
            }

            const auto &objects = document.objects();
            if (objects.empty())
            {
                document.clearSelection();
                return true;
            }

            const std::size_t fallbackIndex = typedCommand.index < objects.size() ? typedCommand.index : (objects.size() - 1U);
            document.selectObject(objects[fallbackIndex].id);
            return true;
        }
        else if constexpr (std::is_same_v<T, ExtrudeObjectCommand>)
        {
            EditableObject *object = document.findObject(typedCommand.objectId);
            if (object == nullptr)
            {
                return false;
            }

            if (typedCommand.vertexIndices.size() != typedCommand.afterY.size())
            {
                return false;
            }

            for (const std::size_t idx : std::views::iota(std::size_t{ 0U }, typedCommand.vertexIndices.size()))
            {
                const uint16_t vertexIndex = typedCommand.vertexIndices[idx];
                if (vertexIndex >= object->localVertices.size())
                {
                    return false;
                }

                object->localVertices[vertexIndex].y = typedCommand.afterY[idx];
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, TranslateObjectCommand>)
        {
            EditableObject *object = document.findObject(typedCommand.objectId);
            if (object == nullptr)
            {
                return false;
            }

            object->position = typedCommand.after;
            return true;
        }

        return false;
    },
        command);
}

/**
 * Applies a command in reverse direction.
 * @param[in,out] document Mutable document state.
 * @param[in] command Command to reverse.
 * @return True when document state changed.
 */
bool applyCommandBackward(Document &document, const EditCommand &command)
{
    return std::visit(
        [&document](const auto &typedCommand) -> bool
    {
        using T = std::decay_t<decltype(typedCommand)>;

        if constexpr (std::is_same_v<T, CreateObjectCommand>)
        {
            std::size_t removedIndex = 0U;
            if (!document.removeObject(typedCommand.object.id, removedIndex).has_value())
            {
                return false;
            }

            restoreSelection(document, typedCommand.previousSelection);
            return true;
        }
        else if constexpr (std::is_same_v<T, DeleteObjectCommand>)
        {
            document.addObject(typedCommand.object, typedCommand.index);
            restoreSelection(document, typedCommand.previousSelection);
            return true;
        }
        else if constexpr (std::is_same_v<T, ExtrudeObjectCommand>)
        {
            EditableObject *object = document.findObject(typedCommand.objectId);
            if (object == nullptr)
            {
                return false;
            }

            if (typedCommand.vertexIndices.size() != typedCommand.beforeY.size())
            {
                return false;
            }

            for (const std::size_t idx : std::views::iota(std::size_t{ 0U }, typedCommand.vertexIndices.size()))
            {
                const uint16_t vertexIndex = typedCommand.vertexIndices[idx];
                if (vertexIndex >= object->localVertices.size())
                {
                    return false;
                }

                object->localVertices[vertexIndex].y = typedCommand.beforeY[idx];
            }
            return true;
        }
        else if constexpr (std::is_same_v<T, TranslateObjectCommand>)
        {
            EditableObject *object = document.findObject(typedCommand.objectId);
            if (object == nullptr)
            {
                return false;
            }

            object->position = typedCommand.before;
            return true;
        }

        return false;
    },
        command);
}

} // namespace Scene
