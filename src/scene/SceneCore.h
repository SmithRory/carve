#pragma once

#include <array>
#include <bx/math.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Scene
{
inline constexpr std::size_t CUBE_CORNER_COUNT = 8U;
inline constexpr std::size_t CAMERA_MOVE_SLOT_COUNT = 6U;

struct PackedVertex
{
    float x;
    float y;
    float z;
    uint32_t normalAbgr;
    float u;
    float v;
    uint32_t colorAbgr;
};

struct BuildTicket
{
    uint64_t targetRevision{};
    std::array<bx::Vec3, CUBE_CORNER_COUNT> buildPositions{
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
        bx::Vec3{ 0.0F, 0.0F, 0.0F },
    };
    bool objectSelected{};
};

struct BuiltMeshData
{
    uint64_t builtRevision;
    std::vector<PackedVertex> vertices;
    std::vector<uint16_t> indices;
};

struct RenderSnapshot
{
    uint64_t editRevision;
    uint64_t builtRevision;
    bool upToDate;
    bool objectSelected;
    bx::Vec3 cameraPosition;
    float cameraYawRadians;
    float cameraPitchRadians;
    std::shared_ptr<const BuiltMeshData> renderMesh;
};

enum class CameraMove : uint8_t
{
    Forward,
    Backward,
    Left,
    Right
};

/**
 * Owns the editor-facing scene state and the build-to-render data lifecycle.
 *
 * Core is the central shared model for the viewport. UI/input code applies edits and
 * camera movement here, background build work consumes build tickets produced by this class,
 * and rendering reads immutable snapshots each frame. This keeps edit operations, async mesh
 * building, and render consumption synchronized around revisioned state.
 */
class Core
{
public:
    Core();

    // Step 1: user edits the build mesh representation.
    void beginPrimaryEdit(float mouseY);
    void updatePrimaryEdit(float mouseY);
    void endPrimaryEdit();

    // Camera controls for navigating the scene.
    void setCameraMoveState(CameraMove move, bool active);
    void addCameraLookDelta(float deltaX, float deltaY);
    void tickCamera(float dtSeconds);

    // Step 2: queue and start build->render conversion work.
    bool tryStartBuild(BuildTicket &outTicket);

    // Step 3: convert build mesh data into upload-ready render buffers.
    static BuiltMeshData buildRenderMesh(const BuildTicket &ticket);

    // Step 4: commit built data so rendering can upload it.
    void finishBuild(BuiltMeshData built);

    // Step 5+: render side consumes this snapshot each frame.
    RenderSnapshot snapshot() const;

private:
    struct EditableCube
    {
        float halfExtent;
        float topExtrude;
        bool selected;
        bool editActive;
        float lastMouseY;
    };

    struct CameraState
    {
        bx::Vec3 position;
        float yawRadians;
        float pitchRadians;
        std::array<bool, CAMERA_MOVE_SLOT_COUNT> moveActive;
    };

    static std::array<bx::Vec3, CUBE_CORNER_COUNT> evaluateBuildPositions(const EditableCube &cube);
    static BuiltMeshData packRenderMesh(uint64_t revision, const std::array<bx::Vec3, CUBE_CORNER_COUNT> &positions, bool selected);

    void markMeshDirtyLocked();

    mutable std::mutex mMutex;
    EditableCube mCube;
    CameraState mCamera;

    uint64_t mEditRevision;
    uint64_t mBuiltRevision;
    bool mBuildPending;
    bool mBuildInFlight;
    std::shared_ptr<const BuiltMeshData> mRenderMesh;
};

} // namespace Scene
