// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>
#include "CnaTestReport.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, __func__, #expression); \
        return CNA_TEST_FAIL(1); \
    } \
} while (0)
#define SV(text) ((CNA_StringView){(text), sizeof(text) - 1U})
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static int filled(const void* data, size_t size, unsigned char byte)
{
    const unsigned char* bytes = data;
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] != byte) return 0;
    }
    return 1;
}

static int last_error(CNA_Result result, CNA_ErrorCategory category)
{
    CNA_ErrorInfo info = {sizeof(info), 1, 0, 0, 0};
    uint64_t size = UINT64_MAX, copied = UINT64_MAX;
    char message[2048];
    memset(message, 0x5a, sizeof(message));
    CHECK(cna_error_get_last_info(&info) == CNA_RESULT_SUCCESS);
    CHECK(info.result == result && info.category == category);
    CHECK(cna_error_get_last_message_size(&size) == CNA_RESULT_SUCCESS);
    CHECK(size > 0 && size < sizeof(message) && size == info.message_byte_length);
    CHECK(cna_error_copy_last_message(message, size, &copied) == CNA_RESULT_SUCCESS);
    CHECK(copied == size && memchr(message, '\0', (size_t)size) == NULL);
    CHECK(filled(message + size, sizeof(message) - (size_t)size, 0x5a));
    return 0;
}


/* Oracles: FNA MathHelper.cs formulas, evaluated below with exact binary fractions;
 * math.h defines the C output, epsilon and boolean contracts. */
static int math_contract(void)
{
    float value = -77;
    int32_t integer = 77;
    CNA_Bool flag = 99;
    /* 2 + (6-2)*(-.5) + (10-2)*1.5 = 12: barycentric is not clamped. */
    CHECK(cna_math_barycentric(2, 6, 10, -.5f, 1.5f, &value) == CNA_RESULT_SUCCESS && value == 12);
    /* CatmullRom: .5*(10 - 1.5 - 8.25 + 3) = 1.625. */
    CHECK(cna_math_catmull_rom(2, 5, -1, 8, .5f, &value) == CNA_RESULT_SUCCESS && value == 1.625f);
    CHECK(cna_math_catmull_rom(2, 5, -1, 8, 0, &value) == CNA_RESULT_SUCCESS && value == 5);
    CHECK(cna_math_catmull_rom(2, 5, -1, 8, 1, &value) == CNA_RESULT_SUCCESS && value == -1);
    /* Hermite: -14/64 + 18/16 + 4/4 + 2 = 3.90625. */
    CHECK(cna_math_hermite(2, 4, 10, -2, .25f, &value) == CNA_RESULT_SUCCESS && value == 3.90625f);
    CHECK(cna_math_hermite(2, 4, 10, -2, 0, &value) == CNA_RESULT_SUCCESS && value == 2);
    CHECK(cna_math_hermite(2, 4, 10, -2, 1, &value) == CNA_RESULT_SUCCESS && value == 10);
    CHECK(cna_math_lerp(2, 10, -.5f, &value) == CNA_RESULT_SUCCESS && value == -2);
    CHECK(cna_math_lerp(2, 10, 1.5f, &value) == CNA_RESULT_SUCCESS && value == 14);
    /* SmoothStep uses 3t^2-2t^3, clamped to [0,1]: 16*(3/16-2/64)=2.5. */
    CHECK(cna_math_smooth_step(0, 16, .25f, &value) == CNA_RESULT_SUCCESS && value == 2.5f);
    CHECK(cna_math_smooth_step(2, 10, -1, &value) == CNA_RESULT_SUCCESS && value == 2);
    CHECK(cna_math_distance(3, -2, &value) == CNA_RESULT_SUCCESS && value == 5);
    const float inputs[] = {-4, -3, 1, 7, 8};
    const float expected[] = {-3, -3, 1, 7, 7};
    for (size_t i = 0; i < ARRAY_COUNT(inputs); ++i) {
        CHECK(cna_math_clamp_float(inputs[i], -3, 7, &value) == CNA_RESULT_SUCCESS && value == expected[i]);
        CHECK(cna_math_clamp_int32((int32_t)inputs[i], -3, 7, &integer) == CNA_RESULT_SUCCESS && integer == (int32_t)expected[i]);
    }
    /* Clamp checks maximum then minimum, including reversed bounds (FNA Clamp). */
    CHECK(cna_math_clamp_int32(5, 10, 0, &integer) == CNA_RESULT_SUCCESS && integer == 10);
    CHECK(cna_math_clamp_int32(INT32_MIN, INT32_MIN, INT32_MAX, &integer) == CNA_RESULT_SUCCESS && integer == INT32_MIN);
    CHECK(cna_math_clamp_float(5, 10, 0, &value) == CNA_RESULT_SUCCESS && value == 10);
    CHECK(cna_math_min(4, -3, &value) == CNA_RESULT_SUCCESS && value == -3);
    CHECK(cna_math_max(4, -3, &value) == CNA_RESULT_SUCCESS && value == 4);
    CHECK(cna_math_to_degrees(-CNA_MATH_PI_OVER_2, &value) == CNA_RESULT_SUCCESS && fabsf(value + 90) < .00001f);
    CHECK(cna_math_to_radians(-90, &value) == CNA_RESULT_SUCCESS && fabsf(value + CNA_MATH_PI_OVER_2) < .000001f);
    CHECK(cna_math_wrap_angle(CNA_MATH_TWO_PI, &value) == CNA_RESULT_SUCCESS && value == 0);
    CHECK(cna_math_wrap_angle(-CNA_MATH_PI, &value) == CNA_RESULT_SUCCESS && value == CNA_MATH_PI);
    CHECK(cna_math_wrap_angle(CNA_MATH_PI, &value) == CNA_RESULT_SUCCESS && value == CNA_MATH_PI);
    CHECK(cna_math_wrap_angle(-0.0f, &value) == CNA_RESULT_SUCCESS && signbit(value));
    /* math.h specifies the framework epsilon 2^-24; WithinEpsilon uses strict
     * abs(a-b) < epsilon, symmetrically (this differs from FNA's 2^-23). */
    CHECK(cna_math_within_epsilon(0, -CNA_MATH_MACHINE_EPSILON_FLOAT, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE);
    CHECK(cna_math_within_epsilon(0, -CNA_MATH_MACHINE_EPSILON_FLOAT * .5f, &flag) == CNA_RESULT_SUCCESS && flag == CNA_TRUE);
    CHECK(cna_math_within_epsilon(NAN, 0, &flag) == CNA_RESULT_SUCCESS && flag == CNA_FALSE);
    const int32_t msaa_input[] = {0, 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, INT32_MAX};
    const int32_t msaa_expected[] = {0, 0, 2, 2, 4, 4, 8, 8, 8, 16, 16, 1073741824};
    for (size_t i = 0; i < ARRAY_COUNT(msaa_input); ++i) {
        CHECK(cna_math_closest_msaa_power(msaa_input[i], &integer) == CNA_RESULT_SUCCESS && integer == msaa_expected[i]);
    }
    integer = 77;
    CHECK(cna_math_closest_msaa_power(INT32_MIN, &integer) == CNA_RESULT_INVALID_ARGUMENT && integer == 77);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CHECK(cna_math_barycentric(1, 2, 3, 0, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_catmull_rom(1, 2, 3, 4, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_clamp_float(0, 0, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_distance(0, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_hermite(1, 2, 3, 4, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_lerp(0, 1, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_min(0, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_max(0, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_smooth_step(0, 1, 0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_to_degrees(0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_to_radians(0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_wrap_angle(0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_clamp_int32(0, 0, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_within_epsilon(0, 1, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_math_closest_msaa_power(0, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    return 0;
}

/* Oracle: shared copy protocol in docs/c-api/STRINGS_AND_BUFFERS.md;
 * FNA CurveKeyCollection.Add sorts by position; each view is live, Clone is independent. */
static int curve_contract(void)
{
    CNA_CurveHandle curve = 0;
    CNA_CurveKeyCollectionHandle keys = 0, sibling = 0, clone = 0;
    const CNA_CurveKey first = {1, 11, 2, 3, CNA_CURVE_CONTINUITY_SMOOTH};
    const CNA_CurveKey second = {3, 33, 4, 5, CNA_CURVE_CONTINUITY_STEP};
    CNA_CurveKey output[5], snapshot[5];
    uint64_t count = 77;
    CHECK(cna_curve_create(&curve) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_get_keys(curve, &keys) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_get_keys(curve, &sibling) == CNA_RESULT_SUCCESS && keys != sibling);
    CHECK(cna_curve_key_collection_copy_to(keys, NULL, 0, 0, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_curve_key_collection_add(keys, second) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_key_collection_add(keys, first) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_key_collection_get_count(sibling, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_curve_key_collection_clone(keys, &clone) == CNA_RESULT_SUCCESS);
    /* FNA Curve.Evaluate Hermite at t=.5: (11+33)/2 + (3-4)/8 = 21.875. */
    float value = -77;
    CHECK(cna_curve_evaluate(curve, 2, &value) == CNA_RESULT_SUCCESS && value == 21.875f);
    CHECK(cna_curve_set_pre_loop(curve, UINT32_MAX) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_curve_set_post_loop(curve, UINT32_MAX) == CNA_RESULT_INVALID_ARGUMENT);
    CNA_CurveLoopType loop = UINT32_MAX;
    CHECK(cna_curve_get_pre_loop(curve, &loop) == CNA_RESULT_SUCCESS && loop == CNA_CURVE_LOOP_CONSTANT);
    CHECK(cna_curve_get_post_loop(curve, &loop) == CNA_RESULT_SUCCESS && loop == CNA_CURVE_LOOP_CONSTANT);
    for (unsigned field = 0; field < 2; ++field) {
        CHECK(cna_curve_compute_tangents_in_out(curve, field ? CNA_CURVE_TANGENT_LINEAR : UINT32_MAX,
              field ? UINT32_MAX : CNA_CURVE_TANGENT_LINEAR) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(cna_curve_compute_tangent_in_out(curve, 0, field ? CNA_CURVE_TANGENT_LINEAR : UINT32_MAX,
              field ? UINT32_MAX : CNA_CURVE_TANGENT_LINEAR) == CNA_RESULT_INVALID_ARGUMENT);
    }
    for (uint64_t capacity = 0; capacity < 3; ++capacity) {
        memset(output, 0xa5, sizeof(output)); count = 77;
        CHECK(cna_curve_key_collection_copy_to(keys, output, capacity, capacity ? 1 : 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(count == 2 && filled(output, sizeof(output), 0xa5));
    }
    count = 77;
    CHECK(cna_curve_key_collection_copy_to(keys, NULL, 0, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == 2);
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_curve_key_collection_copy_to(keys, output, 4, 1, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(filled(&output[0], sizeof(output[0]), 0xa5) && filled(&output[3], 2 * sizeof(output[0]), 0xa5));
    CHECK(memcmp(&output[1], &first, sizeof(first)) == 0 && memcmp(&output[2], &second, sizeof(second)) == 0);
    memset(output, 0xa5, sizeof(output)); memcpy(snapshot, output, sizeof(output));
    for (unsigned mode = 0; mode < 4; ++mode) {
        count = 77;
        CHECK(cna_curve_key_collection_copy_to(keys, mode == 0 ? NULL : output, 4,
              mode == 1 ? -1 : mode == 2 ? 5 : 0, mode == 3 ? NULL : &count) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(count == 77 && memcmp(output, snapshot, sizeof(output)) == 0);
    }
    CNA_CurveKey invalid = first;
    invalid.continuity = UINT32_MAX;
    CHECK(cna_curve_key_collection_set(keys, 0, invalid) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_curve_key_collection_get(sibling, 0, &output[0]) == CNA_RESULT_SUCCESS && memcmp(output, &first, sizeof(first)) == 0);
    /* Destroy owner first: its two key views independently retain the collection. */
    CHECK(cna_curve_destroy(curve) == CNA_RESULT_SUCCESS);
    value = -77;
    CHECK(cna_curve_evaluate(curve, 2, &value) == CNA_RESULT_INVALID_HANDLE && value == -77);
    CNA_Handle refused_keys = UINT64_MAX;
    CHECK(cna_curve_get_keys(curve, &refused_keys) == CNA_RESULT_INVALID_HANDLE && refused_keys == CNA_INVALID_HANDLE);
    CHECK(cna_curve_key_collection_clear(keys) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_key_collection_get_count(sibling, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_curve_key_collection_get_count(clone, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(cna_curve_key_collection_destroy(keys) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_key_collection_add(sibling, second) == CNA_RESULT_SUCCESS);
    CHECK(cna_curve_key_collection_destroy(sibling) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output)); count = 77;
    CHECK(cna_curve_key_collection_copy_to(sibling, output, 5, 0, &count) == CNA_RESULT_INVALID_HANDLE);
    CHECK(count == 77 && filled(output, sizeof(output), 0xa5));
    CNA_Handle refused = UINT64_MAX;
    CHECK(cna_curve_key_collection_clone(sibling, &refused) == CNA_RESULT_INVALID_HANDLE && refused == CNA_INVALID_HANDLE);
    CHECK(cna_curve_key_collection_destroy(clone) == CNA_RESULT_SUCCESS);
    return 0;
}

/* Oracle: vertex_resources.h assignment is native value assignment, not aliasing;
 * STRINGS_AND_BUFFERS.md requires complete copies and an untouched unused suffix. */
static int declaration_contract(void)
{
    const CNA_VertexElement elements[] = {
        {0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0},
        {12, CNA_VERTEX_ELEMENT_FORMAT_COLOR, CNA_VERTEX_ELEMENT_USAGE_COLOR, 0}
    };
    CNA_VertexDeclarationHandle source = 0, target = 0, empty = 0;
    CNA_VertexElement output[4];
    uint64_t count = 77;
    int32_t stride = -1;
    CHECK(cna_vertex_declaration_create_with_stride(20, elements, 2, &source) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_create_empty(&target) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_create_empty(&empty) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_copy_elements(empty, NULL, 0, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_vertex_declaration_assign_ext(target, source) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_assign_ext(source, empty) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_destroy(source) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_get_stride(target, &stride) == CNA_RESULT_SUCCESS && stride == 20);
    for (uint64_t capacity = 0; capacity < 2; ++capacity) {
        memset(output, 0xa5, sizeof(output)); count = 77;
        CHECK(cna_vertex_declaration_copy_elements(target, output + 1, capacity, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(count == 2 && filled(output, sizeof(output), 0xa5));
    }
    CHECK(cna_vertex_declaration_copy_elements(target, NULL, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == 2);
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_vertex_declaration_copy_elements(target, output + 1, 2, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(filled(output, sizeof(output[0]), 0xa5) && filled(output + 3, sizeof(output[0]), 0xa5));
    CHECK(memcmp(output + 1, elements, sizeof(elements)) == 0);
    memset(output, 0xa5, sizeof(output)); count = 77;
    CHECK(cna_vertex_declaration_copy_elements(target, output, 4, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_vertex_declaration_copy_elements(target, NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT && count == 77);
    CHECK(filled(output, sizeof(output), 0xa5));
    CHECK(cna_vertex_declaration_assign_ext(target, source) == CNA_RESULT_INVALID_HANDLE);
    CHECK(cna_vertex_declaration_assign_ext(target, target) == CNA_RESULT_SUCCESS);
    CHECK(cna_vertex_declaration_get_stride(target, &stride) == CNA_RESULT_SUCCESS && stride == 20);
    CHECK(cna_vertex_declaration_copy_elements(target, output, 4, &count) == CNA_RESULT_SUCCESS && count == 2);
    CHECK(memcmp(output, elements, sizeof(elements)) == 0);
    for (unsigned field = 0; field < 4; ++field) {
        CNA_VertexElement invalid = elements[0];
        if (field == 0) invalid.format = UINT32_MAX;
        if (field == 1) invalid.usage = UINT32_MAX;
        if (field == 2) invalid.offset = -1;
        if (field == 3) invalid.usage_index = -1;
        CNA_Handle refused = UINT64_MAX;
        CHECK(cna_vertex_declaration_create(&invalid, 1, &refused) == CNA_RESULT_INVALID_ARGUMENT && refused == CNA_INVALID_HANDLE);
    }
    CNA_Handle refused = UINT64_MAX;
    CHECK(cna_vertex_declaration_create(NULL, 1, &refused) == CNA_RESULT_INVALID_ARGUMENT && refused == CNA_INVALID_HANDLE);
    CHECK(cna_vertex_declaration_destroy(target) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output)); count = 77; stride = -1;
    CHECK(cna_vertex_declaration_copy_elements(target, output, 4, &count) == CNA_RESULT_INVALID_HANDLE);
    CHECK(count == 77 && filled(output, sizeof(output), 0xa5));
    CHECK(cna_vertex_declaration_get_stride(target, &stride) == CNA_RESULT_INVALID_HANDLE && stride == -1);
    CHECK(cna_vertex_declaration_destroy(empty) == CNA_RESULT_SUCCESS);
    return 0;
}


/* Oracle: models.h and runtime_components.h explicitly define default cursor state,
 * cleared owned-handle outputs and preserved scalar outputs on refusal. */
static int cursor_contract(void)
{
    const struct {
        CNA_Result (*create)(CNA_Handle, CNA_Handle*);
        CNA_Result (*next)(CNA_Handle, CNA_Bool*);
        CNA_Result (*current)(CNA_Handle, CNA_Handle*);
        CNA_Result (*reset)(CNA_Handle);
        CNA_Result (*destroy)(CNA_Handle);
    } families[] = {
        {cna_model_effect_collection_create_enumerator, cna_model_effect_enumerator_move_next,
         cna_model_effect_enumerator_get_current, cna_model_effect_enumerator_reset, cna_model_effect_enumerator_destroy},
        {cna_game_components_create_enumerator, cna_game_component_enumerator_move_next,
         cna_game_component_enumerator_get_current, cna_game_component_enumerator_reset, cna_game_component_enumerator_destroy}
    };
    for (size_t i = 0; i < ARRAY_COUNT(families); ++i) {
        CNA_Handle cursor = UINT64_MAX, current = UINT64_MAX;
        CNA_Bool flag = 99;
        CHECK(families[i].create(CNA_INVALID_HANDLE, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(families[i].create(UINT64_MAX, &cursor) == CNA_RESULT_INVALID_HANDLE && cursor == CNA_INVALID_HANDLE);
        CHECK(families[i].create(CNA_INVALID_HANDLE, &cursor) == CNA_RESULT_SUCCESS && cursor != CNA_INVALID_HANDLE);
        CHECK(families[i].current(cursor, &current) == CNA_RESULT_SUCCESS && current == CNA_INVALID_HANDLE);
        CHECK(families[i].next(cursor, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(families[i].next(cursor, &flag) == CNA_RESULT_INVALID_STATE && flag == 99);
        CHECK(families[i].reset(cursor) == CNA_RESULT_INVALID_STATE);
        current = UINT64_MAX;
        CHECK(families[i].current(cursor, &current) == CNA_RESULT_SUCCESS && current == CNA_INVALID_HANDLE);
        CHECK(families[i].current(cursor, NULL) == CNA_RESULT_INVALID_ARGUMENT);
        if (i == 1) {
            current = UINT64_MAX;
            CHECK(cna_game_component_enumerator_get_current_object(cursor, &current) == CNA_RESULT_INVALID_STATE && current == CNA_INVALID_HANDLE);
        }
        CHECK(families[i].destroy(cursor) == CNA_RESULT_SUCCESS);
        CHECK(families[i].next(cursor, &flag) == CNA_RESULT_INVALID_HANDLE && flag == 99);
        current = UINT64_MAX;
        CHECK(families[i].current(cursor, &current) == CNA_RESULT_INVALID_HANDLE && current == CNA_INVALID_HANDLE);
        CHECK(families[i].reset(cursor) == CNA_RESULT_INVALID_HANDLE);
        CHECK(families[i].destroy(cursor) == CNA_RESULT_INVALID_HANDLE);
    }
    return 0;
}

static int point_contract(void)
{
    CNA_Point output = {77, -88};
    CHECK(cna_point_get_zero(&output) == CNA_RESULT_SUCCESS);
    CHECK(output.x == 0 && output.y == 0);
    /* Oracle: FNA Point.cs field equality and unchecked 32-bit component arithmetic. */
    CHECK(cna_point_init(&output) == CNA_RESULT_SUCCESS && output.x == 0 && output.y == 0);
    CHECK(cna_point_init_xy(-19, 23, &output) == CNA_RESULT_SUCCESS && output.x == -19 && output.y == 23);
    CHECK(cna_point_init(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_point_init_xy(1, 2, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    const CNA_Point origin = {INT32_MAX, INT32_MIN};
    CNA_Bool flag = 99;
    for (unsigned component = 0; component < 3; ++component) {
        CNA_Point other = origin;
        if (component == 1) other.x = 0;
        if (component == 2) other.y = 0;
        CHECK(cna_point_equals(origin, other, &flag) == CNA_RESULT_SUCCESS && flag == (component == 0 ? CNA_TRUE : CNA_FALSE));
        CHECK(cna_point_not_equals(origin, other, &flag) == CNA_RESULT_SUCCESS && flag == (component == 0 ? CNA_FALSE : CNA_TRUE));
    }
    CHECK(cna_point_equals(origin, origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_point_not_equals(origin, origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    int32_t hash = 77;
    /* INT32_MAX + INT32_MIN = -1, and 0 + (-1) has the same additive hash. */
    CHECK(cna_point_get_hash_code(origin, &hash) == CNA_RESULT_SUCCESS && hash == -1);
    CHECK(cna_point_get_hash_code((CNA_Point){0, -1}, &hash) == CNA_RESULT_SUCCESS && hash == -1);
    CHECK(cna_point_get_hash_code(origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_point_add(origin, (CNA_Point){1, -1}, &output) == CNA_RESULT_SUCCESS && output.x == INT32_MIN && output.y == INT32_MAX);
    CHECK(cna_point_subtract(origin, (CNA_Point){-1, 1}, &output) == CNA_RESULT_SUCCESS && output.x == INT32_MIN && output.y == INT32_MAX);
    CHECK(cna_point_multiply(origin, (CNA_Point){2, -1}, &output) == CNA_RESULT_SUCCESS && output.x == -2 && output.y == INT32_MIN);
    CHECK(cna_point_add(origin, origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_point_subtract(origin, origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_point_multiply(origin, origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_point_get_zero(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CHECK(cna_point_divide((CNA_Point){-19, 23}, (CNA_Point){4, -5}, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.x == -4 && output.y == -4);
    /* The second component fails after a valid first quotient could have been computed. */
    const CNA_Point left[] = {{19, 23}, {19, 23}, {INT32_MIN, 23}, {19, INT32_MIN}};
    const CNA_Point right[] = {{0, 5}, {4, 0}, {-1, 5}, {4, -1}};
    for (size_t i = 0; i < ARRAY_COUNT(left); ++i) {
        output = (CNA_Point){77, -88};
        CNA_Result expected = i < 2 ? CNA_RESULT_INVALID_ARGUMENT : CNA_RESULT_OVERFLOW;
        CHECK(cna_point_divide(left[i], right[i], &output) == expected);
        CHECK(output.x == 77 && output.y == -88);
        CHECK(last_error(expected, i < 2 ? CNA_ERROR_CATEGORY_ARGUMENT : CNA_ERROR_CATEGORY_RANGE) == 0);
    }
    CHECK(cna_point_divide((CNA_Point){1, 2}, (CNA_Point){1, 1}, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    /* Oracle: Point.ToString format; decimal Int32 endpoints, no terminator in C copy protocol. */
    const char expected[] = "{X:2147483647 Y:-2147483648}";
    char bytes[64];
    uint64_t size = 77, count = 77;
    CHECK(cna_point_get_string_size(origin, &size) == CNA_RESULT_SUCCESS && size == sizeof(expected) - 1U);
    CHECK(cna_point_get_string_size(origin, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    for (uint64_t capacity = 0; capacity <= size + 1; ++capacity) {
        memset(bytes, 0xa5, sizeof(bytes)); count = 77;
        CHECK(cna_point_copy_string(origin, bytes + 1, capacity, &count) ==
              (capacity < size ? CNA_RESULT_BUFFER_TOO_SMALL : CNA_RESULT_SUCCESS));
        CHECK(count == size);
        if (capacity < size) CHECK(filled(bytes, sizeof(bytes), 0xa5));
        else {
            CHECK((unsigned char)bytes[0] == 0xa5 && memcmp(bytes + 1, expected, (size_t)size) == 0);
            CHECK(filled(bytes + 1 + size, sizeof(bytes) - 1U - (size_t)size, 0xa5));
        }
    }
    CHECK(cna_point_copy_string(origin, NULL, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == size);
    count = 77; memset(bytes, 0xa5, sizeof(bytes));
    CHECK(cna_point_copy_string(origin, NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT && count == 77);
    CHECK(cna_point_copy_string(origin, bytes, sizeof(bytes), NULL) == CNA_RESULT_INVALID_ARGUMENT && filled(bytes, sizeof(bytes), 0xa5));
    return 0;
}

static int rectangle_contract(void)
{
    const CNA_Rectangle rectangle = {-12, 34, 56, 78};
    const char expected[] = "{X:-12 Y:34 Width:56 Height:78}";
    char output[96];
    uint64_t size = UINT64_MAX, written = UINT64_MAX;
    CHECK(cna_rectangle_get_string_size(rectangle, &size) == CNA_RESULT_SUCCESS);
    CHECK(size == sizeof(expected) - 1U);
    CHECK(cna_rectangle_copy_string(rectangle, NULL, 0, &written) == CNA_RESULT_BUFFER_TOO_SMALL);
    CHECK(written == size);
    CHECK(last_error(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE) == 0);
    for (uint64_t capacity = 0; capacity < size; ++capacity) {
        memset(output, 0xa5, sizeof(output));
        written = UINT64_MAX;
        CHECK(cna_rectangle_copy_string(rectangle, output + 1, capacity, &written) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(written == size && filled(output, sizeof(output), 0xa5));
    }
    for (unsigned extra = 0; extra < 2; ++extra) {
        memset(output, 0xa5, sizeof(output));
        CHECK(cna_rectangle_copy_string(rectangle, output + 1, size + extra, &written) == CNA_RESULT_SUCCESS);
        CHECK(written == size && (unsigned char)output[0] == 0xa5);
        CHECK(memcmp(output + 1, expected, (size_t)size) == 0);
        CHECK(filled(output + 1 + size, sizeof(output) - 1U - (size_t)size, 0xa5));
    }
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_rectangle_copy_string(rectangle, output, sizeof(output), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(output, sizeof(output), 0xa5));
    CHECK(cna_rectangle_copy_string(rectangle, NULL, 1, &written) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CHECK(cna_rectangle_get_string_size(rectangle, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    return 0;
}

static int vector_contract(void)
{
    const CNA_Matrix matrix = {2, 0, 0, 0, 0, -3, 0, 0, 0, 0, 1, 0, 10, 20, 0, 1};
    const CNA_Vector2 source[] = {{1, 2}, {3, 4}, {5, 6}};
    CNA_Vector2 output[4];
    const CNA_Vector2 sentinel = {-77, 88};
    for (size_t i = 0; i < ARRAY_COUNT(output); ++i) output[i] = sentinel;
    CHECK(cna_vector2_transform_matrix_array(source, 3, 1, matrix, output, 4, 1, 2) == CNA_RESULT_SUCCESS);
    CHECK(output[0].x == -77 && output[0].y == 88 && output[3].x == -77 && output[3].y == 88);
    CHECK(output[1].x == 16 && output[1].y == 8 && output[2].x == 20 && output[2].y == 2);
    CHECK(cna_vector2_transform_normal_array(source, 3, 1, matrix, output, 4, 1, 2) == CNA_RESULT_SUCCESS);
    CHECK(output[1].x == 6 && output[1].y == -12 && output[2].x == 10 && output[2].y == -18);
    const uint64_t ranges[][3] = {{2, 0, 2}, {0, 3, 2}, {4, 0, 0}, {0, 5, 0}, {UINT64_MAX, 0, 1}, {0, UINT64_MAX, 1}};
    for (size_t i = 0; i < ARRAY_COUNT(ranges); ++i) {
        for (size_t j = 0; j < ARRAY_COUNT(output); ++j) output[j] = sentinel;
        CHECK(cna_vector2_transform_matrix_array(source, 3, ranges[i][0], matrix,
              output, 4, ranges[i][1], ranges[i][2]) == CNA_RESULT_INVALID_ARGUMENT);
        for (size_t j = 0; j < ARRAY_COUNT(output); ++j) CHECK(output[j].x == -77 && output[j].y == 88);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
        CHECK(cna_vector2_transform_normal_array(source, 3, ranges[i][0], matrix,
              output, 4, ranges[i][1], ranges[i][2]) == CNA_RESULT_INVALID_ARGUMENT);
        for (size_t j = 0; j < ARRAY_COUNT(output); ++j) CHECK(output[j].x == -77 && output[j].y == 88);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    CHECK(cna_vector2_transform_matrix_array(NULL, 0, 0, matrix, NULL, 0, 0, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_vector2_transform_normal_array(NULL, 0, 0, matrix, NULL, 0, 0, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_vector2_transform_matrix_array(source, 3, 3, matrix, output, 4, 4, 0) == CNA_RESULT_SUCCESS);
    CHECK(cna_vector2_transform_matrix_array(NULL, 1, 0, matrix, output, 4, 0, 0) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_vector2_transform_matrix_array(source, 3, 0, matrix, NULL, 1, 0, 0) == CNA_RESULT_INVALID_ARGUMENT);
    return 0;
}

/* Uniform signature adapters share the guarded capacity checks across text routes. */
typedef CNA_Result (*TextCopy)(CNA_Handle, char*, uint64_t, uint64_t*);
static CNA_Result diagnostic_message(CNA_Handle handle, char* out, uint64_t capacity, uint64_t* count)
{
    return cna_shader_diagnostics_copy_message_ext(handle, 0, out, capacity, count);
}
static CNA_Result diagnostic_label(CNA_Handle handle, char* out, uint64_t capacity, uint64_t* count)
{
    return cna_shader_diagnostics_copy_source_label_ext(handle, 0, out, capacity, count);
}
static CNA_Result binding_name(CNA_Handle handle, char* out, uint64_t capacity, uint64_t* count)
{
    return cna_shader_package_copy_binding_name_ext(handle, 0, out, capacity, count);
}

static int text_copy_contract(TextCopy copy, CNA_Handle handle, const char* expected, size_t size)
{
    char output[128];
    uint64_t count = UINT64_MAX;
    CHECK(size + 2 < sizeof(output));
    CHECK(copy(handle, NULL, 0, &count) == (size ? CNA_RESULT_BUFFER_TOO_SMALL : CNA_RESULT_SUCCESS));
    CHECK(count == size);
    if (size) CHECK(last_error(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE) == 0);
    for (size_t capacity = 0; capacity < size; ++capacity) {
        memset(output, 0xa5, sizeof(output));
        count = UINT64_MAX;
        CHECK(copy(handle, output + 1, capacity, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(count == size && filled(output, sizeof(output), 0xa5));
    }
    for (unsigned extra = 0; extra < 2; ++extra) {
        memset(output, 0xa5, sizeof(output));
        CHECK(copy(handle, output + 1, size + extra, &count) == CNA_RESULT_SUCCESS);
        CHECK(count == size && (unsigned char)output[0] == 0xa5);
        CHECK(memcmp(output + 1, expected, size) == 0);
        CHECK(filled(output + 1 + size, sizeof(output) - 1U - size, 0xa5));
    }
    memset(output, 0xa5, sizeof(output));
    CHECK(copy(handle, output, sizeof(output), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(output, sizeof(output), 0xa5));
    CHECK(copy(handle, NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static const CNA_StringView invalid_text[] = {
    {"\xc0\xaf", 2},             /* Overlong encoding. */
    {"\xed\xa0\x80", 3},         /* Surrogate. */
    {"\xf4\x90\x80\x80", 4},     /* Above U+10FFFF. */
    {"\xe2\x82", 2},             /* Truncated sequence. */
    {"\x80", 1},                 /* Lone continuation. */
    {"a\0b", 3}
};
static const char unicode_text[] = "\xc5\xbe\xe7\x8c\xab\xf0\x9f\x98\x80";


/* Oracle: gamer_services.h copies UTF-8 gamertags during creation; the shared text
 * protocol counts bytes, refuses invalid scalar encodings and never writes a terminator. */
static int gamer_text_contract(void)
{
    CNA_SignedInGamerHandle gamer = 0;
    char input[sizeof(unicode_text) - 1U];
    memcpy(input, unicode_text, sizeof(input));
    CHECK(cna_signed_in_gamer_create_ext((CNA_StringView){input, sizeof(input)}, CNA_TRUE,
          CNA_FALSE, CNA_PLAYER_INDEX_FOUR, &gamer) == CNA_RESULT_SUCCESS);
    memset(input, 'q', sizeof(input));
    uint64_t count = 77;
    CHECK(cna_signed_in_gamer_get_gamertag_size(gamer, &count) == CNA_RESULT_SUCCESS && count == sizeof(input));
    CHECK(text_copy_contract(cna_signed_in_gamer_copy_gamertag, gamer, unicode_text, sizeof(input)) == 0);
    CHECK(cna_signed_in_gamer_get_gamertag_size(gamer, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_signed_in_gamer_destroy(gamer) == CNA_RESULT_SUCCESS);
    char output[32]; memset(output, 0xa5, sizeof(output)); count = 77;
    CHECK(cna_signed_in_gamer_copy_gamertag(gamer, output, sizeof(output), &count) == CNA_RESULT_INVALID_HANDLE);
    CHECK(count == 77 && filled(output, sizeof(output), 0xa5));
    CHECK(cna_signed_in_gamer_get_gamertag_size(gamer, &count) == CNA_RESULT_INVALID_HANDLE && count == 77);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        gamer = UINT64_MAX;
        CHECK(cna_signed_in_gamer_create_ext(invalid_text[i], CNA_FALSE, CNA_FALSE,
              CNA_PLAYER_INDEX_ONE, &gamer) == CNA_RESULT_ENCODING && gamer == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
    }
    for (unsigned mode = 0; mode < 4; ++mode) {
        gamer = UINT64_MAX;
        CHECK(cna_signed_in_gamer_create_ext(mode == 0 ? (CNA_StringView){NULL, 1} : SV("tag"),
              mode == 1 ? (CNA_Bool)2 : CNA_FALSE, mode == 2 ? (CNA_Bool)255 : CNA_FALSE,
              mode == 3 ? UINT32_MAX : CNA_PLAYER_INDEX_ONE, &gamer) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(gamer == CNA_INVALID_HANDLE);
    }
    CHECK(cna_signed_in_gamer_create_ext(SV("tag"), CNA_FALSE, CNA_FALSE, CNA_PLAYER_INDEX_ONE, NULL) == CNA_RESULT_INVALID_ARGUMENT);
    for (unsigned mode = 0; mode < 2; ++mode) {
        CHECK(cna_signed_in_gamer_create_ext(mode ? SV("") : (CNA_StringView){NULL, 0},
              CNA_FALSE, CNA_FALSE, CNA_PLAYER_INDEX_ONE, &gamer) == CNA_RESULT_SUCCESS);
        CHECK(text_copy_contract(cna_signed_in_gamer_copy_gamertag, gamer, "", 0) == 0);
        CHECK(cna_signed_in_gamer_destroy(gamer) == CNA_RESULT_SUCCESS);
    }
    return 0;
}

static int diagnostic_contract(void)
{
    CNA_ShaderDiagnosticInfoEXT info = {sizeof(info), 1, CNA_SHADER_DIAGNOSTIC_SEVERITY_WARNING_EXT,
                                       CNA_SHADER_STAGE_FRAGMENT_EXT, 12, 7};
    CNA_ShaderDiagnosticsHandleEXT handle = CNA_INVALID_HANDLE;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV(unicode_text), SV(unicode_text), &handle) == CNA_RESULT_SUCCESS);
    uint64_t count = UINT64_MAX;
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS && count == 1);
    CNA_ShaderDiagnosticInfoEXT output;
    memset(&output, 0xa5, sizeof(output));
    output.struct_size = sizeof(output); output.struct_version = 1;
    CHECK(cna_shader_diagnostics_get_info_ext(handle, 0, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.severity == info.severity && output.stage == info.stage && output.line == 12 && output.column == 7);
    CHECK(text_copy_contract(diagnostic_message, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(diagnostic_label, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    for (unsigned mode = 0; mode < 3; ++mode) {
        memset(&output, 0xa5, sizeof(output));
        output.struct_size = mode == 0 ? sizeof(output) - 1U : sizeof(output);
        output.struct_version = mode == 1 ? 2 : 1;
        unsigned char before[sizeof(output)]; memcpy(before, &output, sizeof(output));
        CHECK(cna_shader_diagnostics_get_info_ext(handle, mode == 2 ? 1 : 0, &output) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(before, &output, sizeof(output)) == 0);
    }
    char bytes[32]; memset(bytes, 0xa5, sizeof(bytes)); count = 77;
    CHECK(cna_shader_diagnostics_copy_message_ext(handle, 1, bytes, sizeof(bytes), &count) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(count == 77 && filled(bytes, sizeof(bytes), 0xa5));
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_create_ext(&info, SV("label"), SV("message"), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        for (unsigned field = 0; field < 2; ++field) {
            handle = UINT64_MAX;
            CHECK(cna_shader_diagnostics_create_ext(&info, field == 0 ? invalid_text[i] : SV("label"),
                  field == 1 ? invalid_text[i] : SV("message"), &handle) == CNA_RESULT_ENCODING);
            CHECK(handle == CNA_INVALID_HANDLE);
            CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
        }
    }
    for (unsigned field = 0; field < 2; ++field) {
        handle = UINT64_MAX;
        CHECK(cna_shader_diagnostics_create_ext(&info, field == 0 ? (CNA_StringView){NULL, 1} : SV("label"),
              field == 1 ? (CNA_StringView){NULL, 1} : SV("message"), &handle) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(handle == CNA_INVALID_HANDLE);
    }
    CHECK(cna_shader_diagnostics_create_ext(&info, (CNA_StringView){NULL, 0}, SV("message"), &handle) == CNA_RESULT_SUCCESS);
    CHECK(text_copy_contract(diagnostic_label, handle, "", 0) == 0);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_parse_ext((CNA_StringView){NULL, 0}, CNA_SHADER_STAGE_UNKNOWN_EXT,
          (CNA_StringView){NULL, 0}, &handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_parse_ext(SV(unicode_text), CNA_SHADER_STAGE_VERTEX_EXT,
          SV(unicode_text), &handle) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_diagnostics_get_count_ext(handle, &count) == CNA_RESULT_SUCCESS && count == 1);
    output.struct_size = sizeof(output); output.struct_version = 1;
    CHECK(cna_shader_diagnostics_get_info_ext(handle, 0, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.severity == CNA_SHADER_DIAGNOSTIC_SEVERITY_ERROR_EXT &&
          output.stage == CNA_SHADER_STAGE_VERTEX_EXT && output.line == 0 && output.column == 0);
    CHECK(text_copy_contract(diagnostic_message, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(diagnostic_label, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(cna_shader_diagnostics_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        for (unsigned field = 0; field < 2; ++field) {
            handle = UINT64_MAX;
            CHECK(cna_shader_diagnostics_parse_ext(field == 0 ? invalid_text[i] : SV("log"),
                  CNA_SHADER_STAGE_VERTEX_EXT, field == 1 ? invalid_text[i] : SV("label"),
                  &handle) == CNA_RESULT_ENCODING);
            CHECK(handle == CNA_INVALID_HANDLE);
            CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
        }
    }
    info.stage = UINT32_MAX; handle = UINT64_MAX;
    CHECK(cna_shader_diagnostics_create_ext(&info, SV("label"), SV("message"), &handle) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(handle == CNA_INVALID_HANDLE);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static int shader_code_contract(void)
{
    CNA_ShaderCodeHandleEXT handle = CNA_INVALID_HANDLE;
    const CNA_ShaderLanguageEXT language = CNA_SHADER_LANGUAGE_WGSL_EXT;
    const CNA_ShaderStageEXT stage = CNA_SHADER_STAGE_VERTEX_EXT;
    CHECK(cna_shader_code_create_text_ext(language, stage, SV(unicode_text), SV(unicode_text),
          SV(unicode_text), &handle) == CNA_RESULT_SUCCESS);
    CNA_ShaderCodeInfoEXT info;
    memset(&info, 0xa5, sizeof(info)); info.struct_size = sizeof(info); info.struct_version = 1;
    CHECK(cna_shader_code_get_info_ext(handle, &info) == CNA_RESULT_SUCCESS);
    CHECK(info.language == language && info.stage == stage && info.is_text == CNA_TRUE);
    CHECK(info.payload_byte_count == sizeof(unicode_text) - 1U && filled(info.reserved, sizeof(info.reserved), 0));
    CHECK(text_copy_contract(cna_shader_code_copy_entry_point_ext, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(cna_shader_code_copy_source_label_ext, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CHECK(text_copy_contract(cna_shader_code_copy_text_ext, handle, unicode_text, sizeof(unicode_text) - 1U) == 0);
    unsigned char before[sizeof(info)]; info.struct_version = 2; memcpy(before, &info, sizeof(info));
    CHECK(cna_shader_code_get_info_ext(handle, &info) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(memcmp(before, &info, sizeof(info)) == 0);
    uint8_t binary[24]; memset(binary, 0xa5, sizeof(binary)); uint64_t count = 77;
    CHECK(cna_shader_code_copy_bytes_ext(handle, binary, sizeof(binary), &count) == CNA_RESULT_INVALID_STATE);
    CHECK(count == 77 && filled(binary, sizeof(binary), 0xa5));
    CHECK(last_error(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE) == 0);
    CHECK(cna_shader_code_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        for (unsigned field = 0; field < 3; ++field) {
            handle = UINT64_MAX;
            CHECK(cna_shader_code_create_text_ext(language, stage,
                  field == 0 ? invalid_text[i] : SV("entry"), field == 1 ? invalid_text[i] : SV("label"),
                  field == 2 ? invalid_text[i] : SV("source"), &handle) == CNA_RESULT_ENCODING);
            CHECK(handle == CNA_INVALID_HANDLE);
            CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
        }
    }
    for (unsigned field = 0; field < 3; ++field) {
        handle = UINT64_MAX;
        CHECK(cna_shader_code_create_text_ext(language, stage,
              field == 0 ? (CNA_StringView){NULL, 1} : SV("entry"),
              field == 1 ? (CNA_StringView){NULL, 1} : SV("label"),
              field == 2 ? (CNA_StringView){NULL, 1} : SV("source"), &handle) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(handle == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    for (unsigned field = 0; field < 4; ++field) {
        handle = UINT64_MAX;
        CHECK(cna_shader_code_create_text_ext(field == 0 ? UINT32_MAX : language,
              field == 1 ? UINT32_MAX : stage, field == 2 ? (CNA_StringView){NULL, 0} : SV("entry"),
              (CNA_StringView){NULL, 0}, field == 3 ? (CNA_StringView){NULL, 0} : SV("source"),
              &handle) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(handle == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    const uint8_t payload[] = {0x03, 0x02, 0x23, 0x07, 0xff, 0x00, 0x80, 0xfe};
    CHECK(cna_shader_code_create_binary_ext(CNA_SHADER_LANGUAGE_SPIRV_EXT, stage, SV("main"),
          (CNA_StringView){NULL, 0}, payload, sizeof(payload), &handle) == CNA_RESULT_SUCCESS);
    info.struct_version = 1;
    CHECK(cna_shader_code_get_info_ext(handle, &info) == CNA_RESULT_SUCCESS);
    CHECK(info.language == CNA_SHADER_LANGUAGE_SPIRV_EXT && info.is_text == CNA_FALSE && info.payload_byte_count == sizeof(payload));
    for (size_t capacity = 0; capacity < sizeof(payload); ++capacity) {
        memset(binary, 0xa5, sizeof(binary));
        CHECK(cna_shader_code_copy_bytes_ext(handle, binary + 1, capacity, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(count == sizeof(payload) && filled(binary, sizeof(binary), 0xa5));
    }
    CHECK(cna_shader_code_copy_bytes_ext(handle, NULL, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == sizeof(payload));
    CHECK(last_error(CNA_RESULT_BUFFER_TOO_SMALL, CNA_ERROR_CATEGORY_RANGE) == 0);
    memset(binary, 0xa5, sizeof(binary));
    CHECK(cna_shader_code_copy_bytes_ext(handle, binary + 1, sizeof(payload), &count) == CNA_RESULT_SUCCESS);
    CHECK(count == sizeof(payload) && binary[0] == 0xa5 && memcmp(binary + 1, payload, sizeof(payload)) == 0);
    CHECK(filled(binary + 1 + sizeof(payload), sizeof(binary) - 1U - sizeof(payload), 0xa5));
    memset(binary, 0xa5, sizeof(binary));
    CHECK(cna_shader_code_copy_bytes_ext(handle, binary, sizeof(binary), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(binary, sizeof(binary), 0xa5));
    CHECK(cna_shader_code_copy_bytes_ext(handle, NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT);
    memset(binary, 0xa5, sizeof(binary)); count = 77;
    CHECK(cna_shader_code_copy_text_ext(handle, (char*)binary, sizeof(binary), &count) == CNA_RESULT_INVALID_STATE);
    CHECK(count == 77 && filled(binary, sizeof(binary), 0xa5));
    CHECK(last_error(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE) == 0);
    CHECK(cna_shader_code_destroy_ext(handle) == CNA_RESULT_SUCCESS);
    handle = UINT64_MAX;
    CHECK(cna_shader_code_create_binary_ext(CNA_SHADER_LANGUAGE_SPIRV_EXT, stage, SV("main"), SV(""),
          NULL, 0, &handle) == CNA_RESULT_INVALID_ARGUMENT && handle == CNA_INVALID_HANDLE);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    return 0;
}

static int shader_package_contract(void)
{
    CNA_ShaderCodeHandleEXT code = CNA_INVALID_HANDLE;
    CNA_ShaderPackageHandleEXT package = CNA_INVALID_HANDLE;
    CNA_ShaderStageEXT stage = CNA_SHADER_STAGE_VERTEX_EXT;
    CHECK(cna_shader_code_create_text_ext(CNA_SHADER_LANGUAGE_WGSL_EXT, stage, SV("main"), SV(""), SV("source"), &code) == CNA_RESULT_SUCCESS);
    CNA_ShaderBindingRequirementEXT binding = {sizeof(binding), 1, SV(unicode_text), 3,
        CNA_SHADER_BINDING_SAMPLED_TEXTURE2D_EXT, CNA_SHADER_STAGE_VERTEX_EXT};
    CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, &binding, 1, &package) == CNA_RESULT_SUCCESS);
    uint64_t count = UINT64_MAX;
    CHECK(cna_shader_package_get_binding_count_ext(package, &count) == CNA_RESULT_SUCCESS && count == 1);
    CHECK(text_copy_contract(binding_name, package, unicode_text, sizeof(unicode_text) - 1U) == 0);
    CNA_ShaderBindingRequirementEXT output;
    memset(&output, 0xa5, sizeof(output)); output.struct_size = sizeof(output); output.struct_version = 1;
    CHECK(cna_shader_package_get_binding_ext(package, 0, &output) == CNA_RESULT_SUCCESS);
    CHECK(output.name.data == NULL && output.name.byte_length == 0 && output.binding == 3);
    CHECK(output.type == binding.type && output.stage == stage);
    for (unsigned mode = 0; mode < 3; ++mode) {
        memset(&output, 0xa5, sizeof(output));
        output.struct_size = mode == 0 ? sizeof(output) - 1U : sizeof(output);
        output.struct_version = mode == 1 ? 2 : 1;
        unsigned char before[sizeof(output)]; memcpy(before, &output, sizeof(output));
        CHECK(cna_shader_package_get_binding_ext(package, mode == 2 ? 1 : 0, &output) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(before, &output, sizeof(output)) == 0);
    }
    CHECK(cna_shader_package_destroy_ext(package) == CNA_RESULT_SUCCESS);
    for (size_t i = 0; i < ARRAY_COUNT(invalid_text); ++i) {
        binding.name = invalid_text[i]; package = UINT64_MAX;
        CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, &binding, 1, &package) == CNA_RESULT_ENCODING);
        CHECK(package == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_ENCODING, CNA_ERROR_CATEGORY_ENCODING) == 0);
    }
    binding.name = SV("texture");
    for (unsigned field = 0; field < 3; ++field) {
        CNA_ShaderBindingRequirementEXT invalid = binding;
        if (field == 0) invalid.type = UINT32_MAX;
        if (field == 1) invalid.stage = UINT32_MAX;
        if (field == 2) invalid.binding = -1;
        package = UINT64_MAX;
        CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, &invalid, 1, &package) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(package == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, NULL, 0, &package) == CNA_RESULT_SUCCESS);
    CHECK(cna_shader_package_get_binding_count_ext(package, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_shader_package_destroy_ext(package) == CNA_RESULT_SUCCESS);
    package = UINT64_MAX;
    CHECK(cna_shader_package_create_ext(&code, 1, &stage, 1, NULL, 1, &package) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(package == CNA_INVALID_HANDLE);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    for (unsigned field = 0; field < 4; ++field) {
        package = UINT64_MAX;
        CHECK(cna_shader_package_create_ext(field == 0 ? NULL : &code, field == 1 ? 0 : 1,
              field == 2 ? NULL : &stage, field == 3 ? 0 : 1, NULL, 0, &package) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(package == CNA_INVALID_HANDLE);
        CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    }
    CHECK(cna_shader_code_destroy_ext(code) == CNA_RESULT_SUCCESS);
    return 0;
}

static int error_contract(void)
{
    CHECK(cna_point_get_zero(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    CNA_ErrorInfo original = {sizeof(original), 1, 0, 0, 0};
    CHECK(cna_error_get_last_info(&original) == CNA_RESULT_SUCCESS);
    char original_text[2048], copy[2048]; uint64_t count = 0;
    CHECK(cna_error_copy_last_message(original_text, sizeof(original_text), &count) == CNA_RESULT_SUCCESS);
    CHECK(count == original.message_byte_length);
    for (unsigned mode = 0; mode < 2; ++mode) {
        CNA_ErrorInfo invalid;
        memset(&invalid, 0xa5, sizeof(invalid));
        invalid.struct_size = mode == 0 ? sizeof(invalid) - 1U : sizeof(invalid);
        invalid.struct_version = mode == 1 ? 2 : 1;
        unsigned char before[sizeof(invalid)]; memcpy(before, &invalid, sizeof(invalid));
        CHECK(cna_error_get_last_info(&invalid) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(memcmp(before, &invalid, sizeof(invalid)) == 0);
    }
    CHECK(cna_error_get_last_info(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_error_get_last_message_size(NULL) == CNA_RESULT_INVALID_ARGUMENT);
    memset(copy, 0xa5, sizeof(copy));
    CHECK(cna_error_copy_last_message(copy, original.message_byte_length - 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL);
    CHECK(count == original.message_byte_length && filled(copy, sizeof(copy), 0xa5));
    CHECK(cna_error_copy_last_message(NULL, 0, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == original.message_byte_length);
    CHECK(cna_error_copy_last_message(NULL, 1, &count) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(cna_error_copy_last_message(copy, sizeof(copy), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CNA_Point zero = {1, 2};
    CHECK(cna_point_get_zero(&zero) == CNA_RESULT_SUCCESS && zero.x == 0 && zero.y == 0);
    CHECK(cna_error_copy_last_message(copy, original.message_byte_length, &count) == CNA_RESULT_SUCCESS);
    CHECK(count == original.message_byte_length && memcmp(copy, original_text, (size_t)count) == 0);
    CHECK(filled(copy + count, sizeof(copy) - (size_t)count, 0xa5));
    CNA_ErrorInfo after = {sizeof(after), 1, 0, 0, 0};
    CHECK(cna_error_get_last_info(&after) == CNA_RESULT_SUCCESS);
    CHECK(after.result == original.result && after.category == original.category && after.message_byte_length == original.message_byte_length);
    count = UINT64_MAX;
    CHECK(cna_error_get_last_exception_type_size_ext(&count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_error_copy_last_exception_type_ext(NULL, 0, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_error_get_last_exception_param_name_size_ext(&count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(cna_error_copy_last_exception_param_name_ext(NULL, 0, &count) == CNA_RESULT_SUCCESS && count == 0);
    CHECK(last_error(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT) == 0);
    count = 77;
    CHECK(cna_shader_diagnostics_get_count_ext(CNA_INVALID_HANDLE, &count) == CNA_RESULT_INVALID_HANDLE);
    CHECK(count == 77);
    CHECK(last_error(CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_HANDLE) == 0);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2) return CNA_TEST_FAIL(1);
    const struct {const char* name; int (*run)(void);} cases[] = {
        {"cursors", cursor_contract}, {"gamer-text", gamer_text_contract}, {"math", math_contract}, {"curve", curve_contract}, {"declaration", declaration_contract},
        {"point", point_contract}, {"rectangle", rectangle_contract}, {"vector", vector_contract},
        {"diagnostics", diagnostic_contract}, {"code", shader_code_contract},
        {"package", shader_package_contract}, {"error", error_contract}
    };
    for (size_t i = 0; i < ARRAY_COUNT(cases); ++i) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            int result = cases[i].run();
            if (result == 0) printf("OutputContracts/%s: PASS\n", cases[i].name);
            return result;
        }
    }
    return CNA_TEST_FAIL(1);
}
