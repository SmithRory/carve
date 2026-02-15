#include "render_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>

namespace
{
const char *shaderDirectoryName(bgfx::RendererType::Enum rendererType)
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
        return nullptr;
    }
}

const bgfx::Memory *loadShaderMemory(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return nullptr;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0)
    {
        return nullptr;
    }

    file.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (!file.read(bytes.data(), size))
    {
        return nullptr;
    }

    return bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

bgfx::ShaderHandle loadShader(const std::filesystem::path &shaderRoot, const char *shaderName)
{
    const std::filesystem::path shaderPath = shaderRoot / shaderName;
    const bgfx::Memory *memory = loadShaderMemory(shaderPath.string());
    if (memory == nullptr)
    {
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createShader(memory);
}

bgfx::ProgramHandle loadProgram(const char *vsName, const char *fsName)
{
    const char *shaderDir = shaderDirectoryName(bgfx::getRendererType());
    if (shaderDir == nullptr)
    {
        return BGFX_INVALID_HANDLE;
    }

    const std::array<std::filesystem::path, 3> roots = {
        std::filesystem::path("shaders") / shaderDir,
        std::filesystem::path("src/render/shaders") / shaderDir,
        std::filesystem::path(CARVE_SOURCE_DIR) / "src/render/shaders" / shaderDir,
    };

    for (const std::filesystem::path &root : roots)
    {
        const bgfx::ShaderHandle vertexShader = loadShader(root, vsName);
        const bgfx::ShaderHandle fragmentShader = loadShader(root, fsName);

        if (bgfx::isValid(vertexShader) && bgfx::isValid(fragmentShader))
        {
            return bgfx::createProgram(vertexShader, fragmentShader, true);
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

bgfx::TextureHandle createSolidCubeTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    std::array<std::uint8_t, 6 * 4> texels{};
    for (std::size_t face = 0; face < 6; ++face)
    {
        const std::size_t offset = face * 4;
        texels[offset + 0] = r;
        texels[offset + 1] = g;
        texels[offset + 2] = b;
        texels[offset + 3] = a;
    }

    const bgfx::Memory *memory = bgfx::copy(texels.data(), static_cast<std::uint32_t>(texels.size()));
    return bgfx::createTextureCube(1,
                                   false,
                                   1,
                                   bgfx::TextureFormat::RGBA8,
                                   BGFX_TEXTURE_NONE | BGFX_SAMPLER_UVW_CLAMP,
                                   memory);
}

bgfx::TextureHandle loadTexture(const std::filesystem::path &texturePath)
{
    const bgfx::Memory *memory = loadShaderMemory(texturePath.string());
    if (memory == nullptr)
    {
        return BGFX_INVALID_HANDLE;
    }

    return bgfx::createTexture(memory, BGFX_SAMPLER_UVW_CLAMP, 0, nullptr);
}

bgfx::TextureHandle loadIblTexture(const std::string &fileName)
{
    std::vector<std::filesystem::path> roots;
    roots.reserve(5);
    roots.emplace_back(std::filesystem::path("textures"));
    roots.emplace_back(std::filesystem::path("src/render/textures"));
    roots.emplace_back(std::filesystem::path(CARVE_SOURCE_DIR) / "src/render/textures");
    roots.emplace_back(std::filesystem::path("/home/rsmith/bgfx/examples/runtime/textures"));

    if (const char *home = std::getenv("HOME"); home != nullptr)
    {
        roots.emplace_back(std::filesystem::path(home) / "bgfx/examples/runtime/textures");
    }

    for (const std::filesystem::path &root : roots)
    {
        const bgfx::TextureHandle texture = loadTexture(root / fileName);
        if (bgfx::isValid(texture))
        {
            return texture;
        }
    }

    return BGFX_INVALID_HANDLE;
}
} // namespace

RenderBridge::RenderBridge()
    : m_initialized(false)
    , m_width(1)
    , m_height(1)
    , m_vertexBuffer(BGFX_INVALID_HANDLE)
    , m_indexBuffer(BGFX_INVALID_HANDLE)
    , m_programViewport(BGFX_INVALID_HANDLE)
    , m_uniformParams(BGFX_INVALID_HANDLE)
    , m_samplerRadiance(BGFX_INVALID_HANDLE)
    , m_samplerIrradiance(BGFX_INVALID_HANDLE)
    , m_radianceTexture(BGFX_INVALID_HANDLE)
    , m_irradianceTexture(BGFX_INVALID_HANDLE)
    , m_iblParams{}
    , m_vertexCapacity(0)
    , m_indexCapacity(0)
    , m_activeVertexCount(0)
    , m_activeIndexCount(0)
    , m_uploadedRevision(0)
{
}

RenderBridge::~RenderBridge()
{
    shutdown();
}

bool RenderBridge::initialize(void *nativeWindowHandle, std::uint32_t width, std::uint32_t height)
{
    if (m_initialized)
    {
        return true;
    }

    bgfx::Init init;
    init.type = bgfx::RendererType::Count;
    init.resolution.width = width;
    init.resolution.height = height;
    init.resolution.reset = BGFX_RESET_VSYNC;

    bgfx::PlatformData platformData{};
    platformData.nwh = nativeWindowHandle;
    init.platformData = platformData;

    m_initialized = bgfx::init(init);
    if (!m_initialized)
    {
        return false;
    }

    m_width = static_cast<std::uint16_t>(width == 0 ? 1 : width);
    m_height = static_cast<std::uint16_t>(height == 0 ? 1 : height);

    m_vertexLayout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 4, bgfx::AttribType::Uint8, true)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    m_programViewport = loadProgram("vs_ibl_mesh.bin", "fs_ibl_mesh.bin");
    m_uniformParams = bgfx::createUniform("u_params", bgfx::UniformType::Vec4, 12);
    m_samplerRadiance = bgfx::createUniform("s_texCube", bgfx::UniformType::Sampler);
    m_samplerIrradiance = bgfx::createUniform("s_texCubeIrr", bgfx::UniformType::Sampler);
    m_radianceTexture = loadIblTexture("bolonga_lod.dds");
    m_irradianceTexture = loadIblTexture("bolonga_irr.dds");
    if (!bgfx::isValid(m_radianceTexture))
    {
        m_radianceTexture = loadIblTexture("kyoto_lod.dds");
    }
    if (!bgfx::isValid(m_irradianceTexture))
    {
        m_irradianceTexture = loadIblTexture("kyoto_irr.dds");
    }
    if (!bgfx::isValid(m_radianceTexture))
    {
        m_radianceTexture = createSolidCubeTexture(166U, 181U, 204U, 255U);
    }
    if (!bgfx::isValid(m_irradianceTexture))
    {
        m_irradianceTexture = createSolidCubeTexture(84U, 94U, 112U, 255U);
    }

    if (!bgfx::isValid(m_programViewport)
        || !bgfx::isValid(m_uniformParams)
        || !bgfx::isValid(m_samplerRadiance)
        || !bgfx::isValid(m_samplerIrradiance)
        || !bgfx::isValid(m_radianceTexture)
        || !bgfx::isValid(m_irradianceTexture))
    {
        shutdown();
        return false;
    }

    bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
    return true;
}

void RenderBridge::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    if (bgfx::isValid(m_radianceTexture))
    {
        bgfx::destroy(m_radianceTexture);
        m_radianceTexture = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_irradianceTexture))
    {
        bgfx::destroy(m_irradianceTexture);
        m_irradianceTexture = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_samplerIrradiance))
    {
        bgfx::destroy(m_samplerIrradiance);
        m_samplerIrradiance = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_samplerRadiance))
    {
        bgfx::destroy(m_samplerRadiance);
        m_samplerRadiance = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_uniformParams))
    {
        bgfx::destroy(m_uniformParams);
        m_uniformParams = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_programViewport))
    {
        bgfx::destroy(m_programViewport);
        m_programViewport = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_indexBuffer))
    {
        bgfx::destroy(m_indexBuffer);
        m_indexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_vertexBuffer))
    {
        bgfx::destroy(m_vertexBuffer);
        m_vertexBuffer = BGFX_INVALID_HANDLE;
    }

    m_vertexCapacity = 0;
    m_indexCapacity = 0;
    m_activeVertexCount = 0;
    m_activeIndexCount = 0;
    m_uploadedRevision = 0;

    bgfx::shutdown();
    m_initialized = false;
}

void RenderBridge::resize(std::uint16_t width, std::uint16_t height)
{
    if (!m_initialized)
    {
        return;
    }

    m_width = width == 0 ? 1 : width;
    m_height = height == 0 ? 1 : height;
    bgfx::reset(m_width, m_height, BGFX_RESET_VSYNC);
    bgfx::setViewRect(0, 0, 0, bgfx::BackbufferRatio::Equal);
}

void RenderBridge::render(const SceneCore::RenderSnapshot &sceneSnapshot)
{
    if (!m_initialized)
    {
        return;
    }

    // Step 5: upload new render mesh data only when build revision changes.
    if (sceneSnapshot.renderMesh != nullptr && sceneSnapshot.renderMesh->builtRevision > m_uploadedRevision)
    {
        uploadRenderMesh(*sceneSnapshot.renderMesh);
    }

    std::uint32_t clearColor = sceneSnapshot.upToDate ? 0x26313dffu : 0x3d2b26ffu;
    if (sceneSnapshot.objectSelected)
    {
        clearColor = sceneSnapshot.upToDate ? 0x203b48ffu : 0x483020ffu;
    }
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor, 1.0f, 0);

    // Step 6: camera navigation only updates transforms, not geometry uploads.
    const float cosPitch = std::cos(sceneSnapshot.cameraPitchRadians);
    const bx::Vec3 eye = {
        sceneSnapshot.cameraPosition.x,
        sceneSnapshot.cameraPosition.y,
        sceneSnapshot.cameraPosition.z,
    };
    const bx::Vec3 at = {
        eye.x + std::sin(sceneSnapshot.cameraYawRadians) * cosPitch,
        eye.y + std::sin(sceneSnapshot.cameraPitchRadians),
        eye.z + std::cos(sceneSnapshot.cameraYawRadians) * cosPitch,
    };

    float view[16];
    bx::mtxLookAt(view, eye, at);

    float proj[16];
    const float aspect = static_cast<float>(m_width) / static_cast<float>(m_height);
    bx::mtxProj(proj, 60.0F, aspect, 0.1F, 100.0F, bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(0, view, proj);
    bgfx::setViewRect(0, 0, 0, m_width, m_height);
    bgfx::touch(0);

    if (bgfx::isValid(m_programViewport)
        && bgfx::isValid(m_uniformParams)
        && bgfx::isValid(m_samplerRadiance)
        && bgfx::isValid(m_samplerIrradiance)
        && bgfx::isValid(m_radianceTexture)
        && bgfx::isValid(m_irradianceTexture)
        && bgfx::isValid(m_vertexBuffer)
        && bgfx::isValid(m_indexBuffer)
        && m_activeVertexCount > 0
        && m_activeIndexCount > 0)
    {
        float transform[16];
        bx::mtxIdentity(transform);

        bgfx::setTransform(transform);
        updateViewportUniforms(sceneSnapshot);
        bgfx::setUniform(m_uniformParams, m_iblParams.data(), static_cast<std::uint16_t>(m_iblParams.size()));
        bgfx::setTexture(0, m_samplerRadiance, m_radianceTexture);
        bgfx::setTexture(1, m_samplerIrradiance, m_irradianceTexture);
        bgfx::setVertexBuffer(0, m_vertexBuffer, 0, m_activeVertexCount);
        bgfx::setIndexBuffer(m_indexBuffer, 0, m_activeIndexCount);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
        bgfx::submit(0, m_programViewport);
    }

    bgfx::frame();
}

bool RenderBridge::initialized() const
{
    return m_initialized;
}

void RenderBridge::updateViewportUniforms(const SceneCore::RenderSnapshot &sceneSnapshot)
{
    for (std::array<float, 4> &param : m_iblParams)
    {
        param = {0.0F, 0.0F, 0.0F, 0.0F};
    }

    m_iblParams[0] = {1.0F, 0.0F, 0.0F, 0.0F};
    m_iblParams[1] = {0.0F, 1.0F, 0.0F, 0.0F};
    m_iblParams[2] = {0.0F, 0.0F, 1.0F, 0.0F};
    m_iblParams[3] = {0.0F, 0.0F, 0.0F, 1.0F};

    // Material and viewport controls expected by bgfx 18-ibl shaders.
    m_iblParams[4] = {0.55F, 0.25F, 0.0F, 0.0F}; // glossiness, reflectivity, exposure, bgType.
    m_iblParams[5] = {0.0F, 0.0F, 0.0F, 0.0F};   // metallic workflow.
    m_iblParams[6] = {1.0F, 1.0F, 1.0F, 1.0F};   // direct + ibl diffuse/specular on.
    m_iblParams[7] = {
        sceneSnapshot.cameraPosition.x,
        sceneSnapshot.cameraPosition.y,
        sceneSnapshot.cameraPosition.z,
        0.0F,
    };
    m_iblParams[8] = {0.68F, 0.70F, 0.72F, 1.0F}; // base albedo.
    m_iblParams[9] = {1.0F, 1.0F, 1.0F, 1.0F};    // specular tint.

    constexpr float rawLightDirX = 0.55F;
    constexpr float rawLightDirY = 0.65F;
    constexpr float rawLightDirZ = 0.35F;
    const float lightLen = std::sqrt(rawLightDirX * rawLightDirX
                                     + rawLightDirY * rawLightDirY
                                     + rawLightDirZ * rawLightDirZ);
    m_iblParams[10] = {rawLightDirX / lightLen, rawLightDirY / lightLen, rawLightDirZ / lightLen, 0.0F};
    m_iblParams[11] = {1.0F, 0.98F, 0.95F, 0.0F};
}

bool RenderBridge::ensureMeshCapacity(std::uint32_t vertexCount, std::uint32_t indexCount)
{
    if (vertexCount == 0 || indexCount == 0)
    {
        return false;
    }

    const bool growVertices = !bgfx::isValid(m_vertexBuffer) || vertexCount > m_vertexCapacity;
    const bool growIndices = !bgfx::isValid(m_indexBuffer) || indexCount > m_indexCapacity;

    if (!growVertices && !growIndices)
    {
        return true;
    }

    const std::uint32_t newVertexCapacity = std::max<std::uint32_t>(vertexCount, std::max<std::uint32_t>(m_vertexCapacity * 2U, 8U));
    const std::uint32_t newIndexCapacity = std::max<std::uint32_t>(indexCount, std::max<std::uint32_t>(m_indexCapacity * 2U, 36U));

    if (bgfx::isValid(m_vertexBuffer))
    {
        bgfx::destroy(m_vertexBuffer);
        m_vertexBuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_indexBuffer))
    {
        bgfx::destroy(m_indexBuffer);
        m_indexBuffer = BGFX_INVALID_HANDLE;
    }

    m_vertexBuffer = bgfx::createDynamicVertexBuffer(newVertexCapacity, m_vertexLayout);
    m_indexBuffer = bgfx::createDynamicIndexBuffer(newIndexCapacity);

    if (!bgfx::isValid(m_vertexBuffer) || !bgfx::isValid(m_indexBuffer))
    {
        if (bgfx::isValid(m_vertexBuffer))
        {
            bgfx::destroy(m_vertexBuffer);
            m_vertexBuffer = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_indexBuffer))
        {
            bgfx::destroy(m_indexBuffer);
            m_indexBuffer = BGFX_INVALID_HANDLE;
        }

        m_vertexCapacity = 0;
        m_indexCapacity = 0;
        m_activeVertexCount = 0;
        m_activeIndexCount = 0;
        return false;
    }

    m_vertexCapacity = newVertexCapacity;
    m_indexCapacity = newIndexCapacity;
    return true;
}

void RenderBridge::uploadRenderMesh(const SceneCore::BuiltMeshData &meshData)
{
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(meshData.vertices.size());
    const std::uint32_t indexCount = static_cast<std::uint32_t>(meshData.indices.size());

    if (!ensureMeshCapacity(vertexCount, indexCount))
    {
        return;
    }

    const std::uint32_t vertexBytes = static_cast<std::uint32_t>(meshData.vertices.size() * sizeof(SceneCore::PackedVertex));
    const std::uint32_t indexBytes = static_cast<std::uint32_t>(meshData.indices.size() * sizeof(std::uint16_t));

    const bgfx::Memory *vertexMemory = bgfx::copy(meshData.vertices.data(), vertexBytes);
    const bgfx::Memory *indexMemory = bgfx::copy(meshData.indices.data(), indexBytes);

    bgfx::update(m_vertexBuffer, 0, vertexMemory);
    bgfx::update(m_indexBuffer, 0, indexMemory);

    m_activeVertexCount = vertexCount;
    m_activeIndexCount = indexCount;
    m_uploadedRevision = meshData.builtRevision;
}
