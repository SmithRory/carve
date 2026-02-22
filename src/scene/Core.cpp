#include "Core.h"

#include <algorithm>
#include <bx/math.h>
#include <array>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include "scene/CommandOps.h"
#include "scene/MeshBuilder.h"
#include "scene/Selection.h"

namespace
{
constexpr float DEFAULT_CUBE_HALF_EXTENT = 0.5F;
constexpr float CREATE_DISTANCE_FROM_CAMERA = 6.0F;

constexpr float DEFAULT_CAMERA_X = 0.0F;
constexpr float DEFAULT_CAMERA_Y = 2.0F;
constexpr float DEFAULT_CAMERA_Z = -8.0F;
constexpr float DEFAULT_CAMERA_YAW = 0.0F;
constexpr float DEFAULT_CAMERA_PITCH = 0.0F;
constexpr uint64_t INITIAL_EDIT_REVISION = 1U;
constexpr uint64_t INITIAL_BUILT_REVISION = 0U;
constexpr uint64_t INITIAL_OBJECT_ID = 1U;

constexpr float LOOK_SENSITIVITY = 0.005F;
constexpr float PITCH_LIMIT_RADIANS = 1.45F;

constexpr float ZERO_SECONDS = 0.0F;
constexpr float ZERO_AXIS = 0.0F;
constexpr float MOVEMENT_LENGTH_EPSILON = 0.00001F;
constexpr float MOVE_SPEED_UNITS_PER_SECOND = 20.0F;

std::vector<bx::Vec3> makeDefaultCubeVertices(float halfExtent)
{
    /* Seed geometry for the default primitive object shape used by create-object. */
    const float h = halfExtent;
    return {
        bx::Vec3{ -h, h, h },
        bx::Vec3{ h, h, h },
        bx::Vec3{ -h, -h, h },
        bx::Vec3{ h, -h, h },
        bx::Vec3{ -h, h, -h },
        bx::Vec3{ h, h, -h },
        bx::Vec3{ -h, -h, -h },
        bx::Vec3{ h, -h, -h },
    };
}

const std::vector<Scene::Face> &defaultCubeFaces()
{
    static const std::vector<Scene::Face> faces = {
        Scene::Face{ 0U, 1U, 3U, 2U }, // front
        Scene::Face{ 5U, 4U, 6U, 7U }, // back
        Scene::Face{ 4U, 0U, 2U, 6U }, // left
        Scene::Face{ 1U, 5U, 7U, 3U }, // right
        Scene::Face{ 4U, 5U, 1U, 0U }, // top
        Scene::Face{ 2U, 3U, 7U, 6U }, // bottom
    };
    return faces;
}

const std::vector<std::array<uint16_t, 2>> &defaultCubeEdges()
{
    static const std::vector<std::array<uint16_t, 2>> edges = {
        std::array<uint16_t, 2>{ 0U, 1U },
        std::array<uint16_t, 2>{ 1U, 3U },
        std::array<uint16_t, 2>{ 3U, 2U },
        std::array<uint16_t, 2>{ 2U, 0U },
        std::array<uint16_t, 2>{ 4U, 5U },
        std::array<uint16_t, 2>{ 5U, 7U },
        std::array<uint16_t, 2>{ 7U, 6U },
        std::array<uint16_t, 2>{ 6U, 4U },
        std::array<uint16_t, 2>{ 0U, 4U },
        std::array<uint16_t, 2>{ 1U, 5U },
        std::array<uint16_t, 2>{ 2U, 6U },
        std::array<uint16_t, 2>{ 3U, 7U },
    };
    return edges;
}

std::vector<Scene::TopologyIndex> selectionIndicesToVector(const std::unordered_set<uint16_t> &selection)
{
    /* Build tickets use vectors, so normalize internal set-based selection into vectors. */
    std::vector<Scene::TopologyIndex> values;
    values.reserve(selection.size());
    for (const uint16_t value : selection)
    {
        values.push_back(value);
    }
    return values;
}
} // namespace

namespace Scene
{

/**
 * Initializes high-level scene systems and default camera/build state.
 */
Core::Core()
    : mCamera({
          .position = bx::Vec3{ DEFAULT_CAMERA_X, DEFAULT_CAMERA_Y, DEFAULT_CAMERA_Z },
          .yawRadians = DEFAULT_CAMERA_YAW,
          .pitchRadians = DEFAULT_CAMERA_PITCH,
          .moveActive = {},
      }),
      mBuild({
          .editRevision = INITIAL_EDIT_REVISION,
          .builtRevision = INITIAL_BUILT_REVISION,
          .buildPending = true,
          .buildInFlight = false,
          .renderMesh = nullptr,
      }),
      mNextObjectId(INITIAL_OBJECT_ID)
{
}

/**
 * Creates a cube in front of the camera and records undo state.
 */
void Core::createCubeInFrontOfCamera()
{
    std::lock_guard<std::mutex> lock(mMutex);
    const EditableObject object = makeCubeInFrontOfCameraLocked();
    const EditCommand command = CreateObjectCommand{
        .object = object,
        .index = mDocument.objects().size(),
        .previousSelection = mDocument.selectedObjectId(),
    };

    if (applyForwardAndMarkLocked(command))
    {
        recordAppliedCommandLocked(command);
    }
}

/**
 * Deletes the selected object and records undo state.
 */
void Core::deleteSelectedObject()
{
    std::lock_guard<std::mutex> lock(mMutex);
    const EditableObject *selected = mDocument.selectedObject();
    if (selected == nullptr)
    {
        return;
    }

    const auto &objects = mDocument.objects();
    const auto found = std::ranges::find_if(objects, [selected](const EditableObject &object) { return object.id == selected->id; });
    const std::size_t index = static_cast<std::size_t>(std::distance(objects.begin(), found));

    const EditCommand command = DeleteObjectCommand{
        .object = *selected,
        .index = index,
        .previousSelection = mDocument.selectedObjectId(),
    };

    if (applyForwardAndMarkLocked(command))
    {
        recordAppliedCommandLocked(command);
    }
}

/**
 * Cycles selection to the next object.
 */
void Core::selectNextObject()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mDocument.selectNext())
    {
        markMeshDirtyLocked();
    }
}

/**
 * Selects an object from screen coordinates.
 * @param[in] mousePosition Cursor position in pixels.
 * @param[in] viewportWidth Viewport width in pixels.
 * @param[in] viewportHeight Viewport height in pixels.
 * @return True when an object was hit.
 */
bool Core::selectObjectAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight)
{
    return selectObjectAtScreen(mousePosition, viewportWidth, viewportHeight, false);
}

/**
 * Selects an object id from screen coordinates.
 * @param[in] mousePosition Cursor position in pixels.
 * @param[in] viewportWidth Viewport width in pixels.
 * @param[in] viewportHeight Viewport height in pixels.
 * @return Selected object id, or std::nullopt when no hit.
 */
std::optional<ObjectId> Core::selectObjectIdAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    const CameraParameters cameraParameters{
        .position = mCamera.position,
        .yawRadians = mCamera.yawRadians,
        .pitchRadians = mCamera.pitchRadians,
    };
    return selectObjectFromScreen(
        mDocument,
        cameraParameters,
        mousePosition,
        viewportWidth,
        viewportHeight);
}

/**
 * Selects with optional additive behavior.
 * @param[in] mousePosition Cursor position in pixels.
 * @param[in] viewportWidth Viewport width in pixels.
 * @param[in] viewportHeight Viewport height in pixels.
 * @param[in] additiveSelection True to keep existing selected objects.
 * @return True when an object was hit.
 */
bool Core::selectObjectAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight, bool additiveSelection)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const CameraParameters cameraParameters{
        .position = mCamera.position,
        .yawRadians = mCamera.yawRadians,
        .pitchRadians = mCamera.pitchRadians,
    };
    const std::optional<ObjectId> selectedId = selectObjectFromScreen(
        mDocument,
        cameraParameters,
        mousePosition,
        viewportWidth,
        viewportHeight);

    bool selectionChanged = false;
    if (selectedId.has_value())
    {
        selectionChanged = additiveSelection ? mDocument.addToSelection(*selectedId) : mDocument.selectObject(*selectedId);
    }
    else
    {
        selectionChanged = mDocument.clearSelection();
    }

    if (selectionChanged)
    {
        markMeshDirtyLocked();
    }

    return selectedId.has_value();
}

bool Core::selectComponentAtScreen(const MousePosition &mousePosition, float viewportWidth, float viewportHeight, bool additiveSelection)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const ObjectId selectedId = mDocument.selectedObjectId();
    if (selectedId == 0U)
    {
        return false;
    }

    const EditableObject *selectedObject = mDocument.findObject(selectedId);
    if (selectedObject == nullptr)
    {
        return false;
    }

    const CameraParameters cameraParameters{
        .position = mCamera.position,
        .yawRadians = mCamera.yawRadians,
        .pitchRadians = mCamera.pitchRadians,
    };
    const std::optional<ComponentSelection> componentSelection = selectComponentFromScreen(
        *selectedObject,
        cameraParameters,
        mousePosition,
        viewportWidth,
        viewportHeight);

    bool changed = false;

    /* Component clicks mutate sub-selection only on the currently selected object. */
    if (componentSelection.has_value())
    {
        if (componentSelection->type == ComponentType::Vertex)
        {
            changed = mDocument.selectVertex(selectedId, componentSelection->index, additiveSelection);
        }
        else if (componentSelection->type == ComponentType::Edge)
        {
            changed = mDocument.selectEdge(selectedId, componentSelection->index, additiveSelection);
        }
        else if (componentSelection->type == ComponentType::Face)
        {
            changed = mDocument.selectFace(selectedId, componentSelection->index, additiveSelection);
        }
    }
    else if (!additiveSelection)
    {
        changed = mDocument.clearComponentSelection();
    }

    if (changed)
    {
        markMeshDirtyLocked();
    }

    return componentSelection.has_value();
}

/**
 * Clears all selected objects.
 * @return True when selection changed.
 */
bool Core::clearSelection()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mDocument.clearSelection())
    {
        return false;
    }

    markMeshDirtyLocked();
    return true;
}

/**
 * Returns true when object id is selected.
 * @param[in] id Object id to test.
 * @return True when selected.
 */
bool Core::isObjectSelected(ObjectId id) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mDocument.isObjectSelected(id);
}

/**
 * Reverts the last committed edit command.
 * @return True when a command was undone.
 */
bool Core::undo()
{
    std::lock_guard<std::mutex> lock(mMutex);
    EditCommand command;
    if (!mUndoHistory.popUndo(command))
    {
        return false;
    }

    if (!applyBackwardAndMarkLocked(command))
    {
        return false;
    }

    mUndoHistory.pushRedo(command);
    return true;
}

/**
 * Reapplies the last reverted edit command.
 * @return True when a command was redone.
 */
bool Core::redo()
{
    std::lock_guard<std::mutex> lock(mMutex);
    EditCommand command;
    if (!mUndoHistory.popRedo(command))
    {
        return false;
    }

    if (!applyForwardAndMarkLocked(command))
    {
        return false;
    }

    mUndoHistory.pushUndo(command);
    return true;
}

/**
 * Starts extrusion drag interaction on selected object.
 * @param[in] mouseY Cursor y position in pixels.
 */
void Core::beginExtrudeEdit(float mouseY)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mEditSession.beginExtrude(mDocument, mouseY);
}

/**
 * Applies one extrusion drag step.
 * @param[in] mouseY Cursor y position in pixels.
 */
void Core::updateExtrudeEdit(float mouseY)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mEditSession.updateExtrude(mDocument, mouseY))
    {
        markMeshDirtyLocked();
    }
}

/**
 * Ends extrusion drag and commits one undo command when changed.
 */
void Core::endExtrudeEdit()
{
    std::lock_guard<std::mutex> lock(mMutex);
    const std::optional<EditCommand> command = mEditSession.endExtrude(mDocument);
    if (command.has_value())
    {
        recordAppliedCommandLocked(*command);
    }
}

/**
 * Starts translation drag interaction on selected object.
 * @param[in] mousePosition Cursor position in pixels.
 */
void Core::beginTranslateEdit(const MousePosition &mousePosition)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mEditSession.beginTranslate(mDocument, mousePosition);
}

/**
 * Applies one translation drag step.
 * @param[in] mousePosition Cursor position in pixels.
 */
void Core::updateTranslateEdit(const MousePosition &mousePosition)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mEditSession.updateTranslate(mDocument, mCamera.yawRadians, mousePosition))
    {
        markMeshDirtyLocked();
    }
}

/**
 * Ends translation drag and commits one undo command when changed.
 */
void Core::endTranslateEdit()
{
    std::lock_guard<std::mutex> lock(mMutex);
    const std::optional<EditCommand> command = mEditSession.endTranslate(mDocument);
    if (command.has_value())
    {
        recordAppliedCommandLocked(*command);
    }
}

/**
 * Toggles one camera movement direction.
 * @param[in] move Camera movement direction.
 * @param[in] active True while pressed, false when released.
 */
void Core::setCameraMoveState(CameraMove move, bool active)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCamera.moveActive[static_cast<std::size_t>(move)] = active;
}

/**
 * Applies mouse-look deltas to camera yaw/pitch.
 * @param[in] deltaX Horizontal mouse delta in pixels.
 * @param[in] deltaY Vertical mouse delta in pixels.
 */
void Core::addCameraLookDelta(float deltaX, float deltaY)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mCamera.yawRadians += deltaX * LOOK_SENSITIVITY;
    mCamera.pitchRadians = bx::clamp(mCamera.pitchRadians - (deltaY * LOOK_SENSITIVITY), -PITCH_LIMIT_RADIANS, PITCH_LIMIT_RADIANS);
}

/**
 * Advances camera transform based on active movement state.
 * @param[in] dtSeconds Frame delta time in seconds.
 */
void Core::tickCamera(float dtSeconds)
{
    if (dtSeconds <= ZERO_SECONDS)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);

    const float cosPitch = bx::cos(mCamera.pitchRadians);
    const bx::Vec3 forward(bx::sin(mCamera.yawRadians) * cosPitch, bx::sin(mCamera.pitchRadians), bx::cos(mCamera.yawRadians) * cosPitch);
    const bx::Vec3 right(bx::cos(mCamera.yawRadians), ZERO_AXIS, -bx::sin(mCamera.yawRadians));
    bx::Vec3 velocity(ZERO_AXIS, ZERO_AXIS, ZERO_AXIS);

    if (mCamera.moveActive[static_cast<std::size_t>(CameraMove::Forward)])
    {
        velocity = bx::add(velocity, forward);
    }
    if (mCamera.moveActive[static_cast<std::size_t>(CameraMove::Backward)])
    {
        velocity = bx::sub(velocity, forward);
    }
    if (mCamera.moveActive[static_cast<std::size_t>(CameraMove::Right)])
    {
        velocity = bx::add(velocity, right);
    }
    if (mCamera.moveActive[static_cast<std::size_t>(CameraMove::Left)])
    {
        velocity = bx::sub(velocity, right);
    }

    const float velocityLen = bx::length(velocity);
    if (velocityLen <= MOVEMENT_LENGTH_EPSILON)
    {
        return;
    }

    const bx::Vec3 velocityDirection = bx::normalize(velocity);
    mCamera.position = bx::add(mCamera.position, bx::mul(velocityDirection, MOVE_SPEED_UNITS_PER_SECOND * dtSeconds));
}

/**
 * Emits an immutable build ticket when scene mesh is dirty.
 * @param[out] outTicket Build ticket to be consumed by the worker.
 * @return True when a build should start.
 */
bool Core::tryStartBuild(BuildTicket &outTicket)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mBuild.buildPending || mBuild.buildInFlight)
    {
        return false;
    }

    outTicket.targetRevision = mBuild.editRevision;
    outTicket.objects.clear();
    outTicket.objects.reserve(mDocument.objects().size());

    for (const EditableObject &object : mDocument.objects())
    {
        outTicket.objects.push_back(BuildObject{
            .objectId = object.id,
            .position = object.position,
            .localVertices = object.localVertices,
            .faces = object.faces,
            .edges = object.edges,
            .selectedVertexIndices = (mDocument.componentSelectionObjectId() == object.id) ? selectionIndicesToVector(mDocument.selectedVertexIndices()) : std::vector<TopologyIndex>{},
            .selectedEdgeIndices = (mDocument.componentSelectionObjectId() == object.id) ? selectionIndicesToVector(mDocument.selectedEdgeIndices()) : std::vector<TopologyIndex>{},
            .selectedFaceIndices = (mDocument.componentSelectionObjectId() == object.id) ? selectionIndicesToVector(mDocument.selectedFaceIndices()) : std::vector<TopologyIndex>{},
            .selected = mDocument.isObjectSelected(object.id),
        });
    }

    outTicket.objectSelected = mDocument.hasSelection();

    mBuild.buildInFlight = true;
    mBuild.buildPending = false;
    return true;
}

/**
 * Builds mesh payload from a build ticket.
 * @param[in] ticket Immutable build input payload.
 * @return Built mesh data tagged with ticket revision.
 */
BuiltMeshData Core::buildRenderMesh(const BuildTicket &ticket)
{
    return buildMeshFromTicket(ticket);
}

/**
 * Publishes finished build data for rendering.
 * @param[in] built Built mesh payload from worker thread.
 */
void Core::finishBuild(BuiltMeshData built)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (built.builtRevision >= mBuild.builtRevision)
    {
        mBuild.builtRevision = built.builtRevision;
        mBuild.renderMesh = std::make_shared<BuiltMeshData>(std::move(built));
    }

    mBuild.buildInFlight = false;
    if (mBuild.builtRevision < mBuild.editRevision)
    {
        mBuild.buildPending = true;
    }
}

/**
 * Returns a thread-safe snapshot for renderer consumption.
 * @return Snapshot with camera and mesh state.
 */
RenderSnapshot Core::snapshot() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return RenderSnapshot{
        .editRevision = mBuild.editRevision,
        .builtRevision = mBuild.builtRevision,
        .upToDate = (mBuild.editRevision == mBuild.builtRevision),
        .objectSelected = mDocument.hasSelection(),
        .cameraPosition = mCamera.position,
        .cameraYawRadians = mCamera.yawRadians,
        .cameraPitchRadians = mCamera.pitchRadians,
        .renderMesh = mBuild.renderMesh,
    };
}

EditableObject Core::makeCubeInFrontOfCameraLocked()
{
    const float cosPitch = bx::cos(mCamera.pitchRadians);
    const bx::Vec3 forward(
        bx::sin(mCamera.yawRadians) * cosPitch,
        bx::sin(mCamera.pitchRadians),
        bx::cos(mCamera.yawRadians) * cosPitch);

    const bx::Vec3 center = bx::add(mCamera.position, bx::mul(forward, CREATE_DISTANCE_FROM_CAMERA));

    return EditableObject{
        .id = mNextObjectId++,
        .position = bx::Vec3{ center.x, 0.0F, center.z },
        .localVertices = makeDefaultCubeVertices(DEFAULT_CUBE_HALF_EXTENT),
        .faces = defaultCubeFaces(),
        .edges = defaultCubeEdges(),
    };
}

bool Core::applyForwardAndMarkLocked(const EditCommand &command)
{
    const bool changed = applyCommandForward(mDocument, command);
    if (changed)
    {
        markMeshDirtyLocked();
    }

    return changed;
}

bool Core::applyBackwardAndMarkLocked(const EditCommand &command)
{
    const bool changed = applyCommandBackward(mDocument, command);
    if (changed)
    {
        markMeshDirtyLocked();
    }

    return changed;
}

void Core::recordAppliedCommandLocked(EditCommand command)
{
    mUndoHistory.recordApplied(std::move(command));
}

void Core::markMeshDirtyLocked()
{
    ++mBuild.editRevision;
    mBuild.buildPending = true;
}

} // namespace Scene
