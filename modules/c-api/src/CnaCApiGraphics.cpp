// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiGraphicsStateDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
// The renderer contract is an internal header written for the graphics module's own warning
// settings, not for this library's `-Wall -Wextra -Werror` (CBIND-052A). It is included only for
// the ShaderDialectEXT enumerators the dialect route names, so the one diagnostic its defaulted
// interface methods trip is suppressed for the include alone rather than for this file.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <optional>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetOwnedSpriteFont;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ToNativeBlendState;
using CNA::C::Detail::ToNativeDepthStencilState;
using CNA::C::Detail::ToNativeRasterizerState;
using CNA::C::Detail::ToNativeSamplerState;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteEffects;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr uint32_t StructureVersion = UINT32_C(1);

// The C alias carries the native mirroring bits verbatim, so a C caller's own bitwise operators
// replace the native operator overloads without a conversion step.
static_assert(
    static_cast<uint32_t>(SpriteEffects::None) == CNA_SPRITE_EFFECT_NONE &&
    static_cast<uint32_t>(SpriteEffects::FlipHorizontally) ==
        CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY &&
    static_cast<uint32_t>(SpriteEffects::FlipVertically) ==
        CNA_SPRITE_EFFECT_FLIP_VERTICALLY &&
    static_cast<uint32_t>(
        SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically) ==
        (CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY));

struct SpriteBatchResource final {
    std::shared_ptr<SpriteBatch> value;
    CNA_Handle parentGame;
    bool begun;
    std::vector<std::shared_ptr<Texture2DResource>> retainedTextures;
};

struct ResolvedSpriteCommand final {
    CNA_SpriteCommand value;
    std::shared_ptr<Texture2DResource> texture;
};

[[nodiscard]] bool TryMapSpriteSortMode(
    const CNA_SpriteSortMode mode,
    SpriteSortMode* const outMode) noexcept
{
    if (outMode == nullptr) {
        return false;
    }
    switch (mode) {
        case CNA_SPRITE_SORT_MODE_DEFERRED:
            *outMode = SpriteSortMode::Deferred;
            return true;
        case CNA_SPRITE_SORT_MODE_IMMEDIATE:
            *outMode = SpriteSortMode::Immediate;
            return true;
        case CNA_SPRITE_SORT_MODE_TEXTURE:
            *outMode = SpriteSortMode::Texture;
            return true;
        case CNA_SPRITE_SORT_MODE_BACK_TO_FRONT:
            *outMode = SpriteSortMode::BackToFront;
            return true;
        case CNA_SPRITE_SORT_MODE_FRONT_TO_BACK:
            *outMode = SpriteSortMode::FrontToBack;
            return true;
        default:
            // XNA's Begin stores an unnamed sort enum rather than validating it, and its flush
            // path reaches every comparison by equality, so the value sorts like Deferred and is
            // still readable back. Refusing it here made a C caller unable to reproduce that,
            // which downstream measured as a behavioural divergence rather than a safety check.
            // SpriteSortMode fixes its underlying type for exactly this cast.
            *outMode = static_cast<SpriteSortMode>(static_cast<int32_t>(mode));
            return true;
    }
}

/// The C identity for a renderer's declared shading dialect.
///
/// An exhaustive switch with no `default`, so `-Werror=switch` catches a dialect appended to the
/// canonical enumeration that nothing here answers -- the failure mode CBIND-052A found in the
/// renderer identity table and fixed the same way.
[[nodiscard]] CNA_ShaderDialect MapShaderDialect(
    const CNA::Internal::Renderers::ShaderDialectEXT dialect) noexcept
{
    using CNA::Internal::Renderers::ShaderDialectEXT;
    switch (dialect) {
        case ShaderDialectEXT::Unknown:     return CNA_SHADER_DIALECT_UNKNOWN;
        case ShaderDialectEXT::GlslDesktop: return CNA_SHADER_DIALECT_GLSL_DESKTOP;
        case ShaderDialectEXT::GlslEs:      return CNA_SHADER_DIALECT_GLSL_ES;
        case ShaderDialectEXT::GlslVulkan:  return CNA_SHADER_DIALECT_GLSL_VULKAN;
        case ShaderDialectEXT::Hlsl:        return CNA_SHADER_DIALECT_HLSL;
        case ShaderDialectEXT::Msl:         return CNA_SHADER_DIALECT_MSL;
        case ShaderDialectEXT::Wgsl:        return CNA_SHADER_DIALECT_WGSL;
    }
    return CNA_SHADER_DIALECT_UNKNOWN;
}

[[nodiscard]] bool TryMapGraphicsCapability(
    const CNA_GraphicsCapability capability,
    CNA::GraphicsCapability* const outCapability) noexcept
{
    if (outCapability == nullptr) {
        return false;
    }
    switch (capability) {
        case CNA_GRAPHICS_CAPABILITY_THREE_D:
            *outCapability = CNA::GraphicsCapability::ThreeD;
            return true;
        case CNA_GRAPHICS_CAPABILITY_DEPTH_STENCIL_BUFFER:
            *outCapability = CNA::GraphicsCapability::DepthStencilBuffer;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTI_SAMPLE_ANTI_ALIASING:
            *outCapability = CNA::GraphicsCapability::MultiSampleAntiAliasing;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTIPLE_RENDER_TARGETS:
            *outCapability = CNA::GraphicsCapability::MultipleRenderTargets;
            return true;
        case CNA_GRAPHICS_CAPABILITY_ANISOTROPIC_FILTERING:
            *outCapability = CNA::GraphicsCapability::AnisotropicFiltering;
            return true;
        case CNA_GRAPHICS_CAPABILITY_WIRE_FRAME:
            *outCapability = CNA::GraphicsCapability::WireFrame;
            return true;
        case CNA_GRAPHICS_CAPABILITY_OCCLUSION_QUERY:
            *outCapability = CNA::GraphicsCapability::OcclusionQuery;
            return true;
        case CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS:
            *outCapability = CNA::GraphicsCapability::CustomEffects;
            return true;
        case CNA_GRAPHICS_CAPABILITY_TEXTURE_3D:
            *outCapability = CNA::GraphicsCapability::Texture3D;
            return true;
        case CNA_GRAPHICS_CAPABILITY_MULTI_STREAM_VERTEX_INPUT:
            *outCapability = CNA::GraphicsCapability::MultiStreamVertexInput;
            return true;
        case CNA_GRAPHICS_CAPABILITY_INSTANCING:
            *outCapability = CNA::GraphicsCapability::Instancing;
            return true;
        case CNA_GRAPHICS_CAPABILITY_STENCIL_BUFFER:
            *outCapability = CNA::GraphicsCapability::StencilBuffer;
            return true;
        case CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING:
            *outCapability = CNA::GraphicsCapability::AdditiveBlending;
            return true;
        case CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS:
            *outCapability = CNA::GraphicsCapability::CompiledEffects;
            return true;
        case CNA_GRAPHICS_CAPABILITY_FLOAT_RENDER_TARGETS:
            *outCapability = CNA::GraphicsCapability::FloatRenderTargets;
            return true;
        case CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_RENDER_TARGETS:
            *outCapability = CNA::GraphicsCapability::HalfFloatRenderTargets;
            return true;
        case CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_TEXTURE_LINEAR_FILTERING:
            *outCapability = CNA::GraphicsCapability::HalfFloatTextureLinearFiltering;
            return true;
        case CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS:
            *outCapability = CNA::GraphicsCapability::ComputeShaders;
            return true;
        case CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW:
            *outCapability = CNA::GraphicsCapability::IndirectDraw;
            return true;
        default:
            return false;
    }
}

// The reverse direction exists for its exhaustiveness rather than for a caller: it has no
// `default`, so a capability appended to CNA::GraphicsCapability stops this translation unit
// compiling instead of silently never reaching the C identity space.
[[nodiscard]] CNA_GraphicsCapability MapGraphicsCapabilityToC(
    const CNA::GraphicsCapability capability) noexcept
{
    switch (capability) {
        case CNA::GraphicsCapability::ThreeD: return CNA_GRAPHICS_CAPABILITY_THREE_D;
        case CNA::GraphicsCapability::DepthStencilBuffer:
            return CNA_GRAPHICS_CAPABILITY_DEPTH_STENCIL_BUFFER;
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            return CNA_GRAPHICS_CAPABILITY_MULTI_SAMPLE_ANTI_ALIASING;
        case CNA::GraphicsCapability::MultipleRenderTargets:
            return CNA_GRAPHICS_CAPABILITY_MULTIPLE_RENDER_TARGETS;
        case CNA::GraphicsCapability::AnisotropicFiltering:
            return CNA_GRAPHICS_CAPABILITY_ANISOTROPIC_FILTERING;
        case CNA::GraphicsCapability::WireFrame: return CNA_GRAPHICS_CAPABILITY_WIRE_FRAME;
        case CNA::GraphicsCapability::OcclusionQuery:
            return CNA_GRAPHICS_CAPABILITY_OCCLUSION_QUERY;
        case CNA::GraphicsCapability::CustomEffects:
            return CNA_GRAPHICS_CAPABILITY_CUSTOM_EFFECTS;
        case CNA::GraphicsCapability::Texture3D: return CNA_GRAPHICS_CAPABILITY_TEXTURE_3D;
        case CNA::GraphicsCapability::MultiStreamVertexInput:
            return CNA_GRAPHICS_CAPABILITY_MULTI_STREAM_VERTEX_INPUT;
        case CNA::GraphicsCapability::Instancing: return CNA_GRAPHICS_CAPABILITY_INSTANCING;
        case CNA::GraphicsCapability::StencilBuffer:
            return CNA_GRAPHICS_CAPABILITY_STENCIL_BUFFER;
        case CNA::GraphicsCapability::AdditiveBlending:
            return CNA_GRAPHICS_CAPABILITY_ADDITIVE_BLENDING;
        case CNA::GraphicsCapability::CompiledEffects:
            return CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS;
        case CNA::GraphicsCapability::FloatRenderTargets:
            return CNA_GRAPHICS_CAPABILITY_FLOAT_RENDER_TARGETS;
        case CNA::GraphicsCapability::HalfFloatRenderTargets:
            return CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_RENDER_TARGETS;
        case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
            return CNA_GRAPHICS_CAPABILITY_HALF_FLOAT_TEXTURE_LINEAR_FILTERING;
        case CNA::GraphicsCapability::ComputeShaders:
            return CNA_GRAPHICS_CAPABILITY_COMPUTE_SHADERS;
        case CNA::GraphicsCapability::IndirectDraw:
            return CNA_GRAPHICS_CAPABILITY_INDIRECT_DRAW;
    }
    return CNA_GRAPHICS_CAPABILITY_MAXIMUM + UINT32_C(1);
}

static_assert(
    static_cast<uint32_t>(CNA::RendererFeature::ThreeDimensionalPipeline) ==
        CNA_RENDERER_FEATURE_THREE_DIMENSIONAL_PIPELINE &&
    static_cast<uint32_t>(CNA::RendererFeature::ShaderEffectSourceExecution) ==
        CNA_RENDERER_FEATURE_SHADER_EFFECT_SOURCE_EXECUTION &&
    static_cast<uint32_t>(CNA::RendererFeature::ComputeImageBinding) ==
        CNA_RENDERER_FEATURE_COMPUTE_IMAGE_BINDING &&
    static_cast<uint32_t>(CNA::RendererFeature::ShaderDialectWgsl) ==
        CNA_RENDERER_FEATURE_MAXIMUM &&
    static_cast<uint32_t>(CNA::RendererFeature::Count) ==
        CNA_RENDERER_FEATURE_MAXIMUM + UINT32_C(1));

static_assert(
    static_cast<uint32_t>(CNA::RendererLimit::MaxTextureDimension) ==
        CNA_RENDERER_LIMIT_MAX_TEXTURE_DIMENSION &&
    static_cast<uint32_t>(CNA::RendererLimit::MaxComputeWorkGroupInvocations) ==
        CNA_RENDERER_LIMIT_MAX_COMPUTE_WORK_GROUP_INVOCATIONS &&
    static_cast<uint32_t>(CNA::RendererLimit::MaxVertexShaderStorageBlocks) ==
        CNA_RENDERER_LIMIT_MAXIMUM &&
    static_cast<uint32_t>(CNA::RendererLimit::Count) ==
        CNA_RENDERER_LIMIT_MAXIMUM + UINT32_C(1));

[[nodiscard]] bool TryMapRendererFeature(
    const CNA_RendererFeature feature,
    CNA::RendererFeature* const outFeature) noexcept
{
    if (outFeature == nullptr || feature > CNA_RENDERER_FEATURE_MAXIMUM) {
        return false;
    }
    *outFeature = static_cast<CNA::RendererFeature>(feature);
    return true;
}

[[nodiscard]] bool TryMapRendererLimit(
    const CNA_RendererLimit limit,
    CNA::RendererLimit* const outLimit) noexcept
{
    if (outLimit == nullptr || limit > CNA_RENDERER_LIMIT_MAXIMUM) {
        return false;
    }
    *outLimit = static_cast<CNA::RendererLimit>(limit);
    return true;
}

[[nodiscard]] CNA_RendererFeatureSupport MapRendererFeatureSupport(
    const CNA::RendererFeatureSupport support) noexcept
{
    switch (support) {
        case CNA::RendererFeatureSupport::Unknown:
            return CNA_RENDERER_FEATURE_SUPPORT_UNKNOWN;
        case CNA::RendererFeatureSupport::Unsupported:
            return CNA_RENDERER_FEATURE_SUPPORT_UNSUPPORTED;
        case CNA::RendererFeatureSupport::Supported:
            return CNA_RENDERER_FEATURE_SUPPORT_SUPPORTED;
        case CNA::RendererFeatureSupport::Restricted:
            return CNA_RENDERER_FEATURE_SUPPORT_RESTRICTED;
    }
    return CNA_RENDERER_FEATURE_SUPPORT_UNKNOWN;
}

[[nodiscard]] CNA_GraphicsRendererType MapGraphicsRendererType(
    const CNA::GraphicsRendererType rendererType) noexcept
{
    switch (rendererType) {
        case CNA::GraphicsRendererType::SdlRenderer: return CNA_GRAPHICS_RENDERER_SDL_RENDERER;
        case CNA::GraphicsRendererType::OpenGLES2: return CNA_GRAPHICS_RENDERER_OPENGLES2;
        case CNA::GraphicsRendererType::OpenGLES3: return CNA_GRAPHICS_RENDERER_OPENGLES3;
        case CNA::GraphicsRendererType::OpenGL33: return CNA_GRAPHICS_RENDERER_OPENGL33;
        case CNA::GraphicsRendererType::WebGL1: return CNA_GRAPHICS_RENDERER_WEBGL1;
        case CNA::GraphicsRendererType::WebGL2: return CNA_GRAPHICS_RENDERER_WEBGL2;
        case CNA::GraphicsRendererType::Bgfx: return CNA_GRAPHICS_RENDERER_BGFX;
        case CNA::GraphicsRendererType::Vulkan: return CNA_GRAPHICS_RENDERER_VULKAN;
        case CNA::GraphicsRendererType::WebGPU: return CNA_GRAPHICS_RENDERER_WEBGPU;
        case CNA::GraphicsRendererType::Magnum: return CNA_GRAPHICS_RENDERER_MAGNUM;
        case CNA::GraphicsRendererType::Headless: return CNA_GRAPHICS_RENDERER_HEADLESS;
        case CNA::GraphicsRendererType::Software: return CNA_GRAPHICS_RENDERER_SOFTWARE;
        case CNA::GraphicsRendererType::Stub: return CNA_GRAPHICS_RENDERER_STUB;
        case CNA::GraphicsRendererType::DirectX11: return CNA_GRAPHICS_RENDERER_DIRECTX11;
        case CNA::GraphicsRendererType::DirectX12: return CNA_GRAPHICS_RENDERER_DIRECTX12;
        case CNA::GraphicsRendererType::Direct2D: return CNA_GRAPHICS_RENDERER_DIRECT2D;
        case CNA::GraphicsRendererType::Canvas: return CNA_GRAPHICS_RENDERER_CANVAS;
        case CNA::GraphicsRendererType::HtmlDom: return CNA_GRAPHICS_RENDERER_HTML_DOM;
        case CNA::GraphicsRendererType::Skia: return CNA_GRAPHICS_RENDERER_SKIA;
        case CNA::GraphicsRendererType::Blend2D: return CNA_GRAPHICS_RENDERER_BLEND2D;
        case CNA::GraphicsRendererType::FreeDirect: return CNA_GRAPHICS_RENDERER_FREEDIRECT;
        case CNA::GraphicsRendererType::DirectX9: return CNA_GRAPHICS_RENDERER_DIRECTX9;
        case CNA::GraphicsRendererType::DirectX1: return CNA_GRAPHICS_RENDERER_DIRECTX1;
        case CNA::GraphicsRendererType::DirectX2: return CNA_GRAPHICS_RENDERER_DIRECTX2;
        case CNA::GraphicsRendererType::DirectX3: return CNA_GRAPHICS_RENDERER_DIRECTX3;
        case CNA::GraphicsRendererType::DirectX5: return CNA_GRAPHICS_RENDERER_DIRECTX5;
        case CNA::GraphicsRendererType::DirectX6: return CNA_GRAPHICS_RENDERER_DIRECTX6;
        case CNA::GraphicsRendererType::DirectX7: return CNA_GRAPHICS_RENDERER_DIRECTX7;
        case CNA::GraphicsRendererType::DirectX8: return CNA_GRAPHICS_RENDERER_DIRECTX8;
        case CNA::GraphicsRendererType::DirectX10: return CNA_GRAPHICS_RENDERER_DIRECTX10;
        case CNA::GraphicsRendererType::SdlGpu: return CNA_GRAPHICS_RENDERER_SDL_GPU;
        case CNA::GraphicsRendererType::OpenGLES1: return CNA_GRAPHICS_RENDERER_OPENGLES1;
        case CNA::GraphicsRendererType::OpenGL4: return CNA_GRAPHICS_RENDERER_OPENGL4;
        case CNA::GraphicsRendererType::OpenGL1: return CNA_GRAPHICS_RENDERER_OPENGL1;
        case CNA::GraphicsRendererType::OpenGL2: return CNA_GRAPHICS_RENDERER_OPENGL2;
        case CNA::GraphicsRendererType::Wicked: return CNA_GRAPHICS_RENDERER_WICKED;
        case CNA::GraphicsRendererType::Sokol: return CNA_GRAPHICS_RENDERER_SOKOL;
        case CNA::GraphicsRendererType::Diligent: return CNA_GRAPHICS_RENDERER_DILIGENT;
        case CNA::GraphicsRendererType::Glide: return CNA_GRAPHICS_RENDERER_GLIDE;
        case CNA::GraphicsRendererType::Gdi: return CNA_GRAPHICS_RENDERER_GDI;
        case CNA::GraphicsRendererType::Llgl: return CNA_GRAPHICS_RENDERER_LLGL;
        case CNA::GraphicsRendererType::Metal: return CNA_GRAPHICS_RENDERER_METAL;
        case CNA::GraphicsRendererType::Fna3d: return CNA_GRAPHICS_RENDERER_FNA3D;
        case CNA::GraphicsRendererType::SvgDom: return CNA_GRAPHICS_RENDERER_SVG_DOM;
        case CNA::GraphicsRendererType::OpenVg: return CNA_GRAPHICS_RENDERER_OPENVG;
        case CNA::GraphicsRendererType::PortableGL: return CNA_GRAPHICS_RENDERER_PORTABLEGL;
        case CNA::GraphicsRendererType::TinyGL: return CNA_GRAPHICS_RENDERER_TINYGL;
        case CNA::GraphicsRendererType::Igl: return CNA_GRAPHICS_RENDERER_IGL;
        case CNA::GraphicsRendererType::PixiJs: return CNA_GRAPHICS_RENDERER_PIXIJS;
        case CNA::GraphicsRendererType::NanoVg: return CNA_GRAPHICS_RENDERER_NANOVG;
    }
    return CNA_GRAPHICS_RENDERER_UNKNOWN;
}

[[nodiscard]] CNA_SurfaceFormat MapSurfaceFormat(const SurfaceFormat format) noexcept
{
    switch (format) {
        case SurfaceFormat::Color: return CNA_SURFACE_FORMAT_COLOR;
        case SurfaceFormat::Bgr565: return CNA_SURFACE_FORMAT_BGR565;
        case SurfaceFormat::Bgra5551: return CNA_SURFACE_FORMAT_BGRA5551;
        case SurfaceFormat::Bgra4444: return CNA_SURFACE_FORMAT_BGRA4444;
        case SurfaceFormat::Dxt1: return CNA_SURFACE_FORMAT_DXT1;
        case SurfaceFormat::Dxt3: return CNA_SURFACE_FORMAT_DXT3;
        case SurfaceFormat::Dxt5: return CNA_SURFACE_FORMAT_DXT5;
        case SurfaceFormat::NormalizedByte2: return CNA_SURFACE_FORMAT_NORMALIZED_BYTE2;
        case SurfaceFormat::NormalizedByte4: return CNA_SURFACE_FORMAT_NORMALIZED_BYTE4;
        case SurfaceFormat::Rgba1010102: return CNA_SURFACE_FORMAT_RGBA1010102;
        case SurfaceFormat::Rg32: return CNA_SURFACE_FORMAT_RG32;
        case SurfaceFormat::Rgba64: return CNA_SURFACE_FORMAT_RGBA64;
        case SurfaceFormat::Alpha8: return CNA_SURFACE_FORMAT_ALPHA8;
        case SurfaceFormat::Single: return CNA_SURFACE_FORMAT_SINGLE;
        case SurfaceFormat::Vector2: return CNA_SURFACE_FORMAT_VECTOR2;
        case SurfaceFormat::Vector4: return CNA_SURFACE_FORMAT_VECTOR4;
        case SurfaceFormat::HalfSingle: return CNA_SURFACE_FORMAT_HALF_SINGLE;
        case SurfaceFormat::HalfVector2: return CNA_SURFACE_FORMAT_HALF_VECTOR2;
        case SurfaceFormat::HalfVector4: return CNA_SURFACE_FORMAT_HALF_VECTOR4;
        case SurfaceFormat::HdrBlendable: return CNA_SURFACE_FORMAT_HDR_BLENDABLE;
        case SurfaceFormat::ColorBgraEXT: return CNA_SURFACE_FORMAT_COLOR_BGRA_EXT;
        case SurfaceFormat::ColorSrgbEXT: return CNA_SURFACE_FORMAT_COLOR_SRGB_EXT;
        case SurfaceFormat::Dxt5SrgbEXT: return CNA_SURFACE_FORMAT_DXT5_SRGB_EXT;
        case SurfaceFormat::Bc7EXT: return CNA_SURFACE_FORMAT_BC7_EXT;
        case SurfaceFormat::Bc7SrgbEXT: return CNA_SURFACE_FORMAT_BC7_SRGB_EXT;
        case SurfaceFormat::ByteEXT: return CNA_SURFACE_FORMAT_BYTE_EXT;
        case SurfaceFormat::UShortEXT: return CNA_SURFACE_FORMAT_USHORT_EXT;
    }
    return CNA_SURFACE_FORMAT_COLOR;
}

[[nodiscard]] CNA_GraphicsCapabilityFlags GetGraphicsCapabilityFlags(
    GraphicsDevice& graphicsDevice)
{
    CNA_GraphicsCapabilityFlags flags = UINT64_C(0);
    for (CNA_GraphicsCapability capability = CNA_GRAPHICS_CAPABILITY_THREE_D;
         capability <= CNA_GRAPHICS_CAPABILITY_MAXIMUM;
         ++capability) {
        CNA::GraphicsCapability nativeCapability{};
        if (!TryMapGraphicsCapability(capability, &nativeCapability)) {
            continue;
        }
        if (graphicsDevice.SupportsCapability(nativeCapability)) {
            flags |= UINT64_C(1) << MapGraphicsCapabilityToC(nativeCapability);
        }
    }
    return flags;
}

[[nodiscard]] CNA_Result GetTexture2D(
    const CNA_Handle handle,
    std::shared_ptr<Texture2DResource>* const outTexture)
{
    return CNA::C::Detail::GetOwnedTexture2D(handle, outTexture);
}

[[nodiscard]] CNA_Result GetSpriteBatch(
    const CNA_Handle handle,
    std::shared_ptr<SpriteBatchResource>* const outSpriteBatch)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle,
        ObjectKind::SpriteBatch,
        outSpriteBatch);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned SpriteBatch handle is invalid for this call.");
}

void ReleaseBatchTextureReferences(SpriteBatchResource& spriteBatch) noexcept
{
    for (const std::shared_ptr<Texture2DResource>& texture : spriteBatch.retainedTextures) {
        if (texture->activeBatchReferenceCount != 0U) {
            --texture->activeBatchReferenceCount;
        }
    }
    spriteBatch.retainedTextures.clear();
}

[[nodiscard]] uint64_t GetTexturePixelCount(const Texture2D& texture) noexcept
{
    return static_cast<uint64_t>(texture.getWidthProperty()) *
        static_cast<uint64_t>(texture.getHeightProperty());
}

} // namespace

namespace CNA::C::Detail {

namespace {

CNA_Result CreateOwnedTexture2DWithKind(
    std::shared_ptr<Texture2D> texture,
    const CNA_Handle parentGame,
    const ObjectKind kind,
    CNA_Handle* const outTexture)
{
    if (texture == nullptr || outTexture == nullptr ||
        (kind != ObjectKind::Texture2D && kind != ObjectKind::RenderTarget2D)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The native Texture2D ownership transfer is invalid.");
    }
    *outTexture = CNA_INVALID_HANDLE;
    const auto resource = std::make_shared<Texture2DResource>(
        Texture2DResource{std::move(texture), parentGame, 0U, 0U, 0U, 0U});
    const CNA_Result result = GetRuntimeHandles().Create(kind, resource, outTexture);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Texture2D handle could not be created.");
    }
    if (parentGame != CNA_INVALID_HANDLE) {
        AddOwnedGraphicsResourceFor(parentGame);
    }
    return CNA_RESULT_SUCCESS;
}

} // namespace

CNA_Result CreateOwnedTexture2D(
    std::shared_ptr<Texture2D> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    return CreateOwnedTexture2DWithKind(
        std::move(texture), parentGame, ObjectKind::Texture2D, outTexture);
}

CNA_Result CreateStandaloneTexture2D(
    std::shared_ptr<Texture2D> texture,
    CNA_Handle* const outTexture)
{
    return CreateOwnedTexture2DWithKind(
        std::move(texture), CNA_INVALID_HANDLE, ObjectKind::Texture2D, outTexture);
}

CNA_Result CreateOwnedRenderTarget2D(
    std::shared_ptr<Texture2D> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    return CreateOwnedTexture2DWithKind(
        std::move(texture), parentGame, ObjectKind::RenderTarget2D, outTexture);
}

CNA_Result CreateBorrowedRenderTarget2D(
    std::shared_ptr<Texture2D> texture,
    const CNA_Handle parentGame,
    std::shared_ptr<void> adapterLifetime,
    CNA_Handle* const outTexture)
{
    if (texture == nullptr || adapterLifetime == nullptr || outTexture == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The borrowed RenderTarget2D view is invalid.");
    }
    *outTexture = CNA_INVALID_HANDLE;
    const auto resource = std::make_shared<Texture2DResource>(Texture2DResource{
        std::move(texture),
        parentGame,
        0U,
        0U,
        0U,
        0U,
        std::move(adapterLifetime),
        0U,
        false,
        false});
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::RenderTarget2D, resource, outTexture);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The borrowed RenderTarget2D handle could not be created.");
}

CNA_Result GetOwnedTexture2D(
    const CNA_Handle handle,
    std::shared_ptr<Texture2DResource>* const outTexture)
{
    ObjectKind kind = ObjectKind::Unknown;
    CNA_Result result = GetRuntimeHandles().GetKind(handle, &kind);
    if (result != CNA_RESULT_SUCCESS ||
        (kind != ObjectKind::Texture2D && kind != ObjectKind::RenderTarget2D)) {
        if (result == CNA_RESULT_SUCCESS) {
            result = CNA_RESULT_INVALID_HANDLE;
        }
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Texture2D handle is invalid for this call.");
    }
    result = GetRuntimeHandles().Get(handle, kind, outTexture);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned Texture2D handle is invalid for this call.");
}


CNA_Result GetOwnedSpriteBatchValue(
    const CNA_Handle handle,
    Microsoft::Xna::Framework::Graphics::SpriteBatch** const outSpriteBatch)
{
    if (outSpriteBatch == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The SpriteBatch output is null.");
    }
    *outSpriteBatch = nullptr;
    std::shared_ptr<SpriteBatchResource> spriteBatch;
    if (const CNA_Result result = GetSpriteBatch(handle, &spriteBatch);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outSpriteBatch = spriteBatch->value.get();
    return CNA_RESULT_SUCCESS;
}

} // namespace CNA::C::Detail

CNA_Result cna_graphics_device_get_renderer_info(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RendererInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_RendererInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-info output structure is invalid.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        GraphicsDevice& nativeDevice = *graphicsDevice->value;
        const std::string_view rendererName = nativeDevice.GetGraphicsRendererName();
        const int maxTextureDimension = nativeDevice.GetMaxTextureDimension();
        if (maxTextureDimension < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native renderer reported a negative texture-dimension limit.");
        }
        const CNA_RendererInfo info = {
            .struct_size = sizeof(CNA_RendererInfo),
            .struct_version = StructureVersion,
            .renderer_name_byte_length = rendererName.size(),
            .capability_flags = GetGraphicsCapabilityFlags(nativeDevice),
            .renderer_type = MapGraphicsRendererType(nativeDevice.GetGraphicsRendererType()),
            .max_texture_dimension = static_cast<uint32_t>(maxTextureDimension)
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_renderer_name_size(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-name size output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = graphicsDevice->value->GetGraphicsRendererName().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_copy_renderer_name(
    const CNA_Handle graphicsDeviceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-name output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::string_view rendererName = graphicsDevice->value->GetGraphicsRendererName();
        *outBytes = rendererName.size();
        if (capacity < rendererName.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The renderer-name output buffer is too small.");
        }
        if (!rendererName.empty()) {
            std::memcpy(destination, rendererName.data(), rendererName.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_supports_capability(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsCapability capability,
    CNA_Bool* const outSupported)
{
    return CallWithExceptionBarrier([&]() {
        CNA::GraphicsCapability nativeCapability{};
        if (outSupported == nullptr || !TryMapGraphicsCapability(capability, &nativeCapability)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The graphics-capability query arguments are invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupported = graphicsDevice->value->SupportsCapability(nativeCapability)
            ? CNA_TRUE
            : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_renderer_feature_support_ext(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RendererFeature feature,
    CNA_RendererFeatureSupport* const outSupport)
{
    return CallWithExceptionBarrier([&]() {
        CNA::RendererFeature nativeFeature{};
        if (outSupport == nullptr || !TryMapRendererFeature(feature, &nativeFeature)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The detailed renderer-feature query arguments are invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outSupport = MapRendererFeatureSupport(
            graphicsDevice->value->GetRendererFeatureSupportEXT(nativeFeature));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_renderer_limit_ext(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RendererLimit limit,
    CNA_Bool* const outKnown,
    uint64_t* const outValue)
{
    return CallWithExceptionBarrier([&]() {
        CNA::RendererLimit nativeLimit{};
        if (outKnown == nullptr || outValue == nullptr ||
            !TryMapRendererLimit(limit, &nativeLimit)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-limit query arguments are invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA::RendererLimitValue value =
            graphicsDevice->value->GetRendererLimitEXT(nativeLimit);
        *outKnown = value.known ? CNA_TRUE : CNA_FALSE;
        *outValue = value.known ? value.value : UINT64_C(0);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_surface_format_support_ext(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_SurfaceFormat format,
    CNA_RendererFormatUsageFlags* const outKnownUsages,
    CNA_RendererFormatUsageFlags* const outSupportedUsages)
{
    return CallWithExceptionBarrier([&]() {
        if (format > CNA_SURFACE_FORMAT_USHORT_EXT || outKnownUsages == nullptr ||
            outSupportedUsages == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer surface-format query arguments are invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA::RendererFormatSupport support =
            graphicsDevice->value->GetRendererSurfaceFormatSupportEXT(
                static_cast<SurfaceFormat>(format));
        *outKnownUsages = support.knownUsages;
        *outSupportedUsages = support.supportedUsages;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_capability_report_size_ext(
    const CNA_Handle graphicsDeviceHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-capability report size output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = graphicsDevice->value->GetRendererCapabilityReportEXT().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_copy_capability_report_ext(
    const CNA_Handle graphicsDeviceHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The renderer-capability report output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const std::string_view report =
            graphicsDevice->value->GetRendererCapabilityReportEXT();
        *outBytes = report.size();
        if (capacity < report.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The renderer-capability report output buffer is too small.");
        }
        if (!report.empty()) {
            std::memcpy(destination, report.data(), report.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_shader_dialect_ext(
    const CNA_Handle graphicsDeviceHandle,
    CNA_ShaderDialect* const outDialect)
{
    return CallWithExceptionBarrier([&]() {
        if (outDialect == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The shader-dialect output is null.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outDialect = MapShaderDialect(graphicsDevice->value->GetShaderDialectEXT());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_backbuffer_info(
    const CNA_Handle graphicsDeviceHandle,
    CNA_BackBufferInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_BackBufferInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The backbuffer-info output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const auto& presentationParameters =
            graphicsDevice->value->getPresentationParametersProperty();
        const int width = presentationParameters.getBackBufferWidthProperty();
        const int height = presentationParameters.getBackBufferHeightProperty();
        if (width <= 0 || height <= 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native graphics device reported invalid backbuffer dimensions.");
        }
        *outInfo = CNA_BackBufferInfo{
            .struct_size = sizeof(CNA_BackBufferInfo),
            .struct_version = StructureVersion,
            .width = static_cast<uint32_t>(width),
            .height = static_cast<uint32_t>(height),
            .format = MapSurfaceFormat(presentationParameters.getBackBufferFormatProperty()),
            .reserved = 0U
        };
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_backbuffer_data_rgba8(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Color* const destination,
    const uint64_t capacity,
    uint64_t* const outPixels)
{
    return CallWithExceptionBarrier([&]() {
        if (outPixels == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The backbuffer readback output buffer is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const auto& presentationParameters =
            graphicsDevice->value->getPresentationParametersProperty();
        const int width = presentationParameters.getBackBufferWidthProperty();
        const int height = presentationParameters.getBackBufferHeightProperty();
        if (width <= 0 || height <= 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native graphics device reported invalid backbuffer dimensions.");
        }
        const uint64_t requiredPixels =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        *outPixels = requiredPixels;
        if (requiredPixels > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The backbuffer pixel count exceeds the native readback range.");
        }
        if (capacity < requiredPixels) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The backbuffer readback output buffer is too small.");
        }

        std::vector<Color> nativePixels;
        if (requiredPixels > nativePixels.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The backbuffer pixel count exceeds the native collection range.");
        }
        nativePixels.reserve(static_cast<std::size_t>(requiredPixels));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            nativePixels.emplace_back(
                UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0));
        }
        graphicsDevice->value->GetBackBufferData(
            nativePixels.data(),
            static_cast<int>(nativePixels.size()));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            destination[index] = CNA_Color{
                .r = nativePixels[index].getRProperty(),
                .g = nativePixels[index].getGProperty(),
                .b = nativePixels[index].getBProperty(),
                .a = nativePixels[index].getAProperty()
            };
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_create(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Texture2DCreateInfo* const createInfo,
    CNA_Handle* const outTexture)
{
    return CallWithExceptionBarrier([&]() {
        if (outTexture == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D output handle is null.");
        }
        *outTexture = CNA_INVALID_HANDLE;
        if (createInfo == nullptr || createInfo->struct_size < sizeof(CNA_Texture2DCreateInfo) ||
            createInfo->struct_version != StructureVersion || createInfo->width == 0U ||
            createInfo->height == 0U ||
            (createInfo->mip_map != CNA_FALSE && createInfo->mip_map != CNA_TRUE) ||
            createInfo->reserved[0] != 0U || createInfo->reserved[1] != 0U ||
            createInfo->reserved[2] != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D creation configuration is invalid.");
        }
        if (createInfo->format > CNA_SURFACE_FORMAT_USHORT_EXT) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D surface-format identity is invalid.");
        }
        if (!CNA::C::Detail::IsTexture2DFormatSupportedByBuild(createInfo->format)) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The Texture2D surface format is unavailable on the selected renderer.");
        }
        if (createInfo->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            createInfo->height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
            static_cast<uint64_t>(createInfo->width) * createInfo->height >
                static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The Texture2D dimensions exceed the native transfer range.");
        }

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int maxTextureDimension = graphicsDevice->value->GetMaxTextureDimension();
        if (maxTextureDimension < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native renderer reported a negative texture-dimension limit.");
        }
        if (createInfo->width > static_cast<uint32_t>(maxTextureDimension) ||
            createInfo->height > static_cast<uint32_t>(maxTextureDimension)) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                "The Texture2D dimensions exceed the active renderer's maximum.");
        }

        const auto texture = std::make_shared<Texture2D>(
            *graphicsDevice->value,
            static_cast<int>(createInfo->width),
            static_cast<int>(createInfo->height),
            createInfo->mip_map == CNA_TRUE,
            static_cast<SurfaceFormat>(createInfo->format));
        return CNA::C::Detail::CreateOwnedTexture2D(
            texture,
            graphicsDevice->parentGame,
            outTexture);
    });
}

CNA_Result cna_texture2d_get_info(
    const CNA_Handle textureHandle,
    CNA_Texture2DInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_Texture2DInfo) ||
            outInfo->struct_version != StructureVersion) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D info output structure is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Texture2DInfo info = {
            .struct_size = sizeof(CNA_Texture2DInfo),
            .struct_version = StructureVersion,
            .width = static_cast<uint32_t>(texture->value->getWidthProperty()),
            .height = static_cast<uint32_t>(texture->value->getHeightProperty()),
            .level_count = static_cast<uint32_t>(texture->value->getLevelCountProperty()),
            .format = MapSurfaceFormat(texture->value->getFormatProperty())
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_set_data_rgba8(
    const CNA_Handle textureHandle,
    const CNA_Color* const pixels,
    const uint64_t pixelCount)
{
    return CallWithExceptionBarrier([&]() {
        std::size_t ignoredByteCount = 0U;
        if (const CNA_Result validationResult = CheckedElementByteCount(
                pixels,
                pixelCount,
                sizeof(CNA_Color),
                &ignoredByteCount);
            validationResult != CNA_RESULT_SUCCESS) {
            return Fail(
                validationResult,
                ErrorCategoryForResult(validationResult),
                "The Texture2D upload buffer is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t requiredPixels = GetTexturePixelCount(*texture->value);
        if (pixelCount != requiredPixels) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D upload pixel count must equal width multiplied by height.");
        }

        std::vector<Color> nativePixels;
        nativePixels.reserve(static_cast<std::size_t>(requiredPixels));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            nativePixels.emplace_back(
                pixels[index].r,
                pixels[index].g,
                pixels[index].b,
                pixels[index].a);
        }
        texture->value->SetData(nativePixels.data(), static_cast<int>(nativePixels.size()));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_get_data_rgba8(
    const CNA_Handle textureHandle,
    CNA_Color* const destination,
    const uint64_t capacity,
    uint64_t* const outPixels)
{
    return CallWithExceptionBarrier([&]() {
        if (outPixels == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Texture2D readback buffer is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t requiredPixels = GetTexturePixelCount(*texture->value);
        *outPixels = requiredPixels;
        if (capacity < requiredPixels) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The Texture2D readback buffer is too small.");
        }

        std::vector<Color> nativePixels;
        nativePixels.reserve(static_cast<std::size_t>(requiredPixels));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            nativePixels.emplace_back(
                UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0));
        }
        texture->value->GetData(nativePixels.data(), static_cast<int>(nativePixels.size()));
        for (uint64_t index = 0U; index < requiredPixels; ++index) {
            destination[index] = CNA_Color{
                .r = nativePixels[index].getRProperty(),
                .g = nativePixels[index].getGProperty(),
                .b = nativePixels[index].getBProperty(),
                .a = nativePixels[index].getAProperty()
            };
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_texture2d_destroy(const CNA_Handle textureHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (texture->activeBatchReferenceCount != 0U ||
            texture->activeFontReferenceCount != 0U ||
            texture->activeEffectReferenceCount != 0U ||
            texture->activeModelReferenceCount != 0U ||
            texture->activeScopeReferenceCount != 0U) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The Texture2D is retained by an active SpriteBatch, SpriteFont, effect, model "
                "or render-target scope.");
        }
        if (texture->disposeAllowed) {
            texture->value->Dispose();
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(textureHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned Texture2D handle could not be released.");
        }
        if (texture->ownedResource && texture->parentGame != CNA_INVALID_HANDLE) {
            RemoveOwnedGraphicsResourceFor(texture->parentGame);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Handle* const outSpriteBatch)
{
    return CallWithExceptionBarrier([&]() {
        if (outSpriteBatch == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteBatch output handle is null.");
        }
        *outSpriteBatch = CNA_INVALID_HANDLE;

        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle,
                &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        const auto nativeSpriteBatch = std::make_shared<SpriteBatch>(*graphicsDevice->value);
        const auto resource = std::make_shared<SpriteBatchResource>(SpriteBatchResource{
            nativeSpriteBatch,
            graphicsDevice->parentGame,
            false,
            {}});
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SpriteBatch,
            resource,
            outSpriteBatch);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SpriteBatch handle could not be created.");
        }
        AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_begin(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteBatchBeginInfo* const beginInfo)
{
    return CallWithExceptionBarrier([&]() {
        SpriteSortMode nativeSortMode{};
        if (beginInfo == nullptr ||
            beginInfo->struct_size < sizeof(CNA_SpriteBatchBeginInfo) ||
            beginInfo->struct_version != StructureVersion || beginInfo->reserved != 0U ||
            !TryMapSpriteSortMode(beginInfo->sort_mode, &nativeSortMode)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteBatch begin configuration is invalid.");
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch already has an active begin/end interval.");
        }

        spriteBatch->value->Begin(
            nativeSortMode,
            Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend);
        spriteBatch->begun = true;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_begin_with_states(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteSortMode sortMode,
    const CNA_BlendState* const blendState,
    const CNA_SamplerState* const samplerState,
    const CNA_DepthStencilState* const depthStencilState,
    const CNA_RasterizerState* const rasterizerState)
{
    return CallWithExceptionBarrier([&]() {
        SpriteSortMode nativeSortMode{};
        Microsoft::Xna::Framework::Graphics::BlendState nativeBlendState;
        Microsoft::Xna::Framework::Graphics::SamplerState nativeSamplerState;
        Microsoft::Xna::Framework::Graphics::DepthStencilState nativeDepthStencilState;
        Microsoft::Xna::Framework::Graphics::RasterizerState nativeRasterizerState;
        if (!TryMapSpriteSortMode(sortMode, &nativeSortMode)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteBatch sort mode is invalid.");
        }
        if (const CNA_Result result = ToNativeBlendState(blendState, &nativeBlendState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeSamplerState(samplerState, &nativeSamplerState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeDepthStencilState(
                depthStencilState, &nativeDepthStencilState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeRasterizerState(
                rasterizerState, &nativeRasterizerState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch already has an active begin/end interval.");
        }

        spriteBatch->value->Begin(
            nativeSortMode,
            nativeBlendState,
            &nativeSamplerState,
            &nativeDepthStencilState,
            &nativeRasterizerState);
        spriteBatch->begun = true;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_begin_with_effect(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteSortMode sortMode,
    const CNA_BlendState* const blendState,
    const CNA_SamplerState* const samplerState,
    const CNA_DepthStencilState* const depthStencilState,
    const CNA_RasterizerState* const rasterizerState,
    const CNA_Handle effectHandle,
    const CNA_Matrix* const transformMatrix)
{
    return CallWithExceptionBarrier([&]() {
        SpriteSortMode nativeSortMode{};
        Microsoft::Xna::Framework::Graphics::BlendState nativeBlendState;
        Microsoft::Xna::Framework::Graphics::SamplerState nativeSamplerState;
        Microsoft::Xna::Framework::Graphics::DepthStencilState nativeDepthStencilState;
        Microsoft::Xna::Framework::Graphics::RasterizerState nativeRasterizerState;
        if (!TryMapSpriteSortMode(sortMode, &nativeSortMode)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteBatch sort mode is invalid.");
        }
        if (const CNA_Result result = ToNativeBlendState(blendState, &nativeBlendState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeSamplerState(samplerState, &nativeSamplerState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeDepthStencilState(
                depthStencilState, &nativeDepthStencilState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ToNativeRasterizerState(
                rasterizerState, &nativeRasterizerState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        // CABI-38: a non-finite transform component is carried through, not refused. XNA validates
        // nothing here and propagates the bits into the vertex path; this boundary used to refuse
        // them, which was the observable divergence fixcnacs.md Phase 5 asked about.
        Microsoft::Xna::Framework::Matrix nativeTransform =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        if (transformMatrix != nullptr) {
            nativeTransform = Microsoft::Xna::Framework::Matrix(
                transformMatrix->m11, transformMatrix->m12,
                transformMatrix->m13, transformMatrix->m14,
                transformMatrix->m21, transformMatrix->m22,
                transformMatrix->m23, transformMatrix->m24,
                transformMatrix->m31, transformMatrix->m32,
                transformMatrix->m33, transformMatrix->m34,
                transformMatrix->m41, transformMatrix->m42,
                transformMatrix->m43, transformMatrix->m44);
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch already has an active begin/end interval.");
        }

        // CNA_INVALID_HANDLE is the default sprite effect, which is what a null Effect* means to
        // the canonical call -- not a missing argument.
        Microsoft::Xna::Framework::Graphics::Effect* nativeEffect = nullptr;
        std::shared_ptr<EffectResource> effect;
        if (effectHandle != CNA_INVALID_HANDLE) {
            const CNA_Result effectResult =
                GetRuntimeHandles().Get(effectHandle, ObjectKind::Effect, &effect);
            if (effectResult != CNA_RESULT_SUCCESS) {
                return Fail(
                    effectResult,
                    ErrorCategoryForResult(effectResult),
                    "The Effect handle is invalid for this call.");
            }
            if (effect->parentGame != spriteBatch->parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "The Effect belongs to a different game than the SpriteBatch.");
            }
            nativeEffect = effect->value.get();
        }

        spriteBatch->value->Begin(
            nativeSortMode,
            nativeBlendState,
            &nativeSamplerState,
            &nativeDepthStencilState,
            &nativeRasterizerState,
            nativeEffect,
            nativeTransform);
        spriteBatch->begun = true;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_submit_many(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteCommand* const commands,
    const uint64_t commandCount)
{
    return CallWithExceptionBarrier([&]() {
        std::size_t ignoredByteCount = 0U;
        if (const CNA_Result validationResult = CheckedElementByteCount(
                commands,
                commandCount,
                sizeof(CNA_SpriteCommand),
                &ignoredByteCount);
            validationResult != CNA_RESULT_SUCCESS) {
            return Fail(
                validationResult,
                ErrorCategoryForResult(validationResult),
                "The SpriteBatch command array is invalid.");
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch has no active begin/end interval.");
        }

        std::vector<ResolvedSpriteCommand> resolvedCommands;
        if (commandCount > resolvedCommands.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The SpriteBatch command count exceeds the native collection range.");
        }
        resolvedCommands.reserve(static_cast<std::size_t>(commandCount));
        constexpr CNA_SpriteEffects ValidEffects =
            CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY;
        for (uint64_t index = 0U; index < commandCount; ++index) {
            const CNA_SpriteCommand& command = commands[index];
            // CABI-38: the structure's own shape is still checked; its floating-point values are
            // not. A non-finite rotation, origin or layer depth is XNA-valid and travels into the
            // vertex path, so refusing it here was a divergence rather than a safety check.
            if (command.struct_size != sizeof(CNA_SpriteCommand) ||
                command.struct_version != StructureVersion ||
                (command.effects & ~ValidEffects) != 0U) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "A SpriteBatch command contains an invalid version or effect value.");
            }

            std::shared_ptr<Texture2DResource> texture;
            if (const CNA_Result result = GetTexture2D(command.texture, &texture);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (texture->parentGame != spriteBatch->parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "A SpriteBatch command texture belongs to a different game.");
            }
            resolvedCommands.push_back(ResolvedSpriteCommand{command, std::move(texture)});
        }

        for (const ResolvedSpriteCommand& resolved : resolvedCommands) {
            if (resolved.texture->activeBatchReferenceCount ==
                std::numeric_limits<uint64_t>::max()) {
                return Fail(
                    CNA_RESULT_OVERFLOW,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The SpriteBatch texture reference count overflowed.");
            }
            spriteBatch->retainedTextures.push_back(resolved.texture);
            ++resolved.texture->activeBatchReferenceCount;
            const CNA_SpriteCommand& command = resolved.value;
            spriteBatch->value->Draw(
                *resolved.texture->value,
                Rectangle(
                    command.destination.x,
                    command.destination.y,
                    command.destination.width,
                    command.destination.height),
                Rectangle(
                    command.source.x,
                    command.source.y,
                    command.source.width,
                    command.source.height),
                Color(command.color.r, command.color.g, command.color.b, command.color.a),
                command.rotation,
                Vector2(command.origin.x, command.origin.y),
                static_cast<SpriteEffects>(command.effects),
                command.layer_depth);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_submit_scaled_many(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteScaledCommand* const commands,
    const uint64_t commandCount)
{
    return CallWithExceptionBarrier([&]() {
        std::size_t ignoredByteCount = 0U;
        if (const CNA_Result validationResult = CheckedElementByteCount(
                commands,
                commandCount,
                sizeof(CNA_SpriteScaledCommand),
                &ignoredByteCount);
            validationResult != CNA_RESULT_SUCCESS) {
            return Fail(
                validationResult,
                ErrorCategoryForResult(validationResult),
                "The SpriteBatch scaled-command array is invalid.");
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch has no active begin/end interval.");
        }

        struct ResolvedScaledCommand final {
            CNA_SpriteScaledCommand value;
            std::shared_ptr<Texture2DResource> texture;
        };

        std::vector<ResolvedScaledCommand> resolvedCommands;
        if (commandCount > resolvedCommands.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The SpriteBatch command count exceeds the native collection range.");
        }
        resolvedCommands.reserve(static_cast<std::size_t>(commandCount));
        constexpr CNA_SpriteEffects ValidEffects =
            CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY;
        for (uint64_t index = 0U; index < commandCount; ++index) {
            const CNA_SpriteScaledCommand& command = commands[index];
            // CABI-38: shape yes, floating-point values no. See the note in the unscaled route.
            if (command.struct_size != sizeof(CNA_SpriteScaledCommand) ||
                command.struct_version != StructureVersion ||
                (command.effects & ~ValidEffects) != 0U) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "A SpriteBatch scaled command contains an invalid version or effect value.");
            }

            std::shared_ptr<Texture2DResource> texture;
            if (const CNA_Result result = GetTexture2D(command.texture, &texture);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            if (texture->parentGame != spriteBatch->parentGame) {
                return Fail(
                    CNA_RESULT_INVALID_HANDLE,
                    CNA_ERROR_CATEGORY_HANDLE,
                    "A SpriteBatch command texture belongs to a different game.");
            }
            resolvedCommands.push_back(ResolvedScaledCommand{command, std::move(texture)});
        }

        for (const ResolvedScaledCommand& resolved : resolvedCommands) {
            if (resolved.texture->activeBatchReferenceCount ==
                std::numeric_limits<uint64_t>::max()) {
                return Fail(
                    CNA_RESULT_OVERFLOW,
                    CNA_ERROR_CATEGORY_RANGE,
                    "The SpriteBatch texture reference count overflowed.");
            }
            spriteBatch->retainedTextures.push_back(resolved.texture);
            ++resolved.texture->activeBatchReferenceCount;
            const CNA_SpriteScaledCommand& command = resolved.value;
            // Zero width and height is the empty optional the canonical overloads take, which draws
            // the whole texture; anything else is a real source rectangle.
            std::optional<Rectangle> source;
            if (command.source.width != 0 || command.source.height != 0) {
                source = Rectangle(
                    command.source.x,
                    command.source.y,
                    command.source.width,
                    command.source.height);
            }
            spriteBatch->value->Draw(
                *resolved.texture->value,
                Vector2(command.position.x, command.position.y),
                source,
                Color(command.color.r, command.color.g, command.color.b, command.color.a),
                command.rotation,
                Vector2(command.origin.x, command.origin.y),
                Vector2(command.scale.x, command.scale.y),
                static_cast<SpriteEffects>(command.effects),
                command.layer_depth);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_end(const CNA_Handle spriteBatchHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch has no active begin/end interval.");
        }

        spriteBatch->value->End();
        spriteBatch->begun = false;
        ReleaseBatchTextureReferences(*spriteBatch);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_destroy(const CNA_Handle spriteBatchHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (spriteBatch->begun) {
            spriteBatch->begun = false;
            ReleaseBatchTextureReferences(*spriteBatch);
        }

        spriteBatch->value->Dispose();
        const CNA_Result releaseResult = GetRuntimeHandles().Release(spriteBatchHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned SpriteBatch handle could not be released.");
        }
        RemoveOwnedGraphicsResourceFor(spriteBatch->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_get_type_name_size(
    const CNA_Handle spriteBatchHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The required-byte output is null.");
        }
        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = static_cast<uint64_t>(spriteBatch->value->GetTypeName().size());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_copy_type_name(
    const CNA_Handle spriteBatchHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string destination or required-byte output is invalid.");
        }
        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::string& text = spriteBatch->value->GetTypeName();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete type name.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_draw_string(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteTextCommand* const command)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        // CABI-38: shape yes, floating-point values no. See the note in the unscaled route.
        if (command == nullptr || command->struct_size < sizeof(CNA_SpriteTextCommand) ||
            command->struct_version != StructureVersion ||
            (command->effects & ~(CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY |
                                  CNA_SPRITE_EFFECT_FLIP_VERTICALLY)) != 0U) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteBatch text command is invalid.");
        }
        std::string text;
        if (const CNA_Result result = CopyStringView(command->text, false, &text);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch is not inside a begin/end interval.");
        }
        std::shared_ptr<SpriteFontResource> font;
        if (const CNA_Result result = GetOwnedSpriteFont(command->sprite_font, &font);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (font->parentGame != spriteBatch->parentGame) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteFont belongs to a different game than the SpriteBatch.");
        }

        spriteBatch->value->DrawString(
            *font->value,
            text,
            Vector2(command->position.x, command->position.y),
            Color(command->color.r, command->color.g, command->color.b, command->color.a),
            command->rotation,
            Vector2(command->origin.x, command->origin.y),
            Vector2(command->scale.x, command->scale.y),
            static_cast<SpriteEffects>(command->effects),
            command->layer_depth);

        // The atlas must outlive the interval, so it is retained exactly like a drawn texture.
        if (font->texture->activeBatchReferenceCount !=
            std::numeric_limits<uint64_t>::max()) {
            ++font->texture->activeBatchReferenceCount;
            spriteBatch->retainedTextures.push_back(font->texture);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_batch_draw_mesh_ext(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteMeshEXT* const mesh)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (mesh == nullptr || mesh->struct_size < sizeof(CNA_SpriteMeshEXT) ||
            mesh->struct_version != StructureVersion || mesh->positions == nullptr ||
            mesh->indices == nullptr || mesh->vertex_count == 0U || mesh->index_count == 0U ||
            mesh->vertex_count > static_cast<uint64_t>(INT32_MAX) ||
            mesh->index_count > static_cast<uint64_t>(INT32_MAX)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The SpriteBatch mesh command is invalid.");
        }

        std::shared_ptr<SpriteBatchResource> spriteBatch;
        if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (!spriteBatch->begun) {
            return Fail(
                CNA_RESULT_INVALID_STATE,
                CNA_ERROR_CATEGORY_STATE,
                "The SpriteBatch is not inside a begin/end interval.");
        }
        std::shared_ptr<EffectResource> effect;
        const CNA_Result effectResult =
            GetRuntimeHandles().Get(mesh->effect, ObjectKind::Effect, &effect);
        if (effectResult != CNA_RESULT_SUCCESS) {
            return Fail(
                effectResult,
                ErrorCategoryForResult(effectResult),
                "The Effect handle is invalid for this call.");
        }
        if (effect->parentGame != spriteBatch->parentGame) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Effect belongs to a different game than the SpriteBatch.");
        }

        const auto vertexCount = static_cast<std::size_t>(mesh->vertex_count);
        std::vector<Vector2> positions;
        std::vector<Vector2> textureCoordinates;
        std::vector<Color> colors;
        positions.reserve(vertexCount);
        for (std::size_t index = 0U; index < vertexCount; ++index) {
            if (!std::isfinite(mesh->positions[index].x) ||
                !std::isfinite(mesh->positions[index].y)) {
                return Fail(
                    CNA_RESULT_INVALID_ARGUMENT,
                    CNA_ERROR_CATEGORY_ARGUMENT,
                    "A mesh position component is not finite.");
            }
            positions.emplace_back(mesh->positions[index].x, mesh->positions[index].y);
        }
        if (mesh->texture_coordinates != nullptr) {
            textureCoordinates.reserve(vertexCount);
            for (std::size_t index = 0U; index < vertexCount; ++index) {
                textureCoordinates.emplace_back(
                    mesh->texture_coordinates[index].x, mesh->texture_coordinates[index].y);
            }
        }
        // Native Color carries a vtable, so C colors are converted rather than reinterpreted.
        colors.reserve(vertexCount);
        for (std::size_t index = 0U; index < vertexCount; ++index) {
            const CNA_Color value = mesh->colors != nullptr
                ? mesh->colors[index]
                : CNA_Color{UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(255)};
            colors.emplace_back(value.r, value.g, value.b, value.a);
        }

        // Mesh submission has no capability flag: the renderer interface's own default
        // implementation throws for every backend that has not implemented it. Every state and
        // argument precondition is already decided above, so the remaining native failure from
        // this one call is exactly that unsupported-operation case.
        try {
            spriteBatch->value->DrawMeshEXT(
                *effect->value,
                positions.data(),
                colors.data(),
                textureCoordinates.empty() ? nullptr : textureCoordinates.data(),
                static_cast<int>(vertexCount),
                mesh->indices,
                static_cast<int>(mesh->index_count));
        } catch (const std::runtime_error& exception) {
            return Fail(
                CNA_RESULT_NOT_SUPPORTED,
                CNA_ERROR_CATEGORY_NOT_SUPPORTED,
                exception.what());
        }
        return CNA_RESULT_SUCCESS;
    });
}
