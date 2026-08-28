// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <string.h>

static int near_float(float left, float right)
{
    return fabsf(left - right) <= 0.0001F;
}

static int vector3_near(CNA_Vector3 value, float x, float y, float z)
{
    return near_float(value.x, x) && near_float(value.y, y) && near_float(value.z, z);
}

static int quaternion_near(CNA_Quaternion value, float x, float y, float z, float w)
{
    return near_float(value.x, x) && near_float(value.y, y) &&
        near_float(value.z, z) && near_float(value.w, w);
}

static int matrix_near(CNA_Matrix left, CNA_Matrix right)
{
    return near_float(left.m11, right.m11) && near_float(left.m12, right.m12) &&
        near_float(left.m13, right.m13) && near_float(left.m14, right.m14) &&
        near_float(left.m21, right.m21) && near_float(left.m22, right.m22) &&
        near_float(left.m23, right.m23) && near_float(left.m24, right.m24) &&
        near_float(left.m31, right.m31) && near_float(left.m32, right.m32) &&
        near_float(left.m33, right.m33) && near_float(left.m34, right.m34) &&
        near_float(left.m41, right.m41) && near_float(left.m42, right.m42) &&
        near_float(left.m43, right.m43) && near_float(left.m44, right.m44);
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

static CNA_Matrix filled_matrix(float value)
{
    CNA_Matrix matrix;
    matrix.m11 = matrix.m12 = matrix.m13 = matrix.m14 = value;
    matrix.m21 = matrix.m22 = matrix.m23 = matrix.m24 = value;
    matrix.m31 = matrix.m32 = matrix.m33 = matrix.m34 = value;
    matrix.m41 = matrix.m42 = matrix.m43 = matrix.m44 = value;
    return matrix;
}

static int validate_construction_properties_and_members(void)
{
    CNA_Matrix zero = identity_matrix();
    CNA_Matrix identity;
    CNA_Matrix values;
    CNA_Vector3 direction;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    float determinant = 0.0F;
    if (cna_matrix_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_matrix_init(&zero) != CNA_RESULT_SUCCESS ||
        !matrix_near(zero, filled_matrix(0.0F)) ||
        cna_matrix_init_values(
            1.0F, 2.0F, 3.0F, 4.0F,
            5.0F, 6.0F, 7.0F, 8.0F,
            9.0F, 10.0F, 11.0F, 12.0F,
            13.0F, 14.0F, 15.0F, 16.0F,
            &values) != CNA_RESULT_SUCCESS ||
        values.m11 != 1.0F || values.m24 != 8.0F ||
        values.m31 != 9.0F || values.m44 != 16.0F ||
        cna_matrix_get_identity(&identity) != CNA_RESULT_SUCCESS ||
        !matrix_near(identity, identity_matrix())) {
        return 0;
    }

    CNA_Matrix property = identity;
    if (cna_matrix_set_backward(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_backward(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }
    property = identity;
    if (cna_matrix_set_down(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_down(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }
    property = identity;
    if (cna_matrix_set_forward(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_forward(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }
    property = identity;
    if (cna_matrix_set_left(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_left(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }
    property = identity;
    if (cna_matrix_set_right(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_right(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }
    property = identity;
    if (cna_matrix_set_translation(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_translation(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F)) {
        return 0;
    }
    property = identity;
    if (cna_matrix_set_up(&property, (CNA_Vector3){2.0F, 3.0F, 4.0F}) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_get_up(property, &direction) != CNA_RESULT_SUCCESS ||
        !vector3_near(direction, 2.0F, 3.0F, 4.0F) ||
        cna_matrix_set_up(0, (CNA_Vector3){0.0F, 1.0F, 0.0F}) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_matrix_determinant(identity, &determinant) != CNA_RESULT_SUCCESS ||
        determinant != 1.0F ||
        cna_matrix_equals(identity, identity, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_matrix_not_equals(identity, zero, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_matrix_get_hash_code(identity, &hash) != CNA_RESULT_SUCCESS ||
        cna_matrix_get_hash_code(identity, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash ||
        cna_matrix_get_hash_code(identity, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Matrix scale_matrix;
    if (cna_matrix_create_scale_xyz(2.0F, 3.0F, 4.0F, &scale_matrix) != CNA_RESULT_SUCCESS ||
        cna_matrix_set_translation(
            &scale_matrix, (CNA_Vector3){5.0F, 6.0F, 7.0F}) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    CNA_Vector3 scale;
    CNA_Quaternion rotation;
    CNA_Vector3 translation;
    CNA_Bool decomposed = CNA_FALSE;
    if (cna_matrix_decompose(
            scale_matrix, &scale, &rotation, &translation, &decomposed) != CNA_RESULT_SUCCESS ||
        decomposed != CNA_TRUE || !vector3_near(scale, 2.0F, 3.0F, 4.0F) ||
        !quaternion_near(rotation, 0.0F, 0.0F, 0.0F, 1.0F) ||
        !vector3_near(translation, 5.0F, 6.0F, 7.0F) ||
        cna_matrix_decompose(zero, &scale, &rotation, &translation, &decomposed) !=
            CNA_RESULT_SUCCESS ||
        decomposed != CNA_FALSE || !quaternion_near(rotation, 0.0F, 0.0F, 0.0F, 1.0F) ||
        cna_matrix_decompose(zero, 0, &rotation, &translation, &decomposed) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    static const char Expected[] =
        "{M11:1 M12:0 M13:0 M14:0} {M21:0 M22:1 M23:0 M24:0} "
        "{M31:0 M32:0 M33:1 M34:0} {M41:0 M42:0 M43:0 M44:1}";
    uint64_t byte_count = 0U;
    char bytes[sizeof(Expected) - 1U];
    char too_small = 'm';
    if (cna_matrix_get_string_size(identity, &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(Expected) - 1U ||
        cna_matrix_copy_string(identity, &too_small, 1U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'm' ||
        cna_matrix_copy_string(identity, bytes, sizeof(bytes), &byte_count) != CNA_RESULT_SUCCESS ||
        byte_count != sizeof(bytes) || memcmp(bytes, Expected, sizeof(bytes)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_factories(void)
{
    const CNA_Matrix identity = identity_matrix();
    const CNA_Vector3 zero = {0.0F, 0.0F, 0.0F};
    const CNA_Vector3 up = {0.0F, 1.0F, 0.0F};
    const CNA_Vector3 forward = {0.0F, 0.0F, -1.0F};
    const float half_sqrt = 0.70710678F;
    const CNA_Quaternion quarter_turn = {0.0F, 0.0F, half_sqrt, half_sqrt};
    CNA_Matrix result;

    if (cna_matrix_create_billboard(
            zero, (CNA_Vector3){0.0F, 0.0F, 10.0F}, up, 0, &result) !=
            CNA_RESULT_SUCCESS ||
        !vector3_near((CNA_Vector3){result.m41, result.m42, result.m43}, 0.0F, 0.0F, 0.0F) ||
        cna_matrix_create_billboard(zero, zero, up, &forward, &result) != CNA_RESULT_SUCCESS ||
        cna_matrix_create_constrained_billboard(
            zero,
            (CNA_Vector3){0.0F, 0.0F, 10.0F},
            up,
            0,
            &forward,
            &result) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_matrix_create_from_axis_angle(
            (CNA_Vector3){0.0F, 0.0F, 1.0F}, 1.57079632679F, &result) !=
            CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 0.0F) || !near_float(result.m12, 1.0F) ||
        !near_float(result.m21, -1.0F) ||
        cna_matrix_create_from_quaternion(quarter_turn, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m12, 1.0F) ||
        cna_matrix_create_from_yaw_pitch_roll(0.0F, 0.0F, 1.57079632679F, &result) !=
            CNA_RESULT_SUCCESS || !near_float(result.m12, 1.0F) ||
        cna_matrix_create_rotation_x(1.57079632679F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m23, 1.0F) ||
        cna_matrix_create_rotation_y(1.57079632679F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m13, -1.0F) ||
        cna_matrix_create_rotation_z(1.57079632679F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m12, 1.0F)) {
        return 0;
    }

    if (cna_matrix_create_look_at(
            (CNA_Vector3){0.0F, 0.0F, 10.0F}, zero, up, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m43, -10.0F) ||
        cna_matrix_create_orthographic(2.0F, 2.0F, 0.0F, 10.0F, &result) !=
            CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 1.0F) || !near_float(result.m22, 1.0F) ||
        !near_float(result.m33, -0.1F) ||
        cna_matrix_create_orthographic_off_center(
            -1.0F, 1.0F, -1.0F, 1.0F, 0.0F, 10.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 1.0F) || !near_float(result.m22, 1.0F) ||
        cna_matrix_create_perspective(2.0F, 2.0F, 1.0F, 10.0F, &result) !=
            CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 1.0F) || !near_float(result.m34, -1.0F) ||
        cna_matrix_create_perspective_field_of_view(
            1.57079632679F, 1.0F, 1.0F, 10.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 1.0F) || !near_float(result.m22, 1.0F) ||
        cna_matrix_create_perspective_off_center(
            -1.0F, 1.0F, -1.0F, 1.0F, 1.0F, 10.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 1.0F) || !near_float(result.m22, 1.0F)) {
        return 0;
    }

    result = identity;
    if (cna_matrix_create_perspective(2.0F, 2.0F, 0.0F, 10.0F, &result) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !matrix_near(result, identity)) {
        return 0;
    }

    if (cna_matrix_create_scale_scalar(2.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 2.0F) || !near_float(result.m22, 2.0F) ||
        !near_float(result.m33, 2.0F) ||
        cna_matrix_create_scale_xyz(2.0F, 3.0F, 4.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 2.0F) || !near_float(result.m22, 3.0F) ||
        !near_float(result.m33, 4.0F) ||
        cna_matrix_create_scale_vector3(
            (CNA_Vector3){4.0F, 5.0F, 6.0F}, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 4.0F) || !near_float(result.m22, 5.0F) ||
        !near_float(result.m33, 6.0F)) {
        return 0;
    }

    const CNA_Plane ground = {{0.0F, 1.0F, 0.0F}, 0.0F};
    if (cna_matrix_create_shadow(
            (CNA_Vector3){0.0F, -1.0F, 0.0F}, ground, &result) != CNA_RESULT_SUCCESS ||
        !isfinite(result.m11) ||
        cna_matrix_create_reflection(ground, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m22, -1.0F) ||
        cna_matrix_create_translation_xyz(1.0F, 2.0F, 3.0F, &result) !=
            CNA_RESULT_SUCCESS ||
        !vector3_near((CNA_Vector3){result.m41, result.m42, result.m43}, 1.0F, 2.0F, 3.0F) ||
        cna_matrix_create_translation_vector3(
            (CNA_Vector3){4.0F, 5.0F, 6.0F}, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near((CNA_Vector3){result.m41, result.m42, result.m43}, 4.0F, 5.0F, 6.0F) ||
        cna_matrix_create_world(
            (CNA_Vector3){1.0F, 2.0F, 3.0F}, forward, up, &result) != CNA_RESULT_SUCCESS ||
        !matrix_near(
            result,
            (CNA_Matrix){
                1.0F, 0.0F, 0.0F, 0.0F,
                0.0F, 1.0F, 0.0F, 0.0F,
                0.0F, 0.0F, 1.0F, 0.0F,
                1.0F, 2.0F, 3.0F, 1.0F})) {
        return 0;
    }
    return 1;
}

static int validate_operations(void)
{
    const CNA_Matrix zero = filled_matrix(0.0F);
    const CNA_Matrix one = filled_matrix(1.0F);
    const CNA_Matrix two = filled_matrix(2.0F);
    const CNA_Matrix identity = identity_matrix();
    CNA_Matrix result = identity;
    CNA_Bool predicate = CNA_FALSE;
    if (cna_matrix_add(result, identity, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 2.0F) || !near_float(result.m22, 2.0F) ||
        cna_matrix_subtract(identity, identity, &result) != CNA_RESULT_SUCCESS ||
        !matrix_near(result, zero) ||
        cna_matrix_multiply(identity, identity, &result) != CNA_RESULT_SUCCESS ||
        !matrix_near(result, identity) ||
        cna_matrix_multiply_scalar(identity, 2.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 2.0F) || !near_float(result.m12, 0.0F) ||
        cna_matrix_divide(one, two, &result) != CNA_RESULT_SUCCESS ||
        !matrix_near(result, filled_matrix(0.5F)) ||
        cna_matrix_divide_scalar(identity, 2.0F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 0.5F) || !near_float(result.m12, 0.0F) ||
        cna_matrix_negate(identity, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, -1.0F) || !near_float(result.m12, 0.0F) ||
        cna_matrix_lerp(zero, identity, 0.5F, &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 0.5F) || !near_float(result.m22, 0.5F) ||
        cna_matrix_add(identity, identity, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Matrix values;
    if (cna_matrix_init_values(
            1.0F, 2.0F, 3.0F, 4.0F,
            5.0F, 6.0F, 7.0F, 8.0F,
            9.0F, 10.0F, 11.0F, 12.0F,
            13.0F, 14.0F, 15.0F, 16.0F,
            &values) != CNA_RESULT_SUCCESS ||
        cna_matrix_transpose(values, &result) != CNA_RESULT_SUCCESS ||
        result.m12 != 5.0F || result.m21 != 2.0F || result.m34 != 15.0F ||
        result.m43 != 12.0F) {
        return 0;
    }

    CNA_Matrix translation;
    if (cna_matrix_create_translation_xyz(1.0F, 2.0F, 3.0F, &translation) !=
            CNA_RESULT_SUCCESS ||
        cna_matrix_invert(translation, &result) != CNA_RESULT_SUCCESS ||
        !vector3_near((CNA_Vector3){result.m41, result.m42, result.m43}, -1.0F, -2.0F, -3.0F) ||
        cna_matrix_invert(zero, &result) != CNA_RESULT_SUCCESS ||
        (!isnan(result.m11) && !isinf(result.m11))) {
        return 0;
    }

    const float half_sqrt = 0.70710678F;
    if (cna_matrix_transform(
            identity,
            (CNA_Quaternion){0.0F, 0.0F, half_sqrt, half_sqrt},
            &result) != CNA_RESULT_SUCCESS ||
        !near_float(result.m11, 0.0F) || !near_float(result.m12, 1.0F) ||
        cna_matrix_equals(result, identity, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE) {
        return 0;
    }
    return 1;
}

/*
 * CBIND-103: `Matrix::operator*=` has no C route of its own, because C has neither operator
 * overloading nor compound assignment on a struct: `M *= N` is the binary route with the
 * destination naming the left operand.
 *
 * That is only well defined if the operands are copies. `CBIND-081` established it for `Vector2`,
 * but a `Matrix` is 64 bytes and a wider value is exactly where an ABI might have switched to
 * passing by pointer -- so this measures it rather than inheriting the conclusion. The aliased call
 * must produce the same matrix as the unaliased one, for both overloads.
 */
static int validate_compound_assignment(void)
{
    CNA_Matrix left = {0};
    CNA_Matrix right = {0};
    for (int index = 0; index < 16; ++index) {
        ((float*)&left)[index] = (float)(index + 1);
        ((float*)&right)[index] = (float)(32 - index);
    }

    CNA_Matrix separate = {0};
    if (cna_matrix_multiply(left, right, &separate) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    CNA_Matrix aliased = left;
    if (cna_matrix_multiply(aliased, right, &aliased) != CNA_RESULT_SUCCESS ||
        !matrix_near(aliased, separate)) {
        return 0;
    }
    /* The right operand aliasing the destination is the same question from the other side. */
    CNA_Matrix rightAliased = right;
    if (cna_matrix_multiply(left, rightAliased, &rightAliased) != CNA_RESULT_SUCCESS ||
        !matrix_near(rightAliased, separate)) {
        return 0;
    }

    if (cna_matrix_multiply_scalar(left, 3.0F, &separate) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    aliased = left;
    if (cna_matrix_multiply_scalar(aliased, 3.0F, &aliased) != CNA_RESULT_SUCCESS ||
        !matrix_near(aliased, separate)) {
        return 0;
    }

    /* And the destination really was written, so a match cannot be a no-op agreeing with itself. */
    return !matrix_near(aliased, left);
}

int main(void)
{
    return CNA_TEST_STAGE(validate_construction_properties_and_members()) &&
        CNA_TEST_STAGE(validate_factories()) && CNA_TEST_STAGE(validate_operations()) &&
        CNA_TEST_STAGE(validate_compound_assignment()) ? 0 : 1;
}
