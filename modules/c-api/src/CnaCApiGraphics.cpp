// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiGraphicsStateDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/ShaderDialectEXT.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <optional>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::EffectResource;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedSpriteFont;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::Overflow;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNativeBlendState;
using CNA::C::Detail::ToNativeDepthStencilState;
using CNA::C::Detail::ToNativeRasterizerState;
using CNA::C::Detail::ToNativeSamplerState;
using CNA::C::Detail::ValidateVersionedStructure;
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
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::Texture2D;

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
    const CNA::ShaderDialectEXT dialect) noexcept
{
    using CNA::ShaderDialectEXT;
    switch (dialect) {
        case ShaderDialectEXT::Unknown:     return CNA_SHADER_DIALECT_UNKNOWN;
        case ShaderDialectEXT::GlslDesktop: return CNA_SHADER_DIALECT_GLSL_DESKTOP;
        case ShaderDialectEXT::GlslEs:      return CNA_SHADER_DIALECT_GLSL_ES;
        case ShaderDialectEXT::GlslVulkan:  return CNA_SHADER_DIALECT_GLSL_VULKAN;
        case ShaderDialectEXT::Hlsl:        return CNA_SHADER_DIALECT_HLSL;
        case ShaderDialectEXT::Msl:         return CNA_SHADER_DIALECT_MSL;
        case ShaderDialectEXT::Wgsl:        return CNA_SHADER_DIALECT_WGSL;
        case ShaderDialectEXT::SpirV:       return CNA_SHADER_DIALECT_SPIRV;
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
    }
    return CNA_GRAPHICS_CAPABILITY_MAXIMUM + UINT32_C(1);
}

static_assert(
    static_cast<uint32_t>(CNA::RendererFeature::ThreeDimensionalPipeline) ==
        CNA_RENDERER_FEATURE_THREE_DIMENSIONAL_PIPELINE &&
    static_cast<uint32_t>(CNA::RendererFeature::ShaderEffectSourceExecution) ==
        CNA_RENDERER_FEATURE_SHADER_EFFECT_SOURCE_EXECUTION &&
    static_cast<uint32_t>(CNA::RendererFeature::ImageBasedLighting) ==
        CNA_RENDERER_FEATURE_IMAGE_BASED_LIGHTING &&
    static_cast<uint32_t>(CNA::RendererFeature::ShaderDialectWgsl) ==
        CNA_RENDERER_FEATURE_SHADER_DIALECT_WGSL &&
    static_cast<uint32_t>(CNA::RendererFeature::Texture3DSampling) ==
        CNA_RENDERER_FEATURE_TEXTURE_3D_SAMPLING &&
    static_cast<uint32_t>(CNA::RendererFeature::BaseInstanceDrawing) ==
        CNA_RENDERER_FEATURE_BASE_INSTANCE_DRAWING &&
    static_cast<uint32_t>(CNA::RendererFeature::SamplerLevelOfDetailBias) ==
        CNA_RENDERER_FEATURE_SAMPLER_LEVEL_OF_DETAIL_BIAS &&
    static_cast<uint32_t>(CNA::RendererFeature::PbrEffectShading) ==
        CNA_RENDERER_FEATURE_PBR_EFFECT_SHADING &&
    static_cast<uint32_t>(CNA::RendererFeature::SkinnedPbrEffectShading) ==
        CNA_RENDERER_FEATURE_SKINNED_PBR_EFFECT_SHADING &&
    static_cast<uint32_t>(CNA::RendererFeature::MultiSampleMask) ==
        CNA_RENDERER_FEATURE_MAXIMUM &&
    static_cast<uint32_t>(CNA::RendererFeature::Count) ==
        CNA_RENDERER_FEATURE_MAXIMUM + UINT32_C(1));

static_assert(
    static_cast<uint32_t>(CNA::RendererLimit::MaxTextureDimension) ==
        CNA_RENDERER_LIMIT_MAX_TEXTURE_DIMENSION &&
    static_cast<uint32_t>(CNA::RendererLimit::MaxUniformBufferBytes) ==
        CNA_RENDERER_LIMIT_MAX_UNIFORM_BUFFER_BYTES &&
    static_cast<uint32_t>(CNA::RendererLimit::MaxColorAttachments) ==
        CNA_RENDERER_LIMIT_MAX_COLOR_ATTACHMENTS &&
    static_cast<uint32_t>(CNA::RendererLimit::MinUniformBufferOffsetAlignment) ==
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
    // A reserved ordinal (a retired feature) is in range but names nothing.
    const auto native = static_cast<CNA::RendererFeature>(feature);
    const auto defined = CNA::AllRendererFeatures();
    if (std::find(defined.begin(), defined.end(), native) == defined.end()) {
        return false;
    }
    *outFeature = native;
    return true;
}

[[nodiscard]] bool TryMapRendererLimit(
    const CNA_RendererLimit limit,
    CNA::RendererLimit* const outLimit) noexcept
{
    if (outLimit == nullptr || limit > CNA_RENDERER_LIMIT_MAXIMUM) {
        return false;
    }
    const auto native = static_cast<CNA::RendererLimit>(limit);
    const auto defined = CNA::AllRendererLimits();
    if (std::find(defined.begin(), defined.end(), native) == defined.end()) {
        return false;
    }
    *outLimit = native;
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
        case CNA::GraphicsRendererType::OpenGLES3: return CNA_GRAPHICS_RENDERER_OPENGLES3;
        case CNA::GraphicsRendererType::OpenGL33: return CNA_GRAPHICS_RENDERER_OPENGL33;
        case CNA::GraphicsRendererType::WebGL2: return CNA_GRAPHICS_RENDERER_WEBGL2;
        case CNA::GraphicsRendererType::WebGPU: return CNA_GRAPHICS_RENDERER_WEBGPU;
        case CNA::GraphicsRendererType::Headless: return CNA_GRAPHICS_RENDERER_HEADLESS;
        case CNA::GraphicsRendererType::Software: return CNA_GRAPHICS_RENDERER_SOFTWARE;
        case CNA::GraphicsRendererType::Stub: return CNA_GRAPHICS_RENDERER_STUB;
        case CNA::GraphicsRendererType::DirectX9: return CNA_GRAPHICS_RENDERER_DIRECTX9;
        case CNA::GraphicsRendererType::SdlGpu: return CNA_GRAPHICS_RENDERER_SDL_GPU;
        case CNA::GraphicsRendererType::Fna3d: return CNA_GRAPHICS_RENDERER_FNA3D;
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
    return LookupHandle(handle, ObjectKind::SpriteBatch, outSpriteBatch);
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
        return InvalidArgument("The native Texture2D ownership transfer is invalid.");
    }
    *outTexture = CNA_INVALID_HANDLE;
    const auto resource = std::make_shared<Texture2DResource>(
        Texture2DResource{std::move(texture), parentGame, 0U, 0U, 0U, 0U});
    if (const CNA_Result result = CreateHandle(kind, resource, outTexture);
        result != CNA_RESULT_SUCCESS) {
        return result;
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

CNA_Result CreateGameFrameTexture2D(
    std::shared_ptr<Texture2D> texture,
    const CNA_Handle parentGame,
    CNA_Handle* const outTexture)
{
    if (texture == nullptr || outTexture == nullptr) {
        return InvalidArgument("The frame Texture2D view is invalid.");
    }
    *outTexture = CNA_INVALID_HANDLE;
    auto resource = std::make_shared<Texture2DResource>(
        Texture2DResource{std::move(texture), parentGame, 0U, 0U, 0U, 0U});
    resource->ownedResource = false;
    return CreateHandle(ObjectKind::Texture2D, resource, outTexture);
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
        return InvalidArgument("The borrowed RenderTarget2D view is invalid.");
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
    return CreateHandle(ObjectKind::RenderTarget2D, resource, outTexture);
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
        return InvalidArgument("The SpriteBatch output is null.");
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

namespace {

[[nodiscard]] CNA_Result GraphicsDeviceGetRendererInfo(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RendererInfo* const outInfo)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outInfo, "The renderer-info output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return InternalError(
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
}

[[nodiscard]] CNA_Result GraphicsDeviceSupportsCapability(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_GraphicsCapability capability,
    CNA_Bool* const outSupported)
{
    CNA::GraphicsCapability nativeCapability{};
    if (!TryMapGraphicsCapability(capability, &nativeCapability)) {
        return InvalidArgument("The graphics-capability query arguments are invalid.");
    }
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result = GetBorrowedGraphicsDevice(
            graphicsDeviceHandle,
            &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    *outSupported = ToCBool(graphicsDevice->value->SupportsCapability(nativeCapability));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceGetRendererFeatureSupportExt(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RendererFeature feature,
    CNA_RendererFeatureSupport* const outSupport)
{
    CNA::RendererFeature nativeFeature{};
    if (!TryMapRendererFeature(feature, &nativeFeature)) {
        return InvalidArgument("The detailed renderer-feature query arguments are invalid.");
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
}

[[nodiscard]] CNA_Result GraphicsDeviceGetRendererLimitExt(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RendererLimit limit,
    CNA_Bool* const outKnown,
    uint64_t* const outValue)
{
    CNA::RendererLimit nativeLimit{};
    if (!TryMapRendererLimit(limit, &nativeLimit)) {
        return InvalidArgument("The renderer-limit query arguments are invalid.");
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
    *outKnown = ToCBool(value.known);
    *outValue = value.known ? value.value : UINT64_C(0);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result GraphicsDeviceGetSurfaceFormatSupportExt(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_SurfaceFormat format,
    CNA_RendererFormatUsageFlags* const outKnownUsages,
    CNA_RendererFormatUsageFlags* const outSupportedUsages)
{
    if (format > CNA_SURFACE_FORMAT_USHORT_EXT) {
        return InvalidArgument("The renderer surface-format query arguments are invalid.");
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
}

[[nodiscard]] CNA_Result GraphicsDeviceGetBackbufferInfo(
    const CNA_Handle graphicsDeviceHandle,
    CNA_BackBufferInfo* const outInfo)
{
    if (const CNA_Result result = ValidateVersionedStructure(
            outInfo, "The backbuffer-info output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
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
        return InternalError(
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
}

[[nodiscard]] CNA_Result GraphicsDeviceGetBackbufferDataRgba8(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Color* const destination,
    const uint64_t capacity,
    uint64_t* const outPixels)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The backbuffer readback output buffer is invalid.");
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
        return InternalError(
            "The native graphics device reported invalid backbuffer dimensions.");
    }
    const uint64_t requiredPixels =
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    *outPixels = requiredPixels;
    if (requiredPixels > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Overflow("The backbuffer pixel count exceeds the native readback range.");
    }
    if (capacity < requiredPixels) {
        return BufferTooSmall("The backbuffer readback output buffer is too small.");
    }

    std::vector<Color> nativePixels;
    if (requiredPixels > nativePixels.max_size()) {
        return Overflow("The backbuffer pixel count exceeds the native collection range.");
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
}

[[nodiscard]] CNA_Result Texture2dCreate(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_Texture2DCreateInfo* const createInfo,
    CNA_Handle* const outTexture)
{
    if (!IsVersionedStructure(createInfo) || createInfo->width == 0U ||
        createInfo->height == 0U ||
        (createInfo->mip_map != CNA_FALSE && createInfo->mip_map != CNA_TRUE) ||
        createInfo->reserved[0] != 0U || createInfo->reserved[1] != 0U ||
        createInfo->reserved[2] != 0U) {
        return InvalidArgument("The Texture2D creation configuration is invalid.");
    }
    if (createInfo->format > CNA_SURFACE_FORMAT_USHORT_EXT) {
        return InvalidArgument("The Texture2D surface-format identity is invalid.");
    }
    if (createInfo->width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        createInfo->height > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        static_cast<uint64_t>(createInfo->width) * createInfo->height >
            static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        return Overflow("The Texture2D dimensions exceed the native transfer range.");
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
        return InternalError(
            "The native renderer reported a negative texture-dimension limit.");
    }
    if (createInfo->width > static_cast<uint32_t>(maxTextureDimension) ||
        createInfo->height > static_cast<uint32_t>(maxTextureDimension)) {
        return NotSupported("The Texture2D dimensions exceed the active renderer's maximum.");
    }

    const SurfaceFormat format = static_cast<SurfaceFormat>(createInfo->format);
    if (!Texture::IsFormatAllowedByProfileEXT(
            graphicsDevice->value->getGraphicsProfileProperty(), format)) {
        return NotSupported(
            "The Texture2D surface format is unavailable on the active graphics profile.");
    }
    const CNA::RendererFormatSupport formatSupport =
        graphicsDevice->value->GetRendererSurfaceFormatSupportEXT(format);
    if (formatSupport.IsKnown(CNA::RendererFormatUsage::TextureStorage) &&
        !formatSupport.Supports(CNA::RendererFormatUsage::TextureStorage)) {
        return NotSupported(
            "The Texture2D surface format is unavailable on the selected renderer.");
    }

    const auto texture = std::make_shared<Texture2D>(
        *graphicsDevice->value,
        static_cast<int>(createInfo->width),
        static_cast<int>(createInfo->height),
        createInfo->mip_map == CNA_TRUE,
        format);
    return CNA::C::Detail::CreateOwnedTexture2D(
        texture,
        graphicsDevice->parentGame,
        outTexture);
}

[[nodiscard]] CNA_Result Texture2dSetDataRgba8(
    const CNA_Handle textureHandle,
    const CNA_Color* const pixels,
    const uint64_t pixelCount)
{
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
        return InvalidArgument(
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
}

[[nodiscard]] CNA_Result Texture2dGetDataRgba8(
    const CNA_Handle textureHandle,
    CNA_Color* const destination,
    const uint64_t capacity,
    uint64_t* const outPixels)
{
    if ((destination == nullptr && capacity != 0U)) {
        return InvalidArgument("The Texture2D readback buffer is invalid.");
    }
    std::shared_ptr<Texture2DResource> texture;
    if (const CNA_Result result = GetTexture2D(textureHandle, &texture);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    const uint64_t requiredPixels = GetTexturePixelCount(*texture->value);
    *outPixels = requiredPixels;
    if (capacity < requiredPixels) {
        return BufferTooSmall("The Texture2D readback buffer is too small.");
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
}

[[nodiscard]] CNA_Result Texture2dDestroy(const CNA_Handle textureHandle)
{
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
        return InvalidState(
            "The Texture2D is retained by an active SpriteBatch, SpriteFont, effect, model "
            "or render-target scope.");
    }
    if (texture->disposeAllowed) {
        texture->value->Dispose();
    }
    if (const CNA_Result releaseResult = ReleaseHandle(textureHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    if (texture->ownedResource && texture->parentGame != CNA_INVALID_HANDLE) {
        RemoveOwnedGraphicsResourceFor(texture->parentGame);
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SpriteBatchCreate(
    const CNA_Handle graphicsDeviceHandle,
    CNA_Handle* const outSpriteBatch)
{
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
    if (const CNA_Result result = CreateHandle(ObjectKind::SpriteBatch,
        resource,
        outSpriteBatch);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SpriteBatchBegin(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteBatchBeginInfo* const beginInfo)
{
    SpriteSortMode nativeSortMode{};
    if (!IsVersionedStructure(beginInfo) || beginInfo->reserved != 0U ||
        !TryMapSpriteSortMode(beginInfo->sort_mode, &nativeSortMode)) {
        return InvalidArgument("The SpriteBatch begin configuration is invalid.");
    }

    std::shared_ptr<SpriteBatchResource> spriteBatch;
    if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (spriteBatch->begun) {
        return InvalidState("The SpriteBatch already has an active begin/end interval.");
    }

    spriteBatch->value->Begin(
        nativeSortMode,
        Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend);
    spriteBatch->begun = true;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SpriteBatchBeginWithStates(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteSortMode sortMode,
    const CNA_BlendState* const blendState,
    const CNA_SamplerState* const samplerState,
    const CNA_DepthStencilState* const depthStencilState,
    const CNA_RasterizerState* const rasterizerState)
{
    SpriteSortMode nativeSortMode{};
    Microsoft::Xna::Framework::Graphics::BlendState nativeBlendState;
    Microsoft::Xna::Framework::Graphics::SamplerState nativeSamplerState;
    Microsoft::Xna::Framework::Graphics::DepthStencilState nativeDepthStencilState;
    Microsoft::Xna::Framework::Graphics::RasterizerState nativeRasterizerState;
    if (!TryMapSpriteSortMode(sortMode, &nativeSortMode)) {
        return InvalidArgument("The SpriteBatch sort mode is invalid.");
    }
    // Both Begin routes document each of these four as "or null for <default>", and that is
    // the canonical behaviour: FNA's SpriteBatch.Begin stores
    // `blendState ?? BlendState.AlphaBlend` and the same for the other three. Substituting
    // here rather than teaching the shared converters null keeps every other route's
    // refusal of a null descriptor exactly as it was.
    if (blendState == nullptr) {
        nativeBlendState = Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend;
    } else if (const CNA_Result result = ToNativeBlendState(blendState, &nativeBlendState);
               result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (samplerState == nullptr) {
        nativeSamplerState = Microsoft::Xna::Framework::Graphics::SamplerState::LinearClamp;
    } else if (const CNA_Result result = ToNativeSamplerState(samplerState, &nativeSamplerState);
               result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (depthStencilState == nullptr) {
        nativeDepthStencilState =
            Microsoft::Xna::Framework::Graphics::DepthStencilState::None;
    } else if (const CNA_Result result = ToNativeDepthStencilState(
                   depthStencilState, &nativeDepthStencilState);
               result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (rasterizerState == nullptr) {
        nativeRasterizerState =
            Microsoft::Xna::Framework::Graphics::RasterizerState::CullCounterClockwise;
    } else if (const CNA_Result result = ToNativeRasterizerState(
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
        return InvalidState("The SpriteBatch already has an active begin/end interval.");
    }

    spriteBatch->value->Begin(
        nativeSortMode,
        nativeBlendState,
        &nativeSamplerState,
        &nativeDepthStencilState,
        &nativeRasterizerState);
    spriteBatch->begun = true;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SpriteBatchBeginWithEffect(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteSortMode sortMode,
    const CNA_BlendState* const blendState,
    const CNA_SamplerState* const samplerState,
    const CNA_DepthStencilState* const depthStencilState,
    const CNA_RasterizerState* const rasterizerState,
    const CNA_Handle effectHandle,
    const CNA_Matrix* const transformMatrix)
{
    SpriteSortMode nativeSortMode{};
    Microsoft::Xna::Framework::Graphics::BlendState nativeBlendState;
    Microsoft::Xna::Framework::Graphics::SamplerState nativeSamplerState;
    Microsoft::Xna::Framework::Graphics::DepthStencilState nativeDepthStencilState;
    Microsoft::Xna::Framework::Graphics::RasterizerState nativeRasterizerState;
    if (!TryMapSpriteSortMode(sortMode, &nativeSortMode)) {
        return InvalidArgument("The SpriteBatch sort mode is invalid.");
    }
    // Both Begin routes document each of these four as "or null for <default>", and that is
    // the canonical behaviour: FNA's SpriteBatch.Begin stores
    // `blendState ?? BlendState.AlphaBlend` and the same for the other three. Substituting
    // here rather than teaching the shared converters null keeps every other route's
    // refusal of a null descriptor exactly as it was.
    if (blendState == nullptr) {
        nativeBlendState = Microsoft::Xna::Framework::Graphics::BlendState::AlphaBlend;
    } else if (const CNA_Result result = ToNativeBlendState(blendState, &nativeBlendState);
               result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (samplerState == nullptr) {
        nativeSamplerState = Microsoft::Xna::Framework::Graphics::SamplerState::LinearClamp;
    } else if (const CNA_Result result = ToNativeSamplerState(samplerState, &nativeSamplerState);
               result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (depthStencilState == nullptr) {
        nativeDepthStencilState =
            Microsoft::Xna::Framework::Graphics::DepthStencilState::None;
    } else if (const CNA_Result result = ToNativeDepthStencilState(
                   depthStencilState, &nativeDepthStencilState);
               result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (rasterizerState == nullptr) {
        nativeRasterizerState =
            Microsoft::Xna::Framework::Graphics::RasterizerState::CullCounterClockwise;
    } else if (const CNA_Result result = ToNativeRasterizerState(
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
        return InvalidState("The SpriteBatch already has an active begin/end interval.");
    }

    // CNA_INVALID_HANDLE is the default sprite effect, which is what a null Effect* means to
    // the canonical call -- not a missing argument.
    Microsoft::Xna::Framework::Graphics::Effect* nativeEffect = nullptr;
    std::shared_ptr<EffectResource> effect;
    if (effectHandle != CNA_INVALID_HANDLE) {
        const CNA_Result effectResult = LookupHandle(effectHandle, ObjectKind::Effect, &effect);
        if (effectResult != CNA_RESULT_SUCCESS) {
            return effectResult;
        }
        if (effect->parentGame != spriteBatch->parentGame) {
            return InvalidArgument(
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
}

[[nodiscard]] CNA_Result SpriteBatchSubmitMany(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteCommand* const commands,
    const uint64_t commandCount)
{
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
        return InvalidState("The SpriteBatch has no active begin/end interval.");
    }

    std::vector<ResolvedSpriteCommand> resolvedCommands;
    if (commandCount > resolvedCommands.max_size()) {
        return Overflow("The SpriteBatch command count exceeds the native collection range.");
    }
    resolvedCommands.reserve(static_cast<std::size_t>(commandCount));
    constexpr CNA_SpriteEffects ValidEffects =
        CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY;
    for (uint64_t index = 0U; index < commandCount; ++index) {
        const CNA_SpriteCommand& command = commands[index];
        // CABI-38: the structure's own shape is still checked; its floating-point values are
        // not. A non-finite rotation, origin or layer depth is XNA-valid and travels into the
        // vertex path, so refusing it here was a divergence rather than a safety check.
        if (command.struct_size < sizeof(CNA_SpriteCommand) ||
            command.struct_version != StructureVersion ||
            (command.effects & ~ValidEffects) != 0U) {
            return InvalidArgument(
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
            return Overflow("The SpriteBatch texture reference count overflowed.");
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
}

[[nodiscard]] CNA_Result SpriteBatchSubmitScaledMany(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteScaledCommand* const commands,
    const uint64_t commandCount)
{
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
        return InvalidState("The SpriteBatch has no active begin/end interval.");
    }

    struct ResolvedScaledCommand final {
        CNA_SpriteScaledCommand value;
        std::shared_ptr<Texture2DResource> texture;
    };

    std::vector<ResolvedScaledCommand> resolvedCommands;
    if (commandCount > resolvedCommands.max_size()) {
        return Overflow("The SpriteBatch command count exceeds the native collection range.");
    }
    resolvedCommands.reserve(static_cast<std::size_t>(commandCount));
    constexpr CNA_SpriteEffects ValidEffects =
        CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY;
    for (uint64_t index = 0U; index < commandCount; ++index) {
        const CNA_SpriteScaledCommand& command = commands[index];
        // CABI-38: shape yes, floating-point values no. See the note in the unscaled route.
        if (command.struct_size < sizeof(CNA_SpriteScaledCommand) ||
            command.struct_version != StructureVersion ||
            (command.effects & ~ValidEffects) != 0U) {
            return InvalidArgument(
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
            return Overflow("The SpriteBatch texture reference count overflowed.");
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
}

[[nodiscard]] CNA_Result SpriteBatchEnd(const CNA_Handle spriteBatchHandle)
{
    std::shared_ptr<SpriteBatchResource> spriteBatch;
    if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!spriteBatch->begun) {
        return InvalidState("The SpriteBatch has no active begin/end interval.");
    }

    spriteBatch->value->End();
    spriteBatch->begun = false;
    ReleaseBatchTextureReferences(*spriteBatch);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SpriteBatchDestroy(const CNA_Handle spriteBatchHandle)
{
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
    if (const CNA_Result releaseResult = ReleaseHandle(spriteBatchHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    RemoveOwnedGraphicsResourceFor(spriteBatch->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result SpriteBatchDrawString(
    const CNA_Handle spriteBatchHandle,
    const CNA_SpriteTextCommand* const command)
{
    // CABI-38: shape yes, floating-point values no. See the note in the unscaled route.
    if (!IsVersionedStructure(command) ||
        (command->effects & ~(CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY |
                              CNA_SPRITE_EFFECT_FLIP_VERTICALLY)) != 0U) {
        return InvalidArgument("The SpriteBatch text command is invalid.");
    }
    std::string text;
    if (const CNA_Result result =
            ReadText(command->text, "The SpriteBatch text is not valid UTF-8.", &text);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    std::shared_ptr<SpriteBatchResource> spriteBatch;
    if (const CNA_Result result = GetSpriteBatch(spriteBatchHandle, &spriteBatch);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (!spriteBatch->begun) {
        return InvalidState("The SpriteBatch is not inside a begin/end interval.");
    }
    std::shared_ptr<SpriteFontResource> font;
    if (const CNA_Result result = GetOwnedSpriteFont(command->sprite_font, &font);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (font->parentGame != spriteBatch->parentGame) {
        return InvalidArgument(
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
}

} // namespace

// The routes generated from modules/c-api/spec/Graphics.toml.
#include "generated/Graphics.inc"
