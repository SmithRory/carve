#pragma once

#include "scene/Types.h"

namespace Scene
{

/**
 * Builds renderable mesh buffers from an immutable build ticket.
 * @param[in] ticket Immutable build input payload.
 * @return Built mesh data tagged with ticket revision.
 */
BuiltMeshData buildMeshFromTicket(const BuildTicket &ticket);

} // namespace Scene
