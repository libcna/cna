// SPDX-License-Identifier: MS-PL

#include "CNA/C/vectors.h"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;

[[nodiscard]] Vector2 ToNative(const CNA_Vector2 value)
{
    return Vector2(value.x, value.y);
}

[[nodiscard]] Quaternion ToNative(const CNA_Quaternion value)
{
    return Quaternion(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Matrix ToNative(const CNA_Matrix value)
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] CNA_Vector2 ToC(const Vector2 value) noexcept
{
    return CNA_Vector2{value.X, value.Y};
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
                "The destination cannot hold the complete formatted value.");
        }
        if (!text.empty()) {
            std::memcpy(destination, text.data(), text.size());
        }
        return CNA_RESULT_SUCCESS;
    });
}

[[nodiscard]] CNA_Result ValidateArrayRange(
    const CNA_Vector2* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    CNA_Vector2* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length) noexcept
{
    if ((source == nullptr && sourceCount != 0U) ||
        (destination == nullptr && destinationCount != 0U) ||
        sourceIndex > sourceCount || length > sourceCount - sourceIndex ||
        destinationIndex > destinationCount || length > destinationCount - destinationIndex) {
        return Fail(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "The Vector2 array pointer, count, index or length is invalid.");
    }
    constexpr uint64_t MaximumSize = static_cast<uint64_t>(
        std::numeric_limits<std::size_t>::max());
    if (sourceCount > MaximumSize || destinationCount > MaximumSize) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "A Vector2 array count cannot be represented by the native platform.");
    }
    return CNA_RESULT_SUCCESS;
}

template<typename TCallable>
[[nodiscard]] CNA_Result TransformArray(
    const CNA_Vector2* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    CNA_Vector2* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        const CNA_Result validation = ValidateArrayRange(
            source,
            sourceCount,
            sourceIndex,
            destination,
            destinationCount,
            destinationIndex,
            length);
        if (validation != CNA_RESULT_SUCCESS) {
            return validation;
        }
        for (uint64_t index = 0U; index < length; ++index) {
            const Vector2 input = ToNative(source[sourceIndex + index]);
            destination[destinationIndex + index] = ToC(callable(input));
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_vector2_init(CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [] {
        return CNA_Vector2{0.0F, 0.0F};
    });
}

CNA_Result cna_vector2_init_xy(
    const float x,
    const float y,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return CNA_Vector2{x, y};
    });
}

CNA_Result cna_vector2_init_scalar(
    const float value,
    CNA_Vector2* const outValue)
{
    return cna_vector2_init_xy(value, value, outValue);
}

CNA_Result cna_vector2_get_zero(CNA_Vector2* const outValue)
{
    return cna_vector2_init(outValue);
}

CNA_Result cna_vector2_get_one(CNA_Vector2* const outValue)
{
    return cna_vector2_init_scalar(1.0F, outValue);
}

CNA_Result cna_vector2_get_unit_x(CNA_Vector2* const outValue)
{
    return cna_vector2_init_xy(1.0F, 0.0F, outValue);
}

CNA_Result cna_vector2_get_unit_y(CNA_Vector2* const outValue)
{
    return cna_vector2_init_xy(0.0F, 1.0F, outValue);
}

CNA_Result cna_vector2_equals(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_vector2_not_equals(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_vector2_get_hash_code(
    const CNA_Vector2 value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(value).GetHashCode());
    });
}

CNA_Result cna_vector2_length(
    const CNA_Vector2 value,
    float* const outLength)
{
    return StoreOutput(outLength, "The float output is null.", [=] {
        return ToNative(value).Length();
    });
}

CNA_Result cna_vector2_length_squared(
    const CNA_Vector2 value,
    float* const outLengthSquared)
{
    return StoreOutput(outLengthSquared, "The float output is null.", [=] {
        return ToNative(value).LengthSquared();
    });
}

CNA_Result cna_vector2_normalize_in_place(CNA_Vector2* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Vector2 is null.");
        }
        Vector2 native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vector2_get_string_size(
    const CNA_Vector2 value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_vector2_copy_string(
    const CNA_Vector2 value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_vector2_add(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Add(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_vector2_barycentric(
    const CNA_Vector2 value1,
    const CNA_Vector2 value2,
    const CNA_Vector2 value3,
    const float amount1,
    const float amount2,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Barycentric(
            ToNative(value1), ToNative(value2), ToNative(value3), amount1, amount2));
    });
}

CNA_Result cna_vector2_catmull_rom(
    const CNA_Vector2 value1,
    const CNA_Vector2 value2,
    const CNA_Vector2 value3,
    const CNA_Vector2 value4,
    const float amount,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::CatmullRom(
            ToNative(value1), ToNative(value2), ToNative(value3), ToNative(value4), amount));
    });
}

CNA_Result cna_vector2_clamp(
    const CNA_Vector2 value,
    const CNA_Vector2 minimum,
    const CNA_Vector2 maximum,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Clamp(ToNative(value), ToNative(minimum), ToNative(maximum)));
    });
}

CNA_Result cna_vector2_distance(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    float* const outDistance)
{
    return StoreOutput(outDistance, "The float output is null.", [=] {
        return Vector2::Distance(ToNative(left), ToNative(right));
    });
}

CNA_Result cna_vector2_distance_squared(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    float* const outDistanceSquared)
{
    return StoreOutput(outDistanceSquared, "The float output is null.", [=] {
        return Vector2::DistanceSquared(ToNative(left), ToNative(right));
    });
}

CNA_Result cna_vector2_divide(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Divide(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_vector2_divide_scalar(
    const CNA_Vector2 value,
    const float divider,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Divide(ToNative(value), divider));
    });
}

CNA_Result cna_vector2_dot(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    float* const outValue)
{
    return StoreOutput(outValue, "The float output is null.", [=] {
        return Vector2::Dot(ToNative(left), ToNative(right));
    });
}

CNA_Result cna_vector2_hermite(
    const CNA_Vector2 value1,
    const CNA_Vector2 tangent1,
    const CNA_Vector2 value2,
    const CNA_Vector2 tangent2,
    const float amount,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Hermite(
            ToNative(value1), ToNative(tangent1), ToNative(value2), ToNative(tangent2), amount));
    });
}

CNA_Result cna_vector2_lerp(
    const CNA_Vector2 value1,
    const CNA_Vector2 value2,
    const float amount,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Lerp(ToNative(value1), ToNative(value2), amount));
    });
}

CNA_Result cna_vector2_max(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Max(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_vector2_min(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Min(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_vector2_multiply(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Multiply(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_vector2_multiply_scalar(
    const CNA_Vector2 value,
    const float scale,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Multiply(ToNative(value), scale));
    });
}

CNA_Result cna_vector2_negate(
    const CNA_Vector2 value,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Negate(ToNative(value)));
    });
}

CNA_Result cna_vector2_normalize(
    const CNA_Vector2 value,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Normalize(ToNative(value)));
    });
}

CNA_Result cna_vector2_reflect(
    const CNA_Vector2 value,
    const CNA_Vector2 normal,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Reflect(ToNative(value), ToNative(normal)));
    });
}

CNA_Result cna_vector2_smooth_step(
    const CNA_Vector2 value1,
    const CNA_Vector2 value2,
    const float amount,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::SmoothStep(ToNative(value1), ToNative(value2), amount));
    });
}

CNA_Result cna_vector2_subtract(
    const CNA_Vector2 left,
    const CNA_Vector2 right,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Subtract(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_vector2_transform_matrix(
    const CNA_Vector2 value,
    const CNA_Matrix matrix,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Transform(ToNative(value), ToNative(matrix)));
    });
}

CNA_Result cna_vector2_transform_matrix_array(
    const CNA_Vector2* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Matrix matrix,
    CNA_Vector2* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length)
{
    const Matrix nativeMatrix = ToNative(matrix);
    return TransformArray(
        source,
        sourceCount,
        sourceIndex,
        destination,
        destinationCount,
        destinationIndex,
        length,
        [&nativeMatrix](const Vector2 value) {
            return Vector2::Transform(value, nativeMatrix);
        });
}

CNA_Result cna_vector2_transform_quaternion(
    const CNA_Vector2 value,
    const CNA_Quaternion rotation,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::Transform(ToNative(value), ToNative(rotation)));
    });
}

CNA_Result cna_vector2_transform_quaternion_array(
    const CNA_Vector2* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Quaternion rotation,
    CNA_Vector2* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length)
{
    const Quaternion nativeRotation = ToNative(rotation);
    return TransformArray(
        source,
        sourceCount,
        sourceIndex,
        destination,
        destinationCount,
        destinationIndex,
        length,
        [&nativeRotation](const Vector2 value) {
            return Vector2::Transform(value, nativeRotation);
        });
}

CNA_Result cna_vector2_transform_normal(
    const CNA_Vector2 value,
    const CNA_Matrix matrix,
    CNA_Vector2* const outValue)
{
    return StoreOutput(outValue, "The Vector2 output is null.", [=] {
        return ToC(Vector2::TransformNormal(ToNative(value), ToNative(matrix)));
    });
}

CNA_Result cna_vector2_transform_normal_array(
    const CNA_Vector2* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Matrix matrix,
    CNA_Vector2* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length)
{
    const Matrix nativeMatrix = ToNative(matrix);
    return TransformArray(
        source,
        sourceCount,
        sourceIndex,
        destination,
        destinationCount,
        destinationIndex,
        length,
        [&nativeMatrix](const Vector2 value) {
            return Vector2::TransformNormal(value, nativeMatrix);
        });
}
