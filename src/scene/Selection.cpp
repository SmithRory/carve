#include "Selection.h"

#include <bx/math.h>
#include <limits>
#include <unordered_map>

namespace
{
constexpr float CAMERA_FOV_DEGREES = 60.0F;
constexpr float INTERSECTION_EPSILON = 0.000001F;
constexpr float MISS_T = -1.0F;
constexpr float VERTEX_PICK_RADIUS_PIXELS = 18.0F;
constexpr float EDGE_PICK_RADIUS_PIXELS = 16.0F;

bool intersectRayAabb(const bx::Vec3 &origin, const bx::Vec3 &direction, const bx::Vec3 &minBounds, const bx::Vec3 &maxBounds, float &outT)
{
    float tMin = 0.0F;
    float tMax = std::numeric_limits<float>::max();

    const float originValues[3] = { origin.x, origin.y, origin.z };
    const float directionValues[3] = { direction.x, direction.y, direction.z };
    const float minValues[3] = { minBounds.x, minBounds.y, minBounds.z };
    const float maxValues[3] = { maxBounds.x, maxBounds.y, maxBounds.z };

    for (int axis = 0; axis < 3; ++axis)
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
            const float temp = t0;
            t0 = t1;
            t1 = temp;
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

bool projectToScreen(
    const bx::Vec3 &worldPoint,
    const bx::Vec3 &cameraPosition,
    const bx::Vec3 &forward,
    const bx::Vec3 &right,
    const bx::Vec3 &up,
    float viewportWidth,
    float viewportHeight,
    float &outX,
    float &outY)
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

float edgeFunction(float ax, float ay, float bx, float by, float px, float py)
{
    return ((px - ax) * (by - ay)) - ((py - ay) * (bx - ax));
}

bool pointInTriangle2D(float px, float py, float ax, float ay, float bx, float by, float cx, float cy)
{
    const float e0 = edgeFunction(ax, ay, bx, by, px, py);
    const float e1 = edgeFunction(bx, by, cx, cy, px, py);
    const float e2 = edgeFunction(cx, cy, ax, ay, px, py);
    const bool hasNeg = (e0 < 0.0F) || (e1 < 0.0F) || (e2 < 0.0F);
    const bool hasPos = (e0 > 0.0F) || (e1 > 0.0F) || (e2 > 0.0F);
    return !(hasNeg && hasPos);
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

} // namespace

namespace Scene
{

std::optional<ObjectId> selectObjectFromScreen(
    const Document &document,
    const bx::Vec3 &cameraPosition,
    float cameraYawRadians,
    float cameraPitchRadians,
    float mouseX,
    float mouseY,
    float viewportWidth,
    float viewportHeight)
{
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F)
    {
        return std::nullopt;
    }

    const float ndcX = ((2.0F * mouseX) / viewportWidth) - 1.0F;
    const float ndcY = 1.0F - ((2.0F * mouseY) / viewportHeight);

    const float cosPitch = bx::cos(cameraPitchRadians);
    const bx::Vec3 forward(
        bx::sin(cameraYawRadians) * cosPitch,
        bx::sin(cameraPitchRadians),
        bx::cos(cameraYawRadians) * cosPitch);

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
    const float aspect = viewportWidth / viewportHeight;
    const float tanHalfFov = bx::tan(bx::toRad(CAMERA_FOV_DEGREES) * 0.5F);

    bx::Vec3 rayDirection = bx::add(
        forward,
        bx::add(
            bx::mul(right, ndcX * aspect * tanHalfFov),
            bx::mul(up, ndcY * tanHalfFov)));
    rayDirection = bx::normalize(rayDirection);

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

        float tHit = MISS_T;
        if (intersectRayAabb(cameraPosition, rayDirection, minBounds, maxBounds, tHit) && tHit >= 0.0F && tHit < bestT)
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

std::optional<ComponentSelection> selectComponentFromScreen(
    const EditableObject &object,
    const bx::Vec3 &cameraPosition,
    float cameraYawRadians,
    float cameraPitchRadians,
    float mouseX,
    float mouseY,
    float viewportWidth,
    float viewportHeight)
{
    if (viewportWidth <= 0.0F || viewportHeight <= 0.0F || object.localVertices.empty())
    {
        return std::nullopt;
    }

    const float cosPitch = bx::cos(cameraPitchRadians);
    const bx::Vec3 forward(
        bx::sin(cameraYawRadians) * cosPitch,
        bx::sin(cameraPitchRadians),
        bx::cos(cameraYawRadians) * cosPitch);

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
    const float ndcX = ((2.0F * mouseX) / viewportWidth) - 1.0F;
    const float ndcY = 1.0F - ((2.0F * mouseY) / viewportHeight);
    const float aspect = viewportWidth / viewportHeight;
    const float tanHalfFov = bx::tan(bx::toRad(CAMERA_FOV_DEGREES) * 0.5F);
    bx::Vec3 rayDirection = bx::add(
        forward,
        bx::add(
            bx::mul(right, ndcX * aspect * tanHalfFov),
            bx::mul(up, ndcY * tanHalfFov)));
    rayDirection = bx::normalize(rayDirection);

    uint16_t hitFace = std::numeric_limits<uint16_t>::max();
    float hitT = std::numeric_limits<float>::max();
    std::array<uint16_t, 3> hitTriangleVertexIndices{ 0U, 0U, 0U };
    for (std::size_t faceIndex = 0U; faceIndex < object.faces.size(); ++faceIndex)
    {
        const Face &face = object.faces[faceIndex];
        if (face.size() < 3U)
        {
            continue;
        }

        const uint16_t baseIndex = face[0];
        if (baseIndex >= object.localVertices.size())
        {
            continue;
        }

        const bx::Vec3 w0 = bx::add(object.position, object.localVertices[baseIndex]);
        for (std::size_t i = 1U; i + 1U < face.size(); ++i)
        {
            const uint16_t i1 = face[i];
            const uint16_t i2 = face[i + 1U];
            if (i1 >= object.localVertices.size() || i2 >= object.localVertices.size())
            {
                continue;
            }

            const bx::Vec3 w1 = bx::add(object.position, object.localVertices[i1]);
            const bx::Vec3 w2 = bx::add(object.position, object.localVertices[i2]);
            float t = 0.0F;
            if (intersectRayTriangle(cameraPosition, rayDirection, w0, w1, w2, t) && t < hitT)
            {
                hitT = t;
                hitFace = static_cast<uint16_t>(faceIndex);
                hitTriangleVertexIndices = { baseIndex, i1, i2 };
            }
        }
    }

    std::unordered_map<uint32_t, uint16_t> edgeLookup;
    edgeLookup.reserve(object.edges.size());
    for (uint16_t edgeIndex = 0U; edgeIndex < object.edges.size(); ++edgeIndex)
    {
        const uint16_t a = object.edges[edgeIndex][0];
        const uint16_t b = object.edges[edgeIndex][1];
        const uint16_t lo = bx::min(a, b);
        const uint16_t hi = bx::max(a, b);
        const uint32_t key = (static_cast<uint32_t>(lo) << 16U) | static_cast<uint32_t>(hi);
        edgeLookup[key] = edgeIndex;
    }

    if (hitFace != std::numeric_limits<uint16_t>::max())
    {
        float bestVertexDistSq = VERTEX_PICK_RADIUS_PIXELS * VERTEX_PICK_RADIUS_PIXELS;
        uint16_t bestVertex = std::numeric_limits<uint16_t>::max();
        for (const uint16_t vi : hitTriangleVertexIndices)
        {
            const bx::Vec3 worldVertex = bx::add(object.position, object.localVertices[vi]);
            float sx = 0.0F;
            float sy = 0.0F;
            if (!projectToScreen(worldVertex, cameraPosition, forward, right, up, viewportWidth, viewportHeight, sx, sy))
            {
                continue;
            }

            const float dx = sx - mouseX;
            const float dy = sy - mouseY;
            const float distSq = (dx * dx) + (dy * dy);
            if (distSq < bestVertexDistSq)
            {
                bestVertexDistSq = distSq;
                bestVertex = vi;
            }
        }

        if (bestVertex != std::numeric_limits<uint16_t>::max())
        {
            return ComponentSelection{ .type = ComponentType::Vertex, .index = bestVertex };
        }

        float bestEdgeDistSq = EDGE_PICK_RADIUS_PIXELS * EDGE_PICK_RADIUS_PIXELS;
        uint16_t bestEdge = std::numeric_limits<uint16_t>::max();
        const std::array<std::array<uint16_t, 2>, 3> triEdges = {
            std::array<uint16_t, 2>{ hitTriangleVertexIndices[0], hitTriangleVertexIndices[1] },
            std::array<uint16_t, 2>{ hitTriangleVertexIndices[1], hitTriangleVertexIndices[2] },
            std::array<uint16_t, 2>{ hitTriangleVertexIndices[2], hitTriangleVertexIndices[0] },
        };

        for (const auto &triEdge : triEdges)
        {
            float ax = 0.0F;
            float ay = 0.0F;
            float bx2 = 0.0F;
            float by2 = 0.0F;
            if (!projectToScreen(bx::add(object.position, object.localVertices[triEdge[0]]), cameraPosition, forward, right, up, viewportWidth, viewportHeight, ax, ay)
                || !projectToScreen(bx::add(object.position, object.localVertices[triEdge[1]]), cameraPosition, forward, right, up, viewportWidth, viewportHeight, bx2, by2))
            {
                continue;
            }

            const float distSq = pointSegmentDistanceSquared(mouseX, mouseY, ax, ay, bx2, by2);
            if (distSq >= bestEdgeDistSq)
            {
                continue;
            }

            const uint16_t lo = bx::min(triEdge[0], triEdge[1]);
            const uint16_t hi = bx::max(triEdge[0], triEdge[1]);
            const uint32_t key = (static_cast<uint32_t>(lo) << 16U) | static_cast<uint32_t>(hi);
            const auto it = edgeLookup.find(key);
            if (it == edgeLookup.end())
            {
                continue;
            }

            bestEdgeDistSq = distSq;
            bestEdge = it->second;
        }

        if (bestEdge != std::numeric_limits<uint16_t>::max())
        {
            return ComponentSelection{ .type = ComponentType::Edge, .index = bestEdge };
        }

        return ComponentSelection{ .type = ComponentType::Face, .index = hitFace };
    }

    float bestVertexDistSq = VERTEX_PICK_RADIUS_PIXELS * VERTEX_PICK_RADIUS_PIXELS;
    uint16_t bestVertex = std::numeric_limits<uint16_t>::max();

    /* Vertex selection takes precedence over edges for precise component targeting. */
    for (uint16_t i = 0U; i < object.localVertices.size(); ++i)
    {
        const bx::Vec3 worldVertex = bx::add(object.position, object.localVertices[i]);
        float sx = 0.0F;
        float sy = 0.0F;
        if (!projectToScreen(worldVertex, cameraPosition, forward, right, up, viewportWidth, viewportHeight, sx, sy))
        {
            continue;
        }

        const float dx = sx - mouseX;
        const float dy = sy - mouseY;
        const float distSq = (dx * dx) + (dy * dy);
        if (distSq < bestVertexDistSq)
        {
            bestVertexDistSq = distSq;
            bestVertex = i;
        }
    }

    if (bestVertex != std::numeric_limits<uint16_t>::max())
    {
        return ComponentSelection{ .type = ComponentType::Vertex, .index = bestVertex };
    }

    float bestEdgeDistSq = EDGE_PICK_RADIUS_PIXELS * EDGE_PICK_RADIUS_PIXELS;
    uint16_t bestEdge = std::numeric_limits<uint16_t>::max();

    /* Edge selection uses shortest screen-space distance to the projected edge segment. */
    for (uint16_t edgeIndex = 0U; edgeIndex < object.edges.size(); ++edgeIndex)
    {
        const auto &edge = object.edges[edgeIndex];
        if (edge[0] >= object.localVertices.size() || edge[1] >= object.localVertices.size())
        {
            continue;
        }

        float ax = 0.0F;
        float ay = 0.0F;
        float bx2 = 0.0F;
        float by2 = 0.0F;
        if (!projectToScreen(bx::add(object.position, object.localVertices[edge[0]]), cameraPosition, forward, right, up, viewportWidth, viewportHeight, ax, ay)
            || !projectToScreen(bx::add(object.position, object.localVertices[edge[1]]), cameraPosition, forward, right, up, viewportWidth, viewportHeight, bx2, by2))
        {
            continue;
        }

        const float distSq = pointSegmentDistanceSquared(mouseX, mouseY, ax, ay, bx2, by2);
        if (distSq < bestEdgeDistSq)
        {
            bestEdgeDistSq = distSq;
            bestEdge = edgeIndex;
        }
    }

    if (bestEdge != std::numeric_limits<uint16_t>::max())
    {
        return ComponentSelection{ .type = ComponentType::Edge, .index = bestEdge };
    }

    return std::nullopt;
}

} // namespace Scene
