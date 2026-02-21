#include "CommandOps.h"

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

            for (std::size_t i = 0U; i < typedCommand.vertexIndices.size(); ++i)
            {
                const uint16_t vertexIndex = typedCommand.vertexIndices[i];
                if (vertexIndex >= object->localVertices.size())
                {
                    return false;
                }

                object->localVertices[vertexIndex].y = typedCommand.afterY[i];
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

            for (std::size_t i = 0U; i < typedCommand.vertexIndices.size(); ++i)
            {
                const uint16_t vertexIndex = typedCommand.vertexIndices[i];
                if (vertexIndex >= object->localVertices.size())
                {
                    return false;
                }

                object->localVertices[vertexIndex].y = typedCommand.beforeY[i];
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
