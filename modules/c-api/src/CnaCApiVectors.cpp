// SPDX-License-Identifier: MS-PL

#include "CNA/C/vectors.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <cstring>
#include <limits>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

template<typename TElement>
[[nodiscard]] CNA_Result ValidateArrayRange(
    const TElement* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    TElement* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length) noexcept
{
    if ((source == nullptr && sourceCount != 0U) ||
        (destination == nullptr && destinationCount != 0U) ||
        sourceIndex > sourceCount || length > sourceCount - sourceIndex ||
        destinationIndex > destinationCount || length > destinationCount - destinationIndex) {
        return InvalidArgument("The array pointer, count, index or length is invalid.");
    }
    constexpr uint64_t MaximumSize = static_cast<uint64_t>(
        std::numeric_limits<std::size_t>::max());
    if (sourceCount > MaximumSize || destinationCount > MaximumSize) {
        return Fail(
            CNA_RESULT_OVERFLOW,
            CNA_ERROR_CATEGORY_RANGE,
            "An array count cannot be represented by the native platform.");
    }
    return CNA_RESULT_SUCCESS;
}

/// Converts each element in the range, transforms it, and writes it back converted.
template<typename TElement, typename TTransform>
[[nodiscard]] CNA_Result TransformArray(
    const TElement* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    TElement* const destination,
    const uint64_t destinationCount,
    const uint64_t destinationIndex,
    const uint64_t length,
    TTransform&& transform) noexcept
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
            const auto input = ToNative(source[sourceIndex + index]);
            destination[destinationIndex + index] = ToC(transform(input));
        }
        return CNA_RESULT_SUCCESS;
    });
}

} // namespace

CNA_Result cna_vector2_normalize_in_place(CNA_Vector2* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return InvalidArgument("The Vector2 is null.");
        }
        Vector2 native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
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

CNA_Result cna_vector3_normalize_in_place(CNA_Vector3* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return InvalidArgument("The Vector3 is null.");
        }
        Vector3 native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vector3_transform_matrix_array(
    const CNA_Vector3* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Matrix matrix,
    CNA_Vector3* const destination,
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
        [&nativeMatrix](const Vector3 value) {
            return Vector3::Transform(value, nativeMatrix);
        });
}

CNA_Result cna_vector3_transform_quaternion_array(
    const CNA_Vector3* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Quaternion rotation,
    CNA_Vector3* const destination,
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
        [&nativeRotation](const Vector3 value) {
            return Vector3::Transform(value, nativeRotation);
        });
}

CNA_Result cna_vector3_transform_normal_array(
    const CNA_Vector3* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Matrix matrix,
    CNA_Vector3* const destination,
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
        [&nativeMatrix](const Vector3 value) {
            return Vector3::TransformNormal(value, nativeMatrix);
        });
}

CNA_Result cna_vector4_normalize_in_place(CNA_Vector4* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return InvalidArgument("The Vector4 is null.");
        }
        Vector4 native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_vector4_transform_matrix_array(
    const CNA_Vector4* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Matrix matrix,
    CNA_Vector4* const destination,
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
        [&nativeMatrix](const Vector4 value) {
            return Vector4::Transform(value, nativeMatrix);
        });
}

CNA_Result cna_vector4_transform_quaternion_array(
    const CNA_Vector4* const source,
    const uint64_t sourceCount,
    const uint64_t sourceIndex,
    const CNA_Quaternion rotation,
    CNA_Vector4* const destination,
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
        [&nativeRotation](const Vector4 value) {
            return Vector4::Transform(value, nativeRotation);
        });
}

// The routes generated from modules/c-api/spec/Vectors.toml.
#include "generated/Vectors.inc"
