#pragma once

#include <array>
#include <bx/math.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Scene
{
using TopologyIndex = uint16_t;
using Edge = std::pair<uint16_t, uint16_t>;
using Face = std::vector<TopologyIndex>;
using EdgeHash = uint32_t;

constexpr uint32_t edgeHash(uint16_t a, uint16_t b)
{
    /* Canonicalize undirected edge endpoints into a stable 32-bit lookup key. */
    const uint16_t lo = bx::min(a, b);
    const uint16_t hi = bx::max(a, b);
    return (static_cast<uint32_t>(lo) << 16U) | static_cast<uint32_t>(hi);
}

struct MousePosition {
    float x{};
    float y{};
};

struct CameraParameters {
    bx::Vec3 position{ 0.0F, 0.0F, 0.0F };
    float yawRadians{};
    float pitchRadians{};
};

/**
 * Packed vertex layout uploaded to bgfx dynamic buffers.
 */
struct PackedVertex {
    float x;
    float y;
    float z;
    uint32_t normalAbgr;
    float u;
    float v;
    uint32_t colorAbgr;
};

/**
 * Immutable object payload copied into a build ticket.
 */
struct BuildObject {
    uint64_t objectId{};
    bx::Vec3 position{ 0.0F, 0.0F, 0.0F };
    std::vector<bx::Vec3> localVertices;
    std::vector<Face> faces;
    std::vector<Edge> edges;
    std::vector<TopologyIndex> selectedVertexIndices;
    std::vector<TopologyIndex> selectedEdgeIndices;
    std::vector<TopologyIndex> selectedFaceIndices;
    bool selected{};
};

/**
 * Snapshot of document state consumed by the mesh build worker.
 */
struct BuildTicket {
    uint64_t targetRevision{};
    std::vector<BuildObject> objects;
    bool objectSelected{};
};

/**
 * CPU-built mesh buffers tagged with the source edit revision.
 */
struct BuiltMeshData {
    uint64_t builtRevision;
    std::vector<PackedVertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<PackedVertex> selectionOverlayEdgeVertices;
    std::vector<uint16_t> selectionOverlayEdgeIndices;
};

/**
 * Thread-safe scene state consumed by the renderer each frame.
 */
struct RenderSnapshot {
    uint64_t editRevision;
    uint64_t builtRevision;
    bool upToDate;
    bool objectSelected;
    bx::Vec3 cameraPosition;
    float cameraYawRadians;
    float cameraPitchRadians;
    std::shared_ptr<const BuiltMeshData> renderMesh;
};

/**
 * Camera movement directions toggled by keyboard input.
 */
enum class CameraMove : uint8_t {
    Forward,
    Backward,
    Left,
    Right
};

} // namespace Scene
