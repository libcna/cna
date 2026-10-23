// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_ext.h"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiGraphicsExtDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include <memory>

#ifdef CNA_CNAEXT
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/IDisposable.hpp"

using CNA::C::Detail::AsciiEffectResource;
using CNA::C::Detail::LookupHandle;
#include <type_traits>
#endif

namespace {

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::NotSupported;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::ToCBool;

// Whether this build contains the extended graphics layer; its routes are exported either way.
#ifdef CNA_CNAEXT
constexpr bool GraphicsExtBuilt = true;
#else
constexpr bool GraphicsExtBuilt = false;
#endif

// What every extension route answers in a build without CNA_CNAEXT (the `unavailable` of the
// spec's feature groups); a build with the layer compiles it unused. The routes stay exported
// either way, which is the point.
[[nodiscard, maybe_unused]] CNA_Result ExtensionUnavailable()
{
    return NotSupported("This CNA build does not contain the extended graphics layer.");
}

template<typename TValue>
[[nodiscard]] CNA_Result StoreValue(TValue* const output, const TValue value) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return InvalidArgument("The output value is null.");
        }
        *output = value;
        return CNA_RESULT_SUCCESS;
    });
}

#ifdef CNA_CNAEXT

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::NativeOrdinal;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::TextureResourceView;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace Ext = CNA::Graphics;

static_assert(
    NativeOrdinal(Ext::AsciiQuantizeMode::BlackWhite) == CNA_ASCII_QUANTIZE_MODE_BLACK_WHITE &&
    NativeOrdinal(Ext::AsciiQuantizeMode::Color) == CNA_ASCII_QUANTIZE_MODE_COLOR);
static_assert(
    NativeOrdinal(Ext::CRTMaskType::None) == CNA_CRT_MASK_TYPE_NONE &&
    NativeOrdinal(Ext::CRTMaskType::ApertureGrille) == CNA_CRT_MASK_TYPE_APERTURE_GRILLE &&
    NativeOrdinal(Ext::CRTMaskType::ShadowMask) == CNA_CRT_MASK_TYPE_SHADOW_MASK);
static_assert(
    NativeOrdinal(Ext::DitherMode::None) == CNA_DITHER_MODE_NONE &&
    NativeOrdinal(Ext::DitherMode::Bayer4x4) == CNA_DITHER_MODE_BAYER_4X4 &&
    NativeOrdinal(Ext::DitherMode::Bayer8x8) == CNA_DITHER_MODE_BAYER_8X8);
static_assert(
    NativeOrdinal(Ext::DepthEffectMode::Color16Bit) == CNA_DEPTH_EFFECT_MODE_COLOR_16_BIT &&
    NativeOrdinal(Ext::DepthEffectMode::Color8Bit) == CNA_DEPTH_EFFECT_MODE_COLOR_8_BIT &&
    NativeOrdinal(Ext::DepthEffectMode::Grayscale4Bit) ==
        CNA_DEPTH_EFFECT_MODE_GRAYSCALE_4_BIT &&
    NativeOrdinal(Ext::DepthEffectMode::Grayscale2Bit) ==
        CNA_DEPTH_EFFECT_MODE_GRAYSCALE_2_BIT &&
    NativeOrdinal(Ext::DepthEffectMode::Grayscale1Bit) ==
        CNA_DEPTH_EFFECT_MODE_GRAYSCALE_1_BIT &&
    NativeOrdinal(Ext::DepthEffectMode::Palette256) == CNA_DEPTH_EFFECT_MODE_PALETTE_256 &&
    NativeOrdinal(Ext::DepthEffectMode::Palette16) == CNA_DEPTH_EFFECT_MODE_PALETTE_16);

[[nodiscard]] CNA_Result GetAsciiEffect(
    const CNA_Handle handle,
    std::shared_ptr<AsciiEffectResource>* const outEffect)
{
    return LookupHandle(handle, ObjectKind::AsciiPostProcessEffect, outEffect);
}

#endif // CNA_CNAEXT

} // namespace

namespace {

[[nodiscard]] CNA_Result ImageBasedLightExtInit(CNA_ImageBasedLightEXT* const outLight)
{
    const Microsoft::Xna::Framework::Graphics::ImageBasedLightEXT defaults;
    *outLight = CNA_ImageBasedLightEXT{};
    outLight->struct_size = static_cast<uint32_t>(sizeof(CNA_ImageBasedLightEXT));
    outLight->struct_version = UINT32_C(1);
    outLight->irradiance = CNA_INVALID_HANDLE;
    outLight->prefiltered_specular = CNA_INVALID_HANDLE;
    outLight->brdf_lut = CNA_INVALID_HANDLE;
    outLight->prefiltered_mip_count = static_cast<int32_t>(defaults.PrefilteredMipCount);
    outLight->intensity = defaults.Intensity;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ImageBasedLightExtIsValid(
    const CNA_ImageBasedLightEXT* const light, CNA_Bool* const outValid)
{
    if (light == nullptr) {
        return InvalidArgument("The light is null.");
    }
    if (light->struct_size < static_cast<uint32_t>(sizeof(CNA_ImageBasedLightEXT)) ||
        light->struct_version == UINT32_C(0)) {
        return InvalidArgument("The image-based-light structure is malformed.");
    }
    // The canonical predicate asks whether all three textures are present and the mip count is
    // at least one. Here the textures are handles, so "present" is "a live handle of the type the
    // C++ field holds": TextureCube for the two maps, Texture2D for the lookup, a render target
    // of that type included. A stale handle, or a live one of another kind, is not that texture
    // and must not read as a complete light (CNA-DEMOS-ISSUE-091).
    using CNA::C::Detail::ObjectKind;
    struct Field {
        CNA_Handle handle;
        ObjectKind texture;
        ObjectKind renderTarget;
    };
    const Field fields[] = {
        {light->irradiance, ObjectKind::TextureCube, ObjectKind::RenderTargetCube},
        {light->prefiltered_specular, ObjectKind::TextureCube, ObjectKind::RenderTargetCube},
        {light->brdf_lut, ObjectKind::Texture2D, ObjectKind::RenderTarget2D}};
    // IsValidEXT() also refuses a negative intensity: a bundle that would subtract light
    // is not two thirds of an answer either.
    bool complete = light->prefiltered_mip_count >= INT32_C(1) && light->intensity >= 0.0F;
    for (const Field& field : fields) {
        if (!complete) {
            break;
        }
        ObjectKind kind = ObjectKind::Unknown;
        complete = field.handle != CNA_INVALID_HANDLE &&
            CNA::C::Detail::GetRuntimeHandles().GetKind(field.handle, &kind) ==
                CNA_RESULT_SUCCESS &&
            (kind == field.texture || kind == field.renderTarget);
    }
    return StoreValue(outValid, static_cast<CNA_Bool>(ToCBool(complete)));
}

} // namespace

#ifdef CNA_CNAEXT

namespace {

[[nodiscard]] CNA_Result AsciiPostProcessEffectCreate(
    const CNA_Handle graphicsDeviceHandle,
    CNA_AsciiPostProcessEffectHandle* const outEffect)
{
    std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
    if (const CNA_Result result =
            GetBorrowedGraphicsDevice(graphicsDeviceHandle, &graphicsDevice);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }

    auto native =
        std::make_shared<Ext::AsciiPostProcessEffect>(*graphicsDevice->value);
    const auto resource = std::make_shared<AsciiEffectResource>(
        AsciiEffectResource{std::move(native), graphicsDevice->parentGame});
    if (const CNA_Result result =
            CreateHandle(ObjectKind::AsciiPostProcessEffect, resource, outEffect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    AddOwnedGraphicsResourceFor(graphicsDevice->parentGame);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AsciiPostProcessEffectGetCellSize(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    int32_t* const outWidth,
    int32_t* const outHeight)
{
    std::shared_ptr<AsciiEffectResource> effect;
    if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    int width = 0;
    int height = 0;
    effect->value->getCellSize(width, height);
    *outWidth = static_cast<int32_t>(width);
    *outHeight = static_cast<int32_t>(height);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AsciiPostProcessEffectSetQuantizeMode(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    const CNA_AsciiQuantizeMode mode)
{
    if (mode > CNA_ASCII_QUANTIZE_MODE_COLOR) {
        return InvalidArgument("The ASCII quantize mode is not recognized.");
    }
    std::shared_ptr<AsciiEffectResource> effect;
    if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    effect->value->setQuantizeMode(static_cast<Ext::AsciiQuantizeMode>(mode));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AsciiPostProcessEffectDraw(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    const CNA_Handle sourceHandle,
    const CNA_Rectangle* const destinationRectangle)
{
    std::shared_ptr<AsciiEffectResource> effect;
    if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    TextureResourceView source;
    if (const CNA_Result result = GetOwnedTexture(sourceHandle, &source);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    auto* const texture = dynamic_cast<Texture2D*>(source.value.get());
    if (texture == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_HANDLE,
            CNA_ERROR_CATEGORY_HANDLE,
            "The ASCII source must be a two-dimensional texture.");
    }

    if (destinationRectangle == nullptr) {
        effect->value->Draw(*texture);
    } else {
        effect->value->Draw(
            *texture,
            Rectangle(
                destinationRectangle->x,
                destinationRectangle->y,
                destinationRectangle->width,
                destinationRectangle->height));
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AsciiPostProcessEffectGetLastGridDimensions(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    int32_t* const outColumns,
    int32_t* const outRows)
{
    std::shared_ptr<AsciiEffectResource> effect;
    if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    int columns = 0;
    int rows = 0;
    effect->value->GetLastGridDimensions(columns, rows);
    *outColumns = static_cast<int32_t>(columns);
    *outRows = static_cast<int32_t>(rows);
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result AsciiPostProcessEffectDestroy(
    const CNA_AsciiPostProcessEffectHandle effectHandle)
{
    std::shared_ptr<AsciiEffectResource> effect;
    if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    if (const CNA_Result releaseResult = ReleaseHandle(effectHandle);
        releaseResult != CNA_RESULT_SUCCESS) {
        return releaseResult;
    }
    RemoveOwnedGraphicsResourceFor(effect->parentGame);
    return CNA_RESULT_SUCCESS;
}

} // namespace

#endif // CNA_CNAEXT

// The routes generated from modules/c-api/spec/GraphicsExt.toml.
#include "generated/GraphicsExt.inc"
