#pragma once

#include <bx/math.h>
#include <cstddef>
#include <variant>

#include "scene/Document.h"

namespace Scene
{

/**
 * Command payload for object creation.
 */
struct CreateObjectCommand
{
    EditableObject object;
    std::size_t index;
    ObjectId previousSelection;
};

/**
 * Command payload for object deletion.
 */
struct DeleteObjectCommand
{
    EditableObject object;
    std::size_t index;
    ObjectId previousSelection;
};

/**
 * Command payload for object translation.
 */
struct TranslateObjectCommand
{
    ObjectId objectId;
    bx::Vec3 before;
    bx::Vec3 after;
};

/**
 * Sum type for all undoable edit commands.
 */
using EditCommand = std::variant<CreateObjectCommand, DeleteObjectCommand, TranslateObjectCommand>;

} // namespace Scene
