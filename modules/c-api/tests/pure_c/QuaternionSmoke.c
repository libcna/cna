// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <string.h>

static int near_float(float left, float right)
{
    return fabsf(left - right) <= 0.0001F;
}

static int quaternion_near(CNA_Quaternion value, float x, float y, float z, float w)
{
    return near_float(value.x, x) && near_float(value.y, y) &&
        near_float(value.z, z) && near_float(value.w, w);
}

static CNA_Matrix identity_matrix(void)
{
    CNA_Matrix matrix = {0};
    matrix.m11 = 1.0F;
    matrix.m22 = 1.0F;
    matrix.m33 = 1.0F;
    matrix.m44 = 1.0F;
    return matrix;
}

static int validate_construction_and_members(void)
{
    CNA_Quaternion value;
    CNA_Quaternion identity;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    float scalar = 0.0F;
    if (cna_quaternion_init_xyzw(1.0F, 2.0F, 2.0F, 4.0F, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_quaternion_init_xyzw(1.0F, 2.0F, 2.0F, 4.0F, &value) !=
            CNA_RESULT_SUCCESS || !quaternion_near(value, 1.0F, 2.0F, 2.0F, 4.0F) ||
        cna_quaternion_init_vector3_w(
            (CNA_Vector3){1.0F, 2.0F, 3.0F}, 4.0F, &identity) != CNA_RESULT_SUCCESS ||
        !quaternion_near(identity, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_quaternion_get_identity(&identity) != CNA_RESULT_SUCCESS ||
        !quaternion_near(identity, 0.0F, 0.0F, 0.0F, 1.0F) ||
        cna_quaternion_equals(value, value, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_quaternion_not_equals(value, identity, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_quaternion_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        cna_quaternion_get_hash_code(value, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash ||
        cna_quaternion_get_hash_code(value, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_quaternion_length(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 5.0F) ||
        cna_quaternion_length_squared(value, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 25.0F)) {
        return 0;
    }

    if (cna_quaternion_conjugate_in_place(&value) != CNA_RESULT_SUCCESS ||
        !quaternion_near(value, -1.0F, -2.0F, -2.0F, 4.0F) ||
        cna_quaternion_conjugate_in_place(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    value = (CNA_Quaternion){1.0F, 2.0F, 2.0F, 4.0F};
    if (cna_quaternion_normalize_in_place(&value) != CNA_RESULT_SUCCESS ||
        !quaternion_near(value, 0.2F, 0.4F, 0.4F, 0.8F) ||
        cna_quaternion_normalize_in_place(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    value = (CNA_Quaternion){1.0F, 2.0F, 2.0F, 4.0F};
    static const char Expected[] = "{X:1 Y:2 Z:2 W:4}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'q';
    if (cna_quaternion_get_string_size(value, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_quaternion_copy_string(value, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'q' ||
        cna_quaternion_copy_string(value, bytes, sizeof(bytes), &byte_count) !=
            CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_operations(void)
{
    const CNA_Quaternion a = {1.0F, 2.0F, 3.0F, 4.0F};
    const CNA_Quaternion identity = {0.0F, 0.0F, 0.0F, 1.0F};
    const float half_sqrt = 0.70710678F;
    const CNA_Quaternion quarter_turn = {0.0F, 0.0F, half_sqrt, half_sqrt};
    CNA_Quaternion result = a;
    float scalar = 0.0F;
    if (cna_quaternion_add(result, identity, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 1.0F, 2.0F, 3.0F, 5.0F) ||
        cna_quaternion_subtract(a, identity, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 1.0F, 2.0F, 3.0F, 3.0F) ||
        cna_quaternion_multiply(a, identity, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_quaternion_multiply_scalar(a, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.5F, 1.0F, 1.5F, 2.0F) ||
        cna_quaternion_negate(a, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, -1.0F, -2.0F, -3.0F, -4.0F) ||
        cna_quaternion_conjugate(a, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, -1.0F, -2.0F, -3.0F, 4.0F) ||
        cna_quaternion_concatenate(a, identity, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 1.0F, 2.0F, 3.0F, 4.0F) ||
        cna_quaternion_dot(quarter_turn, quarter_turn, &scalar) != CNA_RESULT_SUCCESS ||
        !near_float(scalar, 1.0F) ||
        cna_quaternion_add(a, identity, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_quaternion_create_from_axis_angle(
            (CNA_Vector3){0.0F, 0.0F, 1.0F}, 1.57079632679F, &result) !=
            CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.0F, 0.0F, half_sqrt, half_sqrt) ||
        cna_quaternion_create_from_rotation_matrix(identity_matrix(), &result) !=
            CNA_RESULT_SUCCESS || !quaternion_near(result, 0.0F, 0.0F, 0.0F, 1.0F) ||
        cna_quaternion_create_from_yaw_pitch_roll(
            0.0F, 0.0F, 1.57079632679F, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.0F, 0.0F, half_sqrt, half_sqrt) ||
        cna_quaternion_divide(quarter_turn, quarter_turn, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.0F, 0.0F, 0.0F, 1.0F) ||
        cna_quaternion_inverse(quarter_turn, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.0F, 0.0F, -half_sqrt, half_sqrt)) {
        return 0;
    }

    if (cna_quaternion_lerp(identity, quarter_turn, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.0F, 0.0F, 0.38268343F, 0.92387953F) ||
        cna_quaternion_slerp(identity, quarter_turn, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !quaternion_near(result, 0.0F, 0.0F, 0.38268343F, 0.92387953F) ||
        cna_quaternion_normalize((CNA_Quaternion){0.0F, 0.0F, 0.0F, 2.0F}, &result) !=
            CNA_RESULT_SUCCESS || !quaternion_near(result, 0.0F, 0.0F, 0.0F, 1.0F)) {
        return 0;
    }

    if (cna_quaternion_normalize((CNA_Quaternion){0.0F, 0.0F, 0.0F, 0.0F}, &result) !=
            CNA_RESULT_SUCCESS ||
        !isnan(result.x) || !isnan(result.y) || !isnan(result.z) || !isnan(result.w)) {
        return 0;
    }
    return 1;
}

/*
 * CBIND-103: the canonical parameterless `Quaternion()` sets all four components to zero -- C# gives
 * every struct one that zeroes its fields -- so its C form is the zero-initialized `CNA_Quaternion`
 * a caller already writes in a declaration, and a `cna_quaternion_init` route would be a second
 * spelling of it.
 *
 * The trap `CBIND-081` recorded for `Matrix()` is the same one here, and this is what pins it: the
 * zero quaternion is **not** the identity. A future "friendlier" default that returned (0,0,0,1)
 * would silently disagree with every default-constructed canonical Quaternion, and this fails.
 */
static int validate_default_value(void)
{
    CNA_Quaternion zeroed;
    memset(&zeroed, 0, sizeof(zeroed));
    if (zeroed.x != 0.0F || zeroed.y != 0.0F || zeroed.z != 0.0F || zeroed.w != 0.0F) {
        return 0;
    }

    CNA_Quaternion identity;
    memset(&identity, 0, sizeof(identity));
    if (cna_quaternion_get_identity(&identity) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (identity.x != 0.0F || identity.y != 0.0F || identity.z != 0.0F || identity.w != 1.0F) {
        return 0;
    }
    /* The default is the zero quaternion, and the identity is a different value. */
    return identity.w != zeroed.w;
}

int main(void)
{
    return CNA_TEST_STAGE(validate_construction_and_members()) && CNA_TEST_STAGE(validate_operations()) &&
        CNA_TEST_STAGE(validate_default_value()) ? 0 : 1;
}
