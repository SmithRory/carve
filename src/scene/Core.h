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
    Core();
    void createCubeInFrontOfCamera();
    void deleteSelectedObject();
    void selectNextObject();
    bool selectObjectAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight);
    std::optional<ObjectId> selectObjectIdAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight) const;
    bool selectObjectAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight, bool additiveSelection);
    bool selectComponentAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight, bool additiveSelection);
    bool clearSelection();
    bool isObjectSelected(ObjectId id) const;
    bool undo();
    bool redo();
    void beginExtrudeEdit(float mouseY);
    void updateExtrudeEdit(float mouseY);
    void endExtrudeEdit();
    void beginTranslateEdit(const MousePosition &mousePosition);
    void updateTranslateEdit(const MousePosition &mousePosition);
    void endTranslateEdit();
    void setCameraMoveState(CameraMove move, bool active);
    void addCameraLookDelta(float deltaX, float deltaY);
    void tickCamera(float dtSeconds);
    bool tryStartBuild(BuildTicket &outTicket);
    static BuiltMeshData buildRenderMesh(const BuildTicket &ticket);
    void finishBuild(BuiltMeshData built);
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
