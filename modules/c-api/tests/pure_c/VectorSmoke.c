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

static int vector3_near(CNA_Vector3 value, float x, float y, float z)
{
    return near_float(value.x, x) && near_float(value.y, y) && near_float(value.z, z);
}

static int vector4_near(CNA_Vector4 value, float x, float y, float z, float w)
{
    return near_float(value.x, x) && near_float(value.y, y) &&
        near_float(value.z, z) && near_float(value.w, w);
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

static CNA_Matrix translation_matrix3(float x, float y, float z)
{
    CNA_Matrix matrix = translation_matrix(x, y);
    matrix.m43 = z;
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

static int validate_vector3_construction_and_members(void)
{
    CNA_Vector3 zero = {9.0F, 9.0F, 9.0F};
    CNA_Vector3 one;
    CNA_Vector3 value;
    CNA_Vector3 direction;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    float scalar = 0.0F;
    if (cna_vector3_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vector3_init(&zero) != CNA_RESULT_SUCCESS ||
        !vector3_near(zero, 0.0F, 0.0F, 0.0F) ||
        cna_vector3_init_xyz(2.0F, 3.0F, 6.0F, &value) != CNA_RESULT_SUCCESS ||
        !vector3_near(value, 2.0F, 3.0F, 6.0F) ||
        cna_vector3_init_scalar(2.0F, &one) != CNA_RESULT_SUCCESS ||
        !vector3_near(one, 2.0F, 2.0F, 2.0F) ||
        cna_vector3_init_vector2_z((CNA_Vector2){4.0F, 5.0F}, 6.0F, &one) !=
            CNA_RESULT_SUCCESS || !vector3_near(one, 4.0F, 5.0F, 6.0F) ||
        cna_vector3_get_zero(&zero) != CNA_RESULT_SUCCESS ||
        cna_vector3_get_one(&one) != CNA_RESULT_SUCCESS ||
        !vector3_near(one, 1.0F, 1.0F, 1.0F) ||
        cna_vector3_get_unit_x(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 1.0F, 0.0F, 0.0F) ||
        cna_vector3_get_unit_y(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 0.0F, 1.0F, 0.0F) ||
        cna_vector3_get_unit_z(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 0.0F, 0.0F, 1.0F) ||
        cna_vector3_get_up(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 0.0F, 1.0F, 0.0F) ||
        cna_vector3_get_down(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 0.0F, -1.0F, 0.0F) ||
        cna_vector3_get_right(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 1.0F, 0.0F, 0.0F) ||
        cna_vector3_get_left(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, -1.0F, 0.0F, 0.0F) ||
        cna_vector3_get_forward(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 0.0F, 0.0F, -1.0F) ||
        cna_vector3_get_backward(&direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 0.0F, 0.0F, 1.0F)) {
        return 0;
    }

    if (cna_vector3_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vector3_not_equals(value, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vector3_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        cna_vector3_get_hash_code(value, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash ||
        cna_vector3_get_hash_code(value, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vector3_length(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 7.0F) ||
        cna_vector3_length_squared(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 49.0F)) {
        return 0;
    }

    if (cna_vector3_normalize_in_place(&value) != CNA_RESULT_SUCCESS ||
        !vector3_near(value, 2.0F / 7.0F, 3.0F / 7.0F, 6.0F / 7.0F) ||
        cna_vector3_normalize_in_place(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    value = (CNA_Vector3){2.0F, 3.0F, 6.0F};
    static const char Expected[] = "{X:2 Y:3 Z:6}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'v';
    if (cna_vector3_get_string_size(value, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_vector3_copy_string(value, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'v' ||
        cna_vector3_copy_string(value, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_vector3_arithmetic(void)
{
    const CNA_Vector3 a = {2.0F, 4.0F, 6.0F};
    const CNA_Vector3 b = {1.0F, -2.0F, 3.0F};
    CNA_Vector3 result = a;
    float scalar = 0.0F;
    if (cna_vector3_add(result, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 3.0F, 2.0F, 9.0F) ||
        cna_vector3_subtract(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.0F, 6.0F, 3.0F) ||
        cna_vector3_multiply(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 2.0F, -8.0F, 18.0F) ||
        cna_vector3_multiply_scalar(a, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.0F, 2.0F, 3.0F) ||
        cna_vector3_divide(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 2.0F, -2.0F, 2.0F) ||
        cna_vector3_divide_scalar(a, 2.0F, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.0F, 2.0F, 3.0F) ||
        cna_vector3_negate(a, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, -2.0F, -4.0F, -6.0F) ||
        cna_vector3_dot(a, b, &scalar) != CNA_RESULT_SUCCESS || scalar != 12.0F ||
        cna_vector3_distance(a, b, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, sqrtf(46.0F)) ||
        cna_vector3_distance_squared(a, b, &scalar) != CNA_RESULT_SUCCESS ||
        scalar != 46.0F ||
        cna_vector3_cross(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 24.0F, 0.0F, -8.0F)) {
        return 0;
    }

    if (cna_vector3_barycentric(
            (CNA_Vector3){0.0F, 0.0F, 0.0F},
            (CNA_Vector3){2.0F, 4.0F, 6.0F},
            (CNA_Vector3){4.0F, 8.0F, 12.0F},
            0.25F,
            0.5F,
            &result) != CNA_RESULT_SUCCESS || !vector3_near(result, 2.5F, 5.0F, 7.5F) ||
        cna_vector3_catmull_rom(
            (CNA_Vector3){0.0F, 0.0F, 0.0F},
            (CNA_Vector3){1.0F, 2.0F, 3.0F},
            (CNA_Vector3){2.0F, 4.0F, 6.0F},
            (CNA_Vector3){3.0F, 6.0F, 9.0F},
            0.5F,
            &result) != CNA_RESULT_SUCCESS || !vector3_near(result, 1.5F, 3.0F, 4.5F) ||
        cna_vector3_clamp(
            (CNA_Vector3){-2.0F, 9.0F, 4.0F},
            (CNA_Vector3){0.0F, 1.0F, 2.0F},
            (CNA_Vector3){3.0F, 5.0F, 3.0F},
            &result) != CNA_RESULT_SUCCESS || !vector3_near(result, 0.0F, 5.0F, 3.0F) ||
        cna_vector3_hermite(
            (CNA_Vector3){0.0F, 0.0F, 0.0F},
            (CNA_Vector3){0.0F, 0.0F, 0.0F},
            (CNA_Vector3){10.0F, 20.0F, 30.0F},
            (CNA_Vector3){0.0F, 0.0F, 0.0F},
            0.5F,
            &result) != CNA_RESULT_SUCCESS || !vector3_near(result, 5.0F, 10.0F, 15.0F) ||
        cna_vector3_lerp(a, b, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.5F, 1.0F, 4.5F) ||
        cna_vector3_max(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 2.0F, 4.0F, 6.0F) ||
        cna_vector3_min(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.0F, -2.0F, 3.0F) ||
        cna_vector3_normalize((CNA_Vector3){2.0F, 3.0F, 6.0F}, &result) !=
            CNA_RESULT_SUCCESS ||
        !vector3_near(result, 2.0F / 7.0F, 3.0F / 7.0F, 6.0F / 7.0F) ||
        cna_vector3_reflect(
            (CNA_Vector3){1.0F, -1.0F, 2.0F},
            (CNA_Vector3){0.0F, 1.0F, 0.0F},
            &result) != CNA_RESULT_SUCCESS || !vector3_near(result, 1.0F, 1.0F, 2.0F) ||
        cna_vector3_smooth_step(a, b, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.5F, 1.0F, 4.5F) ||
        cna_vector3_add(a, b, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_vector3_divide_scalar(a, 0.0F, &result) != CNA_RESULT_SUCCESS ||
        !isinf(result.x) || !isinf(result.y) || !isinf(result.z)) {
        return 0;
    }
    if (cna_vector3_normalize((CNA_Vector3){0.0F, 0.0F, 0.0F}, &result) !=
            CNA_RESULT_SUCCESS ||
        !isnan(result.x) || !isnan(result.y) || !isnan(result.z)) {
        return 0;
    }
    return 1;
}

static int validate_vector3_transforms(void)
{
    const CNA_Matrix matrix = translation_matrix3(10.0F, 20.0F, 30.0F);
    const CNA_Quaternion identity = {0.0F, 0.0F, 0.0F, 1.0F};
    const float half_sqrt = 0.70710678F;
    const CNA_Quaternion quarter_turn = {0.0F, 0.0F, half_sqrt, half_sqrt};
    CNA_Vector3 result;
    if (cna_vector3_transform_matrix((CNA_Vector3){1.0F, 2.0F, 3.0F}, matrix, &result) !=
            CNA_RESULT_SUCCESS || !vector3_near(result, 11.0F, 22.0F, 33.0F) ||
        cna_vector3_transform_normal((CNA_Vector3){1.0F, 2.0F, 3.0F}, matrix, &result) !=
            CNA_RESULT_SUCCESS || !vector3_near(result, 1.0F, 2.0F, 3.0F) ||
        cna_vector3_transform_quaternion(
            (CNA_Vector3){1.0F, 2.0F, 3.0F}, identity, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 1.0F, 2.0F, 3.0F) ||
        cna_vector3_transform_quaternion(
            (CNA_Vector3){1.0F, 0.0F, 3.0F}, quarter_turn, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near(result, 0.0F, 1.0F, 3.0F)) {
        return 0;
    }

    const CNA_Vector3 source[3] = {
        {1.0F, 2.0F, 3.0F}, {3.0F, 4.0F, 5.0F}, {5.0F, 6.0F, 7.0F}
    };
    CNA_Vector3 destination[3] = {
        {-1.0F, -1.0F, -1.0F}, {-1.0F, -1.0F, -1.0F}, {-1.0F, -1.0F, -1.0F}
    };
    if (cna_vector3_transform_matrix_array(
            source, 3U, 1U, matrix, destination, 3U, 0U, 2U) != CNA_RESULT_SUCCESS ||
        !vector3_near(destination[0], 13.0F, 24.0F, 35.0F) ||
        !vector3_near(destination[1], 15.0F, 26.0F, 37.0F) ||
        !vector3_near(destination[2], -1.0F, -1.0F, -1.0F) ||
        cna_vector3_transform_quaternion_array(
            source, 3U, 0U, identity, destination, 3U, 0U, 3U) != CNA_RESULT_SUCCESS ||
        !vector3_near(destination[2], 5.0F, 6.0F, 7.0F) ||
        cna_vector3_transform_normal_array(
            source, 3U, 0U, matrix, destination, 3U, 0U, 3U) != CNA_RESULT_SUCCESS ||
        !vector3_near(destination[0], 1.0F, 2.0F, 3.0F)) {
        return 0;
    }

    CNA_Vector3 alias[3] = {
        {1.0F, 2.0F, 3.0F}, {4.0F, 5.0F, 6.0F}, {7.0F, 8.0F, 9.0F}
    };
    if (cna_vector3_transform_quaternion_array(
            alias, 3U, 0U, identity, alias, 3U, 1U, 2U) != CNA_RESULT_SUCCESS ||
        !vector3_near(alias[0], 1.0F, 2.0F, 3.0F) ||
        !vector3_near(alias[1], 1.0F, 2.0F, 3.0F) ||
        !vector3_near(alias[2], 1.0F, 2.0F, 3.0F)) {
        return 0;
    }

    destination[0] = (CNA_Vector3){77.0F, 88.0F, 99.0F};
    if (cna_vector3_transform_matrix_array(
            source, 3U, 2U, matrix, destination, 3U, 0U, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !vector3_near(destination[0], 77.0F, 88.0F, 99.0F) ||
        cna_vector3_transform_matrix_array(0, 0U, 0U, matrix, 0, 0U, 0U, 0U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_vector4_construction_and_members(void)
{
    CNA_Vector4 zero = {9.0F, 9.0F, 9.0F, 9.0F};
    CNA_Vector4 one;
    CNA_Vector4 value;
    CNA_Vector4 unit;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    float scalar = 0.0F;
    if (cna_vector4_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vector4_init(&zero) != CNA_RESULT_SUCCESS ||
        !vector4_near(zero, 0.0F, 0.0F, 0.0F, 0.0F) ||
        cna_vector4_init_xyzw(1.0F, 2.0F, 2.0F, 4.0F, &value) != CNA_RESULT_SUCCESS ||
        !vector4_near(value, 1.0F, 2.0F, 2.0F, 4.0F) ||
        cna_vector4_init_vector2_zw(
            (CNA_Vector2){1.0F, 2.0F}, 3.0F, 4.0F, &one) != CNA_RESULT_SUCCESS ||
        !vector4_near(one, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_vector4_init_vector3_w(
            (CNA_Vector3){1.0F, 2.0F, 3.0F}, 4.0F, &one) != CNA_RESULT_SUCCESS ||
        !vector4_near(one, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_vector4_init_scalar(2.0F, &one) != CNA_RESULT_SUCCESS ||
        !vector4_near(one, 2.0F, 2.0F, 2.0F, 2.0F) ||
        cna_vector4_get_zero(&zero) != CNA_RESULT_SUCCESS ||
        cna_vector4_get_one(&one) != CNA_RESULT_SUCCESS ||
        !vector4_near(one, 1.0F, 1.0F, 1.0F, 1.0F) ||
        cna_vector4_get_unit_x(&unit) != CNA_RESULT_SUCCESS ||
        !vector4_near(unit, 1.0F, 0.0F, 0.0F, 0.0F) ||
        cna_vector4_get_unit_y(&unit) != CNA_RESULT_SUCCESS ||
        !vector4_near(unit, 0.0F, 1.0F, 0.0F, 0.0F) ||
        cna_vector4_get_unit_z(&unit) != CNA_RESULT_SUCCESS ||
        !vector4_near(unit, 0.0F, 0.0F, 1.0F, 0.0F) ||
        cna_vector4_get_unit_w(&unit) != CNA_RESULT_SUCCESS ||
        !vector4_near(unit, 0.0F, 0.0F, 0.0F, 1.0F)) {
        return 0;
    }

    if (cna_vector4_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vector4_not_equals(value, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_vector4_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        cna_vector4_get_hash_code(value, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash ||
        cna_vector4_get_hash_code(value, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vector4_length(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 5.0F) ||
        cna_vector4_length_squared(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 25.0F)) {
        return 0;
    }

    if (cna_vector4_normalize_in_place(&value) != CNA_RESULT_SUCCESS ||
        !vector4_near(value, 0.2F, 0.4F, 0.4F, 0.8F) ||
        cna_vector4_normalize_in_place(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    value = (CNA_Vector4){1.0F, 2.0F, 2.0F, 4.0F};
    static const char Expected[] = "{X:1 Y:2 Z:2 W:4}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'v';
    if (cna_vector4_get_string_size(value, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_vector4_copy_string(value, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'v' ||
        cna_vector4_copy_string(value, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_vector4_arithmetic(void)
{
    const CNA_Vector4 a = {2.0F, 4.0F, 6.0F, 8.0F};
    const CNA_Vector4 b = {1.0F, -2.0F, 3.0F, 4.0F};
    CNA_Vector4 result = a;
    float scalar = 0.0F;
    if (cna_vector4_add(result, b, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 3.0F, 2.0F, 9.0F, 12.0F) ||
        cna_vector4_subtract(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.0F, 6.0F, 3.0F, 4.0F) ||
        cna_vector4_multiply(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 2.0F, -8.0F, 18.0F, 32.0F) ||
        cna_vector4_multiply_scalar(a, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_vector4_divide(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 2.0F, -2.0F, 2.0F, 2.0F) ||
        cna_vector4_divide_scalar(a, 2.0F, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_vector4_negate(a, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, -2.0F, -4.0F, -6.0F, -8.0F) ||
        cna_vector4_dot(a, b, &scalar) != CNA_RESULT_SUCCESS || scalar != 44.0F ||
        cna_vector4_distance(a, b, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, sqrtf(62.0F)) ||
        cna_vector4_distance_squared(a, b, &scalar) != CNA_RESULT_SUCCESS ||
        scalar != 62.0F) {
        return 0;
    }

    if (cna_vector4_barycentric(
            (CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F},
            (CNA_Vector4){2.0F, 4.0F, 6.0F, 8.0F},
            (CNA_Vector4){4.0F, 8.0F, 12.0F, 16.0F},
            0.25F,
            0.5F,
            &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 2.5F, 5.0F, 7.5F, 10.0F) ||
        cna_vector4_catmull_rom(
            (CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F},
            (CNA_Vector4){1.0F, 2.0F, 3.0F, 4.0F},
            (CNA_Vector4){2.0F, 4.0F, 6.0F, 8.0F},
            (CNA_Vector4){3.0F, 6.0F, 9.0F, 12.0F},
            0.5F,
            &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.5F, 3.0F, 4.5F, 6.0F) ||
        cna_vector4_clamp(
            (CNA_Vector4){-2.0F, 9.0F, 4.0F, 8.0F},
            (CNA_Vector4){0.0F, 1.0F, 2.0F, 3.0F},
            (CNA_Vector4){3.0F, 5.0F, 3.0F, 6.0F},
            &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 0.0F, 5.0F, 3.0F, 6.0F) ||
        cna_vector4_hermite(
            (CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F},
            (CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F},
            (CNA_Vector4){10.0F, 20.0F, 30.0F, 40.0F},
            (CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F},
            0.5F,
            &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 5.0F, 10.0F, 15.0F, 20.0F) ||
        cna_vector4_lerp(a, b, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.5F, 1.0F, 4.5F, 6.0F) ||
        cna_vector4_max(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 2.0F, 4.0F, 6.0F, 8.0F) ||
        cna_vector4_min(a, b, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.0F, -2.0F, 3.0F, 4.0F) ||
        cna_vector4_normalize((CNA_Vector4){1.0F, 2.0F, 2.0F, 4.0F}, &result) !=
            CNA_RESULT_SUCCESS || !vector4_near(result, 0.2F, 0.4F, 0.4F, 0.8F) ||
        cna_vector4_smooth_step(a, b, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 1.5F, 1.0F, 4.5F, 6.0F) ||
        cna_vector4_add(a, b, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_vector4_divide_scalar(a, 0.0F, &result) != CNA_RESULT_SUCCESS ||
        !isinf(result.x) || !isinf(result.y) || !isinf(result.z) || !isinf(result.w) ||
        cna_vector4_normalize((CNA_Vector4){0.0F, 0.0F, 0.0F, 0.0F}, &result) !=
            CNA_RESULT_SUCCESS ||
        !isnan(result.x) || !isnan(result.y) || !isnan(result.z) || !isnan(result.w)) {
        return 0;
    }
    return 1;
}

static int validate_vector4_transforms(void)
{
    const CNA_Matrix matrix = translation_matrix3(10.0F, 20.0F, 30.0F);
    const CNA_Quaternion identity = {0.0F, 0.0F, 0.0F, 1.0F};
    const float half_sqrt = 0.70710678F;
    const CNA_Quaternion quarter_turn = {0.0F, 0.0F, half_sqrt, half_sqrt};
    CNA_Vector4 result;
    if (cna_vector4_transform_vector2_matrix(
            (CNA_Vector2){1.0F, 2.0F}, matrix, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 11.0F, 22.0F, 30.0F, 1.0F) ||
        cna_vector4_transform_vector3_matrix(
            (CNA_Vector3){1.0F, 2.0F, 3.0F}, matrix, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 11.0F, 22.0F, 33.0F, 1.0F) ||
        cna_vector4_transform_matrix(
            (CNA_Vector4){1.0F, 2.0F, 3.0F, 2.0F}, matrix, &result) !=
            CNA_RESULT_SUCCESS || !vector4_near(result, 21.0F, 42.0F, 63.0F, 2.0F) ||
        cna_vector4_transform_vector2_quaternion(
            (CNA_Vector2){1.0F, 0.0F}, quarter_turn, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 0.0F, 1.0F, 0.0F, 1.0F) ||
        cna_vector4_transform_vector3_quaternion(
            (CNA_Vector3){1.0F, 0.0F, 3.0F}, quarter_turn, &result) != CNA_RESULT_SUCCESS ||
        !vector4_near(result, 0.0F, 1.0F, 3.0F, 1.0F) ||
        cna_vector4_transform_quaternion(
            (CNA_Vector4){1.0F, 0.0F, 3.0F, 4.0F}, quarter_turn, &result) !=
            CNA_RESULT_SUCCESS || !vector4_near(result, 0.0F, 1.0F, 3.0F, 4.0F)) {
        return 0;
    }

    const CNA_Vector4 source[3] = {
        {1.0F, 2.0F, 3.0F, 1.0F},
        {3.0F, 4.0F, 5.0F, 1.0F},
        {5.0F, 6.0F, 7.0F, 1.0F}
    };
    CNA_Vector4 destination[3] = {
        {-1.0F, -1.0F, -1.0F, -1.0F},
        {-1.0F, -1.0F, -1.0F, -1.0F},
        {-1.0F, -1.0F, -1.0F, -1.0F}
    };
    if (cna_vector4_transform_matrix_array(
            source, 3U, 1U, matrix, destination, 3U, 0U, 2U) != CNA_RESULT_SUCCESS ||
        !vector4_near(destination[0], 13.0F, 24.0F, 35.0F, 1.0F) ||
        !vector4_near(destination[1], 15.0F, 26.0F, 37.0F, 1.0F) ||
        !vector4_near(destination[2], -1.0F, -1.0F, -1.0F, -1.0F) ||
        cna_vector4_transform_quaternion_array(
            source, 3U, 0U, identity, destination, 3U, 0U, 3U) != CNA_RESULT_SUCCESS ||
        !vector4_near(destination[2], 5.0F, 6.0F, 7.0F, 1.0F)) {
        return 0;
    }

    CNA_Vector4 alias[3] = {
        {1.0F, 2.0F, 3.0F, 4.0F},
        {5.0F, 6.0F, 7.0F, 8.0F},
        {9.0F, 10.0F, 11.0F, 12.0F}
    };
    if (cna_vector4_transform_quaternion_array(
            alias, 3U, 0U, identity, alias, 3U, 1U, 2U) != CNA_RESULT_SUCCESS ||
        !vector4_near(alias[0], 1.0F, 2.0F, 3.0F, 4.0F) ||
        !vector4_near(alias[1], 1.0F, 2.0F, 3.0F, 4.0F) ||
        !vector4_near(alias[2], 1.0F, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }

    destination[0] = (CNA_Vector4){77.0F, 88.0F, 99.0F, 111.0F};
    if (cna_vector4_transform_matrix_array(
            source, 3U, 2U, matrix, destination, 3U, 0U, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !vector4_near(destination[0], 77.0F, 88.0F, 99.0F, 111.0F) ||
        cna_vector4_transform_matrix_array(0, 0U, 0U, matrix, 0, 0U, 0U, 0U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

/*
 * CBIND-103: `Vector3::operator*=` and `operator/=` have no C routes of their own -- `v *= w` is the
 * binary route with the destination naming the left operand. The routes take their vectors by
 * value, which is what makes that well defined rather than an aliasing hazard, and this measures it
 * for all four overloads instead of asserting it.
 */
static int validate_vector3_compound_assignment(void)
{
    const CNA_Vector3 left = {2.0F, 3.0F, 4.0F};
    const CNA_Vector3 right = {5.0F, -2.0F, 8.0F};
    CNA_Vector3 separate = {0.0F, 0.0F, 0.0F};
    CNA_Vector3 aliased = left;

    if (cna_vector3_multiply(left, right, &separate) != CNA_RESULT_SUCCESS ||
        cna_vector3_multiply(aliased, right, &aliased) != CNA_RESULT_SUCCESS ||
        !vector3_near(aliased, separate.x, separate.y, separate.z)) {
        return 0;
    }
    /* The right operand aliasing the destination is the same question from the other side. */
    aliased = right;
    if (cna_vector3_multiply(left, aliased, &aliased) != CNA_RESULT_SUCCESS ||
        !vector3_near(aliased, separate.x, separate.y, separate.z)) {
        return 0;
    }

    aliased = left;
    if (cna_vector3_multiply_scalar(left, 1.5F, &separate) != CNA_RESULT_SUCCESS ||
        cna_vector3_multiply_scalar(aliased, 1.5F, &aliased) != CNA_RESULT_SUCCESS ||
        !vector3_near(aliased, separate.x, separate.y, separate.z)) {
        return 0;
    }

    aliased = left;
    if (cna_vector3_divide(left, right, &separate) != CNA_RESULT_SUCCESS ||
        cna_vector3_divide(aliased, right, &aliased) != CNA_RESULT_SUCCESS ||
        !vector3_near(aliased, separate.x, separate.y, separate.z)) {
        return 0;
    }
    aliased = right;
    if (cna_vector3_divide(left, aliased, &aliased) != CNA_RESULT_SUCCESS ||
        !vector3_near(aliased, separate.x, separate.y, separate.z)) {
        return 0;
    }

    aliased = left;
    if (cna_vector3_divide_scalar(left, 4.0F, &separate) != CNA_RESULT_SUCCESS ||
        cna_vector3_divide_scalar(aliased, 4.0F, &aliased) != CNA_RESULT_SUCCESS ||
        !vector3_near(aliased, separate.x, separate.y, separate.z)) {
        return 0;
    }

    /* The destination really was written, so a match cannot be a no-op agreeing with itself. */
    return !vector3_near(aliased, left.x, left.y, left.z);
}

int main(void)
{
    return validate_construction_and_members() &&
        validate_arithmetic() && validate_transforms() &&
        validate_vector3_construction_and_members() &&
        validate_vector3_arithmetic() && validate_vector3_transforms() &&
        validate_vector3_compound_assignment() &&
        validate_vector4_construction_and_members() &&
        validate_vector4_arithmetic() && validate_vector4_transforms() ? 0 : 1;
}
