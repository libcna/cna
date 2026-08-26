// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_DirectionalLightHandle) == 8U,
               "CNA DirectionalLight handle size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "BasicEffectSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_Handle borrowed_device;
    CNA_DirectionalLightHandle retained_light;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_DirectionalLightHandle light;
    CNA_Result result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static int vector_equals(
    const CNA_Vector3 value,
    const float x,
    const float y,
    const float z)
{
    return value.x == x && value.y == y && value.z == z;
}

static int inspect_light_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_Bool value = CNA_FALSE;
    state->result = cna_directional_light_get_enabled(state->light, &value);
    return 0;
}

static int validate_standalone_light(void)
{
    CNA_DirectionalLightHandle light = CNA_INVALID_HANDLE;
    CNA_Vector3 value = {9.0F, 9.0F, 9.0F};
    CNA_Bool enabled = CNA_TRUE;
    /*
     * A new light carries XNA's own defaults, not FNA's zero-initialized fields. Measured on a
     * live XNA 4.0 build: a freshly constructed BasicEffect, before EnableDefaultLighting() has
     * touched anything, reports DiffuseColor (1,1,1), Direction (0,-1,0) and SpecularColor
     * (0,0,0) on all three of its lights. This test used to pin the zeros, which is what FNA
     * leaves behind and what XNA does not.
     *
     * Enabled is false here because XNA enables only DirectionalLight0, and it does that from
     * BasicEffect rather than from the light: lights 1 and 2 come up disabled, which is the
     * right default for a standalone light with no effect behind it.
     */
    REQUIRE(cna_directional_light_create(&light) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_directional_light_get_diffuse_color(light, &value) ==
                CNA_RESULT_SUCCESS && vector_equals(value, 1, 1, 1));
    REQUIRE(cna_directional_light_get_direction(light, &value) ==
                CNA_RESULT_SUCCESS && vector_equals(value, 0, -1, 0));
    REQUIRE(cna_directional_light_get_specular_color(light, &value) ==
                CNA_RESULT_SUCCESS && vector_equals(value, 0, 0, 0));
    REQUIRE(cna_directional_light_get_enabled(light, &enabled) ==
                CNA_RESULT_SUCCESS && enabled == CNA_FALSE);

    REQUIRE(cna_directional_light_set_diffuse_color(
                light, (CNA_Vector3){1, 2, 3}) == CNA_RESULT_SUCCESS &&
            cna_directional_light_set_direction(
                light, (CNA_Vector3){4, 5, 6}) == CNA_RESULT_SUCCESS &&
            cna_directional_light_set_specular_color(
                light, (CNA_Vector3){7, 8, 9}) == CNA_RESULT_SUCCESS &&
            cna_directional_light_set_enabled(light, CNA_TRUE) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_directional_light_get_diffuse_color(light, &value) ==
                CNA_RESULT_SUCCESS && vector_equals(value, 1, 2, 3));
    REQUIRE(cna_directional_light_get_direction(light, &value) ==
                CNA_RESULT_SUCCESS && vector_equals(value, 4, 5, 6));
    REQUIRE(cna_directional_light_get_specular_color(light, &value) ==
                CNA_RESULT_SUCCESS && vector_equals(value, 7, 8, 9));
    REQUIRE(cna_directional_light_set_enabled(light, UINT32_C(2)) ==
            CNA_RESULT_INVALID_ARGUMENT);

    WrongThreadState wrong_thread = {light, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_light_on_wrong_thread, &wrong_thread) ==
                thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD);
    REQUIRE(cna_directional_light_destroy(light) == CNA_RESULT_SUCCESS &&
            cna_directional_light_get_enabled(light, &enabled) ==
                CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static int validate_defaults(const CNA_EffectHandle basic)
{
    CNA_Matrix identity = {0};
    CNA_Matrix matrix = {0};
    CNA_Vector3 vector = {9, 9, 9};
    CNA_Bool boolean = CNA_TRUE;
    float scalar = -1.0F;
    CNA_Bool has_texture = CNA_TRUE;
    CNA_Handle texture = UINT64_MAX;
    CNA_DirectionalLightHandle light0 = CNA_INVALID_HANDLE;
    CNA_DirectionalLightHandle light1 = CNA_INVALID_HANDLE;
    CNA_DirectionalLightHandle light2 = CNA_INVALID_HANDLE;

    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_matrices_get_world(basic, &matrix) == CNA_RESULT_SUCCESS &&
            memcmp(&matrix, &identity, sizeof(matrix)) == 0);
    REQUIRE(cna_effect_matrices_get_view(basic, &matrix) == CNA_RESULT_SUCCESS &&
            memcmp(&matrix, &identity, sizeof(matrix)) == 0);
    REQUIRE(cna_effect_matrices_get_projection(basic, &matrix) == CNA_RESULT_SUCCESS &&
            memcmp(&matrix, &identity, sizeof(matrix)) == 0);
    REQUIRE(cna_basic_effect_get_vertex_color_enabled(basic, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_basic_effect_get_prefer_per_pixel_lighting(basic, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_effect_lights_get_ambient_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_effect_lights_get_enabled(basic, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE);
    REQUIRE(cna_basic_effect_get_diffuse_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_basic_effect_get_emissive_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_basic_effect_get_specular_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 1, 1, 1));
    REQUIRE(cna_basic_effect_get_specular_power(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 16.0F);
    REQUIRE(cna_basic_effect_get_alpha(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1.0F);
    REQUIRE(cna_basic_effect_get_texture_enabled(basic, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE);
    REQUIRE(cna_basic_effect_get_texture(basic, &has_texture, &texture) ==
                CNA_RESULT_SUCCESS && has_texture == CNA_FALSE &&
            texture == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_fog_get_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0, 0, 0));
    REQUIRE(cna_effect_fog_get_enabled(basic, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE);
    REQUIRE(cna_effect_fog_get_start(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.0F);
    REQUIRE(cna_effect_fog_get_end(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 1.0F);

    REQUIRE(cna_effect_lights_get_directional_light(basic, 0U, &light0) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_directional_light(basic, 1U, &light1) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_directional_light(basic, 2U, &light2) ==
                CNA_RESULT_SUCCESS);
    REQUIRE(cna_directional_light_get_enabled(light0, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE);
    REQUIRE(cna_directional_light_get_enabled(light1, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE);
    REQUIRE(cna_directional_light_get_enabled(light2, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_FALSE);
    REQUIRE(cna_directional_light_destroy(light0) == CNA_RESULT_SUCCESS &&
            cna_directional_light_destroy(light1) == CNA_RESULT_SUCCESS &&
            cna_directional_light_destroy(light2) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_properties(
    const CNA_Handle device,
    const CNA_EffectHandle basic)
{
    CNA_Matrix matrix = {0};
    CNA_Vector3 vector = {0, 0, 0};
    CNA_Bool boolean = CNA_FALSE;
    float scalar = 0.0F;
    CNA_DirectionalLightHandle first = CNA_INVALID_HANDLE;
    CNA_DirectionalLightHandle same = CNA_INVALID_HANDLE;
    uint64_t type_count = 0U;
    char type_name[52];
    static const char TypeName[] = "Microsoft.Xna.Framework.Graphics.BasicEffect";

    REQUIRE(cna_effect_get_type_name_byte_count(basic, &type_count) ==
                CNA_RESULT_SUCCESS && type_count == sizeof(TypeName) - 1U &&
            cna_effect_copy_type_name(
                basic, type_name, sizeof(TypeName) - 1U, &type_count) ==
                CNA_RESULT_SUCCESS &&
            memcmp(type_name, TypeName, sizeof(TypeName) - 1U) == 0);

    REQUIRE(cna_matrix_get_identity(&matrix) == CNA_RESULT_SUCCESS);
    matrix.m41 = 1.0F;
    REQUIRE(cna_effect_matrices_set_world(basic, matrix) == CNA_RESULT_SUCCESS);
    matrix.m42 = 2.0F;
    REQUIRE(cna_effect_matrices_set_view(basic, matrix) == CNA_RESULT_SUCCESS);
    matrix.m43 = 3.0F;
    REQUIRE(cna_effect_matrices_set_projection(basic, matrix) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_matrices_get_projection(basic, &matrix) == CNA_RESULT_SUCCESS &&
            matrix.m41 == 1.0F && matrix.m42 == 2.0F && matrix.m43 == 3.0F);

    REQUIRE(cna_basic_effect_set_vertex_color_enabled(basic, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_prefer_per_pixel_lighting(basic, CNA_TRUE) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_ambient_color(
                basic, (CNA_Vector3){0.1F, 0.2F, 0.3F}) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_set_enabled(basic, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_diffuse_color(
                basic, (CNA_Vector3){0.4F, 0.5F, 0.6F}) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_emissive_color(
                basic, (CNA_Vector3){0.7F, 0.8F, 0.9F}) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_specular_color(
                basic, (CNA_Vector3){0.2F, 0.4F, 0.6F}) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_specular_power(basic, 32.0F) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_alpha(basic, 0.75F) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_texture_enabled(basic, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_color(
                basic, (CNA_Vector3){0.3F, 0.4F, 0.5F}) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_enabled(basic, CNA_TRUE) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_start(basic, 10.0F) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_set_end(basic, 20.0F) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_basic_effect_get_vertex_color_enabled(basic, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_basic_effect_get_prefer_per_pixel_lighting(basic, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_effect_lights_get_ambient_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.1F, 0.2F, 0.3F));
    REQUIRE(cna_effect_lights_get_enabled(basic, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE);
    REQUIRE(cna_basic_effect_get_diffuse_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.4F, 0.5F, 0.6F));
    REQUIRE(cna_basic_effect_get_emissive_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.7F, 0.8F, 0.9F));
    REQUIRE(cna_basic_effect_get_specular_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.2F, 0.4F, 0.6F));
    REQUIRE(cna_basic_effect_get_specular_power(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 32.0F);
    REQUIRE(cna_basic_effect_get_alpha(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 0.75F);
    REQUIRE(cna_basic_effect_get_texture_enabled(basic, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE);
    REQUIRE(cna_effect_fog_get_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.3F, 0.4F, 0.5F));
    REQUIRE(cna_effect_fog_get_enabled(basic, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE);
    REQUIRE(cna_effect_fog_get_start(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 10.0F);
    REQUIRE(cna_effect_fog_get_end(basic, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 20.0F);

    REQUIRE(cna_effect_lights_get_directional_light(basic, 0U, &first) ==
                CNA_RESULT_SUCCESS &&
            cna_directional_light_set_direction(
                first, (CNA_Vector3){-1, -2, -3}) == CNA_RESULT_SUCCESS &&
            cna_effect_lights_get_directional_light(basic, 0U, &same) ==
                CNA_RESULT_SUCCESS &&
            cna_directional_light_get_direction(same, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, -1, -2, -3));
    REQUIRE(cna_effect_lights_enable_default(basic) == CNA_RESULT_SUCCESS &&
            cna_directional_light_get_direction(first, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, -0.5265408F, -0.5735765F, -0.6275069F));
    REQUIRE(cna_effect_lights_get_ambient_color(basic, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.05333332F, 0.09882354F, 0.1819608F));
    REQUIRE(cna_directional_light_get_enabled(first, &boolean) == CNA_RESULT_SUCCESS &&
            boolean == CNA_TRUE);
    REQUIRE(cna_directional_light_destroy(first) == CNA_RESULT_SUCCESS &&
            cna_directional_light_destroy(same) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_apply(basic) == CNA_RESULT_SUCCESS);
    (void)device;
    return 1;
}

static int validate_texture_clone(
    const CNA_Handle device,
    const CNA_EffectHandle basic)
{
    const CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle returned = CNA_INVALID_HANDLE;
    CNA_Bool has_texture = CNA_FALSE;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_Vector3 vector = {0, 0, 0};
    REQUIRE(cna_texture2d_create(device, &texture_info, &texture) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_set_texture(basic, texture) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_get_texture(basic, &has_texture, &returned) ==
                CNA_RESULT_SUCCESS && has_texture == CNA_TRUE && returned == texture &&
            cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_effect_clone(basic, &clone) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_get_texture(clone, &has_texture, &returned) ==
                CNA_RESULT_SUCCESS && has_texture == CNA_TRUE && returned == texture &&
            cna_basic_effect_get_diffuse_color(clone, &vector) == CNA_RESULT_SUCCESS &&
            vector_equals(vector, 0.4F, 0.5F, 0.6F));
    REQUIRE(cna_basic_effect_set_texture(basic, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_basic_effect_set_texture(clone, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_interfaces_and_failures(const CNA_Handle device)
{
    CNA_EffectHandle empty = CNA_INVALID_HANDLE;
    CNA_EffectHandle shader = CNA_INVALID_HANDLE;
    CNA_Matrix matrix = {0};
    CNA_Vector3 vector = {0, 0, 0};
    CNA_DirectionalLightHandle light = UINT64_MAX;
    REQUIRE(cna_effect_create_empty(device, &empty) == CNA_RESULT_SUCCESS &&
            cna_effect_matrices_get_world(empty, &matrix) == CNA_RESULT_NOT_SUPPORTED &&
            cna_effect_fog_get_color(empty, &vector) == CNA_RESULT_NOT_SUPPORTED &&
            cna_effect_lights_get_ambient_color(empty, &vector) ==
                CNA_RESULT_NOT_SUPPORTED &&
            cna_basic_effect_get_diffuse_color(empty, &vector) ==
                CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_shader_effect_create(
                device, string_view("void main() { }"), string_view("void main() { }"),
                &shader) == CNA_RESULT_SUCCESS &&
            cna_effect_matrices_get_world(shader, &matrix) == CNA_RESULT_SUCCESS &&
            cna_effect_fog_get_color(shader, &vector) == CNA_RESULT_NOT_SUPPORTED);
    REQUIRE(cna_effect_lights_get_directional_light(empty, 3U, &light) ==
                CNA_RESULT_INVALID_ARGUMENT && light == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_lights_get_directional_light(empty, 0U, &light) ==
                CNA_RESULT_NOT_SUPPORTED && light == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_fog_set_enabled(empty, UINT32_C(2)) ==
                CNA_RESULT_INVALID_ARGUMENT &&
            cna_basic_effect_set_vertex_color_enabled(empty, UINT32_C(2)) ==
                CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_effect_destroy(shader) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(empty) == CNA_RESULT_SUCCESS);
    return 1;
}

static int create_retained_light(
    const CNA_Handle device,
    CallbackState* const state)
{
    CNA_EffectHandle basic = CNA_INVALID_HANDLE;
    if (cna_basic_effect_create(device, &basic) != CNA_RESULT_SUCCESS ||
        cna_effect_lights_get_directional_light(
            basic, 2U, &state->retained_light) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(basic) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CallbackState* const state = (CallbackState*)context;
    CNA_EffectHandle basic = CNA_INVALID_HANDLE;
    (void)out_error;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &state->borrowed_device) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (!validate_standalone_light()) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    if (cna_basic_effect_create(state->borrowed_device, &basic) != CNA_RESULT_SUCCESS ||
        !validate_defaults(basic)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 3;
    if (!validate_properties(state->borrowed_device, basic)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 4;
    if (!validate_texture_clone(state->borrowed_device, basic)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 5;
    if (cna_effect_destroy(basic) != CNA_RESULT_SUCCESS ||
        !validate_interfaces_and_failures(state->borrowed_device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 6;
    if (!create_retained_light(state->borrowed_device, state)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 7;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API BasicEffect";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Vector3 value = {0, 0, 0};
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 7 ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_directional_light_set_diffuse_color(
            state.retained_light, (CNA_Vector3){1, 2, 3}) != CNA_RESULT_SUCCESS ||
        cna_directional_light_get_diffuse_color(state.retained_light, &value) !=
            CNA_RESULT_SUCCESS || !vector_equals(value, 1, 2, 3) ||
        cna_directional_light_destroy(state.retained_light) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "BasicEffectSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
