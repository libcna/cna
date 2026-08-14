// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(CNA_CurveKey) == 20U, "CNA_CurveKey size changed");
_Static_assert(_Alignof(CNA_CurveKey) == 4U, "CNA_CurveKey alignment changed");
_Static_assert(offsetof(CNA_CurveKey, position) == 0U, "position offset changed");
_Static_assert(offsetof(CNA_CurveKey, value) == 4U, "value offset changed");
_Static_assert(offsetof(CNA_CurveKey, tangent_in) == 8U, "tangent_in offset changed");
_Static_assert(offsetof(CNA_CurveKey, tangent_out) == 12U, "tangent_out offset changed");
_Static_assert(offsetof(CNA_CurveKey, continuity) == 16U, "continuity offset changed");

static int validate_construction_and_properties(void)
{
    CNA_CurveKey basic;
    CNA_CurveKey tangents;
    CNA_CurveKey full;
    if (cna_curve_key_init_position_value(1.0F, 2.0F, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_key_init_position_value(1.0F, 2.0F, &basic) != CNA_RESULT_SUCCESS ||
        basic.position != 1.0F || basic.value != 2.0F || basic.tangent_in != 0.0F ||
        basic.tangent_out != 0.0F || basic.continuity != CNA_CURVE_CONTINUITY_SMOOTH ||
        cna_curve_key_init_tangents(3.0F, 4.0F, 5.0F, 6.0F, &tangents) !=
            CNA_RESULT_SUCCESS ||
        tangents.position != 3.0F || tangents.value != 4.0F || tangents.tangent_in != 5.0F ||
        tangents.tangent_out != 6.0F || tangents.continuity != CNA_CURVE_CONTINUITY_SMOOTH ||
        cna_curve_key_init_full(
            7.0F, 8.0F, 9.0F, 10.0F, CNA_CURVE_CONTINUITY_STEP, &full) !=
            CNA_RESULT_SUCCESS ||
        full.position != 7.0F || full.value != 8.0F || full.tangent_in != 9.0F ||
        full.tangent_out != 10.0F || full.continuity != CNA_CURVE_CONTINUITY_STEP) {
        return 0;
    }

    CNA_CurveContinuity continuity = UINT32_MAX;
    float scalar = -1.0F;
    if (cna_curve_key_get_continuity(full, &continuity) != CNA_RESULT_SUCCESS ||
        continuity != CNA_CURVE_CONTINUITY_STEP ||
        cna_curve_key_get_position(full, &scalar) != CNA_RESULT_SUCCESS || scalar != 7.0F ||
        cna_curve_key_get_tangent_in(full, &scalar) != CNA_RESULT_SUCCESS || scalar != 9.0F ||
        cna_curve_key_get_tangent_out(full, &scalar) != CNA_RESULT_SUCCESS || scalar != 10.0F ||
        cna_curve_key_get_value(full, &scalar) != CNA_RESULT_SUCCESS || scalar != 8.0F) {
        return 0;
    }

    if (cna_curve_key_set_continuity(&full, CNA_CURVE_CONTINUITY_SMOOTH) != CNA_RESULT_SUCCESS ||
        cna_curve_key_set_tangent_in(&full, -2.0F) != CNA_RESULT_SUCCESS ||
        cna_curve_key_set_tangent_out(&full, 3.0F) != CNA_RESULT_SUCCESS ||
        cna_curve_key_set_value(&full, 11.0F) != CNA_RESULT_SUCCESS ||
        full.continuity != CNA_CURVE_CONTINUITY_SMOOTH || full.tangent_in != -2.0F ||
        full.tangent_out != 3.0F || full.value != 11.0F ||
        cna_curve_key_set_value(0, 1.0F) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    const CNA_CurveKey sentinel = full;
    if (cna_curve_key_init_full(0.0F, 0.0F, 0.0F, 0.0F, UINT32_MAX, &full) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        full.position != sentinel.position || full.value != sentinel.value ||
        full.tangent_in != sentinel.tangent_in || full.tangent_out != sentinel.tangent_out ||
        full.continuity != sentinel.continuity ||
        cna_curve_key_set_continuity(&full, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        full.continuity != sentinel.continuity) {
        return 0;
    }
    return 1;
}

static int validate_value_operations(void)
{
    CNA_CurveKey value;
    CNA_CurveKey clone;
    CNA_CurveKey later;
    if (cna_curve_key_init_full(
            2.0F, 3.0F, 4.0F, 5.0F, CNA_CURVE_CONTINUITY_STEP, &value) !=
            CNA_RESULT_SUCCESS ||
        cna_curve_key_clone(value, &clone) != CNA_RESULT_SUCCESS ||
        cna_curve_key_init_position_value(4.0F, 9.0F, &later) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    int32_t comparison = 99;
    CNA_Bool predicate = CNA_FALSE;
    int32_t hash = 0;
    int32_t equal_hash = 1;
    if (cna_curve_key_compare_to(value, later, &comparison) != CNA_RESULT_SUCCESS ||
        comparison >= 0 ||
        cna_curve_key_compare_to(later, value, &comparison) != CNA_RESULT_SUCCESS ||
        comparison <= 0 ||
        cna_curve_key_compare_to(value, clone, &comparison) != CNA_RESULT_SUCCESS ||
        comparison != 0 ||
        cna_curve_key_equals(value, clone, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_curve_key_not_equals(value, later, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_curve_key_get_hash_code(value, &hash) != CNA_RESULT_SUCCESS ||
        cna_curve_key_get_hash_code(clone, &equal_hash) != CNA_RESULT_SUCCESS ||
        hash != equal_hash ||
        cna_curve_key_get_hash_code(value, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_CurveKey nan_key = value;
    nan_key.position = NAN;
    if (cna_curve_key_compare_to(nan_key, value, &comparison) != CNA_RESULT_SUCCESS ||
        comparison != 0) {
        return 0;
    }

    CNA_CurveKey invalid = value;
    invalid.continuity = UINT32_MAX;
    comparison = 77;
    if (cna_curve_key_compare_to(invalid, value, &comparison) != CNA_RESULT_INVALID_ARGUMENT ||
        comparison != 77 ||
        cna_curve_key_set_continuity(&invalid, CNA_CURVE_CONTINUITY_SMOOTH) !=
            CNA_RESULT_INVALID_ARGUMENT || invalid.continuity != UINT32_MAX) {
        return 0;
    }
    return 1;
}

int main(void)
{
    return validate_construction_and_properties() && validate_value_operations() ? 0 : 1;
}
