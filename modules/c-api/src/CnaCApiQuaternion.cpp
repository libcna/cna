// SPDX-License-Identifier: MS-PL

#include "CNA/C/quaternion.h"
#include "CnaCApiDetail.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstring>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyFormattedText;
using CNA::C::Detail::Fail;
using CNA::C::Detail::StoreOutput;
using CNA::C::Detail::ToC;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ToNative;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

} // namespace

CNA_Result cna_quaternion_init_xyzw(
    const float x,
    const float y,
    const float z,
    const float w,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion(x, y, z, w));
    });
}

CNA_Result cna_quaternion_init_vector3_w(
    const CNA_Vector3 vectorPart,
    const float scalarPart,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion(ToNative(vectorPart), scalarPart));
    });
}

CNA_Result cna_quaternion_get_identity(CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [] {
        return ToC(Quaternion::Identity);
    });
}

CNA_Result cna_quaternion_conjugate_in_place(CNA_Quaternion* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Quaternion is null.");
        }
        Quaternion native = ToNative(*value);
        native.Conjugate();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_quaternion_equals(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToCBool(ToNative(left).Equals(ToNative(right)));
    });
}

CNA_Result cna_quaternion_not_equals(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToCBool(ToNative(left) != ToNative(right));
    });
}

CNA_Result cna_quaternion_get_hash_code(
    const CNA_Quaternion value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(value).GetHashCode());
    });
}

CNA_Result cna_quaternion_length(
    const CNA_Quaternion value,
    float* const outLength)
{
    return StoreOutput(outLength, "The float output is null.", [=] {
        return ToNative(value).Length();
    });
}

CNA_Result cna_quaternion_length_squared(
    const CNA_Quaternion value,
    float* const outLengthSquared)
{
    return StoreOutput(outLengthSquared, "The float output is null.", [=] {
        return ToNative(value).LengthSquared();
    });
}

CNA_Result cna_quaternion_normalize_in_place(CNA_Quaternion* const value)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Quaternion is null.");
        }
        Quaternion native = ToNative(*value);
        native.Normalize();
        *value = ToC(native);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_quaternion_get_string_size(
    const CNA_Quaternion value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_quaternion_copy_string(
    const CNA_Quaternion value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedText(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_quaternion_add(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Add(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_quaternion_concatenate(
    const CNA_Quaternion first,
    const CNA_Quaternion second,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Concatenate(ToNative(first), ToNative(second)));
    });
}

CNA_Result cna_quaternion_conjugate(
    const CNA_Quaternion value,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Conjugate(ToNative(value)));
    });
}

CNA_Result cna_quaternion_create_from_axis_angle(
    const CNA_Vector3 axis,
    const float angle,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::CreateFromAxisAngle(ToNative(axis), angle));
    });
}

CNA_Result cna_quaternion_create_from_rotation_matrix(
    const CNA_Matrix matrix,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::CreateFromRotationMatrix(ToNative(matrix)));
    });
}

CNA_Result cna_quaternion_create_from_yaw_pitch_roll(
    const float yaw,
    const float pitch,
    const float roll,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::CreateFromYawPitchRoll(yaw, pitch, roll));
    });
}

CNA_Result cna_quaternion_divide(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Divide(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_quaternion_dot(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    float* const outValue)
{
    return StoreOutput(outValue, "The float output is null.", [=] {
        return Quaternion::Dot(ToNative(left), ToNative(right));
    });
}

CNA_Result cna_quaternion_inverse(
    const CNA_Quaternion value,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Inverse(ToNative(value)));
    });
}

CNA_Result cna_quaternion_lerp(
    const CNA_Quaternion value1,
    const CNA_Quaternion value2,
    const float amount,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Lerp(ToNative(value1), ToNative(value2), amount));
    });
}

CNA_Result cna_quaternion_slerp(
    const CNA_Quaternion value1,
    const CNA_Quaternion value2,
    const float amount,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Slerp(ToNative(value1), ToNative(value2), amount));
    });
}

CNA_Result cna_quaternion_subtract(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Subtract(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_quaternion_multiply(
    const CNA_Quaternion left,
    const CNA_Quaternion right,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Multiply(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_quaternion_multiply_scalar(
    const CNA_Quaternion value,
    const float scale,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Multiply(ToNative(value), scale));
    });
}

CNA_Result cna_quaternion_negate(
    const CNA_Quaternion value,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Negate(ToNative(value)));
    });
}

CNA_Result cna_quaternion_normalize(
    const CNA_Quaternion value,
    CNA_Quaternion* const outValue)
{
    return StoreOutput(outValue, "The Quaternion output is null.", [=] {
        return ToC(Quaternion::Normalize(ToNative(value)));
    });
}
