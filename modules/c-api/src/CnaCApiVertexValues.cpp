// SPDX-License-Identifier: MS-PL

#include "CNA/C/vertex_values.h"
#include "CnaCApiDetail.hpp"

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

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
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

[[nodiscard]] Vector2 ToNative(const CNA_Vector2 value) noexcept
{
    return Vector2(value.x, value.y);
}

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value) noexcept
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] Vector4 ToNative(const CNA_Vector4 value) noexcept
{
    return Vector4(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Color ToNative(const CNA_Color value)
{
    return Color(value.r, value.g, value.b, value.a);
}

[[nodiscard]] CNA_Vector2 ToC(const Vector2& value) noexcept
{
    return CNA_Vector2{value.X, value.Y};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3& value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Vector4 ToC(const Vector4& value) noexcept
{
    return CNA_Vector4{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] CNA_Color ToC(const Color& value) noexcept
{
    return CNA_Color{
        value.getRProperty(),
        value.getGProperty(),
        value.getBProperty(),
        value.getAProperty()};
}

[[nodiscard]] VertexPositionColor ToNative(const CNA_VertexPositionColor& value)
{
    return VertexPositionColor(ToNative(value.position), ToNative(value.color));
}

[[nodiscard]] VertexPositionColorTexture ToNative(
    const CNA_VertexPositionColorTexture& value)
{
    return VertexPositionColorTexture(
        ToNative(value.position),
        ToNative(value.color),
        ToNative(value.texture_coordinate));
}

[[nodiscard]] VertexPositionNormalTangentTexture ToNative(
    const CNA_VertexPositionNormalTangentTexture& value)
{
    return VertexPositionNormalTangentTexture(
        ToNative(value.position),
        ToNative(value.normal),
        ToNative(value.tangent),
        ToNative(value.texture_coordinate));
}

[[nodiscard]] VertexPositionNormalTangentTextureSkinned ToNative(
    const CNA_VertexPositionNormalTangentTextureSkinned& value)
{
    return VertexPositionNormalTangentTextureSkinned(
        ToNative(value.position),
        ToNative(value.normal),
        ToNative(value.tangent),
        ToNative(value.texture_coordinate),
        ToNative(value.blend_weight),
        std::array<uint8_t, 4>{
            value.blend_indices[0],
            value.blend_indices[1],
            value.blend_indices[2],
            value.blend_indices[3]});
}

[[nodiscard]] VertexPositionNormalTexture ToNative(
    const CNA_VertexPositionNormalTexture& value)
{
    return VertexPositionNormalTexture(
        ToNative(value.position),
        ToNative(value.normal),
        ToNative(value.texture_coordinate));
}

[[nodiscard]] VertexPositionNormalTextureSkinned ToNative(
    const CNA_VertexPositionNormalTextureSkinned& value)
{
    return VertexPositionNormalTextureSkinned(
        ToNative(value.position),
        ToNative(value.normal),
        ToNative(value.texture_coordinate),
        ToNative(value.blend_weight),
        std::array<uint8_t, 4>{
            value.blend_indices[0],
            value.blend_indices[1],
            value.blend_indices[2],
            value.blend_indices[3]});
}

[[nodiscard]] VertexPositionTexture ToNative(const CNA_VertexPositionTexture& value)
{
    return VertexPositionTexture(ToNative(value.position), ToNative(value.texture_coordinate));
}

[[nodiscard]] CNA_VertexPositionColor ToC(const VertexPositionColor& value) noexcept
{
    return CNA_VertexPositionColor{ToC(value.Position), ToC(value.Color)};
}

[[nodiscard]] CNA_VertexPositionColorTexture ToC(
    const VertexPositionColorTexture& value) noexcept
{
    return CNA_VertexPositionColorTexture{
        ToC(value.Position), ToC(value.Color), ToC(value.TextureCoordinate)};
}

[[nodiscard]] CNA_VertexPositionNormalTangentTexture ToC(
    const VertexPositionNormalTangentTexture& value) noexcept
{
    return CNA_VertexPositionNormalTangentTexture{
        ToC(value.Position), ToC(value.Normal), ToC(value.Tangent), ToC(value.TextureCoordinate)};
}

[[nodiscard]] CNA_VertexPositionNormalTangentTextureSkinned ToC(
    const VertexPositionNormalTangentTextureSkinned& value) noexcept
{
    return CNA_VertexPositionNormalTangentTextureSkinned{
        ToC(value.Position),
        ToC(value.Normal),
        ToC(value.Tangent),
        ToC(value.TextureCoordinate),
        ToC(value.BlendWeight),
        {value.BlendIndices[0], value.BlendIndices[1],
         value.BlendIndices[2], value.BlendIndices[3]}};
}

[[nodiscard]] CNA_VertexPositionNormalTexture ToC(
    const VertexPositionNormalTexture& value) noexcept
{
    return CNA_VertexPositionNormalTexture{
        ToC(value.Position), ToC(value.Normal), ToC(value.TextureCoordinate)};
}

[[nodiscard]] CNA_VertexPositionNormalTextureSkinned ToC(
    const VertexPositionNormalTextureSkinned& value) noexcept
{
    return CNA_VertexPositionNormalTextureSkinned{
        ToC(value.Position),
        ToC(value.Normal),
        ToC(value.TextureCoordinate),
        ToC(value.BlendWeight),
        {value.BlendIndices[0], value.BlendIndices[1],
         value.BlendIndices[2], value.BlendIndices[3]}};
}

[[nodiscard]] CNA_VertexPositionTexture ToC(const VertexPositionTexture& value) noexcept
{
    return CNA_VertexPositionTexture{ToC(value.Position), ToC(value.TextureCoordinate)};
}

[[nodiscard]] bool IsValidVertexType(const CNA_VertexType type) noexcept
{
    return type <= CNA_VERTEX_TYPE_POSITION_TEXTURE;
}

[[nodiscard]] CNA_Result ValidateVertexType(const CNA_VertexType type) noexcept
{
    if (!IsValidVertexType(type)) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The built-in vertex type is invalid.");
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

[[nodiscard]] bool TryMapVertexElementFormat(
    const CNA_VertexElementFormat format,
    VertexElementFormat* const outFormat) noexcept
{
    if (outFormat == nullptr || format > CNA_VERTEX_ELEMENT_FORMAT_HALF_VECTOR4) {
        return false;
    }
    *outFormat = static_cast<VertexElementFormat>(format);
    return true;
}

[[nodiscard]] bool TryMapVertexElementUsage(
    const CNA_VertexElementUsage usage,
    VertexElementUsage* const outUsage) noexcept
{
    if (outUsage == nullptr || usage > CNA_VERTEX_ELEMENT_USAGE_TESSELLATE_FACTOR) {
        return false;
    }
    *outUsage = static_cast<VertexElementUsage>(usage);
    return true;
}

[[nodiscard]] bool TryToNative(
    const CNA_VertexElement value,
    VertexElement* const outValue)
{
    VertexElementFormat format{};
    VertexElementUsage usage{};
    if (outValue == nullptr || !TryMapVertexElementFormat(value.format, &format) ||
        !TryMapVertexElementUsage(value.usage, &usage)) {
        return false;
    }
    *outValue = VertexElement(value.offset, format, usage, value.usage_index);
    return true;
}

[[nodiscard]] CNA_VertexElement ToC(const VertexElement& value) noexcept
{
    return CNA_VertexElement{
        value.getOffsetProperty(),
        static_cast<CNA_VertexElementFormat>(value.getVertexElementFormatProperty()),
        static_cast<CNA_VertexElementUsage>(value.getVertexElementUsageProperty()),
        value.getUsageIndexProperty()};
}

[[nodiscard]] CNA_Result InvalidVertexElement() noexcept
{
    return Fail(
        CNA_RESULT_INVALID_ARGUMENT,
        CNA_ERROR_CATEGORY_ARGUMENT,
        "A vertex element contains an invalid format or usage identity.");
}

template<typename TCallable>
[[nodiscard]] CNA_Result CopyFormattedString(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string output buffer is invalid.");
        }
        const std::string text = std::forward<TCallable>(callable)();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete formatted value.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_vertex_value_init_default(
    const CNA_VertexType type,
    CNA_VertexValue* const outValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outValue == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-value output is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_VertexValue value = DefaultValueForType(type);
        *outValue = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_value_equals(
    const CNA_VertexType type,
    const CNA_VertexValue* const left,
    const CNA_VertexValue* const right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outEqual == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A vertex-value equality argument is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool equal = CompareVertexValues(type, *left, *right, false) ?
            CNA_TRUE : CNA_FALSE;
        *outEqual = equal;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_value_not_equals(
    const CNA_VertexType type,
    const CNA_VertexValue* const left,
    const CNA_VertexValue* const right,
    CNA_Bool* const outNotEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (left == nullptr || right == nullptr || outNotEqual == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A vertex-value inequality argument is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Bool notEqual = CompareVertexValues(type, *left, *right, true) ?
            CNA_TRUE : CNA_FALSE;
        *outNotEqual = notEqual;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_value_get_hash_code(
    const CNA_VertexType type,
    const CNA_VertexValue* const value,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr || outHash == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A vertex-value hash argument is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int32_t hash = VertexHashCode(type, *value);
        *outHash = hash;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_value_get_string_byte_count(
    const CNA_VertexType type,
    const CNA_VertexValue* const value,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr || outByteCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A vertex-value string-count argument is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t byteCount = VertexString(type, *value).size();
        *outByteCount = byteCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_value_copy_string(
    const CNA_VertexType type,
    const CNA_VertexValue* const value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    if (value == nullptr) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The vertex value is null.");
    }
    if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return CopyFormattedString(destination, capacity, outByteCount, [=] {
        return VertexString(type, *value);
    });
}

CNA_Result cna_vertex_type_get_stride(
    const CNA_VertexType type,
    uint32_t* const outStride)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outStride == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-stride output is null.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const int nativeStride = DeclarationForType(type).getVertexStrideProperty();
        if (nativeStride < 0) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The native vertex declaration reported a negative stride.");
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
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The vertex-element output buffer is invalid.");
        }
        if (const CNA_Result result = ValidateVertexType(type); result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto& elements = DeclarationForType(type).GetVertexElements();
        const uint64_t required = static_cast<uint64_t>(elements.size());
        *outElementCount = required;
        if (capacity < required) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The vertex-element output buffer is too small.");
        }
        for (uint64_t index = 0U; index < required; ++index) {
            destination[index] = ToC(elements[static_cast<std::size_t>(index)]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_element_equals(
    const CNA_VertexElement left,
    const CNA_VertexElement right,
    CNA_Bool* const outEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEqual == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean output is null.");
        }
        VertexElement nativeLeft;
        VertexElement nativeRight;
        if (!TryToNative(left, &nativeLeft) || !TryToNative(right, &nativeRight)) {
            return InvalidVertexElement();
        }
        const CNA_Bool equal = nativeLeft == nativeRight ? CNA_TRUE : CNA_FALSE;
        *outEqual = equal;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_element_not_equals(
    const CNA_VertexElement left,
    const CNA_VertexElement right,
    CNA_Bool* const outNotEqual)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outNotEqual == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Boolean output is null.");
        }
        VertexElement nativeLeft;
        VertexElement nativeRight;
        if (!TryToNative(left, &nativeLeft) || !TryToNative(right, &nativeRight)) {
            return InvalidVertexElement();
        }
        const CNA_Bool notEqual = nativeLeft != nativeRight ? CNA_TRUE : CNA_FALSE;
        *outNotEqual = notEqual;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_element_get_hash_code(
    const CNA_VertexElement value,
    int32_t* const outHash)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHash == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The hash-code output is null.");
        }
        VertexElement native;
        if (!TryToNative(value, &native)) {
            return InvalidVertexElement();
        }
        const int32_t hash = static_cast<int32_t>(native.GetHashCode());
        *outHash = hash;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_element_get_string_byte_count(
    const CNA_VertexElement value,
    uint64_t* const outByteCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outByteCount == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The string byte-count output is null.");
        }
        VertexElement native;
        if (!TryToNative(value, &native)) {
            return InvalidVertexElement();
        }
        const uint64_t byteCount = native.ToString().size();
        *outByteCount = byteCount;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vertex_element_copy_string(
    const CNA_VertexElement value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    VertexElement native;
    if (!TryToNative(value, &native)) {
        return InvalidVertexElement();
    }
    return CopyFormattedString(destination, capacity, outByteCount, [&] {
        return native.ToString();
    });
}
