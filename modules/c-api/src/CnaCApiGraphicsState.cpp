// SPDX-License-Identifier: MS-PL

#include "CNA/C/graphics_state.h"
#include "CnaCApiGraphicsStateDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <cmath>
#include <cstdint>
#include <memory>

namespace {

using CNA::C::Detail::BorrowedGraphicsDevice;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetBorrowedGraphicsDevice;
using CNA::C::Detail::ToCBlendState;
using CNA::C::Detail::ToCDepthStencilState;
using CNA::C::Detail::ToCRasterizerState;
using CNA::C::Detail::ToCSamplerState;
using CNA::C::Detail::ToNativeBlendState;
using CNA::C::Detail::ToNativeDepthStencilState;
using CNA::C::Detail::ToNativeRasterizerState;
using CNA::C::Detail::ToNativeSamplerState;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::Blend;
using Microsoft::Xna::Framework::Graphics::BlendFunction;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::ColorWriteChannels;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::FillMode;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::SamplerState;
using Microsoft::Xna::Framework::Graphics::StencilOperation;
using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
using Microsoft::Xna::Framework::Graphics::TextureFilter;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] bool IsBlend(const CNA_Blend value) noexcept
{
    return value <= CNA_BLEND_SOURCE_ALPHA_SATURATION;
}

[[nodiscard]] bool IsBlendFunction(const CNA_BlendFunction value) noexcept
{
    return value <= CNA_BLEND_FUNCTION_MIN;
}

[[nodiscard]] bool IsColorWriteChannels(const CNA_ColorWriteChannels value) noexcept
{
    return (value & ~CNA_COLOR_WRITE_ALL) == 0U;
}

[[nodiscard]] bool IsCompareFunction(const CNA_CompareFunction value) noexcept
{
    return value <= CNA_COMPARE_NOT_EQUAL;
}

[[nodiscard]] bool IsStencilOperation(const CNA_StencilOperation value) noexcept
{
    return value <= CNA_STENCIL_INVERT;
}

[[nodiscard]] bool IsCullMode(const CNA_CullMode value) noexcept
{
    return value <= CNA_CULL_COUNTER_CLOCKWISE_FACE;
}

[[nodiscard]] bool IsFillMode(const CNA_FillMode value) noexcept
{
    return value <= CNA_FILL_WIREFRAME;
}

[[nodiscard]] bool IsTextureAddressMode(const CNA_TextureAddressMode value) noexcept
{
    return value <= CNA_TEXTURE_ADDRESS_MIRROR;
}

[[nodiscard]] bool IsTextureFilter(const CNA_TextureFilter value) noexcept
{
    return value <= CNA_TEXTURE_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
}

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

template<typename T>
[[nodiscard]] bool HasSupportedOutputHeader(const T* const value) noexcept
{
    return value != nullptr && value->struct_size >= sizeof(T) &&
        value->struct_version == StructureVersion;
}

[[nodiscard]] CNA_Result InvalidStateDescriptor(const char* const typeName)
{
    return Fail(
        CNA_RESULT_INVALID_ARGUMENT,
        CNA_ERROR_CATEGORY_ARGUMENT,
        typeName);
}

} // namespace

namespace CNA::C::Detail {

CNA_Result ToNativeBlendState(
    const CNA_BlendState* const source,
    BlendState* const destination)
{
    if (source == nullptr || destination == nullptr ||
        source->struct_size < sizeof(CNA_BlendState) ||
        source->struct_version != StructureVersion ||
        !IsBlendFunction(source->alpha_blend_function) ||
        !IsBlend(source->alpha_destination_blend) ||
        !IsBlend(source->alpha_source_blend) ||
        !IsBlendFunction(source->color_blend_function) ||
        !IsBlend(source->color_destination_blend) ||
        !IsBlend(source->color_source_blend) ||
        !IsColorWriteChannels(source->color_write_channels) ||
        !IsColorWriteChannels(source->color_write_channels1) ||
        !IsColorWriteChannels(source->color_write_channels2) ||
        !IsColorWriteChannels(source->color_write_channels3)) {
        return InvalidStateDescriptor("The BlendState descriptor is invalid.");
    }

    BlendState result;
    result.setAlphaBlendFunctionProperty(static_cast<BlendFunction>(source->alpha_blend_function));
    result.setAlphaDestinationBlendProperty(static_cast<Blend>(source->alpha_destination_blend));
    result.setAlphaSourceBlendProperty(static_cast<Blend>(source->alpha_source_blend));
    result.setColorBlendFunctionProperty(static_cast<BlendFunction>(source->color_blend_function));
    result.setColorDestinationBlendProperty(static_cast<Blend>(source->color_destination_blend));
    result.setColorSourceBlendProperty(static_cast<Blend>(source->color_source_blend));
    result.setColorWriteChannelsProperty(
        static_cast<ColorWriteChannels>(source->color_write_channels));
    result.setColorWriteChannels1Property(
        static_cast<ColorWriteChannels>(source->color_write_channels1));
    result.setColorWriteChannels2Property(
        static_cast<ColorWriteChannels>(source->color_write_channels2));
    result.setColorWriteChannels3Property(
        static_cast<ColorWriteChannels>(source->color_write_channels3));
    result.setBlendFactorProperty(Color(
        source->blend_factor.r,
        source->blend_factor.g,
        source->blend_factor.b,
        source->blend_factor.a));
    result.setMultiSampleMaskProperty(source->multi_sample_mask);
    *destination = result;
    return CNA_RESULT_SUCCESS;
}

CNA_Result ToNativeDepthStencilState(
    const CNA_DepthStencilState* const source,
    DepthStencilState* const destination)
{
    if (source == nullptr || destination == nullptr ||
        source->struct_size < sizeof(CNA_DepthStencilState) ||
        source->struct_version != StructureVersion || source->reserved != 0U ||
        !IsBool(source->depth_buffer_enable) ||
        !IsBool(source->depth_buffer_write_enable) ||
        !IsBool(source->stencil_enable) ||
        !IsBool(source->two_sided_stencil_mode) ||
        !IsCompareFunction(source->depth_buffer_function) ||
        !IsCompareFunction(source->stencil_function) ||
        !IsStencilOperation(source->stencil_fail) ||
        !IsStencilOperation(source->stencil_depth_buffer_fail) ||
        !IsStencilOperation(source->stencil_pass) ||
        !IsCompareFunction(source->counter_clockwise_stencil_function) ||
        !IsStencilOperation(source->counter_clockwise_stencil_fail) ||
        !IsStencilOperation(source->counter_clockwise_stencil_depth_buffer_fail) ||
        !IsStencilOperation(source->counter_clockwise_stencil_pass)) {
        return InvalidStateDescriptor("The DepthStencilState descriptor is invalid.");
    }

    DepthStencilState result;
    result.setDepthBufferEnableProperty(source->depth_buffer_enable == CNA_TRUE);
    result.setDepthBufferWriteEnableProperty(source->depth_buffer_write_enable == CNA_TRUE);
    result.setDepthBufferFunctionProperty(
        static_cast<CompareFunction>(source->depth_buffer_function));
    result.setStencilEnableProperty(source->stencil_enable == CNA_TRUE);
    result.setStencilFunctionProperty(static_cast<CompareFunction>(source->stencil_function));
    result.setStencilMaskProperty(source->stencil_mask);
    result.setStencilWriteMaskProperty(source->stencil_write_mask);
    result.setReferenceStencilProperty(source->reference_stencil);
    result.setStencilFailProperty(static_cast<StencilOperation>(source->stencil_fail));
    result.setStencilDepthBufferFailProperty(
        static_cast<StencilOperation>(source->stencil_depth_buffer_fail));
    result.setStencilPassProperty(static_cast<StencilOperation>(source->stencil_pass));
    result.setTwoSidedStencilModeProperty(source->two_sided_stencil_mode == CNA_TRUE);
    result.setCounterClockwiseStencilFunctionProperty(
        static_cast<CompareFunction>(source->counter_clockwise_stencil_function));
    result.setCounterClockwiseStencilFailProperty(
        static_cast<StencilOperation>(source->counter_clockwise_stencil_fail));
    result.setCounterClockwiseStencilDepthBufferFailProperty(
        static_cast<StencilOperation>(source->counter_clockwise_stencil_depth_buffer_fail));
    result.setCounterClockwiseStencilPassProperty(
        static_cast<StencilOperation>(source->counter_clockwise_stencil_pass));
    *destination = result;
    return CNA_RESULT_SUCCESS;
}

CNA_Result ToNativeRasterizerState(
    const CNA_RasterizerState* const source,
    RasterizerState* const destination)
{
    if (source == nullptr || destination == nullptr ||
        source->struct_size < sizeof(CNA_RasterizerState) ||
        source->struct_version != StructureVersion || source->reserved[0] != 0U ||
        source->reserved[1] != 0U || !IsCullMode(source->cull_mode) ||
        !IsFillMode(source->fill_mode) || !std::isfinite(source->depth_bias) ||
        !std::isfinite(source->slope_scale_depth_bias) ||
        !IsBool(source->multi_sample_anti_alias) || !IsBool(source->scissor_test_enable)) {
        return InvalidStateDescriptor("The RasterizerState descriptor is invalid.");
    }

    RasterizerState result;
    result.setCullModeProperty(static_cast<CullMode>(source->cull_mode));
    result.setFillModeProperty(static_cast<FillMode>(source->fill_mode));
    result.setDepthBiasProperty(source->depth_bias);
    result.setSlopeScaleDepthBiasProperty(source->slope_scale_depth_bias);
    result.setMultiSampleAntiAliasProperty(source->multi_sample_anti_alias == CNA_TRUE);
    result.setScissorTestEnableProperty(source->scissor_test_enable == CNA_TRUE);
    *destination = result;
    return CNA_RESULT_SUCCESS;
}

CNA_Result ToNativeSamplerState(
    const CNA_SamplerState* const source,
    SamplerState* const destination)
{
    if (source == nullptr || destination == nullptr ||
        source->struct_size < sizeof(CNA_SamplerState) ||
        source->struct_version != StructureVersion || source->reserved != 0U ||
        !IsTextureAddressMode(source->address_u) ||
        !IsTextureAddressMode(source->address_v) ||
        !IsTextureAddressMode(source->address_w) || !IsTextureFilter(source->filter) ||
        !std::isfinite(source->mip_map_level_of_detail_bias)) {
        return InvalidStateDescriptor("The SamplerState descriptor is invalid.");
    }

    SamplerState result;
    result.setAddressUProperty(static_cast<TextureAddressMode>(source->address_u));
    result.setAddressVProperty(static_cast<TextureAddressMode>(source->address_v));
    result.setAddressWProperty(static_cast<TextureAddressMode>(source->address_w));
    result.setFilterProperty(static_cast<TextureFilter>(source->filter));
    result.setMaxAnisotropyProperty(source->max_anisotropy);
    result.setMaxMipLevelProperty(source->max_mip_level);
    result.setMipMapLevelOfDetailBiasProperty(source->mip_map_level_of_detail_bias);
    *destination = result;
    return CNA_RESULT_SUCCESS;
}

void ToCBlendState(const BlendState& source, CNA_BlendState* const destination) noexcept
{
    *destination = CNA_BlendState{
        .struct_size = sizeof(CNA_BlendState),
        .struct_version = StructureVersion,
        .alpha_blend_function = static_cast<CNA_BlendFunction>(
            source.getAlphaBlendFunctionProperty()),
        .alpha_destination_blend = static_cast<CNA_Blend>(
            source.getAlphaDestinationBlendProperty()),
        .alpha_source_blend = static_cast<CNA_Blend>(source.getAlphaSourceBlendProperty()),
        .color_blend_function = static_cast<CNA_BlendFunction>(
            source.getColorBlendFunctionProperty()),
        .color_destination_blend = static_cast<CNA_Blend>(
            source.getColorDestinationBlendProperty()),
        .color_source_blend = static_cast<CNA_Blend>(source.getColorSourceBlendProperty()),
        .color_write_channels = static_cast<CNA_ColorWriteChannels>(
            source.getColorWriteChannelsProperty()),
        .color_write_channels1 = static_cast<CNA_ColorWriteChannels>(
            source.getColorWriteChannels1Property()),
        .color_write_channels2 = static_cast<CNA_ColorWriteChannels>(
            source.getColorWriteChannels2Property()),
        .color_write_channels3 = static_cast<CNA_ColorWriteChannels>(
            source.getColorWriteChannels3Property()),
        .blend_factor = CNA_Color{
            source.getBlendFactorProperty().getRProperty(),
            source.getBlendFactorProperty().getGProperty(),
            source.getBlendFactorProperty().getBProperty(),
            source.getBlendFactorProperty().getAProperty()},
        .multi_sample_mask = source.getMultiSampleMaskProperty()};
}

void ToCDepthStencilState(
    const DepthStencilState& source,
    CNA_DepthStencilState* const destination) noexcept
{
    *destination = CNA_DepthStencilState{
        .struct_size = sizeof(CNA_DepthStencilState),
        .struct_version = StructureVersion,
        .depth_buffer_enable = source.getDepthBufferEnableProperty() ? CNA_TRUE : CNA_FALSE,
        .depth_buffer_write_enable = source.getDepthBufferWriteEnableProperty()
            ? CNA_TRUE : CNA_FALSE,
        .stencil_enable = source.getStencilEnableProperty() ? CNA_TRUE : CNA_FALSE,
        .two_sided_stencil_mode = source.getTwoSidedStencilModeProperty()
            ? CNA_TRUE : CNA_FALSE,
        .depth_buffer_function = static_cast<CNA_CompareFunction>(
            source.getDepthBufferFunctionProperty()),
        .stencil_function = static_cast<CNA_CompareFunction>(
            source.getStencilFunctionProperty()),
        .stencil_mask = source.getStencilMaskProperty(),
        .stencil_write_mask = source.getStencilWriteMaskProperty(),
        .reference_stencil = source.getReferenceStencilProperty(),
        .stencil_fail = static_cast<CNA_StencilOperation>(source.getStencilFailProperty()),
        .stencil_depth_buffer_fail = static_cast<CNA_StencilOperation>(
            source.getStencilDepthBufferFailProperty()),
        .stencil_pass = static_cast<CNA_StencilOperation>(source.getStencilPassProperty()),
        .counter_clockwise_stencil_function = static_cast<CNA_CompareFunction>(
            source.getCounterClockwiseStencilFunctionProperty()),
        .counter_clockwise_stencil_fail = static_cast<CNA_StencilOperation>(
            source.getCounterClockwiseStencilFailProperty()),
        .counter_clockwise_stencil_depth_buffer_fail = static_cast<CNA_StencilOperation>(
            source.getCounterClockwiseStencilDepthBufferFailProperty()),
        .counter_clockwise_stencil_pass = static_cast<CNA_StencilOperation>(
            source.getCounterClockwiseStencilPassProperty()),
        .reserved = 0U};
}

void ToCRasterizerState(
    const RasterizerState& source,
    CNA_RasterizerState* const destination) noexcept
{
    *destination = CNA_RasterizerState{
        .struct_size = sizeof(CNA_RasterizerState),
        .struct_version = StructureVersion,
        .cull_mode = static_cast<CNA_CullMode>(source.getCullModeProperty()),
        .fill_mode = static_cast<CNA_FillMode>(source.getFillModeProperty()),
        .depth_bias = source.getDepthBiasProperty(),
        .slope_scale_depth_bias = source.getSlopeScaleDepthBiasProperty(),
        .multi_sample_anti_alias = source.getMultiSampleAntiAliasProperty()
            ? CNA_TRUE : CNA_FALSE,
        .scissor_test_enable = source.getScissorTestEnableProperty() ? CNA_TRUE : CNA_FALSE,
        .reserved = {0U, 0U}};
}

void ToCSamplerState(const SamplerState& source, CNA_SamplerState* const destination) noexcept
{
    *destination = CNA_SamplerState{
        .struct_size = sizeof(CNA_SamplerState),
        .struct_version = StructureVersion,
        .address_u = static_cast<CNA_TextureAddressMode>(source.getAddressUProperty()),
        .address_v = static_cast<CNA_TextureAddressMode>(source.getAddressVProperty()),
        .address_w = static_cast<CNA_TextureAddressMode>(source.getAddressWProperty()),
        .filter = static_cast<CNA_TextureFilter>(source.getFilterProperty()),
        .max_anisotropy = source.getMaxAnisotropyProperty(),
        .max_mip_level = source.getMaxMipLevelProperty(),
        .mip_map_level_of_detail_bias = source.getMipMapLevelOfDetailBiasProperty(),
        .reserved = 0U};
}

} // namespace CNA::C::Detail

CNA_Result cna_blend_state_init(
    const CNA_BlendStatePreset preset,
    CNA_BlendState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (outState == nullptr) {
            return InvalidStateDescriptor("The BlendState output is null.");
        }
        switch (preset) {
            case CNA_BLEND_STATE_PRESET_DEFAULT: {
                const BlendState state;
                ToCBlendState(state, outState);
                return CNA_RESULT_SUCCESS;
            }
            case CNA_BLEND_STATE_PRESET_ADDITIVE:
                ToCBlendState(BlendState::Additive, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_BLEND_STATE_PRESET_ALPHA_BLEND:
                ToCBlendState(BlendState::AlphaBlend, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_BLEND_STATE_PRESET_NON_PREMULTIPLIED:
                ToCBlendState(BlendState::NonPremultiplied, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_BLEND_STATE_PRESET_OPAQUE:
                ToCBlendState(BlendState::Opaque, outState);
                return CNA_RESULT_SUCCESS;
            default:
                return InvalidStateDescriptor("The BlendState preset is invalid.");
        }
    });
}

CNA_Result cna_depth_stencil_state_init(
    const CNA_DepthStencilStatePreset preset,
    CNA_DepthStencilState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (outState == nullptr) {
            return InvalidStateDescriptor("The DepthStencilState output is null.");
        }
        switch (preset) {
            case CNA_DEPTH_STENCIL_STATE_PRESET_DEFAULT:
                ToCDepthStencilState(DepthStencilState::Default, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_DEPTH_STENCIL_STATE_PRESET_DEPTH_READ:
                ToCDepthStencilState(DepthStencilState::DepthRead, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_DEPTH_STENCIL_STATE_PRESET_NONE:
                ToCDepthStencilState(DepthStencilState::None, outState);
                return CNA_RESULT_SUCCESS;
            default:
                return InvalidStateDescriptor("The DepthStencilState preset is invalid.");
        }
    });
}

CNA_Result cna_rasterizer_state_init(
    const CNA_RasterizerStatePreset preset,
    CNA_RasterizerState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (outState == nullptr) {
            return InvalidStateDescriptor("The RasterizerState output is null.");
        }
        switch (preset) {
            case CNA_RASTERIZER_STATE_PRESET_DEFAULT: {
                const RasterizerState state;
                ToCRasterizerState(state, outState);
                return CNA_RESULT_SUCCESS;
            }
            case CNA_RASTERIZER_STATE_PRESET_CULL_CLOCKWISE:
                ToCRasterizerState(RasterizerState::CullClockwise, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_RASTERIZER_STATE_PRESET_CULL_COUNTER_CLOCKWISE:
                ToCRasterizerState(RasterizerState::CullCounterClockwise, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_RASTERIZER_STATE_PRESET_CULL_NONE:
                ToCRasterizerState(RasterizerState::CullNone, outState);
                return CNA_RESULT_SUCCESS;
            default:
                return InvalidStateDescriptor("The RasterizerState preset is invalid.");
        }
    });
}

CNA_Result cna_sampler_state_init(
    const CNA_SamplerStatePreset preset,
    CNA_SamplerState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (outState == nullptr) {
            return InvalidStateDescriptor("The SamplerState output is null.");
        }
        switch (preset) {
            case CNA_SAMPLER_STATE_PRESET_DEFAULT: {
                const SamplerState state;
                ToCSamplerState(state, outState);
                return CNA_RESULT_SUCCESS;
            }
            case CNA_SAMPLER_STATE_PRESET_ANISOTROPIC_CLAMP:
                ToCSamplerState(SamplerState::AnisotropicClamp, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_SAMPLER_STATE_PRESET_ANISOTROPIC_WRAP:
                ToCSamplerState(SamplerState::AnisotropicWrap, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_SAMPLER_STATE_PRESET_LINEAR_CLAMP:
                ToCSamplerState(SamplerState::LinearClamp, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_SAMPLER_STATE_PRESET_LINEAR_WRAP:
                ToCSamplerState(SamplerState::LinearWrap, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_SAMPLER_STATE_PRESET_POINT_CLAMP:
                ToCSamplerState(SamplerState::PointClamp, outState);
                return CNA_RESULT_SUCCESS;
            case CNA_SAMPLER_STATE_PRESET_POINT_WRAP:
                ToCSamplerState(SamplerState::PointWrap, outState);
                return CNA_RESULT_SUCCESS;
            default:
                return InvalidStateDescriptor("The SamplerState preset is invalid.");
        }
    });
}

CNA_Result cna_graphics_device_get_blend_state(
    const CNA_Handle graphicsDeviceHandle,
    CNA_BlendState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasSupportedOutputHeader(outState)) {
            return InvalidStateDescriptor("The BlendState output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ToCBlendState(graphicsDevice->value->getBlendStateProperty(), outState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_set_blend_state(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_BlendState* const state)
{
    return CallWithExceptionBarrier([&]() {
        BlendState nativeState;
        if (const CNA_Result result = ToNativeBlendState(state, &nativeState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        graphicsDevice->value->setBlendStateProperty(nativeState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_depth_stencil_state(
    const CNA_Handle graphicsDeviceHandle,
    CNA_DepthStencilState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasSupportedOutputHeader(outState)) {
            return InvalidStateDescriptor("The DepthStencilState output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ToCDepthStencilState(graphicsDevice->value->getDepthStencilStateProperty(), outState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_set_depth_stencil_state(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_DepthStencilState* const state)
{
    return CallWithExceptionBarrier([&]() {
        DepthStencilState nativeState;
        if (const CNA_Result result = ToNativeDepthStencilState(state, &nativeState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        graphicsDevice->value->setDepthStencilStateProperty(nativeState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_rasterizer_state(
    const CNA_Handle graphicsDeviceHandle,
    CNA_RasterizerState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasSupportedOutputHeader(outState)) {
            return InvalidStateDescriptor("The RasterizerState output structure is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        ToCRasterizerState(graphicsDevice->value->getRasterizerStateProperty(), outState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_set_rasterizer_state(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_RasterizerState* const state)
{
    return CallWithExceptionBarrier([&]() {
        RasterizerState nativeState;
        if (const CNA_Result result = ToNativeRasterizerState(state, &nativeState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        graphicsDevice->value->setRasterizerStateProperty(nativeState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_get_sampler_state(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShaderStage stage,
    const uint32_t slot,
    CNA_SamplerState* const outState)
{
    return CallWithExceptionBarrier([&]() {
        if (!HasSupportedOutputHeader(outState) ||
            (stage != CNA_SHADER_STAGE_PIXEL && stage != CNA_SHADER_STAGE_VERTEX) ||
            slot >= CNA_MAX_SAMPLERS) {
            return InvalidStateDescriptor("The sampler-state query is invalid.");
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const SamplerState& nativeState = stage == CNA_SHADER_STAGE_PIXEL
            ? graphicsDevice->value->getSamplerStatesProperty()[static_cast<int>(slot)]
            : graphicsDevice->value->getVertexSamplerStatesProperty()[static_cast<int>(slot)];
        ToCSamplerState(nativeState, outState);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_graphics_device_set_sampler_state(
    const CNA_Handle graphicsDeviceHandle,
    const CNA_ShaderStage stage,
    const uint32_t slot,
    const CNA_SamplerState* const state)
{
    return CallWithExceptionBarrier([&]() {
        if ((stage != CNA_SHADER_STAGE_PIXEL && stage != CNA_SHADER_STAGE_VERTEX) ||
            slot >= CNA_MAX_SAMPLERS) {
            return InvalidStateDescriptor("The sampler-state slot is invalid.");
        }
        SamplerState nativeState;
        if (const CNA_Result result = ToNativeSamplerState(state, &nativeState);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<BorrowedGraphicsDevice> graphicsDevice;
        if (const CNA_Result result = GetBorrowedGraphicsDevice(
                graphicsDeviceHandle, &graphicsDevice);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (stage == CNA_SHADER_STAGE_PIXEL) {
            graphicsDevice->value->getSamplerStatesProperty()[static_cast<int>(slot)] = nativeState;
        } else {
            graphicsDevice->value->getVertexSamplerStatesProperty()[static_cast<int>(slot)] =
                nativeState;
        }
        return CNA_RESULT_SUCCESS;
    });
}
