// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_QUATERNION_H
#define CNA_C_QUATERNION_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initializes a quaternion from four components. @param x X component. @param y Y component. @param z Z component. @param w W component. @param out_value Receives the quaternion. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_init_xyzw(
    float x,
    float y,
    float z,
    float w,
    CNA_Quaternion* out_value);

/** @brief Initializes a quaternion from its vector and scalar parts. @param vector_part XYZ vector part. @param scalar_part W scalar part. @param out_value Receives the quaternion. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_init_vector3_w(
    CNA_Vector3 vector_part,
    float scalar_part,
    CNA_Quaternion* out_value);

/** @brief Gets the identity quaternion. @param out_value Receives the quaternion. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_get_identity(CNA_Quaternion* out_value);
/** @brief Conjugates a quaternion in place. @param value Quaternion to mutate. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_conjugate_in_place(CNA_Quaternion* value);
/** @brief Tests quaternion equality. @param left First quaternion. @param right Second quaternion. @param out_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_equals(
    CNA_Quaternion left,
    CNA_Quaternion right,
    CNA_Bool* out_equal);
/** @brief Tests quaternion inequality. @param left First quaternion. @param right Second quaternion. @param out_not_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_not_equals(
    CNA_Quaternion left,
    CNA_Quaternion right,
    CNA_Bool* out_not_equal);
/** @brief Computes a quaternion hash. @param value Source quaternion. @param out_hash Receives the hash. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_get_hash_code(
    CNA_Quaternion value,
    int32_t* out_hash);
/** @brief Computes quaternion length. @param value Source quaternion. @param out_length Receives the length. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_length(
    CNA_Quaternion value,
    float* out_length);
/** @brief Computes squared quaternion length. @param value Source quaternion. @param out_length_squared Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_length_squared(
    CNA_Quaternion value,
    float* out_length_squared);
/** @brief Normalizes a quaternion in place. @param value Quaternion to mutate. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_normalize_in_place(CNA_Quaternion* value);
/** @brief Gets a quaternion string byte count. @param value Quaternion to format. @param out_bytes Receives the count. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_get_string_size(
    CNA_Quaternion value,
    uint64_t* out_bytes);

/**
 * @brief Copies the canonical quaternion UTF-8 string without a terminator.
 * @param value Quaternion to format.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination byte capacity.
 * @param out_bytes Receives the required count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_quaternion_copy_string(
    CNA_Quaternion value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Adds quaternions component-wise. @param left First quaternion. @param right Second quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_add(
    CNA_Quaternion left,
    CNA_Quaternion right,
    CNA_Quaternion* out_value);
/** @brief Concatenates two quaternion rotations. @param first Rotation applied first. @param second Rotation applied second. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_concatenate(
    CNA_Quaternion first,
    CNA_Quaternion second,
    CNA_Quaternion* out_value);
/** @brief Returns a quaternion conjugate. @param value Source quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_conjugate(
    CNA_Quaternion value,
    CNA_Quaternion* out_value);
/** @brief Creates a rotation from an axis and angle. @param axis Rotation axis. @param angle Angle in radians. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_create_from_axis_angle(
    CNA_Vector3 axis,
    float angle,
    CNA_Quaternion* out_value);
/** @brief Creates a rotation from a matrix. @param matrix Source rotation matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_create_from_rotation_matrix(
    CNA_Matrix matrix,
    CNA_Quaternion* out_value);
/** @brief Creates a rotation from yaw, pitch and roll. @param yaw Y-axis angle in radians. @param pitch X-axis angle in radians. @param roll Z-axis angle in radians. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_create_from_yaw_pitch_roll(
    float yaw,
    float pitch,
    float roll,
    CNA_Quaternion* out_value);
/** @brief Divides one quaternion by another. @param left Dividend. @param right Divisor. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_divide(
    CNA_Quaternion left,
    CNA_Quaternion right,
    CNA_Quaternion* out_value);
/** @brief Computes a quaternion dot product. @param left First quaternion. @param right Second quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_dot(
    CNA_Quaternion left,
    CNA_Quaternion right,
    float* out_value);
/** @brief Returns a quaternion inverse. @param value Source quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_inverse(
    CNA_Quaternion value,
    CNA_Quaternion* out_value);
/** @brief Normalized-linearly interpolates two quaternions. @param value1 Source quaternion. @param value2 Destination quaternion. @param amount Interpolation amount. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_lerp(
    CNA_Quaternion value1,
    CNA_Quaternion value2,
    float amount,
    CNA_Quaternion* out_value);
/** @brief Spherically interpolates two quaternions. @param value1 Source quaternion. @param value2 Destination quaternion. @param amount Interpolation amount. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_slerp(
    CNA_Quaternion value1,
    CNA_Quaternion value2,
    float amount,
    CNA_Quaternion* out_value);
/** @brief Subtracts quaternions component-wise. @param left First quaternion. @param right Second quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_subtract(
    CNA_Quaternion left,
    CNA_Quaternion right,
    CNA_Quaternion* out_value);
/** @brief Multiplies two quaternions. @param left First quaternion. @param right Second quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_multiply(
    CNA_Quaternion left,
    CNA_Quaternion right,
    CNA_Quaternion* out_value);
/** @brief Multiplies a quaternion by a scalar. @param value Source quaternion. @param scale Multiplier. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_multiply_scalar(
    CNA_Quaternion value,
    float scale,
    CNA_Quaternion* out_value);
/** @brief Negates all quaternion components. @param value Source quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_negate(
    CNA_Quaternion value,
    CNA_Quaternion* out_value);
/** @brief Returns a normalized quaternion. @param value Source quaternion. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_quaternion_normalize(
    CNA_Quaternion value,
    CNA_Quaternion* out_value);

#ifdef __cplusplus
}
#endif

#endif
