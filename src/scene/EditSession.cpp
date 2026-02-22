#include "EditSession.h"

#include <bx/math.h>
#include <limits>
#include <ranges>

namespace
{
constexpr float PIXEL_DEADZONE = 0.001F;
constexpr float EXTRUDE_PER_PIXEL = 0.01F;
constexpr float EXTRUDE_MIN_WORLD_Y = -10000.0F;
constexpr float EXTRUDE_MAX_WORLD_Y = 10000.0F;
constexpr float EXTRUDE_CHANGE_EPSILON = 0.00001F;
constexpr float TOP_VERTEX_EPSILON = 0.0001F;

constexpr float TRANSLATE_WORLD_UNITS_PER_PIXEL = 0.02F;
constexpr float TRANSLATE_CHANGE_EPSILON = 0.00001F;
} // namespace

namespace Scene
{

/**
 * Starts an extrusion drag on the selected object.
 * @param[in] document Current document state.
 * @param[in] mouseY Cursor y position in pixels.
 */
void EditSession::beginExtrude(const Document &document, float mouseY)
{
    const EditableObject *object = document.selectedObject();
    if (object == nullptr)
    {
        return;
    }

    mExtrudeDrag.active = true;
    mExtrudeDrag.objectId = object->id;
    mExtrudeDrag.previousMouseY = mouseY;
    mExtrudeDrag.vertexIndices.clear();
    mExtrudeDrag.beforeY.clear();

    if (object->localVertices.empty())
    {
        mExtrudeDrag.active = false;
        return;
    }

    /* Extrude currently targets the top ring (all vertices near max local Y). */
    float maxY = object->localVertices.front().y;
    for (const bx::Vec3 &vertex : object->localVertices)
    {
        maxY = bx::max(maxY, vertex.y);
    }

    const std::size_t maxTopologyIndex = static_cast<std::size_t>(std::numeric_limits<uint16_t>::max());
    const std::size_t vertexCount = bx::min(object->localVertices.size(), maxTopologyIndex + 1U);
    for (const std::size_t i : std::views::iota(std::size_t{ 0U }, vertexCount))
    {
        const bx::Vec3 &vertex = object->localVertices[i];
        if (bx::abs(vertex.y - maxY) <= TOP_VERTEX_EPSILON)
        {
            mExtrudeDrag.vertexIndices.push_back(static_cast<uint16_t>(i));
            mExtrudeDrag.beforeY.push_back(vertex.y);
        }
    }

    if (mExtrudeDrag.vertexIndices.empty())
    {
        mExtrudeDrag.active = false;
    }
}

/**
 * Applies one extrusion drag update. Returns true when object changed.
 * @param[in,out] document Mutable document state.
 * @param[in] mouseY Cursor y position in pixels.
 * @return True when extrusion changed object state.
 */
bool EditSession::updateExtrude(Document &document, float mouseY)
{
    if (!mExtrudeDrag.active)
    {
        return false;
    }

    EditableObject *object = document.findObject(mExtrudeDrag.objectId);
    if (object == nullptr)
    {
        mExtrudeDrag = ExtrudeDrag{};
        return false;
    }

    const float pixelDelta = mExtrudeDrag.previousMouseY - mouseY;
    mExtrudeDrag.previousMouseY = mouseY;
    if (bx::abs(pixelDelta) < PIXEL_DEADZONE)
    {
        return false;
    }

    if (mExtrudeDrag.vertexIndices.empty())
    {
        return false;
    }

    const float delta = pixelDelta * EXTRUDE_PER_PIXEL;
    bool changed = false;

    /* Apply drag delta uniformly to captured top vertices for predictable prism-like extrusion. */
    for (const uint16_t vertexIndex : mExtrudeDrag.vertexIndices)
    {
        if (vertexIndex >= object->localVertices.size())
        {
            continue;
        }

        const float previous = object->localVertices[vertexIndex].y;
        object->localVertices[vertexIndex].y = bx::clamp(previous + delta, EXTRUDE_MIN_WORLD_Y, EXTRUDE_MAX_WORLD_Y);
        changed = changed || (bx::abs(previous - object->localVertices[vertexIndex].y) > EXTRUDE_CHANGE_EPSILON);
    }

    return changed;
}

/**
 * Ends extrusion drag and returns a command when changed.
 * @param[in] document Current document state.
 * @return Undo command when extrusion changed object state.
 */
std::optional<EditCommand> EditSession::endExtrude(const Document &document)
{
    if (!mExtrudeDrag.active)
    {
        return std::nullopt;
    }

    std::optional<EditCommand> command;
    const EditableObject *object = document.findObject(mExtrudeDrag.objectId);
    if (object != nullptr && !mExtrudeDrag.vertexIndices.empty())
    {
        std::vector<float> afterY;
        afterY.reserve(mExtrudeDrag.vertexIndices.size());
        bool changed = false;
        const std::size_t pairedCount = bx::min(mExtrudeDrag.vertexIndices.size(), mExtrudeDrag.beforeY.size());
        for (const std::size_t idx : std::views::iota(std::size_t{ 0U }, pairedCount))
        {
            const uint16_t vertexIndex = mExtrudeDrag.vertexIndices[idx];
            const float beforeY = mExtrudeDrag.beforeY[idx];
            if (vertexIndex >= object->localVertices.size())
            {
                continue;
            }

            const float value = object->localVertices[vertexIndex].y;
            afterY.push_back(value);
            changed = changed || (bx::abs(value - beforeY) > EXTRUDE_CHANGE_EPSILON);
        }

        if (changed && afterY.size() == mExtrudeDrag.beforeY.size())
        {
            command = ExtrudeObjectCommand{
                .objectId = object->id,
                .vertexIndices = mExtrudeDrag.vertexIndices,
                .beforeY = mExtrudeDrag.beforeY,
                .afterY = std::move(afterY),
            };
        }
    }

    mExtrudeDrag = ExtrudeDrag{};
    return command;
}

/**
 * Starts a translation drag on the selected object.
 * @param[in] document Current document state.
 * @param[in] mousePosition Cursor position in pixels.
 */
void EditSession::beginTranslate(const Document &document, const MousePosition &mousePosition)
{
    const EditableObject *object = document.selectedObject();
    if (object == nullptr)
    {
        return;
    }

    mTranslateDrag.active = true;
    mTranslateDrag.objectId = object->id;
    mTranslateDrag.beforePosition = object->position;
    mTranslateDrag.previousMouseX = mousePosition.x;
    mTranslateDrag.previousMouseY = mousePosition.y;
}

/**
 * Applies one translation drag update. Returns true when object changed.
 * @param[in,out] document Mutable document state.
 * @param[in] cameraYawRadians Camera yaw in radians.
 * @param[in] mousePosition Cursor position in pixels.
 * @return True when translation changed object state.
 */
bool EditSession::updateTranslate(Document &document, float cameraYawRadians, const MousePosition &mousePosition)
{
    if (!mTranslateDrag.active)
    {
        return false;
    }

    EditableObject *object = document.findObject(mTranslateDrag.objectId);
    if (object == nullptr)
    {
        mTranslateDrag = TranslateDrag{};
        return false;
    }

    const float deltaX = mousePosition.x - mTranslateDrag.previousMouseX;
    const float deltaY = mousePosition.y - mTranslateDrag.previousMouseY;
    mTranslateDrag.previousMouseX = mousePosition.x;
    mTranslateDrag.previousMouseY = mousePosition.y;
    if (bx::abs(deltaX) < PIXEL_DEADZONE && bx::abs(deltaY) < PIXEL_DEADZONE)
    {
        return false;
    }

    const bx::Vec3 right(
        bx::cos(cameraYawRadians),
        0.0F,
        -bx::sin(cameraYawRadians));
    const bx::Vec3 forwardFlat(
        bx::sin(cameraYawRadians),
        0.0F,
        bx::cos(cameraYawRadians));

    const bx::Vec3 moveX = bx::mul(right, deltaX * TRANSLATE_WORLD_UNITS_PER_PIXEL);
    const bx::Vec3 moveY = bx::mul(forwardFlat, -deltaY * TRANSLATE_WORLD_UNITS_PER_PIXEL);
    object->position = bx::add(object->position, bx::add(moveX, moveY));
    return true;
}

/**
 * Ends translation drag and returns a command when changed.
 * @param[in] document Current document state.
 * @return Undo command when translation changed object state.
 */
std::optional<EditCommand> EditSession::endTranslate(const Document &document)
{
    if (!mTranslateDrag.active)
    {
        return std::nullopt;
    }

    std::optional<EditCommand> command;
    const EditableObject *object = document.findObject(mTranslateDrag.objectId);
    if (object != nullptr)
    {
        const bx::Vec3 delta = bx::sub(object->position, mTranslateDrag.beforePosition);
        if (bx::length(delta) > TRANSLATE_CHANGE_EPSILON)
        {
            command = TranslateObjectCommand{
                .objectId = object->id,
                .before = mTranslateDrag.beforePosition,
                .after = object->position,
            };
        }
    }

    mTranslateDrag = TranslateDrag{};
    return command;
}

/**
 * Cancels all active drag interactions.
 */
void EditSession::cancel()
{
    mExtrudeDrag = ExtrudeDrag{};
    mTranslateDrag = TranslateDrag{};
}

} // namespace Scene
