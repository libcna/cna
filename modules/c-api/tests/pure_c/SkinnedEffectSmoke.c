// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(CNA_SKINNED_EFFECT_MAX_BONES == UINT32_C(72),
               "SkinnedEffect maximum bone count changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "SkinnedEffectSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_DirectionalLightHandle retained_light;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_EffectHandle effect;
    CNA_Result result;
} WrongThreadState;

static int vector_equals(
    const CNA_Vector3 value,
    const float x,
    const float y,
    const float z)
{
    return value.x == x && value.y == y && value.z == z;
}

static int matrix_equals(const CNA_Matrix* const left, const CNA_Matrix* const right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    int32_t value = 0;
    state->result = cna_skinned_effect_get_weights_per_vertex(state->effect, &value);
    return 0;
}

static int validate_defaults(const CNA_EffectHandle skinned)
{
    static const char TypeName[] =
        "Microsoft.Xna.Framework.Graphics.SkinnedEffect";
    CNA_Matrix identity = {0};
    CNA_Matrix matrix = {0};
    CNA_Matrix bones[CNA_SKINNED_EFFECT_MAX_BONES];
    CNA_Vector3 vector = {9, 9, 9};
    CNA_Bool boolean = CNA_TRUE;
    CNA_Handle texture = UINT64_MAX;
    int32_t weights = 0;
    float scalar = -1.0F;
    uint64_t count = 0U;
    char type_name[sizeof(TypeName) - 1U];

    REQUIRE(cna_effect_get_type_name_byte_count(skinned, &count) == CNA_RESULT_SUCCESS &&
            count == sizeof(TypeName) - 1U &&
            cna_effect_copy_type_name(
                skinned, type_name, sizeof(type_name), &count) == CNA_RESULT_SUCCESS &&
            memcmp(type_name, TypeName, sizeof(type_name)) == 0);
    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS &&
            cna_effect_matrices_get_world(skinned, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equals(&matrix, &identity) &&
            cna_effect_matrices_get_view(skinned, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equals(&matrix, &identity) &&
            cna_effect_matrices_get_projection(skinned, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equals(&matrix, &identity));
    REQUIRE(cna_skinned_effect_get_diffuse_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_skinned_effect_get_emissive_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_skinned_effect_get_specular_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_skinned_effect_get_specular_power(skinned, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 16.0F);
    REQUIRE(cna_skinned_effect_get_alpha(skinned, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1.0F);
    REQUIRE(cna_skinned_effect_get_prefer_per_pixel_lighting(skinned, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_skinned_effect_get_vertex_color_enabled(skinned, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_skinned_effect_get_weights_per_vertex(skinned, &weights) ==
                CNA_RESULT_SUCCESS && weights == 4);
    REQUIRE(cna_skinned_effect_get_texture(skinned, &boolean, &texture) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE &&
            texture == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_lights_get_ambient_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_effect_lights_get_enabled(skinned, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE);
    REQUIRE(cna_effect_fog_get_color(skinned, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_effect_fog_get_enabled(skinned, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE);
    REQUIRE(cna_effect_fog_get_start(skinned, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.0F);
    REQUIRE(cna_effect_fog_get_end(skinned, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1.0F);

    REQUIRE(cna_skinned_effect_copy_bone_transforms(
                skinned, CNA_SKINNED_EFFECT_MAX_BONES, bones,
                CNA_SKINNED_EFFECT_MAX_BONES, &count) == CNA_RESULT_SUCCESS &&
            count == CNA_SKINNED_EFFECT_MAX_BONES);
    for (uint32_t index = 0U; index < CNA_SKINNED_EFFECT_MAX_BONES; ++index) {
        REQUIRE(matrix_equals(&bones[index], &identity));
    }
    return 1;
}

static int validate_properties(
    const CNA_Handle device,
    const CNA_EffectHandle skinned,
    CallbackState* const state)
{
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    const CNA_Color cpu_pixel = {1U, 2U, 3U, 4U};
    CNA_Matrix identity = {0};
    CNA_Matrix bones[2];
    CNA_Matrix copied[2];
    CNA_Matrix untouched = {0};
    CNA_Vector3 vector = {0, 0, 0};
    CNA_Bool boolean = CNA_FALSE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle cpu_texture = CNA_INVALID_HANDLE;
    CNA_Handle returned = CNA_INVALID_HANDLE;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_DirectionalLightHandle light0 = CNA_INVALID_HANDLE;
    CNA_DirectionalLightHandle light2 = CNA_INVALID_HANDLE;
    int32_t weights = 0;
    float scalar = 0.0F;
    uint64_t count = 0U;

    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS);
    bones[0] = identity;
    bones[1] = identity;
    bones[0].m41 = 1.0F;
    bones[1].m42 = 2.0F;
    untouched = bones[1];

    REQUIRE(cna_skinned_effect_set_diffuse_color(
                skinned, (CNA_Vector3){0.1F, 0.2F, 0.3F}) == CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_emissive_color(
                skinned, (CNA_Vector3){0.4F, 0.5F, 0.6F}) == CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_specular_color(
                skinned, (CNA_Vector3){0.7F, 0.8F, 0.9F}) == CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_specular_power(skinned, 64.0F) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_alpha(skinned, 0.75F) == CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_prefer_per_pixel_lighting(skinned, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_vertex_color_enabled(skinned, CNA_TRUE) ==
                CNA_RESULT_SUCCESS);
    REQUIRE(cna_skinned_effect_get_diffuse_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.1F, 0.2F, 0.3F));
    REQUIRE(cna_skinned_effect_get_emissive_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.4F, 0.5F, 0.6F));
    REQUIRE(cna_skinned_effect_get_specular_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.7F, 0.8F, 0.9F));
    REQUIRE(cna_skinned_effect_get_specular_power(skinned, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 64.0F);
    REQUIRE(cna_skinned_effect_get_alpha(skinned, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.75F);
    REQUIRE(cna_skinned_effect_get_prefer_per_pixel_lighting(skinned, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_skinned_effect_get_vertex_color_enabled(skinned, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);

    REQUIRE(cna_effect_lights_set_ambient_color(
                skinned, (CNA_Vector3){0.2F, 0.3F, 0.4F}) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_enabled(skinned, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_enabled(skinned, CNA_FALSE) ==
                CNA_RESULT_INVALID_STATE &&
            cna_effect_lights_enable_default(skinned) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_ambient_color(skinned, &vector) ==
                CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.05333332F, 0.09882354F, 0.1819608F));
    REQUIRE(cna_effect_lights_get_directional_light(
                skinned, 0U, &light0) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_directional_light(
                skinned, 1U, &state->retained_light) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_directional_light(
                skinned, 2U, &light2) == CNA_RESULT_SUCCESS &&
            cna_directional_light_get_enabled(light0, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_directional_light_get_enabled(state->retained_light, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_directional_light_get_enabled(light2, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_directional_light_destroy(light0) == CNA_RESULT_SUCCESS &&
            cna_directional_light_destroy(light2) == CNA_RESULT_SUCCESS);

    identity.m41 = 3.0F;
    REQUIRE(cna_effect_matrices_set_world(skinned, identity) == CNA_RESULT_SUCCESS);
    identity.m42 = 4.0F;
    REQUIRE(cna_effect_matrices_set_view(skinned, identity) == CNA_RESULT_SUCCESS);
    identity.m43 = 5.0F;
    REQUIRE(cna_effect_matrices_set_projection(skinned, identity) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_color(
                skinned, (CNA_Vector3){0.5F, 0.6F, 0.7F}) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_enabled(skinned, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_start(skinned, 10.0F) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_end(skinned, 20.0F) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_skinned_effect_set_weights_per_vertex(skinned, 1) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_weights_per_vertex(skinned, 2) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_weights_per_vertex(skinned, 4) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_get_weights_per_vertex(skinned, &weights) ==
                CNA_RESULT_SUCCESS && weights == 4 &&
            cna_skinned_effect_set_weights_per_vertex(skinned, 0) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_effect_set_weights_per_vertex(skinned, 3) ==
                CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_skinned_effect_set_prefer_per_pixel_lighting(
                skinned, UINT32_C(2)) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_effect_set_vertex_color_enabled(skinned, UINT32_C(2)) ==
                CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_skinned_effect_set_bone_transforms(skinned, bones, 2U) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_copy_bone_transforms(
                skinned, 2U, copied, 2U, &count) == CNA_RESULT_SUCCESS &&
            count == 2U && matrix_equals(&copied[0], &bones[0]) &&
            matrix_equals(&copied[1], &bones[1]));
    copied[0] = untouched;
    REQUIRE(cna_skinned_effect_copy_bone_transforms(
                skinned, 2U, copied, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 2U && matrix_equals(&copied[0], &untouched));
    REQUIRE(cna_skinned_effect_set_bone_transforms(skinned, 0, 0U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_effect_set_bone_transforms(
                skinned, bones, CNA_SKINNED_EFFECT_MAX_BONES + 1U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_effect_copy_bone_transforms(
                skinned, 0U, copied, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_effect_copy_bone_transforms(
                skinned, CNA_SKINNED_EFFECT_MAX_BONES + 1U,
                copied, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_texture2d_create_cpu_only_rgba8(
                1U, 1U, CNA_SURFACE_FORMAT_COLOR, &cpu_pixel, 1U, &cpu_texture) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_texture(skinned, cpu_texture) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_texture2d_destroy(cpu_texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_create(device, &texture_info, &texture) == CNA_RESULT_SUCCESS &&
            cna_skinned_effect_set_texture(skinned, texture) == CNA_RESULT_SUCCESS &&
            cna_skinned_effect_get_texture(skinned, &boolean, &returned) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE && returned == texture &&
            cna_effect_clone(skinned, &clone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_skinned_effect_get_specular_color(clone, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.7F, 0.8F, 0.9F) &&
            cna_skinned_effect_copy_bone_transforms(
                clone, 2U, copied, 2U, &count) == CNA_RESULT_SUCCESS &&
            matrix_equals(&copied[0], &bones[0]) && matrix_equals(&copied[1], &bones[1]) &&
            cna_skinned_effect_get_texture(clone, &boolean, &returned) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE && returned == texture);
    REQUIRE(cna_skinned_effect_set_texture(skinned, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE &&
            cna_skinned_effect_set_texture(clone, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS &&
            cna_effect_apply(skinned) == CNA_RESULT_SUCCESS);

    WrongThreadState wrong_thread = {skinned, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) ==
                thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD);
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CallbackState* const state = (CallbackState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_EffectHandle skinned = CNA_INVALID_HANDLE;
    CNA_EffectHandle basic = CNA_INVALID_HANDLE;
    CNA_Vector3 vector = {0, 0, 0};
    (void)out_error;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (cna_skinned_effect_create(device, &skinned) != CNA_RESULT_SUCCESS ||
        cna_basic_effect_create(device, &basic) != CNA_RESULT_SUCCESS ||
        !validate_defaults(skinned)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    if (cna_skinned_effect_get_diffuse_color(basic, &vector) !=
            CNA_RESULT_INVALID_HANDLE ||
        !validate_properties(device, skinned, state)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 3;
    if (cna_effect_destroy(basic) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(skinned) != CNA_RESULT_SUCCESS ||
        cna_skinned_effect_get_diffuse_color(skinned, &vector) !=
            CNA_RESULT_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 4;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API SkinnedEffect";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Vector3 value = {0, 0, 0};
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 4 ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_directional_light_set_diffuse_color(
            state.retained_light, (CNA_Vector3){1, 2, 3}) != CNA_RESULT_SUCCESS ||
        cna_directional_light_get_diffuse_color(state.retained_light, &value) !=
            CNA_RESULT_SUCCESS || !vector_equals(value, 1, 2, 3) ||
        cna_directional_light_destroy(state.retained_light) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "SkinnedEffectSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
