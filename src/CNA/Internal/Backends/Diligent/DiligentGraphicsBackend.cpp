// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/Diligent/DiligentGraphicsBackend.hpp"

#include "CNA/Logger.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "Graphics/GraphicsEngine/interface/GraphicsTypes.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceVariable.h"
#include "Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp"

#if CNA_DILIGENT_HAS_VULKAN
#include "Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h"
#endif
#if CNA_DILIGENT_HAS_OPENGL
#include "Graphics/GraphicsEngineOpenGL/interface/EngineFactoryOpenGL.h"
#endif
#if CNA_DILIGENT_HAS_D3D11
#include "Graphics/GraphicsEngineD3D11/interface/EngineFactoryD3D11.h"
#endif
#if CNA_DILIGENT_HAS_D3D12
#include "Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h"
#endif

namespace CNA::Internal::Backends::Diligent
{
    namespace
    {
        constexpr const char* kBackendName = "Diligent";

        /// Every built-in shader shares one constant buffer, so one HLSL declaration is prepended
        /// to each program instead of being repeated. The matrices are `row_major` deliberately:
        /// CNA's Matrix stores rows contiguously and XNA's convention is `v * M`, so this lets the
        /// shaders write `mul(v, m)` with the CNA matrix memory uploaded verbatim.
        constexpr const char* kConstantsHlsl = R"(
cbuffer Constants
{
    row_major float4x4 g_WorldViewProj;
    row_major float4x4 g_World;
    float4 g_DiffuseColor;
    float4 g_EmissiveAmbient;
    float4 g_EyePositionSpecularPower;
    float4 g_SpecularColor;
    float4 g_LightDir[3];
    float4 g_LightDiffuse[3];
    float4 g_LightSpecular[3];
    float4 g_Flags;
};
)";

        constexpr const char* kSpriteVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float2 UV    : ATTRIB1;
    float4 Color : ATTRIB2;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos   = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV    = vsIn.UV;
    psIn.Color = vsIn.Color;
}
)";

        constexpr const char* kSpritePixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    psOut.Color = g_Texture.Sample(g_Texture_sampler, psIn.UV) * psIn.Color;
}
)";

        constexpr const char* kColoredVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos   = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.Color = vsIn.Color * g_DiffuseColor;
}
)";

        constexpr const char* kColoredPixelHlsl = R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    psOut.Color = psIn.Color;
}
)";

        constexpr const char* kTexturedVertexHlsl = R"(
struct VSInput
{
    float3 Pos : ATTRIB0;
    float2 UV  : ATTRIB1;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos   = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV    = vsIn.UV;
    psIn.Color = g_DiffuseColor;
}
)";

        constexpr const char* kColoredTexturedVertexHlsl = R"(
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
    float2 UV    : ATTRIB2;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos   = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.UV    = vsIn.UV;
    psIn.Color = vsIn.Color * g_DiffuseColor;
}
)";

        constexpr const char* kTexturedPixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float2 UV    : TEX_COORD;
    float4 Color : COLOR0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 texel = g_Texture.Sample(g_Texture_sampler, psIn.UV);
    psOut.Color = lerp(psIn.Color, texel * psIn.Color, g_Flags.x);
}
)";

        /// Blinn-Phong lighting over the three XNA directional lights, evaluated per pixel. The
        /// light direction/diffuse/specular vectors are already zeroed by the effect layer for a
        /// disabled light, so no per-light enable flag is needed here.
        constexpr const char* kLitVertexHlsl = R"(
struct VSInput
{
    float3 Pos    : ATTRIB0;
    float3 Normal : ATTRIB1;
    float2 UV     : ATTRIB2;
};

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEX_COORD;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
};

void main(in VSInput vsIn, out PSInput psIn)
{
    psIn.Pos      = mul(float4(vsIn.Pos, 1.0), g_WorldViewProj);
    psIn.WorldPos = mul(float4(vsIn.Pos, 1.0), g_World).xyz;
    psIn.Normal   = mul(float4(vsIn.Normal, 0.0), g_World).xyz;
    psIn.UV       = vsIn.UV;
}
)";

        constexpr const char* kLitPixelHlsl = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSInput
{
    float4 Pos      : SV_POSITION;
    float2 UV       : TEX_COORD;
    float3 WorldPos : WORLD_POS;
    float3 Normal   : NORMAL;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput psIn, out PSOutput psOut)
{
    float4 baseColor = g_DiffuseColor;
    if (g_Flags.x > 0.5)
        baseColor *= g_Texture.Sample(g_Texture_sampler, psIn.UV);

    if (g_Flags.z < 0.5)
    {
        psOut.Color = baseColor;
        return;
    }

    float3 normal   = normalize(psIn.Normal);
    float3 eyeDir   = normalize(g_EyePositionSpecularPower.xyz - psIn.WorldPos);
    float3 diffuse  = g_EmissiveAmbient.rgb;
    float3 specular = float3(0.0, 0.0, 0.0);

    for (int i = 0; i < 3; ++i)
    {
        float3 lightDir = -g_LightDir[i].xyz;
        float  nDotL    = max(dot(normal, lightDir), 0.0);
        diffuse += g_LightDiffuse[i].rgb * nDotL;
        if (nDotL > 0.0)
        {
            float3 halfVec = normalize(lightDir + eyeDir);
            float  nDotH   = max(dot(normal, halfVec), 0.0);
            specular += g_LightSpecular[i].rgb * pow(nDotH, max(g_EyePositionSpecularPower.w, 1.0));
        }
    }

    float3 lit = baseColor.rgb * diffuse + specular * g_SpecularColor.rgb;
    psOut.Color = float4(lit, baseColor.a);
}
)";

        [[nodiscard]] Dg::BLEND_FACTOR ToBlendFactor(int xnaBlend)
        {
            switch (xnaBlend)
            {
                case 0:  return Dg::BLEND_FACTOR_ONE;
                case 1:  return Dg::BLEND_FACTOR_ZERO;
                case 2:  return Dg::BLEND_FACTOR_SRC_COLOR;
                case 3:  return Dg::BLEND_FACTOR_INV_SRC_COLOR;
                case 4:  return Dg::BLEND_FACTOR_SRC_ALPHA;
                case 5:  return Dg::BLEND_FACTOR_INV_SRC_ALPHA;
                case 6:  return Dg::BLEND_FACTOR_DEST_COLOR;
                case 7:  return Dg::BLEND_FACTOR_INV_DEST_COLOR;
                case 8:  return Dg::BLEND_FACTOR_DEST_ALPHA;
                case 9:  return Dg::BLEND_FACTOR_INV_DEST_ALPHA;
                case 10: return Dg::BLEND_FACTOR_BLEND_FACTOR;
                case 11: return Dg::BLEND_FACTOR_INV_BLEND_FACTOR;
                case 12: return Dg::BLEND_FACTOR_SRC_ALPHA_SAT;
                default: return Dg::BLEND_FACTOR_ONE;
            }
        }

        [[nodiscard]] Dg::BLEND_OPERATION ToBlendOperation(int xnaBlendFunction)
        {
            switch (xnaBlendFunction)
            {
                case 0:  return Dg::BLEND_OPERATION_ADD;
                case 1:  return Dg::BLEND_OPERATION_SUBTRACT;
                case 2:  return Dg::BLEND_OPERATION_REV_SUBTRACT;
                case 3:  return Dg::BLEND_OPERATION_MAX;
                case 4:  return Dg::BLEND_OPERATION_MIN;
                default: return Dg::BLEND_OPERATION_ADD;
            }
        }

        [[nodiscard]] Dg::COMPARISON_FUNCTION ToComparisonFunction(int xnaCompareFunction)
        {
            switch (xnaCompareFunction)
            {
                case 0:  return Dg::COMPARISON_FUNC_ALWAYS;
                case 1:  return Dg::COMPARISON_FUNC_NEVER;
                case 2:  return Dg::COMPARISON_FUNC_LESS;
                case 3:  return Dg::COMPARISON_FUNC_LESS_EQUAL;
                case 4:  return Dg::COMPARISON_FUNC_EQUAL;
                case 5:  return Dg::COMPARISON_FUNC_GREATER_EQUAL;
                case 6:  return Dg::COMPARISON_FUNC_GREATER;
                case 7:  return Dg::COMPARISON_FUNC_NOT_EQUAL;
                default: return Dg::COMPARISON_FUNC_ALWAYS;
            }
        }

        [[nodiscard]] Dg::STENCIL_OP ToStencilOperation(int xnaStencilOperation)
        {
            switch (xnaStencilOperation)
            {
                case 0:  return Dg::STENCIL_OP_KEEP;
                case 1:  return Dg::STENCIL_OP_ZERO;
                case 2:  return Dg::STENCIL_OP_REPLACE;
                // XNA's Increment/Decrement wrap; IncrementSaturation/DecrementSaturation clamp.
                case 3:  return Dg::STENCIL_OP_INCR_WRAP;
                case 4:  return Dg::STENCIL_OP_DECR_WRAP;
                case 5:  return Dg::STENCIL_OP_INCR_SAT;
                case 6:  return Dg::STENCIL_OP_DECR_SAT;
                case 7:  return Dg::STENCIL_OP_INVERT;
                default: return Dg::STENCIL_OP_KEEP;
            }
        }

        [[nodiscard]] Dg::PRIMITIVE_TOPOLOGY ToTopology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return Dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                case PrimitiveType::TriangleStrip: return Dg::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                case PrimitiveType::LineList:      return Dg::PRIMITIVE_TOPOLOGY_LINE_LIST;
                case PrimitiveType::LineStrip:     return Dg::PRIMITIVE_TOPOLOGY_LINE_STRIP;
                case PrimitiveType::PointListEXT:  return Dg::PRIMITIVE_TOPOLOGY_POINT_LIST;
                default:
                    throw std::runtime_error("CNA Diligent: unsupported primitive type");
            }
        }

        [[nodiscard]] int VertexCountForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:      return primitiveCount * 2;
                case PrimitiveType::LineStrip:     return primitiveCount + 1;
                case PrimitiveType::PointListEXT:  return primitiveCount;
                default:
                    throw std::runtime_error("CNA Diligent: unsupported primitive type");
            }
        }

        [[nodiscard]] Dg::TEXTURE_ADDRESS_MODE ToAddressMode(int xnaAddressMode)
        {
            switch (xnaAddressMode)
            {
                case 0:  return Dg::TEXTURE_ADDRESS_WRAP;
                case 1:  return Dg::TEXTURE_ADDRESS_CLAMP;
                case 2:  return Dg::TEXTURE_ADDRESS_MIRROR;
                default: return Dg::TEXTURE_ADDRESS_CLAMP;
            }
        }

        /// Maps the full XNA TextureFilter set, which names min/mag/mip independently, onto the
        /// single Diligent FILTER_TYPE triple. Anisotropic is the only entry that also depends on
        /// the sampler's MaxAnisotropy, handled by the caller.
        void ToFilterTypes(int xnaFilter, Dg::FILTER_TYPE& minFilter, Dg::FILTER_TYPE& magFilter,
                           Dg::FILTER_TYPE& mipFilter)
        {
            constexpr Dg::FILTER_TYPE kPoint = Dg::FILTER_TYPE_POINT;
            constexpr Dg::FILTER_TYPE kLinear = Dg::FILTER_TYPE_LINEAR;
            switch (xnaFilter)
            {
                case 0: minFilter = kLinear; magFilter = kLinear; mipFilter = kLinear; return;
                case 1: minFilter = kPoint;  magFilter = kPoint;  mipFilter = kPoint;  return;
                case 2:
                    minFilter = Dg::FILTER_TYPE_ANISOTROPIC;
                    magFilter = Dg::FILTER_TYPE_ANISOTROPIC;
                    mipFilter = Dg::FILTER_TYPE_ANISOTROPIC;
                    return;
                case 3: minFilter = kLinear; magFilter = kLinear; mipFilter = kPoint;  return;
                case 4: minFilter = kPoint;  magFilter = kPoint;  mipFilter = kLinear; return;
                case 5: minFilter = kLinear; magFilter = kPoint;  mipFilter = kLinear; return;
                case 6: minFilter = kLinear; magFilter = kPoint;  mipFilter = kPoint;  return;
                case 7: minFilter = kPoint;  magFilter = kLinear; mipFilter = kLinear; return;
                case 8: minFilter = kPoint;  magFilter = kLinear; mipFilter = kPoint;  return;
                default: minFilter = kLinear; magFilter = kLinear; mipFilter = kLinear; return;
            }
        }

        void MatrixToFloats(const Matrix& matrix, float (&out)[16])
        {
            out[0]  = matrix.M11; out[1]  = matrix.M12; out[2]  = matrix.M13; out[3]  = matrix.M14;
            out[4]  = matrix.M21; out[5]  = matrix.M22; out[6]  = matrix.M23; out[7]  = matrix.M24;
            out[8]  = matrix.M31; out[9]  = matrix.M32; out[10] = matrix.M33; out[11] = matrix.M34;
            out[12] = matrix.M41; out[13] = matrix.M42; out[14] = matrix.M43; out[15] = matrix.M44;
        }

        [[nodiscard]] std::string ToLowerAscii(const std::string& value)
        {
            std::string lowered = value;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lowered;
        }

        /// True when a texture format stores blue in the first byte of each texel rather than red.
        ///
        /// Necessary because `SwapChainDesc::ColorBufferFormat` is a *request*: Diligent silently
        /// substitutes a supported format when the surface rejects the requested one (see
        /// `SwapChainVkImpl`'s own "will be replaced with" info message), and several real surfaces
        /// offer only the BGRA ordering. Rendering is unaffected — the shader writes float RGBA and
        /// the format conversion happens on write — but a raw byte-for-byte readback is not, so the
        /// back buffer's actual format has to be consulted rather than the one CNA asked for.
        [[nodiscard]] bool IsBlueFirstFormat(Dg::TEXTURE_FORMAT format)
        {
            return format == Dg::TEX_FORMAT_BGRA8_UNORM ||
                   format == Dg::TEX_FORMAT_BGRA8_UNORM_SRGB ||
                   format == Dg::TEX_FORMAT_BGRA8_TYPELESS;
        }

        /// Number of mip levels in a full chain down to 1x1, matching what every CNA backend and
        /// `Texture2D`/`TextureCube`'s own level count assume.
        [[nodiscard]] int MipLevelCount(int width, int height)
        {
            int levels = 1;
            int extent = std::max(width, height);
            while (extent > 1)
            {
                extent /= 2;
                ++levels;
            }
            return levels;
        }

        /// Extent of one axis at @p level, floored at 1 as every graphics API defines it.
        [[nodiscard]] int MipLevelExtent(int baseExtent, int level)
        {
            int extent = baseExtent;
            for (int i = 0; i < level; ++i)
                extent = std::max(1, extent / 2);
            return std::max(1, extent);
        }

        [[nodiscard]] std::uint32_t PackBytes(int a, int b, int c, int d)
        {
            return (static_cast<std::uint32_t>(a & 0xFF)) |
                   (static_cast<std::uint32_t>(b & 0xFF) << 8) |
                   (static_cast<std::uint32_t>(c & 0xFF) << 16) |
                   (static_cast<std::uint32_t>(d & 0xFF) << 24);
        }
    }

    const char* GetDeviceTypeName(DiligentDeviceType type)
    {
        switch (type)
        {
            case DiligentDeviceType::D3D12:  return "Direct3D12";
            case DiligentDeviceType::Vulkan: return "Vulkan";
            case DiligentDeviceType::D3D11:  return "Direct3D11";
            case DiligentDeviceType::OpenGL: return "OpenGL";
        }
        return "Unknown";
    }

    std::vector<DiligentDeviceType> ParseDeviceTypeOverride(const std::string& value)
    {
        const std::string lowered = ToLowerAscii(value);
        if (lowered.empty() || lowered == "auto")
            return GetDeviceTypePreferenceOrder();
        if (lowered == "d3d12" || lowered == "direct3d12" || lowered == "dx12")
            return {DiligentDeviceType::D3D12};
        if (lowered == "vulkan" || lowered == "vk")
            return {DiligentDeviceType::Vulkan};
        if (lowered == "d3d11" || lowered == "direct3d11" || lowered == "dx11")
            return {DiligentDeviceType::D3D11};
        if (lowered == "opengl" || lowered == "gl" || lowered == "gles")
            return {DiligentDeviceType::OpenGL};
        throw std::runtime_error("CNA Diligent: unknown CNA_DILIGENT_DEVICE value: " + value);
    }

    std::vector<DiligentDeviceType> GetDeviceTypePreferenceOrder()
    {
        std::vector<DiligentDeviceType> order;
#if CNA_DILIGENT_HAS_D3D12
        order.push_back(DiligentDeviceType::D3D12);
#endif
#if CNA_DILIGENT_HAS_VULKAN
        order.push_back(DiligentDeviceType::Vulkan);
#endif
#if CNA_DILIGENT_HAS_D3D11
        order.push_back(DiligentDeviceType::D3D11);
#endif
#if CNA_DILIGENT_HAS_OPENGL
        order.push_back(DiligentDeviceType::OpenGL);
#endif
        return order;
    }

    // ---- DiligentTextureBackend ----

    DiligentTextureBackend::DiligentTextureBackend(DiligentGraphicsBackend& owner, const ImageData& data)
        : owner_(owner)
        , width_(data.width)
        , height_(data.height)
        , mipLevels_(std::max(1, data.mipLevels))
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("CNA Diligent: texture dimensions must be positive");

        Dg::TextureDesc desc;
        desc.Name = "CNA texture";
        desc.Type = Dg::RESOURCE_DIM_TEX_2D;
        desc.Width = static_cast<Dg::Uint32>(width_);
        desc.Height = static_cast<Dg::Uint32>(height_);
        desc.MipLevels = static_cast<Dg::Uint32>(mipLevels_);
        desc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = Dg::USAGE_DEFAULT;
        desc.BindFlags = Dg::BIND_SHADER_RESOURCE;

        const std::size_t level0Bytes = static_cast<std::size_t>(width_) * height_ * 4;
        Dg::TextureSubResData level0{};
        level0.pData = data.pixels.size() >= level0Bytes ? data.pixels.data() : nullptr;
        level0.Stride = static_cast<Dg::Uint64>(width_) * 4;

        Dg::TextureData initialData{};
        if (level0.pData != nullptr)
        {
            initialData.pSubResources = &level0;
            initialData.NumSubresources = 1;
            initialData.pContext = owner_.context_;
        }

        // Diligent requires initial data either for every mip level or for none, so a mipped
        // texture is created empty and its levels are filled by UpdatePixelsLevel().
        owner_.device_->CreateTexture(desc, mipLevels_ == 1 ? &initialData : nullptr, &texture_);
        if (!texture_)
            throw std::runtime_error("CNA Diligent: CreateTexture failed");

        if (mipLevels_ > 1 && level0.pData != nullptr)
            UpdatePixelsLevel(0, static_cast<const std::uint8_t*>(level0.pData), width_, height_);

        srv_ = texture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        if (srv_ == nullptr)
            throw std::runtime_error("CNA Diligent: texture has no shader resource view");
    }

    DiligentTextureBackend::~DiligentTextureBackend() = default;

    void DiligentTextureBackend::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            return;

        Dg::Box box;
        box.MinX = 0;
        box.MaxX = static_cast<Dg::Uint32>(width_);
        box.MinY = 0;
        box.MaxY = static_cast<Dg::Uint32>(height_);

        Dg::TextureSubResData subresource{};
        subresource.pData = rgba;
        subresource.Stride = static_cast<Dg::Uint64>(stride > 0 ? stride : width_ * 4);

        owner_.context_->UpdateTexture(texture_, 0, 0, box, subresource,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentTextureBackend::UpdatePixelsLevel(int level, const std::uint8_t* rgba,
                                                   int levelW, int levelH)
    {
        if (rgba == nullptr || level < 0 || level >= mipLevels_ || levelW <= 0 || levelH <= 0)
            return;

        Dg::Box box;
        box.MinX = 0;
        box.MaxX = static_cast<Dg::Uint32>(levelW);
        box.MinY = 0;
        box.MaxY = static_cast<Dg::Uint32>(levelH);

        Dg::TextureSubResData subresource{};
        subresource.pData = rgba;
        subresource.Stride = static_cast<Dg::Uint64>(levelW) * 4;

        owner_.context_->UpdateTexture(texture_, static_cast<Dg::Uint32>(level), 0, box, subresource,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    bool DiligentTextureBackend::GetData(int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        if (level < 0 || level >= mipLevels_)
            return false;
        const int levelW = MipLevelExtent(width_, level);
        const int levelH = MipLevelExtent(height_, level);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelW || y + h > levelH)
            return false;
        return owner_.ReadTextureRegion(texture_, static_cast<Dg::Uint32>(level), 0, x, y, 0,
                                        w, h, 1, data, dataLength);
    }

    // ---- DiligentTextureCubeBackend ----

    DiligentTextureCubeBackend::DiligentTextureCubeBackend(DiligentGraphicsBackend& owner, int size,
                                                            bool mipMap, int /*surfaceFormat*/)
        : owner_(owner)
        , size_(size)
        , mipLevels_(mipMap ? MipLevelCount(size, size) : 1)
    {
        if (size_ <= 0)
            throw std::runtime_error("CNA Diligent: cube face size must be positive");

        Dg::TextureDesc desc;
        desc.Name = "CNA cube texture";
        desc.Type = Dg::RESOURCE_DIM_TEX_CUBE;
        desc.Width = static_cast<Dg::Uint32>(size_);
        desc.Height = static_cast<Dg::Uint32>(size_);
        desc.ArraySize = 6;
        desc.MipLevels = static_cast<Dg::Uint32>(mipLevels_);
        desc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = Dg::USAGE_DEFAULT;
        desc.BindFlags = Dg::BIND_SHADER_RESOURCE;

        owner_.device_->CreateTexture(desc, nullptr, &texture_);
        if (!texture_)
            throw std::runtime_error("CNA Diligent: cube texture creation failed");

        srv_ = texture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        if (srv_ == nullptr)
            throw std::runtime_error("CNA Diligent: cube texture has no shader resource view");
    }

    DiligentTextureCubeBackend::~DiligentTextureCubeBackend() = default;

    bool DiligentTextureCubeBackend::SetData(int face, int level, int x, int y, int w, int h,
                                              const void* data, int dataLength)
    {
        if (data == nullptr || face < 0 || face >= 6 || level < 0 || level >= mipLevels_ ||
            w <= 0 || h <= 0)
            return false;
        const std::size_t requiredBytes = static_cast<std::size_t>(w) * h * 4;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            return false;
        const int levelSize = MipLevelExtent(size_, level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize)
            return false;

        Dg::Box box;
        box.MinX = static_cast<Dg::Uint32>(x);
        box.MaxX = static_cast<Dg::Uint32>(x + w);
        box.MinY = static_cast<Dg::Uint32>(y);
        box.MaxY = static_cast<Dg::Uint32>(y + h);

        Dg::TextureSubResData subresource{};
        subresource.pData = data;
        subresource.Stride = static_cast<Dg::Uint64>(w) * 4;

        owner_.context_->UpdateTexture(texture_, static_cast<Dg::Uint32>(level),
                                       static_cast<Dg::Uint32>(face), box, subresource,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return true;
    }

    bool DiligentTextureCubeBackend::GetData(int face, int level, int x, int y, int w, int h,
                                              void* data, int dataLength) const
    {
        if (face < 0 || face >= 6 || level < 0 || level >= mipLevels_)
            return false;
        const int levelSize = MipLevelExtent(size_, level);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelSize || y + h > levelSize)
            return false;
        return owner_.ReadTextureRegion(texture_, static_cast<Dg::Uint32>(level),
                                        static_cast<Dg::Uint32>(face), x, y, 0, w, h, 1,
                                        data, dataLength);
    }

    // ---- DiligentTexture3DBackend ----

    DiligentTexture3DBackend::DiligentTexture3DBackend(DiligentGraphicsBackend& owner, int width,
                                                        int height, int depth, bool mipMap,
                                                        int /*surfaceFormat*/)
        : owner_(owner)
        , width_(width)
        , height_(height)
        , depth_(depth)
        , mipLevels_(mipMap ? MipLevelCount(width, height) : 1)
    {
        if (width_ <= 0 || height_ <= 0 || depth_ <= 0)
            throw std::runtime_error("CNA Diligent: volume texture dimensions must be positive");

        Dg::TextureDesc desc;
        desc.Name = "CNA volume texture";
        desc.Type = Dg::RESOURCE_DIM_TEX_3D;
        desc.Width = static_cast<Dg::Uint32>(width_);
        desc.Height = static_cast<Dg::Uint32>(height_);
        desc.Depth = static_cast<Dg::Uint32>(depth_);
        desc.MipLevels = static_cast<Dg::Uint32>(mipLevels_);
        desc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = Dg::USAGE_DEFAULT;
        desc.BindFlags = Dg::BIND_SHADER_RESOURCE;

        owner_.device_->CreateTexture(desc, nullptr, &texture_);
        if (!texture_)
            throw std::runtime_error("CNA Diligent: volume texture creation failed");

        srv_ = texture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        if (srv_ == nullptr)
            throw std::runtime_error("CNA Diligent: volume texture has no shader resource view");
    }

    DiligentTexture3DBackend::~DiligentTexture3DBackend() = default;

    bool DiligentTexture3DBackend::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                            const void* data, int dataLength)
    {
        if (data == nullptr || level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0)
            return false;
        const std::size_t requiredBytes = static_cast<std::size_t>(w) * h * depth * 4;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            return false;
        const int levelW = MipLevelExtent(width_, level);
        const int levelH = MipLevelExtent(height_, level);
        const int levelD = MipLevelExtent(depth_, level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;

        Dg::Box box;
        box.MinX = static_cast<Dg::Uint32>(x);
        box.MaxX = static_cast<Dg::Uint32>(x + w);
        box.MinY = static_cast<Dg::Uint32>(y);
        box.MaxY = static_cast<Dg::Uint32>(y + h);
        box.MinZ = static_cast<Dg::Uint32>(z);
        box.MaxZ = static_cast<Dg::Uint32>(z + depth);

        Dg::TextureSubResData subresource{};
        subresource.pData = data;
        subresource.Stride = static_cast<Dg::Uint64>(w) * 4;
        subresource.DepthStride = static_cast<Dg::Uint64>(w) * h * 4;

        owner_.context_->UpdateTexture(texture_, static_cast<Dg::Uint32>(level), 0, box, subresource,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return true;
    }

    bool DiligentTexture3DBackend::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                            void* data, int dataLength) const
    {
        if (level < 0 || level >= mipLevels_)
            return false;
        const int levelW = MipLevelExtent(width_, level);
        const int levelH = MipLevelExtent(height_, level);
        const int levelD = MipLevelExtent(depth_, level);
        if (x < 0 || y < 0 || z < 0 || w <= 0 || h <= 0 || depth <= 0 ||
            x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        return owner_.ReadTextureRegion(texture_, static_cast<Dg::Uint32>(level), 0, x, y, z,
                                        w, h, depth, data, dataLength);
    }

    // ---- DiligentVertexBufferBackend ----

    DiligentVertexBufferBackend::DiligentVertexBufferBackend(DiligentGraphicsBackend& owner,
                                                              int vertexCapacity)
        : owner_(owner)
        , capacity_(std::max(0, vertexCapacity))
    {
    }

    DiligentVertexBufferBackend::~DiligentVertexBufferBackend() = default;

    void DiligentVertexBufferBackend::SetData(const void* data, int vertexCount,
                                               std::size_t strideInBytes)
    {
        if (vertexCount < 0)
            throw std::out_of_range("CNA Diligent: vertex count must not be negative");
        stride_ = strideInBytes;
        vertexCount_ = vertexCount;
        capacity_ = std::max(capacity_, vertexCount);
        if (data == nullptr || vertexCount == 0 || strideInBytes == 0)
            return;

        const std::size_t requiredBytes = static_cast<std::size_t>(vertexCount) * strideInBytes;
        if (!buffer_ || allocatedBytes_ < requiredBytes)
        {
            buffer_.Release();
            Dg::BufferDesc desc;
            desc.Name = "CNA vertex buffer";
            desc.Size = static_cast<Dg::Uint64>(requiredBytes);
            desc.BindFlags = Dg::BIND_VERTEX_BUFFER;
            desc.Usage = Dg::USAGE_DEFAULT;
            owner_.device_->CreateBuffer(desc, nullptr, &buffer_);
            if (!buffer_)
                throw std::runtime_error("CNA Diligent: vertex buffer creation failed");
            allocatedBytes_ = requiredBytes;
        }

        owner_.context_->UpdateBuffer(buffer_, 0, static_cast<Dg::Uint64>(requiredBytes), data,
                                      Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentVertexBufferBackend::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // The built-in shaders are selected by vertex stride (see DiligentGraphicsBackend::
        // DrawInternal), which the declaration already carries; genuinely custom element layouts
        // need the custom-shader path that is not part of this baseline, so the declaration is
        // deliberately consumed only for its stride rather than silently ignored.
        stride_ = static_cast<std::size_t>(vertexDeclaration.getVertexStrideProperty());
    }

    // ---- DiligentIndexBufferBackend ----

    DiligentIndexBufferBackend::DiligentIndexBufferBackend(DiligentGraphicsBackend& owner,
                                                            int indexCapacity, bool thirtyTwoBit)
        : owner_(owner)
        , capacity_(std::max(0, indexCapacity))
        , thirtyTwoBit_(thirtyTwoBit)
    {
    }

    DiligentIndexBufferBackend::~DiligentIndexBufferBackend() = default;

    void DiligentIndexBufferBackend::SetData16(const void* data, int indexCount)
    {
        if (thirtyTwoBit_)
            throw std::runtime_error("CNA Diligent: 16-bit upload into a 32-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void DiligentIndexBufferBackend::SetData32(const void* data, int indexCount)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error("CNA Diligent: 32-bit upload into a 16-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    void DiligentIndexBufferBackend::Upload(const void* data, int indexCount, std::size_t elementSize)
    {
        if (indexCount < 0)
            throw std::out_of_range("CNA Diligent: index count must not be negative");
        indexCount_ = indexCount;
        capacity_ = std::max(capacity_, indexCount);
        if (data == nullptr || indexCount == 0)
            return;

        const std::size_t requiredBytes = static_cast<std::size_t>(indexCount) * elementSize;
        if (!buffer_ || allocatedBytes_ < requiredBytes)
        {
            buffer_.Release();
            Dg::BufferDesc desc;
            desc.Name = "CNA index buffer";
            desc.Size = static_cast<Dg::Uint64>(requiredBytes);
            desc.BindFlags = Dg::BIND_INDEX_BUFFER;
            desc.Usage = Dg::USAGE_DEFAULT;
            owner_.device_->CreateBuffer(desc, nullptr, &buffer_);
            if (!buffer_)
                throw std::runtime_error("CNA Diligent: index buffer creation failed");
            allocatedBytes_ = requiredBytes;
        }

        owner_.context_->UpdateBuffer(buffer_, 0, static_cast<Dg::Uint64>(requiredBytes), data,
                                      Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // ---- DiligentSpriteBatchBackend ----

    DiligentSpriteBatchBackend::DiligentSpriteBatchBackend(DiligentGraphicsBackend& owner)
        : owner_(owner)
    {
    }

    DiligentSpriteBatchBackend::~DiligentSpriteBatchBackend() = default;

    void DiligentSpriteBatchBackend::Begin()
    {
        vertices_.clear();
        bufferedSprites_ = 0;
        currentTexture_ = nullptr;
        inBatch_ = true;
    }

    void DiligentSpriteBatchBackend::End()
    {
        Flush();
        inBatch_ = false;
        hasTransform_ = false;
    }

    void DiligentSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
    {
        Flush();
        transform_ = m;
        hasTransform_ = true;
    }

    void DiligentSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        if (textureFilter == filter_)
            return;
        Flush();
        filter_ = textureFilter;
    }

    void DiligentSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU == addressU_ && addressV == addressV_)
            return;
        Flush();
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void DiligentSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle destination(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
                                    texture.GetWidth(), texture.GetHeight());
        const Rectangle source(0, 0, texture.GetWidth(), texture.GetHeight());
        Draw(texture, destination, source, Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void DiligentSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void DiligentSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           float layerDepth)
    {
        const auto* diligentTexture = dynamic_cast<const DiligentTextureBackend*>(&texture);
        if (diligentTexture == nullptr)
            throw std::runtime_error("CNA Diligent: sprite draw with a foreign texture backend");

        if (currentTexture_ != nullptr && currentTexture_ != diligentTexture)
            Flush();
        currentTexture_ = diligentTexture;

        PushQuad(*diligentTexture, destinationRectangle, sourceRectangle, color, rotation, origin,
                 effects, layerDepth);
    }

    void DiligentSpriteBatchBackend::PushQuad(const DiligentTextureBackend& texture,
                                               const Rectangle& destinationRectangle,
                                               const Rectangle& sourceRectangle,
                                               const Color& color,
                                               float rotation,
                                               const Vector2& origin,
                                               SpriteEffects effects,
                                               float layerDepth)
    {
        const float textureWidth = static_cast<float>(std::max(1, texture.GetWidth()));
        const float textureHeight = static_cast<float>(std::max(1, texture.GetHeight()));

        float u0 = static_cast<float>(sourceRectangle.X) / textureWidth;
        float v0 = static_cast<float>(sourceRectangle.Y) / textureHeight;
        float u1 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / textureWidth;
        float v1 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / textureHeight;

        const int effectsValue = static_cast<int>(effects);
        if ((effectsValue & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
            std::swap(u0, u1);
        if ((effectsValue & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
            std::swap(v0, v1);

        // XNA's origin is expressed in source texels; the destination rectangle may be scaled
        // relative to the source, so the origin scales with it.
        const float sourceWidth = static_cast<float>(std::max(1, sourceRectangle.Width));
        const float sourceHeight = static_cast<float>(std::max(1, sourceRectangle.Height));
        const float destinationWidth = static_cast<float>(destinationRectangle.Width);
        const float destinationHeight = static_cast<float>(destinationRectangle.Height);
        const float originX = origin.X * destinationWidth / sourceWidth;
        const float originY = origin.Y * destinationHeight / sourceHeight;

        const float destinationX = static_cast<float>(destinationRectangle.X);
        const float destinationY = static_cast<float>(destinationRectangle.Y);
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);

        const float cornerX[4] = {-originX, destinationWidth - originX,
                                  -originX, destinationWidth - originX};
        const float cornerY[4] = {-originY, -originY,
                                  destinationHeight - originY, destinationHeight - originY};
        const float cornerU[4] = {u0, u1, u0, u1};
        const float cornerV[4] = {v0, v0, v1, v1};

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        SpriteVertex quad[4];
        for (int corner = 0; corner < 4; ++corner)
        {
            quad[corner].x = destinationX + cornerX[corner] * cosine - cornerY[corner] * sine;
            quad[corner].y = destinationY + cornerX[corner] * sine + cornerY[corner] * cosine;
            quad[corner].z = std::clamp(layerDepth, 0.0f, 1.0f);
            quad[corner].u = cornerU[corner];
            quad[corner].v = cornerV[corner];
            quad[corner].r = r;
            quad[corner].g = g;
            quad[corner].b = b;
            quad[corner].a = a;
        }

        vertices_.push_back(quad[0]);
        vertices_.push_back(quad[1]);
        vertices_.push_back(quad[2]);
        vertices_.push_back(quad[3]);
        ++bufferedSprites_;
    }

    void DiligentSpriteBatchBackend::EnsureCapacity(std::size_t spriteCount)
    {
        const std::size_t requiredVertexBytes = spriteCount * 4 * sizeof(SpriteVertex);
        if (!vertexBuffer_ || vertexBuffer_->GetDesc().Size < requiredVertexBytes)
        {
            vertexBuffer_.Release();
            Dg::BufferDesc desc;
            desc.Name = "CNA sprite vertices";
            desc.Size = static_cast<Dg::Uint64>(requiredVertexBytes);
            desc.BindFlags = Dg::BIND_VERTEX_BUFFER;
            desc.Usage = Dg::USAGE_DYNAMIC;
            desc.CPUAccessFlags = Dg::CPU_ACCESS_WRITE;
            owner_.device_->CreateBuffer(desc, nullptr, &vertexBuffer_);
            if (!vertexBuffer_)
                throw std::runtime_error("CNA Diligent: sprite vertex buffer creation failed");
        }

        const std::size_t requiredIndices = spriteCount * 6;
        if (!indexBuffer_ || indexBuffer_->GetDesc().Size < requiredIndices * sizeof(std::uint16_t))
        {
            if (requiredIndices > 0xFFFFu)
                throw std::runtime_error("CNA Diligent: sprite batch exceeds the 16-bit index range");

            std::vector<std::uint16_t> indices(requiredIndices);
            for (std::size_t sprite = 0; sprite < spriteCount; ++sprite)
            {
                const auto base = static_cast<std::uint16_t>(sprite * 4);
                indices[sprite * 6 + 0] = base;
                indices[sprite * 6 + 1] = static_cast<std::uint16_t>(base + 1);
                indices[sprite * 6 + 2] = static_cast<std::uint16_t>(base + 2);
                indices[sprite * 6 + 3] = static_cast<std::uint16_t>(base + 1);
                indices[sprite * 6 + 4] = static_cast<std::uint16_t>(base + 3);
                indices[sprite * 6 + 5] = static_cast<std::uint16_t>(base + 2);
            }

            indexBuffer_.Release();
            Dg::BufferDesc desc;
            desc.Name = "CNA sprite indices";
            desc.Size = static_cast<Dg::Uint64>(indices.size() * sizeof(std::uint16_t));
            desc.BindFlags = Dg::BIND_INDEX_BUFFER;
            desc.Usage = Dg::USAGE_IMMUTABLE;
            Dg::BufferData data{indices.data(), desc.Size};
            owner_.device_->CreateBuffer(desc, &data, &indexBuffer_);
            if (!indexBuffer_)
                throw std::runtime_error("CNA Diligent: sprite index buffer creation failed");
        }
    }

    void DiligentSpriteBatchBackend::Flush()
    {
        if (bufferedSprites_ == 0 || currentTexture_ == nullptr)
        {
            vertices_.clear();
            bufferedSprites_ = 0;
            return;
        }

        EnsureCapacity(bufferedSprites_);

        void* mapped = nullptr;
        owner_.context_->MapBuffer(vertexBuffer_, Dg::MAP_WRITE, Dg::MAP_FLAG_DISCARD, mapped);
        if (mapped != nullptr)
        {
            std::memcpy(mapped, vertices_.data(), vertices_.size() * sizeof(SpriteVertex));
            owner_.context_->UnmapBuffer(vertexBuffer_, Dg::MAP_WRITE);
        }

        owner_.DrawSpriteQuads(vertexBuffer_, indexBuffer_, bufferedSprites_, *currentTexture_,
                               hasTransform_ ? &transform_ : nullptr, filter_, addressU_, addressV_);

        vertices_.clear();
        bufferedSprites_ = 0;
    }

    // ---- DiligentGraphicsBackend: pipeline key ----

    bool DiligentGraphicsBackend::PipelineKey::operator==(const PipelineKey& other) const noexcept
    {
        return variant == other.variant && topology == other.topology && blend == other.blend &&
               blendFuncs == other.blendFuncs && writeMask == other.writeMask &&
               depth == other.depth && stencilFront == other.stencilFront &&
               stencilBack == other.stencilBack && stencilMasks == other.stencilMasks &&
               raster == other.raster;
    }

    std::size_t DiligentGraphicsBackend::PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(key.variant);
        const std::uint32_t fields[] = {key.topology, key.blend, key.blendFuncs, key.writeMask,
                                        key.depth, key.stencilFront, key.stencilBack,
                                        key.stencilMasks, key.raster};
        for (const std::uint32_t field : fields)
            hash = hash * 1099511628211ull ^ static_cast<std::size_t>(field);
        return hash;
    }

    // ---- DiligentGraphicsBackend ----

    DiligentGraphicsBackend::DiligentGraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , swapInterval_(args.swapInterval)
        , presentationMode_(args.presentationMode)
    {
        if (window_ == nullptr)
            throw std::runtime_error("CNA Diligent: a live SDL window is required");

        SDL_GetWindowSizeInPixels(window_, &physicalWidth_, &physicalHeight_);
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            SDL_GetWindowSize(window_, &physicalWidth_, &physicalHeight_);

        CreateDeviceAndSwapChain(args);
        CreateConstantBuffer();

        maxTextureDimension_ = static_cast<int>(device_->GetAdapterInfo().Texture.MaxTexture2DDimension);
        if (maxTextureDimension_ <= 0)
            maxTextureDimension_ = 16384;

        // The alpha-blended, depth-tested defaults every other CNA backend starts from.
        state_.blend = PackBytes(4, 5, 4, 5);
        state_.blendFuncs = PackBytes(0, 0, 1, 0);
        state_.writeMask = 15;
        state_.depth = PackBytes(1, 1, 3, 0);
        state_.stencilMasks = PackBytes(0xFF, 0xFF, 0, 0);
        state_.raster = PackBytes(0, 0, 0, 0);

        IGraphicsBackend::RegisterForWindow(window_, this);
        CNA::Logger::Info(std::string("CNA Diligent: device type ") + GetDeviceTypeName(deviceType_),
                          CNA::LogCategory::GPU);
    }

    DiligentGraphicsBackend::~DiligentGraphicsBackend()
    {
        if (window_ != nullptr)
            IGraphicsBackend::UnregisterForWindow(window_);
        if (context_)
        {
            context_->Flush();
            context_->WaitForIdle();
        }
        pipelines_.clear();
        samplers_.clear();
    }

    void DiligentGraphicsBackend::CreateDeviceAndSwapChain(const GraphicsBackendCreateArgs& args)
    {
        std::vector<DiligentDeviceType> candidates;
        if (const char* override = std::getenv("CNA_DILIGENT_DEVICE"); override != nullptr)
            candidates = ParseDeviceTypeOverride(override);
        else
            candidates = GetDeviceTypePreferenceOrder();

        if (candidates.empty())
            throw std::runtime_error("CNA Diligent: this build contains no Diligent engine");

        std::string failures;
        for (const DiligentDeviceType candidate : candidates)
        {
            try
            {
                if (TryCreateDevice(candidate, args.multiSampleCount))
                {
                    deviceType_ = candidate;
                    return;
                }
                failures += std::string(failures.empty() ? "" : ", ") + GetDeviceTypeName(candidate) +
                            " (device creation returned no device)";
            }
            catch (const std::exception& error)
            {
                failures += std::string(failures.empty() ? "" : ", ") + GetDeviceTypeName(candidate) +
                            " (" + error.what() + ")";
            }
            device_.Release();
            context_.Release();
            swapChain_.Release();
            engineFactory_.Release();
        }

        throw std::runtime_error("CNA Diligent: no device type could be created -- tried " + failures);
    }

    bool DiligentGraphicsBackend::TryCreateDevice(DiligentDeviceType type, int /*multiSampleCount*/)
    {
        Dg::SwapChainDesc swapChainDesc;
        swapChainDesc.Width = static_cast<Dg::Uint32>(std::max(1, physicalWidth_));
        swapChainDesc.Height = static_cast<Dg::Uint32>(std::max(1, physicalHeight_));
        // A non-sRGB back buffer keeps colours numerically identical to what a game wrote, matching
        // every other CNA backend; Diligent's own default is the sRGB variant.
        swapChainDesc.ColorBufferFormat = Dg::TEX_FORMAT_RGBA8_UNORM;
        // XNA's DepthFormat family tops out at Depth24Stencil8 and CNA's stencil support needs the
        // stencil half, so a combined format is used regardless of the requested DepthFormat.
        swapChainDesc.DepthBufferFormat = Dg::TEX_FORMAT_D24_UNORM_S8_UINT;

        Dg::NativeWindow nativeWindow{};
#if defined(__linux__)
        SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        const char* driver = SDL_GetCurrentVideoDriver();
        if (driver != nullptr && std::strcmp(driver, "x11") == 0)
        {
            nativeWindow.pDisplay = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            nativeWindow.WindowId = static_cast<Dg::Uint32>(
                SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
            if (nativeWindow.pDisplay == nullptr || nativeWindow.WindowId == 0)
                throw std::runtime_error("SDL did not expose X11 display/window properties");
        }
        else
        {
            // Diligent's Linux native window is X11/XCB only; a Wayland session has to go through
            // SDL's own X11 fallback (SDL_VIDEODRIVER=x11).
            throw std::runtime_error(std::string("unsupported SDL video driver for Diligent: ") +
                                     (driver != nullptr ? driver : "unknown"));
        }
#elif defined(_WIN32)
        SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        nativeWindow.hWnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (nativeWindow.hWnd == nullptr)
            throw std::runtime_error("SDL did not expose a Win32 HWND");
#else
        throw std::runtime_error("native window handling is not implemented on this platform");
#endif

        switch (type)
        {
#if CNA_DILIGENT_HAS_VULKAN
            case DiligentDeviceType::Vulkan:
            {
                auto* factory = Dg::GetEngineFactoryVk();
                Dg::EngineVkCreateInfo createInfo;
                factory->CreateDeviceAndContextsVk(createInfo, &device_, &context_);
                if (!device_ || !context_)
                    return false;
                factory->CreateSwapChainVk(device_, context_, swapChainDesc, nativeWindow, &swapChain_);
                engineFactory_ = Dg::RefCntAutoPtr<Dg::IEngineFactory>(factory);
                break;
            }
#endif
#if CNA_DILIGENT_HAS_OPENGL
            case DiligentDeviceType::OpenGL:
            {
                auto* factory = Dg::GetEngineFactoryOpenGL();
                Dg::EngineGLCreateInfo createInfo;
                createInfo.Window = nativeWindow;
                // XNA projection matrices produce Direct3D-style [0,1] clip depth, so the GL
                // device is asked for the same range instead of GL's default [-1,1].
                createInfo.ZeroToOneNDZ = true;
                factory->CreateDeviceAndSwapChainGL(createInfo, &device_, &context_, swapChainDesc,
                                                    &swapChain_);
                engineFactory_ = Dg::RefCntAutoPtr<Dg::IEngineFactory>(factory);
                break;
            }
#endif
#if CNA_DILIGENT_HAS_D3D11
            case DiligentDeviceType::D3D11:
            {
                auto* factory = Dg::GetEngineFactoryD3D11();
                Dg::EngineD3D11CreateInfo createInfo;
                factory->CreateDeviceAndContextsD3D11(createInfo, &device_, &context_);
                if (!device_ || !context_)
                    return false;
                factory->CreateSwapChainD3D11(device_, context_, swapChainDesc, Dg::FullScreenModeDesc{},
                                              nativeWindow, &swapChain_);
                engineFactory_ = Dg::RefCntAutoPtr<Dg::IEngineFactory>(factory);
                break;
            }
#endif
#if CNA_DILIGENT_HAS_D3D12
            case DiligentDeviceType::D3D12:
            {
                auto* factory = Dg::GetEngineFactoryD3D12();
                Dg::EngineD3D12CreateInfo createInfo;
                if (!factory->LoadD3D12())
                    return false;
                factory->CreateDeviceAndContextsD3D12(createInfo, &device_, &context_);
                if (!device_ || !context_)
                    return false;
                factory->CreateSwapChainD3D12(device_, context_, swapChainDesc, Dg::FullScreenModeDesc{},
                                              nativeWindow, &swapChain_);
                engineFactory_ = Dg::RefCntAutoPtr<Dg::IEngineFactory>(factory);
                break;
            }
#endif
            default:
                return false;
        }

        return device_ && context_ && swapChain_;
    }

    void DiligentGraphicsBackend::CreateConstantBuffer()
    {
        Dg::BufferDesc desc;
        desc.Name = "CNA shader constants";
        desc.Size = sizeof(ShaderConstants);
        desc.BindFlags = Dg::BIND_UNIFORM_BUFFER;
        desc.Usage = Dg::USAGE_DYNAMIC;
        desc.CPUAccessFlags = Dg::CPU_ACCESS_WRITE;
        device_->CreateBuffer(desc, nullptr, &constantBuffer_);
        if (!constantBuffer_)
            throw std::runtime_error("CNA Diligent: constant buffer creation failed");
    }

    void DiligentGraphicsBackend::SyncSwapChainSize()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width <= 0 || height <= 0)
            return;
        if (width == physicalWidth_ && height == physicalHeight_)
            return;

        physicalWidth_ = width;
        physicalHeight_ = height;
        swapChain_->Resize(static_cast<Dg::Uint32>(width), static_cast<Dg::Uint32>(height));
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::EnsureRenderTargetsBound()
    {
        if (renderTargetsBound_)
            return;
        Dg::ITextureView* renderTarget = swapChain_->GetCurrentBackBufferRTV();
        Dg::ITextureView* depthStencil = swapChain_->GetDepthBufferDSV();
        context_->SetRenderTargets(1, &renderTarget, depthStencil,
                                   Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        renderTargetsBound_ = true;
        ApplyViewportAndScissor();
    }

    DiligentGraphicsBackend::LogicalViewport DiligentGraphicsBackend::ComputeLogicalViewport() const
    {
        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physicalWidth_));
        viewport.height = static_cast<float>(std::max(0, physicalHeight_));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalWidth = logicalHeight * static_cast<float>(physicalWidth_) /
                           static_cast<float>(physicalHeight_);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;

        const float scaleX = static_cast<float>(physicalWidth_) / logicalWidth;
        const float scaleY = static_cast<float>(physicalHeight_) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan
                                ? std::max(scaleX, scaleY)
                                : std::min(scaleX, scaleY);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physicalWidth_) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physicalHeight_) - viewport.height) * 0.5f;
        return viewport;
    }

    void DiligentGraphicsBackend::ApplyViewportAndScissor()
    {
        const LogicalViewport logical = ComputeLogicalViewport();
        const float scaleX = logical.logicalWidth > 0.0f ? logical.width / logical.logicalWidth : 1.0f;
        const float scaleY = logical.logicalHeight > 0.0f ? logical.height / logical.logicalHeight : 1.0f;

        Dg::Viewport viewport;
        if (customViewport_)
        {
            viewport.TopLeftX = logical.x + static_cast<float>(viewportRect_[0]) * scaleX;
            viewport.TopLeftY = logical.y + static_cast<float>(viewportRect_[1]) * scaleY;
            viewport.Width = static_cast<float>(viewportRect_[2]) * scaleX;
            viewport.Height = static_cast<float>(viewportRect_[3]) * scaleY;
            viewport.MinDepth = viewportDepth_[0];
            viewport.MaxDepth = viewportDepth_[1];
        }
        else
        {
            viewport.TopLeftX = logical.x;
            viewport.TopLeftY = logical.y;
            viewport.Width = logical.width;
            viewport.Height = logical.height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
        }
        context_->SetViewports(1, &viewport, static_cast<Dg::Uint32>(std::max(1, physicalWidth_)),
                               static_cast<Dg::Uint32>(std::max(1, physicalHeight_)));

        if (scissorEnabled_)
        {
            Dg::Rect scissor;
            scissor.left = static_cast<Dg::Int32>(std::lround(logical.x + scissorRect_[0] * scaleX));
            scissor.top = static_cast<Dg::Int32>(std::lround(logical.y + scissorRect_[1] * scaleY));
            scissor.right = static_cast<Dg::Int32>(
                std::lround(logical.x + (scissorRect_[0] + scissorRect_[2]) * scaleX));
            scissor.bottom = static_cast<Dg::Int32>(
                std::lround(logical.y + (scissorRect_[1] + scissorRect_[3]) * scaleY));
            context_->SetScissorRects(1, &scissor, static_cast<Dg::Uint32>(std::max(1, physicalWidth_)),
                                      static_cast<Dg::Uint32>(std::max(1, physicalHeight_)));
        }
    }

    void DiligentGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        const float clearColor[] = {r, g, b, a};
        context_->ClearRenderTarget(swapChain_->GetCurrentBackBufferRTV(), clearColor,
                                    Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentGraphicsBackend::ClearDepth(float depth)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        context_->ClearDepthStencil(swapChain_->GetDepthBufferDSV(), Dg::CLEAR_DEPTH_FLAG, depth, 0,
                                    Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentGraphicsBackend::ClearStencil(int stencil)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        context_->ClearDepthStencil(swapChain_->GetDepthBufferDSV(), Dg::CLEAR_STENCIL_FLAG, 1.0f,
                                    static_cast<Dg::Uint8>(stencil & 0xFF),
                                    Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void DiligentGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        context_->ClearDepthStencil(swapChain_->GetDepthBufferDSV(),
                                    Dg::CLEAR_DEPTH_FLAG | Dg::CLEAR_STENCIL_FLAG, depth,
                                    static_cast<Dg::Uint8>(stencil & 0xFF),
                                    Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        ClearStencil(stencil);
    }

    void DiligentGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                             float depth, int stencil)
    {
        Clear(r, g, b, a);
        ClearDepthAndStencil(depth, stencil);
    }

    void DiligentGraphicsBackend::Present()
    {
        SyncSwapChainSize();
        swapChain_->Present(static_cast<Dg::Uint32>(std::max(0, swapInterval_)));
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        SyncSwapChainSize();
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void DiligentGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA Diligent: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::SetSwapInterval(int interval)
    {
        swapInterval_ = std::clamp(interval, 0, 2);
    }

    bool DiligentGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                            float& logX, float& logY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            return false;
        logX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return true;
    }

    bool DiligentGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                            float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f)
            return false;
        windowX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureBackend> DiligentGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<DiligentTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> DiligentGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<DiligentSpriteBatchBackend>(*this);
    }

    std::unique_ptr<ITextureCubeBackend> DiligentGraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<DiligentTextureCubeBackend>(*this, size, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITexture3DBackend> DiligentGraphicsBackend::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<DiligentTexture3DBackend>(*this, w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<IVertexBufferBackend> DiligentGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<DiligentVertexBufferBackend>(*this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> DiligentGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<DiligentIndexBufferBackend>(*this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> DiligentGraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<DiligentIndexBufferBackend>(*this, index_capacity, true);
    }

    Dg::ITextureView* DiligentGraphicsBackend::GetBackBufferTextureView() const
    {
        return swapChain_->GetCurrentBackBufferRTV();
    }

    void DiligentGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error("CNA Diligent: ReadBackbuffer requires a positive region");

        Dg::ITextureView* backBufferView = GetBackBufferTextureView();
        if (backBufferView == nullptr)
            throw std::runtime_error("CNA Diligent: the swap chain exposes no back buffer view");
        Dg::ITexture* backBuffer = backBufferView->GetTexture();

        // The caller works in logical game coordinates; the back buffer is physical.
        const LogicalViewport viewport = ComputeLogicalViewport();
        const float scaleX = viewport.logicalWidth > 0.0f ? viewport.width / viewport.logicalWidth : 1.0f;
        const float scaleY = viewport.logicalHeight > 0.0f ? viewport.height / viewport.logicalHeight : 1.0f;
        const auto physicalX = static_cast<int>(std::lround(viewport.x + x * scaleX));
        const auto physicalY = static_cast<int>(std::lround(viewport.y + y * scaleY));
        const auto physicalW = std::max(1, static_cast<int>(std::lround(w * scaleX)));
        const auto physicalH = std::max(1, static_cast<int>(std::lround(h * scaleY)));

        Dg::TextureDesc stagingDesc;
        stagingDesc.Name = "CNA backbuffer readback";
        stagingDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
        stagingDesc.Width = static_cast<Dg::Uint32>(physicalW);
        stagingDesc.Height = static_cast<Dg::Uint32>(physicalH);
        stagingDesc.MipLevels = 1;
        stagingDesc.Format = backBuffer->GetDesc().Format;
        stagingDesc.Usage = Dg::USAGE_STAGING;
        stagingDesc.BindFlags = Dg::BIND_NONE;
        stagingDesc.CPUAccessFlags = Dg::CPU_ACCESS_READ;

        Dg::RefCntAutoPtr<Dg::ITexture> staging;
        device_->CreateTexture(stagingDesc, nullptr, &staging);
        if (!staging)
            throw std::runtime_error("CNA Diligent: readback staging texture creation failed");

        Dg::Box sourceBox;
        sourceBox.MinX = static_cast<Dg::Uint32>(std::max(0, physicalX));
        sourceBox.MaxX = static_cast<Dg::Uint32>(std::max(0, physicalX) + physicalW);
        sourceBox.MinY = static_cast<Dg::Uint32>(std::max(0, physicalY));
        sourceBox.MaxY = static_cast<Dg::Uint32>(std::max(0, physicalY) + physicalH);

        // Unbind first: the back buffer cannot be both the bound render target and a copy source,
        // and letting Diligent notice that itself only produces an info message about the same
        // unbinding happening implicitly.
        context_->SetRenderTargets(0, nullptr, nullptr, Dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        renderTargetsBound_ = false;

        Dg::CopyTextureAttribs copyAttribs;
        copyAttribs.pSrcTexture = backBuffer;
        copyAttribs.pSrcBox = &sourceBox;
        copyAttribs.SrcTextureTransitionMode = Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        copyAttribs.pDstTexture = staging;
        copyAttribs.DstTextureTransitionMode = Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context_->CopyTexture(copyAttribs);
        context_->Flush();
        context_->WaitForIdle();

        // MAP_FLAG_DO_NOT_WAIT is the correct flag *because* of the WaitForIdle() above: Diligent's
        // Vulkan backend never synchronizes staging reads itself and asks callers to state that
        // they have already done so.
        Dg::MappedTextureSubresource mapped{};
        context_->MapTextureSubresource(staging, 0, 0, Dg::MAP_READ, Dg::MAP_FLAG_DO_NOT_WAIT,
                                        nullptr, mapped);
        if (mapped.pData == nullptr)
            throw std::runtime_error("CNA Diligent: readback staging texture could not be mapped");

        const bool blueFirst = IsBlueFirstFormat(stagingDesc.Format);

        // Resample the physical region back to the caller's logical region: with letterboxing or a
        // scaled presentation the two differ, and the caller asked in logical pixels.
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        for (int row = 0; row < h; ++row)
        {
            const int sourceRow = std::clamp(static_cast<int>(row * scaleY), 0, physicalH - 1);
            for (int column = 0; column < w; ++column)
            {
                const int sourceColumn = std::clamp(static_cast<int>(column * scaleX), 0, physicalW - 1);
                const std::uint8_t* texel =
                    source + static_cast<std::size_t>(sourceRow) * mapped.Stride + sourceColumn * 4;
                std::uint8_t* destination =
                    pixels + (static_cast<std::size_t>(row) * w + column) * 4;
                destination[0] = blueFirst ? texel[2] : texel[0];
                destination[1] = texel[1];
                destination[2] = blueFirst ? texel[0] : texel[2];
                destination[3] = texel[3];
            }
        }
        context_->UnmapTextureSubresource(staging, 0, 0);
    }

    void DiligentGraphicsBackend::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                                    int count)
    {
        if (renderTargets == nullptr || count <= 0)
        {
            renderTargetsBound_ = false;
            EnsureRenderTargetsBound();
            return;
        }
        // Refusing is deliberate: CreateRenderTarget2D/CreateRenderTargetCube return nullptr on this
        // backend, so any non-empty set here would be a target this backend never created.
        throw std::runtime_error(
            "CNA Diligent: render targets are not implemented yet on this backend");
    }

    bool DiligentGraphicsBackend::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::WireFrame:
                return true;
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return deviceType_ != DiligentDeviceType::OpenGL;
            case CNA::GraphicsCapability::Texture3D:
                return true;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            case CNA::GraphicsCapability::MultipleRenderTargets:
            case CNA::GraphicsCapability::OcclusionQuery:
            case CNA::GraphicsCapability::CustomEffects:
                return false;
        }
        return false;
    }

    void DiligentGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                   int colorDstBlend, int alphaDstBlend,
                                                   int colorBlendFunc, int alphaBlendFunc,
                                                   const BlendWriteState& writeState)
    {
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        state_.blend = PackBytes(colorSrcBlend, colorDstBlend, alphaSrcBlend, alphaDstBlend);
        state_.blendFuncs = PackBytes(colorBlendFunc, alphaBlendFunc, blendEnabled ? 1 : 0, 0);
        // Only slot 0 is meaningful: this backend renders to a single target, and MultiSampleMask
        // has nothing to mask on a single-sampled back buffer.
        state_.writeMask = static_cast<std::uint32_t>(writeState.colorWriteChannels[0] & 0xF);
    }

    void DiligentGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                          int depthFunc,
                                                          bool stencilEnable, int stencilFunc,
                                                          int stencilPass, int stencilFail,
                                                          int stencilDepthFail,
                                                          int stencilMask, int stencilWriteMask,
                                                          int referenceStencil,
                                                          bool twoSidedStencilMode,
                                                          int ccwStencilFunc, int ccwStencilPass,
                                                          int ccwStencilFail, int ccwStencilDepthFail)
    {
        state_.depth = PackBytes(depthEnable ? 1 : 0, depthWriteEnable ? 1 : 0, depthFunc,
                                 stencilEnable ? 1 : 0);
        state_.stencilFront = PackBytes(stencilFunc, stencilPass, stencilFail, stencilDepthFail);
        state_.stencilBack = twoSidedStencilMode
                                 ? PackBytes(ccwStencilFunc, ccwStencilPass, ccwStencilFail,
                                             ccwStencilDepthFail)
                                 : state_.stencilFront;
        state_.stencilMasks = PackBytes(stencilMask, stencilWriteMask, 0, 0);
        referenceStencil_ = referenceStencil;
    }

    void DiligentGraphicsBackend::ApplyRasterizerState(int cullMode, int fillMode,
                                                        bool scissorTestEnable,
                                                        float depthBias, float slopeScaleDepthBias)
    {
        state_.raster = PackBytes(cullMode, fillMode,
                                  static_cast<int>(std::lround(depthBias * 1000.0f)) & 0xFF,
                                  static_cast<int>(std::lround(slopeScaleDepthBias * 16.0f)) & 0xFF);
        scissorEnabled_ = scissorTestEnable;
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                     int maxAnisotropy)
    {
        // Slot 0 is the only sampler any built-in shader in this baseline declares.
        if (slot != 0)
            return;
        samplerFilter_ = filter;
        samplerAddressU_ = addressU;
        samplerAddressV_ = addressV;
        samplerMaxAnisotropy_ = std::clamp(maxAnisotropy, 1, 16);
    }

    void DiligentGraphicsBackend::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void DiligentGraphicsBackend::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void DiligentGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        customViewport_ = true;
        viewportRect_[0] = x;
        viewportRect_[1] = y;
        viewportRect_[2] = w;
        viewportRect_[3] = h;
        viewportDepth_[0] = minDepth;
        viewportDepth_[1] = maxDepth;
        renderTargetsBound_ = false;
    }

    void DiligentGraphicsBackend::SetDepthTestEnabled(bool enabled)
    {
        const std::uint32_t depth = state_.depth;
        state_.depth = (depth & 0xFFFFFF00u) | (enabled ? 1u : 0u);
    }

    void DiligentGraphicsBackend::SetDepthWriteEnabled(bool enabled)
    {
        const std::uint32_t depth = state_.depth;
        state_.depth = (depth & 0xFFFF00FFu) | (enabled ? 0x100u : 0u);
    }

    void DiligentGraphicsBackend::SetBlendEnabled(bool enabled)
    {
        const std::uint32_t funcs = state_.blendFuncs;
        state_.blendFuncs = (funcs & 0xFF00FFFFu) | (enabled ? 0x10000u : 0u);
    }

    Dg::ISampler* DiligentGraphicsBackend::GetOrCreateSampler(int filter, int addressU, int addressV,
                                                              int maxAnisotropy)
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(filter & 0xFF)) |
                                  (static_cast<std::uint64_t>(addressU & 0xFF) << 8) |
                                  (static_cast<std::uint64_t>(addressV & 0xFF) << 16) |
                                  (static_cast<std::uint64_t>(maxAnisotropy & 0xFF) << 24);
        if (const auto it = samplers_.find(key); it != samplers_.end())
            return it->second;

        Dg::SamplerDesc desc;
        ToFilterTypes(filter, desc.MinFilter, desc.MagFilter, desc.MipFilter);
        desc.AddressU = ToAddressMode(addressU);
        desc.AddressV = ToAddressMode(addressV);
        desc.AddressW = ToAddressMode(addressV);
        desc.MaxAnisotropy = static_cast<Dg::Uint32>(std::clamp(maxAnisotropy, 1, 16));

        Dg::RefCntAutoPtr<Dg::ISampler> sampler;
        device_->CreateSampler(desc, &sampler);
        if (!sampler)
            throw std::runtime_error("CNA Diligent: sampler creation failed");
        auto* raw = sampler.RawPtr();
        samplers_.emplace(key, std::move(sampler));
        return raw;
    }

    DiligentGraphicsBackend::PipelineKey DiligentGraphicsBackend::MakePipelineKey(
        ShaderVariant variant, PrimitiveType primitive) const
    {
        PipelineKey key = state_;
        key.variant = variant;
        key.topology = static_cast<std::uint32_t>(ToTopology(primitive));
        return key;
    }

    DiligentGraphicsBackend::CachedPipeline& DiligentGraphicsBackend::GetOrCreatePipeline(
        const PipelineKey& key)
    {
        if (const auto it = pipelines_.find(key); it != pipelines_.end())
            return it->second;

        const char* vertexSource = nullptr;
        const char* pixelSource = nullptr;
        std::vector<Dg::LayoutElement> layout;
        bool usesTexture = false;

        switch (key.variant)
        {
            case ShaderVariant::Sprite:
                vertexSource = kSpriteVertexHlsl;
                pixelSource = kSpritePixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 2, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{2, 0, 4, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                break;
            case ShaderVariant::Colored3D:
                vertexSource = kColoredVertexHlsl;
                pixelSource = kColoredPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 4, Dg::VT_UINT8, Dg::True},
                };
                break;
            case ShaderVariant::Textured3D:
                vertexSource = kTexturedVertexHlsl;
                pixelSource = kTexturedPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                break;
            case ShaderVariant::ColoredTextured3D:
                vertexSource = kColoredTexturedVertexHlsl;
                pixelSource = kTexturedPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 4, Dg::VT_UINT8, Dg::True},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                break;
            case ShaderVariant::LitTextured3D:
                vertexSource = kLitVertexHlsl;
                pixelSource = kLitPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                break;
        }

        const std::string vertexHlsl = std::string(kConstantsHlsl) + vertexSource;
        const std::string pixelHlsl = std::string(kConstantsHlsl) + pixelSource;

        Dg::ShaderCreateInfo shaderCI;
        shaderCI.SourceLanguage = Dg::SHADER_SOURCE_LANGUAGE_HLSL;
        shaderCI.Desc.UseCombinedTextureSamplers = true;
        shaderCI.EntryPoint = "main";

        Dg::RefCntAutoPtr<Dg::IShader> vertexShader;
        shaderCI.Desc.ShaderType = Dg::SHADER_TYPE_VERTEX;
        shaderCI.Desc.Name = "CNA vertex shader";
        shaderCI.Source = vertexHlsl.c_str();
        shaderCI.SourceLength = vertexHlsl.size();
        device_->CreateShader(shaderCI, &vertexShader, nullptr);
        if (!vertexShader)
            throw std::runtime_error("CNA Diligent: vertex shader compilation failed");

        Dg::RefCntAutoPtr<Dg::IShader> pixelShader;
        shaderCI.Desc.ShaderType = Dg::SHADER_TYPE_PIXEL;
        shaderCI.Desc.Name = "CNA pixel shader";
        shaderCI.Source = pixelHlsl.c_str();
        shaderCI.SourceLength = pixelHlsl.size();
        device_->CreateShader(shaderCI, &pixelShader, nullptr);
        if (!pixelShader)
            throw std::runtime_error("CNA Diligent: pixel shader compilation failed");

        Dg::GraphicsPipelineStateCreateInfo psoCI;
        psoCI.PSODesc.Name = "CNA pipeline";
        psoCI.PSODesc.PipelineType = Dg::PIPELINE_TYPE_GRAPHICS;

        auto& graphicsPipeline = psoCI.GraphicsPipeline;
        graphicsPipeline.NumRenderTargets = 1;
        graphicsPipeline.RTVFormats[0] = swapChain_->GetDesc().ColorBufferFormat;
        graphicsPipeline.DSVFormat = swapChain_->GetDesc().DepthBufferFormat;
        graphicsPipeline.PrimitiveTopology = static_cast<Dg::PRIMITIVE_TOPOLOGY>(key.topology);
        graphicsPipeline.InputLayout.LayoutElements = layout.data();
        graphicsPipeline.InputLayout.NumElements = static_cast<Dg::Uint32>(layout.size());

        auto& blend = graphicsPipeline.BlendDesc.RenderTargets[0];
        blend.BlendEnable = ((key.blendFuncs >> 16) & 0xFF) != 0 ? Dg::True : Dg::False;
        blend.SrcBlend = ToBlendFactor(static_cast<int>(key.blend & 0xFF));
        blend.DestBlend = ToBlendFactor(static_cast<int>((key.blend >> 8) & 0xFF));
        blend.SrcBlendAlpha = ToBlendFactor(static_cast<int>((key.blend >> 16) & 0xFF));
        blend.DestBlendAlpha = ToBlendFactor(static_cast<int>((key.blend >> 24) & 0xFF));
        blend.BlendOp = ToBlendOperation(static_cast<int>(key.blendFuncs & 0xFF));
        blend.BlendOpAlpha = ToBlendOperation(static_cast<int>((key.blendFuncs >> 8) & 0xFF));
        auto colorMask = Dg::COLOR_MASK_NONE;
        if (ColorWriteHasRed(static_cast<int>(key.writeMask)))   colorMask |= Dg::COLOR_MASK_RED;
        if (ColorWriteHasGreen(static_cast<int>(key.writeMask))) colorMask |= Dg::COLOR_MASK_GREEN;
        if (ColorWriteHasBlue(static_cast<int>(key.writeMask)))  colorMask |= Dg::COLOR_MASK_BLUE;
        if (ColorWriteHasAlpha(static_cast<int>(key.writeMask))) colorMask |= Dg::COLOR_MASK_ALPHA;
        blend.RenderTargetWriteMask = colorMask;

        auto& depthStencil = graphicsPipeline.DepthStencilDesc;
        depthStencil.DepthEnable = (key.depth & 0xFF) != 0 ? Dg::True : Dg::False;
        depthStencil.DepthWriteEnable = ((key.depth >> 8) & 0xFF) != 0 ? Dg::True : Dg::False;
        depthStencil.DepthFunc = ToComparisonFunction(static_cast<int>((key.depth >> 16) & 0xFF));
        depthStencil.StencilEnable = ((key.depth >> 24) & 0xFF) != 0 ? Dg::True : Dg::False;
        depthStencil.StencilReadMask = static_cast<Dg::Uint8>(key.stencilMasks & 0xFF);
        depthStencil.StencilWriteMask = static_cast<Dg::Uint8>((key.stencilMasks >> 8) & 0xFF);
        depthStencil.FrontFace.StencilFunc = ToComparisonFunction(static_cast<int>(key.stencilFront & 0xFF));
        depthStencil.FrontFace.StencilPassOp = ToStencilOperation(static_cast<int>((key.stencilFront >> 8) & 0xFF));
        depthStencil.FrontFace.StencilFailOp = ToStencilOperation(static_cast<int>((key.stencilFront >> 16) & 0xFF));
        depthStencil.FrontFace.StencilDepthFailOp = ToStencilOperation(static_cast<int>((key.stencilFront >> 24) & 0xFF));
        depthStencil.BackFace.StencilFunc = ToComparisonFunction(static_cast<int>(key.stencilBack & 0xFF));
        depthStencil.BackFace.StencilPassOp = ToStencilOperation(static_cast<int>((key.stencilBack >> 8) & 0xFF));
        depthStencil.BackFace.StencilFailOp = ToStencilOperation(static_cast<int>((key.stencilBack >> 16) & 0xFF));
        depthStencil.BackFace.StencilDepthFailOp = ToStencilOperation(static_cast<int>((key.stencilBack >> 24) & 0xFF));

        auto& rasterizer = graphicsPipeline.RasterizerDesc;
        const int cullMode = static_cast<int>(key.raster & 0xFF);
        // XNA winds front faces clockwise; Diligent's FrontCounterClockwise default matches that
        // only when it stays false, so culling maps directly onto the front/back distinction.
        rasterizer.CullMode = cullMode == 0   ? Dg::CULL_MODE_NONE
                              : cullMode == 1 ? Dg::CULL_MODE_FRONT
                                              : Dg::CULL_MODE_BACK;
        rasterizer.FillMode = ((key.raster >> 8) & 0xFF) != 0 ? Dg::FILL_MODE_WIREFRAME
                                                              : Dg::FILL_MODE_SOLID;
        rasterizer.FrontCounterClockwise = Dg::False;
        rasterizer.ScissorEnable = scissorEnabled_ ? Dg::True : Dg::False;
        rasterizer.DepthBias = static_cast<Dg::Int32>(static_cast<std::int8_t>((key.raster >> 16) & 0xFF));
        rasterizer.SlopeScaledDepthBias =
            static_cast<float>(static_cast<std::int8_t>((key.raster >> 24) & 0xFF)) / 16.0f;

        psoCI.pVS = vertexShader;
        psoCI.pPS = pixelShader;

        Dg::ShaderResourceVariableDesc variables[1];
        Dg::Uint32 variableCount = 0;
        if (usesTexture)
        {
            variables[0] = Dg::ShaderResourceVariableDesc{Dg::SHADER_TYPE_PIXEL, "g_Texture",
                                                          Dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
            variableCount = 1;
        }
        psoCI.PSODesc.ResourceLayout.DefaultVariableType = Dg::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
        psoCI.PSODesc.ResourceLayout.Variables = variableCount > 0 ? variables : nullptr;
        psoCI.PSODesc.ResourceLayout.NumVariables = variableCount;

        CachedPipeline cached;
        device_->CreateGraphicsPipelineState(psoCI, &cached.pipeline);
        if (!cached.pipeline)
            throw std::runtime_error("CNA Diligent: pipeline state creation failed");

        for (const Dg::SHADER_TYPE stage : {Dg::SHADER_TYPE_VERTEX, Dg::SHADER_TYPE_PIXEL})
        {
            if (auto* variable = cached.pipeline->GetStaticVariableByName(stage, "Constants"))
                variable->Set(constantBuffer_);
        }

        cached.pipeline->CreateShaderResourceBinding(&cached.binding, true);
        if (!cached.binding)
            throw std::runtime_error("CNA Diligent: shader resource binding creation failed");
        if (usesTexture)
            cached.textureVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_Texture");

        return pipelines_.emplace(key, std::move(cached)).first->second;
    }

    bool DiligentGraphicsBackend::ReadTextureRegion(Dg::ITexture* texture, Dg::Uint32 mipLevel,
                                                     Dg::Uint32 arraySlice, int x, int y, int z,
                                                     int w, int h, int depth,
                                                     void* data, int dataLength)
    {
        if (texture == nullptr || data == nullptr || w <= 0 || h <= 0 || depth <= 0)
            return false;
        const std::size_t requiredBytes = static_cast<std::size_t>(w) * h * depth * 4;
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < requiredBytes)
            return false;

        const Dg::TextureDesc& sourceDesc = texture->GetDesc();

        Dg::TextureDesc stagingDesc;
        stagingDesc.Name = "CNA texture readback";
        stagingDesc.Type = depth > 1 ? Dg::RESOURCE_DIM_TEX_3D : Dg::RESOURCE_DIM_TEX_2D;
        stagingDesc.Width = static_cast<Dg::Uint32>(w);
        stagingDesc.Height = static_cast<Dg::Uint32>(h);
        if (depth > 1)
            stagingDesc.Depth = static_cast<Dg::Uint32>(depth);
        stagingDesc.MipLevels = 1;
        stagingDesc.Format = sourceDesc.Format;
        stagingDesc.Usage = Dg::USAGE_STAGING;
        stagingDesc.BindFlags = Dg::BIND_NONE;
        stagingDesc.CPUAccessFlags = Dg::CPU_ACCESS_READ;

        Dg::RefCntAutoPtr<Dg::ITexture> staging;
        device_->CreateTexture(stagingDesc, nullptr, &staging);
        if (!staging)
            return false;

        Dg::Box sourceBox;
        sourceBox.MinX = static_cast<Dg::Uint32>(x);
        sourceBox.MaxX = static_cast<Dg::Uint32>(x + w);
        sourceBox.MinY = static_cast<Dg::Uint32>(y);
        sourceBox.MaxY = static_cast<Dg::Uint32>(y + h);
        sourceBox.MinZ = static_cast<Dg::Uint32>(z);
        sourceBox.MaxZ = static_cast<Dg::Uint32>(z + depth);

        Dg::CopyTextureAttribs copyAttribs;
        copyAttribs.pSrcTexture = texture;
        copyAttribs.SrcMipLevel = mipLevel;
        copyAttribs.SrcSlice = arraySlice;
        copyAttribs.pSrcBox = &sourceBox;
        copyAttribs.SrcTextureTransitionMode = Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        copyAttribs.pDstTexture = staging;
        copyAttribs.DstTextureTransitionMode = Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context_->CopyTexture(copyAttribs);
        context_->Flush();
        context_->WaitForIdle();

        // See ReadBackbuffer for why MAP_FLAG_DO_NOT_WAIT is the correct flag after WaitForIdle().
        Dg::MappedTextureSubresource mapped{};
        context_->MapTextureSubresource(staging, 0, 0, Dg::MAP_READ, Dg::MAP_FLAG_DO_NOT_WAIT,
                                        nullptr, mapped);
        if (mapped.pData == nullptr)
            return false;

        const bool blueFirst = IsBlueFirstFormat(sourceDesc.Format);
        auto* destination = static_cast<std::uint8_t*>(data);
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        const std::size_t sliceStride =
            mapped.DepthStride != 0 ? static_cast<std::size_t>(mapped.DepthStride)
                                    : static_cast<std::size_t>(mapped.Stride) * h;
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const std::uint8_t* sourceRow = source + static_cast<std::size_t>(slice) * sliceStride +
                                                static_cast<std::size_t>(row) * mapped.Stride;
                std::uint8_t* destinationRow =
                    destination + ((static_cast<std::size_t>(slice) * h + row) * w) * 4;
                if (!blueFirst)
                {
                    std::memcpy(destinationRow, sourceRow, static_cast<std::size_t>(w) * 4);
                    continue;
                }
                for (int column = 0; column < w; ++column)
                {
                    destinationRow[column * 4 + 0] = sourceRow[column * 4 + 2];
                    destinationRow[column * 4 + 1] = sourceRow[column * 4 + 1];
                    destinationRow[column * 4 + 2] = sourceRow[column * 4 + 0];
                    destinationRow[column * 4 + 3] = sourceRow[column * 4 + 3];
                }
            }
        }
        context_->UnmapTextureSubresource(staging, 0, 0);

        // A readback flushes and waits, so whatever render targets were bound for the frame are no
        // longer guaranteed to be current on the context.
        renderTargetsBound_ = false;
        return true;
    }

    void DiligentGraphicsBackend::UploadConstants(const ShaderConstants& constants)
    {
        void* mapped = nullptr;
        context_->MapBuffer(constantBuffer_, Dg::MAP_WRITE, Dg::MAP_FLAG_DISCARD, mapped);
        if (mapped == nullptr)
            throw std::runtime_error("CNA Diligent: constant buffer could not be mapped");
        std::memcpy(mapped, &constants, sizeof(ShaderConstants));
        context_->UnmapBuffer(constantBuffer_, Dg::MAP_WRITE);
    }

    void DiligentGraphicsBackend::DrawSpriteQuads(Dg::IBuffer* vertexBuffer, Dg::IBuffer* indexBuffer,
                                                   std::size_t spriteCount,
                                                   const DiligentTextureBackend& texture,
                                                   const Matrix* transform, int filter,
                                                   int addressU, int addressV)
    {
        if (spriteCount == 0)
            return;

        SyncSwapChainSize();
        EnsureRenderTargetsBound();

        const LogicalViewport logical = ComputeLogicalViewport();
        Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, std::max(1.0f, logical.logicalWidth), std::max(1.0f, logical.logicalHeight), 0.0f,
            0.0f, 1.0f);
        if (transform != nullptr)
            projection = (*transform) * projection;

        ShaderConstants constants{};
        MatrixToFloats(projection, constants.worldViewProj);
        MatrixToFloats(Matrix::getIdentityProperty(), constants.world);
        constants.diffuseColor[0] = 1.0f;
        constants.diffuseColor[1] = 1.0f;
        constants.diffuseColor[2] = 1.0f;
        constants.diffuseColor[3] = 1.0f;
        constants.flags[0] = 1.0f;
        UploadConstants(constants);

        // Sprites go through the sampler the SpriteBatch selected, which is independent of the
        // GraphicsDevice-level SamplerState the 3D path uses.
        if (auto* view = texture.GetShaderResourceView())
            view->SetSampler(GetOrCreateSampler(filter, addressU, addressV, samplerMaxAnisotropy_));

        CachedPipeline& pipeline = GetOrCreatePipeline(
            MakePipelineKey(ShaderVariant::Sprite, PrimitiveType::TriangleList));
        if (pipeline.textureVariable != nullptr)
            pipeline.textureVariable->Set(texture.GetShaderResourceView());

        context_->SetPipelineState(pipeline.pipeline);
        context_->SetStencilRef(static_cast<Dg::Uint32>(referenceStencil_));
        context_->SetBlendFactors(blendFactor_);
        context_->CommitShaderResources(pipeline.binding, Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const Dg::Uint64 offset = 0;
        Dg::IBuffer* vertexBuffers[] = {vertexBuffer};
        context_->SetVertexBuffers(0, 1, vertexBuffers, &offset,
                                   Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Dg::SET_VERTEX_BUFFERS_FLAG_RESET);
        context_->SetIndexBuffer(indexBuffer, 0, Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Dg::DrawIndexedAttribs drawAttribs;
        drawAttribs.NumIndices = static_cast<Dg::Uint32>(spriteCount * 6);
        drawAttribs.IndexType = Dg::VT_UINT16;
        drawAttribs.Flags = Dg::DRAW_FLAG_VERIFY_ALL;
        context_->DrawIndexed(drawAttribs);
    }

    void DiligentGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                         const Matrix& world, const Matrix& view,
                                                         const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount)
    {
        DrawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void DiligentGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                                const IIndexBufferBackend& ib,
                                                                const Matrix& world, const Matrix& view,
                                                                const Matrix& projection,
                                                                PrimitiveType primitive,
                                                                int primitiveCount)
    {
        DrawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void DiligentGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                    const Matrix& world, const Matrix& view,
                                                    const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        DrawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
    }

    void DiligentGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                                           const IIndexBufferBackend& ib,
                                                           const Matrix& world, const Matrix& view,
                                                           const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount,
                                                           const GpuDrawParams& params)
    {
        DrawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
    }

    void DiligentGraphicsBackend::DrawInternal(const IVertexBufferBackend& vb,
                                                const IIndexBufferBackend* ib,
                                                const Matrix& world, const Matrix& view,
                                                const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams* params)
    {
        if (primitiveCount <= 0)
            return;

        const auto* vertexBuffer = dynamic_cast<const DiligentVertexBufferBackend*>(&vb);
        if (vertexBuffer == nullptr || vertexBuffer->GetBuffer() == nullptr)
            throw std::runtime_error("CNA Diligent: draw with a foreign or empty vertex buffer");

        if (params != nullptr)
        {
            // Each of these selects a genuinely different shader in XNA. Rendering the nearest
            // available variant instead would silently produce a different image, so the draw is
            // refused until the matching phase of plan_diligent.md lands.
            const char* unsupported = nullptr;
            if (params->dualTexture)                 unsupported = "DualTextureEffect";
            else if (params->envMapping)             unsupported = "EnvironmentMapEffect";
            else if (params->skinned)                unsupported = "SkinnedEffect";
            else if (params->pbr)                    unsupported = "PbrEffect";
            else if (params->customEffectBackend)    unsupported = "custom ShaderEffect programs";
            else if (params->instanceCount > 1)      unsupported = "hardware instancing";
            else if (params->fogEnabled)             unsupported = "fog";
            else if (params->alphaTest[0] != 0.0f || params->alphaTest[1] != 0.0f)
                unsupported = "AlphaTestEffect";
            if (unsupported != nullptr)
                throw std::runtime_error(std::string("CNA ") + kBackendName + " backend: " +
                                         unsupported + " is not implemented yet");
        }

        const std::size_t stride = vertexBuffer->GetStride();
        ShaderVariant variant;
        switch (stride)
        {
            case 16: variant = ShaderVariant::Colored3D; break;
            case 20: variant = ShaderVariant::Textured3D; break;
            case 24: variant = ShaderVariant::ColoredTextured3D; break;
            case 32: variant = ShaderVariant::LitTextured3D; break;
            default:
                throw std::runtime_error("CNA Diligent: unsupported vertex stride " +
                                         std::to_string(stride));
        }

        SyncSwapChainSize();
        EnsureRenderTargetsBound();

        ShaderConstants constants{};
        MatrixToFloats(world * view * projection, constants.worldViewProj);
        MatrixToFloats(world, constants.world);
        constants.diffuseColor[0] = 1.0f;
        constants.diffuseColor[1] = 1.0f;
        constants.diffuseColor[2] = 1.0f;
        constants.diffuseColor[3] = 1.0f;
        constants.eyePositionSpecularPower[3] = 16.0f;

        const ITextureBackend* texture = nullptr;
        if (params != nullptr)
        {
            texture = params->texture0;
            for (int component = 0; component < 4; ++component)
                constants.diffuseColor[component] = params->diffuseColor[component];
            for (int component = 0; component < 3; ++component)
            {
                constants.emissiveAmbient[component] =
                    params->ambientColor[component] + params->emissiveColor[component];
                constants.eyePositionSpecularPower[component] = params->eyePositionWorld[component];
                constants.specularColor[component] = params->specularColor[component];
                constants.lightDir[0][component] = params->light0Dir[component];
                constants.lightDir[1][component] = params->light1Dir[component];
                constants.lightDir[2][component] = params->light2Dir[component];
                constants.lightDiffuse[0][component] = params->light0Diffuse[component];
                constants.lightDiffuse[1][component] = params->light1Diffuse[component];
                constants.lightDiffuse[2][component] = params->light2Diffuse[component];
                constants.lightSpecular[0][component] = params->light0Specular[component];
                constants.lightSpecular[1][component] = params->light1Specular[component];
                constants.lightSpecular[2][component] = params->light2Specular[component];
            }
            constants.eyePositionSpecularPower[3] = params->specularPower;
            constants.flags[0] = params->textureEnabled && texture != nullptr ? 1.0f : 0.0f;
            constants.flags[1] = params->vertexColorEnabled ? 1.0f : 0.0f;
            constants.flags[2] = params->lightingEnabled ? 1.0f : 0.0f;
        }
        else
        {
            constants.flags[1] = 1.0f;
        }
        UploadConstants(constants);

        CachedPipeline& pipeline = GetOrCreatePipeline(MakePipelineKey(variant, primitive));
        if (pipeline.textureVariable != nullptr)
        {
            const auto* diligentTexture = dynamic_cast<const DiligentTextureBackend*>(texture);
            if (diligentTexture == nullptr)
                throw std::runtime_error(
                    "CNA Diligent: a textured vertex layout was drawn without a texture");
            if (auto* view = diligentTexture->GetShaderResourceView())
            {
                view->SetSampler(GetOrCreateSampler(samplerFilter_, samplerAddressU_,
                                                    samplerAddressV_, samplerMaxAnisotropy_));
                pipeline.textureVariable->Set(view);
            }
        }

        context_->SetPipelineState(pipeline.pipeline);
        context_->SetStencilRef(static_cast<Dg::Uint32>(referenceStencil_));
        context_->SetBlendFactors(blendFactor_);
        context_->CommitShaderResources(pipeline.binding, Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const Dg::Uint64 offset = 0;
        Dg::IBuffer* vertexBuffers[] = {vertexBuffer->GetBuffer()};
        context_->SetVertexBuffers(0, 1, vertexBuffers, &offset,
                                   Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Dg::SET_VERTEX_BUFFERS_FLAG_RESET);

        if (ib != nullptr)
        {
            const auto* indexBuffer = dynamic_cast<const DiligentIndexBufferBackend*>(ib);
            if (indexBuffer == nullptr || indexBuffer->GetBuffer() == nullptr)
                throw std::runtime_error("CNA Diligent: draw with a foreign or empty index buffer");
            context_->SetIndexBuffer(indexBuffer->GetBuffer(), 0,
                                     Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

            Dg::DrawIndexedAttribs drawAttribs;
            drawAttribs.NumIndices =
                static_cast<Dg::Uint32>(VertexCountForPrimitives(primitive, primitiveCount));
            drawAttribs.IndexType = indexBuffer->IsThirtyTwoBit() ? Dg::VT_UINT32 : Dg::VT_UINT16;
            drawAttribs.Flags = Dg::DRAW_FLAG_VERIFY_ALL;
            drawAttribs.FirstIndexLocation = params != nullptr
                                                 ? static_cast<Dg::Uint32>(std::max(0, params->startIndex))
                                                 : 0;
            drawAttribs.BaseVertex = params != nullptr
                                         ? static_cast<Dg::Uint32>(std::max(0, params->baseVertex))
                                         : 0;
            context_->DrawIndexed(drawAttribs);
            return;
        }

        Dg::DrawAttribs drawAttribs;
        drawAttribs.NumVertices =
            static_cast<Dg::Uint32>(VertexCountForPrimitives(primitive, primitiveCount));
        drawAttribs.Flags = Dg::DRAW_FLAG_VERIFY_ALL;
        drawAttribs.StartVertexLocation = params != nullptr
                                              ? static_cast<Dg::Uint32>(std::max(0, params->vertexStart))
                                              : 0;
        context_->Draw(drawAttribs);
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Diligent::DiligentGraphicsBackend>(args);
    }
}
