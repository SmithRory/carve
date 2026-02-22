#include "Selection.h"
#include "scene/TopologyUtils.h"

#include <array>
#include <bx/math.h>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr float CAMERA_FOV_DEGREES = 60.0F;
constexpr float INTERSECTION_EPSILON = 0.000001F;
constexpr float VERTEX_PICK_RADIUS_PIXELS = 18.0F;
constexpr float EDGE_PICK_RADIUS_PIXELS = 16.0F;

uint32_t edgeKey(uint16_t a, uint16_t b)
{
    const uint16_t lo = bx::min(a, b);
    const uint16_t hi = bx::max(a, b);
    return (static_cast<uint32_t>(lo) << 16U) | static_cast<uint32_t>(hi);
}

bool intersectRayAabb(const bx::Vec3 &origin, const bx::Vec3 &direction, const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds, float &outT)
{
    float tMin = 0.0F;
    float tMax = std::numeric_limits<float>::max();

    const std::array<float, 3> originValues{ origin.x, origin.y, origin.z };
    const std::array<float, 3> directionValues{ direction.x, direction.y, direction.z };
    const std::array<float, 3> minValues{ minBounds.x, minBounds.y, minBounds.z };
    const std::array<float, 3> maxValues{ maxBounds.x, maxBounds.y, maxBounds.z };

    for (std::size_t axis = 0U; axis < originValues.size(); ++axis)
    {
        const float dir = directionValues[axis];
        if (bx::abs(dir) < INTERSECTION_EPSILON)
        {
            if (originValues[axis] < minValues[axis] || originValues[axis] > maxValues[axis])
            {
                return false;
            }
            continue;
        }

        const float invDir = 1.0F / dir;
        float t0 = (minValues[axis] - originValues[axis]) * invDir;
        float t1 = (maxValues[axis] - originValues[axis]) * invDir;
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }

        tMin = bx::max(tMin, t0);
        tMax = bx::min(tMax, t1);
        if (tMin > tMax)
        {
            return false;
        }
    }

    outT = tMin;
    return true;
}

bool projectToScreen(const bx::Vec3 &worldPoint, const bx::Vec3 &cameraPosition, const bx::Vec3 &forward, const bx::Vec3 &right, const bx::Vec3 &up, float viewportWidth, float viewportHeight, float &outX, float &outY)
{
    /* Project a world-space point with the current camera basis into viewport pixel coordinates. */
    const bx::Vec3 toPoint = bx::sub(worldPoint, cameraPosition);
    const float viewX = bx::dot(toPoint, right);
    const float viewY = bx::dot(toPoint, up);
    const float viewZ = bx::dot(toPoint, forward);
    if (viewZ <= INTERSECTION_EPSILON)
    {
        return false;
    }

    const float aspect = viewportWidth / viewportHeight;
    const float tanHalfFov = bx::tan(bx::toRad(CAMERA_FOV_DEGREES) * 0.5F);
    const float ndcX = viewX / (viewZ * tanHalfFov * aspect);
    const float ndcY = viewY / (viewZ * tanHalfFov);

    outX = (ndcX + 1.0F) * 0.5F * viewportWidth;
    outY = (1.0F - ndcY) * 0.5F * viewportHeight;
    return true;
}

float pointSegmentDistanceSquared(float px, float py, float ax, float ay, float bx, float by)
{
    /* Compute squared screen-space distance from a point to a line segment for edge hit-testing. */
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float abLenSq = (abx * abx) + (aby * aby);
    if (abLenSq <= INTERSECTION_EPSILON)
    {
        const float dx = px - ax;
        const float dy = py - ay;
        return (dx * dx) + (dy * dy);
    }

    const float t = bx::clamp(((apx * abx) + (apy * aby)) / abLenSq, 0.0F, 1.0F);
    const float cx = ax + (abx * t);
    const float cy = ay + (aby * t);
    const float dx = px - cx;
    const float dy = py - cy;
    return (dx * dx) + (dy * dy);
}

bool intersectRayTriangle(
    const bx::Vec3 &origin,
    const bx::Vec3 &direction,
    const bx::Vec3 &v0,
    const bx::Vec3 &v1,
    const bx::Vec3 &v2,
    float &outT)
{
    const bx::Vec3 e1 = bx::sub(v1, v0);
    const bx::Vec3 e2 = bx::sub(v2, v0);
    const bx::Vec3 p = bx::cross(direction, e2);
    const float det = bx::dot(e1, p);
    if (bx::abs(det) < INTERSECTION_EPSILON)
    {
        return false;
    }

    const float invDet = 1.0F / det;
    const bx::Vec3 tVec = bx::sub(origin, v0);
    const float u = bx::dot(tVec, p) * invDet;
    if (u < 0.0F || u > 1.0F)
    {
        return false;
    }

    const bx::Vec3 q = bx::cross(tVec, e1);
    const float v = bx::dot(direction, q) * invDet;
    if (v < 0.0F || (u + v) > 1.0F)
    {
        return false;
    }

    const float t = bx::dot(e2, q) * invDet;
    if (t <= INTERSECTION_EPSILON)
    {
        return false;
    }

    outT = t;
    return true;
}

struct PickContext
{
    bx::Vec3 cameraPosition{ 0.0F, 0.0F, 0.0F };
    bx::Vec3 forward{ 0.0F, 0.0F, 0.0F };
    bx::Vec3 right{ 0.0F, 0.0F, 0.0F };
    bx::Vec3 up{ 0.0F, 0.0F, 0.0F };
    bx::Vec3 rayDirection{ 0.0F, 0.0F, 0.0F };
    float viewportWidth{};
    float viewportHeight{};
};

struct TriangleHit
{
    uint16_t faceIndex{};
    float t{};
    std::array<uint16_t, 3> triangleVertexIndices{ 0U, 0U, 0U };
};

struct EdgeCandidate
{
    uint16_t index{};
    const Scene::Edge *edge{};
};

PickContext buildPickContext(const Scene::CameraParameters &cameraParameters, const Scene::MousePosition &mousePosition, float viewportWidth, float viewportHeight)
{
    const float cosPitch = bx::cos(cameraParameters.pitchRadians);
    const bx::Vec3 forward(
        bx::sin(cameraParameters.yawRadians) * cosPitch,
        bx::sin(cameraParameters.pitchRadians),
        bx::cos(cameraParameters.yawRadians) * cosPitch);

    bx::Vec3 right = bx::cross(bx::Vec3{ 0.0F, 1.0F, 0.0F }, forward);
    const float rightLength = bx::length(right);
    if (rightLength < INTERSECTION_EPSILON)
    {
        right = bx::Vec3{ 1.0F, 0.0F, 0.0F };
    }
    else
    {
        right = bx::mul(right, 1.0F / rightLength);
    }

    const bx::Vec3 up = bx::normalize(bx::cross(forward, right));
    const float ndcX = ((2.0F * mousePosition.x) / viewportWidth) - 1.0F;
    const float ndcY = 1.0F - ((2.0F * mousePosition.y) / viewportHeight);
    const float aspect = viewportWidth / viewportHeight;
    const float tanHalfFov = bx::tan(bx::toRad(CAMERA_FOV_DEGREES) * 0.5F);

    bx::Vec3 rayDirection = bx::add(
        forward,
        bx::add(
            bx::mul(right, ndcX * aspect * tanHalfFov),
            bx::mul(up, ndcY * tanHalfFov)));
    rayDirection = bx::normalize(rayDirection);

    return PickContext{
        .cameraPosition = cameraParameters.position,
        .forward = forward,
        .right = right,
        .up = up,
        .rayDirection = rayDirection,
        .viewportWidth = viewportWidth,
        .viewportHeight = viewportHeight,
    };
}

bool projectVertexToScreen(const Scene::EditableObject &object, uint16_t vertexIndex, const PickContext &pickContext, float &outX, float &outY)
{
    if (vertexIndex >= object.localVertices.size())
    {
        return false;
    }

    return projectToScreen(
        bx::add(object.position, object.localVertices[vertexIndex]),
        pickContext.cameraPosition,
        pickContext.forward,
        pickContext.right,
        pickContext.up,
        pickContext.viewportWidth,
        pickContext.viewportHeight,
        outX,
        outY);
}

std::optional<TriangleHit> findNearestTriangleHit(const Scene::EditableObject &object, const bx::Vec3 &cameraPosition, const bx::Vec3 &rayDirection)
{
    std::optional<TriangleHit> bestHit;

    for (const std::size_t faceIndex : std::views::iota(std::size_t{ 0U }, object.faces.size()))
    {
        const Scene::Face &face = object.faces[faceIndex];
        Scene::forEachFaceTriangle(face, [&](const std::array<Scene::TopologyIndex, 3> &triangle)
        {
            const auto [i0, i1, i2] = triangle;
            if (i0 >= object.localVertices.size()
                || i1 >= object.localVertices.size()
                || i2 >= object.localVertices.size())
            {
                return;
            }

            const bx::Vec3 w0 = bx::add(object.position, object.localVertices[i0]);
            const bx::Vec3 w1 = bx::add(object.position, object.localVertices[i1]);
            const bx::Vec3 w2 = bx::add(object.position, object.localVertices[i2]);
            float t = 0.0F;
            if (!intersectRayTriangle(cameraPosition, rayDirection, w0, w1, w2, t))
            {
                return;
            }

            if (bestHit.has_value() && t >= bestHit->t)
            {
                return;
            }

            bestHit = TriangleHit{
                .faceIndex = static_cast<uint16_t>(faceIndex),
                .t = t,
                .triangleVertexIndices = { i0, i1, i2 },
            };
        });
    }

    return bestHit;
}

std::unordered_map<uint32_t, uint16_t> buildEdgeLookup(const Scene::EditableObject &object)
{
    std::unordered_map<uint32_t, uint16_t> edgeLookup;
    edgeLookup.reserve(object.edges.size());
    for (const std::size_t edgeIndex : std::views::iota(std::size_t{ 0U }, object.edges.size()))
    {
        if (edgeIndex > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()))
        {
            break;
        }

        const uint16_t e0 = object.edges[edgeIndex][0];
        const uint16_t e1 = object.edges[edgeIndex][1];
        edgeLookup[edgeKey(e0, e1)] = static_cast<uint16_t>(edgeIndex);
    }

    return edgeLookup;
}

std::optional<uint16_t> findEdgeIndexForVertices(const std::unordered_map<uint32_t, uint16_t> &edgeLookup, uint16_t a, uint16_t b)
{
    const auto found = edgeLookup.find(edgeKey(a, b));
    if (found == edgeLookup.end())
    {
        return std::nullopt;
    }

    return found->second;
}

std::vector<EdgeCandidate> buildAllEdgeCandidates(const Scene::EditableObject &object)
{
    std::vector<EdgeCandidate> candidates;
    candidates.reserve(object.edges.size());
    for (const std::size_t edgeIndex : std::views::iota(std::size_t{ 0U }, object.edges.size()))
    {
        if (edgeIndex > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()))
        {
            break;
        }

        const Scene::Edge &edge = object.edges[edgeIndex];
        candidates.push_back(EdgeCandidate{
            .index = static_cast<uint16_t>(edgeIndex),
            .edge = &edge,
        });
    }

    return candidates;
}

std::vector<uint16_t> buildAllVertexIndices(const Scene::EditableObject &object)
{
    std::vector<uint16_t> vertices;
    vertices.reserve(object.localVertices.size());
    for (const std::size_t vertexIndex : std::views::iota(std::size_t{ 0U }, object.localVertices.size()))
    {
        if (vertexIndex > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max()))
        {
            break;
        }

        vertices.push_back(static_cast<uint16_t>(vertexIndex));
    }

    return vertices;
}

void updateBestByDistance(std::optional<std::pair<float, uint16_t>> &best, float distSq, uint16_t index)
{
    if (!best.has_value() || distSq < best->first)
    {
        best = std::pair<float, uint16_t>{ distSq, index };
    }
}

std::optional<uint16_t> pickNearestVertex(const Scene::EditableObject &object, std::span<const uint16_t> candidateVertices, const PickContext &pickContext, const Scene::MousePosition &mousePosition)
{
    std::optional<std::pair<float, uint16_t>> bestVertex;
    const float pickRadiusSq = VERTEX_PICK_RADIUS_PIXELS * VERTEX_PICK_RADIUS_PIXELS;

    const auto evaluateVertex = [&](uint16_t vertexIndex)
    {
        float sx = 0.0F;
        float sy = 0.0F;
        if (!projectVertexToScreen(object, vertexIndex, pickContext, sx, sy))
        {
            return;
        }

        const float dx = sx - mousePosition.x;
        const float dy = sy - mousePosition.y;
        const float distSq = (dx * dx) + (dy * dy);
        if (distSq <= pickRadiusSq)
        {
            updateBestByDistance(bestVertex, distSq, vertexIndex);
        }
    };

    if (candidateVertices.empty())
    {
        for (const uint16_t vertexIndex : buildAllVertexIndices(object))
        {
            evaluateVertex(vertexIndex);
        }
    }
    else
    {
        for (const uint16_t vertexIndex : candidateVertices)
        {
            evaluateVertex(vertexIndex);
        }
    }

    return bestVertex.has_value() ? std::optional<uint16_t>{ bestVertex->second } : std::nullopt;
}

std::optional<uint16_t> pickNearestEdge(const Scene::EditableObject &object, std::span<const EdgeCandidate> candidateEdges, const PickContext &pickContext, const Scene::MousePosition &mousePosition)
{
    std::optional<std::pair<float, uint16_t>> bestEdge;
    const float pickRadiusSq = EDGE_PICK_RADIUS_PIXELS * EDGE_PICK_RADIUS_PIXELS;

    const auto evaluateEdge = [&](const EdgeCandidate &candidate)
    {
        if (candidate.edge == nullptr)
        {
            return;
        }

        const Scene::Edge &edge = *candidate.edge;
        float ax = 0.0F;
        float ay = 0.0F;
        float bx2 = 0.0F;
        float by2 = 0.0F;
        if (!projectVertexToScreen(object, edge[0], pickContext, ax, ay)
            || !projectVertexToScreen(object, edge[1], pickContext, bx2, by2))
        {
            return;
        }

        const float distSq = pointSegmentDistanceSquared(mousePosition.x, mousePosition.y, ax, ay, bx2, by2);
        if (distSq <= pickRadiusSq)
        {
            updateBestByDistance(bestEdge, distSq, candidate.index);
        }
    };

    for (const EdgeCandidate &candidate : candidateEdges)
    {
        evaluateEdge(candidate);
    }

    return bestEdge.has_value() ? std::optional<uint16_t>{ bestEdge->second } : std::nullopt;
}

} // namespace

namespace Scene
{

/**
 * Performs screen-space ray selection against object bounds.
 * @param[in] document Document containing selection candidates.
 * @param[in] cameraParameters Camera position/orientation.
 * @param[in] mousePosition Cursor position in pixels.
 * @param[in] viewportWidth Viewport width in pixels.
 * @param[in] viewportHeight Viewport height in pixels.
 * @return Hit object id when an object is under the cursor, otherwise std::nullopt.
 */
std::optional<ObjectId> selectObjectFromScreen(const Document &document, const CameraParameters &cameraParameters, const MousePosition &mousePosition, float viewportWidth, float viewportHeight)
{
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F)
    {
        return std::nullopt;
    }

    const PickContext pickContext = buildPickContext(cameraParameters, mousePosition, viewportWidth, viewportHeight);

    float bestT = std::numeric_limits<float>::max();
    ObjectId bestId = 0U;

    for (const EditableObject &object : document.objects())
    {
        if (object.localVertices.empty())
        {
            continue;
        }

        bx::Vec3 minBounds = bx::add(object.position, object.localVertices.front());
        bx::Vec3 maxBounds = minBounds;
        for (const bx::Vec3 &localVertex : object.localVertices)
        {
            const bx::Vec3 worldVertex = bx::add(object.position, localVertex);
            minBounds.x = bx::min(minBounds.x, worldVertex.x);
            minBounds.y = bx::min(minBounds.y, worldVertex.y);
            minBounds.z = bx::min(minBounds.z, worldVertex.z);
            maxBounds.x = bx::max(maxBounds.x, worldVertex.x);
            maxBounds.y = bx::max(maxBounds.y, worldVertex.y);
            maxBounds.z = bx::max(maxBounds.z, worldVertex.z);
        }

        float tHit = 0.0F;
        if (intersectRayAabb(cameraParameters.position, pickContext.rayDirection, minBounds, maxBounds, tHit) && tHit >= 0.0F && tHit < bestT)
        {
            bestT = tHit;
            bestId = object.id;
        }
    }

    if (bestId == 0U)
    {
        return std::nullopt;
    }

    return bestId;
}

/**
 * Performs component picking for one editable object using screen-space cursor position.
 * @param[in] object Editable object to test.
 * @param[in] cameraParameters Camera position/orientation.
 * @param[in] mousePosition Cursor position in pixels.
 * @param[in] viewportWidth Viewport width in pixels.
 * @param[in] viewportHeight Viewport height in pixels.
 * @return Hit component selection when found, otherwise std::nullopt.
 */
std::optional<ComponentSelection> selectComponentFromScreen(const EditableObject &object, const CameraParameters &cameraParameters, const MousePosition &mousePosition, float viewportWidth, float viewportHeight)
{
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F || object.localVertices.empty())
    {
        return std::nullopt;
    }

    const PickContext pickContext = buildPickContext(cameraParameters, mousePosition, viewportWidth, viewportHeight);
    const std::unordered_map<uint32_t, uint16_t> edgeLookup = buildEdgeLookup(object);

    const std::optional<TriangleHit> hit = findNearestTriangleHit(object, cameraParameters.position, pickContext.rayDirection);
    if (hit.has_value())
    {
        if (const std::optional<uint16_t> bestVertex = pickNearestVertex(object, std::span<const uint16_t>(hit->triangleVertexIndices), pickContext, mousePosition); bestVertex.has_value())
        {
            return ComponentSelection{ .type = ComponentType::Vertex, .index = *bestVertex };
        }

        const std::array<std::array<uint16_t, 2>, 3> triEdges = {
            std::array<uint16_t, 2>{ hit->triangleVertexIndices[0], hit->triangleVertexIndices[1] },
            std::array<uint16_t, 2>{ hit->triangleVertexIndices[1], hit->triangleVertexIndices[2] },
            std::array<uint16_t, 2>{ hit->triangleVertexIndices[2], hit->triangleVertexIndices[0] },
        };

        std::array<EdgeCandidate, 3> triangleEdgeCandidates{};
        std::size_t triangleEdgeCount = 0U;
        for (const auto &triEdge : triEdges)
        {
            if (const auto edgeIndex = findEdgeIndexForVertices(edgeLookup, triEdge[0], triEdge[1]))
            {
                triangleEdgeCandidates[triangleEdgeCount] = EdgeCandidate{
                    .index = *edgeIndex,
                    .edge = &object.edges[*edgeIndex],
                };
                ++triangleEdgeCount;
            }
        }

        if (const std::optional<uint16_t> bestEdge = pickNearestEdge(object, std::span<const EdgeCandidate>(triangleEdgeCandidates.data(), triangleEdgeCount), pickContext, mousePosition); bestEdge.has_value())
        {
            return ComponentSelection{ .type = ComponentType::Edge, .index = *bestEdge };
        }

        return ComponentSelection{ .type = ComponentType::Face, .index = hit->faceIndex };
    }

    /* Vertex selection takes precedence over edges for precise component targeting. */
    if (const std::optional<uint16_t> bestVertex = pickNearestVertex(object, std::span<const uint16_t>{}, pickContext, mousePosition); bestVertex.has_value())
    {
        return ComponentSelection{ .type = ComponentType::Vertex, .index = *bestVertex };
    }

    /* Edge selection uses shortest screen-space distance to the projected edge segment. */
    const std::vector<EdgeCandidate> allEdgeCandidates = buildAllEdgeCandidates(object);

    if (const std::optional<uint16_t> bestEdge = pickNearestEdge(object, allEdgeCandidates, pickContext, mousePosition); bestEdge.has_value())
    {
        return ComponentSelection{ .type = ComponentType::Edge, .index = *bestEdge };
    }

    return std::nullopt;
}

} // namespace Scene
