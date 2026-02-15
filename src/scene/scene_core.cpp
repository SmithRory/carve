#include "scene_core.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
float clampFloat(float value, float minValue, float maxValue)
{
    return std::clamp(value, minValue, maxValue);
}

SceneCore::Float3 add(const SceneCore::Float3& a, const SceneCore::Float3& b)
{
    return SceneCore::Float3{
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}

SceneCore::Float3 scale(const SceneCore::Float3& value, float scalar)
{
    return SceneCore::Float3{
        .x = value.x * scalar,
        .y = value.y * scalar,
        .z = value.z * scalar,
    };
}

float length(const SceneCore::Float3& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

std::uint32_t packNormal(const SceneCore::Float3& normal)
{
    const auto toUnorm8 = [](float value) -> std::uint32_t {
        const float clamped = std::clamp(value, -1.0F, 1.0F);
        const float mapped = clamped * 0.5F + 0.5F;
        return static_cast<std::uint32_t>(mapped * 255.0F + 0.5F);
    };

    const std::uint32_t x = toUnorm8(normal.x);
    const std::uint32_t y = toUnorm8(normal.y);
    const std::uint32_t z = toUnorm8(normal.z);
    const std::uint32_t w = 255U;
    return x | (y << 8U) | (z << 16U) | (w << 24U);
}

SceneCore::Float3 normalize(const SceneCore::Float3& value)
{
    const float len = length(value);
    if (len <= 0.00001F)
    {
        return SceneCore::Float3{ .x = 0.0F, .y = 0.0F, .z = 0.0F };
    }

    const float inv = 1.0F / len;
    return scale(value, inv);
}
} // namespace

SceneCore::SceneCore()
    : m_cube({
        .halfExtent = 0.5F,
        .topExtrude = 0.0F,
        .selected = false,
        .editActive = false,
        .lastMouseY = 0.0F,
    })
    , m_camera({
        .position = { .x = 0.0F, .y = 0.0F, .z = -4.0F },
        .yawRadians = 0.0F,
        .pitchRadians = 0.0F,
        .moveActive = {},
    })
    , m_editRevision(1)
    , m_builtRevision(0)
    , m_buildPending(true)
    , m_buildInFlight(false)
    , m_renderMesh(nullptr)
{
}

void SceneCore::beginPrimaryEdit(float mouseY)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cube.editActive = true;
    m_cube.lastMouseY = mouseY;

    if (!m_cube.selected)
    {
        m_cube.selected = true;
        markMeshDirtyLocked();
    }
}

void SceneCore::updatePrimaryEdit(float mouseY)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_cube.editActive)
    {
        return;
    }

    const float pixelDelta = m_cube.lastMouseY - mouseY;
    m_cube.lastMouseY = mouseY;

    if (std::abs(pixelDelta) < 0.001F)
    {
        return;
    }

    const float previous = m_cube.topExtrude;
    m_cube.topExtrude = clampFloat(m_cube.topExtrude + pixelDelta * 0.01F, -0.35F, 1.5F);

    if (std::abs(previous - m_cube.topExtrude) > 0.00001F)
    {
        markMeshDirtyLocked();
    }
}

void SceneCore::endPrimaryEdit()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cube.editActive = false;
}

void SceneCore::setCameraMoveState(CameraMove move, bool active)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_camera.moveActive[cameraMoveIndex(move)] = active;
}

void SceneCore::addCameraLookDelta(float deltaX, float deltaY)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    constexpr float sensitivity = 0.0035F;
    m_camera.yawRadians += deltaX * sensitivity;
    m_camera.pitchRadians = clampFloat(m_camera.pitchRadians - deltaY * sensitivity, -1.45F, 1.45F);
}

void SceneCore::tickCamera(float dtSeconds)
{
    if (dtSeconds <= 0.0F)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    const float cosPitch = std::cos(m_camera.pitchRadians);
    const Float3 forward = {
        .x = std::sin(m_camera.yawRadians) * cosPitch,
        .y = std::sin(m_camera.pitchRadians),
        .z = std::cos(m_camera.yawRadians) * cosPitch,
    };
    const Float3 right = {
        .x = std::cos(m_camera.yawRadians),
        .y = 0.0F,
        .z = -std::sin(m_camera.yawRadians),
    };
    const Float3 worldUp = { .x = 0.0F, .y = 1.0F, .z = 0.0F };

    Float3 velocity = { .x = 0.0F, .y = 0.0F, .z = 0.0F };

    if (m_camera.moveActive[cameraMoveIndex(CameraMove::Forward)])
    {
        velocity = add(velocity, forward);
    }
    if (m_camera.moveActive[cameraMoveIndex(CameraMove::Backward)])
    {
        velocity = add(velocity, scale(forward, -1.0F));
    }
    if (m_camera.moveActive[cameraMoveIndex(CameraMove::Right)])
    {
        velocity = add(velocity, right);
    }
    if (m_camera.moveActive[cameraMoveIndex(CameraMove::Left)])
    {
        velocity = add(velocity, scale(right, -1.0F));
    }
    if (m_camera.moveActive[cameraMoveIndex(CameraMove::Up)])
    {
        velocity = add(velocity, worldUp);
    }
    if (m_camera.moveActive[cameraMoveIndex(CameraMove::Down)])
    {
        velocity = add(velocity, scale(worldUp, -1.0F));
    }

    const Float3 velocityDirection = normalize(velocity);
    constexpr float moveSpeedUnitsPerSecond = 4.0F;
    m_camera.position = add(m_camera.position, scale(velocityDirection, moveSpeedUnitsPerSecond * dtSeconds));
}

bool SceneCore::tryStartBuild(BuildTicket& outTicket)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_buildPending || m_buildInFlight)
    {
        return false;
    }

    outTicket.targetRevision = m_editRevision;
    outTicket.buildPositions = evaluateBuildPositions(m_cube);
    outTicket.objectSelected = m_cube.selected;

    m_buildInFlight = true;
    m_buildPending = false;
    return true;
}

SceneCore::BuiltMeshData SceneCore::buildRenderMesh(const BuildTicket& ticket)
{
    return packRenderMesh(ticket.targetRevision, ticket.buildPositions, ticket.objectSelected);
}

void SceneCore::finishBuild(BuiltMeshData built)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (built.builtRevision >= m_builtRevision)
    {
        m_builtRevision = built.builtRevision;
        m_renderMesh = std::make_shared<BuiltMeshData>(std::move(built));
    }

    m_buildInFlight = false;
    if (m_builtRevision < m_editRevision)
    {
        m_buildPending = true;
    }
}

SceneCore::RenderSnapshot SceneCore::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return RenderSnapshot{
        .editRevision = m_editRevision,
        .builtRevision = m_builtRevision,
        .upToDate = (m_editRevision == m_builtRevision),
        .objectSelected = m_cube.selected,
        .cameraPosition = m_camera.position,
        .cameraYawRadians = m_camera.yawRadians,
        .cameraPitchRadians = m_camera.pitchRadians,
        .renderMesh = m_renderMesh,
    };
}

std::array<SceneCore::Float3, 8> SceneCore::evaluateBuildPositions(const EditableCube& cube)
{
    const float h = cube.halfExtent;
    const float topY = h + cube.topExtrude;
    const float bottomY = -h;

    return {
        Float3{ .x = -h, .y = topY, .z = h },
        Float3{ .x = h, .y = topY, .z = h },
        Float3{ .x = -h, .y = bottomY, .z = h },
        Float3{ .x = h, .y = bottomY, .z = h },
        Float3{ .x = -h, .y = topY, .z = -h },
        Float3{ .x = h, .y = topY, .z = -h },
        Float3{ .x = -h, .y = bottomY, .z = -h },
        Float3{ .x = h, .y = bottomY, .z = -h },
    };
}

SceneCore::BuiltMeshData SceneCore::packRenderMesh(std::uint64_t revision, const std::array<Float3, 8>& positions, bool selected)
{
    struct FaceDef
    {
        std::array<std::uint16_t, 4> corners;
        Float3 normal;
    };

    static constexpr std::array<FaceDef, 6> faces = {
        FaceDef{ .corners = { 0, 1, 2, 3 }, .normal = { .x = 0.0F, .y = 0.0F, .z = 1.0F } },  // Front
        FaceDef{ .corners = { 5, 4, 7, 6 }, .normal = { .x = 0.0F, .y = 0.0F, .z = -1.0F } }, // Back
        FaceDef{ .corners = { 4, 0, 6, 2 }, .normal = { .x = -1.0F, .y = 0.0F, .z = 0.0F } }, // Left
        FaceDef{ .corners = { 1, 5, 3, 7 }, .normal = { .x = 1.0F, .y = 0.0F, .z = 0.0F } },  // Right
        FaceDef{ .corners = { 4, 5, 0, 1 }, .normal = { .x = 0.0F, .y = 1.0F, .z = 0.0F } },  // Top
        FaceDef{ .corners = { 2, 3, 6, 7 }, .normal = { .x = 0.0F, .y = -1.0F, .z = 0.0F } }, // Bottom
    };

    const std::uint32_t topColor = selected ? 0xff20a0ffu : 0xff3050d0u;
    const std::uint32_t bottomColor = selected ? 0xff60f0d0u : 0xff80d080u;

    BuiltMeshData built{};
    built.builtRevision = revision;
    built.vertices.reserve(24);
    built.indices.reserve(36);

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex)
    {
        const FaceDef& face = faces[faceIndex];
        const std::uint16_t base = static_cast<std::uint16_t>(built.vertices.size());
        const std::uint32_t faceColor = face.normal.y > 0.5F ? topColor : bottomColor;

        static constexpr std::array<std::array<float, 2>, 4> uvs = {
            std::array<float, 2>{ 0.0F, 0.0F },
            std::array<float, 2>{ 1.0F, 0.0F },
            std::array<float, 2>{ 0.0F, 1.0F },
            std::array<float, 2>{ 1.0F, 1.0F },
        };

        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            const Float3& p = positions[face.corners[corner]];
            built.vertices.push_back(PackedVertex{
                .x = p.x,
                .y = p.y,
                .z = p.z,
                .normalAbgr = packNormal(face.normal),
                .u = uvs[corner][0],
                .v = uvs[corner][1],
                .colorAbgr = faceColor,
            });
        }

        built.indices.push_back(base + 0);
        built.indices.push_back(base + 1);
        built.indices.push_back(base + 2);
        built.indices.push_back(base + 1);
        built.indices.push_back(base + 3);
        built.indices.push_back(base + 2);
    }

    return built;
}

void SceneCore::markMeshDirtyLocked()
{
    ++m_editRevision;
    m_buildPending = true;
}

std::size_t SceneCore::cameraMoveIndex(CameraMove move)
{
    switch (move)
    {
    case CameraMove::Forward:
        return 0;
    case CameraMove::Backward:
        return 1;
    case CameraMove::Left:
        return 2;
    case CameraMove::Right:
        return 3;
    case CameraMove::Up:
        return 4;
    case CameraMove::Down:
        return 5;
    }

    return 0;
}
