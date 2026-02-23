#include "EditSession.h"

#include <bx/math.h>

namespace
{
constexpr float PIXEL_DEADZONE = 0.001F;
constexpr float TRANSLATE_WORLD_UNITS_PER_PIXEL = 0.02F;
constexpr float TRANSLATE_CHANGE_EPSILON = 0.00001F;
} // namespace

namespace Scene
{

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

    const bx::Vec3 right(bx::cos(cameraYawRadians), 0.0F, -bx::sin(cameraYawRadians));
    const bx::Vec3 forwardFlat(bx::sin(cameraYawRadians), 0.0F, bx::cos(cameraYawRadians));

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
    mTranslateDrag = TranslateDrag{};
}

} // namespace Scene
