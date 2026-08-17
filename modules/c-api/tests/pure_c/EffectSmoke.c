// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_EffectHandle) == 8U, "CNA Effect handle size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "EffectSmoke failure at line %d: %s\n", __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_Handle borrowed_device;
    CNA_EffectPassHandle retained_pass;
    CNA_Handle retained_cube;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_EffectHandle effect;
    CNA_Result result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static int expect_effect_string(
    const CNA_EffectHandle effect,
    const char* const expected,
    CNA_Result (*count_function)(CNA_EffectHandle, uint64_t*),
    CNA_Result (*copy_function)(CNA_EffectHandle, char*, uint64_t, uint64_t*))
{
    const uint64_t expected_count = (uint64_t)strlen(expected);
    uint64_t count = UINT64_MAX;
    char bytes[128];
    char sentinel[128];
    memset(bytes, 0x5a, sizeof(bytes));
    memcpy(sentinel, bytes, sizeof(bytes));
    if (count_function(effect, &count) != CNA_RESULT_SUCCESS ||
        count != expected_count ||
        copy_function(effect, bytes, expected_count == 0U ? 0U : expected_count - 1U,
                      &count) !=
            (expected_count == 0U ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL) ||
        count != expected_count ||
        (expected_count != 0U && memcmp(bytes, sentinel, sizeof(bytes)) != 0) ||
        copy_function(effect, bytes, expected_count, &count) != CNA_RESULT_SUCCESS ||
        count != expected_count || memcmp(bytes, expected, (size_t)expected_count) != 0) {
        return 0;
    }
    return 1;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint64_t count = 0U;
    state->result = cna_effect_get_type_name_byte_count(state->effect, &count);
    return 0;
}

static int validate_base_effect(const CNA_Handle device)
{
    CNA_EffectHandle effect = CNA_INVALID_HANDLE;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_EffectHandle material = CNA_INVALID_HANDLE;
    CNA_EffectHandle material_clone = CNA_INVALID_HANDLE;
    CNA_EffectHandle compiled = UINT64_MAX;
    CNA_EffectParameterCollectionHandle parameters = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueCollectionHandle techniques = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle current = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle indexed = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle foreign = CNA_INVALID_HANDLE;
    CNA_EffectPassCollectionHandle passes = CNA_INVALID_HANDLE;
    CNA_EffectPassHandle pass = CNA_INVALID_HANDLE;
    CNA_Handle owner = CNA_INVALID_HANDLE;
    uint64_t count = UINT64_MAX;
    uint64_t current_identity = 0U;
    uint64_t indexed_identity = 0U;
    CNA_Bool disposed = CNA_TRUE;

    REQUIRE(cna_effect_create_empty(device, &effect) == CNA_RESULT_SUCCESS &&
            effect != CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_create_compiled(device, 0, 0U, &compiled) ==
                CNA_RESULT_NOT_SUPPORTED && compiled == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_get_graphics_device(effect, &owner) == CNA_RESULT_SUCCESS &&
            owner == device);
    REQUIRE(expect_effect_string(
        effect, "Microsoft.Xna.Framework.Graphics.Effect",
        cna_effect_get_type_name_byte_count, cna_effect_copy_type_name));
    REQUIRE(expect_effect_string(
        effect, "", cna_effect_get_vertex_source_byte_count,
        cna_effect_copy_vertex_source));
    REQUIRE(expect_effect_string(
        effect, "", cna_effect_get_fragment_source_byte_count,
        cna_effect_copy_fragment_source));

    REQUIRE(cna_effect_get_parameters(effect, &parameters) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_get_count(parameters, &count) ==
                CNA_RESULT_SUCCESS && count == 0U);
    REQUIRE(cna_effect_get_techniques(effect, &techniques) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_collection_get_count(techniques, &count) ==
                CNA_RESULT_SUCCESS && count == 1U);
    REQUIRE(cna_effect_get_current_technique(effect, &current) == CNA_RESULT_SUCCESS &&
            current != CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_technique_collection_get_at(techniques, 0U, &indexed) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_get_identity(current, &current_identity) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_technique_get_identity(indexed, &indexed_identity) ==
                CNA_RESULT_SUCCESS && current_identity == indexed_identity);
    REQUIRE(cna_effect_technique_get_passes(current, &passes) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_get_at(passes, 0U, &pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_apply(effect) == CNA_RESULT_SUCCESS &&
            cna_effect_pass_apply(pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_set_current_technique(effect, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_pass_apply(pass) == CNA_RESULT_INVALID_STATE);
    CNA_EffectTechniqueHandle absent = UINT64_MAX;
    REQUIRE(cna_effect_get_current_technique(effect, &absent) == CNA_RESULT_SUCCESS &&
            absent == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_set_current_technique(effect, indexed) == CNA_RESULT_SUCCESS &&
            cna_effect_pass_apply(pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_create_named(string_view("Foreign"), &foreign) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_set_current_technique(effect, foreign) ==
                CNA_RESULT_INVALID_ARGUMENT);

    WrongThreadState wrong_thread = {effect, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD);

    REQUIRE(cna_effect_clone(effect, &clone) == CNA_RESULT_SUCCESS &&
            expect_effect_string(
                clone, "Microsoft.Xna.Framework.Graphics.Effect",
                cna_effect_get_type_name_byte_count, cna_effect_copy_type_name));
    REQUIRE(cna_effect_material_create(effect, &material) == CNA_RESULT_SUCCESS &&
            expect_effect_string(
                material, "Microsoft.Xna.Framework.Graphics.EffectMaterial",
                cna_effect_get_type_name_byte_count, cna_effect_copy_type_name));
    REQUIRE(cna_effect_clone(material, &material_clone) == CNA_RESULT_SUCCESS &&
            cna_effect_apply(material_clone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_dispose(clone) == CNA_RESULT_SUCCESS &&
            cna_graphics_resource_get_is_disposed(clone, &disposed) == CNA_RESULT_SUCCESS &&
            disposed == CNA_TRUE && cna_effect_apply(clone) == CNA_RESULT_INVALID_STATE);

    REQUIRE(cna_effect_technique_destroy(foreign) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_destroy(pass) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_pass_collection_destroy(passes) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(current) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_destroy(indexed) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_technique_collection_destroy(techniques) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_parameter_collection_destroy(parameters) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_destroy(material_clone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_destroy(material) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_destroy(effect) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_apply(effect) == CNA_RESULT_INVALID_HANDLE);
    return 1;
}

static int validate_sprite_effect(const CNA_Handle device)
{
    CNA_EffectHandle sprite = CNA_INVALID_HANDLE;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_Bool exact = CNA_FALSE;
    REQUIRE(cna_sprite_effect_create(device, &sprite) == CNA_RESULT_SUCCESS);
    REQUIRE(expect_effect_string(
        sprite, "Microsoft.Xna.Framework.Graphics.SpriteEffect",
        cna_effect_get_type_name_byte_count, cna_effect_copy_type_name));
    REQUIRE(cna_effect_is_exact_stock_sprite_effect(sprite, &exact) ==
                CNA_RESULT_SUCCESS && exact == CNA_TRUE);
    REQUIRE(cna_effect_apply(sprite) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_clone(sprite, &clone) == CNA_RESULT_SUCCESS &&
            cna_effect_is_exact_stock_sprite_effect(clone, &exact) ==
                CNA_RESULT_SUCCESS && exact == CNA_TRUE);
    REQUIRE(cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_destroy(sprite) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_shader_effect(const CNA_Handle device)
{
    static const char VertexSource[] = "void main() { }";
    static const char FragmentSource[] = "void main() { }";
    CNA_EffectHandle shader = CNA_INVALID_HANDLE;
    CNA_EffectHandle clone = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueCollectionHandle techniques = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle technique = CNA_INVALID_HANDLE;
    CNA_EffectPassCollectionHandle passes = CNA_INVALID_HANDLE;
    CNA_EffectPassHandle pass = CNA_INVALID_HANDLE;
    CNA_Handle texture2d = CNA_INVALID_HANDLE;
    CNA_Handle cube = CNA_INVALID_HANDLE;
    CNA_Handle texture3d = UINT64_MAX;
    CNA_Texture2DCreateInfo texture_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR};
    CNA_TextureCubeCreateInfo cube_info = {
        sizeof(CNA_TextureCubeCreateInfo), UINT32_C(1), 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, 0U};
    CNA_Texture3DCreateInfo volume_info = {
        sizeof(CNA_Texture3DCreateInfo), UINT32_C(1), 1U, 1U, 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, 0U};
    CNA_Matrix identity = {0};
    CNA_Matrix value = {0};
    CNA_Bool is_valid = CNA_FALSE;
    CNA_Bool shader_has_renderer = CNA_FALSE;
    CNA_Bool base_has_renderer = CNA_FALSE;
    const float floats[3] = {1.0F, 2.0F, 3.0F};
    const CNA_Vector2 vectors[2] = {{1.0F, 2.0F}, {3.0F, 4.0F}};

    REQUIRE(cna_matrix_get_identity(&identity) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_shader_effect_create(
                device, string_view(VertexSource), string_view(FragmentSource), &shader) ==
            CNA_RESULT_SUCCESS);
    REQUIRE(expect_effect_string(
        shader, "CNA.ShaderEffect", cna_effect_get_type_name_byte_count,
        cna_effect_copy_type_name));
    REQUIRE(expect_effect_string(
        shader, VertexSource, cna_effect_get_vertex_source_byte_count,
        cna_effect_copy_vertex_source));
    REQUIRE(expect_effect_string(
        shader, FragmentSource, cna_effect_get_fragment_source_byte_count,
        cna_effect_copy_fragment_source));
    REQUIRE(cna_shader_effect_is_valid(shader, &is_valid) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_has_renderer(shader, &shader_has_renderer) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_has_renderer(shader, &base_has_renderer) == CNA_RESULT_SUCCESS &&
            shader_has_renderer == base_has_renderer &&
            (is_valid == CNA_FALSE || shader_has_renderer == CNA_TRUE));

    REQUIRE(cna_shader_effect_get_world(shader, &value) == CNA_RESULT_SUCCESS &&
            memcmp(&value, &identity, sizeof(value)) == 0);
    value.m41 = 7.0F;
    REQUIRE(cna_shader_effect_set_world(shader, value) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_get_world(shader, &value) == CNA_RESULT_SUCCESS &&
            value.m41 == 7.0F);
    value = identity;
    value.m42 = 8.0F;
    REQUIRE(cna_shader_effect_set_view(shader, value) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_get_view(shader, &value) == CNA_RESULT_SUCCESS &&
            value.m42 == 8.0F);
    value = identity;
    value.m43 = 9.0F;
    REQUIRE(cna_shader_effect_set_projection(shader, value) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_get_projection(shader, &value) == CNA_RESULT_SUCCESS &&
            value.m43 == 9.0F);

    REQUIRE(cna_shader_effect_set_uniform_matrix(
                shader, string_view("Matrix"), identity) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_vector4(
                shader, string_view("V4"), (CNA_Vector4){1, 2, 3, 4}) ==
                CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_vector3(
                shader, string_view("V3"), (CNA_Vector3){1, 2, 3}) ==
                CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_vector2(
                shader, string_view("V2"), (CNA_Vector2){1, 2}) ==
                CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_float(shader, string_view("F"), 5.0F) ==
                CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_int32(shader, string_view("I"), 6) ==
                CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_float_array(
                shader, string_view("FA"), floats, 3U) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_float_array(
                shader, string_view("Empty"), 0, 0U) == CNA_RESULT_SUCCESS &&
            cna_shader_effect_set_uniform_vector2_array(
                shader, string_view("V2A"), vectors, 2U) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_texture2d_create(device, &texture_info, &texture2d) == CNA_RESULT_SUCCESS &&
            cna_texturecube_create(device, &cube_info, &cube) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_shader_effect_set_texture2d(shader, 1, texture2d) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture2d) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_shader_effect_set_texture_cube(shader, 1, cube) == CNA_RESULT_SUCCESS &&
            cna_texture2d_destroy(texture2d) == CNA_RESULT_SUCCESS &&
            cna_texturecube_destroy(cube) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_texture3d_create(device, &volume_info, &texture3d) ==
                CNA_RESULT_NOT_SUPPORTED && texture3d == CNA_INVALID_HANDLE);
    REQUIRE(cna_shader_effect_set_texture3d(shader, 2, cube) ==
            CNA_RESULT_INVALID_HANDLE);

    REQUIRE(cna_effect_clone(shader, &clone) == CNA_RESULT_SUCCESS &&
            expect_effect_string(
                clone, "CNA.ShaderEffect", cna_effect_get_type_name_byte_count,
                cna_effect_copy_type_name));
    REQUIRE(cna_effect_destroy(clone) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_get_techniques(shader, &techniques) == CNA_RESULT_SUCCESS &&
            cna_effect_technique_collection_get_at(techniques, 0U, &technique) ==
                CNA_RESULT_SUCCESS &&
            cna_effect_technique_get_passes(technique, &passes) == CNA_RESULT_SUCCESS &&
            cna_effect_pass_collection_get_at(passes, 0U, &pass) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_effect_destroy(shader) == CNA_RESULT_SUCCESS &&
            cna_effect_pass_apply(pass) == CNA_RESULT_SUCCESS &&
            cna_texturecube_destroy(cube) == CNA_RESULT_INVALID_STATE);
    REQUIRE(cna_effect_technique_collection_destroy(techniques) == CNA_RESULT_SUCCESS &&
            cna_effect_technique_destroy(technique) == CNA_RESULT_SUCCESS &&
            cna_effect_pass_collection_destroy(passes) == CNA_RESULT_SUCCESS &&
            cna_effect_pass_destroy(pass) == CNA_RESULT_SUCCESS &&
            cna_texturecube_destroy(cube) == CNA_RESULT_SUCCESS);
    return 1;
}

static int validate_failures(const CNA_Handle device)
{
    CNA_EffectHandle effect = UINT64_MAX;
    CNA_EffectTechniqueHandle technique = UINT64_MAX;
    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    const unsigned char bad_utf8[] = {0xffU};
    CNA_Bool boolean = CNA_FALSE;
    REQUIRE(cna_effect_create_empty(device, 0) == CNA_RESULT_INVALID_ARGUMENT);
    REQUIRE(cna_shader_effect_create(
                device, (CNA_StringView){(const char*)bad_utf8, 1U},
                string_view("x"), &effect) == CNA_RESULT_ENCODING &&
            effect == CNA_INVALID_HANDLE);
    REQUIRE(cna_curve_create(&wrong_kind) == CNA_RESULT_SUCCESS);
    REQUIRE(cna_effect_clone(wrong_kind, &effect) == CNA_RESULT_INVALID_HANDLE &&
            effect == CNA_INVALID_HANDLE);
    REQUIRE(cna_effect_get_current_technique(wrong_kind, &technique) ==
                CNA_RESULT_INVALID_HANDLE && technique == CNA_INVALID_HANDLE);
    REQUIRE(cna_shader_effect_is_valid(wrong_kind, &boolean) ==
            CNA_RESULT_INVALID_HANDLE);
    REQUIRE(cna_curve_destroy(wrong_kind) == CNA_RESULT_SUCCESS);
    return 1;
}

static int create_retained_descendant(
    const CNA_Handle device,
    CallbackState* const state)
{
    CNA_EffectHandle shader = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueCollectionHandle techniques = CNA_INVALID_HANDLE;
    CNA_EffectTechniqueHandle technique = CNA_INVALID_HANDLE;
    CNA_EffectPassCollectionHandle passes = CNA_INVALID_HANDLE;
    const CNA_TextureCubeCreateInfo cube_info = {
        sizeof(CNA_TextureCubeCreateInfo), UINT32_C(1), 1U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, 0U};
    if (cna_shader_effect_create(
            device, string_view("void main() { }"), string_view("void main() { }"),
            &shader) != CNA_RESULT_SUCCESS ||
        cna_texturecube_create(device, &cube_info, &state->retained_cube) !=
            CNA_RESULT_SUCCESS ||
        cna_shader_effect_set_texture_cube(shader, 1, state->retained_cube) !=
            CNA_RESULT_SUCCESS ||
        cna_effect_get_techniques(shader, &techniques) != CNA_RESULT_SUCCESS ||
        cna_effect_technique_collection_get_at(techniques, 0U, &technique) !=
            CNA_RESULT_SUCCESS ||
        cna_effect_technique_get_passes(technique, &passes) != CNA_RESULT_SUCCESS ||
        cna_effect_pass_collection_get_at(passes, 0U, &state->retained_pass) !=
            CNA_RESULT_SUCCESS ||
        cna_effect_technique_collection_destroy(techniques) != CNA_RESULT_SUCCESS ||
        cna_effect_technique_destroy(technique) != CNA_RESULT_SUCCESS ||
        cna_effect_pass_collection_destroy(passes) != CNA_RESULT_SUCCESS ||
        cna_effect_destroy(shader) != CNA_RESULT_SUCCESS) {
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
    (void)out_error;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &state->borrowed_device) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (!validate_base_effect(state->borrowed_device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    if (!validate_sprite_effect(state->borrowed_device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 3;
    if (!validate_shader_effect(state->borrowed_device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 4;
    if (!validate_failures(state->borrowed_device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 5;
    if (!create_retained_descendant(state->borrowed_device, state)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 6;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {
        CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API effects";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 6 ||
        state.borrowed_device == CNA_INVALID_HANDLE ||
        state.retained_pass == CNA_INVALID_HANDLE ||
        state.retained_cube == CNA_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_effect_pass_apply(state.retained_pass) != CNA_RESULT_SUCCESS ||
        cna_texturecube_destroy(state.retained_cube) != CNA_RESULT_INVALID_STATE ||
        cna_effect_pass_destroy(state.retained_pass) != CNA_RESULT_SUCCESS ||
        cna_texturecube_destroy(state.retained_cube) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "EffectSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
