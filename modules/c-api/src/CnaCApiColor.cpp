// SPDX-License-Identifier: MS-PL

#include "CNA/C/color.h"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <bit>
#include <cstring>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

[[nodiscard]] Color ToNative(const CNA_Color value)
{
    return Color(value.r, value.g, value.b, value.a);
}

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value)
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] Vector4 ToNative(const CNA_Vector4 value)
{
    return Vector4(value.x, value.y, value.z, value.w);
}

[[nodiscard]] CNA_Color ToC(const Color& value) noexcept
{
    return CNA_Color{
        value.getRProperty(),
        value.getGProperty(),
        value.getBProperty(),
        value.getAProperty()};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3& value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Vector4 ToC(const Vector4& value) noexcept
{
    return CNA_Vector4{value.X, value.Y, value.Z, value.W};
}

[[nodiscard]] int32_t PremultiplyUnchecked(
    const int32_t component,
    const int32_t alpha) noexcept
{
    // Preserve FNA's unchecked Int32 product without invoking signed-overflow UB.
    const uint32_t product =
        std::bit_cast<uint32_t>(component) * std::bit_cast<uint32_t>(alpha);
    return std::bit_cast<int32_t>(product) / 255;
}

template<typename TValue, typename TCallable>
[[nodiscard]] CNA_Result StoreOutput(
    TValue* const output,
    const char* const nullMessage,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (output == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                nullMessage);
        }
        const TValue result = std::forward<TCallable>(callable)();
        *output = result;
        return CNA_RESULT_SUCCESS;
    });
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
                "The string destination or required-byte output is invalid.");
        }
        const std::string text = std::forward<TCallable>(callable)();
        *outBytes = static_cast<uint64_t>(text.size());
        if (capacity < text.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination cannot hold the complete formatted color.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_color_init_vector4(
    const CNA_Vector4 vector,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(ToNative(vector)));
    });
}

CNA_Result cna_color_init_vector3(
    const CNA_Vector3 vector,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(ToNative(vector)));
    });
}

CNA_Result cna_color_init_float_rgb(
    const float r,
    const float g,
    const float b,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(r, g, b));
    });
}

CNA_Result cna_color_init_float_rgba(
    const float r,
    const float g,
    const float b,
    const float a,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(r, g, b, a));
    });
}

CNA_Result cna_color_init_int_rgb(
    const int32_t r,
    const int32_t g,
    const int32_t b,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(r, g, b));
    });
}

CNA_Result cna_color_init_int_rgba(
    const int32_t r,
    const int32_t g,
    const int32_t b,
    const int32_t a,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(r, g, b, a));
    });
}

CNA_Result cna_color_init_bytes_rgb(
    const uint8_t r,
    const uint8_t g,
    const uint8_t b,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(r, g, b));
    });
}

CNA_Result cna_color_init_bytes_rgba(
    const uint8_t r,
    const uint8_t g,
    const uint8_t b,
    const uint8_t a,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(r, g, b, a));
    });
}

CNA_Result cna_color_get_packed_value(
    const CNA_Color color,
    uint32_t* const outPackedValue)
{
    return StoreOutput(outPackedValue, "The packed-value output is null.", [=] {
        return static_cast<uint32_t>(ToNative(color).getPackedValueProperty());
    });
}

CNA_Result cna_color_set_packed_value(
    CNA_Color* const color,
    const uint32_t packedValue)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (color == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Color pointer is null.");
        }
        Color native = ToNative(*color);
        native.setPackedValueProperty(packedValue);
        const CNA_Color result = ToC(native);
        *color = result;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_color_get_debug_string_byte_count(
    const CNA_Color color,
    uint64_t* const outByteCount)
{
    return StoreOutput(outByteCount, "The byte-count output is null.", [=] {
        return static_cast<uint64_t>(ToNative(color).getDebugDisplayStringProperty().size());
    });
}

CNA_Result cna_color_copy_debug_string(
    const CNA_Color color,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CopyFormattedString(destination, capacity, outByteCount, [=] {
        return ToNative(color).getDebugDisplayStringProperty();
    });
}

CNA_Result cna_color_equals(
    const CNA_Color left,
    const CNA_Color right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_color_not_equals(
    const CNA_Color left,
    const CNA_Color right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_color_to_vector3(
    const CNA_Color color,
    CNA_Vector3* const outVector)
{
    return StoreOutput(outVector, "The Vector3 output is null.", [=] {
        return ToC(ToNative(color).ToVector3());
    });
}

CNA_Result cna_color_to_vector4(
    const CNA_Color color,
    CNA_Vector4* const outVector)
{
    return StoreOutput(outVector, "The Vector4 output is null.", [=] {
        return ToC(ToNative(color).ToVector4());
    });
}

CNA_Result cna_color_get_hash_code(
    const CNA_Color color,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(color).GetHashCode());
    });
}

CNA_Result cna_color_get_string_byte_count(
    const CNA_Color color,
    uint64_t* const outByteCount)
{
    return StoreOutput(outByteCount, "The byte-count output is null.", [=] {
        return static_cast<uint64_t>(ToNative(color).ToString().size());
    });
}

CNA_Result cna_color_copy_string(
    const CNA_Color color,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outByteCount)
{
    return CopyFormattedString(destination, capacity, outByteCount, [=] {
        return ToNative(color).ToString();
    });
}

CNA_Result cna_color_lerp(
    const CNA_Color value1,
    const CNA_Color value2,
    const float amount,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color::Lerp(ToNative(value1), ToNative(value2), amount));
    });
}

CNA_Result cna_color_from_non_premultiplied_vector4(
    const CNA_Vector4 vector,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color::FromNonPremultiplied(ToNative(vector)));
    });
}

CNA_Result cna_color_from_non_premultiplied_int(
    const int32_t r,
    const int32_t g,
    const int32_t b,
    const int32_t a,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color(
            PremultiplyUnchecked(r, a),
            PremultiplyUnchecked(g, a),
            PremultiplyUnchecked(b, a),
            a));
    });
}

CNA_Result cna_color_multiply(
    const CNA_Color color,
    const float scale,
    CNA_Color* const outColor)
{
    return StoreOutput(outColor, "The Color output is null.", [=] {
        return ToC(Color::Multiply(ToNative(color), scale));
    });
}

CNA_Result cna_color_pack_from_vector4(
    CNA_Color* const color,
    const CNA_Vector4 vector)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (color == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Color pointer is null.");
        }
        Color native = ToNative(*color);
        native.PackFromVector4(ToNative(vector));
        const CNA_Color result = ToC(native);
        *color = result;
        return CNA_RESULT_SUCCESS;
    });
}
