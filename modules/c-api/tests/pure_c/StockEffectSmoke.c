// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "StockEffectSmoke failure at line %d: %s\n", \
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

static int type_equals(const CNA_EffectHandle effect, const char* const expected)
{
    const uint64_t expected_size = (uint64_t)strlen(expected);
    uint64_t byte_count = 0U;
    char value[64];
    return expected_size <= sizeof(value) &&
           cna_effect_get_type_name_byte_count(effect, &byte_count) == CNA_RESULT_SUCCESS &&
           byte_count == expected_size &&
           cna_effect_copy_type_name(effect, value, sizeof(value), &byte_count) ==
               CNA_RESULT_SUCCESS &&
           byte_count == expected_size && memcmp(value, expected, (size_t)byte_count) == 0;
}

static int exercise_matrices_and_fog(
    const CNA_EffectHandle effect,
    const float marker)
{
    CNA_Matrix identity = {0};
    CNA_Matrix value = {0};
    CNA_Vector3 color = {9, 9, 9};
    CNA_Bool enabled = CNA_TRUE;
    float scalar = -1.0F;
    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_matrices_get_world(effect, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &identity, sizeof(value)) == 0);
    REQUIRE(cna_effect_matrices_get_view(effect, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &identity, sizeof(value)) == 0);
    REQUIRE(cna_effect_matrices_get_projection(effect, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &identity, sizeof(value)) == 0);
    REQUIRE(cna_effect_fog_get_color(effect, &color) == CNA_RESULT_SUCCESS &&
            vector_equals(color, 0, 0, 0));
    REQUIRE(cna_effect_fog_get_enabled(effect, &enabled) == CNA_RESULT_SUCCESS &&
            enabled == CNA_FALSE);
    REQUIRE(cna_effect_fog_get_start(effect, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.0F);
    REQUIRE(cna_effect_fog_get_end(effect, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1.0F);

    identity.m41 = marker;
    REQUIRE(cna_effect_matrices_set_world(effect, identity) == CNA_RESULT_SUCCESS);
    identity.m42 = marker + 1.0F;
    REQUIRE(cna_effect_matrices_set_view(effect, identity) == CNA_RESULT_SUCCESS);
    identity.m43 = marker + 2.0F;
    REQUIRE(cna_effect_matrices_set_projection(effect, identity) == CNA_RESULT_SUCCESS &&
            cna_effect_matrices_get_projection(effect, &value) == CNA_RESULT_SUCCESS &&
            value.m41 == marker && value.m42 == marker + 1.0F &&
            value.m43 == marker + 2.0F);
    REQUIRE(cna_effect_fog_set_color(
                effect, (CNA_Vector3){marker, marker + 1.0F, marker + 2.0F}) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_enabled(effect, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_start(effect, marker + 3.0F) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_end(effect, marker + 4.0F) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_fog_get_color(effect, &color) == CNA_RESULT_SUCCESS &&
            vector_equals(color, marker, marker + 1.0F, marker + 2.0F));
    REQUIRE(cna_effect_fog_get_enabled(effect, &enabled) == CNA_RESULT_SUCCESS &&
            enabled == CNA_TRUE);
    REQUIRE(cna_effect_fog_get_start(effect, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == marker + 3.0F);
    REQUIRE(cna_effect_fog_get_end(effect, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == marker + 4.0F);
    return 1;
}

static int inspect_alpha_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    float value = 0.0F;
    state->result = cna_alpha_test_effect_get_alpha(state->effect, &value);
    return 0;
}

static int validate_alpha_test(
    const CNA_Handle device,
    const CNA_EffectHandle alpha_test)
{
    static const char TypeName[] =
        "Microsoft.Xna.Framework.Graphics.AlphaTestEffect";
    const CNA_Texture2DCreateInfo create_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    const CNA_Color cpu_pixel = {1U, 2U, 3U, 4U};
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle cpu_texture = CNA_INVALID_HANDLE;
    CNA_Handle returned = UINT64_MAX;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_Vector3 vector = {0, 0, 0};
    CNA_Bool boolean = CNA_TRUE;
    CNA_CompareFunction compare = UINT32_MAX;
    int32_t reference = INT32_MAX;
    float scalar = -1.0F;

    REQUIRE(type_equals(alpha_test, TypeName));
    REQUIRE(exercise_matrices_and_fog(alpha_test, 1.0F));
    REQUIRE(cna_alpha_test_effect_get_diffuse_color(alpha_test, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_alpha_test_effect_get_alpha(alpha_test, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1.0F);
    REQUIRE(cna_alpha_test_effect_get_vertex_color_enabled(alpha_test, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_alpha_test_effect_get_alpha_function(alpha_test, &compare) ==
                CNA_RESULT_SUCCESS && compare == CNA_COMPARE_GREATER);
    REQUIRE(cna_alpha_test_effect_get_reference_alpha(alpha_test, &reference) ==
                CNA_RESULT_SUCCESS && reference == 0);
    REQUIRE(cna_alpha_test_effect_get_texture(
                alpha_test, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && returned == CNA_INVALID_HANDLE);
    for (CNA_CompareFunction value = CNA_COMPARE_ALWAYS;
         value <= CNA_COMPARE_NOT_EQUAL;
         ++value) {
        REQUIRE(cna_alpha_test_effect_set_alpha_function(alpha_test, value) ==
                    CNA_RESULT_SUCCESS &&
                cna_alpha_test_effect_get_alpha_function(alpha_test, &compare) ==
                    CNA_RESULT_SUCCESS && compare == value);
    }

    REQUIRE(cna_alpha_test_effect_set_diffuse_color(
                alpha_test, (CNA_Vector3){0.2F, 0.4F, 0.6F}) == CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_set_alpha(alpha_test, -0.5F) == CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_set_vertex_color_enabled(alpha_test, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_set_alpha_function(alpha_test, CNA_COMPARE_EQUAL) ==
                CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_set_reference_alpha(alpha_test, -10) ==
                CNA_RESULT_SUCCESS);
    REQUIRE(cna_alpha_test_effect_get_diffuse_color(alpha_test, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.2F, 0.4F, 0.6F));
    REQUIRE(cna_alpha_test_effect_get_alpha(alpha_test, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == -0.5F);
    REQUIRE(cna_alpha_test_effect_get_vertex_color_enabled(alpha_test, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_alpha_test_effect_get_alpha_function(alpha_test, &compare) ==
                CNA_RESULT_SUCCESS && compare == CNA_COMPARE_EQUAL);
    REQUIRE(cna_alpha_test_effect_get_reference_alpha(alpha_test, &reference) ==
                CNA_RESULT_SUCCESS && reference == -10);
    REQUIRE(cna_alpha_test_effect_set_reference_alpha(alpha_test, 300) ==
                CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_get_reference_alpha(alpha_test, &reference) ==
                CNA_RESULT_SUCCESS && reference == 300);
    REQUIRE(cna_alpha_test_effect_set_alpha_function(alpha_test, UINT32_C(8)) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_alpha_test_effect_set_vertex_color_enabled(alpha_test, UINT32_C(2)) ==
                CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_texture2d_create_cpu_only_rgba8(
                1U, 1U, CNA_SURFACE_FORMAT_COLOR, &cpu_pixel, 1U, &cpu_texture) ==
                CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_set_texture(alpha_test, cpu_texture) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_texture2d_destroy(cpu_texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_create(device, &create_info, &texture) == CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_set_texture(alpha_test, texture) == CNA_RESULT_SUCCESS &&
            cna_alpha_test_effect_get_texture(alpha_test, &boolean, &returned) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE && returned == texture &&
            cna_effect_clone(alpha_test, &clone) == CNA_RESULT_SUCCESS &&
            type_equals(clone, TypeName));
    REQUIRE(cna_alpha_test_effect_get_reference_alpha(clone, &reference) ==
                CNA_RESULT_SUCCESS && reference == 300 &&
            cna_alpha_test_effect_get_texture(clone, &boolean, &returned) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE && returned == texture);
    REQUIRE(cna_alpha_test_effect_set_texture(alpha_test, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE &&
            cna_alpha_test_effect_set_texture(clone, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS &&
            cna_effect_apply(alpha_test) == CNA_RESULT_SUCCESS);

    WrongThreadState wrong_thread = {alpha_test, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_alpha_on_wrong_thread, &wrong_thread) ==
                thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD);
    return 1;
}

static int validate_dual_texture(
    const CNA_Handle device,
    const CNA_EffectHandle dual_texture,
    const CNA_EffectHandle alpha_test)
{
    static const char TypeName[] =
        "Microsoft.Xna.Framework.Graphics.DualTextureEffect";
    const CNA_Texture2DCreateInfo create_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    CNA_Handle texture0 = CNA_INVALID_HANDLE;
    CNA_Handle texture1 = CNA_INVALID_HANDLE;
    CNA_Handle returned = UINT64_MAX;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_Vector3 vector = {0, 0, 0};
    CNA_Bool boolean = CNA_TRUE;
    float scalar = -1.0F;

    REQUIRE(type_equals(dual_texture, TypeName));
    REQUIRE(exercise_matrices_and_fog(dual_texture, 10.0F));
    REQUIRE(cna_dual_texture_effect_get_diffuse_color(dual_texture, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_dual_texture_effect_get_alpha(dual_texture, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 1.0F);
    REQUIRE(cna_dual_texture_effect_get_vertex_color_enabled(dual_texture, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_dual_texture_effect_get_texture(
                dual_texture, 0U, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && returned == CNA_INVALID_HANDLE);
    returned = UINT64_MAX;
    boolean = CNA_TRUE;
    REQUIRE(cna_dual_texture_effect_get_texture(
                dual_texture, 2U, &boolean, &returned) == CNA_RESULT_INVALID_ARGUMENT &&
            boolean == CNA_FALSE && returned == CNA_INVALID_HANDLE);
    REQUIRE(cna_dual_texture_effect_set_texture(
                dual_texture, 2U, CNA_INVALID_HANDLE) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_dual_texture_effect_set_vertex_color_enabled(
                dual_texture, UINT32_C(2)) == CNA_RESULT_INVALID_ARGUMENT);

    REQUIRE(cna_dual_texture_effect_set_diffuse_color(
                dual_texture, (CNA_Vector3){0.3F, 0.5F, 0.7F}) == CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_set_alpha(dual_texture, 1.5F) == CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_set_vertex_color_enabled(dual_texture, CNA_TRUE) ==
                CNA_RESULT_SUCCESS);
    REQUIRE(cna_dual_texture_effect_get_diffuse_color(dual_texture, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.3F, 0.5F, 0.7F));
    REQUIRE(cna_dual_texture_effect_get_alpha(dual_texture, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 1.5F);
    REQUIRE(cna_dual_texture_effect_get_vertex_color_enabled(dual_texture, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_alpha_test_effect_get_alpha(dual_texture, &scalar) ==
                CNA_RESULT_INVALID_HANDLE &&
            cna_dual_texture_effect_get_alpha(alpha_test, &scalar) ==
                CNA_RESULT_INVALID_HANDLE);

    REQUIRE(cna_texture2d_create(device, &create_info, &texture0) == CNA_RESULT_SUCCESS &&
            cna_texture2d_create(device, &create_info, &texture1) == CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_set_texture(dual_texture, 0U, texture0) ==
                CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_set_texture(dual_texture, 1U, texture1) ==
                CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_get_texture(
                dual_texture, 1U, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE && returned == texture1 &&
            cna_effect_clone(dual_texture, &clone) == CNA_RESULT_SUCCESS &&
            type_equals(clone, TypeName));
    REQUIRE(cna_dual_texture_effect_get_diffuse_color(clone, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.3F, 0.5F, 0.7F));
    REQUIRE(cna_dual_texture_effect_set_texture(
                dual_texture, 0U, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_set_texture(
                dual_texture, 1U, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture0) == CNA_RESULT_INVALID_STATE &&
            cna_texture2d_destroy(texture1) == CNA_RESULT_INVALID_STATE &&
            cna_dual_texture_effect_set_texture(
                clone, 0U, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_dual_texture_effect_set_texture(
                clone, 1U, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture0) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture1) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS &&
            cna_effect_apply(dual_texture) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_environment_map(
    const CNA_Handle device,
    const CNA_EffectHandle environment_map,
    CallbackState* const state)
{
    static const char TypeName[] =
        "Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect";
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    const CNA_TextureCubeCreateInfo cube_info = {
        sizeof(CNA_TextureCubeCreateInfo), UINT32_C(1), 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, 0U};
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle cube = CNA_INVALID_HANDLE;
    CNA_Handle returned = UINT64_MAX;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_Vector3 vector = {0, 0, 0};
    CNA_Bool boolean = CNA_FALSE;
    float scalar = -1.0F;

    REQUIRE(type_equals(environment_map, TypeName));
    REQUIRE(exercise_matrices_and_fog(environment_map, 20.0F));
    REQUIRE(cna_environment_map_effect_get_diffuse_color(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_environment_map_effect_get_emissive_color(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_environment_map_effect_get_alpha(environment_map, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 1.0F);
    REQUIRE(cna_environment_map_effect_get_amount(environment_map, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 1.0F);
    REQUIRE(cna_environment_map_effect_get_specular(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_environment_map_effect_get_fresnel_factor(environment_map, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 1.0F);
    REQUIRE(cna_environment_map_effect_get_texture(
                environment_map, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && returned == CNA_INVALID_HANDLE);
    REQUIRE(cna_environment_map_effect_get_environment_map(
                environment_map, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE && returned == CNA_INVALID_HANDLE);

    REQUIRE(cna_effect_lights_get_enabled(environment_map, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_effect_lights_set_enabled(environment_map, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_enabled(environment_map, CNA_FALSE) ==
                CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_effect_lights_get_directional_light(
                environment_map, 0U, &state->retained_light) == CNA_RESULT_SUCCESS &&
            cna_directional_light_get_enabled(state->retained_light, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_effect_lights_enable_default(environment_map) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_ambient_color(environment_map, &vector) ==
                CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.05333332F, 0.09882354F, 0.1819608F));
    REQUIRE(cna_effect_lights_set_ambient_color(
                environment_map, (CNA_Vector3){0.2F, 0.3F, 0.4F}) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_ambient_color(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.2F, 0.3F, 0.4F));

    REQUIRE(cna_environment_map_effect_set_diffuse_color(
                environment_map, (CNA_Vector3){0.1F, 0.2F, 0.3F}) ==
                CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_emissive_color(
                environment_map, (CNA_Vector3){0.4F, 0.5F, 0.6F}) ==
                CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_alpha(environment_map, 0.75F) ==
                CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_amount(environment_map, -2.0F) ==
                CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_specular(
                environment_map, (CNA_Vector3){0.7F, 0.8F, 0.9F}) ==
                CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_fresnel_factor(environment_map, 2.5F) ==
                CNA_RESULT_SUCCESS);
    REQUIRE(cna_environment_map_effect_get_diffuse_color(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.1F, 0.2F, 0.3F));
    REQUIRE(cna_environment_map_effect_get_emissive_color(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.4F, 0.5F, 0.6F));
    REQUIRE(cna_environment_map_effect_get_alpha(environment_map, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 0.75F);
    REQUIRE(cna_environment_map_effect_get_amount(environment_map, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == -2.0F);
    REQUIRE(cna_environment_map_effect_get_specular(environment_map, &vector) ==
                CNA_RESULT_SUCCESS && vector_equals(vector, 0.7F, 0.8F, 0.9F));
    REQUIRE(cna_environment_map_effect_get_fresnel_factor(environment_map, &scalar) ==
                CNA_RESULT_SUCCESS && scalar == 2.5F);

    REQUIRE(cna_texture2d_create(device, &texture_info, &texture) == CNA_RESULT_SUCCESS &&
            cna_texturecube_create(device, &cube_info, &cube) == CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_texture(environment_map, texture) ==
                CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_environment_map(environment_map, cube) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_clone(environment_map, &clone) == CNA_RESULT_SUCCESS &&
            type_equals(clone, TypeName));
    REQUIRE(cna_environment_map_effect_get_texture(
                clone, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE && returned == texture &&
            cna_environment_map_effect_get_environment_map(
                clone, &boolean, &returned) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE && returned == cube);
    REQUIRE(cna_environment_map_effect_set_texture(
                environment_map, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_environment_map(
                environment_map, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE &&
            cna_texturecube_destroy(cube) == CNA_RESULT_INVALID_STATE &&
            cna_environment_map_effect_set_texture(
                clone, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_environment_map_effect_set_environment_map(
                clone, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS &&
            cna_texturecube_destroy(cube) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS &&
            cna_effect_apply(environment_map) == CNA_RESULT_SUCCESS);
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
    CNA_EffectHandle alpha_test = CNA_INVALID_HANDLE;
    CNA_EffectHandle dual_texture = CNA_INVALID_HANDLE;
    CNA_EffectHandle environment_map = CNA_INVALID_HANDLE;
    (void)out_error;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (cna_alpha_test_effect_create(device, &alpha_test) != CNA_RESULT_SUCCESS ||
        cna_dual_texture_effect_create(device, &dual_texture) != CNA_RESULT_SUCCESS ||
        cna_environment_map_effect_create(device, &environment_map) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    if (!validate_alpha_test(device, alpha_test)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 3;
    if (!validate_dual_texture(device, dual_texture, alpha_test)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 4;
    if (!validate_environment_map(device, environment_map, state)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 5;
    if (cna_effect_destroy(alpha_test) != CNA_RESULT_SUCCESS ||
        cna_alpha_test_effect_get_alpha(alpha_test, &(float){0}) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_effect_destroy(dual_texture) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(environment_map) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 6;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API stock effects";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Vector3 value = {0, 0, 0};
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 6 ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_directional_light_set_direction(
            state.retained_light, (CNA_Vector3){3, 2, 1}) != CNA_RESULT_SUCCESS ||
        cna_directional_light_get_direction(state.retained_light, &value) !=
            CNA_RESULT_SUCCESS || !vector_equals(value, 3, 2, 1) ||
        cna_directional_light_destroy(state.retained_light) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "StockEffectSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
