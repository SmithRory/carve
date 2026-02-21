#pragma once

#include <array>
#include <bx/math.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include "scene/Document.h"
#include "scene/EditCommand.h"
#include "scene/EditSession.h"
#include "scene/Types.h"
#include "scene/UndoHistory.h"

namespace Scene
{
inline constexpr std::size_t CAMERA_MOVE_SLOT_COUNT = 4U;

/**
 * Owns the editor-facing scene state and the build-to-render data lifecycle.
 *
 * Core is the high-level scene API. It delegates document mutation, drag-edit sessions,
 * undo history, selection queries, and mesh building to dedicated scene components.
 */
class Core
{
public:
    /**
     * Initializes high-level scene systems and default camera/build state.
     */
    Core();

    /**
     * Creates a cube in front of the camera and records undo state.
     */
    void createCubeInFrontOfCamera();
    /**
     * Deletes the selected object and records undo state.
     */
    void deleteSelectedObject();

    /**
     * Cycles selection to the next object.
     */
    void selectNextObject();

    /**
     * Selects an object from screen coordinates.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     * @param[in] viewportWidth Viewport width in pixels.
     * @param[in] viewportHeight Viewport height in pixels.
     * @return True when an object was hit.
     */
    bool selectObjectAtScreen(float mouseX, float mouseY, float viewportWidth, float viewportHeight);
    /**
     * Selects an object id from screen coordinates.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     * @param[in] viewportWidth Viewport width in pixels.
     * @param[in] viewportHeight Viewport height in pixels.
     * @return Selected object id, or std::nullopt when no hit.
     */
    std::optional<ObjectId> selectObjectIdAtScreen(float mouseX, float mouseY, float viewportWidth, float viewportHeight) const;
    /**
     * Selects with optional additive behavior.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     * @param[in] viewportWidth Viewport width in pixels.
     * @param[in] viewportHeight Viewport height in pixels.
     * @param[in] additiveSelection True to keep existing selected objects.
     * @return True when an object was hit.
     */
    bool selectObjectAtScreen(float mouseX, float mouseY, float viewportWidth, float viewportHeight, bool additiveSelection);
    bool selectComponentAtScreen(float mouseX, float mouseY, float viewportWidth, float viewportHeight, bool additiveSelection);
    /**
     * Clears all selected objects.
     * @return True when selection changed.
     */
    bool clearSelection();
    /**
     * Returns true when object id is selected.
     * @param[in] id Object id to test.
     * @return True when selected.
     */
    bool isObjectSelected(ObjectId id) const;

    /**
     * Reverts the last committed edit command.
     * @return True when a command was undone.
     */
    bool undo();

    /**
     * Reapplies the last reverted edit command.
     * @return True when a command was redone.
     */
    bool redo();

    /**
     * Starts extrusion drag interaction on selected object.
     * @param[in] mouseY Cursor y position in pixels.
     */
    void beginExtrudeEdit(float mouseY);

    /**
     * Applies one extrusion drag step.
     * @param[in] mouseY Cursor y position in pixels.
     */
    void updateExtrudeEdit(float mouseY);

    /**
     * Ends extrusion drag and commits one undo command when changed.
     */
    void endExtrudeEdit();

    /**
     * Starts translation drag interaction on selected object.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     */
    void beginTranslateEdit(float mouseX, float mouseY);

    /**
     * Applies one translation drag step.
     * @param[in] mouseX Cursor x position in pixels.
     * @param[in] mouseY Cursor y position in pixels.
     */
    void updateTranslateEdit(float mouseX, float mouseY);

    /**
     * Ends translation drag and commits one undo command when changed.
     */
    void endTranslateEdit();

    /**
     * Toggles one camera movement direction.
     * @param[in] move Camera movement direction.
     * @param[in] active True while pressed, false when released.
     */
    void setCameraMoveState(CameraMove move, bool active);

    /**
     * Applies mouse-look deltas to camera yaw/pitch.
     * @param[in] deltaX Horizontal mouse delta in pixels.
     * @param[in] deltaY Vertical mouse delta in pixels.
     */
    void addCameraLookDelta(float deltaX, float deltaY);

    /**
     * Advances camera transform based on active movement state.
     * @param[in] dtSeconds Frame delta time in seconds.
     */
    void tickCamera(float dtSeconds);

    /**
     * Emits an immutable build ticket when scene mesh is dirty.
     * @param[out] outTicket Build ticket to be consumed by the worker.
     * @return True when a build should start.
     */
    bool tryStartBuild(BuildTicket &outTicket);

    /**
     * Builds mesh payload from a build ticket.
     * @param[in] ticket Immutable build input payload.
     * @return Built mesh data tagged with ticket revision.
     */
    static BuiltMeshData buildRenderMesh(const BuildTicket &ticket);

    /**
     * Publishes finished build data for rendering.
     * @param[in] built Built mesh payload from worker thread.
     */
    void finishBuild(BuiltMeshData built);
    /**
     * Returns a thread-safe snapshot for renderer consumption.
     * @return Snapshot with camera and mesh state.
     */
    RenderSnapshot snapshot() const;

private:
    struct CameraState
    {
        bx::Vec3 position;
        float yawRadians;
        float pitchRadians;
        std::array<bool, CAMERA_MOVE_SLOT_COUNT> moveActive;
    };

    struct BuildState
    {
        uint64_t editRevision;
        uint64_t builtRevision;
        bool buildPending;
        bool buildInFlight;
        std::shared_ptr<const BuiltMeshData> renderMesh;
    };

    EditableObject makeCubeInFrontOfCameraLocked();

    bool applyForwardAndMarkLocked(const EditCommand &command);
    bool applyBackwardAndMarkLocked(const EditCommand &command);

    void recordAppliedCommandLocked(EditCommand command);
    void markMeshDirtyLocked();

    mutable std::mutex mMutex;

    Document mDocument;
    UndoHistory mUndoHistory;
    EditSession mEditSession;

    CameraState mCamera;
    BuildState mBuild;

    ObjectId mNextObjectId{};
};

} // namespace Scene
