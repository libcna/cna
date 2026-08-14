// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <string.h>

static int near_float(float left, float right)
{
    return fabsf(left - right) <= 0.0001F;
}

static int vector2_near(CNA_Vector2 value, float x, float y)
{
    return near_float(value.x, x) && near_float(value.y, y);
}

static CNA_Matrix translation_matrix(float x, float y)
{
    CNA_Matrix matrix = {0};
    matrix.m11 = 1.0F;
    matrix.m22 = 1.0F;
    matrix.m33 = 1.0F;
    matrix.m44 = 1.0F;
    matrix.m41 = x;
    matrix.m42 = y;
    return matrix;
}

static int validate_construction_and_members(void)
{
    CNA_Vector2 zero = {9.0F, 9.0F};
    CNA_Vector2 one;
    CNA_Vector2 unit_x;
    CNA_Vector2 unit_y;
    CNA_Vector2 value;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    float scalar = 0.0F;
    if (cna_vector2_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vector2_init(&zero) != CNA_RESULT_SUCCESS || !vector2_near(zero, 0.0F, 0.0F) ||
        cna_vector2_init_xy(3.0F, 4.0F, &value) != CNA_RESULT_SUCCESS ||
        !vector2_near(value, 3.0F, 4.0F) ||
        cna_vector2_init_scalar(2.0F, &one) != CNA_RESULT_SUCCESS ||
        !vector2_near(one, 2.0F, 2.0F) ||
        cna_vector2_get_zero(&zero) != CNA_RESULT_SUCCESS ||
        cna_vector2_get_one(&one) != CNA_RESULT_SUCCESS || !vector2_near(one, 1.0F, 1.0F) ||
        cna_vector2_get_unit_x(&unit_x) != CNA_RESULT_SUCCESS ||
        !vector2_near(unit_x, 1.0F, 0.0F) ||
        cna_vector2_get_unit_y(&unit_y) != CNA_RESULT_SUCCESS ||
        !vector2_near(unit_y, 0.0F, 1.0F) ||
        cna_vector2_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vector2_not_equals(value, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vector2_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        cna_vector2_get_hash_code(value, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vector2_length(value, &scalar) != CNA_RESULT_SUCCESS || !near_float(scalar, 5.0F) ||
        cna_vector2_length_squared(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 25.0F)) {
        return 0;
    }

    if (cna_vector2_normalize_in_place(&value) != CNA_RESULT_SUCCESS ||
        !vector2_near(value, 0.6F, 0.8F) ||
        cna_vector2_normalize_in_place(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    value = (CNA_Vector2){3.0F, 4.0F};
    static const char Expected[] = "{X:3 Y:4}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'v';
    if (cna_vector2_get_string_size(value, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_vector2_copy_string(value, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'v' ||
        cna_vector2_copy_string(value, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_arithmetic(void)
{
    const CNA_Vector2 a = {2.0F, 4.0F};
    const CNA_Vector2 b = {1.0F, -2.0F};
    CNA_Vector2 result;
    float scalar = 0.0F;
    if (cna_vector2_add(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 3.0F, 2.0F) ||
        cna_vector2_subtract(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 1.0F, 6.0F) ||
        cna_vector2_multiply(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 2.0F, -8.0F) ||
        cna_vector2_multiply_scalar(a, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 1.0F, 2.0F) ||
        cna_vector2_divide(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 2.0F, -2.0F) ||
        cna_vector2_divide_scalar(a, 2.0F, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 1.0F, 2.0F) ||
        cna_vector2_negate(a, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, -2.0F, -4.0F) ||
        cna_vector2_dot(a, b, &scalar) != CNA_RESULT_SUCCESS || scalar != -6.0F ||
        cna_vector2_distance(a, b, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, sqrtf(37.0F)) ||
        cna_vector2_distance_squared(a, b, &scalar) != CNA_RESULT_SUCCESS || scalar != 37.0F) {
        return 0;
    }

    if (cna_vector2_barycentric(
            (CNA_Vector2){0.0F, 0.0F},
            (CNA_Vector2){2.0F, 4.0F},
            (CNA_Vector2){4.0F, 8.0F},
            0.25F,
            0.5F,
            &result) != CNA_RESULT_SUCCESS || !vector2_near(result, 2.5F, 5.0F) ||
        cna_vector2_catmull_rom(
            (CNA_Vector2){0.0F, 0.0F},
            (CNA_Vector2){1.0F, 2.0F},
            (CNA_Vector2){2.0F, 4.0F},
            (CNA_Vector2){3.0F, 6.0F},
            0.5F,
            &result) != CNA_RESULT_SUCCESS || !vector2_near(result, 1.5F, 3.0F) ||
        cna_vector2_clamp(
            (CNA_Vector2){-2.0F, 9.0F},
            (CNA_Vector2){0.0F, 1.0F},
            (CNA_Vector2){3.0F, 5.0F},
            &result) != CNA_RESULT_SUCCESS || !vector2_near(result, 0.0F, 5.0F) ||
        cna_vector2_hermite(
            (CNA_Vector2){0.0F, 0.0F},
            (CNA_Vector2){0.0F, 0.0F},
            (CNA_Vector2){10.0F, 20.0F},
            (CNA_Vector2){0.0F, 0.0F},
            0.5F,
            &result) != CNA_RESULT_SUCCESS || !vector2_near(result, 5.0F, 10.0F) ||
        cna_vector2_lerp(a, b, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 1.5F, 1.0F) ||
        cna_vector2_max(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 2.0F, 4.0F) ||
        cna_vector2_min(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 1.0F, -2.0F) ||
        cna_vector2_normalize((CNA_Vector2){3.0F, 4.0F}, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 0.6F, 0.8F) ||
        cna_vector2_reflect((CNA_Vector2){1.0F, -1.0F}, (CNA_Vector2){0.0F, 1.0F}, &result) !=
            CNA_RESULT_SUCCESS || !vector2_near(result, 1.0F, 1.0F) ||
        cna_vector2_smooth_step(a, b, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector2_near(result, 1.5F, 1.0F) ||
        cna_vector2_add(a, b, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_vector2_divide_scalar(a, 0.0F, &result) != CNA_RESULT_SUCCESS ||
        !isinf(result.x) || !isinf(result.y)) {
        return 0;
    }
    return 1;
}

static int validate_transforms(void)
{
    const CNA_Matrix matrix = translation_matrix(10.0F, 20.0F);
    const CNA_Quaternion identity = {0.0F, 0.0F, 0.0F, 1.0F};
    const float half_sqrt = 0.70710678F;
    const CNA_Quaternion quarter_turn = {0.0F, 0.0F, half_sqrt, half_sqrt};
    CNA_Vector2 result;
    if (cna_vector2_transform_matrix((CNA_Vector2){1.0F, 2.0F}, matrix, &result) !=
            CNA_RESULT_SUCCESS || !vector2_near(result, 11.0F, 22.0F) ||
        cna_vector2_transform_normal((CNA_Vector2){1.0F, 2.0F}, matrix, &result) !=
            CNA_RESULT_SUCCESS || !vector2_near(result, 1.0F, 2.0F) ||
        cna_vector2_transform_quaternion((CNA_Vector2){1.0F, 2.0F}, identity, &result) !=
            CNA_RESULT_SUCCESS || !vector2_near(result, 1.0F, 2.0F) ||
        cna_vector2_transform_quaternion((CNA_Vector2){1.0F, 0.0F}, quarter_turn, &result) !=
            CNA_RESULT_SUCCESS || !vector2_near(result, 0.0F, 1.0F)) {
        return 0;
    }

    const CNA_Vector2 source[3] = {
        {1.0F, 2.0F}, {3.0F, 4.0F}, {5.0F, 6.0F}
    };
    CNA_Vector2 destination[3] = {
        {-1.0F, -1.0F}, {-1.0F, -1.0F}, {-1.0F, -1.0F}
    };
    if (cna_vector2_transform_matrix_array(
            source, 3U, 1U, matrix, destination, 3U, 0U, 2U) != CNA_RESULT_SUCCESS ||
        !vector2_near(destination[0], 13.0F, 24.0F) ||
        !vector2_near(destination[1], 15.0F, 26.0F) ||
        !vector2_near(destination[2], -1.0F, -1.0F) ||
        cna_vector2_transform_quaternion_array(
            source, 3U, 0U, identity, destination, 3U, 0U, 3U) != CNA_RESULT_SUCCESS ||
        !vector2_near(destination[2], 5.0F, 6.0F) ||
        cna_vector2_transform_normal_array(
            source, 3U, 0U, matrix, destination, 3U, 0U, 3U) != CNA_RESULT_SUCCESS ||
        !vector2_near(destination[0], 1.0F, 2.0F)) {
        return 0;
    }

    destination[0] = (CNA_Vector2){77.0F, 88.0F};
    if (cna_vector2_transform_matrix_array(
            source, 3U, 2U, matrix, destination, 3U, 0U, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !vector2_near(destination[0], 77.0F, 88.0F) ||
        cna_vector2_transform_matrix_array(0, 0U, 0U, matrix, 0, 0U, 0U, 0U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

int main(void)
{
    return validate_construction_and_members() &&
        validate_arithmetic() && validate_transforms() ? 0 : 1;
}
