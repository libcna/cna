// SPDX-License-Identifier: MS-PL

#include "CNA/C/matrix.h"
#include "CnaCApiDetail.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstring>
#include <optional>
#include <string>
#include <utility>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::Fail;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

[[nodiscard]] Matrix ToNative(const CNA_Matrix value)
{
    return Matrix(
        value.m11, value.m12, value.m13, value.m14,
        value.m21, value.m22, value.m23, value.m24,
        value.m31, value.m32, value.m33, value.m34,
        value.m41, value.m42, value.m43, value.m44);
}

[[nodiscard]] Vector3 ToNative(const CNA_Vector3 value)
{
    return Vector3(value.x, value.y, value.z);
}

[[nodiscard]] Quaternion ToNative(const CNA_Quaternion value)
{
    return Quaternion(value.x, value.y, value.z, value.w);
}

[[nodiscard]] Plane ToNative(const CNA_Plane value)
{
    return Plane(ToNative(value.normal), value.d);
}

[[nodiscard]] std::optional<Vector3> ToNativeOptional(const CNA_Vector3* const value)
{
    if (value == nullptr) {
        return std::nullopt;
    }
    return ToNative(*value);
}

[[nodiscard]] CNA_Matrix ToC(const Matrix value) noexcept
{
    return CNA_Matrix{
        value.M11, value.M12, value.M13, value.M14,
        value.M21, value.M22, value.M23, value.M24,
        value.M31, value.M32, value.M33, value.M34,
        value.M41, value.M42, value.M43, value.M44};
}

[[nodiscard]] CNA_Vector3 ToC(const Vector3 value) noexcept
{
    return CNA_Vector3{value.X, value.Y, value.Z};
}

[[nodiscard]] CNA_Quaternion ToC(const Quaternion value) noexcept
{
    return CNA_Quaternion{value.X, value.Y, value.Z, value.W};
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
[[nodiscard]] CNA_Result MutateMatrix(
    CNA_Matrix* const value,
    TCallable&& callable) noexcept
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (value == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "The Matrix is null.");
        }
        Matrix native = ToNative(*value);
        std::forward<TCallable>(callable)(native);
        *value = ToC(native);
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

} // namespace

CNA_Result cna_matrix_init(CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [] {
        return ToC(Matrix());
    });
}

CNA_Result cna_matrix_init_values(
    const float m11, const float m12, const float m13, const float m14,
    const float m21, const float m22, const float m23, const float m24,
    const float m31, const float m32, const float m33, const float m34,
    const float m41, const float m42, const float m43, const float m44,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix(
            m11, m12, m13, m14,
            m21, m22, m23, m24,
            m31, m32, m33, m34,
            m41, m42, m43, m44));
    });
}

CNA_Result cna_matrix_get_identity(CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [] {
        return ToC(Matrix::getIdentityProperty());
    });
}

CNA_Result cna_matrix_get_backward(
    const CNA_Matrix value,
    CNA_Vector3* const outDirection)
{
    return StoreOutput(outDirection, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getBackwardProperty());
    });
}

CNA_Result cna_matrix_set_backward(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setBackwardProperty(ToNative(direction));
    });
}

CNA_Result cna_matrix_get_down(
    const CNA_Matrix value,
    CNA_Vector3* const outDirection)
{
    return StoreOutput(outDirection, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getDownProperty());
    });
}

CNA_Result cna_matrix_set_down(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setDownProperty(ToNative(direction));
    });
}

CNA_Result cna_matrix_get_forward(
    const CNA_Matrix value,
    CNA_Vector3* const outDirection)
{
    return StoreOutput(outDirection, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getForwardProperty());
    });
}

CNA_Result cna_matrix_set_forward(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setForwardProperty(ToNative(direction));
    });
}

CNA_Result cna_matrix_get_left(
    const CNA_Matrix value,
    CNA_Vector3* const outDirection)
{
    return StoreOutput(outDirection, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getLeftProperty());
    });
}

CNA_Result cna_matrix_set_left(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setLeftProperty(ToNative(direction));
    });
}

CNA_Result cna_matrix_get_right(
    const CNA_Matrix value,
    CNA_Vector3* const outDirection)
{
    return StoreOutput(outDirection, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getRightProperty());
    });
}

CNA_Result cna_matrix_set_right(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setRightProperty(ToNative(direction));
    });
}

CNA_Result cna_matrix_get_translation(
    const CNA_Matrix value,
    CNA_Vector3* const outTranslation)
{
    return StoreOutput(outTranslation, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getTranslationProperty());
    });
}

CNA_Result cna_matrix_set_translation(
    CNA_Matrix* const value,
    const CNA_Vector3 translation)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setTranslationProperty(ToNative(translation));
    });
}

CNA_Result cna_matrix_get_up(
    const CNA_Matrix value,
    CNA_Vector3* const outDirection)
{
    return StoreOutput(outDirection, "The Vector3 output is null.", [=] {
        return ToC(ToNative(value).getUpProperty());
    });
}

CNA_Result cna_matrix_set_up(
    CNA_Matrix* const value,
    const CNA_Vector3 direction)
{
    return MutateMatrix(value, [=](Matrix& native) {
        native.setUpProperty(ToNative(direction));
    });
}

CNA_Result cna_matrix_decompose(
    const CNA_Matrix value,
    CNA_Vector3* const outScale,
    CNA_Quaternion* const outRotation,
    CNA_Vector3* const outTranslation,
    CNA_Bool* const outDecomposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outScale == nullptr || outRotation == nullptr ||
            outTranslation == nullptr || outDecomposed == nullptr) {
            return Fail(
                CNA_RESULT_INVALID_ARGUMENT,
                CNA_ERROR_CATEGORY_ARGUMENT,
                "A Matrix decomposition output is null.");
        }
        Vector3 scale;
        Quaternion rotation = Quaternion::Identity;
        Vector3 translation;
        const bool decomposed = ToNative(value).Decompose(scale, rotation, translation);
        *outScale = ToC(scale);
        *outRotation = ToC(rotation);
        *outTranslation = ToC(translation);
        *outDecomposed = decomposed ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_matrix_determinant(
    const CNA_Matrix value,
    float* const outDeterminant)
{
    return StoreOutput(outDeterminant, "The float output is null.", [=] {
        return ToNative(value).Determinant();
    });
}

CNA_Result cna_matrix_equals(
    const CNA_Matrix left,
    const CNA_Matrix right,
    CNA_Bool* const outEqual)
{
    return StoreOutput(outEqual, "The Boolean output is null.", [=] {
        return ToNative(left).Equals(ToNative(right)) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_matrix_not_equals(
    const CNA_Matrix left,
    const CNA_Matrix right,
    CNA_Bool* const outNotEqual)
{
    return StoreOutput(outNotEqual, "The Boolean output is null.", [=] {
        return ToNative(left) != ToNative(right) ? CNA_TRUE : CNA_FALSE;
    });
}

CNA_Result cna_matrix_get_hash_code(
    const CNA_Matrix value,
    int32_t* const outHash)
{
    return StoreOutput(outHash, "The hash output is null.", [=] {
        return static_cast<int32_t>(ToNative(value).GetHashCode());
    });
}

CNA_Result cna_matrix_get_string_size(
    const CNA_Matrix value,
    uint64_t* const outBytes)
{
    return StoreOutput(outBytes, "The required-byte output is null.", [=] {
        return static_cast<uint64_t>(ToNative(value).ToString().size());
    });
}

CNA_Result cna_matrix_copy_string(
    const CNA_Matrix value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CopyFormattedString(destination, capacity, outBytes, [=] {
        return ToNative(value).ToString();
    });
}

CNA_Result cna_matrix_add(
    const CNA_Matrix left,
    const CNA_Matrix right,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Add(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_matrix_create_billboard(
    const CNA_Vector3 objectPosition,
    const CNA_Vector3 cameraPosition,
    const CNA_Vector3 cameraUp,
    const CNA_Vector3* const cameraForward,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateBillboard(
            ToNative(objectPosition),
            ToNative(cameraPosition),
            ToNative(cameraUp),
            ToNativeOptional(cameraForward)));
    });
}

CNA_Result cna_matrix_create_constrained_billboard(
    const CNA_Vector3 objectPosition,
    const CNA_Vector3 cameraPosition,
    const CNA_Vector3 rotateAxis,
    const CNA_Vector3* const cameraForward,
    const CNA_Vector3* const objectForward,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateConstrainedBillboard(
            ToNative(objectPosition),
            ToNative(cameraPosition),
            ToNative(rotateAxis),
            ToNativeOptional(cameraForward),
            ToNativeOptional(objectForward)));
    });
}

CNA_Result cna_matrix_create_from_axis_angle(
    const CNA_Vector3 axis,
    const float angle,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateFromAxisAngle(ToNative(axis), angle));
    });
}

CNA_Result cna_matrix_create_from_quaternion(
    const CNA_Quaternion quaternion,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateFromQuaternion(ToNative(quaternion)));
    });
}

CNA_Result cna_matrix_create_from_yaw_pitch_roll(
    const float yaw,
    const float pitch,
    const float roll,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateFromYawPitchRoll(yaw, pitch, roll));
    });
}

CNA_Result cna_matrix_create_look_at(
    const CNA_Vector3 cameraPosition,
    const CNA_Vector3 cameraTarget,
    const CNA_Vector3 cameraUp,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateLookAt(
            ToNative(cameraPosition), ToNative(cameraTarget), ToNative(cameraUp)));
    });
}

CNA_Result cna_matrix_create_orthographic(
    const float width,
    const float height,
    const float nearPlane,
    const float farPlane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateOrthographic(width, height, nearPlane, farPlane));
    });
}

CNA_Result cna_matrix_create_orthographic_off_center(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float nearPlane,
    const float farPlane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateOrthographicOffCenter(
            left, right, bottom, top, nearPlane, farPlane));
    });
}

CNA_Result cna_matrix_create_perspective(
    const float width,
    const float height,
    const float nearPlane,
    const float farPlane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreatePerspective(width, height, nearPlane, farPlane));
    });
}

CNA_Result cna_matrix_create_perspective_field_of_view(
    const float fieldOfView,
    const float aspectRatio,
    const float nearPlane,
    const float farPlane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreatePerspectiveFieldOfView(
            fieldOfView, aspectRatio, nearPlane, farPlane));
    });
}

CNA_Result cna_matrix_create_perspective_off_center(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float nearPlane,
    const float farPlane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreatePerspectiveOffCenter(
            left, right, bottom, top, nearPlane, farPlane));
    });
}

CNA_Result cna_matrix_create_rotation_x(
    const float radians,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateRotationX(radians));
    });
}

CNA_Result cna_matrix_create_rotation_y(
    const float radians,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateRotationY(radians));
    });
}

CNA_Result cna_matrix_create_rotation_z(
    const float radians,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateRotationZ(radians));
    });
}

CNA_Result cna_matrix_create_scale_scalar(
    const float scale,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateScale(scale));
    });
}

CNA_Result cna_matrix_create_scale_xyz(
    const float xScale,
    const float yScale,
    const float zScale,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateScale(xScale, yScale, zScale));
    });
}

CNA_Result cna_matrix_create_scale_vector3(
    const CNA_Vector3 scales,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateScale(ToNative(scales)));
    });
}

CNA_Result cna_matrix_create_shadow(
    const CNA_Vector3 lightDirection,
    const CNA_Plane plane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateShadow(ToNative(lightDirection), ToNative(plane)));
    });
}

CNA_Result cna_matrix_create_translation_xyz(
    const float x,
    const float y,
    const float z,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateTranslation(x, y, z));
    });
}

CNA_Result cna_matrix_create_translation_vector3(
    const CNA_Vector3 position,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateTranslation(ToNative(position)));
    });
}

CNA_Result cna_matrix_create_reflection(
    const CNA_Plane plane,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateReflection(ToNative(plane)));
    });
}

CNA_Result cna_matrix_create_world(
    const CNA_Vector3 position,
    const CNA_Vector3 forward,
    const CNA_Vector3 up,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::CreateWorld(
            ToNative(position), ToNative(forward), ToNative(up)));
    });
}

CNA_Result cna_matrix_divide(
    const CNA_Matrix left,
    const CNA_Matrix right,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Divide(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_matrix_divide_scalar(
    const CNA_Matrix value,
    const float divider,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Divide(ToNative(value), divider));
    });
}

CNA_Result cna_matrix_invert(
    const CNA_Matrix value,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Invert(ToNative(value)));
    });
}

CNA_Result cna_matrix_lerp(
    const CNA_Matrix value1,
    const CNA_Matrix value2,
    const float amount,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Lerp(ToNative(value1), ToNative(value2), amount));
    });
}

CNA_Result cna_matrix_multiply(
    const CNA_Matrix left,
    const CNA_Matrix right,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Multiply(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_matrix_multiply_scalar(
    const CNA_Matrix value,
    const float scale,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Multiply(ToNative(value), scale));
    });
}

CNA_Result cna_matrix_negate(
    const CNA_Matrix value,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Negate(ToNative(value)));
    });
}

CNA_Result cna_matrix_subtract(
    const CNA_Matrix left,
    const CNA_Matrix right,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Subtract(ToNative(left), ToNative(right)));
    });
}

CNA_Result cna_matrix_transpose(
    const CNA_Matrix value,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Transpose(ToNative(value)));
    });
}

CNA_Result cna_matrix_transform(
    const CNA_Matrix value,
    const CNA_Quaternion rotation,
    CNA_Matrix* const outValue)
{
    return StoreOutput(outValue, "The Matrix output is null.", [=] {
        return ToC(Matrix::Transform(ToNative(value), ToNative(rotation)));
    });
}
