// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_VECTORS_H
#define CNA_C_VECTORS_H

#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes a zero Vector2.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_init(CNA_Vector2* out_value);

/**
 * @brief Initializes a Vector2 from two components.
 * @param x X component.
 * @param y Y component.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_init_xy(float x, float y, CNA_Vector2* out_value);

/**
 * @brief Initializes both Vector2 components from one scalar.
 * @param value Component value.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_init_scalar(float value, CNA_Vector2* out_value);

/**
 * @brief Gets the zero Vector2.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_get_zero(CNA_Vector2* out_value);

/**
 * @brief Gets the all-one Vector2.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_get_one(CNA_Vector2* out_value);

/**
 * @brief Gets the positive X unit Vector2.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_get_unit_x(CNA_Vector2* out_value);

/**
 * @brief Gets the positive Y unit Vector2.
 * @param out_value Receives the vector.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_get_unit_y(CNA_Vector2* out_value);

/**
 * @brief Tests two Vector2 values for equality.
 * @param left First vector.
 * @param right Second vector.
 * @param out_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_equals(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Bool* out_equal);

/**
 * @brief Tests two Vector2 values for inequality.
 * @param left First vector.
 * @param right Second vector.
 * @param out_not_equal Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_not_equals(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Bool* out_not_equal);

/**
 * @brief Computes the canonical Vector2 hash code.
 * @param value Vector to hash.
 * @param out_hash Receives the hash code.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_get_hash_code(CNA_Vector2 value, int32_t* out_hash);

/**
 * @brief Computes a Vector2 length.
 * @param value Source vector.
 * @param out_length Receives the length.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_length(CNA_Vector2 value, float* out_length);

/**
 * @brief Computes a squared Vector2 length.
 * @param value Source vector.
 * @param out_length_squared Receives the squared length.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_length_squared(
    CNA_Vector2 value,
    float* out_length_squared);

/**
 * @brief Normalizes a Vector2 in place.
 * @param value Vector to mutate.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_normalize_in_place(CNA_Vector2* value);

/**
 * @brief Gets the canonical Vector2 string byte count.
 * @param value Vector to format.
 * @param out_bytes Receives the byte count without a terminator.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_get_string_size(CNA_Vector2 value, uint64_t* out_bytes);

/**
 * @brief Copies the canonical Vector2 UTF-8 string without a terminator.
 * @param value Vector to format.
 * @param destination Destination bytes, or null only for zero capacity.
 * @param capacity Destination byte capacity.
 * @param out_bytes Receives the required byte count.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_copy_string(
    CNA_Vector2 value,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Adds two Vector2 values component-wise.
 * @param left First vector.
 * @param right Second vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_add(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Vector2* out_value);

/**
 * @brief Performs Vector2 barycentric interpolation.
 * @param value1 First vector.
 * @param value2 Second vector.
 * @param value3 Third vector.
 * @param amount1 Second-vector weight.
 * @param amount2 Third-vector weight.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_barycentric(
    CNA_Vector2 value1,
    CNA_Vector2 value2,
    CNA_Vector2 value3,
    float amount1,
    float amount2,
    CNA_Vector2* out_value);

/**
 * @brief Performs Vector2 Catmull-Rom interpolation.
 * @param value1 First control vector.
 * @param value2 Second control vector.
 * @param value3 Third control vector.
 * @param value4 Fourth control vector.
 * @param amount Interpolation amount.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_catmull_rom(
    CNA_Vector2 value1,
    CNA_Vector2 value2,
    CNA_Vector2 value3,
    CNA_Vector2 value4,
    float amount,
    CNA_Vector2* out_value);

/**
 * @brief Clamps Vector2 components between matching bounds.
 * @param value Vector to clamp.
 * @param minimum Minimum components.
 * @param maximum Maximum components.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_clamp(
    CNA_Vector2 value,
    CNA_Vector2 minimum,
    CNA_Vector2 maximum,
    CNA_Vector2* out_value);

/**
 * @brief Computes the distance between two Vector2 values.
 * @param left First vector.
 * @param right Second vector.
 * @param out_distance Receives the distance.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_distance(
    CNA_Vector2 left,
    CNA_Vector2 right,
    float* out_distance);

/**
 * @brief Computes the squared distance between two Vector2 values.
 * @param left First vector.
 * @param right Second vector.
 * @param out_distance_squared Receives the squared distance.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_distance_squared(
    CNA_Vector2 left,
    CNA_Vector2 right,
    float* out_distance_squared);

/**
 * @brief Divides two Vector2 values component-wise.
 * @param left Dividend vector.
 * @param right Divisor vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_divide(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Vector2* out_value);

/**
 * @brief Divides a Vector2 by a scalar.
 * @param value Dividend vector.
 * @param divider Scalar divisor.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_divide_scalar(
    CNA_Vector2 value,
    float divider,
    CNA_Vector2* out_value);

/**
 * @brief Computes a Vector2 dot product.
 * @param left First vector.
 * @param right Second vector.
 * @param out_value Receives the scalar result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_dot(
    CNA_Vector2 left,
    CNA_Vector2 right,
    float* out_value);

/**
 * @brief Performs Vector2 Hermite interpolation.
 * @param value1 Source vector.
 * @param tangent1 Source tangent.
 * @param value2 Destination vector.
 * @param tangent2 Destination tangent.
 * @param amount Interpolation amount.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_hermite(
    CNA_Vector2 value1,
    CNA_Vector2 tangent1,
    CNA_Vector2 value2,
    CNA_Vector2 tangent2,
    float amount,
    CNA_Vector2* out_value);

/**
 * @brief Performs Vector2 linear interpolation.
 * @param value1 Source vector.
 * @param value2 Destination vector.
 * @param amount Interpolation amount.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_lerp(
    CNA_Vector2 value1,
    CNA_Vector2 value2,
    float amount,
    CNA_Vector2* out_value);

/**
 * @brief Selects component-wise Vector2 maxima.
 * @param left First vector.
 * @param right Second vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_max(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Vector2* out_value);

/**
 * @brief Selects component-wise Vector2 minima.
 * @param left First vector.
 * @param right Second vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_min(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Vector2* out_value);

/**
 * @brief Multiplies two Vector2 values component-wise.
 * @param left First vector.
 * @param right Second vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_multiply(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Vector2* out_value);

/**
 * @brief Multiplies a Vector2 by a scalar.
 * @param value Source vector.
 * @param scale Scalar multiplier.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_multiply_scalar(
    CNA_Vector2 value,
    float scale,
    CNA_Vector2* out_value);

/**
 * @brief Negates a Vector2.
 * @param value Source vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_negate(CNA_Vector2 value, CNA_Vector2* out_value);

/**
 * @brief Returns a normalized Vector2 copy.
 * @param value Source vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_normalize(CNA_Vector2 value, CNA_Vector2* out_value);

/**
 * @brief Reflects a Vector2 across a normal.
 * @param value Source vector.
 * @param normal Reflection normal.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_reflect(
    CNA_Vector2 value,
    CNA_Vector2 normal,
    CNA_Vector2* out_value);

/**
 * @brief Performs clamped Vector2 smooth-step interpolation.
 * @param value1 Source vector.
 * @param value2 Destination vector.
 * @param amount Interpolation amount.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_smooth_step(
    CNA_Vector2 value1,
    CNA_Vector2 value2,
    float amount,
    CNA_Vector2* out_value);

/**
 * @brief Subtracts two Vector2 values component-wise.
 * @param left First vector.
 * @param right Second vector.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_subtract(
    CNA_Vector2 left,
    CNA_Vector2 right,
    CNA_Vector2* out_value);

/**
 * @brief Transforms one Vector2 position by a matrix.
 * @param value Source vector.
 * @param matrix Transformation matrix.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_transform_matrix(
    CNA_Vector2 value,
    CNA_Matrix matrix,
    CNA_Vector2* out_value);

/**
 * @brief Transforms a Vector2 array range by a matrix.
 * @param source Source array, or null only when @p source_count is zero.
 * @param source_count Source array element count.
 * @param source_index First source element.
 * @param matrix Transformation matrix.
 * @param destination Destination array, or null only when @p destination_count is zero.
 * @param destination_count Destination array element count.
 * @param destination_index First destination element.
 * @param length Number of elements to transform.
 * @return A CNA result code; range validation occurs before any destination write.
 */
CNA_C_API CNA_Result cna_vector2_transform_matrix_array(
    const CNA_Vector2* source,
    uint64_t source_count,
    uint64_t source_index,
    CNA_Matrix matrix,
    CNA_Vector2* destination,
    uint64_t destination_count,
    uint64_t destination_index,
    uint64_t length);

/**
 * @brief Transforms one Vector2 by a quaternion.
 * @param value Source vector.
 * @param rotation Quaternion rotation.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_transform_quaternion(
    CNA_Vector2 value,
    CNA_Quaternion rotation,
    CNA_Vector2* out_value);

/**
 * @brief Transforms a Vector2 array range by a quaternion.
 * @param source Source array, or null only when @p source_count is zero.
 * @param source_count Source array element count.
 * @param source_index First source element.
 * @param rotation Quaternion rotation.
 * @param destination Destination array, or null only when @p destination_count is zero.
 * @param destination_count Destination array element count.
 * @param destination_index First destination element.
 * @param length Number of elements to transform.
 * @return A CNA result code; range validation occurs before any destination write.
 */
CNA_C_API CNA_Result cna_vector2_transform_quaternion_array(
    const CNA_Vector2* source,
    uint64_t source_count,
    uint64_t source_index,
    CNA_Quaternion rotation,
    CNA_Vector2* destination,
    uint64_t destination_count,
    uint64_t destination_index,
    uint64_t length);

/**
 * @brief Transforms one Vector2 normal by a matrix without translation.
 * @param value Source normal.
 * @param matrix Transformation matrix.
 * @param out_value Receives the result.
 * @return A CNA result code.
 */
CNA_C_API CNA_Result cna_vector2_transform_normal(
    CNA_Vector2 value,
    CNA_Matrix matrix,
    CNA_Vector2* out_value);

/**
 * @brief Transforms a Vector2 normal array range by a matrix without translation.
 * @param source Source array, or null only when @p source_count is zero.
 * @param source_count Source array element count.
 * @param source_index First source element.
 * @param matrix Transformation matrix.
 * @param destination Destination array, or null only when @p destination_count is zero.
 * @param destination_count Destination array element count.
 * @param destination_index First destination element.
 * @param length Number of elements to transform.
 * @return A CNA result code; range validation occurs before any destination write.
 */
CNA_C_API CNA_Result cna_vector2_transform_normal_array(
    const CNA_Vector2* source,
    uint64_t source_count,
    uint64_t source_index,
    CNA_Matrix matrix,
    CNA_Vector2* destination,
    uint64_t destination_count,
    uint64_t destination_index,
    uint64_t length);

#ifdef __cplusplus
}
#endif

#endif
