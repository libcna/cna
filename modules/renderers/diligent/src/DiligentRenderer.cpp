// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Diligent/DiligentRenderer.hpp"
#include "CNA/Platform/Detail/Sdl3RendererInterop.hpp"
#include "CNA/Internal/Renderers/Diligent/DiligentShaderSources.hpp"

#include "CNA/Logger.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
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

namespace CNA::Internal::Renderers::Diligent
{
    namespace
    {
        constexpr const char* kRendererName = "Diligent";


        // Every built-in shader's HLSL source lives in DiligentShaderSources.hpp (DILIGENT-68).

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

        /// Number of mip levels in a full chain down to 1x1, matching what every CNA renderer and
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

        /// DILIGENT-61: without this, every EngineCreateInfo::Features field defaults to
        /// DEVICE_FEATURE_STATE_DISABLED, which the API contract guarantees is never auto-enabled
        /// regardless of real hardware support -- `SupportsCapability()` reading
        /// `IRenderDevice::GetDeviceInfo().Features` would then honestly, but wrongly, report every
        /// one of these as unsupported on hardware that actually has them. OPTIONAL asks Diligent to
        /// enable each one when (and only when) the device/driver actually supports it, and can
        /// never fail device creation the way ENABLED could.
        void RequestOptionalCapabilityFeatures(Dg::EngineCreateInfo& createInfo)
        {
            createInfo.Features.WireframeFill = Dg::DEVICE_FEATURE_STATE_OPTIONAL;
            createInfo.Features.OcclusionQueries = Dg::DEVICE_FEATURE_STATE_OPTIONAL;
            createInfo.Features.BinaryOcclusionQueries = Dg::DEVICE_FEATURE_STATE_OPTIONAL;
        }

#if defined(_WIN32)
#define CNA_DILIGENT_GLAPI __stdcall
#else
#define CNA_DILIGENT_GLAPI
#endif

        /// Diligent's own OpenGL renderer (`RenderDeviceGLImpl::Initialize()`) unconditionally calls
        /// `glEnable(GL_FRAMEBUFFER_SRGB)` whenever the context reports the *feature* as available
        /// (GL >= 4.0) -- regardless of whether the swap chain's own colour format is sRGB.
        /// `TryCreateDevice()` always requests `TEX_FORMAT_RGBA8_UNORM` (its own comment: "a
        /// non-sRGB back buffer keeps colours numerically identical to what a game wrote, matching
        /// every other CNA renderer"), but the default window framebuffer this project's Mesa/
        /// llvmpipe test environment hands back for a plain `SDL_WINDOW_OPENGL` window is itself
        /// sRGB-capable, so that unconditional enable silently gamma-encodes every write regardless
        /// -- every colour this renderer produced under the OpenGL device type read back visibly
        /// brighter than written (confirmed: readback values consistently matched an sRGB decode of
        /// the expected linear value, e.g. expected 204 decodes to 204/255 -> ^(1/2.2) -> *255 ~= 232,
        /// matching the observed 231 to within rounding, across every mismatching check in
        /// `Diligent_Pbr`/`Diligent_LightingFidelity`). Disabled explicitly right after OpenGL
        /// device/context creation to restore the stated non-sRGB contract; resolved through SDL's
        /// own `SDL_GL_GetProcAddress()` rather than adding a GL loader dependency to this renderer.
        void DisableGLFramebufferSrgb()
        {
            using PFNGLDISABLEPROC = void(CNA_DILIGENT_GLAPI*)(unsigned int);
            constexpr unsigned int kGlFramebufferSrgb = 0x8DB9;
            auto* glDisableFn =
                reinterpret_cast<PFNGLDISABLEPROC>(SDL_GL_GetProcAddress("glDisable"));
            if (glDisableFn != nullptr)
                glDisableFn(kGlFramebufferSrgb);
        }

#undef CNA_DILIGENT_GLAPI
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

    std::int32_t ComputeDiligentDepthBiasRawUnits(float depthBias)
    {
        const double scaled = static_cast<double>(depthBias) * 1000.0;
        const double clamped =
            std::clamp(scaled, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                      static_cast<double>(std::numeric_limits<std::int32_t>::max()));
        return static_cast<std::int32_t>(std::lround(clamped));
    }

    // ---- DiligentTextureRenderer ----

    DiligentTextureRenderer::DiligentTextureRenderer(DiligentRenderer& owner, const ImageData& data)
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

    DiligentTextureRenderer::~DiligentTextureRenderer() = default;

    void DiligentTextureRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
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

    void DiligentTextureRenderer::UpdatePixelsLevel(int level, const std::uint8_t* rgba,
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

    bool DiligentTextureRenderer::GetData(int level, int x, int y, int w, int h,
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

    // ---- DiligentTextureCubeRenderer ----

    DiligentTextureCubeRenderer::DiligentTextureCubeRenderer(DiligentRenderer& owner, int size,
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

    DiligentTextureCubeRenderer::~DiligentTextureCubeRenderer() = default;

    bool DiligentTextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
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

    bool DiligentTextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
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

    // ---- DiligentTexture3DRenderer ----

    DiligentTexture3DRenderer::DiligentTexture3DRenderer(DiligentRenderer& owner, int width,
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

    DiligentTexture3DRenderer::~DiligentTexture3DRenderer() = default;

    bool DiligentTexture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
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

    bool DiligentTexture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
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

    // ---- DiligentRenderTargetRenderer ----

    DiligentRenderTargetRenderer::DiligentRenderTargetRenderer(DiligentRenderer& owner,
                                                              int width, int height, int depthFormat,
                                                              bool preserveContents, bool mipMap,
                                                              int multiSampleCount)
        : owner_(owner)
        , width_(width)
        , height_(height)
        , mipLevels_(mipMap ? MipLevelCount(width, height) : 1)
        , appliedMultiSampleCount_(owner_.ClampSampleCount(
              multiSampleCount, Dg::TEX_FORMAT_RGBA8_UNORM,
              depthFormat != 0 ? Dg::TEX_FORMAT_D24_UNORM_S8_UINT : Dg::TEX_FORMAT_UNKNOWN))
        , preserveContents_(preserveContents)
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::runtime_error("CNA Diligent: render target dimensions must be positive");

        const bool wantsMsaa = appliedMultiSampleCount_ > 1;

        Dg::TextureDesc colorDesc;
        colorDesc.Name = "CNA render target";
        colorDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
        colorDesc.Width = static_cast<Dg::Uint32>(width_);
        colorDesc.Height = static_cast<Dg::Uint32>(height_);
        colorDesc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        colorDesc.Usage = Dg::USAGE_DEFAULT;
        colorDesc.BindFlags = Dg::BIND_RENDER_TARGET;
        if (wantsMsaa)
        {
            // A multisampled texture can't have mip levels and this renderer's shaders sample a
            // plain Texture2D, not a Texture2DMS -- resolveTexture_ below carries the mip chain and
            // BIND_SHADER_RESOURCE instead.
            colorDesc.MipLevels = 1;
            colorDesc.SampleCount = static_cast<Dg::Uint32>(appliedMultiSampleCount_);
        }
        else
        {
            colorDesc.MipLevels = static_cast<Dg::Uint32>(mipLevels_);
            colorDesc.BindFlags |= Dg::BIND_SHADER_RESOURCE;
            if (mipLevels_ > 1)
                colorDesc.MiscFlags = Dg::MISC_TEXTURE_FLAG_GENERATE_MIPS;
        }

        owner_.device_->CreateTexture(colorDesc, nullptr, &colorTexture_);
        if (!colorTexture_)
            throw std::runtime_error("CNA Diligent: render target colour texture creation failed");

        rtv_ = colorTexture_->GetDefaultView(Dg::TEXTURE_VIEW_RENDER_TARGET);
        if (rtv_ == nullptr)
            throw std::runtime_error("CNA Diligent: render target is missing a required view");

        if (wantsMsaa)
        {
            Dg::TextureDesc resolveDesc;
            resolveDesc.Name = "CNA render target resolve";
            resolveDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
            resolveDesc.Width = static_cast<Dg::Uint32>(width_);
            resolveDesc.Height = static_cast<Dg::Uint32>(height_);
            resolveDesc.MipLevels = static_cast<Dg::Uint32>(mipLevels_);
            resolveDesc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
            resolveDesc.Usage = Dg::USAGE_DEFAULT;
            resolveDesc.BindFlags = Dg::BIND_SHADER_RESOURCE;
            if (mipLevels_ > 1)
                resolveDesc.MiscFlags = Dg::MISC_TEXTURE_FLAG_GENERATE_MIPS;

            owner_.device_->CreateTexture(resolveDesc, nullptr, &resolveTexture_);
            if (!resolveTexture_)
                throw std::runtime_error("CNA Diligent: render target resolve texture creation failed");
            srv_ = resolveTexture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        }
        else
        {
            srv_ = colorTexture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        }
        if (srv_ == nullptr)
            throw std::runtime_error("CNA Diligent: render target is missing a required view");

        // DepthFormat::None means the game asked for no depth-stencil storage at all, and
        // HasRealDepthBuffer() below has to be able to say so honestly. Any other value gets the
        // one combined format this renderer uses everywhere (design decision 6).
        if (depthFormat != 0)
        {
            Dg::TextureDesc depthDesc;
            depthDesc.Name = "CNA render target depth-stencil";
            depthDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
            depthDesc.Width = static_cast<Dg::Uint32>(width_);
            depthDesc.Height = static_cast<Dg::Uint32>(height_);
            depthDesc.MipLevels = 1;
            depthDesc.Format = Dg::TEX_FORMAT_D24_UNORM_S8_UINT;
            // The depth-stencil buffer must share the colour target's sample count, or Diligent
            // rejects the pipeline/framebuffer combination as incompatible.
            depthDesc.SampleCount = static_cast<Dg::Uint32>(appliedMultiSampleCount_);
            depthDesc.Usage = Dg::USAGE_DEFAULT;
            depthDesc.BindFlags = Dg::BIND_DEPTH_STENCIL;

            owner_.device_->CreateTexture(depthDesc, nullptr, &depthTexture_);
            if (!depthTexture_)
                throw std::runtime_error("CNA Diligent: render target depth buffer creation failed");
            dsv_ = depthTexture_->GetDefaultView(Dg::TEXTURE_VIEW_DEPTH_STENCIL);
            if (dsv_ == nullptr)
                throw std::runtime_error("CNA Diligent: render target has no depth-stencil view");
        }
    }

    DiligentRenderTargetRenderer::~DiligentRenderTargetRenderer()
    {
        // A target destroyed while still bound must not leave the context pointing at freed views.
        for (const DiligentRenderTargetRenderer* bound : owner_.currentRenderTargets_)
        {
            if (bound == this)
            {
                owner_.SetRenderTarget2D(nullptr);
                break;
            }
        }
    }

    Dg::TEXTURE_FORMAT DiligentRenderTargetRenderer::GetColorFormat() const
    {
        return colorTexture_ ? colorTexture_->GetDesc().Format : Dg::TEX_FORMAT_UNKNOWN;
    }

    Dg::TEXTURE_FORMAT DiligentRenderTargetRenderer::GetDepthStencilFormat() const
    {
        return depthTexture_ ? depthTexture_->GetDesc().Format : Dg::TEX_FORMAT_UNKNOWN;
    }

    bool DiligentRenderTargetRenderer::HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const
    {
        return depthTexture_ != nullptr;
    }

    void DiligentRenderTargetRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
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

        // A multisampled texture can't be the destination of UpdateTexture -- write into the
        // single-sampled resolve texture instead, which is what GetData()/sampling read from too.
        Dg::ITexture* destination = resolveTexture_ ? resolveTexture_.RawPtr() : colorTexture_.RawPtr();
        owner_.context_->UpdateTexture(destination, 0, 0, box, subresource,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    bool DiligentRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        if (level < 0 || level >= mipLevels_)
            return false;
        const int levelW = MipLevelExtent(width_, level);
        const int levelH = MipLevelExtent(height_, level);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelW || y + h > levelH)
            return false;
        if (resolveTexture_)
        {
            // owner_ is a reference member: its constness is independent of this method's, so
            // calling its (non-const) resolve helper here needs no const_cast.
            owner_.ResolveTextureSubresource(colorTexture_, resolveTexture_);
            return owner_.ReadTextureRegion(resolveTexture_, static_cast<Dg::Uint32>(level), 0, x, y,
                                            0, w, h, 1, data, dataLength);
        }
        return owner_.ReadTextureRegion(colorTexture_, static_cast<Dg::Uint32>(level), 0, x, y, 0,
                                        w, h, 1, data, dataLength);
    }

    void DiligentRenderTargetRenderer::BindAsRenderTarget()
    {
        owner_.SetRenderTarget2D(this);
    }

    void DiligentRenderTargetRenderer::UnbindAsRenderTarget()
    {
        if (resolveTexture_)
        {
            // Whatever was just rendered lives only in the multisampled colorTexture_ until this
            // resolves it -- both GenerateMips() below and every later sample/GetData() read
            // resolveTexture_, never colorTexture_ directly.
            owner_.ResolveTextureSubresource(colorTexture_, resolveTexture_);
        }
        if (mipLevels_ > 1 && srv_ != nullptr)
        {
            // Levels 1..n only exist because level 0 was just rendered, so they are regenerated at
            // exactly the point FNA3D regenerates them: when the target stops being the draw target.
            owner_.EnsureRenderTargetsBound();
            owner_.context_->GenerateMips(srv_);
        }
        // Deliberately does NOT call owner_.SetRenderTarget2D(nullptr) here: this method's whole
        // job is "resolve/regenerate whatever WAS just rendered", called by the owning renderer's
        // own SetRenderTarget2D()/SetRenderTargetCubeFace()/SetRenderTargets() while this target is
        // still the bound one (so EnsureRenderTargetsBound() above has something real to bind) --
        // the caller is solely responsible for the actual state swap afterwards.
    }

    // ---- DiligentRenderTargetCubeRenderer ----

    DiligentRenderTargetCubeRenderer::DiligentRenderTargetCubeRenderer(DiligentRenderer& owner,
                                                                      int size, int depthFormat,
                                                                      bool preserveContents,
                                                                      bool mipMap)
        : owner_(owner)
        , size_(size)
        , mipLevels_(mipMap ? MipLevelCount(size, size) : 1)
        , preserveContents_(preserveContents)
    {
        if (size_ <= 0)
            throw std::runtime_error("CNA Diligent: cube render target size must be positive");

        Dg::TextureDesc colorDesc;
        colorDesc.Name = "CNA cube render target";
        colorDesc.Type = Dg::RESOURCE_DIM_TEX_CUBE;
        colorDesc.Width = static_cast<Dg::Uint32>(size_);
        colorDesc.Height = static_cast<Dg::Uint32>(size_);
        colorDesc.ArraySize = 6;
        colorDesc.MipLevels = static_cast<Dg::Uint32>(mipLevels_);
        colorDesc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        colorDesc.Usage = Dg::USAGE_DEFAULT;
        colorDesc.BindFlags = Dg::BIND_RENDER_TARGET | Dg::BIND_SHADER_RESOURCE;
        if (mipLevels_ > 1)
            colorDesc.MiscFlags = Dg::MISC_TEXTURE_FLAG_GENERATE_MIPS;

        owner_.device_->CreateTexture(colorDesc, nullptr, &colorTexture_);
        if (!colorTexture_)
            throw std::runtime_error("CNA Diligent: cube render target colour texture creation failed");

        srv_ = colorTexture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        if (srv_ == nullptr)
            throw std::runtime_error("CNA Diligent: cube render target is missing a shader resource view");

        // A render-target view of one face of a cube texture: Diligent auto-derives
        // RESOURCE_DIM_TEX_2D_ARRAY for TEXTURE_VIEW_RENDER_TARGET on a TEX_CUBE resource when
        // TextureDim is left at its default, so only the array slice needs to be named here.
        for (int face = 0; face < 6; ++face)
        {
            Dg::TextureViewDesc faceViewDesc;
            faceViewDesc.Name = "CNA cube render target face view";
            faceViewDesc.ViewType = Dg::TEXTURE_VIEW_RENDER_TARGET;
            faceViewDesc.FirstArraySlice = static_cast<Dg::Uint32>(face);
            faceViewDesc.NumArraySlices = 1;
            faceViewDesc.MostDetailedMip = 0;
            faceViewDesc.NumMipLevels = 1;
            colorTexture_->CreateView(faceViewDesc, &faceRtv_[face]);
            if (faceRtv_[face] == nullptr)
                throw std::runtime_error("CNA Diligent: cube render target face view creation failed");
        }

        if (depthFormat != 0)
        {
            Dg::TextureDesc depthDesc;
            depthDesc.Name = "CNA cube render target depth-stencil";
            depthDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
            depthDesc.Width = static_cast<Dg::Uint32>(size_);
            depthDesc.Height = static_cast<Dg::Uint32>(size_);
            depthDesc.MipLevels = 1;
            depthDesc.Format = Dg::TEX_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.Usage = Dg::USAGE_DEFAULT;
            depthDesc.BindFlags = Dg::BIND_DEPTH_STENCIL;

            owner_.device_->CreateTexture(depthDesc, nullptr, &depthTexture_);
            if (!depthTexture_)
                throw std::runtime_error(
                    "CNA Diligent: cube render target depth buffer creation failed");
            dsv_ = depthTexture_->GetDefaultView(Dg::TEXTURE_VIEW_DEPTH_STENCIL);
            if (dsv_ == nullptr)
                throw std::runtime_error(
                    "CNA Diligent: cube render target has no depth-stencil view");
        }
    }

    DiligentRenderTargetCubeRenderer::~DiligentRenderTargetCubeRenderer()
    {
        if (owner_.currentCubeTarget_ == this)
            owner_.SetRenderTarget2D(nullptr);
    }

    Dg::TEXTURE_FORMAT DiligentRenderTargetCubeRenderer::GetColorFormat() const
    {
        return colorTexture_ ? colorTexture_->GetDesc().Format : Dg::TEX_FORMAT_UNKNOWN;
    }

    Dg::TEXTURE_FORMAT DiligentRenderTargetCubeRenderer::GetDepthStencilFormat() const
    {
        return depthTexture_ ? depthTexture_->GetDesc().Format : Dg::TEX_FORMAT_UNKNOWN;
    }

    bool DiligentRenderTargetCubeRenderer::HasRealDepthBuffer(bool /*depthFormatWasRequested*/) const
    {
        return depthTexture_ != nullptr;
    }

    bool DiligentRenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                                   void* data, int dataLength) const
    {
        if (face < 0 || face >= 6 || level < 0 || level >= mipLevels_)
            return false;
        const int levelSize = MipLevelExtent(size_, level);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > levelSize || y + h > levelSize)
            return false;
        return owner_.ReadTextureRegion(colorTexture_, static_cast<Dg::Uint32>(level),
                                        static_cast<Dg::Uint32>(face), x, y, 0, w, h, 1,
                                        data, dataLength);
    }

    void DiligentRenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
    {
        owner_.SetRenderTargetCubeFace(this, face);
    }

    void DiligentRenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        if (mipLevels_ > 1 && srv_ != nullptr)
        {
            owner_.EnsureRenderTargetsBound();
            owner_.context_->GenerateMips(srv_);
        }
        // See DiligentRenderTargetRenderer::UnbindAsRenderTarget()'s identical note: the caller owns
        // the actual state swap, not this method.
    }

    // ---- DiligentVertexBufferRenderer ----

    DiligentVertexBufferRenderer::DiligentVertexBufferRenderer(DiligentRenderer& owner,
                                                              int vertexCapacity)
        : owner_(owner)
        , capacity_(std::max(0, vertexCapacity))
    {
    }

    DiligentVertexBufferRenderer::~DiligentVertexBufferRenderer() = default;

    void DiligentVertexBufferRenderer::SetData(const void* data, int vertexCount,
                                               std::size_t strideInBytes)
    {
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions::None);
    }

    void DiligentVertexBufferRenderer::SetDataWithOptions(const void* data, int vertexCount,
                                                          std::size_t strideInBytes,
                                                          SetDataOptions options)
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
            // USAGE_DYNAMIC rather than USAGE_DEFAULT + UpdateBuffer: an upload is a transfer
            // command, and a transfer issued while a render target is bound interrupts the render
            // pass. Found empirically -- DrawUserPrimitives uploads its vertices immediately before
            // each draw, and with UpdateBuffer every such draw into a RenderTarget2D rendered
            // nothing at all, while the same draw to the back buffer worked. A mapped write carries
            // no transfer command and leaves the pass intact.
            desc.Usage = Dg::USAGE_DYNAMIC;
            desc.CPUAccessFlags = Dg::CPU_ACCESS_WRITE;
            owner_.device_->CreateBuffer(desc, nullptr, &buffer_);
            if (!buffer_)
                throw std::runtime_error("CNA Diligent: vertex buffer creation failed");
            allocatedBytes_ = requiredBytes;
        }

        // XNA SetDataOptions -> Diligent MAP_FLAGS: Discard = MAP_FLAG_DISCARD (previous contents
        // undefined, driver may return a fresh region); NoOverwrite = MAP_FLAG_NO_OVERWRITE (caller
        // promises not to touch any range still in flight); None also maps to MAP_FLAG_DISCARD,
        // since XNA's own docs only say None *may* stall, never that it must -- this renderer simply
        // never stalls, matching its D3D11 sibling's identical MapTypeFor() choice.
        const Dg::MAP_FLAGS mapFlags =
            options == SetDataOptions::NoOverwrite ? Dg::MAP_FLAG_NO_OVERWRITE : Dg::MAP_FLAG_DISCARD;
        void* mapped = nullptr;
        owner_.context_->MapBuffer(buffer_, Dg::MAP_WRITE, mapFlags, mapped);
        if (mapped == nullptr)
            throw std::runtime_error("CNA Diligent: vertex buffer could not be mapped");
        std::memcpy(mapped, data, requiredBytes);
        owner_.context_->UnmapBuffer(buffer_, Dg::MAP_WRITE);
    }

    void DiligentVertexBufferRenderer::SetVertexDeclaration(const VertexDeclaration& vertexDeclaration)
    {
        // The built-in shaders are selected by vertex stride (see DiligentRenderer::
        // DrawInternal), which the declaration already carries; genuinely custom element layouts
        // need the custom-shader path that is not part of this baseline.
        //
        // REMED-GFX-DECL-GUARD: the elements are remembered rather than discarded, because
        // "selected by stride" is exactly the mechanism that can misread a declaration whose
        // elements do not sit where the stride's canonical layout puts them. The draw-time guard
        // compares the two and refuses such a draw instead of rendering the wrong bytes.
        stride_ = static_cast<std::size_t>(vertexDeclaration.getVertexStrideProperty());
        declaration_.Remember(vertexDeclaration);
    }

    // ---- DiligentIndexBufferRenderer ----

    DiligentIndexBufferRenderer::DiligentIndexBufferRenderer(DiligentRenderer& owner,
                                                            int indexCapacity, bool thirtyTwoBit)
        : owner_(owner)
        , capacity_(std::max(0, indexCapacity))
        , thirtyTwoBit_(thirtyTwoBit)
    {
    }

    DiligentIndexBufferRenderer::~DiligentIndexBufferRenderer() = default;

    void DiligentIndexBufferRenderer::SetData16(const void* data, int indexCount)
    {
        if (thirtyTwoBit_)
            throw std::runtime_error("CNA Diligent: 16-bit upload into a 32-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint16_t));
    }

    void DiligentIndexBufferRenderer::SetData32(const void* data, int indexCount)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error("CNA Diligent: 32-bit upload into a 16-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint32_t));
    }

    void DiligentIndexBufferRenderer::SetData16WithOptions(const void* data, int indexCount,
                                                           SetDataOptions options)
    {
        if (thirtyTwoBit_)
            throw std::runtime_error("CNA Diligent: 16-bit upload into a 32-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint16_t), options);
    }

    void DiligentIndexBufferRenderer::SetData32WithOptions(const void* data, int indexCount,
                                                           SetDataOptions options)
    {
        if (!thirtyTwoBit_)
            throw std::runtime_error("CNA Diligent: 32-bit upload into a 16-bit index buffer");
        Upload(data, indexCount, sizeof(std::uint32_t), options);
    }

    void DiligentIndexBufferRenderer::Upload(const void* data, int indexCount, std::size_t elementSize,
                                            SetDataOptions options)
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
            // See the vertex buffer's own note for why this is dynamic rather than default.
            desc.Usage = Dg::USAGE_DYNAMIC;
            desc.CPUAccessFlags = Dg::CPU_ACCESS_WRITE;
            owner_.device_->CreateBuffer(desc, nullptr, &buffer_);
            if (!buffer_)
                throw std::runtime_error("CNA Diligent: index buffer creation failed");
            allocatedBytes_ = requiredBytes;
        }

        // See DiligentVertexBufferRenderer::SetDataWithOptions()'s identical MAP_FLAGS mapping.
        const Dg::MAP_FLAGS mapFlags =
            options == SetDataOptions::NoOverwrite ? Dg::MAP_FLAG_NO_OVERWRITE : Dg::MAP_FLAG_DISCARD;
        void* mapped = nullptr;
        owner_.context_->MapBuffer(buffer_, Dg::MAP_WRITE, mapFlags, mapped);
        if (mapped == nullptr)
            throw std::runtime_error("CNA Diligent: index buffer could not be mapped");
        std::memcpy(mapped, data, requiredBytes);
        owner_.context_->UnmapBuffer(buffer_, Dg::MAP_WRITE);
    }

    // ---- DiligentSpriteBatchRenderer ----

    DiligentSpriteBatchRenderer::DiligentSpriteBatchRenderer(DiligentRenderer& owner)
        : owner_(owner)
    {
    }

    DiligentSpriteBatchRenderer::~DiligentSpriteBatchRenderer() = default;

    void DiligentSpriteBatchRenderer::Begin()
    {
        vertices_.clear();
        bufferedSprites_ = 0;
        currentTexture_ = nullptr;
        inBatch_ = true;
    }

    void DiligentSpriteBatchRenderer::End()
    {
        Flush();
        inBatch_ = false;
        hasTransform_ = false;
    }

    void DiligentSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        Flush();
        transform_ = m;
        hasTransform_ = true;
    }

    void DiligentSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        if (textureFilter == filter_)
            return;
        Flush();
        filter_ = textureFilter;
    }

    void DiligentSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU == addressU_ && addressV == addressV_)
            return;
        Flush();
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void DiligentSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destination(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)),
                                    texture.GetWidth(), texture.GetHeight());
        const Rectangle source(0, 0, texture.GetWidth(), texture.GetHeight());
        Draw(texture, destination, source, Color(255, 255, 255, 255), 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void DiligentSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
             SpriteEffects::None, 0.0f);
    }

    void DiligentSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                           const Rectangle& destinationRectangle,
                                           const Rectangle& sourceRectangle,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           float layerDepth)
    {
        const auto* diligentTexture = dynamic_cast<const DiligentSampledTexture*>(&texture);
        if (diligentTexture == nullptr)
            throw std::runtime_error("CNA Diligent: sprite draw with a foreign texture renderer");

        if (currentTexture_ != nullptr && currentTexture_ != diligentTexture)
            Flush();
        currentTexture_ = diligentTexture;
        currentTextureSize_ = {texture.GetWidth(), texture.GetHeight()};

        PushQuad(currentTextureSize_.first, currentTextureSize_.second, destinationRectangle,
                 sourceRectangle, color, rotation, origin, effects, layerDepth);
    }

    void DiligentSpriteBatchRenderer::PushQuad(int textureWidthPixels, int textureHeightPixels,
                                               const Rectangle& destinationRectangle,
                                               const Rectangle& sourceRectangle,
                                               const Color& color,
                                               float rotation,
                                               const Vector2& origin,
                                               SpriteEffects effects,
                                               float layerDepth)
    {
        const float textureWidth = static_cast<float>(std::max(1, textureWidthPixels));
        const float textureHeight = static_cast<float>(std::max(1, textureHeightPixels));

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

    void DiligentSpriteBatchRenderer::EnsureCapacity(std::size_t spriteCount)
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

    void DiligentSpriteBatchRenderer::Flush()
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

    // ---- DiligentOcclusionQueryRenderer ----

    DiligentOcclusionQueryRenderer::DiligentOcclusionQueryRenderer(DiligentRenderer* owner)
        : owner_(owner)
    {
        const auto& features = owner_->device_->GetDeviceInfo().Features;
        Dg::QueryDesc desc;
        if (features.OcclusionQueries == Dg::DEVICE_FEATURE_STATE_ENABLED)
        {
            desc.Type = Dg::QUERY_TYPE_OCCLUSION;
        }
        else if (features.BinaryOcclusionQueries == Dg::DEVICE_FEATURE_STATE_ENABLED)
        {
            desc.Type = Dg::QUERY_TYPE_BINARY_OCCLUSION;
            binaryOnly_ = true;
        }
        else
        {
            throw std::runtime_error(
                "CNA Diligent: this device supports neither exact nor binary occlusion queries");
        }

        owner_->device_->CreateQuery(desc, &query_);
        if (!query_)
            throw std::runtime_error("CNA Diligent: failed to create an occlusion query");
    }

    void DiligentOcclusionQueryRenderer::Begin()
    {
        owner_->context_->BeginQuery(query_);
        ended_ = false;
    }

    void DiligentOcclusionQueryRenderer::End()
    {
        owner_->context_->EndQuery(query_);
        // Diligent's immediate context batches commands until some internal limit, Present(), or an
        // explicit Flush() -- without one of those, a query polled right after End() (rather than
        // after a frame boundary) would never leave VK_NOT_READY. Every draw call here already
        // rebinds the pipeline state and shader resources unconditionally (Flush()'s own
        // documented requirement), so flushing mid-frame is safe.
        owner_->context_->Flush();
        ended_ = true;
    }

    bool DiligentOcclusionQueryRenderer::IsComplete() const
    {
        // GetData() is only legal once the query has reached Diligent's own "Ended" state -- never
        // before the first End(), and no longer true after a GetData() call already auto-invalidated
        // it (see PixelCount()). Calling it outside that window is a DEV_CHECK abort, not a "not
        // ready yet" result, so ended_ has to gate the call rather than just its return value.
        if (!ended_)
            return false;
        return query_->GetData(nullptr, 0, false);
    }

    int DiligentOcclusionQueryRenderer::PixelCount() const
    {
        if (!ended_)
            return pixelCount_;

        bool ready = false;
        if (binaryOnly_)
        {
            Dg::QueryDataBinaryOcclusion data;
            ready = query_->GetData(&data, sizeof(data), true);
            if (ready)
                pixelCount_ = data.AnySamplePassed ? 1 : 0;
        }
        else
        {
            Dg::QueryDataOcclusion data;
            ready = query_->GetData(&data, sizeof(data), true);
            if (ready)
                pixelCount_ = static_cast<int>(
                    std::min<Dg::Uint64>(data.NumSamples, static_cast<Dg::Uint64>(INT32_MAX)));
        }
        // AutoInvalidate=true only actually invalidates the query once its data was retrieved, so
        // ended_ must follow that, not the Begin()/End() pair alone -- a further call here before
        // the next Begin() would otherwise hit the same DEV_CHECK IsComplete() guards against.
        if (ready)
            ended_ = false;
        return pixelCount_;
    }

    // ---- DiligentRenderer: pipeline key ----

    bool DiligentRenderer::PipelineKey::operator==(const PipelineKey& other) const noexcept
    {
        return variant == other.variant && topology == other.topology && blend == other.blend &&
               blendFuncs == other.blendFuncs && writeMask == other.writeMask &&
               depth == other.depth && stencilFront == other.stencilFront &&
               stencilBack == other.stencilBack && stencilMasks == other.stencilMasks &&
               raster == other.raster && targetFormats == other.targetFormats &&
               extraTargetFormats == other.extraTargetFormats && sampleCount == other.sampleCount &&
               scissorEnable == other.scissorEnable && depthBias == other.depthBias &&
               slopeScaledDepthBias == other.slopeScaledDepthBias && sampleMask == other.sampleMask &&
               instancedVertexStride == other.instancedVertexStride &&
               instancedInstanceStride == other.instancedInstanceStride &&
               instancedStepRate == other.instancedStepRate;
    }

    std::size_t DiligentRenderer::PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(key.variant);
        std::uint32_t slopeBits = 0;
        std::memcpy(&slopeBits, &key.slopeScaledDepthBias, sizeof(slopeBits));
        const std::uint32_t fields[] = {key.topology, key.blend, key.blendFuncs, key.writeMask,
                                        key.depth, key.stencilFront, key.stencilBack,
                                        key.stencilMasks, key.raster, key.targetFormats,
                                        key.extraTargetFormats, key.sampleCount, key.scissorEnable,
                                        static_cast<std::uint32_t>(key.depthBias), slopeBits,
                                        key.sampleMask, key.instancedVertexStride,
                                        key.instancedInstanceStride, key.instancedStepRate};
        for (const std::uint32_t field : fields)
            hash = hash * 1099511628211ull ^ static_cast<std::size_t>(field);
        return hash;
    }

    // ---- DiligentRenderer ----

    DiligentRenderer::DiligentRenderer(const GraphicsRendererCreateArgs& args)
        : window_(CNA::Platform::Detail::ResolveSdl3RendererWindow(args.surface.windowId))
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
        CreateFallbackTexture();
        CreateFallbackFlatNormalTexture();

        maxTextureDimension_ = static_cast<int>(device_->GetAdapterInfo().Texture.MaxTexture2DDimension);
        if (maxTextureDimension_ <= 0)
            maxTextureDimension_ = 16384;

        sampleCount_ = ClampSampleCount(args.multiSampleCount, swapChain_->GetDesc().ColorBufferFormat,
                                        swapChain_->GetDesc().DepthBufferFormat);
        if (sampleCount_ > 1)
        {
            RecreateMsaaBackBufferTargets();
            CNA::Logger::Info(
                "CNA Diligent: MSAA " + std::to_string(sampleCount_) + "x", CNA::LogCategory::GPU);
        }

        // The alpha-blended, depth-tested defaults every other CNA renderer starts from.
        state_.blend = PackBytes(4, 5, 4, 5);
        state_.blendFuncs = PackBytes(0, 0, 1, 0);
        state_.writeMask = 0xFFFF;
        state_.depth = PackBytes(1, 1, 3, 0);
        state_.stencilMasks = PackBytes(0xFF, 0xFF, 0, 0);
        state_.raster = PackBytes(0, 0, 0, 0);

        IGraphicsRenderer::RegisterForWindow(SDL_GetWindowID(window_), this);
        CNA::Logger::Info(std::string("CNA Diligent: device type ") + GetDeviceTypeName(deviceType_),
                          CNA::LogCategory::GPU);
    }

    DiligentRenderer::~DiligentRenderer()
    {
        if (window_ != nullptr)
            IGraphicsRenderer::UnregisterForWindow(SDL_GetWindowID(window_));
        if (context_)
        {
            context_->Flush();
            context_->WaitForIdle();
        }
        pipelines_.clear();
        samplers_.clear();
        // Every Diligent object is explicitly released here, in the body, rather than left to the
        // members' own implicit destruction (which only runs AFTER this body finishes, in reverse
        // declaration order): on the OpenGL device type, releasing one of these issues real GL
        // delete calls, which need glContext_ to still be current. Destroying glContext_ itself has
        // to be the last thing this destructor does, once nothing Diligent-owned can touch GL again.
        msaaBackBufferColor_.Release();
        msaaBackBufferDepth_.Release();
        constantBuffer_.Release();
        boneBuffer_.Release();
        pbrBuffer_.Release();
        fallbackTexture_.Release();
        flatNormalTexture_.Release();
        swapChain_.Release();
        context_.Release();
        device_.Release();
        engineFactory_.Release();
        if (glContext_ != nullptr)
        {
            SDL_GL_MakeCurrent(window_, nullptr);
            SDL_GL_DestroyContext(reinterpret_cast<SDL_GLContext>(glContext_));
            glContext_ = nullptr;
        }
    }

    void DiligentRenderer::CreateDeviceAndSwapChain(const GraphicsRendererCreateArgs& args)
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
            // A failed OpenGL attempt may have gotten as far as creating (and making current) a
            // real SDL GL context before CreateDeviceAndSwapChainGL() itself threw -- release it
            // too, or it leaks and stays wrongly current for whatever candidate tries next.
            if (glContext_ != nullptr)
            {
                SDL_GL_MakeCurrent(window_, nullptr);
                SDL_GL_DestroyContext(reinterpret_cast<SDL_GLContext>(glContext_));
                glContext_ = nullptr;
            }
        }

        throw std::runtime_error("CNA Diligent: no device type could be created -- tried " + failures);
    }

    bool DiligentRenderer::TryCreateDevice(DiligentDeviceType type, int /*multiSampleCount*/)
    {
        Dg::SwapChainDesc swapChainDesc;
        swapChainDesc.Width = static_cast<Dg::Uint32>(std::max(1, physicalWidth_));
        swapChainDesc.Height = static_cast<Dg::Uint32>(std::max(1, physicalHeight_));
        // A non-sRGB back buffer keeps colours numerically identical to what a game wrote, matching
        // every other CNA renderer; Diligent's own default is the sRGB variant.
        swapChainDesc.ColorBufferFormat = Dg::TEX_FORMAT_RGBA8_UNORM;
        // XNA's DepthFormat family tops out at Depth24Stencil8 and CNA's stencil support needs the
        // stencil half, so a combined format is used regardless of the requested DepthFormat.
        swapChainDesc.DepthBufferFormat = Dg::TEX_FORMAT_D24_UNORM_S8_UINT;
        // DILIGENT-63: ReadBackbuffer()/GetBackBufferData() copy the swap chain's colour image into
        // a staging texture; without COPY_SOURCE, Vulkan creates present images lacking
        // VK_IMAGE_USAGE_TRANSFER_SRC_BIT and validation flags every such copy as a real usage
        // violation, not just a warning.
        swapChainDesc.Usage = Dg::SWAP_CHAIN_USAGE_RENDER_TARGET | Dg::SWAP_CHAIN_USAGE_COPY_SOURCE;

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
                RequestOptionalCapabilityFeatures(createInfo);
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
                // Unlike Vulkan/D3D, Diligent's own GLContext (GLContextLinux.cpp) does not create a
                // GL context itself -- it asserts one is already current on this thread
                // (glXGetCurrentContext()) and attaches to it. SDL owns creating and making it
                // current; this has to happen before CreateDeviceAndSwapChainGL() even attempts
                // anything.
                SDL_GLContext sdlGlContext = SDL_GL_CreateContext(window_);
                if (sdlGlContext == nullptr)
                    throw std::runtime_error(std::string("SDL_GL_CreateContext failed: ") + SDL_GetError());
                if (!SDL_GL_MakeCurrent(window_, sdlGlContext))
                {
                    const std::string message = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
                    SDL_GL_DestroyContext(sdlGlContext);
                    throw std::runtime_error(message);
                }
                glContext_ = reinterpret_cast<SDL_GLContextState*>(sdlGlContext);

                auto* factory = Dg::GetEngineFactoryOpenGL();
                Dg::EngineGLCreateInfo createInfo;
                createInfo.Window = nativeWindow;
                // XNA projection matrices produce Direct3D-style [0,1] clip depth, so the GL
                // device is asked for the same range instead of GL's default [-1,1].
                createInfo.ZeroToOneNDZ = true;
                RequestOptionalCapabilityFeatures(createInfo);
                factory->CreateDeviceAndSwapChainGL(createInfo, &device_, &context_, swapChainDesc,
                                                    &swapChain_);
                engineFactory_ = Dg::RefCntAutoPtr<Dg::IEngineFactory>(factory);
                if (device_ && context_)
                    DisableGLFramebufferSrgb();
                break;
            }
#endif
#if CNA_DILIGENT_HAS_D3D11
            case DiligentDeviceType::D3D11:
            {
                auto* factory = Dg::GetEngineFactoryD3D11();
                Dg::EngineD3D11CreateInfo createInfo;
                RequestOptionalCapabilityFeatures(createInfo);
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
                RequestOptionalCapabilityFeatures(createInfo);
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

    void DiligentRenderer::CreateConstantBuffer()
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

        Dg::BufferDesc boneDesc;
        boneDesc.Name = "CNA bone transforms";
        boneDesc.Size = sizeof(GpuDrawParams{}.boneTransforms);
        boneDesc.BindFlags = Dg::BIND_UNIFORM_BUFFER;
        boneDesc.Usage = Dg::USAGE_DYNAMIC;
        boneDesc.CPUAccessFlags = Dg::CPU_ACCESS_WRITE;
        device_->CreateBuffer(boneDesc, nullptr, &boneBuffer_);
        if (!boneBuffer_)
            throw std::runtime_error("CNA Diligent: bone buffer creation failed");

        Dg::BufferDesc pbrDesc;
        pbrDesc.Name = "CNA PBR constants";
        pbrDesc.Size = 2 * sizeof(float) * 4; // g_PbrAmbientMetallic, g_PbrEmissiveRoughness
        pbrDesc.BindFlags = Dg::BIND_UNIFORM_BUFFER;
        pbrDesc.Usage = Dg::USAGE_DYNAMIC;
        pbrDesc.CPUAccessFlags = Dg::CPU_ACCESS_WRITE;
        device_->CreateBuffer(pbrDesc, nullptr, &pbrBuffer_);
        if (!pbrBuffer_)
            throw std::runtime_error("CNA Diligent: PBR constant buffer creation failed");
    }

    void DiligentRenderer::UploadBoneTransforms(const GpuDrawParams& params)
    {
        void* mapped = nullptr;
        context_->MapBuffer(boneBuffer_, Dg::MAP_WRITE, Dg::MAP_FLAG_DISCARD, mapped);
        if (mapped == nullptr)
            throw std::runtime_error("CNA Diligent: bone buffer could not be mapped");
        std::memcpy(mapped, params.boneTransforms, sizeof(params.boneTransforms));
        context_->UnmapBuffer(boneBuffer_, Dg::MAP_WRITE);
    }

    void DiligentRenderer::UploadPbrConstants(const GpuDrawParams& params)
    {
        const float values[8] = {
            params.ambientColor[0], params.ambientColor[1], params.ambientColor[2],
            params.pbrMetallicFactor,
            params.emissiveColor[0], params.emissiveColor[1], params.emissiveColor[2],
            params.pbrRoughnessFactor,
        };
        void* mapped = nullptr;
        context_->MapBuffer(pbrBuffer_, Dg::MAP_WRITE, Dg::MAP_FLAG_DISCARD, mapped);
        if (mapped == nullptr)
            throw std::runtime_error("CNA Diligent: PBR constant buffer could not be mapped");
        std::memcpy(mapped, values, sizeof(values));
        context_->UnmapBuffer(pbrBuffer_, Dg::MAP_WRITE);
    }

    void DiligentRenderer::CreateFallbackTexture()
    {
        Dg::TextureDesc desc;
        desc.Name = "CNA fallback white texture";
        desc.Type = Dg::RESOURCE_DIM_TEX_2D;
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = Dg::USAGE_IMMUTABLE;
        desc.BindFlags = Dg::BIND_SHADER_RESOURCE;

        constexpr std::uint8_t kWhite[4] = {255, 255, 255, 255};
        Dg::TextureSubResData level0{};
        level0.pData = kWhite;
        level0.Stride = 4;
        Dg::TextureData initialData{&level0, 1};

        device_->CreateTexture(desc, &initialData, &fallbackTexture_);
        if (!fallbackTexture_)
            throw std::runtime_error("CNA Diligent: fallback texture creation failed");
        fallbackTextureView_ = fallbackTexture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        if (fallbackTextureView_ == nullptr)
            throw std::runtime_error("CNA Diligent: fallback texture has no shader resource view");
    }

    void DiligentRenderer::CreateFallbackFlatNormalTexture()
    {
        Dg::TextureDesc desc;
        desc.Name = "CNA fallback flat-normal texture";
        desc.Type = Dg::RESOURCE_DIM_TEX_2D;
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.Format = Dg::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = Dg::USAGE_IMMUTABLE;
        desc.BindFlags = Dg::BIND_SHADER_RESOURCE;

        constexpr std::uint8_t kFlatNormal[4] = {128, 128, 255, 255};
        Dg::TextureSubResData level0{};
        level0.pData = kFlatNormal;
        level0.Stride = 4;
        Dg::TextureData initialData{&level0, 1};

        device_->CreateTexture(desc, &initialData, &flatNormalTexture_);
        if (!flatNormalTexture_)
            throw std::runtime_error("CNA Diligent: fallback flat-normal texture creation failed");
        flatNormalTextureView_ = flatNormalTexture_->GetDefaultView(Dg::TEXTURE_VIEW_SHADER_RESOURCE);
        if (flatNormalTextureView_ == nullptr)
            throw std::runtime_error(
                "CNA Diligent: fallback flat-normal texture has no shader resource view");
    }

    int DiligentRenderer::GetMultiSampleCount() const
    {
        return sampleCount_ > 1 ? sampleCount_ : 0;
    }

    int DiligentRenderer::ClampSampleCount(int requested, Dg::TEXTURE_FORMAT colorFormat,
                                                  Dg::TEXTURE_FORMAT depthFormat) const
    {
        if (requested <= 1 || !device_)
            return 1;

        const auto& colorInfo = device_->GetTextureFormatInfoExt(colorFormat);
        Dg::Uint32 supported = static_cast<Dg::Uint32>(colorInfo.SampleCounts);
        if (depthFormat != Dg::TEX_FORMAT_UNKNOWN)
        {
            const auto& depthInfo = device_->GetTextureFormatInfoExt(depthFormat);
            supported &= static_cast<Dg::Uint32>(depthInfo.SampleCounts);
        }

        // Largest supported power-of-two sample count not exceeding the request, matching every
        // other CNA renderer's own MSAA clamp (e.g. VulkanRenderer's PickSampleCount()).
        for (const int candidate : {64, 32, 16, 8, 4, 2})
        {
            if (candidate <= requested && (supported & static_cast<Dg::Uint32>(candidate)) != 0)
                return candidate;
        }
        return 1;
    }

    int DiligentRenderer::CurrentSampleCount() const
    {
        if (currentCubeTarget_ != nullptr)
            return 1; // cube render target MSAA is not implemented yet
        if (DiligentRenderTargetRenderer* rt = PrimaryRenderTarget())
            return rt->GetDiligentSampleCount();
        return sampleCount_;
    }

    void DiligentRenderer::RecreateMsaaBackBufferTargets()
    {
        msaaBackBufferColor_.Release();
        msaaBackBufferDepth_.Release();
        msaaBackBufferColorView_ = nullptr;
        msaaBackBufferDepthView_ = nullptr;
        if (sampleCount_ <= 1 || physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return;

        const auto& scDesc = swapChain_->GetDesc();

        Dg::TextureDesc colorDesc;
        colorDesc.Name = "CNA MSAA back buffer colour";
        colorDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
        colorDesc.Width = static_cast<Dg::Uint32>(physicalWidth_);
        colorDesc.Height = static_cast<Dg::Uint32>(physicalHeight_);
        colorDesc.MipLevels = 1;
        colorDesc.Format = scDesc.ColorBufferFormat;
        colorDesc.SampleCount = static_cast<Dg::Uint32>(sampleCount_);
        colorDesc.Usage = Dg::USAGE_DEFAULT;
        colorDesc.BindFlags = Dg::BIND_RENDER_TARGET;
        device_->CreateTexture(colorDesc, nullptr, &msaaBackBufferColor_);
        if (!msaaBackBufferColor_)
            throw std::runtime_error("CNA Diligent: MSAA back buffer colour texture creation failed");
        msaaBackBufferColorView_ = msaaBackBufferColor_->GetDefaultView(Dg::TEXTURE_VIEW_RENDER_TARGET);

        if (scDesc.DepthBufferFormat != Dg::TEX_FORMAT_UNKNOWN)
        {
            Dg::TextureDesc depthDesc;
            depthDesc.Name = "CNA MSAA back buffer depth-stencil";
            depthDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
            depthDesc.Width = static_cast<Dg::Uint32>(physicalWidth_);
            depthDesc.Height = static_cast<Dg::Uint32>(physicalHeight_);
            depthDesc.MipLevels = 1;
            depthDesc.Format = scDesc.DepthBufferFormat;
            depthDesc.SampleCount = static_cast<Dg::Uint32>(sampleCount_);
            depthDesc.Usage = Dg::USAGE_DEFAULT;
            depthDesc.BindFlags = Dg::BIND_DEPTH_STENCIL;
            device_->CreateTexture(depthDesc, nullptr, &msaaBackBufferDepth_);
            if (!msaaBackBufferDepth_)
                throw std::runtime_error("CNA Diligent: MSAA back buffer depth texture creation failed");
            msaaBackBufferDepthView_ = msaaBackBufferDepth_->GetDefaultView(Dg::TEXTURE_VIEW_DEPTH_STENCIL);
        }
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::ResolveTextureSubresource(Dg::ITexture* src, Dg::ITexture* dst)
    {
        Dg::ResolveTextureSubresourceAttribs attribs;
        attribs.SrcTextureTransitionMode = Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        attribs.DstTextureTransitionMode = Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context_->ResolveTextureSubresource(src, dst, attribs);
    }

    void DiligentRenderer::ResolveMsaaBackBufferIfNeeded()
    {
        if (sampleCount_ <= 1 || !msaaBackBufferColor_)
            return;
        Dg::ITextureView* backBufferView = swapChain_->GetCurrentBackBufferRTV();
        if (backBufferView == nullptr)
            return;
        ResolveTextureSubresource(msaaBackBufferColor_, backBufferView->GetTexture());
    }

    int DiligentRenderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        const int clamped = ClampSampleCount(requestedMultiSampleCount,
                                             swapChain_->GetDesc().ColorBufferFormat,
                                             swapChain_->GetDesc().DepthBufferFormat);
        if (clamped != sampleCount_)
        {
            sampleCount_ = clamped;
            RecreateMsaaBackBufferTargets();
            CNA::Logger::Info(
                "CNA Diligent: back buffer MultiSampleCount reset to " + std::to_string(sampleCount_) + "x",
                CNA::LogCategory::GPU);
        }
        return GetMultiSampleCount();
    }

    void DiligentRenderer::SyncSwapChainSize()
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
        // The offscreen MSAA back buffer is sized to match; a resize invalidates it exactly the
        // same way it invalidates the swap chain's own images.
        RecreateMsaaBackBufferTargets();
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::EnsureRenderTargetsBound()
    {
        if (renderTargetsBound_)
            return;

        if (currentCubeTarget_ != nullptr)
        {
            Dg::ITextureView* faceView = currentCubeTarget_->GetFaceRenderTargetView(currentCubeFace_);
            context_->SetRenderTargets(1, &faceView, currentCubeTarget_->GetDepthStencilView(),
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        else if (currentRenderTargets_.empty())
        {
            if (sampleCount_ > 1 && msaaBackBufferColorView_ != nullptr)
            {
                context_->SetRenderTargets(1, &msaaBackBufferColorView_, msaaBackBufferDepthView_,
                                           Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
            else
            {
                Dg::ITextureView* backBuffer = swapChain_->GetCurrentBackBufferRTV();
                context_->SetRenderTargets(1, &backBuffer, swapChain_->GetDepthBufferDSV(),
                                           Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            }
        }
        else
        {
            // Slot order is the caller's binding order; the depth-stencil buffer always comes from
            // slot 0, which is also the target whose size drives the viewport.
            std::vector<Dg::ITextureView*> views;
            views.reserve(currentRenderTargets_.size());
            for (DiligentRenderTargetRenderer* target : currentRenderTargets_)
                views.push_back(target->GetRenderTargetView());
            context_->SetRenderTargets(static_cast<Dg::Uint32>(views.size()), views.data(),
                                       currentRenderTargets_.front()->GetDepthStencilView(),
                                       Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        renderTargetsBound_ = true;
        ApplyViewportAndScissor();
    }

    Dg::ITextureView* DiligentRenderer::GetCurrentRenderTargetView() const
    {
        if (currentCubeTarget_ != nullptr)
            return currentCubeTarget_->GetFaceRenderTargetView(currentCubeFace_);
        if (PrimaryRenderTarget() != nullptr)
            return PrimaryRenderTarget()->GetRenderTargetView();
        if (sampleCount_ > 1 && msaaBackBufferColorView_ != nullptr)
            return msaaBackBufferColorView_;
        return swapChain_->GetCurrentBackBufferRTV();
    }

    Dg::ITextureView* DiligentRenderer::GetCurrentDepthStencilView() const
    {
        if (currentCubeTarget_ != nullptr)
            return currentCubeTarget_->GetDepthStencilView();
        if (PrimaryRenderTarget() != nullptr)
            return PrimaryRenderTarget()->GetDepthStencilView();
        if (sampleCount_ > 1 && msaaBackBufferColorView_ != nullptr)
            return msaaBackBufferDepthView_;
        return swapChain_->GetDepthBufferDSV();
    }

    DiligentRenderer::LogicalViewport DiligentRenderer::ComputeLogicalViewport() const
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

    void DiligentRenderer::ApplyViewportAndScissor()
    {
        if (currentCubeTarget_ != nullptr || PrimaryRenderTarget() != nullptr)
        {
            // A render target has no window to letterbox into: its own pixels are the coordinate
            // space, so the presentation-mode scaling that applies to the back buffer must not.
            const auto targetWidth = static_cast<Dg::Uint32>(
                currentCubeTarget_ != nullptr ? currentCubeTarget_->GetSize()
                                              : PrimaryRenderTarget()->GetWidth());
            const auto targetHeight = static_cast<Dg::Uint32>(
                currentCubeTarget_ != nullptr ? currentCubeTarget_->GetSize()
                                              : PrimaryRenderTarget()->GetHeight());

            Dg::Viewport targetViewport;
            targetViewport.TopLeftX = customViewport_ ? static_cast<float>(viewportRect_[0]) : 0.0f;
            targetViewport.TopLeftY = customViewport_ ? static_cast<float>(viewportRect_[1]) : 0.0f;
            targetViewport.Width = customViewport_ ? static_cast<float>(viewportRect_[2])
                                                   : static_cast<float>(targetWidth);
            targetViewport.Height = customViewport_ ? static_cast<float>(viewportRect_[3])
                                                    : static_cast<float>(targetHeight);
            targetViewport.MinDepth = customViewport_ ? viewportDepth_[0] : 0.0f;
            targetViewport.MaxDepth = customViewport_ ? viewportDepth_[1] : 1.0f;
            context_->SetViewports(1, &targetViewport, targetWidth, targetHeight);

            if (scissorEnabled_)
            {
                Dg::Rect scissor;
                scissor.left = scissorRect_[0];
                scissor.top = scissorRect_[1];
                scissor.right = scissorRect_[0] + scissorRect_[2];
                scissor.bottom = scissorRect_[1] + scissorRect_[3];
                context_->SetScissorRects(1, &scissor, targetWidth, targetHeight);
            }
            return;
        }

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

    void DiligentRenderer::Clear(float r, float g, float b, float a)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        const float clearColor[] = {r, g, b, a};
        if (currentCubeTarget_ != nullptr)
        {
            context_->ClearRenderTarget(currentCubeTarget_->GetFaceRenderTargetView(currentCubeFace_),
                                        clearColor, Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            return;
        }
        if (currentRenderTargets_.empty())
        {
            Dg::ITextureView* target = (sampleCount_ > 1 && msaaBackBufferColorView_ != nullptr)
                                           ? msaaBackBufferColorView_
                                           : swapChain_->GetCurrentBackBufferRTV();
            context_->ClearRenderTarget(target, clearColor,
                                        Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            return;
        }
        // XNA clears every bound target, not only slot 0.
        for (DiligentRenderTargetRenderer* target : currentRenderTargets_)
            context_->ClearRenderTarget(target->GetRenderTargetView(), clearColor,
                                        Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentRenderer::ClearDepth(float depth)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        if (Dg::ITextureView* depthStencil = GetCurrentDepthStencilView())
            context_->ClearDepthStencil(depthStencil, Dg::CLEAR_DEPTH_FLAG, depth, 0,
                                        Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentRenderer::ClearStencil(int stencil)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        if (Dg::ITextureView* depthStencil = GetCurrentDepthStencilView())
            context_->ClearDepthStencil(depthStencil, Dg::CLEAR_STENCIL_FLAG, 1.0f,
                                        static_cast<Dg::Uint8>(stencil & 0xFF),
                                        Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void DiligentRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        SyncSwapChainSize();
        EnsureRenderTargetsBound();
        if (Dg::ITextureView* depthStencil = GetCurrentDepthStencilView())
            context_->ClearDepthStencil(depthStencil,
                                        Dg::CLEAR_DEPTH_FLAG | Dg::CLEAR_STENCIL_FLAG, depth,
                                        static_cast<Dg::Uint8>(stencil & 0xFF),
                                        Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        ClearStencil(stencil);
    }

    void DiligentRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a,
                                                             float depth, int stencil)
    {
        Clear(r, g, b, a);
        ClearDepthAndStencil(depth, stencil);
    }

    void DiligentRenderer::Present()
    {
        SyncSwapChainSize();
        ResolveMsaaBackBufferIfNeeded();
        swapChain_->Present(static_cast<Dg::Uint32>(std::max(0, swapInterval_)));
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::GetViewportSize(int& width, int& height)
    {
        SyncSwapChainSize();
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void DiligentRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA Diligent: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::SetSwapInterval(int interval)
    {
        swapInterval_ = std::clamp(interval, 0, 2);
    }

    bool DiligentRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                            float& logX, float& logY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            return false;
        logX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return true;
    }

    bool DiligentRenderer::TransformLogicalToWindow(float logX, float logY,
                                                            float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f)
            return false;
        windowX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureRenderer> DiligentRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<DiligentTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> DiligentRenderer::CreateSpriteBatch()
    {
        return std::make_unique<DiligentSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<ITextureCubeRenderer> DiligentRenderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<DiligentTextureCubeRenderer>(*this, size, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITexture3DRenderer> DiligentRenderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<DiligentTexture3DRenderer>(*this, w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetRenderer> DiligentRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        // DiligentRenderTargetRenderer's own constructor clamps this to what the device actually
        // supports (DILIGENT-25) and IRenderTargetRenderer::GetMultiSampleCount() reports the real
        // applied value, matching FNA's own RenderTarget2D.MultiSampleCount semantics.
        return std::make_unique<DiligentRenderTargetRenderer>(*this, w, h, depthFormat,
                                                             preserveContents, mipMap,
                                                             multiSampleCount);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> DiligentRenderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        if (multiSampleCount > 1)
        {
            CNA::Logger::Warn(
                "CNA Diligent: multisampled cube render targets are not implemented; creating a "
                "single-sampled target instead",
                CNA::LogCategory::GPU);
        }
        return std::make_unique<DiligentRenderTargetCubeRenderer>(*this, size, depthFormat,
                                                                 preserveContents, mipMap);
    }

    void DiligentRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        auto* target = static_cast<DiligentRenderTargetRenderer*>(rt);
        if (currentCubeTarget_ == nullptr && currentRenderTargets_.size() == (target != nullptr ? 1u : 0u) &&
            (target == nullptr || currentRenderTargets_[0] == target))
            return;

        // Resolve/regenerate mips for whatever WAS bound before switching away from it -- must
        // happen while it is still the active render target (EnsureRenderTargetsBound() inside
        // UnbindAsRenderTarget() reads currentCubeTarget_/currentRenderTargets_, still unchanged
        // here) and before this method's own state below replaces them.
        if (currentCubeTarget_ != nullptr)
            currentCubeTarget_->UnbindAsRenderTarget();
        for (DiligentRenderTargetRenderer* previous : currentRenderTargets_)
        {
            if (previous != target)
                previous->UnbindAsRenderTarget();
        }

        currentCubeTarget_ = nullptr;
        currentCubeFace_ = -1;
        currentRenderTargets_.clear();
        if (target != nullptr)
            currentRenderTargets_.push_back(target);
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        auto* target = static_cast<DiligentRenderTargetCubeRenderer*>(rt);
        if (target == nullptr)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (currentCubeTarget_ == target && currentCubeFace_ == face)
            return;

        // Only a genuine cube-object switch resolves/mip-regenerates -- switching faces on the
        // SAME cube does not, since GenerateMips() covers the whole cube resource (all faces) from
        // its base level, not just the face that was just drawn into, so regenerating on every
        // face switch would be redundant work repeated up to five extra times per frame.
        if (currentCubeTarget_ != nullptr && currentCubeTarget_ != target)
            currentCubeTarget_->UnbindAsRenderTarget();
        for (DiligentRenderTargetRenderer* previous : currentRenderTargets_)
            previous->UnbindAsRenderTarget();

        currentRenderTargets_.clear();
        currentCubeTarget_ = target;
        currentCubeFace_ = face;
        renderTargetsBound_ = false;
    }

    DiligentRenderTargetRenderer* DiligentRenderer::PrimaryRenderTarget() const
    {
        return currentRenderTargets_.empty() ? nullptr : currentRenderTargets_.front();
    }

    std::unique_ptr<IVertexBufferRenderer> DiligentRenderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<DiligentVertexBufferRenderer>(*this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> DiligentRenderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<DiligentIndexBufferRenderer>(*this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> DiligentRenderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<DiligentIndexBufferRenderer>(*this, index_capacity, true);
    }

    Dg::TEXTURE_FORMAT DiligentRenderer::CurrentColorFormat() const
    {
        if (currentCubeTarget_ != nullptr)
            return currentCubeTarget_->GetColorFormat();
        return PrimaryRenderTarget() != nullptr ? PrimaryRenderTarget()->GetColorFormat()
                                               : swapChain_->GetDesc().ColorBufferFormat;
    }

    Dg::TEXTURE_FORMAT DiligentRenderer::CurrentDepthStencilFormat() const
    {
        if (currentCubeTarget_ != nullptr)
            return currentCubeTarget_->GetDepthStencilFormat();
        return PrimaryRenderTarget() != nullptr ? PrimaryRenderTarget()->GetDepthStencilFormat()
                                               : swapChain_->GetDesc().DepthBufferFormat;
    }

    Dg::ITextureView* DiligentRenderer::GetBackBufferTextureView() const
    {
        return swapChain_->GetCurrentBackBufferRTV();
    }

    void DiligentRenderer::ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels)
    {
        if (pixels == nullptr || w <= 0 || h <= 0)
            throw std::runtime_error("CNA Diligent: ReadBackbuffer requires a positive region");

        // A read can happen mid-frame, before Present() would otherwise resolve the MSAA target --
        // GetBackBufferData() must see what was actually drawn either way.
        ResolveMsaaBackBufferIfNeeded();

        Dg::ITextureView* backBufferView = GetBackBufferTextureView();
        if (backBufferView == nullptr)
            throw std::runtime_error("CNA Diligent: the swap chain exposes no back buffer view");
        Dg::ITexture* backBuffer = backBufferView->GetTexture();
        const auto& backBufferDesc = backBuffer->GetDesc();
        const auto backBufferWidth = static_cast<int>(backBufferDesc.Width);
        const auto backBufferHeight = static_cast<int>(backBufferDesc.Height);

        // The caller works in logical game coordinates; the back buffer is physical. This is the
        // FULL region the caller's logical request maps to -- it may extend outside the real back
        // buffer (Overscan's negative origin, or simply the edge of the presentation area).
        const LogicalViewport viewport = ComputeLogicalViewport();
        const float scaleX = viewport.logicalWidth > 0.0f ? viewport.width / viewport.logicalWidth : 1.0f;
        const float scaleY = viewport.logicalHeight > 0.0f ? viewport.height / viewport.logicalHeight : 1.0f;
        const int requestedX = static_cast<int>(std::lround(viewport.x + x * scaleX));
        const int requestedY = static_cast<int>(std::lround(viewport.y + y * scaleY));
        const int requestedW = std::max(1, static_cast<int>(std::lround(w * scaleX)));
        const int requestedH = std::max(1, static_cast<int>(std::lround(h * scaleY)));

        // DILIGENT-63: intersect with the real back buffer extent instead of trusting the request.
        // The old code clamped MinX/MinY to 0 but left MaxX/MaxY as `clampedMin + physicalW/H`,
        // which SHIFTS the copied region rather than clipping it whenever requestedX/Y were
        // negative (Overscan) -- reading the wrong content, not just too much of it. It also never
        // clamped against the back buffer's own width/height at all, risking an out-of-bounds
        // source box at the right/bottom edge.
        const int copyX0 = std::clamp(requestedX, 0, backBufferWidth);
        const int copyY0 = std::clamp(requestedY, 0, backBufferHeight);
        const int copyX1 = std::clamp(requestedX + requestedW, 0, backBufferWidth);
        const int copyY1 = std::clamp(requestedY + requestedH, 0, backBufferHeight);
        const int copyW = std::max(0, copyX1 - copyX0);
        const int copyH = std::max(0, copyY1 - copyY0);

        const bool blueFirst = IsBlueFirstFormat(backBufferDesc.Format);

        if (copyW <= 0 || copyH <= 0)
        {
            // The entire requested region is outside the real back buffer -- every destination
            // pixel is uncovered. Zero-fill and return without creating a staging texture or
            // issuing a copy against an empty/invalid box at all.
            std::memset(pixels, 0, static_cast<std::size_t>(w) * h * 4);
            return;
        }

        Dg::TextureDesc stagingDesc;
        stagingDesc.Name = "CNA backbuffer readback";
        stagingDesc.Type = Dg::RESOURCE_DIM_TEX_2D;
        stagingDesc.Width = static_cast<Dg::Uint32>(copyW);
        stagingDesc.Height = static_cast<Dg::Uint32>(copyH);
        stagingDesc.MipLevels = 1;
        stagingDesc.Format = backBufferDesc.Format;
        stagingDesc.Usage = Dg::USAGE_STAGING;
        stagingDesc.BindFlags = Dg::BIND_NONE;
        stagingDesc.CPUAccessFlags = Dg::CPU_ACCESS_READ;

        Dg::RefCntAutoPtr<Dg::ITexture> staging;
        device_->CreateTexture(stagingDesc, nullptr, &staging);
        if (!staging)
            throw std::runtime_error("CNA Diligent: readback staging texture creation failed");

        // DILIGENT-66: DiligentCore's GL renderer copies the swap chain's own default framebuffer
        // (Texture2D_GL::CopyTexSubimage -> glCopyTexSubImage2D) with the source Y taken straight
        // from Box.MinY/MaxY, but the DEFAULT framebuffer's Y axis is GL's own native bottom-up
        // convention (row 0 = bottom of the window) -- unlike every other texture read in this
        // renderer, which are ordinary allocated textures and therefore self-consistently top-down
        // (their own upload and this renderer's own staging-texture readback both use the same
        // convention, so nothing but the swap chain's own default-framebuffer source needs this).
        // Flipping the requested source rows here restores the top-down contract every other
        // device type already provides.
        Dg::Box sourceBox;
        sourceBox.MinX = static_cast<Dg::Uint32>(copyX0);
        sourceBox.MaxX = static_cast<Dg::Uint32>(copyX1);
        if (deviceType_ == DiligentDeviceType::OpenGL)
        {
            sourceBox.MinY = static_cast<Dg::Uint32>(backBufferHeight - copyY1);
            sourceBox.MaxY = static_cast<Dg::Uint32>(backBufferHeight - copyY0);
        }
        else
        {
            sourceBox.MinY = static_cast<Dg::Uint32>(copyY0);
            sourceBox.MaxY = static_cast<Dg::Uint32>(copyY1);
        }

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

        // DiligentCore v2.5.6's OpenGL renderer has a real bug (DILIGENT-30): DeviceContextGLImpl::
        // Flush() resets its whole SRB-binding bookkeeping (m_BindInfo = {}, including ActiveSRBMask)
        // but DeviceContextGLImpl::SetPipelineState() early-outs on "same pipeline object as before"
        // (IsSameObject()) WITHOUT recomputing ActiveSRBMask. Every sprite/3D draw here reuses one
        // cached pipeline object per ShaderVariant, so the very next draw after this Flush() (unless
        // it happens to pick a different ShaderVariant) sets the *same* pipeline object again, hits
        // that early-out, and is left with ActiveSRBMask == 0 forever after -- GetCommitMask() then
        // always returns 0, so BindProgramResources() (the call that actually issues glBindTexture)
        // is silently skipped on every following draw, leaving whatever GL texture unit content was
        // last actually bound. InvalidateState() forces the next SetPipelineState() call to take the
        // real (non-early-out) path again, correctly recomputing ActiveSRBMask; renderTargetsBound_
        // is already false above, so the next draw's own EnsureRenderTargetsBound() re-establishes
        // the render target before drawing, avoiding the "framebuffer without attachments" assert an
        // earlier attempt at this hit from calling InvalidateState() without that guarantee in place.
        if (deviceType_ == DiligentDeviceType::OpenGL)
            context_->InvalidateState();

        // MAP_FLAG_DO_NOT_WAIT is the correct flag *because* of the WaitForIdle() above: Diligent's
        // Vulkan renderer never synchronizes staging reads itself and asks callers to state that
        // they have already done so.
        Dg::MappedTextureSubresource mapped{};
        context_->MapTextureSubresource(staging, 0, 0, Dg::MAP_READ, Dg::MAP_FLAG_DO_NOT_WAIT,
                                        nullptr, mapped);
        if (mapped.pData == nullptr)
            throw std::runtime_error("CNA Diligent: readback staging texture could not be mapped");

        // Resample the physical region back to the caller's logical region: with letterboxing or a
        // scaled presentation the two differ, and the caller asked in logical pixels. Each logical
        // pixel's position is first found in the FULL (unclamped) requested region, then translated
        // into the staging texture's own local coordinates (the copied/intersected box) -- a
        // position that falls outside it was clipped away above and is zero-filled instead of read.
        const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
        for (int row = 0; row < h; ++row)
        {
            const int fullSourceRow =
                requestedY + std::clamp(static_cast<int>(row * scaleY), 0, requestedH - 1);
            const int stagingRowTopDown = fullSourceRow - copyY0;
            // The Y-flipped source box above means glCopyTexSubImage2D copied GL-bottom-up rows
            // in ascending order, landing the LOGICALLY-BOTTOM row of the copied box at staging
            // row 0 -- un-reverse that here so stagingRow keeps meaning "row 0 = top", matching
            // every non-GL device type.
            const int stagingRow = (deviceType_ == DiligentDeviceType::OpenGL)
                ? (copyH - 1 - stagingRowTopDown)
                : stagingRowTopDown;
            for (int column = 0; column < w; ++column)
            {
                const int fullSourceColumn =
                    requestedX + std::clamp(static_cast<int>(column * scaleX), 0, requestedW - 1);
                const int stagingColumn = fullSourceColumn - copyX0;
                std::uint8_t* destination =
                    pixels + (static_cast<std::size_t>(row) * w + column) * 4;
                if (stagingRow < 0 || stagingRow >= copyH || stagingColumn < 0 || stagingColumn >= copyW)
                {
                    destination[0] = 0;
                    destination[1] = 0;
                    destination[2] = 0;
                    destination[3] = 0;
                    continue;
                }
                const std::uint8_t* texel =
                    source + static_cast<std::size_t>(stagingRow) * mapped.Stride + stagingColumn * 4;
                destination[0] = blueFirst ? texel[2] : texel[0];
                destination[1] = texel[1];
                destination[2] = blueFirst ? texel[0] : texel[2];
                destination[3] = texel[3];
            }
        }
        context_->UnmapTextureSubresource(staging, 0, 0);
    }

    void DiligentRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                                                    int count)
    {
        if (renderTargets == nullptr || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            EnsureRenderTargetsBound();
            return;
        }
        if (count == 1 && renderTargets[0].IsRenderTargetCubeFace())
        {
            SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(), renderTargets[0].GetCubeFace());
            EnsureRenderTargetsBound();
            return;
        }
        // Each refusal below names the one thing this renderer cannot do, instead of quietly
        // binding slot 0 and dropping the rest -- which would render a scene that looks right
        // until something reads the targets that were never written.
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "CNA Diligent: mixing a cube-map face into a multi-target set is not "
                    "implemented on this renderer");
        }
        // Resolve/regenerate mips for whatever WAS bound before this reconfiguration -- see
        // SetRenderTarget2D()'s identical note.
        if (currentCubeTarget_ != nullptr)
            currentCubeTarget_->UnbindAsRenderTarget();
        for (DiligentRenderTargetRenderer* previous : currentRenderTargets_)
            previous->UnbindAsRenderTarget();

        currentRenderTargets_.clear();
        currentCubeTarget_ = nullptr;
        currentCubeFace_ = -1;
        for (int i = 0; i < count; ++i)
        {
            auto* target = static_cast<DiligentRenderTargetRenderer*>(renderTargets[i].GetRenderTarget2D());
            if (target == nullptr)
                throw std::runtime_error(
                    "CNA Diligent: a render-target slot holds a target this renderer never created");
            currentRenderTargets_.push_back(target);
        }
        renderTargetsBound_ = false;
        EnsureRenderTargetsBound();
    }

    bool EvaluateCapability(CNA::GraphicsCapability capability, const Dg::DeviceFeatures& features,
                            Dg::Uint8 maxAnisotropy, bool multiSampleSupported)
    {
        switch (capability)
        {
            // Every CNA draw here always has vertex/index buffers, 3D pipelines and a real
            // D24_UNORM_S8_UINT depth-stencil buffer (design decision 6) regardless of device type
            // -- these three are structural to this renderer, not device-variable.
            case CNA::GraphicsCapability::ThreeD:
            case CNA::GraphicsCapability::DepthStencilBuffer:
            case CNA::GraphicsCapability::Texture3D:
            case CNA::GraphicsCapability::AdditiveBlending:
                return true;
            case CNA::GraphicsCapability::WireFrame:
                return features.WireframeFill == Dg::DEVICE_FEATURE_STATE_ENABLED;
            case CNA::GraphicsCapability::AnisotropicFiltering:
                return maxAnisotropy > 1;
            // DILIGENT_MAX_RENDER_TARGETS is a fixed compile-time 8 on every Diligent device type,
            // well above the 4 slots GetOrCreatePipeline() itself clamps to (DILIGENT-24) -- there
            // is no per-device MRT limit to probe here.
            case CNA::GraphicsCapability::MultipleRenderTargets:
                return true;
            case CNA::GraphicsCapability::OcclusionQuery:
                return features.OcclusionQueries == Dg::DEVICE_FEATURE_STATE_ENABLED ||
                      features.BinaryOcclusionQueries == Dg::DEVICE_FEATURE_STATE_ENABLED;
            case CNA::GraphicsCapability::MultiSampleAntiAliasing:
                return multiSampleSupported;
            case CNA::GraphicsCapability::CustomEffects:
                return false;
            // REMED-GFX-201/202: this renderer binds exactly one per-vertex stream (its input
            // layout and shader variant are both selected from that one buffer's byte stride) and
            // exactly one per-instance stream. A declaration split across several buffers, or a
            // second per-instance binding, would need element re-slotting this renderer does not
            // do -- so it answers false and both draw routes refuse the shape outright rather
            // than rendering from a subset of what the caller bound.
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                return false;
            // DILIGENT-43/DILIGENT-65: real hardware instancing -- a per-instance vertex buffer at
            // slot 1 with a genuine INPUT_ELEMENT_FREQUENCY_PER_INSTANCE step rate, drawn through
            // IDeviceContext::DrawIndexed with NumInstances. Structural to the renderer rather than
            // device-variable: every Diligent device type CNA can select supports instanced draws.
            case CNA::GraphicsCapability::Instancing:
                return true;
            case CNA::GraphicsCapability::StencilBuffer:
                return true;
        }
        // No default arm: every GraphicsCapability member is answered above, so adding one to the
        // enum breaks this build instead of silently returning a wrong answer here.
        return false;
    }

    bool DiligentRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // DILIGENT-61: device-type guesses and unconditional `true`s replaced with the actual
        // facts CreateOcclusionQuery()/ApplyRasterizerState()/ApplySamplerState()/ClampSampleCount()
        // themselves already consult (or, for MultiSampleAntiAliasing, the same helper those use),
        // so a `true` here can never be followed by the corresponding operation throwing.
        //
        // Probing with the largest candidate ClampSampleCount() itself ever considers (64) --
        // rather than the smallest non-1 value (2) -- matters: ClampSampleCount() only returns a
        // candidate <= the requested value, so probing with 2 alone reports "unsupported" on a
        // real device that supports 4x/8x but happens not to support 2x specifically (found live:
        // this renderer's own lavapipe verification device is exactly such a case).
        return EvaluateCapability(capability, device_->GetDeviceInfo().Features,
                                  device_->GetAdapterInfo().Sampler.MaxAnisotropy,
                                  ClampSampleCount(64, Dg::TEX_FORMAT_RGBA8_UNORM) > 1);
    }

    std::unique_ptr<IOcclusionQueryRenderer> DiligentRenderer::CreateOcclusionQuery()
    {
        return std::make_unique<DiligentOcclusionQueryRenderer>(this);
    }

    void DiligentRenderer::SetStringMarkerEXT(const char* marker)
    {
        if (marker == nullptr || marker[0] == '\0')
            return;
        context_->InsertDebugLabel(marker);
    }

    void DiligentRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                   int colorDstBlend, int alphaDstBlend,
                                                   int colorBlendFunc, int alphaBlendFunc,
                                                   const BlendWriteState& writeState)
    {
        const bool blendEnabled = !(colorSrcBlend == 0 && colorDstBlend == 1 &&
                                    alphaSrcBlend == 0 && alphaDstBlend == 1);
        state_.blend = PackBytes(colorSrcBlend, colorDstBlend, alphaSrcBlend, alphaDstBlend);
        state_.blendFuncs = PackBytes(colorBlendFunc, alphaBlendFunc, blendEnabled ? 1 : 0, 0);
        // All four slot masks are carried into the pipeline state (DILIGENT-24 made slots 1..3
        // bindable), but only slot 0's is observable today: every built-in shader here declares a
        // single SV_TARGET, so slots 1..3 receive clears and no fragments until a multi-output
        // custom ShaderEffect exists (DILIGENT-42).
        state_.writeMask = 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            state_.writeMask |= static_cast<std::uint32_t>(writeState.colorWriteChannels[slot] & 0xF)
                                << (slot * 4);
        }
        state_.sampleMask = writeState.multiSampleMask;
    }

    void DiligentRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
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

    void DiligentRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                        bool scissorTestEnable,
                                                        float depthBias, float slopeScaleDepthBias)
    {
        state_.raster = PackBytes(cullMode, fillMode, 0, 0);
        state_.depthBias = ComputeDiligentDepthBiasRawUnits(depthBias);
        state_.slopeScaledDepthBias = slopeScaleDepthBias;
        scissorEnabled_ = scissorTestEnable;
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                     int maxAnisotropy)
    {
        if (slot < 0 || slot >= 16)
            return;
        SamplerSlotState& state = samplerSlots_[slot];
        state.filter = filter;
        state.addressU = addressU;
        state.addressV = addressV;
        state.maxAnisotropy = std::clamp(maxAnisotropy, 1, 16);
    }

    void DiligentRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_[0] = r;
        blendFactor_[1] = g;
        blendFactor_[2] = b;
        blendFactor_[3] = a;
    }

    void DiligentRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value;
    }

    void DiligentRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorRect_[0] = x;
        scissorRect_[1] = y;
        scissorRect_[2] = w;
        scissorRect_[3] = h;
        renderTargetsBound_ = false;
    }

    void DiligentRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
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

    void DiligentRenderer::SetDepthTestEnabled(bool enabled)
    {
        const std::uint32_t depth = state_.depth;
        state_.depth = (depth & 0xFFFFFF00u) | (enabled ? 1u : 0u);
    }

    void DiligentRenderer::SetDepthWriteEnabled(bool enabled)
    {
        const std::uint32_t depth = state_.depth;
        state_.depth = (depth & 0xFFFF00FFu) | (enabled ? 0x100u : 0u);
    }

    void DiligentRenderer::SetBlendEnabled(bool enabled)
    {
        const std::uint32_t funcs = state_.blendFuncs;
        state_.blendFuncs = (funcs & 0xFF00FFFFu) | (enabled ? 0x10000u : 0u);
    }

    Dg::ISampler* DiligentRenderer::GetOrCreateSampler(int filter, int addressU, int addressV,
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

    DiligentRenderer::PipelineKey DiligentRenderer::MakePipelineKey(
        ShaderVariant variant, PrimitiveType primitive, std::uint32_t instancedVertexStride,
        std::uint32_t instancedInstanceStride, std::uint32_t instancedStepRate) const
    {
        PipelineKey key = state_;
        key.variant = variant;
        key.topology = static_cast<std::uint32_t>(ToTopology(primitive));
        key.instancedVertexStride = instancedVertexStride;
        key.instancedInstanceStride = instancedInstanceStride;
        key.instancedStepRate = instancedStepRate;
        key.targetFormats = static_cast<std::uint32_t>(CurrentColorFormat()) |
                            (static_cast<std::uint32_t>(CurrentDepthStencilFormat()) << 16);

        const auto targetCount = static_cast<std::uint32_t>(
            std::max<std::size_t>(1, currentRenderTargets_.size()));
        std::uint32_t extra = targetCount << 24;
        for (std::size_t slot = 1; slot < currentRenderTargets_.size() && slot < 4; ++slot)
        {
            extra |= static_cast<std::uint32_t>(currentRenderTargets_[slot]->GetColorFormat())
                     << ((slot - 1) * 8);
        }
        key.extraTargetFormats = extra;
        key.sampleCount = static_cast<std::uint32_t>(CurrentSampleCount());
        key.scissorEnable = scissorEnabled_ ? 1u : 0u;
        return key;
    }

    DiligentRenderer::CachedPipeline& DiligentRenderer::GetOrCreatePipeline(
        const PipelineKey& key)
    {
        if (const auto it = pipelines_.find(key); it != pipelines_.end())
            return it->second;

        const char* vertexSource = nullptr;
        const char* pixelSource = nullptr;
        std::vector<Dg::LayoutElement> layout;
        bool usesTexture = false;
        bool usesSecondTexture = false;
        bool usesEnvironmentMap = false;
        bool usesBones = false;
        bool usesPbr = false;

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
            case ShaderVariant::DualTexture3D:
                vertexSource = kDualTextureVertexHlsl;
                pixelSource = kDualTexturePixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                usesSecondTexture = true;
                break;
            case ShaderVariant::DualTextureColored3D:
                vertexSource = kDualTextureColoredVertexHlsl;
                pixelSource = kDualTexturePixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 4, Dg::VT_UINT8, Dg::True},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                usesSecondTexture = true;
                break;
            case ShaderVariant::Skinned3D:
                vertexSource = kSkinnedVertexHlsl;
                pixelSource = kLitPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{3, 0, 4, Dg::VT_FLOAT32, Dg::False},
                    // Bone indices are indices, not a normalized colour: they must reach the
                    // shader as the integers they are.
                    Dg::LayoutElement{4, 0, 4, Dg::VT_UINT8, Dg::False},
                };
                usesTexture = true;
                usesBones = true;
                break;
            case ShaderVariant::EnvironmentMap3D:
                vertexSource = kLitVertexHlsl;
                pixelSource = kEnvironmentMapPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                usesEnvironmentMap = true;
                break;
            case ShaderVariant::Instanced3D:
            {
                vertexSource = kInstancedVertexHlsl;
                pixelSource = kInstancedPixelHlsl;
                // Both strides come from the real buffers bound for this specific draw
                // (DrawInstancedPrimitivesEx() validates and passes them into the pipeline key) --
                // never a literal or LAYOUT_ELEMENT_AUTO_STRIDE guess. Diligent pipelines are
                // immutable, so a caller with a different real stride gets its own distinct cached
                // pipeline instead of silently misfetching through one built for someone else's
                // layout (DILIGENT-65).
                const auto vertexStride = static_cast<Dg::Uint32>(key.instancedVertexStride);
                const auto instanceStride = static_cast<Dg::Uint32>(key.instancedInstanceStride);
                // REMED-GFX-202: the binding's own InstanceFrequency, not an assumed 1. Diligent
                // maps this to D3D11's InstanceDataStepRate / a glVertexAttribDivisor.
                const auto stepRate = static_cast<Dg::Uint32>(std::max<std::uint32_t>(1, key.instancedStepRate));
                layout = {
                    // Slot 0: per-vertex Position only, out of whatever real stream the caller
                    // bound (a plain 12-byte position buffer, a full VertexPositionColor stream,
                    // or anything else at least 12 bytes with Position first).
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False, Dg::LAYOUT_ELEMENT_AUTO_OFFSET,
                                      vertexStride},
                    // Slot 1: one 4x4 world matrix per instance, as four consecutive float4 rows,
                    // advancing once per instance rather than once per vertex.
                    Dg::LayoutElement{1, 1, 4, Dg::VT_FLOAT32, Dg::False, Dg::LAYOUT_ELEMENT_AUTO_OFFSET,
                                      instanceStride, Dg::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,
                                      stepRate},
                    Dg::LayoutElement{2, 1, 4, Dg::VT_FLOAT32, Dg::False, Dg::LAYOUT_ELEMENT_AUTO_OFFSET,
                                      instanceStride, Dg::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,
                                      stepRate},
                    Dg::LayoutElement{3, 1, 4, Dg::VT_FLOAT32, Dg::False, Dg::LAYOUT_ELEMENT_AUTO_OFFSET,
                                      instanceStride, Dg::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,
                                      stepRate},
                    Dg::LayoutElement{4, 1, 4, Dg::VT_FLOAT32, Dg::False, Dg::LAYOUT_ELEMENT_AUTO_OFFSET,
                                      instanceStride, Dg::INPUT_ELEMENT_FREQUENCY_PER_INSTANCE,
                                      stepRate},
                };
                break;
            }
            case ShaderVariant::LitTexturedVertexLit3D:
                vertexSource = kLitVertexLitVertexHlsl;
                pixelSource = kLitVertexLitPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                };
                usesTexture = true;
                break;
            case ShaderVariant::SkinnedVertexLit3D:
                vertexSource = kSkinnedVertexLitVertexHlsl;
                pixelSource = kLitVertexLitPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{2, 0, 2, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{3, 0, 4, Dg::VT_FLOAT32, Dg::False},
                    Dg::LayoutElement{4, 0, 4, Dg::VT_UINT8, Dg::False},
                };
                usesTexture = true;
                usesBones = true;
                break;
            case ShaderVariant::Pbr3D:
                vertexSource = kPbrVertexHlsl;
                pixelSource = kPbrPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False}, // Position
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False}, // Normal
                    Dg::LayoutElement{2, 0, 4, Dg::VT_FLOAT32, Dg::False}, // Tangent (xyz + sign)
                    Dg::LayoutElement{3, 0, 2, Dg::VT_FLOAT32, Dg::False}, // UV
                };
                usesTexture = true;
                usesPbr = true;
                break;
            case ShaderVariant::SkinnedPbr3D:
                vertexSource = kSkinnedPbrVertexHlsl;
                pixelSource = kPbrPixelHlsl;
                layout = {
                    Dg::LayoutElement{0, 0, 3, Dg::VT_FLOAT32, Dg::False}, // Position
                    Dg::LayoutElement{1, 0, 3, Dg::VT_FLOAT32, Dg::False}, // Normal
                    Dg::LayoutElement{2, 0, 4, Dg::VT_FLOAT32, Dg::False}, // Tangent (xyz + sign)
                    Dg::LayoutElement{3, 0, 2, Dg::VT_FLOAT32, Dg::False}, // UV
                    Dg::LayoutElement{4, 0, 4, Dg::VT_FLOAT32, Dg::False}, // BlendWeights
                    // Bone indices are indices, not a normalized colour: they must reach the
                    // shader as the integers they are.
                    Dg::LayoutElement{5, 0, 4, Dg::VT_UINT8, Dg::False},   // BlendIndices
                };
                usesTexture = true;
                usesPbr = true;
                usesBones = true;
                break;
        }

        const std::string vertexHlsl = std::string(kConstantsHlsl) + kVertexLightingHlsl +
            (usesBones ? kBonesHlsl : "") + vertexSource;
        const std::string pixelHlsl = std::string(kConstantsHlsl) + kPixelHelpersHlsl + pixelSource;

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
        const auto targetCount = std::clamp<Dg::Uint8>(
            static_cast<Dg::Uint8>((key.extraTargetFormats >> 24) & 0xFF), 1, 4);
        graphicsPipeline.NumRenderTargets = targetCount;
        graphicsPipeline.RTVFormats[0] = static_cast<Dg::TEXTURE_FORMAT>(key.targetFormats & 0xFFFF);
        for (Dg::Uint8 slot = 1; slot < targetCount; ++slot)
        {
            graphicsPipeline.RTVFormats[slot] = static_cast<Dg::TEXTURE_FORMAT>(
                (key.extraTargetFormats >> ((slot - 1) * 8)) & 0xFF);
        }
        graphicsPipeline.DSVFormat = static_cast<Dg::TEXTURE_FORMAT>((key.targetFormats >> 16) & 0xFFFF);
        // Must match the sample count of whatever framebuffer this pipeline is ever bound against,
        // or Vulkan rejects it as incompatible with the render pass -- see PipelineKey::sampleCount.
        graphicsPipeline.SmplDesc.Count = static_cast<Dg::Uint8>(std::max<std::uint32_t>(1, key.sampleCount));
        graphicsPipeline.SampleMask = key.sampleMask;
        graphicsPipeline.PrimitiveTopology = static_cast<Dg::PRIMITIVE_TOPOLOGY>(key.topology);
        graphicsPipeline.InputLayout.LayoutElements = layout.data();
        graphicsPipeline.InputLayout.NumElements = static_cast<Dg::Uint32>(layout.size());

        graphicsPipeline.BlendDesc.IndependentBlendEnable = targetCount > 1 ? Dg::True : Dg::False;
        for (Dg::Uint8 slot = 0; slot < targetCount; ++slot)
        {
            auto& blend = graphicsPipeline.BlendDesc.RenderTargets[slot];
            blend.BlendEnable = ((key.blendFuncs >> 16) & 0xFF) != 0 ? Dg::True : Dg::False;
            blend.SrcBlend = ToBlendFactor(static_cast<int>(key.blend & 0xFF));
            blend.DestBlend = ToBlendFactor(static_cast<int>((key.blend >> 8) & 0xFF));
            blend.SrcBlendAlpha = ToBlendFactor(static_cast<int>((key.blend >> 16) & 0xFF));
            blend.DestBlendAlpha = ToBlendFactor(static_cast<int>((key.blend >> 24) & 0xFF));
            blend.BlendOp = ToBlendOperation(static_cast<int>(key.blendFuncs & 0xFF));
            blend.BlendOpAlpha = ToBlendOperation(static_cast<int>((key.blendFuncs >> 8) & 0xFF));

            const int slotMask = static_cast<int>((key.writeMask >> (slot * 4)) & 0xF);
            auto colorMask = Dg::COLOR_MASK_NONE;
            if (ColorWriteHasRed(slotMask))   colorMask |= Dg::COLOR_MASK_RED;
            if (ColorWriteHasGreen(slotMask)) colorMask |= Dg::COLOR_MASK_GREEN;
            if (ColorWriteHasBlue(slotMask))  colorMask |= Dg::COLOR_MASK_BLUE;
            if (ColorWriteHasAlpha(slotMask)) colorMask |= Dg::COLOR_MASK_ALPHA;
            blend.RenderTargetWriteMask = colorMask;
        }

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
        rasterizer.ScissorEnable = key.scissorEnable != 0 ? Dg::True : Dg::False;
        rasterizer.DepthBias = static_cast<Dg::Int32>(key.depthBias);
        rasterizer.SlopeScaledDepthBias = key.slopeScaledDepthBias;

        psoCI.pVS = vertexShader;
        psoCI.pPS = pixelShader;

        Dg::ShaderResourceVariableDesc variables[7];
        Dg::Uint32 variableCount = 0;
        if (usesTexture)
        {
            variables[variableCount++] =
                Dg::ShaderResourceVariableDesc{Dg::SHADER_TYPE_PIXEL, "g_Texture",
                                               Dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
        }
        if (usesSecondTexture)
        {
            variables[variableCount++] =
                Dg::ShaderResourceVariableDesc{Dg::SHADER_TYPE_PIXEL, "g_Texture2",
                                               Dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
        }
        if (usesEnvironmentMap)
        {
            variables[variableCount++] =
                Dg::ShaderResourceVariableDesc{Dg::SHADER_TYPE_PIXEL, "g_EnvMap",
                                               Dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
        }
        if (usesPbr)
        {
            for (const char* name :
                 {"g_NormalMap", "g_MetallicRoughnessMap", "g_EmissiveMap", "g_OcclusionMap"})
            {
                variables[variableCount++] = Dg::ShaderResourceVariableDesc{
                    Dg::SHADER_TYPE_PIXEL, name, Dg::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC};
            }
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
            if (auto* variable = cached.pipeline->GetStaticVariableByName(stage, "Bones"))
                variable->Set(boneBuffer_);
            if (auto* variable = cached.pipeline->GetStaticVariableByName(stage, "PbrConstants"))
                variable->Set(pbrBuffer_);
        }

        cached.pipeline->CreateShaderResourceBinding(&cached.binding, true);
        if (!cached.binding)
            throw std::runtime_error("CNA Diligent: shader resource binding creation failed");
        if (usesTexture)
            cached.textureVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_Texture");
        if (usesSecondTexture)
            cached.texture2Variable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_Texture2");
        if (usesEnvironmentMap)
            cached.envMapVariable = cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_EnvMap");
        if (usesPbr)
        {
            cached.normalMapVariable =
                cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_NormalMap");
            cached.metallicRoughnessVariable =
                cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_MetallicRoughnessMap");
            cached.emissiveMapVariable =
                cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_EmissiveMap");
            cached.occlusionMapVariable =
                cached.binding->GetVariableByName(Dg::SHADER_TYPE_PIXEL, "g_OcclusionMap");
        }

        return pipelines_.emplace(key, std::move(cached)).first->second;
    }

    bool DiligentRenderer::ReadTextureRegion(Dg::ITexture* texture, Dg::Uint32 mipLevel,
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

        // See ReadBackbuffer's identical InvalidateState() call for why this is needed (DILIGENT-30):
        // Flush() wipes DeviceContextGLImpl's SRB-binding bookkeeping (ActiveSRBMask in particular),
        // but SetPipelineState() silently skips recomputing it when the next draw reuses the same
        // pipeline object, permanently disabling resource rebinding from that point on. Unlike
        // ReadBackbuffer, this function does not unbind render targets first, so renderTargetsBound_
        // must be explicitly cleared here too -- InvalidateState() wipes Diligent's own FBO/render-
        // target tracking regardless of what that CNA-side shadow flag still says, and skipping this
        // would make EnsureRenderTargetsBound()'s cheap early-out skip re-establishing it before the
        // next draw.
        if (deviceType_ == DiligentDeviceType::OpenGL)
        {
            context_->InvalidateState();
            renderTargetsBound_ = false;
        }

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

    void DiligentRenderer::UploadConstants(const ShaderConstants& constants)
    {
        void* mapped = nullptr;
        context_->MapBuffer(constantBuffer_, Dg::MAP_WRITE, Dg::MAP_FLAG_DISCARD, mapped);
        if (mapped == nullptr)
            throw std::runtime_error("CNA Diligent: constant buffer could not be mapped");
        std::memcpy(mapped, &constants, sizeof(ShaderConstants));
        context_->UnmapBuffer(constantBuffer_, Dg::MAP_WRITE);
    }

    void DiligentRenderer::DrawSpriteQuads(Dg::IBuffer* vertexBuffer, Dg::IBuffer* indexBuffer,
                                                   std::size_t spriteCount,
                                                   const DiligentSampledTexture& texture,
                                                   const Matrix* transform, int filter,
                                                   int addressU, int addressV)
    {
        if (spriteCount == 0)
            return;

        SyncSwapChainSize();
        EnsureRenderTargetsBound();

        // A bound render target is its own coordinate space (XNA sets the viewport to the target's
        // size when one is bound), so the sprite projection must span the target, not the window's
        // logical canvas -- otherwise every sprite lands at back-buffer scale inside a
        // differently-sized target.
        float surfaceWidth;
        float surfaceHeight;
        if (currentCubeTarget_ != nullptr)
        {
            surfaceWidth = static_cast<float>(currentCubeTarget_->GetSize());
            surfaceHeight = surfaceWidth;
        }
        else if (PrimaryRenderTarget() != nullptr)
        {
            surfaceWidth = static_cast<float>(PrimaryRenderTarget()->GetWidth());
            surfaceHeight = static_cast<float>(PrimaryRenderTarget()->GetHeight());
        }
        else
        {
            const LogicalViewport logical = ComputeLogicalViewport();
            surfaceWidth = logical.logicalWidth;
            surfaceHeight = logical.logicalHeight;
        }
        Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, std::max(1.0f, surfaceWidth), std::max(1.0f, surfaceHeight), 0.0f, 0.0f, 1.0f);
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
        constants.alphaTest[2] = 1.0f;
        constants.alphaTest[3] = 1.0f;
        UploadConstants(constants);

        // Sprites go through the sampler the SpriteBatch selected, which is independent of the
        // GraphicsDevice-level SamplerState the 3D path uses (only MaxAnisotropy has no per-batch
        // override, so slot 0's own value is reused for it).
        if (auto* view = texture.GetShaderResourceView())
            view->SetSampler(GetOrCreateSampler(filter, addressU, addressV, samplerSlots_[0].maxAnisotropy));

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

    void DiligentRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                                         const Matrix& world, const Matrix& view,
                                                         const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount)
    {
        DrawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void DiligentRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                                const IIndexBufferRenderer& ib,
                                                                const Matrix& world, const Matrix& view,
                                                                const Matrix& projection,
                                                                PrimitiveType primitive,
                                                                int primitiveCount)
    {
        DrawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, nullptr);
    }

    void DiligentRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                                                    const Matrix& world, const Matrix& view,
                                                    const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        DrawInternal(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
    }

    void DiligentRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                           const IIndexBufferRenderer& ib,
                                                           const Matrix& world, const Matrix& view,
                                                           const Matrix& projection,
                                                           PrimitiveType primitive, int primitiveCount,
                                                           const GpuDrawParams& params)
    {
        DrawInternal(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
    }

    namespace
    {
        // REMED-GFX-DECL-GUARD: this renderer selects its native input layout from the buffer's
        // byte stride (DrawInternal's own `switch (stride)`), exactly like Vulkan/SDL_GPU/D3D11,
        // so the shared stride-inferring rule is the right one to reuse rather than re-derive.
        //
        // The rule is asymmetric on purpose: only the bytes the caller actually declared are
        // checked. A declaration shorter than the canonical layout leaves the remaining native
        // elements unfilled, which is a missing input rather than a reinterpretation -- and it is
        // what a position-only declaration already relies on. It is pure: nothing is created,
        // nothing is queued, and a refused draw leaves the device usable for the next valid one.
        void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb_in, const char* route,
                                           bool positionOnlyFallback = false)
        {
            const auto* vb = dynamic_cast<const DiligentVertexBufferRenderer*>(&vb_in);
            if (vb == nullptr)
                return;
            CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
                vb->GetDeclarationEXT(), static_cast<int>(vb->GetStride()),
                positionOnlyFallback
                    ? CNA::Internal::Graphics::UnlistedStrideLayout::PositionOnlyFallback
                    : CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt,
                kRendererName, route);
        }
    }

    void DiligentRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                             const IIndexBufferRenderer& ib,
                                                             const Matrix& /*world*/, const Matrix& view,
                                                             const Matrix& projection,
                                                             PrimitiveType primitive, int primitiveCount,
                                                             int instanceCount,
                                                             const GpuDrawParams& params)
    {
        if (primitiveCount <= 0 || instanceCount <= 0)
            return;

        const auto* vertexBuffer = dynamic_cast<const DiligentVertexBufferRenderer*>(&vb);
        if (vertexBuffer == nullptr || vertexBuffer->GetBuffer() == nullptr)
            throw std::runtime_error(
                "CNA Diligent: instanced draw with a foreign or empty vertex buffer");
        const auto* indexBuffer = dynamic_cast<const DiligentIndexBufferRenderer*>(&ib);
        if (indexBuffer == nullptr || indexBuffer->GetBuffer() == nullptr)
            throw std::runtime_error(
                "CNA Diligent: instanced draw with a foreign or empty index buffer");
        // REMED-GFX-201/202: the instance buffer is no longer a field of its own -- it is simply
        // the lowest-slot binding whose InstanceFrequency is greater than zero. This renderer binds
        // exactly one stream of each input rate, so anything richer is refused outright rather
        // than rendered from a subset of what the caller actually bound.
        RejectUnsupportedStreamCombination(params, kRendererName);
        RequireFaithfulDeclarationEXT(vb, "instanced-indexed", /*positionOnlyFallback=*/true);
        const GpuVertexStreamBinding* instanceStream = FirstInstanceStream(params);
        const auto* instanceBuffer =
            instanceStream != nullptr
                ? dynamic_cast<const DiligentVertexBufferRenderer*>(instanceStream->buffer)
                : nullptr;
        if (instanceBuffer == nullptr || instanceBuffer->GetBuffer() == nullptr)
            throw std::runtime_error(
                "CNA Diligent: instanced draw requires a real per-instance vertex buffer");

        // DILIGENT-65: the pipeline's slot-0/slot-1 LayoutElement strides are baked in from these
        // real buffer strides (below), not guessed at 16/64 -- but a stride too small to hold what
        // the shader actually reads (float3 Position at slot 0, four float4 rows at slot 1) can
        // never be made to work by any stride value, so it is refused loudly here rather than
        // producing an out-of-bounds or nonsensical fetch.
        const auto vertexStride = static_cast<std::uint32_t>(vertexBuffer->GetStride());
        const auto instanceStride = static_cast<std::uint32_t>(instanceBuffer->GetStride());
        if (vertexStride < 12)
            throw std::runtime_error(
                "CNA Diligent: instanced draw's per-vertex stream stride (" +
                std::to_string(vertexStride) + " bytes) is too small to hold Position (12 bytes)");
        if (instanceStride < 64)
            throw std::runtime_error(
                "CNA Diligent: instanced draw's per-instance stream stride (" +
                std::to_string(instanceStride) +
                " bytes) is too small to hold a 4x4 world matrix (64 bytes)");

        SyncSwapChainSize();
        EnsureRenderTargetsBound();

        // No single shared World matrix exists here -- each instance supplies its own -- so
        // g_WorldViewProj is repurposed to hold just View*Projection, matching
        // kInstancedVertexHlsl's own doc comment.
        ShaderConstants constants{};
        MatrixToFloats(view * projection, constants.worldViewProj);
        for (int component = 0; component < 4; ++component)
            constants.diffuseColor[component] = params.diffuseColor[component];
        constants.alphaTest[2] = 1.0f;
        constants.alphaTest[3] = 1.0f;
        UploadConstants(constants);

        // REMED-GFX-202: the instanced route folds nothing into baseVertex -- every stream carries
        // its whole public VertexOffset, and a per-instance slot is addressed by instance index,
        // which a base-vertex term does not touch. This is FNA3D's own D3D11 shape:
        // `offset = binding.VertexOffset * stride` per binding, with BaseVertexLocation passed
        // separately below. The multiplier is each stream's REAL buffer stride, not
        // `GpuVertexStreamBinding::strideInBytes`, which is 0 for an instance buffer uploaded
        // through SetDataRaw without a VertexDeclaration.
        const GpuVertexStreamBinding* geometryStream = FirstPerVertexStream(params);
        const auto geometryOffset =
            geometryStream != nullptr
                ? static_cast<Dg::Uint64>(geometryStream->vertexOffset) * vertexStride
                : Dg::Uint64{0};
        const auto instanceOffset =
            static_cast<Dg::Uint64>(instanceStream->vertexOffset) * instanceStride;
        const auto stepRate = static_cast<std::uint32_t>(std::max(1, instanceStream->instanceFrequency));

        CachedPipeline& pipeline = GetOrCreatePipeline(
            MakePipelineKey(ShaderVariant::Instanced3D, primitive, vertexStride, instanceStride,
                            stepRate));

        context_->SetPipelineState(pipeline.pipeline);
        context_->SetStencilRef(static_cast<Dg::Uint32>(referenceStencil_));
        context_->SetBlendFactors(blendFactor_);
        context_->CommitShaderResources(pipeline.binding, Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const Dg::Uint64 offsets[] = {geometryOffset, instanceOffset};
        Dg::IBuffer* vertexBuffers[] = {vertexBuffer->GetBuffer(), instanceBuffer->GetBuffer()};
        context_->SetVertexBuffers(0, 2, vertexBuffers, offsets,
                                   Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   Dg::SET_VERTEX_BUFFERS_FLAG_RESET);
        context_->SetIndexBuffer(indexBuffer->GetBuffer(), 0,
                                 Dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        Dg::DrawIndexedAttribs drawAttribs;
        drawAttribs.NumIndices =
            static_cast<Dg::Uint32>(VertexCountForPrimitives(primitive, primitiveCount));
        drawAttribs.IndexType = indexBuffer->IsThirtyTwoBit() ? Dg::VT_UINT32 : Dg::VT_UINT16;
        drawAttribs.Flags = Dg::DRAW_FLAG_VERIFY_ALL;
        drawAttribs.NumInstances = static_cast<Dg::Uint32>(instanceCount);
        drawAttribs.FirstIndexLocation = static_cast<Dg::Uint32>(std::max(0, params.startIndex));
        drawAttribs.BaseVertex = static_cast<Dg::Uint32>(std::max(0, params.baseVertex));
        context_->DrawIndexed(drawAttribs);
    }

    void DiligentRenderer::DrawInternal(const IVertexBufferRenderer& vb,
                                                const IIndexBufferRenderer* ib,
                                                const Matrix& world, const Matrix& view,
                                                const Matrix& projection,
                                                PrimitiveType primitive, int primitiveCount,
                                                const GpuDrawParams* params)
    {
        if (primitiveCount <= 0)
            return;

        const auto* vertexBuffer = dynamic_cast<const DiligentVertexBufferRenderer*>(&vb);
        if (vertexBuffer == nullptr || vertexBuffer->GetBuffer() == nullptr)
            throw std::runtime_error("CNA Diligent: draw with a foreign or empty vertex buffer");

        if (params != nullptr)
            RejectUnsupportedStreamCombination(*params, kRendererName);
        RequireFaithfulDeclarationEXT(vb, ib != nullptr ? "ordinary-indexed" : "ordinary");

        if (params != nullptr)
        {
            // Each of these selects a genuinely different shader in XNA. Rendering the nearest
            // available variant instead would silently produce a different image, so the draw is
            // refused until the matching phase of plan_diligent.md lands.
            const char* unsupported = nullptr;
            if (params->customEffectRenderer)         unsupported = "custom ShaderEffect programs";
            else if (params->instanceCount > 1)      unsupported = "hardware instancing";
            if (unsupported != nullptr)
                throw std::runtime_error(std::string("CNA ") + kRendererName + " renderer: " +
                                         unsupported + " is not implemented yet");
        }

        const std::size_t stride = vertexBuffer->GetStride();
        const bool dualTexture = params != nullptr && params->dualTexture;
        ShaderVariant variant;
        switch (stride)
        {
            case 16: variant = ShaderVariant::Colored3D; break;
            case 20:
                variant = dualTexture ? ShaderVariant::DualTexture3D : ShaderVariant::Textured3D;
                break;
            case 24:
                variant = dualTexture ? ShaderVariant::DualTextureColored3D
                                      : ShaderVariant::ColoredTextured3D;
                break;
            case 32:
                // EnvironmentMapEffect has no PreferPerPixelLighting property in real XNA -- it
                // always evaluates per pixel regardless of what a caller sharing GpuDrawParams with
                // BasicEffect might have left set. Only the plain lit path honors the flag.
                if (params != nullptr && params->envMapping)
                    variant = ShaderVariant::EnvironmentMap3D;
                else if (params != nullptr && params->lightingEnabled && !params->preferPerPixelLighting)
                    variant = ShaderVariant::LitTexturedVertexLit3D;
                else
                    variant = ShaderVariant::LitTextured3D;
                break;
            case 52:
                variant = (params != nullptr && params->lightingEnabled && !params->preferPerPixelLighting)
                    ? ShaderVariant::SkinnedVertexLit3D
                    : ShaderVariant::Skinned3D;
                break;
            case 48: variant = ShaderVariant::Pbr3D; break;
            case 68: variant = ShaderVariant::SkinnedPbr3D; break;
            default:
                throw std::runtime_error("CNA Diligent: unsupported vertex stride " +
                                         std::to_string(stride));
        }
        if (dualTexture && stride != 20 && stride != 24)
            throw std::runtime_error(
                "CNA Diligent: DualTextureEffect needs a textured vertex layout (stride 20 or 24)");
        if (params != nullptr && params->skinned && !params->pbr && stride != 52)
            throw std::runtime_error(
                "CNA Diligent: SkinnedEffect needs a skinned vertex layout (stride 52)");
        if (params != nullptr && params->envMapping && stride != 32)
            throw std::runtime_error(
                "CNA Diligent: EnvironmentMapEffect needs a position/normal/UV vertex layout "
                "(stride 32)");
        if (params != nullptr && params->pbr && !params->skinned && stride != 48)
            throw std::runtime_error(
                "CNA Diligent: PbrEffect needs a position/normal/tangent/UV vertex layout "
                "(stride 48)");
        if (params != nullptr && params->pbr && params->skinned && stride != 68)
            throw std::runtime_error(
                "CNA Diligent: SkinnedPbrEffect needs a skinned PBR vertex layout (stride 68)");

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
        // GpuDrawParams' own "always pass" encoding; an all-zero fog vector is a no-op keep of 1.
        constants.alphaTest[2] = 1.0f;
        constants.alphaTest[3] = 1.0f;

        const ITextureRenderer* texture = nullptr;
        const ITextureRenderer* texture1 = nullptr;
        if (params != nullptr)
        {
            texture = params->texture0;
            texture1 = params->texture1;
            for (int component = 0; component < 4; ++component)
                constants.diffuseColor[component] = params->diffuseColor[component];
            for (int component = 0; component < 3; ++component)
            {
                // DILIGENT-59: previously summed together here and multiplied by DiffuseColor (and
                // any bound texture) as a single value in the shader -- silently multiplying
                // EmissiveColor by DiffuseColor/the texture a second time (it is already
                // premultiplied where FNA's own convention calls for that, e.g.
                // EnvironmentMapEffect::FillGpuDrawParams()'s own ambient*diffuse fold). Kept apart
                // and forwarded to their own g_Ambient/g_Emissive constants instead.
                constants.ambient[component] = params->ambientColor[component];
                constants.emissive[component] = params->emissiveColor[component];
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
            constants.envMapParams[0] = params->envMapAmount;
            constants.envMapParams[1] = params->fresnelEnabled ? 1.0f : 0.0f;
            constants.envMapParams[2] = params->fresnelFactor;
            for (int component = 0; component < 3; ++component)
                constants.envMapSpecular[component] = params->envMapSpecular[component];
            for (int component = 0; component < 4; ++component)
            {
                constants.alphaTest[component] = params->alphaTest[component];
                constants.fogVector[component] = params->fogVector[component];
            }
            for (int component = 0; component < 3; ++component)
                constants.fogColor[component] = params->fogColor[component];
            constants.flags[0] = params->textureEnabled && texture != nullptr ? 1.0f : 0.0f;
            constants.flags[1] = params->vertexColorEnabled ? 1.0f : 0.0f;
            constants.flags[2] = params->lightingEnabled ? 1.0f : 0.0f;
            constants.flags[3] = static_cast<float>(params->weightsPerVertex);
        }
        else
        {
            constants.flags[1] = 1.0f;
        }
        UploadConstants(constants);
        if ((variant == ShaderVariant::Skinned3D || variant == ShaderVariant::SkinnedVertexLit3D ||
            variant == ShaderVariant::SkinnedPbr3D) &&
            params != nullptr)
            UploadBoneTransforms(*params);
        if ((variant == ShaderVariant::Pbr3D || variant == ShaderVariant::SkinnedPbr3D) &&
            params != nullptr)
            UploadPbrConstants(*params);

        CachedPipeline& pipeline = GetOrCreatePipeline(MakePipelineKey(variant, primitive));
        if (pipeline.textureVariable != nullptr)
        {
            // A vertex layout that carries texture coordinates does not oblige the caller to
            // supply a texture: BasicEffect.TextureEnabled = false over a
            // VertexPositionColorTexture stream is ordinary XNA. The shader already ignores the
            // sample in that case (g_Flags.x is 0), but the pipeline still declares the variable,
            // so something has to be bound -- the fallback white texture is that something.
            const auto* diligentTexture = dynamic_cast<const DiligentSampledTexture*>(texture);
            Dg::ITextureView* view =
                diligentTexture != nullptr ? diligentTexture->GetShaderResourceView() : nullptr;
            if (view == nullptr)
                view = fallbackTextureView_;
            const SamplerSlotState& slot0 = samplerSlots_[0];
            view->SetSampler(
                GetOrCreateSampler(slot0.filter, slot0.addressU, slot0.addressV, slot0.maxAnisotropy));
            pipeline.textureVariable->Set(view);
        }
        if (pipeline.texture2Variable != nullptr)
        {
            const auto* secondTexture = dynamic_cast<const DiligentSampledTexture*>(texture1);
            Dg::ITextureView* view =
                secondTexture != nullptr ? secondTexture->GetShaderResourceView() : nullptr;
            // A missing second layer falls back to white, which the doubled-first-layer modulate
            // turns into "just the first layer at double brightness" rather than a black surface.
            if (view == nullptr)
                view = fallbackTextureView_;
            // Slot 1: matches this project's own established cross-renderer convention for
            // DualTextureEffect's second layer (see e.g. dual_texture3d.frag.hlsl's own t1/s1).
            const SamplerSlotState& slot1 = samplerSlots_[1];
            view->SetSampler(
                GetOrCreateSampler(slot1.filter, slot1.addressU, slot1.addressV, slot1.maxAnisotropy));
            pipeline.texture2Variable->Set(view);
        }
        if (pipeline.envMapVariable != nullptr)
        {
            // A cube map bound as EnvironmentMapEffect's reflection source can be a plain
            // TextureCube or a RenderTargetCube (XNA allows both, since the latter derives from
            // the former) -- DiligentSampledTexture is what both concrete types share.
            const auto* envMap = dynamic_cast<const DiligentSampledTexture*>(
                params != nullptr ? params->envMap : nullptr);
            if (envMap == nullptr || envMap->GetShaderResourceView() == nullptr)
                throw std::runtime_error(
                    "CNA Diligent: EnvironmentMapEffect was drawn without a cube map");
            Dg::ITextureView* view = envMap->GetShaderResourceView();
            // Slot 1: matches env_map3d.frag.hlsl's own t1/s1 convention (uTexture=0, uEnvMap=1).
            const SamplerSlotState& slot1 = samplerSlots_[1];
            view->SetSampler(
                GetOrCreateSampler(slot1.filter, slot1.addressU, slot1.addressV, slot1.maxAnisotropy));
            pipeline.envMapVariable->Set(view);
        }
        if (pipeline.normalMapVariable != nullptr)
        {
            // PbrEffect's optional maps: an unbound one is ordinary XNA (glTF materials frequently
            // omit some), not an error -- BindPbrMap()'s own fallback view is each map's "absent"
            // identity (flat tangent-space normal, or white for the other three -- see
            // flatNormalTextureView_/fallbackTextureView_'s own doc comments). Slots 1-4 match
            // pbr3d.frag.hlsl's own t1..t4 convention (base colour is slot 0, bound above).
            const auto BindPbrMap = [this](Dg::IShaderResourceVariable* variable,
                                           const ITextureRenderer* texture,
                                           Dg::ITextureView* fallback, int slotIndex) {
                const auto* diligentTexture = dynamic_cast<const DiligentSampledTexture*>(texture);
                Dg::ITextureView* view =
                    diligentTexture != nullptr ? diligentTexture->GetShaderResourceView() : nullptr;
                if (view == nullptr)
                    view = fallback;
                const SamplerSlotState& slot = samplerSlots_[slotIndex];
                view->SetSampler(
                    GetOrCreateSampler(slot.filter, slot.addressU, slot.addressV, slot.maxAnisotropy));
                variable->Set(view);
            };
            BindPbrMap(pipeline.normalMapVariable, params != nullptr ? params->pbrNormalMap : nullptr,
                      flatNormalTextureView_, 1);
            BindPbrMap(pipeline.metallicRoughnessVariable,
                      params != nullptr ? params->pbrMetallicRoughnessMap : nullptr,
                      fallbackTextureView_, 2);
            BindPbrMap(pipeline.emissiveMapVariable,
                      params != nullptr ? params->pbrEmissiveMap : nullptr, fallbackTextureView_, 3);
            BindPbrMap(pipeline.occlusionMapVariable,
                      params != nullptr ? params->pbrOcclusionMap : nullptr, fallbackTextureView_, 4);
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
            const auto* indexBuffer = dynamic_cast<const DiligentIndexBufferRenderer*>(ib);
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

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Diligent::DiligentRenderer>(args);
    }
}
