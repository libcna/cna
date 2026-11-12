// SPDX-License-Identifier: MS-PL
#pragma once

// The WebGPU renderer's pure XNA -> WebGPU translations: state enums, texture and vertex formats,
// copy-layout arithmetic, pipeline cache keys and the stock-effect uniform packing. None of them
// touches a device, which is why they live here rather than in WebGPURenderer.cpp's anonymous
// namespace: modules/renderers/webgpu/tests pins each one against XNA's enum semantics and the
// WebGPU specification without needing an adapter (TQ-012).

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::WebGPU
{
    inline constexpr std::uint64_t kMinimumBufferSize = 4;

    [[nodiscard]] inline std::uint64_t Align4(std::uint64_t value)
    {
        return std::max(kMinimumBufferSize, (value + 3u) & ~std::uint64_t{3u});
    }

    // WEBGPU-144: resolves an XNA SurfaceFormat ordinal to the WGPU texture format used to store
    // it. Block-compressed formats (DXT1/3/5, BC7, and their sRGB variants) map to the matching
    // WGPUTextureFormat_BC* and are uploaded as raw 4x4 blocks (blockBytes bytes each); every
    // other format is treated as the renderer's plain RGBA8Unorm.
    struct WebGPUTexFormatInfo
    {
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        bool compressed = false;
        int blockBytes = 4;  ///< bytes per 4x4 block (compressed) / per texel (RGBA8)
    };

    [[nodiscard]] inline WebGPUTexFormatInfo ClassifyWebGPUTextureFormat(int surfaceFormat)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Dxt1:        return {WGPUTextureFormat_BC1RGBAUnorm, true, 8};
        case SurfaceFormat::Dxt3:        return {WGPUTextureFormat_BC2RGBAUnorm, true, 16};
        case SurfaceFormat::Dxt5:        return {WGPUTextureFormat_BC3RGBAUnorm, true, 16};
        case SurfaceFormat::Dxt5SrgbEXT: return {WGPUTextureFormat_BC3RGBAUnormSrgb, true, 16};
        case SurfaceFormat::Bc7EXT:      return {WGPUTextureFormat_BC7RGBAUnorm, true, 16};
        case SurfaceFormat::Bc7SrgbEXT:  return {WGPUTextureFormat_BC7RGBAUnormSrgb, true, 16};
        // WEBGPU-184: XNA's two signed-normalized byte formats map cleanly onto core WebGPU --
        // `rg8snorm` and `rgba8snorm` are both filterable float formats there, and a shader
        // samples them in [-1,1], which is the semantic NormalizedByte carries. No expansion,
        // no CPU conversion: the bytes the game supplies are the bytes the GPU stores.
        //
        // Deliberately NOT here: Bgr565, Bgra5551 and Bgra4444. WebGPU has no 16-bit packed
        // colour format at all, so the only way to accept them would be to expand each texel to
        // RGBA8 at upload -- a texture that says it is 565 while the GPU holds 8888, which is
        // the shape of defect WEBGPU-163 was and which WEBGPU-183's surface test exists to
        // stop. They stay refused, by name, and GetAdditionalLimitationsTextEXT() says so.
        case SurfaceFormat::NormalizedByte2: return {WGPUTextureFormat_RG8Snorm, false, 2};
        case SurfaceFormat::NormalizedByte4: return {WGPUTextureFormat_RGBA8Snorm, false, 4};
        // Apple M4 AM4-187: XNA's four-channel half-float formats -- HdrBlendable
        // is RGBA16F, as the render-target map already states -- are core WebGPU `rgba16float`,
        // which is sampleable, filterable and renderable without any feature, so the game's
        // eight bytes per texel are stored exactly and mip generation works as for RGBA8.
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:    return {WGPUTextureFormat_RGBA16Float, false, 8};
        case SurfaceFormat::Single:           return {WGPUTextureFormat_R32Float, false, 4};
        case SurfaceFormat::Vector4:          return {WGPUTextureFormat_RGBA32Float, false, 16};
        default:                         return {WGPUTextureFormat_RGBA8Unorm, false, 4};
        }
    }

    /// MSR-018: signed-normalized WebGPU formats are sampleable and filterable but cannot be
    /// render attachments. They may still own and receive an explicitly-authored mip chain;
    /// only the render-pass-based automatic mip generator is unavailable to them.
    [[nodiscard]] constexpr bool IsSignedNormalizedTextureFormatEXT(
        WGPUTextureFormat format) noexcept
    {
        return format == WGPUTextureFormat_RG8Snorm ||
               format == WGPUTextureFormat_RGBA8Snorm;
    }

    // WebGPU enables primitive restart for indexed strip pipelines through
    // WGPUPrimitiveState::stripIndexFormat. It must match the format passed to
    // SetIndexBuffer for indexed line/triangle strips, and must remain Undefined for
    // non-indexed strips and every list/point topology. Both inputs come from the same
    // deferred command snapshot used by CreateAndBindDeferredIndexBuffer(), never from a
    // live IndexBuffer object or a renderer-global "last format".
    [[nodiscard]] inline WGPUIndexFormat RequiredStripIndexFormat(
        WGPUPrimitiveTopology topology,
        bool indexed,
        bool index32)
    {
        if (!indexed ||
            (topology != WGPUPrimitiveTopology_LineStrip &&
             topology != WGPUPrimitiveTopology_TriangleStrip))
        {
            return WGPUIndexFormat_Undefined;
        }
        return index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16;
    }

    template<typename TCommand>
    [[nodiscard]] inline WGPUIndexFormat RequiredStripIndexFormat(const TCommand& command)
    {
        return RequiredStripIndexFormat(
            command.topology,
            command.indexed && (!command.indexData.empty() ||
                                command.resident.indexStorage != nullptr),
            command.index32);
    }

    /// WEBGPU-153: whether a topology has a polygon interior at all.
    ///
    /// WebGPU forbids a non-zero depth bias on a line or point topology, so a pipeline built for
    /// the wireframe expansion below has to drop the caller's `RasterizerState.DepthBias` --
    /// which is not a substitution, because a depth bias is defined as an offset applied to a
    /// polygon's depth slope and a line has no slope to offset.
    [[nodiscard]] constexpr bool TopologyHasPolygonInterior(WGPUPrimitiveTopology topology) noexcept
    {
        return topology == WGPUPrimitiveTopology_TriangleList ||
               topology == WGPUPrimitiveTopology_TriangleStrip;
    }

    /**
     * @brief The non-sRGB counterpart of an 8-bit colour format, or the format unchanged.
     *
     * CNA's SurfaceFormat::Color is a plain UNORM byte format, so every colour
     * attachment this renderer renders into must use the linear variant. Channel order is
     * deliberately preserved -- only the transfer function is dropped -- so the readback
     * swizzle that keys on BGRA-ness stays correct either way.
     */
    [[nodiscard]] constexpr WGPUTextureFormat NonSrgbColorFormat(WGPUTextureFormat format)
    {
        switch (format)
        {
            case WGPUTextureFormat_BGRA8UnormSrgb: return WGPUTextureFormat_BGRA8Unorm;
            case WGPUTextureFormat_RGBA8UnormSrgb: return WGPUTextureFormat_RGBA8Unorm;
            default:                               return format;
        }
    }

    [[nodiscard]] inline std::uint32_t AlignBytesPerRow(std::uint32_t bytesPerRow)
    {
        constexpr std::uint32_t kAlignment = 256;
        return (bytesPerRow + kAlignment - 1) / kAlignment * kAlignment;
    }

    // WEBGPU-51/57/112/113: mip-level dimension helper shared by every GetData()/texture
    // constructor below that needs a specific level's real width/height/depth -- mirrors
    // Texture2D.cpp's own file-local mipDim(base, level) formula exactly (max(1, base>>level)).
    [[nodiscard]] inline int MipDim(int base, int level)
    {
        return std::max(1, base >> level);
    }

    // XNA CompareFunction ordinals -> WGPUCompareFunction:
    // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7.
    [[nodiscard]] inline WGPUCompareFunction ToWGPUCompareFunction(int xnaCompare)
    {
        switch (xnaCompare)
        {
            case 1: return WGPUCompareFunction_Never;
            case 2: return WGPUCompareFunction_Less;
            case 3: return WGPUCompareFunction_LessEqual;
            case 4: return WGPUCompareFunction_Equal;
            case 5: return WGPUCompareFunction_GreaterEqual;
            case 6: return WGPUCompareFunction_Greater;
            case 7: return WGPUCompareFunction_NotEqual;
            default: return WGPUCompareFunction_Always;
        }
    }

    // WEBGPU-83: XNA StencilOperation ordinal -> WGPU. Increment/Decrement (unsaturated) wrap;
    // the *Saturation variants clamp.
    [[nodiscard]] inline WGPUStencilOperation ToWGPUStencilOperation(int xnaOp)
    {
        switch (xnaOp)
        {
            case 1: return WGPUStencilOperation_Zero;
            case 2: return WGPUStencilOperation_Replace;
            case 3: return WGPUStencilOperation_IncrementWrap;
            case 4: return WGPUStencilOperation_DecrementWrap;
            case 5: return WGPUStencilOperation_IncrementClamp;
            case 6: return WGPUStencilOperation_DecrementClamp;
            case 7: return WGPUStencilOperation_Invert;
            default: return WGPUStencilOperation_Keep;  // 0 = Keep
        }
    }

    // WEBGPU-39: maps XNA DepthFormat -> the exact WGPU depth attachment format the other CNA
    // backends (e.g. EasyGL) also use, per render target.
    // None has no depth attachment at all. All of Depth16Unorm/Depth24Plus/Depth24PlusStencil8 are
    // core WebGPU formats (always available). hasStencil is true only for the combined format.
    struct DepthAttachmentEXT { WGPUTextureFormat format; bool hasStencil; bool hasDepth; };
    [[nodiscard]] inline DepthAttachmentEXT MapDepthFormatEXT(int depthFormat)
    {
        switch (depthFormat)  // DepthFormat: None=0, Depth16=1, Depth24=2, Depth24Stencil8=3
        {
            case 1:  return { WGPUTextureFormat_Depth16Unorm,        false, true };
            case 2:  return { WGPUTextureFormat_Depth24Plus,         false, true };
            case 3:  return { WGPUTextureFormat_Depth24PlusStencil8, true,  true };
            default: return { WGPUTextureFormat_Undefined,           false, false }; // None
        }
    }
    [[nodiscard]] inline bool DepthFormatHasStencilEXT(WGPUTextureFormat f)
    {
        return f == WGPUTextureFormat_Depth24PlusStencil8 || f == WGPUTextureFormat_Depth32FloatStencil8;
    }

    // WEBGPU-83: bakes the XNA stencil state into a WGPUDepthStencilState's stencilFront/back +
    // masks. When disabled, leaves the INIT defaults (a stencil test that always passes and
    // never writes).
    //
    // TQ-012: XNA applies the ordinary stencil state to CLOCKWISE triangles and the
    // CounterClockwise* state to COUNTER-CLOCKWISE ones (DepthStencilState::Apply writes the
    // latter to D3DRS_CCW_STENCIL*). Every pipeline here declares frontFace = CCW, which makes the
    // counter-clockwise-as-displayed winding WebGPU's FRONT face -- the pairing ToWGPUCullMode
    // makes, where CullCounterClockwiseFace removes WGPUCullMode_Front. So the counter-clockwise
    // state belongs on stencilFront and the ordinary state on stencilBack. This used to be the
    // other way round, and the two-sided example asserted the swap.
    //
    // A line or point has no winding: XNA's two-sided mode leaves it on the ordinary state, while
    // WebGPU counts every non-polygon primitive as FRONT-facing -- the slot that now holds the
    // counter-clockwise state. So for such a topology (@p polygonTopology false; the topology is in
    // every pipeline key already) both slots carry the ordinary state.
    inline void FillWGPUStencilState(WGPUDepthStencilState& ds, const WebGPURenderer::StencilKeyParams& s,
                                     bool polygonTopology)
    {
        if (!s.enable)
            return;
        WGPUStencilFaceState clockwise{};
        clockwise.compare = ToWGPUCompareFunction(s.func);
        clockwise.failOp = ToWGPUStencilOperation(s.stencilFail);
        clockwise.depthFailOp = ToWGPUStencilOperation(s.depthFail);
        clockwise.passOp = ToWGPUStencilOperation(s.stencilPass);
        ds.stencilBack = clockwise;
        if (s.twoSided && polygonTopology)
        {
            WGPUStencilFaceState counterClockwise{};
            counterClockwise.compare = ToWGPUCompareFunction(s.ccwFunc);
            counterClockwise.failOp = ToWGPUStencilOperation(s.ccwFail);
            counterClockwise.depthFailOp = ToWGPUStencilOperation(s.ccwDepthFail);
            counterClockwise.passOp = ToWGPUStencilOperation(s.ccwPass);
            ds.stencilFront = counterClockwise;
        }
        else
        {
            ds.stencilFront = clockwise;
        }
        ds.stencilReadMask = static_cast<std::uint32_t>(s.readMask & 0xFF);
        ds.stencilWriteMask = static_cast<std::uint32_t>(s.writeMask & 0xFF);
    }

    /// TQ-012: the depth half of a WGPUDepthStencilState from XNA's DepthBufferEnable,
    /// DepthBufferWriteEnable and DepthBufferFunction.
    ///
    /// DepthBufferEnable = false disables depth buffering outright (D3DRS_ZENABLE): nothing is
    /// tested and nothing is written, whatever DepthBufferWriteEnable says -- and XNA's
    /// DepthBufferWriteEnable defaults to true. WebGPU has no separate enable: a compare of Always
    /// with depthWriteEnabled still writes, so the write must be dropped here.
    inline void FillWGPUDepthState(WGPUDepthStencilState& ds, bool depthTest, bool depthWrite, int depthFunc)
    {
        ds.depthWriteEnabled = depthTest && depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
    }

    // WEBGPU-83: a stencil state's contribution to the pipeline cache key. A disabled stencil
    // folds a single constant (0) regardless of its stored-but-unused func/op fields, so
    // different disabled DepthStencilStates never fragment the cache.
    [[nodiscard]] inline std::uint64_t HashStencilState(const WebGPURenderer::StencilKeyParams& s)
    {
        if (!s.enable)
            return 0;
        std::uint64_t h = 1;
        auto mix = [&h](std::uint64_t v) { h = h * 31u + v; };
        mix(static_cast<std::uint64_t>(s.func));
        mix(static_cast<std::uint64_t>(s.stencilPass));
        mix(static_cast<std::uint64_t>(s.stencilFail));
        mix(static_cast<std::uint64_t>(s.depthFail));
        mix(static_cast<std::uint64_t>(s.readMask & 0xFF));
        mix(static_cast<std::uint64_t>(s.writeMask & 0xFF));
        mix(s.twoSided ? 1u : 0u);
        if (s.twoSided)
        {
            mix(static_cast<std::uint64_t>(s.ccwFunc));
            mix(static_cast<std::uint64_t>(s.ccwPass));
            mix(static_cast<std::uint64_t>(s.ccwFail));
            mix(static_cast<std::uint64_t>(s.ccwDepthFail));
        }
        return h;
    }

    [[nodiscard]] inline WGPUAddressMode ToAddressMode(int mode)
    {
        switch (mode)
        {
            case 0: return WGPUAddressMode_Repeat;
            case 2: return WGPUAddressMode_MirrorRepeat;
            default: return WGPUAddressMode_ClampToEdge;
        }
    }

    // ---- WEBGPU-41/77/78/79/80/81/82/83: graphics-state -> WGPU translation helpers ----
    // Shared by every 3D GetOrCreatePipeline*() family so the exact same XNA->WGPU mapping is
    // used everywhere (mirrors ToWGPUCompareFunction()'s own established role/pattern).

    // XNA Blend enum -> WGPUBlendFactor: One=0, Zero=1, SourceColor=2, InverseSourceColor=3,
    // SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7,
    // DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10, InverseBlendFactor=11,
    // SourceAlphaSaturation=12.
    [[nodiscard]] inline WGPUBlendFactor ToWGPUBlendFactor(int xnaBlend)
    {
        switch (xnaBlend)
        {
            case  1: return WGPUBlendFactor_Zero;
            case  2: return WGPUBlendFactor_Src;
            case  3: return WGPUBlendFactor_OneMinusSrc;
            case  4: return WGPUBlendFactor_SrcAlpha;
            case  5: return WGPUBlendFactor_OneMinusSrcAlpha;
            case  6: return WGPUBlendFactor_Dst;
            case  7: return WGPUBlendFactor_OneMinusDst;
            case  8: return WGPUBlendFactor_DstAlpha;
            case  9: return WGPUBlendFactor_OneMinusDstAlpha;
            case 10: return WGPUBlendFactor_Constant;
            case 11: return WGPUBlendFactor_OneMinusConstant;
            case 12: return WGPUBlendFactor_SrcAlphaSaturated;
            default: return WGPUBlendFactor_One; // Blend::One = 0
        }
    }

    // XNA BlendFunction enum -> WGPUBlendOperation: Add=0, Subtract=1, ReverseSubtract=2,
    // Min=3, Max=4.
    [[nodiscard]] inline WGPUBlendOperation ToWGPUBlendOperation(int xnaBlendFunc)
    {
        switch (xnaBlendFunc)
        {
            case 1: return WGPUBlendOperation_Subtract;
            case 2: return WGPUBlendOperation_ReverseSubtract;
            case 3: return WGPUBlendOperation_Min;
            case 4: return WGPUBlendOperation_Max;
            default: return WGPUBlendOperation_Add; // BlendFunction::Add = 0
        }
    }

    // Fills a WGPUBlendState's real blend factors/op from BlendKeyParams. Caller is
    // responsible for only setting target.blend to &result when blending is actually enabled
    // (a disabled WGPUColorTargetState::blend must stay nullptr, not a state with
    // srcFactor=One/dstFactor=Zero -- see every GetOrCreatePipeline*() call site).
    inline void FillWGPUBlendState(WGPUBlendState& blendState, const WebGPURenderer::BlendKeyParams& bp)
    {
        blendState.color.srcFactor = ToWGPUBlendFactor(bp.colorSrc);
        blendState.color.dstFactor = ToWGPUBlendFactor(bp.colorDst);
        blendState.color.operation = ToWGPUBlendOperation(bp.colorFunc);
        blendState.alpha.srcFactor = ToWGPUBlendFactor(bp.alphaSrc);
        blendState.alpha.dstFactor = ToWGPUBlendFactor(bp.alphaDst);
        blendState.alpha.operation = ToWGPUBlendOperation(bp.alphaFunc);
    }

    // XNA CullMode: None=0, CullClockwiseFace=1, CullCounterClockwiseFace=2. Every 3D pipeline
    // sets pipeline.primitive.frontFace = WGPUFrontFace_CCW, and WebGPU determines facing from
    // the signed area in FRAMEBUFFER space, whose Y points down -- so this renderer's front face
    // is the counter-clockwise-AS-DISPLAYED one and its back face the clockwise one.
    //
    // Each XNA enum names the face it CULLS, and clockwise-as-displayed is XNA's
    // FRONT face (FNA's SpriteBatch emits TL->TR->BL and BR->BL->TR, both clockwise as
    // displayed, and they must survive the RasterizerState.CullCounterClockwise that
    // SpriteBatch.Begin itself defaults to). So CullClockwiseFace has to remove the clockwise
    // face, which here is WGPUCullMode_Back, and CullCounterClockwiseFace has to remove the
    // counter-clockwise one, which here is WGPUCullMode_Front.
    //
    // This pairing was previously the other way round. The "empirical verification" behind it
    // used a probe quad whose own winding was mis-derived, so it demanded that a BACK face stay
    // visible under XNA's default cull mode and the mapping was inverted to satisfy it -- which
    // is why the defect only ever showed in the stock 3D path (the SpriteBatch pipeline below
    // hardcodes WGPUCullMode_None and so was immune). Now measured against the FNA-derived
    // contract by modules/graphics/examples/frontface_winding_test.cpp on eight renderers.
    [[nodiscard]] inline WGPUCullMode ToWGPUCullMode(int xnaCullMode)
    {
        switch (xnaCullMode)
        {
            case 1: return WGPUCullMode_Back;   // CullClockwiseFace
            case 2: return WGPUCullMode_Front;  // CullCounterClockwiseFace
            default: return WGPUCullMode_None;
        }
    }

    // WEBGPU-116: the WebGPU spelling of every XNA VertexElementFormat. Every pipeline builder
    // selects its per-attribute WGPUVertexFormat through this one map, so each attribute names
    // the XNA format it means and the ordinal->format table lives in exactly one place instead
    // of ~55 scattered literals. Color is Unorm8x4, not a BGRA vertex format: WebGPU has no
    // B8G8R8A8 vertex format, and this renderer's packed layouts already store colour as RGBA
    // the shader reads straight.
    [[nodiscard]] inline WGPUVertexFormat WebGPUVertexFormatFromVEF(
        Microsoft::Xna::Framework::Graphics::VertexElementFormat format)
    {
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        switch (format)
        {
            case VertexElementFormat::Single:  return WGPUVertexFormat_Float32;
            case VertexElementFormat::Vector2: return WGPUVertexFormat_Float32x2;
            case VertexElementFormat::Vector3: return WGPUVertexFormat_Float32x3;
            case VertexElementFormat::Vector4: return WGPUVertexFormat_Float32x4;
            case VertexElementFormat::Color:   return WGPUVertexFormat_Unorm8x4;
            case VertexElementFormat::Byte4:   return WGPUVertexFormat_Uint8x4;
            case VertexElementFormat::Short2:  return WGPUVertexFormat_Sint16x2;
            case VertexElementFormat::Short4:  return WGPUVertexFormat_Sint16x4;
            case VertexElementFormat::NormalizedShort2: return WGPUVertexFormat_Snorm16x2;
            case VertexElementFormat::NormalizedShort4: return WGPUVertexFormat_Snorm16x4;
            case VertexElementFormat::HalfVector2: return WGPUVertexFormat_Float16x2;
            case VertexElementFormat::HalfVector4: return WGPUVertexFormat_Float16x4;
        }
        throw std::invalid_argument(
            "CNA WebGPU: unrecognized VertexElementFormat ordinal " +
            std::to_string(static_cast<int>(format)));
    }

    /// WEBGPU-198: bytes per texel of a native colour format, for the
    /// render-target readback.
    ///
    /// The readback used to assume four, because a render target could only ever be `Color`.
    /// The XNA layer asks for exactly `getBytesPerTexel()` bytes per texel and decodes them
    /// itself (`Texture2D::GetDataBytes` refuses a mismatched element type before the renderer
    /// is reached), so this has to agree with `SurfaceFormat`'s own widths rather than with
    /// anything convenient.
    ///
    /// @param format The native colour format.
    /// @return Bytes per texel, or 0 for a format this renderer does not read back.
    /// WEBGPU-200: whether a native colour format is one of the 32-bit
    /// float formats whose FILTERING and BLENDING are optional adapter features.
    ///
    /// The 16-bit float formats are deliberately absent: `R16Float`/`RG16Float`/`RGBA16Float`
    /// are renderable, filterable AND blendable in core WebGPU, which is why `WEBGPU-199` was
    /// the cheap half of the family and this one is not.
    ///
    /// @param format The native colour format.
    /// @return Whether it is a 32-bit float format.
    [[nodiscard]] constexpr bool IsThirtyTwoBitFloatFormatEXT(WGPUTextureFormat format) noexcept
    {
        return format == WGPUTextureFormat_R32Float ||
               format == WGPUTextureFormat_RG32Float ||
               format == WGPUTextureFormat_RGBA32Float;
    }

    [[nodiscard]] constexpr int BytesPerTexelEXT(WGPUTextureFormat format) noexcept
    {
        switch (format)
        {
        case WGPUTextureFormat_R16Float:        return 2;
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RG16Float:
        case WGPUTextureFormat_R32Float:        return 4;
        case WGPUTextureFormat_RGBA16Float:
        case WGPUTextureFormat_RGBA16Unorm:     // WEBGPU-201: four 16-bit UNORM channels.
        case WGPUTextureFormat_RG32Float:       return 8;
        case WGPUTextureFormat_RGBA32Float:     return 16;
        default:                                return 0;
        }
    }

    /// WEBGPU-198: the `SurfaceFormat` -> `WGPUTextureFormat` map for a
    /// RENDER TARGET's colour attachment, which is a strictly narrower question than what this
    /// renderer can STORE (`ClassifyWebGPUTextureFormat`).
    ///
    /// The eight entries are the reference renderer's own set (`MapRenderTargetColorFormat`),
    /// so a game asking for a float target gets the same answer from both. `Color` is
    /// deliberately absent: it maps to the renderer's live `surfaceFormat_`, which is a member
    /// rather than a constant, and keeping it out of this table is what makes every existing
    /// `Color` target byte-identical to its pre-198 form.
    ///
    /// `Rgba64` maps to `RGBA16Unorm`, which is NOT core WebGPU: natively it needs
    /// wgpu-native's `WGPUNativeFeature_TextureFormat16bitNorm`; a browser needs
    /// `TextureFormatsTier1`. The map describes representation, not availability. The probe
    /// first checks the enabled feature: a missing browser feature throws a JavaScript
    /// TypeError before WebGPU's validation error scopes can catch it.
    ///
    /// @param surfaceFormat SurfaceFormat ordinal.
    /// @param out Receives the native format when the return value is true.
    /// @return Whether this format has a render-target representation here at all.
    [[nodiscard]] inline bool MapRenderTargetColorFormatEXT(int surfaceFormat, WGPUTextureFormat& out)
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Single:       out = WGPUTextureFormat_R32Float;    return true;
        case SurfaceFormat::Vector2:      out = WGPUTextureFormat_RG32Float;   return true;
        case SurfaceFormat::Vector4:      out = WGPUTextureFormat_RGBA32Float; return true;
        case SurfaceFormat::HalfSingle:   out = WGPUTextureFormat_R16Float;    return true;
        case SurfaceFormat::HalfVector2:  out = WGPUTextureFormat_RG16Float;   return true;
        // HdrBlendable is XNA's "float format for HDR data"; on Windows it was RGBA16F, and CNA
        // makes that equivalence explicit rather than inventing a third meaning for it -- the
        // same choice the reference renderer states in its own map.
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable: out = WGPUTextureFormat_RGBA16Float; return true;
        case SurfaceFormat::Rgba64:       out = WGPUTextureFormat_RGBA16Unorm;  return true;
        default:                          return false;
        }
    }

    // Whether this per-vertex stride's established packed layout carries a
    // COLOR0 element, and at which byte offset. The ordinary route's own pipelines derive
    // their Unorm8x4 colour attribute from exactly this table -- colored3d.wgsl's stride-16
    // ColoredVertex and the stride-24 colored+textured layout, both at offset 12 -- so reading
    // the same one here is what makes the instanced route's VertexColorEnabled mean what its
    // own ordinary route's already means. Every other stride keeps the position-only
    // Instanced3D module it has always used: 20 (VertexPositionTexture) and 32
    // (VertexPositionNormalTexture) carry no COLOR0 at all, and the skinned/PBR strides carry
    // one the instanced route has no bone palette or tangent basis to render anyway.
    [[nodiscard]] inline bool InstancedPackedColorOffsetForStride(
        std::size_t pvStride, std::uint64_t& colorOffsetOut)
    {
        switch (pvStride)
        {
            case 16:   // VertexPositionColor
            case 24:   // VertexPositionColorTexture
                colorOffsetOut = 12;
                return true;
            default:
                return false;
        }
    }

    // Encodes (topology, strip-index compatibility, depth test/write/func, blend enable+params,
    // cull mode, wireframe, depth bias, slope-scale depth bias, and a caller-supplied salt for
    // any extra dimension -- e.g. stride for AlphaTest3D/DualTexture3D) into a single uint64_t
    // pipeline cache key.
    // When blend is disabled, the (irrelevant) blend factors always collapse to the same bits
    // so different disabled BlendStates don't create duplicate pipelines.
    /// WEBGPU-202: the default extra-MRT-slot formats for a single-target draw -- never read,
    /// because the loop that consumes them runs only when `colorAttachmentCount > 1`.
    inline constexpr std::array<WGPUTextureFormat, 4> kNoMrtColorFormatsEXT{};

    [[nodiscard]] inline std::uint64_t Make3DPipelineKey(
        WGPUPrimitiveTopology topology,
        WGPUIndexFormat stripIndexFormat,
        bool depthTest,
        bool depthWrite,
        int depthFunc,
        bool blend,
        const WebGPURenderer::BlendKeyParams& bp,
        int cullMode,
        bool wireframe,
        float depthBias,
        float slopeScaleDepthBias,
        std::uint64_t salt,
        int colorWriteMask,
        std::uint32_t sampleMask,
        int colorAttachmentCount = 1,
        WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth24PlusStencil8,
        WGPUTextureFormat colorFormat = WGPUTextureFormat_Undefined,
        std::uint32_t sampleCount = 0,
        const std::array<WGPUTextureFormat, 4>& mrtColorFormats = kNoMrtColorFormatsEXT)
    {
        std::uint64_t key = static_cast<std::uint64_t>(topology);
        // RequiredStripIndexFormat() canonicalizes this to Undefined for
        // list/point topologies and non-indexed strips, so only WebGPU-incompatible strip
        // formats create distinct pipeline variants. Buffer identity never participates.
        key = key * 31u + static_cast<std::uint64_t>(stripIndexFormat);
        key = key * 31u + (depthTest ? 1u : 0u);
        key = key * 31u + (depthWrite ? 1u : 0u);
        key = key * 31u + static_cast<std::uint64_t>(depthFunc);
        key = key * 31u + (blend ? 1u : 0u);
        if (blend)
        {
            key = key * 31u + static_cast<std::uint64_t>(bp.colorSrc);
            key = key * 31u + static_cast<std::uint64_t>(bp.colorDst);
            key = key * 31u + static_cast<std::uint64_t>(bp.alphaSrc);
            key = key * 31u + static_cast<std::uint64_t>(bp.alphaDst);
            key = key * 31u + static_cast<std::uint64_t>(bp.colorFunc);
            key = key * 31u + static_cast<std::uint64_t>(bp.alphaFunc);
        }
        key = key * 31u + static_cast<std::uint64_t>(cullMode);
        key = key * 31u + (wireframe ? 1u : 0u);
        // WEBGPU-41/79: DepthBias/SlopeScaleDepthBias are baked into the pipeline object --
        // wgpu-native has no per-draw depth-bias override (unlike Vulkan's vkCmdSetDepthBias),
        // so the exact float bits must be part of the cache key, not just approximately
        // bucketed.
        key = key * 1000003u + static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(depthBias));
        key = key * 1000003u + static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(slopeScaleDepthBias));
        key = key * 1000003u + salt;
        // Colour write mask + sample mask are static wgpu-native pipeline state.
        key = key * 31u + static_cast<std::uint64_t>(colorWriteMask & 0xF);
        key = key * 31u + static_cast<std::uint64_t>(sampleMask);
        // WEBGPU-86 MRT: a pipeline built for an N-attachment pass is NOT compatible with a
        // 1-attachment pass (WebGPU rejects the mismatch), so the count must separate them.
        // Folded only when > 1, so every single-target pipeline's key is byte-identical to
        // before -- the instanced/viewport/scissor CARDINALITY tests still see the same variants.
        if (colorAttachmentCount > 1)
            key = key * 31u + static_cast<std::uint64_t>(colorAttachmentCount);
        // WEBGPU-39: a pipeline built for one depth attachment format is incompatible with a pass
        // that uses another (WebGPU rejects the mismatch). Folded only when != the default
        // Depth24PlusStencil8, so every existing (backbuffer/Depth24Stencil8) key is byte-identical.
        if (depthFormat != WGPUTextureFormat_Depth24PlusStencil8)
            key = key * 31u + static_cast<std::uint64_t>(depthFormat);
        // WEBGPU-197: a WebGPU pipeline bakes its colour target's FORMAT and SAMPLE COUNT, so
        // two draws into targets that differ in either must not share one pipeline -- WebGPU
        // rejects the mismatch outright. Both are folded unconditionally: a cache is per
        // process, so what matters is that keys stay DISTINCT where the pipelines must be, not
        // that a key keeps the numeric value it had before this dimension existed. Every draw
        // in one pass sees the same pair, so no cardinality test gains a variant.
        key = key * 31u + static_cast<std::uint64_t>(colorFormat);
        key = key * 31u + static_cast<std::uint64_t>(sampleCount);
        // WEBGPU-202: and every EXTRA MRT slot's format, not slot 0 alone. WebGPU permits an MRT
        // set whose attachments differ in format, but a pipeline's `targets[]` must match the
        // bound set exactly -- so two sets differing only in slot 1 would otherwise share one
        // pipeline and fail validation on the second. Folded only when there are extra slots,
        // so every single-target key is unchanged.
        for (int slot = 1; slot < colorAttachmentCount && slot < 4; ++slot)
            key = key * 31u + static_cast<std::uint64_t>(mrtColorFormats[slot]);
        return key;
    }

    // The level-of-detail lower clamp a SamplerState's MaxMipLevel names, within the upper clamp of
    // 32 (see FillWGPUSamplerDescriptor): XNA writes the field to Direct3D 9's UNSIGNED
    // MaxMipLevel, so a negative value names the last level, as 99 does.
    [[nodiscard]] inline int WGPUSamplerMinLodLevel(int maxMipLevel)
    {
        return maxMipLevel < 0 ? 32 : std::min(maxMipLevel, 32);
    }

    // The slot-sampler cache key: every FillWGPUSamplerDescriptor input as the descriptor reads it,
    // so two sampler states share one native sampler exactly when their descriptors agree. A byte of
    // the raw MaxMipLevel aliased 256 onto 0 although their descriptors differ (RV-MIX3 review).
    // 64-bit because filter, U, V, W, anisotropy and the level take a byte each (WEBGPU-160/161).
    [[nodiscard]] inline std::uint64_t WGPUSlotSamplerKey(int filter, int addressU, int addressV,
                                                          int addressW, int maxMipLevel,
                                                          int maxAnisotropy)
    {
        return (static_cast<std::uint64_t>(filter) & 0xFFull)
             | ((static_cast<std::uint64_t>(addressU) & 0xFFull) << 8)
             | ((static_cast<std::uint64_t>(addressV) & 0xFFull) << 16)
             | (static_cast<std::uint64_t>(std::clamp(maxAnisotropy, 1, 16)) << 24)
             | ((static_cast<std::uint64_t>(addressW) & 0xFFull) << 32)
             | (static_cast<std::uint64_t>(WGPUSamplerMinLodLevel(maxMipLevel)) << 40);
    }

    // WEBGPU-82: fills a full WGPUSamplerDescriptor (address modes, mag/min/mipmap filter,
    // anisotropy) from a real XNA SamplerState -- shared translation used by
    // GetOrCreateSlotSampler() (magFilter/minFilter split per XNA TextureFilter value).
    inline void FillWGPUSamplerDescriptor(WGPUSamplerDescriptor& descriptor, int filter, int addressU,
                                   int addressV, int addressW, int maxMipLevel,
                                   int maxAnisotropy)
    {
        // XNA TextureFilter: 0=Linear,1=Point,2=Anisotropic,3=LinearMipPoint,4=PointMipLinear,
        // 5=MinLinearMagPointMipLinear,6=MinLinearMagPointMipPoint,
        // 7=MinPointMagLinearMipLinear,8=MinPointMagLinearMipPoint.
        WGPUFilterMode magF = WGPUFilterMode_Linear;
        WGPUFilterMode minF = WGPUFilterMode_Linear;
        WGPUMipmapFilterMode mipMode = WGPUMipmapFilterMode_Linear;
        bool enableAniso = false;
        switch (filter)
        {
            case 1: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Nearest; break;
            case 2: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Linear;  enableAniso = true; break;
            case 3: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Nearest; break;
            case 4: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Linear;  break;
            case 5: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Linear;  break;
            case 6: magF = WGPUFilterMode_Nearest; minF = WGPUFilterMode_Linear;  mipMode = WGPUMipmapFilterMode_Nearest; break;
            case 7: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Linear;  break;
            case 8: magF = WGPUFilterMode_Linear;  minF = WGPUFilterMode_Nearest; mipMode = WGPUMipmapFilterMode_Nearest; break;
            default: break; // Linear (0)
        }
        descriptor.addressModeU = ToAddressMode(addressU);
        descriptor.addressModeV = ToAddressMode(addressV);
        // WEBGPU-160: the third axis is the caller's, through the same ordinal table as U and
        // V. It was hardcoded to ClampToEdge, which discarded SamplerState.AddressW in silence
        // -- and for a Texture3D that is the only axis that matters.
        descriptor.addressModeW = ToAddressMode(addressW);
        descriptor.magFilter = magF;
        descriptor.minFilter = minF;
        descriptor.mipmapFilter = mipMode;
        // WEBGPU-161: XNA's MaxMipLevel is the highest-resolution level sampling may use, so
        // it is a LOWER clamp on the level-of-detail -- the same mapping the reference renderer
        // makes to GL_TEXTURE_MIN_LOD and FNA3D's SDLGPU driver to min_lod. 0 is XNA's default
        // and means no restriction, which is also lodMinClamp's own default.
        // WGF-0005: bounded by the upper clamp. WebGPU
        // refuses a sampler whose lodMinClamp exceeds its lodMaxClamp -- "Invalid lodMaxClamp:
        // 32. Must be greater or equal to lodMinClamp (which is 99)" -- and an invalid sampler
        // then invalidates the bind group that names it, which invalidates the draw, which
        // makes wgpuQueueSubmit PANIC in a non-unwinding frame and abort the process. XNA
        // accepts any MaxMipLevel and simply has no level that far down the chain, so the
        // faithful answer is the last level a sampler can express, not a dead process.
        descriptor.lodMaxClamp = 32.0f;
        // A NEGATIVE MaxMipLevel is not "no restriction": XNA writes the field to Direct3D 9's
        // UNSIGNED MaxMipLevel, so -1 arrives as a very large level and names the LAST one,
        // exactly as 99 does. Folding it to 0 with std::max selected the most detailed level
        // instead -- the opposite end of the chain.
        descriptor.lodMinClamp = static_cast<float>(WGPUSamplerMinLodLevel(maxMipLevel));
        // WebGPU requires maxAnisotropy==1 unless mag/min/mipmap are all Linear (true only for
        // filter==2 above).
        descriptor.maxAnisotropy = enableAniso
            ? static_cast<std::uint16_t>(std::clamp(maxAnisotropy, 1, 16))
            : 1;
    }

    // WEBGPU-145/149: the shared primary uniform block is 160 bytes = the
    // 128-byte MVP/material/light0 layout plus the 32-byte FNA fog tail (fogColor + fogVector).
    // EVERY consumer of the shared coloredBindGroupLayout_/litBindGroupLayout_/skinnedBindGroupLayout_
    // binding-0 now binds exactly this many bytes (all *DrawCommand::uniforms are std::array<float,40>),
    // so the bind-group layouts declare this as their binding-0 minBindingSize -- the strongest
    // validation, not the weaker minBindingSize=0.
    // WEBGPU-205: 176 bytes. The 160-byte block was exactly full -- 128 base plus WEBGPU-149's
    // 32-byte fog tail -- so the per-slot LOD bias got its own 16-byte tail rather than being
    // squeezed into a field that already means something else.
    inline constexpr std::uint64_t kPrimaryUniformByteSize = 44u * sizeof(float);
    static_assert(kPrimaryUniformByteSize == 176u,
                  "primary WebGPU UBO must be 176 bytes (128 base + 32 fog tail + 16 bias tail)");

    // The FillExtUniforms() byte layout for DrawColoredPrimitives(): this path carries no
    // BasicEffect diffuse/VertexColorEnabled (no GpuDrawParams at all), so it preserves the
    // historical XNA behaviour of outputting the raw vertex colours unmodified
    // (diffuseColor=white, vertexColorEnabled=true), everything else left zeroed.
    inline void FillColoredUniforms(std::array<float, 44>& out, const Matrix& world, const Matrix& view,
                             const Matrix& projection)
    {
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(out.data());
        out[16] = 1.0f; out[17] = 1.0f; out[18] = 1.0f; out[19] = 1.0f;
        for (int i = 20; i < 31; ++i) out[i] = 0.0f;
        out[31] = 1.0f;
        // WEBGPU-145: fog tail (fogColor.rgb [32..34] + pad [35]; fogVector [36..39]). This plain
        // DrawColoredPrimitives path carries no GpuDrawParams, so fog is disabled -- an all-zero
        // fogVector makes dot(pos, fogVector)=0 -> keep=1 -> no fog, matching FNA's own "reset the
        // fog vector to zero when fog is disabled" behaviour (EffectHelpers.SetWorldViewProjAndFog).
        for (int i = 32; i < 40; ++i) out[i] = 0.0f;
    }

    // Real GpuDrawParams this time, not DrawColoredPrimitives()'s hardcoded
    // white/vertex-color-always-true values -- used by DrawPrimitivesEx()'s stride-16 dispatch
    // so a BasicEffect draw's real DiffuseColor/VertexColorEnabled actually reach the shader.
    inline void FillExtUniforms(std::array<float, 44>& out, const Matrix& wvp, const GpuDrawParams& p)
    {
        wvp.ToColumnMajor(out.data());
        out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
        out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
        out[20] = p.ambientColor[0]; out[21] = p.ambientColor[1]; out[22] = p.ambientColor[2];
        out[23] = p.lightingEnabled ? 1.0f : 0.0f;
        out[24] = p.light0Dir[0]; out[25] = p.light0Dir[1]; out[26] = p.light0Dir[2];
        out[27] = p.textureEnabled ? 1.0f : 0.0f;
        out[28] = p.light0Diffuse[0]; out[29] = p.light0Diffuse[1]; out[30] = p.light0Diffuse[2];
        out[31] = p.vertexColorEnabled ? 1.0f : 0.0f;
        // WEBGPU-145: FNA fog tail. fogColor.rgb ([32..34]) + pad ([35]); the CPU-prepared FNA
        // view-space fog vector ([36..39], EffectHelpers.SetFogVector). Dotting fogVector with the
        // object-space position in the vertex shader yields FNA's fogFactor; all-zero disables fog
        // (dot->0->keep=1) and {0,0,0,1} is the degenerate fogStart==fogEnd (dot->1->keep=0) case.
        out[32] = p.fogColor[0]; out[33] = p.fogColor[1]; out[34] = p.fogColor[2]; out[35] = 0.0f;
        out[36] = p.fogVector[0]; out[37] = p.fogVector[1]; out[38] = p.fogVector[2]; out[39] = p.fogVector[3];
    }

    // Normal matrix = inverse(world3x3), via the cofactor/det shortcut, applied directly to
    // the already-GPU-space (dumped) world matrix -- mirrors
    // BgfxRenderer::ComputeNormalMatrix3x3() byte-for-byte (verified independently
    // against FNA's own Lighting.fxh: HLSL computes WorldInverseTranspose =
    // Transpose(Invert(World)) on the CPU and applies it as mul(normal, WorldInverseTranspose)
    // -- a row-vector multiply. Working through this codebase's established
    // dump(M) = M^T GPU-column-major convention for both World and the result reduces to
    // exactly this cofactor inverse of the dumped array, with no shader-side transpose.
    inline void ComputeNormalMatrix3x3(const float* w, float out[9])
    {
        const float a = w[0], d = w[1], g = w[2];
        const float b = w[4], e = w[5], h = w[6];
        const float c = w[8], f = w[9], i = w[10];
        const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
        out[0] = (e * i - f * h) * invDet; out[1] = -(b * i - c * h) * invDet; out[2] = (b * f - c * e) * invDet;
        out[3] = -(d * i - f * g) * invDet; out[4] = (a * i - c * g) * invDet; out[5] = -(a * f - c * d) * invDet;
        out[6] = (d * h - e * g) * invDet; out[7] = -(a * h - b * g) * invDet; out[8] = (a * e - b * d) * invDet;
    }

    // HND-TQ-25 D9: a classic stock shader transforms normals with its WorldInverseTranspose
    // parameter. The stock classes derive it from World (above); a clone of one hands its raw
    // value over in this same layout (GpuDrawParams::rawStockTransforms).
    inline void ComputeClassicNormalMatrix3x3(const GpuDrawParams& p, float out[9])
    {
        if (!p.rawStockTransforms)
        {
            ComputeNormalMatrix3x3(p.worldColMajor, out);
            return;
        }
        for (int i = 0; i < 9; ++i) out[i] = p.worldInverseTransposeColMajor[i];
    }

    // Secondary UBO for lit_textured3d.wgsl: DirectionalLight1/DirectionalLight2, EmissiveColor,
    // World (for world-space position), EyePosition, per-light SpecularColor, material
    // SpecularColor/Power, and the 3x3 normal matrix -- forwarded here since the primary
    // 160-byte uniform block (FillExtUniforms) carries MVP/material/light0 plus the fog tail
    // (WEBGPU-145). Fog itself rides in the primary Uniforms block (fogColor/fogVector),
    // consumed by the lit_textured3d shaders.
    inline void FillLitLightUniforms(std::array<float, 68>& out, const GpuDrawParams& p)
    {
        out[0] = p.light1Dir[0]; out[1] = p.light1Dir[1]; out[2] = p.light1Dir[2]; out[3] = 0.0f;
        out[4] = p.light1Diffuse[0]; out[5] = p.light1Diffuse[1]; out[6] = p.light1Diffuse[2]; out[7] = 0.0f;
        out[8] = p.light2Dir[0]; out[9] = p.light2Dir[1]; out[10] = p.light2Dir[2]; out[11] = 0.0f;
        out[12] = p.light2Diffuse[0]; out[13] = p.light2Diffuse[1]; out[14] = p.light2Diffuse[2]; out[15] = 0.0f;
        out[16] = p.emissiveColor[0]; out[17] = p.emissiveColor[1]; out[18] = p.emissiveColor[2]; out[19] = 0.0f;
        for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi];
        out[36] = p.eyePositionWorld[0]; out[37] = p.eyePositionWorld[1]; out[38] = p.eyePositionWorld[2]; out[39] = 0.0f;
        out[40] = p.light0Specular[0]; out[41] = p.light0Specular[1]; out[42] = p.light0Specular[2]; out[43] = 0.0f;
        out[44] = p.light1Specular[0]; out[45] = p.light1Specular[1]; out[46] = p.light1Specular[2]; out[47] = 0.0f;
        out[48] = p.light2Specular[0]; out[49] = p.light2Specular[1]; out[50] = p.light2Specular[2]; out[51] = 0.0f;
        out[52] = p.specularColor[0]; out[53] = p.specularColor[1]; out[54] = p.specularColor[2]; out[55] = p.specularPower;
        float normalMatrix[9];
        ComputeClassicNormalMatrix3x3(p, normalMatrix);
        out[56] = normalMatrix[0]; out[57] = normalMatrix[1]; out[58] = normalMatrix[2]; out[59] = 0.0f;
        out[60] = normalMatrix[3]; out[61] = normalMatrix[4]; out[62] = normalMatrix[5]; out[63] = 0.0f;
        out[64] = normalMatrix[6]; out[65] = normalMatrix[7]; out[66] = normalMatrix[8]; out[67] = 0.0f;
    }

    // env_map3d.wgsl's Transform UBO (group 0 binding 0): mvp + world, since WebGPU has no
    // push constants.
    inline void FillEnvMapTransform(std::array<float, 32>& out, const Matrix& mvp, const Matrix& world)
    {
        mvp.ToColumnMajor(out.data());
        world.ToColumnMajor(out.data() + 16);
    }

    // env_map3d.wgsl's EnvMapParams UBO (group 0 binding 1): the retired VULKAN renderer's
    // envMapUboData[] packing, field for field (cross-checked against
    // EasyGLRenderer::EnsureEnvMapped3DProgram()'s identical GLSL formula before porting), plus
    // a CPU-precomputed normal matrix at the tail (WGSL has no inverse() -- same reason
    // FillLitLightUniforms() precomputes its own).
    inline void FillEnvMapParams(std::array<float, 64>& out, const GpuDrawParams& p)
    {
        out[0] = p.eyePositionWorld[0]; out[1] = p.eyePositionWorld[1];
        out[2] = p.eyePositionWorld[2]; out[3] = 0.0f;
        out[4] = p.diffuseColor[0]; out[5] = p.diffuseColor[1];
        out[6] = p.diffuseColor[2]; out[7] = p.diffuseColor[3];
        out[8] = p.emissiveColor[0]; out[9] = p.emissiveColor[1];
        out[10] = p.emissiveColor[2]; out[11] = p.envMapAmount;
        out[12] = p.light0Dir[0]; out[13] = p.light0Dir[1]; out[14] = p.light0Dir[2]; out[15] = 0.0f;
        out[16] = p.light0Diffuse[0]; out[17] = p.light0Diffuse[1]; out[18] = p.light0Diffuse[2];
        out[19] = p.fresnelEnabled ? 1.0f : 0.0f;
        out[20] = p.envMapSpecular[0]; out[21] = p.envMapSpecular[1]; out[22] = p.envMapSpecular[2];
        out[23] = p.fresnelFactor;
        // The CPU-prepared FNA view-space fog vector is the only fog
        // representation uploaded here. `fogColor.w` remains explicit UBO padding.
        out[24] = p.fogColor[0]; out[25] = p.fogColor[1]; out[26] = p.fogColor[2]; out[27] = 0.0f;
        out[28] = p.fogVector[0]; out[29] = p.fogVector[1];
        out[30] = p.fogVector[2]; out[31] = p.fogVector[3];
        out[32] = p.light1Dir[0]; out[33] = p.light1Dir[1]; out[34] = p.light1Dir[2]; out[35] = 0.0f;
        out[36] = p.light1Diffuse[0]; out[37] = p.light1Diffuse[1]; out[38] = p.light1Diffuse[2]; out[39] = 0.0f;
        out[40] = p.light2Dir[0]; out[41] = p.light2Dir[1]; out[42] = p.light2Dir[2]; out[43] = 0.0f;
        out[44] = p.light2Diffuse[0]; out[45] = p.light2Diffuse[1]; out[46] = p.light2Diffuse[2]; out[47] = 0.0f;
        float normalMatrix[9];
        ComputeClassicNormalMatrix3x3(p, normalMatrix);
        out[48] = normalMatrix[0]; out[49] = normalMatrix[1]; out[50] = normalMatrix[2]; out[51] = 0.0f;
        out[52] = normalMatrix[3]; out[53] = normalMatrix[4]; out[54] = normalMatrix[5]; out[55] = 0.0f;
        out[56] = normalMatrix[6]; out[57] = normalMatrix[7]; out[58] = normalMatrix[8]; out[59] = 0.0f;
    }

    // AlphaTestEffect has no lighting, so this repurposes the [20..23]/[24] slots
    // (ambient/light0/vertexColorEnabled in FillExtUniforms) for {alphaRef, alphaTolerance,
    // passWeight, failWeight, vertexColorEnabled} instead, plus the 32-byte fog tail (160
    // total), so it still fits the existing coloredBindGroupLayout_ unchanged.
    inline void FillAlphaTestUniforms(std::array<float, 44>& out, const Matrix& wvp, const GpuDrawParams& p)
    {
        wvp.ToColumnMajor(out.data());
        out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
        out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
        out[20] = p.alphaTest[0]; out[21] = p.alphaTest[1];
        out[22] = p.alphaTest[2]; out[23] = p.alphaTest[3];
        out[24] = p.vertexColorEnabled ? 1.0f : 0.0f;
        for (int i = 25; i < 32; ++i) out[i] = 0.0f;
        // WEBGPU-146: AlphaTestEffect fog tail (same layout as FillExtUniforms; the WGSL blend is
        // added in the alpha_test3d shaders). AlphaTestEffect is a fog-capable FNA stock effect.
        out[32] = p.fogColor[0]; out[33] = p.fogColor[1]; out[34] = p.fogColor[2]; out[35] = 0.0f;
        out[36] = p.fogVector[0]; out[37] = p.fogVector[1]; out[38] = p.fogVector[2]; out[39] = p.fogVector[3];
    }

    // pbr3d.wgsl's third (small) uniform buffer: PBR factors/map scales plus glTF MASK coverage,
    // the only per-draw PBR-specific scalars not already covered by FillExtUniforms()'s
    // diffuseColor/ambientColor or FillLitLightUniforms()'s emissiveColor/world/eyePos.
    // GLTF-344: widened from 56 to 76 floats (304 bytes) for KHR_materials_specular's own two
    // inputs -- the UNCLAMPED dielectric F0 plus the specular factor, and two affine transform
    // rows per specular map. The unclamped value is the point: `specularColorTexture` multiplies
    // BEFORE the min(...,1), so a shader handed the pre-clamped F0 cannot reproduce the
    // extension's own equation. Float 76 is the glTF doubleSided sign (80 floats, 320 bytes).
    inline void FillPbrFactors(std::array<float, 80>& out, const GpuDrawParams& p)
    {
        out[0] = p.pbrMetallicFactor;
        out[1] = p.pbrRoughnessFactor;
        out[2] = p.pbrNormalScale;
        out[3] = p.pbrOcclusionStrength;
        out[4] = p.alphaTest[0];
        out[5] = p.alphaTest[1];
        out[6] = p.alphaTest[2];
        out[7] = p.alphaTest[3];
        out[8] = p.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f;
        out[9] = p.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f;
        out[10] = p.pbrEncodeOutputToSrgb ? 1.0f : 0.0f;
        // GLTF-344: w decodes the specular COLOUR sample from sRGB, the extension's own rule.
        out[11] = p.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f;
        out[12] = p.pbrDielectricF0[0];
        out[13] = p.pbrDielectricF0[1];
        out[14] = p.pbrDielectricF0[2];
        out[15] = p.pbrDielectricF90;
        for (int row = 0; row < 10; ++row)
            for (int component = 0; component < 4; ++component)
                out[16 + row * 4 + component] =
                    p.pbrTextureTransformRows[row][component];
        // GLTF-344: xyz = unclamped dielectric F0, w = specularFactor.
        out[56] = p.pbrDielectricF0Unclamped[0];
        out[57] = p.pbrDielectricF0Unclamped[1];
        out[58] = p.pbrDielectricF0Unclamped[2];
        out[59] = p.pbrSpecularFactor;
        for (int row = 0; row < 4; ++row)
            for (int component = 0; component < 4; ++component)
                out[60 + row * 4 + component] =
                    p.pbrSpecularTextureTransformRows[row][component];
        // glTF doubleSided, EasyGL's encoding: a mirroring World reverses the on-screen
        // winding, so front_facing then names the back face and the shader reads it the other
        // way round.
        out[76] = 0.0f;
        if (p.pbrDoubleSided)
        {
            const float* w = p.worldColMajor;
            const float det = w[0] * (w[5] * w[10] - w[9] * w[6])
                            - w[4] * (w[1] * w[10] - w[9] * w[2])
                            + w[8] * (w[1] * w[6] - w[5] * w[2]);
            out[76] = det < 0.0f ? -1.0f : 1.0f;
        }
    }

    // New bone-palette uniform buffer for the skinned shaders (skinned3d.wgsl/skinned_pbr3d.wgsl):
    // a small vec4f header (x = WeightsPerVertex, Task 895's 1/2/4 convention -- only the first
    // N weight/index pairs are summed) followed by all 72 column-major bone matrices, XNA's
    // SkinnedEffect.MaxBones. (EasyGL packs the same 72 bones as three vec4 rows each to stay
    // inside GLES's 256-vector floor, CNA-ISLAND-002; a WebGPU uniform buffer has no such floor,
    // so whole matrices stay here.) Always copies the full fixed
    // 72-entry array regardless of GpuDrawParams::boneCount: the tail is guaranteed zero
    // (GpuDrawParams::boneTransforms's own default member initializer, since
    // SkinnedEffect::FillGpuDrawParams only overwrites the first boneCount entries), and
    // well-formed vertex data never indexes past boneCount anyway.
    inline void FillSkinningParams(std::array<float, 4 + 72 * 16>& out, const GpuDrawParams& p)
    {
        out[0] = static_cast<float>(p.weightsPerVertex);
        out[1] = 0.0f; out[2] = 0.0f; out[3] = 0.0f;
        for (int i = 0; i < 72 * 16; ++i)
            out[4 + i] = p.boneTransforms[i];
    }
}
