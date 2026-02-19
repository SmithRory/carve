#pragma once

#include <array>
#include <cstdint>

#include <bgfx/bgfx.h>

#include "scene/SceneCore.h"

/**
 * Bridges scene snapshots to bgfx resources and draw submission.
 *
 * RenderBridge is the renderer integration layer used by the viewport widget. It owns bgfx
 * initialization, resize/shutdown handling, and GPU-side mesh resources. Callers provide the
 * latest scene snapshot each frame, and RenderBridge ensures mesh uploads and view uniforms
 * are current before issuing rendering work.
 */
class RenderBridge
{
public:
    static constexpr uint16_t MINIMUM_DIMENSION = 1U;
    static constexpr uint16_t IBL_PARAM_COUNT = 12U;

    RenderBridge() = default;
    ~RenderBridge();

    RenderBridge(const RenderBridge &) = delete;
    RenderBridge &operator=(const RenderBridge &) = delete;

    void initialize(void *nativeWindowHandle, uint32_t width, uint32_t height);
    void shutdown();
    void resize(uint16_t width, uint16_t height);
    void render(const Scene::RenderSnapshot &sceneSnapshot);

private:
    bool ensureMeshCapacity(uint32_t vertexCount, uint32_t indexCount);
    void uploadRenderMesh(const Scene::BuiltMeshData &meshData);
    void updateViewportUniforms(const Scene::RenderSnapshot &sceneSnapshot);

    bool mInitialized = false;
    uint16_t mWidth = MINIMUM_DIMENSION;
    uint16_t mHeight = MINIMUM_DIMENSION;

    bgfx::VertexLayout mVertexLayout;
    bgfx::DynamicVertexBufferHandle mVertexBuffer = BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle mIndexBuffer = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle mProgramViewport = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle mUniformParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle mSamplerRadiance = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle mSamplerIrradiance = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mRadianceTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mIrradianceTexture = BGFX_INVALID_HANDLE;
    std::array<std::array<float, 4>, IBL_PARAM_COUNT> mIblParams{};

    uint32_t mVertexCapacity{};
    uint32_t mIndexCapacity{};
    uint32_t mActiveVertexCount{};
    uint32_t mActiveIndexCount{};
    uint64_t mUploadedRevision{};
};
