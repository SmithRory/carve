#pragma once

#include <optional>
#include <vector>

#include "scene/Document.h"
#include "scene/EditCommand.h"

namespace Scene
{

/**
 * Owns transient drag interaction state for mesh edit gestures.
 */
class EditSession
{
public:
    /**
     * Starts an extrusion drag on the selected object.
     * @param[in] document Current document state.
     * @param[in] mouseY Cursor y position in pixels.
     */
    void beginExtrude(const Document &document, float mouseY);
    /**
     * Applies one extrusion drag update. Returns true when object changed.
     * @param[in,out] document Mutable document state.
     * @param[in] mouseY Cursor y position in pixels.
     * @return True when extrusion changed object state.
     */
    bool updateExtrude(Document &document, float mouseY);
    /**
     * Ends extrusion drag and returns a command when changed.
     * @param[in] document Current document state.
     * @return Undo command when extrusion changed object state.
     */
    std::optional<EditCommand> endExtrude(const Document &document);

    /**
     * Starts a translation drag on the selected object.
     * @param[in] document Current document state.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     */
    void beginTranslate(const Document &document, float mouseX, float mouseY);
    /**
     * Applies one translation drag update. Returns true when object changed.
     * @param[in,out] document Mutable document state.
     * @param[in] cameraYawRadians Camera yaw in radians.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     * @return True when translation changed object state.
     */
    bool updateTranslate(Document &document, float cameraYawRadians, float mouseX, float mouseY);
    /**
     * Ends translation drag and returns a command when changed.
     * @param[in] document Current document state.
     * @return Undo command when translation changed object state.
     */
    std::optional<EditCommand> endTranslate(const Document &document);

    /**
     * Cancels all active drag interactions.
     */
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
