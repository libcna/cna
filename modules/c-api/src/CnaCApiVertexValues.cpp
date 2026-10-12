// SPDX-License-Identifier: MS-PL

#include "CNA/C/vertex_values.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::InternalError;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using CNA::C::Detail::TryMapVertexElementFormat;
using CNA::C::Detail::TryMapVertexElementUsage;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

[[nodiscard]] bool IsValidVertexType(const CNA_VertexType type) noexcept
{
    return type <= CNA_VERTEX_TYPE_POSITION_TEXTURE;
}

[[nodiscard]] CNA_Result ValidateVertexType(const CNA_VertexType type) noexcept
{
    if (!IsValidVertexType(type)) {
        return InvalidArgument("The built-in vertex type is invalid.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_VertexValue DefaultValueForType(const CNA_VertexType type)
{
    CNA_VertexValue result{};
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            result.position_color = ToC(VertexPositionColor());
            break;
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            result.position_color_texture = ToC(VertexPositionColorTexture(
                Vector3(), Color(0, 0, 0, 0), Vector2()));
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            result.position_normal_tangent_texture = ToC(VertexPositionNormalTangentTexture());
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            result.position_normal_tangent_texture_skinned =
                ToC(VertexPositionNormalTangentTextureSkinned());
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            result.position_normal_texture = ToC(VertexPositionNormalTexture());
            break;
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            result.position_normal_texture_skinned = ToC(VertexPositionNormalTextureSkinned());
            break;
        default:
            result.position_texture = ToC(VertexPositionTexture());
            break;
    }
    return result;
}

[[nodiscard]] bool CompareVertexValues(
    const CNA_VertexType type,
    const CNA_VertexValue& left,
    const CNA_VertexValue& right,
    const bool inequality)
{
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR: {
            const auto lhs = ToNative(left.position_color);
            const auto rhs = ToNative(right.position_color);
            return inequality ? lhs != rhs : lhs == rhs;
        }
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE: {
            const auto lhs = ToNative(left.position_color_texture);
            const auto rhs = ToNative(right.position_color_texture);
            return inequality ? lhs != rhs : lhs == rhs;
        }
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE: {
            const auto lhs = ToNative(left.position_normal_tangent_texture);
            const auto rhs = ToNative(right.position_normal_tangent_texture);
            return inequality ? lhs != rhs : lhs == rhs;
        }
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED: {
            const auto lhs = ToNative(left.position_normal_tangent_texture_skinned);
            const auto rhs = ToNative(right.position_normal_tangent_texture_skinned);
            return inequality ? lhs != rhs : lhs == rhs;
        }
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE: {
            const auto lhs = ToNative(left.position_normal_texture);
            const auto rhs = ToNative(right.position_normal_texture);
            return inequality ? lhs != rhs : lhs == rhs;
        }
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED: {
            const auto lhs = ToNative(left.position_normal_texture_skinned);
            const auto rhs = ToNative(right.position_normal_texture_skinned);
            return inequality ? lhs != rhs : lhs == rhs;
        }
        default: {
            const auto lhs = ToNative(left.position_texture);
            const auto rhs = ToNative(right.position_texture);
            return inequality ? lhs != rhs : lhs == rhs;
        }
    }
}

[[nodiscard]] int32_t VertexHashCode(
    const CNA_VertexType type,
    const CNA_VertexValue& value)
{
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            return static_cast<int32_t>(ToNative(value.position_color).GetHashCode());
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            return static_cast<int32_t>(ToNative(value.position_color_texture).GetHashCode());
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            return static_cast<int32_t>(
                ToNative(value.position_normal_tangent_texture).GetHashCode());
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            return static_cast<int32_t>(
                ToNative(value.position_normal_tangent_texture_skinned).GetHashCode());
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            return static_cast<int32_t>(ToNative(value.position_normal_texture).GetHashCode());
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            return static_cast<int32_t>(
                ToNative(value.position_normal_texture_skinned).GetHashCode());
        default:
            return static_cast<int32_t>(ToNative(value.position_texture).GetHashCode());
    }
}

[[nodiscard]] std::string VertexString(
    const CNA_VertexType type,
    const CNA_VertexValue& value)
{
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            return ToNative(value.position_color).ToString();
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            return ToNative(value.position_color_texture).ToString();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            return ToNative(value.position_normal_tangent_texture).ToString();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            return ToNative(value.position_normal_tangent_texture_skinned).ToString();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            return ToNative(value.position_normal_texture).ToString();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            return ToNative(value.position_normal_texture_skinned).ToString();
        default:
            return ToNative(value.position_texture).ToString();
    }
}

[[nodiscard]] const VertexDeclaration& DeclarationForType(const CNA_VertexType type)
{
    switch (type) {
        case CNA_VERTEX_TYPE_POSITION_COLOR:
            return VertexPositionColor::getVertexDeclarationStatic();
        case CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE:
            return VertexPositionColorTexture::getVertexDeclarationStatic();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE:
            return VertexPositionNormalTangentTexture::getVertexDeclarationStatic();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED:
            return VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE:
            return VertexPositionNormalTexture::getVertexDeclarationStatic();
        case CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED:
            return VertexPositionNormalTextureSkinned::getVertexDeclarationStatic();
        default:
            return VertexPositionTexture::getVertexDeclarationStatic();
    }
}

[[nodiscard]] CNA_Result ValidateVertexElement(const CNA_VertexElement value) noexcept
{
    VertexElementFormat format{};
    VertexElementUsage usage{};
    if (!TryMapVertexElementFormat(value.format, &format) ||
        !TryMapVertexElementUsage(value.usage, &usage)) {
        return InvalidArgument("A vertex element contains an invalid format or usage identity.");
    }
    return CNA_RESULT_SUCCESS;
}

/** A vertex element ValidateVertexElement accepted, as the canonical value. */
[[nodiscard]] VertexElement ToNativeElement(const CNA_VertexElement value)
{
    return VertexElement(
        value.offset,
        static_cast<VertexElementFormat>(value.format),
        static_cast<VertexElementUsage>(value.usage),
        value.usage_index);
}

} // namespace

CNA_Result cna_vertex_type_get_stride(
    const CNA_VertexType type,
    uint32_t* const outStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outStride == nullptr) {
            return InvalidArgument("The vertex-stride output is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int nativeStride = DeclarationForType(type).getVertexStrideProperty();
        if (nativeStride < 0) {
            return InternalError("The native vertex declaration reported a negative stride.");
        }
        *outStride = static_cast<uint32_t>(nativeStride);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_type_copy_elements(
    const CNA_VertexType type,
    CNA_VertexElement* const destination,
    const uint64_t capacity,
    uint64_t* const outElementCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outElementCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The vertex-element output buffer is invalid.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& elements = DeclarationForType(type).GetVertexElements();
        const uint64_t required = static_cast<uint64_t>(elements.size());
        *outElementCount = required;
        if (capacity < required) {
            return BufferTooSmall("The vertex-element output buffer is too small.");
        }
        for (uint64_t index = 0U; index < required; ++index) {
            destination[index] = ToC(elements[static_cast<std::size_t>(index)]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

// The routes generated from modules/c-api/spec/VertexValues.toml.
#include "generated/VertexValues.inc"
