// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_EffectParameterHandle) == 8U &&
                   sizeof(CNA_EffectParameterCollectionHandle) == 8U,
               "CNA effect-parameter handles changed");
_Static_assert(sizeof(CNA_EffectParameterCreateInfo) == 56U &&
                   _Alignof(CNA_EffectParameterCreateInfo) == 8U,
               "CNA_EffectParameterCreateInfo layout changed");
_Static_assert(offsetof(CNA_EffectParameterCreateInfo, name) == 8U &&
                   offsetof(CNA_EffectParameterCreateInfo, semantic) == 24U &&
                   offsetof(CNA_EffectParameterCreateInfo, row_count) == 40U &&
                   offsetof(CNA_EffectParameterCreateInfo, parameter_type) == 52U,
               "CNA_EffectParameterCreateInfo offsets changed");
_Static_assert(sizeof(CNA_EffectParameterInfo) == 24U &&
                   _Alignof(CNA_EffectParameterInfo) == 4U,
               "CNA_EffectParameterInfo layout changed");
_Static_assert(sizeof(CNA_EffectValueType) == sizeof(uint32_t) &&
                   CNA_EFFECT_VALUE_BOOLEAN == UINT32_C(0) &&
                   CNA_EFFECT_VALUE_VECTOR4 == UINT32_C(8),
               "CNA effect-value identities changed");
_Static_assert(sizeof(CNA_EffectTextureType) == sizeof(uint32_t) &&
                   CNA_EFFECT_TEXTURE_BASE == UINT32_C(0) &&
                   CNA_EFFECT_TEXTURE_CUBE == UINT32_C(3),
               "CNA effect-texture identities changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "EffectParameterSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct WrongThreadState {
    CNA_EffectParameterHandle parameter;
    CNA_EffectParameterCollectionHandle collection;
    CNA_Result parameter_result;
    CNA_Result collection_result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static CNA_Result create_parameter(
    const char* const name,
    const char* const semantic,
    const int32_t rows,
    const int32_t columns,
    CNA_EffectParameterHandle* const out_parameter)
{
    const CNA_EffectParameterCreateInfo info = {
        sizeof(CNA_EffectParameterCreateInfo), UINT32_C(1),
        string_view(name), string_view(semantic), rows, columns,
        CNA_EFFECT_PARAMETER_CLASS_VECTOR, CNA_EFFECT_PARAMETER_TYPE_SINGLE};
    return cna_effect_parameter_create(&info, out_parameter);
}

static CNA_EffectParameterCreateInfo parameter_info(
    const char* const name,
    const char* const semantic)
{
    const CNA_EffectParameterCreateInfo info = {
        sizeof(CNA_EffectParameterCreateInfo), UINT32_C(1),
        string_view(name), string_view(semantic), 1, 4,
        CNA_EFFECT_PARAMETER_CLASS_VECTOR, CNA_EFFECT_PARAMETER_TYPE_SINGLE};
    return info;
}

static CNA_Matrix sequence_matrix(const float offset)
{
    const CNA_Matrix result = {
        offset + 1.0f, offset + 2.0f, offset + 3.0f, offset + 4.0f,
        offset + 5.0f, offset + 6.0f, offset + 7.0f, offset + 8.0f,
        offset + 9.0f, offset + 10.0f, offset + 11.0f, offset + 12.0f,
        offset + 13.0f, offset + 14.0f, offset + 15.0f, offset + 16.0f};
    return result;
}

static int matrix_equal(const CNA_Matrix* const left, const CNA_Matrix* const right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int use_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    int32_t value = 0;
    uint64_t count = 0U;
    state->parameter_result = cna_effect_parameter_get_value(
        state->parameter, CNA_EFFECT_VALUE_INT32, &value);
    state->collection_result = cna_effect_parameter_collection_get_count(
        state->collection, &count);
    return 0;
}

static int validate_metadata_defaults_and_scalars(void)
{
    CNA_EffectParameterHandle parameter = CNA_INVALID_HANDLE;
    REQUIRE(create_parameter("Transform", "WORLD", -2, -3, &parameter) ==
            CNA_RESULT_SUCCESS);

    CNA_EffectParameterInfo info = {
        sizeof(CNA_EffectParameterInfo), UINT32_C(1), 0, 0, 0U, 0U};
    uint64_t byte_count = UINT64_MAX;
    char text[16];
    char sentinel[16];
    memset(text, 0x4a, sizeof(text));
    memcpy(sentinel, text, sizeof(text));
    REQUIRE(cna_effect_parameter_get_info(parameter, &info) == CNA_RESULT_SUCCESS);
    REQUIRE(info.row_count == -2 && info.column_count == -3 &&
            info.parameter_class == CNA_EFFECT_PARAMETER_CLASS_VECTOR &&
            info.parameter_type == CNA_EFFECT_PARAMETER_TYPE_SINGLE);
    REQUIRE(cna_effect_parameter_get_name_byte_count(parameter, &byte_count) ==
                CNA_RESULT_SUCCESS && byte_count == 9U);
    REQUIRE(cna_effect_parameter_copy_name(parameter, text, 8U, &byte_count) ==
                CNA_RESULT_BUFFER_TOO_SMALL && byte_count == 9U &&
            memcmp(text, sentinel, sizeof(text)) == 0);
    REQUIRE(cna_effect_parameter_copy_name(parameter, text, 9U, &byte_count) ==
                CNA_RESULT_SUCCESS && memcmp(text, "Transform", 9U) == 0);
    REQUIRE(cna_effect_parameter_get_semantic_byte_count(parameter, &byte_count) ==
                CNA_RESULT_SUCCESS && byte_count == 5U);
    REQUIRE(cna_effect_parameter_copy_semantic(parameter, text, 5U, &byte_count) ==
                CNA_RESULT_SUCCESS && memcmp(text, "WORLD", 5U) == 0);

    CNA_Bool boolean = CNA_TRUE;
    int32_t integer = 1;
    float single = 1.0f;
    CNA_Matrix matrix = {0};
    CNA_Quaternion quaternion = {1.0f, 1.0f, 1.0f, 0.0f};
    CNA_Vector2 vector2 = {1.0f, 1.0f};
    CNA_Vector3 vector3 = {1.0f, 1.0f, 1.0f};
    CNA_Vector4 vector4 = {1.0f, 1.0f, 1.0f, 0.0f};
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_BOOLEAN, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_INT32, &integer) == CNA_RESULT_SUCCESS &&
            integer == 0);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_SINGLE, &single) == CNA_RESULT_SUCCESS &&
            single == 0.0f);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_MATRIX, &matrix) == CNA_RESULT_SUCCESS &&
            matrix.m11 == 1.0f && matrix.m22 == 1.0f &&
            matrix.m33 == 1.0f && matrix.m44 == 1.0f);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_QUATERNION, &quaternion) ==
                CNA_RESULT_SUCCESS && quaternion.x == 0.0f && quaternion.w == 1.0f);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_VECTOR2, &vector2) == CNA_RESULT_SUCCESS &&
            vector2.x == 0.0f && vector2.y == 0.0f);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_VECTOR3, &vector3) == CNA_RESULT_SUCCESS &&
            vector3.z == 0.0f);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_VECTOR4, &vector4) == CNA_RESULT_SUCCESS &&
            vector4.x == 0.0f && vector4.w == 1.0f);

    boolean = CNA_TRUE;
    integer = -42;
    single = 3.5f;
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_BOOLEAN, &boolean) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_INT32, &integer) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_INT32, &integer) == CNA_RESULT_SUCCESS &&
            integer == -42);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_SINGLE, &single) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_SINGLE, &single) == CNA_RESULT_SUCCESS &&
            single == 3.5f);
    boolean = UINT32_C(2);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_BOOLEAN, &boolean) ==
            CNA_RESULT_INVALID_ARGUMENT);

    const CNA_Matrix input_matrix = sequence_matrix(0.0f);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_MATRIX, &input_matrix) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_MATRIX, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equal(&matrix, &input_matrix));
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_MATRIX_TRANSPOSE, &input_matrix) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_MATRIX_TRANSPOSE, &matrix) ==
                CNA_RESULT_SUCCESS && matrix_equal(&matrix, &input_matrix));
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_MATRIX, &matrix) == CNA_RESULT_SUCCESS &&
            matrix.m12 == input_matrix.m21 && matrix.m21 == input_matrix.m12);

    quaternion = (CNA_Quaternion){1.0f, 2.0f, 3.0f, 4.0f};
    vector2 = (CNA_Vector2){5.0f, 6.0f};
    vector3 = (CNA_Vector3){7.0f, 8.0f, 9.0f};
    vector4 = (CNA_Vector4){10.0f, 11.0f, 12.0f, 13.0f};
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_QUATERNION, &quaternion) == CNA_RESULT_SUCCESS);
    quaternion = (CNA_Quaternion){0};
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_QUATERNION, &quaternion) ==
                CNA_RESULT_SUCCESS && quaternion.z == 3.0f && quaternion.w == 4.0f);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_VECTOR2, &vector2) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_VECTOR3, &vector3) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_set_value(
                parameter, CNA_EFFECT_VALUE_VECTOR4, &vector4) == CNA_RESULT_SUCCESS);
    vector4 = (CNA_Vector4){0};
    REQUIRE(cna_effect_parameter_get_value(
                parameter, CNA_EFFECT_VALUE_VECTOR4, &vector4) == CNA_RESULT_SUCCESS &&
            vector4.x == 10.0f && vector4.w == 13.0f);

    REQUIRE(cna_effect_parameter_set_value_string(
                parameter, string_view("hello-effect")) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value_string_byte_count(parameter, &byte_count) ==
                CNA_RESULT_SUCCESS && byte_count == 12U);
    memset(text, 0x3c, sizeof(text));
    memcpy(sentinel, text, sizeof(text));
    REQUIRE(cna_effect_parameter_copy_value_string(
                parameter, text, 11U, &byte_count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            memcmp(text, sentinel, sizeof(text)) == 0);
    REQUIRE(cna_effect_parameter_copy_value_string(
                parameter, text, 12U, &byte_count) == CNA_RESULT_SUCCESS &&
            memcmp(text, "hello-effect", 12U) == 0);

    REQUIRE(cna_effect_parameter_get_value(parameter, UINT32_MAX, &integer) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_effect_parameter_get_value(parameter, CNA_EFFECT_VALUE_INT32, 0) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_effect_parameter_destroy(parameter) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_arrays(void)
{
    CNA_EffectParameterHandle parameter = CNA_INVALID_HANDLE;
    REQUIRE(create_parameter("Array", "VALUES", 1, 4, &parameter) == CNA_RESULT_SUCCESS);

    const CNA_Bool booleans[3] = {CNA_TRUE, CNA_FALSE, CNA_TRUE};
    CNA_Bool boolean_output[3] = {0U, 0U, 0U};
    uint64_t count = UINT64_MAX;
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_BOOLEAN, booleans, 3U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_BOOLEAN, 3U,
                boolean_output, 3U, &count) == CNA_RESULT_SUCCESS && count == 3U &&
            memcmp(booleans, boolean_output, sizeof(booleans)) == 0);
    const CNA_Bool invalid_booleans[2] = {CNA_TRUE, UINT32_C(2)};
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_BOOLEAN, invalid_booleans, 2U) ==
            CNA_RESULT_INVALID_ARGUMENT);

    const int32_t integers[3] = {-7, 8, 99};
    int32_t integer_output[3] = {111, 222, 333};
    const int32_t sentinel[3] = {111, 222, 333};
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_INT32, integers, 3U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_INT32, 3U,
                integer_output, 2U, &count) == CNA_RESULT_BUFFER_TOO_SMALL && count == 3U &&
            memcmp(integer_output, sentinel, sizeof(sentinel)) == 0);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_INT32, 2U,
                integer_output, 2U, &count) == CNA_RESULT_SUCCESS && count == 2U &&
            integer_output[0] == -7 && integer_output[1] == 8);

    const float singles[3] = {1.25f, 2.5f, 5.0f};
    float single_output[3] = {0.0f, 0.0f, 0.0f};
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_SINGLE, singles, 3U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_SINGLE, 3U,
                single_output, 3U, &count) == CNA_RESULT_SUCCESS && count == 3U &&
            memcmp(singles, single_output, sizeof(singles)) == 0);

    const CNA_Matrix matrices[2] = {sequence_matrix(0.0f), sequence_matrix(20.0f)};
    CNA_Matrix matrix_output[2];
    memset(matrix_output, 0, sizeof(matrix_output));
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_MATRIX, matrices, 2U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_MATRIX, 2U,
                matrix_output, 2U, &count) == CNA_RESULT_SUCCESS && count == 2U &&
            memcmp(matrices, matrix_output, sizeof(matrices)) == 0);
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_MATRIX_TRANSPOSE, matrices, 2U) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_MATRIX_TRANSPOSE, 2U,
                matrix_output, 2U, &count) == CNA_RESULT_SUCCESS && count == 2U &&
            memcmp(matrices, matrix_output, sizeof(matrices)) == 0);

    const CNA_Quaternion quaternions[2] = {
        {1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}};
    CNA_Quaternion quaternion_output[2];
    memset(quaternion_output, 0, sizeof(quaternion_output));
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_QUATERNION, quaternions, 2U) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_QUATERNION, 2U,
                quaternion_output, 2U, &count) == CNA_RESULT_SUCCESS && count == 2U &&
            memcmp(quaternions, quaternion_output, sizeof(quaternions)) == 0);

    const CNA_Vector2 vector2_values[2] = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    const CNA_Vector3 vector3_values[2] = {
        {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    const CNA_Vector4 vector4_values[2] = {
        {1.0f, 2.0f, 3.0f, 4.0f}, {5.0f, 6.0f, 7.0f, 8.0f}};
    CNA_Vector2 vector2_output[2];
    CNA_Vector3 vector3_output[2];
    CNA_Vector4 vector4_output[2];
    memset(vector2_output, 0, sizeof(vector2_output));
    memset(vector3_output, 0, sizeof(vector3_output));
    memset(vector4_output, 0, sizeof(vector4_output));
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_VECTOR2, vector2_values, 2U) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_VECTOR2, 2U,
                vector2_output, 2U, &count) == CNA_RESULT_SUCCESS &&
            memcmp(vector2_values, vector2_output, sizeof(vector2_values)) == 0);
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_VECTOR3, vector3_values, 2U) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_VECTOR3, 2U,
                vector3_output, 2U, &count) == CNA_RESULT_SUCCESS &&
            memcmp(vector3_values, vector3_output, sizeof(vector3_values)) == 0);
    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_VECTOR4, vector4_values, 2U) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_VECTOR4, 2U,
                vector4_output, 2U, &count) == CNA_RESULT_SUCCESS &&
            memcmp(vector4_values, vector4_output, sizeof(vector4_values)) == 0);

    REQUIRE(cna_effect_parameter_set_values(
                parameter, CNA_EFFECT_VALUE_VECTOR4, 0, 0U) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_VECTOR4, 2U, 0, 0U, &count) ==
                CNA_RESULT_SUCCESS && count == 0U);
    REQUIRE(cna_effect_parameter_get_values(
                parameter, CNA_EFFECT_VALUE_VECTOR4, UINT64_MAX,
                vector4_output, 2U, &count) == CNA_RESULT_OVERFLOW);
    REQUIRE(cna_effect_parameter_set_values(
                parameter, UINT32_MAX, 0, 0U) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_effect_parameter_destroy(parameter) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_collections_and_views(void)
{
    CNA_EffectParameterCollectionHandle collection = CNA_INVALID_HANDLE;
    CNA_EffectParameterHandle first = CNA_INVALID_HANDLE;
    CNA_EffectParameterHandle second = CNA_INVALID_HANDLE;
    CNA_EffectParameterCreateInfo first_info = parameter_info("First", "POSITION");
    CNA_EffectParameterCreateInfo second_info = parameter_info("Second", "COLOR");
    uint64_t count = UINT64_MAX;
    REQUIRE(cna_effect_parameter_collection_create(&collection) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_get_count(collection, &count) ==
                CNA_RESULT_SUCCESS && count == 0U);
    REQUIRE(cna_effect_parameter_collection_add_create(
                collection, &first_info, &first) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_add_create(
                collection, &second_info, &second) == CNA_RESULT_SUCCESS);

    int32_t stored = 41;
    REQUIRE(cna_effect_parameter_set_value(
                first, CNA_EFFECT_VALUE_INT32, &stored) == CNA_RESULT_SUCCESS);
    for (int index = 0; index < 48; ++index) {
        CNA_EffectParameterHandle added = CNA_INVALID_HANDLE;
        CNA_EffectParameterCreateInfo added_info = parameter_info("Extra", "EXTRA");
        added_info.row_count = index;
        REQUIRE(cna_effect_parameter_collection_add_create(
                    collection, &added_info, &added) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_effect_parameter_destroy(added) == CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_effect_parameter_collection_get_count(collection, &count) ==
                CNA_RESULT_SUCCESS && count == 50U);
    stored = 0;
    REQUIRE(cna_effect_parameter_get_value(
                first, CNA_EFFECT_VALUE_INT32, &stored) == CNA_RESULT_SUCCESS && stored == 41);

    CNA_EffectParameterHandle indexed = CNA_INVALID_HANDLE;
    CNA_EffectParameterHandle named = CNA_INVALID_HANDLE;
    CNA_EffectParameterHandle semantic = CNA_INVALID_HANDLE;
    CNA_Bool found = CNA_FALSE;
    REQUIRE(cna_effect_parameter_collection_get_at(
                collection, 0U, &indexed) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_find_name(
                collection, string_view("First"), &found, &named) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);
    REQUIRE(cna_effect_parameter_collection_find_semantic(
                collection, string_view("POSITION"), &found, &semantic) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);
    stored = 77;
    REQUIRE(cna_effect_parameter_set_value(
                indexed, CNA_EFFECT_VALUE_INT32, &stored) == CNA_RESULT_SUCCESS);
    stored = 0;
    REQUIRE(cna_effect_parameter_get_value(
                named, CNA_EFFECT_VALUE_INT32, &stored) == CNA_RESULT_SUCCESS && stored == 77);
    CNA_EffectParameterHandle missing = UINT64_MAX;
    found = CNA_TRUE;
    REQUIRE(cna_effect_parameter_collection_find_name(
                collection, string_view("Missing"), &found, &missing) ==
                CNA_RESULT_SUCCESS && found == CNA_FALSE &&
            missing == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_parameter_collection_get_at(
                collection, 50U, &missing) == CNA_RESULT_INVALID_ARGUMENT &&
            missing == CNA_INVALID_HANDLE);

    CNA_EffectParameterCollectionHandle elements = CNA_INVALID_HANDLE;
    CNA_EffectParameterCollectionHandle members = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_parameter_get_elements(first, &elements) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_structure_members(first, &members) ==
            CNA_RESULT_SUCCESS);
    CNA_EffectParameterHandle element = CNA_INVALID_HANDLE;
    CNA_EffectParameterHandle member = CNA_INVALID_HANDLE;
    CNA_EffectParameterCreateInfo child_info = parameter_info("Child", "CHILD");
    REQUIRE(cna_effect_parameter_collection_add_create(
                elements, &child_info, &element) == CNA_RESULT_SUCCESS);
    child_info.name = string_view("Member");
    REQUIRE(cna_effect_parameter_collection_add_create(
                members, &child_info, &member) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_get_count(elements, &count) ==
                CNA_RESULT_SUCCESS && count == 1U);
    REQUIRE(cna_effect_parameter_collection_get_count(members, &count) ==
                CNA_RESULT_SUCCESS && count == 1U);

    CNA_EffectAnnotationCollectionHandle annotations = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationHandle annotation = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationHandle annotation_copy = CNA_INVALID_HANDLE;
    const CNA_EffectAnnotationCreateInfo annotation_info = {
        sizeof(CNA_EffectAnnotationCreateInfo), UINT32_C(1),
        {"Tag", UINT64_C(3)}, {"META", UINT64_C(4)}, 1, 1,
        CNA_EFFECT_PARAMETER_CLASS_SCALAR, CNA_EFFECT_PARAMETER_TYPE_SINGLE,
        0, 0U, {0, 0U}};
    REQUIRE(cna_effect_parameter_get_annotations(first, &annotations) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_create(&annotation_info, &annotation) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_add(annotations, annotation) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_destroy(annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_find(
                annotations, string_view("Tag"), &found, &annotation_copy) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);
    REQUIRE(cna_effect_annotation_destroy(annotation_copy) == CNA_RESULT_SUCCESS);

    WrongThreadState wrong_thread = {
        first, collection, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, use_on_wrong_thread, &wrong_thread) == thrd_success);
    REQUIRE(thrd_join(thread, 0) == thrd_success);
    REQUIRE(wrong_thread.parameter_result == CNA_RESULT_THREAD &&
            wrong_thread.collection_result == CNA_RESULT_THREAD);

    REQUIRE(cna_effect_parameter_collection_destroy(collection) == CNA_RESULT_SUCCESS);
    stored = 88;
    REQUIRE(cna_effect_parameter_set_value(
                first, CNA_EFFECT_VALUE_INT32, &stored) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_get_count(elements, &count) ==
                CNA_RESULT_SUCCESS && count == 1U);
    REQUIRE(cna_effect_annotation_collection_get_count(annotations, &count) ==
                CNA_RESULT_SUCCESS && count == 1U);

    REQUIRE(cna_effect_parameter_collection_destroy(elements) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_destroy(members) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_destroy(annotations) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(element) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(member) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(indexed) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(named) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(semantic) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(second) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(first) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(first) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static int validate_textures(void)
{
    const CNA_Color pixel = {10U, 20U, 30U, 255U};
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_EffectParameterHandle parameter = CNA_INVALID_HANDLE;
    REQUIRE(create_parameter("Diffuse", "TEXTURE", 1, 1, &parameter) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_create_cpu_only_rgba8(
                1U, 1U, CNA_SURFACE_FORMAT_COLOR, &pixel, 1U, &texture) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_BASE, texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_2D, texture) == CNA_RESULT_SUCCESS);
    CNA_Handle returned = UINT64_MAX;
    REQUIRE(cna_effect_parameter_get_value_texture(
                parameter, CNA_EFFECT_TEXTURE_2D, &returned) == CNA_RESULT_SUCCESS &&
            returned == texture);
    REQUIRE(cna_effect_parameter_get_value_texture(
                parameter, CNA_EFFECT_TEXTURE_BASE, &returned) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_graphics_resource_dispose(texture) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_BASE, CNA_INVALID_HANDLE) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_2D, CNA_INVALID_HANDLE) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value_texture(
                parameter, CNA_EFFECT_TEXTURE_2D, &returned) == CNA_RESULT_SUCCESS &&
            returned == CNA_INVALID_HANDLE);

    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_3D, CNA_INVALID_HANDLE) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value_texture(
                parameter, CNA_EFFECT_TEXTURE_3D, &returned) == CNA_RESULT_SUCCESS &&
            returned == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_CUBE, CNA_INVALID_HANDLE) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_value_texture(
                parameter, CNA_EFFECT_TEXTURE_CUBE, &returned) == CNA_RESULT_SUCCESS &&
            returned == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, UINT32_MAX, CNA_INVALID_HANDLE) == CNA_RESULT_INVALID_ARGUMENT);

    CNA_Handle retained = CNA_INVALID_HANDLE;
    REQUIRE(cna_texture2d_create_cpu_only_rgba8(
                1U, 1U, CNA_SURFACE_FORMAT_COLOR, &pixel, 1U, &retained) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_set_value_texture(
                parameter, CNA_EFFECT_TEXTURE_2D, retained) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_destroy(parameter) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_destroy(retained) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_failures(void)
{
    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_EffectParameterInfo output_info = {
        sizeof(CNA_EffectParameterInfo), UINT32_C(1), 0, 0, 0U, 0U};
    REQUIRE(cna_curve_create(&wrong_kind) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_get_info(wrong_kind, &output_info) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_curve_destroy(wrong_kind) == CNA_RESULT_SUCCESS);

    const unsigned char bad_utf8[] = {0xffU};
    CNA_EffectParameterCreateInfo invalid = parameter_info("Invalid", "BAD");
    invalid.name = (CNA_StringView){(const char*)bad_utf8, 1U};
    CNA_EffectParameterHandle output = UINT64_MAX;
    REQUIRE(cna_effect_parameter_create(&invalid, &output) == CNA_RESULT_ENCODING &&
            output == CNA_INVALID_HANDLE);
    invalid.name = string_view("Invalid");
    invalid.parameter_class = UINT32_MAX;
    output = UINT64_MAX;
    REQUIRE(cna_effect_parameter_create(&invalid, &output) ==
                CNA_RESULT_INVALID_ARGUMENT && output == CNA_INVALID_HANDLE);
    invalid.parameter_class = CNA_EFFECT_PARAMETER_CLASS_SCALAR;
    invalid.struct_size = 0U;
    output = UINT64_MAX;
    REQUIRE(cna_effect_parameter_create(&invalid, &output) ==
                CNA_RESULT_INVALID_ARGUMENT && output == CNA_INVALID_HANDLE);

    CNA_EffectParameterHandle parameter = CNA_INVALID_HANDLE;
    REQUIRE(create_parameter("Valid", "OK", 1, 1, &parameter) == CNA_RESULT_SUCCESS);
    output_info.struct_version = 2U;
    REQUIRE(cna_effect_parameter_get_info(parameter, &output_info) ==
            CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_effect_parameter_destroy(parameter) == CNA_RESULT_SUCCESS);
    output_info.struct_version = UINT32_C(1);
    REQUIRE(cna_effect_parameter_get_info(parameter, &output_info) ==
            CNA_RESULT_INVALID_HANDLE);
    return 1;
}

int main(void)
{
    if (!validate_metadata_defaults_and_scalars() ||
        !validate_arrays() ||
        !validate_collections_and_views() ||
        !validate_textures() ||
        !validate_failures()) {
        return 1;
    }
    return 0;
}
