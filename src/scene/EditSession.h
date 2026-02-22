#pragma once

#include <optional>
#include <vector>

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
    void beginExtrude(const Document &document, float mouseY);
    bool updateExtrude(Document &document, float mouseY);
    std::optional<EditCommand> endExtrude(const Document &document);
    void beginTranslate(const Document &document, const MousePosition &mousePosition);
    bool updateTranslate(Document &document, float cameraYawRadians, const MousePosition &mousePosition);
    std::optional<EditCommand> endTranslate(const Document &document);
    void cancel();

private:
    struct ExtrudeDrag
    {
        bool active{};
        ObjectId objectId{};
        float previousMouseY{};
        std::vector<uint16_t> vertexIndices;
        std::vector<float> beforeY;
    };

    struct TranslateDrag
    {
        bool active{};
        ObjectId objectId{};
        bx::Vec3 beforePosition{ 0.0F, 0.0F, 0.0F };
        float previousMouseX{};
        float previousMouseY{};
    };

    ExtrudeDrag mExtrudeDrag;
    TranslateDrag mTranslateDrag;
};

} // namespace Scene
