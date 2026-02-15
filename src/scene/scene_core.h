#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class SceneCore final
{
  public:
    struct Float3
    {
        float x;
        float y;
        float z;
    };

    struct PackedVertex
    {
        float x;
        float y;
        float z;
        std::uint32_t normalAbgr;
        float u;
        float v;
        std::uint32_t colorAbgr;
    };

    struct BuildTicket
    {
        std::uint64_t targetRevision;
        std::array<Float3, 8> buildPositions;
        bool objectSelected;
    };

    struct BuiltMeshData
    {
        std::uint64_t builtRevision;
        std::vector<PackedVertex> vertices;
        std::vector<std::uint16_t> indices;
    };

    struct RenderSnapshot
    {
        std::uint64_t editRevision;
        std::uint64_t builtRevision;
        bool upToDate;
        bool objectSelected;
        Float3 cameraPosition;
        float cameraYawRadians;
        float cameraPitchRadians;
        std::shared_ptr<const BuiltMeshData> renderMesh;
    };

    enum class CameraMove : std::uint8_t
    {
        Forward,
        Backward,
        Left,
        Right,
    };

    SceneCore();

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
        Float3 position;
        float yawRadians;
        float pitchRadians;
        std::array<bool, 6> moveActive;
    };

    static std::array<Float3, 8> evaluateBuildPositions(const EditableCube &cube);
    static BuiltMeshData packRenderMesh(std::uint64_t revision, const std::array<Float3, 8> &positions, bool selected);

    void markMeshDirtyLocked();
    static std::size_t cameraMoveIndex(CameraMove move);

    mutable std::mutex m_mutex;
    EditableCube m_cube;
    CameraState m_camera;

    std::uint64_t m_editRevision;
    std::uint64_t m_builtRevision;
    bool m_buildPending;
    bool m_buildInFlight;
    std::shared_ptr<const BuiltMeshData> m_renderMesh;
};
