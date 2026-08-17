// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_MATRIX_H
#define CNA_C_MATRIX_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initializes a zero-filled matrix. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_init(CNA_Matrix* out_value);

/**
 * @brief Initializes all 16 row-major matrix fields.
 * @param m11 Row 1, column 1. @param m12 Row 1, column 2.
 * @param m13 Row 1, column 3. @param m14 Row 1, column 4.
 * @param m21 Row 2, column 1. @param m22 Row 2, column 2.
 * @param m23 Row 2, column 3. @param m24 Row 2, column 4.
 * @param m31 Row 3, column 1. @param m32 Row 3, column 2.
 * @param m33 Row 3, column 3. @param m34 Row 3, column 4.
 * @param m41 Row 4, column 1. @param m42 Row 4, column 2.
 * @param m43 Row 4, column 3. @param m44 Row 4, column 4.
 * @param out_value Receives the matrix.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_matrix_init_values(
    float m11, float m12, float m13, float m14,
    float m21, float m22, float m23, float m24,
    float m31, float m32, float m33, float m34,
    float m41, float m42, float m43, float m44,
    CNA_Matrix* out_value);

/** @brief Gets the identity matrix. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_identity(CNA_Matrix* out_value);

/** @brief Gets the backward direction. @param value Source matrix. @param out_direction Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_backward(CNA_Matrix value, CNA_Vector3* out_direction);
/** @brief Sets the backward direction. @param value Matrix to mutate. @param direction Direction value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_backward(CNA_Matrix* value, CNA_Vector3 direction);
/** @brief Gets the down direction. @param value Source matrix. @param out_direction Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_down(CNA_Matrix value, CNA_Vector3* out_direction);
/** @brief Sets the down direction. @param value Matrix to mutate. @param direction Direction value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_down(CNA_Matrix* value, CNA_Vector3 direction);
/** @brief Gets the forward direction. @param value Source matrix. @param out_direction Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_forward(CNA_Matrix value, CNA_Vector3* out_direction);
/** @brief Sets the forward direction. @param value Matrix to mutate. @param direction Direction value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_forward(CNA_Matrix* value, CNA_Vector3 direction);
/** @brief Gets the left direction. @param value Source matrix. @param out_direction Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_left(CNA_Matrix value, CNA_Vector3* out_direction);
/** @brief Sets the left direction. @param value Matrix to mutate. @param direction Direction value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_left(CNA_Matrix* value, CNA_Vector3 direction);
/** @brief Gets the right direction. @param value Source matrix. @param out_direction Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_right(CNA_Matrix value, CNA_Vector3* out_direction);
/** @brief Sets the right direction. @param value Matrix to mutate. @param direction Direction value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_right(CNA_Matrix* value, CNA_Vector3 direction);
/** @brief Gets the translation. @param value Source matrix. @param out_translation Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_translation(CNA_Matrix value, CNA_Vector3* out_translation);
/** @brief Sets the translation. @param value Matrix to mutate. @param translation Translation value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_translation(CNA_Matrix* value, CNA_Vector3 translation);
/** @brief Gets the up direction. @param value Source matrix. @param out_direction Receives the vector. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_up(CNA_Matrix value, CNA_Vector3* out_direction);
/** @brief Sets the up direction. @param value Matrix to mutate. @param direction Direction value. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_set_up(CNA_Matrix* value, CNA_Vector3 direction);

/**
 * @brief Decomposes a matrix into scale, rotation and translation.
 * @param value Source matrix.
 * @param out_scale Receives scale, including on decomposition failure.
 * @param out_rotation Receives rotation, including Identity on singular scale.
 * @param out_translation Receives translation.
 * @param out_decomposed Receives whether decomposition succeeded.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_matrix_decompose(
    CNA_Matrix value,
    CNA_Vector3* out_scale,
    CNA_Quaternion* out_rotation,
    CNA_Vector3* out_translation,
    CNA_Bool* out_decomposed);
/** @brief Computes a determinant. @param value Source matrix. @param out_determinant Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_determinant(CNA_Matrix value, float* out_determinant);
/** @brief Tests matrix equality. @param left First matrix. @param right Second matrix. @param out_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_equals(CNA_Matrix left, CNA_Matrix right, CNA_Bool* out_equal);
/** @brief Tests matrix inequality. @param left First matrix. @param right Second matrix. @param out_not_equal Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_not_equals(
    CNA_Matrix left,
    CNA_Matrix right,
    CNA_Bool* out_not_equal);
/** @brief Computes a matrix hash. @param value Source matrix. @param out_hash Receives the hash. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_hash_code(CNA_Matrix value, int32_t* out_hash);
/** @brief Gets a matrix string byte count. @param value Matrix to format. @param out_bytes Receives the count. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_get_string_size(CNA_Matrix value, uint64_t* out_bytes);
/** @brief Copies the canonical matrix UTF-8 string without a terminator. @param value Matrix to format. @param destination Destination bytes, or null only for zero capacity. @param capacity Destination byte capacity. @param out_bytes Receives the required count. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_copy_string(
    CNA_Matrix value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Adds matrices component-wise. @param left First matrix. @param right Second matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_add(CNA_Matrix left, CNA_Matrix right, CNA_Matrix* out_value);

/**
 * @brief Creates a spherical billboard.
 * @param object_position Object position.
 * @param camera_position Camera position.
 * @param camera_up Camera up direction.
 * @param camera_forward Optional camera forward direction, or null.
 * @param out_value Receives the matrix.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_matrix_create_billboard(
    CNA_Vector3 object_position,
    CNA_Vector3 camera_position,
    CNA_Vector3 camera_up,
    const CNA_Vector3* camera_forward,
    CNA_Matrix* out_value);

/**
 * @brief Creates a constrained billboard.
 * @param object_position Object position.
 * @param camera_position Camera position.
 * @param rotate_axis Constraining axis.
 * @param camera_forward Optional camera forward direction, or null.
 * @param object_forward Optional object forward direction, or null.
 * @param out_value Receives the matrix.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_matrix_create_constrained_billboard(
    CNA_Vector3 object_position,
    CNA_Vector3 camera_position,
    CNA_Vector3 rotate_axis,
    const CNA_Vector3* camera_forward,
    const CNA_Vector3* object_forward,
    CNA_Matrix* out_value);

/** @brief Creates a rotation from an axis and angle. @param axis Rotation axis. @param angle Angle in radians. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_from_axis_angle(
    CNA_Vector3 axis,
    float angle,
    CNA_Matrix* out_value);
/** @brief Creates a rotation from a quaternion. @param quaternion Source quaternion. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_from_quaternion(
    CNA_Quaternion quaternion,
    CNA_Matrix* out_value);
/** @brief Creates a rotation from yaw, pitch and roll. @param yaw Y-axis angle. @param pitch X-axis angle. @param roll Z-axis angle. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_from_yaw_pitch_roll(
    float yaw,
    float pitch,
    float roll,
    CNA_Matrix* out_value);
/** @brief Creates a right-handed view matrix. @param camera_position Camera position. @param camera_target Camera target. @param camera_up Camera up direction. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_look_at(
    CNA_Vector3 camera_position,
    CNA_Vector3 camera_target,
    CNA_Vector3 camera_up,
    CNA_Matrix* out_value);
/** @brief Creates an orthographic projection. @param width View width. @param height View height. @param near_plane Near plane. @param far_plane Far plane. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_orthographic(
    float width,
    float height,
    float near_plane,
    float far_plane,
    CNA_Matrix* out_value);
/** @brief Creates an off-center orthographic projection. @param left Left plane. @param right Right plane. @param bottom Bottom plane. @param top Top plane. @param near_plane Near plane. @param far_plane Far plane. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_orthographic_off_center(
    float left,
    float right,
    float bottom,
    float top,
    float near_plane,
    float far_plane,
    CNA_Matrix* out_value);
/** @brief Creates a perspective projection. @param width Near-plane width. @param height Near-plane height. @param near_plane Near distance. @param far_plane Far distance. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_perspective(
    float width,
    float height,
    float near_plane,
    float far_plane,
    CNA_Matrix* out_value);
/** @brief Creates a field-of-view perspective projection. @param field_of_view Vertical field of view. @param aspect_ratio Width/height ratio. @param near_plane Near distance. @param far_plane Far distance. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_perspective_field_of_view(
    float field_of_view,
    float aspect_ratio,
    float near_plane,
    float far_plane,
    CNA_Matrix* out_value);
/** @brief Creates an off-center perspective projection. @param left Near-plane left. @param right Near-plane right. @param bottom Near-plane bottom. @param top Near-plane top. @param near_plane Near distance. @param far_plane Far distance. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_perspective_off_center(
    float left,
    float right,
    float bottom,
    float top,
    float near_plane,
    float far_plane,
    CNA_Matrix* out_value);
/** @brief Creates an X-axis rotation. @param radians Rotation angle. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_rotation_x(float radians, CNA_Matrix* out_value);
/** @brief Creates a Y-axis rotation. @param radians Rotation angle. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_rotation_y(float radians, CNA_Matrix* out_value);
/** @brief Creates a Z-axis rotation. @param radians Rotation angle. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_rotation_z(float radians, CNA_Matrix* out_value);
/** @brief Creates a uniform scale. @param scale Scale factor. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_scale_scalar(float scale, CNA_Matrix* out_value);
/** @brief Creates a nonuniform scale from components. @param x_scale X scale. @param y_scale Y scale. @param z_scale Z scale. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_scale_xyz(
    float x_scale,
    float y_scale,
    float z_scale,
    CNA_Matrix* out_value);
/** @brief Creates a nonuniform scale from a vector. @param scales Scale vector. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_scale_vector3(
    CNA_Vector3 scales,
    CNA_Matrix* out_value);
/** @brief Creates a directional shadow matrix. @param light_direction Light direction. @param plane Projection plane. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_shadow(
    CNA_Vector3 light_direction,
    CNA_Plane plane,
    CNA_Matrix* out_value);
/** @brief Creates a translation from components. @param x X translation. @param y Y translation. @param z Z translation. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_translation_xyz(
    float x,
    float y,
    float z,
    CNA_Matrix* out_value);
/** @brief Creates a translation from a vector. @param position Translation vector. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_translation_vector3(
    CNA_Vector3 position,
    CNA_Matrix* out_value);
/** @brief Creates a reflection matrix. @param plane Reflection plane. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_reflection(CNA_Plane plane, CNA_Matrix* out_value);
/** @brief Creates a world matrix. @param position World position. @param forward Forward direction. @param up Up direction. @param out_value Receives the matrix. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_create_world(
    CNA_Vector3 position,
    CNA_Vector3 forward,
    CNA_Vector3 up,
    CNA_Matrix* out_value);
/** @brief Divides matrices component-wise. @param left Dividend. @param right Divisor. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_divide(CNA_Matrix left, CNA_Matrix right, CNA_Matrix* out_value);
/** @brief Divides a matrix by a scalar. @param value Dividend. @param divider Divisor. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_divide_scalar(
    CNA_Matrix value,
    float divider,
    CNA_Matrix* out_value);
/** @brief Inverts a matrix. @param value Source matrix. @param out_value Receives the inverse, including IEEE non-finite components for singular input. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_invert(CNA_Matrix value, CNA_Matrix* out_value);
/** @brief Linearly interpolates matrices. @param value1 Source matrix. @param value2 Destination matrix. @param amount Interpolation amount. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_lerp(
    CNA_Matrix value1,
    CNA_Matrix value2,
    float amount,
    CNA_Matrix* out_value);
/** @brief Multiplies matrices. @param left First matrix. @param right Second matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_multiply(
    CNA_Matrix left,
    CNA_Matrix right,
    CNA_Matrix* out_value);
/** @brief Multiplies a matrix by a scalar. @param value Source matrix. @param scale Multiplier. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_multiply_scalar(
    CNA_Matrix value,
    float scale,
    CNA_Matrix* out_value);
/** @brief Negates every matrix component. @param value Source matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_negate(CNA_Matrix value, CNA_Matrix* out_value);
/** @brief Subtracts matrices component-wise. @param left First matrix. @param right Second matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_subtract(
    CNA_Matrix left,
    CNA_Matrix right,
    CNA_Matrix* out_value);
/** @brief Transposes a matrix. @param value Source matrix. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_transpose(CNA_Matrix value, CNA_Matrix* out_value);
/** @brief Applies a quaternion rotation to a matrix. @param value Source matrix. @param rotation Quaternion rotation. @param out_value Receives the result. @return A CNA result code. */
CNA_C_API CNA_Result cna_matrix_transform(
    CNA_Matrix value,
    CNA_Quaternion rotation,
    CNA_Matrix* out_value);

#ifdef __cplusplus
}
#endif

#endif
