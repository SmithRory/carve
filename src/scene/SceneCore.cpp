#include "SceneCore.h"

#include <bx/math.h>
#include <bx/pixelformat.h>
#include <utility>

namespace
{
constexpr float NORMAL_MAP_SCALE = 0.5F;
constexpr float NORMAL_MAP_BIAS = 0.5F;
constexpr float NORMAL_MAP_ALPHA = 1.0F;

constexpr float DEFAULT_HALF_EXTENT = 0.5F;
constexpr float DEFAULT_TOP_EXTRUDE = 0.0F;
constexpr float DEFAULT_MOUSE_Y = 0.0F;
constexpr float DEFAULT_CAMERA_X = 0.0F;
constexpr float DEFAULT_CAMERA_Y = 0.0F;
constexpr float DEFAULT_CAMERA_Z = -4.0F;
constexpr float DEFAULT_CAMERA_YAW = 0.0F;
constexpr float DEFAULT_CAMERA_PITCH = 0.0F;
constexpr uint64_t INITIAL_EDIT_REVISION = 1U;
constexpr uint64_t INITIAL_BUILT_REVISION = 0U;

constexpr float PIXEL_DEADZONE = 0.001F;
constexpr float EXTRUDE_PER_PIXEL = 0.01F;
constexpr float MIN_TOP_EXTRUDE = -0.35F;
constexpr float MAX_TOP_EXTRUDE = 1.5F;
constexpr float EXTRUDE_CHANGE_EPSILON = 0.00001F;

constexpr float LOOK_SENSITIVITY = 0.005F;
constexpr float PITCH_LIMIT_RADIANS = 1.45F;

constexpr float ZERO_SECONDS = 0.0F;
constexpr float ZERO_AXIS = 0.0F;
constexpr float MOVEMENT_LENGTH_EPSILON = 0.00001F;
constexpr float MOVE_SPEED_UNITS_PER_SECOND = 20.0F;
constexpr std::size_t VECTOR_COMPONENT_COUNT = 4U;
constexpr std::size_t ZERO_SIZE = 0U;
constexpr uint32_t ZERO_PACKED_NORMAL = 0U;

constexpr std::size_t FACE_CORNER_COUNT = 4U;
constexpr std::size_t CUBE_FACE_COUNT = 6U;
constexpr uint16_t TRIANGLE_INDEX_COUNT_PER_FACE = 6U;
constexpr uint16_t FACE_VERTEX_COUNT = 4U;
constexpr float TOP_FACE_NORMAL_THRESHOLD = 0.5F;
constexpr uint16_t FACE_INDEX_0 = 0U;
constexpr uint16_t FACE_INDEX_1 = 1U;
constexpr uint16_t FACE_INDEX_2 = 2U;
constexpr uint16_t FACE_INDEX_3 = 3U;
constexpr uint16_t CORNER_INDEX_0 = 0U;
constexpr uint16_t CORNER_INDEX_1 = 1U;
constexpr uint16_t CORNER_INDEX_2 = 2U;
constexpr uint16_t CORNER_INDEX_3 = 3U;
constexpr uint16_t CORNER_INDEX_4 = 4U;
constexpr uint16_t CORNER_INDEX_5 = 5U;
constexpr uint16_t CORNER_INDEX_6 = 6U;
constexpr uint16_t CORNER_INDEX_7 = 7U;

constexpr bx::Vec3 NORMAL_FRONT{ 0.0F, 0.0F, 1.0F };
constexpr bx::Vec3 NORMAL_BACK{ 0.0F, 0.0F, -1.0F };
constexpr bx::Vec3 NORMAL_LEFT{ -1.0F, 0.0F, 0.0F };
constexpr bx::Vec3 NORMAL_RIGHT{ 1.0F, 0.0F, 0.0F };
constexpr bx::Vec3 NORMAL_TOP{ 0.0F, 1.0F, 0.0F };
constexpr bx::Vec3 NORMAL_BOTTOM{ 0.0F, -1.0F, 0.0F };

constexpr std::array<float, 2> UV_00{ 0.0F, 0.0F };
constexpr std::array<float, 2> UV_10{ 1.0F, 0.0F };
constexpr std::array<float, 2> UV_01{ 0.0F, 1.0F };
constexpr std::array<float, 2> UV_11{ 1.0F, 1.0F };
constexpr std::size_t UV_COMPONENT_U = 0U;
constexpr std::size_t UV_COMPONENT_V = 1U;

constexpr uint32_t SELECTED_TOP_COLOR_ABGR = 0xff20a0ffU;
constexpr uint32_t DEFAULT_TOP_COLOR_ABGR = 0xff3050d0U;
constexpr uint32_t SELECTED_BOTTOM_COLOR_ABGR = 0xff60f0d0U;
constexpr uint32_t DEFAULT_BOTTOM_COLOR_ABGR = 0xff80d080U;

/**
 * Packs a normalized vector into RGBA8 for bgfx vertex attributes.
 * @param[in] normal Unit-length normal in [-1, 1] range.
 * @return Packed ABGR normal value.
 */
uint32_t packNormal(const bx::Vec3 &normal)
{
    const float mapped[VECTOR_COMPONENT_COUNT] = {
        (normal.x * NORMAL_MAP_SCALE) + NORMAL_MAP_BIAS,
        (normal.y * NORMAL_MAP_SCALE) + NORMAL_MAP_BIAS,
        (normal.z * NORMAL_MAP_SCALE) + NORMAL_MAP_BIAS,
        NORMAL_MAP_ALPHA,
    };

    uint32_t packed = ZERO_PACKED_NORMAL;
    bx::packRgba8(&packed, mapped);
    return packed;
}
} /* namespace */

namespace Scene
{

/**
 * Initializes editable cube state, camera state, and mesh build state.
 */
Core::Core()
    : mCube({
          .halfExtent = DEFAULT_HALF_EXTENT,
          .topExtrude = DEFAULT_TOP_EXTRUDE,
          .selected = false,
          .editActive = false,
          .lastMouseY = DEFAULT_MOUSE_Y,
      }),
      mCamera({
          .position = bx::Vec3{ DEFAULT_CAMERA_X, DEFAULT_CAMERA_Y, DEFAULT_CAMERA_Z },
          .yawRadians = DEFAULT_CAMERA_YAW,
          .pitchRadians = DEFAULT_CAMERA_PITCH,
          .moveActive = {},
      }),
      mEditRevision(INITIAL_EDIT_REVISION), mBuiltRevision(INITIAL_BUILT_REVISION), mBuildPending(true), mBuildInFlight(false), mRenderMesh(nullptr)
{
}

/**
 * Starts primary edit interaction for cube extrusion.
 * @param[in] mouseY Current mouse Y position in pixels.
 */
void Core::beginPrimaryEdit(float mouseY)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCube.editActive = true;
    mCube.lastMouseY = mouseY;

    if (!mCube.selected)
    {
        mCube.selected = true;
        markMeshDirtyLocked();
    }
}

/**
 * Applies primary edit drag delta to cube extrusion.
 * @param[in] mouseY Current mouse Y position in pixels.
 */
void Core::updatePrimaryEdit(float mouseY)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mCube.editActive)
    {
        return;
    }

    const float pixelDelta = mCube.lastMouseY - mouseY;
    mCube.lastMouseY = mouseY;

    if (bx::abs(pixelDelta) < PIXEL_DEADZONE)
    {
        return;
    }

    const float previous = mCube.topExtrude;

    /* Map drag pixels to extrusion units and clamp to editor limits. */
    mCube.topExtrude = bx::clamp(mCube.topExtrude + (pixelDelta * EXTRUDE_PER_PIXEL), MIN_TOP_EXTRUDE, MAX_TOP_EXTRUDE);

    if (bx::abs(previous - mCube.topExtrude) > EXTRUDE_CHANGE_EPSILON)
    {
        markMeshDirtyLocked();
    }
}

/**
 * Ends primary edit interaction.
 */
void Core::endPrimaryEdit()
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCube.editActive = false;
}

/**
 * Updates keyboard movement state for the camera.
 * @param[in] move Movement direction.
 * @param[in] active True while pressed, false when released.
 */
void Core::setCameraMoveState(CameraMove move, bool active)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCamera.moveActive.at(static_cast<std::size_t>(move)) = active;
}

/**
 * Applies mouse look deltas to camera yaw and pitch.
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
 * Advances camera position based on active movement flags.
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
    const bx::Vec3 forward(
        bx::sin(mCamera.yawRadians) * cosPitch,
        bx::sin(mCamera.pitchRadians),
        bx::cos(mCamera.yawRadians) * cosPitch);
    const bx::Vec3 right(
        bx::cos(mCamera.yawRadians),
        ZERO_AXIS,
        -bx::sin(mCamera.yawRadians));
    bx::Vec3 velocity(ZERO_AXIS, ZERO_AXIS, ZERO_AXIS);

    /* Compose movement vector from per-axis input flags. */
    if (mCamera.moveActive.at(static_cast<std::size_t>(CameraMove::Forward)))
    {
        velocity = bx::add(velocity, forward);
    }
    if (mCamera.moveActive.at(static_cast<std::size_t>(CameraMove::Backward)))
    {
        velocity = bx::sub(velocity, forward);
    }
    if (mCamera.moveActive.at(static_cast<std::size_t>(CameraMove::Right)))
    {
        velocity = bx::add(velocity, right);
    }
    if (mCamera.moveActive.at(static_cast<std::size_t>(CameraMove::Left)))
    {
        velocity = bx::sub(velocity, right);
    }

    const float velocityLen = bx::length(velocity);
    if (velocityLen <= MOVEMENT_LENGTH_EPSILON)
    {
        return;
    }

    const bx::Vec3 velocityDirection = bx::normalize(velocity);

    /* Keep speed constant regardless of diagonal input. */
    const bx::Vec3 movedPosition = bx::add(mCamera.position, bx::mul(velocityDirection, MOVE_SPEED_UNITS_PER_SECOND * dtSeconds));
    mCamera.position = movedPosition;
}

/**
 * Produces the next build ticket if a mesh rebuild is pending.
 * @param[out] outTicket Output ticket consumed by the build worker.
 * @return True when a build should start, otherwise false.
 */
bool Core::tryStartBuild(BuildTicket &outTicket)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mBuildPending || mBuildInFlight)
    {
        return false;
    }

    outTicket.targetRevision = mEditRevision;
    outTicket.buildPositions = evaluateBuildPositions(mCube);
    outTicket.objectSelected = mCube.selected;

    mBuildInFlight = true;
    mBuildPending = false;
    return true;
}

/**
 * Builds packed render mesh data for a build ticket.
 * @param[in] ticket Immutable build input payload.
 * @return Built mesh data and revision.
 */
BuiltMeshData Core::buildRenderMesh(const BuildTicket &ticket)
{
    return packRenderMesh(ticket.targetRevision, ticket.buildPositions, ticket.objectSelected);
}

/**
 * Publishes a finished build result to the render snapshot state.
 * @param[in] built Built mesh payload from the worker.
 */
void Core::finishBuild(BuiltMeshData built)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (built.builtRevision >= mBuiltRevision)
    {
        mBuiltRevision = built.builtRevision;
        mRenderMesh = std::make_shared<BuiltMeshData>(std::move(built));
    }

    mBuildInFlight = false;
    if (mBuiltRevision < mEditRevision)
    {
        mBuildPending = true;
    }
}

/**
 * Captures a thread-safe view of current render state.
 * @return Snapshot containing camera and mesh information.
 */
RenderSnapshot Core::snapshot() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return RenderSnapshot{
        .editRevision = mEditRevision,
        .builtRevision = mBuiltRevision,
        .upToDate = (mEditRevision == mBuiltRevision),
        .objectSelected = mCube.selected,
        .cameraPosition = mCamera.position,
        .cameraYawRadians = mCamera.yawRadians,
        .cameraPitchRadians = mCamera.pitchRadians,
        .renderMesh = mRenderMesh,
    };
}

/**
 * Evaluates cube corner positions for the current editable state.
 * @param[in] cube Editable cube model.
 * @return Eight cube corner positions.
 */
std::array<bx::Vec3, CUBE_CORNER_COUNT> Core::evaluateBuildPositions(const EditableCube &cube)
{
    const float h = cube.halfExtent;
    const float topY = h + cube.topExtrude;
    const float bottomY = -h;

    return {
        bx::Vec3{ -h, topY, h },
        bx::Vec3{ h, topY, h },
        bx::Vec3{ -h, bottomY, h },
        bx::Vec3{ h, bottomY, h },
        bx::Vec3{ -h, topY, -h },
        bx::Vec3{ h, topY, -h },
        bx::Vec3{ -h, bottomY, -h },
        bx::Vec3{ h, bottomY, -h },
    };
}

/**
 * Converts cube corners into packed vertices and triangle indices.
 * @param[in] revision Revision attached to the built output.
 * @param[in] positions Cube corner positions.
 * @param[in] selected True when object is selected for highlight colors.
 * @return Packed mesh buffers ready for upload.
 */
BuiltMeshData Core::packRenderMesh(uint64_t revision, const std::array<bx::Vec3, CUBE_CORNER_COUNT> &positions, bool selected)
{
    struct FaceDef
    {
        std::array<uint16_t, FACE_CORNER_COUNT> corners;
        bx::Vec3 normal;
    };

    static constexpr std::array<FaceDef, CUBE_FACE_COUNT> FACES = {
        FaceDef{ .corners = { CORNER_INDEX_0, CORNER_INDEX_1, CORNER_INDEX_2, CORNER_INDEX_3 }, .normal = NORMAL_FRONT },  /* Front */
        FaceDef{ .corners = { CORNER_INDEX_5, CORNER_INDEX_4, CORNER_INDEX_7, CORNER_INDEX_6 }, .normal = NORMAL_BACK }, /* Back */
        FaceDef{ .corners = { CORNER_INDEX_4, CORNER_INDEX_0, CORNER_INDEX_6, CORNER_INDEX_2 }, .normal = NORMAL_LEFT }, /* Left */
        FaceDef{ .corners = { CORNER_INDEX_1, CORNER_INDEX_5, CORNER_INDEX_3, CORNER_INDEX_7 }, .normal = NORMAL_RIGHT },  /* Right */
        FaceDef{ .corners = { CORNER_INDEX_4, CORNER_INDEX_5, CORNER_INDEX_0, CORNER_INDEX_1 }, .normal = NORMAL_TOP },  /* Top */
        FaceDef{ .corners = { CORNER_INDEX_2, CORNER_INDEX_3, CORNER_INDEX_6, CORNER_INDEX_7 }, .normal = NORMAL_BOTTOM }, /* Bottom */
    };

    const uint32_t topColor = selected ? SELECTED_TOP_COLOR_ABGR : DEFAULT_TOP_COLOR_ABGR;
    const uint32_t bottomColor = selected ? SELECTED_BOTTOM_COLOR_ABGR : DEFAULT_BOTTOM_COLOR_ABGR;

    BuiltMeshData built{};
    built.builtRevision = revision;
    built.vertices.reserve(CUBE_FACE_COUNT * FACE_VERTEX_COUNT);
    built.indices.reserve(CUBE_FACE_COUNT * TRIANGLE_INDEX_COUNT_PER_FACE);

    /* Emit one independent quad per cube face for stable normals/UVs. */
    for (const auto &face : FACES)
    {
        const auto base = static_cast<uint16_t>(built.vertices.size());
        const uint32_t faceColor = face.normal.y > TOP_FACE_NORMAL_THRESHOLD ? topColor : bottomColor;

        static constexpr std::array<std::array<float, 2>, FACE_CORNER_COUNT> UVS = {
            UV_00,
            UV_10,
            UV_01,
            UV_11,
        };

        for (std::size_t corner = ZERO_SIZE; corner < FACE_CORNER_COUNT; ++corner)
        {
            const bx::Vec3 &p = positions.at(face.corners.at(corner));
            built.vertices.push_back(PackedVertex{
                .x = p.x,
                .y = p.y,
                .z = p.z,
                .normalAbgr = packNormal(face.normal),
                .u = UVS.at(corner).at(UV_COMPONENT_U),
                .v = UVS.at(corner).at(UV_COMPONENT_V),
                .colorAbgr = faceColor,
            });
        }

        built.indices.push_back(base + FACE_INDEX_0);
        built.indices.push_back(base + FACE_INDEX_1);
        built.indices.push_back(base + FACE_INDEX_2);
        built.indices.push_back(base + FACE_INDEX_1);
        built.indices.push_back(base + FACE_INDEX_3);
        built.indices.push_back(base + FACE_INDEX_2);
    }

    return built;
}

/**
 * Marks mesh data dirty and schedules an asynchronous rebuild.
 */
void Core::markMeshDirtyLocked()
{
    ++mEditRevision;
    mBuildPending = true;
}

} // namespace Scene
