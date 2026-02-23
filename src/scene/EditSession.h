#pragma once

#include <optional>

#include "scene/Document.h"
#include "scene/EditCommand.h"
#include "scene/Types.h"

namespace Scene
{

/**
 * Owns transient drag interaction state for mesh edit gestures.
 */
class EditSession
{
public:
    void beginTranslate(const Document &document, const MousePosition &mousePosition);
    bool updateTranslate(Document &document, float cameraYawRadians, const MousePosition &mousePosition);
    std::optional<EditCommand> endTranslate(const Document &document);
    void cancel();

private:
    struct TranslateDrag
    {
        bool active{};
        ObjectId objectId{};
        bx::Vec3 beforePosition{ 0.0F, 0.0F, 0.0F };
        float previousMouseX{};
        float previousMouseY{};
    };

    TranslateDrag mTranslateDrag;
};

} // namespace Scene
