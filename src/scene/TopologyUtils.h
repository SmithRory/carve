#pragma once

#include <array>
#include <cstddef>
#include <ranges>
#include <utility>

#include "scene/Types.h"

namespace Scene
{
/**
 * Visits each triangle produced by fan-triangulating a polygon face.
 * The first face vertex is used as the fan anchor.
 * @param[in] face Polygon face indices.
 * @param[in] callback Callback receiving std::array<TopologyIndex, 3>.
 */
template <typename Fn>
void forEachFaceTriangle(const Face &face, Fn &&callback)
{
    if (face.size() >= 3U)
    {
        const TopologyIndex base = face.front();
        for (const std::size_t i : std::views::iota(std::size_t{ 1U }, face.size() - 1U))
        {
            std::forward<Fn>(callback)(std::array<TopologyIndex, 3>{ base, face[i], face[i + 1U] });
        }
    }
}
} // namespace Scene
