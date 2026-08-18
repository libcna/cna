// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_ext.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include <memory>

#ifdef CNA_CNAEXT
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/IDisposable.hpp"

#include <type_traits>
#endif

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;

// Every caller is in the #ifndef CNA_CNAEXT half below, so a build that has the extension layer
// compiles this without using it. The routes stay exported either way, which is the point.
[[nodiscard, maybe_unused]] CNA_Result ExtensionUnavailable()
{
    return Fail(
        CNA_RESULT_NOT_SUPPORTED,
        CNA_ERROR_CATEGORY_NOT_SUPPORTED,
        "This CNA build does not contain the extended graphics layer.");
}

template<typename TValue>
[[nodiscard]] CNA_Result StoreValue(TValue* const output, const TValue value) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The output value is null.");
        }
        *output = value;
        return CNA_RESULT_SUCCESS;
    });
}

#ifdef CNA_CNAEXT

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::GetOwnedTexture;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::TextureResourceView;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace Ext = CNA::Graphics;

template<typename TEnum>
[[nodiscard]] constexpr uint32_t NativeOrdinal(const TEnum value) noexcept
{
    return static_cast<uint32_t>(static_cast<std::underlying_type_t<TEnum>>(value));
}

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
    NativeOrdinal(Ext::RenderQuality::Low) == CNA_RENDER_QUALITY_LOW &&
    NativeOrdinal(Ext::RenderQuality::Medium) == CNA_RENDER_QUALITY_MEDIUM &&
    NativeOrdinal(Ext::RenderQuality::High) == CNA_RENDER_QUALITY_HIGH &&
    NativeOrdinal(Ext::RenderQuality::Ultra) == CNA_RENDER_QUALITY_ULTRA);
static_assert(
    NativeOrdinal(Ext::ShadowQuality::Disabled) == CNA_SHADOW_QUALITY_DISABLED &&
    NativeOrdinal(Ext::ShadowQuality::Low) == CNA_SHADOW_QUALITY_LOW &&
    NativeOrdinal(Ext::ShadowQuality::Medium) == CNA_SHADOW_QUALITY_MEDIUM &&
    NativeOrdinal(Ext::ShadowQuality::High) == CNA_SHADOW_QUALITY_HIGH &&
    NativeOrdinal(Ext::ShadowQuality::Ultra) == CNA_SHADOW_QUALITY_ULTRA);
static_assert(
    NativeOrdinal(Ext::TonemappingMode::None) == CNA_TONEMAPPING_MODE_NONE &&
    NativeOrdinal(Ext::TonemappingMode::Reinhard) == CNA_TONEMAPPING_MODE_REINHARD &&
    NativeOrdinal(Ext::TonemappingMode::Filmic) == CNA_TONEMAPPING_MODE_FILMIC &&
    NativeOrdinal(Ext::TonemappingMode::Aces) == CNA_TONEMAPPING_MODE_ACES);
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

struct AsciiEffectResource final {
    std::shared_ptr<Ext::AsciiPostProcessEffect> value;
    CNA_Handle parentGame;
};

[[nodiscard]] CNA_Result GetAsciiEffect(
    const CNA_Handle handle,
    std::shared_ptr<AsciiEffectResource>* const outEffect)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::AsciiPostProcessEffect, outEffect);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The AsciiPostProcessEffect handle is invalid for this call.");
}

#endif // CNA_CNAEXT

} // namespace

CNA_Result cna_graphics_ext_is_available(CNA_Bool* const outAvailable)
{
#ifdef CNA_CNAEXT
    return StoreValue(outAvailable, CNA_TRUE);
#else
    return StoreValue(outAvailable, CNA_FALSE);
#endif
}

CNA_Result cna_pbr_material_init(CNA_PbrMaterial* const outMaterial)
{
    // The canonical defaults are reproduced here so the value route works in every build; when the
    // extension layer is present the assertions below prove the two agree.
    const CNA_PbrMaterial defaults = {
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        CNA_INVALID_HANDLE,
        {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(255)},
        {UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(255)},
        0.0F,
        0.5F,
        1.0F,
        1.0F,
        0.5F,
        CNA_FALSE,
        {UINT8_C(0), UINT8_C(0), UINT8_C(0)}};
    return StoreValue(outMaterial, defaults);
}

CNA_Result cna_render_pipeline_settings_init(CNA_RenderPipelineSettings* const outSettings)
{
    const CNA_RenderPipelineSettings defaults = {
        1.0F,
        2.2F,
        1.0F,
        CNA_TONEMAPPING_MODE_NONE,
        CNA_RENDER_QUALITY_MEDIUM,
        CNA_SHADOW_QUALITY_DISABLED,
        CNA_FALSE,
        CNA_FALSE,
        CNA_FALSE,
        CNA_FALSE};
    return StoreValue(outSettings, defaults);
}

#ifndef CNA_CNAEXT

CNA_Result cna_ascii_post_process_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_AsciiPostProcessEffectHandle* const outEffect)
{
    (void)graphicsDeviceHandle;
    if (outEffect != nullptr) {
        *outEffect = CNA_INVALID_HANDLE;
    }
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_get_cell_size(
    const CNA_AsciiPostProcessEffectHandle effect,
    int32_t* const outWidth,
    int32_t* const outHeight)
{
    (void)effect;
    (void)outWidth;
    (void)outHeight;
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_set_cell_size(
    const CNA_AsciiPostProcessEffectHandle effect,
    const int32_t width,
    const int32_t height)
{
    (void)effect;
    (void)width;
    (void)height;
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_get_quantize_mode(
    const CNA_AsciiPostProcessEffectHandle effect,
    CNA_AsciiQuantizeMode* const outMode)
{
    (void)effect;
    (void)outMode;
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_set_quantize_mode(
    const CNA_AsciiPostProcessEffectHandle effect,
    const CNA_AsciiQuantizeMode mode)
{
    (void)effect;
    (void)mode;
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_draw(
    const CNA_AsciiPostProcessEffectHandle effect,
    const CNA_Handle source,
    const CNA_Rectangle* const destinationRectangle)
{
    (void)effect;
    (void)source;
    (void)destinationRectangle;
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_get_last_grid_dimensions(
    const CNA_AsciiPostProcessEffectHandle effect,
    int32_t* const outColumns,
    int32_t* const outRows)
{
    (void)effect;
    (void)outColumns;
    (void)outRows;
    return ExtensionUnavailable();
}

CNA_Result cna_ascii_post_process_effect_destroy(
    const CNA_AsciiPostProcessEffectHandle effect)
{
    (void)effect;
    return ExtensionUnavailable();
}

#else // CNA_CNAEXT

CNA_Result cna_ascii_post_process_effect_create(
    const CNA_Handle graphicsDeviceHandle,
    CNA_AsciiPostProcessEffectHandle* const outEffect)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEffect == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The ASCII post-process effect output handle is null.");
        }
        *outEffect = CNA_INVALID_HANDLE;
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::AsciiPostProcessEffect, resource, outEffect);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned ASCII post-process effect handle could not be created.");
        }
        AddOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_ascii_post_process_effect_get_cell_size(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    int32_t* const outWidth,
    int32_t* const outHeight)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outWidth == nullptr || outHeight == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A cell-size output is null.");
        }
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
    });
}

CNA_Result cna_ascii_post_process_effect_set_cell_size(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    const int32_t width,
    const int32_t height)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AsciiEffectResource> effect;
        if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->value->setCellSize(static_cast<int>(width), static_cast<int>(height));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_ascii_post_process_effect_get_quantize_mode(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    CNA_AsciiQuantizeMode* const outMode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outMode == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The quantize-mode output is null.");
        }
        std::shared_ptr<AsciiEffectResource> effect;
        if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outMode = NativeOrdinal(effect->value->getQuantizeMode());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_ascii_post_process_effect_set_quantize_mode(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    const CNA_AsciiQuantizeMode mode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (mode > CNA_ASCII_QUANTIZE_MODE_COLOR) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The ASCII quantize mode is not recognized.");
        }
        std::shared_ptr<AsciiEffectResource> effect;
        if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        effect->value->setQuantizeMode(static_cast<Ext::AsciiQuantizeMode>(mode));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_ascii_post_process_effect_draw(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    const CNA_Handle sourceHandle,
    const CNA_Rectangle* const destinationRectangle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
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
    });
}

CNA_Result cna_ascii_post_process_effect_get_last_grid_dimensions(
    const CNA_AsciiPostProcessEffectHandle effectHandle,
    int32_t* const outColumns,
    int32_t* const outRows)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outColumns == nullptr || outRows == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A grid-dimension output is null.");
        }
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
    });
}

CNA_Result cna_ascii_post_process_effect_destroy(
    const CNA_AsciiPostProcessEffectHandle effectHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<AsciiEffectResource> effect;
        if (const CNA_Result result = GetAsciiEffect(effectHandle, &effect);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result releaseResult = GetRuntimeHandles().Release(effectHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned ASCII post-process effect handle could not be released.");
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

#endif // CNA_CNAEXT
