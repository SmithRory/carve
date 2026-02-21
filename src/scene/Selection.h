#pragma once

#include <optional>

#include "scene/Document.h"

namespace Scene
{
enum class ComponentType : uint8_t
{
    Vertex,
    Edge,
    Face,
};

struct ComponentSelection
{
    ComponentType type{};
    uint16_t index{};
};

/**
 * Performs screen-space ray selection against object bounds.
 * @param[in] document Document containing selection candidates.
 * @param[in] cameraPosition Camera position in world space.
 * @param[in] cameraYawRadians Camera yaw in radians.
 * @param[in] cameraPitchRadians Camera pitch in radians.
 * @param[in] mouseX Cursor x position in pixels.
 * @param[in] mouseY Cursor y position in pixels.
 * @param[in] viewportWidth Viewport width in pixels.
 * @param[in] viewportHeight Viewport height in pixels.
 * @return Hit object id when an object is under the cursor, otherwise std::nullopt.
 */
std::optional<ObjectId> selectObjectFromScreen(
    const Document &document,
    const bx::Vec3 &cameraPosition,
    float cameraYawRadians,
    float cameraPitchRadians,
    float mouseX,
    float mouseY,
    float viewportWidth,
    float viewportHeight);

std::optional<ComponentSelection> selectComponentFromScreen(
    const EditableObject &object,
    const bx::Vec3 &cameraPosition,
    float cameraYawRadians,
    float cameraPitchRadians,
    float mouseX,
    float mouseY,
    float viewportWidth,
    float viewportHeight);

} // namespace Scene
