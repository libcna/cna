// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_EffectPassHandle) == 8U &&
                   sizeof(CNA_EffectPassCollectionHandle) == 8U &&
                   sizeof(CNA_EffectTechniqueHandle) == 8U &&
                   sizeof(CNA_EffectTechniqueCollectionHandle) == 8U,
               "CNA effect technique/pass handles changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "EffectTechniqueSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct WrongThreadState {
    CNA_EffectPassHandle pass;
    CNA_EffectTechniqueHandle technique;
    CNA_EffectPassCollectionHandle passes;
    CNA_EffectTechniqueCollectionHandle techniques;
    CNA_Result pass_result;
    CNA_Result technique_result;
    CNA_Result passes_result;
    CNA_Result techniques_result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static CNA_Result create_annotation(
    const char* const name,
    CNA_EffectAnnotationHandle* const out_annotation)
{
    const CNA_EffectAnnotationCreateInfo info = {
        sizeof(CNA_EffectAnnotationCreateInfo), UINT32_C(1),
        string_view(name), {0, 0U}, 1, 1,
        CNA_EFFECT_PARAMETER_CLASS_SCALAR, CNA_EFFECT_PARAMETER_TYPE_SINGLE,
        0, 0U, {0, 0U}};
    return cna_effect_annotation_create(&info, out_annotation);
}

static int use_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint64_t value = 0U;
    state->pass_result = cna_effect_pass_get_name_byte_count(state->pass, &value);
    state->technique_result = cna_effect_technique_get_identity(
        state->technique, &value);
    state->passes_result = cna_effect_pass_collection_get_count(
        state->passes, &value);
    state->techniques_result = cna_effect_technique_collection_get_count(
        state->techniques, &value);
    return 0;
}

static int validate_standalone_and_nested(void)
{
    CNA_EffectTechniqueHandle default_technique = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle named_technique = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_technique_create_default(&default_technique) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_create_named(
                string_view("Lighting"), &named_technique) == CNA_RESULT_SUCCESS);

    uint64_t byte_count = UINT64_MAX;
    uint64_t default_identity = 0U;
    uint64_t named_identity = 0U;
    char text[16];
    char sentinel[16];
    memset(text, 0x4b, sizeof(text));
    memcpy(sentinel, text, sizeof(text));
    REQUIRE(cna_effect_technique_get_name_byte_count(
                default_technique, &byte_count) == CNA_RESULT_SUCCESS && byte_count == 0U);
    REQUIRE(cna_effect_technique_get_name_byte_count(
                named_technique, &byte_count) == CNA_RESULT_SUCCESS && byte_count == 8U);
    REQUIRE(cna_effect_technique_copy_name(
                named_technique, text, 7U, &byte_count) ==
                CNA_RESULT_BUFFER_TOO_SMALL && byte_count == 8U &&
            memcmp(text, sentinel, sizeof(text)) == 0);
    REQUIRE(cna_effect_technique_copy_name(
                named_technique, text, 8U, &byte_count) == CNA_RESULT_SUCCESS &&
            memcmp(text, "Lighting", 8U) == 0);
    REQUIRE(cna_effect_technique_get_identity(
                default_technique, &default_identity) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_get_identity(
                named_technique, &named_identity) == CNA_RESULT_SUCCESS);
    REQUIRE(default_identity != 0U && named_identity != 0U &&
            default_identity != named_identity);

    CNA_EffectPassCollectionHandle default_passes = CNA_INVALID_HANDLE;
    CNA_EffectPassCollectionHandle named_passes = CNA_INVALID_HANDLE;
    uint64_t count = UINT64_MAX;
    REQUIRE(cna_effect_technique_get_passes(default_technique, &default_passes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_get_passes(named_technique, &named_passes) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_get_count(default_passes, &count) ==
                CNA_RESULT_SUCCESS && count == 0U);
    REQUIRE(cna_effect_pass_collection_get_count(named_passes, &count) ==
                CNA_RESULT_SUCCESS && count == 1U);

    CNA_EffectPassHandle p0 = CNA_INVALID_HANDLE;
    CNA_EffectPassHandle added = CNA_INVALID_HANDLE;
    CNA_EffectPassHandle found_added = CNA_INVALID_HANDLE;
    CNA_Bool found = CNA_FALSE;
    REQUIRE(cna_effect_pass_collection_get_at(named_passes, 0U, &p0) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_get_name_byte_count(p0, &byte_count) ==
                CNA_RESULT_SUCCESS && byte_count == 2U);
    REQUIRE(cna_effect_pass_copy_name(p0, text, 2U, &byte_count) ==
                CNA_RESULT_SUCCESS && memcmp(text, "P0", 2U) == 0);
    REQUIRE(cna_effect_pass_apply(p0) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_add_create(
                named_passes, string_view("Shadow"), named_identity, &added) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_find(
                named_passes, string_view("Shadow"), &found, &found_added) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);

    CNA_EffectAnnotationCollectionHandle technique_annotations = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationCollectionHandle pass_annotations = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationHandle annotation = CNA_INVALID_HANDLE;
    CNA_EffectAnnotationHandle copy = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_technique_get_annotations(
                named_technique, &technique_annotations) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_get_annotations(p0, &pass_annotations) == CNA_RESULT_SUCCESS);
    REQUIRE(create_annotation("TechniqueTag", &annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_add(
                technique_annotations, annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_destroy(annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(create_annotation("PassTag", &annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_add(
                pass_annotations, annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_destroy(annotation) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_find(
                pass_annotations, string_view("PassTag"), &found, &copy) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);
    REQUIRE(cna_effect_annotation_destroy(copy) == CNA_RESULT_SUCCESS);

    CNA_EffectPassHandle standalone = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_pass_create(
                string_view("Standalone"), named_identity, &standalone) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_apply(standalone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(standalone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_destroy(technique_annotations) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_annotation_collection_destroy(pass_annotations) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(p0) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(added) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(found_added) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_destroy(default_passes) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_destroy(named_passes) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(default_technique) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(named_technique) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_collections_and_lifetimes(void)
{
    CNA_EffectPassCollectionHandle passes = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueCollectionHandle techniques = CNA_INVALID_HANDLE;
    CNA_EffectPassHandle first_pass = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle first_technique = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_pass_collection_create(&passes) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_collection_create(&techniques) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_add_create(
                passes, string_view("FirstPass"), UINT64_C(11), &first_pass) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_collection_add_named(
                techniques, string_view("FirstTechnique"), &first_technique) ==
            CNA_RESULT_SUCCESS);

    for (int index = 0; index < 48; ++index) {
        CNA_EffectPassHandle pass = CNA_INVALID_HANDLE;
        CNA_EffectTechniqueHandle technique = CNA_INVALID_HANDLE;
        REQUIRE(cna_effect_pass_collection_add_create(
                    passes, string_view("ExtraPass"), (uint64_t)index, &pass) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_effect_technique_collection_add_named(
                    techniques, string_view("ExtraTechnique"), &technique) ==
                CNA_RESULT_SUCCESS);
        REQUIRE(cna_effect_pass_destroy(pass) == CNA_RESULT_SUCCESS);
        REQUIRE(cna_effect_technique_destroy(technique) == CNA_RESULT_SUCCESS);
    }
    CNA_EffectTechniqueHandle default_element = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_technique_collection_add_default(
                techniques, &default_element) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(default_element) == CNA_RESULT_SUCCESS);

    uint64_t count = 0U;
    uint64_t identity = 0U;
    REQUIRE(cna_effect_pass_collection_get_count(passes, &count) ==
                CNA_RESULT_SUCCESS && count == 49U);
    REQUIRE(cna_effect_technique_collection_get_count(techniques, &count) ==
                CNA_RESULT_SUCCESS && count == 50U);
    REQUIRE(cna_effect_pass_apply(first_pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_get_identity(first_technique, &identity) ==
                CNA_RESULT_SUCCESS && identity != 0U);

    CNA_EffectPassHandle indexed_pass = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle indexed_technique = CNA_INVALID_HANDLE;
    CNA_EffectPassHandle found_pass = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle found_technique = CNA_INVALID_HANDLE;
    CNA_Bool found = CNA_FALSE;
    REQUIRE(cna_effect_pass_collection_get_at(passes, 0U, &indexed_pass) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_collection_get_at(
                techniques, 0U, &indexed_technique) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_find(
                passes, string_view("FirstPass"), &found, &found_pass) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);
    REQUIRE(cna_effect_technique_collection_find(
                techniques, string_view("FirstTechnique"), &found, &found_technique) ==
                CNA_RESULT_SUCCESS && found == CNA_TRUE);
    CNA_EffectPassHandle missing_pass = UINT64_MAX;
    CNA_EffectTechniqueHandle missing_technique = UINT64_MAX;
    REQUIRE(cna_effect_pass_collection_find(
                passes, string_view("Missing"), &found, &missing_pass) ==
                CNA_RESULT_SUCCESS && found == CNA_FALSE &&
            missing_pass == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_technique_collection_find(
                techniques, string_view("Missing"), &found, &missing_technique) ==
                CNA_RESULT_SUCCESS && found == CNA_FALSE &&
            missing_technique == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_pass_collection_get_at(passes, 49U, &missing_pass) ==
                CNA_RESULT_INVALID_ARGUMENT && missing_pass == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_technique_collection_get_at(
                techniques, 50U, &missing_technique) == CNA_RESULT_INVALID_ARGUMENT &&
            missing_technique == CNA_INVALID_HANDLE);

    WrongThreadState wrong_thread = {
        first_pass, first_technique, passes, techniques,
        CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS,
        CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, use_on_wrong_thread, &wrong_thread) == thrd_success);
    REQUIRE(thrd_join(thread, 0) == thrd_success);
    REQUIRE(wrong_thread.pass_result == CNA_RESULT_THREAD &&
            wrong_thread.technique_result == CNA_RESULT_THREAD &&
            wrong_thread.passes_result == CNA_RESULT_THREAD &&
            wrong_thread.techniques_result == CNA_RESULT_THREAD);

    REQUIRE(cna_effect_pass_collection_destroy(passes) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_collection_destroy(techniques) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_apply(first_pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_get_identity(first_technique, &identity) ==
                CNA_RESULT_SUCCESS && identity != 0U);
    REQUIRE(cna_effect_pass_destroy(indexed_pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(found_pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(first_pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(indexed_technique) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(found_technique) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(first_technique) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_failures(void)
{
    const unsigned char bad_utf8[] = {0xffU};
    CNA_EffectPassHandle pass = UINT64_MAX;
    CNA_EffectTechniqueHandle technique = UINT64_MAX;
    REQUIRE(cna_effect_pass_create(
                (CNA_StringView){(const char*)bad_utf8, 1U}, 0U, &pass) ==
                CNA_RESULT_ENCODING && pass == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_technique_create_named(
                (CNA_StringView){(const char*)bad_utf8, 1U}, &technique) ==
                CNA_RESULT_ENCODING && technique == CNA_INVALID_HANDLE);

    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    uint64_t value = 0U;
    REQUIRE(cna_curve_create(&wrong_kind) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_get_name_byte_count(wrong_kind, &value) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_effect_technique_get_identity(wrong_kind, &value) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_curve_destroy(wrong_kind) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_effect_pass_create(string_view("Stale"), 0U, &pass) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_apply(pass) == CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_effect_technique_create_default(&technique) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(technique) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_get_identity(technique, &value) ==
            CNA_RESULT_INVALID_HANDLE);
    return 1;
}

int main(void)
{
    if (!validate_standalone_and_nested() ||
        !validate_collections_and_lifetimes() ||
        !validate_failures()) {
        return 1;
    }
    return 0;
}
