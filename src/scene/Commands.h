#pragma once

#include "scene/Document.h"
#include "scene/EditCommand.h"

namespace Scene {

bool applyCommand(Document &document, const EditCommand &command);
bool undoCommand(Document &document, const EditCommand &command);

} // namespace Scene
