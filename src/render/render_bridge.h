#pragma once

#include <array>
#include <cstdint>

#include <bgfx/bgfx.h>

#include "scene/scene_core.h"

class RenderBridge final
{
public:
    RenderBridge();
    ~RenderBridge();

    RenderBridge(const RenderBridge&) = delete;
    RenderBridge& operator=(const RenderBridge&) = delete;

    bool initialize(void* nativeWindowHandle, std::uint32_t width, std::uint32_t height);
    void shutdown();
    void resize(std::uint16_t width, std::uint16_t height);
    void render(const SceneCore::RenderSnapshot& sceneSnapshot);

    bool initialized() const;

private:
    bool ensureMeshCapacity(std::uint32_t vertexCount, std::uint32_t indexCount);
    void uploadRenderMesh(const SceneCore::BuiltMeshData& meshData);
    void updateViewportUniforms(const SceneCore::RenderSnapshot& sceneSnapshot);

    bool m_initialized;
    std::uint16_t m_width;
    std::uint16_t m_height;

    bgfx::VertexLayout m_vertexLayout;
    bgfx::DynamicVertexBufferHandle m_vertexBuffer;
    bgfx::DynamicIndexBufferHandle m_indexBuffer;
    bgfx::ProgramHandle m_programViewport;
    bgfx::UniformHandle m_uniformParams;
    bgfx::UniformHandle m_samplerRadiance;
    bgfx::UniformHandle m_samplerIrradiance;
    bgfx::TextureHandle m_radianceTexture;
    bgfx::TextureHandle m_irradianceTexture;
    std::array<std::array<float, 4>, 12> m_iblParams;

    std::uint32_t m_vertexCapacity;
    std::uint32_t m_indexCapacity;
    std::uint32_t m_activeVertexCount;
    std::uint32_t m_activeIndexCount;
    std::uint64_t m_uploadedRevision;
};
