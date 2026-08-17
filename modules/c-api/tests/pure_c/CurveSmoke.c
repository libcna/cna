// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>

_Static_assert(sizeof(CNA_CurveKey) == 20U, "CNA_CurveKey size changed");
_Static_assert(_Alignof(CNA_CurveKey) == 4U, "CNA_CurveKey alignment changed");
_Static_assert(offsetof(CNA_CurveKey, position) == 0U, "position offset changed");
_Static_assert(offsetof(CNA_CurveKey, value) == 4U, "value offset changed");
_Static_assert(offsetof(CNA_CurveKey, tangent_in) == 8U, "tangent_in offset changed");
_Static_assert(offsetof(CNA_CurveKey, tangent_out) == 12U, "tangent_out offset changed");
_Static_assert(offsetof(CNA_CurveKey, continuity) == 16U, "continuity offset changed");
_Static_assert(sizeof(CNA_CurveKeyCollectionHandle) == 8U, "collection handle size changed");
_Static_assert(sizeof(CNA_CurveHandle) == 8U, "curve handle size changed");

static int nearly_equal(const float left, const float right)
{
    return fabsf(left - right) <= 0.00001F;
}

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

typedef struct WrongThreadState {
    CNA_CurveKeyCollectionHandle collection;
    CNA_Result result;
} WrongThreadState;

static int get_collection_count_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint64_t count = 0U;
    state->result = cna_curve_key_collection_get_count(state->collection, &count);
    return 0;
}

static int validate_collection(void)
{
    CNA_CurveKeyCollectionHandle collection = UINT64_MAX;
    CNA_CurveKeyCollectionHandle clone = CNA_INVALID_HANDLE;
    if (cna_curve_key_collection_create(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_key_collection_create(&collection) != CNA_RESULT_SUCCESS ||
        collection == CNA_INVALID_HANDLE) {
        return 0;
    }

    uint64_t count = UINT64_MAX;
    CNA_Bool predicate = CNA_TRUE;
    if (cna_curve_key_collection_get_count(collection, &count) != CNA_RESULT_SUCCESS ||
        count != 0U ||
        cna_curve_key_collection_get_is_read_only(collection, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE) {
        return 0;
    }

    CNA_CurveKey key0;
    CNA_CurveKey key1;
    CNA_CurveKey key2;
    CNA_CurveKey key3;
    if (cna_curve_key_init_position_value(0.0F, 10.0F, &key0) != CNA_RESULT_SUCCESS ||
        cna_curve_key_init_position_value(1.0F, 11.0F, &key1) != CNA_RESULT_SUCCESS ||
        cna_curve_key_init_position_value(2.0F, 12.0F, &key2) != CNA_RESULT_SUCCESS ||
        cna_curve_key_init_position_value(3.0F, 13.0F, &key3) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_add(collection, key2) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_add(collection, key0) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_add(collection, key1) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get_count(collection, &count) != CNA_RESULT_SUCCESS ||
        count != 3U) {
        return 0;
    }

    CNA_CurveKey result = key3;
    int32_t index = -2;
    if (cna_curve_key_collection_get(collection, 0, &result) != CNA_RESULT_SUCCESS ||
        result.position != 0.0F ||
        cna_curve_key_collection_get(collection, 1, &result) != CNA_RESULT_SUCCESS ||
        result.position != 1.0F ||
        cna_curve_key_collection_get(collection, 2, &result) != CNA_RESULT_SUCCESS ||
        result.position != 2.0F ||
        cna_curve_key_collection_contains(collection, key1, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_curve_key_collection_index_of(collection, key1, &index) != CNA_RESULT_SUCCESS ||
        index != 1 ||
        cna_curve_key_collection_set(collection, 0, key3) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get(collection, 2, &result) != CNA_RESULT_SUCCESS ||
        result.position != 3.0F) {
        return 0;
    }

    CNA_CurveKey destination[4] = {key0, key0, key0, key0};
    if (cna_curve_key_collection_copy_to(collection, destination, 2U, 0, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || count != 3U || destination[0].position != 0.0F ||
        cna_curve_key_collection_copy_to(collection, destination, 4U, 1, &count) !=
            CNA_RESULT_SUCCESS || count != 3U || destination[0].position != 0.0F ||
        destination[1].position != 1.0F || destination[2].position != 2.0F ||
        destination[3].position != 3.0F ||
        cna_curve_key_collection_copy_to(collection, destination, 4U, -1, &count) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_curve_key_collection_clone(collection, &clone) != CNA_RESULT_SUCCESS ||
        clone == CNA_INVALID_HANDLE || clone == collection ||
        cna_curve_key_collection_clear(collection) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get_count(collection, &count) != CNA_RESULT_SUCCESS ||
        count != 0U ||
        cna_curve_key_collection_get_count(clone, &count) != CNA_RESULT_SUCCESS || count != 3U ||
        cna_curve_key_collection_remove(clone, key0, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_FALSE ||
        cna_curve_key_collection_remove(clone, key1, &predicate) != CNA_RESULT_SUCCESS ||
        predicate != CNA_TRUE ||
        cna_curve_key_collection_remove_at(clone, 1) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get_count(clone, &count) != CNA_RESULT_SUCCESS || count != 1U) {
        return 0;
    }

    result = key3;
    CNA_CurveKey invalid = key2;
    invalid.continuity = UINT32_MAX;
    if (cna_curve_key_collection_get(clone, 2, &result) != CNA_RESULT_INVALID_ARGUMENT ||
        result.position != 3.0F ||
        cna_curve_key_collection_set(clone, 0, invalid) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_key_collection_get(clone, 0, &result) != CNA_RESULT_SUCCESS ||
        result.position != 2.0F ||
        cna_curve_key_collection_get_count(CNA_INVALID_HANDLE, &count) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    WrongThreadState wrong_thread = {clone, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, get_collection_count_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 0;
    }

    if (cna_curve_key_collection_destroy(collection) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get_count(collection, &count) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_key_collection_destroy(collection) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_key_collection_destroy(clone) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

typedef struct CurveWrongThreadState {
    CNA_CurveHandle curve;
    CNA_Result result;
} CurveWrongThreadState;

static int get_curve_state_on_wrong_thread(void* const context)
{
    CurveWrongThreadState* const state = (CurveWrongThreadState*)context;
    CNA_Bool is_constant = CNA_FALSE;
    state->result = cna_curve_get_is_constant(state->curve, &is_constant);
    return 0;
}

static int validate_curve_loops(
    const CNA_CurveHandle curve,
    CNA_CurveLoopType* const loop_type,
    float* const value)
{
    if (cna_curve_set_pre_loop(curve, CNA_CURVE_LOOP_CONSTANT) != CNA_RESULT_SUCCESS ||
        cna_curve_set_post_loop(curve, CNA_CURVE_LOOP_CONSTANT) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, -1.0F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 0.0F) ||
        cna_curve_evaluate(curve, 3.0F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 4.0F) ||
        cna_curve_set_pre_loop(curve, CNA_CURVE_LOOP_LINEAR) != CNA_RESULT_SUCCESS ||
        cna_curve_set_post_loop(curve, CNA_CURVE_LOOP_LINEAR) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, -1.0F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, -2.0F) ||
        cna_curve_evaluate(curve, 3.0F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 6.0F) ||
        cna_curve_set_pre_loop(curve, CNA_CURVE_LOOP_CYCLE) != CNA_RESULT_SUCCESS ||
        cna_curve_set_post_loop(curve, CNA_CURVE_LOOP_CYCLE) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, -0.5F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 3.0F) ||
        cna_curve_evaluate(curve, 2.5F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 1.0F) ||
        cna_curve_set_pre_loop(curve, CNA_CURVE_LOOP_CYCLE_OFFSET) != CNA_RESULT_SUCCESS ||
        cna_curve_set_post_loop(curve, CNA_CURVE_LOOP_CYCLE_OFFSET) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, -0.5F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, -1.0F) ||
        cna_curve_evaluate(curve, 2.5F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 5.0F) ||
        cna_curve_set_pre_loop(curve, CNA_CURVE_LOOP_OSCILLATE) != CNA_RESULT_SUCCESS ||
        cna_curve_set_post_loop(curve, CNA_CURVE_LOOP_OSCILLATE) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, -0.5F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 1.0F) ||
        cna_curve_evaluate(curve, 2.5F, value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(*value, 3.0F) ||
        cna_curve_get_pre_loop(curve, loop_type) != CNA_RESULT_SUCCESS ||
        *loop_type != CNA_CURVE_LOOP_OSCILLATE ||
        cna_curve_get_post_loop(curve, loop_type) != CNA_RESULT_SUCCESS ||
        *loop_type != CNA_CURVE_LOOP_OSCILLATE) {
        return 0;
    }
    return 1;
}

static int validate_curve(void)
{
    CNA_CurveHandle curve = UINT64_MAX;
    CNA_CurveHandle clone = CNA_INVALID_HANDLE;
    CNA_CurveKeyCollectionHandle keys = CNA_INVALID_HANDLE;
    CNA_CurveKeyCollectionHandle clone_keys = CNA_INVALID_HANDLE;
    if (cna_curve_create(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_create(&curve) != CNA_RESULT_SUCCESS || curve == CNA_INVALID_HANDLE ||
        cna_curve_get_keys(curve, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_get_keys(curve, &keys) != CNA_RESULT_SUCCESS ||
        keys == CNA_INVALID_HANDLE) {
        return 0;
    }

    CNA_Bool is_constant = CNA_FALSE;
    CNA_CurveLoopType loop_type = UINT32_MAX;
    float value = -1.0F;
    if (cna_curve_get_is_constant(curve, &is_constant) != CNA_RESULT_SUCCESS ||
        is_constant != CNA_TRUE ||
        cna_curve_get_pre_loop(curve, &loop_type) != CNA_RESULT_SUCCESS ||
        loop_type != CNA_CURVE_LOOP_CONSTANT ||
        cna_curve_get_post_loop(curve, &loop_type) != CNA_RESULT_SUCCESS ||
        loop_type != CNA_CURVE_LOOP_CONSTANT ||
        cna_curve_evaluate(curve, 10.0F, &value) != CNA_RESULT_SUCCESS || value != 0.0F) {
        return 0;
    }

    CNA_CurveKey key0;
    CNA_CurveKey key1;
    CNA_CurveKey key2;
    if (cna_curve_key_init_full(
            0.0F, 0.0F, 2.0F, 2.0F, CNA_CURVE_CONTINUITY_SMOOTH, &key0) !=
            CNA_RESULT_SUCCESS ||
        cna_curve_key_init_full(
            1.0F, 2.0F, 2.0F, 2.0F, CNA_CURVE_CONTINUITY_SMOOTH, &key1) !=
            CNA_RESULT_SUCCESS ||
        cna_curve_key_init_full(
            2.0F, 4.0F, 2.0F, 2.0F, CNA_CURVE_CONTINUITY_SMOOTH, &key2) !=
            CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_add(keys, key2) != CNA_RESULT_SUCCESS ||
        cna_curve_get_is_constant(curve, &is_constant) != CNA_RESULT_SUCCESS ||
        is_constant != CNA_TRUE ||
        cna_curve_evaluate(curve, 0.5F, &value) != CNA_RESULT_SUCCESS || value != 4.0F ||
        cna_curve_key_collection_add(keys, key0) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_add(keys, key1) != CNA_RESULT_SUCCESS ||
        cna_curve_get_is_constant(curve, &is_constant) != CNA_RESULT_SUCCESS ||
        is_constant != CNA_FALSE ||
        cna_curve_evaluate(curve, 0.5F, &value) != CNA_RESULT_SUCCESS ||
        !nearly_equal(value, 1.0F)) {
        return 0;
    }

    CNA_CurveKey computed = key0;
    if (cna_curve_compute_tangents(curve, CNA_CURVE_TANGENT_LINEAR) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get(keys, 1, &computed) != CNA_RESULT_SUCCESS ||
        !nearly_equal(computed.tangent_in, 2.0F) || !nearly_equal(computed.tangent_out, 2.0F) ||
        cna_curve_compute_tangents_in_out(
            curve, CNA_CURVE_TANGENT_FLAT, CNA_CURVE_TANGENT_SMOOTH) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get(keys, 1, &computed) != CNA_RESULT_SUCCESS ||
        !nearly_equal(computed.tangent_in, 0.0F) || !nearly_equal(computed.tangent_out, 2.0F) ||
        cna_curve_compute_tangent(curve, 1, CNA_CURVE_TANGENT_LINEAR) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get(keys, 1, &computed) != CNA_RESULT_SUCCESS ||
        !nearly_equal(computed.tangent_in, 2.0F) || !nearly_equal(computed.tangent_out, 2.0F) ||
        cna_curve_compute_tangent_in_out(
            curve, 1, CNA_CURVE_TANGENT_FLAT, CNA_CURVE_TANGENT_LINEAR) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get(keys, 1, &computed) != CNA_RESULT_SUCCESS ||
        !nearly_equal(computed.tangent_in, 0.0F) || !nearly_equal(computed.tangent_out, 2.0F) ||
        cna_curve_compute_tangents(curve, CNA_CURVE_TANGENT_LINEAR) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_set(keys, 0, key0) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_set(keys, 1, key1) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_set(keys, 2, key2) != CNA_RESULT_SUCCESS ||
        !validate_curve_loops(curve, &loop_type, &value)) {
        return 0;
    }

    if (cna_curve_clone(curve, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_clone(curve, &clone) != CNA_RESULT_SUCCESS || clone == CNA_INVALID_HANDLE ||
        clone == curve || cna_curve_get_keys(clone, &clone_keys) != CNA_RESULT_SUCCESS ||
        cna_curve_key_set_value(&key1, 20.0F) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_set(keys, 1, key1) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, 1.0F, &value) != CNA_RESULT_SUCCESS || value != 20.0F ||
        cna_curve_evaluate(clone, 1.0F, &value) != CNA_RESULT_SUCCESS || value != 2.0F ||
        cna_curve_get_pre_loop(clone, &loop_type) != CNA_RESULT_SUCCESS ||
        loop_type != CNA_CURVE_LOOP_OSCILLATE ||
        cna_curve_get_post_loop(clone, &loop_type) != CNA_RESULT_SUCCESS ||
        loop_type != CNA_CURVE_LOOP_OSCILLATE) {
        return 0;
    }

    if (cna_curve_set_pre_loop(curve, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_get_pre_loop(curve, &loop_type) != CNA_RESULT_SUCCESS ||
        loop_type != CNA_CURVE_LOOP_OSCILLATE ||
        cna_curve_set_post_loop(curve, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_compute_tangents(curve, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_compute_tangents_in_out(
            curve, CNA_CURVE_TANGENT_LINEAR, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_compute_tangent(curve, 1, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_compute_tangent_in_out(
            curve, 1, CNA_CURVE_TANGENT_LINEAR, UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_compute_tangent(curve, -1, CNA_CURVE_TANGENT_LINEAR) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_compute_tangent_in_out(
            curve, 3, CNA_CURVE_TANGENT_LINEAR, CNA_CURVE_TANGENT_LINEAR) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_get_is_constant(curve, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_get_pre_loop(curve, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_evaluate(curve, 0.0F, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_get_is_constant(keys, &is_constant) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_destroy(keys) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    CurveWrongThreadState wrong_thread = {curve, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, get_curve_state_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 0;
    }

    uint64_t count = 0U;
    value = 123.0F;
    if (cna_curve_destroy(curve) != CNA_RESULT_SUCCESS ||
        cna_curve_evaluate(curve, 0.0F, &value) != CNA_RESULT_INVALID_HANDLE || value != 123.0F ||
        cna_curve_destroy(curve) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_key_collection_get_count(keys, &count) != CNA_RESULT_SUCCESS || count != 3U ||
        cna_curve_key_collection_destroy(keys) != CNA_RESULT_SUCCESS ||
        cna_curve_destroy(clone) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_get_count(clone_keys, &count) != CNA_RESULT_SUCCESS ||
        count != 3U || cna_curve_key_collection_destroy(clone_keys) != CNA_RESULT_SUCCESS ||
        cna_curve_destroy(clone) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!validate_construction_and_properties()) {
        return 1;
    }
    if (!validate_value_operations()) {
        return 2;
    }
    if (!validate_collection()) {
        return 3;
    }
    return validate_curve() ? 0 : 4;
}
