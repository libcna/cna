// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_ModelBoneHandle) == 8U,
               "CNA model-bone handle size changed");
_Static_assert(sizeof(CNA_ModelBoneCollectionHandle) == 8U,
               "CNA model-bone collection handle size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "ModelBoneSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct WrongThreadState {
    CNA_ModelBoneHandle bone;
    CNA_Result result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static int matrix_equals(const CNA_Matrix* const left, const CNA_Matrix* const right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int name_equals(const CNA_ModelBoneHandle bone, const char* const expected)
{
    const uint64_t expected_size = (uint64_t)strlen(expected);
    uint64_t count = UINT64_MAX;
    char bytes[64];
    return expected_size <= sizeof(bytes) &&
           cna_model_bone_get_name_byte_count(bone, &count) == CNA_RESULT_SUCCESS &&
           count == expected_size &&
           cna_model_bone_copy_name(bone, bytes, sizeof(bytes), &count) ==
               CNA_RESULT_SUCCESS &&
           count == expected_size && memcmp(bytes, expected, (size_t)count) == 0;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    int32_t index = 0;
    state->result = cna_model_bone_get_index(state->bone, &index);
    return 0;
}

static int validate_default_and_errors(void)
{
    CNA_ModelBoneHandle bone = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle invalid = UINT64_MAX;
    CNA_ModelBoneCollectionHandle empty = CNA_INVALID_HANDLE;
    CNA_Matrix identity = {0};
    CNA_Matrix transform = {0};
    CNA_Bool boolean = CNA_TRUE;
    int32_t index = -1;
    uint64_t count = UINT64_MAX;
    char byte = 'x';
    const char malformed[] = {(char)0xc3, '('};

    REQUIRE(cna_model_bone_create_default(&bone) == CNA_RESULT_SUCCESS &&
            name_equals(bone, "") &&
            cna_model_bone_get_index(bone, &index) == CNA_RESULT_SUCCESS && index == 0 &&
            cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS &&
            cna_model_bone_get_transform(bone, &transform) == CNA_RESULT_SUCCESS &&
            matrix_equals(&identity, &transform) &&
            cna_model_bone_get_parent(bone, &boolean, &invalid) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && invalid == CNA_INVALID_HANDLE);
    identity.m41 = 4.0F;
    identity.m42 = 5.0F;
    REQUIRE(cna_model_bone_set_transform(bone, identity) == CNA_RESULT_SUCCESS &&
            cna_model_bone_get_transform(bone, &transform) == CNA_RESULT_SUCCESS &&
            matrix_equals(&identity, &transform));
    REQUIRE(cna_model_bone_copy_name(bone, &byte, 0U, &count) == CNA_RESULT_SUCCESS &&
            count == 0U && byte == 'x');

    REQUIRE(cna_model_bone_collection_create(&empty) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_get_count(empty, &count) == CNA_RESULT_SUCCESS &&
            count == 0U &&
            cna_model_bone_collection_get_at(empty, 0U, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE &&
            cna_model_bone_collection_find(
                empty, string_view("missing"), &boolean, &invalid) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && invalid == CNA_INVALID_HANDLE &&
            cna_model_bone_collection_contains(empty, bone, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE &&
            cna_model_bone_get_index(empty, &index) == CNA_RESULT_INVALID_HANDLE);

    invalid = UINT64_MAX;
    REQUIRE(cna_model_bone_create(
                1, (CNA_StringView){malformed, sizeof(malformed)}, &invalid) ==
                CNA_RESULT_ENCODING && invalid == CNA_INVALID_HANDLE &&
            cna_model_bone_create(
                1, (CNA_StringView){"a\0b", 3U}, &invalid) == CNA_RESULT_ENCODING &&
            invalid == CNA_INVALID_HANDLE);

    WrongThreadState wrong_thread = {bone, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD &&
            cna_model_bone_collection_destroy(empty) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(bone) == CNA_RESULT_SUCCESS &&
            cna_model_bone_get_index(bone, &index) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static int validate_hierarchy(void)
{
    static const char LeftName[] = "L\xc3\xa9v\xc3\xa1";
    CNA_ModelBoneHandle root = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle left = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle right = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle outsider = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle parent = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle at = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle found = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle invalid = UINT64_MAX;
    CNA_ModelBoneCollectionHandle children = CNA_INVALID_HANDLE;
    CNA_Bool boolean = CNA_FALSE;
    int32_t index = -1;
    uint64_t count = UINT64_MAX;
    char small[3] = {'q', 'q', 'q'};
    const char sentinel[3] = {'q', 'q', 'q'};

    REQUIRE(cna_model_bone_create(0, string_view("Root"), &root) == CNA_RESULT_SUCCESS &&
            cna_model_bone_create(1, string_view(LeftName), &left) == CNA_RESULT_SUCCESS &&
            cna_model_bone_create(2, string_view("Right"), &right) == CNA_RESULT_SUCCESS &&
            cna_model_bone_create(3, string_view("Other"), &outsider) == CNA_RESULT_SUCCESS &&
            cna_model_bone_get_children(root, &children) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_get_count(children, &count) == CNA_RESULT_SUCCESS &&
            count == 0U);
    REQUIRE(cna_model_bone_add_child(root, left) == CNA_RESULT_SUCCESS &&
            cna_model_bone_add_child(root, right) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_get_count(children, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_model_bone_add_child(root, root) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_model_bone_add_child(left, root) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_model_bone_get_parent(left, &boolean, &parent) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE && name_equals(parent, "Root") &&
            cna_model_bone_collection_get_at(children, 0U, &at) == CNA_RESULT_SUCCESS &&
            name_equals(at, LeftName) &&
            cna_model_bone_collection_find(
                children, string_view(LeftName), &boolean, &found) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE && name_equals(found, LeftName) &&
            cna_model_bone_collection_contains(children, found, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_model_bone_collection_contains(children, outsider, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_model_bone_copy_name(at, small, sizeof(small), &count) ==
                CNA_RESULT_BUFFER_TOO_SMALL &&
            count == sizeof(LeftName) - 1U && memcmp(small, sentinel, sizeof(small)) == 0 &&
            cna_model_bone_collection_get_at(children, 2U, &invalid) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid == CNA_INVALID_HANDLE);

    REQUIRE(cna_model_bone_destroy(left) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(root) == CNA_RESULT_SUCCESS &&
            cna_model_bone_get_index(root, &index) == CNA_RESULT_INVALID_HANDLE &&
            cna_model_bone_collection_get_count(children, &count) == CNA_RESULT_SUCCESS &&
            count == 2U &&
            cna_model_bone_destroy(parent) == CNA_RESULT_SUCCESS &&
            cna_model_bone_collection_destroy(children) == CNA_RESULT_SUCCESS &&
            cna_model_bone_get_parent(at, &boolean, &parent) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && parent == CNA_INVALID_HANDLE);

    REQUIRE(cna_model_bone_destroy(at) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(found) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(right) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(outsider) == CNA_RESULT_SUCCESS);
    return 1;
}

int main(void)
{
    if (!validate_default_and_errors() || !validate_hierarchy()) {
        return 1;
    }
    return 0;
}
