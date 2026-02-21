#include "MeshBuilder.h"

#include <array>
#include <bx/math.h>
#include <bx/pixelformat.h>
#include <cmath>
#include <unordered_set>

namespace
{
constexpr float NORMAL_MAP_SCALE = 0.5F;
constexpr float NORMAL_MAP_BIAS = 0.5F;
constexpr float NORMAL_MAP_ALPHA = 1.0F;
constexpr std::size_t VECTOR_COMPONENT_COUNT = 4U;
constexpr uint32_t ZERO_PACKED_NORMAL = 0U;

constexpr uint32_t SELECTED_OBJECT_COLOR_ABGR = 0xffffffffU;
constexpr uint32_t DEFAULT_OBJECT_COLOR_ABGR = 0xffffffffU;
constexpr uint32_t SELECTED_FACE_COLOR_ABGR = 0xff4ca3ffU;

constexpr uint16_t EDGE_PRISM_VERTEX_COUNT = 8U;
constexpr uint16_t EDGE_PRISM_INDEX_COUNT = 36U;
constexpr uint16_t SPHERE_RING_COUNT = 6U;
constexpr uint16_t SPHERE_SEGMENT_COUNT = 10U;
constexpr uint16_t SPHERE_VERTEX_COUNT = (SPHERE_RING_COUNT + 1U) * SPHERE_SEGMENT_COUNT;
constexpr uint16_t SPHERE_INDEX_COUNT = SPHERE_RING_COUNT * SPHERE_SEGMENT_COUNT * 6U;
constexpr float SELECTION_EDGE_HALF_THICKNESS = 0.002F;
constexpr float SELECTION_EDGE_OUTWARD_OFFSET = 0.0025F;
constexpr float SELECTION_VERTEX_MARKER_OUTWARD_OFFSET = 0.001F;
constexpr float SELECTION_VERTEX_MARKER_RADIUS = 0.015F;
constexpr float NEAR_ZERO_EPSILON = 0.000001F;
constexpr uint32_t OVERLAY_COLOR_DEFAULT_ABGR = 0xffffffffU;
constexpr uint32_t OVERLAY_COLOR_SELECTED_ABGR = 0xff0080ffU;

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

struct SphereTemplate
{
    std::array<std::array<float, 3>, SPHERE_VERTEX_COUNT> unitVertices{};
    std::array<uint16_t, SPHERE_INDEX_COUNT> indices{};
};

const SphereTemplate &getSphereTemplate()
{
    static const SphereTemplate templateData = []
    {
        SphereTemplate sphere{};

        uint16_t vertexCursor = 0U;
        for (uint16_t ring = 0U; ring <= SPHERE_RING_COUNT; ++ring)
        {
            const float theta = (static_cast<float>(ring) / static_cast<float>(SPHERE_RING_COUNT)) * bx::kPi;
            const float sinTheta = bx::sin(theta);
            const float cosTheta = bx::cos(theta);
            for (uint16_t segment = 0U; segment < SPHERE_SEGMENT_COUNT; ++segment)
            {
                const float phi = (static_cast<float>(segment) / static_cast<float>(SPHERE_SEGMENT_COUNT)) * bx::kPi2;
                const float x = sinTheta * bx::cos(phi);
                const float z = sinTheta * bx::sin(phi);
                sphere.unitVertices[vertexCursor++] = { x, cosTheta, z };
            }
        }

        uint16_t indexCursor = 0U;
        for (uint16_t ring = 0U; ring < SPHERE_RING_COUNT; ++ring)
        {
            for (uint16_t segment = 0U; segment < SPHERE_SEGMENT_COUNT; ++segment)
            {
                const uint16_t nextSegment = static_cast<uint16_t>((segment + 1U) % SPHERE_SEGMENT_COUNT);
                const uint16_t i0 = static_cast<uint16_t>((ring * SPHERE_SEGMENT_COUNT) + segment);
                const uint16_t i1 = static_cast<uint16_t>((ring * SPHERE_SEGMENT_COUNT) + nextSegment);
                const uint16_t i2 = static_cast<uint16_t>(((ring + 1U) * SPHERE_SEGMENT_COUNT) + segment);
                const uint16_t i3 = static_cast<uint16_t>(((ring + 1U) * SPHERE_SEGMENT_COUNT) + nextSegment);

                sphere.indices[indexCursor++] = i0;
                sphere.indices[indexCursor++] = i2;
                sphere.indices[indexCursor++] = i1;
                sphere.indices[indexCursor++] = i1;
                sphere.indices[indexCursor++] = i2;
                sphere.indices[indexCursor++] = i3;
            }
        }

        return sphere;
    }();

    return templateData;
}

bx::Vec3 computeObjectCenter(const Scene::BuildObject &object)
{
    if (object.localVertices.empty())
    {
        return object.position;
    }

    /* Use the average world-space vertex position as a stable direction anchor for overlay offsets. */
    bx::Vec3 sum{ 0.0F, 0.0F, 0.0F };
    for (const bx::Vec3 &localVertex : object.localVertices)
    {
        sum = bx::add(sum, bx::add(object.position, localVertex));
    }

    return bx::mul(sum, 1.0F / static_cast<float>(object.localVertices.size()));
}

void appendMeshTriangles(Scene::BuiltMeshData &built, const Scene::BuildObject &object)
{
    const uint32_t objectColor = object.selected ? SELECTED_OBJECT_COLOR_ABGR : DEFAULT_OBJECT_COLOR_ABGR;
    const std::unordered_set<Scene::TopologyIndex> selectedFaces(
        object.selectedFaceIndices.begin(),
        object.selectedFaceIndices.end());

    for (Scene::TopologyIndex faceIndex = 0U; faceIndex < object.faces.size(); ++faceIndex)
    {
        const Scene::Face &face = object.faces[faceIndex];
        if (face.size() < 3U)
        {
            continue;
        }

        const uint16_t baseIndex = face[0];
        if (baseIndex >= object.localVertices.size())
        {
            continue;
        }

        const bx::Vec3 p0 = bx::add(object.position, object.localVertices[baseIndex]);
        for (std::size_t i = 1U; i + 1U < face.size(); ++i)
        {
            const uint16_t i1 = face[i];
            const uint16_t i2 = face[i + 1U];
            if (i1 >= object.localVertices.size() || i2 >= object.localVertices.size())
            {
                continue;
            }

            const bx::Vec3 p1 = bx::add(object.position, object.localVertices[i1]);
            const bx::Vec3 p2 = bx::add(object.position, object.localVertices[i2]);

            const bx::Vec3 e1 = bx::sub(p1, p0);
            const bx::Vec3 e2 = bx::sub(p2, p0);
            bx::Vec3 normal = bx::cross(e1, e2);
            if (bx::dot(normal, normal) < NEAR_ZERO_EPSILON)
            {
                continue;
            }

            /* Emit non-index-shared triangle vertices so each face can keep a flat normal. */
            normal = bx::normalize(normal);

            const uint16_t base = static_cast<uint16_t>(built.vertices.size());
            const uint32_t color = selectedFaces.find(faceIndex) != selectedFaces.end()
                                       ? SELECTED_FACE_COLOR_ABGR
                                       : objectColor;
            const uint32_t packedNormal = packNormal(normal);

            built.vertices.push_back(Scene::PackedVertex{ .x = p0.x, .y = p0.y, .z = p0.z, .normalAbgr = packedNormal, .u = 0.0F, .v = 0.0F, .colorAbgr = color });
            built.vertices.push_back(Scene::PackedVertex{ .x = p1.x, .y = p1.y, .z = p1.z, .normalAbgr = packedNormal, .u = 1.0F, .v = 0.0F, .colorAbgr = color });
            built.vertices.push_back(Scene::PackedVertex{ .x = p2.x, .y = p2.y, .z = p2.z, .normalAbgr = packedNormal, .u = 0.0F, .v = 1.0F, .colorAbgr = color });

            built.indices.push_back(base + 0U);
            built.indices.push_back(base + 1U);
            built.indices.push_back(base + 2U);
        }
    }
}

void appendVertexMarkerSphere(Scene::BuiltMeshData &built, const bx::Vec3 &center, uint32_t colorAbgr)
{
    const SphereTemplate &sphere = getSphereTemplate();
    const uint16_t base = static_cast<uint16_t>(built.selectionOverlayEdgeVertices.size());

    for (const auto &unit : sphere.unitVertices)
    {
        const bx::Vec3 markerPosition = bx::add(
            center,
            bx::mul(bx::Vec3{ unit[0], unit[1], unit[2] }, SELECTION_VERTEX_MARKER_RADIUS));
        built.selectionOverlayEdgeVertices.push_back(Scene::PackedVertex{
            .x = markerPosition.x,
            .y = markerPosition.y,
            .z = markerPosition.z,
            .normalAbgr = ZERO_PACKED_NORMAL,
            .u = 0.0F,
            .v = 0.0F,
            .colorAbgr = colorAbgr,
        });
    }

    for (const uint16_t index : sphere.indices)
    {
        built.selectionOverlayEdgeIndices.push_back(base + index);
    }
}

void appendSelectionOverlay(Scene::BuiltMeshData &built, const Scene::BuildObject &object)
{
    if (!object.selected || object.localVertices.empty())
    {
        return;
    }

    const bx::Vec3 objectCenter = computeObjectCenter(object);
    const std::unordered_set<Scene::TopologyIndex> selectedVertices(
        object.selectedVertexIndices.begin(),
        object.selectedVertexIndices.end());
    const std::unordered_set<Scene::TopologyIndex> selectedEdges(
        object.selectedEdgeIndices.begin(),
        object.selectedEdgeIndices.end());

    auto appendPrismVertex = [&built](const bx::Vec3 &point, uint32_t colorAbgr)
    {
        built.selectionOverlayEdgeVertices.push_back(Scene::PackedVertex{
            .x = point.x,
            .y = point.y,
            .z = point.z,
            .normalAbgr = ZERO_PACKED_NORMAL,
            .u = 0.0F,
            .v = 0.0F,
            .colorAbgr = colorAbgr,
        });
    };

    /* Local index topology for one 8-vertex edge prism primitive; independent of object topology. */
    static constexpr std::array<uint16_t, EDGE_PRISM_INDEX_COUNT> PRISM_INDICES = {
        0, 1, 2, 1, 3, 2,
        4, 6, 5, 5, 6, 7,
        0, 4, 1, 1, 4, 5,
        1, 5, 3, 3, 5, 7,
        3, 7, 2, 2, 7, 6,
        2, 6, 0, 0, 6, 4,
    };

    for (Scene::TopologyIndex edgeIndex = 0U; edgeIndex < object.edges.size(); ++edgeIndex)
    {
        const Scene::Edge &edge = object.edges[edgeIndex];
        const uint32_t edgeColor = selectedEdges.find(edgeIndex) != selectedEdges.end()
                                       ? OVERLAY_COLOR_SELECTED_ABGR
                                       : OVERLAY_COLOR_DEFAULT_ABGR;

        const uint16_t i0 = edge[0];
        const uint16_t i1 = edge[1];
        if (i0 >= object.localVertices.size() || i1 >= object.localVertices.size())
        {
            continue;
        }

        const bx::Vec3 p0 = bx::add(object.position, object.localVertices[i0]);
        const bx::Vec3 p1 = bx::add(object.position, object.localVertices[i1]);
        const bx::Vec3 p0Offset = bx::add(p0, bx::mul(bx::normalize(bx::sub(p0, objectCenter)), SELECTION_EDGE_OUTWARD_OFFSET));
        const bx::Vec3 p1Offset = bx::add(p1, bx::mul(bx::normalize(bx::sub(p1, objectCenter)), SELECTION_EDGE_OUTWARD_OFFSET));
        const bx::Vec3 axis = bx::sub(p1Offset, p0Offset);

        bx::Vec3 axisDirection = axis;
        if (bx::dot(axisDirection, axisDirection) < NEAR_ZERO_EPSILON)
        {
            continue;
        }

        /* Build an orthonormal frame around the edge so thickness is consistent in any orientation. */
        axisDirection = bx::normalize(axisDirection);

        const bx::Vec3 edgeMidpoint = bx::mul(bx::add(p0Offset, p1Offset), 0.5F);
        const bx::Vec3 radialDirection = bx::normalize(bx::sub(edgeMidpoint, objectCenter));

        bx::Vec3 outwardDirection = bx::sub(radialDirection, bx::mul(axisDirection, bx::dot(radialDirection, axisDirection)));
        if (bx::dot(outwardDirection, outwardDirection) < NEAR_ZERO_EPSILON)
        {
            const bx::Vec3 fallbackAxis = (std::fabs(axisDirection.x) < 0.9F)
                                              ? bx::Vec3{ 1.0F, 0.0F, 0.0F }
                                              : bx::Vec3{ 0.0F, 1.0F, 0.0F };
            outwardDirection = bx::cross(axisDirection, fallbackAxis);
        }
        outwardDirection = bx::normalize(outwardDirection);

        bx::Vec3 sideDirection = bx::cross(axisDirection, outwardDirection);
        if (bx::dot(sideDirection, sideDirection) < NEAR_ZERO_EPSILON)
        {
            sideDirection = bx::Vec3{ 0.0F, 0.0F, 1.0F };
        }
        sideDirection = bx::normalize(sideDirection);

        const bx::Vec3 du = bx::mul(outwardDirection, SELECTION_EDGE_HALF_THICKNESS);
        const bx::Vec3 dv = bx::mul(sideDirection, SELECTION_EDGE_HALF_THICKNESS);
        const uint16_t base = static_cast<uint16_t>(built.selectionOverlayEdgeVertices.size());

        appendPrismVertex(bx::sub(bx::sub(p0Offset, du), dv), edgeColor);
        appendPrismVertex(bx::add(bx::sub(p0Offset, dv), du), edgeColor);
        appendPrismVertex(bx::add(bx::sub(p0Offset, du), dv), edgeColor);
        appendPrismVertex(bx::add(bx::add(p0Offset, du), dv), edgeColor);
        appendPrismVertex(bx::sub(bx::sub(p1Offset, du), dv), edgeColor);
        appendPrismVertex(bx::add(bx::sub(p1Offset, dv), du), edgeColor);
        appendPrismVertex(bx::add(bx::sub(p1Offset, du), dv), edgeColor);
        appendPrismVertex(bx::add(bx::add(p1Offset, du), dv), edgeColor);

        for (const uint16_t index : PRISM_INDICES)
        {
            built.selectionOverlayEdgeIndices.push_back(base + index);
        }
    }

    /* Generate per-vertex markers and color them by component selection state. */
    for (Scene::TopologyIndex vertexIndex = 0U; vertexIndex < object.localVertices.size(); ++vertexIndex)
    {
        const bx::Vec3 &localVertex = object.localVertices[vertexIndex];
        const uint32_t vertexColor = selectedVertices.find(vertexIndex) != selectedVertices.end()
                                         ? OVERLAY_COLOR_SELECTED_ABGR
                                         : OVERLAY_COLOR_DEFAULT_ABGR;
        const bx::Vec3 worldPosition = bx::add(object.position, localVertex);
        const bx::Vec3 markerCenter = bx::add(worldPosition, bx::mul(bx::normalize(bx::sub(worldPosition, objectCenter)), SELECTION_VERTEX_MARKER_OUTWARD_OFFSET));
        appendVertexMarkerSphere(built, markerCenter, vertexColor);
    }
}

} // namespace

namespace Scene
{

BuiltMeshData buildMeshFromTicket(const BuildTicket &ticket)
{
    BuiltMeshData built{};
    built.builtRevision = ticket.targetRevision;

    std::size_t totalVertexCount = 0U;
    std::size_t totalIndexCount = 0U;
    std::size_t totalOverlayVertexCount = 0U;
    std::size_t totalOverlayIndexCount = 0U;
    for (const BuildObject &object : ticket.objects)
    {
        std::size_t triangleCount = 0U;
        for (const Scene::Face &face : object.faces)
        {
            if (face.size() >= 3U)
            {
                triangleCount += face.size() - 2U;
            }
        }

        totalVertexCount += triangleCount * 3U;
        totalIndexCount += triangleCount * 3U;
        if (object.selected)
        {
            totalOverlayVertexCount += (object.edges.size() * EDGE_PRISM_VERTEX_COUNT) + (object.localVertices.size() * SPHERE_VERTEX_COUNT);
            totalOverlayIndexCount += (object.edges.size() * EDGE_PRISM_INDEX_COUNT) + (object.localVertices.size() * SPHERE_INDEX_COUNT);
        }
    }

    built.vertices.reserve(totalVertexCount);
    built.indices.reserve(totalIndexCount);
    built.selectionOverlayEdgeVertices.reserve(totalOverlayVertexCount);
    built.selectionOverlayEdgeIndices.reserve(totalOverlayIndexCount);

    for (const BuildObject &object : ticket.objects)
    {
        appendMeshTriangles(built, object);
        appendSelectionOverlay(built, object);
    }

    return built;
}

} // namespace Scene
