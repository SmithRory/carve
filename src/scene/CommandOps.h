#pragma once

#include "scene/Document.h"
#include "scene/EditCommand.h"

namespace Scene
{

/**
 * Applies a command in forward direction.
 * @param[in,out] document Mutable document state.
 * @param[in] command Command to apply.
 * @return True when document state changed.
 */
bool applyCommandForward(Document &document, const EditCommand &command);
/**
 * Applies a command in reverse direction.
 * @param[in,out] document Mutable document state.
 * @param[in] command Command to reverse.
 * @return True when document state changed.
 */
bool applyCommandBackward(Document &document, const EditCommand &command);

} // namespace Scene
