#pragma once

#include "scene/Document.h"
#include "scene/EditCommand.h"

namespace Scene
{

bool applyCommandForward(Document &document, const EditCommand &command);
bool applyCommandBackward(Document &document, const EditCommand &command);

} // namespace Scene
