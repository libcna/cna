// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_EffectAnnotationHandle) == 8U,
               "CNA_EffectAnnotationHandle size changed");
_Static_assert(sizeof(CNA_EffectAnnotationCollectionHandle) == 8U,
               "CNA_EffectAnnotationCollectionHandle size changed");
_Static_assert(sizeof(CNA_EffectAnnotationCreateInfo) == 88U &&
                   _Alignof(CNA_EffectAnnotationCreateInfo) == 8U,
               "CNA_EffectAnnotationCreateInfo layout changed");
_Static_assert(offsetof(CNA_EffectAnnotationCreateInfo, name) == 8U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, semantic) == 24U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, row_count) == 40U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, data) == 56U &&
                   offsetof(CNA_EffectAnnotationCreateInfo, cached_string) == 72U,
               "CNA_EffectAnnotationCreateInfo field offsets changed");
_Static_assert(sizeof(CNA_EffectAnnotationInfo) == 24U &&
                   _Alignof(CNA_EffectAnnotationInfo) == 4U,
               "CNA_EffectAnnotationInfo layout changed");

typedef struct WrongThreadState {
    CNA_EffectAnnotationHandle annotation;
    CNA_EffectAnnotationCollectionHandle collection;
    CNA_Result annotation_result;
    CNA_Result collection_result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static CNA_Result create_annotation(
    const char* const name,
    const char* const semantic,
    const int32_t rows,
    const int32_t columns,
    const CNA_EffectParameterClass parameter_class,
    const CNA_EffectParameterType parameter_type,
    const float* const data,
    const uint64_t data_count,
    const char* const cached_string,
    CNA_EffectAnnotationHandle* const out_annotation)
{
    const CNA_EffectAnnotationCreateInfo info = {
        sizeof(CNA_EffectAnnotationCreateInfo), UINT32_C(1),
        string_view(name), string_view(semantic), rows, columns,
        parameter_class, parameter_type, data, data_count,
        string_view(cached_string)};
    return cna_effect_annotation_create(&info, out_annotation);
}

static int use_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_EffectAnnotationInfo info = {
        sizeof(CNA_EffectAnnotationInfo), UINT32_C(1), 0, 0, 0U, 0U};
    uint64_t count = 0U;
    state->annotation_result =
        cna_effect_annotation_get_info(state->annotation, &info);
    state->collection_result =
        cna_effect_annotation_collection_get_count(state->collection, &count);
    return 0;
}

static int validate_values_and_strings(
    CNA_EffectAnnotationHandle* const out_matrix,
    CNA_EffectAnnotationHandle* const out_integer)
{
    const float matrix_data[16] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f};
    if (create_annotation(
            "MatrixTag", "WORLD", 4, 4,
            CNA_EFFECT_PARAMETER_CLASS_MATRIX,
            CNA_EFFECT_PARAMETER_TYPE_SINGLE,
            matrix_data, 16U, "cached-value", out_matrix) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_EffectAnnotationInfo info = {
        sizeof(CNA_EffectAnnotationInfo), UINT32_C(1), 0, 0, UINT32_MAX, UINT32_MAX};
    uint64_t byte_count = UINT64_MAX;
    char text[16];
    char sentinel[16];
    memset(text, 0x5a, sizeof(text));
    memcpy(sentinel, text, sizeof(text));
    CNA_Bool boolean_value = CNA_FALSE;
    int32_t integer_value = 0;
    float single_value = 0.0f;
    CNA_Vector2 vector2 = {0.0f, 0.0f};
    CNA_Vector3 vector3 = {0.0f, 0.0f, 0.0f};
    CNA_Vector4 vector4 = {0.0f, 0.0f, 0.0f, 0.0f};
    CNA_Matrix matrix = {0};
    if (cna_effect_annotation_get_info(*out_matrix, &info) != CNA_RESULT_SUCCESS ||
        info.row_count != 4 || info.column_count != 4 ||
        info.parameter_class != CNA_EFFECT_PARAMETER_CLASS_MATRIX ||
        info.parameter_type != CNA_EFFECT_PARAMETER_TYPE_SINGLE ||
        cna_effect_annotation_get_name_byte_count(*out_matrix, &byte_count) !=
            CNA_RESULT_SUCCESS || byte_count != 9U ||
        cna_effect_annotation_copy_name(*out_matrix, text, 8U, &byte_count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || byte_count != 9U ||
        memcmp(text, sentinel, sizeof(text)) != 0 ||
        cna_effect_annotation_copy_name(*out_matrix, text, 9U, &byte_count) !=
            CNA_RESULT_SUCCESS || memcmp(text, "MatrixTag", 9U) != 0 ||
        cna_effect_annotation_get_semantic_byte_count(*out_matrix, &byte_count) !=
            CNA_RESULT_SUCCESS || byte_count != 5U ||
        cna_effect_annotation_copy_semantic(*out_matrix, text, 5U, &byte_count) !=
            CNA_RESULT_SUCCESS || memcmp(text, "WORLD", 5U) != 0 ||
        cna_effect_annotation_get_value_string_byte_count(*out_matrix, &byte_count) !=
            CNA_RESULT_SUCCESS || byte_count != 12U ||
        cna_effect_annotation_copy_value_string(*out_matrix, text, 12U, &byte_count) !=
            CNA_RESULT_SUCCESS || memcmp(text, "cached-value", 12U) != 0 ||
        cna_effect_annotation_get_value_boolean(*out_matrix, &boolean_value) !=
            CNA_RESULT_SUCCESS || boolean_value != CNA_TRUE ||
        cna_effect_annotation_get_value_int32(*out_matrix, &integer_value) !=
            CNA_RESULT_SUCCESS || integer_value != INT32_C(1065353216) ||
        cna_effect_annotation_get_value_single(*out_matrix, &single_value) !=
            CNA_RESULT_SUCCESS || single_value != 1.0f ||
        cna_effect_annotation_get_value_vector2(*out_matrix, &vector2) !=
            CNA_RESULT_SUCCESS || vector2.x != 1.0f || vector2.y != 2.0f ||
        cna_effect_annotation_get_value_vector3(*out_matrix, &vector3) !=
            CNA_RESULT_SUCCESS || vector3.z != 3.0f ||
        cna_effect_annotation_get_value_vector4(*out_matrix, &vector4) !=
            CNA_RESULT_SUCCESS || vector4.w != 4.0f ||
        cna_effect_annotation_get_value_matrix(*out_matrix, &matrix) !=
            CNA_RESULT_SUCCESS ||
        matrix.m11 != 1.0f || matrix.m12 != 5.0f || matrix.m14 != 13.0f ||
        matrix.m21 != 2.0f || matrix.m32 != 7.0f || matrix.m44 != 16.0f) {
        return 0;
    }

    int32_t raw_integer = -42;
    float integer_data = 0.0f;
    memcpy(&integer_data, &raw_integer, sizeof(integer_data));
    if (create_annotation(
            "Answer", "", 1, 1,
            CNA_EFFECT_PARAMETER_CLASS_SCALAR,
            CNA_EFFECT_PARAMETER_TYPE_INT32,
            &integer_data, 1U, "", out_integer) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_get_value_int32(*out_integer, &integer_value) !=
            CNA_RESULT_SUCCESS || integer_value != -42 ||
        cna_effect_annotation_get_value_boolean(*out_integer, &boolean_value) !=
            CNA_RESULT_SUCCESS || boolean_value != CNA_TRUE) {
        return 0;
    }

    CNA_EffectAnnotationHandle empty = CNA_INVALID_HANDLE;
    if (create_annotation(
            "Empty", "", -2, -3,
            CNA_EFFECT_PARAMETER_CLASS_VECTOR,
            CNA_EFFECT_PARAMETER_TYPE_SINGLE,
            0, 0U, "", &empty) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    boolean_value = CNA_TRUE;
    integer_value = 1;
    single_value = 1.0f;
    vector4 = (CNA_Vector4){1.0f, 1.0f, 1.0f, 1.0f};
    matrix = (CNA_Matrix){0};
    if (cna_effect_annotation_get_info(empty, &info) != CNA_RESULT_SUCCESS ||
        info.row_count != -2 || info.column_count != -3 ||
        cna_effect_annotation_get_value_boolean(empty, &boolean_value) !=
            CNA_RESULT_SUCCESS || boolean_value != CNA_FALSE ||
        cna_effect_annotation_get_value_int32(empty, &integer_value) !=
            CNA_RESULT_SUCCESS || integer_value != 0 ||
        cna_effect_annotation_get_value_single(empty, &single_value) !=
            CNA_RESULT_SUCCESS || single_value != 0.0f ||
        cna_effect_annotation_get_value_vector4(empty, &vector4) !=
            CNA_RESULT_SUCCESS || vector4.x != 0.0f || vector4.w != 0.0f ||
        cna_effect_annotation_get_value_matrix(empty, &matrix) != CNA_RESULT_SUCCESS ||
        matrix.m11 != 1.0f || matrix.m22 != 1.0f ||
        matrix.m33 != 1.0f || matrix.m44 != 1.0f ||
        cna_effect_annotation_destroy(empty) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_collection_and_failures(
    const CNA_EffectAnnotationHandle matrix,
    const CNA_EffectAnnotationHandle integer)
{
    CNA_EffectAnnotationCollectionHandle collection = CNA_INVALID_HANDLE;
    uint64_t count = UINT64_MAX;
    if (cna_effect_annotation_collection_create(&collection) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_collection_get_count(collection, &count) !=
            CNA_RESULT_SUCCESS || count != 0U ||
        cna_effect_annotation_collection_add(collection, matrix) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_collection_add(collection, integer) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_collection_get_count(collection, &count) !=
            CNA_RESULT_SUCCESS || count != 2U) {
        return 0;
    }

    WrongThreadState wrong_thread = {
        matrix, collection, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, use_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.annotation_result != CNA_RESULT_THREAD ||
        wrong_thread.collection_result != CNA_RESULT_THREAD) {
        return 0;
    }

    CNA_EffectAnnotationHandle copy = UINT64_MAX;
    CNA_Bool found = CNA_TRUE;
    if (cna_effect_annotation_collection_get_at(collection, 0U, &copy) !=
            CNA_RESULT_SUCCESS || copy == CNA_INVALID_HANDLE) {
        return 0;
    }
    uint64_t copy_name_count = 0U;
    if (cna_effect_annotation_get_name_byte_count(copy, &copy_name_count) !=
            CNA_RESULT_SUCCESS || copy_name_count != 9U ||
        cna_effect_annotation_destroy(copy) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    copy = UINT64_MAX;
    if (cna_effect_annotation_collection_get_at(collection, 2U, &copy) !=
            CNA_RESULT_INVALID_ARGUMENT || copy != CNA_INVALID_HANDLE ||
        cna_effect_annotation_collection_find(
            collection, string_view("Missing"), &found, &copy) != CNA_RESULT_SUCCESS ||
        found != CNA_FALSE || copy != CNA_INVALID_HANDLE ||
        cna_effect_annotation_collection_find(
            collection, string_view("Answer"), &found, &copy) != CNA_RESULT_SUCCESS ||
        found != CNA_TRUE || copy == CNA_INVALID_HANDLE) {
        return 0;
    }
    int32_t value = 0;
    if (cna_effect_annotation_get_value_int32(copy, &value) != CNA_RESULT_SUCCESS ||
        value != -42 ||
        cna_effect_annotation_collection_destroy(collection) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_get_value_int32(copy, &value) != CNA_RESULT_SUCCESS ||
        value != -42 || cna_effect_annotation_destroy(copy) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_collection_get_count(collection, &count) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationInfo info = {
        sizeof(CNA_EffectAnnotationInfo), UINT32_C(1), 0, 0, 0U, 0U};
    if (cna_curve_create(&wrong_kind) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_get_info(wrong_kind, &info) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_destroy(wrong_kind) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    const unsigned char bad_utf8[] = {0xffU};
    CNA_EffectAnnotationCreateInfo invalid = {
        sizeof(CNA_EffectAnnotationCreateInfo), UINT32_C(1),
        {(const char*)bad_utf8, 1U}, {0, 0U}, 1, 1,
        CNA_EFFECT_PARAMETER_CLASS_SCALAR, CNA_EFFECT_PARAMETER_TYPE_SINGLE,
        0, 0U, {0, 0U}};
    CNA_EffectAnnotationHandle invalid_output = UINT64_MAX;
    if (cna_effect_annotation_create(&invalid, &invalid_output) != CNA_RESULT_ENCODING ||
        invalid_output != CNA_INVALID_HANDLE) {
        return 0;
    }
    invalid.name = string_view("Invalid");
    invalid.parameter_type = UINT32_MAX;
    if (cna_effect_annotation_create(&invalid, &invalid_output) !=
            CNA_RESULT_INVALID_ARGUMENT || invalid_output != CNA_INVALID_HANDLE) {
        return 0;
    }
    invalid.parameter_type = CNA_EFFECT_PARAMETER_TYPE_SINGLE;
    invalid.data_count = 1U;
    if (cna_effect_annotation_create(&invalid, &invalid_output) !=
            CNA_RESULT_INVALID_ARGUMENT || invalid_output != CNA_INVALID_HANDLE ||
        cna_effect_annotation_get_value_single(matrix, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

int main(void)
{
    CNA_EffectAnnotationHandle matrix = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationHandle integer = CNA_INVALID_HANDLE;
    if (!validate_values_and_strings(&matrix, &integer) ||
        !validate_collection_and_failures(matrix, integer) ||
        cna_effect_annotation_destroy(matrix) != CNA_RESULT_SUCCESS ||
        cna_effect_annotation_destroy(matrix) != CNA_RESULT_INVALID_HANDLE ||
        cna_effect_annotation_destroy(integer) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(1);
    }
    return 0;
}
