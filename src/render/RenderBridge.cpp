#include "RenderBridge.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/file.h>
#include <bx/math.h>
#include <bx/readerwriter.h>

namespace
{
constexpr bgfx::ViewId MAIN_VIEW_ID = 0U;
constexpr uint16_t CUBE_TEXTURE_SIDE_PIXELS = 1U;
constexpr uint16_t CUBE_TEXTURE_MIP_COUNT = 1U;
constexpr bool CUBE_TEXTURE_HAS_MIPS = false;
constexpr std::size_t CUBE_FACE_COUNT = 6U;
constexpr std::size_t SHADER_ROOT_COUNT = 3U;
constexpr std::size_t RGBA_CHANNEL_COUNT = 4U;
constexpr std::size_t CHANNEL_RED_INDEX = 0U;
constexpr std::size_t CHANNEL_GREEN_INDEX = 1U;
constexpr std::size_t CHANNEL_BLUE_INDEX = 2U;
constexpr std::size_t CHANNEL_ALPHA_INDEX = 3U;
constexpr uint16_t VERTEX_POSITION_COMPONENTS = 3U;
constexpr uint16_t VERTEX_NORMAL_COMPONENTS = 4U;
constexpr uint16_t VERTEX_TEXCOORD_COMPONENTS = 2U;
constexpr uint16_t VERTEX_COLOR_COMPONENTS = 4U;
constexpr uint8_t TEXTURE_STAGE_RADIANCE = 0U;
constexpr uint8_t TEXTURE_STAGE_IRRADIANCE = 1U;
constexpr uint8_t VERTEX_STREAM_MAIN = 0U;
constexpr uint32_t BUFFER_START_OFFSET = 0U;
constexpr uint16_t VIEW_ORIGIN_X = 0U;
constexpr uint16_t VIEW_ORIGIN_Y = 0U;
constexpr uint16_t ZERO_DIMENSION = 0U;
constexpr uint8_t TEXTURE_SKIP = 0U;
constexpr uint16_t MATRIX_ELEMENT_COUNT = 16U;
constexpr float CAMERA_FOV_DEGREES = 60.0F;
constexpr float CAMERA_NEAR_PLANE = 0.1F;
constexpr float CAMERA_FAR_PLANE = 100.0F;
constexpr uint32_t NO_DEPTH_STENCIL_CLEAR = 0U;
constexpr uint32_t ZERO_COUNT = 0U;
constexpr std::size_t ZERO_SIZE = 0U;
constexpr uint16_t DYNAMIC_VERTEX_GROWTH_FACTOR = 2U;
constexpr uint16_t DYNAMIC_INDEX_GROWTH_FACTOR = 2U;
constexpr uint32_t MINIMUM_VERTEX_CAPACITY = 8U;
constexpr uint32_t MINIMUM_INDEX_CAPACITY = 36U;
constexpr uint32_t MINIMUM_SELECTION_OVERLAY_VERTEX_CAPACITY = 8U;
constexpr uint32_t MINIMUM_SELECTION_OVERLAY_EDGE_INDEX_CAPACITY = 24U;
constexpr uint16_t ROOT_SEARCH_CAPACITY = 5U;
constexpr std::int64_t EMPTY_FILE_SIZE = 0;
constexpr bool DESTROY_SHADERS_ON_PROGRAM_DESTROY = true;
constexpr float VIEW_CLEAR_DEPTH = 1.0F;
constexpr uint32_t RESET_FLAGS = BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X4;

constexpr uint8_t RADIANCE_FALLBACK_RED = 166U;
constexpr uint8_t RADIANCE_FALLBACK_GREEN = 181U;
constexpr uint8_t RADIANCE_FALLBACK_BLUE = 204U;
constexpr uint8_t FALLBACK_ALPHA = 255U;
constexpr uint8_t IRRADIANCE_FALLBACK_RED = 84U;
constexpr uint8_t IRRADIANCE_FALLBACK_GREEN = 94U;
constexpr uint8_t IRRADIANCE_FALLBACK_BLUE = 112U;

constexpr uint32_t CLEAR_COLOR_READY = 0x26313dffU;

using IBL_PARAM_VEC4 = std::array<float, 4>;
constexpr IBL_PARAM_VEC4 IBL_PARAM_ZERO{ 0.0F, 0.0F, 0.0F, 0.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_INV_MTX{ 1.0F, 0.0F, 0.0F, 0.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_MTX{ 0.0F, 1.0F, 0.0F, 0.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_LIGHT_MTX{ 0.0F, 0.0F, 1.0F, 0.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_GLOSS_SCALE{ 0.0F, 0.0F, 0.0F, 1.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_MATERIAL{ 0.55F, 0.25F, 0.0F, 0.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_METALLIC{ 0.0F, 0.0F, 0.0F, 0.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_LIGHT_FLAGS{ 1.0F, 1.0F, 1.0F, 1.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_ALBEDO{ 0.68F, 0.70F, 0.72F, 1.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_SPECULAR_TINT{ 1.0F, 1.0F, 1.0F, 1.0F };
constexpr IBL_PARAM_VEC4 IBL_PARAM_LIGHT_COLOR{ 1.0F, 0.98F, 0.95F, 0.0F };
constexpr float IBL_PARAM_POSITION_W = 0.0F;
constexpr float IBL_PARAM_LIGHT_DIRECTION_W = 0.0F;

enum class IblParamSlot : uint8_t
{
    InvMtx,
    Mtx,
    LightMtx,
    GlossScale,
    Material,
    Metallic,
    LightFlags,
    CameraPos,
    Albedo,
    SpecularTint,
    LightDir,
    LightColor
};

constexpr std::size_t IBL_SLOT(IblParamSlot slot)
{
    return static_cast<std::size_t>(slot);
}

/**
 * Maps bgfx renderer type to compiled shader folder name.
 * @param[in] rendererType Active bgfx renderer backend.
 * @return Shader directory name for the backend, or empty if unsupported.
 */
std::string_view shaderDirectoryName(bgfx::RendererType::Enum rendererType)
{
    switch (rendererType)
    {
    case bgfx::RendererType::Direct3D11:
        return "dxbc";
    case bgfx::RendererType::Direct3D12:
        return "dxil";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::OpenGLES:
        return "essl";
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return {};
    }
}

/**
 * Reads a binary file into bgfx-owned memory.
 * @param[in] path Input file path.
 * @return Pointer to copied memory, or nullptr on failure.
 */
const bgfx::Memory *loadShaderMemory(const std::string &path)
{
    bx::FileReader fileReader;
    bx::Error error;

    if (!bx::open(&fileReader, bx::FilePath(path.c_str()), &error) || !error.isOk())
    {
        return nullptr;
    }

    const std::int64_t size64 = bx::getSize(&fileReader);
    if (size64 <= EMPTY_FILE_SIZE || size64 > std::numeric_limits<uint32_t>::max())
    {
        bx::close(&fileReader);
        return nullptr;
    }

    const auto size = static_cast<uint32_t>(size64);
    std::vector<uint8_t> bytes(size);
    const std::int32_t bytesRead = bx::read(&fileReader, bytes.data(), static_cast<std::int32_t>(size), &error);
    bx::close(&fileReader);

    if (!error.isOk() || bytesRead != static_cast<std::int32_t>(size))
    {
        return nullptr;
    }

    return bgfx::copy(bytes.data(), size);
}

/**
 * Loads a single shader binary from a given root.
 * @param[in] shaderRoot Root directory containing shader binaries.
 * @param[in] shaderName File name of the shader binary.
 * @return Valid shader handle on success, invalid handle on failure.
 */
bgfx::ShaderHandle loadShader(const std::filesystem::path &shaderRoot, std::string_view shaderName)
{
    const std::filesystem::path shaderPath = shaderRoot / std::filesystem::path(shaderName);
    const bgfx::Memory *memory = loadShaderMemory(shaderPath.string());
    if (memory == nullptr)
    {
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createShader(memory);
}

/**
 * Loads vertex/fragment shaders and links a bgfx program.
 * @param[in] vsName Vertex shader file name.
 * @param[in] fsName Fragment shader file name.
 * @return Linked program handle, or invalid handle if not found.
 */
bgfx::ProgramHandle loadProgram(std::string_view vsName, std::string_view fsName)
{
    const std::string_view shaderDir = shaderDirectoryName(bgfx::getRendererType());
    if (shaderDir.empty())
    {
        return BGFX_INVALID_HANDLE;
    }

    const std::array<std::filesystem::path, SHADER_ROOT_COUNT> roots = {
        std::filesystem::path("src/render/shaders") / std::filesystem::path(shaderDir),
        std::filesystem::path("shaders") / std::filesystem::path(shaderDir),
        std::filesystem::path(CARVE_SOURCE_DIR) / "src/render/shaders" / std::filesystem::path(shaderDir),
    };

    for (const std::filesystem::path &root : roots)
    {
        const bgfx::ShaderHandle vertexShader = loadShader(root, vsName);
        const bgfx::ShaderHandle fragmentShader = loadShader(root, fsName);

        if (bgfx::isValid(vertexShader) && bgfx::isValid(fragmentShader))
        {
            return bgfx::createProgram(vertexShader, fragmentShader, DESTROY_SHADERS_ON_PROGRAM_DESTROY);
        }

        if (bgfx::isValid(vertexShader))
        {
            bgfx::destroy(vertexShader);
        }
        if (bgfx::isValid(fragmentShader))
        {
            bgfx::destroy(fragmentShader);
        }
    }

    return BGFX_INVALID_HANDLE;
}

/**
 * Creates a 1x1 cube texture with the same RGBA texel on each face.
 * @param[in] r Red channel value.
 * @param[in] g Green channel value.
 * @param[in] b Blue channel value.
 * @param[in] a Alpha channel value.
 * @return Created cube texture handle.
 */
bgfx::TextureHandle createSolidCubeTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    std::array<uint8_t, CUBE_FACE_COUNT * RGBA_CHANNEL_COUNT> texels{};
    for (std::size_t face = ZERO_SIZE; face < CUBE_FACE_COUNT; ++face)
    {
        const std::size_t offset = face * RGBA_CHANNEL_COUNT;
        texels[offset + CHANNEL_RED_INDEX] = r;
        texels[offset + CHANNEL_GREEN_INDEX] = g;
        texels[offset + CHANNEL_BLUE_INDEX] = b;
        texels[offset + CHANNEL_ALPHA_INDEX] = a;
    }

    const bgfx::Memory *memory = bgfx::copy(texels.data(), static_cast<uint32_t>(texels.size()));
    return bgfx::createTextureCube(CUBE_TEXTURE_SIDE_PIXELS, CUBE_TEXTURE_HAS_MIPS, CUBE_TEXTURE_MIP_COUNT, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE | BGFX_SAMPLER_UVW_CLAMP, memory);
}

/**
 * Loads a texture file through bgfx texture decoder.
 * @param[in] texturePath Texture file path.
 * @return Texture handle on success, invalid handle on failure.
 */
bgfx::TextureHandle loadTexture(const std::filesystem::path &texturePath)
{
    const bgfx::Memory *memory = loadShaderMemory(texturePath.string());
    if (memory == nullptr)
    {
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createTexture(memory, BGFX_SAMPLER_UVW_CLAMP, TEXTURE_SKIP, nullptr);
}

/**
 * Attempts to load an IBL texture from several known runtime roots.
 * @param[in] fileName IBL texture file name.
 * @return Texture handle on success, invalid handle if all roots fail.
 */
bgfx::TextureHandle loadIblTexture(std::string_view fileName)
{
    std::vector<std::filesystem::path> roots;
    roots.reserve(ROOT_SEARCH_CAPACITY);
    roots.emplace_back("textures");
    roots.emplace_back("src/render/textures");
    roots.emplace_back(std::filesystem::path(CARVE_SOURCE_DIR) / "src/render/textures");
    roots.emplace_back("/home/rsmith/bgfx/examples/runtime/textures");

    if (const auto *home = std::getenv("HOME"); home != nullptr)
    {
        roots.emplace_back(std::filesystem::path(std::string_view(home)) / "bgfx/examples/runtime/textures");
    }

    for (const std::filesystem::path &root : roots)
    {
        const bgfx::TextureHandle texture = loadTexture(root / std::filesystem::path(fileName));
        if (bgfx::isValid(texture))
        {
            return texture;
        }
    }

    return BGFX_INVALID_HANDLE;
}

} /* namespace */

/**
 * Ensures renderer resources are released on destruction.
 */
RenderBridge::~RenderBridge()
{
    shutdown();
}

/**
 * Initializes bgfx, shader program, uniforms, and IBL textures.
 * @param[in] nativeWindowHandle Native window handle used by bgfx.
 * @param[in] width Initial backbuffer width in pixels.
 * @param[in] height Initial backbuffer height in pixels.
 */
void RenderBridge::initialize(void *nativeWindowHandle, uint32_t width, uint32_t height)
{
    if (mInitialized)
    {
        return;
    }

    bgfx::Init init;
    init.type = bgfx::RendererType::Count;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = RESET_FLAGS;

    bgfx::PlatformData platformData{};
    platformData.nwh = nativeWindowHandle;
    init.platformData = platformData;

    if (!bgfx::init(init))
    {
        std::terminate();
    }
    mInitialized = true;

    mWidth = static_cast<uint16_t>(width == ZERO_DIMENSION ? MINIMUM_DIMENSION : width);
    mHeight = static_cast<uint16_t>(height == ZERO_DIMENSION ? MINIMUM_DIMENSION : height);

    mVertexLayout.begin()
        .add(bgfx::Attrib::Position, VERTEX_POSITION_COMPONENTS, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, VERTEX_NORMAL_COMPONENTS, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, VERTEX_TEXCOORD_COMPONENTS, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, VERTEX_COLOR_COMPONENTS, bgfx::AttribType::Uint8, true)
        .end();

    mProgramViewport = loadProgram("vs_ibl_mesh.bin", "fs_ibl_mesh.bin");
    mProgramSelectionOverlay = loadProgram("vs_selection_overlay.bin", "fs_selection_overlay.bin");
    mUniformParams = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, IBL_PARAM_COUNT);
    mSamplerRadiance = bgfx::createUniform("s_texCube", bgfx::UniformType::Sampler);
    mSamplerIrradiance = bgfx::createUniform("s_texCubeIrr", bgfx::UniformType::Sampler);
    mRadianceTexture = loadIblTexture("bolonga_lod.dds");
    mIrradianceTexture = loadIblTexture("bolonga_irr.dds");
    if (!bgfx::isValid(mRadianceTexture))
    {
        mRadianceTexture = loadIblTexture("kyoto_lod.dds");
    }
    if (!bgfx::isValid(mIrradianceTexture))
    {
        mIrradianceTexture = loadIblTexture("kyoto_irr.dds");
    }
    if (!bgfx::isValid(mRadianceTexture))
    {
        mRadianceTexture = createSolidCubeTexture(RADIANCE_FALLBACK_RED, RADIANCE_FALLBACK_GREEN, RADIANCE_FALLBACK_BLUE, FALLBACK_ALPHA);
    }
    if (!bgfx::isValid(mIrradianceTexture))
    {
        mIrradianceTexture = createSolidCubeTexture(IRRADIANCE_FALLBACK_RED, IRRADIANCE_FALLBACK_GREEN, IRRADIANCE_FALLBACK_BLUE, FALLBACK_ALPHA);
    }

    if (!bgfx::isValid(mProgramViewport)
        || !bgfx::isValid(mProgramSelectionOverlay)
        || !bgfx::isValid(mUniformParams)
        || !bgfx::isValid(mSamplerRadiance)
        || !bgfx::isValid(mSamplerIrradiance)
        || !bgfx::isValid(mRadianceTexture)
        || !bgfx::isValid(mIrradianceTexture))
    {
        shutdown();
        std::terminate();
    }

    bgfx::setViewRect(MAIN_VIEW_ID, VIEW_ORIGIN_X, VIEW_ORIGIN_Y, bgfx::BackbufferRatio::Equal);
}

/**
 * Releases all owned bgfx resources and shuts down bgfx.
 */
void RenderBridge::shutdown()
{
    if (!mInitialized)
    {
        return;
    }

    if (bgfx::isValid(mRadianceTexture))
    {
        bgfx::destroy(mRadianceTexture);
    }
    if (bgfx::isValid(mIrradianceTexture))
    {
        bgfx::destroy(mIrradianceTexture);
    }
    if (bgfx::isValid(mSamplerIrradiance))
    {
        bgfx::destroy(mSamplerIrradiance);
    }
    if (bgfx::isValid(mSamplerRadiance))
    {
        bgfx::destroy(mSamplerRadiance);
    }
    if (bgfx::isValid(mUniformParams))
    {
        bgfx::destroy(mUniformParams);
    }
    if (bgfx::isValid(mProgramViewport))
    {
        bgfx::destroy(mProgramViewport);
    }
    if (bgfx::isValid(mProgramSelectionOverlay))
    {
        bgfx::destroy(mProgramSelectionOverlay);
    }
    if (bgfx::isValid(mIndexBuffer))
    {
        bgfx::destroy(mIndexBuffer);
    }
    if (bgfx::isValid(mVertexBuffer))
    {
        bgfx::destroy(mVertexBuffer);
    }
    if (bgfx::isValid(mSelectionOverlayEdgeIndexBuffer))
    {
        bgfx::destroy(mSelectionOverlayEdgeIndexBuffer);
    }
    if (bgfx::isValid(mSelectionOverlayVertexBuffer))
    {
        bgfx::destroy(mSelectionOverlayVertexBuffer);
    }

    bgfx::shutdown();
    mInitialized = false;
}

/**
 * Resizes the backbuffer and updates view rectangle.
 * @param[in] width New viewport width in pixels.
 * @param[in] height New viewport height in pixels.
 */
void RenderBridge::resize(uint16_t width, uint16_t height)
{
    mWidth = width == ZERO_DIMENSION ? MINIMUM_DIMENSION : width;
    mHeight = height == ZERO_DIMENSION ? MINIMUM_DIMENSION : height;
    bgfx::reset(mWidth, mHeight, RESET_FLAGS);
    bgfx::setViewRect(MAIN_VIEW_ID, VIEW_ORIGIN_X, VIEW_ORIGIN_Y, bgfx::BackbufferRatio::Equal);
}

/**
 * Renders one frame from the current scene snapshot.
 * @param[in] sceneSnapshot Thread-safe scene state for this frame.
 */
void RenderBridge::render(const Scene::RenderSnapshot &sceneSnapshot)
{
    /* Upload mesh buffers only when a newer build revision is available. */
    if (sceneSnapshot.renderMesh != nullptr && sceneSnapshot.renderMesh->builtRevision > mUploadedRevision)
    {
        uploadRenderMesh(*sceneSnapshot.renderMesh);
    }

    bgfx::setViewClear(MAIN_VIEW_ID, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, CLEAR_COLOR_READY, VIEW_CLEAR_DEPTH, NO_DEPTH_STENCIL_CLEAR);

    /* Camera movement affects view matrices only; geometry stays unchanged. */
    const float cosPitch = bx::cos(sceneSnapshot.cameraPitchRadians);
    const bx::Vec3 eye = {
        sceneSnapshot.cameraPosition.x,
        sceneSnapshot.cameraPosition.y,
        sceneSnapshot.cameraPosition.z,
    };
    const bx::Vec3 at = {
        eye.x + (bx::sin(sceneSnapshot.cameraYawRadians) * cosPitch),
        eye.y + bx::sin(sceneSnapshot.cameraPitchRadians),
        eye.z + (bx::cos(sceneSnapshot.cameraYawRadians) * cosPitch),
    };

    std::array<float, MATRIX_ELEMENT_COUNT> view{};
    bx::mtxLookAt(view.data(), eye, at);

    std::array<float, MATRIX_ELEMENT_COUNT> proj{};
    const float aspect = static_cast<float>(mWidth) / static_cast<float>(mHeight);
    bx::mtxProj(proj.data(), CAMERA_FOV_DEGREES, aspect, CAMERA_NEAR_PLANE, CAMERA_FAR_PLANE, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(MAIN_VIEW_ID, view.data(), proj.data());
    bgfx::setViewRect(MAIN_VIEW_ID, VIEW_ORIGIN_X, VIEW_ORIGIN_Y, mWidth, mHeight);
    bgfx::touch(MAIN_VIEW_ID);

    if (bgfx::isValid(mProgramViewport)
        && bgfx::isValid(mUniformParams)
        && bgfx::isValid(mSamplerRadiance)
        && bgfx::isValid(mSamplerIrradiance)
        && bgfx::isValid(mRadianceTexture)
        && bgfx::isValid(mIrradianceTexture)
        && bgfx::isValid(mVertexBuffer)
        && bgfx::isValid(mIndexBuffer)
        && mActiveVertexCount > ZERO_COUNT
        && mActiveIndexCount > ZERO_COUNT)
    {
        std::array<float, MATRIX_ELEMENT_COUNT> transform{};
        bx::mtxIdentity(transform.data());

        bgfx::setTransform(transform.data());
        updateViewportUniforms(sceneSnapshot);
        bgfx::setUniform(mUniformParams, mIblParams.data(), static_cast<uint16_t>(mIblParams.size()));
        bgfx::setTexture(TEXTURE_STAGE_RADIANCE, mSamplerRadiance, mRadianceTexture);
        bgfx::setTexture(TEXTURE_STAGE_IRRADIANCE, mSamplerIrradiance, mIrradianceTexture);
        bgfx::setVertexBuffer(VERTEX_STREAM_MAIN, mVertexBuffer, BUFFER_START_OFFSET, mActiveVertexCount);
        bgfx::setIndexBuffer(mIndexBuffer, BUFFER_START_OFFSET, mActiveIndexCount);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
        bgfx::submit(MAIN_VIEW_ID, mProgramViewport);
    }

    if (bgfx::isValid(mProgramSelectionOverlay)
        && bgfx::isValid(mSelectionOverlayVertexBuffer)
        && bgfx::isValid(mSelectionOverlayEdgeIndexBuffer)
        && mActiveSelectionOverlayVertexCount > ZERO_COUNT
        && mActiveSelectionOverlayEdgeIndexCount > ZERO_COUNT)
    {
        std::array<float, MATRIX_ELEMENT_COUNT> transform{};
        bx::mtxIdentity(transform.data());
        bgfx::setTransform(transform.data());
        bgfx::setVertexBuffer(VERTEX_STREAM_MAIN, mSelectionOverlayVertexBuffer, BUFFER_START_OFFSET, mActiveSelectionOverlayVertexCount);
        bgfx::setIndexBuffer(mSelectionOverlayEdgeIndexBuffer, BUFFER_START_OFFSET, mActiveSelectionOverlayEdgeIndexCount);
        bgfx::setState(
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
            | BGFX_STATE_DEPTH_TEST_LEQUAL
            | BGFX_STATE_MSAA);
        bgfx::submit(MAIN_VIEW_ID, mProgramSelectionOverlay);
    }

    bgfx::frame();
}

/**
 * Updates IBL shader parameter block for current camera/material settings.
 * @param[in] sceneSnapshot Current scene snapshot.
 */
void RenderBridge::updateViewportUniforms(const Scene::RenderSnapshot &sceneSnapshot)
{
    for (auto &param : mIblParams)
    {
        param = IBL_PARAM_ZERO;
    }

    mIblParams[IBL_SLOT(IblParamSlot::InvMtx)] = IBL_PARAM_INV_MTX;
    mIblParams[IBL_SLOT(IblParamSlot::Mtx)] = IBL_PARAM_MTX;
    mIblParams[IBL_SLOT(IblParamSlot::LightMtx)] = IBL_PARAM_LIGHT_MTX;
    mIblParams[IBL_SLOT(IblParamSlot::GlossScale)] = IBL_PARAM_GLOSS_SCALE;

    /* Material and viewport controls expected by bgfx 18-ibl shaders. */
    mIblParams[IBL_SLOT(IblParamSlot::Material)] = IBL_PARAM_MATERIAL;      /* glossiness, reflectivity, exposure, bgType */
    mIblParams[IBL_SLOT(IblParamSlot::Metallic)] = IBL_PARAM_METALLIC;      /* metallic workflow */
    mIblParams[IBL_SLOT(IblParamSlot::LightFlags)] = IBL_PARAM_LIGHT_FLAGS; /* direct + ibl diffuse/specular on */
    mIblParams[IBL_SLOT(IblParamSlot::CameraPos)] = { sceneSnapshot.cameraPosition.x, sceneSnapshot.cameraPosition.y, sceneSnapshot.cameraPosition.z, IBL_PARAM_POSITION_W };
    mIblParams[IBL_SLOT(IblParamSlot::Albedo)] = IBL_PARAM_ALBEDO;              /* base albedo */
    mIblParams[IBL_SLOT(IblParamSlot::SpecularTint)] = IBL_PARAM_SPECULAR_TINT; /* specular tint */

    constexpr float RAW_LIGHT_DIR_X = 0.55F;
    constexpr float RAW_LIGHT_DIR_Y = 0.65F;
    constexpr float RAW_LIGHT_DIR_Z = 0.35F;
    const bx::Vec3 lightDirection = bx::normalize({ RAW_LIGHT_DIR_X, RAW_LIGHT_DIR_Y, RAW_LIGHT_DIR_Z });
    mIblParams[IBL_SLOT(IblParamSlot::LightDir)] = { lightDirection.x, lightDirection.y, lightDirection.z, IBL_PARAM_LIGHT_DIRECTION_W };
    mIblParams[IBL_SLOT(IblParamSlot::LightColor)] = IBL_PARAM_LIGHT_COLOR;
}

/**
 * Ensures dynamic buffers can hold the requested mesh size.
 * @param[in] vertexCount Required vertex count.
 * @param[in] indexCount Required index count.
 * @return True when buffers are ready for upload, otherwise false.
 */
bool RenderBridge::ensureMeshCapacity(uint32_t vertexCount, uint32_t indexCount)
{
    if (vertexCount == ZERO_COUNT || indexCount == ZERO_COUNT)
    {
        return false;
    }

    const bool growVertices = !bgfx::isValid(mVertexBuffer) || vertexCount > mVertexCapacity;
    const bool growIndices = !bgfx::isValid(mIndexBuffer) || indexCount > mIndexCapacity;

    if (!growVertices && !growIndices)
    {
        return true;
    }

    const auto newVertexCapacity = bx::max(vertexCount, mVertexCapacity * DYNAMIC_VERTEX_GROWTH_FACTOR, MINIMUM_VERTEX_CAPACITY);
    const auto newIndexCapacity = bx::max(indexCount, mIndexCapacity * DYNAMIC_INDEX_GROWTH_FACTOR, MINIMUM_INDEX_CAPACITY);

    if (bgfx::isValid(mVertexBuffer))
    {
        bgfx::destroy(mVertexBuffer);
        mVertexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(mIndexBuffer))
    {
        bgfx::destroy(mIndexBuffer);
        mIndexBuffer = BGFX_INVALID_HANDLE;
    }

    mVertexBuffer = bgfx::createDynamicVertexBuffer(newVertexCapacity, mVertexLayout);
    mIndexBuffer = bgfx::createDynamicIndexBuffer(newIndexCapacity);

    if (!bgfx::isValid(mVertexBuffer) || !bgfx::isValid(mIndexBuffer))
    {
        if (bgfx::isValid(mVertexBuffer))
        {
            bgfx::destroy(mVertexBuffer);
            mVertexBuffer = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mIndexBuffer))
        {
            bgfx::destroy(mIndexBuffer);
            mIndexBuffer = BGFX_INVALID_HANDLE;
        }

        mVertexCapacity = ZERO_COUNT;
        mIndexCapacity = ZERO_COUNT;
        mActiveVertexCount = ZERO_COUNT;
        mActiveIndexCount = ZERO_COUNT;
        return false;
    }

    mVertexCapacity = newVertexCapacity;
    mIndexCapacity = newIndexCapacity;
    return true;
}

bool RenderBridge::ensureSelectionOverlayCapacity(uint32_t vertexCount, uint32_t edgeIndexCount)
{
    const bool needVertices = vertexCount > ZERO_COUNT;
    const bool needEdgeIndices = edgeIndexCount > ZERO_COUNT;
    if (!needVertices && !needEdgeIndices)
    {
        if (bgfx::isValid(mSelectionOverlayVertexBuffer))
        {
            bgfx::destroy(mSelectionOverlayVertexBuffer);
            mSelectionOverlayVertexBuffer = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mSelectionOverlayEdgeIndexBuffer))
        {
            bgfx::destroy(mSelectionOverlayEdgeIndexBuffer);
            mSelectionOverlayEdgeIndexBuffer = BGFX_INVALID_HANDLE;
        }

        mSelectionOverlayVertexCapacity = ZERO_COUNT;
        mSelectionOverlayEdgeIndexCapacity = ZERO_COUNT;
        mActiveSelectionOverlayVertexCount = ZERO_COUNT;
        mActiveSelectionOverlayEdgeIndexCount = ZERO_COUNT;
        return true;
    }

    const bool growVertices = !bgfx::isValid(mSelectionOverlayVertexBuffer) || vertexCount > mSelectionOverlayVertexCapacity;
    const bool growIndices = !bgfx::isValid(mSelectionOverlayEdgeIndexBuffer) || edgeIndexCount > mSelectionOverlayEdgeIndexCapacity;

    if (!growVertices && !growIndices)
    {
        return true;
    }

    const auto newVertexCapacity = bx::max(vertexCount, mSelectionOverlayVertexCapacity * DYNAMIC_VERTEX_GROWTH_FACTOR, MINIMUM_SELECTION_OVERLAY_VERTEX_CAPACITY);
    const auto newIndexCapacity = bx::max(edgeIndexCount, mSelectionOverlayEdgeIndexCapacity * DYNAMIC_INDEX_GROWTH_FACTOR, MINIMUM_SELECTION_OVERLAY_EDGE_INDEX_CAPACITY);

    if (bgfx::isValid(mSelectionOverlayVertexBuffer))
    {
        bgfx::destroy(mSelectionOverlayVertexBuffer);
        mSelectionOverlayVertexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(mSelectionOverlayEdgeIndexBuffer))
    {
        bgfx::destroy(mSelectionOverlayEdgeIndexBuffer);
        mSelectionOverlayEdgeIndexBuffer = BGFX_INVALID_HANDLE;
    }

    mSelectionOverlayVertexBuffer = bgfx::createDynamicVertexBuffer(newVertexCapacity, mVertexLayout);
    mSelectionOverlayEdgeIndexBuffer = bgfx::createDynamicIndexBuffer(newIndexCapacity);

    if (!bgfx::isValid(mSelectionOverlayVertexBuffer) || !bgfx::isValid(mSelectionOverlayEdgeIndexBuffer))
    {
        if (bgfx::isValid(mSelectionOverlayVertexBuffer))
        {
            bgfx::destroy(mSelectionOverlayVertexBuffer);
            mSelectionOverlayVertexBuffer = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(mSelectionOverlayEdgeIndexBuffer))
        {
            bgfx::destroy(mSelectionOverlayEdgeIndexBuffer);
            mSelectionOverlayEdgeIndexBuffer = BGFX_INVALID_HANDLE;
        }

        mSelectionOverlayVertexCapacity = ZERO_COUNT;
        mSelectionOverlayEdgeIndexCapacity = ZERO_COUNT;
        mActiveSelectionOverlayVertexCount = ZERO_COUNT;
        mActiveSelectionOverlayEdgeIndexCount = ZERO_COUNT;
        return false;
    }

    mSelectionOverlayVertexCapacity = newVertexCapacity;
    mSelectionOverlayEdgeIndexCapacity = newIndexCapacity;
    return true;
}

/**
 * Uploads CPU-built mesh buffers into bgfx dynamic buffers.
 * @param[in] meshData Built mesh data produced by the scene build step.
 */
void RenderBridge::uploadRenderMesh(const Scene::BuiltMeshData &meshData)
{
    const auto vertexCount = static_cast<uint32_t>(meshData.vertices.size());
    const auto indexCount = static_cast<uint32_t>(meshData.indices.size());
    const auto selectionOverlayVertexCount = static_cast<uint32_t>(meshData.selectionOverlayEdgeVertices.size());
    const auto selectionOverlayEdgeIndexCount = static_cast<uint32_t>(meshData.selectionOverlayEdgeIndices.size());

    if (!ensureMeshCapacity(vertexCount, indexCount))
    {
        return;
    }

    if (!ensureSelectionOverlayCapacity(selectionOverlayVertexCount, selectionOverlayEdgeIndexCount))
    {
        return;
    }

    const auto vertexBytes = static_cast<uint32_t>(meshData.vertices.size() * sizeof(Scene::PackedVertex));
    const auto indexBytes = static_cast<uint32_t>(meshData.indices.size() * sizeof(uint16_t));

    const bgfx::Memory *vertexMemory = bgfx::copy(meshData.vertices.data(), vertexBytes);
    const bgfx::Memory *indexMemory = bgfx::copy(meshData.indices.data(), indexBytes);

    bgfx::update(mVertexBuffer, BUFFER_START_OFFSET, vertexMemory);
    bgfx::update(mIndexBuffer, BUFFER_START_OFFSET, indexMemory);

    if (selectionOverlayVertexCount > ZERO_COUNT)
    {
        const auto selectionOverlayVertexBytes = static_cast<uint32_t>(meshData.selectionOverlayEdgeVertices.size() * sizeof(Scene::PackedVertex));
        const bgfx::Memory *selectionOverlayVertexMemory = bgfx::copy(meshData.selectionOverlayEdgeVertices.data(), selectionOverlayVertexBytes);
        bgfx::update(mSelectionOverlayVertexBuffer, BUFFER_START_OFFSET, selectionOverlayVertexMemory);
    }

    if (selectionOverlayEdgeIndexCount > ZERO_COUNT)
    {
        const auto selectionOverlayEdgeIndexBytes = static_cast<uint32_t>(meshData.selectionOverlayEdgeIndices.size() * sizeof(uint16_t));
        const bgfx::Memory *selectionOverlayEdgeIndexMemory = bgfx::copy(meshData.selectionOverlayEdgeIndices.data(), selectionOverlayEdgeIndexBytes);
        bgfx::update(mSelectionOverlayEdgeIndexBuffer, BUFFER_START_OFFSET, selectionOverlayEdgeIndexMemory);
    }

    mActiveVertexCount = vertexCount;
    mActiveIndexCount = indexCount;
    mActiveSelectionOverlayVertexCount = selectionOverlayVertexCount;
    mActiveSelectionOverlayEdgeIndexCount = selectionOverlayEdgeIndexCount;
    mUploadedRevision = meshData.builtRevision;
}
