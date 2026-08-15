// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_ColorMatrix4x4) == 64U,
               "CNA color-matrix layout changed");
_Static_assert(_Alignof(CNA_ColorMatrix4x4) == 4U,
               "CNA color-matrix alignment changed");
_Static_assert(CNA_PBR_TEXTURE_BASE_COLOR == UINT32_C(0) &&
                   CNA_PBR_TEXTURE_NORMAL == UINT32_C(1) &&
                   CNA_PBR_TEXTURE_METALLIC_ROUGHNESS == UINT32_C(2) &&
                   CNA_PBR_TEXTURE_EMISSIVE == UINT32_C(3) &&
                   CNA_PBR_TEXTURE_OCCLUSION == UINT32_C(4),
               "CNA PBR texture identities changed");
_Static_assert(CNA_SKINNED_PBR_EFFECT_MAX_BONES == UINT32_C(72),
               "SkinnedPbrEffect maximum bone count changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "PbrEffectSmoke failure at line %d: %s\n", \
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

static int vector3_equals(
    const CNA_Vector3 value,
    const float x,
    const float y,
    const float z)
{
    return value.x == x && value.y == y && value.z == z;
}

static int vector4_equals(
    const CNA_Vector4 value,
    const float x,
    const float y,
    const float z,
    const float w)
{
    return value.x == x && value.y == y && value.z == z && value.w == w;
}

static int matrix_equals(const CNA_Matrix* const left, const CNA_Matrix* const right)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

static int type_equals(const CNA_EffectHandle effect, const char* const expected)
{
    const uint64_t expected_size = (uint64_t)strlen(expected);
    uint64_t byte_count = 0U;
    char value[72];
    return expected_size <= sizeof(value) &&
           cna_effect_get_type_name_byte_count(effect, &byte_count) == CNA_RESULT_SUCCESS &&
           byte_count == expected_size &&
           cna_effect_copy_type_name(effect, value, sizeof(value), &byte_count) ==
               CNA_RESULT_SUCCESS &&
           memcmp(value, expected, (size_t)byte_count) == 0;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    float value = 0.0F;
    state->result = cna_pbr_effect_get_alpha(state->effect, &value);
    return 0;
}

static int validate_color_matrix(const CNA_EffectHandle effect)
{
    const CNA_ColorMatrix4x4 identity = {{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    const CNA_ColorMatrix4x4 custom = {{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}};
    const CNA_ColorMatrix4x4 grayscale = {{
        0.2126F, 0.7152F, 0.0722F, 0,
        0.2126F, 0.7152F, 0.0722F, 0,
        0.2126F, 0.7152F, 0.0722F, 0,
        0, 0, 0, 1}};
    CNA_ColorMatrix4x4 value = {{0}};
    CNA_ColorMatrix4x4 invalid = custom;
    CNA_Vector4 offset = {9, 9, 9, 9};
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;

    REQUIRE(type_equals(
        effect, "Microsoft.Xna.Framework.Graphics.ColorMatrixEffect"));
    REQUIRE(cna_color_matrix_effect_get_matrix(effect, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &identity, sizeof(value)) == 0 &&
            cna_color_matrix_effect_get_offset(effect, &offset) == CNA_RESULT_SUCCESS &&
            vector4_equals(offset, 0, 0, 0, 0));
    REQUIRE(cna_color_matrix_effect_set_matrix(effect, custom) == CNA_RESULT_SUCCESS &&
            cna_color_matrix_effect_set_offset(
                effect, (CNA_Vector4){1, 2, 3, 4}) == CNA_RESULT_SUCCESS &&
            cna_effect_clone(effect, &clone) == CNA_RESULT_SUCCESS &&
            cna_color_matrix_effect_get_matrix(clone, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &custom, sizeof(value)) == 0 &&
            cna_color_matrix_effect_get_offset(clone, &offset) == CNA_RESULT_SUCCESS &&
            vector4_equals(offset, 1, 2, 3, 4));
    invalid.values[7] = NAN;
    REQUIRE(cna_color_matrix_effect_set_matrix(effect, invalid) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_color_matrix_effect_set_offset(
                effect, (CNA_Vector4){0, INFINITY, 0, 0}) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_color_matrix_effect_set_grayscale(effect) == CNA_RESULT_SUCCESS &&
            cna_color_matrix_effect_get_matrix(effect, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &grayscale, sizeof(value)) == 0 &&
            cna_color_matrix_effect_get_offset(effect, &offset) == CNA_RESULT_SUCCESS &&
            vector4_equals(offset, 0, 0, 0, 0) &&
            cna_color_matrix_effect_reset(effect) == CNA_RESULT_SUCCESS &&
            cna_color_matrix_effect_get_matrix(effect, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &identity, sizeof(value)) == 0 &&
            cna_effect_apply(effect) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
    return 1;
}

static int exercise_common_pbr(
    const CNA_Handle device,
    const CNA_EffectHandle effect,
    const char* const expected_type,
    CallbackState* const state,
    const int retain_light)
{
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    const CNA_Color pixel = {1U, 2U, 3U, 4U};
    CNA_Matrix identity = {0};
    CNA_Matrix matrix = {0};
    CNA_Vector3 vector = {9, 9, 9};
    CNA_Bool boolean = CNA_FALSE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle cpu_texture = CNA_INVALID_HANDLE;
    CNA_Handle returned = UINT64_MAX;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_DirectionalLightHandle lights[3] = {
        CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE};
    float scalar = -1.0F;

    REQUIRE(type_equals(effect, expected_type));
    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS &&
            cna_effect_matrices_get_world(effect, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equals(&matrix, &identity) &&
            cna_effect_matrices_get_view(effect, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equals(&matrix, &identity) &&
            cna_effect_matrices_get_projection(effect, &matrix) == CNA_RESULT_SUCCESS &&
            matrix_equals(&matrix, &identity));
    REQUIRE(cna_pbr_effect_get_diffuse_color(effect, &vector) == CNA_RESULT_SUCCESS &&
            vector3_equals(vector, 1, 1, 1) &&
            cna_pbr_effect_get_alpha(effect, &scalar) == CNA_RESULT_SUCCESS && scalar == 1 &&
            cna_pbr_effect_get_metallic_factor(effect, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1 &&
            cna_pbr_effect_get_roughness_factor(effect, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1 &&
            cna_pbr_effect_get_emissive_factor(effect, &vector) == CNA_RESULT_SUCCESS &&
            vector3_equals(vector, 0, 0, 0));
    REQUIRE(cna_effect_lights_get_ambient_color(effect, &vector) == CNA_RESULT_SUCCESS &&
            vector3_equals(vector, 0, 0, 0) &&
            cna_effect_lights_get_enabled(effect, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE &&
            cna_effect_lights_set_enabled(effect, CNA_FALSE) == CNA_RESULT_INVALID_STATE &&
            cna_effect_lights_get_directional_light(effect, 0U, &lights[0]) ==
                CNA_RESULT_SUCCESS &&
            cna_directional_light_get_enabled(lights[0], &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE &&
            cna_directional_light_destroy(lights[0]) == CNA_RESULT_SUCCESS);
    lights[0] = CNA_INVALID_HANDLE;
    REQUIRE(cna_effect_fog_get_color(effect, &vector) == CNA_RESULT_SUCCESS &&
            vector3_equals(vector, 0, 0, 0) &&
            cna_effect_fog_get_enabled(effect, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE &&
            cna_effect_fog_get_start(effect, &scalar) == CNA_RESULT_SUCCESS && scalar == 0 &&
            cna_effect_fog_get_end(effect, &scalar) == CNA_RESULT_SUCCESS && scalar == 1);
    for (uint32_t slot = 0U; slot <= CNA_PBR_TEXTURE_OCCLUSION; ++slot) {
        REQUIRE(cna_pbr_effect_get_texture(effect, slot, &boolean, &returned) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_FALSE &&
                returned == CNA_INVALID_HANDLE);
    }

    identity.m41 = 2.0F;
    REQUIRE(cna_effect_matrices_set_world(effect, identity) == CNA_RESULT_SUCCESS);
    identity.m42 = 3.0F;
    REQUIRE(cna_effect_matrices_set_view(effect, identity) == CNA_RESULT_SUCCESS);
    identity.m43 = 4.0F;
    REQUIRE(cna_effect_matrices_set_projection(effect, identity) == CNA_RESULT_SUCCESS &&
            cna_pbr_effect_set_diffuse_color(
                effect, (CNA_Vector3){0.1F, 0.2F, 0.3F}) == CNA_RESULT_SUCCESS &&
            cna_pbr_effect_set_alpha(effect, -0.25F) == CNA_RESULT_SUCCESS &&
            cna_pbr_effect_set_metallic_factor(effect, -1.0F) == CNA_RESULT_SUCCESS &&
            cna_pbr_effect_set_roughness_factor(effect, 2.0F) == CNA_RESULT_SUCCESS &&
            cna_pbr_effect_set_emissive_factor(
                effect, (CNA_Vector3){0.4F, 0.5F, 0.6F}) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_ambient_color(
                effect, (CNA_Vector3){0.7F, 0.8F, 0.9F}) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_enabled(effect, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_color(
                effect, (CNA_Vector3){0.2F, 0.3F, 0.4F}) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_enabled(effect, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_start(effect, 5.0F) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_end(effect, 9.0F) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_lights_enable_default(effect) == CNA_RESULT_SUCCESS);
    for (uint32_t index = 0U; index < 3U; ++index) {
        REQUIRE(cna_effect_lights_get_directional_light(effect, index, &lights[index]) ==
                    CNA_RESULT_SUCCESS &&
                cna_directional_light_get_enabled(lights[index], &boolean) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    }
    REQUIRE(cna_effect_lights_get_directional_light(effect, 3U, &returned) ==
                CNA_RESULT_INVALID_ARGUMENT);
    if (retain_light) {
        state->retained_light = lights[1];
        lights[1] = CNA_INVALID_HANDLE;
    }
    for (uint32_t index = 0U; index < 3U; ++index) {
        if (lights[index] != CNA_INVALID_HANDLE) {
            REQUIRE(cna_directional_light_destroy(lights[index]) == CNA_RESULT_SUCCESS);
        }
    }

    REQUIRE(cna_texture2d_create_cpu_only_rgba8(
                1U, 1U, CNA_SURFACE_FORMAT_COLOR, &pixel, 1U, &cpu_texture) ==
                CNA_RESULT_SUCCESS &&
            cna_pbr_effect_set_texture(effect, CNA_PBR_TEXTURE_BASE_COLOR, cpu_texture) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_texture2d_destroy(cpu_texture) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_texture2d_create(device, &texture_info, &texture) == CNA_RESULT_SUCCESS);
    for (uint32_t slot = 0U; slot <= CNA_PBR_TEXTURE_OCCLUSION; ++slot) {
        REQUIRE(cna_pbr_effect_set_texture(effect, slot, texture) == CNA_RESULT_SUCCESS &&
                cna_pbr_effect_get_texture(effect, slot, &boolean, &returned) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_TRUE && returned == texture);
    }
    REQUIRE(cna_pbr_effect_get_texture(
                effect, CNA_PBR_TEXTURE_OCCLUSION + 1U, &boolean, &returned) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_pbr_effect_set_texture(
                effect, CNA_PBR_TEXTURE_OCCLUSION + 1U, texture) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_effect_clone(effect, &clone) == CNA_RESULT_SUCCESS &&
            type_equals(clone, expected_type));
    REQUIRE(cna_pbr_effect_get_alpha(clone, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == -0.25F &&
            cna_pbr_effect_get_diffuse_color(clone, &vector) == CNA_RESULT_SUCCESS &&
            vector3_equals(vector, 0.1F, 0.2F, 0.3F) &&
            cna_pbr_effect_get_metallic_factor(clone, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == -1.0F &&
            cna_pbr_effect_get_roughness_factor(clone, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 2.0F &&
            cna_pbr_effect_get_emissive_factor(clone, &vector) == CNA_RESULT_SUCCESS &&
            vector3_equals(vector, 0.4F, 0.5F, 0.6F) &&
            cna_effect_matrices_get_projection(clone, &matrix) == CNA_RESULT_SUCCESS &&
            matrix.m41 == 2.0F && matrix.m42 == 3.0F && matrix.m43 == 4.0F &&
            cna_effect_fog_get_enabled(clone, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE &&
            cna_effect_fog_get_end(clone, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 9.0F &&
            cna_effect_lights_get_directional_light(clone, 0U, &lights[0]) ==
                CNA_RESULT_SUCCESS &&
            cna_directional_light_get_enabled(lights[0], &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE &&
            cna_directional_light_destroy(lights[0]) == CNA_RESULT_SUCCESS);
    lights[0] = CNA_INVALID_HANDLE;
    for (uint32_t slot = 0U; slot <= CNA_PBR_TEXTURE_OCCLUSION; ++slot) {
        REQUIRE(cna_pbr_effect_get_texture(clone, slot, &boolean, &returned) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_TRUE && returned == texture &&
                cna_pbr_effect_set_texture(effect, slot, CNA_INVALID_HANDLE) ==
                    CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE);
    for (uint32_t slot = 0U; slot <= CNA_PBR_TEXTURE_OCCLUSION; ++slot) {
        REQUIRE(cna_pbr_effect_set_texture(clone, slot, CNA_INVALID_HANDLE) ==
                    CNA_RESULT_SUCCESS);
    }
    REQUIRE(cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS &&
            cna_effect_apply(effect) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_skinned_pbr(const CNA_EffectHandle effect)
{
    CNA_Matrix identity = {0};
    CNA_Matrix bones[CNA_SKINNED_PBR_EFFECT_MAX_BONES];
    CNA_Matrix copied[2];
    CNA_Matrix sentinel = {0};
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    int32_t weights = 0;
    uint64_t count = 0U;

    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS &&
            cna_skinned_pbr_effect_get_weights_per_vertex(effect, &weights) ==
                CNA_RESULT_SUCCESS && weights == 4 &&
            cna_skinned_pbr_effect_copy_bone_transforms(
                effect, CNA_SKINNED_PBR_EFFECT_MAX_BONES, bones,
                CNA_SKINNED_PBR_EFFECT_MAX_BONES, &count) == CNA_RESULT_SUCCESS &&
            count == CNA_SKINNED_PBR_EFFECT_MAX_BONES);
    for (uint32_t index = 0U; index < CNA_SKINNED_PBR_EFFECT_MAX_BONES; ++index) {
        REQUIRE(matrix_equals(&bones[index], &identity));
    }
    REQUIRE(cna_skinned_pbr_effect_set_weights_per_vertex(effect, 1) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_pbr_effect_set_weights_per_vertex(effect, 2) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_pbr_effect_set_weights_per_vertex(effect, 4) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_pbr_effect_set_weights_per_vertex(effect, 3) ==
                CNA_RESULT_INVALID_ARGUMENT);
    bones[0].m41 = 8.0F;
    bones[1].m42 = 9.0F;
    REQUIRE(cna_skinned_pbr_effect_set_bone_transforms(effect, bones, 2U) ==
                CNA_RESULT_SUCCESS &&
            cna_skinned_pbr_effect_copy_bone_transforms(
                effect, 2U, copied, 2U, &count) == CNA_RESULT_SUCCESS &&
            matrix_equals(&bones[0], &copied[0]) && matrix_equals(&bones[1], &copied[1]));
    sentinel.m11 = 123.0F;
    copied[0] = sentinel;
    REQUIRE(cna_skinned_pbr_effect_copy_bone_transforms(
                effect, 2U, copied, 1U, &count) == CNA_RESULT_BUFFER_TOO_SMALL &&
            count == 2U && matrix_equals(&copied[0], &sentinel) &&
            cna_skinned_pbr_effect_set_bone_transforms(effect, 0, 0U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_pbr_effect_set_bone_transforms(
                effect, bones, CNA_SKINNED_PBR_EFFECT_MAX_BONES + 1U) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_pbr_effect_copy_bone_transforms(
                effect, 0U, copied, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_skinned_pbr_effect_copy_bone_transforms(
                effect, CNA_SKINNED_PBR_EFFECT_MAX_BONES + 1U,
                copied, 2U, &count) == CNA_RESULT_INVALID_ARGUMENT &&
            cna_effect_clone(effect, &clone) == CNA_RESULT_SUCCESS &&
            cna_skinned_pbr_effect_copy_bone_transforms(
                clone, 2U, copied, 2U, &count) == CNA_RESULT_SUCCESS &&
            matrix_equals(&bones[0], &copied[0]) && matrix_equals(&bones[1], &copied[1]) &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
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
    CNA_EffectHandle color_matrix = CNA_INVALID_HANDLE;
    CNA_EffectHandle pbr = CNA_INVALID_HANDLE;
    CNA_EffectHandle skinned = CNA_INVALID_HANDLE;
    CNA_Vector3 vector = {0, 0, 0};
    (void)out_error;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (cna_color_matrix_effect_create(device, &color_matrix) != CNA_RESULT_SUCCESS ||
        cna_pbr_effect_create(device, &pbr) != CNA_RESULT_SUCCESS ||
        cna_skinned_pbr_effect_create(device, &skinned) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    if (!validate_color_matrix(color_matrix) ||
        cna_pbr_effect_get_diffuse_color(color_matrix, &vector) !=
            CNA_RESULT_INVALID_HANDLE ||
        !exercise_common_pbr(
            device, pbr, "Microsoft.Xna.Framework.Graphics.PbrEffect", state, 0) ||
        !exercise_common_pbr(
            device, skinned, "Microsoft.Xna.Framework.Graphics.SkinnedPbrEffect", state, 1) ||
        !validate_skinned_pbr(skinned)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 3;
    WrongThreadState wrong_thread = {pbr, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 4;
    if (cna_effect_destroy(color_matrix) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(pbr) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(skinned) != CNA_RESULT_SUCCESS ||
        cna_pbr_effect_get_alpha(pbr, &(float){0}) != CNA_RESULT_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 5;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API PBR effects";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Vector3 value = {0, 0, 0};
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 5 ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_directional_light_set_diffuse_color(
            state.retained_light, (CNA_Vector3){1, 2, 3}) != CNA_RESULT_SUCCESS ||
        cna_directional_light_get_diffuse_color(state.retained_light, &value) !=
            CNA_RESULT_SUCCESS || !vector3_equals(value, 1, 2, 3) ||
        cna_directional_light_destroy(state.retained_light) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "PbrEffectSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
