#pragma once

#include <optional>

#include "scene/Document.h"
#include "scene/Types.h"

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

std::optional<ObjectId> selectObjectFromScreen(
    const Document &document,
    const CameraParameters &cameraParameters,
    const MousePosition &mousePosition,
    float viewportWidth,
    float viewportHeight);

std::optional<ComponentSelection> selectComponentFromScreen(
    const EditableObject &object,
    const CameraParameters &cameraParameters,
    const MousePosition &mousePosition,
    float viewportWidth,
    float viewportHeight);

} // namespace Scene
