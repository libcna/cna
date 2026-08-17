// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_ModelMeshHandle) == 8U,
               "CNA model-mesh handle size changed");
_Static_assert(sizeof(CNA_ModelMeshCollectionHandle) == 8U,
               "CNA model-mesh collection handle size changed");
_Static_assert(sizeof(CNA_ModelEffectCollectionHandle) == 8U,
               "CNA model-effect collection handle size changed");
_Static_assert(sizeof(CNA_ModelMeshTag) == 8U,
               "CNA model-mesh tag size changed");

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "ModelMeshSmoke failure at line %d: %s\n", \
                __LINE__, #condition); \
        return 0; \
    } \
} while (0)

typedef struct CallbackState {
    CNA_Handle device;
    int stage;
} CallbackState;

typedef struct WrongThreadState {
    CNA_ModelMeshHandle mesh;
    CNA_Result result;
} WrongThreadState;

static CNA_StringView string_view(const char* const value)
{
    const CNA_StringView result = {value, (uint64_t)strlen(value)};
    return result;
}

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_ModelMeshTag tag = 0U;
    state->result = cna_model_mesh_get_tag(state->mesh, &tag);
    return 0;
}

static int name_equals(const CNA_ModelMeshHandle mesh, const char* const expected)
{
    const uint64_t expected_size = (uint64_t)strlen(expected);
    uint64_t count = UINT64_MAX;
    char bytes[64];
    return expected_size <= sizeof(bytes) &&
           cna_model_mesh_get_name_byte_count(mesh, &count) == CNA_RESULT_SUCCESS &&
           count == expected_size &&
           cna_model_mesh_copy_name(mesh, bytes, sizeof(bytes), &count) ==
               CNA_RESULT_SUCCESS &&
           count == expected_size && memcmp(bytes, expected, (size_t)count) == 0;
}

static int validate_meshes(const CNA_Handle device)
{
    static const char MeshName[] = "MÅÃ­Å¾";
    CNA_ModelMeshPartHandle part0 = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle part1 = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle part_alias = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartHandle parts[2];
    CNA_ModelMeshHandle empty_mesh = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle mesh = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle mesh_alias = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle found_mesh = CNA_INVALID_HANDLE;
    CNA_ModelMeshHandle invalid_mesh = UINT64_MAX;
    CNA_ModelMeshCollectionHandle meshes = CNA_INVALID_HANDLE;
    CNA_ModelMeshPartCollectionHandle mesh_parts = CNA_INVALID_HANDLE;
    CNA_ModelEffectCollectionHandle effects = CNA_INVALID_HANDLE;
    CNA_EffectHandle effect0 = CNA_INVALID_HANDLE;
    CNA_EffectHandle effect1 = CNA_INVALID_HANDLE;
    CNA_EffectHandle returned_effect = UINT64_MAX;
    CNA_ModelBoneHandle bone = CNA_INVALID_HANDLE;
    CNA_ModelBoneHandle parent = CNA_INVALID_HANDLE;
    CNA_BoundingSphere sphere = {{0.0F, 0.0F, 0.0F}, 0.0F};
    CNA_BoundingSphere returned_sphere = {{0.0F, 0.0F, 0.0F}, 0.0F};
    CNA_RendererInfo renderer = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U};
    CNA_Bool boolean = CNA_TRUE;
    CNA_ModelMeshTag tag = UINT64_MAX;
    uint64_t count = UINT64_MAX;
    char short_name[2] = {'x', 'y'};
    const char short_sentinel[2] = {'x', 'y'};
    int32_t scalar = 0;

    REQUIRE(cna_model_mesh_part_create_default(&part0) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_create_default(&part1) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_create(device, 0, 0U, &empty_mesh) == CNA_RESULT_SUCCESS &&
            name_equals(empty_mesh, "") &&
            cna_model_mesh_get_bounding_sphere(empty_mesh, &returned_sphere) ==
                CNA_RESULT_SUCCESS && returned_sphere.center.x == 0.0F &&
            returned_sphere.center.y == 0.0F && returned_sphere.center.z == 0.0F &&
            returned_sphere.radius == 0.0F &&
            cna_model_mesh_get_parent_bone(empty_mesh, &boolean, &parent) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE &&
            parent == CNA_INVALID_HANDLE &&
            cna_model_mesh_get_tag(empty_mesh, &tag) == CNA_RESULT_SUCCESS && tag == 0U);

    parts[0] = part0;
    parts[1] = part1;
    REQUIRE(cna_model_mesh_create_named(
                device, string_view(MeshName), parts, 2U, &mesh) ==
                CNA_RESULT_SUCCESS && name_equals(mesh, MeshName) &&
            cna_model_mesh_copy_name(mesh, short_name, sizeof(short_name), &count) ==
                CNA_RESULT_BUFFER_TOO_SMALL && count == sizeof(MeshName) - 1U &&
            memcmp(short_name, short_sentinel, sizeof(short_name)) == 0 &&
            cna_model_mesh_set_tag(mesh, UINT64_MAX) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_get_tag(mesh, &tag) == CNA_RESULT_SUCCESS && tag == UINT64_MAX);
    sphere = (CNA_BoundingSphere){{1.0F, -2.0F, 3.0F}, -4.0F};
    REQUIRE(cna_model_mesh_set_bounding_sphere(mesh, sphere) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_get_bounding_sphere(mesh, &returned_sphere) ==
                CNA_RESULT_SUCCESS &&
            memcmp(&sphere, &returned_sphere, sizeof(sphere)) == 0);

    REQUIRE(cna_model_mesh_get_mesh_parts(mesh, &mesh_parts) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_get_count(mesh_parts, &count) ==
                CNA_RESULT_SUCCESS && count == 2U &&
            cna_model_mesh_part_collection_get_at(mesh_parts, 0U, &part_alias) ==
                CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_num_vertices(part_alias, 17) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_num_vertices(part0, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == 17 &&
            cna_model_mesh_get_effects(mesh, &effects) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 0U);

    REQUIRE(cna_basic_effect_create(device, &effect0) == CNA_RESULT_SUCCESS &&
            cna_basic_effect_create(device, &effect1) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_effect(part0, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 1U &&
            cna_model_mesh_part_set_effect(part1, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 1U &&
            cna_model_mesh_part_set_effect(part0, effect1) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 2U &&
            cna_model_mesh_part_set_effect(part1, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 1U &&
            cna_model_mesh_part_set_effect(part0, CNA_INVALID_HANDLE) ==
                CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 0U &&
            cna_effect_destroy(effect1) == CNA_RESULT_SUCCESS);

    REQUIRE(cna_model_effect_collection_add(effects, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_add(effects, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 2U &&
            cna_model_effect_collection_contains(effects, effect0, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_model_effect_collection_get_at(effects, 1U, &returned_effect) ==
                CNA_RESULT_SUCCESS && returned_effect == effect0 &&
            cna_model_effect_collection_remove(effects, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_get_count(effects, &count) ==
                CNA_RESULT_SUCCESS && count == 1U &&
            cna_effect_destroy(effect0) == CNA_RESULT_INVALID_STATE &&
            cna_model_effect_collection_remove(effects, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_remove(effects, effect0) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_contains(effects, effect0, &boolean) ==
                CNA_RESULT_SUCCESS && boolean == CNA_FALSE &&
            cna_model_mesh_part_set_effect(part0, effect0) == CNA_RESULT_SUCCESS &&
            cna_effect_dispose(effect0) == CNA_RESULT_INVALID_STATE);

    REQUIRE(cna_model_bone_create(7, string_view("Parent"), &bone) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_set_parent_bone(mesh, bone) == CNA_RESULT_SUCCESS &&
            cna_model_bone_destroy(bone) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_get_parent_bone(mesh, &boolean, &parent) ==
                CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
            cna_model_bone_get_index(parent, &scalar) == CNA_RESULT_SUCCESS && scalar == 7 &&
            cna_model_bone_destroy(parent) == CNA_RESULT_SUCCESS);

    parts[0] = part0;
    REQUIRE(cna_model_mesh_create_named(
                device, string_view("duplicate"), parts, 1U, &invalid_mesh) ==
                CNA_RESULT_INVALID_STATE && invalid_mesh == CNA_INVALID_HANDLE &&
            cna_model_mesh_create_named(
                device, (CNA_StringView){"x\0y", 3U}, 0, 0U, &invalid_mesh) ==
                CNA_RESULT_ENCODING && invalid_mesh == CNA_INVALID_HANDLE &&
            cna_model_mesh_create(device, 0, 1U, &invalid_mesh) ==
                CNA_RESULT_INVALID_ARGUMENT && invalid_mesh == CNA_INVALID_HANDLE);

    {
        CNA_ModelMeshHandle values[2] = {empty_mesh, mesh};
        REQUIRE(cna_model_mesh_collection_create(values, 2U, &meshes) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_collection_get_count(meshes, &count) ==
                    CNA_RESULT_SUCCESS && count == 2U &&
                cna_model_mesh_collection_get_at(meshes, 1U, &mesh_alias) ==
                    CNA_RESULT_SUCCESS &&
                cna_model_mesh_collection_find(
                    meshes, string_view(MeshName), &boolean, &found_mesh) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
                cna_model_mesh_collection_contains(meshes, found_mesh, &boolean) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_TRUE &&
                cna_model_mesh_collection_find(
                    meshes, string_view("missing"), &boolean, &invalid_mesh) ==
                    CNA_RESULT_SUCCESS && boolean == CNA_FALSE &&
                invalid_mesh == CNA_INVALID_HANDLE);
    }

    WrongThreadState wrong_thread = {mesh, CNA_RESULT_SUCCESS};
    thrd_t thread;
    REQUIRE(thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) == thrd_success &&
            thrd_join(thread, 0) == thrd_success &&
            wrong_thread.result == CNA_RESULT_THREAD &&
            cna_graphics_device_get_renderer_info(device, &renderer) == CNA_RESULT_SUCCESS);
    if (renderer.renderer_type == CNA_GRAPHICS_RENDERER_SDL_RENDERER) {
        REQUIRE(cna_model_mesh_draw(empty_mesh) == CNA_RESULT_NOT_SUPPORTED);
    } else {
        REQUIRE(cna_model_mesh_draw(empty_mesh) == CNA_RESULT_SUCCESS);
    }

    REQUIRE(cna_model_mesh_destroy(mesh) == CNA_RESULT_SUCCESS &&
            name_equals(mesh_alias, MeshName) && name_equals(found_mesh, MeshName) &&
            cna_model_mesh_destroy(mesh_alias) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_destroy(found_mesh) == CNA_RESULT_SUCCESS &&
            cna_model_effect_collection_destroy(effects) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_collection_destroy(mesh_parts) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_collection_destroy(meshes) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_set_vertex_offset(part0, -123) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_get_vertex_offset(part0, &scalar) == CNA_RESULT_SUCCESS &&
            scalar == -123 &&
            cna_effect_destroy(effect0) == CNA_RESULT_INVALID_STATE &&
            cna_model_mesh_part_set_effect(part0, CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS &&
            cna_effect_destroy(effect0) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_destroy(empty_mesh) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(part_alias) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(part0) == CNA_RESULT_SUCCESS &&
            cna_model_mesh_part_destroy(part1) == CNA_RESULT_SUCCESS);
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
        cna_game_get_graphics_device(game, &state->device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    if (!validate_meshes(state->device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    CallbackState state = {CNA_INVALID_HANDLE, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API ModelMesh";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.stage != 2 ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        fprintf(stderr, "ModelMeshSmoke lifecycle failure at stage %d\n", state.stage);
        return 1;
    }
    return 0;
}
